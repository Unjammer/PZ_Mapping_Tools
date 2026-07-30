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

#include "worldscene.h"

#include "basegraphicsview.h"
#include "biomemapitem.h"
#include "bmptotmx.h"
#include "celldocument.h"
#include "documentmanager.h"
#include "loadthumbnailsdialog.h"
#include "mainwindow.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "preferences.h"
#include "progress.h"
#include "scenetools.h"
#include "thumbnailsettingsmgr.h"
#include "toolmanager.h"
#include "undoredo.h"
#include "world.h"
#include "worlddocument.h"
#include "worldreader.h"
#include "zoomable.h"

#include <qmath.h>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsSceneMouseEvent>
#include <QImageWriter>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QSaveFile>
#include <QStatusBar>
#include <QStyleOptionGraphicsItem>
#include <QThread>
#include <QUrl>

const int WorldScene::ZVALUE_CELLITEM = 1;
const int WorldScene::ZVALUE_ROADITEM_UNSELECTED = 2;
const int WorldScene::ZVALUE_ROADITEM_SELECTED = 3;
const int WorldScene::ZVALUE_ROADITEM_CREATING = 4;
const int WorldScene::ZVALUE_GRIDITEM = 5;
const int WorldScene::ZVALUE_SELECTIONITEM = 6;
const int WorldScene::ZVALUE_COORDITEM = 7;
const int WorldScene::ZVALUE_DNDITEM = 100;

WorldScene::WorldScene(WorldDocument *worldDoc, QObject *parent)
    : BaseGraphicsScene(WorldSceneType, parent)
    , mWorldDoc(worldDoc)
    , mGridItem(new WorldGridItem(this))
    , mCoordItem(new WorldCoordItem(this))
    , mSelectionItem(new WorldSelectionItem(this))
    , mPasteCellsTool(0)
    , mActiveTool(0)
    , mDragMapImageItem(0)
    , mDragBMPItem(0)
    , mZombieSpawnImageItem(nullptr)
    , mBiomeMapItem(nullptr)
    , mBMPToolActive(false)

{
    setBackgroundBrush(Qt::darkGray);

    mGridItem->setZValue(ZVALUE_GRIDITEM);
    addItem(mGridItem);

    mSelectionItem->setZValue(ZVALUE_SELECTIONITEM);
    addItem(mSelectionItem);

    mCoordItem->setZValue(ZVALUE_COORDITEM);
    addItem(mCoordItem);

    connect(mWorldDoc, &WorldDocument::worldAboutToResize,
            this, &WorldScene::worldAboutToResize);
    connect(mWorldDoc, &WorldDocument::worldResized,
            this, &WorldScene::worldResized);

    connect(mWorldDoc, &WorldDocument::selectedCellsChanged,
            this, &WorldScene::selectedCellsChanged);
    connect(mWorldDoc, &WorldDocument::cellMapFileChanged,
            this, &WorldScene::cellMapFileChanged);
    connect(mWorldDoc, &WorldDocument::cellLotAdded,
            this, &WorldScene::cellLotAdded);
    connect(mWorldDoc, &WorldDocument::cellLotAboutToBeRemoved,
            this, &WorldScene::cellLotAboutToBeRemoved);
    connect(mWorldDoc, &WorldDocument::cellLotMoved,
            this, &WorldScene::cellLotMoved);
    connect(mWorldDoc, &WorldDocument::cellContentsChanged,
            this, &WorldScene::cellContentsChanged);

    connect(mWorldDoc, &WorldDocument::cellObjectAdded, this, &WorldScene::cellObjectAdded);
    connect(mWorldDoc, &WorldDocument::cellObjectAboutToBeRemoved, this, &WorldScene::cellObjectAboutToBeRemoved);
    connect(mWorldDoc, &WorldDocument::cellObjectPointMoved, this, &WorldScene::cellObjectPointMoved);
    connect(mWorldDoc, &WorldDocument::cellObjectPointsChanged, this, &WorldScene::cellObjectPointsChanged);

    connect(mWorldDoc, &WorldDocument::generateLotSettingsChanged,
            this, &WorldScene::generateLotsSettingsChanged);

    connect(mWorldDoc, SIGNAL(selectedRoadsChanged()),
            SLOT(selectedRoadsChanged()));
    connect(mWorldDoc, SIGNAL(roadAdded(int)),
            SLOT(roadAdded(int)));
    connect(mWorldDoc, SIGNAL(roadAboutToBeRemoved(int)),
            SLOT(roadAboutToBeRemoved(int)));
    connect(mWorldDoc, SIGNAL(roadCoordsChanged(int)),
            SLOT(roadCoordsChanged(int)));
    connect(mWorldDoc, SIGNAL(roadWidthChanged(int)),
            SLOT(roadWidthChanged(int)));

    connect(mWorldDoc, &WorldDocument::selectedBMPsChanged,
            this, &WorldScene::selectedBMPsChanged);
    connect(mWorldDoc, &WorldDocument::bmpAdded,
            this, &WorldScene::bmpAdded);
    connect(mWorldDoc, &WorldDocument::bmpCoordsChanged,
            this, &WorldScene::bmpCoordsChanged);
    connect(mWorldDoc, &WorldDocument::bmpAboutToBeRemoved,
            this, &WorldScene::bmpAboutToBeRemoved);

    mGridItem->updateBoundingRect();
    setSceneRect(mGridItem->boundingRect());
    mCoordItem->updateBoundingRect();

    Preferences *prefs = Preferences::instance();
    foreach (QString path, world()->otherWorlds()) {
        WorldReader reader;
        World *otherWorld = reader.readWorld(path);
        if (otherWorld) {
            OtherWorld *_otherWorld = new OtherWorld();
            _otherWorld->mWorld = otherWorld;
            _otherWorld->mFileName = path;
            _otherWorld->mCellItems.resize(otherWorld->width() * otherWorld->height());
            mOtherWorlds += _otherWorld;

            foreach (WorldBMP *bmp, otherWorld->bmps()) {
                WorldBMPItem *item = new WorldBMPItem(this, bmp);
                item->setVisible(prefs->showOtherWorlds());
                addItem(item);
                _otherWorld->mBMPItems += item;
            }

            QSet<ThumbnailCell> visibleCells;
            ThumbnailSettingsMgr::instance().visibleCells(path, visibleCells);

            for (int y = 0; y < otherWorld->height(); y++) {
                for (int x = 0; x < otherWorld->width(); x++) {
                    WorldCell *cell = otherWorld->cellAt(x, y);
                    OtherWorldCellItem *item = new OtherWorldCellItem(cell, this);
                    item->setVisible(prefs->showOtherWorlds());
                    addItem(item);
                    item->setZValue(ZVALUE_CELLITEM); // below mGridItem
                    _otherWorld->mCellItems[y * otherWorld->width() + x] = item;
                    if (prefs->showWorldThumbnails()
                            && prefs->loadAllWorldThumbnails()) {
                        _otherWorld->mPendingThumbnails += item;
                    } else if (prefs->showWorldThumbnails()
                               && visibleCells.contains(
                                   ThumbnailCell(cell->x(), cell->y()))) {
                        _otherWorld->mPendingThumbnails += item;
                    }
                }
            }

            if (prefs->showOtherWorlds())
                setSceneRect(sceneRect().united(boundingRect(_otherWorld->adjustedBounds(world()))));
        }
    }

    QSet<ThumbnailCell> visibleCells;
    ThumbnailSettingsMgr::instance().visibleCells(worldDocument()->fileName(), visibleCells);

    mCellItems.resize(world()->width() * world()->height());
    for (int y = 0; y < world()->height(); y++) {
        for (int x = 0; x < world()->width(); x++) {
            WorldCell *cell = world()->cellAt(x, y);
            WorldCellItem *item = new WorldCellItem(cell, this);
            addItem(item);
            item->setZValue(ZVALUE_CELLITEM); // below mGridItem
            mCellItems[y * world()->width() + x] = item;
            if (prefs->showWorldThumbnails()
                    && prefs->loadAllWorldThumbnails()) {
                mPendingThumbnails += item;
            } else if (prefs->showWorldThumbnails()
                       && visibleCells.contains(
                           ThumbnailCell(cell->x(), cell->y()))) {
                mPendingThumbnails += item;
            }
        }
    }

    foreach (Road *road, world()->roads()) {
        WorldRoadItem *item = new WorldRoadItem(this, road);
        item->setZValue(ZVALUE_ROADITEM_UNSELECTED);
        addItem(item);
        mRoadItems += item;
    }

    mGridItem->setVisible(prefs->showWorldGrid());
    mCoordItem->setVisible(prefs->showCoordinates());
    connect(prefs, &Preferences::showWorldGridChanged, this, &WorldScene::setShowGrid);
    connect(prefs, &Preferences::gridColorChanged, this, [this]{ mGridItem->update(); update(); });
    connect(prefs, &Preferences::gridWidthChanged, this, [this]{ mGridItem->update(); update(); });
    connect(prefs, &Preferences::showCoordinatesChanged, this, &WorldScene::setShowCoordinates);
    connect(prefs, &Preferences::showBMPsChanged,
            this, &WorldScene::setShowBMPs);
    connect(prefs, &Preferences::showZombieSpawnImageChanged, this, &WorldScene::setShowZombieSpawnImage);
    connect(prefs, &Preferences::zombieSpawnImageOpacityChanged, this, &WorldScene::zombieSpawnImageOpacityChanged);
    connect(prefs, &Preferences::showBiomeMapChanged,
            this, &WorldScene::setShowBiomeMap);
    connect(prefs, &Preferences::biomeMapOpacityChanged,
            this, &WorldScene::biomeMapOpacityChanged);
    connect(prefs, &Preferences::showZonesInWorldViewChanged, this, &WorldScene::setShowZonesInWorldView);
    connect(prefs, &Preferences::showOtherWorldsChanged, this, &WorldScene::setShowOtherWorlds);
    connect(prefs, &Preferences::loadAllWorldThumbnailsChanged,
            this, &WorldScene::loadAllWorldThumbnailsChanged);
    connect(prefs, &Preferences::showWorldThumbnailsChanged,
            this, &WorldScene::showWorldThumbnailsChanged);

    mPasteCellsTool = PasteCellsTool::instance();

    foreach (WorldBMP *bmp, world()->bmps()) {
        WorldBMPItem *item = new WorldBMPItem(this, bmp);
        item->setVisible(prefs->showBMPs() && !prefs->showZonesInWorldView());
        addItem(item);
        mBMPItems += item;
    }

    mZombieSpawnImageItem = new ZombieSpawnImageItem(this);
    mZombieSpawnImageItem->setZValue(ZVALUE_CELLITEM + 1);
    mZombieSpawnImageItem->setVisible(prefs->showZombieSpawnImage());
    mZombieSpawnImageItem->setOpacity(prefs->zombieSpawnImageOpacity());
    addItem(mZombieSpawnImageItem);

    mBiomeMapItem = new BiomeMapItem(this);
    mBiomeMapItem->setZValue(ZVALUE_CELLITEM + 0.5);
    mBiomeMapItem->setVisible(prefs->showBiomeMap());
    mBiomeMapItem->setOpacity(prefs->biomeMapOpacity());
    addItem(mBiomeMapItem);

    connect(MapManager::instance(), &MapManager::mapFileCreated,
            this, &WorldScene::mapFileCreated);

    connect(MapImageManager::instance(), &MapImageManager::mapImageChanged,
            this, &WorldScene::mapImageChanged);
    connect(MapImageManager::instance(),
            &MapImageManager::mapImageFailedToLoad,
            this, &WorldScene::mapImageChanged);

    if (prefs->showWorldThumbnails()
            && prefs->loadAllWorldThumbnails()) {
        startThumbnailProgress();
    } else {
        handlePendingThumbnails();
    }
}

