/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "lotsourcefiles.h"
#include "chunkdataoverride.h"
#include "navigation/navigationflags.h"
#include "navigation/chunkdatafile256.h"
#include "navigation/isogridsquare256.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "map.h"
#include "tilelayer.h"
#include "tileset.h"
#include "world.h"
#include "tiledeffile.h"
#include "tiledefinitionpaths.h"

#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QScopeGuard>
#include <climits>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QTextCodec>
#include <QTemporaryDir>
#include <QVector>
#include <memory>
#include <vector>

#if defined(Q_OS_WIN) && (_MSC_VER >= 1600)
template class __declspec(dllimport) QMap<QString, QString>;
#endif

namespace {

constexpr int ChunkCount = 1024;
constexpr int ChunkSide = 8;
constexpr int CellSide = 32;

struct Room
{
    QString name;
    int level = 0;
    QVector<QVector<int>> rects;
    QVector<QVector<int>> objects;
};

struct Header
{
    int version = 1;
    int minLevel = 0;
    int maxLevel = 0;
    QVector<QString> tiles;
    QVector<Room> rooms;
    QVector<QVector<int>> buildings;
    QVector<quint8> density;
};

class Lines
{
public:
    explicit Lines(const QString &text, int *lineNumber = nullptr)
        : mLines(text.split(QLatin1Char('\n'))), mLineNumber(lineNumber)
    {
    }

    QString next(bool trim = true)
    {
        while (mIndex < mLines.size()) {
            QString line = mLines.at(mIndex++);
            if (mLineNumber)
                *mLineNumber = mIndex;
            if (line.endsWith(QLatin1Char('\r')))
                line.chop(1);
            if (!line.trimmed().isEmpty())
                return trim ? line.trimmed() : line;
        }
        return QString();
    }

    bool expect(const QString &expected)
    {
        return next() == expected;
    }

    bool number(const QString &key, int *result)
    {
        const QString line = next();
        const int equal = line.indexOf(QLatin1Char('='));
        if (equal < 0 || line.left(equal).trimmed() != key)
            return false;
        bool ok = false;
        const int value = line.mid(equal + 1).trimmed().toInt(&ok);
        if (ok)
            *result = value;
        return ok;
    }

    bool value(const QString &key, QString *result, bool preserveWhitespace = false)
    {
        const QString line = next(!preserveWhitespace);
        const int equal = line.indexOf(QLatin1Char('='));
        if (equal < 0 || line.left(equal).trimmed() != key)
            return false;
        *result = line.mid(equal + 1);
        if (preserveWhitespace) {
            if (result->startsWith(QLatin1Char(' ')))
                result->remove(0, 1);
        } else {
            *result = result->trimmed();
        }
        return true;
    }

private:
    QStringList mLines;
    int mIndex = 0;
    int *mLineNumber = nullptr;
};

bool readFile(const QString &path, QByteArray *data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString() + QStringLiteral(": ") + path;
        return false;
    }
    *data = file.readAll();
    if (file.error() != QFile::NoError) {
        *error = file.errorString() + QStringLiteral(": ") + path;
        return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
            || !file.commit()) {
        *error = file.errorString() + QStringLiteral(": ") + path;
        return false;
    }
    return true;
}

bool writeFiles(const QDir &directory, const QStringList &names,
                const QVector<QByteArray> &data, QString *error)
{
    std::vector<std::unique_ptr<QSaveFile>> pending;
    for (int index = 0; index < names.size(); ++index) {
        const QString path = directory.filePath(names.at(index));
        QByteArray previous;
        QString readError;
        if (readFile(path, &previous, &readError) && previous == data.at(index))
            continue;
        std::unique_ptr<QSaveFile> file(new QSaveFile(path));
        if (!file->open(QIODevice::WriteOnly)
                || file->write(data.at(index)) != data.at(index).size()) {
            *error = file->errorString() + QStringLiteral(": ") + path;
            return false;
        }
        pending.push_back(std::move(file));
    }
    for (const auto &file : pending) {
        if (!file->commit()) {
            *error = file->errorString() + QStringLiteral(": ") + file->fileName();
            return false;
        }
    }
    return true;
}

bool readName(QDataStream &stream, QString *name)
{
    QByteArray bytes;
    char character = 0;
    while (bytes.size() < 4096 && stream.readRawData(&character, 1) == 1) {
        if (character == '\n') {
            *name = QString::fromLatin1(bytes);
            return true;
        }
        bytes.append(character);
    }
    return false;
}

bool writeName(QDataStream &stream, const QString &name)
{
    if (name.contains(QLatin1Char('\n'))
            || QString::fromLatin1(name.toLatin1()) != name)
        return false;
    const QByteArray bytes = name.toLatin1() + '\n';
    return stream.writeRawData(bytes.constData(), bytes.size())
            == bytes.size();
}

bool validCount(int count, int maximum)
{
    return count >= 0 && count <= maximum;
}

bool readHeader(const QByteArray &bytes, Header *header)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&buffer);
    in.setByteOrder(QDataStream::LittleEndian);
    char magic[4];
    if (in.readRawData(magic, 4) != 4
            || QByteArray(magic, 4) != "LOTH")
        return false;
    qint32 version = 0;
    qint32 tileCount = 0;
    in >> version >> tileCount;
    if (version != 1 || !validCount(tileCount, 100000))
        return false;
    header->version = version;
    for (int index = 0; index < tileCount; ++index) {
        QString name;
        if (!readName(in, &name))
            return false;
        header->tiles.append(name);
    }
    qint32 width = 0;
    qint32 height = 0;
    qint32 minLevel = 0;
    qint32 maxLevel = 0;
    qint32 roomCount = 0;
    in >> width >> height >> minLevel >> maxLevel >> roomCount;
    if (width != ChunkSide || height != ChunkSide
            || minLevel > maxLevel || qint64(maxLevel) - minLevel > 31
            || !validCount(roomCount, 100000))
        return false;
    header->minLevel = minLevel;
    header->maxLevel = maxLevel;
    for (int index = 0; index < roomCount; ++index) {
        Room room;
        if (!readName(in, &room.name))
            return false;
        qint32 level = 0;
        qint32 rectCount = 0;
        in >> level >> rectCount;
        if (!validCount(rectCount, 100000))
            return false;
        room.level = level;
        for (int rect = 0; rect < rectCount; ++rect) {
            QVector<int> values(4);
            for (int &value : values) {
                qint32 number = 0;
                in >> number;
                value = number;
            }
            room.rects.append(values);
        }
        qint32 objectCount = 0;
        in >> objectCount;
        if (!validCount(objectCount, 1000000))
            return false;
        for (int object = 0; object < objectCount; ++object) {
            QVector<int> values(3);
            for (int &value : values) {
                qint32 number = 0;
                in >> number;
                value = number;
            }
            room.objects.append(values);
        }
        header->rooms.append(room);
    }
    qint32 buildingCount = 0;
    in >> buildingCount;
    if (!validCount(buildingCount, 100000))
        return false;
    for (int index = 0; index < buildingCount; ++index) {
        qint32 roomCountInBuilding = 0;
        in >> roomCountInBuilding;
        if (!validCount(roomCountInBuilding, 100000))
            return false;
        QVector<int> roomIds;
        for (int room = 0; room < roomCountInBuilding; ++room) {
            qint32 roomId = 0;
            in >> roomId;
            roomIds.append(roomId);
        }
        header->buildings.append(roomIds);
    }
    for (int index = 0; index < ChunkCount; ++index) {
        quint8 value = 0;
        in >> value;
        header->density.append(value);
    }
    return in.status() == QDataStream::Ok && buffer.atEnd();
}

QByteArray writeHeader(const Header &header)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("LOTH", 4);
    out << qint32(header.version) << qint32(header.tiles.size());
    for (const QString &name : header.tiles) {
        if (!writeName(out, name))
            return QByteArray();
    }
    out << qint32(ChunkSide) << qint32(ChunkSide)
        << qint32(header.minLevel) << qint32(header.maxLevel)
        << qint32(header.rooms.size());
    for (const Room &room : header.rooms) {
        if (!writeName(out, room.name))
            return QByteArray();
        out << qint32(room.level) << qint32(room.rects.size());
        for (const QVector<int> &rect : room.rects) {
            for (int value : rect)
                out << qint32(value);
        }
        out << qint32(room.objects.size());
        for (const QVector<int> &object : room.objects) {
            for (int value : object)
                out << qint32(value);
        }
    }
    out << qint32(header.buildings.size());
    for (const QVector<int> &building : header.buildings) {
        out << qint32(building.size());
        for (int roomId : building)
            out << qint32(roomId);
    }
    for (quint8 value : header.density)
        out << value;
    return out.status() == QDataStream::Ok ? bytes : QByteArray();
}

QString headerText(const Header &header)
{
    QString text;
    QTextStream out(&text);
    out << "lotheader\nversion = " << header.version
        << "\nchunkWidth = " << ChunkSide
        << "\nchunkHeight = " << ChunkSide
        << "\nminLevel = " << header.minLevel
        << "\nmaxLevel = " << header.maxLevel
        << "\ntiles = " << header.tiles.size() << "\n{\n";
    for (const QString &name : header.tiles)
        out << "    " << name << '\n';
    out << "}\nrooms = " << header.rooms.size() << "\n{\n";
    for (int index = 0; index < header.rooms.size(); ++index) {
        const Room &room = header.rooms.at(index);
        out << "    room " << index << "\n    {\n"
            << "        name = " << room.name
            << "\n        level = " << room.level
            << "\n        rects = " << room.rects.size()
            << "\n        {\n";
        for (const QVector<int> &rect : room.rects)
            out << "            " << rect.at(0) << ',' << rect.at(1)
                << ',' << rect.at(2) << ',' << rect.at(3) << '\n';
        out << "        }\n        objects = " << room.objects.size() << '\n';
        if (!room.objects.isEmpty()) {
            out << "        {\n";
            for (const QVector<int> &object : room.objects)
                out << "            " << object.at(0) << ',' << object.at(1)
                    << ',' << object.at(2) << '\n';
            out << "        }\n";
        }
        out << "    }\n";
    }
    out << "}\nbuildings = " << header.buildings.size() << "\n{\n";
    for (int index = 0; index < header.buildings.size(); ++index) {
        const QVector<int> &building = header.buildings.at(index);
        out << "    building " << index << "\n    {\n"
            << "        rooms = " << building.size() << "\n        {\n";
        for (int roomId : building)
            out << "            " << roomId << '\n';
        out << "        }\n    }\n";
    }
    out << "}\nzombieDensity = " << ChunkCount << "\n{\n";
    for (int x = 0; x < CellSide; ++x) {
        out << "   ";
        for (int y = 0; y < CellSide; ++y)
            out << QStringLiteral("%1").arg(
                       header.density.at(x * CellSide + y), -4);
        out << '\n';
    }
    out << "}\n";
    return text;
}

