/*
 * mapscene.cpp
 * Copyright 2008-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2008, Roderic Morris <roderic@ccs.neu.edu>
 * Copyright 2009, Edward Hutchins <eah1@yahoo.com>
 * Copyright 2010, Jeff Bland <jksb@member.fsf.org>
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

#include "mapscene.h"

#include "abstracttool.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "mapobjectitem.h"
#include "maprenderer.h"
#include "objectgroup.h"
#include "objectgroupitem.h"
#include "preferences.h"
#include "tilelayer.h"
#include "tilelayeritem.h"
#include "tileselectionitem.h"
#include "imagelayer.h"
#include "imagelayeritem.h"
#include "toolmanager.h"
#include "tilesetmanager.h"
#ifdef ZOMBOID
#include "abstractobjecttool.h"
#include "bmpselectionitem.h"
#include "mapcomposite.h"
#include "zgriditem.h"
#endif

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>

#include <cmath>

using namespace Tiled;
using namespace Tiled::Internal;

static const qreal darkeningFactor = 0.6;
static const qreal opacityFactor = 0.4;

MapScene::MapScene(QObject *parent):
    QGraphicsScene(parent),
    mMapDocument(0),
    mSelectedTool(0),
    mActiveTool(0),
    mGridVisible(true),
    mUnderMouse(false),
    mCurrentModifiers(Qt::NoModifier),
    mDarkRectangle(new QGraphicsRectItem)
#ifdef ZOMBOID
    ,
    mGridItem(new ZGridItem)
#ifdef SEPARATE_BMP_SELECTION
    , mBmpSelectionItem(0)
#endif
#endif
{
#ifndef ZOMBOID
    setBackgroundBrush(Qt::darkGray);
#endif

    TilesetManager *tilesetManager = TilesetManager::instance();
    connect(tilesetManager, &TilesetManager::tilesetChanged,
            this, &MapScene::tilesetChanged);

    Preferences *prefs = Preferences::instance();
    connect(prefs, &Preferences::objectTypesChanged, this, &MapScene::syncAllObjectItems);
    connect(prefs, &Preferences::highlightCurrentLayerChanged,
            this, &MapScene::setHighlightCurrentLayer);
    connect(prefs, &Preferences::gridColorChanged, this, [this]{this->update();});

    mDarkRectangle->setPen(Qt::NoPen);
    mDarkRectangle->setBrush(Qt::black);
    mDarkRectangle->setOpacity(darkeningFactor);
    addItem(mDarkRectangle);

#ifdef ZOMBOID
    setBackgroundBrush(prefs->backgroundColor());
    connect(prefs, &Preferences::backgroundColorChanged,
            this, &MapScene::bgColorChanged);
    addItem(mGridItem);
#endif

    mHighlightCurrentLayer = prefs->highlightCurrentLayer();

    // Install an event filter so that we can get key events on behalf of the
    // active tool without having to have the current focus.
    qApp->installEventFilter(this);
}

MapScene::~MapScene()
{
    qApp->removeEventFilter(this);
}

void MapScene::setMapDocument(MapDocument *mapDocument)
{
    if (mMapDocument)
        mMapDocument->disconnect(this);

    mMapDocument = mapDocument;
    loadPartialChunks();
#ifdef ZOMBOID
    mGridItem->setMapDocument(mapDocument);
#endif

    refreshScene();

    if (mMapDocument) {
        connect(mMapDocument, &MapDocument::mapChanged,
                this, &MapScene::mapChanged);
#ifdef ZOMBOID
        connect(mMapDocument, &MapDocument::regionChanged,
                this, &MapScene::regionChanged);
#else
        connect(mMapDocument, SIGNAL(regionChanged(QRegion)),
                this, SLOT(repaintRegion(QRegion)));
#endif
        connect(mMapDocument, &MapDocument::layerAdded,
                this, &MapScene::layerAdded);
#ifdef ZOMBOID
        connect(mMapDocument, &MapDocument::layerAboutToBeRemoved,
                this, &MapScene::layerAboutToBeRemoved);
        connect(mMapDocument, &MapDocument::layerRenamed,
                this, &MapScene::layerRenamed);
#endif
        connect(mMapDocument, &MapDocument::layerRemoved,
                this, &MapScene::layerRemoved);
        connect(mMapDocument, &MapDocument::layerChanged,
                this, &MapScene::layerChanged);
        connect(mMapDocument, &MapDocument::currentLevelChanged,
                this, &MapScene::currentLevelChanged);
        connect(mMapDocument, &MapDocument::currentLayerIndexChanged,
                this, &MapScene::currentLayerIndexChanged);
        connect(mMapDocument, &MapDocument::objectsAdded,
                this, &MapScene::objectsAdded);
        connect(mMapDocument, &MapDocument::objectsRemoved,
                this, &MapScene::objectsRemoved);
        connect(mMapDocument, &MapDocument::objectsChanged,
                this, &MapScene::objectsChanged);
        connect(mMapDocument, &MapDocument::selectedObjectsChanged,
                this, &MapScene::updateSelectedObjectItems);
#ifdef ZOMBOID
        // The tooltip on lot objects contains the relative path to the lot.
        connect(mMapDocument, &MapDocument::fileNameChanged,
                this, &MapScene::syncAllObjectItems);
        connect(mMapDocument, &MapDocument::fileNameChanged,
                this, &MapScene::savePartialChunks);
#endif
    }
}

bool MapScene::supportsPartialChunks() const
{
    return mMapDocument && mMapDocument->map()
            && mMapDocument->map()->size() == QSize(256, 256)
            && !mMapDocument->fileName().isEmpty();
}

bool MapScene::partialChunksEnabled() const
{
    return supportsPartialChunks() && mPartialChunks.enabled();
}

int MapScene::selectedPartialChunkCount() const
{
    return mPartialChunks.selectedCount();
}

bool MapScene::partialChunkPreviewSelected(int x, int y) const
{
    if (mPartialChunkLassoActive && partialChunkLassoRect().contains(x, y))
        return mPartialChunkLassoSelect;
    return mPartialChunks.isSelected(x, y);
}

void MapScene::setPartialChunksEnabled(bool enabled)
{
    if (!supportsPartialChunks())
        return;
    mPartialChunkLassoActive = false;
    mPartialChunks.setEnabled(enabled);
    savePartialChunks();
    update();
    emit partialChunkSelectionChanged();
}

void MapScene::selectAllPartialChunks()
{
    if (!partialChunksEnabled())
        return;
    mPartialChunkLassoActive = false;
    mPartialChunks.selectAll();
    savePartialChunks();
    update();
    emit partialChunkSelectionChanged();
}

void MapScene::clearPartialChunks()
{
    if (!partialChunksEnabled())
        return;
    mPartialChunkLassoActive = false;
    mPartialChunks.clear();
    savePartialChunks();
    update();
    emit partialChunkSelectionChanged();
}

void MapScene::loadPartialChunks()
{
    QString error;
    const QString path = mMapDocument ? mMapDocument->fileName() : QString();
    if (!mPartialChunks.load(path, &error) && !error.isEmpty())
        emit partialChunkSaveFailed(error);
}

void MapScene::savePartialChunks()
{
    if (!mMapDocument || mMapDocument->fileName().isEmpty())
        return;
    QString error;
    if (!mPartialChunks.save(mMapDocument->fileName(), &error))
        emit partialChunkSaveFailed(error);
}

void MapScene::setSelectedObjectItems(const QSet<MapObjectItem *> &items)
{
    // Inform the map document about the newly selected objects
    QList<MapObject*> selectedObjects;
#if QT_VERSION >= 0x040700
    selectedObjects.reserve(items.size());
#endif
    foreach (const MapObjectItem *item, items)
        selectedObjects.append(item->mapObject());
    mMapDocument->setSelectedObjects(selectedObjects);
}

void MapScene::setSelectedTool(AbstractTool *tool)
{
    mSelectedTool = tool;
}

#ifdef ZOMBOID
void MapScene::setHandScrolling(bool handScrolling)
{
    if (mActiveTool != nullptr) {
        mActiveTool->setHandScrolling(handScrolling);
    }
}
#endif

void MapScene::refreshScene()
{
#ifdef ZOMBOID
    QElapsedTimer refreshTimer;
    refreshTimer.start();
    qint64 previousRefreshMs = 0;
    auto logRefreshStep = [&](const char *step) {
        const qint64 elapsedMs = refreshTimer.elapsed();
        qInfo() << "TileZed scene setup:" << step
                << (elapsedMs - previousRefreshMs) << "ms step,"
                << elapsedMs << "ms total";
        previousRefreshMs = elapsedMs;
    };
#endif
    mLayerItems.clear();
    mObjectItems.clear();

#ifdef ZOMBOID
    removeItem(mGridItem);
#endif
    removeItem(mDarkRectangle);
    clear();
    addItem(mDarkRectangle);
#ifdef ZOMBOID
    addItem(mGridItem);
    mGridItem->setZValue(20000);
    logRefreshStep("base items");
#endif

    if (!mMapDocument) {
        setSceneRect(QRectF());
        return;
    }

#ifdef ZOMBOID
    // This stops tall tiles being cut off near the 0,0 tile at the top of the window
    // by including the map's drawMargins.
    // It also includes lot bounds.
    QRectF sceneRect = mMapDocument->mapComposite()->boundingRect(mMapDocument->renderer());
    logRefreshStep("map bounds");
    setSceneRect(sceneRect);
    mDarkRectangle->setRect(sceneRect);
#else
    const QSize mapSize = mMapDocument->renderer()->mapSize();
    setSceneRect(0, 0, mapSize.width(), mapSize.height());
    mDarkRectangle->setRect(0, 0, mapSize.width(), mapSize.height());
#endif

    const Map *map = mMapDocument->map();
    mLayerItems.resize(map->layerCount());

    int layerIndex = 0;
    foreach (Layer *layer, map->layers()) {
        QGraphicsItem *layerItem = createLayerItem(layer);
        layerItem->setZValue(layerIndex);
        addItem(layerItem);
        mLayerItems[layerIndex] = layerItem;
        ++layerIndex;
    }
    logRefreshStep("layer graphics");

    TileSelectionItem *selectionItem = new TileSelectionItem(mMapDocument);
    selectionItem->setZValue(10000 - 1);
    addItem(selectionItem);

#ifdef ZOMBOID
#ifdef SEPARATE_BMP_SELECTION
    mBmpSelectionItem = new BmpSelectionItem(mMapDocument);
    mBmpSelectionItem->setZValue(10000 - 1);
    addItem(mBmpSelectionItem);
#else
    mTileSelectionItem = selectionItem;
#endif
#endif

    updateCurrentLayerHighlight();
    logRefreshStep("layer highlight");
}

QGraphicsItem *MapScene::createLayerItem(Layer *layer)
{
    QGraphicsItem *layerItem = 0;

    if (TileLayer *tl = layer->asTileLayer()) {
        layerItem = new TileLayerItem(tl, mMapDocument->renderer());
    } else if (ObjectGroup *og = layer->asObjectGroup()) {
        ObjectGroupItem *ogItem = new ObjectGroupItem(og);
        foreach (MapObject *object, og->objects()) {
            MapObjectItem *item = new MapObjectItem(object, mMapDocument,
                                                    ogItem);
            mObjectItems.insert(object, item);
        }
        layerItem = ogItem;
    } else if (ImageLayer *il = layer->asImageLayer()) {
        layerItem = new ImageLayerItem(il, mMapDocument->renderer());
    }

    Q_ASSERT(layerItem);

    layerItem->setVisible(layer->isVisible());
    return layerItem;
}

void MapScene::updateCurrentLayerHighlight()
{
    if (!mMapDocument)
        return;

    const int currentLayerIndex = mMapDocument->currentLayerIndex();

    if (!mHighlightCurrentLayer || currentLayerIndex == -1) {
        mDarkRectangle->setVisible(false);

        // Restore opacity for all layers
        for (int i = 0; i < mLayerItems.size(); ++i) {
            const Layer *layer = mMapDocument->map()->layerAt(i);
            mLayerItems.at(i)->setOpacity(layer->opacity());
        }

        return;
    }

    // Darken layers below the current layer
    mDarkRectangle->setZValue(currentLayerIndex - 0.5);
    mDarkRectangle->setVisible(true);

    // Set layers above the current layer to half opacity
    for (int i = 1; i < mLayerItems.size(); ++i) {
        const Layer *layer = mMapDocument->map()->layerAt(i);
        const qreal multiplier = (currentLayerIndex < i) ? opacityFactor : 1;
        mLayerItems.at(i)->setOpacity(layer->opacity() * multiplier);
    }
}

#ifdef ZOMBOID
void MapScene::regionChanged(const QRegion &region, Layer *layer)
{
    const MapRenderer *renderer = mMapDocument->renderer();
    QMargins margins = mMapDocument->map()->drawMargins();
    if (TileLayer *tileLayer = layer ? layer->asTileLayer() : nullptr) {
        margins = tileLayer->drawMargins();
        margins.setTop(qMax(0, margins.top()
                            - mMapDocument->map()->tileHeight()));
        margins.setRight(qMax(0, margins.right()
                              - mMapDocument->map()->tileWidth()));
        if (renderer->is2x())
            margins *= 2;
    }

    for (const QRect &r : region) {
        update(renderer->boundingRect(r, layer->level()).adjusted(-margins.left(),
                                                                  -margins.top(),
                                                                  margins.right(),
                                                                  margins.bottom()));
    }
}

void MapScene::currentLevelChanged(int z)
{
    Q_UNUSED(z)
    updateCurrentLayerHighlight();
#ifdef ZOMBOID
    // LevelIsometric orientation may move the grid
    mGridItem->currentLayerIndexChanged();
#endif
}
#else
void MapScene::repaintRegion(const QRegion &region)
{
    const MapRenderer *renderer = mMapDocument->renderer();
    const QMargins margins = mMapDocument->map()->drawMargins();

    foreach (const QRect &r, region.rects()) {
        update(renderer->boundingRect(r).adjusted(-margins.left(),
                                                  -margins.top(),
                                                  margins.right(),
                                                  margins.bottom()));
    }
}
#endif

void MapScene::enableSelectedTool()
{
    if (!mSelectedTool || !mMapDocument)
        return;

    mActiveTool = mSelectedTool;
    mActiveTool->activate(this);

    mCurrentModifiers = QApplication::keyboardModifiers();
    if (mCurrentModifiers != Qt::NoModifier)
        mActiveTool->modifiersChanged(mCurrentModifiers);

    if (mUnderMouse) {
        mActiveTool->mouseEntered();
        mActiveTool->mouseMoved(mLastMousePos, Qt::KeyboardModifiers());
    }

#ifdef ZOMBOID
    // When an item accepts hover events, it stops the active tool getting
    // mouse move events.  For example, the Stamp brush won't update its
    // position when the mouse is hovering over a MapObjectItem.
    bool hover = dynamic_cast<AbstractObjectTool*>(mActiveTool) != 0;
    foreach (QGraphicsItem *item, items()) {
        if (MapObjectItem *mo = dynamic_cast<MapObjectItem*>(item)) {
            mo->setAcceptHoverEvents(hover);
            mo->labelItem()->setAcceptHoverEvents(hover);
        }
    }
#endif
}

void MapScene::disableSelectedTool()
{
    if (!mActiveTool)
        return;

    if (mUnderMouse)
        mActiveTool->mouseLeft();
    mActiveTool->deactivate(this);
    mActiveTool = 0;
}

void MapScene::currentLayerIndexChanged()
{
    updateCurrentLayerHighlight();
#ifdef ZOMBOID
    // LevelIsometric orientation may move the grid
    mGridItem->currentLayerIndexChanged();
#endif
}

/**
 * Adapts the scene rect and layers to the new map size.
 */
