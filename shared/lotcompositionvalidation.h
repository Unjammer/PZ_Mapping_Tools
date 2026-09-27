#ifndef LOTCOMPOSITIONVALIDATION_H
#define LOTCOMPOSITIONVALIDATION_H

#include "mapcomposite.h"
#include "mapmanager.h"
#include "tilelayer.h"
#include "tileset.h"
#include "zlevelrenderer.h"
#include <QDebug>

inline bool validateLotComposition(Tiled::Tileset *tiles, QString *error)
{
    using namespace Tiled;
    if (!tiles || tiles->tileCount() < 8) {
        *error = QStringLiteral("Lot composition tiles are unavailable");
        return false;
    }
    QStringList failures;
    int checks = 0;
    const QStringList structures = {QStringLiteral("Walls"), QStringLiteral("Walls2"),
        QStringLiteral("Frames"), QStringLiteral("Doors"), QStringLiteral("Windows"),
        QStringLiteral("WallTrim"), QStringLiteral("WallGrime"), QStringLiteral("WallOverlay")};
    QStringList replacements = structures;
    replacements << QStringLiteral("Furniture") << QStringLiteral("FloorFurniture")
                 << QStringLiteral("WallFurniture") << QStringLiteral("Floor");
    for (const QString &replacement : replacements) {
        Map map(Map::LevelIsometric, 8, 8, 64, 32);
        map.addTileset(tiles);
        const QStringList baseLayers = {QStringLiteral("Floor"), QStringLiteral("Vegetation"),
            QStringLiteral("Furniture"), QStringLiteral("AboveLot")};
        for (int i = 0; i < baseLayers.size(); ++i) {
            auto *layer = new TileLayer(baseLayers.at(i), 0, 0, 8, 8);
            layer->setCell(1, 1, Cell(tiles->tileAt(i)));
            map.addLayer(layer);
        }
        Map lot(Map::LevelIsometric, 8, 8, 64, 32);
        lot.addTileset(tiles);
        auto *layer = new TileLayer(replacement, 0, 0, 8, 8);
        layer->setCell(1, 1, Cell(tiles->tileAt(4)));
        lot.addLayer(layer);
        MapInfo mapInfo(&map);
        MapInfo lotInfo(&lot);
        MapComposite composite(&mapInfo);
        composite.setCellMap(true);
        composite.addMap(&lotInfo, QPoint(), 0);
        composite.synch();
        ZLevelRenderer renderer(&map);
        auto *group = composite.tileLayersForLevel(0);
        const QRect bounds = composite.boundingRect(&renderer).toAlignedRect();
        QSet<const Tile *> expected{tiles->tileAt(3), tiles->tileAt(4)};
        if (replacement != QLatin1String("Floor")) {
            expected.insert(tiles->tileAt(0));
            if (replacement != QLatin1String("Furniture"))
                expected.insert(tiles->tileAt(2));
            if (structures.contains(replacement))
                expected.insert(tiles->tileAt(1));
        }
        const auto check = [&](const QSet<const Tile *> &actual, const QString &path) {
            ++checks;
            if (actual != expected)
                failures.append(replacement + QLatin1Char('/') + path);
        };
        group->prepareDrawing(&renderer, bounds);
        QVector<const Cell *> cells;
        QVector<qreal> opacity;
#ifdef WORLDED
        group->orderedCellsAt(QPoint(1, 1), cells, opacity);
#else
        OrderedCellsTemporaries raster;
        group->orderedCellsAt(QPoint(1, 1), cells, opacity,
                             reinterpret_cast<ZTileLayerGroupRenderData *>(&raster));
#endif
        QSet<const Tile *> actual;
        for (const Cell *cell : cells)
            actual.insert(cell->tile);
        check(actual, QStringLiteral("raster"));
        group->prepareDrawing2();
        OrderedCellsTemporaries exported;
        group->orderedCellsAt2(QPoint(1, 1), exported, cells);
        actual.clear();
        for (const Cell *cell : cells)
            actual.insert(cell->tile);
        check(actual, QStringLiteral("export"));
#ifdef WORLDED
        group->prepareDrawing3(&renderer, bounds);
        OrderedCellsTemporaries3 gl;
        QVector<TilePlusLayer> glCells;
        group->orderedCellsAt3(QPoint(1, 1), gl, glCells);
        actual.clear();
        for (const TilePlusLayer &cell : glCells) {
            if (cell.mVisible && (!cell.mHideIfVisible || !cell.mHideIfVisible->isVisible()))
                actual.insert(cell.mTile);
        }
        check(actual, QStringLiteral("opengl"));
#endif
    }
    qInfo() << "Lot composition validation:" << checks << "checks," << failures.size() << "failures";
    *error = failures.join(QLatin1Char('\n'));
    return failures.isEmpty();
}

#endif
