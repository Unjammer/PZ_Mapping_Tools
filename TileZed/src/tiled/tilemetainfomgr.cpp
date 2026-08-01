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

#include "tilemetainfomgr.h"

#include "preferences.h"
#include "tilesetmanager.h"
#include "tilesetstxtfile.h"
#include "zprogress.h"

#include "tile.h"
#include "tileset.h"

#include <QDir>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSignalBlocker>

using namespace Tiled;
using namespace Tiled::Internal;

static const char *TXT_FILE = "Tilesets.txt";

static int recoverSingleRowColumnCount(Tiled::Tileset *tileset)
{
    if (tileset->columnCount() > 0 || tileset->tileCount() <= 0)
        return tileset->columnCount();

    QString path1x;
    QString path2x;
    TilesetManager::instance()->getTilesetFileName(
                tileset->name(), path1x, path2x);

    QList<QPair<QString, int> > candidates;
    candidates += qMakePair(path2x, 2);
    candidates += qMakePair(path1x, 1);
    if (!tileset->imageSource2x().isEmpty())
        candidates += qMakePair(tileset->imageSource2x(), 2);
    if (!tileset->imageSource().isEmpty()) {
        candidates += qMakePair(tileset->imageSource(), 1);
        // Some imported 2x-only sheets temporarily retain their selected
        // 2x path as the logical image source.
        candidates += qMakePair(tileset->imageSource(), 2);
    }

    QSet<QString> checked;
    for (const QPair<QString, int> &candidate : std::as_const(candidates)) {
        const QString path = QDir::cleanPath(candidate.first);
        const QString key = path.toLower()
                + QLatin1Char('|') + QString::number(candidate.second);
        if (path.isEmpty() || checked.contains(key))
            continue;
        checked.insert(key);

        const QSize imageSize = QImageReader(path).size();
        const int scale = candidate.second;
        const int tileWidth = tileset->tileWidth() * scale;
        const int tileHeight = tileset->tileHeight() * scale;
        if (!imageSize.isValid() || tileWidth <= 0 || tileHeight <= 0)
            continue;

        const int columns =
                (imageSize.width() - tileset->margin()
                 + tileset->tileSpacing())
                / (tileWidth + tileset->tileSpacing());
        const int rows =
                (imageSize.height() - tileset->margin()
                 + tileset->tileSpacing())
                / (tileHeight + tileset->tileSpacing());
        if (rows == 1 && columns == tileset->tileCount()) {
            tileset->setColumnCount(columns);
            qInfo() << "Recovered single-row tileset geometry"
                    << tileset->name()
                    << columns << "columns, 1 row from"
                    << path << imageSize << "scale" << scale;
            return columns;
        }
    }

    return tileset->columnCount();
}

TileMetaInfoMgr* TileMetaInfoMgr::mInstance = nullptr;

TileMetaInfoMgr* TileMetaInfoMgr::instance()
{
    if (!mInstance)
        mInstance = new TileMetaInfoMgr;
    return mInstance;
}

void TileMetaInfoMgr::deleteInstance()
{
    delete mInstance;
    mInstance = nullptr;
}

void TileMetaInfoMgr::changeTilesDirectory(const QString &path)
{
    Preferences::instance()->setTilesDirectory(path); // must be done before loading tilesets
    TilesetManager::instance()->tilesetDirectoryChanged();
    for (Tileset *ts : tilesets())
        ts->setLoaded(false);
    resolveTilesets();
    loadTilesets(QList<Tileset *>(), false);
    TilesetManager::instance()->waitForTilesets(tilesets());
}

TileMetaInfoMgr::TileMetaInfoMgr(QObject *parent) :
    QObject(parent),
    mRevision(0),
    mSourceRevision(0),
    mHasReadTxt(false),
    mDiscoveringTilesets(false)
{
    connect(TilesetManager::instance(), &TilesetManager::tilesetChanged,
            this, &TileMetaInfoMgr::tilesetChanged);
    TilesetManager::instance()->tilesetDirectoryChanged();
}

TileMetaInfoMgr::~TileMetaInfoMgr()
{
    TilesetManager::instance()->removeReferences(tilesets());
    TilesetManager::instance()->removeReferences(mRemovedTilesets);
    qDeleteAll(mTilesetInfo);
}

QString TileMetaInfoMgr::tilesDirectory() const
{
    return Preferences::instance()->tilesDirectory();
}

QString TileMetaInfoMgr::tiles2xDirectory() const
{
    return Preferences::instance()->tiles2xDirectory();
}

QStringList TileMetaInfoMgr::tilesetNames() const
{
    QStringList ret;
    foreach (Tileset *ts, tilesets()) {
        ret += ts->name();
    }
    return ret;
}

QString TileMetaInfoMgr::txtName()
{
    return QLatin1String(TXT_FILE);
}

QString TileMetaInfoMgr::txtPath()
{
    return Preferences::instance()->configPath(txtName());
}

