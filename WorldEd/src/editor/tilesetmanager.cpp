/*
 * tilesetmanager.cpp
 * Copyright 2008-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2009, Edward Hutchins <eah1@yahoo.com>
 *
 * This file is part of Tiled.
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

#include "tilesetmanager.h"

#include "filesystemwatcher.h"
#include "tiledeffile.h"
#include "tileset.h"
#include "tilesetimagelock.h"

#include <QImage>
#ifdef ZOMBOID
#include "preferences.h"
#include "progress.h"
#include "tile.h"
#include "tilemetainfomgr.h"
#include "BuildingEditor/buildingfloor.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QImageReader>
#include <QMetaType>
#include <QScopedPointer>
#include <QStringList>
#include <QWriteLocker>
#endif

using namespace Tiled;
using namespace Tiled::Internal;

TilesetManager *TilesetManager::mInstance = 0;

TilesetManager::TilesetManager():
#ifdef ZOMBOID
    mTilesetImageCache(new TilesetImageCache),
#endif
    mWatcher(new FileSystemWatcher(this)),
    mReloadTilesetsOnChange(false)
{
#ifdef ZOMBOID
    const int TILE_WIDTH = 64;
    const int TILE_HEIGHT = 128;

    mInvisibleTileset = new Tileset(QLatin1String("invisible"), TILE_WIDTH, TILE_HEIGHT);
    mInvisibleTileset->setTransparentColor(Qt::transparent);
    mInvisibleTileset->setMissing(true);
    QString fileName = QLatin1String(":/images/invisible-tile.png");
    if (!mInvisibleTileset->loadFromImage(QImage(fileName), fileName)) {
        QImage image(TILE_WIDTH, TILE_HEIGHT, QImage::Format_ARGB32);
        image.fill(Qt::red);
        mInvisibleTileset->loadFromImage(image, fileName);
    }
    mInvisibleTile = mInvisibleTileset->tileAt(0);
    mTilesets.insert(mInvisibleTileset, 1);

    mMissingTileset = new Tileset(QLatin1String("missing"), TILE_WIDTH, TILE_HEIGHT);
    mMissingTileset->setTransparentColor(Qt::white);
    mMissingTileset->setMissing(true);
    fileName = QLatin1String(":/images/missing-tile.png");
    if (!mMissingTileset->loadFromImage(QImage(fileName), fileName)) {
        QImage image(TILE_WIDTH, TILE_HEIGHT, QImage::Format_ARGB32);
        image.fill(Qt::red);
        mMissingTileset->loadFromImage(image, fileName);
    }
    mMissingTile = mMissingTileset->tileAt(0);
    mTilesets.insert(mMissingTileset, 1); //addReference(mMissingTileset);

    mNoBlendTileset = new Tileset(QLatin1String("noblend"), TILE_WIDTH, TILE_HEIGHT);
    mNoBlendTileset->setTransparentColor(Qt::white);
    mNoBlendTileset->setMissing(true);
    fileName = QLatin1String(":/images/noblend.png");
    if (!mNoBlendTileset->loadFromImage(QImage(fileName), fileName)) {
        QImage image(TILE_WIDTH, TILE_HEIGHT, QImage::Format_ARGB32);
        image.fill(Qt::red);
        mNoBlendTileset->loadFromImage(image, fileName);
    }
    mNoBlendTile = mNoBlendTileset->tileAt(0);
    mTilesets.insert(mNoBlendTileset, 1); //addReference(mNoBlendTileset);

#ifndef WORLDED
    mReloadTilesetsOnChange = Preferences::instance()->reloadTilesetsOnChange();
#endif
#endif

    connect(mWatcher, &FileSystemWatcher::fileChanged,
            this, &TilesetManager::fileChanged);
    connect(mWatcher, &FileSystemWatcher::directoryChanged,
            this, &TilesetManager::directoryChanged);

    mChangedFilesTimer.setInterval(500);
    mChangedFilesTimer.setSingleShot(true);

    connect(&mChangedFilesTimer, &QTimer::timeout,
            this, &TilesetManager::fileChangedTimeout);
}

TilesetManager::~TilesetManager()
{
#ifdef ZOMBOID
    removeReference(mInvisibleTileset);
    removeReference(mMissingTileset);
    removeReference(mNoBlendTileset);
    delete mTilesetImageCache;
#endif

    // Since all MapDocuments should be deleted first, we assert that there are
    // no remaining tileset references.
    Q_ASSERT(mTilesets.size() == 0);

#ifdef ZOMBOID_TILE_LAYER_NAMES
    foreach (ZTileLayerNames *tln, mTileLayerNames)
        writeTileLayerNames(tln);
#endif
}

TilesetManager *TilesetManager::instance()
{
    if (!mInstance) {
        mInstance = new TilesetManager;
        TileDefWatcher *tileDefWatcher = BuildingEditor::getTileDefWatcher();
        QObject::connect(tileDefWatcher, &TileDefWatcher::tilePropertiesChanged, mInstance, &TilesetManager::tilePropertiesChanged);
    }

    return mInstance;
}

void TilesetManager::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

Tileset *TilesetManager::findTileset(const QString &fileName) const
{
    foreach (Tileset *tileset, tilesets())
        if (tileset->fileName() == fileName)
            return tileset;

    return 0;
}

Tileset *TilesetManager::findTileset(const TilesetSpec &spec) const
{
    foreach (Tileset *tileset, tilesets()) {
        if (tileset->imageSource() == spec.imageSource
            && tileset->tileWidth() == spec.tileWidth
            && tileset->tileHeight() == spec.tileHeight
            && tileset->tileSpacing() == spec.tileSpacing
            && tileset->margin() == spec.margin)
        {
            return tileset;
        }
    }

    return 0;
}

void TilesetManager::addReference(Tileset *tileset, bool loadImage)
{
    if (mTilesets.contains(tileset)) {
        mTilesets[tileset]++;
    } else {
        mTilesets.insert(tileset, 1);
#ifdef ZOMBOID
#else
        if (!tileset->imageSource().isEmpty())
            mWatcher->addPath(tileset->imageSource());
#endif
    }
#ifdef ZOMBOID_TILE_LAYER_NAMES
    if (!tileset->imageSource().isEmpty() && !tileset->isMissing())
        readTileLayerNames(tileset);
#endif

#ifdef ZOMBOID
    if (loadImage)
        loadTileset(tileset, tileset->imageSource());
#endif
}

void TilesetManager::removeReference(Tileset *tileset)
{
    Q_ASSERT(mTilesets.value(tileset) > 0);
    mTilesets[tileset]--;

    if (mTilesets.value(tileset) == 0) {
        mTilesets.remove(tileset);
#ifdef ZOMBOID
#else
        if (!tileset->imageSource().isEmpty())
            mWatcher->removePath(tileset->imageSource());
#endif

        delete tileset;
    }
}

void TilesetManager::addReferences(const QList<Tileset*> &tilesets, bool loadImages)
{
    foreach (Tileset *tileset, tilesets)
        addReference(tileset, loadImages);
}

void TilesetManager::removeReferences(const QList<Tileset*> &tilesets)
{
    foreach (Tileset *tileset, tilesets)
        removeReference(tileset);
}

QList<Tileset*> TilesetManager::tilesets() const
{
    return mTilesets.keys();
}

void TilesetManager::setReloadTilesetsOnChange(bool enabled)
{
    mReloadTilesetsOnChange = enabled;
    // TODO: Clear the file system watcher when disabled
}

void TilesetManager::tilesetDirectoryChanged()
{
    mTilesetPaths.clear();

    if (!mWatchedTilesetDirectories.isEmpty())
        mWatcher->removePaths(mWatchedTilesetDirectories);
    mWatchedTilesetDirectories.clear();

    const QString rootPath = Preferences::instance()->tilesDirectory();
    const QString path2x = Preferences::instance()->tiles2xDirectory();
    QStringList directories = { rootPath, path2x };
    const QStringList roots = directories;
    for (const QString &path : roots) {
        QDirIterator iterator(path, QDir::Dirs | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            directories += QFileInfo(iterator.next()).absoluteFilePath();
    }
    directories.removeAll(QString());
    directories.removeDuplicates();
    for (const QString &path : std::as_const(directories)) {
        if (QDir(path).exists()) {
            mWatcher->addPath(path);
            mWatchedTilesetDirectories += path;
        }
    }
}

bool TilesetManager::getTilesetFileName(const QString &tilesetName, QString &path1x, QString &path2x)
{
    TilesetPaths paths = mTilesetPaths.value(tilesetName);
    if (paths.bValid) {
        path1x = paths.path1x;
        path2x = paths.path2x;
        return true;
    }

    QString tiles1xDir = Preferences::instance()->tilesDirectory();
    QString tiles2xDir = Preferences::instance()->tiles2xDirectory();

    QDir dir1x(tiles1xDir);
    QDir dir2x(tiles2xDir);

    QString fileName = tilesetName + QLatin1String(".png");
    path1x = dir1x.filePath(fileName);
    path2x = dir2x.filePath(fileName);

    if (QImageReader(path2x).size().isValid()) {
        paths.bValid = true;
        paths.path1x = path1x;
        paths.path2x = path2x;
        mTilesetPaths[tilesetName] = paths;
        return true;
    }

    auto findImage = [&fileName](const QDir &root,
                                 const QString &excludedTree = QString()) {
        QStringList candidates;
        const QString excludedPrefix = excludedTree.isEmpty()
                ? QString()
                : QDir::cleanPath(QFileInfo(excludedTree).absoluteFilePath())
                  + QDir::separator();
        QDirIterator iterator(root.absolutePath(),
                              QStringList() << fileName,
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString candidate =
                    QDir::cleanPath(QFileInfo(iterator.next())
                                    .absoluteFilePath());
            if (!excludedPrefix.isEmpty()
                    && candidate.startsWith(
                        excludedPrefix, Qt::CaseInsensitive)) {
                continue;
            }
            if (QImageReader(candidate).size().isValid())
                candidates += candidate;
        }
        candidates.sort(Qt::CaseInsensitive);
        return candidates.isEmpty() ? QString() : candidates.first();
    };

    const QString nested2x = findImage(dir2x);
    if (!nested2x.isEmpty()) {
        const QString relative = dir2x.relativeFilePath(nested2x);
        path1x = dir1x.filePath(relative);
        path2x = nested2x;
        paths.bValid = true;
        paths.path1x = path1x;
        paths.path2x = path2x;
        mTilesetPaths[tilesetName] = paths;
        return true;
    }

    if (QImageReader(path1x).size().isValid()) {
        paths.bValid = true;
        paths.path1x = path1x;
        paths.path2x = path2x;
        mTilesetPaths[tilesetName] = paths;
        return true;
    }

    const QString nested1x =
            findImage(dir1x, dir2x.absolutePath());
    if (!nested1x.isEmpty()) {
        const QString relative = dir1x.relativeFilePath(nested1x);
        path1x = nested1x;
        path2x = dir2x.filePath(relative);
        paths.bValid = true;
        paths.path1x = path1x;
        paths.path2x = path2x;
        mTilesetPaths[tilesetName] = paths;
        return true;
    }

    return false;
}

void TilesetManager::fileChanged(const QString &path)
{
#ifndef ZOMBOID
    if (!mReloadTilesetsOnChange)
        return;
#endif

    /*
     * Use a one-shot timer since GIMP (for example) seems to generate many
     * file changes during a save, and some of the intermediate attempts to
     * reload the tileset images actually fail (at least for .png files).
     */
    mChangedFiles.insert(path);
    mChangedFilesTimer.start();
}

