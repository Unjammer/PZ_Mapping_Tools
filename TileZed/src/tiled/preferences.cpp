/*
 * preferences.cpp
 * Copyright 2009-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
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

#include "documentmanager.h"
#include "languagemanager.h"
#include "tilesetmanager.h"
#include "../portablesettings.h"
#ifdef ZOMBOID
#include "zprogress.h"
#endif

#ifdef ZOMBOID
#include <QApplication>
#include <QDir>
#include <QPalette>
#include <QSaveFile>
#include <QTextStream>
#endif
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QSettings>

using namespace Tiled;
using namespace Tiled::Internal;

#ifdef ZOMBOID
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
QPalette defaultApplicationPalette()
{
    static const QPalette palette = qApp->palette();
    return palette;
}
QPalette applicationPaletteForTheme(const QString &themeName)
{
    QPalette palette = defaultApplicationPalette();
    QColor window;
    QColor base;
    QColor alternate;
    QColor text;
    QColor muted;
    QColor highlight;
    QColor highlightedText;
    if (themeName == QLatin1String("Breeze (Dark)")
            || themeName == QLatin1String("Breeze (Dark Blue)")) {
        window = QColor(QStringLiteral("#31363b"));
        base = QColor(QStringLiteral("#1d2023"));
        alternate = QColor(QStringLiteral("#2c3034"));
        text = QColor(QStringLiteral("#eff0f1"));
        muted = QColor(QStringLiteral("#76797c"));
        highlight = QColor(QStringLiteral("#3daee9"));
        highlightedText = text;
    } else if (themeName == QLatin1String("QDarkStyle (Dark)")) {
        window = QColor(QStringLiteral("#19232d"));
        base = QColor(QStringLiteral("#37414f"));
        alternate = QColor(QStringLiteral("#455364"));
        text = QColor(QStringLiteral("#dfe1e2"));
        muted = QColor(QStringLiteral("#788d9c"));
        highlight = QColor(QStringLiteral("#1a72bb"));
        highlightedText = text;
    } else if (themeName == QLatin1String("Mapping Discord (B42)")) {
        window = QColor(QStringLiteral("#141c17"));
        base = QColor(QStringLiteral("#101712"));
        alternate = QColor(QStringLiteral("#19231c"));
        text = QColor(QStringLiteral("#eef2ef"));
        muted = QColor(QStringLiteral("#9da9a1"));
        highlight = QColor(QStringLiteral("#4e8f61"));
        highlightedText = text;
    } else {
        return palette;
    }
    const QPalette::ColorGroup groups[] = {
        QPalette::Active, QPalette::Inactive, QPalette::Disabled
    };
    for (QPalette::ColorGroup group : groups) {
        const QColor groupText = group == QPalette::Disabled ? muted : text;
        palette.setColor(group, QPalette::Window, window);
        palette.setColor(group, QPalette::WindowText, groupText);
        palette.setColor(group, QPalette::Base, base);
        palette.setColor(group, QPalette::AlternateBase, alternate);
        palette.setColor(group, QPalette::ToolTipBase, base);
        palette.setColor(group, QPalette::ToolTipText, groupText);
        palette.setColor(group, QPalette::Text, groupText);
        palette.setColor(group, QPalette::Button, window);
        palette.setColor(group, QPalette::ButtonText, groupText);
        palette.setColor(group, QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
        palette.setColor(group, QPalette::Link, highlight);
        palette.setColor(group, QPalette::Highlight, highlight);
        palette.setColor(group, QPalette::HighlightedText, highlightedText);
    }
    return palette;
}
void ensureBuiltInThemesExtracted()
{
    const QString directoryPath = themesDirectoryPath();
    if (!QDir().mkpath(directoryPath))
        return;
    const QDir directory(directoryPath);
    for (const BuiltInTheme &theme : builtInThemes) {
        const QString destinationPath = directory.filePath(QLatin1String(theme.fileName));
        const QString styleSheet = builtInThemeStyleSheet(QLatin1String(theme.displayName));
        if (styleSheet.isEmpty())
            continue;
        QFile existing(destinationPath);
        if (existing.open(QIODevice::ReadOnly)
                && existing.readAll() == styleSheet.toUtf8()) {
            continue;
        }
        existing.close();
        QSaveFile destination(destinationPath);
        if (!destination.open(QIODevice::WriteOnly))
            continue;
        destination.write(styleSheet.toUtf8());
        destination.commit();
    }
}
}
#endif
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

Preferences::Preferences()
    : mSettings(new QSettings(QSettings::IniFormat, QSettings::UserScope,
                              QLatin1String("TheIndieStone"),
                              QLatin1String("TileZed")))
{
    // Retrieve storage settings
    mSettings->beginGroup(QLatin1String("Storage"));
    mLayerDataFormat = (MapWriter::LayerDataFormat)
                       mSettings->value(QLatin1String("LayerDataFormat"),
                                        MapWriter::Base64Zlib).toInt();
    mDtdEnabled = mSettings->value(QLatin1String("DtdEnabled")).toBool();
    mReloadTilesetsOnChange =
            mSettings->value(QLatin1String("ReloadTilesets"), true).toBool();
    mSettings->endGroup();

    // Retrieve interface settings
    mSettings->beginGroup(QLatin1String("Interface"));
    mShowGrid = mSettings->value(QLatin1String("ShowGrid"), false).toBool();
    mSnapToGrid = mSettings->value(QLatin1String("SnapToGrid"),
                                   false).toBool();
    mGridColor = QColor(mSettings->value(QLatin1String("GridColor"),
                                  QColor(Qt::black).name()).toString());
    mHighlightCurrentLayer = mSettings->value(QLatin1String("HighlightCurrentLayer"),
                                              false).toBool();
    mShowTilesetGrid = mSettings->value(QLatin1String("ShowTilesetGrid"),
                                        true).toBool();
    mLanguage = mSettings->value(QLatin1String("Language"),
                                 QString()).toString();
    mUseOpenGL = false;
    mSettings->setValue(QLatin1String("OpenGL"), false);
#ifdef ZOMBOID
    mAutoSwitchLayer = mSettings->value(QLatin1String("AutoSwitchLayer"), true).toBool();
    mTilesetScale = mSettings->value(QLatin1String("TilesetScale"), 1.0).toReal();
    mSortTilesets = mSettings->value(QLatin1String("SortTilesets"), false).toBool();
    mShowLotFloorsOnly = mSettings->value(QLatin1String("ShowLotFloorsOnly"), false).toBool();
    mShowMiniMap = mSettings->value(QLatin1String("ShowMiniMap"), true).toBool();
    mMiniMapWidth = mSettings->value(QLatin1String("MiniMapWidth"), 256).toInt();
    mShowTileIds = mSettings->value(QLatin1String("ShowTileIds"), true).toBool();
    mShowTileLayersPanel = mSettings->value(QLatin1String("ShowTileLayersPanel"), true).toBool();
    mShowTileSelection = mSettings->value(QLatin1String("ShowTileSelection"), true).toBool();
    mShowInvisibleTiles = mSettings->value(QLatin1String("ShowInvisibleTiles"), true).toBool();
    mBackgroundColor = QColor(mSettings->value(QLatin1String("BackgroundColor"),
                                               QColor(Qt::darkGray).name()).toString());
    mShowAdjacentMaps = mSettings->value(QLatin1String("ShowAdjacentMaps"), true).toBool();
    mRestoreLastSession = mSettings->value(QLatin1String("RestoreLastSession"), true).toBool();
    mAutoSaveIntervalMinutes = mSettings->value(
                QLatin1String("AutoSaveIntervalMinutes"), 0).toInt();
    if (!QList<int>({0, 1, 5, 10, 20, 60}).contains(
                mAutoSaveIntervalMinutes))
        mAutoSaveIntervalMinutes = 0;
    mHighlightRoomUnderPointer = mSettings->value(QLatin1String("HighlightRoomUnderPointer"), false).toBool();
    mTilesetBackgroundColorIsDefault =
            !mSettings->contains(QLatin1String("TilesetBackgroundColor"));
    mTilesetBackgroundColor = QColor(mSettings->value(QLatin1String("TilesetBackgroundColor"), QColor(Qt::white).name()).toString());
    mShowCellBorder = mSettings->value(QLatin1String("ShowCelLBorder"), true).toBool();
    mTheme = mSettings->value(QLatin1String("Theme"), QLatin1String("Default")).toString();
    if (PortableSettings::syncThemeAcrossApplications())
        mTheme = PortableSettings::sharedTheme(mTheme);
#endif
    mSettings->endGroup();
#ifdef ZOMBOID
    mEraserBrushSize = mSettings->value(QLatin1String("Tools/Eraser/BrushSize"), 1).toInt();
#endif

    // Retrieve defined object types
    mSettings->beginGroup(QLatin1String("ObjectTypes"));
    const QStringList names =
            mSettings->value(QLatin1String("Names")).toStringList();
    const QStringList colors =
            mSettings->value(QLatin1String("Colors")).toStringList();
    mSettings->endGroup();

    const int count = qMin(names.size(), colors.size());
    for (int i = 0; i < count; ++i)
        mObjectTypes.append(ObjectType(names.at(i), QColor(colors.at(i))));

    mSettings->beginGroup(QLatin1String("Automapping"));
    mAutoMapDrawing = mSettings->value(QLatin1String("WhileDrawing"),
                                       false).toBool();
    mSettings->endGroup();

#ifdef ZOMBOID
    mTilesDirectory = PortableSettings::sharedTilesPath();
    mProjectZomboidDirectory = PortableSettings::sharedGamePath();
    mSettings->remove(QLatin1String("Tilesets/TilesDirectory"));

    mSettings->beginGroup(QLatin1String("MapsDirectory"));
    mMapsDirectory = mSettings->value(QLatin1String("Current"), QString()).toString();
    mSettings->endGroup();

    mConfigDirectory = PortableSettings::sharedConfigurationPath();
    mSettings->remove(QLatin1String("ConfigDirectory"));
    qInfo().noquote() << "Effective shared configuration directory"
                      << QDir::toNativeSeparators(mConfigDirectory);
    qInfo().noquote() << "Effective Tiles directory"
                      << QDir::toNativeSeparators(mTilesDirectory);
    qInfo().noquote() << "Effective Project Zomboid directory"
                      << (mProjectZomboidDirectory.isEmpty()
                          ? QLatin1String("<not configured>")
                          : QDir::toNativeSeparators(
                                mProjectZomboidDirectory));
    qInfo().noquote() << "Portable basement source directory"
                      << QDir::toNativeSeparators(
                             PortableSettings::basementSourcePath())
                      << (QDir(PortableSettings::basementSourcePath()).exists()
                          ? QLatin1String("[available]")
                          : QLatin1String("[missing]"));
    qInfo().noquote() << "Portable basement PZBY directory"
                      << QDir::toNativeSeparators(
                             PortableSettings::basementBinMapPath())
                      << (QDir(PortableSettings::basementBinMapPath()).exists()
                          ? QLatin1String("[available]")
                          : QLatin1String("[missing]"));

    mThumbnailsDirectory = mSettings->value(QLatin1String("Thumbnails/Directory"), QString()).toString();

    mWorldEdFiles = mSettings->value(QLatin1String("WorldEd/ProjectFile")).toStringList();
    mTilePropertiesFiles = mSettings->value(QLatin1String("TilePropertiesFiles")).toStringList();

    const QStringList builtInTileDefs = {
        QStringLiteral("newtiledefinitions.tiles"),
        QStringLiteral("tiledefinitions_erosion.tiles"),
        QStringLiteral("tiledefinitions_overlays.tiles"),
        QStringLiteral("tiledefinitions_b42chunkcaching.tiles"),
        QStringLiteral("tiledefinitions_noiseworks.patch.tiles"),
        QStringLiteral("jumbo_trees_big.tiles"),
        QStringLiteral("jumbo_trees.tiles")
    };
    QStringList orderedTileDefs;
    for (const QString &builtInName : builtInTileDefs) {
        QString path;
        for (const QString &configuredPath : qAsConst(mTilePropertiesFiles)) {
            if (QFileInfo(configuredPath).fileName().compare(
                        builtInName, Qt::CaseInsensitive) == 0) {
                path = configuredPath;
                break;
            }
        }
        if (path.isEmpty() && !mProjectZomboidDirectory.isEmpty()) {
            const QFileInfo candidate(QDir(mProjectZomboidDirectory)
                                      .filePath(QStringLiteral("media/") +
                                                builtInName));
            if (candidate.exists() && candidate.isFile())
                path = candidate.canonicalFilePath();
        }
        if (path.isEmpty() && !mTilesDirectory.isEmpty()) {
            const QFileInfo candidate(QDir(mTilesDirectory).filePath(
                                          builtInName));
            if (candidate.exists())
                path = candidate.canonicalFilePath();
        }
        if (!path.isEmpty())
            orderedTileDefs += QDir::toNativeSeparators(path);
    }
    for (const QString &configuredPath : qAsConst(mTilePropertiesFiles)) {
        bool isBuiltIn = false;
        for (const QString &builtInName : builtInTileDefs) {
            if (QFileInfo(configuredPath).fileName().compare(
                        builtInName, Qt::CaseInsensitive) == 0) {
                isBuiltIn = true;
                break;
            }
        }
        if (!isBuiltIn && !configuredPath.isEmpty()
                && !orderedTileDefs.contains(configuredPath))
            orderedTileDefs += configuredPath;
    }
    if (mTilePropertiesFiles != orderedTileDefs) {
        mTilePropertiesFiles = orderedTileDefs;
        mSettings->setValue(QLatin1String("TilePropertiesFiles"),
                            mTilePropertiesFiles);
    }
#endif
#ifndef ZOMBOID // do this in TilesetManager constructor to avoid infinite loop
    TilesetManager *tilesetManager = TilesetManager::instance();
    tilesetManager->setReloadTilesetsOnChange(mReloadTilesetsOnChange);
#endif
}

Preferences::~Preferences()
{
    delete mSettings;
}

void Preferences::setShowGrid(bool showGrid)
{
    if (mShowGrid == showGrid)
        return;

    mShowGrid = showGrid;
    mSettings->setValue(QLatin1String("Interface/ShowGrid"), mShowGrid);
    emit showGridChanged(mShowGrid);
}

void Preferences::setSnapToGrid(bool snapToGrid)
{
    if (mSnapToGrid == snapToGrid)
        return;

    mSnapToGrid = snapToGrid;
    mSettings->setValue(QLatin1String("Interface/SnapToGrid"), mSnapToGrid);
    emit snapToGridChanged(mSnapToGrid);
}

void Preferences::setGridColor(QColor gridColor)
{
    if (mGridColor == gridColor)
        return;

    mGridColor = gridColor;
    mSettings->setValue(QLatin1String("Interface/GridColor"), mGridColor.name());
    emit gridColorChanged(mGridColor);
}

void Preferences::setHighlightCurrentLayer(bool highlight)
{
    if (mHighlightCurrentLayer == highlight)
        return;

    mHighlightCurrentLayer = highlight;
    mSettings->setValue(QLatin1String("Interface/HighlightCurrentLayer"),
                        mHighlightCurrentLayer);
    emit highlightCurrentLayerChanged(mHighlightCurrentLayer);
}

void Preferences::setShowTilesetGrid(bool showTilesetGrid)
{
    if (mShowTilesetGrid == showTilesetGrid)
        return;

    mShowTilesetGrid = showTilesetGrid;
    mSettings->setValue(QLatin1String("Interface/ShowTilesetGrid"),
                        mShowTilesetGrid);
    emit showTilesetGridChanged(mShowTilesetGrid);
}
void Preferences::setShowTileIds(bool show)
{
    if (mShowTileIds == show)
        return;
    mShowTileIds = show;
    mSettings->setValue(QLatin1String("Interface/ShowTileIds"), show);
    emit showTileIdsChanged(show);
}

MapWriter::LayerDataFormat Preferences::layerDataFormat() const
{
    return mLayerDataFormat;
}

void Preferences::setLayerDataFormat(MapWriter::LayerDataFormat
                                     layerDataFormat)
{
    if (mLayerDataFormat == layerDataFormat)
        return;

    mLayerDataFormat = layerDataFormat;
    mSettings->setValue(QLatin1String("Storage/LayerDataFormat"),
                        mLayerDataFormat);
}

bool Preferences::dtdEnabled() const
{
    return mDtdEnabled;
}

void Preferences::setDtdEnabled(bool enabled)
{
    mDtdEnabled = enabled;
    mSettings->setValue(QLatin1String("Storage/DtdEnabled"), enabled);
}

QString Preferences::language() const
{
    return mLanguage;
}

void Preferences::setLanguage(const QString &language)
{
    if (mLanguage == language)
        return;

    mLanguage = language;
    mSettings->setValue(QLatin1String("Interface/Language"),
                        mLanguage);

    LanguageManager::instance()->installTranslators();
}

bool Preferences::reloadTilesetsOnChange() const
{
    return mReloadTilesetsOnChange;
}

void Preferences::setReloadTilesetsOnChanged(bool value)
{
    if (mReloadTilesetsOnChange == value)
        return;

    mReloadTilesetsOnChange = value;
    mSettings->setValue(QLatin1String("Storage/ReloadTilesets"),
                        mReloadTilesetsOnChange);

    TilesetManager *tilesetManager = TilesetManager::instance();
    tilesetManager->setReloadTilesetsOnChange(mReloadTilesetsOnChange);
}

void Preferences::setUseOpenGL(bool useOpenGL)
{
    Q_UNUSED(useOpenGL)
    if (!mUseOpenGL)
        return;

    mUseOpenGL = false;
    mSettings->setValue(QLatin1String("Interface/OpenGL"), false);

    emit useOpenGLChanged(false);
}

void Preferences::setObjectTypes(const ObjectTypes &objectTypes)
{
    mObjectTypes = objectTypes;

    QStringList names;
    QStringList colors;
    foreach (const ObjectType &objectType, objectTypes) {
        names.append(objectType.name);
        colors.append(objectType.color.name());
    }

    mSettings->beginGroup(QLatin1String("ObjectTypes"));
    mSettings->setValue(QLatin1String("Names"), names);
    mSettings->setValue(QLatin1String("Colors"), colors);
    mSettings->endGroup();

    emit objectTypesChanged();
}

static QString lastPathKey(Preferences::FileType fileType)
{
    QString key = QLatin1String("LastPaths/");

    switch (fileType) {
    case Preferences::ObjectTypesFile:
        key.append(QLatin1String("ObjectTypes"));
        break;
    case Preferences::ImageFile:
        key.append(QLatin1String("Images"));
        break;
    case Preferences::ExportedFile:
        key.append(QLatin1String("ExportedFile"));
        break;
    default:
        Q_ASSERT(false); // Getting here means invalid file type
    }

    return key;
}

/**
 * Returns the last location of a file chooser for the given file type. As long
 * as it was set using setLastPath().
 *
 * When no last path for this file type exists yet, the path of the currently
 * selected map is returned.
 *
 * When no map is open, the user's 'Documents' folder is returned.
 */
