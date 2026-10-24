/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "lotsourcefiles.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QVector>

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
    explicit Lines(const QString &text)
        : mLines(text.split(QLatin1Char('\n')))
    {
    }

    QString next()
    {
        while (mIndex < mLines.size()) {
            const QString line = mLines.at(mIndex++).trimmed();
            if (!line.isEmpty())
                return line;
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

    bool value(const QString &key, QString *result)
    {
        const QString line = next();
        const int equal = line.indexOf(QLatin1Char('='));
        if (equal < 0 || line.left(equal).trimmed() != key)
            return false;
        *result = line.mid(equal + 1).trimmed();
        return true;
    }

private:
    QStringList mLines;
    int mIndex = 0;
};

bool readFile(const QString &path, QByteArray *data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString() + QStringLiteral(": ") + path;
        return false;
    }
    *data = file.readAll();
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
            || minLevel > maxLevel || maxLevel - minLevel > 31
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

bool parseHeader(const QString &text, Header *header)
{
    Lines lines(text);
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
            || header->maxLevel - header->minLevel > 31
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
                || !lines.value(QStringLiteral("name"), &room.name)
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
                  QVector<Chunk> *chunks)
{
    Lines lines(text);
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
            const int z = match.captured(1).toInt();
            const int x = match.captured(2).toInt();
            const int y = match.captured(3).toInt();
            if (z < minLevel || z > maxLevel
                    || x < 0 || x >= ChunkSide
                    || y < 0 || y >= ChunkSide)
                return false;
            const int position = (z - minLevel) * ChunkSide * ChunkSide
                    + x * ChunkSide + y;
            if (chunk.contains(position))
                return false;
            Square square;
            square.roomId = match.captured(4).toInt();
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
                    QVector<ChunkData> *chunks)
{
    Lines lines(text);
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

}

bool LotSourceFiles::exportCell(const QString &directory, int cellX,
                                int cellY, QString *error)
{
    const QDir output(directory);
    QByteArray headerBytes;
    QByteArray lotpackBytes;
    QByteArray chunkDataBytes;
    if (!readFile(output.filePath(headerFileName(cellX, cellY)),
                  &headerBytes, error)
            || !readFile(output.filePath(lotpackFileName(cellX, cellY)),
                         &lotpackBytes, error)
            || !readFile(output.filePath(chunkDataFileName(cellX, cellY)),
                         &chunkDataBytes, error))
        return false;
    Header header;
    QVector<Chunk> lotpack;
    QVector<ChunkData> chunkData;
    if (!readHeader(headerBytes, &header)
            || !readLotpack(lotpackBytes, header, &lotpack)
            || !readChunkData(chunkDataBytes, &chunkData)) {
        *error = QStringLiteral("Generated LOT files are invalid or unsupported");
        return false;
    }
    return writeFile(output.filePath(headerFileName(cellX, cellY)
                                     + QStringLiteral(".source")),
                     headerText(header).toUtf8(), error)
            && writeFile(output.filePath(lotpackFileName(cellX, cellY)
                                          + QStringLiteral(".source")),
                         lotpackText(header, lotpack).toUtf8(), error)
            && writeFile(output.filePath(chunkDataFileName(cellX, cellY)
                                          + QStringLiteral(".source")),
                         chunkDataText(chunkData).toUtf8(), error);
}

bool LotSourceFiles::importCell(const QString &directory, int cellX,
                                int cellY, QString *error)
{
    const QDir output(directory);
    QByteArray headerSource;
    QByteArray lotpackSource;
    QByteArray chunkDataSource;
    if (!readFile(output.filePath(headerFileName(cellX, cellY)
                                  + QStringLiteral(".source")),
                  &headerSource, error)
            || !readFile(output.filePath(lotpackFileName(cellX, cellY)
                                          + QStringLiteral(".source")),
                         &lotpackSource, error)
            || !readFile(output.filePath(chunkDataFileName(cellX, cellY)
                                          + QStringLiteral(".source")),
                         &chunkDataSource, error))
        return false;
    Header header;
    QVector<Chunk> lotpack;
    QVector<ChunkData> chunkData;
    if (!parseHeader(QString::fromUtf8(headerSource), &header)
            || !parseLotpack(QString::fromUtf8(lotpackSource),
                             header, &lotpack)
            || !parseChunkData(QString::fromUtf8(chunkDataSource),
                               &chunkData)) {
        *error = QStringLiteral("LOT source text is invalid or incompatible");
        return false;
    }
    const QByteArray headerBytes = writeHeader(header);
    const QByteArray lotpackBytes = writeLotpack(header, lotpack);
    const QByteArray chunkDataBytes = writeChunkData(chunkData);
    if (headerBytes.isEmpty() || lotpackBytes.isEmpty()
            || chunkDataBytes.isEmpty()) {
        *error = QStringLiteral("LOT source text could not be encoded");
        return false;
    }
    return writeFile(output.filePath(headerFileName(cellX, cellY)),
                     headerBytes, error)
            && writeFile(output.filePath(lotpackFileName(cellX, cellY)),
                         lotpackBytes, error)
            && writeFile(output.filePath(chunkDataFileName(cellX, cellY)),
                         chunkDataBytes, error);
}

bool LotSourceFiles::processDirectory(const QString &directory, bool import,
                                      int *cellCount, QString *error)
{
    if (cellCount)
        *cellCount = 0;
    const QDir folder(directory);
    const QString suffix = import
            ? QStringLiteral(".lotheader.source")
            : QStringLiteral(".lotheader");
    const QRegularExpression pattern(import
            ? QStringLiteral("^(-?\\d+)_(-?\\d+)\\.lotheader\\.source$")
            : QStringLiteral("^(-?\\d+)_(-?\\d+)\\.lotheader$"));
    const QStringList files = folder.entryList(
                QStringList(QStringLiteral("*") + suffix),
                QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const QRegularExpressionMatch match = pattern.match(name);
        if (!match.hasMatch())
            continue;
        const int x = match.captured(1).toInt();
        const int y = match.captured(2).toInt();
        if (!(import ? importCell(directory, x, y, error)
                     : exportCell(directory, x, y, error))) {
            *error = QStringLiteral("Cell %1,%2: %3")
                    .arg(x).arg(y).arg(*error);
            return false;
        }
        if (cellCount)
            ++*cellCount;
    }
    if (!cellCount || *cellCount == 0) {
        *error = QStringLiteral("No LOT files were found in the selected directory");
        return false;
    }
    return true;
}