void TilesetManager::directoryChanged(const QString &path)
{
    mChangedDirectories.insert(path);
    mChangedFilesTimer.start();
}

void TilesetManager::fileChangedTimeout()
{
#ifdef ZOMBOID
    if (!mChangedDirectories.isEmpty()) {
        qInfo() << "Tiles directory changed:" << mChangedDirectories;
        mTilesetPaths.clear();

        if (!TileMetaInfoMgr::instance()->addNewTilesets()) {
            qWarning().noquote()
                    << "Unable to discover new PNG sheets after a Tiles "
                       "directory change:"
                    << TileMetaInfoMgr::instance()->errorString();
        }

        // Deleting a PNG commonly emits only directoryChanged(). Feed missing
        // cache sources through the normal file invalidation path as well.
        for (Tileset *cached : std::as_const(mTilesetImageCache->mTilesets)) {
            const QString source = cached->imageSource2x().isEmpty()
                    ? cached->imageSource() : cached->imageSource2x();
            if (!source.isEmpty()
                    && !QImageReader(source).size().isValid()) {
                qWarning() << "Tileset image removed:" << source;
                mChangedFiles.insert(source);
            }
        }

        for (Tileset *tileset
             : TileMetaInfoMgr::instance()->tilesets()) {
            if (tileset->isLoaded() && !tileset->isMissing())
                continue;
            tileset->setLoaded(false);
            tileset->setMissing(false);
            loadTileset(tileset, tileset->imageSource());
        }
        mChangedDirectories.clear();
        tilesetDirectoryChanged();
    }

    qDebug() << "fileChangedTimeout " << mChangedFiles;
    foreach (Tileset *tileset, mTilesetImageCache->mTilesets) {
        QString fileName = tileset->imageSource2x().isEmpty() ? tileset->imageSource() : tileset->imageSource2x();
        if (mChangedFiles.contains(fileName)) {
            QWriteLocker imageWriteLock(&tilesetImageLock());
            if (QImageReader(fileName).size().isValid()) {
                tileset->loadFromImage(QImage(fileName), tileset->imageSource());
                tileset->setMissing(false);
            } else {
                if (tileset->tileHeight() == mMissingTile->height()
                        && tileset->tileWidth() == mMissingTile->width()) {
                    for (int i = 0; i < tileset->tileCount(); i++)
                        tileset->tileAt(i)->setImage(mMissingTile);
                }
                tileset->setMissing(true);
            }
        }
    }
    foreach (Tileset *tileset, tilesets()) {
        QString fileName = tileset->imageSource();
        QString fileName2 = tileset->imageSource2x().isEmpty() ? tileset->imageSource() : tileset->imageSource2x();
        if (mChangedFiles.contains(fileName2)) {
            if (Tileset *cached = mTilesetImageCache->findMatch(tileset, fileName, fileName2)) {
                QWriteLocker imageWriteLock(&tilesetImageLock());
                if (tileset->loadFromCache(cached)) {
                    tileset->setMissing(cached->isMissing());
#ifdef ZOMBOID_TILE_LAYER_NAMES
                    syncTileLayerNames(tileset);
#endif
                    emit tilesetChanged(tileset);
                }
            } else if (tileset->isLoaded()) {
                tileset->setLoaded(false);
                loadTileset(tileset, fileName);
            }
        }
    }
#else

    foreach (Tileset *tileset, tilesets()) {
        QString fileName = tileset->imageSource();
        if (mChangedFiles.contains(fileName))
            if (tileset->loadFromImage(QImage(fileName), fileName))
#ifdef ZOMBOID
            {
                syncTileLayerNames(tileset);
                emit tilesetChanged(tileset);
            }
#else
                emit tilesetChanged(tileset);
#endif
    }
#endif

    mChangedFiles.clear();
}

