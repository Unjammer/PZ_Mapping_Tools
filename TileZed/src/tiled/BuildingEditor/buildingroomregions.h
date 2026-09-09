#ifndef BUILDINGROOMREGIONS_H
#define BUILDINGROOMREGIONS_H

#include "buildingdocument.h"
#include "buildingroomdef.h"

#include <QHash>

namespace BuildingEditor {

class BuildingRoomRegions : public QObject
{
public:
    explicit BuildingRoomRegions(BuildingDocument *document, QObject *parent = nullptr)
        : QObject(parent)
    {
        const auto invalidate = [this]() { mRegions.clear(); };
        connect(document, &BuildingDocument::roomAtPositionChanged, this, invalidate);
        connect(document, &BuildingDocument::roomDefinitionChanged, this, invalidate);
        connect(document, &BuildingDocument::roomAdded, this, invalidate);
        connect(document, &BuildingDocument::roomRemoved, this, invalidate);
        connect(document, &BuildingDocument::roomChanged, this, invalidate);
        connect(document, &BuildingDocument::floorAdded, this, invalidate);
        connect(document, &BuildingDocument::floorRemoved, this, invalidate);
        connect(document, &BuildingDocument::floorEdited, this, invalidate);
        connect(document, &BuildingDocument::objectAdded, this, invalidate);
        connect(document, &BuildingDocument::objectRemoved, this, invalidate);
        connect(document, &BuildingDocument::objectMoved, this, invalidate);
        connect(document, &BuildingDocument::objectTileChanged, this, invalidate);
        connect(document, &BuildingDocument::objectChanged, this, invalidate);
        connect(document, &BuildingDocument::buildingResized, this, invalidate);
        connect(document, &BuildingDocument::buildingRotated, this, invalidate);
        connect(document, &QObject::destroyed, this, invalidate);
    }

    const QList<QRegion> &regions(BuildingFloor *floor, Room *room)
    {
        QHash<Room*, QList<QRegion>> &rooms = mRegions[floor];
        auto found = rooms.constFind(room);
        if (found != rooms.constEnd())
            return found.value();
        BuildingRoomDefecator defecator(floor, room);
        defecator.defecate();
        return rooms.insert(room, defecator.mRegions).value();
    }

private:
    QHash<BuildingFloor*, QHash<Room*, QList<QRegion>>> mRegions;
};

}

#endif