void MapScene::mapChanged()
{
#ifdef ZOMBOID
    // This stops tall tiles being cut off near the 0,0 tile at the top of the window
    // by including the map's drawMargins.
    // It also includes lot bounds.
    QRectF sceneRect = mMapDocument->mapComposite()->boundingRect(mMapDocument->renderer());
    setSceneRect(sceneRect);
    mDarkRectangle->setRect(sceneRect);
    mGridItem->currentLayerIndexChanged(); // index didn't change, just updating the bounds
#else
    const QSize mapSize = mMapDocument->renderer()->mapSize();
    setSceneRect(0, 0, mapSize.width(), mapSize.height());
    mDarkRectangle->setRect(0, 0, mapSize.width(), mapSize.height());
#endif

    foreach (QGraphicsItem *item, mLayerItems) {
        if (TileLayerItem *tli = dynamic_cast<TileLayerItem*>(item))
            tli->syncWithTileLayer();
    }
#ifdef ZOMBOID
    // BUG: create object layer, add items, resize map much larger, try to select the objects
    foreach (MapObjectItem *item, mObjectItems)
        item->syncWithMapObject();
#endif
}

void MapScene::tilesetChanged(Tileset *tileset)
{
    if (!mMapDocument)
        return;

#ifdef ZOMBOID
    if (mMapDocument->mapComposite()->isTilesetUsed(tileset))
        update();
#else
    if (mMapDocument->map()->tilesets().contains(tileset))
        update();
#endif
}

