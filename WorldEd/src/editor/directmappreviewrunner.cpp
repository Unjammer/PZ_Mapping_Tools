#include "directmappreviewrunner.h"
#include "directmappreview.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <memory>

namespace {

const QByteArray readyMarker("[PZDirectMapPreview] Lua ready");
const QByteArray passedMarker("[PZDirectMapPreview] validation passed");

QByteArray encodeCell(const QByteArray &source)
{
    const QJsonObject cell = QJsonDocument::fromJson(source).object();
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    const auto writeText = [&stream](const QString &text) {
        const QByteArray utf8 = text.toUtf8();
        stream << qint32(utf8.size());
        stream.writeRawData(utf8.constData(), utf8.size());
    };
    const auto writeNumbers = [&stream](const QJsonArray &numbers) {
        for (const QJsonValue &number : numbers)
            stream << qint32(number.toInt());
    };
    stream << qint32(cell.value(QStringLiteral("cellX")).toInt())
           << qint32(cell.value(QStringLiteral("cellY")).toInt())
           << qint32(cell.value(QStringLiteral("minLevel")).toInt())
           << qint32(cell.value(QStringLiteral("maxLevel")).toInt());
    const QJsonArray tiles = cell.value(QStringLiteral("tiles")).toArray();
    stream << qint32(tiles.size());
    for (const QJsonValue &tile : tiles)
        writeText(tile.toString());
    const QJsonArray buildings = cell.value(QStringLiteral("buildings")).toArray();
    stream << qint32(buildings.size());
    for (const QJsonValue &building : buildings) {
        const QJsonArray rooms = building.toObject().value(QStringLiteral("rooms")).toArray();
        stream << qint32(rooms.size());
        for (const QJsonValue &entry : rooms) {
            const QJsonObject room = entry.toObject();
            const QJsonArray rects = room.value(QStringLiteral("rects")).toArray();
            writeText(room.value(QStringLiteral("name")).toString());
            stream << qint32(room.value(QStringLiteral("z")).toInt()) << qint32(rects.size());
            for (const QJsonValue &rect : rects)
                writeNumbers(rect.toArray());
        }
    }
    const QJsonArray squares = cell.value(QStringLiteral("squares")).toArray();
    stream << qint32(squares.size());
    for (const QJsonValue &entry : squares) {
        const QJsonArray square = entry.toArray();
        stream << qint32(square.at(0).toInt()) << qint32(square.at(1).toInt())
               << qint32(square.at(2).toInt()) << qint32(square.size() - 3);
        for (int i = 3; i < square.size(); ++i)
            stream << qint32(square.at(i).toInt());
    }
    return bytes;
}

class PreviewSession final : public QObject
{
public:
    explicit PreviewSession(std::function<void(bool)> finished)
        : QObject(qApp), completion(std::move(finished))
    {
        process.setProcessChannelMode(QProcess::MergedChannels);
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
            readOutput();
        });
        connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus status) {
            readOutput();
            bool mapFilesAbsent = true;
            if (validation) {
                const QStringList mapFiles{QStringLiteral("*.lotheader"), QStringLiteral("*.lotpack"),
                                           QStringLiteral("chunkdata*"), QStringLiteral("*.lot")};
                QDirIterator artifacts(profile.path(), mapFiles, QDir::Files, QDirIterator::Subdirectories);
                mapFilesAbsent = !artifacts.hasNext();
                if (mapFilesAbsent)
                    qInfo() << "Direct map preview generated no LOT or chunkdata files";
                else
                    qWarning().noquote() << "Direct map preview produced an unexpected map file:" << artifacts.next();
            }
            const bool success = status == QProcess::NormalExit && code == 0
                    && ready && mapFilesAbsent && (!validation || validationPassed);
            if (!success)
                qWarning().noquote() << "Direct map preview exited with code" << code;
            if (completion)
                completion(success);
            deleteLater();
        });
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->bytesAvailable() > 128) {
                        socket->disconnectFromHost();
                    } else if (socket->canReadLine()) {
                        if (socket->readLine() == request && !request.isEmpty()) {
                            request.clear();
                            server.close();
                            QByteArray header;
                            QDataStream stream(&header, QIODevice::WriteOnly);
                            stream << quint32(0x505A4450) << quint32(2) << qint32(cells.size());
                            socket->write(header);
                            connect(socket, &QTcpSocket::bytesWritten, this, [this, socket] {
                                sendCells(socket);
                            });
                            sendCells(socket);
                        } else {
                            socket->disconnectFromHost();
                        }
                    }
                });
            }
        });
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [this] {
            qWarning() << "Direct map preview timed out before becoming playable";
            process.kill();
        });
    }

    ~PreviewSession() override
    {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
    }

    bool begin(const QDir &game, const QString &agent, QVector<QByteArray> sourceCells,
               const QJsonArray &spawn, const QString &validationScript, QString &error)
    {
        if (!profile.isValid()) {
            error = profile.errorString();
            return false;
        }
        if (spawn.size() != 3) {
            error = tr("The map sources do not provide a playable starting square.");
            return false;
        }
        QByteArray checks;
        validation = !validationScript.isEmpty();
        if (validation) {
            QFile file(validationScript);
            if (!file.open(QIODevice::ReadOnly)) {
                error = file.errorString();
                return false;
            }
            checks = file.readAll();
        }
        const QByteArray scenario = QStringLiteral(R"lua(
debugScenarios = debugScenarios or {}
local scenario = {
    name = "WorldEd Direct Map Preview",
    forceLaunch = true,
    world = "PZDirectMapPreview;Muldraugh, KY",
    startLoc = { x = %1, y = %2, z = %3 },
    setSandbox = function()
        getWorld():setGameMode(GameMode.SANDBOX)
        getWorld():setLuaPlayerDesc(SurvivorFactory.CreateSurvivor(SurvivorType.Neutral, false))
        getWorld():getLuaTraits():clear()
        ActiveMods.getById("currentGame"):copyFrom(ActiveMods.getById("default"))
        SandboxVars.Zombies = 6
    end
}
debugScenarios.PZDirectMapPreview = scenario

Events.OnGameStart.Add(function()
    if selectedDebugScenario ~= scenario then return end
    scenario.forceLaunch = false
    local player = getPlayer()
    if not player then error("Direct preview has no player") end
    print("[PZDirectMapPreview] Lua ready at " .. player:getX() .. "," .. player:getY() .. "," .. player:getZ())
end)
)lua").arg(spawn.at(0).toInt()).arg(spawn.at(1).toInt()).arg(spawn.at(2).toInt()).toUtf8();
        const QDir cache(profile.path());
        const QString mod = QStringLiteral("mods/PZDirectMapPreview/common/");
        if (!write(cache, QStringLiteral("mods/reset-mods-42_00.txt"), "", error)
                || !write(cache, QStringLiteral("mods/default.txt"),
                          "VERSION = 1,\nmods { mod = PZDirectMapPreview, }\nmaps { }\n", error)
                || !write(cache, QStringLiteral("options.ini"),
                          "version=8\nlanguage=EN\nwidth=1280\nheight=720\nfullScreen=false\n", error)
                || !write(cache, QStringLiteral("debug-options.ini"),
                          "VERSION=1\nDebugScenario.ForceLaunch=true\n", error)
                || !write(cache, mod + QStringLiteral("mod.info"),
                          "name=WorldEd Direct Map Preview\nid=PZDirectMapPreview\n", error)
                || !write(cache, mod + QStringLiteral("media/maps/PZDirectMapPreview/map.info"),
                          "title=WorldEd Direct Map Preview\nfixed2x=true\n", error)
                || !write(cache, mod + QStringLiteral("media/lua/client/PZDirectMapPreview.lua"),
                          scenario + '\n' + checks, error))
            return false;

        if (!server.listen(QHostAddress::LocalHost)) {
            error = server.errorString();
            return false;
        }
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request = "PZDP/1 " + token.toLatin1() + '\n';
        cells = std::move(sourceCells);
        QFile configuration(game.filePath(QStringLiteral("ProjectZomboid64.json")));
        if (!configuration.open(QIODevice::ReadOnly)) {
            error = configuration.errorString();
            return false;
        }
        const QJsonObject launcher = QJsonDocument::fromJson(configuration.readAll()).object();
        QStringList args;
        for (const QJsonValue &argument : launcher.value(QStringLiteral("vmArgs")).toArray()) {
            const QString option = argument.toString();
            if (option.startsWith(QLatin1String("-javaagent:"))
                    || option.startsWith(QLatin1String("-agentlib:"))
                    || option.startsWith(QLatin1String("-agentpath:")))
                continue;
            args << option;
        }
        QStringList classpath;
        for (const QJsonValue &part : launcher.value(QStringLiteral("classpath")).toArray())
            classpath << part.toString();
        if (classpath.isEmpty() || launcher.value(QStringLiteral("mainClass")).toString().isEmpty()) {
            error = tr("The installed game launcher configuration is invalid.");
            return false;
        }
        args << QStringLiteral("-Dnosave=true")
             << QStringLiteral("-javaagent:%1=%2:%3").arg(agent).arg(server.serverPort()).arg(token)
             << QStringLiteral("-cp") << classpath.join(QLatin1Char(';'))
             << launcher.value(QStringLiteral("mainClass")).toString().replace(QLatin1Char('/'), QLatin1Char('.'))
             << QStringLiteral("-debug") << QStringLiteral("-nosteam")
             << QStringLiteral("-cachedir=") + QDir::toNativeSeparators(profile.path())
             << QStringLiteral("-modfolders") << QStringLiteral("mods");
        qInfo().noquote() << "Direct map preview profile:" << profile.path();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.remove(QStringLiteral("_JAVA_OPTIONS"));
        environment.remove(QStringLiteral("JAVA_TOOL_OPTIONS"));
        environment.remove(QStringLiteral("JDK_JAVA_OPTIONS"));
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(game.absolutePath());
        process.start(game.filePath(QStringLiteral("jre64/bin/java.exe")), args);
        if (!process.waitForStarted(5000)) {
            error = process.errorString();
            return false;
        }
        timeout.start(180000);
        return true;
    }

