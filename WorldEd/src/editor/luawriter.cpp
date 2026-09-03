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

#include "luawriter.h"

#include "lotfilesmanager.h"
#include "luatablewriter.h"
#include "map.h"
#include "maplevel.h"
#include "mapmanager.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "world.h"
#include "worldcell.h"
#include "worldobjectvalidation.h"

#include "BuildingEditor/roofhiding.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QtMath>

using namespace Lua;

class LuaWriterPrivate
{
    Q_DECLARE_TR_FUNCTIONS(LuaWriterPrivate)

public:
    LuaWriterPrivate()
        : mWorld(0)
    {

    }

    bool openFile(QFile *file)
    {
        if (!file->open(QIODevice::WriteOnly)) {
            mError = tr("Could not open file for writing.");
            return false;
        }

        return true;
    }

    void writeWorld(World *world, QIODevice *device, const QString &absDirPath)
    {
        Q_UNUSED(absDirPath)
        mWorld = world;

        LuaTableWriter w(device);
        this->w = &w;

        w.writeStartDocument();
        device->write("function TheWorld()\n");
        w.writeStartReturnTable();

        w.writeStartTable("propertydef");
        foreach (PropertyDef *pd, mWorld->propertyDefinitions())
            writePropertyDef(pd);
        w.writeEndTable();

        w.writeStartTable("cells");
        for (int y = 0; y < mWorld->width(); y++) {
            for (int x = 0; x < mWorld->height(); x++) {
                WorldCell *cell = mWorld->cellAt(x, y);
                writeCell(cell);
            }
        }
        w.writeEndTable();

        w.writeEndTable();
        device->write("\nend");
        w.writeEndDocument();
    }

    void writePropertyDef(PropertyDef *pd)
    {
        w->writeStartTable();
        w->setSuppressNewlines(true);
        w->writeKeyAndValue("name", pd->mName);
        writePropertyKeyAndValue("default", pd->mDefaultValue);
        w->writeEndTable();
        w->setSuppressNewlines(false);
    }

    void writeCell(WorldCell *cell)
    {
        if (cell->isEmpty())
            return;

        w->setSuppressNewlines(false);
        w->writeStartTable();
        w->setSuppressNewlines(true);

        w->writeKeyAndValue("x", cell->x());
        w->writeKeyAndValue("y", cell->y());
        if (!cell->mapFilePath().isEmpty())
            w->writeKeyAndValue("map", QFileInfo(cell->mapFilePath()).completeBaseName());

        PropertyList properties;
        resolveProperties(cell, properties);
        writePropertyList(properties);

        writeLotList(cell->lots());

        writeObjectList(cell->objects());

        if (properties.isEmpty() && cell->lots().isEmpty() && cell->objects().isEmpty())
            w->setSuppressNewlines(true);

        w->writeEndTable();
    }

    void writePropertyList(const PropertyList &properties)
    {
        if (properties.isEmpty())
            return;

        w->setSuppressNewlines(false);
        w->writeStartTable("properties");
        foreach (Property *p, properties)
            writeProperty(p);
        w->setSuppressNewlines(false);
        w->writeEndTable();
    }

    void writeProperty(Property *p)
    {
        w->setSuppressNewlines(false);
        w->writeStartTable();
        w->setSuppressNewlines(true);
        w->writeKeyAndValue("name", p->mDefinition->mName);
        writePropertyKeyAndValue("value", p->mValue);
        w->writeEndTable();
    }

    void writePropertyKeyAndValue(const QByteArray &_key, const QString &value)
    {
        bool isDouble;
        value.toDouble(&isDouble);
#if 1
        QByteArray key = _key;
        bool unquoted = key.length() && !isdigit(key[0]);
        for (int i = 0; i < key.length(); i++) {
            if (key[i] == '_' || isalnum(key[i])) continue;
            unquoted = false;
            break;
        }
        if (!unquoted)
            key = QString::fromUtf8("[\"%1\"]").arg(QString::fromUtf8(_key)).toUtf8();
#endif
        if (value == QLatin1String("true")
                || value == QLatin1String("false")
                || isDouble)
            w->writeKeyAndUnquotedValue(key, value.toUtf8());
        else
            w->writeKeyAndValue(key, value);
    }