#ifdef ZOMBOID
void TilesetManager::imageLoaded(QImage *image, Tileset *tileset)
{
    QWriteLocker imageWriteLock(&tilesetImageLock());
    Q_ASSERT(mTilesetImageCache->mTilesets.contains(tileset));

    // This updates a tileset in the cache.
    if (!tileset->loadFromImage(*image, tileset->imageSource())) {
        const QString source = tileset->imageSource2x().isEmpty()
                ? tileset->imageSource() : tileset->imageSource2x();
        qWarning() << "Unable to decode tileset image:" << source;
        tileset->setMissing(true);
        for (Tileset *candidate : tilesets()) {
            const bool sameSource =
                    candidate->imageSource() == tileset->imageSource()
                    || (!tileset->imageSource2x().isEmpty()
                        && candidate->imageSource2x()
                        == tileset->imageSource2x());
            if (!sameSource) {
                continue;
            }
            candidate->setMissing(true);
            candidate->setLoaded(false);
            if (candidate->tileHeight() == mMissingTile->height()
                    && candidate->tileWidth() == mMissingTile->width()) {
                for (int i = 0; i < candidate->tileCount(); ++i)
                    candidate->tileAt(i)->setImage(mMissingTile);
            }
            emit tilesetChanged(candidate);
        }
        delete image;
        return;
    }

    // Watch the image file for changes.
    mWatcher->addPath(tileset->imageSource2x().isEmpty() ? tileset->imageSource() : tileset->imageSource2x());

    // Now update every tileset using this image.
    foreach (Tileset *candidate, tilesets()) {
        if (candidate->isLoaded())
            continue;
        if (((candidate->imageSource() == tileset->imageSource()) || (!tileset->imageSource2x().isEmpty() && (candidate->imageSource2x() == tileset->imageSource2x())))
                && candidate->tileWidth() == tileset->tileWidth()
                && candidate->tileHeight() == tileset->tileHeight()
                && candidate->tileSpacing() == tileset->tileSpacing()
                && candidate->margin() == tileset->margin()
                && candidate->transparentColor() == tileset->transparentColor()) {
            candidate->loadFromCache(tileset);
            candidate->setMissing(false);
            copyPZProperties(tileset, candidate);
            emit tilesetChanged(candidate);
        }
    }
    delete image;
}

