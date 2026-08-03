/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "preferences.h"
#include "../portablesettings.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>

namespace {

struct BuiltInTheme
{
    const char *displayName;
    const char *fileName;
    const char *resourcePath;
    const char *baseResourcePath;
};

const BuiltInTheme builtInThemes[] = {
    { "Breeze (Dark)", "Breeze-Dark.qss", ":breeze/dark/stylesheet.qss", 0 },
    { "Breeze (Dark Blue)", "Breeze-Dark-Blue.qss", ":breeze/dark-blue/stylesheet.qss", 0 },
    { "QDarkStyle (Dark)", "QDarkStyle-Dark.qss", ":qdarkstyle/dark/darkstyle.qss", 0 },
    { "QDarkStyle (Light)", "QDarkStyle-Light.qss", ":qdarkstyle/light/lightstyle.qss", 0 },
    { "Mapping Discord (B42)", "Mapping-Discord-B42.qss",
      ":breeze/mapping-discord/stylesheet.qss", ":breeze/dark/stylesheet.qss" }
};

QString themesDirectoryPath()
{
    return QDir(PortableSettings::installRootPath()).filePath(QStringLiteral("themes"));
}

QString builtInThemePath(const QString &displayName)
{
    for (const BuiltInTheme &theme : builtInThemes) {
        if (displayName == QLatin1String(theme.displayName))
            return QDir(themesDirectoryPath()).filePath(QLatin1String(theme.fileName));
    }
    return QString();
}

QString builtInThemeResource(const QString &displayName)
{
    for (const BuiltInTheme &theme : builtInThemes) {
        if (displayName == QLatin1String(theme.displayName))
            return QLatin1String(theme.resourcePath);
    }
    return QString();
}

bool isBuiltInThemeFile(const QString &fileName)
{
    for (const BuiltInTheme &theme : builtInThemes) {
        if (fileName.compare(QLatin1String(theme.fileName), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString readStyleSheet(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    return stream.readAll();
}

QString mappingDiscordPalette(QString styleSheet)
{
    struct ColorReplacement
    {
        const char *source;
        const char *destination;
    };

    const ColorReplacement replacements[] = {
        { "#eff0f1", "#eef2ef" },
        { "#31363b", "#141c17" },
        { "#2c3034", "#101712" },
        { "#1d2023", "#0a0f0c" },
        { "#76797c", "#4a5c50" },
        { "#626568", "#2d3c32" },
        { "#3daee9", "#65c981" },
        { "#2a79a3", "#4e8f61" },
        { "#2f88b7", "#59ad72" },
        { "#334e5e", "#24452f" },
        { "#454545", "#46544a" },
        { "#454a4f", "#243229" },
        { "#b0b0b0", "#9da9a1" },
        { "#ffffff", "#eef2ef" }
    };

    for (const ColorReplacement &replacement : replacements) {
        styleSheet.replace(QLatin1String(replacement.source),
                           QLatin1String(replacement.destination),
                           Qt::CaseInsensitive);
    }
    return styleSheet;
}

QString builtInThemeStyleSheet(const QString &displayName)
{
    for (const BuiltInTheme &theme : builtInThemes) {
        if (displayName != QLatin1String(theme.displayName))
            continue;

        QString styleSheet;
        if (theme.baseResourcePath) {
            styleSheet = readStyleSheet(QLatin1String(theme.baseResourcePath));
            if (displayName == QLatin1String("Mapping Discord (B42)"))
                styleSheet = mappingDiscordPalette(styleSheet);
            styleSheet += QLatin1Char('\n');
        }
        styleSheet += readStyleSheet(QLatin1String(theme.resourcePath));
        return styleSheet;
    }
    return QString();
}

void ensureBuiltInThemesExtracted()
{
    const QString directoryPath = themesDirectoryPath();
    if (!QDir().mkpath(directoryPath))
        return;

    const QDir directory(directoryPath);
    for (const BuiltInTheme &theme : builtInThemes) {
        const QString destinationPath = directory.filePath(QLatin1String(theme.fileName));
        if (QFileInfo::exists(destinationPath))
            continue;

        const QString styleSheet = builtInThemeStyleSheet(QLatin1String(theme.displayName));
        if (styleSheet.isEmpty())
            continue;

        QSaveFile destination(destinationPath);
        if (!destination.open(QIODevice::WriteOnly))
            continue;
        destination.write(styleSheet.toUtf8());
        destination.commit();
    }
}

} // anonymous namespace

Preferences *Preferences::mInstance = 0;

Preferences *Preferences::instance()
{
    if (!mInstance)
        mInstance = new Preferences;
    return mInstance;
}

void Preferences::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

bool Preferences::snapToGrid() const
{
    return mSnapToGrid;
}

bool Preferences::showCellBorder() const
{
    return mShowCellBorder;
}

bool Preferences::showCoordinates() const
{
    return mShowCoordinates;
}

bool Preferences::showWorldGrid() const
{
    return mShowWorldGrid;
}

bool Preferences::showCellGrid() const
{
    return mShowCellGrid;
}

bool Preferences::showMiniMap() const
{
    return mShowMiniMap;
}

int Preferences::miniMapWidth() const
{
    return mMiniMapWidth;
}

bool Preferences::highlightCurrentLevel() const
{
    return mHighlightCurrentLevel;
}

Preferences::Preferences()
    : QObject()
    , mSettings(new QSettings)
{
    // Retrieve interface settings
    mSettings->beginGroup(QLatin1String("Interface"));
    mSnapToGrid = mSettings->value(QLatin1String("SnapToGrid"), true).toBool();
    mShowCellBorder = mSettings->value(QLatin1String("ShowCellBorder"), true).toBool();
    mShowCoordinates = mSettings->value(QLatin1String("ShowCoordinates"), true).toBool();
    mShowWorldGrid = mSettings->value(QLatin1String("ShowWorldGrid"), true).toBool();
    mShowCellGrid = mSettings->value(QLatin1String("ShowCellGrid"), false).toBool();
    mGridColor = QColor(mSettings->value(QLatin1String("GridColor"),
                                         QColor(Qt::black).name()).toString());
    mGridWidth = qBound(1, mSettings->value(QLatin1String("GridWidth"), 1).toInt(), 10);
    mThumbnailWidth = qBound(32, mSettings->value(QLatin1String("ThumbnailWidth"), 512).toInt(), 8192);
    mTerrainImageMemoryLimitMiB = qBound(
                128,
                mSettings->value(
                    QLatin1String("TerrainImageMemoryLimitMiB"), 512).toInt(),
                65536);
    // Reopening every cell is expensive and used to crash when a saved cell
    // could no longer be reconstructed. It remains available as an opt-in.
    mRestoreLastSession = mSettings->value(QLatin1String("RestoreLastSession"), false).toBool();
    mRoadSimplificationHighway = qBound(0.0, mSettings->value(
            QLatin1String("RoadSimplificationHighway"), 2.0).toDouble(), 32.0);
    mRoadPointSpacingHighway = qBound(1, mSettings->value(
            QLatin1String("RoadPointSpacingHighway"), 40).toInt(), 300);
    mRoadSimplificationTrail = qBound(0.0, mSettings->value(
            QLatin1String("RoadSimplificationTrail"), 2.0).toDouble(), 32.0);
    mRoadPointSpacingTrail = qBound(1, mSettings->value(
            QLatin1String("RoadPointSpacingTrail"), 40).toInt(), 300);
    mRoadSimplificationRailway = qBound(0.0, mSettings->value(
            QLatin1String("RoadSimplificationRailway"), 2.0).toDouble(), 32.0);
    mRoadPointSpacingRailway = qBound(1, mSettings->value(
            QLatin1String("RoadPointSpacingRailway"), 40).toInt(), 300);
    mShowObjects = mSettings->value(QLatin1String("ShowObjects"), true).toBool();
    mShowObjectNames = mSettings->value(QLatin1String("ShowObjectNames"), true).toBool();
    mShowBMPs = mSettings->value(QLatin1String("ShowBMPs"), true).toBool();
    mShowMiniMap = mSettings->value(QLatin1String("ShowMiniMap"), true).toBool();
    mShowZombieSpawnImage = mSettings->value(QLatin1String("ShowZombieSpawnImage"), false).toBool();
    mZombieSpawnImageOpacity = mSettings->value(QLatin1String("ZombieSpawnImageOpacity"), 0.8).toReal();
    mShowBiomeMap = mSettings->value(QLatin1String("ShowBiomeMap"), false).toBool();
    mBiomeMapOpacity = mSettings->value(QLatin1String("BiomeMapOpacity"), 0.65).toReal();
    mShowZonesInWorldView = mSettings->value(QLatin1String("ShowZonesInWorldView"), false).toBool();
    mMiniMapWidth = mSettings->value(QLatin1String("MiniMapWidth"), 256).toInt();
    mHighlightCurrentLevel = mSettings->value(QLatin1String("HighlightCurrentLevel"),
                                              false).toBool();
    mHighlightRoomUnderPointer = mSettings->value(QLatin1String("HighlightRoomUnderPointer"),
                                                  false).toBool();
    mShowLotFloorsOnly = mSettings->value(QLatin1String("ShowLotFloorsOnly"), false).toBool();
    mShowOtherWorlds = mSettings->value(QLatin1String("ShowOtherWorlds"), true).toBool();
    mUseOpenGL = mSettings->value(QLatin1String("OpenGL"), false).toBool();
    mLoadAllWorldThumbnails = mSettings->value(QLatin1String("LoadAllWorldThumbnails"), false).toBool();
    mShowWorldThumbnails = mSettings->value(QLatin1String("ShowWorldThumbnails"), true).toBool();
    mShowAdjacentMaps = mSettings->value(QLatin1String("ShowAdjacentMaps"), true).toBool();
    // Invisible helper tiles can cover an otherwise healthy map with their
    // wireframe placeholder. Keep them available from View, but do not enable
    // them automatically for a new portable installation.
    mShowInvisibleTiles = mSettings->value(QLatin1String("ShowInvisibleTiles"), false).toBool();
    mTheme = mSettings->value(QLatin1String("Theme"), QLatin1String("Default")).toString();
    if (PortableSettings::syncThemeAcrossApplications())
        mTheme = PortableSettings::sharedTheme(mTheme);
    mSettings->endGroup();

    mSettings->beginGroup(QLatin1String("MapsDirectory"));
    mMapsDirectory = mSettings->value(QLatin1String("Current"), QString()).toString();
    mSettings->endGroup();

    mTilesDirectory = PortableSettings::sharedTilesPath();
    mSettings->remove(QLatin1String("TilesDirectory"));

    // Use the same .tiles files as TileZed.
    QSettings settings(QLatin1String("TheIndieStone"), QLatin1String("TileZed"));
    mTilePropertiesFiles = settings.value(QLatin1String("TilePropertiesFiles")).toStringList();

    mOpenFileDirectory = mSettings->value(QLatin1String("OpenFileDirectory")).toString();
    mWorldMapXMLFile = mSettings->value(QLatin1String("WorldMapXMLFile")).toString();

    mConfigDirectory = PortableSettings::sharedConfigurationPath();
    qInfo().noquote() << "Effective shared configuration directory"
                      << QDir::toNativeSeparators(mConfigDirectory);
    qInfo().noquote() << "Effective Tiles directory"
                      << QDir::toNativeSeparators(mTilesDirectory);

    // Use the same directory as TileZed.
    mThumbnailsDirectory = settings.value(QLatin1String("Thumbnails/Directory")).toString();
}

Preferences::~Preferences()
{
    delete mSettings;
}

QString Preferences::userPath() const
{
    return PortableSettings::rootPath();
}

QString Preferences::userPath(const QString &fileName) const
{
    return userPath() + QLatin1Char('/') + fileName;
}

QString Preferences::configPath() const
{
    return mConfigDirectory;
}

QString Preferences::configPath(const QString &fileName) const
{
    return configPath() + QLatin1Char('/') + fileName;
}

QString Preferences::appConfigPath() const
{
#ifdef Q_OS_WIN
    return PortableSettings::applicationConfigPath();
#elif defined(Q_OS_MAC)
    return QCoreApplication::applicationDirPath() + QLatin1String("/../Config");
#elif defined(Q_OS_UNIX)
    return QCoreApplication::applicationDirPath() + QLatin1String("/../../TileZed/share/tilezed/config");
#else
#error "Unsupported platform: implement the WorldEd configuration path."
#endif
}

QString Preferences::appConfigPath(const QString &fileName) const
{
    const QString localFile = configPath(fileName);
    if (QFileInfo::exists(localFile))
        return localFile;
    return appConfigPath() + QLatin1Char('/') + fileName;
}

QString Preferences::docsPath() const
{
#ifdef Q_OS_WIN
    return PortableSettings::installPath(QLatin1String("docs"));
#elif defined(Q_OS_MAC)
    return QCoreApplication::applicationDirPath() + QLatin1String("/../Docs");
#elif defined(Q_OS_UNIX)
    return QCoreApplication::applicationDirPath() + QLatin1String("/../share/tilezed/docs");
#else
#error "Unsupported platform: implement the WorldEd documentation path."
#endif
}

QString Preferences::docsPath(const QString &fileName) const
{
    return docsPath() + QLatin1Char('/') + fileName;
}

QString Preferences::luaPath() const
{
#ifdef Q_OS_WIN
    return PortableSettings::installPath(QLatin1String("lua"));
#elif defined(Q_OS_MAC)
    return QCoreApplication::applicationDirPath() + QLatin1String("/../Lua");
#elif defined(Q_OS_UNIX)
    return QCoreApplication::applicationDirPath() + QLatin1String("/../share/tilezed/lua");
#else
#error "Unsupported platform: implement the WorldEd Lua path."
#endif
}

QString Preferences::luaPath(const QString &fileName) const
{
    return luaPath() + QLatin1Char('/') + fileName;
}

QString Preferences::mapsDirectory() const
{
    return mMapsDirectory;
}

void Preferences::setSnapToGrid(bool snapToGrid)
{
    if (snapToGrid == mSnapToGrid)
        return;

    mSnapToGrid = snapToGrid;
    mSettings->setValue(QLatin1String("Interface/SnapToGrid"), mSnapToGrid);
    emit snapToGridChanged(mSnapToGrid);
}

void Preferences::setShowCellBorder(bool showCellBorder)
{
    if (showCellBorder == mShowCellBorder)
        return;

    mShowCellBorder = showCellBorder;
    mSettings->setValue(QLatin1String("Interface/ShowCellBorder"), mShowCellBorder);
    emit showCellBorderChanged(mShowCellBorder);
}

void Preferences::setShowCoordinates(bool showCoords)
{
    if (showCoords == mShowCoordinates)
        return;

    mShowCoordinates = showCoords;
    mSettings->setValue(QLatin1String("Interface/ShowCoordinates"), mShowCoordinates);
    emit showCoordinatesChanged(mShowCoordinates);
}

void Preferences::setShowWorldGrid(bool showGrid)
{
    if (showGrid == mShowWorldGrid)
        return;

    mShowWorldGrid = showGrid;
    mSettings->setValue(QLatin1String("Interface/ShowWorldGrid"), mShowWorldGrid);
    emit showWorldGridChanged(mShowWorldGrid);
}

void Preferences::setShowCellGrid(bool showGrid)
{
    if (showGrid == mShowCellGrid)
        return;

    mShowCellGrid = showGrid;
    mSettings->setValue(QLatin1String("Interface/ShowCellGrid"), mShowCellGrid);
    emit showCellGridChanged(mShowCellGrid);
}

void Preferences::setGridColor(const QColor &gridColor)
{
    if (mGridColor == gridColor)
        return;

    mGridColor = gridColor;
    mSettings->setValue(QLatin1String("Interface/GridColor"), mGridColor.name());
    emit gridColorChanged(mGridColor);
}

void Preferences::setGridWidth(int width)
{
    width = qBound(1, width, 10);
    if (mGridWidth == width)
        return;

    mGridWidth = width;
    mSettings->setValue(QLatin1String("Interface/GridWidth"), mGridWidth);
    emit gridWidthChanged(mGridWidth);
}

void Preferences::setThumbnailWidth(int width)
{
    width = qBound(32, width, 8192);
    if (mThumbnailWidth == width)
        return;

    mThumbnailWidth = width;
    mSettings->setValue(QLatin1String("Interface/ThumbnailWidth"), mThumbnailWidth);
    emit thumbnailWidthChanged(mThumbnailWidth);
}

void Preferences::setTerrainImageMemoryLimitMiB(int limitMiB)
{
    limitMiB = qBound(128, limitMiB, 65536);
    if (mTerrainImageMemoryLimitMiB == limitMiB)
        return;

    mTerrainImageMemoryLimitMiB = limitMiB;
    mSettings->setValue(
                QLatin1String("Interface/TerrainImageMemoryLimitMiB"),
                mTerrainImageMemoryLimitMiB);
}

void Preferences::setRestoreLastSession(bool restore)
{
    if (mRestoreLastSession == restore)
        return;

    mRestoreLastSession = restore;
    mSettings->setValue(QLatin1String("Interface/RestoreLastSession"), mRestoreLastSession);
}

void Preferences::setRoadSimplificationHighway(qreal tolerance)
{
    mRoadSimplificationHighway = qBound(0.0, double(tolerance), 32.0);
    mSettings->setValue(QLatin1String("Interface/RoadSimplificationHighway"),
                        mRoadSimplificationHighway);
}

void Preferences::setRoadPointSpacingHighway(int spacing)
{
    mRoadPointSpacingHighway = qBound(1, spacing, 300);
    mSettings->setValue(QLatin1String("Interface/RoadPointSpacingHighway"),
                        mRoadPointSpacingHighway);
}

void Preferences::setRoadSimplificationTrail(qreal tolerance)
{
    mRoadSimplificationTrail = qBound(0.0, double(tolerance), 32.0);
    mSettings->setValue(QLatin1String("Interface/RoadSimplificationTrail"),
                        mRoadSimplificationTrail);
}

void Preferences::setRoadPointSpacingTrail(int spacing)
{
    mRoadPointSpacingTrail = qBound(1, spacing, 300);
    mSettings->setValue(QLatin1String("Interface/RoadPointSpacingTrail"),
                        mRoadPointSpacingTrail);
}

void Preferences::setRoadSimplificationRailway(qreal tolerance)
{
    mRoadSimplificationRailway = qBound(0.0, double(tolerance), 32.0);
    mSettings->setValue(QLatin1String("Interface/RoadSimplificationRailway"),
                        mRoadSimplificationRailway);
}

void Preferences::setRoadPointSpacingRailway(int spacing)
{
    mRoadPointSpacingRailway = qBound(1, spacing, 300);
    mSettings->setValue(QLatin1String("Interface/RoadPointSpacingRailway"),
                        mRoadPointSpacingRailway);
}

void Preferences::setUseOpenGL(bool useOpenGL)
{
    if (mUseOpenGL == useOpenGL)
        return;

    mUseOpenGL = useOpenGL;
    mSettings->setValue(QLatin1String("Interface/OpenGL"), mUseOpenGL);

    emit useOpenGLChanged(mUseOpenGL);
}

void Preferences::setLoadAllWorldThumbnails(bool thumbs)
{
    if (mLoadAllWorldThumbnails == thumbs)
        return;

    mLoadAllWorldThumbnails = thumbs;
    mSettings->setValue(QLatin1String("Interface/LoadAllWorldThumbnails"), mLoadAllWorldThumbnails);

    emit loadAllWorldThumbnailsChanged(mLoadAllWorldThumbnails);
}

void Preferences::setShowWorldThumbnails(bool thumbs)
{
    if (mShowWorldThumbnails == thumbs)
        return;

    mShowWorldThumbnails = thumbs;
    mSettings->setValue(QLatin1String("Interface/ShowWorldThumbnails"), mShowWorldThumbnails);

    emit showWorldThumbnailsChanged(mShowWorldThumbnails);
}

QString Preferences::openFileDirectory() const
{
    return mOpenFileDirectory;
}

void Preferences::setOpenFileDirectory(const QString &path)
{
    if (mOpenFileDirectory == path)
        return;
    mOpenFileDirectory = path;
    mSettings->setValue(QLatin1String("OpenFileDirectory"), mOpenFileDirectory);
}

QString Preferences::worldMapXMLFile() const
{
    return mWorldMapXMLFile;
}

void Preferences::setWorldMapXMLFile(const QString &path)
{
    if (mWorldMapXMLFile == path)
        return;
    mWorldMapXMLFile = path;
    mSettings->setValue(QLatin1String("WorldMapXMLFile"), mWorldMapXMLFile);
}

void Preferences::setShowAdjacentMaps(bool show)
{
    if (mShowAdjacentMaps == show)
        return;

    mShowAdjacentMaps = show;
    mSettings->setValue(QLatin1String("Interface/ShowAdjacentMaps"), mShowAdjacentMaps);

    emit showAdjacentMapsChanged(mShowAdjacentMaps);
}

void Preferences::setShowObjects(bool show)
{
    if (mShowObjects == show)
        return;

    mShowObjects = show;
    mSettings->setValue(QLatin1String("Interface/ShowObjects"), mShowObjects);

    emit showObjectsChanged(mShowObjects);
}

void Preferences::setShowObjectNames(bool show)
{
    if (mShowObjectNames == show)
        return;

    mShowObjectNames = show;
    mSettings->setValue(QLatin1String("Interface/ShowObjectNames"), mShowObjectNames);

    emit showObjectNamesChanged(mShowObjectNames);
}

void Preferences::setShowBMPs(bool show)
{
    if (mShowBMPs == show)
        return;

    mShowBMPs = show;
    mSettings->setValue(QLatin1String("Interface/ShowBMPs"), mShowBMPs);

    emit showBMPsChanged(mShowBMPs);
}

void Preferences::setShowZombieSpawnImage(bool show)
{
    if (mShowZombieSpawnImage == show)
        return;

    mShowZombieSpawnImage = show;
    mSettings->setValue(QLatin1String("Interface/ShowZombieSpawnImage"), mShowZombieSpawnImage);

    emit showZombieSpawnImageChanged(mShowZombieSpawnImage);
}

void Preferences::setZombieSpawnImageOpacity(qreal opacity)
{
    opacity = qMin(opacity, 1.0);
    opacity = qMax(opacity, 0.0);

    if (mZombieSpawnImageOpacity == opacity)
        return;

    mZombieSpawnImageOpacity = opacity;
    mSettings->setValue(QLatin1String("Interface/ZombieSpawnImageOpacity"), mZombieSpawnImageOpacity);

    emit zombieSpawnImageOpacityChanged(mZombieSpawnImageOpacity);
}

void Preferences::setShowBiomeMap(bool show)
{
    if (mShowBiomeMap == show)
        return;

    mShowBiomeMap = show;
    mSettings->setValue(QLatin1String("Interface/ShowBiomeMap"), mShowBiomeMap);
    emit showBiomeMapChanged(mShowBiomeMap);
}

void Preferences::setBiomeMapOpacity(qreal opacity)
{
    opacity = qBound(0.0, double(opacity), 1.0);
    if (mBiomeMapOpacity == opacity)
        return;

    mBiomeMapOpacity = opacity;
    mSettings->setValue(QLatin1String("Interface/BiomeMapOpacity"), mBiomeMapOpacity);
    emit biomeMapOpacityChanged(mBiomeMapOpacity);
}

void Preferences::setShowZonesInWorldView(bool show)
{
    if (mShowZonesInWorldView == show)
        return;

    mShowZonesInWorldView = show;
    mSettings->setValue(QLatin1String("Interface/ShowZonesInWorldView"), mShowZonesInWorldView);

    emit showZonesInWorldViewChanged(mShowZonesInWorldView);
}

void Preferences::setShowMiniMap(bool show)
{
    if (show == mShowMiniMap)
        return;

    mShowMiniMap = show;
    mSettings->setValue(QLatin1String("Interface/ShowMiniMap"), mShowMiniMap);
    emit showMiniMapChanged(mShowMiniMap);
}

void Preferences::setMiniMapWidth(int width)
{
    width = qMin(width, MINIMAP_WIDTH_MAX);
    width = qMax(width, MINIMAP_WIDTH_MIN);

    if (mMiniMapWidth == width)
        return;
    mMiniMapWidth = width;
    mSettings->setValue(QLatin1String("Interface/MiniMapWidth"), width);
    emit miniMapWidthChanged(mMiniMapWidth);
}

void Preferences::setHighlightCurrentLevel(bool highlight)
{
    if (highlight == mHighlightCurrentLevel)
        return;

    mHighlightCurrentLevel = highlight;
    mSettings->setValue(QLatin1String("Interface/HighlightCurrentLevel"), mHighlightCurrentLevel);
    emit highlightCurrentLevelChanged(mHighlightCurrentLevel);
}

void Preferences::setHighlightRoomUnderPointer(bool highlight)
{
    if (highlight == mHighlightRoomUnderPointer)
        return;
    mHighlightRoomUnderPointer = highlight;
    mSettings->setValue(QLatin1String("Interface/HighlightRoomUnderPointer"),
                        mHighlightRoomUnderPointer);
    emit highlightRoomUnderPointerChanged(mHighlightRoomUnderPointer);
}

void Preferences::setShowLotFloorsOnly(bool show)
{
    if (mShowLotFloorsOnly == show)
        return;
    mShowLotFloorsOnly = show;
    mSettings->setValue(QLatin1String("Interface/ShowLotFloorsOnly"), show);
    emit showLotFloorsOnlyChanged(mShowLotFloorsOnly);
}

void Preferences::setShowOtherWorlds(bool show)
{
    if (show == mShowOtherWorlds)
        return;
    mShowOtherWorlds = show;
    mSettings->setValue(QLatin1String("Interface/ShowOtherWorlds"),
                        mShowOtherWorlds);
    emit showOtherWorldsChanged(mShowOtherWorlds);
}

void Preferences::setMapsDirectory(const QString &path)
{
    if (mMapsDirectory == path)
        return;
    mMapsDirectory = path;
    mSettings->setValue(QLatin1String("MapsDirectory/Current"), path);

    // Put this up, otherwise the progress dialog shows and hides for each lot.
    // Since each open document has its own ZLotManager, this shows and hides for each document as well.
//    ZProgressManager::instance()->begin(QLatin1String("Checking lots..."));

    emit mapsDirectoryChanged();
}

QString Preferences::tilesDirectory() const
{
    return mTilesDirectory;
}

void Preferences::setTilesDirectory(const QString &path)
{
    const QString normalizedPath =
            PortableSettings::normalizedTilesPath(path);
    if (mTilesDirectory == normalizedPath)
        return;
    mTilesDirectory = normalizedPath;
    PortableSettings::setSharedTilesPath(normalizedPath);
    emit tilesDirectoryChanged();
}

QString Preferences::tiles2xDirectory() const
{
    if (mTilesDirectory.isEmpty())
        return QString();
    return mTilesDirectory + QLatin1Char('/') + QLatin1String("2x");
}

QString Preferences::texturesDirectory() const
{
    return QDir(mTilesDirectory).filePath(QLatin1String("Textures"));
}

void Preferences::setShowInvisibleTiles(bool show)
{
    if (mShowInvisibleTiles == show)
        return;

    mShowInvisibleTiles = show;
    mSettings->setValue(QLatin1String("Interface/ShowInvisibleTiles"), mShowInvisibleTiles);

    emit showInvisibleTilesChanged(mShowInvisibleTiles);
}

void Preferences::setTheme(const QString &theme)
{
    if (mTheme == theme) {
        if (PortableSettings::syncThemeAcrossApplications())
            PortableSettings::setThemeForAllApplications(theme);
        return;
    }
    mTheme = theme;
    applyTheme();
    if (PortableSettings::syncThemeAcrossApplications())
        PortableSettings::setThemeForAllApplications(theme);
}

QStringList Preferences::availableThemes() const
{
    ensureBuiltInThemesExtracted();

    QStringList themes;
    themes << QStringLiteral("Default")
           << QStringLiteral("Breeze (Dark)")
           << QStringLiteral("Breeze (Dark Blue)")
           << QStringLiteral("QDarkStyle (Dark)")
           << QStringLiteral("QDarkStyle (Light)")
           << QStringLiteral("Mapping Discord (B42)");

    QStringList externalThemes;
    const QString applicationDirectory = PortableSettings::installRootPath();
    const QStringList themeDirectories = {
        QDir(applicationDirectory).filePath(QStringLiteral("themes")),
        QDir(applicationDirectory).filePath(QStringLiteral("theme"))
    };
    for (const QString &directoryPath : themeDirectories) {
        const QDir directory(directoryPath);
        const QStringList files = directory.entryList(QDir::Files | QDir::Readable,
                                                       QDir::Name | QDir::IgnoreCase);
        for (const QString &fileName : files) {
            if (QFileInfo(fileName).suffix().compare(QStringLiteral("qss"), Qt::CaseInsensitive) != 0)
                continue;
            if (isBuiltInThemeFile(fileName))
                continue;
            if (!themes.contains(fileName, Qt::CaseInsensitive) &&
                    !externalThemes.contains(fileName, Qt::CaseInsensitive)) {
                externalThemes.append(fileName);
            }
        }
    }
    externalThemes.sort(Qt::CaseInsensitive);
    themes.append(externalThemes);
    return themes;
}

void Preferences::applyTheme() const
{
    ensureBuiltInThemesExtracted();

    mSettings->setValue(QLatin1String("Interface/Theme"), mTheme);
    if (mTheme == QStringLiteral("Default")) {
        qApp->setStyleSheet(QString());
        return;
    }
    QString styleSheet;
    bool styleSheetLoaded = false;
    QString resource = builtInThemeResource(mTheme);
    if (!resource.isEmpty()) {
        const QString localPath = builtInThemePath(mTheme);
        if (QFileInfo::exists(localPath)) {
            resource = localPath;
        } else {
            styleSheet = builtInThemeStyleSheet(mTheme);
            styleSheetLoaded = !styleSheet.isEmpty();
            resource.clear();
        }
    } else {
        const QString applicationDirectory = PortableSettings::installRootPath();
        const QStringList themeDirectories = {
            QDir(applicationDirectory).filePath(QStringLiteral("themes")),
            QDir(applicationDirectory).filePath(QStringLiteral("theme"))
        };
        for (const QString &directoryPath : themeDirectories) {
            const QDir directory(directoryPath);
            const QStringList files = directory.entryList(QDir::Files | QDir::Readable);
            for (const QString &fileName : files) {
                if (fileName.compare(mTheme, Qt::CaseInsensitive) == 0 &&
                        QFileInfo(fileName).suffix().compare(QStringLiteral("qss"), Qt::CaseInsensitive) == 0) {
                    resource = directory.filePath(fileName);
                    break;
                }
            }
            if (!resource.isEmpty())
                break;
        }
    }
    if (resource.isEmpty() && !styleSheetLoaded) {
        qApp->setStyleSheet(QString());
        return;
    }

    if (!resource.isEmpty()) {
        styleSheet = readStyleSheet(resource);
        styleSheetLoaded = !styleSheet.isEmpty();
    }
    qApp->setStyleSheet(styleSheetLoaded ? styleSheet : QString());
}
