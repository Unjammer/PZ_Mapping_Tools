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

#include "mapimagemanager.h"

#ifdef WORLDED
#include "bmptotmx.h"
#endif // WORLDED
#include "bmpblender.h"
#include "imagelayer.h"
#include "isometricrenderer.h"
#include "mainwindow.h"
#include "map.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "objectgroup.h"
#include "orthogonalrenderer.h"
#include "preferences.h"
#include "progress.h"
#include "staggeredrenderer.h"
#include "tilelayer.h"
#include "tilesetmanager.h"
#include "tilesetimagelock.h"
#include "zlevelrenderer.h"

#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPainterPath>
#include <QReadLocker>

#ifdef QT_NO_DEBUG
inline QNoDebug noise() { return QNoDebug(); }
#else
inline QDebug noise() { return QDebug(QtDebugMsg); }
#endif

using namespace Tiled;
using namespace Tiled::Internal;

static int thumbnailImageWidth()
{
    return Preferences::instance()->thumbnailWidth();
}

MapImageManager *MapImageManager::mInstance = NULL;

MapImageManager::MapImageManager() :
    QObject(),
    mNextRenderWorker(0),
    mDeferralDepth(0),
    mDeferralQueued(false)
{
    mImageReaderThreads.resize(4);
    mImageReaderWorkers.resize(mImageReaderThreads.size());
    mNextThreadForJob = 0;
    for (int i = 0; i < mImageReaderWorkers.size(); i++) {
        mImageReaderThreads[i] = new InterruptibleThread;
        mImageReaderThreads[i]->setObjectName(
                    QStringLiteral("thumbnail-reader-%1").arg(i));
        mImageReaderWorkers[i] = new MapImageReaderWorker(mImageReaderThreads[i]);
        mImageReaderWorkers[i]->moveToThread(mImageReaderThreads[i]);
        connect(mImageReaderWorkers[i], &MapImageReaderWorker::imageLoaded,
                this, &MapImageManager::imageLoadedByThread);
        mImageReaderThreads[i]->start();
    }

    qRegisterMetaType<MapImageData>("MapImageData");
    qRegisterMetaType<MapImage*>("MapImage*");
    qRegisterMetaType<MapComposite*>("MapComposite*");
    const int renderThreadCount =
            qBound(1, QThread::idealThreadCount() - 1, 4);
    mImageRenderThreads.resize(renderThreadCount);
    mImageRenderWorkers.resize(renderThreadCount);
    for (int i = 0; i < renderThreadCount; ++i) {
        mImageRenderThreads[i] = new InterruptibleThread;
        mImageRenderThreads[i]->setObjectName(
                    QStringLiteral("thumbnail-renderer-%1").arg(i));
        mImageRenderWorkers[i] =
                new MapImageRenderWorker(mImageRenderThreads[i]);
        mImageRenderWorkers[i]->moveToThread(mImageRenderThreads[i]);
        connect(mImageRenderWorkers[i], &MapImageRenderWorker::mapNeeded,
                this, &MapImageManager::renderThreadNeedsMap);
        connect(mImageRenderWorkers[i],
                &MapImageRenderWorker::imageRendered,
                this, &MapImageManager::imageRenderedByThread);
        connect(mImageRenderWorkers[i],
                &MapImageRenderWorker::imageRenderFailed,
                this, &MapImageManager::imageRenderFailedByThread);
        connect(mImageRenderWorkers[i], &MapImageRenderWorker::jobDone,
                this, &MapImageManager::renderJobDone);
        mImageRenderThreads[i]->start();
    }

    connect(MapManager::instance(), &MapManager::mapAboutToChange,
            this, &MapImageManager::mapAboutToChange);
    connect(MapManager::instance(), &MapManager::mapChanged,
            this, &MapImageManager::mapChanged);
    connect(MapManager::instance(), &MapManager::mapFileChanged,
            this, &MapImageManager::mapFileChanged);
    connect(MapManager::instance(), &MapManager::mapLoaded,
            this, &MapImageManager::mapLoaded);
    connect(MapManager::instance(), &MapManager::mapFailedToLoad,
            this, &MapImageManager::mapFailedToLoad);
}

MapImageManager::~MapImageManager()
{
    for (int i = 0; i < mImageReaderThreads.size(); i++) {
        mImageReaderThreads[i]->interrupt();
        mImageReaderThreads[i]->quit();
        mImageReaderThreads[i]->wait();
        delete mImageReaderWorkers[i];
        delete mImageReaderThreads[i];
    }

    for (int i = 0; i < mImageRenderThreads.size(); ++i) {
        mImageRenderThreads[i]->interrupt();
        mImageRenderThreads[i]->quit();
        mImageRenderThreads[i]->wait();
        delete mImageRenderWorkers[i];
        delete mImageRenderThreads[i];
    }

#ifdef WORLDED
    for (const RenderPreparation &preparation : qAsConst(mRenderPreparations)) {
        for (MapInfo *referencedMap : preparation.referencedMaps)
            MapManager::instance()->removeReferenceToMap(referencedMap);
    }
#endif
    mRenderPreparations.clear();
    qDeleteAll(mActiveRenders.keys());
    mActiveRenders.clear();
    qDeleteAll(mMapImages);
    mMapImages.clear();
}

MapImageManager *MapImageManager::instance()
{
    if (mInstance == NULL)
        mInstance = new MapImageManager;
    return mInstance;
}

void MapImageManager::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

void MapImageManager::retainMapImage(MapImage *mapImage, QObject *owner)
{
    if (!mapImage)
        return;
    QObject *effectiveOwner = owner ? owner : this;
    if (mImageOwners[mapImage].contains(effectiveOwner))
        return;
    mImageOwners[mapImage].insert(effectiveOwner);
    mOwnerImages[effectiveOwner].insert(mapImage);
    if (effectiveOwner != this) {
        connect(effectiveOwner, &QObject::destroyed,
                this, &MapImageManager::releaseOwner,
                Qt::UniqueConnection);
    }
}

void MapImageManager::releaseOwner(QObject *owner)
{
    if (!owner)
        return;
    const QSet<MapImage*> images = mOwnerImages.take(owner);
    int removed = 0;
    int pending = 0;
    for (MapImage *mapImage : images) {
        auto owners = mImageOwners.find(mapImage);
        if (owners == mImageOwners.end())
            continue;
        owners->remove(owner);
        if (!owners->isEmpty())
            continue;
        mImageOwners.erase(owners);
        if (discardMapImageIfUnused(mapImage))
            ++removed;
        else
            ++pending;
    }
    if (!images.isEmpty()) {
        qInfo() << "Thumbnail owner released" << images.size()
                << "images, removed" << removed
                << "pending" << pending
                << "cached" << mMapImages.size();
    }
}

bool MapImageManager::containsMapImage(MapImage *mapImage) const
{
    return mMapImages.values().contains(mapImage);
}