bool parseValues(const QString &line, int count, QVector<int> *values)
{
    const QStringList parts = line.split(QLatin1Char(','));
    if (parts.size() != count)
        return false;
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok)
            return false;
        values->append(value);
    }
    return true;
}

bool parseHeader(const QString &text, Header *header, int *lineNumber = nullptr)
{
    Lines lines(text, lineNumber);
    int width = 0;
    int height = 0;
    int count = 0;
    if (!lines.expect(QStringLiteral("lotheader"))
            || !lines.number(QStringLiteral("version"), &header->version)
            || header->version != 1
            || !lines.number(QStringLiteral("chunkWidth"), &width)
            || !lines.number(QStringLiteral("chunkHeight"), &height)
            || width != ChunkSide || height != ChunkSide
            || !lines.number(QStringLiteral("minLevel"), &header->minLevel)
            || !lines.number(QStringLiteral("maxLevel"), &header->maxLevel)
            || header->minLevel > header->maxLevel
            || qint64(header->maxLevel) - header->minLevel > 31
            || !lines.number(QStringLiteral("tiles"), &count)
            || !validCount(count, 100000) || !lines.expect(QStringLiteral("{")))
        return false;
    for (int index = 0; index < count; ++index) {
        const QString name = lines.next();
        if (name.isEmpty() || name == QLatin1String("}"))
            return false;
        header->tiles.append(name);
    }
    if (!lines.expect(QStringLiteral("}"))
            || !lines.number(QStringLiteral("rooms"), &count)
            || !validCount(count, 100000) || !lines.expect(QStringLiteral("{")))
        return false;
    for (int index = 0; index < count; ++index) {
        Room room;
        int rectCount = 0;
        int objectCount = 0;
        if (!lines.expect(QStringLiteral("room %1").arg(index))
                || !lines.expect(QStringLiteral("{"))
                || !lines.value(QStringLiteral("name"), &room.name, true)
                || !lines.number(QStringLiteral("level"), &room.level)
                || !lines.number(QStringLiteral("rects"), &rectCount)
                || !validCount(rectCount, 100000)
                || !lines.expect(QStringLiteral("{")))
            return false;
        for (int rect = 0; rect < rectCount; ++rect) {
            QVector<int> values;
            if (!parseValues(lines.next(), 4, &values))
                return false;
            room.rects.append(values);
        }
        if (!lines.expect(QStringLiteral("}"))
                || !lines.number(QStringLiteral("objects"), &objectCount)
                || !validCount(objectCount, 1000000))
            return false;
        if (objectCount > 0 && !lines.expect(QStringLiteral("{")))
            return false;
        for (int object = 0; object < objectCount; ++object) {
            QVector<int> values;
            if (!parseValues(lines.next(), 3, &values))
                return false;
            room.objects.append(values);
        }
        if (objectCount > 0 && !lines.expect(QStringLiteral("}")))
            return false;
        if (!lines.expect(QStringLiteral("}")))
            return false;
        header->rooms.append(room);
    }
    if (!lines.expect(QStringLiteral("}"))
            || !lines.number(QStringLiteral("buildings"), &count)
            || !validCount(count, 100000) || !lines.expect(QStringLiteral("{")))
        return false;
    for (int index = 0; index < count; ++index) {
        int roomCount = 0;
        QVector<int> building;
        if (!lines.expect(QStringLiteral("building %1").arg(index))
                || !lines.expect(QStringLiteral("{"))
                || !lines.number(QStringLiteral("rooms"), &roomCount)
                || !validCount(roomCount, 100000)
                || !lines.expect(QStringLiteral("{")))
            return false;
        for (int room = 0; room < roomCount; ++room) {
            bool ok = false;
            const int roomId = lines.next().toInt(&ok);
            if (!ok || roomId < 0 || roomId >= header->rooms.size())
                return false;
            building.append(roomId);
        }
        if (!lines.expect(QStringLiteral("}"))
                || !lines.expect(QStringLiteral("}")))
            return false;
        header->buildings.append(building);
    }
    if (!lines.expect(QStringLiteral("}"))
            || !lines.number(QStringLiteral("zombieDensity"), &count)
            || count != ChunkCount || !lines.expect(QStringLiteral("{")))
        return false;
    for (int x = 0; x < CellSide; ++x) {
        const QStringList values = lines.next().split(
                    QRegularExpression(QStringLiteral("\\s+")),
                    Qt::SkipEmptyParts);
        if (values.size() != CellSide)
            return false;
        for (const QString &value : values) {
            bool ok = false;
            const int number = value.toInt(&ok);
            if (!ok || number < 0 || number > 255)
                return false;
            header->density.append(quint8(number));
        }
    }
    return lines.expect(QStringLiteral("}")) && lines.next().isEmpty();
}

struct Square
{
    int roomId = -1;
    QVector<int> tileIds;
};

using Chunk = QMap<int, Square>;

bool readLotpack(const QByteArray &bytes, const Header &header,
                 QVector<Chunk> *chunks)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&buffer);
    in.setByteOrder(QDataStream::LittleEndian);
    char magic[4];
    qint32 version = 0;
    qint32 chunkCount = 0;
    if (in.readRawData(magic, 4) != 4
            || QByteArray(magic, 4) != "LOTP")
        return false;
    in >> version >> chunkCount;
    if (version != header.version || chunkCount != ChunkCount)
        return false;
    QVector<qint64> offsets;
    for (int index = 0; index < ChunkCount; ++index) {
        qint64 offset = 0;
        in >> offset;
        if (offset < 12 + ChunkCount * 8 || offset >= bytes.size())
            return false;
        offsets.append(offset);
    }
    const int squareCount = ChunkSide * ChunkSide
            * (header.maxLevel - header.minLevel + 1);
    for (qint64 offset : offsets) {
        if (!buffer.seek(offset))
            return false;
        Chunk chunk;
        int position = 0;
        while (position < squareCount) {
            qint32 count = 0;
            in >> count;
            if (in.status() != QDataStream::Ok)
                return false;
            if (count == -1) {
                qint32 skip = 0;
                in >> skip;
                if (skip <= 0 || skip > squareCount - position)
                    return false;
                position += skip;
                continue;
            }
            if (count < 2 || count > 4096)
                return false;
            Square square;
            qint32 roomId = 0;
            in >> roomId;
            square.roomId = roomId;
            for (int tile = 1; tile < count; ++tile) {
                qint32 tileId = 0;
                in >> tileId;
                if (tileId < 0 || tileId >= header.tiles.size())
                    return false;
                square.tileIds.append(tileId);
            }
            if (in.status() != QDataStream::Ok)
                return false;
            chunk.insert(position++, square);
        }
        chunks->append(chunk);
    }
    return chunks->size() == ChunkCount;
}

QString lotpackText(const Header &header, const QVector<Chunk> &chunks)
{
    QString text;
    QTextStream out(&text);
    out << "lotpack\nversion = " << header.version
        << "\nchunks = " << ChunkCount
        << "\nchunkWidth = " << ChunkSide
        << "\nchunkHeight = " << ChunkSide
        << "\nminLevel = " << header.minLevel
        << "\nmaxLevel = " << header.maxLevel << '\n';
    for (int index = 0; index < chunks.size(); ++index) {
        out << "chunk " << index << "\n{\n";
        const Chunk &chunk = chunks.at(index);
        for (auto entry = chunk.cbegin(); entry != chunk.cend(); ++entry) {
            const int position = entry.key();
            const int y = position % ChunkSide;
            const int x = (position / ChunkSide) % ChunkSide;
            const int z = header.minLevel + position
                    / (ChunkSide * ChunkSide);
            out << "    " << z << ',' << x << ',' << y
                << " = " << entry.value().roomId << " : ";
            for (int tile = 0; tile < entry.value().tileIds.size(); ++tile) {
                if (tile)
                    out << ", ";
                out << header.tiles.at(entry.value().tileIds.at(tile));
            }
            out << '\n';
        }
        out << "}\n";
    }
    return text;
}

