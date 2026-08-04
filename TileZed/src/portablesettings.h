#ifndef PORTABLESETTINGS_H
#define PORTABLESETTINGS_H

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThread>

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PortableSettings {

static const int SETTINGS_SCHEMA_VERSION = 2;

inline QString executableDirectoryPath()
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

inline bool usesBinLayout()
{
    return QDir(executableDirectoryPath()).dirName().compare(
                QLatin1String("bin"), Qt::CaseInsensitive) == 0;
}

inline QString installRootPath()
{
    QDir directory(executableDirectoryPath());
    if (usesBinLayout())
        directory.cdUp();
    return directory.absolutePath();
}

inline QString installPath(const QString &relativePath)
{
    return QDir(installRootPath()).filePath(relativePath);
}

inline QString applicationConfigPath()
{
    const QString configDirectory = installPath(QLatin1String("config"));
    if (usesBinLayout() || QDir(configDirectory).exists())
        return configDirectory;
    return executableDirectoryPath();
}

inline QString rootPath()
{
    return installPath(QLatin1String("settings"));
}

inline bool containsConfigurationCatalogs(const QString &candidate)
{
    const QFileInfo directoryInfo(candidate);
    if (!directoryInfo.exists() || !directoryInfo.isDir())
        return false;

    // Portable settings are never a configuration-catalog directory, even if
    // an early preview copied stale catalogs there.
    if (directoryInfo.fileName().compare(
                QLatin1String("settings"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    const QDir directory(candidate);
    const QStringList requiredCatalogs = {
        QLatin1String("Tilesets.txt"),
        QLatin1String("TMXConfig.txt"),
        QLatin1String("BuildingTiles.txt"),
        QLatin1String("BuildingFurniture.txt"),
        QLatin1String("BuildingTemplates.txt")
    };
    for (const QString &fileName : requiredCatalogs) {
        if (!QFileInfo(directory.filePath(fileName)).isFile())
            return false;
    }
    return true;
}

inline QString normalizedConfigurationPath(const QString &candidate)
{
    if (candidate.trimmed().isEmpty())
        return QString();

    const QString cleaned = QDir::cleanPath(candidate);
    if (containsConfigurationCatalogs(cleaned))
        return cleaned;

    const QString nested =
            QDir(cleaned).filePath(QLatin1String("config"));
    if (containsConfigurationCatalogs(nested))
        return QDir::cleanPath(nested);
    return cleaned;
}

inline bool isConfigurationPath(const QString &candidate)
{
    return containsConfigurationCatalogs(
                normalizedConfigurationPath(candidate));
}

inline QString validatedConfigurationPath(const QString &candidate)
{
    const QString cleaned = normalizedConfigurationPath(candidate);
    if (isConfigurationPath(cleaned))
        return cleaned;
    return QDir::cleanPath(applicationConfigPath());
}

inline QString sharedSettingsFilePath()
{
    return QDir(rootPath()).filePath(QLatin1String("PZTools.ini"));
}

inline bool syncThemeAcrossApplications()
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    return shared.value(
                QLatin1String("Interface/SyncThemeAcrossApplications"),
                false).toBool();
}

inline QString sharedTheme(const QString &fallback = QStringLiteral("Default"))
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    return shared.value(QLatin1String("Interface/Theme"), fallback).toString();
}

inline void setThemeForAllApplications(const QString &theme)
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    shared.setValue(QLatin1String("Interface/Theme"), theme);
    shared.sync();

    const QStringList applicationNames = {
        QLatin1String("TileZed"),
        QLatin1String("BuildingEd"),
        QLatin1String("PZWorldEd")
    };
    for (const QString &applicationName : applicationNames) {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QLatin1String("TheIndieStone"), applicationName);
        settings.setValue(QLatin1String("Interface/Theme"), theme);
        settings.sync();
    }
}

inline void setSyncThemeAcrossApplications(bool enabled,
                                           const QString &currentTheme = QString())
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    shared.setValue(QLatin1String("Interface/SyncThemeAcrossApplications"),
                    enabled);
    shared.sync();
    if (enabled && !currentTheme.isEmpty())
        setThemeForAllApplications(currentTheme);
}