    void resolveProperties(PropertyHolder *ph, PropertyList &result)
    {
        foreach (PropertyTemplate *pt, ph->templates())
            resolveProperties(pt, result);
        foreach (Property *p, ph->properties()) {
            result.removeAll(p->mDefinition);
            result += p;
        }
    }

    void writeLotList(const WorldCellLotList &lots)
    {
        if (lots.isEmpty())
            return;

        w->setSuppressNewlines(false);
        w->writeStartTable("lots");
        foreach (WorldCellLot *lot, lots)
            writeLot(lot);
        w->setSuppressNewlines(false);
        w->writeEndTable();
    }

    void writeLot(WorldCellLot *lot)
    {
        w->setSuppressNewlines(false);
        w->writeStartTable();
        w->setSuppressNewlines(true);
        w->writeKeyAndValue("x", lot->x());
        w->writeKeyAndValue("y", lot->y());
        w->writeKeyAndValue("level", lot->level());
        w->writeKeyAndValue("map", QFileInfo(lot->mapName()).completeBaseName());
        w->writeEndTable();
    }

    void writeObjectList(const WorldCellObjectList &objects)
    {
        if (objects.isEmpty())
            return;

        w->setSuppressNewlines(false);
        w->writeStartTable("objects");
        foreach (WorldCellObject *obj, objects)
            writeObject(obj);
        w->setSuppressNewlines(false);
        w->writeEndTable();
    }

    void writeObject(WorldCellObject *obj)
    {
        QPoint origin = mWorld->getGenerateLotsSettings().worldOrigin;

        w->writeStartTable();
        w->setSuppressNewlines(true);
        if (!obj->name().isEmpty())
            w->writeKeyAndValue("name", obj->name());
        w->writeKeyAndValue("type", obj->type()->name());
        if (obj->geometryType() == ObjectGeometryType::INVALID) {
            const QPointF absolute = obj->absoluteWorldPosition();
            w->writeKeyAndValue("x", absolute.x());
            w->writeKeyAndValue("y", absolute.y());
            w->writeKeyAndValue("level", obj->level());
            w->writeKeyAndValue("width", obj->width());
            w->writeKeyAndValue("height", obj->height());
        } else {
            QString geometry;
            switch (obj->geometryType()) {
            case ObjectGeometryType::INVALID:
                break;
            case ObjectGeometryType::Point:
                geometry = QLatin1String("point");
                break;
            case ObjectGeometryType::Polygon:
                geometry = QLatin1String("polygon");
                break;
            case ObjectGeometryType::Polyline:
                geometry = QLatin1String("polyline");
                break;
            }
            w->writeKeyAndValue("level", obj->level());
            w->writeKeyAndValue("geometry", geometry);
            if (obj->isPolyline() && (obj->polylineWidth() > 0)) {
                w->writeKeyAndValue("lineWidth", obj->polylineWidth());
            }
            QBuffer buf;
            buf.open(QIODevice::ReadWrite);
            LuaTableWriter w2(&buf);
            w2.setSuppressNewlines(true);
            w2.writeStartTable();
            for (const auto &point : obj->points()) {
                w2.writeValue((obj->cell()->x() + origin.x())
                              * mWorld->cellSize() + point.x);
                w2.writeValue((obj->cell()->y() + origin.y())
                              * mWorld->cellSize() + point.y);
            }
            w2.writeEndTable();
            buf.close();
            w->writeKeyAndUnquotedValue("points", buf.data());
        }

        PropertyList properties;
        resolveProperties(obj, properties);
        writePropertyList(properties);

        w->writeEndTable();
        w->setSuppressNewlines(false);
    }