private:
    void sendCells(QTcpSocket *socket)
    {
        while (nextCell < cells.size() && socket->bytesToWrite() < 256 * 1024) {
            const QByteArray encoded = encodeCell(cells.at(nextCell));
            if (socket->write(encoded) != encoded.size()) {
                socket->abort();
                return;
            }
            cells[nextCell].clear();
            ++nextCell;
        }
        if (nextCell == cells.size())
            socket->disconnectFromHost();
    }

    static bool write(const QDir &directory, const QString &name, const QByteArray &data, QString &error)
    {
        const QString path = directory.filePath(name);
        QFile file(path);
        if (!QDir().mkpath(QFileInfo(path).absolutePath())
                || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            error = tr("Could not stage %1.").arg(QDir::toNativeSeparators(path));
            return false;
        }
        return true;
    }

    void readOutput()
    {
        pendingOutput += process.readAllStandardOutput();
        int end;
        while ((end = pendingOutput.indexOf('\n')) >= 0) {
            const QByteArray line = pendingOutput.left(end).trimmed();
            pendingOutput.remove(0, end + 1);
            if (line.contains(readyMarker))
                ready = true;
            if (line.contains(passedMarker))
                validationPassed = true;
            if (ready && (!validation || validationPassed))
                timeout.stop();
            if (line.contains("[PZDirectMapPreview]") || line.contains("[PZ Direct Map]")
                    || line.contains("ERROR") || line.contains("Exception") || line.startsWith("at ")
                    || line.startsWith("Caused by:") || line.startsWith("Suppressed:"))
                qInfo().noquote() << QString::fromUtf8(line);
        }
    }

    QTemporaryDir profile{QDir::tempPath() + QStringLiteral("/PZDirectMapPreview-XXXXXX")};
    QTcpServer server;
    QProcess process;
    QTimer timeout;
    QByteArray request;
    QVector<QByteArray> cells;
    int nextCell = 0;
    QByteArray pendingOutput;
    std::function<void(bool)> completion;
    bool validation = false;
    bool ready = false;
    bool validationPassed = false;
};

