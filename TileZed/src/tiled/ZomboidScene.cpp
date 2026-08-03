/*
 * ZomboidScene.cpp
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

#include "ZomboidScene.h"

#include "bmpblender.h"
#include "bmptool.h"
#include "map.h"
#include "mapbuildings.h"
#include "mapcomposite.h"
#include "mapdocument.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "mapobject.h"
#include "mapobjectitem.h"
#include "maprenderer.h"
#include "nightpreviewitem.h"
#include "zgriditem.h"
#include "objectgroup.h"
#include "preferences.h"
#include "tile.h"
#include "tilelayer.h"
#include "tilelayeritem.h"
#include "tileset.h"
#include "toolmanager.h"
#include "zlevelsmodel.h"
#include "zlotmanager.h"
#include "tiledeffile.h"

#include "BuildingEditor/buildingfloor.h"

#include "worlded/worldcell.h"

#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QSettings>
#include <QTimer>

using namespace Tiled;
using namespace Tiled::Internal;

extern bool gStartupBlockRendering;

///// ///// ///// ///// /////

#include <QStyleOptionGraphicsItem>

CompositeLayerGroupItem::CompositeLayerGroupItem(CompositeLayerGroup *layerGroup, Tiled::MapRenderer *renderer, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , mLayerGroup(layerGroup)
    , mRenderer(renderer)
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);

    mBoundingRect = layerGroup->boundingRect(mRenderer);
}

QRectF CompositeLayerGroupItem::boundingRect() const
{
    return mBoundingRect;
}

void CompositeLayerGroupItem::paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *)
{
    // This is a dumb hack used when restoring a session.
    if (gStartupBlockRendering)
        return;

    if (mLayerGroup->needsSynch() /*mBoundingRect != mLayerGroup->boundingRect(mRenderer)*/)
        return;

    OrderedCellsTemporaries vars;
    mRenderer->drawTileLayerGroup(p, mLayerGroup, option->exposedRect, reinterpret_cast<ZTileLayerGroupRenderData*>(&vars));
#ifdef _DEBUG
    p->drawRect(mBoundingRect);
#endif
}

void CompositeLayerGroupItem::synchWithTileLayers()
{
//    if (layerGroup()->needsSynch())
        layerGroup()->synch();
    update();
}