inline QString sharedConfigurationPath()
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    const QString configured = shared.value(
                QLatin1String("Paths/ConfigDirectory"),
                applicationConfigPath()).toString();
    const QString validated = validatedConfigurationPath(configured);
    if (QDir::cleanPath(configured).compare(
                validated, Qt::CaseInsensitive) != 0) {
        qWarning().noquote() << "Ignoring invalid shared configuration directory"
                             << QDir::toNativeSeparators(configured)
                             << "- using"
                             << QDir::toNativeSeparators(validated);
    }
    shared.setValue(QLatin1String("Paths/ConfigDirectory"), validated);
    return validated;
}

inline void setSharedConfigurationPath(const QString &directory)
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    shared.setValue(QLatin1String("Paths/ConfigDirectory"),
                    normalizedConfigurationPath(directory));
    shared.sync();
}

inline QString normalizedTilesPath(const QString &candidate);
inline bool isTilesPath(const QString &candidate);

inline QString detectTilesPath()
{
    QStringList candidates;
    candidates += installPath(QLatin1String("Tiles"));
    candidates += QDir(installRootPath()).absoluteFilePath(
                QLatin1String("../Tiles"));
    candidates += QDir(installRootPath()).absoluteFilePath(
                QLatin1String("../../Tiles"));

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isDir() && isTilesPath(candidate))
            return normalizedTilesPath(info.absoluteFilePath());
    }
    return QString();
}

inline QString normalizedTilesPath(const QString &candidate)
{
    if (candidate.trimmed().isEmpty())
        return QString();
    QDir directory(QDir::cleanPath(candidate));
    const QString name = directory.dirName();
    const bool scaleDirectory =
            name.compare(QLatin1String("1x"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("2x"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("custom"), Qt::CaseInsensitive) == 0;
    if (scaleDirectory)
        directory.cdUp();

    const QStringList filters = { QLatin1String("*.png") };
    auto containsTiles = [&filters](const QDir &root) {
        if (!root.entryList(filters, QDir::Files).isEmpty())
            return true;
        const QStringList scales = {
            QLatin1String("1x"), QLatin1String("2x"),
            QLatin1String("custom")
        };
        for (const QString &scale : scales) {
            const QDir scaled(root.filePath(scale));
            if (scaled.exists()
                    && !scaled.entryList(filters, QDir::Files).isEmpty())
                return true;
        }
        return false;
    };

    if (!containsTiles(directory)) {
        const QDir nested(directory.filePath(QLatin1String("Tiles")));
        if (nested.exists() && containsTiles(nested))
            directory = nested;
    }
    return QDir::cleanPath(directory.absolutePath());
}

inline bool isTilesPath(const QString &candidate)
{
    const QDir directory(normalizedTilesPath(candidate));
    if (candidate.trimmed().isEmpty() || !directory.exists())
        return false;
    const QStringList filters = { QLatin1String("*.png") };
    const QStringList scaleDirectories = {
        QLatin1String("1x"), QLatin1String("2x"),
        QLatin1String("custom")
    };
    for (const QString &scale : scaleDirectories) {
        const QDir scaled(directory.filePath(scale));
        if (scaled.exists()
                && !scaled.entryList(filters, QDir::Files).isEmpty()) {
            return true;
        }
    }
    return !directory.entryList(filters, QDir::Files).isEmpty();
}

inline QString sharedTilesPath()
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    QString configured =
            shared.value(QLatin1String("Paths/TilesDirectory")).toString();
    if (!configured.isEmpty())
        configured = normalizedTilesPath(configured);
    if (!isTilesPath(configured))
        configured = detectTilesPath();
    if (!configured.isEmpty())
        shared.setValue(QLatin1String("Paths/TilesDirectory"), configured);
    return configured;
}

inline void setSharedTilesPath(const QString &directory)
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    shared.setValue(QLatin1String("Paths/TilesDirectory"),
                    normalizedTilesPath(directory));
    shared.sync();
}

