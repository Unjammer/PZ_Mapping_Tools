/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include "tiletoolpreviewcache.h"

#include "BuildingEditor/buildingtiles.h"
#include "map.h"
#include "mapdocument.h"
#include "maprenderer.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"

#include <QHash>
#include <QImage>

using namespace Tiled;
using namespace Tiled::Internal;

bool TileToolPreviewKey::operator==(const TileToolPreviewKey &other) const
{
    return tilePosition == other.tilePosition
            && layer == other.layer
            && selection == other.selection
            && level == other.level
            && mode == other.mode
            && variant == other.variant
            && modifiers == other.modifiers;
}

bool TileToolPreviewState::accept(const TileToolPreviewKey &key)
{
    if (mValid && mKey == key)
        return false;
    mKey = key;
    mValid = true;
    return true;
}

void TileToolPreviewState::invalidate()
{
    mValid = false;
}

const QVector<Tile *> &TileToolTilesCache::resolve(
        Map *map, const QVector<QString> &tileNames,
        int shapeCount, Tile *missingTile)
{
    if (mValid && mMap == map && mTileNames == tileNames
            && mShapeCount == shapeCount && mMissingTile == missingTile)
        return mTiles;

    mMap = map;
    mTileNames = tileNames;
    mShapeCount = shapeCount;
    mMissingTile = missingTile;
    mTiles.fill(missingTile, shapeCount);

    QHash<QString, Tileset *> tilesets;
    if (map) {
        tilesets.reserve(map->tilesets().size());
        for (Tileset *tileset : map->tilesets()) {
            if (tileset)
                tilesets.insert(tileset->name(), tileset);
        }
    }

    const int count = qMin(shapeCount, tileNames.size());
    for (int index = 0; index < count; ++index) {
        if (tileNames.at(index).isEmpty())
            continue;
        QString tilesetName;
        int tileIndex = -1;
        if (!BuildingEditor::BuildingTilesMgr::parseTileName(
                    tileNames.at(index), tilesetName, tileIndex))
            continue;
        Tileset *tileset = tilesets.value(tilesetName, nullptr);
        if (tileset)
            mTiles[index] = tileset->tileAt(tileIndex);
    }

    mValid = true;
    return mTiles;
}

void TileToolTilesCache::invalidate()
{
    mValid = false;
    mMap = nullptr;
    mTileNames.clear();
    mTiles.clear();
}

QRectF Tiled::Internal::tileToolPreviewRect(
        MapDocument *document, const TileLayer *preview,
        const QRect &tileBounds)
{
    if (!document || !document->map() || !document->renderer())
        return QRectF();
    QMargins margins;
    if (preview) {
        margins = preview->drawMargins();
        margins.setTop(qMax(0, margins.top() - document->map()->tileHeight()));
        margins.setRight(qMax(0, margins.right() - document->map()->tileWidth()));
        if (document->renderer()->is2x())
            margins *= 2;
    }
    return document->renderer()->boundingRect(
                tileBounds, document->currentLevel()).adjusted(
                -margins.left() - 3, -margins.top() - 3,
                margins.right() + 3, margins.bottom() + 3);
}