bool parseLotpack(const QString &text, const Header &header,
                  QVector<Chunk> *chunks, int *lineNumber = nullptr)
{
    Lines lines(text, lineNumber);
    int version = 0;
    int count = 0;
    int width = 0;
    int height = 0;
    int minLevel = 0;
    int maxLevel = 0;
    if (!lines.expect(QStringLiteral("lotpack"))
            || !lines.number(QStringLiteral("version"), &version)
            || version != header.version
            || !lines.number(QStringLiteral("chunks"), &count)
            || count != ChunkCount
            || !lines.number(QStringLiteral("chunkWidth"), &width)
            || !lines.number(QStringLiteral("chunkHeight"), &height)
            || width != ChunkSide || height != ChunkSide
            || !lines.number(QStringLiteral("minLevel"), &minLevel)
            || !lines.number(QStringLiteral("maxLevel"), &maxLevel)
            || minLevel != header.minLevel || maxLevel != header.maxLevel)
        return false;
    QHash<QString, int> tileIds;
    for (int index = 0; index < header.tiles.size(); ++index)
        tileIds.insert(header.tiles.at(index), index);
    const QRegularExpression squarePattern(QStringLiteral(
        "^(-?\\d+),(\\d+),(\\d+)\\s*=\\s*(-?\\d+)\\s*:\\s*(.+)$"));
    for (int index = 0; index < ChunkCount; ++index) {
        if (!lines.expect(QStringLiteral("chunk %1").arg(index))
                || !lines.expect(QStringLiteral("{")))
            return false;
        Chunk chunk;
        while (true) {
            const QString line = lines.next();
            if (line == QLatin1String("}"))
                break;
            const QRegularExpressionMatch match = squarePattern.match(line);
            if (!match.hasMatch())
                return false;
            bool validZ = false, validX = false, validY = false, validRoom = false;
            const int z = match.captured(1).toInt(&validZ);
            const int x = match.captured(2).toInt(&validX);
            const int y = match.captured(3).toInt(&validY);
            const int roomId = match.captured(4).toInt(&validRoom);
            if (!validZ || !validX || !validY || !validRoom
                    || z < minLevel || z > maxLevel
                    || x < 0 || x >= ChunkSide
                    || y < 0 || y >= ChunkSide)
                return false;
            const int position = (z - minLevel) * ChunkSide * ChunkSide
                    + x * ChunkSide + y;
            if (chunk.contains(position))
                return false;
            Square square;
            square.roomId = roomId;
            const QStringList names = match.captured(5).split(
                        QLatin1Char(','), Qt::SkipEmptyParts);
            if (names.isEmpty() || names.size() > 4095)
                return false;
            for (const QString &name : names) {
                const QString normalized = name.trimmed();
                if (!tileIds.contains(normalized))
                    return false;
                square.tileIds.append(tileIds.value(normalized));
            }
            chunk.insert(position, square);
        }
        chunks->append(chunk);
    }
    return lines.next().isEmpty();
}

QByteArray writeLotpack(const Header &header,
                        const QVector<Chunk> &chunks)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadWrite);
    QDataStream out(&buffer);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("LOTP", 4);
    out << qint32(header.version) << qint32(ChunkCount);
    for (int index = 0; index < ChunkCount; ++index)
        out << qint64(0);
    QVector<qint64> offsets;
    const int squareCount = ChunkSide * ChunkSide
            * (header.maxLevel - header.minLevel + 1);
    for (const Chunk &chunk : chunks) {
        offsets.append(buffer.pos());
        int position = 0;
        for (auto entry = chunk.cbegin(); entry != chunk.cend(); ++entry) {
            if (entry.key() > position)
                out << qint32(-1) << qint32(entry.key() - position);
            const Square &square = entry.value();
            out << qint32(square.tileIds.size() + 1)
                << qint32(square.roomId);
            for (int tileId : square.tileIds)
                out << qint32(tileId);
            position = entry.key() + 1;
        }
        if (position < squareCount)
            out << qint32(-1) << qint32(squareCount - position);
    }
    buffer.seek(12);
    for (qint64 offset : offsets)
        out << offset;
    return out.status() == QDataStream::Ok
            && offsets.size() == ChunkCount ? bytes : QByteArray();
}

struct ChunkData
{
    int type = 0;
    QVector<quint8> bits;
};

bool readChunkData(const QByteArray &bytes,
                   QVector<ChunkData> *chunks)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&buffer);
    qint16 version = 0;
    in >> version;
    if (version != 1)
        return false;
    for (int index = 0; index < ChunkCount; ++index) {
        quint8 type = 0;
        in >> type;
        if (type > 5)
            return false;
        ChunkData chunk;
        chunk.type = type;
        if (type == 2) {
            for (int square = 0; square < ChunkSide * ChunkSide;
                 ++square) {
                quint8 bits = 0;
                in >> bits;
                chunk.bits.append(bits);
            }
        }
        chunks->append(chunk);
    }
    return in.status() == QDataStream::Ok && buffer.atEnd();
}

QString chunkDataText(const QVector<ChunkData> &chunks)
{
    QString text;
    QTextStream out(&text);
    out << "chunkdata\nversion = 1\nchunks = " << ChunkCount << '\n';
    for (int y = 0; y < CellSide; ++y) {
        for (int x = 0; x < CellSide; ++x) {
            const int index = x + y * CellSide;
            const ChunkData &chunk = chunks.at(index);
            out << "chunk " << index << " = ";
            if (chunk.type != 2) {
                out << chunk.type << '\n';
                continue;
            }
            out << "grid\n{\n";
            for (int row = 0; row < ChunkSide; ++row) {
                out << "   ";
                for (int column = 0; column < ChunkSide; ++column)
                    out << QStringLiteral("%1").arg(
                               chunk.bits.at(row * ChunkSide + column), -3);
                out << '\n';
            }
            out << "}\n";
        }
    }
    return text;
}

bool parseChunkData(const QString &text,
                    QVector<ChunkData> *chunks, int *lineNumber = nullptr)
{
    Lines lines(text, lineNumber);
    int version = 0;
    int count = 0;
    if (!lines.expect(QStringLiteral("chunkdata"))
            || !lines.number(QStringLiteral("version"), &version)
            || version != 1
            || !lines.number(QStringLiteral("chunks"), &count)
            || count != ChunkCount)
        return false;
    for (int y = 0; y < CellSide; ++y) {
        for (int x = 0; x < CellSide; ++x) {
            const int index = x + y * CellSide;
            QString value;
            if (!lines.value(QStringLiteral("chunk %1").arg(index),
                             &value))
                return false;
            ChunkData chunk;
            if (value == QLatin1String("grid")) {
                chunk.type = 2;
                if (!lines.expect(QStringLiteral("{")))
                    return false;
                for (int row = 0; row < ChunkSide; ++row) {
                    const QStringList values = lines.next().split(
                                QRegularExpression(QStringLiteral("\\s+")),
                                Qt::SkipEmptyParts);
                    if (values.size() != ChunkSide)
                        return false;
                    for (const QString &part : values) {
                        bool ok = false;
                        const int bits = part.toInt(&ok);
                        if (!ok || bits < 0 || bits > 255)
                            return false;
                        chunk.bits.append(quint8(bits));
                    }
                }
                if (!lines.expect(QStringLiteral("}")))
                    return false;
            } else {
                bool ok = false;
                chunk.type = value.toInt(&ok);
                if (!ok || chunk.type < 0 || chunk.type > 5
                        || chunk.type == 2)
                    return false;
            }
            chunks->append(chunk);
        }
    }
    return lines.next().isEmpty();
}

QByteArray writeChunkData(const QVector<ChunkData> &chunks)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out << qint16(1);
    for (const ChunkData &chunk : chunks) {
        out << quint8(chunk.type);
        if (chunk.type == 2) {
            for (quint8 bits : chunk.bits)
                out << bits;
        }
    }
    return out.status() == QDataStream::Ok
            && chunks.size() == ChunkCount ? bytes : QByteArray();
}

QString headerFileName(int x, int y)
{
    return QStringLiteral("%1_%2.lotheader").arg(x).arg(y);
}

QString lotpackFileName(int x, int y)
{
    return QStringLiteral("world_%1_%2.lotpack").arg(x).arg(y);
}

QString chunkDataFileName(int x, int y)
{
    return QStringLiteral("chunkdata_%1_%2.bin").arg(x).arg(y);
}

QStringList cellFileNames(int x, int y, bool source)
{
    QStringList names = {headerFileName(x, y), lotpackFileName(x, y),
                         chunkDataFileName(x, y)};
    if (source) {
        for (QString &name : names)
            name += QStringLiteral(".source");
    }
    return names;
}

QString navigationBaselineName(int x, int y)
{
    return chunkDataFileName(x, y) + QStringLiteral(".source.baseline");
}

QString navigationOverrideName(int x, int y)
{
    return QStringLiteral("chunkdata_%1_%2.override.png").arg(x).arg(y);
}

QString navigationPreviousName(int x, int y)
{
    return chunkDataFileName(x, y) + QStringLiteral(".source.previous");
}

QString navigationDependenciesName(int x, int y)
{
    return chunkDataFileName(x, y) + QStringLiteral(".source.dependencies");
}

QByteArray navigationDependenciesText(QStringList headers)
{
    headers.removeDuplicates();
    headers.sort();
    return QStringLiteral("navigationDependencies\nversion = 1\nheaders = %1\n{\n%2\n}\n")
            .arg(headers.size()).arg(headers.join(QLatin1Char('\n'))).toUtf8();
}

bool checkNavigationDependencies(const QDir &directory, int x, int y, QString *error)
{
    const QString path = directory.filePath(navigationDependenciesName(x, y));
    if (!QFileInfo::exists(path)) {
        if (!QFileInfo::exists(directory.filePath(navigationBaselineName(x, y))))
            return true;
        *error = QStringLiteral("Navigation preservation requires its dependency reference: %1").arg(path);
        return false;
    }
    QByteArray bytes;
    if (!readFile(path, &bytes, error))
        return false;
    Lines lines(QString::fromUtf8(bytes));
    int version = 0, count = 0;
    if (!lines.expect(QStringLiteral("navigationDependencies"))
            || !lines.number(QStringLiteral("version"), &version) || version != 1
            || !lines.number(QStringLiteral("headers"), &count) || !validCount(count, 100000)
            || !lines.expect(QStringLiteral("{"))) {
        *error = QStringLiteral("Invalid navigation dependency reference: %1").arg(path);
        return false;
    }
    const QRegularExpression pattern(QStringLiteral("^-?\\d+_-?\\d+\\.lotheader$"));
    for (int index = 0; index < count; ++index) {
        const QString name = lines.next();
        if (!pattern.match(name).hasMatch()) {
            *error = QStringLiteral("Invalid neighboring header name in %1").arg(path);
            return false;
        }
        if (!QFileInfo::exists(directory.filePath(name))
                && !QFileInfo::exists(directory.filePath(name + QStringLiteral(".source")))
                && !QFileInfo::exists(directory.filePath(name + QStringLiteral(".source.context")))) {
            *error = QStringLiteral("Required neighboring header is missing: %1").arg(name);
            return false;
        }
    }
    if (!lines.expect(QStringLiteral("}")) || !lines.next().isEmpty()) {
        *error = QStringLiteral("Invalid navigation dependency reference: %1").arg(path);
        return false;
    }
    return true;
}

