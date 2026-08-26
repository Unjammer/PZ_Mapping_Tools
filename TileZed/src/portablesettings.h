#ifndef PORTABLESETTINGS_H
#define PORTABLESETTINGS_H
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QWidget>
#include <QWindow>
#ifdef PZTOOLS_APP_ISSUE_NOTIFIER
#include "appissuenotifier.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <exception>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#endif
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#include <unistd.h>
#elif defined(Q_OS_MAC)
#include <mach/mach.h>
#include <sys/sysctl.h>
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
inline int recommendedWorkerCount(int maximum = 16, int reserved = 1)
{
    const int logicalProcessors = qMax(1, QThread::idealThreadCount());
    return qBound(1, logicalProcessors - qMax(0, reserved),
                  qMax(1, maximum));
}
inline QSize oneShotMainWindowSizeFromEnvironment()
{
    const QString text = QString::fromLatin1(
                qgetenv("PZTOOLS_ONESHOT_WINDOW_SIZE")).trimmed().toLower();
    const int separator = text.indexOf(QLatin1Char('x'));
    if (separator <= 0)
        return QSize();
    bool widthOk = false;
    bool heightOk = false;
    const int width = text.left(separator).toInt(&widthOk);
    const int height = text.mid(separator + 1).toInt(&heightOk);
    if (!widthOk || !heightOk || width < 1 || height < 1)
        return QSize();
    return QSize(width, height);
}
inline QSize applyOneShotMainWindowGeometry(
        QWidget *window, const QSize &requestedSize = QSize())
{
    if (!window)
        return QSize();
    const QSize requested = requestedSize.isValid()
            ? requestedSize : oneShotMainWindowSizeFromEnvironment();
    if (!requested.isValid())
        return QSize();
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen || !screen->availableGeometry().isValid())
        return QSize();
    const QRect available = screen->availableGeometry();
    const QSize applied(qMin(requested.width(), available.width()),
                        qMin(requested.height(), available.height()));
    QRect geometry(QPoint(), applied);
    geometry.moveCenter(available.center());
    if (!window->property("PZToolsOneShotWindowGeometry").toBool()) {
        QSettings settings;
        settings.beginGroup(QLatin1String("MainWindow"));
        settings.setValue(QLatin1String("geometry"), window->saveGeometry());
        settings.endGroup();
        settings.sync();
    }
    if (window->isMaximized() || window->isFullScreen())
        window->showNormal();
    window->setProperty("PZToolsOneShotWindowGeometry", true);
    window->setGeometry(geometry);
    qInfo() << "One-shot main-window geometry applied:"
            << requested << "requested," << applied << "available-screen fit,"
            << "centered on" << screen->name()
            << "without persistent geometry propagation";
    return applied;
}
inline bool shouldPersistMainWindowGeometry(const QWidget *window)
{
    return !window ||
            !window->property("PZToolsOneShotWindowGeometry").toBool();
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
inline QString normalizedGamePath(const QString &candidate)
{
    if (candidate.trimmed().isEmpty())
        return QString();
    QDir directory(QDir::cleanPath(
                       QDir::fromNativeSeparators(candidate.trimmed())));
    if (directory.dirName().compare(
                QLatin1String("media"), Qt::CaseInsensitive) == 0) {
        directory.cdUp();
    }
    const QDir media(directory.filePath(QLatin1String("media")));
    if (!media.exists())
        return QString();
    const bool hasTileDefinitions = QFileInfo(media.filePath(
                QLatin1String("newtiledefinitions.tiles"))).isFile();
    const bool hasLua = QDir(media.filePath(
                QLatin1String("lua"))).exists();
    const bool hasPacks = QDir(media.filePath(
                QLatin1String("texturepacks"))).exists();
    if (!hasTileDefinitions && !hasLua && !hasPacks)
        return QString();
    return QDir::cleanPath(directory.absolutePath());
}
inline bool isGamePath(const QString &candidate)
{
    return !normalizedGamePath(candidate).isEmpty();
}
inline QString detectGamePath()
{
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << QStringLiteral(
                      "C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid")
               << QStringLiteral(
                      "C:/Program Files/Steam/steamapps/common/ProjectZomboid");
#elif defined(Q_OS_MAC)
    candidates << QStringLiteral(
                      "/Applications/Project Zomboid.app/Contents/Resources")
               << QDir::home().filePath(QStringLiteral(
                      "Library/Application Support/Steam/steamapps/common/"
                      "ProjectZomboid/Project Zomboid.app/Contents/Resources"));
#else
    candidates << QDir::home().filePath(QStringLiteral(
                      ".steam/steam/steamapps/common/ProjectZomboid"))
               << QDir::home().filePath(QStringLiteral(
                      ".local/share/Steam/steamapps/common/ProjectZomboid"));
#endif
    for (const QString &candidate : candidates) {
        const QString normalized = normalizedGamePath(candidate);
        if (!normalized.isEmpty())
            return normalized;
    }
    return QString();
}
inline QString sharedGamePath()
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    const QString key =
            QLatin1String("Paths/ProjectZomboidDirectory");
    QString configured = normalizedGamePath(shared.value(key).toString());
    if (configured.isEmpty() && !shared.contains(key))
        configured = detectGamePath();
    if (!configured.isEmpty())
        shared.setValue(key, configured);
    return configured;
}
inline void setSharedGamePath(const QString &directory)
{
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    const QString normalized = normalizedGamePath(directory);
    if (directory.trimmed().isEmpty())
        shared.setValue(QLatin1String("Paths/ProjectZomboidDirectory"),
                        QString());
    else
        shared.setValue(QLatin1String("Paths/ProjectZomboidDirectory"),
                        normalized);
    shared.sync();
}
inline QString gamePath(const QString &relativePath = QString())
{
    const QString root = sharedGamePath();
    return root.isEmpty() || relativePath.isEmpty()
            ? root : QDir(root).filePath(relativePath);
}
inline QString gameMediaPath(const QString &relativePath = QString())
{
    const QString media = gamePath(QLatin1String("media"));
    return media.isEmpty() || relativePath.isEmpty()
            ? media : QDir(media).filePath(relativePath);
}
inline QString gameTexturePacksPath()
{
    return gameMediaPath(QLatin1String("texturepacks"));
}
inline QString gameWorldGenPath()
{
    return gameMediaPath(QLatin1String("lua/server/WorldGen"));
}
inline QString gameLootDefinitionsPath()
{
    return gameMediaPath(QLatin1String("lua/server/Items"));
}
inline QString basementSourcePath()
{
    return installPath(QLatin1String("pzby_tbx/basement_access"));
}
inline QString basementBinMapPath()
{
    return installPath(QLatin1String("pzby_tbx/binmap"));
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
    migrateLegacySharedPaths();
    prepareNamedApplicationSettings(QCoreApplication::applicationName());
    QSettings shared(sharedSettingsFilePath(), QSettings::IniFormat);
    const QString configPath =
            shared.value(QLatin1String("Paths/ConfigDirectory")).toString();
    const QString tilesPath =
            shared.value(QLatin1String("Paths/TilesDirectory")).toString();
    const bool hasGamePath = shared.contains(
                QLatin1String("Paths/ProjectZomboidDirectory"));
    const QString gamePath = shared.value(
                QLatin1String("Paths/ProjectZomboidDirectory")).toString();
    const int storedVersion =
            shared.value(QLatin1String("General/SettingsSchema"), 0).toInt();
    if (storedVersion != SETTINGS_SCHEMA_VERSION) {
        shared.clear();
        if (!configPath.isEmpty())
            shared.setValue(QLatin1String("Paths/ConfigDirectory"), configPath);
        if (!tilesPath.isEmpty())
            shared.setValue(QLatin1String("Paths/TilesDirectory"), tilesPath);
        if (hasGamePath)
            shared.setValue(
                        QLatin1String("Paths/ProjectZomboidDirectory"),
                        gamePath);
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
inline QString cpuDescription()
{
#ifdef Q_OS_WIN
    QSettings cpuRegistry(
                QStringLiteral(
                    "HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\"
                    "CentralProcessor\\0"),
                QSettings::NativeFormat);
    const QString description = cpuRegistry.value(
                QStringLiteral("ProcessorNameString")).toString().simplified();
    if (!description.isEmpty())
        return description;
#elif defined(Q_OS_LINUX)
    QFile cpuInfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<QByteArray> lines = cpuInfo.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("model name") &&
                    !line.startsWith("Hardware"))
                continue;
            const int separator = line.indexOf(':');
            if (separator >= 0) {
                const QString description =
                        QString::fromLocal8Bit(line.mid(separator + 1))
                        .simplified();
                if (!description.isEmpty())
                    return description;
            }
        }
    }
