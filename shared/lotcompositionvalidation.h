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
    if (!tiles || tiles->tileCount() < 9) {
        *error = QStringLiteral("Lot composition tiles are unavailable");
        return false;
    }
    QStringList failures;
    int checks = 0;
    const QStringList structures = {QStringLiteral("Walls"), QStringLiteral("Walls2"),
        QStringLiteral("Frames"), QStringLiteral("Doors"), QStringLiteral("Windows"),
        QStringLiteral("WallTrim"), QStringLiteral("WallGrime"), QStringLiteral("WallOverlay")};
    struct Case {
        QStringList lotLayers;
        QVector<int> expected;
        QString upperLotLayer;
        QVector<int> lowerLotOnly;
        QVector<int> upperLotOnly;
    };
    QVector<Case> cases;
    for (const QString &structure : structures)
        cases.append(Case{{structure}, {0, 1, 2, 7, 6}});
    for (const QString &contents : {QStringLiteral("Furniture"), QStringLiteral("FloorFurniture"),
                                    QStringLiteral("WallFurniture")})
        cases.append(Case{{contents}, {0, 1, 7, 6}});
    cases.append(Case{{QStringLiteral("Floor")}, {7, 6}});
    cases.append(Case{{QStringLiteral("Walls"), QStringLiteral("Furniture")}, {0, 1, 7, 8, 6}});
    cases.append(Case{{QStringLiteral("Floor"), QStringLiteral("Walls")}, {7, 8, 6}});
    const QVector<int> uncovered{0, 1, 2, 3, 4, 5, 6};
    cases.append(Case{{}, uncovered});
    cases.append(Case{{QStringLiteral("Floor")}, {7, 8, 6}, QStringLiteral("Walls"),
                      {7, 6}, {0, 1, 2, 8, 6}});
    cases.append(Case{{QStringLiteral("Walls")}, {0, 1, 8, 6}, QStringLiteral("Furniture"),
                      {0, 1, 2, 7, 6}, {0, 1, 8, 6}});
    cases.append(Case{{QStringLiteral("Furniture")}, {8, 6}, QStringLiteral("Floor"),
                      {0, 1, 7, 6}, {8, 6}});
    for (const Case &test : cases) {
        Map map(Map::LevelIsometric, 8, 8, 64, 32);
        map.addTileset(tiles);
        const QStringList baseLayers = {QStringLiteral("Floor"), QStringLiteral("FloorOverlay"),
            QStringLiteral("Vegetation"), QStringLiteral("Furniture"), QStringLiteral("Decorations"),
            QStringLiteral("FloorOverlay2"), QStringLiteral("AboveLot")};
        for (int i = 0; i < baseLayers.size(); ++i) {
            auto *layer = new TileLayer(baseLayers.at(i), 0, 0, 8, 8);
            layer->setCell(1, 1, Cell(tiles->tileAt(i)));
            map.addLayer(layer);
        }
        Map lot(Map::LevelIsometric, 8, 8, 64, 32);
        lot.addTileset(tiles);
        for (int i = 0; i < test.lotLayers.size(); ++i) {
            auto *layer = new TileLayer(test.lotLayers.at(i), 0, 0, 8, 8);
            layer->setCell(1, 1, Cell(tiles->tileAt(7 + i)));
            lot.addLayer(layer);
        }
        Map upperLot(Map::LevelIsometric, 8, 8, 64, 32);
        if (!test.upperLotLayer.isEmpty()) {
            upperLot.addTileset(tiles);
            auto *layer = new TileLayer(test.upperLotLayer, 0, 0, 8, 8);
            layer->setCell(1, 1, Cell(tiles->tileAt(8)));
            upperLot.addLayer(layer);
        }
        MapInfo mapInfo(&map);
        MapInfo lotInfo(&lot);
        MapInfo upperLotInfo(&upperLot);
        MapComposite composite(&mapInfo);
        composite.setCellMap(true);
        MapComposite *placed = composite.addMap(&lotInfo, QPoint(), 0);
        MapComposite *upper = test.upperLotLayer.isEmpty() ? nullptr
                : composite.addMap(&upperLotInfo, QPoint(), 0);
        composite.synch();
        ZLevelRenderer renderer(&map);
        auto *group = composite.tileLayersForLevel(0);
        const QRect bounds = composite.boundingRect(&renderer).toAlignedRect();
        const auto check = [&](const QVector<int> &actual, const QVector<int> &expected, const QString &path) {
            ++checks;
            if (actual != expected)
                failures.append(test.lotLayers.join(QLatin1Char('+')) + QLatin1Char('/')
                                + test.upperLotLayer + QLatin1Char('/') + path);
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
        QVector<int> actual;
        for (const Cell *cell : cells)
            actual.append(cell->tile->id());
        check(actual, test.expected, QStringLiteral("raster"));
        group->prepareDrawing2();
        OrderedCellsTemporaries exported;
        group->orderedCellsAt2(QPoint(1, 1), exported, cells);
        actual.clear();
        for (const Cell *cell : cells)
            actual.append(cell->tile->id());
        check(actual, test.expected, QStringLiteral("export"));
#ifdef WORLDED
        group->prepareDrawing3(&renderer, bounds);
        OrderedCellsTemporaries3 gl;
        QVector<TilePlusLayer> glCells;
        group->orderedCellsAt3(QPoint(1, 1), gl, glCells);
        const auto lotVisible = [](const MapComposite *lot) {
            return lot->isVisible() && lot->isGroupVisible() && !lot->isHiddenDuringDrag();
        };
        const auto visibleTiles = [&]() {
            QVector<int> ids;
            for (const TilePlusLayer &cell : glCells) {
                if (cell.mVisible && !cell.mCoveringLots.hidesTile(lotVisible)
                        && (!cell.mSubMap || lotVisible(cell.mSubMap)))
                    ids.append(cell.mTile->id());
            }
            return ids;
        };
        check(visibleTiles(), test.expected, QStringLiteral("opengl"));
        if (upper) {
            upper->setGroupVisible(false);
            check(visibleTiles(), test.lowerLotOnly, QStringLiteral("opengl upper lot hidden"));
            upper->setGroupVisible(true);
            placed->setHiddenDuringDrag(true);
            check(visibleTiles(), test.upperLotOnly, QStringLiteral("opengl lower lot dragged"));
            placed->setHiddenDuringDrag(false);
            upper->setVisible(false);
        }
        placed->setVisible(false);
        check(visibleTiles(), uncovered, QStringLiteral("opengl hidden lot"));
#else
        Q_UNUSED(placed)
        Q_UNUSED(upper)
#endif
    }
    qInfo() << "Lot composition validation:" << checks << "checks," << failures.size() << "failures";
    *error = failures.join(QLatin1Char('\n'));
    return failures.isEmpty();
}

#endif
