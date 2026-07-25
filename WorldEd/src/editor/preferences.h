/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
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

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QObject>
#include <QColor>
#include <QStringList>

class QSettings;

class Preferences : public QObject
{
    Q_OBJECT
public:
    static Preferences *instance();
    static void deleteInstance();

    QString userPath() const;
    QString userPath(const QString &fileName) const;

    QString configPath() const;
    QString configPath(const QString &fileName) const;

    QString appConfigPath() const;
    QString appConfigPath(const QString &fileName) const;

    QString docsPath() const;
    QString docsPath(const QString &fileName) const;

    QString luaPath() const;
    QString luaPath(const QString &fileName) const;

    bool snapToGrid() const;
    bool showCellBorder() const;
    bool showCoordinates() const;
    bool showWorldGrid() const;
    bool showCellGrid() const;
    QColor gridColor() const { return mGridColor; }
    int gridWidth() const { return mGridWidth; }
    int thumbnailWidth() const { return mThumbnailWidth; }
    bool restoreLastSession() const { return mRestoreLastSession; }
    qreal roadSimplificationHighway() const { return mRoadSimplificationHighway; }
    int roadPointSpacingHighway() const { return mRoadPointSpacingHighway; }
    qreal roadSimplificationTrail() const { return mRoadSimplificationTrail; }
    int roadPointSpacingTrail() const { return mRoadPointSpacingTrail; }
    qreal roadSimplificationRailway() const { return mRoadSimplificationRailway; }
    int roadPointSpacingRailway() const { return mRoadPointSpacingRailway; }
    bool showMiniMap() const;
    int miniMapWidth() const;
    bool highlightCurrentLevel() const;
    bool highlightRoomUnderPointer() const
    { return mHighlightRoomUnderPointer; }
    bool showLotFloorsOnly() const
    { return mShowLotFloorsOnly; }
    bool showOtherWorlds() const
    { return mShowOtherWorlds; }

    QString mapsDirectory() const;
    void setMapsDirectory(const QString &path);

    QString tilesDirectory() const;
    void setTilesDirectory(const QString &path);

    QString tiles2xDirectory() const;

    QStringList tilePropertiesFiles() const { return mTilePropertiesFiles; }

    QString texturesDirectory() const;

    QString thumbnailsDirectory() const
    { return mThumbnailsDirectory; }

    bool useOpenGL() const { return mUseOpenGL; }
    void setUseOpenGL(bool useOpenGL);

    bool loadAllWorldThumbnails() const { return mLoadAllWorldThumbnails; }
    void setLoadAllWorldThumbnails(bool thumbs);

    bool showWorldThumbnails() const { return mShowWorldThumbnails; }
    void setShowWorldThumbnails(bool thumbs);

    bool showObjects() const { return mShowObjects; }
    bool showObjectNames() const { return mShowObjectNames; }
    bool showBMPs() const { return mShowBMPs; }
    bool showZombieSpawnImage() const { return mShowZombieSpawnImage; }
    qreal zombieSpawnImageOpacity() const { return mZombieSpawnImageOpacity; }
    bool showBiomeMap() const { return mShowBiomeMap; }
    qreal biomeMapOpacity() const { return mBiomeMapOpacity; }
    bool showZonesInWorldView() const { return mShowZonesInWorldView; }

    QString openFileDirectory() const;
    void setOpenFileDirectory(const QString &path);

    QString worldMapXMLFile() const;
    void setWorldMapXMLFile(const QString &path);

    bool showAdjacentMaps() const { return mShowAdjacentMaps; }
    void setShowAdjacentMaps(bool show);

    bool showInvisibleTiles() const { return mShowInvisibleTiles; }

    QString theme() const { return mTheme; }
    QStringList availableThemes() const;
    void applyTheme() const;

signals:
    void snapToGridChanged(bool snapToGrid);
    void showCellBorderChanged(bool showGrid);
    void showCoordinatesChanged(bool showGrid);
    void showWorldGridChanged(bool showGrid);
    void showCellGridChanged(bool showGrid);
    void gridColorChanged(const QColor &gridColor);
    void gridWidthChanged(int width);
    void thumbnailWidthChanged(int width);

    void useOpenGLChanged(bool useOpenGL);
    void loadAllWorldThumbnailsChanged(bool thumbs);
    void showWorldThumbnailsChanged(bool show);

