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

static QString ReadString(QDataStream &in)
{
    QString str;
    quint8 c = ' ';
    while (c != '\n') {
        in >> c;
        if (in.status() != QDataStream::Status::Ok) {
            qDebug() << "ReadString read past end of file?";
            break;
        }
        if (c != '\n')
            str += QLatin1Char(c);
    }
    return str;
}

#define VERSION0 0
#define VERSION1 1
#define VERSION_LATEST VERSION1

bool TileDefFile::read(const QString &fileName)
{
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

    int numTilesets;
    in >> numTilesets;
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
        ts->mName = ReadString(in);
        ts->mImageSource = ReadString(in); // no path, just file + extension
        qint32 columns, rows;
        in >> columns;
        in >> rows;

        qint32 id = i + 1;
        if (version > VERSION0)
            in >> id;

        qint32 tileCount;
        in >> tileCount;
        const qint64 imageTileCount = qint64(columns) * qint64(rows);
        if (columns < 0 || rows < 0 || imageTileCount > 1024 * 1024
                || tileCount < 0 || tileCount > imageTileCount
                || (gameVersion1 && (id < 1 || id > maxTilesets
                                     || tileCount > maxTiles))) {
            mError = tr("Invalid dimensions, ID, or tile count in tileset "
                        "\"%1\" (%2x%3, ID %4, %5 tiles).\n%6")
                    .arg(ts->mName).arg(columns).arg(rows).arg(id)
                    .arg(tileCount).arg(fileName);
            delete ts;
            return false;
        }

        ts->mColumns = columns;
        ts->mRows = rows;
        ts->mID = id;

        QVector<TileDefTile*> tiles(columns * rows);
        for (int j = 0; j < tileCount; j++) {
            TileDefTile *tile = new TileDefTile(ts, j);
            qint32 numProperties;
            in >> numProperties;
            QMap<QString,QString> properties;
            for (int k = 0; k < numProperties; k++) {
                QString propertyName = ReadString(in);
                QString propertyValue = ReadString(in);
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
    mRows = ts->tileCount() / mColumns;
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