WorldScene::~WorldScene()
{
    qDeleteAll(mOtherWorlds);
}

void WorldScene::setTool(AbstractTool *tool)
{
    BaseWorldSceneTool *worldTool = tool ? tool->asWorldTool() : 0;

    if (mActiveTool == worldTool)
        return;

    if (mActiveTool) {
        mActiveTool->deactivate();
    }

    mActiveTool = worldTool;

    if (mActiveTool) {
        mActiveTool->activate();
    }

    if (mActiveTool != WorldEditRoadTool::instance()) {
        for (WorldRoadItem *item : std::as_const(mRoadItems)) {
            item->setEditable(false);
        }
    }

    bool bmpToolActive = mActiveTool == WorldBMPTool::instance();
    if (bmpToolActive != mBMPToolActive) {
        if (bmpToolActive) {
            worldDocument()->setSelectedCells(QList<WorldCell*>());
            foreach (WorldCellItem *item, mCellItems)
                item->setVisible(false); //item->setOpacity(0.2);
            setShowBMPs(true);
        } else {
            worldDocument()->setSelectedBMPs(QList<WorldBMP*>());
            foreach (WorldCellItem *item, mCellItems)
                item->setVisible(true); //item->setOpacity(1.0);
            setShowBMPs(Preferences::instance()->showBMPs());
        }
        mBMPToolActive = bmpToolActive;

        foreach (OtherWorld *otherWorld, mOtherWorlds) {
            foreach (OtherWorldCellItem *item, otherWorld->mCellItems)
                item->setVisible(Preferences::instance()->showOtherWorlds() && !mBMPToolActive);
        }
    }
}

World *WorldScene::world() const
{
    return mWorldDoc ? mWorldDoc->world() : 0;
}

WorldCellItem *WorldScene::itemForCell(WorldCell *cell)
{
    return itemForCell(cell->x(), cell->y());
}

WorldCellItem *WorldScene::itemForCell(int x, int y)
{
    if (!world()->contains(x, y))
        return 0;
    return mCellItems[y * world()->width() + x];
}

QPoint WorldScene::pixelToRoadCoords(qreal x, qreal y) const
{
    QPointF cellPos = pixelToCellCoords(x, y);
    return QPoint(cellPos.x() * world()->cellSize(),
                  cellPos.y() * world()->cellSize());
}

QPointF WorldScene::roadToSceneCoords(const QPoint &pt) const
{
    return cellToPixelCoords(QPointF(pt) / world()->cellSize());
}

QPolygonF WorldScene::roadRectToScenePolygon(const QRect &roadRect) const
{
    QPolygonF polygon;
    QRect adjusted = roadRect.adjusted(0, 0, 1, 1);
    polygon += roadToSceneCoords(adjusted.topLeft());
    polygon += roadToSceneCoords(adjusted.topRight());
    polygon += roadToSceneCoords(adjusted.bottomRight());
    polygon += roadToSceneCoords(adjusted.bottomLeft());
    return polygon;
}

WorldRoadItem *WorldScene::itemForRoad(Road *road)
{
    foreach (WorldRoadItem *item, mRoadItems)
        if (item->road() == road)
            return item;

    return 0;
}

QList<Road *> WorldScene::roadsInRect(const QRectF &bounds)
{
    QPolygonF polygon;
    polygon += cellToPixelCoords(bounds.topLeft());
    polygon += cellToPixelCoords(bounds.topRight());
    polygon += cellToPixelCoords(bounds.bottomRight());
    polygon += cellToPixelCoords(bounds.bottomLeft());

    QList<Road*> result;
    foreach (QGraphicsItem *item, items(polygon)) {
        if (WorldRoadItem *roadItem = dynamic_cast<WorldRoadItem*>(item))
            result += roadItem->road();
    }
    return result;
}

QList<WorldBMP *> WorldScene::bmpsInRect(const QRectF &cellRect)
{
    QPolygonF polygon = cellRectToPolygon(cellRect);

    QList<WorldBMP*> result;
    foreach (QGraphicsItem *item, items(polygon)) {
        if (WorldBMPItem *bmpItem = dynamic_cast<WorldBMPItem*>(item))
            if (bmpItem->bmp()->world() == world()) // not an OtherWorld.mBMP
                result += bmpItem->bmp();
    }
    return result;
}

void WorldScene::pasteCellsFromClipboard()
{
    ToolManager::instance()->selectTool(mPasteCellsTool);
    //    mActiveTool = mPasteCellsTool;
}

void WorldScene::cancelLoadingThumbnails()
{
    mPendingThumbnails.clear();
    for (OtherWorld *otherWorld : std::as_const(mOtherWorlds))
        otherWorld->mPendingThumbnails.clear();
    if (mLoadThumbnailsDialog)
        mLoadThumbnailsDialog->close();
    mThumbnailLoadTotal = 0;
}

void WorldScene::worldAboutToResize(const QSize &newSize)
{
    // Delete items for cells that are getting chopped off.
    QRect newBounds = QRect(QPoint(0, 0), newSize);
    for (int x = 0; x < world()->width(); x++)
        for (int y = 0; y < world()->height(); y++) {
            if (!newBounds.contains(x, y)) {
                delete itemForCell(x, y);
                mCellItems[x + y * world()->width()] = 0;
            }
        }
}

void WorldScene::worldResized(const QSize &oldSize)
{
    QVector<WorldCellItem*> items(world()->width() * world()->height());

    // Reuse items still in bounds.
    for (int x = 0; x < qMin(oldSize.width(), world()->width()); x++) {
        for (int y = 0; y < qMin(oldSize.height(), world()->height()); y++) {
            WorldCellItem *item = mCellItems[x + y * oldSize.width()];
            item->worldResized();
            items[x + y * world()->width()] = item;
        }
    }

    // Create new items for new cells.
    QRect oldBounds = QRect(QPoint(0, 0), oldSize);
    for (int x = 0; x < world()->width(); x++) {
        for (int y = 0; y < world()->height(); y++) {
            if (!oldBounds.contains(x, y)) {
                WorldCellItem *item = new WorldCellItem(world()->cellAt(x, y), this);
                item->setZValue(ZVALUE_CELLITEM);
                items[x + y * world()->width()] = item;
                addItem(item);
            }
        }
    }

    mCellItems = items;
    mGridItem->updateBoundingRect();
    setSceneRect(mGridItem->boundingRect());
    mCoordItem->updateBoundingRect();
    mSelectionItem->updateBoundingRect();
    foreach (WorldBMPItem *item, mBMPItems)
        item->synchWithBMP();
    foreach (WorldRoadItem *item, mRoadItems)
        item->synchWithRoad();
}

void WorldScene::generateLotsSettingsChanged()
{
    mCoordItem->update();
    if (mZombieSpawnImageItem)
        mZombieSpawnImageItem->reloadFromSettings();
    if (mBiomeMapItem)
        mBiomeMapItem->reloadFromSettings();
    ZombieHeatMapTool::instance()->updateEnabledState();
    BiomeMapTool::instance()->updateEnabledState();
}

QPointF WorldScene::pixelToCellCoords(qreal x, qreal y) const
{
    const int tileWidth = GRID_WIDTH;
    const int tileHeight = GRID_HEIGHT;
    const qreal ratio = (qreal) tileWidth / tileHeight;

    x -= world()->height() * tileWidth / 2;
    const qreal mx = y + (x / ratio);
    const qreal my = y - (x / ratio);

    return QPointF(mx / tileHeight,
                   my / tileHeight);
}

QPoint WorldScene::pixelToCellCoordsInt(const QPointF &point) const
{
    QPointF pos = pixelToCellCoords(point.x(), point.y());
    return QPoint(qFloor(pos.x()), qFloor(pos.y()));
}

WorldCell *WorldScene::pointToCell(const QPointF &point)
{
    QPoint cellCoords = pixelToCellCoordsInt(point);
    if (world()->contains(cellCoords))
        return world()->cellAt(cellCoords);
    return NULL;
}

QPointF WorldScene::cellToPixelCoords(qreal x, qreal y) const
{
    const int tileWidth = GRID_WIDTH;
    const int tileHeight = GRID_HEIGHT;
    const int originX = world()->height() * tileWidth / 2;

    return QPointF((x - y) * tileWidth / 2 + originX,
                   (x + y) * tileHeight / 2);
}

QRectF WorldScene::boundingRect(const QRect &rect) const
{
    const int tileWidth = GRID_WIDTH;
    const int tileHeight = GRID_HEIGHT;

    const int originX = world()->height() * tileWidth / 2;
    const QPoint pos((rect.x() - (rect.y() + rect.height()))
                     * tileWidth / 2 + originX,
                     (rect.x() + rect.y()) * tileHeight / 2);

    const int side = rect.height() + rect.width();
    const QSize size(side * tileWidth / 2,
                     side * tileHeight / 2);

    return QRect(pos, size);
}

QRectF WorldScene::boundingRect(const QPoint &pos) const
{
    return boundingRect(QRect(pos, QSize(1,1)));
}

QRectF WorldScene::boundingRect(int x, int y) const
{
    return boundingRect(QRect(x, y, 1, 1));
}