int MapImageManager::mapImageReferenceCount(MapImage *mapImage) const
{
    return mImageOwners.value(mapImage).size();
}

bool MapImageManager::discardMapImageIfUnused(MapImage *mapImage)
{
    if (!mapImage || !mapImage->mLoaded
            || !mImageOwners.value(mapImage).isEmpty())
        return false;
    removeMapImage(mapImage);
    return true;
}

void MapImageManager::removeMapImage(MapImage *mapImage)
{
    mForceRebuildAfterLoad.remove(mapImage);
    mDeferredMapImages.removeAll(mapImage);
    mImageOwners.remove(mapImage);
    for (auto it = mOwnerImages.begin(); it != mOwnerImages.end();) {
        it->remove(mapImage);
        if (it->isEmpty())
            it = mOwnerImages.erase(it);
        else
            ++it;
    }
    for (auto it = mMapImages.begin(); it != mMapImages.end();) {
        if (it.value() == mapImage)
            it = mMapImages.erase(it);
        else
            ++it;
    }
    delete mapImage;
}

MapImage *MapImageManager::getMapImage(const QString &mapName,
                                       const QString &relativeTo,
                                       QObject *owner)
{
    // Do not emit mapImageChanged as a result of worker threads finishing
    // loading any images while we are creating a new thumbnail image.
    // Any time QCoreApplication::processEvents() gets called (as is done
    // by MapManager's EditorMapReader class and the PROGRESS class) a
    // worker-thread's signal to us may be processed.
    MapImageManagerDeferral deferral; // FIXME: optimized out?

#ifdef WORLDED
    QString suffix = QFileInfo(mapName).suffix();
    if (BMPToTMX::supportedImageFormats().contains(
                suffix, Qt::CaseInsensitive)) {
        QString keyName = QFileInfo(mapName).canonicalFilePath();
        if (mMapImages.contains(keyName)) {
            MapImage *mapImage = mMapImages[keyName];
            retainMapImage(mapImage, owner);
            return mapImage;
        }
        ImageData data = generateBMPImage(mapName);
        if (!data.valid)
            return 0;
        // Abusing the MapInfo struct
        MapInfo *mapInfo = new MapInfo(Map::Isometric,
                                       data.levelZeroBounds.width(),
                                       data.levelZeroBounds.height(), 1, 1);
        MapImage *mapImage = new MapImage(data.image, data.scale,
                                          data.levelZeroBounds, data.mapSize, data.tileSize,
                                          mapInfo, true);
        mapImage->mLoaded = true;
        mMapImages[keyName] = mapImage;
        retainMapImage(mapImage, owner);
        mapImage->chopIntoPieces();
        return mapImage;
    }
#endif

    QString mapFilePath = MapManager::instance()->pathForMap(mapName, relativeTo);
    if (mapFilePath.isEmpty())
        return 0;

    if (mMapImages.contains(mapFilePath)) {
        MapImage *mapImage = mMapImages[mapFilePath];
        retainMapImage(mapImage, owner);
        return mapImage;
    }

    ImageData data = generateMapImage(mapFilePath);
    if (!data.valid)
        return 0;

    MapInfo *mapInfo = MapManager::instance()->mapInfo(mapFilePath);
    if (data.threadLoad || data.threadRender)
        paintDummyImage(data, mapInfo);
    MapImage *mapImage = new MapImage(data.image, data.scale, data.levelZeroBounds, data.mapSize, data.tileSize, mapInfo);
    mapImage->mMissingTilesets = data.missingTilesets;
    mapImage->mLoaded = !(data.threadLoad || data.threadRender);

    if (data.threadLoad || data.threadRender) {
        if (data.threadLoad) {
            QString imageFileName = imageFileInfo(mapFilePath).canonicalFilePath();
            QMetaObject::invokeMethod(mImageReaderWorkers[mNextThreadForJob],
                                      "addJob", Qt::QueuedConnection,
                                      Q_ARG(QString,imageFileName),
                                      Q_ARG(MapImage*,mapImage));
            mNextThreadForJob = (mNextThreadForJob + 1) % mImageReaderWorkers.size();
        }
        if (data.threadRender) {
            queueRenderJob(mapImage);
        }
    }

    // Set up file modification tracking on each TMX that makes
    // up this image.
    QList<MapInfo*> sources;
    for (const QString& source : qAsConst(data.sources)) {
        if (MapInfo *sourceInfo = MapManager::instance()->mapInfo(source)) {
            sources += sourceInfo;
        } else {
            qDebug() << "MapImage source" << source << "not found";
        }
    }
    mapImage->setSources(sources);

    mMapImages.insert(mapFilePath, mapImage);
    retainMapImage(mapImage, owner);
    return mapImage;
}

bool MapImageManager::recreateMapImage(const QString &mapName,
                                       const QString &relativeTo,
                                       QObject *owner)
{
    mError.clear();
    QString mapFilePath = MapManager::instance()->pathForMap(mapName, relativeTo);
    if (mapFilePath.isEmpty()) {
        mError = tr("Map not found: %1").arg(mapName);
        qWarning() << "Cannot recreate thumbnail:" << mError;
        return false;
    }

    MapImage *mapImage = mMapImages.value(mapFilePath, nullptr);
    if (!mapImage) {
        ImageData data = generateMapImage(mapFilePath, true);
        MapInfo *mapInfo = MapManager::instance()->mapInfo(mapFilePath);
        if (!data.valid || !data.threadRender || !mapInfo) {
            if (mError.isEmpty())
                mError = tr("Unable to prepare the thumbnail renderer for %1")
                        .arg(mapFilePath);
            qWarning() << "Cannot recreate thumbnail for" << mapFilePath
                       << mError;
            return false;
        }

        paintDummyImage(data, mapInfo);
        mapImage = new MapImage(data.image, data.scale,
                                data.levelZeroBounds, data.mapSize,
                                data.tileSize, mapInfo);
        mapImage->mLoaded = false;
        mapImage->mSources += mapInfo;
        mMapImages.insert(mapFilePath, mapImage);
        retainMapImage(mapImage, owner);
        queueRenderJob(mapImage);
        qInfo() << "Recreating thumbnail at width" << thumbnailImageWidth()
                << "for" << mapFilePath;
        return true;
    }
    retainMapImage(mapImage, owner);
    if (!mapImage->mLoaded) {
        mForceRebuildAfterLoad.insert(mapImage);
        qInfo() << "Thumbnail recreation queued after current load for" << mapFilePath;
        return true;
    }
    return scheduleMapImageRebuild(mapImage);
}
bool MapImageManager::scheduleMapImageRebuild(MapImage *mapImage)
{
    if (!mapImage || !mapImage->mapInfo())
        return false;
    ImageData data = generateMapImage(mapImage->mapInfo()->path(), true);
    if (!data.valid || !data.threadRender) {
        mError = tr("Unable to prepare the thumbnail renderer for %1")
                .arg(mapImage->mapInfo()->path());
        qWarning() << mError;
        emit mapImageFailedToLoad(mapImage);
        return false;
    }
    paintDummyImage(data, mapImage->mapInfo());
    mapImage->mapFileChanged(data.image, data.scale,
                             data.levelZeroBounds,
                             data.mapSize, data.tileSize);
    mapImage->mSources.clear();
    mapImage->mSources += mapImage->mapInfo();
    mapImage->mLoaded = false;
    queueRenderJob(mapImage);
    emit mapImageChanged(mapImage);
    qInfo() << "Recreating thumbnail at width" << thumbnailImageWidth()
            << "for" << mapImage->mapInfo()->path();
    return true;
}
void MapImageManager::queueRenderJob(MapImage *mapImage)
{
    Q_ASSERT(!mImageRenderWorkers.isEmpty());
    MapImageRenderWorker *worker =
            mImageRenderWorkers.at(mNextRenderWorker);
    mNextRenderWorker =
            (mNextRenderWorker + 1) % mImageRenderWorkers.size();
    const QString imageFileName =
            imageFileInfo(mapImage->mapInfo()->path()).absoluteFilePath();
    QMetaObject::invokeMethod(worker, "addJob", Qt::QueuedConnection,
                              Q_ARG(MapImage*, mapImage),
                              Q_ARG(QString, imageFileName));
}