void CompositeLayerGroupItem::updateBounds()
{
    QRectF bounds = layerGroup()->boundingRect(mRenderer);
    if (bounds != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

///// ///// ///// ///// /////

ZomboidScene::ZomboidScene(QObject *parent)
    : MapScene(parent)
    , mPendingActive(false)
    , mDnDItem(0)
    , mWasHighlightCurrentLayer(false)
    , mMapBordersItem(new QGraphicsPolygonItem)
    , mMapBordersItem2(new QGraphicsPolygonItem)
    , mMapBuildings(new MapBuildings)
    , mMapBuildingsInvalid(true)
    , mNightPreviewTimer(new QTimer(this))
{
    mNightPreviewTimer->setSingleShot(true);
    mNightPreviewTimer->setInterval(120);
    connect(mNightPreviewTimer, &QTimer::timeout,
            this, &ZomboidScene::rebuildNightPreview);
    connect(&mLotManager, qOverload<MapComposite*,Tiled::MapObject*>(&ZLotManager::lotAdded),
        this, qOverload<MapComposite*,Tiled::MapObject*>(&ZomboidScene::onLotAdded));
    connect(&mLotManager, qOverload<MapComposite*,Tiled::MapObject*>(&ZLotManager::lotRemoved),
        this, qOverload<MapComposite*,Tiled::MapObject*>(&ZomboidScene::onLotRemoved));
    connect(&mLotManager, qOverload<MapComposite*,Tiled::MapObject*>(&ZLotManager::lotUpdated),
        this, qOverload<MapComposite*,Tiled::MapObject*>(&ZomboidScene::onLotUpdated));

    connect(&mLotManager, qOverload<MapComposite*,WorldCellLot*>(&ZLotManager::lotAdded),
            this, qOverload<MapComposite*,WorldCellLot*>(&ZomboidScene::onLotUpdated));
    connect(&mLotManager, qOverload<MapComposite*,WorldCellLot*>(&ZLotManager::lotRemoved),
            this, qOverload<MapComposite*,WorldCellLot*>(&ZomboidScene::onLotUpdated));
    connect(&mLotManager, qOverload<MapComposite*,WorldCellLot*>(&ZLotManager::lotUpdated),
            this, qOverload<MapComposite*,WorldCellLot*>(&ZomboidScene::onLotUpdated));

    QPen pen(QColor(128, 128, 128, 128));
    pen.setWidth(28); // only good for isometric 64x32 tiles!
    pen.setJoinStyle(Qt::MiterJoin);
    mMapBordersItem->setPen(pen);
    mMapBordersItem->setZValue(20000 - 1); // ZVALUE_GRID - 1
#if 1
    mMapBordersItem2->setVisible(false);
#else
    pen = QPen();
    pen.setWidth(2);
    pen.setCosmetic(true);
    mMapBordersItem2->setPen(pen);
    addItem(mMapBordersItem2);
#endif
}

ZomboidScene::~ZomboidScene()
{
    mLotManager.disconnect(this);
    delete mMapBuildings;
}

void ZomboidScene::setMapDocument(MapDocument *mapDoc)
{
    MapScene::setMapDocument(mapDoc);
    mLotManager.setMapDocument(mapDocument());

    if (mapDocument()) {
        connect(mMapDocument, &MapDocument::regionAltered,
                this, &ZomboidScene::regionAltered);
        connect(mMapDocument, &MapDocument::layerGroupAdded, this, &ZomboidScene::layerGroupAdded);
        connect(mMapDocument, &MapDocument::layerGroupVisibilityChanged, this, &ZomboidScene::layerGroupVisibilityChanged);
        connect(mMapDocument, &MapDocument::layerAddedToGroup, this, &ZomboidScene::layerAddedToGroup);
        connect(mMapDocument, &MapDocument::layerRemovedFromGroup, this, &ZomboidScene::layerRemovedFromGroup);
        connect(mMapDocument, &MapDocument::layerLevelChanged, this, &ZomboidScene::layerLevelChanged);
        connect(mMapDocument, &MapDocument::mapCompositeChanged,
                this, &ZomboidScene::mapCompositeChanged);

        connect(mMapDocument, &MapDocument::objectsAdded,
                this, &ZomboidScene::invalidateMapBuildings);
        connect(mMapDocument, &MapDocument::objectsRemoved,
                this, &ZomboidScene::invalidateMapBuildings);
        connect(mMapDocument, &MapDocument::objectsChanged,
                this, &ZomboidScene::invalidateMapBuildings);

        connect(mMapDocument->mapComposite()->bmpBlender(), &BmpBlender::layersRecreated,
                this, &ZomboidScene::bmpBlenderLayersRecreated);
        connect(mMapDocument, &MapDocument::bmpPainted, this, &ZomboidScene::bmpPainted);
        connect(mMapDocument, &MapDocument::bmpAliasesChanged, this, &ZomboidScene::bmpXXXChanged);
        connect(mMapDocument, &MapDocument::bmpRulesChanged, this, &ZomboidScene::bmpXXXChanged);
        connect(mMapDocument, &MapDocument::bmpBlendsChanged, this, &ZomboidScene::bmpXXXChanged);

        connect(mMapDocument, &MapDocument::noBlendPainted, this, &ZomboidScene::noBlendPainted);
        connect(mMapDocument, &MapDocument::currentLayerIndexChanged, this, &ZomboidScene::synchNoBlendVisible);
        connect(ToolManager::instance(), &ToolManager::selectedToolChanged, this, &ZomboidScene::synchNoBlendVisible);

        connect(Preferences::instance(), &Preferences::highlightRoomUnderPointerChanged,
                this, &ZomboidScene::highlightRoomUnderPointerChanged);
        connect(Preferences::instance(), &Preferences::showLotFloorsOnlyChanged, this, &ZomboidScene::showLotFloorsOnlyChanged);
        connect(Preferences::instance(), &Preferences::showInvisibleTilesChanged, this, &ZomboidScene::showInvisibleTilesChanged);
        connect(Preferences::instance(), &Preferences::showCellBorderChanged, this, &ZomboidScene::showCellBorderChanged);
    }
}

void ZomboidScene::regionAltered(const QRegion &region, Layer *layer)
{
    // Painting tiles may update the draw margins of a layer.
    if (TileLayer *tl = layer->asTileLayer()) {
        // The drawMargins will only change the first time painting occurs
        // in an empty layer.
        if (tl->group() && mTileLayerGroupItems.contains(tl->level())) {
            if (mTileLayerGroupItems[tl->level()]->layerGroup()->regionAltered(tl))
                updateLayerGroupLater(tl->level(), Synch | Bounds); // recalculate CompositeLayerGroup::mDrawMargins
        } else {
            // TileLayer not part of a layer group.
            int layerIndex = mapDocument()->map()->layers().indexOf(tl);
            TileLayerItem *item = dynamic_cast<TileLayerItem*>(mLayerItems[layerIndex]);
            QRectF r = item->boundingRect();
            item->syncWithTileLayer();
            if (r != item->boundingRect())
                doLater(Bounds);
        }
    }

    MapScene::regionChanged(region, layer);
    scheduleNightPreviewRebuild();
}

void ZomboidScene::updateLayerGroupsLater(PendingFlags flags)
{
    if (flags & Synch) {
        foreach (CompositeLayerGroupItem *item, mTileLayerGroupItems)
            item->layerGroup()->setNeedsSynch(true);
    }
    doLater(AllGroups | flags);
}

void ZomboidScene::updateLayerGroupLater(int level, PendingFlags flags)
{
    if (mTileLayerGroupItems.contains(level)) {
        CompositeLayerGroupItem *item = mTileLayerGroupItems[level];
        if (!mPendingGroupItems.contains(item))
            mPendingGroupItems += item;
        if (flags & Synch)
            item->layerGroup()->setNeedsSynch(true);
        doLater(flags);
    }
}

void ZomboidScene::refreshScene()
{
    qDeleteAll(mTileLayerGroupItems); // QGraphicsScene.clear() will delete these actually
    mTileLayerGroupItems.clear();

    if (mMapBordersItem->scene()) {
        removeItem(mMapBordersItem);
        removeItem(mMapBordersItem2);
    }

    MapScene::refreshScene();

    addItem(mMapBordersItem);
    addItem(mMapBordersItem2);

    updateLayerGroupsLater(Synch | Bounds | ZOrder);
}

void ZomboidScene::mapChanged()
{
    MapScene::mapChanged();

    updateLayerGroupsLater(Bounds);
    scheduleNightPreviewRebuild();
}

void ZomboidScene::scheduleNightPreviewRebuild()
{
    if (isNightPreviewEnabled())
        mNightPreviewTimer->start();
}

void ZomboidScene::rebuildNightPreview()
{
    QVector<NightPreviewLight> lights;
    QVector<QPolygonF> litRooms;
    if (!isNightPreviewEnabled() || !mapDocument() ||
            !mapDocument()->mapComposite()) {
        mNightPreviewItem->setLights(lights);
        mNightPreviewItem->setLitRooms(litRooms);
        return;
    }

    Map *map = mapDocument()->map();
    MapRenderer *renderer = mapDocument()->renderer();
    MapComposite *composite = mapDocument()->mapComposite();
    int level = mapDocument()->currentLevel();
    if (level == INVALID_LEVEL && mapDocument()->currentLayer())
        level = mapDocument()->currentLayer()->level();
    CompositeLayerGroup *layerGroup =
            composite->tileLayersForLevel(level);
    if (!layerGroup) {
        MapScene::rebuildNightPreview();
        return;
    }

    layerGroup->prepareDrawing2();
    OrderedCellsTemporaries vars;
    QVector<const Tiled::Cell*> cells;
    TileDefWatcher *tileDefWatcher =
            BuildingEditor::getTileDefWatcher();
    tileDefWatcher->check();
    QVector<QPoint> roomSwitchPositions;
    QSet<quint64> roomSwitchKeys;
    QSet<QString> lightKeys;

    for (int y = 0; y < map->height(); ++y) {
        for (int x = 0; x < map->width(); ++x) {
            if (!layerGroup->orderedCellsAt2(QPoint(x, y), vars, cells))
                continue;
            for (const Tiled::Cell *cell : qAsConst(cells)) {
                if (!cell || !cell->tile)
                    continue;
                const Tiled::Tile *tile = cell->tile;
                TileDefTile *tileDef = tileDefWatcher->tile(
                            tile->tileset()->name(), tile->id());
                const auto property = [tile, tileDef](
                        const QString &name) {
                    if (tileDef) {
                        auto exact = tileDef->mProperties.constFind(name);
                        if (exact != tileDef->mProperties.constEnd())
                            return exact.value();
                        for (auto it = tileDef->mProperties.constBegin();
                             it != tileDef->mProperties.constEnd(); ++it) {
                            if (it.key().compare(name,
                                                 Qt::CaseInsensitive) == 0)
                                return it.value();
                        }
                    }
                    const auto &properties = tile->properties();
                    auto exact = properties.constFind(name);
                    if (exact != properties.constEnd())
                        return exact.value();
                    for (auto it = properties.constBegin();
                         it != properties.constEnd(); ++it) {
                        if (it.key().compare(name,
                                             Qt::CaseInsensitive) == 0)
                            return it.value();
                    }
                    return QString();
                };
                const auto containsProperty = [tile, tileDef](
                        const QString &name) {
                    if (tileDef) {
                        for (auto it = tileDef->mProperties.constBegin();
                             it != tileDef->mProperties.constEnd(); ++it) {
                            if (it.key().compare(name,
                                                 Qt::CaseInsensitive) == 0)
                                return true;
                        }
                    }
                    const auto &properties = tile->properties();
                    for (auto it = properties.constBegin();
                         it != properties.constEnd(); ++it) {
                        if (it.key().compare(name,
                                             Qt::CaseInsensitive) == 0)
                            return true;
                    }
                    return false;
                };
                const QString tilesetName = tile->tileset()->name();
                const bool isKnownRoomSwitch =
                        tilesetName.compare(
                            QStringLiteral("lighting_indoor_01"),
                            Qt::CaseInsensitive) == 0 &&
                        tile->id() >= 0 && tile->id() < 8;
                const bool isSwitch =
                        property(QStringLiteral("IsoType")).compare(
                            QStringLiteral("lightswitch"),
                            Qt::CaseInsensitive) == 0 ||
                        containsProperty(
                            QStringLiteral("lightswitch")) ||
                        isKnownRoomSwitch;
                bool redOk = false;
                bool greenOk = false;
                bool blueOk = false;
                int red = property(
                            QStringLiteral("lightR")).toInt(&redOk);
                int green = property(
                            QStringLiteral("lightG")).toInt(&greenOk);
                int blue = property(
                            QStringLiteral("lightB")).toInt(&blueOk);
                bool hasLightColor = redOk && greenOk && blueOk;
                bool fallbackLight = false;
                if (!hasLightColor &&
                        tilesetName.startsWith(
                            QStringLiteral("lighting_outdoor_"),
                            Qt::CaseInsensitive) &&
                        !tilesetName.endsWith(
                            QStringLiteral("_on"),
                            Qt::CaseInsensitive)) {
                    const QColor fallbackColor(
                                QSettings().value(
                                    QStringLiteral(
                                        "NightPreview/FallbackColor"),
                                    QStringLiteral("#ffdca4")).toString());
                    red = fallbackColor.isValid()
                            ? fallbackColor.red() : 255;
                    green = fallbackColor.isValid()
                            ? fallbackColor.green() : 220;
                    blue = fallbackColor.isValid()
                            ? fallbackColor.blue() : 164;
                    hasLightColor = true;
                    fallbackLight = true;
                }
                if (!hasLightColor) {
                    if (!isSwitch)
                        continue;
                    const quint64 positionKey =
                            (quint64(quint32(x)) << 32) | quint32(y);
                    if (!roomSwitchKeys.contains(positionKey)) {
                        roomSwitchKeys.insert(positionKey);
                        roomSwitchPositions.append(QPoint(x, y));
                    }
                    continue;
                }

                const QString key = QStringLiteral("%1:%2:%3:%4")
                        .arg(x).arg(y)
                        .arg(tilesetName)
                        .arg(tile->id());
                if (lightKeys.contains(key))
                    continue;
                lightKeys.insert(key);

                bool radiusOk = false;
                int radius = property(
                            QStringLiteral("LightRadius")).toInt(&radiusOk);
                if (!radiusOk || radius <= 0)
                    radius = fallbackLight
                            ? QSettings().value(
                                  QStringLiteral(
                                      "NightPreview/FallbackRadius"),
                                  4).toInt()
                            : 10;
                NightPreviewLight light;
                light.center = renderer->tileToPixelCoords(
                            QPointF(x + 0.5, y + 0.5), level);
                light.color = QColor(qBound(0, red, 255),
                                     qBound(0, green, 255),
                                     qBound(0, blue, 255));
                light.radiusX = QLineF(
                            light.center,
                            renderer->tileToPixelCoords(
                                QPointF(x + radius + 0.5,
                                        y + 0.5), level)).length();
                light.radiusY = QLineF(
                            light.center,
                            renderer->tileToPixelCoords(
                                QPointF(x + 0.5,
                                        y + radius + 0.5), level)).length();
                lights.append(light);
            }
        }
    }

    if (!roomSwitchPositions.isEmpty()) {
        if (mMapBuildingsInvalid) {
            mMapBuildings->calculate(composite);
            mMapBuildingsInvalid = false;
        }
        QSet<MapBuildingsNS::Room*> switchedRooms;
        for (const QPoint &position : qAsConst(roomSwitchPositions)) {
            if (MapBuildingsNS::Room *room =
                    mMapBuildings->roomAt(position, level))
                switchedRooms.insert(room);
        }
        for (MapBuildingsNS::Room *room : qAsConst(switchedRooms)) {
            for (MapBuildingsNS::RoomRect *rect : qAsConst(room->rects)) {
                QPolygonF polygon;
                polygon << renderer->tileToPixelCoords(
                               QPointF(rect->x, rect->y), level)
                        << renderer->tileToPixelCoords(
                               QPointF(rect->x + rect->w, rect->y), level)
                        << renderer->tileToPixelCoords(
                               QPointF(rect->x + rect->w,
                                       rect->y + rect->h), level)
                        << renderer->tileToPixelCoords(
                               QPointF(rect->x, rect->y + rect->h), level);
                litRooms.append(polygon);
            }
        }
    }

    mNightPreviewItem->setBounds(sceneRect());
    mNightPreviewItem->setLights(lights);
    mNightPreviewItem->setLitRooms(litRooms);
    qInfo() << "Night preview detected" << lights.size()
            << "tile light source(s) and" << litRooms.size()
            << "lit room polygon(s) at level" << level;
}

class DummyGraphicsItem : public QGraphicsItem
{
public:
    DummyGraphicsItem()
        : QGraphicsItem()
    {
        // Since we don't do any painting, we can spare us the call to paint()
        setFlag(QGraphicsItem::ItemHasNoContents);
    }
        
    // QGraphicsItem
    QRectF boundingRect() const
    {
        return QRectF();
    }
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = 0)
    {
        Q_UNUSED(painter)
        Q_UNUSED(option)
        Q_UNUSED(widget)
    }
};

QGraphicsItem *ZomboidScene::createLayerItem(Layer *layer)
{
    if (TileLayer *tl = layer->asTileLayer()) {
        if (tl->group()) {
            if (!mTileLayerGroupItems[tl->level()]) {
                mTileLayerGroupItems[tl->level()] = new CompositeLayerGroupItem((CompositeLayerGroup*)tl->group(),
                                                                                mMapDocument->renderer());
                addItem(mTileLayerGroupItems[tl->level()]);
                mMapDocument->renderer()->setMaxLevel(mMapDocument->mapComposite()->maxLevel());
                updateLayerGroupLater(tl->level(), Bounds);
            }
            return new DummyGraphicsItem();
        }
    }
    return MapScene::createLayerItem(layer);
}

void ZomboidScene::updateCurrentLayerHighlight()
{
    if (!mMapDocument)
        return;

    int currentLevel = mMapDocument->currentLevel();

    Layer *currentLayer = mMapDocument->currentLayer();
    int currentLayerIndex = mMapDocument->currentLayerIndex();

    if (currentLevel == INVALID_LEVEL) {
        if (currentLayer != nullptr) {
            currentLevel = currentLayer->level();
        }
    } else {
        if (mTileLayerGroupItems.contains(currentLevel)) {
            CompositeLayerGroup *layerGroup = mTileLayerGroupItems[currentLevel]->layerGroup();
            if (layerGroup->layerCount()) {
                currentLayer = layerGroup->layers().first();
                currentLayerIndex = mMapDocument->map()->layers().indexOf(currentLayer);
            }
        } else {
            currentLevel = INVALID_LEVEL;
        }
    }

    if (!mHighlightCurrentLayer || (currentLevel == INVALID_LEVEL)) {
        mDarkRectangle->setVisible(false);

        // Restore visibility for all non-ZTileLayerGroupItem layers
        for (int i = 0; i < mLayerItems.size(); ++i) {
            const Layer *layer = mMapDocument->map()->layerAt(i);
            mLayerItems.at(i)->setVisible(layer->isVisible());
        }

        // Restore visibility for all ZTileLayerGroupItem layers
        foreach (CompositeLayerGroupItem *item, mTileLayerGroupItems) {
            item->setVisible(item->layerGroup()->isVisible());
        }

        return;
    }

    QGraphicsItem *currentItem = nullptr;
//    if (currentLayer) {
//        currentItem = mLayerItems[currentLayerIndex];
//    } else {
        Q_ASSERT(mTileLayerGroupItems.contains(currentLevel));
        if (mTileLayerGroupItems.contains(currentLevel))
            currentItem = mTileLayerGroupItems[currentLevel];
        else
            return;
//    }

    // Hide items above the current item
    int index = 0;
    foreach (QGraphicsItem *item, mLayerItems) {
        Layer *layer = mMapDocument->map()->layerAt(index);
        bool visible = layer->isVisible() && (layer->level() <= currentLevel);
        if (layer->isObjectGroup() && (layer->level() != currentLevel))
            visible = false;
        item->setVisible(visible);
        ++index;
    }
    foreach (CompositeLayerGroupItem *item, mTileLayerGroupItems) {
        bool visible = item->layerGroup()->isVisible() && (item->layerGroup()->level() <= currentLevel);
        item->setVisible(visible);
    }

    // Darken layers below the current item
    mDarkRectangle->setZValue(currentItem->zValue() - 0.5);
    mDarkRectangle->setVisible(true);
}

void ZomboidScene::layerAdded(int index)
{
    MapScene::layerAdded(index);

    if (mHighlightCurrentLayer) {
        if (ObjectGroup *og = mMapDocument->map()->layerAt(index)->asObjectGroup()) {
            int level;
            MapComposite::levelForLayer(og, &level);
            mLayerItems[index]->setVisible(og->isVisible() &&
                                           (level == mMapDocument->currentLevel()));
        }
    }

    mMapBuildingsInvalid = true;

    doLater(ZOrder);
}

void ZomboidScene::layerRemoved(int index)
{
    MapScene::layerRemoved(index);

    mMapBuildingsInvalid = true;

    doLater(ZOrder);
}

/**
 * A layer has changed. This can mean that the layer visibility, opacity or
 * name has changed.
 */
void ZomboidScene::layerChanged(int index)
{
    MapScene::layerChanged(index);

    Layer *layer = mMapDocument->map()->layerAt(index);
    if (TileLayer *tl = layer->asTileLayer()) {
        // Changing the name of a layer affects MapComposite::mBmpBlendLayers.
        if (!tl->level() && mapDocument()->mapComposite()->layerGroupForLevel(0)->setBmpBlendLayers(
                        mapDocument()->mapComposite()->bmpBlender()->tileLayers()))
            updateLayerGroupLater(0, Synch | Bounds);
        if (tl->group() && mTileLayerGroupItems.contains(tl->level())) {
            CompositeLayerGroupItem *layerGroupItem = mTileLayerGroupItems[tl->level()];
            if (layerGroupItem->layerGroup()->setLayerVisibility(tl, tl->isVisible()))
                updateLayerGroupLater(tl->level(), Synch | Bounds);
            if (layerGroupItem->layerGroup()->setLayerOpacity(tl, tl->opacity())) {
                layerGroupItem->layerGroup()->synchSubMapLayerOpacity(tl->name(), tl->opacity());
                updateLayerGroupLater(tl->level(), Paint);
            }
        }
    } else if (ObjectGroup *og = layer->asObjectGroup()) {
        bool synch = false;
        foreach (MapObject *mo, og->objects()) {
            if (mMapObjectToLot.contains(mo)) {
                // FIXME: layerVisibilityChanged() signal please
                if (mMapObjectToLot[mo]->isGroupVisible() != og->isVisible()) {
                    mMapObjectToLot[mo]->setGroupVisible(og->isVisible());
                    synch = true;
                }
            }
        }
        if (mHighlightCurrentLayer && (index < mLayerItems.size())) {
            QGraphicsItem *layerItem = mLayerItems.at(index);
            layerItem->setVisible(og->isVisible()
                                  && (og->level() == mMapDocument->currentLevel()));
        }
        mMapBuildingsInvalid = true;
        if (synch)
            updateLayerGroupsLater(Synch | Bounds);
    }
#if 0
    const Layer *layer = mMapDocument->map()->layerAt(index);
    QGraphicsItem *layerItem = mLayerItems.at(index);

    layerItem->setVisible(layer->isVisible());

    qreal multiplier = 1;
    if (mHighlightCurrentLayer && mMapDocument->currentLayerIndex() < index)
        multiplier = opacityFactor;

    layerItem->setOpacity(layer->opacity() * multiplier);
#endif
}

void ZomboidScene::layerGroupAdded(int level)
{
    if (!mTileLayerGroupItems.contains(level)) {
        CompositeLayerGroup *layerGroup = mMapDocument->mapComposite()->tileLayersForLevel(level);
        mTileLayerGroupItems[level] = new CompositeLayerGroupItem((CompositeLayerGroup*)layerGroup,
                                                                  mMapDocument->renderer());
        addItem(mTileLayerGroupItems[level]);
        mMapDocument->renderer()->setMaxLevel(mMapDocument->mapComposite()->maxLevel());
        updateLayerGroupLater(level, Synch | Bounds | ZOrder);
    }

    // Setting a new maxLevel() for a map resizes the scene, requiring all existing items to be repositioned.
    if (level == mMapDocument->mapComposite()->maxLevel()) {
        mMapDocument->renderer()->setMaxLevel(level);
        mapChanged();
    }
}

void ZomboidScene::layerGroupVisibilityChanged(CompositeLayerGroup *g)
{
    if (mTileLayerGroupItems.contains(g->level())) {
        mTileLayerGroupItems[g->level()]->setVisible(g->isVisible());
        updateLayerGroupLater(g->level(), Synch | Bounds);
    }
}

void ZomboidScene::layerAddedToGroup(int index)
{
    Layer *layer = mMapDocument->map()->layerAt(index);
    Q_ASSERT(layer->isTileLayer());
    int level = layer->level();
    if (mTileLayerGroupItems.contains(level))
        updateLayerGroupLater(level, Synch | Bounds);

    if (!level)
        mapDocument()->mapComposite()->layerGroupForLevel(0)->setBmpBlendLayers(
                    mapDocument()->mapComposite()->bmpBlender()->tileLayers());

    // If a TileLayerGroup owns a layer, then a DummyGraphicsItem is created which is
    // managed by the base class.
    // If no TileLayerGroup owns a layer, then a TileLayerItem is created which is
    // managed by the base class (MapScene) See createLayerItem().
    delete mLayerItems[index]; // TileLayerItem
    mLayerItems[index] = new DummyGraphicsItem();
    mLayerItems[index]->setVisible(layer->isVisible());
    addItem(mLayerItems[index]);

    doLater(ZOrder);
}

void ZomboidScene::layerRemovedFromGroup(int index, CompositeLayerGroup *oldGroup)
{
    Q_UNUSED(oldGroup)
    Layer *layer = mMapDocument->map()->layerAt(index);
    Q_ASSERT(layer->isTileLayer());
    TileLayer *tl = layer->asTileLayer();

    int level = layer->level(); // STILL VALID FOR GROUP IT WAS IN
    if (mTileLayerGroupItems.contains(level))
        updateLayerGroupLater(level, Synch | Bounds);

    // If a TileLayerGroup owns a layer, then a DummyGraphicsItem is created which is
    // managed by the base class.
    // If no TileLayerGroup owns a layer, then a TileLayerItem is created which is
    // managed by the base class (MapScene) See createLayerItem().
    delete mLayerItems[index]; // DummyGraphicsItem
    mLayerItems[index] = new TileLayerItem(tl, mMapDocument->renderer());
    mLayerItems[index]->setVisible(tl->isVisible());
    mLayerItems[index]->setOpacity(tl->opacity());
    addItem(mLayerItems[index]);

    doLater(ZOrder);
}

void ZomboidScene::layerLevelChanged(int index, int oldLevel)
{
    Q_UNUSED(oldLevel)
    Layer *layer = mMapDocument->map()->layerAt(index);

    if (/*TileLayer *tl =*/ layer->asTileLayer()) {

    } else if (ObjectGroup *og = layer->asObjectGroup()) {
        bool synch = false;
        foreach (MapObject *mapObject, og->objects()) {
            if (mMapObjectToLot.contains(mapObject)) {
                MapComposite *lot = mMapObjectToLot[mapObject];
                lot->setGroupVisible(og->isVisible());
                lot->setLevel(og->level());
                mMapDocument->mapComposite()->checkMinMaxLevels(lot->levelOffset() + lot->minLevel(), lot->levelOffset() + lot->maxLevel());
                // Recalculate the MapObject bounds
                onLotUpdated(lot, mapObject);
                synch = true;
            }
            mObjectItems[mapObject]->syncWithMapObject();
        }
        if (synch)
            updateLayerGroupsLater(Synch | Bounds);
    } else {
        // ImageLayer
        mLayerItems[index]->update();
    }

    mGridItem->currentLayerIndexChanged(); // index didn't change, just updating the bounds
}

void ZomboidScene::doLater(PendingFlags flags)
{
#if 1
    mPendingFlags |= flags;
    handlePendingUpdates();
#else
    mPendingFlags |= flags;
    if (mPendingActive)
        return;
    QMetaObject::invokeMethod(this, "handlePendingUpdates",
                              Qt::QueuedConnection);
    mPendingActive = true;
#endif
}

// Determine sane Z-order for layers in and out of TileLayerGroups
void ZomboidScene::setGraphicsSceneZOrder()
{
    MapComposite::ZOrderList zorder = mMapDocument->mapComposite()->zOrder();
    int z = 0;
    foreach (MapComposite::ZOrderItem zo, zorder) {
        if (zo.group) {
            int level = zo.group->level();
            if (mTileLayerGroupItems.contains(level))
                mTileLayerGroupItems[level]->setZValue(z);
        } else if (mLayerItems.size() > zo.layerIndex) {
            if (mLayerItems[zo.layerIndex])
                mLayerItems[zo.layerIndex]->setZValue(z);
        }
        ++z;
    }
}

QRegion ZomboidScene::getBuildingRegion(const QPoint &tilePos, QRegion &roomRgn)
{
    if (mMapBuildingsInvalid) {
        mMapBuildings->calculate(mMapDocument->mapComposite());
        mMapBuildingsInvalid = false;
    }
    if (MapBuildingsNS::Room *room = mMapBuildings->roomAt(tilePos,
                                                           mMapDocument->currentLevel())) {
        roomRgn = room->region();
        return room->building->region();
    }
    return QRegion();
}

void ZomboidScene::setHighlightRoomPosition(const QPoint &tilePos)
{
    MapComposite *mc = mMapDocument->mapComposite();
    int level = mMapDocument->currentLevel();
    QRegion buildingRgn, roomRgn;
    if (Preferences::instance()->highlightRoomUnderPointer())
        buildingRgn = getBuildingRegion(tilePos, roomRgn);
    if (buildingRgn - roomRgn != mc->suppressRegion() ||
            level != mc->suppressLevel()) {
        mc->setSuppressRegion(buildingRgn - roomRgn, level);
        update();
    }
    mHighlightRoomPosition = tilePos;
}

void ZomboidScene::onLotAdded(MapComposite *lot, MapObject *mapObject)
{
    mMapObjectToLot[mapObject] = lot;

    MapObjectItem *item = itemForObject(mapObject); // FIXME: assumes createLayerItem() was called before this
    if (item) {
        item->setLot(lot);

        // Resize the map object to the size of the lot's map, and snap-to-grid
        mapObject->setPosition(lot->origin());
        item->resize(lot->map()->size());

        MapImage *mapImage = MapImageManager::instance()->getMapImage(lot->mapInfo()->path());
        item->setMapImage(mapImage);
    }

    mMapBuildingsInvalid = true;

    updateLayerGroupsLater(Synch | Bounds);
}

void ZomboidScene::onLotRemoved(MapComposite *lot, MapObject *mapObject)
{
    // NB: 'lot' is deleted already
    Q_UNUSED(lot)
    MapObjectItem *item = itemForObject(mapObject);
    if (item) {
        item->setLot(0);
        item->setMapImage(0);
    }
    mMapObjectToLot.remove(mapObject);

    mMapBuildingsInvalid = true;

    updateLayerGroupsLater(Synch | Bounds);
}

void ZomboidScene::onLotUpdated(MapComposite *lot, MapObject *mapObject)
{
    MapObjectItem *item = itemForObject(mapObject);
    if (item) {
        // Resize the map object to the size of the lot's map, and snap-to-grid
        mapObject->setPosition(lot->origin());
        item->resize(lot->map()->size());
    }

    mMapBuildingsInvalid = true;

    updateLayerGroupsLater(Synch | Bounds);
}

void ZomboidScene::onLotUpdated(MapComposite *mc, WorldCellLot *lot)
{
    Q_UNUSED(mc)
    Q_UNUSED(lot)
    mMapBuildingsInvalid = true;
    updateLayerGroupsLater(Synch | Bounds);
}

void ZomboidScene::invalidateMapBuildings()
{
    mMapBuildingsInvalid = true;
}

// Called when a map file displayed as a Lot is changed on disk.
void ZomboidScene::mapCompositeChanged()
{
    QMap<MapObject*,MapComposite*>::iterator it_begin = mMapObjectToLot.begin();
    QMap<MapObject*,MapComposite*>::iterator it_end = mMapObjectToLot.end();
    QMap<MapObject*,MapComposite*>::iterator it;
    for (it = it_begin; it != it_end; it++) {
        MapObject *mapObject = it.key();
        MapComposite *lot = it.value();
        if (MapObjectItem *item = itemForObject(mapObject))
            item->resize(lot->map()->size());
    }
    mMapBuildingsInvalid = true;
    updateLayerGroupsLater(Synch | Bounds);
}

void ZomboidScene::bmpBlenderLayersRecreated()
{
    if (mTileLayerGroupItems.contains(0))
        updateLayerGroupLater(0, Synch | Bounds);
}

void ZomboidScene::bmpPainted(int bmpIndex, const QRegion &region)
{
    Q_UNUSED(bmpIndex)
    const MapRenderer *renderer = mMapDocument->renderer();
    const QMargins margins = mMapDocument->map()->drawMargins();

    for (const QRect &r : region) {
        update(renderer->boundingRect(r, 0).adjusted(-margins.left(),
                                                     -margins.top(),
                                                     margins.right(),
                                                     margins.bottom()));
    }
}

void ZomboidScene::bmpXXXChanged()
{
    if (mTileLayerGroupItems.contains(0))
        updateLayerGroupLater(0, Synch | Bounds);
}

void ZomboidScene::noBlendPainted(MapNoBlend *noBlend, const QRegion &rgn)
{
    Q_UNUSED(noBlend)
    bmpPainted(0, rgn);
}

void ZomboidScene::synchNoBlendVisible()
{
    QString layerName;
    if (mapDocument()->currentLayer() && mapDocument()->currentLayer()->asTileLayer()) {
        layerName = mapDocument()->currentLayer()->nameWithPrefix();
        if (!mapDocument()->mapComposite()->bmpBlender()->blendLayers().contains(layerName))
            layerName.clear();
    }
    if (NoBlendTool::instance() != ToolManager::instance()->selectedTool())
        layerName.clear();
    if (layerName != mapDocument()->mapComposite()->noBlendLayer()) {
        mapDocument()->mapComposite()->setNoBlendLayer(layerName);
        update();
    }
}

void ZomboidScene::highlightRoomUnderPointerChanged(bool highlight)
{
    Q_UNUSED(highlight)
    setHighlightRoomPosition(mHighlightRoomPosition);
}

void ZomboidScene::showLotFloorsOnlyChanged(bool show)
{
    MapComposite *mc = mMapDocument->mapComposite();
    mc->setShowLotFloorsOnly(show);
    update();
}

void ZomboidScene::showInvisibleTilesChanged(bool show)
{
    mapDocument()->renderer()->setShowInvisibleTiles(show);
    update();
}

void ZomboidScene::showCellBorderChanged(bool show)
{
    mMapBordersItem->setVisible(show && (mapDocument()->map()->size() == QSize(300, 300)));
}

void ZomboidScene::handlePendingUpdates()
{
    MapComposite *mapComposite = mMapDocument->mapComposite();
    if (mTileLayerGroupItems.size() != mapComposite->layerGroupCount()) {
        foreach (CompositeLayerGroup *layerGroup, mapComposite->layerGroups()) {
            int level = layerGroup->level();
            if (!mTileLayerGroupItems.contains(level)) {
                mTileLayerGroupItems[level]
                        = new CompositeLayerGroupItem(layerGroup,
                                                      mMapDocument->renderer());
                addItem(mTileLayerGroupItems[level]);
                mPendingFlags |= ZOrder;
            }
        }
        mMapDocument->renderer()->setMaxLevel(mapComposite->maxLevel());
    }

    if (mPendingFlags & AllGroups)
        mPendingGroupItems = mTileLayerGroupItems.values();
    if (mPendingFlags & Synch) {
        foreach (CompositeLayerGroupItem *item, mPendingGroupItems)
            item->synchWithTileLayers();
    }
    if (mPendingFlags & Bounds) {
        foreach (CompositeLayerGroupItem *item, mPendingGroupItems)
            item->updateBounds();
        QRectF sceneRect = mMapDocument->mapComposite()->boundingRect(mMapDocument->renderer());
        if (sceneRect != this->sceneRect()) {
            MapScene::mapChanged(); // must reposition items
//            setSceneRect(sceneRect);
//            mDarkRectangle->setRect(sceneRect);
        }
        QPolygonF polygon;
        QRectF rect(0 - 0.5, 0 - 0.5,
                    mapDocument()->map()->width() + 1.0,
                    mapDocument()->map()->height() + 1.0);
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.topLeft()));
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.topRight()));
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.bottomRight()));
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.bottomLeft()));
        mMapBordersItem->setPolygon(polygon);
        mMapBordersItem->setVisible(Preferences::instance()->showCellBorder() && (mapDocument()->map()->size() == QSize(300, 300)));