QPolygonF WorldScene::cellRectToPolygon(const QRectF &rect) const
{
    const QPointF topLeft = cellToPixelCoords(rect.topLeft());
    const QPointF topRight = cellToPixelCoords(rect.topRight());
    const QPointF bottomRight = cellToPixelCoords(rect.bottomRight());
    const QPointF bottomLeft = cellToPixelCoords(rect.bottomLeft());
    QPolygonF polygon;
    polygon << topLeft << topRight << bottomRight << bottomLeft;
    return polygon;
}

QPolygonF WorldScene::cellRectToPolygon(WorldCell *cell) const
{
    return cellRectToPolygon(QRect(cell->pos(), QSize(1,1)));
}

void WorldScene::selectedCellsChanged()
{
    foreach (WorldCellItem *item, mSelectedCellItems)
        item->setSelected(false);

    mSelectedCellItems.clear();
    foreach (WorldCell *cell, mWorldDoc->selectedCells()) {
        itemForCell(cell)->setSelected(true);
        mSelectedCellItems += itemForCell(cell);
    }
}

void WorldScene::cellMapFileChanged(WorldCell *cell)
{
    itemForCell(cell)->updateCellImage();
    itemForCell(cell)->updateBoundingRect();
    itemForCell(cell)->update();
}

void WorldScene::cellLotAdded(WorldCell *cell, int index)
{
    itemForCell(cell)->lotAdded(index);
    itemForCell(cell)->update();
}

void WorldScene::cellLotAboutToBeRemoved(WorldCell *cell, int index)
{
    itemForCell(cell)->lotRemoved(index);
    itemForCell(cell)->update();
}

void WorldScene::cellLotMoved(WorldCellLot *lot)
{
    itemForCell(lot->cell())->lotMoved(lot);
}

void WorldScene::cellContentsChanged(WorldCell *cell)
{
    itemForCell(cell)->cellContentsChanged();
}

void WorldScene::cellObjectAdded(WorldCell *cell, int objectIndex)
{
    itemForCell(cell)->objectPointsChanged(objectIndex);
}

void WorldScene::cellObjectAboutToBeRemoved(WorldCell *cell, int objectIndex)
{
    itemForCell(cell)->objectPointsChanged(objectIndex);
}

void WorldScene::cellObjectPointMoved(WorldCell *cell, int objectIndex, int pointIndex)
{
    Q_UNUSED(pointIndex)
    itemForCell(cell)->objectPointsChanged(objectIndex);
}

void WorldScene::cellObjectPointsChanged(WorldCell *cell, int objectIndex)
{
    itemForCell(cell)->objectPointsChanged(objectIndex);
}

void WorldScene::setShowGrid(bool show)
{
    mGridItem->setVisible(show);
}

void WorldScene::setShowCoordinates(bool show)
{
    mCoordItem->setVisible(show);
}

void WorldScene::setShowBMPs(bool show)
{
    if (Preferences::instance()->showZonesInWorldView()) {
        show = false;
    }
    for (WorldBMPItem *bmpItem : std::as_const(mBMPItems)) {
        bmpItem->setVisible(show);
    }
    for (OtherWorld *otherWorld : std::as_const(mOtherWorlds)) {
        for (WorldBMPItem *item : std::as_const(otherWorld->mBMPItems)) {
            item->setVisible(show && Preferences::instance()->showOtherWorlds());
        }
    }
}

void WorldScene::setShowOtherWorlds(bool show)
{
    QRectF bounds = mGridItem->boundingRect();
    for (OtherWorld *otherWorld : std::as_const(mOtherWorlds)) {
        for (WorldBMPItem *item : std::as_const(otherWorld->mBMPItems)) {
            item->setVisible(show && Preferences::instance()->showBMPs());
        }
        for (OtherWorldCellItem *item : std::as_const(otherWorld->mCellItems)) {
            item->setVisible(show && !mBMPToolActive);
        }
        if (show) {
            bounds = bounds.united(boundingRect(otherWorld->adjustedBounds(world())));
        }
    }
    setSceneRect(bounds);
}

void WorldScene::setShowZombieSpawnImage(bool show)
{
    if (show)
        mZombieSpawnImageItem->reloadFromSettings();
    mZombieSpawnImageItem->setVisible(show);
}

void WorldScene::zombieSpawnImageOpacityChanged(qreal opacity)
{
    mZombieSpawnImageItem->setOpacity(opacity);
    mZombieSpawnImageItem->update();
}

void WorldScene::setShowBiomeMap(bool show)
{
    if (show && mBiomeMapItem)
        mBiomeMapItem->reloadFromSettings(true);
    if (mBiomeMapItem)
        mBiomeMapItem->setVisible(show);
}

void WorldScene::biomeMapOpacityChanged(qreal opacity)
{
    if (!mBiomeMapItem)
        return;
    mBiomeMapItem->setOpacity(opacity);
    mBiomeMapItem->update();
}

void WorldScene::setShowZonesInWorldView(bool show)
{
    Q_UNUSED(show)

    setShowBMPs(Preferences::instance()->showBMPs());
    // update() to redisplay WorldCells also.
    update();
}

void WorldScene::selectedRoadsChanged()
{
    const QList<Road*> &selection = worldDocument()->selectedRoads();

    QSet<WorldRoadItem*> items;
    foreach (Road *road, selection)
        items.insert(itemForRoad(road));

    foreach (WorldRoadItem *item, mSelectedRoadItems - items) {
        item->setSelected(false);
        item->setEditable(false);
        item->setZValue(ZVALUE_ROADITEM_UNSELECTED);
    }

    bool editable = WorldEditRoadTool::instance()->isCurrent();
    foreach (WorldRoadItem *item, items - mSelectedRoadItems) {
        item->setSelected(true);
        item->setEditable(editable);
        item->setZValue(ZVALUE_ROADITEM_SELECTED);
    }

    mSelectedRoadItems = items;
}

void WorldScene::roadAdded(int index)
{
    Road *road = world()->roads().at(index);
    Q_ASSERT(itemForRoad(road) == 0);
    WorldRoadItem *item = new WorldRoadItem(this, road);
    item->setZValue(ZVALUE_ROADITEM_UNSELECTED);
    addItem(item);
    mRoadItems += item;
}

void WorldScene::roadAboutToBeRemoved(int index)
{
    Road *road = world()->roads().at(index);
    WorldRoadItem *item = itemForRoad(road);
    Q_ASSERT(item);
    mRoadItems.removeAll(item);
    mSelectedRoadItems.remove(item); // paranoia
    removeItem(item);
    delete item;
}

void WorldScene::roadCoordsChanged(int index)
{
    Road *road = world()->roads().at(index);
    WorldRoadItem *item = itemForRoad(road);
    Q_ASSERT(item);
    item->synchWithRoad();
    item->update();
}

void WorldScene::roadWidthChanged(int index)
{
    Road *road = world()->roads().at(index);
    WorldRoadItem *item = itemForRoad(road);
    Q_ASSERT(item);
    item->synchWithRoad();
    item->update();
}

void WorldScene::selectedBMPsChanged()
{
    const QList<WorldBMP*> &selection = worldDocument()->selectedBMPs();

    QSet<WorldBMPItem*> items;
    foreach (WorldBMP *bmp, selection)
        items.insert(itemForBMP(bmp));

    foreach (WorldBMPItem *item, mSelectedBMPItems - items) {
        item->setSelected(false);
    }

    foreach (WorldBMPItem *item, items - mSelectedBMPItems) {
        item->setSelected(true);
    }

    mSelectedBMPItems = items;
}

void WorldScene::bmpAdded(int index)
{
    WorldBMP *bmp = world()->bmps().at(index);
    Q_ASSERT(!itemForBMP(bmp));
    WorldBMPItem *item = new WorldBMPItem(this, bmp);
    addItem(item);
    mBMPItems.insert(index, item);
    qreal zValue = 0.0;
    for (WorldBMPItem *item2 : std::as_const(mBMPItems)) {
        item2->setZValue(zValue += 0.01);
    }
}

void WorldScene::bmpAboutToBeRemoved(int index)
{
    WorldBMP *bmp = world()->bmps().at(index);
    WorldBMPItem *item = itemForBMP(bmp);
    Q_ASSERT(item);
    mBMPItems.removeAll(item);
    mSelectedBMPItems.remove(item); // paranoia
    removeItem(item);
    delete item;
    qreal zValue = 0.0;
    for (WorldBMPItem *item2 : std::as_const(mBMPItems)) {
        item2->setZValue(zValue += 0.01);
    }
}

void WorldScene::bmpCoordsChanged(int index)
{
    WorldBMP *bmp = world()->bmps().at(index);
    WorldBMPItem *item = itemForBMP(bmp);
    Q_ASSERT(item);
    item->synchWithBMP();
}

WorldBMP *WorldScene::pointToBMP(const QPointF &scenePos)
{
    QPoint cellPos = pixelToCellCoordsInt(scenePos);
    WorldBMP *ret = 0;
    foreach (WorldBMP *bmp, world()->bmps()) {
        if (bmp->bounds().contains(cellPos))
            ret = bmp;
    }
    return ret;
}

WorldBMPItem *WorldScene::itemForBMP(WorldBMP *bmp)
{
    foreach (WorldBMPItem *item, mBMPItems) {
        if (item->bmp() == bmp)
            return item;
    }
    return 0;
}

void WorldScene::mapFileCreated(const QString &path)
{
    foreach (WorldCellItem *item, mCellItems)
        item->mapFileCreated(path);
}

void WorldScene::mapImageChanged(MapImage *mapImage)
{
    foreach (WorldCellItem *item, mCellItems)
        item->mapImageChanged(mapImage);
    handlePendingThumbnails();
}

void WorldScene::loadAllWorldThumbnailsChanged(bool thumbs)
{
    thumbs = thumbs && Preferences::instance()->showWorldThumbnails();

    foreach (OtherWorld *otherWorld, mOtherWorlds) {
        otherWorld->mPendingThumbnails.clear();
        foreach (OtherWorldCellItem *item, otherWorld->mCellItems) {
            if (thumbs)
                otherWorld->mPendingThumbnails += item;
            else
                item->thumbnailsAreFail();
        }
    }

    mPendingThumbnails.clear();
    if (thumbs) {
        for (WorldCellItem *item : std::as_const(mCellItems)) {
            mPendingThumbnails += item;
        }
        startThumbnailProgress();
    } else {
        for (WorldCellItem *item : std::as_const(mCellItems)) {
            item->thumbnailsAreFail();
        }
    }
}