    void writeSpawnPoints(World *world, QIODevice *device)
    {
        mWorld = world;

        LuaTableWriter w(device);
        this->w = &w;

        w.writeStartDocument();
        device->write("function SpawnPoints()\n");
        w.writeStartReturnTable();

        QMap<QString,WorldCellObjectList> spawnByProfession;
        PropertyDef *pd = mWorld->propertyDefinition(QLatin1String("Professions"));

        for (int y = 0; y < mWorld->height(); y++) {
            for (int x = 0; x < mWorld->width(); x++) {
                WorldCell *cell = mWorld->cellAt(x, y);
                foreach (WorldCellObject *obj, cell->objects()) {
                    if (obj->isSpawnPoint()) {
                        QString reason;
                        if (!WorldObjectValidation::validateSpawnPoint(
                                    obj, &reason)) {
                            mWarnings += QString::fromLatin1("%1: %2")
                                    .arg(WorldObjectValidation::describe(obj))
                                    .arg(reason);
                            continue;
                        }
                        PropertyList properties;
                        resolveProperties(obj, properties);
                        if (Property *p = properties.find(pd)) {
                            QStringList professions = p->mValue.split(QLatin1String(","), Qt::SkipEmptyParts);
                            foreach (QString profession, professions) {
                                profession = profession.trimmed();
                                if (!profession.isEmpty()
                                        && !spawnByProfession[profession].contains(obj)) {
                                    spawnByProfession[profession] += obj;
                                }
                            }
                        }
                    }
                }
            }
        }

        foreach (QString profession, spawnByProfession.keys()) {
            w.writeStartTable(profession.toUtf8());
            foreach (WorldCellObject *obj, spawnByProfession[profession]) {
                w.writeStartTable();
                w.setSuppressNewlines(true);
                const QPointF absolute = obj->absoluteWorldPosition();
                w.writeKeyAndValue("posX", int(qRound64(absolute.x())));
                w.writeKeyAndValue("posY", int(qRound64(absolute.y())));
                w.writeKeyAndValue("posZ", obj->level());

                PropertyList properties;
                resolveProperties(obj, properties);
                foreach (Property *p, properties) {
                    if (p->mDefinition == pd) continue;
                    writePropertyKeyAndValue(p->mDefinition->mName.toUtf8(), p->mValue);
                }

                w.writeEndTable();
                w.setSuppressNewlines(false);
            }
            w.writeEndTable();
        }

        w.writeEndTable();

        device->write("\nend");
        w.writeEndDocument();
    }