#elif defined(Q_OS_MAC)
    size_t length = 0;
    if (sysctlbyname("machdep.cpu.brand_string", nullptr, &length,
                     nullptr, 0) == 0 && length > 1) {
        QByteArray description(int(length), '\0');
        if (sysctlbyname("machdep.cpu.brand_string", description.data(),
                         &length, nullptr, 0) == 0) {
            return QString::fromLocal8Bit(description.constData()).simplified();
        }
    }
#endif
    return QSysInfo::currentCpuArchitecture();
}
inline quint64 totalPhysicalMemoryBytes()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        return quint64(memory.ullTotalPhys);
#elif defined(Q_OS_LINUX)
    struct sysinfo memory = {};
    if (::sysinfo(&memory) == 0)
        return quint64(memory.totalram) * quint64(memory.mem_unit);
#elif defined(Q_OS_MAC)
    quint64 memory = 0;
    size_t length = sizeof(memory);
    if (sysctlbyname("hw.memsize", &memory, &length, nullptr, 0) == 0)
        return memory;
#endif
    return 0;
}
inline quint64 availablePhysicalMemoryBytes()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        return quint64(memory.ullAvailPhys);
#elif defined(Q_OS_LINUX)
    QFile memInfo(QStringLiteral("/proc/meminfo"));
    if (memInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<QByteArray> lines = memInfo.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("MemAvailable:"))
                continue;
            const QList<QByteArray> fields = line.simplified().split(' ');
            if (fields.size() >= 2)
                return fields.at(1).toULongLong() * 1024ULL;
        }
    }