#if 0
        rect = QRect(0, 0,
                     mapDocument()->map()->width(),
                     mapDocument()->map()->height());
        polygon.clear();
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.topLeft()));
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.topRight()));
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.bottomRight()));
        polygon << QPointF(mapDocument()->renderer()->tileToPixelCoords(rect.bottomLeft()));
        mMapBordersItem2->setPolygon(polygon);
#endif
    }
    if (mPendingFlags & ZOrder)
        setGraphicsSceneZOrder();
    if (mPendingFlags & Paint) {
        foreach (CompositeLayerGroupItem *item, mPendingGroupItems)
            item->update();
    }
    if (mPendingFlags & Highlight)
        updateCurrentLayerHighlight();

    mPendingFlags = None;
    mPendingGroupItems.clear();
    mPendingActive = false;
}

void ZomboidScene::mouseMoveEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
   MapScene::mouseMoveEvent(mouseEvent);

   QPoint tilePos = mapDocument()->renderer()->pixelToTileCoordsInt(mouseEvent->scenePos(),
                                                                    mapDocument()->currentLevel());
   if (tilePos != mHighlightRoomPosition)
       setHighlightRoomPosition(tilePos);
}

#include "preferences.h"
#include "addremovemapobject.h"
#include <QFileInfo>
#include <QMimeData>
#include <QStringList>
#include <qgraphicssceneevent.h>
#include <QUrl>