    void writeWorldObjects(World *world, QIODevice *device)
    {
        mWorld = world;

        LuaTableWriter w(device);
        this->w = &w;

        w.writeStartDocument();
        w.writeStartTable("objects");

        QPoint origin = mWorld->getGenerateLotsSettings().worldOrigin;

        for (int y = 0; y < mWorld->height(); y++) {
            for (int x = 0; x < mWorld->width(); x++) {
                WorldCell *cell = mWorld->cellAt(x, y);
                foreach (WorldCellObject *obj, cell->objects()) {
                    QString reason;
                    if (!WorldObjectValidation::validateExportObject(
                                obj, &reason)) {
                        mWarnings += QString::fromLatin1("%1: %2")
                                .arg(WorldObjectValidation::describe(obj))
                                .arg(reason);
                        continue;
                    }
                    w.writeStartTable();
                    w.setSuppressNewlines(true);
                    w.writeKeyAndValue("name", obj->name());
                    w.writeKeyAndValue("type", obj->type()->name());
                    if (obj->geometryType() == ObjectGeometryType::INVALID) {
                        const QPointF absolute = obj->absoluteWorldPosition();
                        w.writeKeyAndValue("x", absolute.x());
                        w.writeKeyAndValue("y", absolute.y());
                        w.writeKeyAndValue("z", obj->level());
                        w.writeKeyAndValue("width", obj->width());
                        w.writeKeyAndValue("height", obj->height());
                    } else {
                        QString geometry;
                        switch (obj->geometryType()) {
                        case ObjectGeometryType::INVALID:
                            break;
                        case ObjectGeometryType::Point:
                            geometry = QLatin1String("point");
                            break;
                        case ObjectGeometryType::Polygon:
                            geometry = QLatin1String("polygon");
                            break;
                        case ObjectGeometryType::Polyline:
                            geometry = QLatin1String("polyline");
                            break;
                        }
                        w.writeKeyAndValue("z", obj->level());
                        w.writeKeyAndValue("geometry", geometry);
                        if (obj->isPolyline() && (obj->polylineWidth() > 0)) {
                            w.writeKeyAndValue("lineWidth", obj->polylineWidth());
                        }
                        QBuffer buf;
                        buf.open(QIODevice::ReadWrite);
                        LuaTableWriter w2(&buf);
                        w2.setSuppressNewlines(true);
                        w2.writeStartTable();
                        QString pointStr;
                        for (const auto &point : obj->points()) {
                            w2.writeValue((obj->cell()->x() + origin.x())
                                          * mWorld->cellSize() + point.x);
                            w2.writeValue((obj->cell()->y() + origin.y())
                                          * mWorld->cellSize() + point.y);
                        }
                        w2.writeEndTable();
                        buf.close();
                        w.writeKeyAndUnquotedValue("points", buf.data());
                    }
                    PropertyList properties;
                    resolveProperties(obj, properties);
                    if (properties.size()) {

                        // Hack -- See if the "properties { ... }" string is short enough to inline it.
                        QBuffer buf;
                        buf.open(QIODevice::ReadWrite);
                        LuaTableWriter w2(&buf);
                        w2.setSuppressNewlines(true);
                        w2.writeStartTable("properties");
                        this->w = &w2;
                        foreach (Property *p, properties) {
                            writePropertyKeyAndValue(p->mDefinition->mName.toUtf8(), p->mValue);
                        }
                        this->w = &w;
                        w2.writeEndTable();
                        buf.close();
                        bool suppressNewlines = buf.data().length() <= 64; // UTF-8

                        w.setSuppressNewlines(suppressNewlines);
                        w.writeStartTable("properties");
                        foreach (Property *p, properties) {
                            writePropertyKeyAndValue(p->mDefinition->mName.toUtf8(), p->mValue);
                        }
                        w.writeEndTable();
                    }
                    w.writeEndTable();
                    w.setSuppressNewlines(false);
                }
            }
        }

        w.writeEndTable();

        w.writeEndDocument();
    }

    void writeRoomTones(World *world, QIODevice *device)
    {
        mWorld = world;

        LuaTableWriter w(device);
        this->w = &w;

        w.writeStartDocument();
        w.writeStartTable("objects");

        QPoint origin = mWorld->getGenerateLotsSettings().worldOrigin;

        for (int y = 0; y < mWorld->height(); y++) {
            for (int x = 0; x < mWorld->width(); x++) {
                WorldCell *cell = mWorld->cellAt(x, y);
#if 1
                DelayedMapLoader mapLoader;
                WorldCellLotList lots;
                for (WorldCellLot *lot : cell->lots()) {
                    MapInfo *info0 = MapManager::instance()->mapInfo(lot->mapName());
                    if ((info0 == nullptr) || info0->properties().value(QLatin1String("RoomTone")).isEmpty())
                        continue;
                    if (MapInfo *info = MapManager::instance()->loadMap(lot->mapName(), QString(), true, MapManager::PriorityMedium)) {
                        mapLoader.addMap(info);
                        lots += lot;
                    } else {
                    }
                }
                while (mapLoader.isLoading()) {
                    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
                }
#endif
                for (WorldCellLot *lot : lots) {
                    MapInfo *info = MapManager::instance()->mapInfo(lot->mapName());
                    if (info == nullptr)
                        continue;
                    if (info->map() == nullptr)
                        continue;
                    QString roomToneStr = info->properties().value(QLatin1String("RoomTone"));
                    if (roomToneStr.isEmpty())
                        continue;
                    QPoint pointInRoom;
                    int level;
                    if (getPointInRoom(info->map(), pointInRoom, level) == false)
                        continue;
                    w.writeStartTable();
                    w.setSuppressNewlines(true);
                    w.writeKeyAndValue("name", QString());
                    w.writeKeyAndValue("type", QLatin1String("RoomTone"));
                    w.writeKeyAndValue(
                                "x", (origin.x() + cell->x())
                                * mWorld->cellSize()
                                + lot->x() + pointInRoom.x());
                    w.writeKeyAndValue(
                                "y", (origin.y() + cell->y())
                                * mWorld->cellSize()
                                + lot->y() + pointInRoom.y());
                    w.writeKeyAndValue("z", lot->level() + level);
                    w.writeKeyAndValue("width", 1);
                    w.writeKeyAndValue("height", 1);
                    QStringList properties;
                    properties << QLatin1String("RoomTone") << roomToneStr;
                    properties << QLatin1String("EntireBuilding") << QLatin1String("true");
                    if (properties.isEmpty() == false) {
                        w.writeStartTable("properties");
                        for (int i = 0; i < properties.size(); i += 2) {
                            writePropertyKeyAndValue(properties[i].toUtf8(), properties[i + 1]);
                        }
                        w.writeEndTable();
                    }
                    w.writeEndTable();
                    w.setSuppressNewlines(false);
                }
            }
        }

        w.writeEndTable();
        w.writeEndDocument();
    }

