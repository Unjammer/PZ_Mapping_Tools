/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef TILETOOLPREVIEWCACHE_H
#define TILETOOLPREVIEWCACHE_H

#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

namespace Tiled {
class Map;
class Tile;
class TileLayer;

namespace Internal {

class MapDocument;

struct TileToolPreviewKey
{
    QPoint tilePosition;
    const void *layer = nullptr;
    const void *selection = nullptr;
    int level = 0;
    int mode = 0;
    int variant = 0;
    int modifiers = 0;

    bool operator==(const TileToolPreviewKey &other) const;
};

class TileToolPreviewState
{
public:
    bool accept(const TileToolPreviewKey &key);
    void invalidate();
private:
    TileToolPreviewKey mKey;
    bool mValid = false;
};

class TileToolTilesCache
{
public:
    const QVector<Tile *> &resolve(Map *map,
                                  const QVector<QString> &tileNames,
                                  int shapeCount,
                                  Tile *missingTile);
    void invalidate();
private:
    Map *mMap = nullptr;
    QVector<QString> mTileNames;
    QVector<Tile *> mTiles;
    Tile *mMissingTile = nullptr;
    int mShapeCount = 0;
    bool mValid = false;
};

QRectF tileToolPreviewRect(MapDocument *document,
                           const TileLayer *preview,
                           const QRect &tileBounds);

}
}

#endif