void TilesetManager::tilePropertiesChanged()
{
    for (Tileset *cached : qAsConst(mTilesetImageCache->mTilesets)) {
        cachePZProperties(cached);
    }
    for (Tileset *tileset : tilesets()) {
        QString imageSource = tileset->imageSource();
        QString imageSource2x = tileset->imageSource2x();
        Tileset *cached = mTilesetImageCache->findMatch(tileset, imageSource, imageSource2x);
        if (cached) {
            copyPZProperties(cached, tileset);
        }
    }
}

void TilesetManager::loadTileset(Tileset *tileset, const QString &imageSource_)
{
    // Catalog entries keep a relative path until the shared Tiles directory
    // has been configured.
    if (QDir(imageSource_).isRelative()) {
        TileMetaInfoMgr::instance()->resolveTilesets(
                    QList<Tileset *>() << tileset);
        if (tileset->isMissing()
                || QDir(tileset->imageSource()).isRelative()) {
            return;
        }
    }

    if (tileset->isLoaded())
        return;

    QString imageSource, imageSource2x;
    getTilesetFileName(tileset->name(), imageSource, imageSource2x);
    if (Tileset *cached = mTilesetImageCache->findMatch(
                tileset, imageSource, imageSource2x)) {
        if (cached->isLoaded()) {
            {
                QWriteLocker imageWriteLock(&tilesetImageLock());
                tileset->loadFromCache(cached);
                tileset->setMissing(false);
            }
            copyPZProperties(cached, tileset);
            emit tilesetChanged(tileset);
        } else {
            const QString source = cached->imageSource2x().isEmpty()
                    ? cached->imageSource() : cached->imageSource2x();
            imageLoaded(new QImage(source), cached);
        }
    } else if (QImageReader(imageSource2x).size().isValid()) {
        changeTilesetSource(tileset, imageSource, false);
        tileset->setImageSource2x(imageSource2x);
        cached = mTilesetImageCache->addTileset(tileset);
        cachePZProperties(cached);
        imageLoaded(new QImage(imageSource2x), cached);
    } else if (QImageReader(imageSource).size().isValid()) {
        changeTilesetSource(tileset, imageSource, false);
        tileset->setImageSource2x(QString());
        cached = mTilesetImageCache->addTileset(tileset);
        cachePZProperties(cached);
        imageLoaded(new QImage(imageSource), cached);
    } else {
        {
            QWriteLocker imageWriteLock(&tilesetImageLock());
            if (tileset->tileHeight() == mMissingTile->height()
                    && tileset->tileWidth() == mMissingTile->width()) {
                for (int i = 0; i < tileset->tileCount(); i++)
                    tileset->tileAt(i)->setImage(mMissingTile);
            }
        }
        changeTilesetSource(tileset, imageSource, true);
        tileset->setImageSource2x(QString());
    }
}

