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
#include "worldreader.h"

#include "BuildingEditor/roofhiding.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QScopedPointer>
#include <QSaveFile>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <memory>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

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
        QByteArray key = _key;
        bool unquoted = key.length() && !isdigit(key[0]);
        for (int i = 0; i < key.length(); i++) {
            if (key[i] == '_' || isalnum(key[i])) continue;
            unquoted = false;
            break;
        }
        if (!unquoted)
            key = QString::fromUtf8("[\"%1\"]").arg(QString::fromUtf8(_key)).toUtf8();
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
                        const QStringList professions =
                                WorldObjectValidation::spawnProfessions(obj);
                        for (const QString &profession : professions)
                            spawnByProfession[profession] += obj;
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
                    if (obj->type() && WorldObjectValidation::isFishingZone(obj->type()->name()))
                        continue;
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

QString LuaWriter::fishingZonesPath(const QString &modDirectory,
                                   const QString &mapName)
{
    return QDir(modDirectory).filePath(
                QStringLiteral("common/media/lua/shared/%1/WorldEdFishingZones.lua")
                .arg(mapName));
}

bool LuaWriter::writeFishingZones(World *world, const QString &modDirectory,
                                  const QString &mapName, bool writeEmpty)
{
    d->mError.clear();
    d->mWarnings.clear();
    if (modDirectory.isEmpty() || mapName.isEmpty() || mapName == QLatin1String(".")
            || mapName == QLatin1String("..")
            || mapName.endsWith(QLatin1Char('.')) || mapName.endsWith(QLatin1Char(' '))
            || std::any_of(mapName.begin(), mapName.end(), [](QChar c) {
        return c.unicode() < 32 || QLatin1String("\\/:*?\"<>|").contains(c);
    })) {
        d->mError = QCoreApplication::translate("LuaWriter", "Choose a mod folder and a valid map folder name.");
        return false;
    }

    QString noFishZones;
    QString riverZones;
    for (int y = 0; y < world->height(); ++y) {
        for (int x = 0; x < world->width(); ++x) {
            for (WorldCellObject *object : world->cellAt(x, y)->objects()) {
                if (!object->type())
                    continue;
                const QString type = object->type()->name();
                if (!WorldObjectValidation::isFishingZone(type))
                    continue;
                QString reason;
                if (!WorldObjectValidation::validateExportObject(object, &reason)) {
                    d->mWarnings.append(QStringLiteral("%1: %2")
                                        .arg(WorldObjectValidation::describe(object), reason));
                    continue;
                }
                const QPointF position = object->absoluteWorldPosition();
                QString &zones = type == QLatin1String("NoFishZone") ? noFishZones : riverZones;
                zones += QStringLiteral("    {x1 = %1, y1 = %2, x2 = %3, y2 = %4, worldEdSquareBounds = true},\n")
                        .arg(qRound64(position.x())).arg(qRound64(position.y()))
                        .arg(qRound64(position.x() + object->width() - 1))
                        .arg(qRound64(position.y() + object->height() - 1));
            }
        }
    }

    const QString path = fishingZonesPath(modDirectory, mapName);
    const bool hasZones = !noFishZones.isEmpty() || !riverZones.isEmpty();
    const bool exists = QFileInfo::exists(path);
    if (!writeEmpty && !hasZones && !exists)
        return true;
    const QByteArray header = QByteArrayLiteral("-- Generated by PZWorldEd\n");
    if (exists) {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly) || existing.readLine() != header) {
            d->mError = QCoreApplication::translate("LuaWriter", "The destination already contains a different Lua script.");
            return false;
        }
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        d->mError = QCoreApplication::translate("LuaWriter", "Could not create the fishing-zone Lua directory.");
        return false;
    }

    QByteArray script = header;
    if (hasZones) {
        script += QStringLiteral(R"lua(require "Fishing/FishingZones"

local function appendZones(target, zones)
    for _, zone in ipairs(zones) do
        table.insert(target, zone)
    end
end

appendZones(Fishing.NoFishZones, {
%1})
appendZones(Fishing.RiverZones, {
%2})

if %3 then
    Fishing.NoFishZones[0] = Fishing.NoFishZones[0] or Fishing.NoFishZones[1]
end

if not Fishing.worldEdSquareBounds then
    local function includeWholeSquares(check, listName)
        return function(x, y)
            if check(x, y) then
                return true
            end
            local squareX, squareY = math.floor(x), math.floor(y)
            if squareX ~= x or squareY ~= y then
                for _, zone in ipairs(Fishing[listName]) do
                    if zone.worldEdSquareBounds
                            and squareX >= zone.x1 and squareX <= zone.x2
                            and squareY >= zone.y1 and squareY <= zone.y2 then
                        return true
                    end
                end
            end
            return false
        end
    end
    Fishing.isNoFishZone = includeWholeSquares(Fishing.isNoFishZone, "NoFishZones")
    Fishing.isRiver = includeWholeSquares(Fishing.isRiver, "RiverZones")
    Fishing.worldEdSquareBounds = true
end
)lua").arg(noFishZones, riverZones, noFishZones.isEmpty()
                ? QStringLiteral("false") : QStringLiteral("true")).toUtf8();
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(script) != script.size()
            || !file.commit()) {
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