void WorldScene::showWorldThumbnailsChanged(bool show)
{
    mPendingThumbnails.clear();
    for (OtherWorld *otherWorld : std::as_const(mOtherWorlds))
        otherWorld->mPendingThumbnails.clear();

    if (!show) {
        update();
        return;
    }

    if (Preferences::instance()->loadAllWorldThumbnails()) {
        loadAllWorldThumbnailsChanged(true);
        return;
    }

    QSet<ThumbnailCell> visibleCells;
    ThumbnailSettingsMgr::instance().visibleCells(
                worldDocument()->fileName(), visibleCells);
    for (WorldCellItem *item : std::as_const(mCellItems)) {
        const QPoint pos = item->cell()->pos();
        if (visibleCells.contains(ThumbnailCell(pos.x(), pos.y())))
            mPendingThumbnails += item;
    }

    for (OtherWorld *otherWorld : std::as_const(mOtherWorlds)) {
        ThumbnailSettingsMgr::instance().visibleCells(
                    otherWorld->mFileName, visibleCells);
        for (OtherWorldCellItem *item : std::as_const(otherWorld->mCellItems)) {
            const QPoint pos = item->cell()->pos();
            if (visibleCells.contains(ThumbnailCell(pos.x(), pos.y())))
                otherWorld->mPendingThumbnails += item;
        }
    }

    handlePendingThumbnails();
    update();
}

void WorldScene::handlePendingThumbnails()
{
    int availableSlots =
            qBound(1, QThread::idealThreadCount() - 1, 4);

    for (int i = 0;
         i < mPendingThumbnails.size() && availableSlots > 0;) {
        WorldCellItem *item = mPendingThumbnails.at(i);
        WorldCellItem::ThumbnailStatus status = item->thumbnailsAreGo();
        if (status == WorldCellItem::ThumbnailStatus::Loading) {
            --availableSlots;
            ++i;
        } else {
            mPendingThumbnails.removeAt(i);
        }
    }

    for (OtherWorld *otherWorld : std::as_const(mOtherWorlds)) {
        for (int i = 0;
             i < otherWorld->mPendingThumbnails.size()
             && availableSlots > 0;) {
            OtherWorldCellItem *item =
                    otherWorld->mPendingThumbnails.at(i);
            WorldCellItem::ThumbnailStatus status = item->thumbnailsAreGo();
            if (status == WorldCellItem::ThumbnailStatus::Loading) {
                --availableSlots;
                ++i;
            } else {
                otherWorld->mPendingThumbnails.removeAt(i);
            }
        }
    }

    updateThumbnailProgress();
}

int WorldScene::pendingThumbnailCount() const
{
    int count = mPendingThumbnails.size();
    for (OtherWorld *otherWorld : mOtherWorlds)
        count += otherWorld->mPendingThumbnails.size();
    return count;
}

void WorldScene::startThumbnailProgress()
{
    mThumbnailLoadTotal = pendingThumbnailCount();
    if (mThumbnailLoadTotal <= 0) {
        handlePendingThumbnails();
        return;
    }

    if (mLoadThumbnailsDialog)
        mLoadThumbnailsDialog->close();

    mLoadThumbnailsDialog =
            new LoadThumbnailsDialog(this, MainWindow::instance());
    mLoadThumbnailsDialog->setAttribute(Qt::WA_DeleteOnClose);
    mLoadThumbnailsDialog->setModal(false);
    mLoadThumbnailsDialog->show();
    handlePendingThumbnails();
}

void WorldScene::updateThumbnailProgress()
{
    if (!mLoadThumbnailsDialog)
        return;

    const int pending = pendingThumbnailCount();
    const int completed = qMax(0, mThumbnailLoadTotal - pending);
    mLoadThumbnailsDialog->setPrompt(
                tr("Loading thumbnails %1 / %2")
                .arg(completed).arg(mThumbnailLoadTotal));
    if (pending == 0) {
        mLoadThumbnailsDialog->close();
        mThumbnailLoadTotal = 0;
    }
}

void WorldScene::keyPressEvent(QKeyEvent *event)
{
    QGraphicsScene::keyPressEvent(event);
    if (event->isAccepted())
        return;

    if (mActiveTool)
        mActiveTool->keyPressEvent(event);
}

void WorldScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mousePressEvent(event);
    if (event->isAccepted())
        return;

    if (mActiveTool) {
        mDoubleClick = false;
        mActiveTool->setEventView(mEventView);
        mActiveTool->mousePressEvent(event);
    }
}

void WorldScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseMoveEvent(event);
    if (event->isAccepted())
        return;

    if (mActiveTool) {
        mActiveTool->setEventView(mEventView);
        mActiveTool->mouseMoveEvent(event);
    }
}

void WorldScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseReleaseEvent(event);
    if (event->isAccepted())
        return;

    if (mActiveTool) {
        mActiveTool->setEventView(mEventView);
        mActiveTool->mouseReleaseEvent(event);

        if (mDoubleClick) {
            if (WorldCell *cell = pointToCell(event->scenePos())) {
                mWorldDoc->editCell(cell->x(), cell->y());
            }
        }
    }
}

void WorldScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (mActiveTool != WorldCellTool::instance())
        return;

    mDoubleClick = true;
    WorldCellTool::instance()->mouseDoubleClickEvent(event);
}

void WorldScene::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    mDragBMPError.clear();

    if (!world()) {
        event->ignore();
        return;
    }

    foreach (const QUrl &url, event->mimeData()->urls()) {
        QFileInfo info(url.toLocalFile());
        if (!info.exists()) continue;
        if (!info.isFile()) continue;

        if (BMPToTMX::supportedImageFormats().contains(info.suffix().toLower())) {
            QString mainImagePath = info.canonicalFilePath();
            QString baseName = info.completeBaseName();
            if (baseName.endsWith(QLatin1String("_veg"), Qt::CaseInsensitive)) {
                baseName.chop(4);
                mainImagePath = info.absoluteDir().filePath(
                            baseName + QLatin1Char('.') + info.suffix());
            }

            QSize size = BMPToTMX::instance()->validateImages(
                        mainImagePath, world()->cellSize());
            if (size.isEmpty()) {
                mDragBMPError = tr("The image pair could not be added.\n\n%1")
                        .arg(BMPToTMX::instance()->errorString());
                continue;
            }
            QPoint center = pixelToCellCoordsInt(event->scenePos());
            const int cellSize = world()->cellSize();
            int width = size.width() / cellSize;
            int height = size.height() / cellSize;
            int x = center.x() - width / 2;
            int y = center.y() - height / 2;
            mDragBMPError.clear();
            WorldBMP *bmp = new WorldBMP(world(), x, y, width, height,
                                         QFileInfo(mainImagePath).canonicalFilePath());
            mDragBMPItem = new WorldBMPItem(this, bmp);
            mDragBMPItem->setZValue(ZVALUE_DNDITEM);
            addItem(mDragBMPItem);
            if (MainWindow::instance()) {
                MainWindow::instance()->statusBar()->showMessage(
                            tr("Valid image pair: %1 + %2 (%3 x %4 pixels). Drop to add it.")
                            .arg(QFileInfo(mainImagePath).fileName())
                            .arg(QFileInfo(mainImagePath).completeBaseName()
                                 + QLatin1String("_veg.")
                                 + QFileInfo(mainImagePath).suffix())
                            .arg(size.width()).arg(size.height()), 10000);
            }
            event->accept();
            return;
        }

        if (info.suffix() != QLatin1String("tmx")) continue;

        MapInfo *mapInfo = MapManager::instance()->mapInfo(info.canonicalFilePath());
        if (!mapInfo)
            continue;

        const int cellSize = world()->cellSize();
        if (mapInfo->size() != QSize(cellSize, cellSize))
            continue;

        mDragBMPError.clear();
        mDragMapImageItem = new DragMapImageItem(mapInfo, this);
        mDragMapImageItem->setZValue(ZVALUE_DNDITEM);
        mDragMapImageItem->setScenePos(event->scenePos());
        addItem(mDragMapImageItem);
        event->accept();
        return;
    }

    if (!mDragBMPError.isEmpty()) {
        if (MainWindow::instance()) {
            MainWindow::instance()->statusBar()->showMessage(
                        tr("Invalid image pair: %1")
                        .arg(BMPToTMX::instance()->errorString()), 10000);
        }
        event->accept();
        return;
    }

    event->ignore();
}

void WorldScene::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    if (mDragMapImageItem) {
        mDragMapImageItem->setScenePos(event->scenePos());
    }

    if (mDragBMPItem) {
        QPoint center = pixelToCellCoordsInt(event->scenePos());
        mDragBMPItem->bmp()->setPos(center.x() - mDragBMPItem->bmp()->width() / 2,
                                    center.y() - mDragBMPItem->bmp()->height() / 2);
        mDragBMPItem->synchWithBMP();
    }
}

void WorldScene::dragLeaveEvent(QGraphicsSceneDragDropEvent *event)
{
    Q_UNUSED(event)
    if (mDragMapImageItem) {
        delete mDragMapImageItem;
        mDragMapImageItem = 0;
    }

    if (mDragBMPItem) {
        delete mDragBMPItem->bmp();
        delete mDragBMPItem;
        mDragBMPItem = 0;
    }
    mDragBMPError.clear();
}

void WorldScene::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    if (!mDragBMPError.isEmpty()) {
        qWarning().noquote() << mDragBMPError;
        QMessageBox::warning(event->widget(), tr("Image pair not added"),
                             mDragBMPError);
        if (MainWindow::instance()) {
            MainWindow::instance()->statusBar()->showMessage(
                        tr("Image pair was not added."), 10000);
        }
        mDragBMPError.clear();
        event->accept();
        return;
    }

    if (mDragMapImageItem) {
        if (WorldCell *cell = world()->cellAt(mDragMapImageItem->dropPos()))
            mWorldDoc->setCellMapName(cell, mDragMapImageItem->mapFilePath());
        delete mDragMapImageItem;
        mDragMapImageItem = 0;
        event->accept();
        return;
    }

    if (mDragBMPItem) {
        const QString mainImagePath = mDragBMPItem->bmp()->filePath();
        const int cellSize = world()->cellSize();
        const QSize imageSize(mDragBMPItem->bmp()->width() * cellSize,
                              mDragBMPItem->bmp()->height() * cellSize);
        mWorldDoc->insertBMP(mWorldDoc->world()->bmps().count(), mDragBMPItem->bmp());
        delete mDragBMPItem;
        mDragBMPItem = 0;
        const QString message = tr("Image pair added: %1 + %2 (%3 x %4 pixels).")
                .arg(QFileInfo(mainImagePath).fileName())
                .arg(QFileInfo(mainImagePath).completeBaseName()
                     + QLatin1String("_veg.") + QFileInfo(mainImagePath).suffix())
                .arg(imageSize.width()).arg(imageSize.height());
        qInfo().noquote() << message;
        if (MainWindow::instance())
            MainWindow::instance()->statusBar()->showMessage(message, 10000);
        event->accept();
        return;
    }

    event->ignore();
}