MapImage *MapImageManager::getZombieSpawnImage(const QString &imageName,
                                               const QString &relativeTo,
                                               QObject *owner)
{
    Q_UNUSED(relativeTo)

    QString keyName = QFileInfo(imageName).canonicalFilePath();
    if (mMapImages.contains(keyName)) {
        MapImage *mapImage = mMapImages[keyName];
        retainMapImage(mapImage, owner);
        return mapImage;
    }
    ImageData data = generateZombieSpawnImage(imageName);
    if (!data.valid) {
        return nullptr;
    }
    // Abusing the MapInfo struct
    MapInfo *mapInfo = new MapInfo(Map::Isometric,
                                   data.levelZeroBounds.width(),
                                   data.levelZeroBounds.height(), 1, 1);
    MapImage *mapImage = new MapImage(data.image, data.scale,
                                      data.levelZeroBounds, data.mapSize, data.tileSize,
                                      mapInfo, true);
    mapImage->mLoaded = true;
    mMapImages[keyName] = mapImage;
    retainMapImage(mapImage, owner);
    mapImage->chopIntoPieces();
    return mapImage;
}

bool MapImageManager::reloadImageFile(const QString &imageName)
{
    const QString keyName = QFileInfo(imageName).canonicalFilePath();
    MapImage *mapImage = mMapImages.value(keyName, nullptr);
    if (!mapImage)
        return true;

    ImageData data = generateBMPImage(imageName);
    if (!data.valid)
        return false;

    mapImage->mImageSize = data.image.size();
    mapImage->mapFileChanged(data.image, data.scale,
                             data.levelZeroBounds, data.mapSize,
                             data.tileSize);
    mapImage->mLoaded = true;
    mapImage->chopIntoPieces();
    emit mapImageChanged(mapImage);
    return true;
}

MapImageManager::ImageData MapImageManager::generateMapImage(const QString &mapFilePath, bool force)
{
#if 0
    if (mapFilePath == QLatin1String("<fail>")) {
        QImage image(thumbnailImageWidth(), 256, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setFont(QFont(QLatin1String("Helvetica"), 48, 1, true));
        painter.drawText(0, 0, image.width(), image.height(), Qt::AlignCenter, QLatin1String("FAIL"));
        return image;
    }
#endif

    QFileInfo fileInfo(mapFilePath);
    QFileInfo imageInfo = imageFileInfo(mapFilePath);
    QFileInfo imageDataInfo = imageDataFileInfo(imageInfo);
    if (!force && imageInfo.exists() && imageDataInfo.exists() && (fileInfo.lastModified() < imageInfo.lastModified())) {
        QImageReader reader(imageInfo.absoluteFilePath());
        if (!reader.size().isValid())
            QMessageBox::warning(MainWindow::instance(), tr("Error Loading Image"),
                                 tr("An error occurred trying to read a map thumbnail image.\n") + imageInfo.absoluteFilePath());
        if (reader.size().width() == thumbnailImageWidth()) {
            ImageData data = readImageData(imageDataInfo);
            // If the image was originally created with some tilesets missing,
            // try to recreate the image in case those tileset issues were
            // resolved.
            if (data.missingTilesets)
                data.valid = false;
            if (data.valid) {
                foreach (QString source, data.sources) {
                    QFileInfo sourceInfo(source);
                    if (sourceInfo.exists() && (sourceInfo.lastModified() > imageInfo.lastModified())) {
                        data.valid = false;
                        break;
                    }
                }
            }
            if (data.valid) {
                data.threadLoad = true;
                data.size = reader.size();
                return data;
            }
        }
    }

    // Create ImageData appropriate for paintDummyImage().
    MapInfo *mapInfo = MapManager::instance()->mapInfo(mapFilePath);
    if (!mapInfo) {
        mError = MapManager::instance()->errorString();
        return ImageData();
    }

    MapRenderer *renderer = NULL;
    Map staticMap(mapInfo->orientation(), mapInfo->width(), mapInfo->height(),
            mapInfo->tileWidth(), mapInfo->tileHeight());
    Map *map = &staticMap;

    switch (map->orientation()) {
    case Map::Isometric:
        renderer = new IsometricRenderer(map);
        break;
    case Map::LevelIsometric:
        renderer = new ZLevelRenderer(map);
        break;
    case Map::Orthogonal:
        renderer = new OrthogonalRenderer(map);
        break;
    case Map::Staggered:
        renderer = new StaggeredRenderer(map);
        break;
    default:
        return ImageData();
    }

    QRectF sceneRect = renderer->boundingRect(QRect(0, 0, map->width(), map->height()));
    QSize mapSize = sceneRect.size().toSize();
    if (mapSize.isEmpty()) {
        delete renderer;
        return ImageData();
    }

    qreal scale = thumbnailImageWidth() / qreal(mapSize.width());
    mapSize *= scale;

    ImageData data;
    data.threadRender = true;
    data.size = mapSize;
    data.scale = scale;
    data.levelZeroBounds = sceneRect;
    data.sources += mapInfo->path();
    data.mapSize = map->size();
    data.tileSize = renderer->boundingRect(QRect(0, 0, 1, 1)).size();
    data.valid = true;
    delete renderer;
    return data;
}

void MapImageManager::paintDummyImage(ImageData &data, MapInfo *mapInfo)
{
    Q_ASSERT(data.size.isValid());
    data.image = QImage(data.size, QImage::Format_ARGB32);
//        data.image.setColorTable(QVector<QRgb>() << qRgb(255,255,255));
    QPainter p(&data.image);
    QPolygonF poly;
    MapImage mapImage(data.image, data.scale, data.levelZeroBounds, data.mapSize, data.tileSize, mapInfo);
    poly += mapImage.tileToImageCoords(0, 0);
    poly += mapImage.tileToImageCoords(mapInfo->width(), 0);
    poly += mapImage.tileToImageCoords(mapInfo->width(), mapInfo->height());
    poly += mapImage.tileToImageCoords(0, mapInfo->height());
    poly += poly.first();
    QPainterPath path;
    path.addPolygon(poly);
    data.image.fill(Qt::transparent);
    p.fillPath(path, QColor(100,100,100));
}

#ifdef WORLDED
// BMP To TMX image thumbnail
MapImageManager::ImageData MapImageManager::generateBMPImage(const QString &bmpFilePath)
{
    QSize imageSize = BMPToTMX::instance()->validateImages(bmpFilePath);
    if (imageSize.isEmpty()) {
        mError = BMPToTMX::instance()->errorString();
        return ImageData();
    }

    // Transform the image to the isometric view
    QTransform xform;
    xform.scale(1.0 / 2, 0.5 / 2);
    xform.shear(-1, 1);
    QRect skewedImageBounds = xform.mapRect(QRect(QPoint(0, 0), imageSize));

    QFileInfo fileInfo(bmpFilePath);
    QFileInfo imageInfo = imageFileInfo(bmpFilePath);
    QFileInfo imageDataInfo = imageDataFileInfo(imageInfo);
    if (imageInfo.exists() && imageDataInfo.exists() &&
            (fileInfo.lastModified() < imageInfo.lastModified())) {
        QImage image(imageInfo.absoluteFilePath());
        if (image.isNull())
            QMessageBox::warning(MainWindow::instance(), tr("Error Loading Image"),
                                 tr("An error occurred trying to read a BMP thumbnail image.\n")
                                 + imageInfo.absoluteFilePath());
        if (image.size() == skewedImageBounds.size()) {
            ImageData data = readImageData(imageDataInfo);
            if (data.valid) {
                data.image = image;
                return data;
            }
        }
    }

    PROGRESS progress(tr("Generating thumbnail for %1").arg(fileInfo.completeBaseName()));

    BMPToTMXImages *images = BMPToTMX::instance()->getImages(bmpFilePath, QPoint());
    if (!images)
        return ImageData();

#if 1
    QImage bmpRecolored = images->mBmp.convertToFormat(QImage::Format_ARGB32);
    images->mBmp = QImage();
    QRgb ruleColor = qRgb(255, 0, 0);
    QRgb treeColor = qRgb(47, 76, 64);
    const QImage vegetation =
            images->mBmpVeg.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < vegetation.height(); ++y) {
        for (int x = 0; x < vegetation.width(); ++x) {
            if (vegetation.pixel(x, y) == ruleColor)
                bmpRecolored.setPixel(x, y, treeColor);
        }
    }
#else
    QImage bmpRecolored(images->mBmp);
    for (int x = 0; x < images->mBmp.width(); x++) {
        for (int y = 0; y < images->mBmp.height(); y++) {
            if (images->mBmpVeg.pixel(x, y) == qRgb(255, 0, 0))
                bmpRecolored.setPixel(x, y, qRgb(47, 76, 64));
        }
    }
#endif

    const int cellSize = images->mCellSize;
    delete images; // ***** ***** *****

    ImageData data;
    data.image = bmpRecolored.transformed(xform);
    data.scale = 1.0f;
    data.levelZeroBounds = QRectF(
                0, 0, imageSize.width() / double(cellSize),
                imageSize.height() / double(cellSize));
    data.valid = true;

    data.image.save(imageInfo.absoluteFilePath());
    writeImageData(imageDataInfo, data);

    return data;
}