    bool getPointInRoom(Tiled::Map *map, QPoint &point, int &level)
    {
        point = QPoint(0, 0);
        for (Tiled::MapLevel *mapLevel : map->mapLevels()) {
            for (Tiled::ObjectGroup *objectGroup : mapLevel->objectGroups()) {
                if (objectGroup->name().contains(QLatin1String("RoomDefs")) == false)
                    continue;
                for (Tiled::MapObject *mapObject : objectGroup->objects()) {
                    int index = mapObject->name().indexOf(QLatin1Char('#'));
                    if (index == -1)
                        continue;
                    QString internalName = mapObject->name().left(index);
                    if (BuildingEditor::RoofHiding::isEmptyOutside(internalName))
                        continue;
                    point.setX(mapObject->x() + mapObject->width() / 2);
                    point.setY(mapObject->y() + mapObject->height() / 2);
                    level = mapLevel->level();
                    return true;
                }
            }
        }
        return false;
    }

    QString mError;
    QStringList mWarnings;
    World *mWorld;
    LuaTableWriter *w;
};

/////

LuaWriter::LuaWriter()
    : d(new LuaWriterPrivate)
{
}

LuaWriter::~LuaWriter()
{
    delete d;
}

bool LuaWriter::writeWorld(World *world, const QString &filePath)
{
    QFile file(filePath);
    if (!d->openFile(&file))
        return false;

    writeWorld(world, &file, QFileInfo(filePath).absolutePath());

    if (file.error() != QFile::NoError) {
        d->mError = file.errorString();
        return false;
    }

    return true;
}

void LuaWriter::writeWorld(World *world, QIODevice *device, const QString &absDirPath)
{
    d->writeWorld(world, device, absDirPath);
}

bool LuaWriter::writeSpawnPoints(World *world, const QString &filePath)
{
    d->mError.clear();
    d->mWarnings.clear();
    QFile file(filePath);
    if (!d->openFile(&file))
        return false;

    d->writeSpawnPoints(world, &file);

    if (file.error() != QFile::NoError) {
        d->mError = file.errorString();
        return false;
    }

    return true;
}

bool LuaWriter::writeWorldObjects(World *world, const QString &filePath)
{
    d->mError.clear();
    d->mWarnings.clear();
    QFile file(filePath);
    if (!d->openFile(&file))
        return false;

    d->writeWorldObjects(world, &file);

    if (file.error() != QFile::NoError) {
        d->mError = file.errorString();
        return false;
    }

    return true;
}

bool LuaWriter::writeRoomTones(World *world, const QString &filePath)
{
    QFile file(filePath);
    if (!d->openFile(&file))
        return false;

    d->writeRoomTones(world, &file);

    if (file.error() != QFile::NoError) {
        d->mError = file.errorString();
        return false;
    }

    return true;
}