#if 1
bool TileMetaInfoMgr::readTxt()
{
#ifdef WORLDED
    // Make sure the user has chosen the Tiles directory.
    QString tilesDirectory = this->tilesDirectory();
    QDir dir(tilesDirectory);
    if (tilesDirectory.isEmpty() || !dir.exists()) {
        mError = tr("The Tiles directory specified in the preferences doesn't exist!\n%1")
                .arg(tilesDirectory);
        return false;
    }
#endif

    QFileInfo info(txtPath());
    if (!info.exists()) {
        mError = tr("The %1 file doesn't exist.").arg(txtName());
        return false;
    }

    if (!upgradeTxt())
        return false;

    if (!mergeTxt())
        return false;

    TilesetsTxtFile reader;
    if (!reader.read(txtPath())) {
        mError = reader.errorString();
        return false;
    }

    if (reader.mVersion != TilesetsTxtFile::VERSION_LATEST) {
        mError = tr("Expected %1 version %2, got %3")
                .arg(txtName()).arg(TilesetsTxtFile::VERSION_LATEST).arg(reader.mVersion);
        return false;
    }

    mRevision = reader.mRevision;
    mSourceRevision = reader.mSourceRevision;

    for (const TilesetsTxtFile::MetaEnum& metaEnum : std::as_const(reader.mEnums)) {
        mEnumNames += metaEnum.mName;
        mEnums.insert(metaEnum.mName, metaEnum.mValue);
    }

    qint64 sizeLookupNanoseconds = 0;
    qint64 constructionNanoseconds = 0;
    qint64 registrationNanoseconds = 0;
    qint64 metadataNanoseconds = 0;
    QElapsedTimer phaseTimer;
    // BuildingEd has several catalog views connected to tilesetAdded().
    // Rebuilding those views once for every one of the 500+ initial entries is
    // quadratic and used to add roughly a minute to startup. The views already
    // populate themselves once their mode/dialog is initialized, so suppress
    // only the initial bulk-registration notifications.
    {
        const QSignalBlocker bulkRegistrationSignals(this);
        for (const TilesetsTxtFile::Tileset* fileTileset : std::as_const(reader.mTilesets)) {
            phaseTimer.restart();
            QSize tilesetSize = Tiled::getZomboidTilesetSize1x(fileTileset->mName);
            sizeLookupNanoseconds += phaseTimer.nsecsElapsed();

            phaseTimer.restart();
            int tileWidth = tilesetSize.width();
            int tileHeight = tilesetSize.height();
            Tileset *tileset = new Tileset(fileTileset->mName, tileWidth, tileHeight);
            Tiled::setZomboidTileOffset(tileset);

            // Don't load the tilesets yet because the user might not have
            // chosen the Tiles directory. The tilesets will be loaded when
            // other code asks for them or when the Tiles directory is changed.
            int width = fileTileset->mColumns * tileWidth;
            int height = fileTileset->mRows * tileHeight;
            QString tilesetFileName = fileTileset->mFile + QLatin1String(".png");
            tileset->loadFromNothing(QSize(width, height), tilesetFileName);
            // This is an unresolved catalog entry, not a confirmed missing image.
            // Resolving every path and painting every placeholder here made startup
            // take close to a minute on slower drives.
            tileset->setMissing(false);
            constructionNanoseconds += phaseTimer.nsecsElapsed();

            phaseTimer.restart();
            addTileset(tileset);
            registrationNanoseconds += phaseTimer.nsecsElapsed();

            phaseTimer.restart();
            TilesetMetaInfo *info = new TilesetMetaInfo;
            for (const TilesetsTxtFile::Tile& fileTile : fileTileset->mTiles) {
                QString coordString = QString(QLatin1String("%1,%2")).arg(fileTile.mX).arg(fileTile.mY);
                info->mInfo[coordString].mMetaGameEnum = fileTile.mMetaEnum;
            }
            mTilesetInfo[fileTileset->mName] = info;
            metadataNanoseconds += phaseTimer.nsecsElapsed();
        }
    }
    qInfo().noquote()
            << "Tileset catalog registration timings (ms):"
            << "size lookup" << sizeLookupNanoseconds / 1000000
            << "construction" << constructionNanoseconds / 1000000
            << "registration" << registrationNanoseconds / 1000000
            << "metadata" << metadataNanoseconds / 1000000;

    for (const QString& enumName : std::as_const(mEnumNames)) {
        if (isEnumWest(enumName) || isEnumNorth(enumName)) {
            if (mEnums.values().contains(mEnums[enumName] + 1)) {
                QString enumImplicit = enumName;
                enumImplicit.replace(
                            QLatin1Char(isEnumWest(enumName) ? 'W' : 'N'),
                            QLatin1String(isEnumWest(enumName) ? "E" : "S"));
                mError = tr("Meta-enum %1=%2 requires an implicit %3=%4 but that value is used by %5=%6.")
                        .arg(enumName).arg(mEnums[enumName])
                        .arg(enumImplicit).arg(mEnums[enumName]+1)
                        .arg(mEnums.key(mEnums[enumName] + 1)).arg(mEnums[enumName] + 1);
                return false;
            }
        }
    }

    mHasReadTxt = true;
    emit tilesetCatalogLoaded();

    return true;
}

bool TileMetaInfoMgr::writeTxt()
{
    if (!writeTxt(txtPath(), mRevision + 1, mSourceRevision)) {
        return false;
    }
    ++mRevision;
    return true;
}