void MapScene::layerAdded(int index)
{
    Layer *layer = mMapDocument->map()->layerAt(index);
    QGraphicsItem *layerItem = createLayerItem(layer);
    addItem(layerItem);
    mLayerItems.insert(index, layerItem);

#ifndef ZOMBOID
    int z = 0;
    foreach (QGraphicsItem *item, mLayerItems)
        item->setZValue(z++);
#endif
}

#ifdef ZOMBOID
void MapScene::layerAboutToBeRemoved(int index)
{
    Layer *layer = mMapDocument->map()->layerAt(index);
    if (ObjectGroup *og = layer->asObjectGroup()) {
        foreach (MapObject *o, og->objects()) {
            mObjectItems.remove(o);
        }
    }
}
#endif

void MapScene::layerRemoved(int index)
{
    delete mLayerItems.at(index);
    mLayerItems.remove(index);
}

/**
 * A layer has changed. This can mean that the layer visibility or opacity has
 * changed.
 */
void MapScene::layerChanged(int index)
{
    const Layer *layer = mMapDocument->map()->layerAt(index);
    QGraphicsItem *layerItem = mLayerItems.at(index);

    layerItem->setVisible(layer->isVisible());

    qreal multiplier = 1;
#if !defined(ZOMBOID)
    if (mHighlightCurrentLayer && mMapDocument->currentLayerIndex() < index)
        multiplier = opacityFactor;
#endif

    layerItem->setOpacity(layer->opacity() * multiplier);
}

