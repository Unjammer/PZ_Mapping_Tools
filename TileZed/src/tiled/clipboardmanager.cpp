/*
 * clipboardmanager.cpp
 * Copyright 2009, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
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

#include "clipboardmanager.h"

#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "tmxmapreader.h"
#include "tmxmapwriter.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileselectionscope.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSet>

static const char * const TMX_MIMETYPE = "text/tmx";

using namespace Tiled;
using namespace Tiled::Internal;

ClipboardManager::ClipboardManager(QObject *parent) :
    QObject(parent),
    mHasMap(false)
{
    mClipboard = QApplication::clipboard();
    connect(mClipboard, &QClipboard::dataChanged, this, &ClipboardManager::updateHasMap);

    updateHasMap();
}

Map *ClipboardManager::map() const
{
    const QMimeData *mimeData = mClipboard->mimeData();
    const QByteArray data = mimeData->data(QLatin1String(TMX_MIMETYPE));
    if (data.isEmpty())
        return 0;

    TmxMapReader reader;
    return reader.fromByteArray(data);
}

void ClipboardManager::setMap(const Map *map)
{
    TmxMapWriter mapWriter;

    QMimeData *mimeData = new QMimeData;
    mimeData->setData(QLatin1String(TMX_MIMETYPE), mapWriter.toByteArray(map));

    mClipboard->setMimeData(mimeData);
}

bool ClipboardManager::copySelection(const MapDocument *mapDocument,
                                     const TileSelectionScope *scope)
{
    const Layer *currentLayer = mapDocument->currentLayer();
    if (!currentLayer)
        return false;

    const Map *map = mapDocument->map();
    const QRegion &tileSelection = mapDocument->tileSelection();
    const QList<MapObject*> &selectedObjects = mapDocument->selectedObjects();
    const TileLayer *tileLayer = dynamic_cast<const TileLayer*>(currentLayer);
    Layer *copyLayer = 0;

    if (!tileSelection.isEmpty() && tileLayer) {
        QList<TileLayer*> sourceLayers;
        const QList<TileLayer*> mapTileLayers = map->tileLayers();
        for (TileLayer *candidate : mapTileLayers) {
            const bool sameLevel = candidate->level() == tileLayer->level();
            if (scope && scope->levelMode() == TileSelectionScope::CurrentLevel &&
                    !sameLevel) {
                continue;
            }
            if (!scope && candidate != tileLayer)
                continue;
            const bool currentLayer = candidate == tileLayer ||
                    (scope && scope->levelMode() ==
                     TileSelectionScope::AllLevels &&
                     candidate->name() == tileLayer->name());
            if (scope && !scope->includesLayer(candidate->name(),
                                               candidate->isVisible(),
                                               currentLayer)) {
                continue;
            }
            sourceLayers.append(candidate);
        }
        if (sourceLayers.isEmpty())
            return false;

        QRect bounds = tileSelection.boundingRect();
        Map copyMap(map->orientation(),
                    bounds.width(), bounds.height(),
                    map->tileWidth(), map->tileHeight());
        const bool singleLayer = !scope || (scope->layerMode() == TileSelectionScope::CurrentLayer
                && scope->levelMode() == TileSelectionScope::CurrentLevel);
        copyMap.setProperty(QStringLiteral("pz.selection.kind"),
                            singleLayer ? QStringLiteral("single-layer") : QStringLiteral("multi-layer"));
        copyMap.setProperty(QStringLiteral("pz.selection.anchorLevel"),
                            QString::number(tileLayer->level()));

        for (TileLayer *source : sourceLayers) {
            QRegion localSelection = tileSelection.translated(-source->x(),
                                                              -source->y());
            TileLayer *copy = source->copy(localSelection);
            copy->setName(source->name());
            copy->setLevel(source->level());
            copy->setVisible(source->isVisible());
            copy->setOpacity(source->opacity());
            for (Tileset *tileset : copy->usedTilesets()) {
                if (!copyMap.tilesets().contains(tileset))
                    copyMap.addTileset(tileset);
            }
            copyMap.addLayer(copy);
        }

        setMap(&copyMap);
        return true;
    } else if (!selectedObjects.isEmpty()) {
        // Create a new object group with clones of the selected objects
        ObjectGroup *objectGroup = new ObjectGroup;
        foreach (const MapObject *mapObject, selectedObjects)
            objectGroup->addObject(mapObject->clone());
        copyLayer = objectGroup;
    } else {
        return false;
    }

    // Create a temporary map to write to the clipboard
    Map copyMap(map->orientation(),
                copyLayer->width(), copyLayer->height(),
                map->tileWidth(), map->tileHeight());

    // Resolve the set of tilesets used by this layer
    foreach (Tileset *tileset, copyLayer->usedTilesets())
        copyMap.addTileset(tileset);

    copyMap.addLayer(copyLayer);

    setMap(&copyMap);
    return false;
}

void ClipboardManager::updateHasMap()
{
    const QMimeData *data = mClipboard->mimeData();
    const bool mapInClipboard =
            data && data->hasFormat(QLatin1String(TMX_MIMETYPE));

    if (mapInClipboard != mHasMap) {
        mHasMap = mapInClipboard;
        emit hasMapChanged();
    }
}