bool TileMetaInfoMgr::writeTxt(const QString &fileName, int revision, int sourceRevision)
{
    qInfo() << "Writing tileset catalog" << fileName
            << "revision" << revision
            << "entries" << tilesets().count();
    QList<TilesetsTxtFile::Tileset*> fileTilesets;
    QList<TilesetsTxtFile::MetaEnum> fileMetaEnums;

    for (const QString& name : std::as_const(mEnumNames)) {
        fileMetaEnums += TilesetsTxtFile::MetaEnum(name, mEnums[name]);
    }

    QDir tilesDir(tilesDirectory());
    for (Tiled::Tileset *tileset : tilesets()) {
        int columns = tileset->columnCount();
        const int tileCount = tileset->tileCount();
        if (columns <= 0)
            columns = recoverSingleRowColumnCount(tileset);
        if (columns <= 0 || tileCount <= 0
                || tileCount % columns != 0) {
            qDeleteAll(fileTilesets);
            mError = tr(
                        "Cannot save Tilesets.txt because tileset '%1' has "
                        "invalid geometry (%2 tiles, %3 columns).")
                    .arg(tileset->name()).arg(tileCount).arg(columns);
            qWarning() << "Tileset catalog geometry validation failed:"
                       << mError;
            return false;
        }

        QString relativePath = tilesDir.relativeFilePath(tileset->imageSource());
        if (!relativePath.endsWith(
                    QLatin1String(".png"), Qt::CaseInsensitive)) {
            qDeleteAll(fileTilesets);
            mError = tr(
                        "Cannot save Tilesets.txt because tileset '%1' does "
                        "not reference a PNG image:\n'%2'")
                    .arg(tileset->name(),
                         QDir::toNativeSeparators(tileset->imageSource()));
            qWarning() << "Tileset catalog image validation failed:"
                       << mError;
            return false;
        }
        relativePath.chop(4); // remove .png
        TilesetsTxtFile::Tileset* fileTileset = new TilesetsTxtFile::Tileset();
        fileTileset->mName = tileset->name();
        fileTileset->mFile = relativePath;

        const int rows = tileCount / columns;
        fileTileset->mColumns = columns;
        fileTileset->mRows = rows;

        if (mTilesetInfo.contains(tileset->name())) {
            QMap<QString,TileMetaInfo> &info = mTilesetInfo[tileset->name()]->mInfo;
            for (const QString& key : info.keys()) {
                Q_ASSERT(info[key].mMetaGameEnum.isEmpty() == false);
                if (info[key].mMetaGameEnum.isEmpty())
                    continue;
                TilesetsTxtFile::Tile fileTile;
                parse2Ints(key, &fileTile.mX, &fileTile.mY);
                fileTile.mMetaEnum = info[key].mMetaGameEnum;
                fileTileset->mTiles += fileTile;
            }
        }

        fileTilesets += fileTileset;
    }

    TilesetsTxtFile writer;
    if (!writer.write(fileName, revision, sourceRevision, fileTilesets, fileMetaEnums)) {
        mError = writer.errorString();
        qWarning() << "Tileset catalog writer failed:" << mError;
        return false;
    }

    qInfo() << "Finished writing tileset catalog" << fileName;
    return true;
}
#else
bool TileMetaInfoMgr::readTxt()
{
#ifdef WORLDED
    // Make sure the user has chosen the Tiles directory.
    QString tilesDirectory = this->tilesDirectory();
    QDir dir(tilesDirectory);
    if (tilesDirectory.isEmpty() || !dir.exists()) {
        mError = tr("The Tiles directory specified in the preferences doesn't exist!\n%1")
                .arg(tilesDirectory);
        return false;
    }
#endif

    QFileInfo info(txtPath());
    if (!info.exists()) {
        mError = tr("The %1 file doesn't exist.").arg(txtName());
        return false;
    }

    if (!upgradeTxt())
        return false;

    if (!mergeTxt())
        return false;

    QString path = info.canonicalFilePath();
    SimpleFile simple;
    if (!simple.read(path)) {
        mError = simple.errorString();
        return false;
    }

    if (simple.version() != VERSION_LATEST) {
        mError = tr("Expected %1 version %2, got %3")
                .arg(txtName()).arg(VERSION_LATEST).arg(simple.version());
        return false;
    }

    mRevision = simple.value("revision").toInt();
    mSourceRevision = simple.value("source_revision").toInt();

    foreach (SimpleFileBlock block, simple.blocks) {
        if (block.name == QLatin1String("meta-enums")) {
            foreach (SimpleFileKeyValue kv, block.values) {
                if (mEnums.contains(kv.name)) {
                    mError = tr("Duplicate enum %1");
                    return false;
                }
                if (kv.name.contains(QLatin1String(" "))) {
                    mError = tr("No spaces allowed in enum name '%1'").arg(kv.name);
                    return false;
                }
                bool ok;
                int value = kv.value.toInt(&ok);
                if (!ok || value < 0 || value > 255
                        || mEnums.values().contains(value)) {
                    mError = tr("Invalid or duplicate enum value %1 = %2")
                            .arg(kv.name).arg(kv.value);
                    return false;
                }
                mEnumNames += kv.name; // preserve order
                mEnums.insert(kv.name, value);
            }
        } else if (block.name == QLatin1String("tileset")) {
            QString tilesetFileName = block.value("file");
            if (tilesetFileName.isEmpty()) {
                mError = tr("No-name tilesets aren't allowed.");
                return false;
            }
            tilesetFileName += QLatin1String(".png");
            QFileInfo finfo(tilesetFileName); // relative to Tiles directory
            QString tilesetName = finfo.completeBaseName();
            if (mTilesetInfo.contains(tilesetName)) {
                mError = tr("Duplicate tileset '%1'.").arg(tilesetName);
                return false;
            }
            Tileset *tileset = new Tileset(tilesetName, 64, 128);
            {
                QString size = block.value("size");
                int columns, rows;
                if (!parse2Ints(size, &columns, &rows) ||
                        (columns < 1) || (rows < 1)) {
                    mError = tr("Invalid tileset size '%1' for tileset '%2'")
                            .arg(size).arg(tilesetName);
                    return false;
                }

                // Don't load the tilesets yet because the user might not have
                // chosen the Tiles directory. The tilesets will be loaded when
                // other code asks for them or when the Tiles directory is changed.
                int width = columns * 64, height = rows * 128;
                tileset->loadFromNothing(QSize(width, height), tilesetFileName);
                Tile *missingTile = TilesetManager::instance()->missingTile();
                for (int i = 0; i < tileset->tileCount(); i++)
                    tileset->tileAt(i)->setImage(missingTile);
                tileset->setMissing(true);
            }
            addTileset(tileset);

            TilesetMetaInfo *info = new TilesetMetaInfo;
            foreach (SimpleFileBlock tileBlock, block.blocks) {
                if (tileBlock.name == QLatin1String("tile")) {
                    QString coordString;
                    foreach (SimpleFileKeyValue kv, tileBlock.values) {
                        if (kv.name == QLatin1String("xy")) {
                            int column, row;
                            if (!parse2Ints(kv.value, &column, &row) ||
                                    (column < 0) || (row < 0)) {
                                mError = tr("Invalid %1 = %2").arg(kv.name).arg(kv.value);
                                return false;
                            }
                            coordString = kv.value;
                        } else if (kv.name == QLatin1String("meta-enum")) {
                            QString enumName = kv.value;
                            if (!mEnums.contains(enumName)) {
                                mError = tr("Unknown enum '%1'").arg(enumName);
                                return false;
                            }
                            Q_ASSERT(!coordString.isEmpty());
                            info->mInfo[coordString].mMetaGameEnum = enumName;
                        } else {
                            mError = tr("Unknown value name '%1'.").arg(kv.name);
                            return false;
                        }
                    }
                }
            }
            mTilesetInfo[tilesetName] = info;
        } else {
            mError = tr("Unknown block name '%1'.\n%2")
                    .arg(block.name)
                    .arg(path);
            return false;
        }
    }

    foreach (QString enumName, mEnumNames) {
        if (isEnumWest(enumName) || isEnumNorth(enumName)) {
            if (mEnums.values().contains(mEnums[enumName] + 1)) {
                QString enumImplicit = enumName;
                enumImplicit.replace(
                            QLatin1Char(isEnumWest(enumName) ? 'W' : 'N'),
                            QLatin1String(isEnumWest(enumName) ? "E" : "S"));
                mError = tr("Meta-enum %1=%2 requires an implicit %3=%4 but that value is used by %5=%6.")
                        .arg(enumName).arg(mEnums[enumName])
                        .arg(enumImplicit).arg(mEnums[enumName]+1)
                        .arg(mEnums.key(mEnums[enumName] + 1)).arg(mEnums[enumName] + 1);
                return false;
            }
        }
    }

    mHasReadTxt = true;

    return true;
}