#endif
    return 0;
}
inline QStringList graphicsAdapterDescriptions()
{
    QStringList adapters;
#ifdef Q_OS_WIN
    for (DWORD index = 0; ; ++index) {
        DISPLAY_DEVICEW adapter = {};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0))
            break;
        if (adapter.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
            continue;
        const QString description =
                QString::fromWCharArray(adapter.DeviceString).simplified();
        if (description.isEmpty())
            continue;
        int existing = -1;
        for (int adapterIndex = 0; adapterIndex < adapters.size();
             ++adapterIndex) {
            QString existingDescription = adapters.at(adapterIndex);
            existingDescription.remove(QStringLiteral(" [active]"));
            if (existingDescription.compare(
                        description, Qt::CaseInsensitive) == 0) {
                existing = adapterIndex;
                break;
            }
        }
        const QString labelledDescription =
                description + ((adapter.StateFlags & DISPLAY_DEVICE_ACTIVE)
                               ? QStringLiteral(" [active]")
                               : QString());
        if (existing == -1) {
            adapters += labelledDescription;
        } else if (adapter.StateFlags & DISPLAY_DEVICE_ACTIVE) {
            adapters[existing] = labelledDescription;
        }
    }
#elif defined(Q_OS_LINUX)
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList cards = drm.entryList(
                QStringList() << QStringLiteral("card[0-9]*"),
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &card : cards) {
        QFile vendorFile(drm.filePath(
                             card + QStringLiteral("/device/vendor")));
        QFile deviceFile(drm.filePath(
                             card + QStringLiteral("/device/device")));
        if (!vendorFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
                !deviceFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString vendor =
                QString::fromLatin1(vendorFile.readAll()).trimmed();
        const QString device =
                QString::fromLatin1(deviceFile.readAll()).trimmed();
        adapters += QStringLiteral("%1 PCI %2:%3")
                .arg(card, vendor, device);
    }
#endif
    return adapters;
}
inline QString gibibytes(quint64 bytes)
{
    return bytes == 0
            ? QStringLiteral("unknown")
            : QStringLiteral("%1 GiB").arg(
                  double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}
inline quint64 currentProcessMemoryBytes()
{
#ifdef Q_OS_WIN
    typedef BOOL (WINAPI *GetProcessMemoryInfoFunction)(
            HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    static GetProcessMemoryInfoFunction queryMemory =
            reinterpret_cast<GetProcessMemoryInfoFunction>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                               "K32GetProcessMemoryInfo"));
    if (!queryMemory)
        return 0;
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (!queryMemory(GetCurrentProcess(),
                     reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
                     sizeof(counters))) {
        return 0;
    }
    return quint64(counters.WorkingSetSize);
#elif defined(Q_OS_LINUX)
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QList<QByteArray> values = statm.readLine().simplified().split(' ');
    if (values.size() < 2)
        return 0;
    bool ok = false;
    const quint64 residentPages = values.at(1).toULongLong(&ok);
    const long pageSize = sysconf(_SC_PAGESIZE);
    return ok && pageSize > 0 ? residentPages * quint64(pageSize) : 0;
#elif defined(Q_OS_MAC)
    mach_task_basic_info_data_t info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count)
            != KERN_SUCCESS) {
        return 0;
    }
    return quint64(info.resident_size);