QString Preferences::lastPath(FileType fileType) const
{
    QString path = mSettings->value(lastPathKey(fileType)).toString();

    if (path.isEmpty()) {
        DocumentManager *documentManager = DocumentManager::instance();
        MapDocument *mapDocument = documentManager->currentDocument();
        if (mapDocument)
            path = QFileInfo(mapDocument->fileName()).path();
    }

    if (path.isEmpty())
        path = PortableSettings::rootPath();

    return path;
}

/**
 * \see lastPath()
 */
void Preferences::setLastPath(FileType fileType, const QString &path)
{
    mSettings->setValue(lastPathKey(fileType), path);
}

void Preferences::setAutomappingDrawing(bool enabled)
{
    mAutoMapDrawing = enabled;
    mSettings->setValue(QLatin1String("Automapping/WhileDrawing"), enabled);
}

#ifdef ZOMBOID
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

void Preferences::reloadSharedPaths()
{
    const QString configuration = PortableSettings::sharedConfigurationPath();
    const QString tiles = PortableSettings::sharedTilesPath();
    const bool configurationChanged = mConfigDirectory != configuration;
    const bool tilesChanged = mTilesDirectory != tiles;
    mConfigDirectory = configuration;
    mTilesDirectory = tiles;
    if (configurationChanged || tilesChanged) {
        qInfo().noquote() << "Synchronized shared paths:"
                         << QDir::toNativeSeparators(mConfigDirectory)
                         << QDir::toNativeSeparators(mTilesDirectory);
    }
    if (tilesChanged)
        emit tilesDirectoryChanged();
}