bool sourceText(const QByteArray &bytes, const QString &path, QString *text, QString *error)
{
    QTextCodec::ConverterState state;
    *text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars || state.remainingChars) {
        *error = QStringLiteral("Invalid UTF-8: %1").arg(path);
        return false;
    }
    if (text->startsWith(QChar(0xfeff)))
        text->remove(0, 1);
    return true;
}

class NavigationDefinitions
{
public:
    bool load(const QStringList &paths, QString *error)
    {
        if (!mDefinitions.load(paths, *error))
            return false;
        if (!mDefinitions.sheets().isEmpty())
            return true;
        *error = QStringLiteral("Choose base-game and mod tile definitions before rebuilding navigation");
        return false;
    }

    const QMap<QString, QString> *properties(const QString &name, QString *error) const
    {
        const auto cached = mProperties.constFind(name);
        if (cached != mProperties.cend())
            return cached.value();
        static const QMap<QString, QString> jumbo = {{QStringLiteral("tree"), QStringLiteral("4")}};
        if (name == QStringLiteral("jumbo_tree_01_0"))
            return &jumbo;
        const int split = name.lastIndexOf(QLatin1Char('_'));
        bool valid = false;
        const int index = name.mid(split + 1).toInt(&valid);
        if (split > 0 && valid && index >= 0) {
            if (const auto *tile = mDefinitions.tile(name.left(split), index)) {
                mProperties.insert(name, &tile->mProperties);
                return &tile->mProperties;
            }
        }
        *error = QStringLiteral("Missing tile definition for sprite %1. Add its mod TileDefs.").arg(name);
        return nullptr;
    }

private:
    Navigate::IsoGridSquare256::Definitions mDefinitions;
    mutable QHash<QString, const QMap<QString, QString> *> mProperties;
};

using RoomIndex = QMap<QString, QVector<QRect>>;

qint64 cellCoordinate(qint64 position)
{
    return position >= 0 ? position / 256 : (position + 1) / 256 - 1;
}

bool loadRoomIndex(const QDir &directory, const QMap<QString, QPoint> &targets,
                   bool sourcePreferred, RoomIndex *index, QString *error,
                   QMap<QString, QByteArray> *contexts = nullptr,
                   QMap<QString, QSet<QString>> *dependencies = nullptr)
{
    QMap<int, QMap<int, QString>> rows;
    for (auto it = targets.cbegin(); it != targets.cend(); ++it)
        rows[it.value().y()].insert(it.value().x(), it.key());
    QMap<QString, QString> files;
    for (const QString &name : directory.entryList({QStringLiteral("*.lotheader.source.context")}, QDir::Files))
        files.insert(name.left(name.size() - 15), name);
    for (const QString &name : directory.entryList({QStringLiteral("*.lotheader")}, QDir::Files))
        files.insert(name, name);
    if (sourcePreferred) {
        for (const QString &name : directory.entryList({QStringLiteral("*.lotheader.source")}, QDir::Files))
            files.insert(name.left(name.size() - 7), name);
    }
    const QRegularExpression pattern(QStringLiteral("^(-?\\d+)_(-?\\d+)\\.lotheader$"));
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const auto match = pattern.match(it.key());
        if (!match.hasMatch())
            continue;
        bool validX = false, validY = false;
        const int cellX = match.captured(1).toInt(&validX);
        const int cellY = match.captured(2).toInt(&validY);
        if (!validX || !validY) {
            *error = QStringLiteral("Header cell coordinates are out of range: %1").arg(it.value());
            return false;
        }
        const QString path = directory.filePath(it.value());
        QByteArray bytes;
        Header header;
        QString text;
        int line = 0;
        if (!readFile(path, &bytes, error))
            return false;
        const bool binary = it.value().endsWith(QStringLiteral(".lotheader"));
        const bool valid = !binary
                ? sourceText(bytes, path, &text, error) && parseHeader(text, &header, &line)
                : readHeader(bytes, &header);
        if (!valid) {
            if (error->isEmpty())
                *error = QStringLiteral("%1:%2: Invalid neighboring LOT header").arg(path).arg(line);
            return false;
        }
        bool intersects = false;
        for (const Room &room : header.rooms) {
            if (room.level != 0)
                continue;
            for (const QVector<int> &rect : room.rects) {
                if (rect.at(2) <= 0 || rect.at(3) <= 0) {
                    *error = QStringLiteral("Invalid room rectangle in %1").arg(path);
                    return false;
                }
                const qint64 x = qint64(cellX) * 256 + rect.at(0);
                const qint64 y = qint64(cellY) * 256 + rect.at(1);
                const qint64 right = x + rect.at(2);
                const qint64 bottom = y + rect.at(3);
                if (cellCoordinate(x) > INT_MAX || cellCoordinate(y) > INT_MAX
                        || cellCoordinate(right - 1) < INT_MIN || cellCoordinate(bottom - 1) < INT_MIN)
                    continue;
                auto row = rows.lowerBound(int(qMax<qint64>(INT_MIN, cellCoordinate(y))));
                for (; row != rows.cend() && row.key() <= cellCoordinate(bottom - 1); ++row) {
                    auto column = row.value().lowerBound(int(qMax<qint64>(INT_MIN, cellCoordinate(x))));
                    for (; column != row.value().cend() && column.key() <= cellCoordinate(right - 1); ++column) {
                        const qint64 originX = qint64(column.key()) * 256;
                        const qint64 originY = qint64(row.key()) * 256;
                        const int left = int(qMax<qint64>(0, x - originX));
                        const int top = int(qMax<qint64>(0, y - originY));
                        (*index)[column.value()].append(QRect(left, top,
                            int(qMin<qint64>(256, right - originX)) - left,
                            int(qMin<qint64>(256, bottom - originY)) - top));
                        intersects = true;
                        if (dependencies)
                            (*dependencies)[column.value()].insert(it.key());
                    }
                }
            }
        }
        if (contexts && binary && intersects)
            contexts->insert(it.key() + QStringLiteral(".source.context"), headerText(header).toUtf8());
    }
    return true;
}

QVector<quint8> navigationSquares(const QVector<ChunkData> &chunks)
{
    QVector<quint8> result(256 * 256);
    for (int chunk = 0; chunk < chunks.size(); ++chunk) {
        const ChunkData &data = chunks.at(chunk);
        const quint8 uniform = data.type == 3 ? 8 : data.type == 4 ? 16
                : data.type == 5 ? 32 : quint8(data.type);
        for (int square = 0; square < 64; ++square) {
            const int x = (chunk % 32) * 8 + square % 8;
            const int y = (chunk / 32) * 8 + square / 8;
            result[x + y * 256] = data.type == 2 ? data.bits.at(square) : uniform;
        }
    }
    return result;
}

QByteArray navigationBytes(const QVector<quint8> &squares)
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out << qint16(1);
    for (int chunk = 0; chunk < 1024; ++chunk) {
        QVector<quint8> bits;
        bits.reserve(64);
        for (int square = 0; square < 64; ++square) {
            const int x = (chunk % 32) * 8 + square % 8;
            const int y = (chunk / 32) * 8 + square / 8;
            bits.append(squares.at(x + y * 256));
        }
        Navigate::NavigationFlags::writeChunk(out, bits);
    }
    return result;
}

bool calculateNavigation(const Header &header, const QVector<Chunk> &chunks,
                         const QVector<QRect> &rooms, const NavigationDefinitions &definitions,
                         const QVector<quint8> &coverage, bool legacy,
                         QVector<quint8> *result, QString *error)
{
    result->fill(0, 256 * 256);
    for (const QRect &rect : rooms) {
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x)
                (*result)[x + y * 256] |= 16;
    }
    if (header.minLevel <= 0 && header.maxLevel >= 0) {
        for (int chunk = 0; chunk < chunks.size(); ++chunk) {
            for (auto square = chunks.at(chunk).cbegin(); square != chunks.at(chunk).cend(); ++square) {
                if (header.minLevel + square.key() / 64 != 0)
                    continue;
                const int x = (chunk / CellSide) * ChunkSide + (square.key() / ChunkSide) % ChunkSide;
                const int y = (chunk % CellSide) * ChunkSide + square.key() % ChunkSide;
                const int position = x + y * 256;
                if (coverage.at(position) & 32)
                    continue;
                Navigate::NavigationFlags flags;
                for (int tileId : square.value().tileIds) {
                    const auto properties = definitions.properties(header.tiles.at(tileId), error);
                    if (!properties) {
                        *error += QStringLiteral(" Square %1,%2,0").arg(x).arg(y);
                        return false;
                    }
                    flags.add(*properties);
                }
                (*result)[position] |= flags.bits(legacy);
            }
        }
    }
    for (int position = 0; position < result->size(); ++position) {
        if (coverage.at(position) & 32)
            (*result)[position] = coverage.at(position);
        else
            (*result)[position] |= coverage.at(position) & ~31;
    }
    return true;
}