void TilesetManager::waitForTilesets(const QList<Tileset *> &tilesets, QWidget *parent,
                                     int expectedTotal)
{
    QList<Tileset *> requested = tilesets;
    if (requested.isEmpty())
        requested = TileMetaInfoMgr::instance()->tilesets();

    const int total = expectedTotal >= 0 ? expectedTotal : requested.size();
    QScopedPointer<PROGRESS> progress;
    if (parent) {
        progress.reset(new PROGRESS(
                           tr("Loading tilesets 0 / %1...").arg(total),
                           parent));
    }

    int current = qMax(0, total - requested.size());
    int loadAttempts = 0;
    for (Tileset *tileset : requested) {
        if (progress) {
            progress->update(
                        tr("Loading tilesets %1 / %2: %3")
                        .arg(++current).arg(total).arg(tileset->name()));
        }
        if (!tileset->isLoaded() && !tileset->isMissing()) {
            ++loadAttempts;
            loadTileset(tileset, tileset->imageSource());
        }
    }

    int loaded = 0;
    int missing = 0;
    int pending = 0;
    QStringList missingNames;
    QStringList pendingNames;
    for (Tileset *ts : requested) {
        if (ts->isLoaded())
            ++loaded;
        else if (ts->isMissing()) {
            ++missing;
            missingNames += ts->name();
        } else {
            ++pending;
            pendingNames += ts->name();
        }
    }
    if (loadAttempts > 0 || missing > 0 || pending > 0) {
        qInfo() << "Tileset readiness check:"
                << "requested" << requested.size()
                << "load-attempts" << loadAttempts
                << "loaded" << loaded
                << "missing" << missing
                << "pending" << pending;
    }
    if (!missingNames.isEmpty()) {
        const QString suffix = missingNames.size() > 20
                ? tr(" (and %1 more)").arg(missingNames.size() - 20)
                : QString();
        qWarning().noquote()
                << tr("Missing tileset images (%1): %2%3")
                   .arg(missingNames.size())
                   .arg(missingNames.mid(0, 20).join(QLatin1String(", ")))
                   .arg(suffix);
    }
    if (!pendingNames.isEmpty()) {
        const QString suffix = pendingNames.size() > 20
                ? tr(" (and %1 more)").arg(pendingNames.size() - 20)
                : QString();
        qWarning().noquote()
                << tr("Tilesets still pending after preload (%1): %2%3")
                   .arg(pendingNames.size())
                   .arg(pendingNames.mid(0, 20).join(QLatin1String(", ")))
                   .arg(suffix);
    }
}

