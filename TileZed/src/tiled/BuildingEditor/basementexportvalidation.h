#ifndef BASEMENTEXPORTVALIDATION_H
#define BASEMENTEXPORTVALIDATION_H

#include "building.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingtemplates.h"
#include "buildingtiles.h"
#include "buildingwriter.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "propertiesgrid.h"
#include "tilelayer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <functional>
#include <memory>

namespace BuildingEditor {

using BasementExportValidationCallback = std::function<bool(const QString &, const QString &,
        const QSet<QString> &, const QSet<QString> &, QString *, QString *)>;

inline bool validateBasementExportBinary(const QByteArray &bytes, QString *error)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QDataStream stream(&buffer);
    stream.setByteOrder(QDataStream::LittleEndian);
    const auto fail = [&](const QString &message) {
        *error = message;
        return false;
    };
    if (buffer.read(4) != QByteArrayLiteral("PZBY"))
        return fail(QStringLiteral("PZBY signature changed"));
    qint32 version = -1;
    qint32 tileCount = 0;
    stream >> version >> tileCount;
    if (version != 0 || tileCount < 1 || tileCount > 64)
        return fail(QStringLiteral("PZBY version or tile count changed"));
    for (int i = 0; i < tileCount; ++i) {
        if (!buffer.readLine().endsWith('\n'))
            return fail(QStringLiteral("PZBY tile name is truncated"));
    }
    qint32 chunksX = 0;
    qint32 chunksY = 0;
    qint32 levels = 0;
    qint32 rooms = 0;
    stream >> chunksX >> chunksY >> levels >> rooms;
    if (chunksX != 1 || chunksY != 1 || levels != 2 || rooms < 1 || rooms > 32)
        return fail(QStringLiteral("PZBY cropped dimensions, levels or rooms changed"));
    bool foundRoom = false;
    for (int i = 0; i < rooms; ++i) {
        const QByteArray name = buffer.readLine().trimmed();
        qint32 level = 0;
        qint32 rectangles = 0;
        stream >> level >> rectangles;
        if (rectangles < 0 || rectangles > 256)
            return fail(QStringLiteral("PZBY room rectangle count is invalid"));
        for (int j = 0; j < rectangles; ++j) {
            qint32 x = 0;
            qint32 y = 0;
            qint32 width = 0;
            qint32 height = 0;
            stream >> x >> y >> width >> height;
            if (name == QByteArrayLiteral("basementexport") && level == 0
                    && x == 1 && y == 1 && width == 2 && height == 2)
                foundRoom = true;
        }
        qint32 objects = 0;
        stream >> objects;
        if (objects < 0 || objects > 256)
            return fail(QStringLiteral("PZBY room object count is invalid"));
        for (int j = 0; j < objects; ++j) {
            qint32 tile = 0;
            qint32 x = 0;
            qint32 y = 0;
            stream >> tile >> x >> y;
        }
    }
    if (!foundRoom)
        return fail(QStringLiteral("PZBY cropped room definition changed"));
    qint32 buildings = 0;
    stream >> buildings;
    if (buildings < 0 || buildings > 32)
        return fail(QStringLiteral("PZBY building count is invalid"));
    for (int i = 0; i < buildings; ++i) {
        qint32 count = 0;
        stream >> count;
        if (count < 0 || count > rooms)
            return fail(QStringLiteral("PZBY building room count is invalid"));
        for (int j = 0; j < count; ++j) {
            qint32 roomId = 0;
            stream >> roomId;
        }
    }
    qint64 chunkPosition = -1;
    stream >> chunkPosition;
    if (stream.status() != QDataStream::Ok || chunkPosition < buffer.pos()
            || chunkPosition >= bytes.size() || !buffer.seek(chunkPosition))
        return fail(QStringLiteral("PZBY chunk table is invalid"));
    QVector<quint32> attributes(128, 0);
    int index = 0;
    while (index < attributes.size()) {
        qint32 count = 0;
        stream >> count;
        if (count == -1) {
            qint32 skipped = 0;
            stream >> skipped;
            if (skipped < 1 || skipped > attributes.size() - index)
                return fail(QStringLiteral("PZBY attribute run is invalid"));
            index += skipped;
        } else if (count == 2) {
            stream >> attributes[index++];
        } else {
            return fail(QStringLiteral("PZBY attribute value is invalid"));
        }
        if (stream.status() != QDataStream::Ok)
            return fail(QStringLiteral("PZBY attributes are truncated"));
    }
    for (int i = 0; i < attributes.size(); ++i) {
        const quint32 expected = i == 9 ? 1 : (i == 73 ? 2 : 0);
        if (attributes.at(i) != expected)
            return fail(QStringLiteral("PZBY cropped square attributes changed at %1").arg(i));
    }
    return true;
}