bool prepareCell(const QDir &directory, int x, int y, bool importing,
                 QVector<QByteArray> *result, QString *error,
                 Header *decodedHeader = nullptr, QVector<Chunk> *decodedLotpack = nullptr,
                 QVector<ChunkData> *decodedNavigation = nullptr)
{
    const QStringList names = cellFileNames(x, y, importing);
    QVector<QByteArray> input(3);
    for (int index = 0; index < names.size(); ++index) {
        if (!readFile(directory.filePath(names.at(index)), &input[index], error))
            return false;
    }
    Header header;
    QVector<Chunk> lotpack;
    QVector<ChunkData> chunkData;
    if (importing) {
        QVector<QString> text;
        for (int index = 0; index < input.size(); ++index) {
            QString decoded;
            if (!sourceText(input.at(index), directory.filePath(names.at(index)), &decoded, error))
                return false;
            text.append(decoded);
        }
        int line = 0;
        for (int index = 0; index < names.size(); ++index) {
            const bool valid = index == 0 ? parseHeader(text.at(0), &header, &line)
                    : index == 1 ? parseLotpack(text.at(1), header, &lotpack, &line)
                    : parseChunkData(text.at(2), &chunkData, &line);
            if (!valid) {
                *error = QStringLiteral("%1:%2: Invalid or incompatible LOT source text")
                        .arg(directory.filePath(names.at(index))).arg(line);
                return false;
            }
        }
        *result = {writeHeader(header), writeLotpack(header, lotpack),
                   writeChunkData(chunkData)};
        for (const QByteArray &bytes : *result) {
            if (bytes.isEmpty()) {
                *error = QStringLiteral("LOT source text could not be encoded. "
                                        "Tile and room names must fit Latin-1: %1")
                        .arg(directory.filePath(names.first()));
                return false;
            }
        }
    } else {
        for (int index = 0; index < names.size(); ++index) {
            const bool valid = index == 0 ? readHeader(input.at(0), &header)
                    : index == 1 ? readLotpack(input.at(1), header, &lotpack)
                    : readChunkData(input.at(2), &chunkData);
            if (!valid) {
                *error = QStringLiteral("Invalid or unsupported LOT file: %1")
                        .arg(directory.filePath(names.at(index)));
                return false;
            }
        }
        *result = {headerText(header).toUtf8(), lotpackText(header, lotpack).toUtf8(),
                   chunkDataText(chunkData).toUtf8()};
    }
    if (decodedHeader)
        *decodedHeader = header;
    if (decodedLotpack)
        *decodedLotpack = lotpack;
    if (decodedNavigation)
        *decodedNavigation = chunkData;
    return true;
}

}

bool rebuildNavigation(const QDir &directory, int x, int y,
                       const Header &header, const QVector<Chunk> &chunks,
                       const QVector<ChunkData> &sourceNavigation,
                       const NavigationDefinitions &definitions,
                       const RoomIndex &newRooms, const RoomIndex &oldRooms,
                       const QSet<QString> &dependencies,
                       const LotSourceFiles::NavigationOptions &options,
                       QVector<QByteArray> *data, QStringList *names, QString *error)
{
    const QString key = headerFileName(x, y);
    const QVector<quint8> current = navigationSquares(sourceNavigation);
    QVector<quint8> previousAutomatic;
    QVector<quint8> previousFinal;
    QByteArray bytes;
    const QString binaryNavigation = directory.filePath(chunkDataFileName(x, y));
    const QString previousPath = directory.filePath(navigationPreviousName(x, y));
    if (QFileInfo::exists(previousPath)) {
        QVector<ChunkData> previous;
        QString text;
        if (!readFile(previousPath, &bytes, error)
                || !sourceText(bytes, previousPath, &text, error)
                || !parseChunkData(text, &previous)) {
            *error = QStringLiteral("Invalid previous navigation reference: %1").arg(previousPath);
            return false;
        }
        previousFinal = navigationSquares(previous);
    } else if (QFileInfo::exists(binaryNavigation)) {
        QVector<ChunkData> previous;
        if (!readFile(binaryNavigation, &bytes, error) || !readChunkData(bytes, &previous)) {
            *error = QStringLiteral("Invalid previous navigation: %1").arg(binaryNavigation);
            return false;
        }
        previousFinal = navigationSquares(previous);
    }
    const QString baseline = directory.filePath(navigationBaselineName(x, y));
    if (QFileInfo::exists(baseline)) {
        QString text;
        QVector<ChunkData> previous;
        if (!readFile(baseline, &bytes, error) || !sourceText(bytes, baseline, &text, error)
                || !parseChunkData(text, &previous)) {
            *error = QStringLiteral("Invalid automatic navigation baseline: %1").arg(baseline);
            return false;
        }
        previousAutomatic = navigationSquares(previous);
        if (previousFinal.isEmpty()) {
            *error = QStringLiteral("Navigation preservation requires .source.previous or the original binary navigation: %1")
                    .arg(binaryNavigation);
            return false;
        }
    } else {
        Header previousHeader;
        QVector<Chunk> previousChunks;
        QByteArray headerBytes, packBytes;
        if (previousFinal.isEmpty()
                || !readFile(directory.filePath(headerFileName(x, y)), &headerBytes, error)
                || !readFile(directory.filePath(lotpackFileName(x, y)), &packBytes, error)
                || !readHeader(headerBytes, &previousHeader)
                || !readLotpack(packBytes, previousHeader, &previousChunks)) {
            *error = QStringLiteral("Navigation preservation requires a .source.baseline or the original binary triplet for cell %1,%2")
                    .arg(x).arg(y);
            return false;
        }
        if (!calculateNavigation(previousHeader, previousChunks, oldRooms.value(key),
                                  definitions, previousFinal, true, &previousAutomatic, error))
            return false;
    }
    QVector<quint8> automatic;
    if (!calculateNavigation(header, chunks, newRooms.value(key), definitions,
                              current, false, &automatic, error))
        return false;
    QImage overrides(256, 256, QImage::Format_ARGB32);
    overrides.fill(Qt::transparent);
    const QString overridePath = directory.filePath(navigationOverrideName(x, y));
    if (QFileInfo::exists(overridePath)
            && !ChunkDataOverride::loadImage(overridePath, &overrides, error))
        return false;
    overrides = overrides.convertToFormat(QImage::Format_ARGB32);
    for (int position = 0; position < current.size(); ++position) {
        const int px = position % 256;
        const int py = position / 256;
        const bool textEdit = !previousFinal.isEmpty()
                && ((current.at(position) ^ previousFinal.at(position)) & 31);
        const bool difference = (current.at(position) ^ previousAutomatic.at(position)) & 31;
        const bool maskEdit = qAlpha(overrides.pixel(px, py)) == 255
                && !previousFinal.isEmpty()
                && (qRed(overrides.pixel(px, py)) != (previousFinal.at(position) & 31));
        if (!(current.at(position) & 32)
                && ((textEdit && !maskEdit) || (difference && qAlpha(overrides.pixel(px, py)) == 0))) {
            const quint8 value = current.at(position) & 31;
            overrides.setPixel(px, py, qRgba(value, 0, 0, 255));
        }
    }
    const QString projectOverride = options.overrides.value(key);
    if (!projectOverride.isEmpty()) {
        QImage image;
        if (!ChunkDataOverride::loadImage(projectOverride, &image, error))
            return false;
        for (int py = 0; py < 256; ++py)
            for (int px = 0; px < 256; ++px)
                if (qAlpha(image.pixel(px, py)) == 255)
                    overrides.setPixel(px, py, image.pixel(px, py));
    }
    QVector<quint8> final = automatic;
    for (int position = 0; position < final.size(); ++position) {
        if (current.at(position) & 32)
            continue;
        final[position] = (automatic.at(position) & ~31)
                | ChunkDataOverride::mergedBits(overrides, position % 256, position / 256,
                                                automatic.at(position) & 31);
    }
    const QByteArray automaticBytes = navigationBytes(automatic);
    const QByteArray finalBytes = navigationBytes(final);
    QVector<ChunkData> automaticChunks, finalChunks;
    if (!readChunkData(automaticBytes, &automaticChunks) || !readChunkData(finalBytes, &finalChunks)) {
        *error = QStringLiteral("Could not encode rebuilt navigation");
        return false;
    }
    QByteArray png;
    QBuffer pngBuffer(&png);
    pngBuffer.open(QIODevice::WriteOnly);
    if (!overrides.save(&pngBuffer, "PNG")) {
        *error = QStringLiteral("Could not encode navigation correction mask");
        return false;
    }
    (*data)[2] = finalBytes;
    names->append(chunkDataFileName(x, y) + QStringLiteral(".source"));
    data->append(chunkDataText(finalChunks).toUtf8());
    names->append(navigationBaselineName(x, y));
    data->append(chunkDataText(automaticChunks).toUtf8());
    names->append(navigationPreviousName(x, y));
    data->append(chunkDataText(finalChunks).toUtf8());
    names->append(navigationDependenciesName(x, y));
    data->append(navigationDependenciesText(dependencies.values()));
    names->append(navigationOverrideName(x, y));
    data->append(png);
    return true;
}

bool LotSourceFiles::exportCell(const QString &directory, int cellX,
                                int cellY, QString *error,
                                const QByteArray &automaticNavigation, const QImage &overrides,
                                const QStringList &roomDependencies)
{
    QVector<QByteArray> data;
    QStringList names = cellFileNames(cellX, cellY, true);
    if (!prepareCell(QDir(directory), cellX, cellY, false, &data, error))
        return false;
    if (!automaticNavigation.isEmpty()) {
        QVector<ChunkData> automatic;
        if (!readChunkData(automaticNavigation, &automatic)) {
            *error = QStringLiteral("Invalid automatic navigation baseline");
            return false;
        }
        names.append(navigationBaselineName(cellX, cellY));
        data.append(chunkDataText(automatic).toUtf8());
        names.append(navigationPreviousName(cellX, cellY));
        data.append(data.at(2));
        names.append(navigationDependenciesName(cellX, cellY));
        data.append(navigationDependenciesText(roomDependencies));
        QImage mask = overrides;
        if (mask.isNull()) {
            mask = QImage(256, 256, QImage::Format_ARGB32);
            mask.fill(Qt::transparent);
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!mask.save(&buffer, "PNG"))
            return false;
        names.append(navigationOverrideName(cellX, cellY));
        data.append(png);
    }
    return writeFiles(QDir(directory), names, data, error);
}

bool LotSourceFiles::importCell(const QString &directory, int cellX,
                                int cellY, QString *error)
{
    QVector<QByteArray> data;
    return prepareCell(QDir(directory), cellX, cellY, true, &data, error)
            && writeFiles(QDir(directory), cellFileNames(cellX, cellY, false), data, error);
}