#ifdef ZOMBOID
void MapScene::layerRenamed(int index)
{
    Q_UNUSED(index)
}
#endif

/**
 * Inserts map object items for the given objects.
 */
void MapScene::objectsAdded(const QList<MapObject*> &objects)
{
    foreach (MapObject *object, objects) {
        ObjectGroup *og = object->objectGroup();
        ObjectGroupItem *ogItem = 0;

        // Find the object group item for the map object's object group
        foreach (QGraphicsItem *item, mLayerItems) {
            if (ObjectGroupItem *ogi = dynamic_cast<ObjectGroupItem*>(item)) {
                if (ogi->objectGroup() == og) {
                    ogItem = ogi;
                    break;
                }
            }
        }

        Q_ASSERT(ogItem);
        MapObjectItem *item = new MapObjectItem(object, mMapDocument, ogItem);
        mObjectItems.insert(object, item);

#ifdef ZOMBOID
        // When an item accepts hover events, it stops the active tool getting
        // mouse move events.  For example, the Stamp brush won't update its
        // position when the mouse is hovering over a MapObjectItem.
        bool hover = dynamic_cast<AbstractObjectTool*>(mActiveTool) != 0;
        item->setAcceptHoverEvents(hover);
        item->labelItem()->setAcceptHoverEvents(hover);
#endif
    }
}