int TilesetManager::countLoadingTilesets(const QList<Tileset*> &tilesets) const
{
    int count = 0;
    for (Tileset *ts : tilesets) {
        if (ts->isLoaded() || ts->isMissing())
            continue;
        count++;
    }
    return count;
}

void TilesetManager::cachePZProperties(Tileset *cached)
{
    QString tilesetName = QFileInfo(cached->imageSource2x().isEmpty() ? cached->imageSource() : cached->imageSource2x()).completeBaseName();
    TileDefWatcher *tileDefWatcher = BuildingEditor::getTileDefWatcher();
    tileDefWatcher->check();
    QString INVISIBLE = QLatin1String("invisible");
    if (TileDefTileset *tdts = tileDefWatcher->tileset(tilesetName)) {
        for (int i = 0; i < cached->tileCount(); i++) {
            TileDefTile *tdt = tdts->tileAt(i);
            if (tdt == nullptr)
                 break;
#if 1
            cached->tileAt(i)->setProperties({});
            if (tdt->mProperties.contains(INVISIBLE)) {
                cached->tileAt(i)->setProperty(INVISIBLE, QString());
            }
#else
            Properties properties;
            properties.insert(tdt->mProperties);
            cached->setProperties(properties);
#endif
        }
    }
}

void TilesetManager::copyPZProperties(Tileset *src, Tileset *dst)
{
    for (int i = 0; i < src->tileCount(); i++) {
        if (Tile *tileDst = dst->tileAt(i)) {
            Tile *tileSrc = src->tileAt(i);
            tileDst->setProperties(tileSrc->properties());
        }
    }
}

void TilesetManager::changeTilesetSource(Tileset *tileset, const QString &source,
                                         bool missing)
{
    mTilesetImageCache->invalidateLookupTables();
    tileset->setImageSource(source);
    tileset->setMissing(missing);
    if (!tileset->imageSource().isEmpty() && !tileset->isMissing()) {
#ifdef ZOMBOID_TILE_LAYER_NAMES
        readTileLayerNames(tileset);
#endif
    }
    tileset->setLoaded(false);
    if (missing)
        emit tilesetChanged(tileset);
}
#endif

#ifdef ZOMBOID_TILE_LAYER_NAMES
#include "tile.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QXmlStreamReader>

