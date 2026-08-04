/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "tiledeffile.h"

#include "tiledeftextfile.h"

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>

#if defined(Q_OS_WIN) && (_MSC_VER >= 1600)
// Hmmmm.  libtiled.dll defines the Properties class as so:
// class TILEDSHARED_EXPORT Properties : public QMap<QString,QString>
// Suddenly I'm getting a 'multiply-defined symbol' error.
// I found the solution here:
// http://www.archivum.info/qt-interest@trolltech.com/2005-12/00242/RE-Linker-Problem-while-using-QMap.html
template class __declspec(dllimport) QMap<QString, QString>;
#endif

TileDefFile::TileDefFile()
{
}

TileDefFile::~TileDefFile()
{
    qDeleteAll(mTilesets);
}

static const int MAX_TILEDEF_STRING_LENGTH = 1024 * 1024;

static bool ReadString(QDataStream &in, QString *value)
{
    value->clear();
    while (value->size() < MAX_TILEDEF_STRING_LENGTH) {
        quint8 c = 0;
        in >> c;
        if (in.status() != QDataStream::Ok)
            return false;
        if (c == '\n')
            return true;
        *value += QLatin1Char(c);
    }
    return false;
}

#define VERSION0 0
#define VERSION1 1
#define VERSION_LATEST VERSION1