#else
    return 0;
#endif
}
inline void logMachineDiagnostics()
{
    qInfo().noquote()
            << "Machine OS:" << QSysInfo::prettyProductName()
            << "| kernel:" << QSysInfo::kernelType()
            << QSysInfo::kernelVersion()
            << "| architecture:" << QSysInfo::currentCpuArchitecture();
    qInfo().noquote()
            << "Machine CPU:" << cpuDescription()
            << "| logical processors:" << QThread::idealThreadCount();
    const quint64 totalMemory = totalPhysicalMemoryBytes();
    const quint64 availableMemory = availablePhysicalMemoryBytes();
    qInfo().noquote()
            << "Machine RAM: total" << gibibytes(totalMemory)
            << "| available at startup:" << gibibytes(availableMemory);
    const QStringList adapters = graphicsAdapterDescriptions();
    qInfo().noquote()
            << "Machine GPU/display adapters:"
            << (adapters.isEmpty()
                ? QStringLiteral("not reported by the operating system")
                : adapters.join(QStringLiteral(" | ")));
    qInfo().noquote()
            << "Runtime: Qt" << QString::fromLatin1(qVersion())
            << "| Qt build ABI:" << QSysInfo::buildAbi()
            << "| process:" << (sizeof(void *) * 8) << "bit";
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
#ifdef PZTOOLS_APP_ISSUE_NOTIFIER
    AppIssueCenter::captureQtMessage(type, context, message);
#endif
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
#ifdef PZTOOLS_APP_ISSUE_NOTIFIER
    AppIssueNotifier::initialize();
#endif
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionLogger);
#endif
    std::set_terminate(terminateLogger);
    qInfo().noquote() << "Logging to" << QDir::toNativeSeparators(filePath);
    qInfo().noquote() << "Installation root" << QDir::toNativeSeparators(installRootPath());
    qInfo().noquote() << "Application configuration" << QDir::toNativeSeparators(applicationConfigPath());
    QSettings settings;
    qInfo().noquote() << "Settings file" << QDir::toNativeSeparators(settings.fileName());
    logMachineDiagnostics();
    return filePath;
}
}
#endif