bool TileMetaInfoMgr::writeTxt()
{
    SimpleFile simpleFile;

    SimpleFileBlock enumsBlock;
    enumsBlock.name = QLatin1String("meta-enums");
    foreach (QString name, mEnumNames) {
        enumsBlock.addValue(name, QString::number(mEnums[name]));
    }
    simpleFile.blocks += enumsBlock;

    QDir tilesDir(tilesDirectory());
    foreach (Tiled::Tileset *tileset, tilesets()) {
        SimpleFileBlock tilesetBlock;
        tilesetBlock.name = QLatin1String("tileset");

        QString relativePath = tilesDir.relativeFilePath(tileset->imageSource());
        relativePath.truncate(relativePath.length() - 4); // remove .png
        tilesetBlock.addValue("file", relativePath);

        int columns = tileset->columnCount();
        int rows = tileset->tileCount() / columns;
        if (tileset->isLoaded()) {
            columns = tileset->columnCountForWidth(tileset->imageWidth());
            rows = tileset->imageHeight() / (tileset->imageSource2x().isEmpty() ? 128 : (128 * 2));
        }
        tilesetBlock.addValue("size", QString(QLatin1String("%1,%2")).arg(columns).arg(rows));

        if (mTilesetInfo.contains(tileset->name())) {
            QMap<QString,TileMetaInfo> &info = mTilesetInfo[tileset->name()]->mInfo;
            foreach (QString key, info.keys()) {
                Q_ASSERT(info[key].mMetaGameEnum.isEmpty() == false);
                if (info[key].mMetaGameEnum.isEmpty())
                    continue;
                SimpleFileBlock tileBlock;
                tileBlock.name = QLatin1String("tile");
                tileBlock.addValue("xy", key);
                tileBlock.addValue("meta-enum", info[key].mMetaGameEnum);
                tilesetBlock.blocks += tileBlock;
            }
        }
        simpleFile.blocks += tilesetBlock;
    }

    simpleFile.setVersion(VERSION_LATEST);
    simpleFile.replaceValue("revision", QString::number(++mRevision));
    simpleFile.replaceValue("source_revision", QString::number(mSourceRevision));
    if (!simpleFile.write(txtPath())) {
        mError = simpleFile.errorString();
        return false;
    }
    return true;
}
#endif

bool TileMetaInfoMgr::upgradeTxt()
{
    return true;
}

bool TileMetaInfoMgr::mergeTxt()
{
#ifdef WORLDED
    // There isn't a source Tilesets.txt in WorldEd.
    return true;
#endif
    QString userPath = txtPath();

    QString sourcePath = Tiled::Internal::Preferences::instance()->appConfigPath(txtName());
    // In the portable layout the active catalog is also the application
    // catalog. Merging a file with itself would make source_revision trail
    // revision forever and rewrite the file on every startup.
    if (QFileInfo(userPath).canonicalFilePath()
            == QFileInfo(sourcePath).canonicalFilePath()) {
        return true;
    }

    TilesetsTxtFile sourceFileX;
    if (!sourceFileX.read(sourcePath)) {
        mError = sourceFileX.errorString();
        return false;
    }

    TilesetsTxtFile userFileX;
    if (!userFileX.read(userPath)) {
        mError = userFileX.errorString();
        return false;
    }

    int userSourceRevision = userFileX.mSourceRevision;
    int sourceRevision = sourceFileX.mRevision;
    if (sourceRevision == userSourceRevision) {
        return true;
    }

    // MERGE HERE

    // Overwrite all user-defined meta-enums.
    userFileX.mEnums = sourceFileX.mEnums;

    QSet<QString> enumNameSet;
    for (auto& metaEnum : userFileX.mEnums) {
        enumNameSet += metaEnum.mName;
    }

    for (auto& tilesetUser : userFileX.mTilesets) {
        QList<TilesetsTxtFile::Tile> userTiles = tilesetUser->mTiles;
        int tileIndex = 0;
        for (auto& tileUser : userTiles) {
            if (!enumNameSet.contains(tileUser.mMetaEnum)) {
                tilesetUser->mTiles.removeAt(tileIndex);
            } else {
                ++tileIndex;
            }
        }
    }

    QMap<QString,TilesetsTxtFile::Tileset*> userTilesetMap;
    for (auto& tilesetUser : userFileX.mTilesets) {
        userTilesetMap[tilesetUser->mName] = tilesetUser;
    }

    for (auto& tilesetSource : sourceFileX.mTilesets) {
        if (userTilesetMap.contains(tilesetSource->mName)) {
            TilesetsTxtFile::Tileset* userTileset = userTilesetMap[tilesetSource->mName];
            // Add missing tiles to the user file.
            for (auto& tileSource : tilesetSource->mTiles) {
                userTileset->setTile(tileSource);
            }
        }
    }

    if (!userFileX.write(userPath, sourceRevision + 1, sourceRevision, userFileX.mTilesets, userFileX.mEnums)) {
        mError = userFileX.errorString();
        return false;
    }

    return true;
}