inline void prepareNamedApplicationSettings(const QString &applicationName)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QLatin1String("TheIndieStone"), applicationName);
    const QString versionKey =
            QLatin1String("General/PZToolsSettingsSchema");
    const int storedVersion = settings.value(versionKey, 0).toInt();
    if (storedVersion != SETTINGS_SCHEMA_VERSION) {
        settings.clear();
        settings.setValue(versionKey, SETTINGS_SCHEMA_VERSION);
        settings.sync();
    }
}

inline void migrateLegacySharedPaths()
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);

    QString configuration =
            shared.value(QLatin1String("Paths/ConfigDirectory")).toString();
    QString tiles =
            shared.value(QLatin1String("Paths/TilesDirectory")).toString();

    QSettings tileZed(QSettings::IniFormat, QSettings::UserScope,
                      QLatin1String("TheIndieStone"),
                      QLatin1String("TileZed"));
    QSettings worldEd(QSettings::IniFormat, QSettings::UserScope,
                      QLatin1String("TheIndieStone"),
                      QLatin1String("PZWorldEd"));

    if (!isConfigurationPath(configuration)) {
        const QString candidates[] = {
            tileZed.value(QLatin1String("ConfigDirectory")).toString(),
            worldEd.value(QLatin1String("ConfigDirectory")).toString()
        };
        for (const QString &candidate : candidates) {
            if (isConfigurationPath(candidate)) {
                configuration = QDir::cleanPath(candidate);
                shared.setValue(QLatin1String("Paths/ConfigDirectory"),
                                configuration);
                break;
            }
        }
    }

    if (!isTilesPath(tiles)) {
        const QString candidates[] = {
            tileZed.value(
                QLatin1String("Tilesets/TilesDirectory")).toString(),
            tileZed.value(QLatin1String("TilesDirectory")).toString(),
            worldEd.value(QLatin1String("TilesDirectory")).toString()
        };
        for (const QString &candidate : candidates) {
            if (isTilesPath(candidate)) {
                tiles = QDir::cleanPath(candidate);
                shared.setValue(QLatin1String("Paths/TilesDirectory"), tiles);
                break;
            }
        }
    }
    shared.sync();
}

inline void prepareVersionedSettings()
{
    // Reset only application-specific state when the schema changes. Shared
    // paths live in PZTools.ini and are preserved independently.
    migrateLegacySharedPaths();
    prepareNamedApplicationSettings(QCoreApplication::applicationName());
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    const QString configPath =
            shared.value(QLatin1String("Paths/ConfigDirectory")).toString();
    const QString tilesPath =
            shared.value(QLatin1String("Paths/TilesDirectory")).toString();
    const int storedVersion =
            shared.value(QLatin1String("General/SettingsSchema"), 0).toInt();
    if (storedVersion != SETTINGS_SCHEMA_VERSION) {
        shared.clear();
        if (!configPath.isEmpty())
            shared.setValue(QLatin1String("Paths/ConfigDirectory"), configPath);
        if (!tilesPath.isEmpty())
            shared.setValue(QLatin1String("Paths/TilesDirectory"), tilesPath);
        shared.setValue(QLatin1String("General/SettingsSchema"),
                        SETTINGS_SCHEMA_VERSION);
        shared.sync();
    }
}

inline QString path(const QString &relativePath)
{
    return QDir(rootPath()).filePath(relativePath);
}

inline void configure()
{
    const QString path = rootPath();
    QDir().mkpath(path);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
}

inline QFile &messageLogFile()
{
    static QFile file;
    return file;
}

inline QMutex &messageLogMutex()
{
    static QMutex mutex;
    return mutex;
}

inline const char *messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARNING";
    case QtCriticalMsg: return "CRITICAL";
    case QtFatalMsg: return "FATAL";
    }
    return "UNKNOWN";
}