bool Preferences::validateSharedPaths(QString *errorString)
{
    Preferences *preferences = instance();
    const QString configuration = PortableSettings::sharedConfigurationPath();
    const QString tiles = PortableSettings::sharedTilesPath();
    if (!PortableSettings::isConfigurationPath(configuration)
            || !PortableSettings::isTilesPath(tiles)) {
        *errorString = QStringLiteral("Configure valid shared directories first.");
        return false;
    }
    const QString originalConfiguration = preferences->mConfigDirectory;
    const QString originalTiles = preferences->mTilesDirectory;
    preferences->mConfigDirectory.clear();
    preferences->mTilesDirectory.clear();
    int notifications = 0;
    bool coherentNotification = true;
    const QMetaObject::Connection connection = QObject::connect(
                preferences, &Preferences::tilesDirectoryChanged,
                preferences, [&]() {
        ++notifications;
        coherentNotification = coherentNotification
                && preferences->configPath() == configuration
                && preferences->tilesDirectory() == tiles;
    });
    preferences->reloadSharedPaths();
    const bool refreshed = preferences->configPath() == configuration
            && preferences->tilesDirectory() == tiles
            && notifications == 1 && coherentNotification;
    preferences->reloadSharedPaths();
    const bool unchanged = notifications == 1;
    QObject::disconnect(connection);
    preferences->mConfigDirectory = originalConfiguration;
    preferences->mTilesDirectory = originalTiles;
    if (!refreshed || !unchanged) {
        *errorString = QStringLiteral("Shared path refresh or no-op notification failed.");
        return false;
    }
    return true;
}