QString LuaWriter::errorString() const
{
    return d->mError;
}

QStringList LuaWriter::warnings() const
{
    return d->mWarnings;
}

namespace {

struct ExportValidationDefinitions
{
    ObjectType *addType(World &world, const QString &name)
    {
        ObjectType *type = new ObjectType(name);
        world.insertObjectType(world.objectTypes().size(), type);
        WorldObjectGroup *group = new WorldObjectGroup(
                    type, name, QColor(Qt::blue));
        world.insertObjectGroup(world.objectGroups().size(), group);
        return type;
    }

    PropertyDef *addProperty(World &world, const QString &name,
                             const QString &defaultValue,
                             PropertyEnum *propertyEnum = nullptr)
    {
        PropertyDef *definition = new PropertyDef(
                    name, defaultValue, QString(), propertyEnum);
        world.addPropertyDefinition(
                    world.propertyDefinitions().size(), definition);
        return definition;
    }

    void addProperty(WorldCellObject *object, const QString &name,
                     const QString &value)
    {
        PropertyDef *definition =
                object->cell()->world()->propertyDefinition(name);
        object->addProperty(object->properties().size(),
                            new Property(definition, value));
    }

    WorldCellObject *addObject(World &world, const QString &typeName,
                               int cellX, int cellY, qreal localX,
                               qreal localY, qreal width = 1,
                               qreal height = 1)
    {
        WorldCell *cell = world.cellAt(cellX, cellY);
        ObjectType *type = world.objectType(typeName);
        WorldObjectGroup *group = world.objectGroups().find(typeName);
        WorldCellObject *object = new WorldCellObject(
                    cell, QString(), type, group, localX, localY, 0,
                    width, height);
        cell->insertObject(cell->objects().size(), object);
        return object;
    }

    void configure(World &world)
    {
        PropertyEnum *professions = new PropertyEnum(
                    QLatin1String("Professions"),
                    QStringList() << QLatin1String("unemployed")
                                  << QLatin1String("carpenter"), true);
        world.insertPropertyEnum(world.propertyEnums().size(), professions);
        addProperty(world, QLatin1String("Professions"),
                    QLatin1String("unemployed"), professions);
        addProperty(world, QLatin1String("WaterDirection"),
                    QLatin1String("0"));
        addProperty(world, QLatin1String("WaterSpeed"),
                    QLatin1String("0.0"));
        addProperty(world, QLatin1String("WaterGround"),
                    QLatin1String("false"));
        addProperty(world, QLatin1String("WaterShore"),
                    QLatin1String("true"));
        addProperty(world, QLatin1String("RoomTone"),
                    QLatin1String("Generic"));
        addProperty(world, QLatin1String("EntireBuilding"),
                    QLatin1String("false"));
        addType(world, QLatin1String("SpawnPoint"));
        addType(world, QLatin1String("WaterFlow"));
        addType(world, QLatin1String("WaterZone"));
        addType(world, QLatin1String("RoomTone"));
        addType(world, QLatin1String("SpawnOrigin"));
    }
};

bool requireOutput(const QByteArray &output, const QByteArray &expected,
                   QString *error)
{
    if (output.contains(expected))
        return true;
    if (error) {
        *error = QString::fromLatin1("Missing generated Lua fragment: %1")
                .arg(QString::fromUtf8(expected));
    }
    return false;
}

}