bool TileMetaInfoMgr::addNewTilesets(bool loadImages)
{
    mDiscoveringTilesets = true;

    struct ImageCandidate {
        QString path;
        int scale = 1;
    };

    QMap<QString, ImageCandidate> images;
    auto scanDirectory = [&images](const QString &path, int scale,
                                   bool scanChildren) {
        QDir directory(path);
        if (!directory.exists())
            return;

        const QStringList filters = { QLatin1String("*.png") };
        auto addFiles = [&images, scale, &filters](const QDir &source) {
            const QFileInfoList files = source.entryInfoList(
                        filters, QDir::Files, QDir::Name);
            for (const QFileInfo &file : files) {
                const QString name = file.completeBaseName();
                const QString key = name.toCaseFolded();
                if (!images.contains(key)
                        || scale > images.value(key).scale) {
                    ImageCandidate candidate;
                    candidate.path = file.absoluteFilePath();
                    candidate.scale = scale;
                    images.insert(key, candidate);
                }
            }
        };

        addFiles(directory);
        if (!scanChildren)
            return;
        const QFileInfoList children = directory.entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children)
            addFiles(QDir(child.absoluteFilePath()));
    };

    const QString rootPath = tilesDirectory();
    scanDirectory(rootPath, 1, false);
    const QFileInfoList rootChildren = QDir(rootPath).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : rootChildren) {
        if (child.fileName().compare(
                    QLatin1String("2x"), Qt::CaseInsensitive) != 0) {
            scanDirectory(child.absoluteFilePath(), 1, false);
        }
    }
    scanDirectory(tiles2xDirectory(), 2, true);

    int addedCount = 0;
    for (auto it = images.constBegin(); it != images.constEnd(); ++it) {
        const QFileInfo imageInfo(it.value().path);
        const QString tilesetName = imageInfo.completeBaseName();
        if (tileset(tilesetName))
            continue;

        QImageReader reader(it.value().path);
        const QSize imageSize = reader.size();
        const QSize tileSize =
                Tiled::getZomboidTilesetSize1x(tilesetName);
        if (!imageSize.isValid()
                || imageSize.width() < tileSize.width() * it.value().scale
                || imageSize.height() < tileSize.height() * it.value().scale) {
            qWarning() << "Ignoring undersized tileset PNG"
                       << it.value().path << imageSize;
            continue;
        }

        Tileset *newTileset = nullptr;
        if (loadImages) {
            newTileset = loadTileset(it.value().path);
            if (!newTileset) {
                qWarning() << "Unable to discover tileset"
                           << it.value().path << mError;
                continue;
            }
        } else {
            newTileset = new Tileset(tilesetName,
                                     tileSize.width(),
                                     tileSize.height());
            Tiled::setZomboidTileOffset(newTileset);

            QString imageSource;
            QString imageSource2x;
            TilesetManager::instance()->getTilesetFileName(
                        tilesetName, imageSource, imageSource2x);
            const QSize logicalImageSize =
                    imageSize / it.value().scale;
            if (!newTileset->loadFromNothing(logicalImageSize,
                                             imageSource)) {
                qWarning() << "Unable to register tileset metadata"
                           << it.value().path << logicalImageSize;
                delete newTileset;
                continue;
            }
            if (it.value().scale == 2)
                newTileset->setImageSource2x(imageSource2x);
            else
                newTileset->setImageSource2x(QString());
            TilesetManager::instance()->changeTilesetSource(
                        newTileset, imageSource, false);
        }
        const bool quietBuildingEdStartup =
                !loadImages
                && QCoreApplication::applicationName().compare(
                    QLatin1String("BuildingEd"),
                    Qt::CaseInsensitive) == 0;
        if (quietBuildingEdStartup) {
            // BuildingEd creates several catalog views before the initial
            // PNG discovery pass. Emitting one tilesetAdded signal per sheet
            // makes those views resolve and decode each new image
            // immediately, only for Startup() to preload the complete
            // catalog again. Register the metadata silently and refresh the
            // tileset dock once through tilesetDiscoveryFinished().
            const QSignalBlocker blocker(this);
            addTileset(newTileset);
        } else {
            addTileset(newTileset);
        }
        mTilesetInfo[tilesetName] = new TilesetMetaInfo;
        ++addedCount;
    }

    if (addedCount > 0) {
        qInfo() << "Discovered new Tiles PNGs in memory:"
                << addedCount
                << "(Tilesets.txt is unchanged; use the explicit Update "
                   "Tilesets.txt command to persist them)";
    }
    mDiscoveringTilesets = false;
    if (addedCount > 0)
        emit tilesetDiscoveryFinished();
    return true;
}

Tileset *TileMetaInfoMgr::createTilesetFromTxtFile(TilesetsTxtFile::Tileset *fileTileset)
{
    QSize tilesetSize = Tiled::getZomboidTilesetSize1x(fileTileset->mName);
    int tileWidth = tilesetSize.width();
    int tileHeight = tilesetSize.height();
    Tileset *tileset = new Tileset(fileTileset->mName, tileWidth, tileHeight);
    Tiled::setZomboidTileOffset(tileset);

    // Don't load the tileset yet because the user might not have
    // chosen the Tiles directory. The tileset will be loaded when
    // other code asks for it or when the Tiles directory is changed.
    int width = fileTileset->mColumns * tileWidth;
    int height = fileTileset->mRows * tileHeight;
    QString tilesetFileName = fileTileset->mFile + QLatin1String(".png");
    tileset->loadFromNothing(QSize(width, height), tilesetFileName);
    Tile *missingTile = TilesetManager::instance()->missingTile();
    for (int i = 0; i < tileset->tileCount(); i++) {
        tileset->tileAt(i)->setImage(missingTile);
    }
    tileset->setMissing(true);
#if 0
    addTileset(tileset);

    TilesetMetaInfo *info = new TilesetMetaInfo;
    for (const TilesetsTxtFile::Tile& fileTile : std::as_const(fileTileset->mTiles)) {
        QString coordString = QStringLiteral("%1,%2").arg(fileTile.mX).arg(fileTile.mY);
        info->mInfo[coordString].mMetaGameEnum = fileTile.mMetaEnum;
    }
    mTilesetInfo[fileTileset->mName] = info;
#endif
    return tileset;
}