namespace Tiled {
namespace Internal {

struct ZTileLayerName
{
    ZTileLayerName()
    {}
    QString mLayerName;
};

struct ZTileLayerNames
{
    ZTileLayerNames()
        : mColumns(0)
        , mRows(0)
        , mModified(false)
    {}
    ZTileLayerNames(const QString& filePath, int columns, int rows)
        : mColumns(columns)
        , mRows(rows)
        , mFilePath(filePath)
        , mModified(false)
    {
        mTiles.resize(columns * rows);
    }
    void enforceSize(int columns, int rows)
    {
        if (columns == mColumns && rows == mRows)
            return;
        if (columns == mColumns) {
            // Number of rows changed, tile ids are still valid
            mTiles.resize(columns * rows);
            return;
        }
        // Number of columns (and maybe rows) changed.
        // Copy over the preserved part.
        QRect oldBounds(0, 0, mColumns, mRows);
        QRect newBounds(0, 0, columns, rows);
        QRect preserved = oldBounds & newBounds;
        QVector<ZTileLayerName> tiles(columns * rows);
        for (int y = 0; y < preserved.height(); y++) {
            for (int x = 0; x < preserved.width(); x++) {
                tiles[y * columns + x] = mTiles[y * mColumns + x];
            }
        }
        mColumns = columns;
        mRows = rows;
        mTiles = tiles;
    }

    int mColumns;
    int mRows;
    QString mFilePath;
    QVector<ZTileLayerName> mTiles;
    bool mModified;
};

} // namespace Internal
} // namespace Tiled

void TilesetManager::setLayerName(Tile *tile, const QString &name)
{
    Tileset *ts = tile->tileset();
    if (ZTileLayerNames *tln = layerNamesForTileset(ts)) {
        // Prevent an error if two tilesets share the same image file but have
        // different tile dimensions.
        if ((tile->id() >= 0) && (tile->id() < tln->mRows * tln->mColumns)) {
            tln->mTiles[tile->id()].mLayerName = name;
            tln->mModified = true;
            emit tileLayerNameChanged(tile);
        }
    }
}

QString TilesetManager::layerName(Tile *tile)
{
    Tileset *ts = tile->tileset();
    if (mTileLayerNames.contains(ts->imageSource())) {
        ZTileLayerNames *tln =  mTileLayerNames[ts->imageSource()];
        // Prevent an error if two tilesets share the same image file but have
        // different tile dimensions.
        if ((tile->id() >= 0) && (tile->id() < tln->mRows * tln->mColumns))
            return tln->mTiles[tile->id()].mLayerName;
    }
    return QString();
}

class ZTileLayerNamesReader
{
public:
    bool read(const QString &filePath)
    {
        mError.clear();

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            mError = QCoreApplication::translate("TileLayerNames", "Could not open file.");
            return false;
        }

        xml.setDevice(&file);

        if (xml.readNextStartElement() && xml.name() == QLatin1String("tileset")) {
            mTLN.mFilePath = filePath;
            return readTileset();
        } else {
            mError = QCoreApplication::translate("TileLayerNames", "File doesn't contain <tilesets>.");
            return false;
        }
    }

    bool readTileset()
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("tileset"));

        const QXmlStreamAttributes atts = xml.attributes();
        const QString tilesetName = atts.value(QLatin1String("name")).toString();
        uint columns = atts.value(QLatin1String("columns")).toString().toUInt();
        uint rows = atts.value(QLatin1String("rows")).toString().toUInt();

        mTLN.mTiles.resize(columns * rows);

        mTLN.mColumns = columns;
        mTLN.mRows = rows;

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("tile")) {
                const QXmlStreamAttributes atts = xml.attributes();
                uint id = atts.value(QLatin1String("id")).toString().toUInt();
                if (id >= columns * rows) {
                    qDebug() << "<tile> " << id << " out-of-bounds: ignored";
                } else {
                    const QString layerName(atts.value(QLatin1String("layername")).toString());
                    mTLN.mTiles[id].mLayerName = layerName;
                }
            }
            xml.skipCurrentElement();
        }

        return true;
    }

    QString errorString() const { return mError; }
    ZTileLayerNames &result() { return mTLN; }