/**
 * Removes the map object items related to the given objects.
 */
void MapScene::objectsRemoved(const QList<MapObject*> &objects)
{
    foreach (MapObject *o, objects) {
        ObjectItems::iterator i = mObjectItems.find(o);
        Q_ASSERT(i != mObjectItems.end());

        mSelectedObjectItems.remove(i.value());
        delete i.value();
        mObjectItems.erase(i);
    }
}

/**
 * Updates the map object items related to the given objects.
 */
void MapScene::objectsChanged(const QList<MapObject*> &objects)
{
    foreach (MapObject *object, objects) {
        MapObjectItem *item = itemForObject(object);
        Q_ASSERT(item);

        item->syncWithMapObject();
    }
}

void MapScene::updateSelectedObjectItems()
{
    const QList<MapObject *> &objects = mMapDocument->selectedObjects();

    QSet<MapObjectItem*> items;
    foreach (MapObject *object, objects) {
        MapObjectItem *item = itemForObject(object);
        Q_ASSERT(item);

        items.insert(item);
    }

    // Update the editable state of the items
    foreach (MapObjectItem *item, mSelectedObjectItems - items)
        item->setEditable(false);
    foreach (MapObjectItem *item, items - mSelectedObjectItems)
        item->setEditable(true);

    mSelectedObjectItems = items;
    emit selectedObjectItemsChanged();
}