bool TileDefFile::read(const QString &fileName)
{
    mError.clear();
    mFileName.clear();
    qDeleteAll(mTilesets);
    mTilesets.clear();
    mTilesetByName.clear();

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        mError = tr("Error opening file for reading.\n%1").arg(fileName);
        return false;
    }

    QDir dir = QFileInfo(fileName).absoluteDir();

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    quint8 tdef[4] = {0};
    in >> tdef[0];
    in >> tdef[1];
    in >> tdef[2];
    in >> tdef[3];
    if (in.status() != QDataStream::Ok) {
        mError = tr("The tile-definition header is truncated or unreadable.\n"
                    "File: %1").arg(QDir::toNativeSeparators(fileName));
        return false;
    }
    int version = VERSION0;
    if (memcmp(tdef, "tdef", 4) == 0) {
        in >> version;
        if (version < 0 || version > VERSION_LATEST) {
            mError = tr("Unknown version number %1 in .tiles file.\n%2")
                    .arg(version).arg(fileName);
            return false;
        }
    } else
        file.seek(0);

    qint32 numTilesets = 0;
    in >> numTilesets;
    if (in.status() != QDataStream::Ok) {
        mError = tr("The tile-definition file ends before the tileset count "
                    "can be read.\nFile: %1")
                .arg(QDir::toNativeSeparators(fileName));
        return false;
    }
    const bool gameVersion1 = version == VERSION1;
    const bool primaryDefinitions = QFileInfo(fileName).fileName().compare(
                QLatin1String("newtiledefinitions.tiles"),
                Qt::CaseInsensitive) == 0;
    const int maxTilesets = primaryDefinitions ? 1024 : 512;
    const int maxTiles = primaryDefinitions ? 1024 : 512;
    if (gameVersion1 && (numTilesets < 0 || numTilesets > maxTilesets)) {
        mError = tr("Invalid number of tilesets %1 (expected 0-%2).\n%3")
                .arg(numTilesets).arg(maxTilesets).arg(fileName);
        return false;
    }
    for (int i = 0; i < numTilesets; i++) {
        TileDefTileset *ts = new TileDefTileset;
        if (!ReadString(in, &ts->mName) ||
                !ReadString(in, &ts->mImageSource)) {
            mError = tr("Tileset entry %1 of %2 has a truncated name or "
                        "image-source field. A valid field must end with a "
                        "newline and be shorter than %3 bytes.\nFile: %4")
                    .arg(i + 1).arg(numTilesets)
                    .arg(MAX_TILEDEF_STRING_LENGTH)
                    .arg(QDir::toNativeSeparators(fileName));
            delete ts;
            return false;
        }
        qint32 columns = 0;
        qint32 rows = 0;
        in >> columns;
        in >> rows;

        qint32 id = i + 1;
        if (version > VERSION0)
            in >> id;

        qint32 tileCount = 0;
        in >> tileCount;
        if (in.status() != QDataStream::Ok) {
            mError = tr("Tileset \"%1\" (entry %2 of %3) has truncated "
                        "numeric metadata. Expected columns, rows, %4and tile "
                        "count after the image-source field.\nFile: %5")
                    .arg(ts->mName).arg(i + 1).arg(numTilesets)
                    .arg(version > VERSION0 ? tr("ID, ") : QString())
                    .arg(QDir::toNativeSeparators(fileName));
            delete ts;
            return false;
        }
        const qint64 imageTileCount = qint64(columns) * qint64(rows);
        QStringList problems;
        if (columns < 0) {
            problems += tr("Column count is %1; it must be zero or greater.")
                    .arg(columns);
        }
        if (rows < 0) {
            problems += tr("Row count is %1; it must be zero or greater.")
                    .arg(rows);
        }
        if (imageTileCount > 1024 * 1024) {
            problems += tr("The %1 x %2 grid contains %3 slots; the safe "
                           "per-tileset limit is %4.")
                    .arg(columns).arg(rows).arg(imageTileCount)
                    .arg(1024 * 1024);
        }
        if (tileCount < 0) {
            problems += tr("Stored tile count is %1; it must be zero or "
                           "greater.").arg(tileCount);
        }
        if (columns >= 0 && rows >= 0 &&
                tileCount > imageTileCount) {
            problems += tr("Stored tile count %1 exceeds the grid capacity "
                           "of %2 (%3 columns x %4 rows).")
                    .arg(tileCount).arg(imageTileCount)
                    .arg(columns).arg(rows);
        }
        if (gameVersion1 && (id < 1 || id > maxTilesets)) {
            problems += tr("Tileset ID is %1; this version-1 file requires "
                           "an ID from 1 to %2.")
                    .arg(id).arg(maxTilesets);
        }
        if (gameVersion1 && tileCount > maxTiles) {
            problems += tr("Stored tile count is %1; this version-1 file "
                           "allows at most %2 tile records per tileset.")
                    .arg(tileCount).arg(maxTiles);
        }
        if (!problems.isEmpty()) {
            mError = tr(
                        "Cannot load tileset \"%1\" (entry %2 of %3).\n"
                        "File: %4\n\n"
                        "Values stored in the .tiles file:\n"
                        "  Grid: %5 columns x %6 rows = %7 tile slots\n"
                        "  Tileset ID: %8\n"
                        "  Stored tile records: %9\n"
                        "  File format: version %10\n\n"
                        "Invalid value(s):\n- %11\n\n"
                        "For this file, version-1 tileset IDs must be 1-%12 "
                        "and each tileset may contain at most %13 stored tile "
                        "records. The stored count also cannot exceed the "
                        "grid capacity (columns x rows). These values come "
                        "from the binary .tiles file, not from the current "
                        "PNG dimensions. Re-export the definition from its "
                        "original source, or open it in TileZed's Tile "
                        "Definitions editor and use File > Repair / Split "
                        "for B42 Mods when repair loading is possible.")
                    .arg(ts->mName)
                    .arg(i + 1).arg(numTilesets)
                    .arg(QDir::toNativeSeparators(fileName))
                    .arg(columns).arg(rows).arg(imageTileCount)
                    .arg(id).arg(tileCount).arg(version)
                    .arg(problems.join(QStringLiteral("\n- ")))
                    .arg(maxTilesets).arg(maxTiles);
            delete ts;
            return false;
        }

        ts->mColumns = columns;
        ts->mRows = rows;
        ts->mID = id;

        QVector<TileDefTile*> tiles(columns * rows);
        for (int j = 0; j < tileCount; j++) {
            TileDefTile *tile = new TileDefTile(ts, j);
            qint32 numProperties = 0;
            in >> numProperties;
            if (in.status() != QDataStream::Ok) {
                mError = tr("Tile record %1 of %2 in tileset \"%3\" has a "
                            "truncated property-count field.\nFile: %4")
                        .arg(j + 1).arg(tileCount).arg(ts->mName)
                        .arg(QDir::toNativeSeparators(fileName));
                delete tile;
                qDeleteAll(tiles);
                delete ts;
                return false;
            }
            if (numProperties < 0 ||
                    numProperties > 1024 * 1024) {
                mError = tr("Tile record %1 of %2 in tileset \"%3\" stores "
                            "an invalid property count of %4 (expected "
                            "0-%5).\nFile: %6")
                        .arg(j + 1).arg(tileCount).arg(ts->mName)
                        .arg(numProperties).arg(1024 * 1024)
                        .arg(QDir::toNativeSeparators(fileName));
                delete tile;
                qDeleteAll(tiles);
                delete ts;
                return false;
            }
            QMap<QString,QString> properties;
            for (int k = 0; k < numProperties; k++) {
                QString propertyName;
                QString propertyValue;
                if (!ReadString(in, &propertyName) ||
                        !ReadString(in, &propertyValue)) {
                    mError = tr("Property %1 of %2 in tile record %3 of %4, "
                                "tileset \"%5\", has a truncated name or "
                                "value. Each field must end with a newline "
                                "and be shorter than %6 bytes.\nFile: %7")
                            .arg(k + 1).arg(numProperties)
                            .arg(j + 1).arg(tileCount).arg(ts->mName)
                            .arg(MAX_TILEDEF_STRING_LENGTH)
                            .arg(QDir::toNativeSeparators(fileName));
                    delete tile;
                    qDeleteAll(tiles);
                    delete ts;
                    return false;
                }
                properties[propertyName] = propertyValue;
            }
#ifndef WORLDED
            TilePropertyMgr::instance()->modify(properties);
            tile->mPropertyUI.FromProperties(properties);
#endif
            tile->mProperties = properties;
            tiles[j] = tile;
        }
        for (int j = tileCount; j < tiles.size(); j++) {
            tiles[j] = new TileDefTile(ts, j);
        }
        ts->mTiles = tiles;
#ifndef WORLDED
        // Deal with the image being a different size now than it was when the
        // .tiles file was saved.
        {
            QImageReader bmp(dir.filePath(ts->mImageSource));
            if (bmp.size().isValid()) {
                int columns = bmp.size().width() / 64;
                int rows = bmp.size().height() / 128;
                ts->resize(columns, rows);
            }
        }
#endif

        insertTileset(mTilesets.size(), ts);
    }

    mFileName = fileName;

    return true;
}