private:
    QString mError;
    QXmlStreamReader xml;
    ZTileLayerNames mTLN;
};

class ZTileLayerNamesWriter
{
public:
    bool write(const ZTileLayerNames *tln)
    {
        mError.clear();

        QFile file(tln->mFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            mError = QCoreApplication::translate(
                        "TileLayerNames", "Could not open file for writing.");
            return false;
        }

        QXmlStreamWriter writer(&file);

        writer.setAutoFormatting(true);
        writer.setAutoFormattingIndent(1);

        writer.writeStartDocument();
        writer.writeStartElement(QLatin1String("tileset"));
        writer.writeAttribute(QLatin1String("columns"), QString::number(tln->mColumns));
        writer.writeAttribute(QLatin1String("rows"), QString::number(tln->mRows));

        int id = 0;
        foreach (const ZTileLayerName &tile, tln->mTiles) {
            if (!tile.mLayerName.isEmpty()) {
                writer.writeStartElement(QLatin1String("tile"));
                writer.writeAttribute(QLatin1String("id"), QString::number(id));
                writer.writeAttribute(QLatin1String("layername"), tile.mLayerName);
                writer.writeEndElement();
            }
            ++id;
        }

        writer.writeEndElement();
        writer.writeEndDocument();

        if (file.error() != QFile::NoError) {
            mError = file.errorString();
            return false;
        }

        return true;
    }

    QString errorString() const { return mError; }

private:
    QString mError;
};

QFileInfo TilesetManager::tileLayerNamesFile(Tileset *ts)
{
    QString imageSource = ts->imageSource();
    QFileInfo fileInfoImgSrc(imageSource);
    QDir dir = fileInfoImgSrc.absoluteDir();
    return QFileInfo(dir, fileInfoImgSrc.completeBaseName() + QLatin1String(".tilelayers.xml"));
}

ZTileLayerNames *TilesetManager::layerNamesForTileset(Tileset *ts)
{
    QString imageSource = ts->imageSource();
    QMap<QString,ZTileLayerNames*>::iterator it = mTileLayerNames.find(imageSource);
    if (it != mTileLayerNames.end())
        return *it;

    int columns = ts->columnCount();
    int rows = columns ? ts->tileCount() / columns : 0;

    QFileInfo fileInfo = tileLayerNamesFile(ts);
    QString filePath = fileInfo.absoluteFilePath();
    return mTileLayerNames[imageSource] = new ZTileLayerNames(filePath, columns, rows);
}

void TilesetManager::readTileLayerNames(Tileset *ts)
{
    QString imageSource = ts->imageSource();
    if (mTileLayerNames.contains(imageSource))
        return;

    int columns = ts->columnCount();
    int rows = columns ? ts->tileCount() / columns : 0;

    QFileInfo fileInfo = tileLayerNamesFile(ts);
    if (fileInfo.exists()) {
        QString filePath = fileInfo.absoluteFilePath();
//        qDebug() << "Reading: " << filePath;
        ZTileLayerNamesReader reader;
        if (reader.read(filePath)) {
            mTileLayerNames[imageSource] = new ZTileLayerNames(reader.result());
            // Handle the source image being resized
            mTileLayerNames[imageSource]->enforceSize(columns, rows);
        } else {
            QMessageBox::critical(0, tr("Error Reading Tile Layer Names"),
                                  filePath + QLatin1String("\n") + reader.errorString());
        }
    }
}

void TilesetManager::writeTileLayerNames(ZTileLayerNames *tln)
{
    if (tln->mModified == false)
        return;
//    qDebug() << "Writing: " << tln->mFilePath;
    ZTileLayerNamesWriter writer;
    if (writer.write(tln) == false) {
        QMessageBox::critical(0, tr("Error Writing Tile Layer Names"),
            tln->mFilePath + QLatin1String("\n") + writer.errorString());
    }
}

void TilesetManager::syncTileLayerNames(Tileset *ts)
{
    if (mTileLayerNames.contains(ts->imageSource())) {
        ZTileLayerNames *tln = mTileLayerNames[ts->imageSource()];
        int columns = ts->columnCount();
        int rows = columns ? ts->tileCount() / columns : 0;
        tln->enforceSize(columns, rows);
    }
}
#endif // ZOMBOID