///// ///// ///// ///// /////

static QSize mapSize(int mapWidth, int mapHeight, int tileWidth, int tileHeight)
{
    // Map width and height contribute equally in both directions
    const int side = mapHeight + mapWidth;
    return QSize(side * tileWidth / 2,
                 side * tileHeight / 2);
}

BaseCellItem::BaseCellItem(WorldScene *scene, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , mScene(scene)
    , mMapImage(nullptr)
    , mWantsImages(true)
{
    setAcceptedMouseButtons(Qt::MouseButton::NoButton);
#ifndef QT_NO_DEBUG
    mUpdatingImage = false;
#endif
}

void BaseCellItem::initialize()
{
    updateCellImage();

    for (int i = 0; i < lots().size(); i++)
        updateLotImage(i);

    updateBoundingRect();
}

QRectF BaseCellItem::boundingRect() const
{
    return mBoundingRect;
}

QPainterPath BaseCellItem::shape() const
{
    QPainterPath path;
    path.addPolygon(mScene->cellRectToPolygon(QRect(cellPos(), QSize(1, 1))).translated(mDrawOffset));
    return path;
}

void BaseCellItem::paint(QPainter *painter,
                         const QStyleOptionGraphicsItem *option,
                         QWidget *)
{
    Q_UNUSED(option)

    paintThumbnails(painter);

#ifndef QT_NO_DEBUG
    painter->drawRect(mBoundingRect);
#endif
}

void BaseCellItem::paintThumbnails(QPainter *painter) const
{
    if (Preferences::instance()->showWorldThumbnails()) {
        if (mMapImage && mMapImage->isLoaded()) {
            QRectF target = mMapImageBounds.translated(mDrawOffset);
            QRectF source = QRect(QPoint(0, 0), mMapImage->image().size());
            painter->drawImage(target, mMapImage->image(), source);
        }

        for (const LotImage &lotImage : std::as_const(mLotImages)) {
            if (!lotImage.mMapImage || !lotImage.mMapImage->isLoaded()) continue;
            QRectF target = lotImage.mBounds.translated(mDrawOffset);
            QRectF source = QRect(QPoint(0, 0), lotImage.mMapImage->image().size());
            painter->drawImage(target, lotImage.mMapImage->image(), source);
        }
    }

}

void BaseCellItem::updateCellImage()
{
    mMapImage = nullptr;
    mMapImageBounds = QRect();
    if (mWantsImages && !mapFilePath().isEmpty()) {
#ifndef QT_NO_DEBUG
        Q_ASSERT(!mUpdatingImage);
        mUpdatingImage = true;
#endif
        mMapImage = MapImageManager::instance()->getMapImage(mapFilePath());
#ifndef QT_NO_DEBUG
        mUpdatingImage = false;
#endif
        if (mMapImage) {
            calcMapImageBounds();
        }
    }

    setToolTip(QDir::toNativeSeparators(mapFilePath()));
}

void BaseCellItem::updateLotImage(int index)
{
    WorldCellLot *lot = lots().at(index);
    MapImage *mapImage = mWantsImages
            ? MapImageManager::instance()->getMapImage(lot->mapName()/*, mapFilePath()*/)
            : 0;
    if (mapImage) {
        mLotImages.insert(index, LotImage(QRectF(), mapImage));
        calcLotImageBounds(index);
    } else {
        mLotImages.insert(index, LotImage());
    }
}

QPointF BaseCellItem::calcLotImagePosition(WorldCellLot *lot, int scaledImageWidth, MapImage *mapImage)
{
    if (!mapImage)
        return QPointF();

    // Assume LevelIsometric
    QPoint lotPos = lot->pos();
    lotPos += QPoint(-3, -3) * lot->level();

    const qreal cellSize = mScene->world()->cellSize();
    const qreal cellX = cellPos().x() + (lotPos.x() / cellSize);
    const qreal cellY = cellPos().y() + (lotPos.y() / cellSize);
    QPointF pos = mScene->cellToPixelCoords(cellX, cellY);

    const qreal scaleImageToCell = qreal(scaledImageWidth) / mapImage->image().width();
    pos -= mapImage->tileToImageCoords(0, 0) * scaleImageToCell;
    return pos;
}

void BaseCellItem::updateBoundingRect()
{
    QRectF bounds = mScene->boundingRect(cellPos());

    if (!mMapImageBounds.isEmpty())
        bounds |= mMapImageBounds;

    foreach (LotImage lotImage, mLotImages) {
        if (!lotImage.mBounds.isEmpty())
            bounds |= lotImage.mBounds;
    }

    bounds.adjust(mDrawOffset.x(), mDrawOffset.y(), mDrawOffset.x(), mDrawOffset.y());

    if (mBoundingRect != bounds) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

void BaseCellItem::mapImageChanged(MapImage *mapImage)
{
    bool changed = false;
    if (mapImage == mMapImage) {
        calcMapImageBounds();
        changed = true;
    }
#if 0
    // Perhaps the image now exists and didn't before.
    if (!mMapImage) {
        updateCellImage();
        if (mMapImage)
            changed = true;
    }
#endif
    int index = 0;
    foreach (LotImage lotImage, mLotImages) {
        if (mapImage == lotImage.mMapImage) {
            calcLotImageBounds(index);
            changed = true;
        }
#if 0
        // Perhaps the image now exists and didn't before.
        if (!lotImage.mMapImage) {
            updateLotImage(index);
            if (mLotImages[index].mMapImage)
                changed = true;
        }
#endif
        ++index;
    }

    if (changed) {
        updateBoundingRect();
        update();
    }
}

void BaseCellItem::worldResized()
{
    calcMapImageBounds();
    for (int i = 0; i < mLotImages.size(); i++)
        calcLotImageBounds(i);
    updateBoundingRect();
}

void BaseCellItem::calcMapImageBounds()
{
    if (mMapImage) {
        int SCL = 2;
        const int cellSize = mScene->world()->cellSize();
        QSizeF gridSize = mapSize(cellSize, cellSize, 64 * SCL, 32 * SCL);
        QSizeF unscaledMapSize = mMapImage->bounds().size();
        const qreal scaleMapToCell = unscaledMapSize.width() / gridSize.width();
        int scaledImageWidth = GRID_WIDTH * scaleMapToCell;

        const qreal scaleImageToCell = qreal(scaledImageWidth) / mMapImage->image().width();
        QPointF offset = mMapImage->tileToImageCoords(0, 0) * scaleImageToCell;
        int scaledImageHeight = mMapImage->image().height() * scaleImageToCell;
        mMapImageBounds = QRectF(mScene->cellToPixelCoords(cellPos()) - offset,
                                QSizeF(scaledImageWidth, scaledImageHeight));
    }
}

void BaseCellItem::calcLotImageBounds(int index)
{
    WorldCellLot *lot = lots().at(index);
    LotImage &lotImage = mLotImages[index];
    MapImage *mapImage = lotImage.mMapImage;
    if (!mapImage)
        return;

    int SCL = 2;
    const int cellSize = mScene->world()->cellSize();
    QSizeF gridSize = mapSize(cellSize, cellSize, 64 * SCL, 32 * SCL);
    QSizeF unscaledMapSize = mapImage->bounds().size();
    const qreal scaleMapToCell = unscaledMapSize.width() / gridSize.width();
    int scaledImageWidth = GRID_WIDTH * scaleMapToCell;
    const qreal scaleImageToCell = qreal(scaledImageWidth) / mapImage->image().width();
    int scaledImageHeight = mapImage->image().height() * scaleImageToCell;
    QSizeF scaledImageSize(scaledImageWidth, scaledImageHeight);

    lotImage.mBounds = QRectF(calcLotImagePosition(lot, scaledImageWidth, mapImage),
                              scaledImageSize);

    // Update lot with current width and height of the map
    lot->setWidth(mapImage->mapInfo()->width());
    lot->setHeight(mapImage->mapInfo()->height());
}

/////

WorldCellItem::WorldCellItem(WorldCell *cell, WorldScene *scene, QGraphicsItem *parent)
    : BaseCellItem(scene, parent)
    , mCell(cell)
{
    setAcceptHoverEvents(true);
    setFlag(ItemIsSelectable);
    mWantsImages = false;
    initialize();
}

#include "worldview.h"
#include "InGameMap/clipper.hpp"

static QPolygonF createPolylineOutline(WorldScene *scene, WorldCellObject *object)
{
    ClipperLib::ClipperOffset offset;
    ClipperLib::Path path;
    int SCALE = 100;
    for (int i = 0; i < object->points().size(); i++) {
        WorldCellObjectPoint p1 = object->points()[i];
        path << ClipperLib::IntPoint(p1.x * SCALE, p1.y * SCALE);
        if ((object->polylineWidth() % 2) != 0) {
            ClipperLib::IntPoint cp = path[path.size()-1];
            path[path.size()-1] = ClipperLib::IntPoint(cp.X + SCALE / 2, cp.Y + SCALE / 2);
        }
    }
    offset.AddPath(path, ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etOpenButt);
    ClipperLib::Paths paths;
    offset.Execute(paths, object->polylineWidth() * SCALE / 2.0);
    QPolygonF result;
    if (paths.empty()) {
        return result;
    }
    int cellX = object->cell()->x();
    int cellY = object->cell()->y();
    ClipperLib::Path cPath = paths.at(0);
    const qreal cellSize = scene->world()->cellSize();
    for (const auto &cPoint : cPath) {
        result << scene->cellToPixelCoords(
                    cellX + cPoint.X / (qreal) SCALE / cellSize,
                    cellY + cPoint.Y / (qreal) SCALE / cellSize);
    }
    return result;
}

void WorldCellItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    if (Preferences::instance()->showZonesInWorldView()) {
        QPen pen(Qt::black);
        pen.setCosmetic(((WorldView*) mScene->views().at(0))->zoomable()->scale() >= 1.0);
        painter->setPen(pen);

        for (WorldCellObject *object : cell()->objects()) {
            if (object->group() != nullptr) {
                QColor color = object->group()->color();
                color.setAlpha(50);
                painter->setBrush(QBrush(color));
                const qreal cellSize = mScene->world()->cellSize();
                if (object->isRectangle()) {
                    QPointF p1(cell()->x() + object->x() / cellSize,
                               cell()->y() + object->y() / cellSize);
                    QPointF p2(cell()->x() + (object->x() + object->width()) / cellSize,
                               cell()->y() + (object->y() + object->height()) / cellSize);
                    QPolygonF poly = mScene->cellRectToPolygon(QRectF(p1, p2));
                    painter->drawPolygon(poly);
                }
                if (object->isPolygon()) {
                    QPolygonF poly;
                    for (WorldCellObjectPoint pt : object->points()) {
                        poly << mScene->cellToPixelCoords(
                                    cell()->x() + pt.x / cellSize,
                                    cell()->y() + pt.y / cellSize);
                    }
                    painter->drawPolygon(poly);
                }
                if (object->isPolyline() && (object->polylineWidth() > 0)) {
                    if (mPolylineOutlines.contains(object) == false) {
                        mPolylineOutlines[object] = createPolylineOutline(mScene, object);
                    }
                    QPolygonF poly = mPolylineOutlines[object];
                    painter->drawPolygon(poly);
                }
            }
        }
    } else {
        BaseCellItem::paint(painter, option, widget);
    }

    if (mHoverRefCount > 0)
    {
//        QPen pen(Qt::darkGray);
//        pen.setWidth(2);
//        pen.setCosmetic(true);
        // Use no pen to avoid clipping by adjacent cells to the south/east.
        painter->setPen(Qt::NoPen);
        QColor color = QColor(Qt::darkGray).lighter();
        color.setAlpha(50);
        painter->setBrush(QBrush(color));
        QPolygonF poly = mScene->cellRectToPolygon(cell());
        painter->drawPolygon(poly);
    }
}

void WorldCellItem::lotAdded(int index)
{
    updateLotImage(index);
    updateBoundingRect();
}

void WorldCellItem::lotRemoved(int index)
{
    mLotImages.remove(index);
    updateBoundingRect();
}

void WorldCellItem::lotMoved(WorldCellLot *lot)
{
    int index = mCell->lots().indexOf(lot);
    LotImage *lotImage = &mLotImages[index];
    lotImage->mBounds.moveTopLeft(calcLotImagePosition(lot, lotImage->mBounds.width(),
                                                       lotImage->mMapImage));
    updateBoundingRect();
    update();
}

void WorldCellItem::cellContentsChanged()
{
    updateCellImage();
    mLotImages.clear();
    for (int i = 0; i < mCell->lots().size(); i++)
        updateLotImage(i);
    updateBoundingRect();
}

void WorldCellItem::objectPointsChanged(int index)
{
    WorldCellObject *object = cell()->objects().value(index);
    mPolylineOutlines.remove(object);
    if (Preferences::instance()->showZonesInWorldView()) {
        update();
    }
}

void WorldCellItem::mapFileCreated(const QString &path)
{
    // If BMPtoTMX creates our cell's .tmx file and that .tmx file didn't exist
    // before, we need to create the cell/lot images.
    if (mapFilePath().isEmpty() || mMapImage)
        return;
    if (QFileInfo(path) == QFileInfo(mapFilePath()))
        cellContentsChanged();
}

WorldCellItem::ThumbnailStatus WorldCellItem::thumbnailsAreGo()
{
    if (mMapImage && mMapImage->isLoaded())
        return ThumbnailStatus::Loaded;
    if (!mWantsImages) {
        mWantsImages = true;
        cellContentsChanged();
    }
    if (mMapImage && mMapImage->isLoaded())
        return ThumbnailStatus::Loaded;
    return (mMapImage != nullptr) ? ThumbnailStatus::Loading : ThumbnailStatus::Missing;
}

void WorldCellItem::thumbnailsAreFail()
{
    if (mWantsImages) {
        mWantsImages = false;
        cellContentsChanged();
    }
}

void WorldCellItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)
    if (++mHoverRefCount == 1)
    {
        update();
    }
}

void WorldCellItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)
    Q_ASSERT(mHoverRefCount > 0);
    if (--mHoverRefCount == 0)
    {
        update();
    }
}

/////

DragCellItem::DragCellItem(WorldCell *cell, WorldScene *scene, QGraphicsItem *parent)
    : WorldCellItem(cell, scene, parent)
{
    mWantsImages = scene->itemForCell(cell)->wantsImages();
    initialize(); // again?!?!?!?!
}

void DragCellItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    WorldCellItem::paint(painter, option, widget);

    QPen pen(Qt::blue);
//    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
    QPolygonF poly = mScene->cellRectToPolygon(mCell).translated(mDrawOffset);
    painter->drawPolygon(poly);
}

void DragCellItem::setDragOffset(const QPointF &offset)
{
    mDrawOffset = offset;
    updateBoundingRect();
}

/////

PasteCellItem::PasteCellItem(WorldCellContents *contents, WorldScene *scene, QGraphicsItem *parent)
    : BaseCellItem(scene, parent)
    , mContents(contents)
{
    initialize();
}

void PasteCellItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    BaseCellItem::paint(painter, option, widget);

    QPen pen(Qt::blue);
//    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
    QPolygonF poly = mScene->cellRectToPolygon(QRectF(cellPos(), QSize(1, 1))).translated(mDrawOffset);
    painter->drawPolygon(poly);
}

void PasteCellItem::setDragOffset(const QPointF &offset)
{
    mDrawOffset = offset;
    updateBoundingRect();
}

/////

DragMapImageItem::DragMapImageItem(MapInfo *mapInfo, WorldScene *scene, QGraphicsItem *parent)
    : BaseCellItem(scene, parent)
    , mMapInfo(mapInfo)
{
    initialize();
}

void DragMapImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    BaseCellItem::paint(painter, option, widget);

    QPen pen(Qt::blue);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
    QPolygonF poly = mScene->cellRectToPolygon(QRectF(cellPos(), QSize(1, 1))).translated(mDrawOffset);
    painter->drawPolygon(poly);
}

QPoint DragMapImageItem::cellPos() const
{
    return QPoint(0,0);
}

QString DragMapImageItem::mapFilePath() const
{
    return mMapInfo->path();
}

const QList<WorldCellLot *> &DragMapImageItem::lots() const
{
    return mLots;
}

void DragMapImageItem::setScenePos(const QPointF &scenePos)
{
    // scenePos is at the center of the item
    mDropPos = mScene->pixelToCellCoordsInt(scenePos);
    mDrawOffset = mScene->cellToPixelCoords(mDropPos) - mScene->cellToPixelCoords(cellPos() + QPointF(0.5, 0.5));
    updateBoundingRect();
}

QPoint DragMapImageItem::dropPos() const
{
    return mDropPos;
}

/////

OtherWorldCellItem::OtherWorldCellItem(WorldCell *cell, WorldScene *scene, QGraphicsItem *parent)
    : BaseCellItem(scene, parent)
    , mCell(cell)
{
//    setFlag(ItemIsSelectable);
    mWantsImages = false;
    initialize();
}

QPoint OtherWorldCellItem::cellPos() const
{
     return mCell->world()->getGenerateLotsSettings().worldOrigin - mScene->world()->getGenerateLotsSettings().worldOrigin + mCell->pos();
}

void OtherWorldCellItem::cellContentsChanged()
{
    updateCellImage();
    mLotImages.clear();
    for (int i = 0; i < mCell->lots().size(); i++)
        updateLotImage(i);
    updateBoundingRect();
}

WorldCellItem::ThumbnailStatus OtherWorldCellItem::thumbnailsAreGo()
{
    if (mMapImage && mMapImage->isLoaded())
        return WorldCellItem::ThumbnailStatus::Loaded;
    if (!mWantsImages) {
        mWantsImages = true;
        cellContentsChanged();
    }
    if (mMapImage && mMapImage->isLoaded())
        return WorldCellItem::ThumbnailStatus::Loaded;
    return (mMapImage != nullptr) ? WorldCellItem::ThumbnailStatus::Loading : WorldCellItem::ThumbnailStatus::Missing;
}

void OtherWorldCellItem::thumbnailsAreFail()
{
    if (mWantsImages) {
        mWantsImages = false;
        cellContentsChanged();
    }
}

///// ///// ///// ///// /////

WorldGridItem::WorldGridItem(WorldScene *scene)
    : QGraphicsItem()
    , mScene(scene)
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
}

QRectF WorldGridItem::boundingRect() const
{
    return mBoundingRect;
}

void WorldGridItem::paint(QPainter *painter,
                     const QStyleOptionGraphicsItem *option,
                     QWidget *)
{
    if (!mScene->world())
        return;

    const int tileWidth = GRID_WIDTH;
    const int tileHeight = GRID_HEIGHT;

    QRectF r = option->exposedRect;
    r.adjust(-tileWidth / 2, -tileHeight / 2,
             tileWidth / 2, tileHeight / 2);

    const int startX = qMax(qreal(0), mScene->pixelToCellCoords(r.topLeft()).x());
    const int startY = qMax(qreal(0), mScene->pixelToCellCoords(r.topRight()).y());
    const int endX = qMin(qreal(mScene->world()->width()),
                          mScene->pixelToCellCoords(r.bottomRight()).x());
    const int endY = qMin(qreal(mScene->world()->height()),
                          mScene->pixelToCellCoords(r.bottomLeft()).y());

    Preferences *prefs = Preferences::instance();
    QPen gridPen(prefs->gridColor());
    gridPen.setWidth(prefs->gridWidth());
    gridPen.setCosmetic(true);
    painter->setPen(gridPen);

    for (int y = startY; y <= endY; ++y) {
        const QPointF start = mScene->cellToPixelCoords(startX, (qreal)y);
        const QPointF end = mScene->cellToPixelCoords(endX, (qreal)y);
        painter->drawLine(start, end);
    }
    for (int x = startX; x <= endX; ++x) {
        const QPointF start = mScene->cellToPixelCoords(x, (qreal)startY);
        const QPointF end = mScene->cellToPixelCoords(x, (qreal)endY);
        painter->drawLine(start, end);
    }
}

void WorldGridItem::updateBoundingRect()
{
    prepareGeometryChange();
    QRect bounds(0, 0, mScene->world()->width(), mScene->world()->height());
    mBoundingRect = mScene->boundingRect(bounds);
}

/////

WorldCoordItem::WorldCoordItem(WorldScene *scene)
    : QGraphicsItem()
    , mScene(scene)
{
    setFlag(ItemUsesExtendedStyleOption);
//    setFlag(ItemIgnoresTransformations);
}

QRectF WorldCoordItem::boundingRect() const
{
    return mBoundingRect;
}