static void SaveString(QDataStream& out, const QString& str)
{
    for (int i = 0; i < str.length(); i++)
        out << quint8(str[i].toLatin1());
    out << quint8('\n');
}

bool TileDefFile::write(const QString &fileName)
{
#ifndef WORLDED
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        mError = tr("Error opening file for writing.\n%1").arg(fileName);
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out << quint8('t') << quint8('d') << quint8('e') << quint8('f');
    out << qint32(VERSION_LATEST);

    out << qint32(mTilesets.size());
    foreach (TileDefTileset *ts, mTilesets) {
        SaveString(out, ts->mName);
        SaveString(out, ts->mImageSource); // no path, just file + extension
        out << qint32(ts->mColumns);
        out << qint32(ts->mRows);
        out << qint32(ts->mID);
        out << qint32(ts->mTiles.size());
        foreach (TileDefTile *tile, ts->mTiles) {
            QMap<QString,QString> &properties = tile->mProperties;
            tile->mPropertyUI.ToProperties(properties);
            out << qint32(properties.size());
            foreach (QString key, properties.keys()) {
                SaveString(out, key);
                SaveString(out, properties[key]);
            }
        }
    }
#endif
    return true;
}

QString TileDefFile::directory() const
{
    return QFileInfo(mFileName).absolutePath();
}

void TileDefFile::insertTileset(int index, TileDefTileset *ts)
{
    Q_ASSERT(!mTilesets.contains(ts));
    Q_ASSERT(!mTilesetByName.contains(ts->mName));
    mTilesets.insert(index, ts);
    mTilesetByName[ts->mName] = ts;
}