bool LuaWriter::validateSpawnPointExport(QString *summary, QString *error)
{
    World world(18, 55, WorldGridFormat::Native256);
    ExportValidationDefinitions definitions;
    definitions.configure(world);

    struct CoordinateCase {
        int cellX;
        int cellY;
        int localX;
        int localY;
        int absoluteX;
        int absoluteY;
    };
    const QList<CoordinateCase> cases = {
        { 0, 0, 0, 0, 0, 0 },
        { 1, 0, 0, 0, 256, 0 },
        { 0, 1, 0, 0, 0, 256 },
        { 17, 54, 31, 123, 4383, 13947 },
        { 17, 54, 18, 124, 4370, 13948 },
        { 0, 0, 256, 256, 256, 256 },
        { 1, 1, -1, -1, 255, 255 }
    };

    for (const CoordinateCase &coordinate : cases) {
        WorldCellObject *object = definitions.addObject(
                    world, QLatin1String("SpawnPoint"),
                    coordinate.cellX, coordinate.cellY,
                    coordinate.localX, coordinate.localY);
        definitions.addProperty(object, QLatin1String("Professions"),
                                QLatin1String("unemployed"));
        const QPointF absolute = object->absoluteWorldPosition();
        if (qRound64(absolute.x()) != coordinate.absoluteX
                || qRound64(absolute.y()) != coordinate.absoluteY) {
            if (error) {
                *error = QString::fromLatin1(
                            "Coordinate conversion failed for cell %1,%2 local %3,%4")
                        .arg(coordinate.cellX).arg(coordinate.cellY)
                        .arg(coordinate.localX).arg(coordinate.localY);
            }
            return false;
        }
    }

    LuaWriter writer;
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    writer.d->mWarnings.clear();
    writer.d->writeSpawnPoints(&world, &buffer);
    const QByteArray output = buffer.data();
    if (output.contains("worldX") || output.contains("worldY")) {
        if (error)
            *error = QLatin1String("Generated spawnpoints.lua still contains worldX or worldY");
        return false;
    }
    for (const CoordinateCase &coordinate : cases) {
        const QByteArray expected = QString::fromLatin1(
                    "posX = %1, posY = %2, posZ = 0")
                .arg(coordinate.absoluteX).arg(coordinate.absoluteY)
                .toUtf8();
        if (!requireOutput(output, expected, error))
            return false;
    }

    World negativeWorld(1, 1, WorldGridFormat::Native256);
    definitions.configure(negativeWorld);
    GenerateLotsSettings settings =
            negativeWorld.getGenerateLotsSettings();
    settings.worldOrigin = QPoint(-2, -3);
    negativeWorld.setGenerateLotsSettings(settings);
    WorldCellObject *negative = definitions.addObject(
                negativeWorld, QLatin1String("SpawnPoint"),
                0, 0, -1, -1);
    definitions.addProperty(negative, QLatin1String("Professions"),
                            QLatin1String("unemployed"));
    if (negative->absoluteWorldPosition() != QPointF(-513, -769)) {
        if (error)
            *error = QLatin1String("Negative world-origin conversion failed");
        return false;
    }

    QBuffer negativeBuffer;
    negativeBuffer.open(QIODevice::WriteOnly);
    writer.d->mWarnings.clear();
    writer.d->writeSpawnPoints(&negativeWorld, &negativeBuffer);
    if (!requireOutput(negativeBuffer.data(),
                       "posX = -513, posY = -769, posZ = 0", error)) {
        return false;
    }

    if (summary) {
        *summary = QLatin1String(
                    "Native-256 absolute positions, cell boundaries, cross-cell locals, negative locals and negative world origins passed");
    }
    return true;
}