void WorldCoordItem::paint(QPainter *painter,
                     const QStyleOptionGraphicsItem *option,
                     QWidget *)
{
    if (!mScene->world())
        return;

    if (mScene->worldDocument()->view()->zoomable()->scale() < 0.25) return;

    // This only works if there is a single view on the scene
    qreal scale = 1 / mScene->worldDocument()->view()->zoomable()->scale();

    const int tileWidth = GRID_WIDTH;
    const int tileHeight = GRID_HEIGHT;

    QRectF r = option->exposedRect;
    r.adjust(-tileWidth / 2, -tileHeight / 2,
             tileWidth / 2, tileHeight / 2);

    const int startX = qMax(qreal(0), mScene->pixelToCellCoords(r.topLeft()).x());
    const int startY = qMax(qreal(0), mScene->pixelToCellCoords(r.topRight()).y());
    const int endX = qMin(qreal(mScene->world()->width() - 1),
                          mScene->pixelToCellCoords(r.bottomRight()).x());
    const int endY = qMin(qreal(mScene->world()->height() - 1),
                          mScene->pixelToCellCoords(r.bottomLeft()).y());

//    painter->setPen(Qt::black);

    QFont font = QFont(painter->font(), painter->device());
    font.setPointSizeF(font.pointSizeF() * scale);
    painter->setFont(font);
    const QFontMetrics fm = painter->fontMetrics();
    int lineHeight = fm.lineSpacing();

    QPen pen = painter->pen();
    pen.setColor(Qt::black);
    pen.setCosmetic(true);
    painter->setPen(pen);

    QPoint worldOrigin = mScene->world()->getGenerateLotsSettings().worldOrigin;

    for (int y = startY; y <= endY; ++y) {
        for (int x = startX; x <= endX; ++x) {
            QString text = QString(QLatin1String("%1,%2")).arg(worldOrigin.x() + x).arg(worldOrigin.y() + y);

            int textWidth = fm.horizontalAdvance(text);
            QPointF center = mScene->cellToPixelCoords(x + 0.5, y + 0.5);
            QRectF r(center.x() - textWidth/2.0, center.y() - lineHeight/2.0, textWidth, lineHeight);
            r.adjust(-5 * scale, -5 * scale, 4 * scale, 4 * scale);
            painter->setBrush(Qt::lightGray);
            painter->drawRect(r);

            painter->drawText(mScene->boundingRect(x, y), Qt::AlignHCenter | Qt::AlignVCenter, text);
        }
    }
}

void WorldCoordItem::updateBoundingRect()
{
    prepareGeometryChange();
    QRect bounds(0, 0, mScene->world()->width(), mScene->world()->height());
    mBoundingRect = mScene->boundingRect(bounds);
}

/////

WorldSelectionItem::WorldSelectionItem(WorldScene *scene)
    : QGraphicsItem()
    , mScene(scene)
    , mHighlightedCellDuringDnD(-1, -1)
{
    setFlag(ItemUsesExtendedStyleOption);
    connect(mScene->worldDocument(), &WorldDocument::selectedCellsChanged, this, &WorldSelectionItem::selectedCellsChanged);
    updateBoundingRect();
}

QRectF WorldSelectionItem::boundingRect() const
{
    return mBoundingRect;
}

void WorldSelectionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    Q_UNUSED(option)
    QPen pen(Qt::blue);
    pen.setWidth(2);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
    foreach (WorldCell *cell, mScene->worldDocument()->selectedCells()) {
        QPolygonF poly = mScene->cellRectToPolygon(cell);
        painter->drawPolygon(poly);
    }

    if (mHighlightedCellDuringDnD.x() >= 0) {
        QPolygonF poly = mScene->cellRectToPolygon(QRect(mHighlightedCellDuringDnD, QSize(1,1)));
        painter->drawPolygon(poly);
    }
}

void WorldSelectionItem::updateBoundingRect()
{
    QRectF bounds;
    foreach (WorldCell *cell, mScene->worldDocument()->selectedCells())
        bounds |= mScene->boundingRect(cell->pos());
    if (mHighlightedCellDuringDnD.x() >= 0)
        bounds |= mScene->boundingRect(mHighlightedCellDuringDnD);
    if (bounds != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

void WorldSelectionItem::highlightCellDuringDnD(const QPoint &pos)
{
    mHighlightedCellDuringDnD = pos;
    updateBoundingRect();
    update();
}

void WorldSelectionItem::selectedCellsChanged()
{
    updateBoundingRect();
    update();
}

/////

WorldRoadItem::WorldRoadItem(WorldScene *scene, Road *road)
    : QGraphicsItem()
    , mScene(scene)
    , mRoad(road)
    , mSelected(false)
    , mEditable(false)
    , mDragging(false)
{
    synchWithRoad();
}

QRectF WorldRoadItem::boundingRect() const
{
    return mBoundingRect;
}

QPainterPath WorldRoadItem::shape() const
{
    QPainterPath path;
    path.addPolygon(polygon());
    return path;
}

void WorldRoadItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    QColor c = Qt::blue;
    if (mSelected)
        c = Qt::green;
    if (mEditable)
        c = Qt::yellow;
    painter->fillPath(shape(), c);
}

void WorldRoadItem::synchWithRoad()
{
    QRectF bounds = polygon().boundingRect();
    if (bounds != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

void WorldRoadItem::setSelected(bool selected)
{
    mSelected = selected;
    update();
}

void WorldRoadItem::setEditable(bool editable)
{
    mEditable = editable;
    update();
}

void WorldRoadItem::setDragging(bool dragging)
{
    mDragging = dragging;
    synchWithRoad();
}

void WorldRoadItem::setDragOffset(const QPoint &offset)
{
    mDragOffset = offset;
    synchWithRoad();
}

QPolygonF WorldRoadItem::polygon() const
{
    QPoint offset = mDragging ? mDragOffset : QPoint();

    return mScene->roadRectToScenePolygon(mRoad->bounds().translated(offset));
}

/////

WorldBMPItem::WorldBMPItem(WorldScene *scene, WorldBMP *bmp)
    : QGraphicsItem()
    , mScene(scene)
    , mBMP(bmp)
    , mSelected(false)
    , mDragging(false)
{
    mMapImage = MapImageManager::instance()->getMapImage(bmp->filePath());
    if (!mMapImage) qDebug() << MapImageManager::instance()->errorString();

    // I chopped up the image to make OpenGL happy (no 6000x3000 textures), but
    // performance is way better without OpenGL, probably due to pixel format.

    setToolTip(QDir::toNativeSeparators(bmp->filePath()));

    synchWithBMP();
}

QRectF WorldBMPItem::boundingRect() const
{
    return mMapImageBounds;
}

QPainterPath WorldBMPItem::shape() const
{
    QPainterPath path;
    path.addPolygon(this->polygon());
    return path;
}

void WorldBMPItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *,
                         QWidget *)
{
    if (mMapImage) {
#if 1
        int columns = mMapImage->subImageColumns();
        int rows = mMapImage->subImageRows();
        qreal scale = mMapImageBounds.width() / qreal(mMapImage->imageWidth());
        for (int x = 0; x < columns; x++) {
            for (int y = 0; y < rows; y++) {
                const QImage &img = mMapImage->subImages()[x + y * columns];
                int imgw = img.width(), imgh = img.height();
                QRectF target = QRectF(mMapImageBounds.x() + x * 512 * scale,
                                       mMapImageBounds.y() + y * 512 * scale,
                                       imgw * scale, imgh * scale);
                QRectF source = QRect(QPoint(), img.size());
                painter->drawImage(target, img, source);
            }
        }
#else
        QRectF target = mMapImageBounds;
        QRectF source = QRect(QPoint(0, 0), mMapImage->image().size());
        painter->drawImage(target, mMapImage->image(), source);
#endif
    } else {
        QPolygonF polygon = this->polygon();

        painter->save();

        QPen pen(Qt::black);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->drawPolygon(polygon);

        QColor color = Qt::red;
        pen.setColor(color);
        painter->setPen(pen);
        QColor brushColor = color;
        brushColor.setAlpha(50);
        QBrush brush(brushColor);
        painter->setBrush(brush);
        polygon.translate(0, -1);
        painter->drawPolygon(polygon);

        painter->restore();
    }

    if (mSelected) {
        QPolygonF polygon = this->polygon();

        painter->save();

        QPen pen(Qt::black);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->drawPolygon(polygon);

        QColor color = Qt::blue;
        pen.setColor(color);
        painter->setPen(pen);
        QColor brushColor = QColor(0x33,0x99,0xff,255/8);
        QBrush brush(brushColor);
        painter->setBrush(brush);
        polygon.translate(0, -1);
        painter->drawPolygon(polygon);

        painter->restore();
    }
}

QRect WorldBMPItem::bmpBounds() const
{
    QRect bounds = mBMP->bounds().translated(mDragging ? mDragOffset : QPoint());
    if (mBMP->world() != mScene->world()) // OtherWorld.mBMP
        bounds.translate(mBMP->world()->getGenerateLotsSettings().worldOrigin - mScene->world()->getGenerateLotsSettings().worldOrigin);
    return bounds;
}

void WorldBMPItem::synchWithBMP()
{
    QRectF bounds = mScene->boundingRect(bmpBounds());
    if (bounds != mMapImageBounds) {
        prepareGeometryChange();
        mMapImageBounds = bounds;
    }
}

void WorldBMPItem::setSelected(bool selected)
{
    mSelected = selected;
    update();
}

void WorldBMPItem::setDragging(bool dragging)
{
    mDragging = dragging;
    synchWithBMP();
}

void WorldBMPItem::setDragOffset(const QPoint &offset)
{
    mDragOffset = offset;
    synchWithBMP();
}

QPolygonF WorldBMPItem::polygon() const
{
    return mScene->cellRectToPolygon(bmpBounds());
}

/////


ZombieSpawnImageItem::ZombieSpawnImageItem(WorldScene *scene)
    : QGraphicsItem()
    , mScene(scene)
    , mSamplesPerCell(scene->world()->geometry().chunksPerCell)
    , mChunkSize(scene->world()->geometry().chunkSize)
    , mPreviewB42x40(true)
{
    reloadFromSettings();
}

QRectF ZombieSpawnImageItem::boundingRect() const
{
    return mMapImageBounds;
}

QPainterPath ZombieSpawnImageItem::shape() const
{
    QPainterPath path;
    path.addPolygon(this->polygon());
    return path;
}

void ZombieSpawnImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    if (mPreviewImage.isNull())
        return;

    // Keep the editable image in its native orthogonal grid and project it
    // only while painting.  Pre-rasterizing the image as a small diamond and
    // then stretching its bounding rectangle caused a pronounced staircase
    // and subtly changed the heatmap outline.
    const QRectF sourceRect(QPointF(0, 0), mPreviewImage.size());
    QPolygonF sourceQuad;
    sourceQuad << sourceRect.topLeft()
               << sourceRect.topRight()
               << sourceRect.bottomRight()
               << sourceRect.bottomLeft();

    QTransform imageToScene;
    if (!QTransform::quadToQuad(sourceQuad, polygon(), imageToScene))
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setWorldTransform(imageToScene, true);
    painter->drawImage(QPointF(0, 0), mPreviewImage);
    painter->restore();
}

QRectF ZombieSpawnImageItem::imageBounds() const
{
    if (mSourceImage.isNull())
        return QRectF();

    return QRectF(0, 0,
                  mSourceImage.width() / qreal(mSamplesPerCell),
                  mSourceImage.height() / qreal(mSamplesPerCell));
}

void ZombieSpawnImageItem::synchWithImage()
{
    QRectF bounds = polygon().boundingRect();
    if (bounds != mMapImageBounds) {
        prepareGeometryChange();
        mMapImageBounds = bounds;
    }
}

QPolygonF ZombieSpawnImageItem::polygon() const
{
    return mScene->cellRectToPolygon(imageBounds());
}

void ZombieSpawnImageItem::setPreviewB42x40(bool enabled)
{
    if (mPreviewB42x40 == enabled)
        return;

    mPreviewB42x40 = enabled;
    rebuildPreview();
    update();
}

QPoint ZombieSpawnImageItem::imagePointAt(const QPointF &scenePos) const
{
    const QPointF cellPos = mScene->pixelToCellCoords(scenePos);
    return QPoint(qFloor(cellPos.x() * mSamplesPerCell),
                  qFloor(cellPos.y() * mSamplesPerCell));
}

bool ZombieSpawnImageItem::containsImagePoint(const QPoint &point) const
{
    return !mSourceImage.isNull()
            && point.x() >= 0 && point.x() < mSourceImage.width()
            && point.y() >= 0 && point.y() < mSourceImage.height();
}

bool ZombieSpawnImageItem::canEdit() const
{
    const QString path = configuredFilePath();
    return isValid() || (!path.isEmpty() && !QFileInfo::exists(path));
}

bool ZombieSpawnImageItem::ensureEditable(QString *error)
{
    if (!mSourceImage.isNull())
        return true;

    mFilePath = configuredFilePath();
    if (mFilePath.isEmpty()) {
        if (error) {
            *error = QObject::tr("Save the WorldEd project before creating "
                                 "its Zombie Heatmap.");
        }
        return false;
    }
    if (QFileInfo::exists(mFilePath)) {
        if (error) {
            *error = QObject::tr("The Zombie Heatmap exists but is not a "
                                 "readable image:\n%1")
                    .arg(QDir::toNativeSeparators(mFilePath));
        }
        return false;
    }

    const QSize expected(mScene->world()->width() * mSamplesPerCell,
                         mScene->world()->height() * mSamplesPerCell);
    if (expected.isEmpty()) {
        if (error)
            *error = QObject::tr("The current world has no editable Heatmap area.");
        return false;
    }

    mSourceImage = QImage(expected, QImage::Format_ARGB32);
    mSourceImage.fill(qRgba(0, 0, 0, 255));

    GenerateLotsSettings settings =
            mScene->world()->getGenerateLotsSettings();
    if (settings.zombieSpawnMap.isEmpty()) {
        settings.zombieSpawnMap = mFilePath;
        mScene->worldDocument()->changeGenerateLotsSettings(settings);
    }

    rebuildPreview();
    synchWithImage();
    update();
    qInfo() << "Zombie Heatmap initialized for creation:" << mFilePath
            << expected;
    return true;
}

bool ZombieSpawnImageItem::reloadFromSettings(QString *error)
{
    const QString newPath = configuredFilePath();
    if (newPath == mFilePath && !mSourceImage.isNull())
        return true;

    mFilePath = newPath;
    const bool exists = QFileInfo::exists(mFilePath);
    QImage image(mFilePath);
    if (image.isNull()) {
        mSourceImage = QImage();
        mPreviewImage = QImage();
        synchWithImage();
        update();
        if (exists) {
            qWarning() << "Zombie Heatmap couldn't be loaded:" << mFilePath;
            if (error)
                *error = QObject::tr("The Zombie Heatmap could not be read:\n%1")
                        .arg(QDir::toNativeSeparators(mFilePath));
            return false;
        }
        return true;
    }

    mSourceImage = image.convertToFormat(QImage::Format_ARGB32);
    const QSize expected(mScene->world()->width() * mSamplesPerCell,
                         mScene->world()->height() * mSamplesPerCell);
    qInfo() << "Zombie Heatmap loaded:" << mFilePath
            << mSourceImage.size()
            << "expected" << expected
            << "grid" << worldGridFormatName(mScene->world()->gridFormat())
            << "raw red-channel; B42 preview x40";
    if (mSourceImage.width() < expected.width()
            || mSourceImage.height() < expected.height()) {
        qWarning() << "Zombie Heatmap does not cover the complete world;"
                   << "use Expand to world to zero-pad it.";
    }

    rebuildPreview();
    synchWithImage();
    update();
    return true;
}

void ZombieSpawnImageItem::paintStroke(const QPoint &from, const QPoint &to,
                                       int radius, int intensity)
{
    if (mSourceImage.isNull())
        return;

    const int value = qBound(0, intensity, 255);
    QPainter painter(&mSourceImage);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    QPen pen(QColor(value, 0, 0, 255));
    pen.setWidth(qMax(1, radius * 2 + 1));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(from, to);
    painter.end();

    rebuildPreview();
    update();
}

bool ZombieSpawnImageItem::replaceSourceImage(const QImage &image,
                                              bool saveToDisk,
                                              QString *error)
{
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("The Zombie Heatmap image is empty.");
        return false;
    }

    const QImage previous = mSourceImage;
    mSourceImage = image.convertToFormat(QImage::Format_ARGB32);
    rebuildPreview();
    synchWithImage();
    update();
    if (!saveToDisk || save(error))
        return true;

    mSourceImage = previous;
    rebuildPreview();
    synchWithImage();
    update();
    return false;
}

