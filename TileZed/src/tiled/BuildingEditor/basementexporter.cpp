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

#include "basementexporter.h"

#include "building.h"
#include "buildingfloor.h"
#include "buildingmap.h"
#include "buildingobjects.h"
#include "buildingreader.h"
#include "buildingtiles.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "newmapbinaryfile.h"
#include "tiledeffile.h"
#include "tilesetmanager.h"
#include "maplevel.h"
#include "tilelayer.h"

#include <QDir>
#include <QFileInfo>
#include <memory>
#include <vector>

using namespace BuildingEditor;
using namespace Tiled;
using namespace Tiled::Internal;

namespace {

struct ExportMap
{
    std::unique_ptr<Map> map;
    std::unique_ptr<MapInfo> info;
    std::unique_ptr<MapComposite> composite;
    bool ownsTilesetReferences = false;

    ~ExportMap()
    {
        composite.reset();
        info.reset();
        if (ownsTilesetReferences)
            TilesetManager::instance()->removeReferences(map->tilesets());
    }
};

bool findStaircase(Map *map, const BasementStairTiles &tiles, bool access,
                   QPoint &position, QString &direction)
{
    MapLevel *level = access ? map->minMapLevel() : map->maxMapLevel();
    if (!level)
        return false;
    for (TileLayer *layer : level->tileLayers()) {
        for (int y = 0; y < layer->height(); ++y) {
            for (int x = 0; x < layer->width(); ++x) {
                const Cell &cell = layer->cellAt(x, y);
                if (cell.isEmpty())
                    continue;
                const QString name = BuildingTilesMgr::nameForTile(cell.tile);
                if (tiles.north.contains(name))
                    direction = QStringLiteral("N");
                else if (tiles.west.contains(name))
                    direction = QStringLiteral("W");
                else
                    continue;
                position = QPoint(x, y);
                return true;
            }
        }
    }
    return false;
}

}

BasementStairTiles BasementExporter::staircaseTiles(
        const QList<TileDefFile *> &definitions)
{
    BasementStairTiles result;
    for (const TileDefFile *definition : definitions) {
        if (!definition)
            continue;
        for (TileDefTileset *tileset : definition->tilesets()) {
            for (TileDefTile *tile : qAsConst(tileset->mTiles)) {
                const QString name = BuildingTilesMgr::nameForTile(tileset->mName, tile->id());
                if (tile->mProperties.contains(QStringLiteral("stairsTN")))
                    result.north.insert(name);
                else if (tile->mProperties.contains(QStringLiteral("stairsTW")))
                    result.west.insert(name);
            }
        }
    }
    return result;
}

BasementExportResult BasementExporter::exportFile(const BasementExportRequest &request)
{
    BasementExportResult result;
    BuildingReader reader;
    std::unique_ptr<Building> building(reader.read(request.sourceFile));
    if (!building) {
        result.error = reader.errorString();
        return result;
    }
    reader.fix(building.get());
    BuildingMap buildingMap(building.get());
    ExportMap merged;
    merged.map.reset(buildingMap.mergedMap());
    merged.ownsTilesetReferences = true;
    for (BuildingFloor *floor : building->floors())
        buildingMap.addRoomDefObjects(merged.map.get(), floor);
    merged.info.reset(MapManager::instance()->newFromMap(merged.map.get()));
    merged.composite.reset(new MapComposite(merged.info.get()));

    QPoint offset;
    ExportMap cropped;
    cropped.composite.reset(merged.composite->cropToMinimum(offset));
    if (cropped.composite) {
        cropped.map.reset(cropped.composite->map());
        cropped.info.reset(cropped.composite->mapInfo());
    }
    MapComposite *output = cropped.composite ? cropped.composite.get() : merged.composite.get();
    std::vector<std::unique_ptr<PropertiesGrid>> ownedAttributes;
    QVector<PropertiesGrid *> attributes;
    for (BuildingFloor *floor : building->floors()) {
        ownedAttributes.emplace_back(floor->squarePropertiesGrid()->clone(
                                         QRect(offset, output->map()->size())));
        attributes.append(ownedAttributes.back().get());
    }

    const QFileInfo source(request.sourceFile);
    result.outputFile = QDir(request.outputDirectory).filePath(
                source.completeBaseName() + QStringLiteral(".pzby"));
    NewMapBinaryFile file(8);
    if (!file.write(output, attributes, result.outputFile)) {
        result.error = file.errorString();
        return result;
    }
    result.written = true;

    const bool access = source.fileName().startsWith(QStringLiteral("ba_"));
    if (!(access ? merged.map->minMapLevel() : merged.map->maxMapLevel()))
        return result;
    const BasementAccess basementAccess = building->basementAccess();
    QPoint position;
    QString direction;
    if (basementAccess.isValid()) {
        position = QPoint(basementAccess.mX, basementAccess.mY) - offset;
        direction = basementAccess.dirString();
    } else if (!findStaircase(output->map(), request.stairTiles, access, position, direction)) {
        return result;
    }
    result.luaCode = QStringLiteral("%1 = { width=%2, height=%3, stairx=%4, stairy=%5, stairDir=\"%6\" },\n")
            .arg(source.completeBaseName())
            .arg(output->mapInfo()->width())
            .arg(output->mapInfo()->height())
            .arg(position.x())
            .arg(position.y())
            .arg(direction);
    return result;
}