/////


/**
 * Item that represents a map during drag-and-drop.
 */
class DnDItem : public QGraphicsItem
{
public:
    DnDItem(const QString &path, Tiled::MapRenderer *renderer, int level, QGraphicsItem *parent = 0);

    QRectF boundingRect() const;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *);

    QPainterPath shape() const;

    void setTilePosition(QPoint tilePos);

    void setHotSpot(const QPoint &pos);
    void setHotSpot(int x, int y) { setHotSpot(QPoint(x, y)); }
    QPoint hotSpot() { return mHotSpot; }

    QPoint dropPosition();

    MapInfo *mapInfo();

private:
    MapImage *mMapImage;
    Tiled::MapRenderer *mRenderer;
    QRectF mBoundingRect;
    QPoint mPositionInMap;
    QPoint mHotSpot;
    int mLevel;
};

/////

DnDItem::DnDItem(const QString &path, MapRenderer *renderer, int level, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , mMapImage(MapImageManager::instance()->getMapImage(path))
    , mRenderer(renderer)
    , mLevel(level)
{
    setHotSpot(mMapImage->mapInfo()->width() / 2, mMapImage->mapInfo()->height() / 2);
}

QRectF DnDItem::boundingRect() const
{
    return mBoundingRect;
}

void DnDItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setOpacity(0.5);
    QRectF target = mBoundingRect;
    QRectF source = QRect(QPoint(0, 0), mMapImage->image().size());
    painter->drawImage(target, mMapImage->image(), source);
    painter->setOpacity(effectiveOpacity());

    QRect tileBounds(mPositionInMap.x() - mHotSpot.x(), mPositionInMap.y() - mHotSpot.y(),
                     mMapImage->mapInfo()->width(), mMapImage->mapInfo()->height());
    mRenderer->drawFancyRectangle(painter, tileBounds, Qt::darkGray, mLevel);

#ifdef _DEBUG
    painter->drawRect(mBoundingRect);
#endif
}