Tileset *TileMetaInfoMgr::loadTileset(const QString &source)
{
    qInfo() << "Loading tileset selected by user" << source;
    QFileInfo info(source);
    if (!info.exists() || !info.isFile()) {
        mError = tr("Tileset image does not exist:\n'%1'").arg(source);
        qWarning() << "Tileset add rejected:" << mError;
        return nullptr;
    }
    if (info.suffix().compare(
                QLatin1String("png"), Qt::CaseInsensitive) != 0) {
        mError = tr("Project Zomboid tilesets must be PNG images:\n'%1'")
                .arg(source);
        qWarning() << "Tileset add rejected:" << mError;
        return nullptr;
    }

    QSize tilesetSize = Tiled::getZomboidTilesetSize1x(info.completeBaseName());
    int tileWidth = tilesetSize.width();
    int tileHeight = tilesetSize.height();
    Tileset *ts = new Tileset(info.completeBaseName(), tileWidth, tileHeight);
    Tiled::setZomboidTileOffset(ts);
    if (!loadTilesetImage(ts, source)) {
        qWarning() << "Tileset image load failed:" << mError;
        delete ts;
        return nullptr;
    }
    qInfo() << "Tileset image ready" << ts->name()
            << ts->columnCount() << "columns"
            << ts->tileCount() << "tiles";
    return ts;
}