QString Preferences::configPath(const QString &fileName) const
{
    return configPath() + QLatin1Char('/') + fileName;
}

QString Preferences::appConfigPath() const
{
    return PortableSettings::applicationConfigPath();
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
    return PortableSettings::installPath(QLatin1String("docs"));
}

QString Preferences::docsPath(const QString &fileName) const
{
    return docsPath() + QLatin1Char('/') + fileName;
}

QString Preferences::luaPath() const
{
    return PortableSettings::installPath(QLatin1String("lua"));
}

QString Preferences::luaPath(const QString &fileName) const
{
    return luaPath() + QLatin1Char('/') + fileName;
}

QString Preferences::mapsDirectory() const
{
    return mMapsDirectory;
}

void Preferences::setMapsDirectory(const QString &path)
{
    if (mMapsDirectory == path)
        return;
    mMapsDirectory = path;
    mSettings->setValue(QLatin1String("MapsDirectory/Current"), path);
    emit mapsDirectoryChanged();
}

bool Preferences::autoSwitchLayer() const
{
    return mAutoSwitchLayer;
}

void Preferences::setAutoSwitchLayer(bool enabled)
{
    if (mAutoSwitchLayer == enabled)
        return;
    mAutoSwitchLayer = enabled;
    mSettings->setValue(QLatin1String("Interface/AutoSwitchLayer"), enabled);
    emit autoSwitchLayerChanged(mAutoSwitchLayer);
}

