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

#ifndef BUILDINGPREFERENCES_H
#define BUILDINGPREFERENCES_H

#include <QColor>
#include <QObject>
#include <QSettings>

namespace BuildingEditor {

class BuildingPreferences : public QObject
{
    Q_OBJECT
public:
    static BuildingPreferences *instance();
    static void deleteInstance();

    explicit BuildingPreferences(QObject *parent = 0);

    QString configPath() const;
    QString configPath(const QString &fileName) const;

    QSettings &settings() { return mSettings; }

    QString mapsDirectory() const
    { return mMapsDirectory; }

    bool showGrid() const
    { return mShowGrid; }

    bool gapCheckBackground() const
    { return mGapCheckBackground; }

    bool furnitureClipboardEnabled() const { return mFurnitureClipboardEnabled; }
    bool revealPickedTileset() const { return mRevealPickedTileset; }
    bool wallVariantDetails() const { return mWallVariantDetails; }
    bool luaFloorGrimeEnabled() const { return mLuaFloorGrimeEnabled; }

    QColor gridColor() const
    { return mGridColor; }

    bool highlightFloor() const
    { return mHighlightFloor; }

    bool highlightRoom() const
    { return mHighlightRoom; }

    bool showWalls() const
    { return mShowWalls; }

    bool showLowerFloors() const
    { return mShowLowerFloors; }

    bool showOnlyFloors() const
    { return mShowOnlyFloors; }

    bool showObjects() const
    { return mShowObjects; }

    bool highlightUnlitRooms() const
    { return mHighlightUnlitRooms; }

    qreal tileScale() const
    { return mTileScale; }

    bool useOpenGL() const
    { return mUseOpenGL; }

    bool levelIsometric() const
    { return mLevelIsometric; }

    int autoSaveIntervalMinutes() const
    { return mAutoSaveIntervalMinutes; }

signals:
    void mapsDirectoryChanged();
    void showGridChanged(bool show);
    void gapCheckBackgroundChanged(bool enabled);
    void furnitureClipboardEnabledChanged(bool enabled);
    void revealPickedTilesetChanged(bool enabled);
    void wallVariantDetailsChanged(bool enabled);
    void luaFloorGrimeEnabledChanged(bool enabled);
    void gridColorChanged(const QColor &gridColor);
    void highlightFloorChanged(bool highlight);
    void highlightRoomChanged(bool highlight);
    void showWallsChanged(bool show);
    void showLowerFloorsChanged(bool show);
    void showOnlyFloorsChanged(bool show);
    void showObjectsChanged(bool show);
    void highlightUnlitRoomsChanged(bool show);
    void tileScaleChanged(qreal scale);
    void useOpenGLChanged(bool useOpenGL);
    void levelIsometricChanged(bool levels);
    void autoSaveIntervalChanged(int minutes);

public slots:
    void setMapsDirectory(const QString &path);
    void setShowGrid(bool show);
    void setGapCheckBackground(bool enabled);
    void setFurnitureClipboardEnabled(bool enabled);
    void setRevealPickedTileset(bool enabled);
    void setWallVariantDetails(bool enabled);
    void setLuaFloorGrimeEnabled(bool enabled);
    void setGridColor(const QColor &gridColor);
    void setHighlightFloor(bool highlight);
    void setHighlightRoom(bool highlight);
    void setShowWalls(bool show);
    void setShowLowerFloors(bool show);
    void setShowOnlyFloors(bool show);
    void setShowObjects(bool show);
    void setHighlightUnlitRooms(bool show);
    void setTileScale(qreal scale);
    void setUseOpenGL(bool useOpenGL);
    void setLevelIsometric(bool levels);
    void setAutoSaveIntervalMinutes(int minutes);

private:
    static BuildingPreferences *mInstance;
    QSettings mSettings;
    QString mMapsDirectory;
    bool mShowGrid;
    bool mGapCheckBackground;
    bool mFurnitureClipboardEnabled;
    bool mRevealPickedTileset;
    bool mWallVariantDetails;
    bool mLuaFloorGrimeEnabled;
    QColor mGridColor;
    bool mHighlightFloor;
    bool mHighlightRoom;
    bool mShowWalls;
    bool mShowLowerFloors;
    bool mShowOnlyFloors;
    bool mShowObjects;
    bool mHighlightUnlitRooms;
    qreal mTileScale;
    bool mUseOpenGL;
    bool mLevelIsometric;
    int mAutoSaveIntervalMinutes;
};

} // namespace BuildingEditor

#endif // BUILDINGPREFERENCES_H