QPainterPath DnDItem::shape() const
{
    // FIXME: need polygon
    return QGraphicsItem::shape();
}

void DnDItem::setTilePosition(QPoint tilePos)
{
    mPositionInMap = tilePos;
    QRectF bounds;
    QSize scaledImageSize(mMapImage->image().size() / mMapImage->scale());
    bounds = QRectF(-mMapImage->tileToImageCoords(mHotSpot) / mMapImage->scale(), scaledImageSize);
    bounds.translate(mRenderer->tileToPixelCoords(mPositionInMap, mLevel));
    if (bounds != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

void DnDItem::setHotSpot(const QPoint &pos)
{
    // Position the item so that the top-left corner of the hotspot tile is at the item's origin
    mHotSpot = pos;
    QSize scaledImageSize(mMapImage->image().size() / mMapImage->scale());
    mBoundingRect = QRectF(-mMapImage->tileToImageCoords(mHotSpot) / mMapImage->scale(), scaledImageSize);
}

QPoint DnDItem::dropPosition()
{
    return mPositionInMap - mHotSpot;
}

MapInfo *DnDItem::mapInfo()
{
    return mMapImage->mapInfo();
}

/////

void ZomboidScene::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    Layer *layer = mMapDocument->currentLayer();
    if (!layer) {
        event->ignore();
        return;
    }
    ObjectGroup *objectGroup = layer->asObjectGroup();
    if (!objectGroup) {
        event->ignore();
        return;
    }

    foreach (const QUrl &url, event->mimeData()->urls()) {
        QFileInfo info(url.toLocalFile());
        if (!info.exists()) continue;
        if (!info.isFile()) continue;
        if (info.suffix() != QLatin1String("tmx") &&
                info.suffix() != QLatin1String("tbx")) continue;
        if (!MapManager::instance()->mapInfo(info.canonicalFilePath()))
            continue;

        QString path = info.canonicalFilePath();
        MapRenderer *renderer = mMapDocument->renderer();
        mDnDItem = new DnDItem(path, renderer, objectGroup->level());
        QPoint tilePos = renderer->pixelToTileCoords(event->scenePos(), objectGroup->level()).toPoint();
        mDnDItem->setTilePosition(tilePos);
        addItem(mDnDItem);
        mDnDItem->setZValue(10001);

        mWasHighlightCurrentLayer = mHighlightCurrentLayer;
        if (!mWasHighlightCurrentLayer)
            Preferences::instance()->setHighlightCurrentLayer(true);
        else
            updateCurrentLayerHighlight();

        event->accept();
        return;
    }

    event->ignore();
}

void ZomboidScene::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    if (mDnDItem) {
        int level = mMapDocument->currentLevel();
        QPoint tilePos = mMapDocument->renderer()->pixelToTileCoords(event->scenePos(), level).toPoint();
        mDnDItem->setTilePosition(tilePos);
    }
}