QString Preferences::tilesDirectory() const
{
    return mTilesDirectory;
}

QString Preferences::tiles2xDirectory() const
{
    if (mTilesDirectory.isEmpty())
        return QString();
    return mTilesDirectory + QLatin1Char('/') + QLatin1String("2x");
}

QString Preferences::projectZomboidDirectory() const
{
    return mProjectZomboidDirectory;
}
void Preferences::setProjectZomboidDirectory(const QString &path)
{
    const QString normalized = PortableSettings::normalizedGamePath(path);
    if (!path.trimmed().isEmpty() && normalized.isEmpty()) {
        qWarning().noquote()
                << "Ignoring invalid Project Zomboid installation path"
                << QDir::toNativeSeparators(path);
        return;
    }
    if (mProjectZomboidDirectory == normalized)
        return;
    mProjectZomboidDirectory = normalized;
    PortableSettings::setSharedGamePath(normalized);
    emit projectZomboidDirectoryChanged();
}
QString Preferences::gameMediaPath(const QString &relativePath) const
{
    if (mProjectZomboidDirectory.isEmpty())
        return QString();
    const QString media = QDir(mProjectZomboidDirectory).filePath(
                QLatin1String("media"));
    return relativePath.isEmpty()
            ? media : QDir(media).filePath(relativePath);
}
qreal Preferences::tilesetScale() const
{
    return mTilesetScale;
}