bool TileMetaInfoMgr::loadTilesetImage(Tileset *ts, const QString &source)
{
    QString selectedPath =
            QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    QString imageSource, imageSource2x;
    TilesetManager::instance()->getTilesetFileName(ts->name(), imageSource, imageSource2x);
    qInfo() << "Tileset path resolution" << ts->name()
            << "selected" << selectedPath
            << "resolved-1x" << imageSource
            << "resolved-2x" << imageSource2x;

    QString resolved1x =
            QDir::cleanPath(QFileInfo(imageSource).absoluteFilePath());
    QString resolved2x =
            QDir::cleanPath(QFileInfo(imageSource2x).absoluteFilePath());
    bool selectedIsKnown =
            selectedPath.compare(resolved1x, Qt::CaseInsensitive) == 0
            || selectedPath.compare(resolved2x, Qt::CaseInsensitive) == 0;
    if (!selectedIsKnown) {
        const QDir tilesRoot(tilesDirectory());
        if (!tilesRoot.exists()) {
            mError = tr("The configured Tiles folder does not exist:\n'%1'")
                    .arg(QDir::toNativeSeparators(tilesRoot.absolutePath()));
            qWarning() << "Tileset import failed:" << mError;
            return false;
        }

        // "Add Tilesets" historically allowed browsing to another Tiles
        // tree, but loadTilesetImage() then discarded the selected path and
        // searched only the active Tiles tree. Import the selected PNG into
        // the active tree so it can be reloaded on later launches.
        const QString normalized =
                QDir::fromNativeSeparators(selectedPath);
        const QStringList parts =
                normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        int scaleFolder = -1;
        for (int index = parts.count() - 2; index >= 0; --index) {
            if (parts.at(index).compare(
                        QLatin1String("2x"), Qt::CaseInsensitive) == 0
                    || parts.at(index).compare(
                        QLatin1String("1x"), Qt::CaseInsensitive) == 0) {
                scaleFolder = index;
                break;
            }
        }

        QString relativeDestination = QFileInfo(selectedPath).fileName();
        if (scaleFolder >= 0) {
            const bool is2x = parts.at(scaleFolder).compare(
                        QLatin1String("2x"), Qt::CaseInsensitive) == 0;
            QStringList suffix = parts.mid(scaleFolder + 1);
            if (is2x)
                suffix.prepend(QLatin1String("2x"));
            relativeDestination = suffix.join(QLatin1Char('/'));
        } else {
            const QString parentName =
                    QFileInfo(selectedPath).dir().dirName();
            if (parentName.endsWith(
                        QLatin1String(".pack"), Qt::CaseInsensitive)) {
                relativeDestination =
                        parentName + QLatin1Char('/')
                        + QFileInfo(selectedPath).fileName();
            }
        }

        const QString importedPath =
                QDir::cleanPath(tilesRoot.filePath(relativeDestination));
        const QString importedParent = QFileInfo(importedPath).absolutePath();
        if (!QDir().mkpath(importedParent)) {
            mError = tr("Unable to create the tileset import folder:\n'%1'")
                    .arg(QDir::toNativeSeparators(importedParent));
            qWarning() << "Tileset import failed:" << mError;
            return false;
        }

        if (!QFileInfo::exists(importedPath)) {
            if (!QFile::copy(selectedPath, importedPath)) {
                mError = tr(
                            "Unable to import tileset image.\n\n"
                            "From: '%1'\nTo: '%2'")
                        .arg(QDir::toNativeSeparators(selectedPath),
                             QDir::toNativeSeparators(importedPath));
                qWarning() << "Tileset import failed:" << mError;
                return false;
            }
            qInfo() << "Imported external tileset from"
                    << selectedPath << "to" << importedPath;
        } else {
            auto sha256 = [](const QString &path) {
                QFile file(path);
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (!file.open(QIODevice::ReadOnly)
                        || !hash.addData(&file)) {
                    return QByteArray();
                }
                return hash.result();
            };
            const QByteArray selectedHash = sha256(selectedPath);
            const QByteArray importedHash = sha256(importedPath);
            if (selectedHash.isEmpty()
                    || selectedHash != importedHash) {
                mError = tr(
                            "A different tileset image already exists in "
                            "the active Tiles folder.\n\n"
                            "Selected: '%1'\nExisting: '%2'\n\n"
                            "Rename the selected tileset or remove the "
                            "conflicting file first.")
                        .arg(QDir::toNativeSeparators(selectedPath),
                             QDir::toNativeSeparators(importedPath));
                qWarning() << "Tileset import conflict:" << mError;
                return false;
            }
            qInfo() << "Using existing imported tileset" << importedPath;
        }

        selectedPath = importedPath;
        TilesetManager::instance()->tilesetDirectoryChanged();
        TilesetManager::instance()->getTilesetFileName(
                    ts->name(), imageSource, imageSource2x);
        resolved1x =
                QDir::cleanPath(QFileInfo(imageSource).absoluteFilePath());
        resolved2x =
                QDir::cleanPath(QFileInfo(imageSource2x).absoluteFilePath());
        selectedIsKnown =
                selectedPath.compare(resolved1x, Qt::CaseInsensitive) == 0
                || selectedPath.compare(resolved2x, Qt::CaseInsensitive) == 0;
        if (!selectedIsKnown) {
            mError = tr(
                        "The tileset was imported, but its folder layout is "
                        "not supported:\n'%1'")
                    .arg(QDir::toNativeSeparators(importedPath));
            qWarning() << "Tileset import failed:" << mError;
            return false;
        }
    }

    auto validateGeometry = [this, ts](
            const QString &path, const QSize &imageSize, int scale) {
        const int scaledTileWidth = ts->tileWidth() * scale;
        const int scaledTileHeight = ts->tileHeight() * scale;
        if (!imageSize.isValid()
                || scaledTileWidth <= 0 || scaledTileHeight <= 0
                || imageSize.width() < scaledTileWidth
                || imageSize.height() < scaledTileHeight) {
            mError = tr(
                        "Invalid tileset geometry for '%1'.\n\n"
                        "Image: %2 x %3 pixels\n"
                        "Expected at least one %4 x %5 tile.")
                    .arg(QDir::toNativeSeparators(path))
                    .arg(imageSize.width()).arg(imageSize.height())
                    .arg(scaledTileWidth).arg(scaledTileHeight);
            qWarning() << "Tileset geometry validation failed:"
                       << mError;
            return false;
        }
        return true;
    };

    QImageReader ir2x(imageSource2x);
    if (ir2x.size().isValid()) {
        qInfo() << "Loading 2x tileset image" << imageSource2x
                << ir2x.size();
        if (!validateGeometry(imageSource2x, ir2x.size(), 2))
            return false;
        if (!ts->loadFromNothing(ir2x.size() / 2, imageSource)) {
            mError = tr("Unable to initialize tileset '%1' from:\n'%2'")
                    .arg(ts->name(), QDir::toNativeSeparators(imageSource2x));
            return false;
        }
        ts->setImageSource2x(imageSource2x);
        // can't use canonicalFilePath since the 1x tileset may not exist
        TilesetManager::instance()->loadTileset(ts, imageSource);
        if (ts->columnCount() <= 0 || ts->tileCount() <= 0) {
            mError = tr("Tileset '%1' produced no usable tiles.").arg(ts->name());
            return false;
        }
        return true;
    }
    QImageReader reader(imageSource);
    if (reader.size().isValid()) {
        qInfo() << "Loading 1x tileset image" << imageSource
                << reader.size();
        if (!validateGeometry(imageSource, reader.size(), 1))
            return false;
        if (!ts->loadFromNothing(reader.size(), imageSource)) {
            mError = tr("Unable to initialize tileset '%1' from:\n'%2'")
                    .arg(ts->name(), QDir::toNativeSeparators(imageSource));
            return false;
        }
        QFileInfo info(imageSource);
        TilesetManager::instance()->loadTileset(ts, info.canonicalFilePath());
        if (ts->columnCount() <= 0 || ts->tileCount() <= 0) {
            mError = tr("Tileset '%1' produced no usable tiles.").arg(ts->name());
            return false;
        }
        return true;
    }
    mError = tr(
                "No readable PNG was found for tileset '%1'.\n\n"
                "Selected: '%2'\nResolved 1x: '%3'\nResolved 2x: '%4'")
            .arg(ts->name(),
                 QDir::toNativeSeparators(source),
                 QDir::toNativeSeparators(imageSource),
                 QDir::toNativeSeparators(imageSource2x));
    qWarning() << "Tileset image load failed:" << mError;
    return false;
}

void TileMetaInfoMgr::addTileset(Tileset *tileset)
{
    Q_ASSERT(mTilesetByName.contains(tileset->name()) == false);
    Q_ASSERT(mTilesetByFoldedName.contains(
                 tileset->name().toCaseFolded()) == false);
    mTilesetByName[tileset->name()] = tileset;
    mTilesetByFoldedName[tileset->name().toCaseFolded()] = tileset;
    if (!mRemovedTilesets.contains(tileset))
        TilesetManager::instance()->addReference(tileset, false);
    mRemovedTilesets.removeAll(tileset);
    emit tilesetAdded(tileset);
}

void TileMetaInfoMgr::removeTileset(Tileset *tileset)
{
    Q_ASSERT(mTilesetByName.contains(tileset->name()));
    Q_ASSERT(mRemovedTilesets.contains(tileset) == false);
    emit tilesetAboutToBeRemoved(tileset);
    mTilesetByName.remove(tileset->name());
    mTilesetByFoldedName.remove(tileset->name().toCaseFolded());
    emit tilesetRemoved(tileset);

    // Don't remove references now, that will delete the tileset, and the
    // user might undo the removal.
    mRemovedTilesets += tileset;
    //    TilesetManager::instance()->removeReference(tileset);
}