    void showObjectsChanged(bool show);
    void showObjectNamesChanged(bool show);
    void showBMPsChanged(bool show);
    void showZombieSpawnImageChanged(bool show);
    void zombieSpawnImageOpacityChanged(qreal opacity);
    void showBiomeMapChanged(bool show);
    void biomeMapOpacityChanged(qreal opacity);
    void showZonesInWorldViewChanged(bool show);

#define MINIMAP_WIDTH_MIN 128
#define MINIMAP_WIDTH_MAX 512
    void showMiniMapChanged(bool show);
    void miniMapWidthChanged(int width);

    void highlightCurrentLevelChanged(bool highlight);
    void mapsDirectoryChanged();
    void tilesDirectoryChanged();
    void showAdjacentMapsChanged(bool show);
    void highlightRoomUnderPointerChanged(bool highlight);
    void showLotFloorsOnlyChanged(bool show);
    void showOtherWorldsChanged(bool show);
    void showInvisibleTilesChanged(bool show);

public slots:
    void setSnapToGrid(bool snapToGrid);
    void setShowCellBorder(bool showCellBorder);
    void setShowCoordinates(bool showCoords);
    void setShowWorldGrid(bool showGrid);
    void setShowCellGrid(bool showGrid);
    void setGridColor(const QColor &gridColor);
    void setGridWidth(int width);
    void setThumbnailWidth(int width);
    void setRestoreLastSession(bool restore);
    void setRoadSimplificationHighway(qreal tolerance);
    void setRoadPointSpacingHighway(int spacing);
    void setRoadSimplificationTrail(qreal tolerance);
    void setRoadPointSpacingTrail(int spacing);
    void setRoadSimplificationRailway(qreal tolerance);
    void setRoadPointSpacingRailway(int spacing);
    void setShowMiniMap(bool show);
    void setMiniMapWidth(int width);
    void setShowObjects(bool show);
    void setShowObjectNames(bool show);
    void setShowBMPs(bool show);
    void setShowZombieSpawnImage(bool show);
    void setZombieSpawnImageOpacity(qreal opacity);
    void setShowBiomeMap(bool show);
    void setBiomeMapOpacity(qreal opacity);
    void setShowZonesInWorldView(bool show);
    void setHighlightCurrentLevel(bool highlight);
    void setHighlightRoomUnderPointer(bool highlight);
    void setShowLotFloorsOnly(bool show);
    void setShowOtherWorlds(bool show);
    void setShowInvisibleTiles(bool show);
    void setTheme(const QString &theme);

private:
    Preferences();
    ~Preferences();

    QSettings *mSettings;

    bool mSnapToGrid;
    bool mShowCellBorder;
    bool mShowCoordinates;
    bool mShowWorldGrid;
    bool mShowCellGrid;
    QColor mGridColor;
    int mGridWidth;
    int mThumbnailWidth;
    bool mRestoreLastSession;
    qreal mRoadSimplificationHighway;
    int mRoadPointSpacingHighway;
    qreal mRoadSimplificationTrail;
    int mRoadPointSpacingTrail;
    qreal mRoadSimplificationRailway;
    int mRoadPointSpacingRailway;
    bool mUseOpenGL;
    bool mLoadAllWorldThumbnails;
    bool mShowWorldThumbnails;
    bool mShowObjects;
    bool mShowObjectNames;
    bool mShowBMPs;
    bool mShowMiniMap;
    bool mShowZombieSpawnImage;
    qreal mZombieSpawnImageOpacity;
    bool mShowBiomeMap;
    qreal mBiomeMapOpacity;
    bool mShowZonesInWorldView;
    int mMiniMapWidth;
    bool mHighlightCurrentLevel;
    QString mConfigDirectory;
    QString mMapsDirectory;
    QString mTilesDirectory;
    QStringList mTilePropertiesFiles;
    QString mOpenFileDirectory;
    QString mWorldMapXMLFile;
    bool mShowAdjacentMaps;
    bool mHighlightRoomUnderPointer;
    bool mShowLotFloorsOnly = false;
    bool mShowOtherWorlds;
    QString mThumbnailsDirectory;
    bool mShowInvisibleTiles;
    QString mTheme;

    static Preferences *mInstance;
};

#endif // PREFERENCES_H
