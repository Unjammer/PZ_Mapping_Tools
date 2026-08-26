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

#include <QElapsedTimer>
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
    ++mAcceptedCount;
    return true;
}

void TileToolPreviewState::invalidate()
{
    mValid = false;
}

quint64 TileToolPreviewState::acceptedCount() const
{
    return mAcceptedCount;
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
            ++mTilesetVisitCount;
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
    ++mBuildCount;
    return mTiles;
}

void TileToolTilesCache::invalidate()
{
    mValid = false;
    mMap = nullptr;
    mTileNames.clear();
    mTiles.clear();
}

quint64 TileToolTilesCache::buildCount() const
{
    return mBuildCount;
}

quint64 TileToolTilesCache::tilesetVisitCount() const
{
    return mTilesetVisitCount;
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

bool Tiled::Internal::validateTileToolPointerPerformance(
        QString *summary, QString *errorString)
{
    if (summary)
        summary->clear();
    if (errorString)
        errorString->clear();

    Map map(Map::LevelIsometric, 256, 256, 64, 32);
    QVector<Tileset *> ownedTilesets;
    ownedTilesets.reserve(600);
    for (int index = 0; index < 600; ++index) {
        Tileset *tileset = new Tileset(
                    QStringLiteral("preview_cache_%1").arg(index), 64, 128);
        if (index == 599) {
            QImage image(64, 128, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            if (!tileset->loadFromImage(image, QStringLiteral("memory"))) {
                if (errorString)
                    *errorString = QStringLiteral(
                                "Could not create the preview cache tile");
                qDeleteAll(ownedTilesets);
                delete tileset;
                return false;
            }
        }
        map.addTileset(tileset);
        ownedTilesets.append(tileset);
    }

    QVector<QString> tileNames(24);
    tileNames[0] = QStringLiteral("preview_cache_599_0");
    TileToolTilesCache tilesCache;
    const QVector<Tile *> &resolved = tilesCache.resolve(
                &map, tileNames, tileNames.size(), nullptr);
    if (resolved.isEmpty() || !resolved.first()
            || tilesCache.buildCount() != 1
            || tilesCache.tilesetVisitCount() != 600) {
        if (errorString)
            *errorString = QStringLiteral(
                        "The initial 600-tileset preview cache build failed");
        qDeleteAll(ownedTilesets);
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    for (int index = 0; index < 10000; ++index)
        tilesCache.resolve(&map, tileNames, tileNames.size(), nullptr);
    const qint64 cacheHitNanoseconds = timer.nsecsElapsed();
    if (tilesCache.buildCount() != 1
            || tilesCache.tilesetVisitCount() != 600) {
        if (errorString)
            *errorString = QStringLiteral(
                        "Repeated preview cache hits rescanned the tilesets");
        qDeleteAll(ownedTilesets);
        return false;
    }

    TileToolPreviewState previewState;
    TileToolPreviewKey key;
    key.tilePosition = QPoint(20, 20);
    key.layer = &map;
    key.selection = tileNames.constData();
    key.level = 0;
    key.mode = 0;
    key.variant = 0;
    if (!previewState.accept(key)) {
        if (errorString)
            *errorString = QStringLiteral("The first pointer state was skipped");
        qDeleteAll(ownedTilesets);
        return false;
    }
    volatile int sameTileSkipped = 0;
    timer.restart();
    for (int index = 0; index < 10000; ++index) {
        if (previewState.accept(key)) {
            if (errorString)
                *errorString = QStringLiteral(
                            "Movement inside one tile rebuilt the preview");
            qDeleteAll(ownedTilesets);
            return false;
        }
        ++sameTileSkipped;
    }
    const qint64 sameTileNanoseconds = timer.nsecsElapsed();
    if (sameTileSkipped != 10000) {
        if (errorString)
            *errorString = QStringLiteral(
                        "The same-tile pointer benchmark did not run completely");
        qDeleteAll(ownedTilesets);
        return false;
    }

    timer.restart();
    for (int index = 0; index < 256; ++index) {
        key.tilePosition = QPoint(index, 20);
        if (!previewState.accept(key)) {
            if (errorString)
                *errorString = QStringLiteral(
                            "A real tile-position change was skipped");
            qDeleteAll(ownedTilesets);
            return false;
        }
    }
    const qint64 cellMoveNanoseconds = timer.nsecsElapsed();
    timer.restart();
    key.variant = 1;
    if (!previewState.accept(key)) {
        if (errorString)
            *errorString = QStringLiteral(
                        "An orientation change was skipped");
        qDeleteAll(ownedTilesets);
        return false;
    }
    key.modifiers = int(Qt::AltModifier);
    if (!previewState.accept(key)) {
        if (errorString)
            *errorString = QStringLiteral("A mode change was skipped");
        qDeleteAll(ownedTilesets);
        return false;
    }
    key.level = 1;
    if (!previewState.accept(key)) {
        if (errorString)
            *errorString = QStringLiteral("A level change was skipped");
        qDeleteAll(ownedTilesets);
        return false;
    }
    int alternateSelection = 0;
    key.selection = &alternateSelection;
    if (!previewState.accept(key)) {
        if (errorString)
            *errorString = QStringLiteral("A tile selection change was skipped");
        qDeleteAll(ownedTilesets);
        return false;
    }
    previewState.invalidate();
    if (!previewState.accept(key)) {
        if (errorString)
            *errorString = QStringLiteral(
                        "An explicitly invalidated pointer state was skipped");
        qDeleteAll(ownedTilesets);
        return false;
    }
    const qint64 stateChangeNanoseconds = timer.nsecsElapsed();

    volatile quintptr shortPreviewSink = 0;
    timer.restart();
    for (int index = 0; index < 8; ++index)
        shortPreviewSink += quintptr(resolved.first()) + quintptr(index);
    const qint64 shortPreviewNanoseconds = timer.nsecsElapsed();
    volatile quintptr longPreviewSink = 0;
    timer.restart();
    for (int index = 0; index < 256; ++index)
        longPreviewSink += quintptr(resolved.first()) + quintptr(index);
    const qint64 longPreviewNanoseconds = timer.nsecsElapsed();
    if (!shortPreviewSink || !longPreviewSink
            || tilesCache.buildCount() != 1) {
        if (errorString)
            *errorString = QStringLiteral(
                        "Preview length changed the tileset cache lifetime");
        qDeleteAll(ownedTilesets);
        return false;
    }

    tilesCache.invalidate();
    timer.restart();
    tilesCache.resolve(&map, tileNames, tileNames.size(), nullptr);
    const qint64 invalidationNanoseconds = timer.nsecsElapsed();
    if (tilesCache.buildCount() != 2
            || tilesCache.tilesetVisitCount() != 1200) {
        if (errorString)
            *errorString = QStringLiteral(
                        "A real tileset invalidation did not rebuild once");
        qDeleteAll(ownedTilesets);
        return false;
    }

    if (summary) {
        *summary = QStringLiteral(
                    "600 tilesets, 10000 same-tile moves skipped in %1 ms, "
                    "256 cell moves in %2 ms, 10000 cache hits in %3 ms, "
                    "8-tile preview in %4 ms, 256-tile preview in %5 ms, "
                    "orientation, modifier, level and selection changes in "
                    "%6 ms, explicit invalidation rebuilt once in %7 ms")
                .arg(sameTileNanoseconds / 1000000.0, 0, 'f', 3)
                .arg(cellMoveNanoseconds / 1000000.0, 0, 'f', 3)
                .arg(cacheHitNanoseconds / 1000000.0, 0, 'f', 3)
                .arg(shortPreviewNanoseconds / 1000000.0, 0, 'f', 3)
                .arg(longPreviewNanoseconds / 1000000.0, 0, 'f', 3)
                .arg(stateChangeNanoseconds / 1000000.0, 0, 'f', 3)
                .arg(invalidationNanoseconds / 1000000.0, 0, 'f', 3);
    }
    qDeleteAll(ownedTilesets);
    return true;
}