inline void messageHandler(QtMsgType type,
                           const QMessageLogContext &context,
                           const QString &message)
{
    if (type == QtWarningMsg &&
            message == QStringLiteral(
                "libpng warning: iCCP: known incorrect sRGB profile"))
        return;

    const QString source = context.file
            ? QStringLiteral(" (%1:%2)")
              .arg(QString::fromUtf8(context.file))
              .arg(context.line)
            : QString();
    const QString threadName = QThread::currentThread()->objectName();
    const QString threadLabel = threadName.isEmpty()
            ? QString()
            : QStringLiteral(" name:%1").arg(threadName);
    const QString line = QStringLiteral("%1 [%2] [pid:%3 thread:%4%5] %6%7\n")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
            .arg(QString::fromLatin1(messageTypeName(type)))
             .arg(QCoreApplication::applicationPid())
             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
             .arg(threadLabel, message, source);
    const QByteArray encoded = line.toUtf8();

    {
        QMutexLocker locker(&messageLogMutex());
        QFile &file = messageLogFile();
        if (file.isOpen()) {
            file.write(encoded);
            file.flush();
        }
    }

    std::fwrite(encoded.constData(), 1, size_t(encoded.size()), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg)
        std::abort();
}

#ifdef Q_OS_WIN
inline LONG WINAPI unhandledExceptionLogger(EXCEPTION_POINTERS *exceptionInfo)
{
    if (exceptionInfo && exceptionInfo->ExceptionRecord) {
        const quintptr address = reinterpret_cast<quintptr>(
                    exceptionInfo->ExceptionRecord->ExceptionAddress);
        QString moduleDescription;
        HMODULE module = nullptr;
        if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(address), &module)) {
            wchar_t modulePath[MAX_PATH] = {};
                const DWORD length = GetModuleFileNameW(
                        module, modulePath, MAX_PATH);
            if (length > 0) {
                moduleDescription =
                        QStringLiteral(" module=\"%1\" module-offset=0x%2")
                        .arg(QDir::toNativeSeparators(
                                 QString::fromWCharArray(
                                     modulePath, int(length))))
                        .arg(address - reinterpret_cast<quintptr>(module),
                             0, 16);
            }
        }
        qCritical().nospace()
                << "Unhandled Windows exception code=0x"
                << QString::number(
                       exceptionInfo->ExceptionRecord->ExceptionCode, 16)
                << " address=0x"
                << QString::number(address, 16)
                << moduleDescription;
    } else {
        qCritical() << "Unhandled Windows exception (details unavailable)";
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

inline void terminateLogger()
{
    qCritical() << "Unhandled C++ exception or std::terminate()";
    std::abort();
}

inline QString installLogging()
{
    const QString logDirectory = path(QStringLiteral("logs"));
    if (!QDir().mkpath(logDirectory))
        return QString();

    QString applicationName = QCoreApplication::applicationName();
    for (int i = 0; i < applicationName.size(); ++i) {
        const QChar c = applicationName.at(i);
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            applicationName[i] = QLatin1Char('_');
    }
    if (applicationName.isEmpty())
        applicationName = QStringLiteral("application");

    QDir logs(logDirectory);
    const QFileInfoList previousLogs = logs.entryInfoList(
                QStringList() << QStringLiteral("%1-*.log").arg(applicationName),
                QDir::Files, QDir::Time);
    static const int MAX_LOG_FILES_PER_APPLICATION = 20;
    for (int index = MAX_LOG_FILES_PER_APPLICATION - 1;
         index < previousLogs.size(); ++index) {
        QFile::remove(previousLogs.at(index).absoluteFilePath());
    }

    const QString fileName = QStringLiteral("%1-%2-%3.log")
            .arg(applicationName)
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyyMMdd-HHmmss-zzz")))
            .arg(QCoreApplication::applicationPid());
    const QString filePath = QDir(logDirectory).filePath(fileName);

    QFile &file = messageLogFile();
    file.setFileName(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return QString();

    qInstallMessageHandler(messageHandler);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionLogger);
#endif
    std::set_terminate(terminateLogger);
    qInfo().noquote() << "Logging to" << QDir::toNativeSeparators(filePath);
    qInfo().noquote() << "Installation root" << QDir::toNativeSeparators(installRootPath());
    qInfo().noquote() << "Application configuration" << QDir::toNativeSeparators(applicationConfigPath());
    QSettings settings;
    qInfo().noquote() << "Settings file" << QDir::toNativeSeparators(settings.fileName());
    return filePath;
}

} // namespace PortableSettings

#endif // PORTABLESETTINGS_H