bool LotSourceFiles::processDirectory(const QString &directory, bool import,
                                      int *cellCount, QString *error, bool checkOnly,
                                      const NavigationOptions *navigation)
{
    if (cellCount)
        *cellCount = 0;
    const QDir folder(directory);
    const QString suffix = import ? QStringLiteral(".source") : QString();
    const QStringList filters = {QStringLiteral("*.lotheader") + suffix,
                                QStringLiteral("world_*.lotpack") + suffix,
                                QStringLiteral("chunkdata_*.bin") + suffix};
    const QStringList patterns = {
        QStringLiteral("^(-?\\d+)_(-?\\d+)\\.lotheader"),
        QStringLiteral("^world_(-?\\d+)_(-?\\d+)\\.lotpack"),
        QStringLiteral("^chunkdata_(-?\\d+)_(-?\\d+)\\.bin")};
    QMap<QString, QPoint> cells;
    for (int index = 0; index < filters.size(); ++index) {
        const QRegularExpression pattern(patterns.at(index)
                + (import ? QStringLiteral("\\.source$") : QStringLiteral("$")));
        for (const QString &name : folder.entryList({filters.at(index)}, QDir::Files, QDir::Name)) {
            const QRegularExpressionMatch match = pattern.match(name);
            if (!match.hasMatch())
                continue;
            bool validX = false, validY = false;
            const int x = match.captured(1).toInt(&validX);
            const int y = match.captured(2).toInt(&validY);
            if (!validX || !validY) {
                *error = QStringLiteral("Cell coordinates are out of range: %1").arg(name);
                return false;
            }
            if (navigation && index == 0
                    && !QFileInfo::exists(folder.filePath(lotpackFileName(x, y) + suffix))
                    && !QFileInfo::exists(folder.filePath(chunkDataFileName(x, y) + suffix)))
                continue;
            cells.insert(headerFileName(x, y), QPoint(x, y));
        }
    }
    if (cells.isEmpty()) {
        *error = QStringLiteral("No LOT files were found in the selected directory");
        return false;
    }
    NavigationDefinitions definitions;
    RoomIndex newRooms, oldRooms;
    QMap<QString, QByteArray> contexts;
    QMap<QString, QSet<QString>> dependencies;
    if (navigation) {
        if (!import) {
            *error = QStringLiteral("Navigation rebuilding requires source import");
            return false;
        }
        bool needsLegacy = false;
        for (const QPoint &cell : cells) {
            if (!checkNavigationDependencies(folder, cell.x(), cell.y(), error))
                return false;
            needsLegacy |= !QFileInfo::exists(folder.filePath(navigationBaselineName(cell.x(), cell.y())));
        }
        if (!definitions.load(navigation->tileDefinitions, error)
                || !loadRoomIndex(folder, cells, true, &newRooms, error, &contexts, &dependencies)
                || (needsLegacy && !loadRoomIndex(folder, cells, false, &oldRooms, error)))
            return false;
    }
    QTemporaryDir staging;
    if (!staging.isValid()) {
        *error = QStringLiteral("Could not create a temporary LOT conversion directory");
        return false;
    }
    QMap<QString, QStringList> outputNames;
    for (const QPoint &cell : cells) {
        QVector<QByteArray> data;
        Header header;
        QVector<Chunk> chunks;
        QVector<ChunkData> chunkData;
        QStringList names = cellFileNames(cell.x(), cell.y(), !import);
        if (!prepareCell(folder, cell.x(), cell.y(), import, &data, error,
                          &header, &chunks, &chunkData)
                || (navigation && !rebuildNavigation(folder, cell.x(), cell.y(), header, chunks,
                       chunkData, definitions, newRooms, oldRooms,
                       dependencies.value(headerFileName(cell.x(), cell.y())),
                       *navigation, &data, &names, error))
                || (!checkOnly && !writeFiles(QDir(staging.path()),
                                              names, data, error))) {
            *error = QStringLiteral("Cell %1,%2: %3")
                    .arg(cell.x()).arg(cell.y()).arg(*error);
            return false;
        }
        outputNames.insert(headerFileName(cell.x(), cell.y()), names);
    }
    if (!checkOnly) {
        for (auto it = contexts.cbegin(); it != contexts.cend(); ++it)
            if (!writeFile(QDir(staging.path()).filePath(it.key()), it.value(), error))
                return false;
    }
    for (const QPoint &cell : cells) {
        if (!checkOnly) {
            const QStringList names = outputNames.value(headerFileName(cell.x(), cell.y()));
            QVector<QByteArray> data(names.size());
            for (int index = 0; index < names.size(); ++index) {
                if (!readFile(QDir(staging.path()).filePath(names.at(index)),
                              &data[index], error))
                    return false;
            }
            if (!writeFiles(folder, names, data, error))
                return false;
        }
        if (cellCount)
            ++*cellCount;
    }
    if (!checkOnly) {
        for (auto it = contexts.cbegin(); it != contexts.cend(); ++it)
            if (!writeFiles(folder, {it.key()}, {it.value()}, error))
                return false;
    }
    return true;
}