bool Preferences::sortTilesets() const
{
    return mSortTilesets;
}

void Preferences::setTilesDirectory(const QString &path)
{
    mTilesDirectory = PortableSettings::normalizedTilesPath(path);
    PortableSettings::setSharedTilesPath(mTilesDirectory);
    emit tilesDirectoryChanged();
}

void Preferences::setTilesetScale(qreal scale)
{
    if (mTilesetScale == scale)
        return;
    mTilesetScale = scale;
    mSettings->setValue(QLatin1String("Interface/TilesetScale"), scale);
    emit tilesetScaleChanged(mTilesetScale);
}

void Preferences::setSortTilesets(bool sort)
{
    if (mSortTilesets == sort)
        return;
    mSortTilesets = sort;
    mSettings->setValue(QLatin1String("Interface/SortTilesets"), sort);
    emit sortTilesetsChanged(mSortTilesets);
}

void Preferences::setShowLotFloorsOnly(bool show)
{
    if (mShowLotFloorsOnly == show)
        return;
    mShowLotFloorsOnly = show;
    mSettings->setValue(QLatin1String("Interface/ShowLotFloorsOnly"), show);
    emit showLotFloorsOnlyChanged(mShowLotFloorsOnly);
}

bool Preferences::showMiniMap() const
{
    return mShowMiniMap;
}