void MapScene::syncAllObjectItems()
{
    foreach (MapObjectItem *item, mObjectItems)
        item->syncWithMapObject();
}

#ifdef ZOMBOID
void MapScene::bgColorChanged(const QColor &color)
{
    setBackgroundBrush(color);
}
#endif

void MapScene::setGridVisible(bool visible)
{
    if (mGridVisible == visible)
        return;

#ifdef ZOMBOID
    mGridVisible = visible;
    mGridItem->setVisible(mGridVisible);
#else
    mGridVisible = visible;
    update();
#endif
}

void MapScene::setHighlightCurrentLayer(bool highlightCurrentLayer)
{
    if (mHighlightCurrentLayer == highlightCurrentLayer)
        return;

    mHighlightCurrentLayer = highlightCurrentLayer;
    updateCurrentLayerHighlight();
}

void MapScene::drawForeground(QPainter *painter, const QRectF &rect)
{
#ifdef ZOMBOID
    Q_UNUSED(rect)
    if (!partialChunksEnabled())
        return;
    painter->save();
    QColor chunkColor = Preferences::instance()->gridColor();
    chunkColor.setAlpha(210);
    QPen pen(chunkColor);
    pen.setCosmetic(true);
    painter->setPen(pen);
    for (int y = 0; y < PZTools::PartialChunkSelection::ChunksPerCell; ++y) {
        for (int x = 0; x < PZTools::PartialChunkSelection::ChunksPerCell; ++x) {
            const QRect chunkRect(
                        x * PZTools::PartialChunkSelection::ChunkSize,
                        y * PZTools::PartialChunkSelection::ChunkSize,
                        PZTools::PartialChunkSelection::ChunkSize,
                        PZTools::PartialChunkSelection::ChunkSize);
            QColor selectedColor = chunkColor;
            selectedColor.setAlpha(22);
            painter->setBrush(partialChunkPreviewSelected(x, y)
                              ? selectedColor
                              : QColor(10, 10, 10, 165));
            painter->drawPolygon(mMapDocument->renderer()->tileToPixelCoords(
                                     chunkRect, mMapDocument->currentLevel()));
        }
    }
    painter->restore();
#else
    if (!mMapDocument || !mGridVisible)
        return;

    Preferences *prefs = Preferences::instance();
    mMapDocument->renderer()->drawGrid(painter, rect, prefs->gridColor());
#endif
}

bool MapScene::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::Enter:
        mUnderMouse = true;
        if (mActiveTool)
            mActiveTool->mouseEntered();
        break;
    case QEvent::Leave:
        mUnderMouse = false;
        if (mActiveTool)
            mActiveTool->mouseLeft();
        break;
    default:
        break;
    }

    return QGraphicsScene::event(event);
}

void MapScene::mouseMoveEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    mLastMousePos = mouseEvent->scenePos();

    if (!mMapDocument)
        return;

    if (mPartialChunkLassoActive) {
        const QPoint nextChunk = partialChunkAt(
                    mouseEvent->scenePos(), true);
        if (nextChunk != mPartialChunkLassoCurrent) {
            const QRect oldChunks = partialChunkLassoRect();
            mPartialChunkLassoCurrent = nextChunk;
            updatePartialChunkLasso(oldChunks, partialChunkLassoRect());
        }
        mouseEvent->accept();
        return;
    }

    QGraphicsScene::mouseMoveEvent(mouseEvent);
    if (mouseEvent->isAccepted())
        return;

    if (mActiveTool) {
        mActiveTool->mouseMoved(mouseEvent->scenePos(),
                                mouseEvent->modifiers());
        mouseEvent->accept();
    }
}