inline bool validateBasementEmptyCrop(QString *error)
{
    Tiled::Map map(Tiled::Map::LevelIsometric, 14, 14, 64, 32);
    map.addLayer(new Tiled::TileLayer(QStringLiteral("0_Floor"), 0, 0, 14, 14));
    std::unique_ptr<MapInfo> info(MapManager::instance()->newFromMap(&map));
    MapComposite composite(info.get());
    QPoint offset(17, 23);
    std::unique_ptr<Tiled::Map> croppedMap;
    std::unique_ptr<MapInfo> croppedInfo;
    std::unique_ptr<MapComposite> cropped(composite.cropToMinimum(offset));
    if (cropped) {
        croppedMap.reset(cropped->map());
        croppedInfo.reset(cropped->mapInfo());
    }
    qInfo() << "Basement empty crop: cloned=" << bool(cropped)
            << "size=" << (cropped ? cropped->map()->size() : map.size())
            << "offset=" << offset;
    if (cropped || offset != QPoint() || map.size() != QSize(14, 14)) {
        *error = QStringLiteral("Empty basement crop must preserve dimensions and clear the offset");
        return false;
    }
    return true;
}

inline bool validateBasementExport(const BasementExportValidationCallback &exportFile,
                                  QString *error, bool validateErrors = false)
{
    QTemporaryDir directory;
    if (!directory.isValid()) {
        *error = directory.errorString();
        return false;
    }
    BuildingTilesMgr *tiles = BuildingTilesMgr::instance();
    BTC_Stairs category(QStringLiteral("Stairs"));
    BuildingTileEntry entry(&category);
    const int tileIndices[] = {0, 1, 2, 8, 9, 10};
    for (int i = 0; i < BTC_Stairs::EnumCount; ++i)
        entry.setTile(i, tiles->get(BuildingTilesMgr::nameForTile(QStringLiteral("fixtures_stairs_01"), tileIndices[i])));
    const QSet<QString> north = { entry.tile(BTC_Stairs::North3)->name() };
    const QSet<QString> west = { entry.tile(BTC_Stairs::West3)->name() };
    const QString floorTile = BuildingTilesMgr::nameForTile(QStringLiteral("blends_natural_01"), 0);
    const QStringList names = {
        QStringLiteral("basement_north"), QStringLiteral("basement_west"),
        QStringLiteral("ba_north"), QStringLiteral("ba_west"),
        QStringLiteral("explicit_north"), QStringLiteral("explicit_west")
    };
    QString firstInput;
    int checks = 0;
    for (int fixture = 0; fixture < names.size(); ++fixture) {
        const bool northDirection = fixture % 2 == 0;
        const bool access = fixture == 2 || fixture == 3;
        const bool explicitAccess = fixture >= 4;
        const BuildingObject::Direction direction = northDirection ? BuildingObject::N : BuildingObject::W;
        Building building(14, 14);
        for (int i = 0; i < Building::TileCount; ++i)
            building.setTile(i, tiles->noneTileEntry());
        building.setStairsTile(&entry);
        auto *room = new Room;
        room->Name = QStringLiteral("Basement export");
        room->internalName = QStringLiteral("basementexport");
        room->Color = qRgb(80, 120, 160);
        for (int i = 0; i < Room::TileCount; ++i)
            room->setTile(i, tiles->noneTileEntry());
        building.insertRoom(0, room);
        for (int level = 0; level < 2; ++level) {
            auto *floor = new BuildingFloor(&building, level);
            building.insertFloor(level, floor);
            floor->setGrime(QStringLiteral("Floor"), QRegion(QRect(2, 2, 8, 8)), floorTile);
            Tiled::Properties properties;
            properties.insert(level == 0 ? QStringLiteral("KeepFloors") : QStringLiteral("KeepWalls"), QStringLiteral("true"));
            floor->squarePropertiesGrid()->replace(3, 3, properties);
            if (level == 0) {
                for (int y = 3; y < 5; ++y)
                    for (int x = 3; x < 5; ++x)
                        floor->SetRoomAt(x, y, room);
            }
            const BuildingObject::Direction other = northDirection ? BuildingObject::W : BuildingObject::N;
            auto *stairs = new Stairs(floor, 4, 4, level == (access ? 0 : 1) ? direction : other);
            stairs->setTile(&entry);
            floor->insertObject(0, stairs);
        }
        if (explicitAccess) {
            BasementAccess basementAccess;
            basementAccess.mX = 7;
            basementAccess.mY = 8;
            basementAccess.mDirection = direction;
            building.setBasementAccess(basementAccess);
        }
        const QString input = directory.filePath(names.at(fixture) + QStringLiteral(".tbx"));
        if (firstInput.isEmpty())
            firstInput = input;
        BuildingWriter writer;
        if (!writer.write(&building, input)) {
            *error = writer.errorString();
            return false;
        }
        QByteArray firstBinary;
        QString firstLua;
        for (int pass = 0; pass < 2; ++pass) {
            const QString outputDirectory = directory.filePath(QStringLiteral("output_%1_%2").arg(fixture).arg(pass));
            if (!QDir().mkpath(outputDirectory)) {
                *error = QStringLiteral("Could not create basement export output directory");
                return false;
            }
            QString lua;
            QString failure;
            if (!exportFile(input, outputDirectory, north, west, &lua, &failure)) {
                *error = QStringLiteral("%1: %2").arg(names.at(fixture), failure);
                return false;
            }
            QFile output(QDir(outputDirectory).filePath(names.at(fixture) + QStringLiteral(".pzby")));
            if (!output.open(QIODevice::ReadOnly)) {
                *error = output.errorString();
                return false;
            }
            const QByteArray bytes = output.readAll();
            if (!validateBasementExportBinary(bytes, error)) {
                *error = names.at(fixture) + QStringLiteral(": ") + *error;
                return false;
            }
            const int stairX = explicitAccess ? 5 : (northDirection ? 2 : 3);
            const int stairY = explicitAccess ? 6 : (northDirection ? 3 : 2);
            const QString expected = QStringLiteral("%1 = { width=8, height=8, stairx=%2, stairy=%3, stairDir=\"%4\" },\n")
                    .arg(names.at(fixture)).arg(stairX).arg(stairY)
                    .arg(northDirection ? QStringLiteral("N") : QStringLiteral("W"));
            if (lua != expected) {
                *error = QStringLiteral("%1: unexpected Lua output %2").arg(names.at(fixture), lua);
                return false;
            }
            if (pass == 0) {
                firstBinary = bytes;
                firstLua = lua;
            } else if (bytes != firstBinary || lua != firstLua) {
                *error = QStringLiteral("%1: repeated exports differ").arg(names.at(fixture));
                return false;
            }
            checks += 3;
        }
        qInfo().noquote() << QStringLiteral("Basement export %1 PZBY=%2 Lua=%3")
                            .arg(names.at(fixture),
                                 QString::fromLatin1(QCryptographicHash::hash(firstBinary, QCryptographicHash::Sha256).toHex()),
                                 QString::fromLatin1(QCryptographicHash::hash(firstLua.toUtf8(), QCryptographicHash::Sha256).toHex()));
    }
    if (validateErrors) {
        QString lua;
        QString failure;
        if (exportFile(directory.filePath(QStringLiteral("absent.tbx")), directory.path(), north, west, &lua, &failure)
                || failure.isEmpty() || !lua.isEmpty()) {
            *error = QStringLiteral("Missing basement input was not reported");
            return false;
        }
        ++checks;
        const QString blockedOutput = directory.filePath(QStringLiteral("output-file"));
        QFile blocker(blockedOutput);
        if (!blocker.open(QIODevice::WriteOnly)) {
            *error = blocker.errorString();
            return false;
        }
        blocker.close();
        lua.clear();
        failure.clear();
        if (exportFile(firstInput, blockedOutput, north, west, &lua, &failure)
                || failure.isEmpty() || !lua.isEmpty()) {
            *error = QStringLiteral("Unwritable basement output was not reported");
            return false;
        }
        ++checks;
        if (!validateBasementEmptyCrop(error))
            return false;
        ++checks;
    }
    qInfo() << "Basement export validation checks:" << checks;
    return true;
}

}

#endif