bool ZombieSpawnImageItem::save(QString *error)
{
    if (mSourceImage.isNull() || mFilePath.isEmpty()) {
        if (error)
            *error = QObject::tr("No Zombie Heatmap image is loaded.");
        return false;
    }

    const QFileInfo targetInfo(mFilePath);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        if (error) {
            *error = QObject::tr("The Zombie Heatmap directory could not be "
                                 "created:\n%1")
                    .arg(QDir::toNativeSeparators(targetInfo.absolutePath()));
        }
        return false;
    }

    const QString backupPath = mFilePath + QLatin1String(".before-paint.bak");
    if (!QFileInfo::exists(backupPath) && QFileInfo::exists(mFilePath)) {
        if (!QFile::copy(mFilePath, backupPath)) {
            qWarning() << "Unable to create Zombie Heatmap backup:" << backupPath;
        } else {
            qInfo() << "Zombie Heatmap backup created:" << backupPath;
        }
    }

    QSaveFile file(mFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QImageWriter writer(&file, "png");
    writer.setCompression(6);
    if (!writer.write(mSourceImage)) {
        if (error)
            *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }

    qInfo() << "Zombie Heatmap saved:" << mFilePath
            << mSourceImage.size()
            << "samples/cell" << mSamplesPerCell
            << "preview" << (mPreviewB42x40 ? "B42 x40" : "raw");
    return true;
}

bool ZombieSpawnImageItem::expandToWorld(QString *error)
{
    const bool existed = !mSourceImage.isNull();
    if (!ensureEditable(error))
        return false;

    const QSize expected(mScene->world()->width() * mSamplesPerCell,
                         mScene->world()->height() * mSamplesPerCell);
    if (mSourceImage.width() >= expected.width()
            && mSourceImage.height() >= expected.height()) {
        return existed || save(error);
    }

    QImage expanded(qMax(expected.width(), mSourceImage.width()),
                    qMax(expected.height(), mSourceImage.height()),
                    QImage::Format_ARGB32);
    expanded.fill(qRgba(0, 0, 0, 255));
    QPainter painter(&expanded);
    painter.drawImage(QPoint(0, 0), mSourceImage);
    painter.end();
    return replaceSourceImage(expanded, true, error);
}

QString ZombieSpawnImageItem::configuredFilePath() const
{
    const QString configured =
            mScene->world()->getGenerateLotsSettings().zombieSpawnMap.trimmed();
    if (!configured.isEmpty()) {
        const QFileInfo configuredInfo(configured);
        if (configuredInfo.isAbsolute())
            return QDir::cleanPath(configured);
        if (!mScene->worldDocument()->fileName().isEmpty()) {
            return QDir(QFileInfo(mScene->worldDocument()->fileName()).absolutePath())
                    .absoluteFilePath(configured);
        }
        return QDir::current().absoluteFilePath(configured);
    }

    if (mScene->worldDocument()->fileName().isEmpty())
        return QString();
    return QDir(QFileInfo(mScene->worldDocument()->fileName()).absolutePath())
            .filePath(QLatin1String("Map_ZombieSpawnMap.png"));
}

void ZombieSpawnImageItem::rebuildPreview()
{
    if (mSourceImage.isNull()) {
        mPreviewImage = QImage();
        return;
    }

    QImage visual(mSourceImage.size(), QImage::Format_ARGB32);
    for (int y = 0; y < mSourceImage.height(); ++y) {
        const QRgb *source = reinterpret_cast<const QRgb *>(mSourceImage.constScanLine(y));
        QRgb *target = reinterpret_cast<QRgb *>(visual.scanLine(y));
        for (int x = 0; x < mSourceImage.width(); ++x) {
            const int raw = qRed(source[x]);
            const int shown = mPreviewB42x40 ? qMin(255, raw * 40) : raw;
            target[x] = qRgba(shown, 0, 0, 255);
        }
    }

    // Projection is intentionally deferred to paint().  This avoids losing
    // contour detail in an intermediate, low-resolution isometric raster.
    mPreviewImage = visual;
}

/////

OtherWorld::~OtherWorld()
{
    delete mWorld;
}

QPoint OtherWorld::adjustedOrigin(World *world) const
{
    return mWorld->getGenerateLotsSettings().worldOrigin - world->getGenerateLotsSettings().worldOrigin;
}

QRect OtherWorld::adjustedBounds(World *world) const
{
    return QRect(adjustedOrigin(world), mWorld->size());
}