void ZomboidScene::dragLeaveEvent(QGraphicsSceneDragDropEvent *event)
{
    Q_UNUSED(event)
    if (mDnDItem) {
        delete mDnDItem;
        mDnDItem = 0;

        if (!mWasHighlightCurrentLayer)
            Preferences::instance()->setHighlightCurrentLayer(false);
        else
            updateCurrentLayerHighlight();
    }
}

void ZomboidScene::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    Q_UNUSED(event)
    Layer *layer = mMapDocument->currentLayer();
    if (!layer) return;
    ObjectGroup *objectGroup = layer->asObjectGroup();
    if (!objectGroup) return;

    if (mDnDItem) {

        QString mapName = mDnDItem->mapInfo()->path();
        MapObject *newMapObject = new MapObject(QLatin1String("lot"),
                                                mapName,
                                                mDnDItem->dropPosition(),
                                                mDnDItem->mapInfo()->size());
        delete mDnDItem;
        mDnDItem = 0;

        if (!mWasHighlightCurrentLayer)
            Preferences::instance()->setHighlightCurrentLayer(false);
        else
            updateCurrentLayerHighlight();

        mapDocument()->undoStack()->push(new AddMapObject(mapDocument(),
                                                          objectGroup,
                                                          newMapObject));
    }
}
