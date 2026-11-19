#include "directmappreviewrunner.h"
#include "directmappreview.h"
#include "directmappreviewagent.h"
#include "playtestgamepath.h"
#include "../portablesettings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QVersionNumber>

#include <algorithm>
#include <memory>

namespace {

const QByteArray readyMarker("[PZDirectMapPreview] Lua ready");

bool appendLauncherOptions(const QJsonValue &value, QStringList &options)
{
    if (value.isUndefined())
        return true;
    if (!value.isArray())
        return false;
    for (const auto argument : value.toArray()) {
        if (!argument.isString() || argument.toString().trimmed().isEmpty())
            return false;
        const QString option = argument.toString();
        if (!option.startsWith(QLatin1String("-javaagent:"))
                && !option.startsWith(QLatin1String("-agentlib:"))
                && !option.startsWith(QLatin1String("-agentpath:")))
            options.append(option);
    }
    return true;
}

bool readLauncher(const QByteArray &data, const QVersionNumber &windows, QStringList &vmArgs,
                  QStringList &classpath, QString &mainClass, QString &error)
{
    error.clear();
    vmArgs.clear();
    classpath.clear();
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QObject::tr("Invalid launcher JSON at byte %1: %2").arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    const QJsonObject config = json.object();
    mainClass = config.value(QStringLiteral("mainClass")).toString().trimmed().replace(QLatin1Char('/'), QLatin1Char('.'));
    if (mainClass.isEmpty() || !config.value(QStringLiteral("classpath")).isArray()) {
        error = QObject::tr("The game launcher configuration requires mainClass and classpath.");
        return false;
    }
    for (const auto entry : config.value(QStringLiteral("classpath")).toArray()) {
        if (!entry.isString() || entry.toString().trimmed().isEmpty()) {
            error = QObject::tr("The game launcher classpath contains an empty or invalid entry.");
            return false;
        }
        classpath.append(entry.toString());
    }
    if (classpath.isEmpty()) {
        error = QObject::tr("The game launcher classpath is empty.");
        return false;
    }
    if (!appendLauncherOptions(config.value(QStringLiteral("vmArgs")), vmArgs)) {
        error = QObject::tr("The game launcher vmArgs must be a list of Java options.");
        return false;
    }
    const auto platforms = config.value(QStringLiteral("windows")).toObject();
    QVersionNumber matched;
    QString matchedKey;
    for (auto it = platforms.constBegin(); it != platforms.constEnd(); ++it) {
        int length = 0;
        const auto version = QVersionNumber::fromString(it.key(), &length).normalized();
        if (length == it.key().size() && !version.isNull() && version <= windows && version > matched) {
            matched = version;
            matchedKey = it.key();
        }
    }
    QStringList platformOptions;
    if (!matchedKey.isEmpty() && !appendLauncherOptions(platforms.value(matchedKey).toObject().value(QStringLiteral("vmArgs")), platformOptions)) {
        error = QObject::tr("The game launcher has invalid Windows Java options for %1.").arg(matchedKey);
        return false;
    }
    const auto selectsCollector = [](const QString &option) {
        return option.startsWith(QLatin1String("-XX:+Use")) && option.endsWith(QLatin1String("GC"));
    };
    const bool collectorSelected = std::any_of(vmArgs.cbegin(), vmArgs.cend(), selectsCollector);
    for (const QString &option : platformOptions) {
        if (!(collectorSelected && selectsCollector(option)))
            vmArgs.append(option);
    }
    return true;
}

QString javaProfilePath(const QDir &game, const QString &directory)
{
    const auto usable = [](const QString &path) {
        return !path.isEmpty() && !path.contains(QLatin1Char('='))
                && QString::fromLocal8Bit(path.toLocal8Bit()) == path;
    };
    const QString absolute = QDir::toNativeSeparators(directory);
    if (usable(absolute))
        return absolute;
    const QString relative = QDir::toNativeSeparators(game.relativeFilePath(directory));
    if (usable(relative))
        return relative;
#ifdef Q_OS_WIN
    const DWORD size = GetShortPathNameW(reinterpret_cast<LPCWSTR>(absolute.utf16()), nullptr, 0);
    if (size) {
        std::wstring shortPath(size, L'\0');
        const DWORD count = GetShortPathNameW(reinterpret_cast<LPCWSTR>(absolute.utf16()), &shortPath[0], size);
        if (count > 0 && count < size) {
            const QString path = QString::fromWCharArray(shortPath.data(), int(count));
            if (usable(path))
                return path;
        }
    }
#endif
    return {};
}

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
    explicit PreviewSession(std::function<void(bool, const QString &)> finished)
        : QObject(qApp), completion(std::move(finished))
    {
        process.setProcessChannelMode(QProcess::MergedChannels);
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
            readOutput();
        });
        connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus status) {
            readOutput();
            if (!pendingOutput.isEmpty())
                handleLine(pendingOutput.trimmed());
            QString failure;
            if (status != QProcess::NormalExit || code != 0)
                failure = tr("The game stopped with exit code %1.").arg(code);
            else if (!ready)
                failure = tr("The game closed before the source map became playable.");
            if (!failure.isEmpty()) {
                if (!javaError.isEmpty())
                    failure += QStringLiteral("\n") + javaError;
                qWarning().noquote() << "Direct map preview:" << failure << "\n" << QString::fromUtf8(outputTail).trimmed();
                const QFile &log = PortableSettings::messageLogFile();
                if (log.isOpen())
                    failure += tr("\n\nWorldEd log: %1").arg(QDir::toNativeSeparators(log.fileName()));
            }
            if (completion)
                completion(failure.isEmpty(), failure);
            deleteLater();
        }, Qt::QueuedConnection);
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
    }

    ~PreviewSession() override
    {
        disconnect(&process, nullptr, this, nullptr);
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
    }

    bool begin(const QDir &game, QStringList args, const QStringList &classpath, const QString &mainClass,
               QVector<QByteArray> sourceCells,
               const QJsonArray &spawn, QString &error)
    {
        QString javaDirectory;
        QStringList tempRoots{QDir::tempPath()};
#ifdef Q_OS_WIN
        const QString windows = qEnvironmentVariable("SystemRoot");
        if (!windows.isEmpty())
            tempRoots.append(QDir(windows).filePath(QStringLiteral("Temp")));
#endif
        tempRoots.removeDuplicates();
        for (const QString &root : tempRoots) {
            auto candidate = std::make_unique<QTemporaryDir>(QDir(root).filePath(QStringLiteral("PZDirectMapPreview-XXXXXX")));
            if (!candidate->isValid())
                continue;
            javaDirectory = javaProfilePath(game, candidate->path());
            if (!javaDirectory.isEmpty()) {
                profile = std::move(candidate);
                break;
            }
        }
        if (!profile) {
            error = tr("Could not create a temporary game profile usable by the bundled Java runtime. Check the TEMP folder and its permissions.\n%1")
                    .arg(QDir::toNativeSeparators(QDir::tempPath()));
            return false;
        }
        QString agentPath;
        if (!DirectMapPreviewAgent::deploy(profile->path(), agentPath, error))
            return false;
        const QString agent = QDir::toNativeSeparators(QDir(javaDirectory).filePath(QDir(profile->path()).relativeFilePath(agentPath)));
        if (spawn.size() != 3) {
            error = tr("The map sources do not provide a playable starting square.");
            return false;
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
        const QDir cache(profile->path());
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
                          scenario, error))
            return false;

        if (!server.listen(QHostAddress::LocalHost)) {
            error = server.errorString();
            return false;
        }
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request = "PZDP/1 " + token.toLatin1() + '\n';
        cells = std::move(sourceCells);
        args << QStringLiteral("-Dnosave=true")
             << (QStringLiteral("-javaagent:") + agent + QLatin1Char('=') + QString::number(server.serverPort()) + QLatin1Char(':') + token)
             << QStringLiteral("-cp") << classpath.join(QDir::listSeparator())
             << mainClass
             << QStringLiteral("-debug") << QStringLiteral("-nosteam")
             << QStringLiteral("-cachedir=") + javaDirectory
             << QStringLiteral("-modfolders") << QStringLiteral("mods");
        qInfo().noquote() << "Direct map preview profile:" << profile->path();
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.remove(QStringLiteral("_JAVA_OPTIONS"));
        environment.remove(QStringLiteral("JAVA_TOOL_OPTIONS"));
        environment.remove(QStringLiteral("JDK_JAVA_OPTIONS"));
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(game.absolutePath());
        const QString program = game.filePath(QStringLiteral("jre64/bin/java.exe"));
        qInfo().noquote() << "Direct map preview game runtime:" << QDir::toNativeSeparators(program)
                          << "main class:" << mainClass;
        process.start(program, args);
        if (!process.waitForStarted(5000)) {
            error = tr("Could not start the game's Java runtime.\n%1\n%2")
                    .arg(QDir::toNativeSeparators(program), process.errorString());
            return false;
        }
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
            error = tr("Could not write the playtest profile file.\n%1\n%2")
                    .arg(QDir::toNativeSeparators(path), file.errorString());
            return false;
        }
        return true;
    }

    void readOutput()
    {
        const QByteArray bytes = process.readAllStandardOutput();
        if (bytes.isEmpty())
            return;
        constexpr int maxBytes = 16384;
        outputTail.append(bytes);
        if (outputTail.size() > maxBytes)
            outputTail.remove(0, outputTail.size() - maxBytes);
        pendingOutput.append(bytes);
        int begin = 0;
        for (int end = pendingOutput.indexOf('\n'); end >= 0; end = pendingOutput.indexOf('\n', begin)) {
            handleLine(pendingOutput.mid(begin, end - begin).trimmed());
            begin = end + 1;
        }
        pendingOutput.remove(0, qMax(begin, pendingOutput.size() - maxBytes));
    }

    void handleLine(const QByteArray &line)
    {
        if (line.contains(readyMarker))
            ready = true;
        if (line.startsWith("Caused by:") || (javaError.isEmpty()
                && (line.startsWith("Error") || line.startsWith("Unrecognized") || line.startsWith("Exception in thread")
                    || line.startsWith("Invalid maximum heap size") || line.startsWith("Could not"))))
            javaError = QString::fromUtf8(line.left(512));
        if (line.contains("[PZDirectMapPreview]") || line.contains("[PZ Direct Map]")
                || line.contains("ERROR") || line.contains("Exception") || line.startsWith("at ")
                || line.startsWith("Caused by:") || line.startsWith("Suppressed:"))
            qInfo().noquote() << QString::fromUtf8(line);
    }

    std::unique_ptr<QTemporaryDir> profile;
    QTcpServer server;
    QProcess process;
    QByteArray request;
    QVector<QByteArray> cells;
    int nextCell = 0;
    QByteArray pendingOutput;
    QByteArray outputTail;
    QString javaError;
    std::function<void(bool, const QString &)> completion;
    bool ready = false;
};

