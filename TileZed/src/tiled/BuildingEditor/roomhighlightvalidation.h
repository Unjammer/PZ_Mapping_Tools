#ifndef ROOMHIGHLIGHTVALIDATION_H
#define ROOMHIGHLIGHTVALIDATION_H

#include "building.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingreader.h"
#include "buildingroomregions.h"
#include "buildingundoredo.h"

#include <QElapsedTimer>
#include <QScopedPointer>

namespace BuildingEditor {

inline bool validateRoomHighlight(const QString &path, QString *summary, QString *error)
{
    BuildingReader reader;
    Building *building = reader.read(path);
    if (!building) {
        *error = reader.errorString();
        return false;
    }
    QScopedPointer<BuildingDocument> document(new BuildingDocument(building, QString()));
    BuildingRoomRegions cache(document.data());
    struct Sample {
        BuildingFloor *floor;
        Room *room;
        QList<QRegion> expected;
    };
    QVector<Sample> samples;
    const auto check = [&]() {
        for (BuildingFloor *floor : building->floors()) {
            QSet<Room*> rooms;
            for (int y = 0; y < floor->height(); ++y) {
                for (int x = 0; x < floor->width(); ++x) {
                    if (Room *room = floor->GetRoomAt(x, y))
                        rooms.insert(room);
                }
            }
            for (Room *room : rooms) {
                BuildingRoomDefecator reference(floor, room);
                reference.defecate();
                if (cache.regions(floor, room) != reference.mRegions)
                    return false;
            }
        }
        return true;
    };
    for (BuildingFloor *floor : building->floors()) {
        QSet<Room*> rooms;
        for (int y = 0; y < floor->height(); ++y) {
            for (int x = 0; x < floor->width(); ++x) {
                if (Room *room = floor->GetRoomAt(x, y))
                    rooms.insert(room);
            }
        }
        for (Room *room : rooms) {
            BuildingRoomDefecator reference(floor, room);
            reference.defecate();
            samples.append({floor, room, reference.mRegions});
        }
    }
    if (samples.isEmpty()) {
        *error = QStringLiteral("The building has no painted rooms");
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    for (const Sample &sample : samples) {
        if (cache.regions(sample.floor, sample.room) != sample.expected) {
            *error = QStringLiteral("Initial room regions differ");
            return false;
        }
    }
    const qint64 coldNs = timer.nsecsElapsed();
    const int repeats = 20;
    qint64 regionsRead = 0;
    timer.restart();
    for (int i = 0; i < repeats; ++i) {
        for (const Sample &sample : samples) {
            BuildingRoomDefecator reference(sample.floor, sample.room);
            reference.defecate();
            regionsRead += reference.mRegions.size();
        }
    }
    const qint64 previousNs = timer.nsecsElapsed();
    timer.restart();
    for (int i = 0; i < repeats; ++i) {
        for (const Sample &sample : samples) {
            const QList<QRegion> &regions = cache.regions(sample.floor, sample.room);
            if (regions != sample.expected) {
                *error = QStringLiteral("Unchanged room regions differ");
                return false;
            }
            regionsRead += regions.size();
        }
    }
    const qint64 cachedNs = timer.nsecsElapsed();
    const Sample sample = samples.first();
    const QPoint position = sample.expected.first().begin()->topLeft();
    document->undoStack()->push(new ChangeRoomAtPosition(
                                   document.data(), sample.floor, position, nullptr));
    if (!check() || cache.regions(sample.floor, sample.room) == sample.expected) {
        *error = QStringLiteral("Room edit did not invalidate the regions");
        return false;
    }
    document->undoStack()->undo();
    if (!check() || cache.regions(sample.floor, sample.room) != sample.expected) {
        *error = QStringLiteral("Undo did not restore room regions");
        return false;
    }
    document->undoStack()->redo();
    if (!check()) {
        *error = QStringLiteral("Redo retained stale room regions");
        return false;
    }
    document->undoStack()->undo();
    int wallChecks = 0;
    for (BuildingFloor *floor : building->floors()) {
        for (BuildingObject *object : floor->objects()) {
            if (!object->asWall())
                continue;
            const QPoint original = object->pos();
            document->moveObject(object, original + QPoint(1, 0));
            if (!check()) {
                *error = QStringLiteral("Moving a wall retained stale room regions");
                return false;
            }
            document->moveObject(object, original);
            if (!check()) {
                *error = QStringLiteral("Restoring a wall retained stale room regions");
                return false;
            }
            ++wallChecks;
            break;
        }
    }
    document->undoStack()->beginMacro(QStringLiteral("Rotate Building"));
    document->undoStack()->push(new EmitRotateBuilding(document.data(), true));
    document->undoStack()->push(new RotateBuilding(document.data(), true));
    document->undoStack()->push(new EmitRotateBuilding(document.data(), false));
    document->undoStack()->endMacro();
    if (!check()) {
        *error = QStringLiteral("Rotating the building retained stale room regions");
        return false;
    }
    document->undoStack()->undo();
    if (!check()) {
        *error = QStringLiteral("Restoring the building retained stale room regions");
        return false;
    }
    *summary = QStringLiteral("%1 floor/room pairs, %2 repeated queries, initial %3 ms, "
                              "previous %4 ms, cached %5 ms, %6 wall moves, "
                              "edit/undo/redo/rotation, %7 regions read")
            .arg(samples.size()).arg(samples.size() * repeats)
            .arg(coldNs / 1000000.0, 0, 'f', 3)
            .arg(previousNs / 1000000.0, 0, 'f', 3)
            .arg(cachedNs / 1000000.0, 0, 'f', 3)
            .arg(wallChecks).arg(regionsRead);
    return true;
}

}

#endif