void Preferences::setMiniMapWidth(int width)
{
    width = qMin(width, MINIMAP_MAX_WIDTH);
    width = qMax(width, MINIMAP_MIN_WIDTH);

    if (mMiniMapWidth == width)
        return;
    mMiniMapWidth = width;
    mSettings->setValue(QLatin1String("Interface/MiniMapWidth"), width);
    emit miniMapWidthChanged(mMiniMapWidth);
}

int Preferences::miniMapWidth() const
{
    return mMiniMapWidth;
}

void Preferences::setShowMiniMap(bool show)
{
    if (mShowMiniMap == show)
        return;
    mShowMiniMap = show;
    mSettings->setValue(QLatin1String("Interface/ShowMiniMap"), show);
    emit showMiniMapChanged(mShowMiniMap);
}

void Preferences::setShowTileLayersPanel(bool show)
{
    if (mShowTileLayersPanel == show)
        return;
    mShowTileLayersPanel = show;
    mSettings->setValue(QLatin1String("Interface/ShowTileLayersPanel"), show);
    emit showTileLayersPanelChanged(mShowTileLayersPanel);
}

void Preferences::setShowTileSelection(bool show)
{
    if (mShowTileSelection == show)
        return;
    mShowTileSelection = show;
    mSettings->setValue(QLatin1String("Interface/ShowTileSelection"), show);
    emit showTileSelectionChanged(mShowTileLayersPanel);
}

void Preferences::setShowInvisibleTiles(bool show)
{
    if (mShowInvisibleTiles == show)
        return;
    mShowInvisibleTiles = show;
    mSettings->setValue(QLatin1String("Interface/ShowInvisibleTiles"), show);
    emit showInvisibleTilesChanged(mShowInvisibleTiles);
}

void Preferences::setBackgroundColor(const QColor &bgColor)
{
    if (mBackgroundColor == bgColor)
        return;

    mBackgroundColor = bgColor;
    mSettings->setValue(QLatin1String("Interface/BackgroundColor"), mBackgroundColor.name());
    emit backgroundColorChanged(mBackgroundColor);
}

