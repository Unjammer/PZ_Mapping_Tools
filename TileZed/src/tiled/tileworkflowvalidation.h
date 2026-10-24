#ifndef TILEWORKFLOWVALIDATION_H
#define TILEWORKFLOWVALIDATION_H

#include "clipboardmanager.h"
#include "curbtool.h"
#include "erasetiles.h"
#include "mapcomposite.h"
#include "lotcompositionvalidation.h"
#include "layerdock.h"
#include "layermodel.h"
#include "mapdocument.h"
#include "mapmanager.h"
#include "mapscene.h"
#include "preferences.h"
#include "stampbrush.h"
#include "tilemetainfomgr.h"
#include "tileselectionscope.h"
#include "tilesetmodel.h"
#include "tileset.h"
#include "tilesetview.h"
#include "zlevelrenderer.h"
#include "zlevelsdock.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QUndoStack>

namespace Tiled {
namespace Internal {

inline bool validateLayerVisibility(QString *error)
{
    QStringList failures;
    int checks = 0;
    const auto check = [&](bool valid, const QString &name) {
        ++checks;
        if (!valid)
            failures += name;
    };
    QSettings settings;
    const QString key = QStringLiteral("LayerDocks/Visibility");
    const bool hadSetting = settings.contains(key);
    const QVariant savedSetting = settings.value(key);
    settings.setValue(key, 1);
    const auto makeMap = [](int count) {
        auto *map = new Map(Map::LevelIsometric, 8, 8, 64, 32);
        for (int index = 0; index < count; ++index) {
            auto *layer = new TileLayer(
                        QStringLiteral("%1_Furniture").arg(index), 0, 0, 8, 8);
            layer->setVisible(false);
            map->addLayer(layer);
        }
        return map;
    };
    MapDocument document(makeMap(6));
    MapDocument other(makeMap(2));
    MapDocument empty(makeMap(0));
    LayerDock layers;
    ZLevelsDock levels;
    auto *layerSlider = layers.findChild<QSlider *>(QStringLiteral("layerVisibilitySlider"));
    auto *levelSlider = levels.findChild<QSlider *>(QStringLiteral("levelVisibilitySlider"));
    const auto attach = [&](MapDocument *active) {
        layers.setMapDocument(active);
        levels.setMapDocument(active);
    };
    const auto checkSliders = [&](int value, int maximum) {
        check(layerSlider && levelSlider
              && layerSlider->value() == value && levelSlider->value() == value
              && layerSlider->maximum() == maximum && levelSlider->maximum() == maximum,
              QStringLiteral("Both visibility sliders track the document"));
    };
    const auto checkLayers = [&](MapDocument &active, int count) {
        for (int index = 0; index < active.map()->layerCount(); ++index) {
            check(active.map()->layerAt(index)->isVisible() == (index < count),
                  QStringLiteral("Layer %1 visibility at cutoff %2").arg(index).arg(count));
        }
    };
    attach(&document);
    checkSliders(6, 6);
    checkLayers(document, 6);
    if (layerSlider && levelSlider) {
        layerSlider->setValue(2);
        checkSliders(2, 6);
        checkLayers(document, 2);
        attach(&other);
        checkSliders(2, 2);
        checkLayers(other, 2);
        attach(&document);
        checkSliders(2, 6);
        checkLayers(document, 2);
        document.setLayerVisible(5, true);
        attach(&other);
        attach(&document);
        check(document.map()->layerAt(5)->isVisible(), QStringLiteral("Individual checks survive tab switches"));
        levelSlider->setValue(6);
        checkSliders(6, 6);
        checkLayers(document, 6);
        document.layerModel()->insertLayer(6, new TileLayer(QStringLiteral("6_Furniture"), 0, 0, 8, 8));
        checkSliders(7, 7);
        checkLayers(document, 7);
        delete document.layerModel()->takeLayerAt(6);
        checkSliders(6, 6);
        layerSlider->setValue(2);
        document.layerModel()->insertLayer(6, new TileLayer(QStringLiteral("6_Furniture"), 0, 0, 8, 8));
        checkSliders(2, 7);
        check(document.map()->layerAt(6)->isVisible(), QStringLiteral("New layers retain their default visibility"));
        document.setLayerVisible(6, false);
        checkLayers(document, 2);
        document.layerModel()->insertLayer(0, new TileLayer(QStringLiteral("0_Floor"), 0, 0, 8, 8));
        checkSliders(3, 8);
        checkLayers(document, 3);
        delete document.layerModel()->takeLayerAt(0);
        checkSliders(2, 7);
        delete document.layerModel()->takeLayerAt(6);
        checkSliders(2, 6);
        document.setLayerVisible(1, false);
        Layer *hiddenLayer = document.layerModel()->takeLayerAt(1);
        document.layerModel()->insertLayer(1, hiddenLayer);
        check(!hiddenLayer->isVisible(), QStringLiteral("Reinserted layers retain individual visibility"));
        checkSliders(2, 6);
        MapDocument reopened(document.map()->clone());
        attach(&reopened);
        checkSliders(6, 6);
        checkLayers(reopened, 6);
        attach(&empty);
        checkSliders(0, 0);
        empty.layerModel()->insertLayer(0, new TileLayer(QStringLiteral("0_Floor"), 0, 0, 8, 8));
        checkSliders(1, 1);
        checkLayers(empty, 1);
        delete empty.layerModel()->takeLayerAt(0);
        checkSliders(0, 0);
    }
    attach(nullptr);
    check(document.undoStack()->isClean() && other.undoStack()->isClean(),
          QStringLiteral("Visibility updates leave the editing history clean"));
    check(settings.value(key).toInt() == 1, QStringLiteral("Visibility does not overwrite legacy settings"));
    if (hadSetting)
        settings.setValue(key, savedSetting);
    else
        settings.remove(key);
    qInfo() << "Layer visibility validation:" << checks << "checks," << failures.size() << "failures";
    *error = failures.join(QLatin1Char('\n'));
    return failures.isEmpty();
}

inline bool validateTileWorkflows(QString *error)
{
    if (!validateLayerVisibility(error))
        return false;

    QStringList failures;
    int checks = 0;
    const auto check = [&](bool valid, const QString &name) {
        ++checks;
        if (!valid)
            failures.append(name);
    };
    Tileset *tiles = TileMetaInfoMgr::instance()->tileset(QStringLiteral("street_curbs_01"));
    if (!tiles || tiles->tileCount() <= 11) {
        *error = QStringLiteral("Street curb tiles are unavailable");
        return false;
    }
    if (!validateLotComposition(tiles, error))
        return false;
    auto *map = new Map(Map::LevelIsometric, 8, 8, 64, 32);
    map->addTileset(tiles);
    auto *furniture = new TileLayer(QStringLiteral("Furniture"), 0, 0, 8, 8);
    auto *walls = new TileLayer(QStringLiteral("Walls"), 0, 0, 8, 8);
    furniture->setCell(1, 1, Cell(tiles->tileAt(11)));
    map->addLayer(furniture);
    map->addLayer(walls);
    MapDocument document(map);
    document.setCurrentLayerIndex(0);
    document.setTileSelection(QRegion(QRect(1, 1, 1, 1)));
    auto *savedClipboard = new QMimeData;
    const QMimeData *previous = QApplication::clipboard()->mimeData();
    for (const QString &format : previous->formats())
        savedClipboard->setData(format, previous->data(format));
    ClipboardManager clipboard;
    TileSelectionScope scope(QStringLiteral("Level"));
    QWidget scopeOwner;
    QToolButton *button = scope.createToolButton(&scopeOwner, []() { return QStringList{QStringLiteral("Furniture"), QStringLiteral("Walls")}; });
    const auto verifyKind = [&](bool singleLayer) {
        check(clipboard.copySelection(&document, &scope), QStringLiteral("Selection copies successfully"));
        Map *copied = clipboard.map();
        check(copied && copied->property(QStringLiteral("pz.selection.kind"))
                == (singleLayer ? QStringLiteral("single-layer") : QStringLiteral("multi-layer")),
              QStringLiteral("Clipboard scope survives serialization"));
        if (copied) {
            qDeleteAll(copied->tilesets());
            delete copied;
        }
    };
    verifyKind(true);
    QMenu *layersMenu = button->menu()->actions().at(0)->menu();
    QMenu *levelsMenu = button->menu()->actions().at(1)->menu();
    layersMenu->actions().at(2)->trigger();
    verifyKind(false);
    layersMenu->actions().at(1)->trigger();
    verifyKind(false);
    layersMenu->actions().at(0)->trigger();
    levelsMenu->actions().at(1)->trigger();
    verifyKind(false);
    QApplication::clipboard()->setMimeData(savedClipboard);
    document.setTileSelection(QRegion());
    document.setCurrentLayerIndex(1);
    {
        MapScene scene(nullptr);
        scene.setMapDocument(&document);
        StampBrush brush;
        QMetaObject::invokeMethod(&brush, "setMapDocument", Qt::DirectConnection, Q_ARG(Tiled::Internal::MapDocument *, &document));
        brush.activate(&scene);
        brush.mouseEntered();
        for (bool structural : {false, true}) {
            TileLayer *stamp = furniture->copy(QRegion(QRect(1, 1, 1, 1)));
            stamp->setName(furniture->name());
            stamp->setLevel(furniture->level());
            if (structural)
                brush.setLayerStamps({stamp}, 0);
            else
                brush.setStamp(stamp);
            const QPointF position = document.renderer()->tileToPixelCoords(4.5, 4.5);
            brush.mouseMoved(position, Qt::NoModifier);
            QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
            press.setButton(Qt::LeftButton);
            press.setButtons(Qt::LeftButton);
            press.setScenePos(position);
            brush.mousePressed(&press);
            QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
            release.setButton(Qt::LeftButton);
            brush.mouseReleased(&release);
            check(!(structural ? furniture : walls)->cellAt(4, 4).isEmpty()
                  && (structural ? walls : furniture)->cellAt(4, 4).isEmpty(),
                  QStringLiteral("Paste targets the layer selected by its scope"));
            document.undoStack()->undo();
            check(furniture->cellAt(4, 4).isEmpty() && walls->cellAt(4, 4).isEmpty(), QStringLiteral("Paste Undo"));
        }
        brush.deactivate(&scene);
        QMetaObject::invokeMethod(&brush, "setMapDocument", Qt::DirectConnection, Q_ARG(Tiled::Internal::MapDocument *, nullptr));
        scene.setMapDocument(nullptr);
    }
    MapNoBlend *mask = map->noBlend(QStringLiteral("Floor"));
    mask->set(1, 1, true);
    mask->set(6, 6, true);
    document.undoStack()->beginMacro(QStringLiteral("Erase curb"));
    CurbTool::restoreBlendTiles(&document, furniture, QRegion(QRect(1, 1, 1, 1)));
    document.undoStack()->push(new EraseTiles(&document, furniture, QRegion(QRect(1, 1, 1, 1))));
    document.undoStack()->endMacro();
    check(!mask->get(1, 1) && mask->get(6, 6), QStringLiteral("Curb erasure restores only affected blend flags"));
    document.undoStack()->undo();
    check(mask->get(1, 1) && !furniture->cellAt(1, 1).isEmpty(), QStringLiteral("Curb and suppression Undo together"));
    furniture->setCell(0, 1, Cell(tiles->tileAt(9)));
    CurbTool::restoreBlendTiles(&document, furniture, QRegion(QRect(1, 1, 1, 1)));
    check(mask->get(1, 1), QStringLiteral("A remaining adjacent curb retains its blend suppression"));

    {
        Map lot(Map::LevelIsometric, 8, 8, 64, 32);
        lot.addTileset(tiles);
        auto *lotWalls = new TileLayer(QStringLiteral("Walls"), 0, 0, 8, 8);
        lotWalls->setCell(1, 1, Cell(tiles->tileAt(10)));
        lot.addLayer(lotWalls);
        MapInfo info(&lot);
        MapComposite *composite = document.mapComposite();
        MapComposite *placed = composite->addMap(&info, QPoint(), 0);
        CompositeLayerGroup *group = composite->tileLayersForLevel(0);
        const auto composition = [&](bool floor) {
            composite->synch();
            group->prepareDrawing(document.renderer(), composite->boundingRect(document.renderer()).toAlignedRect());
            QVector<const Cell *> cells;
            QVector<qreal> opacities;
            OrderedCellsTemporaries renderTemporary;
            group->orderedCellsAt(QPoint(1, 1), cells, opacities,
                                  reinterpret_cast<ZTileLayerGroupRenderData *>(&renderTemporary));
            bool baseFound = false;
            bool lotFound = false;
            for (const Cell *cell : cells) {
                baseFound = baseFound || cell->tile == tiles->tileAt(11);
                lotFound = lotFound || cell->tile == tiles->tileAt(10);
            }
            check(lotFound && baseFound != floor, QStringLiteral("Map layers remain visible only beneath floorless lot squares"));
            group->prepareDrawing2();
            OrderedCellsTemporaries temporary;
            QVector<const Cell *> exportCells;
            group->orderedCellsAt2(QPoint(1, 1), temporary, exportCells);
            check(cells == exportCells, QStringLiteral("Rendering and export agree on retained layers"));
        };
        composition(false);
        auto *floor = new TileLayer(QStringLiteral("Floor"), 0, 0, 8, 8);
        floor->setCell(1, 1, Cell(tiles->tileAt(0)));
        lot.insertLayer(0, floor);
        placed->layerAdded(0);
        composition(true);
        composite->removeMap(placed);
    }
    {
        TilesetView view;
        TilesetModel model(tiles);
        view.setModel(&model);
        view.resize(520, 300);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.show();
        const bool previousIds = Preferences::instance()->showTileIds();
        Preferences::instance()->setShowTileIds(true);
        QApplication::processEvents();
        const QImage numbered = view.grab().toImage();
        Preferences::instance()->setShowTileIds(false);
        QApplication::processEvents();
        check(numbered != view.grab().toImage(), QStringLiteral("Tile-number toggle changes palette pixels"));
        Preferences::instance()->setShowTileIds(previousIds);
    }
    qInfo() << "Tile workflow validation:" << checks << "checks," << failures.size() << "failures";
    *error = failures.join(QLatin1Char('\n'));
    return failures.isEmpty();
}

}
}

#endif