bool LuaWriter::validateZoneExport(QString *summary, QString *error)
{
    World world(1, 1, WorldGridFormat::Native256);
    ExportValidationDefinitions definitions;
    definitions.configure(world);

    WorldCellObject *flow = definitions.addObject(
                world, QLatin1String("WaterFlow"), 0, 0, 1, 1, 8, 6);
    WorldObjectValidation::applyCreationDefaults(flow);
    QString reason;
    if (flow->size() != QSizeF(1, 1)
            || !WorldObjectValidation::validateExportObject(flow, &reason)) {
        if (error)
            *error = QLatin1String("WaterFlow defaults failed: ") + reason;
        return false;
    }

    WorldCellObject *invalidFlow = definitions.addObject(
                world, QLatin1String("WaterFlow"), 0, 0, 2, 2);
    invalidFlow->setName(QLatin1String("invalid-flow"));
    if (WorldObjectValidation::validateExportObject(invalidFlow, &reason)) {
        if (error)
            *error = QLatin1String("WaterFlow without properties was accepted");
        return false;
    }

    WorldCellObject *waterZone = definitions.addObject(
                world, QLatin1String("WaterZone"), 0, 0, 4, 4, 8, 11);
    WorldObjectValidation::applyCreationDefaults(waterZone);
    if (!WorldObjectValidation::validateExportObject(waterZone, &reason)
            || waterZone->size() != QSizeF(8, 11)) {
        if (error)
            *error = QLatin1String("WaterZone defaults or area preservation failed: ") + reason;
        return false;
    }

    WorldCellObject *roomTone = definitions.addObject(
                world, QLatin1String("RoomTone"), 0, 0, 8, 8, 3, 2);
    WorldObjectValidation::applyCreationDefaults(roomTone);
    if (!WorldObjectValidation::validateExportObject(roomTone, &reason)
            || roomTone->size() != QSizeF(1, 1)) {
        if (error)
            *error = QLatin1String("RoomTone defaults failed: ") + reason;
        return false;
    }

    WorldCellObject *spawnPoint = definitions.addObject(
                world, QLatin1String("SpawnPoint"), 0, 0, 10, 10);
    WorldObjectValidation::applyCreationDefaults(spawnPoint);
    if (!WorldObjectValidation::validateSpawnPoint(spawnPoint, &reason)
            || WorldObjectValidation::resolvedValue(
                spawnPoint, QLatin1String("Professions"))
            != QLatin1String("unemployed")) {
        if (error)
            *error = QLatin1String("SpawnPoint defaults failed: ") + reason;
        return false;
    }

    WorldCellObject *invalidSpawn = definitions.addObject(
                world, QLatin1String("SpawnPoint"), 0, 0, 11, 11);
    invalidSpawn->setName(QLatin1String("invalid-spawn"));
    definitions.addProperty(invalidSpawn, QLatin1String("Professions"),
                            QLatin1String("all"));
    if (WorldObjectValidation::validateSpawnPoint(invalidSpawn, &reason)) {
        if (error)
            *error = QLatin1String("SpawnPoint profession 'all' was accepted");
        return false;
    }

    WorldCellObject *spawnOrigin = definitions.addObject(
                world, QLatin1String("SpawnOrigin"), 0, 0,
                12, 13, 20, 30);
    spawnOrigin->setName(QLatin1String("population-origin"));
    if (!WorldObjectValidation::validateExportObject(spawnOrigin, &reason)) {
        if (error)
            *error = QLatin1String("SpawnOrigin rectangle failed: ") + reason;
        return false;
    }

    WorldCellObject *invalidOrigin = definitions.addObject(
                world, QLatin1String("SpawnOrigin"), 0, 0, 14, 15);
    invalidOrigin->setName(QLatin1String("invalid-origin"));
    invalidOrigin->setGeometryType(ObjectGeometryType::Point);
    if (WorldObjectValidation::validateExportObject(invalidOrigin, &reason)) {
        if (error)
            *error = QLatin1String("Non-rectangular SpawnOrigin was accepted");
        return false;
    }

    LuaWriter writer;
    QBuffer objectBuffer;
    objectBuffer.open(QIODevice::WriteOnly);
    writer.d->mWarnings.clear();
    writer.d->writeWorldObjects(&world, &objectBuffer);
    if (writer.d->mWarnings.size() != 3
            || objectBuffer.data().contains("invalid-flow")
            || objectBuffer.data().contains("invalid-spawn")
            || objectBuffer.data().contains("invalid-origin")
            || !objectBuffer.data().contains("type = \"SpawnOrigin\"")
            || !objectBuffer.data().contains("width = 20, height = 30")) {
        if (error)
            *error = QString::fromLatin1(
                        "Invalid-zone filtering failed with %1 warning(s)")
                    .arg(writer.d->mWarnings.size());
        return false;
    }

    if (summary) {
        *summary = QLatin1String(
                    "creation defaults passed, WaterZone and SpawnOrigin areas were preserved, and invalid WaterFlow, SpawnPoint, and SpawnOrigin records were skipped with warnings");
    }
    return true;
}