TileDefTileset *TileDefFile::removeTileset(int index)
{
    mTilesetByName.remove(mTilesets[index]->mName);
    return mTilesets.takeAt(index);
}

TileDefTileset *TileDefFile::tileset(const QString &name) const
{
    if (mTilesetByName.contains(name))
        return mTilesetByName[name];
    return 0;
}

QList<TileDefTileset *> TileDefFile::takeTilesets()
{
    QList<TileDefTileset*> tilesets = mTilesets;
    mTilesets.clear();
    mTilesetByName.clear();
    return tilesets;
}

int TileDefFile::mergePropertiesFrom(const TileDefFile &overlay)
{
    int mergedTiles = 0;
    for (TileDefTileset *overlayTileset : overlay.tilesets()) {
        TileDefTileset *baseTileset = tileset(overlayTileset->mName);
        if (!baseTileset)
            continue;
        const int count = qMin(baseTileset->mTiles.size(),
                               overlayTileset->mTiles.size());
        for (int i = 0; i < count; ++i) {
            TileDefTile *overlayTile = overlayTileset->mTiles.at(i);
            if (overlayTile->mProperties.isEmpty())
                continue;
            TileDefTile *baseTile = baseTileset->mTiles.at(i);
            for (auto it = overlayTile->mProperties.cbegin();
                 it != overlayTile->mProperties.cend(); ++it) {
                baseTile->mProperties[it.key()] = it.value();
            }
            ++mergedTiles;
        }
    }
    return mergedTiles;
}

/////

#ifndef WORLDED
TileDefTileset::TileDefTileset(Tileset *ts) :
    mID(0)
{
    mName = ts->name();
    mImageSource = QFileInfo(ts->imageSource()).fileName();
    mColumns = ts->columnCount();
    mRows = mColumns > 0 ? ts->tileCount() / mColumns : 0;
    mTiles.resize(ts->tileCount());
    for (int i = 0; i < mTiles.size(); i++)
        mTiles[i] = new TileDefTile(this, i);
}
#endif

TileDefTileset::TileDefTileset() :
    mID(0)
{
}

TileDefTileset::~TileDefTileset()
{
    qDeleteAll(mTiles);
}

TileDefTile *TileDefTileset::tileAt(int index)
{
    if (index >= 0 && index < mTiles.size())
        return mTiles.at(index);
    return nullptr;
}

void TileDefTileset::resize(int columns, int rows)
{
    if (columns <= 0 || rows <= 0)
        return;

    if (columns == mColumns && rows == mRows)
        return;

    int oldColumns = mColumns;
    int oldRows = mRows;
    QVector<TileDefTile*> oldTiles = mTiles;

    mColumns = columns;
    mRows = rows;
    mTiles.resize(mColumns * mRows);
    for (int y = 0; y < qMin(mRows, oldRows); y++) {
        for (int x = 0; x < qMin(mColumns, oldColumns); x++) {
            mTiles[x + y * mColumns] = oldTiles[x + y * oldColumns];
            oldTiles[x + y * oldColumns] = 0;
        }
    }
    for (int i = 0; i < mTiles.size(); i++) {
        if (!mTiles[i]) {
            mTiles[i] = new TileDefTile(this, i);
        }
    }
    qDeleteAll(oldTiles);
}

///// ///// ///// ///// /////

bool TileDefFileReader::read(const QString &fileName, TileDefFile &defFile)
{
    qDeleteAll(defFile.takeTilesets());

    if (fileName.endsWith(QLatin1String(".txt"))) {
        Tiled::Internal::TileDefTextFile textFile;
        if (textFile.read(fileName) == false) {
            defFile.setErrorString(textFile.errorString());
            return false;
        }
        for (TileDefTileset *tileset : textFile.takeTilesets()) {
            defFile.insertTileset(defFile.tilesets().size(), tileset);
        }
        defFile.setFileName(fileName.mid(0, fileName.length() - 4));
        return true;
    }
    if (!defFile.read(fileName)) {
        return false;
    }
    return true;
}