MapImageManager::ImageData MapImageManager::generateZombieSpawnImage(const QString &imageFilePath)
{
    QImage image(imageFilePath);
    if (image.isNull()) {
        mError = tr("Zombie spawn image couldn't be loaded.");
        return ImageData();
    }
    image = image.scaledToWidth(image.width() * 10); // Each pixel == one 10x10 chunk

    QSize imageSize = image.size();
    if (imageSize.isEmpty()) {
        mError = tr("Zombie spawn image is empty.");
        return ImageData();
    }

    // Transform the image to the isometric view
    QTransform xform;
    xform.scale(1.0 / 2, 0.5 / 2);
    xform.shear(-1, 1);
    QRect skewedImageBounds = xform.mapRect(QRect(QPoint(0, 0), imageSize));

    QFileInfo fileInfo(imageFilePath);
    QFileInfo imageInfo = imageFileInfo(imageFilePath);
    QFileInfo imageDataInfo = imageDataFileInfo(imageInfo);
    if (imageInfo.exists() && imageDataInfo.exists() &&
            (fileInfo.lastModified() < imageInfo.lastModified())) {
        QImage image(imageInfo.absoluteFilePath());
        if (image.isNull()) {
            QMessageBox::warning(MainWindow::instance(), tr("Error Loading Image"),
                                 tr("An error occurred trying to read the zombie spawn image thumbnail.\n")
                                 + imageInfo.absoluteFilePath());
        }
        if (image.size() == skewedImageBounds.size()) {
            ImageData data = readImageData(imageDataInfo);
            if (data.valid) {
                data.image = image;
                return data;
            }
        }
    }

    PROGRESS progress(tr("Generating thumbnail for %1").arg(fileInfo.completeBaseName()));

    ImageData data;
    data.image = image.transformed(xform);
    data.scale = 1.0f;
    data.levelZeroBounds = QRectF(0, 0, imageSize.width() / 300.0, imageSize.height() / 300.0);
    data.valid = true;

    data.image.save(imageInfo.absoluteFilePath());
    writeImageData(imageDataInfo, data);

    return data;
}
#endif // WORLDED

#define IMAGE_DATA_MAGIC 0xB15B00B5
#define IMAGE_DATA_VERSION 5

