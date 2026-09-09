#ifndef PZTOOLS_RASTERTILECACHE_H
#define PZTOOLS_RASTERTILECACHE_H

#include <QCache>
#include <QPaintEngine>
#include <QPainter>
#include <QtMath>

namespace PZTools {

struct RasterTileKey
{
    qint64 image;
    quint32 transform;
    bool operator==(const RasterTileKey &other) const
    {
        return image == other.image && transform == other.transform;
    }
};

inline uint qHash(const RasterTileKey &key, uint seed = 0)
{
    return ::qHash(key.image, seed) ^ ::qHash(key.transform, seed);
}

inline bool drawCachedRasterTile(QPainter *painter, const QImage &image)
{
    if (image.isNull() || image.devicePixelRatio() != 1
            || painter->opacity() != 1
            || painter->compositionMode() != QPainter::CompositionMode_SourceOver
            || !painter->testRenderHint(QPainter::SmoothPixmapTransform)
            || painter->paintEngine()->type() != QPaintEngine::Raster)
        return false;
    const QTransform transform = painter->worldTransform();
    const QTransform deviceTransform = painter->deviceTransform();
    const qreal deviceOffsetX = deviceTransform.dx() - transform.dx();
    const qreal deviceOffsetY = deviceTransform.dy() - transform.dy();
    if (!transform.isAffine() || !deviceTransform.isAffine()
            || transform.m11() != deviceTransform.m11() || transform.m12() != deviceTransform.m12()
            || transform.m21() != deviceTransform.m21() || transform.m22() != deviceTransform.m22()
            || deviceOffsetX != qFloor(deviceOffsetX) || deviceOffsetY != qFloor(deviceOffsetY))
        return false;
    const qreal scale = qMax(qAbs(transform.m11()), qAbs(transform.m12()));
    if (scale != 0.25 && scale != 0.5)
        return false;
    if (!((qAbs(transform.m11()) == scale && qAbs(transform.m22()) == scale
            && transform.m12() == 0 && transform.m21() == 0)
            || (qAbs(transform.m12()) == scale && qAbs(transform.m21()) == scale
                && transform.m11() == 0 && transform.m22() == 0)))
        return false;
    const qreal phaseX = transform.dx() - qFloor(transform.dx());
    const qreal phaseY = transform.dy() - qFloor(transform.dy());
    if (phaseX * 4 != qFloor(phaseX * 4) || phaseY * 4 != qFloor(phaseY * 4))
        return false;
    const QRect bounds = transform.mapRect(QRectF(image.rect())).toAlignedRect();
    if (bounds.isEmpty() || qint64(bounds.width()) * bounds.height() > 1024 * 1024)
        return false;
    const quint32 packed = quint32(qRound(transform.m11() * 4) + 2)
            | quint32(qRound(transform.m12() * 4) + 2) << 3
            | quint32(qRound(transform.m21() * 4) + 2) << 6
            | quint32(qRound(transform.m22() * 4) + 2) << 9
            | quint32(phaseX * 4) << 12 | quint32(phaseY * 4) << 14;
    const RasterTileKey key{image.cacheKey(), packed};
    thread_local QCache<RasterTileKey, QImage> cache(64 * 1024);
    QImage *cached = cache.object(key);
    if (!cached) {
        cached = new QImage(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        if (cached->isNull()) {
            delete cached;
            return false;
        }
        cached->fill(Qt::transparent);
        QPainter tilePainter(cached);
        tilePainter.setRenderHint(QPainter::SmoothPixmapTransform);
        tilePainter.setTransform(transform * QTransform::fromTranslate(-bounds.x(), -bounds.y()));
        tilePainter.drawImage(0, 0, image);
        tilePainter.end();
        cache.insert(key, cached, int((cached->sizeInBytes() + 1023) / 1024));
    }
    painter->setWorldTransform(QTransform());
    painter->drawImage(bounds.topLeft(), *cached);
    painter->setWorldTransform(transform);
    return true;
}

}

#endif