bool LotSourceFiles::validateNavigation(QString *error)
{
    for (const QString &key : {QStringLiteral("stairsBN"), QStringLiteral("stairsMN"),
             QStringLiteral("stairsTN"), QStringLiteral("stairsBW"),
             QStringLiteral("stairsMW"), QStringLiteral("stairsTW")}) {
        Navigate::NavigationFlags flags;
        flags.add({{key, QString()}});
        if (flags.bits() != 1 || flags.bits(true) != 0) {
            *error = QStringLiteral("Stair collision flags differ");
            return false;
        }
        flags.add({{QStringLiteral("water"), QString()}});
        if (flags.bits() != 8) {
            *error = QStringLiteral("Water did not override stair solidity");
            return false;
        }
    }
    Navigate::NavigationFlags tree, fence;
    tree.add({{QStringLiteral("tree"), QString()}});
    tree.add({{QStringLiteral("stairsTN"), QString()}});
    fence.add({{QStringLiteral("HoppableN"), QString()}});
    fence.add({{QStringLiteral("collideN"), QString()}});
    if (tree.bits() != 0 || fence.bits() != 0) {
        *error = QStringLiteral("Tree or hoppable collision priority differs");
        return false;
    }
    {
        QTemporaryDir output;
        if (!output.isValid())
            return false;
        TileDefFile definitions;
        auto *sheet = new TileDefTileset;
        sheet->mName = QStringLiteral("export_stairs");
        sheet->mColumns = sheet->mRows = 0;
        sheet->resize(1, 1);
        sheet->tileAt(0)->mProperties.insert(QStringLiteral("stairsTN"), QString());
        definitions.insertTileset(0, sheet);
        const auto saved = Navigate::IsoGridSquare256::mTileDefinitions;
        const auto restore = qScopeGuard([&]() {
            Navigate::IsoGridSquare256::mTileDefinitions = saved;
        });
        Navigate::IsoGridSquare256::mTileDefinitions = {};
        Navigate::IsoGridSquare256::mTileDefinitions.add(definitions);
        Tiled::Tileset tiles(QStringLiteral("export_stairs"), 64, 128);
        tiles.loadFromNothing(QSize(64, 128), QStringLiteral("export_stairs"));
        Tiled::Map map(Tiled::Map::LevelIsometric, 8, 8, 64, 32);
        map.addTileset(&tiles);
        auto *layer = new Tiled::TileLayer(QStringLiteral("Floor"), 0, 0, 8, 8);
        layer->setCell(0, 0, Tiled::Cell(tiles.tileAt(0)));
        map.addLayer(layer);
        MapInfo info(&map);
        info.setFilePath(QStringLiteral("<LotFilesManagerMap>"));
        MapComposite composite(&info);
        composite.synch();
        CombinedCellMaps combined;
        combined.mCell256X = combined.mCell256Y = 0;
        combined.mMinSourceCellX = combined.mMinSourceCellY = 0;
        combined.mSourceCellSize = 256;
        LotFile::RectLookup<LotFile::RoomRect> rooms;
        rooms.clear(0, 0, 32, 32, 8);
        GenerateLotsSettings settings;
        settings.exportDir = output.path();
        QImage mask(256, 256, QImage::Format_ARGB32);
        mask.fill(Qt::transparent);
        mask.setPixel(0, 0, qRgba(0, 0, 0, 255));
        QBitArray selected(1024, false);
        selected.setBit(0);
        QByteArray baseline;
        Navigate::ChunkDataFile256 writer;
        if (!writer.fromMap(combined, &composite, rooms, settings, mask, error,
                            &selected, &baseline))
            return false;
        QByteArray bytes;
        QVector<ChunkData> automatic, final;
        if (!readChunkData(baseline, &automatic)
                || !readFile(QDir(output.path()).filePath(chunkDataFileName(0, 0)), &bytes, error)
                || !readChunkData(bytes, &final)
                || navigationSquares(automatic)[0] != 1 || navigationSquares(final)[0] != 0
                || navigationSquares(final).last() != 32) {
            *error = QStringLiteral("Map export lost stair collision, its automatic baseline or manual empty override");
            return false;
        }
    }
    QTemporaryDir directory;
    if (!directory.isValid())
        return false;
    const QDir folder(directory.path());
    const QString base = folder.filePath(QStringLiteral("base.tiles.txt"));
    const QString mod = folder.filePath(QStringLiteral("mod.tiles.txt"));
    const QString patch = folder.filePath(QStringLiteral("mod.patch.tiles.txt"));
    if (!writeFile(base, QByteArray(
        "version = 1\ntileset\n{\n file = nav_fixture\n size = 8,1\n id = 1\n"
        " tile\n {\n xy = 0,0\n solidfloor =\n }\n"
        " tile\n {\n xy = 1,0\n stairsTN =\n }\n"
        " tile\n {\n xy = 2,0\n solid =\n }\n}\n"), error)
            || !writeFile(mod, QByteArray(
        "version = 1\ntileset\n{\n file = mod_fixture\n size = 8,1\n id = 2\n"
        " tile\n {\n xy = 0,0\n solid =\n }\n}\n"), error)
            || !writeFile(patch, QByteArray(
        "version = 1\ntileset\n{\n file = mod_fixture\n size = 8,1\n id = 2\n"
        " tile\n {\n xy = 0,0\n collideW =\n }\n}\n"), error))
        return false;
    QByteArray binaryPair;
    QDataStream pairStream(&binaryPair, QIODevice::WriteOnly);
    pairStream.setByteOrder(QDataStream::LittleEndian);
    pairStream.writeRawData("tdef", 4);
    pairStream << qint32(1) << qint32(1);
    const auto pairName = [&](const QByteArray &name) {
        const QByteArray bytes = name + '\n';
        pairStream.writeRawData(bytes.constData(), bytes.size());
    };
    pairName("pair_fixture");
    pairName("pair_fixture.png");
    pairStream << qint32(8) << qint32(1) << qint32(3) << qint32(1) << qint32(1);
    pairName("solid");
    pairName("");
    const QString pairText = folder.filePath(QStringLiteral("pair.tiles.txt"));
    const QString regularOverride = folder.filePath(QStringLiteral("override.tiles.txt"));
    if (!writeFile(folder.filePath(QStringLiteral("pair.tiles")), binaryPair, error)
            || !writeFile(pairText, QByteArray(
        "version = 1\ntileset\n{\n file = pair_fixture\n size = 8,1\n id = 3\n"
        " tile\n {\n xy = 0,0\n water =\n }\n}\n"), error)
            || !writeFile(regularOverride, QByteArray(
        "version = 1\ntileset\n{\n file = mod_fixture\n size = 8,1\n id = 2\n"
        " tile\n {\n xy = 0,0\n water =\n }\n}\n"), error))
        return false;
    NavigationDefinitions discovered, ordered;
    if (!discovered.load({folder.path(), pairText}, error)
            || !ordered.load({base, mod, regularOverride}, error))
        return false;
    const auto selectedPair = discovered.properties(QStringLiteral("pair_fixture_0"), error);
    const auto patchedMod = discovered.properties(QStringLiteral("mod_fixture_0"), error);
    const auto overriddenMod = ordered.properties(QStringLiteral("mod_fixture_0"), error);
    if (!selectedPair || !selectedPair->contains(QStringLiteral("water"))
            || selectedPair->contains(QStringLiteral("solid"))
            || !patchedMod || !patchedMod->contains(QStringLiteral("collideW"))
            || !overriddenMod || !overriddenMod->contains(QStringLiteral("water"))
            || overriddenMod->contains(QStringLiteral("solid"))) {
        *error = QStringLiteral("Directory patches, explicit text selection or ordered mod overrides differ");
        return false;
    }
    Header header;
    header.maxLevel = 1;
    header.tiles = {QStringLiteral("nav_fixture_0"), QStringLiteral("nav_fixture_1"),
                    QStringLiteral("nav_fixture_2"), QStringLiteral("mod_fixture_0"),
                    QStringLiteral("jumbo_tree_01_0"), QStringLiteral("unknown_0")};
    header.density.fill(0, 1024);
    Room room;
    room.name = QStringLiteral("room");
    room.rects.append(QVector<int>{0, 0, 1, 1});
    header.rooms.append(room);
    header.buildings.append(QVector<int>{0});
    QVector<Chunk> chunks(1024);
    const auto place = [&](QVector<Chunk> *target, int x, int y, const QVector<int> &tiles, int z = 0) {
        Square square;
        square.roomId = -1;
        square.tileIds = tiles;
        const int chunk = (x / ChunkSide) * CellSide + y / ChunkSide;
        const int position = z * ChunkSide * ChunkSide + (x % ChunkSide) * ChunkSide + y % ChunkSide;
        (*target)[chunk].insert(position, square);
    };
    for (int x = 0; x < 9; ++x)
        place(&chunks, x, 0, {x == 5 ? 1 : 0});
    place(&chunks, 0, 1, {5}, 1);
    QVector<quint8> previous(256 * 256, 0);
    previous[0] = 16;
    previous[2] = 8;
    previous[8] = 8;
    for (int x = 0; x < 30; ++x)
        previous[x + 6 * 256] = 16;
    for (int y = 248; y < 256; ++y)
        for (int x = 248; x < 256; ++x)
            previous[x + y * 256] = 32;
    const QStringList names = cellFileNames(-1, 0, false);
    const QVector<QByteArray> original = {writeHeader(header), writeLotpack(header, chunks),
                                         navigationBytes(previous)};
    if (!writeFiles(folder, names, original, error) || !exportCell(folder.path(), -1, 0, error))
        return false;
    Header neighbor = header;
    neighbor.rooms[0].rects = {{0, 5, 768, 1}};
    if (!writeFile(folder.filePath(headerFileName(-4, 0)), writeHeader(neighbor), error))
        return false;
    neighbor.rooms[0].rects[0][2] = 780;
    if (!writeFile(folder.filePath(headerFileName(-4, 0) + QStringLiteral(".source")),
                   headerText(neighbor).toUtf8(), error))
        return false;
    neighbor.rooms[0].rects = {{0, 6, 542, 1}};
    if (!writeFile(folder.filePath(headerFileName(-3, 0)), writeHeader(neighbor), error))
        return false;
    for (int x : {1, 3, 4, 5, 8})
        place(&chunks, x, 0, {1});
    place(&chunks, 6, 0, {3});
    place(&chunks, 7, 0, {2, 4});
    place(&chunks, 8, 1, {3});
    place(&chunks, 1, 8, {0});
    QVector<quint8> edited = previous;
    edited[4] = 16;
    edited[8] = 0;
    QVector<ChunkData> editedChunks;
    if (!readChunkData(navigationBytes(edited), &editedChunks)
            || !writeFile(folder.filePath(names.at(1) + QStringLiteral(".source")),
                          lotpackText(header, chunks).toUtf8(), error)
            || !writeFile(folder.filePath(names.at(2) + QStringLiteral(".source")),
                          chunkDataText(editedChunks).toUtf8(), error))
        return false;
    QImage mask(256, 256, QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    mask.setPixel(3, 0, qRgba(0, 0, 0, 255));
    const QString maskPath = folder.filePath(QStringLiteral("project-mask.png"));
    if (!ChunkDataOverride::saveImage(maskPath, mask, error))
        return false;
    NavigationOptions options;
    options.tileDefinitions = QStringList{base, mod, patch};
    options.overrides.insert(names.first(), maskPath);
    int cells = 0;
    if (!processDirectory(folder.path(), true, &cells, error, true, &options)
            || cells != 1)
        return false;
    QByteArray bytes;
    for (int i = 0; i < 3; ++i) {
        if (!readFile(folder.filePath(names.at(i)), &bytes, error) || bytes != original.at(i)) {
            *error = QStringLiteral("Navigation check changed binary files");
            return false;
        }
    }
    if (!processDirectory(folder.path(), true, &cells, error, false, &options))
        return false;
    QVector<ChunkData> rebuilt;
    if (!readFile(folder.filePath(names.at(2)), &bytes, error) || !readChunkData(bytes, &rebuilt))
        return false;
    const QVector<quint8> result = navigationSquares(rebuilt);
    if (result[0] != 16 || result[1] != 1 || result[2] != 8 || result[3] != 0
            || result[4] != 16 || result[5] != 1 || result[6] != 5
            || result[7] != 0 || result[8] != 0 || result[255 + 255 * 256] != 32) {
        *error = QStringLiteral("Navigation lost stairs, mod patches, manual values or partial coverage");
        return false;
    }
    if (result[8 + 256] != 5 || result[1 + 8 * 256] != 0) {
        *error = QStringLiteral("Navigation transposed the chunk coordinates from LOT source");
        return false;
    }
    for (int x = 0; x < 12; ++x) {
        if (result[x + 5 * 256] != 16) {
            *error = QStringLiteral("Navigation lost a room owned three cells away");
            return false;
        }
    }
    if (!QFileInfo::exists(folder.filePath(QStringLiteral("-3_0.lotheader.source.context")))) {
        *error = QStringLiteral("Binary-only neighbor context was not retained");
        return false;
    }
    const auto snapshot = [&]() {
        QMap<QString, QByteArray> files;
        for (const QString &name : folder.entryList(QDir::Files)) {
            QFile file(folder.filePath(name));
            if (file.open(QIODevice::ReadOnly))
                files.insert(name, file.readAll());
        }
        return files;
    };
    const auto before = snapshot();
    if (!processDirectory(folder.path(), true, &cells, error, false, &options))
        return false;
    const auto repeated = snapshot();
    if (before != repeated) {
        QStringList changed;
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            if (it.value() != repeated.value(it.key()))
                changed.append(it.key());
        *error = QStringLiteral("Repeated navigation rebuild changed: %1").arg(changed.join(QStringLiteral(", ")));
        return false;
    }
    NavigationOptions missing = options;
    missing.tileDefinitions = QStringList{base};
    if (processDirectory(folder.path(), true, &cells, error, false, &missing)
            || !error->contains(QStringLiteral("mod_fixture_0")) || before != snapshot()) {
        *error = QStringLiteral("Missing mod definitions changed navigation outputs");
        return false;
    }
    error->clear();
    for (const QString &name : names)
        if (!QFile::remove(folder.filePath(name)))
            return false;
    if (!QFile::remove(folder.filePath(headerFileName(-3, 0)))
            || !QFile::remove(folder.filePath(headerFileName(-4, 0))))
        return false;
    place(&chunks, 1, 0, {0});
    place(&chunks, 5, 0, {0});
    if (!writeFile(folder.filePath(names.at(1) + QStringLiteral(".source")),
                   lotpackText(header, chunks).toUtf8(), error)
            || !processDirectory(folder.path(), true, &cells, error, false, &options)
            || !readFile(folder.filePath(names.at(2)), &bytes, error))
        return false;
    rebuilt.clear();
    if (!readChunkData(bytes, &rebuilt))
        return false;
    const auto withoutStairs = navigationSquares(rebuilt);
    if (withoutStairs[1] != 0 || withoutStairs[5] != 0
            || withoutStairs[2] != 8 || withoutStairs[3] != 0 || withoutStairs[8] != 0) {
        *error = QStringLiteral("Source-only rebuilding retained removed stairs or lost saved corrections");
        return false;
    }
    for (int x = 0; x < 30; ++x) {
        if (withoutStairs[x + 6 * 256] != 16) {
            *error = QStringLiteral("Source-only rebuilding lost retained neighboring room context");
            return false;
        }
    }
    QVector<quint8> sourceOnlyEdit = withoutStairs;
    sourceOnlyEdit[2] = 0;
    rebuilt.clear();
    if (!readChunkData(navigationBytes(sourceOnlyEdit), &rebuilt)
            || !writeFile(folder.filePath(names.at(2) + QStringLiteral(".source")),
                          chunkDataText(rebuilt).toUtf8(), error)
            || !QFile::remove(folder.filePath(names.at(2)))
            || !processDirectory(folder.path(), true, &cells, error, false, &options)
            || !readFile(folder.filePath(names.at(2)), &bytes, error))
        return false;
    rebuilt.clear();
    if (!readChunkData(bytes, &rebuilt) || navigationSquares(rebuilt)[2] != 0) {
        *error = QStringLiteral("Source-only text edits lost an explicit zero correction");
        return false;
    }
    const QString contextPath = folder.filePath(QStringLiteral("-3_0.lotheader.source.context"));
    QByteArray contextBytes;
    if (!readFile(contextPath, &contextBytes, error) || !QFile::remove(contextPath))
        return false;
    const auto missingContext = snapshot();
    if (processDirectory(folder.path(), true, &cells, error, false, &options)
            || !error->contains(QStringLiteral("-3_0.lotheader"))
            || missingContext != snapshot()) {
        *error = QStringLiteral("A missing required neighboring header changed navigation outputs");
        return false;
    }
    error->clear();
    if (!writeFile(contextPath, contextBytes, error))
        return false;
    QTemporaryDir exported;
    if (!exported.isValid() || !writeFiles(QDir(exported.path()), names, original, error)
            || !exportCell(exported.path(), -1, 0, error, navigationBytes(previous), mask)
            || !QFileInfo::exists(QDir(exported.path()).filePath(navigationBaselineName(-1, 0)))
            || !QFileInfo::exists(QDir(exported.path()).filePath(navigationOverrideName(-1, 0))))
        return false;
    error->clear();
    return true;
}

bool LotSourceFiles::validate(QString *error)
{
    QTemporaryDir directory;
    if (!directory.isValid())
        return false;
    Header header;
    header.minLevel = -1;
    header.maxLevel = 1;
    header.tiles.append(QStringLiteral("floors_01_0"));
    Room room;
    room.name = QStringLiteral(" room ");
    room.level = -1;
    room.rects.append(QVector<int>{1, 2, 3, 4});
    room.objects.append(QVector<int>{0, 1, 2});
    header.rooms.append(room);
    header.buildings.append(QVector<int>{0});
    for (int index = 0; index < ChunkCount; ++index)
        header.density.append(quint8((index * 17 + index / CellSide) % 256));
    QVector<Chunk> chunks(ChunkCount);
    Square square;
    square.roomId = 0;
    square.tileIds.append(0);
    chunks.first().insert(0, square);
    chunks.last().insert(ChunkSide * ChunkSide * 3 - 1, square);
    QVector<ChunkData> chunkData(ChunkCount);
    for (int index = 0; index < ChunkCount; ++index) {
        chunkData[index].type = index % 5;
        if (chunkData[index].type == 2) {
            for (int position = 0; position < ChunkSide * ChunkSide; ++position)
                chunkData[index].bits.append(quint8((index + position) % 32));
        }
    }
    const QStringList names = {headerFileName(-2, 7), lotpackFileName(-2, 7),
                               chunkDataFileName(-2, 7)};
    const QVector<QByteArray> original = {writeHeader(header),
                                         writeLotpack(header, chunks),
                                         writeChunkData(chunkData)};
    for (int index = 0; index < names.size(); ++index) {
        if (!writeFile(directory.filePath(names.at(index)), original.at(index), error))
            return false;
    }
    const auto unchanged = [&]() {
        for (int index = 0; index < names.size(); ++index) {
            QByteArray bytes;
            if (!readFile(directory.filePath(names.at(index)), &bytes, error)
                    || bytes != original.at(index))
                return false;
        }
        return true;
    };
    if (!exportCell(directory.path(), -2, 7, error)
            || !importCell(directory.path(), -2, 7, error) || !unchanged()) {
        if (error->isEmpty())
            *error = QStringLiteral("LOT source round trip changed binary data");
        return false;
    }
    for (const QString &name : names) {
        const QString path = directory.filePath(name + QStringLiteral(".source"));
        QByteArray text;
        if (!readFile(path, &text, error)
                || !writeFile(path, text.replace("\n", "\r\n"), error))
            return false;
    }
    if (!importCell(directory.path(), -2, 7, error) || !unchanged()) {
        if (error->isEmpty())
            *error = QStringLiteral("CRLF source text changed binary data");
        return false;
    }
    QVector<QByteArray> sourceText(3);
    QVector<QDateTime> timestamps;
    for (int index = 0; index < names.size(); ++index) {
        if (!readFile(directory.filePath(names.at(index) + QStringLiteral(".source")),
                      &sourceText[index], error))
            return false;
        QFile file(directory.filePath(names.at(index)));
        if (!file.open(QIODevice::ReadWrite)
                || !file.setFileTime(QDateTime::fromSecsSinceEpoch(1600000000),
                                     QFileDevice::FileModificationTime))
            return false;
        file.close();
        timestamps.append(QFileInfo(file).lastModified());
    }
    int cells = 0;
    if (!processDirectory(directory.path(), true, &cells, error) || cells != 1)
        return false;
    for (int index = 0; index < names.size(); ++index) {
        if (QFileInfo(directory.filePath(names.at(index))).lastModified() != timestamps.at(index)) {
            *error = QStringLiteral("Unchanged LOT import rewrote a binary file");
            return false;
        }
    }
    QByteArray editedHeader = sourceText.first();
    editedHeader.replace("name =  room ", "name = edited room");
    if (!writeFile(directory.filePath(names.first() + QStringLiteral(".source")),
                   editedHeader, error)
            || !processDirectory(directory.path(), true, nullptr, error, true)
            || !unchanged()) {
        *error = QStringLiteral("Read-only LOT source checking changed binary data");
        return false;
    }
    const QStringList secondNames = cellFileNames(0, 0, true);
    for (int index = 0; index < secondNames.size(); ++index) {
        if (!writeFile(directory.filePath(secondNames.at(index)),
                       index == 1 ? QByteArray("invalid\n") : sourceText.at(index), error))
            return false;
    }
    if (processDirectory(directory.path(), true, &cells, error)
            || cells != 0 || !unchanged()
            || !error->contains(secondNames.at(1) + QStringLiteral(":1:"))) {
        *error = QStringLiteral("Invalid later-cell text changed earlier binaries or lost its location");
        return false;
    }
    const QString headerSourcePath = directory.filePath(names.first() + QStringLiteral(".source"));
    const QString lotpackSourcePath = directory.filePath(names.at(1) + QStringLiteral(".source"));
    if (!writeFile(headerSourcePath, sourceText.first(), error))
        return false;
    QByteArray overflow = sourceText.at(1);
    overflow.replace("-1,0,0 = 0", "-1,0,0 = 999999999999999999999");
    if (!writeFile(lotpackSourcePath, overflow, error)
            || importCell(directory.path(), -2, 7, error) || !unchanged()
            || !error->contains(names.at(1) + QStringLiteral(".source:"))) {
        *error = QStringLiteral("Out-of-range LOT source numbers were accepted");
        return false;
    }
    if (!writeFile(lotpackSourcePath, sourceText.at(1), error)
            || !writeFile(headerSourcePath, QByteArray(1, char(0xff)) + sourceText.first(), error)
            || importCell(directory.path(), -2, 7, error) || !unchanged()
            || !error->contains(QStringLiteral("UTF-8"))) {
        *error = QStringLiteral("Malformed UTF-8 LOT text was accepted");
        return false;
    }
    if (!writeFile(headerSourcePath, QByteArray::fromHex("efbbbf") + sourceText.first(), error)
            || !importCell(directory.path(), -2, 7, error) || !unchanged()) {
        *error = QStringLiteral("UTF-8 BOM LOT text changed binary data");
        return false;
    }
    QTemporaryDir incomplete;
    if (!incomplete.isValid()
            || !writeFile(QDir(incomplete.path()).filePath(secondNames.at(1)), sourceText.at(1), error)
            || processDirectory(incomplete.path(), true, &cells, error, true)
            || !error->contains(secondNames.first())) {
        *error = QStringLiteral("Orphan LOT source companions were ignored");
        return false;
    }
    QTemporaryDir blocked;
    if (!blocked.isValid())
        return false;
    QDir blockedDir(blocked.path());
    if (!writeFile(blockedDir.filePath(names.first()), original.first(), error)
            || !blockedDir.mkdir(names.at(1)))
        return false;
    QVector<QByteArray> replacement = original;
    replacement[0].append('!');
    QByteArray retained;
    if (writeFiles(blockedDir, names, replacement, error)
            || !readFile(blockedDir.filePath(names.first()), &retained, error)
            || retained != original.first()) {
        *error = QStringLiteral("Preparing a blocked LOT triplet changed an earlier file");
        return false;
    }
    if (!writeFile(directory.filePath(names.first() + QStringLiteral(".source")),
                   QByteArray("invalid\n"), error))
        return false;
    if (importCell(directory.path(), -2, 7, error) || !unchanged()) {
        *error = QStringLiteral("Invalid LOT source text changed binary data");
        return false;
    }
    error->clear();
    return true;
}