void TileMetaInfoMgr::resolveTilesets(const QList<Tileset *> &tilesets)
{
    QList<Tileset *> _tilesets = tilesets;
    if (_tilesets.isEmpty())
        _tilesets = this->tilesets();

    for (Tileset *ts : _tilesets) {
        if (ts->isLoaded())
            continue;

        QString imageSource, imageSource2x;
        TilesetManager::instance()->getTilesetFileName(
                    ts->name(), imageSource, imageSource2x);
        QImageReader reader2x(imageSource2x);
        QImageReader reader1x(imageSource);
        if (reader2x.size().isValid()) {
            ts->loadFromNothing(reader2x.size() / 2, imageSource);
            ts->setImageSource2x(imageSource2x);
            TilesetManager::instance()->changeTilesetSource(
                        ts, imageSource, false);
        } else if (reader1x.size().isValid()) {
            const QString canonicalPath =
                    QFileInfo(imageSource).canonicalFilePath();
            ts->loadFromNothing(reader1x.size(), canonicalPath);
            ts->setImageSource2x(QString());
            TilesetManager::instance()->changeTilesetSource(
                        ts, canonicalPath, false);
        } else {
            Tile *missingTile = TilesetManager::instance()->missingTile();
            for (int index = 0; index < ts->tileCount(); ++index)
                ts->tileAt(index)->setImage(missingTile);
            ts->setImageSource2x(QString());
            TilesetManager::instance()->changeTilesetSource(
                        ts, imageSource, true);
        }
    }
}

void TileMetaInfoMgr::loadTilesets(const QList<Tileset *> &tilesets,
                                   bool processEvents, PROGRESS *progress)
{
    Q_UNUSED(processEvents)
    QList<Tileset *> _tilesets = tilesets;
    if (_tilesets.isEmpty())
        _tilesets = this->tilesets();
    resolveTilesets(_tilesets);

    int total = 0;
    for (Tileset *ts : std::as_const(_tilesets)) {
        if (!ts->isLoaded() && !ts->isMissing())
            ++total;
    }
    int current = 0;

    foreach (Tileset *ts, _tilesets) {
        if (!ts->isLoaded() && !ts->isMissing()) {
            ++current;
            if (progress) {
                progress->update(tr("Loading tileset %1 of %2: %3")
                                 .arg(current).arg(total).arg(ts->name()));
            }
            TilesetManager::instance()->loadTileset(ts, ts->imageSource());
        }
    }
}

void TileMetaInfoMgr::replaceEnums(QMap<QString, int> &enums, QStringList &names)
{
    const QMap<QString, int> oldEnums = mEnums;
    const QStringList oldNames = mEnumNames;
    mEnumNames = names;
    mEnums = enums;
    names = oldNames;
    enums = oldEnums;
}

void TileMetaInfoMgr::tilesetChanged(Tileset *ts)
{
    if (tilesets().contains(ts)) {
    }
}

void TileMetaInfoMgr::setTileEnum(Tile *tile, const QString &enumName)
{
    QString key = TilesetMetaInfo::key(tile);
    if (key.isEmpty())
        return;
    QString tilesetName = tile->tileset()->name();
    if (enumName.isEmpty()) {
        if (mTilesetInfo.contains(tilesetName))
            mTilesetInfo[tilesetName]->mInfo.remove(key);
        return;
    }
    if (!mTilesetInfo.contains(tilesetName))
        mTilesetInfo[tilesetName] = new TilesetMetaInfo;
    TilesetMetaInfo *info = mTilesetInfo[tilesetName];
    info->mInfo[key].mMetaGameEnum = enumName;
}

QString TileMetaInfoMgr::tileEnum(Tile *tile)
{
    if (!tile || !tile->tileset())
        return QString();
    QString tilesetName = tile->tileset()->name();
    if (!mTilesetInfo.contains(tilesetName))
        return QString();
    QString key = TilesetMetaInfo::key(tile);
    if (key.isEmpty())
        return QString();
    TilesetMetaInfo *info = mTilesetInfo[tilesetName];
    if (!info->mInfo.contains(key))
        return QString();
    return info->mInfo[key].mMetaGameEnum;
}

int TileMetaInfoMgr::tileEnumValue(Tile *tile)
{
    QString enumName = tileEnum(tile);
    if (!enumName.isEmpty())
        return mEnums[enumName];
    return -1;
}

bool TileMetaInfoMgr::isEnumWest(int enumValue) const
{
    Q_ASSERT(mEnums.values().contains(enumValue));
    return mEnums.key(enumValue).endsWith(QLatin1Char('W'));
}

bool TileMetaInfoMgr::isEnumNorth(int enumValue) const
{
    Q_ASSERT(mEnums.values().contains(enumValue));
    return mEnums.key(enumValue).endsWith(QLatin1Char('N'));
}

bool TileMetaInfoMgr::isEnumWest(const QString &enumName) const
{
    return enumName.endsWith(QLatin1Char('W'));
}

bool TileMetaInfoMgr::isEnumNorth(const QString &enumName) const
{
    return enumName.endsWith(QLatin1Char('N'));
}

bool TileMetaInfoMgr::parse2Ints(const QString &s, int *pa, int *pb)
{
    QStringList coords = s.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (coords.size() != 2)
        return false;
    bool ok;
    int a = coords[0].toInt(&ok);
    if (!ok) return false;
    int b = coords[1].toInt(&ok);
    if (!ok) return false;
    *pa = a, *pb = b;
    return true;
}

/////

QString TilesetMetaInfo::key(Tile *tile)
{
    if (!tile || !tile->tileset())
        return QString();

    Tileset *tileset = tile->tileset();
    int columns = tileset->columnCount();
    if (columns <= 0) {
        if (Tileset *catalogTileset =
                TileMetaInfoMgr::instance()->tileset(tileset->name())) {
            columns = catalogTileset->columnCount();
        }
    }
    if (columns <= 0)
        columns = recoverSingleRowColumnCount(tileset);
    if (columns <= 0)
        return QString();

    int column = tile->id() % columns;
    int row = tile->id() / columns;
    return QString(QLatin1String("%1,%2")).arg(column).arg(row);
}