QPointer<PreviewSession> activeSession;

}

bool DirectMapPreviewRunner::isRunning()
{
    return !activeSession.isNull();
}

bool DirectMapPreviewRunner::start(const QString &project, const QString &gameDirectory,
                                   const QVector<QPoint> &selection, QString &error,
                                   std::function<void(bool, const QString &)> finished)
{
    if (isRunning()) {
        error = QObject::tr("A direct map preview is already running.");
        return false;
    }
    error = PlaytestGamePath::installationError(gameDirectory);
    if (!error.isEmpty())
        return false;
    const QDir game(PlaytestGamePath::root(gameDirectory));
    QFile configuration(game.filePath(QStringLiteral("ProjectZomboid64.json")));
    if (!configuration.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Could not read the game launcher configuration.\n%1\n%2")
                .arg(QDir::toNativeSeparators(configuration.fileName()), configuration.errorString());
        return false;
    }
    const auto os = QOperatingSystemVersion::current();
    const QVersionNumber windows(os.majorVersion(), os.minorVersion(), os.microVersion());
    QStringList vmArgs;
    QStringList classpath;
    QString mainClass;
    if (!readLauncher(configuration.readAll(), windows, vmArgs, classpath, mainClass, error)) {
        error += QLatin1Char('\n') + QDir::toNativeSeparators(configuration.fileName());
        return false;
    }
    QFile jar(game.filePath(QStringLiteral("projectzomboid.jar")));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!jar.open(QIODevice::ReadOnly) || !hash.addData(&jar)) {
        error = QObject::tr("Could not read the installed game JAR.\n%1\n%2")
                .arg(QDir::toNativeSeparators(jar.fileName()), jar.errorString());
        return false;
    }
    if (hash.result().toHex() != QByteArrayLiteral("3c4a7c5f149d784758c6fb5f8fa85f3a21d572e38bc081400e89035f8120d287")) {
        qWarning().noquote() << "Direct map preview game JAR SHA-256:" << hash.result().toHex() << jar.fileName();
        error = QObject::tr("This playtest supports the original Project Zomboid 42.22.2 Windows game JAR. The selected installation has a different JAR.\n%1")
                .arg(QDir::toNativeSeparators(jar.fileName()));
        return false;
    }
    QVector<QByteArray> cells;
    QJsonArray spawn;
    if (!DirectMapPreview::read(project, cells, spawn, error, selection))
        return false;
    auto session = std::make_unique<PreviewSession>(std::move(finished));
    if (!session->begin(game, vmArgs, classpath, mainClass, std::move(cells), spawn, error))
        return false;
    activeSession = session.release();
    return true;
}