void MapScene::mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (partialChunksEnabled() && mouseEvent->button() == Qt::LeftButton) {
        const QPoint chunk = partialChunkAt(mouseEvent->scenePos(), false);
        if (chunk.x() >= 0 && chunk.y() >= 0) {
            mPartialChunkLassoActive = true;
            mPartialChunkLassoStart = chunk;
            mPartialChunkLassoCurrent = chunk;
            mPartialChunkLassoSelect = !mPartialChunks.isSelected(
                        chunk.x(), chunk.y());
            update(partialChunkSceneRect(partialChunkLassoRect()));
            mouseEvent->accept();
            return;
        }
    }
    QGraphicsScene::mousePressEvent(mouseEvent);
    if (mouseEvent->isAccepted())
        return;

    if (mActiveTool) {
        mouseEvent->accept();
        mActiveTool->mousePressed(mouseEvent);
    }
}

void MapScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (mPartialChunkLassoActive && mouseEvent->button() == Qt::LeftButton) {
        const QPoint nextChunk = partialChunkAt(
                    mouseEvent->scenePos(), true);
        if (nextChunk != mPartialChunkLassoCurrent) {
            const QRect oldChunks = partialChunkLassoRect();
            mPartialChunkLassoCurrent = nextChunk;
            updatePartialChunkLasso(oldChunks, partialChunkLassoRect());
        }
        const QRect chunks = partialChunkLassoRect();
        for (int y = chunks.top(); y <= chunks.bottom(); ++y) {
            for (int x = chunks.left(); x <= chunks.right(); ++x)
                mPartialChunks.setSelected(x, y, mPartialChunkLassoSelect);
        }
        mPartialChunkLassoActive = false;
        savePartialChunks();
        emit partialChunkSelectionChanged();
        mouseEvent->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(mouseEvent);
    if (mouseEvent->isAccepted())
        return;

    if (mActiveTool) {
        mouseEvent->accept();
        mActiveTool->mouseReleased(mouseEvent);
    }
}

QPoint MapScene::partialChunkAt(const QPointF &scenePos, bool clamp) const
{
    if (!mMapDocument)
        return QPoint(-1, -1);
    QPoint tile = mMapDocument->renderer()->pixelToTileCoordsInt(
                scenePos, mMapDocument->currentLevel());
    if (!clamp && (tile.x() < 0 || tile.y() < 0
                   || tile.x() >= 256 || tile.y() >= 256))
        return QPoint(-1, -1);
    tile.setX(qBound(0, tile.x(), 255));
    tile.setY(qBound(0, tile.y(), 255));
    return QPoint(tile.x() / PZTools::PartialChunkSelection::ChunkSize,
                  tile.y() / PZTools::PartialChunkSelection::ChunkSize);
}

QRect MapScene::partialChunkLassoRect() const
{
    return QRect(mPartialChunkLassoStart,
                 mPartialChunkLassoCurrent).normalized();
}

QRectF MapScene::partialChunkSceneRect(const QRect &chunks) const
{
    if (!mMapDocument || chunks.isEmpty())
        return QRectF();
    const int chunkSize = PZTools::PartialChunkSelection::ChunkSize;
    const QRect tiles(chunks.x() * chunkSize,
                      chunks.y() * chunkSize,
                      chunks.width() * chunkSize,
                      chunks.height() * chunkSize);
    return mMapDocument->renderer()->boundingRect(
                tiles, mMapDocument->currentLevel()).adjusted(-2, -2, 2, 2);
}

void MapScene::updatePartialChunkLasso(const QRect &oldChunks,
                                       const QRect &newChunks)
{
    const QRegion changed =
            PZTools::PartialChunkSelection::changedLassoRegion(
                oldChunks, newChunks);
    for (const QRect &chunks : changed)
        update(partialChunkSceneRect(chunks));
}

/**
 * Override to ignore drag enter events.
 */
void MapScene::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    event->ignore();
}

bool MapScene::eventFilter(QObject *, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            Qt::KeyboardModifiers newModifiers = keyEvent->modifiers();

            if (mActiveTool && newModifiers != mCurrentModifiers) {
                mActiveTool->modifiersChanged(newModifiers);
                mCurrentModifiers = newModifiers;
            }
        }
        break;
    default:
        break;
    }

    return false;
}