void Preferences::setShowAdjacentMaps(bool show)
{
    if (mShowAdjacentMaps == show)
        return;
    mShowAdjacentMaps = show;
    mSettings->setValue(QLatin1String("Interface/ShowAdjacentMaps"), show);
    emit showAdjacentMapsChanged(mShowAdjacentMaps);
}

void Preferences::setWorldEdFiles(const QStringList &fileNames)
{
    if (mWorldEdFiles == fileNames)
        return;
    mWorldEdFiles = fileNames;
    mSettings->setValue(QLatin1String("WorldEd/ProjectFile"), mWorldEdFiles);
    emit worldEdFilesChanged(mWorldEdFiles);
}

void Preferences::setTilePropertiesFiles(const QStringList &fileNames)
{
    if (mTilePropertiesFiles == fileNames)
        return;
    mTilePropertiesFiles = fileNames;
    mSettings->setValue(QLatin1String("TilePropertiesFiles"), mTilePropertiesFiles);
    emit tilePropertiesFilesChanged(mTilePropertiesFiles);
}

void Preferences::setHighlightRoomUnderPointer(bool highlight)
{
    if (mHighlightRoomUnderPointer == highlight)
        return;
    mHighlightRoomUnderPointer = highlight;
    mSettings->setValue(QLatin1String("Interface/HighlightRoomUnderPointer"), highlight);
    emit highlightRoomUnderPointerChanged(mHighlightRoomUnderPointer);
}

void Preferences::setEraserBrushSize(int newSize)
{
    if (mEraserBrushSize == newSize)
        return;
    mEraserBrushSize = newSize;
    mSettings->setValue(QLatin1String("Tools/Eraser/BrushSize"), mEraserBrushSize);
    emit eraserBrushSizeChanged(mEraserBrushSize);
}

void Preferences::setTilesetBackgroundColor(const QColor &color)
{
    if (mTilesetBackgroundColor == color)
        return;

    mTilesetBackgroundColor = color;
    mTilesetBackgroundColorIsDefault = false;
    mSettings->setValue(QLatin1String("Interface/TilesetBackgroundColor"), mTilesetBackgroundColor.name());
    emit tilesetBackgroundColorChanged(mTilesetBackgroundColor);
}

void Preferences::setThumbnailsDirectory(const QString &path)
{
    mThumbnailsDirectory = path;
    mSettings->setValue(QLatin1String("Thumbnails/Directory"), mThumbnailsDirectory);
    emit thumbnailsDirectoryChanged(mThumbnailsDirectory);
}

void Preferences::setShowCellBorder(bool show)
{
    if (mShowCellBorder == show)
        return;
    mShowCellBorder = show;
    mSettings->setValue(QLatin1String("Interface/ShowCellBorder"), mShowCellBorder);
    emit showCellBorderChanged(mShowCellBorder);
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
    emit themeChanged(mTheme);
    if (PortableSettings::syncThemeAcrossApplications())
        PortableSettings::setThemeForAllApplications(theme);
}
void Preferences::setRestoreLastSession(bool restore)
{
    if (mRestoreLastSession == restore)
        return;
    mRestoreLastSession = restore;
    mSettings->setValue(QLatin1String("Interface/RestoreLastSession"), restore);
}
void Preferences::setAutoSaveIntervalMinutes(int minutes)
{
    const int normalized = QList<int>({0, 1, 5, 10, 20, 60}).contains(
                minutes) ? minutes : 0;
    if (mAutoSaveIntervalMinutes == normalized)
        return;
    mAutoSaveIntervalMinutes = normalized;
    mSettings->setValue(QLatin1String("Interface/AutoSaveIntervalMinutes"),
                        mAutoSaveIntervalMinutes);
    emit autoSaveIntervalChanged(mAutoSaveIntervalMinutes);
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
    qApp->setStyleSheet(QString());
    qApp->setPalette(applicationPaletteForTheme(mTheme));
    if (mTheme == QStringLiteral("Default")) {
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
        return;
    }
    if (!resource.isEmpty()) {
        styleSheet = readStyleSheet(resource);
        styleSheetLoaded = !styleSheet.isEmpty();
    }
    qApp->setStyleSheet(styleSheetLoaded ? styleSheet : QString());
}

#endif // ZOMBOID