QPointer<PreviewSession> activeSession;

}

bool DirectMapPreviewRunner::isRunning()
{
    return !activeSession.isNull();
}

bool DirectMapPreviewRunner::start(const QString &project, const QString &gameDirectory,
                                   const QVector<QPoint> &selection, const QString &validationScript, QString &error,
                                   std::function<void(bool)> finished)
{
    if (isRunning()) {
        error = QObject::tr("A direct map preview is already running.");
        return false;
    }
    const QDir game(gameDirectory);
    const QString agent = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
                QStringLiteral("../tools/DirectMapPreview/PZDirectMapPreview.jar"));
    if (gameDirectory.isEmpty() || !game.exists(QStringLiteral("projectzomboid.jar"))
            || !game.exists(QStringLiteral("jre64/bin/java.exe")) || !QFileInfo::exists(agent)) {
        error = QObject::tr("Direct preview requires a Project Zomboid 42.22.2 installation and the bundled preview agent.");
        return false;
    }
    QFile jar(game.filePath(QStringLiteral("projectzomboid.jar")));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!jar.open(QIODevice::ReadOnly) || !hash.addData(&jar)
            || hash.result().toHex() != QByteArrayLiteral("3c4a7c5f149d784758c6fb5f8fa85f3a21d572e38bc081400e89035f8120d287")) {
        error = QObject::tr("This prototype requires the exact Project Zomboid 42.22.2 Windows JAR.");
        return false;
    }
    QVector<QByteArray> cells;
    QJsonArray spawn;
    if (!DirectMapPreview::read(project, cells, spawn, error, selection))
        return false;
    auto session = std::make_unique<PreviewSession>(std::move(finished));
    if (!session->begin(game, agent, std::move(cells), spawn, validationScript, error))
        return false;
    activeSession = session.release();
    return true;
}
