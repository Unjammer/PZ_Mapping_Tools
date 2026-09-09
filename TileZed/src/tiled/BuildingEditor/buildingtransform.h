#ifndef BUILDINGTRANSFORM_H
#define BUILDINGTRANSFORM_H

#include "building.h"
#include "buildingdocument.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include <QUndoCommand>
#include <memory>

namespace BuildingEditor {

class TransformBuildingContents : public QUndoCommand
{
public:
    TransformBuildingContents(BuildingDocument *document, bool rotation, bool positive)
        : mDocument(document), mRotation(rotation), mPositive(positive),
          mSize(document->building()->size())
    {
        setText(rotation ? (positive ? QObject::tr("Rotate Right") : QObject::tr("Rotate Left"))
                         : (positive ? QObject::tr("Flip Horizontal") : QObject::tr("Flip Vertical")));
    }

    ~TransformBuildingContents() override
    {
        for (const FloorState &state : mFloors) {
            qDeleteAll(state.tiles);
            delete state.attributes;
        }
    }

    bool prepare(QString &error)
    {
        const QSize size = mRotation ? QSize(mSize.height(), mSize.width()) : mSize;
        const QRect bounds(QPoint(), size + QSize(1, 1));
        for (BuildingFloor *floor : mDocument->building()->floors()) {
            mFloors.append(FloorState{floor, {}, new Tiled::PropertiesGrid(size.width(), size.height())});
            FloorState &state = mFloors.last();
            const auto grids = floor->grime();
            for (auto layer = grids.constBegin(); layer != grids.constEnd(); ++layer) {
                auto *grid = new FloorTileGrid(bounds.width(), bounds.height());
                state.tiles.insert(layer.key(), grid);
                for (int y = 0; y < layer.value()->height(); ++y) {
                    for (int x = 0; x < layer.value()->width(); ++x) {
                        const QString &tile = layer.value()->at(x, y);
                        if (tile.isEmpty()) continue;
                        const QPoint target = mapCell(QPoint(x, y));
                        if (!bounds.contains(target)) {
                            error = QObject::tr("A painted tile on level %1, layer %2 at %3,%4 would move outside the building.\n\nMove these tiles inward or enlarge the building before transforming it. No changes were made.")
                                    .arg(floor->level()).arg(layer.key()).arg(x).arg(y);
                            return false;
                        }
                        grid->replace(target.x(), target.y(), tile);
                    }
                }
            }
            const auto *attributes = floor->squarePropertiesGrid();
            for (int y = 0; y < attributes->height(); ++y) {
                for (int x = 0; x < attributes->width(); ++x) {
                    if (!attributes->hasPropertiesAt(x, y)) continue;
                    const QPoint target = mapCell(QPoint(x, y));
                    if (!state.attributes->isValidPosition(target.x(), target.y())) {
                        error = QObject::tr("Square attributes on level %1 would move outside the building. No changes were made.").arg(floor->level());
                        return false;
                    }
                    state.attributes->replace(target.x(), target.y(), attributes->at(x, y));
                }
            }
        }
        mRoomSelection = mapRegion(mDocument->roomSelection());
        mTileSelection = mapRegion(mDocument->tileSelection());
        mBasementAccess = mDocument->building()->basementAccess();
        if (mBasementAccess.isValid()) {
            using Direction = BuildingObject::Direction;
            const bool north = mBasementAccess.mDirection == Direction::N;
            if (!north && mBasementAccess.mDirection != Direction::W) {
                error = QObject::tr("Choose a North or West basement access before transforming the building. No changes were made.");
                return false;
            }
            const QRect access = north ? QRect(mBasementAccess.mX, mBasementAccess.mY - 1, 1, 5)
                                       : QRect(mBasementAccess.mX - 1, mBasementAccess.mY, 5, 1);
            const QRect target = mapRegion(QRegion(access)).boundingRect();
            const bool newNorth = mRotation ? !north : north;
            mBasementAccess.mDirection = newNorth ? Direction::N : Direction::W;
            mBasementAccess.mX = target.x() + (newNorth ? 0 : 1);
            mBasementAccess.mY = target.y() + (newNorth ? 1 : 0);
        }
        return true;
    }

    void redo() override { apply(mPositive); }
    void undo() override { apply(mRotation ? !mPositive : mPositive); }

private:
    QPoint mapCell(const QPoint &point) const
    {
        if (mRotation)
            return mPositive ? QPoint(mSize.height() - point.y() - 1, point.x())
                             : QPoint(point.y(), mSize.width() - point.x() - 1);
        return mPositive ? QPoint(mSize.width() - point.x() - 1, point.y())
                         : QPoint(point.x(), mSize.height() - point.y() - 1);
    }

    QRegion mapRegion(const QRegion &region) const
    {
        QRegion result;
        for (const QRect &rect : region)
            result += QRect(mapCell(rect.topLeft()), mapCell(rect.bottomRight())).normalized();
        return result;
    }

    void apply(bool positive)
    {
        if (mRotation) mDocument->rotateBuilding(positive);
        else mDocument->flipBuilding(positive);
        for (FloorState &state : mFloors) {
            state.tiles = mDocument->swapFloorTiles(state.floor, state.tiles, false);
            auto *previous = state.floor->setSquarePropertiesGrid(state.attributes);
            delete state.attributes;
            state.attributes = previous;
        }
        mRoomSelection = mDocument->setRoomSelection(mRoomSelection);
        mTileSelection = mDocument->setTileSelection(mTileSelection);
        mBasementAccess = mDocument->setBasementAccess(mBasementAccess);
        mDocument->emitBuildingRotated();
    }

    struct FloorState {
        BuildingFloor *floor;
        QMap<QString, FloorTileGrid *> tiles;
        Tiled::PropertiesGrid *attributes;
    };
    BuildingDocument *mDocument;
    bool mRotation;
    bool mPositive;
    QSize mSize;
    QList<FloorState> mFloors;
    QRegion mRoomSelection;
    QRegion mTileSelection;
    BasementAccess mBasementAccess;
};

}

#endif