MapImageManager::ImageData MapImageManager::readImageData(const QFileInfo &imageDataFileInfo)
{
    ImageData data;
    QFile file(imageDataFileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return data;

    QDataStream in(&file);

    quint32 magic = 0;
    in >> magic;
    if (magic != IMAGE_DATA_MAGIC)
        return data;

    quint32 version = 0;
    in >> version;
    if (version != IMAGE_DATA_VERSION)
        return data;
    in.setVersion(QDataStream::Qt_4_0);

    in >> data.scale;

    qreal x = 0;
    qreal y = 0;
    qreal w = 0;
    qreal h = 0;
    in >> x >> y >> w >> h;
    data.levelZeroBounds.setCoords(x, y, x + w, y + h);

    qint32 count = 0;
    in >> count;
    if (count < 0 || qint64(count) >
            file.bytesAvailable() / qint64(sizeof(quint32)))
        return data;
    for (int i = 0; i < count; i++) {
        QString source;
        in >> source;
        if (in.status() != QDataStream::Ok)
            return data;
        data.sources += source;
    }

    in >> data.missingTilesets;

    qint32 wid = 0;
    qint32 hgt = 0;
    in >> wid >> hgt;
    data.mapSize = QSize(wid, hgt);

    in >> wid >> hgt;
    data.tileSize = QSize(wid, hgt);

    if (in.status() != QDataStream::Ok ||
            !qIsFinite(data.scale) || data.scale <= 0 ||
            !qIsFinite(x) || !qIsFinite(y) ||
            !qIsFinite(w) || !qIsFinite(h) || w <= 0 || h <= 0 ||
            data.mapSize.width() <= 0 || data.mapSize.height() <= 0 ||
            data.tileSize.width() <= 0 || data.tileSize.height() <= 0)
        return data;
    data.valid = true;

    return data;
}

void MapImageManager::writeImageData(const QFileInfo &imageDataFileInfo, const MapImageManager::ImageData &data)
{
    QFile file(imageDataFileInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return;

    QDataStream out(&file);
    out << quint32(IMAGE_DATA_MAGIC);
    out << quint32(IMAGE_DATA_VERSION);
    out.setVersion(QDataStream::Qt_4_0);
    out << data.scale;
    QRectF r = data.levelZeroBounds;
    out << r.x() << r.y() << r.width() << r.height();
    out << qint32(data.sources.length());
    foreach (QString source, data.sources)
        out << source;
    out << data.missingTilesets;
    out << (qint32)data.mapSize.width() << (qint32)data.mapSize.height();
    out << (qint32)data.tileSize.width() << (qint32)data.tileSize.height();
}

void MapImageManager::mapAboutToChange(MapInfo *mapInfo)
{
    for (auto it = mActiveRenders.begin(); it != mActiveRenders.end(); ++it) {
        for (MapComposite *composite : it.key()->maps()) {
            if (composite->mapInfo() == mapInfo) {
                it.value().thread->interrupt(true);
                it.value().mapImage->mLoaded = false;
                break;
            }
        }
    }
}

void MapImageManager::mapChanged(MapInfo *mapInfo)
{
    for (auto it = mActiveRenders.begin(); it != mActiveRenders.end(); ++it) {
        for (MapComposite *composite : it.key()->maps()) {
            if (composite->mapInfo() == mapInfo) {
                it.value().thread->resume();
                QMetaObject::invokeMethod(it.value().worker,
                                      "resume", Qt::QueuedConnection,
                                      Q_ARG(MapImage*, it.value().mapImage),
                                      Q_ARG(QString,
                                            imageFileInfo(it.value().mapImage
                                                          ->mapInfo()->path())
                                            .absoluteFilePath()));
                break;
            }
        }
    }
}

void MapImageManager::mapFileChanged(MapInfo *mapInfo)
{
    QMap<QString,MapImage*>::iterator it_begin = mMapImages.begin();
    QMap<QString,MapImage*>::iterator it_end = mMapImages.end();
    QMap<QString,MapImage*>::iterator it;

    MapImageManagerDeferral deferral; // FIXME: optimized out?

    for (it = it_begin; it != it_end; it++) {
        MapImage *mapImage = it.value();
        if (mapImage->sources().contains(mapInfo)) {
            if (mapImage->mLoaded) {
                scheduleMapImageRebuild(mapImage);
            }
        }
    }
}

void MapImageManager::imageLoadedByThread(QImage *image, MapImage *mapImage)
{
    mapImage->setImage(*image);
    mapImage->mLoaded = true;
    delete image;

    if (discardMapImageIfUnused(mapImage))
        return;
    if (mForceRebuildAfterLoad.remove(mapImage)) {
        scheduleMapImageRebuild(mapImage);
        return;
    }
    if (mDeferralDepth > 0)
        mDeferredMapImages += mapImage;
    else
        emit mapImageChanged(mapImage);
}

void MapImageManager::renderThreadNeedsMap(MapImage *mapImage)
{
    MapImageRenderWorker *worker =
            qobject_cast<MapImageRenderWorker*>(sender());
    Q_ASSERT(worker);
    if (!worker)
        return;
    Q_ASSERT(!mRenderPreparations.contains(worker));
    if (mRenderPreparations.contains(worker))
        return;
    beginRenderMapPreparation(worker, mapImage);
}
void MapImageManager::beginRenderMapPreparation(
        MapImageRenderWorker *worker, MapImage *mapImage)
{
    RenderPreparation preparation;
    preparation.mapImage = mapImage;
    mRenderPreparations.insert(worker, preparation);
    bool asynch = true;
    MapInfo *mapInfo = MapManager::instance()->loadMap(mapImage->mapInfo()->path(),
                                                       QString(), asynch,
                                                       MapManager::PriorityLow);
    if (!mapInfo) {
        failRenderMapPreparation(worker);
        return;
    }
    RenderPreparation &active = mRenderPreparations[worker];
    active.discoveredMaps.insert(mapInfo);
    active.pendingMaps.insert(mapInfo);
    Q_ASSERT(mapInfo == mapImage->mapInfo());
    if (!mapInfo->isLoading())
        processLoadedMapForPreparation(worker, mapInfo);
}

void MapImageManager::imageRenderedByThread(MapImageData imgData,
                                            MapImage *mapImage,
                                            bool imageSaved,
                                            QString imageFileName)
{
    noise() << "imageRenderedByThread" << mapImage->mapInfo()->path();

    mapImage->mImage = imgData.image;
    mapImage->mLevelZeroBounds = imgData.levelZeroBounds;
    mapImage->mScale = imgData.scale;
    mapImage->mMissingTilesets = imgData.missingTilesets;
    mapImage->mSources = imgData.sources;
    mapImage->mMapSize = imgData.mapSize;
    mapImage->mTileSize = imgData.tileSize;
    mapImage->mLoaded = true;

    ImageData data;
    data.image = mapImage->image();
    data.levelZeroBounds = mapImage->levelZeroBounds();
    data.scale = mapImage->scale();
    foreach (MapInfo *mapInfo, mapImage->sources())
        data.sources += mapInfo->path();
    data.missingTilesets = mapImage->isMissingTilesets();
    data.mapSize = imgData.mapSize;
    data.tileSize = imgData.tileSize;

    QFileInfo imageInfo(imageFileName);
    QFileInfo imageDataInfo = imageDataFileInfo(imageInfo);
    if (imageInfo.absoluteFilePath().isEmpty() || !imageSaved) {
        mError = tr("Unable to write thumbnail image %1")
                .arg(imageInfo.absoluteFilePath());
        qWarning() << mError;
        emit mapImageFailedToLoad(mapImage);
    } else {
        writeImageData(imageDataInfo, data);
        qInfo() << "Thumbnail saved to" << imageInfo.absoluteFilePath();
    }
    if (mForceRebuildAfterLoad.remove(mapImage)) {
        if (discardMapImageIfUnused(mapImage))
            return;
        scheduleMapImageRebuild(mapImage);
        return;
    }

    if (discardMapImageIfUnused(mapImage))
        return;
    if (mDeferralDepth > 0)
        mDeferredMapImages += mapImage;
    else
        emit mapImageChanged(mapImage);
}

void MapImageManager::imageRenderFailedByThread(MapImage *mapImage)
{
    mForceRebuildAfterLoad.remove(mapImage);
    mapImage->mImage.fill(Qt::transparent);
    mapImage->mLoaded = true;
    emit mapImageFailedToLoad(mapImage);
    discardMapImageIfUnused(mapImage);
}

void MapImageManager::renderJobDone(MapComposite *mapComposite)
{
    Q_ASSERT(mActiveRenders.contains(mapComposite));
    mActiveRenders.remove(mapComposite);
    delete mapComposite;
}

#include "mapobject.h"
QStringList getSubMapFileNames(const MapInfo *mapInfo)
{
    QStringList ret;
    const QString relativeTo = QFileInfo(mapInfo->path()).absolutePath();
    foreach (ObjectGroup *objectGroup, mapInfo->map()->objectGroups()) {
        foreach (MapObject *object, objectGroup->objects()) {
            if (object->name() == QLatin1String("lot") && !object->type().isEmpty()) {
                QString path = MapManager::instance()->pathForMap(object->type(), relativeTo);
                if (!path.isEmpty())
                    ret += path;
            }
        }
    }
    return ret;
}

void MapImageManager::mapLoaded(MapInfo *mapInfo)
{
    const QList<MapImageRenderWorker*> workers = mRenderPreparations.keys();
    for (MapImageRenderWorker *worker : workers) {
        const auto it = mRenderPreparations.constFind(worker);
        if (it != mRenderPreparations.constEnd()
                && it->pendingMaps.contains(mapInfo)) {
            processLoadedMapForPreparation(worker, mapInfo);
        }
    }
}

void MapImageManager::processLoadedMapForPreparation(
        MapImageRenderWorker *worker, MapInfo *mapInfo)
{
    QList<MapInfo*> loadedMaps;
    loadedMaps += mapInfo;
    while (!loadedMaps.isEmpty()) {
        MapInfo *loadedMap = loadedMaps.takeFirst();
        auto it = mRenderPreparations.find(worker);
        if (it == mRenderPreparations.end()
                || !it->pendingMaps.remove(loadedMap)) {
            continue;
        }
#ifdef WORLDED
        MapManager::instance()->addReferenceToMap(loadedMap);
        it->referencedMaps += loadedMap;
#endif
        const QStringList subMapPaths = getSubMapFileNames(loadedMap);
        for (const QString &path : subMapPaths) {
            bool async = true;
            MapInfo *subMapInfo = MapManager::instance()->loadMap(
                        path, QString(), async, MapManager::PriorityLow);
            if (!subMapInfo)
                continue;
            it = mRenderPreparations.find(worker);
            if (it == mRenderPreparations.end())
                return;
            if (it->discoveredMaps.contains(subMapInfo))
                continue;
            it->discoveredMaps.insert(subMapInfo);
            it->pendingMaps.insert(subMapInfo);
            if (!subMapInfo->isLoading())
                loadedMaps += subMapInfo;
        }
    }

    const auto it = mRenderPreparations.constFind(worker);
    if (it != mRenderPreparations.constEnd() && it->pendingMaps.isEmpty())
        finishRenderMapPreparation(worker);
}

void MapImageManager::finishRenderMapPreparation(
        MapImageRenderWorker *renderWorker)
{
    Q_ASSERT(mRenderPreparations.contains(renderWorker));
    const RenderPreparation preparation =
            mRenderPreparations.take(renderWorker);
    MapImage *renderMapImage = preparation.mapImage;
    MapInfo *mapInfo = renderMapImage->mapInfo();
    MapComposite *renderComposite = new MapComposite(mapInfo);
    Q_ASSERT(renderComposite->waitingForMapsToLoad() == false);
#ifdef WORLDED
    // Now that mapComposite is referencing the maps...
    for (MapInfo *referencedMap : preparation.referencedMaps)
        MapManager::instance()->removeReferenceToMap(referencedMap);
#endif

    // BmpBlender sends a signal to the MapComposite when it has finished
    // blending.  That needs to happen in the render thread.
    Q_ASSERT(renderComposite->bmpBlender()->parent() == renderComposite);
    InterruptibleThread *renderThread = renderWorker->workerThread();
    mActiveRenders.insert(renderComposite,
                          ActiveRender{renderWorker, renderThread,
                                       renderMapImage});
    renderComposite->moveToThread(renderThread);
    QMetaObject::invokeMethod(renderWorker,
                              "mapLoaded", Qt::QueuedConnection,
                              Q_ARG(MapComposite*, renderComposite));
}

void MapImageManager::failRenderMapPreparation(
        MapImageRenderWorker *renderWorker)
{
    const auto it = mRenderPreparations.find(renderWorker);
    if (it == mRenderPreparations.end())
        return;
    const RenderPreparation preparation = it.value();
    mRenderPreparations.erase(it);
#ifdef WORLDED
    for (MapInfo *referencedMap : preparation.referencedMaps)
        MapManager::instance()->removeReferenceToMap(referencedMap);
#endif
    MapImage *mapImage = preparation.mapImage;
    mForceRebuildAfterLoad.remove(mapImage);
    mapImage->mImage.fill(Qt::transparent);
    mapImage->mLoaded = true;
    QMetaObject::invokeMethod(renderWorker,
                              "mapFailedToLoad", Qt::QueuedConnection);
    emit mapImageFailedToLoad(mapImage);
    discardMapImageIfUnused(mapImage);
}
void MapImageManager::mapFailedToLoad(MapInfo *mapInfo)
{
    const QList<MapImageRenderWorker*> workers = mRenderPreparations.keys();
    for (MapImageRenderWorker *worker : workers) {
        auto it = mRenderPreparations.find(worker);
        if (it == mRenderPreparations.end()
                || !it->pendingMaps.contains(mapInfo)) {
            continue;
        }
        if (it->mapImage->mapInfo() == mapInfo) {
            failRenderMapPreparation(worker);
            continue;
        }
        it->pendingMaps.remove(mapInfo);
        if (it->pendingMaps.isEmpty())
            finishRenderMapPreparation(worker);
    }
}

QFileInfo MapImageManager::imageFileInfo(const QString &mapFilePath)
{
    QString thumbnailsDirectory = Preferences::instance()->thumbnailsDirectory();
    if (thumbnailsDirectory.isEmpty() || !QFileInfo::exists(thumbnailsDirectory)) {
        QFileInfo mapFileInfo(mapFilePath);
        QDir mapDir = mapFileInfo.absoluteDir();
        if (!mapDir.exists()) {
            return QFileInfo();
        }
        QFileInfo imagesDirInfo(mapDir, QLatin1String(".pzeditor"));
        if (!imagesDirInfo.exists()) {
            if (!mapDir.mkdir(QLatin1String(".pzeditor"))) {
                return QFileInfo();
            }
        }

        // Need to distinguish BMPToTMX image formats, so include .png or .bmp
        // in the file name.
        QString suffix;
        if (mapFileInfo.suffix() != QLatin1String("tmx")) {
            suffix = QLatin1String("_") + mapFileInfo.suffix();
        }

        return QFileInfo(imagesDirInfo.absoluteFilePath() + QLatin1Char('/') +
                         mapFileInfo.completeBaseName() + suffix + QLatin1String(".png"));
    }

    QFileInfo thumbnailDirInfo(thumbnailsDirectory);
    QDir thumbnailDir(thumbnailDirInfo.absoluteFilePath());
    QString canonicalDir = QFileInfo(mapFilePath).absoluteDir().canonicalPath();
#ifdef Q_OS_WINDOWS
    int colon = canonicalDir.indexOf(QLatin1Char(':'));
    if (colon != -1) {
        QString driveLetter = canonicalDir.left(colon);
        canonicalDir = QLatin1String("Drive") + driveLetter + canonicalDir.mid(colon + 1);
    }
#endif
    QFileInfo imagesDirInfo(thumbnailDir, canonicalDir);
    if (!imagesDirInfo.exists()) {
        if (!imagesDirInfo.dir().mkpath(imagesDirInfo.absoluteFilePath())) {
            return QFileInfo();
        }
    }

    // Need to distinguish BMPToTMX image formats, so include .png or .bmp
    // in the file name.
    QString suffix;
    QFileInfo mapFileInfo(mapFilePath);
    if (mapFileInfo.suffix() != QLatin1String("tmx")) {
        suffix = QLatin1String("_") + mapFileInfo.suffix();
    }

    return QFileInfo(imagesDirInfo.absoluteFilePath() + QLatin1Char('/') +
                     mapFileInfo.completeBaseName() + suffix + QLatin1String(".png"));
}

QFileInfo MapImageManager::imageDataFileInfo(const QFileInfo &imageFileInfo)
{
    return QFileInfo(imageFileInfo.absolutePath() + QLatin1Char('/') +
                     imageFileInfo.completeBaseName() + QLatin1String(".dat"));
}

void MapImageManager::deferThreadResults(bool defer)
{
    if (defer) {
        ++mDeferralDepth;
//        noise() << "MapImageManager::deferThreadResults depth++ =" << mDeferralDepth;
    } else {
        Q_ASSERT(mDeferralDepth > 0);
//        noise() << "MapImageManager::deferThreadResults depth-- =" << mDeferralDepth - 1;
        if (--mDeferralDepth == 0) {
            if (!mDeferralQueued && mDeferredMapImages.size()) {
                QMetaObject::invokeMethod(this, "processDeferrals", Qt::QueuedConnection);
                mDeferralQueued = true;
            }
        }
    }
}

void MapImageManager::processDeferrals()
{
    QList<MapImage*> mapImages = mDeferredMapImages;
    mDeferredMapImages.clear();
    mDeferralQueued = false;
    foreach (MapImage *mapImage, mapImages)
        emit mapImageChanged(mapImage);
}

///// ///// ///// ///// /////

MapImage::MapImage(QImage image, qreal scale, const QRectF &levelZeroBounds,
                   const QSize &mapSize, const QSize &tileSize,
                   MapInfo *mapInfo, bool ownsMapInfo)
    : mImage(image)
    , mInfo(mapInfo)
    , mLevelZeroBounds(levelZeroBounds)
    , mScale(scale)
    , mMissingTilesets(false)
    , mMapSize(mapSize)
    , mTileSize(tileSize)
    , mLoaded(false)
    , mOwnsMapInfo(ownsMapInfo)
#ifdef WORLDED
    , mImageSize(image.size())
#endif
{
}

MapImage::~MapImage()
{
    if (mOwnsMapInfo)
        delete mInfo;
}

QPointF MapImage::tileToPixelCoords(qreal x, qreal y)
{
    const int tileWidth = mTileSize.width();
    const int tileHeight = mTileSize.height();
    const int originX = mMapSize.height() * tileWidth / 2;

    return QPointF((x - y) * tileWidth / 2 + originX,
                   (x + y) * tileHeight / 2);
}

QRectF MapImage::tileBoundingRect(const QRect &rect)
{
    const int tileWidth = mTileSize.width();
    const int tileHeight = mTileSize.height();

    const int originX = mMapSize.height() * tileWidth / 2;
    const QPoint pos((rect.x() - (rect.y() + rect.height()))
                     * tileWidth / 2 + originX,
                     (rect.x() + rect.y()) * tileHeight / 2);

    const int side = rect.height() + rect.width();
    const QSize size(side * tileWidth / 2,
                     side * tileHeight / 2);

    return QRect(pos, size);
}

QRectF MapImage::bounds()
{
    return tileBoundingRect(QRect(QPoint(), mMapSize));
}

qreal MapImage::scale()
{
    return mScale;
}

QPointF MapImage::tileToImageCoords(qreal x, qreal y)
{
    QPointF pos = tileToPixelCoords(x, y);
    pos += mLevelZeroBounds.topLeft();
    return pos * scale();
}

void MapImage::mapFileChanged(QImage image, qreal scale, const QRectF &levelZeroBounds, const QSize &mapSize, const QSize &tileSize)
{
    mImage = image;
    mScale = scale;
    mLevelZeroBounds = levelZeroBounds;
    mMapSize = mapSize;
    mTileSize = tileSize;
}

#ifdef WORLDED
void MapImage::chopIntoPieces()
{
    int columns = subImageColumns();
    int rows = subImageRows();
    mSubImages.resize(columns * rows);
    QRect r(QPoint(), image().size());
    for (int x = 0; x < columns; x++) {
        for (int y = 0; y < rows; y++) {
            QRect subr = QRect(x * 512, y * 512, 512, 512) & r;
            mSubImages[x + y * columns] = image().copy(subr).convertToFormat(QImage::Format_ARGB4444_Premultiplied);
        }
    }
    mMiniMapImage = mImage.scaledToWidth(512);
    mImage = QImage();
}
#endif /* WORLDED */

/////

MapImageReaderWorker::MapImageReaderWorker(InterruptibleThread *thread) :
    BaseWorker(thread)
{
}

MapImageReaderWorker::~MapImageReaderWorker()
{
}

void MapImageReaderWorker::work()
{
    IN_WORKER_THREAD

    while (mJobs.size()) {

        if (aborted()) {
            mJobs.clear();
            return;
        }

        Job job = mJobs.takeAt(0);

        QImage *image = new QImage(job.imageFileName);
#ifdef WORLDED
        if (!image->isNull())
            *image = image->convertToFormat(QImage::Format_ARGB4444_Premultiplied);
#endif // WORLDED

#ifndef QT_NO_DEBUG
        Sleep::msleep(250);
#endif
        emit imageLoaded(image, job.mapImage);
    }
}

void MapImageReaderWorker::addJob(const QString &imageFileName, MapImage *mapImage)
{
    IN_WORKER_THREAD

    mJobs += Job(imageFileName, mapImage);
    scheduleWork();
}

/////

MapImageRenderWorker::MapImageRenderWorker(InterruptibleThread *thread) :
    BaseWorker(thread)
{
}

MapImageRenderWorker::~MapImageRenderWorker()
{
}

void MapImageRenderWorker::work()
{
    IN_WORKER_THREAD

    while (mJobs.size()) {
        if (aborted()) {
            return;
        }

        if (!mJobs.at(0).mapComposite) {
            preventWork(); // until mapLoaded() or mapFailedToLoad()
            emit mapNeeded(mJobs.at(0).mapImage);
            return;
        }

        Job job = mJobs.takeFirst();

        qInfo() << "Thumbnail render started"
                << job.mapImage->mapInfo()->path();
#ifndef QT_NO_DEBUG
//        Sleep::msleep(1000);
#endif
        MapImageData data = generateMapImage(job.mapComposite);
        qInfo() << "Thumbnail render"
                << (aborted() ? "aborted" : "finished")
                << job.mapImage->mapInfo()->path();
        bool imageSaved = false;
        if (data.valid() && !job.imageFileName.isEmpty())
            imageSaved = data.image.save(job.imageFileName);

        emit jobDone(job.mapComposite); // main thread needs to delete this

        if (data.valid())
            emit imageRendered(data, job.mapImage,
                               imageSaved, job.imageFileName);
        else
            emit imageRenderFailed(job.mapImage);
    }
}

void MapImageRenderWorker::addJob(MapImage *mapImage,
                                  const QString &imageFileName)
{
    IN_WORKER_THREAD

    mJobs += Job(mapImage, imageFileName);
    scheduleWork();
}

void MapImageRenderWorker::mapLoaded(MapComposite *mapComposite)
{
    IN_WORKER_THREAD

    foreach (CompositeLayerGroup *layerGroup, mapComposite->sortedLayerGroups()) {
        foreach (TileLayer *tl, layerGroup->layers()) {
            bool isVisible = true;
            if (tl->name().contains(QLatin1String("NoRender")))
                isVisible = false;
            layerGroup->setLayerVisibility(tl, isVisible);
            layerGroup->setLayerOpacity(tl, 1.0f);
        }
        layerGroup->synch();
    }

    Q_ASSERT(mJobs.size());
    Q_ASSERT(mJobs[0].mapComposite == 0);
    mJobs[0].mapComposite = mapComposite;
    allowWork();
    scheduleWork();
}

void MapImageRenderWorker::mapFailedToLoad()
{
    IN_WORKER_THREAD

    mJobs.takeFirst();
    allowWork();
    scheduleWork();
}

void MapImageRenderWorker::resume(MapImage *mapImage,
                                  const QString &imageFileName)
{
    IN_WORKER_THREAD

    mJobs.prepend(Job(mapImage, imageFileName));
    scheduleWork();
}

MapImageData MapImageRenderWorker::generateMapImage(MapComposite *mapComposite)
{
    QReadLocker imageReadLock(&tilesetImageLock());
    Map *map = mapComposite->map();

    MapRenderer *renderer = NULL;

    switch (map->orientation()) {
    case Map::Isometric:
        renderer = new IsometricRenderer(map);
        break;
    case Map::LevelIsometric:
        renderer = new ZLevelRenderer(map);
        break;
    case Map::Orthogonal:
        renderer = new OrthogonalRenderer(map);
        break;
    case Map::Staggered:
        renderer = new StaggeredRenderer(map);
        break;
    default:
        return MapImageData();
    }

    renderer->setShowInvisibleTiles(false);
    renderer->mAbortDrawing = workerThread()->var();

    // Don't draw empty levels
    int minLevel = 0;
    int maxLevel = 0;
    foreach (CompositeLayerGroup *layerGroup, mapComposite->sortedLayerGroups()) {
        if (!layerGroup->bounds().isEmpty()) {
            minLevel = std::min(minLevel, layerGroup->level());
            maxLevel = layerGroup->level();
        }
    }
    renderer->setMinLevel(minLevel);
    renderer->setMaxLevel(maxLevel);

    foreach (MapComposite *mc, mapComposite->maps())
        if (mc->bmpBlender())
            mc->bmpBlender()->flush(QRect(0, 0, mc->map()->width() - 1, mc->map()->height() - 1));

    QRectF sceneRect = mapComposite->boundingRect(renderer);
    QSize mapSize = sceneRect.size().toSize();
    if (mapSize.isEmpty())
        return MapImageData();

    qreal scale = thumbnailImageWidth() / qreal(mapSize.width());
    mapSize *= scale;

    QImage image(mapSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);

    painter.setRenderHints(QPainter::SmoothPixmapTransform |
                           QPainter::Antialiasing);
    painter.setTransform(QTransform::fromScale(scale, scale).translate(-sceneRect.left(), -sceneRect.top()));

    foreach (MapComposite::ZOrderItem zo, mapComposite->zOrder()) {
        if (zo.group) {
            renderer->drawTileLayerGroup(&painter, zo.group);
        } else if (TileLayer *tl = zo.layer->asTileLayer()) {
            if (tl->name().contains(QLatin1String("NoRender")))
                continue;
            renderer->drawTileLayer(&painter, tl);
        }
        if (aborted()) {
            painter.end();
            delete renderer;
            return MapImageData();
        }
    }

    painter.end();

    for (int y = 0; y < image.height(); y++) {
        QRgb *pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); x++) {
            QRgb pixel = pixels[x];
            if (qAlpha(pixel) > 0.01f) {
                pixels[x] = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), 255);
            }
        }
    }

    MapImageData data;
#ifdef WORLDED
    data.image = image.convertToFormat(QImage::Format_ARGB4444_Premultiplied);
#else
    data.image = image;
#endif
    data.scale = scale;
    data.levelZeroBounds = renderer->boundingRect(QRect(0, 0, map->width(), map->height()));
    data.levelZeroBounds.translate(-sceneRect.topLeft());
    foreach (MapComposite *mc, mapComposite->maps())
        data.sources += mc->mapInfo();

    foreach (MapComposite *mc, mapComposite->maps()) {
        if (mc->map()->hasUsedMissingTilesets()) {
            data.missingTilesets = true;
            break;
        }
    }

    data.mapSize = map->size();
    data.tileSize = renderer->boundingRect(QRect(0, 0, 1, 1)).size();

    delete renderer;

    return data;
}

MapImageRenderWorker::Job::Job(MapImage *mapImage,
                               const QString &imageFileName) :
    mapComposite(0),
    mapImage(mapImage),
    imageFileName(imageFileName)
{
}
