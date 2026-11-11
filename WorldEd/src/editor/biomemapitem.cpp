#include "biomemapitem.h"
#include "biomemapimageprocessor.h"
#include "world.h"
#include "worlddocument.h"
#include "worldscene.h"
#include "../portablesettings.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QtMath>
#include <array>
#include <cmath>
static int floorDivide(int value, int divisor)
{
    int result = value / divisor;
    if (value < 0 && value % divisor)
        --result;
    return result;
}
BiomeMapItem::BiomeMapItem(WorldScene *scene)
    : QGraphicsItem()
    , mScene(scene)
    , mPixelsPerCell(scene->world()->cellSize())
    , mDisplayZoneChannel(false)
{
    reloadFromSettings();
}
QRectF BiomeMapItem::boundingRect() const
{
    return mMapImageBounds;
}
QPainterPath BiomeMapItem::shape() const
{
    QPainterPath path;
    path.addPolygon(polygon());
    return path;
}
void BiomeMapItem::paint(QPainter *painter,
                         const QStyleOptionGraphicsItem *option,
                         QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    if (mPreviewImage.isNull())
        return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(mMapImageBounds, mPreviewImage,
                       QRectF(QPointF(0, 0), mPreviewImage.size()),
                       Qt::AvoidDither);
}
bool BiomeMapItem::canEdit() const
{
    const QString path = configuredFilePath();
    return isValid() || (!path.isEmpty() && !QFileInfo::exists(path));
}
QPoint BiomeMapItem::imagePointAt(const QPointF &scenePos) const
{
    const QPointF cellPos = mScene->pixelToCellCoords(scenePos);
    return QPoint(qFloor(cellPos.x() * mPixelsPerCell),
                  qFloor(cellPos.y() * mPixelsPerCell));
}
bool BiomeMapItem::containsImagePoint(const QPoint &point) const
{
    return !mSourceImage.isNull()
            && point.x() >= 0 && point.x() < mSourceImage.width()
            && point.y() >= 0 && point.y() < mSourceImage.height();
}
int BiomeMapItem::biomeValueAt(const QPoint &point) const
{
    return containsImagePoint(point)
            ? qRed(mSourceImage.pixel(point))
            : -1;
}
int BiomeMapItem::zoneValueAt(const QPoint &point) const
{
    return containsImagePoint(point)
            ? qGreen(mSourceImage.pixel(point))
            : -1;
}
QPoint BiomeMapItem::worldPixelOrigin() const
{
    const QPoint origin =
            mScene->world()->getGenerateLotsSettings().worldOrigin;
    return QPoint(origin.x() * mPixelsPerCell,
                  origin.y() * mPixelsPerCell);
}
QRect BiomeMapItem::zoneChunkRectAt(const QPoint &point,
                                    int radiusInChunks) const
{
    if (!containsImagePoint(point))
        return QRect();
    const int chunkSize = 8;
    const int radius = qBound(0, radiusInChunks, 16);
    const QPoint absolute = point + worldPixelOrigin();
    const int chunkX = floorDivide(absolute.x(), chunkSize);
    const int chunkY = floorDivide(absolute.y(), chunkSize);
    const QRect absolutePixels(
                (chunkX - radius) * chunkSize,
                (chunkY - radius) * chunkSize,
                (radius * 2 + 1) * chunkSize,
                (radius * 2 + 1) * chunkSize);
    return absolutePixels.translated(-worldPixelOrigin())
            .intersected(mSourceImage.rect());
}
bool BiomeMapItem::ensureEditable(QString *error)
{
    if (!mSourceImage.isNull())
        return true;
    mFilePath = configuredFilePath();
    if (mFilePath.isEmpty()) {
        if (error)
            *error = QObject::tr("No Biomemap output directory is configured.");
        return false;
    }
    if (QFileInfo::exists(mFilePath)) {
        if (error) {
            *error = QObject::tr("The Biomemap file exists but is not a readable image:\n%1")
                    .arg(QDir::toNativeSeparators(mFilePath));
        }
        return false;
    }
    const QSize size = expectedImageSize();
    if (size.isEmpty()) {
        if (error)
            *error = QObject::tr("The current world has no editable Biomemap area.");
        return false;
    }
    mSourceImage = QImage(size, QImage::Format_ARGB32);
    mSourceImage.fill(qRgba(64, 64, 0, 255));
    rebuildPreview();
    synchWithImage();
    update();
    return true;
}
bool BiomeMapItem::reloadFromSettings(bool force, QString *error)
{
    const QString newPath = configuredFilePath();
    if (!force && newPath == mFilePath && !mSourceImage.isNull())
        return true;
    mFilePath = newPath;
    QImage image(mFilePath);
    bool loadedTiles = false;
    if (image.isNull())
        loadedTiles = loadTiles(&image);
    if (image.isNull()) {
        mSourceImage = QImage();
        mPreviewImage = QImage();
        synchWithImage();
        update();
        if (QFileInfo::exists(mFilePath) && error) {
            *error = QObject::tr("The Biomemap file could not be read:\n%1")
                    .arg(QDir::toNativeSeparators(mFilePath));
        }
        return !QFileInfo::exists(mFilePath);
    }
    mSourceImage = image.convertToFormat(QImage::Format_ARGB32);
    rebuildPreview();
    synchWithImage();
    update();
    qInfo() << "Biomemap loaded:"
            << (loadedTiles ? mapsDirectoryPath() : mFilePath)
            << mSourceImage.size()
            << "red channel shown as Biome palette";
    return true;
}
void BiomeMapItem::setDisplayZoneChannel(bool displayZone)
{
    if (mDisplayZoneChannel == displayZone)
        return;
    mDisplayZoneChannel = displayZone;
    rebuildPreview();
    update();
}
void BiomeMapItem::paintBiomeStroke(const QPoint &from, const QPoint &to,
                                    int radius, int biomeValue)
{
    if (mSourceImage.isNull())
        return;
    paintBiomeStrokeOnImage(&mSourceImage, from, to, radius, biomeValue);
    rebuildPreview();
    update();
}
void BiomeMapItem::paintBiomeStrokeOnImage(QImage *image,
                                           const QPoint &from,
                                           const QPoint &to,
                                           int radius, int biomeValue)
{
    if (!image || image->isNull())
        return;
    const QRect affected = QRect(from, to).normalized()
            .adjusted(-radius - 1, -radius - 1, radius + 1, radius + 1)
            .intersected(image->rect());
    if (affected.isEmpty())
        return;
    QImage mask(affected.size(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);
    QPainter maskPainter(&mask);
    maskPainter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(Qt::white);
    pen.setWidth(qMax(1, radius * 2 + 1));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    maskPainter.setPen(pen);
    maskPainter.drawLine(from - affected.topLeft(),
                         to - affected.topLeft());
    maskPainter.end();
    const int value = qBound(0, biomeValue, 255);
    for (int y = affected.top(); y <= affected.bottom(); ++y) {
        const QRgb *maskLine =
                reinterpret_cast<const QRgb *>(
                    mask.constScanLine(y - affected.top()));
        QRgb *target =
                reinterpret_cast<QRgb *>(image->scanLine(y));
        for (int x = affected.left(); x <= affected.right(); ++x) {
            if (qAlpha(maskLine[x - affected.left()]) == 0)
                continue;
            const QRgb old = target[x];
            target[x] = qRgba(value, qGreen(old), qBlue(old), qAlpha(old));
        }
    }
}
void BiomeMapItem::paintZoneStroke(const QPoint &from, const QPoint &to,
                                   int radiusInChunks, int zoneValue)
{
    if (mSourceImage.isNull())
        return;
    paintZoneStrokeOnImage(&mSourceImage, from, to,
                           radiusInChunks, zoneValue, worldPixelOrigin());
    rebuildPreview();
    update();
}
void BiomeMapItem::paintZoneStrokeOnImage(QImage *image,
                                          const QPoint &from,
                                          const QPoint &to,
                                          int radiusInChunks,
                                          int zoneValue,
                                          const QPoint &worldPixelOrigin)
{
    if (!image || image->isNull())
        return;
    const int chunkSize = 8;
    const QPoint absoluteFrom = from + worldPixelOrigin;
    const QPoint absoluteTo = to + worldPixelOrigin;
    const QPoint first(floorDivide(absoluteFrom.x(), chunkSize),
                       floorDivide(absoluteFrom.y(), chunkSize));
    const QPoint last(floorDivide(absoluteTo.x(), chunkSize),
                      floorDivide(absoluteTo.y(), chunkSize));
    const int steps = qMax(qAbs(last.x() - first.x()),
                           qAbs(last.y() - first.y()));
    const int radius = qBound(0, radiusInChunks, 16);
    const int value = qBound(0, zoneValue, 255);
    for (int step = 0; step <= steps; ++step) {
        const qreal amount = steps == 0 ? 0.0 : step / qreal(steps);
        const int centerX = qRound(first.x() +
                                   (last.x() - first.x()) * amount);
        const int centerY = qRound(first.y() +
                                   (last.y() - first.y()) * amount);
        for (int chunkY = centerY - radius;
             chunkY <= centerY + radius; ++chunkY) {
            for (int chunkX = centerX - radius;
                 chunkX <= centerX + radius; ++chunkX) {
                const QRect pixels(chunkX * chunkSize - worldPixelOrigin.x(),
                                   chunkY * chunkSize - worldPixelOrigin.y(),
                                   chunkSize, chunkSize);
                const QRect affected = pixels.intersected(image->rect());
                for (int y = affected.top(); y <= affected.bottom(); ++y) {
                    QRgb *target = reinterpret_cast<QRgb *>(
                                image->scanLine(y));
                    for (int x = affected.left();
                         x <= affected.right(); ++x) {
                        const QRgb old = target[x];
                        target[x] = qRgba(qRed(old), value,
                                          qBlue(old), qAlpha(old));
                    }
                }
            }
        }
    }
}
bool BiomeMapItem::validateChannelPainting(QString *error)
{
    QImage image(24, 16, QImage::Format_ARGB32);
    image.fill(qRgba(179, 64, 33, 255));
    paintBiomeStrokeOnImage(&image, QPoint(2, 2), QPoint(2, 2), 0, 255);
    const QRgb biomePainted = image.pixel(2, 2);
    if (qRed(biomePainted) != 255 || qGreen(biomePainted) != 64 ||
            qBlue(biomePainted) != 33 ||
            image.pixel(7, 7) != qRgba(179, 64, 33, 255)) {
        if (error)
            *error = QStringLiteral("Red-channel painting did not preserve the green and blue channels.");
        return false;
    }
    paintZoneStrokeOnImage(&image, QPoint(9, 1), QPoint(9, 1),
                           0, 115, QPoint());
    for (int y = 0; y < 8; ++y) {
        for (int x = 8; x < 16; ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qGreen(pixel) != 115 || qRed(pixel) != 179 ||
                    qBlue(pixel) != 33) {
                if (error)
                    *error = QStringLiteral("Green-channel painting did not fill one complete 8 x 8 chunk while preserving red and blue.");
                return false;
            }
        }
    }
    if (qGreen(image.pixel(7, 7)) != 64 ||
            qGreen(image.pixel(16, 7)) != 64) {
        if (error)
            *error = QStringLiteral("Green-channel painting escaped its selected 8 x 8 chunk.");
        return false;
    }
    paintZoneStrokeOnImage(&image, QPoint(1, 9), QPoint(23, 9),
                           0, 141, QPoint());
    for (int y = 8; y < 16; ++y) {
        for (int x = 0; x < 24; ++x) {
            if (qGreen(image.pixel(x, y)) != 141) {
                if (error)
                    *error = QStringLiteral("A green-channel drag left a gap between 8 x 8 chunks.");
                return false;
            }
        }
    }
    QImage offsetImage(16, 16, QImage::Format_ARGB32);
    offsetImage.fill(qRgba(192, 64, 0, 255));
    paintZoneStrokeOnImage(&offsetImage, QPoint(0, 0), QPoint(0, 0),
                           0, 102, QPoint(4, 4));
    if (qGreen(offsetImage.pixel(3, 3)) != 102 ||
            qGreen(offsetImage.pixel(4, 3)) != 64 ||
            qGreen(offsetImage.pixel(3, 4)) != 64) {
        if (error)
            *error = QStringLiteral("Green-channel painting was not aligned to the absolute 8 x 8 world grid.");
        return false;
    }
    if (error)
        error->clear();
    return true;
}
bool BiomeMapItem::replaceSourceImage(const QImage &image,
                                      bool saveToDisk,
                                      QString *error)
{
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("The Biomemap image is empty.");
        return false;
    }
    const QImage previous = mSourceImage;
    mSourceImage = image.convertToFormat(QImage::Format_ARGB32);
    rebuildPreview();
    synchWithImage();
    update();
    if (!saveToDisk || save(error))
        return true;
    mSourceImage = previous;
    rebuildPreview();
    synchWithImage();
    update();
    return false;
}
bool BiomeMapItem::save(QString *error)
{
    if (mSourceImage.isNull() || mFilePath.isEmpty()) {
        if (error)
            *error = QObject::tr("No Biomemap image is loaded.");
        return false;
    }
    const QFileInfo fileInfo(mFilePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (error)
            *error = QObject::tr("The Biomemap directory could not be created:\n%1")
                    .arg(QDir::toNativeSeparators(fileInfo.absolutePath()));
        return false;
    }
    const QString backupPath = mFilePath + QLatin1String(".before-paint.bak");
    if (!QFileInfo::exists(backupPath) && QFileInfo::exists(mFilePath)) {
        if (!QFile::copy(mFilePath, backupPath))
            qWarning() << "Unable to create Biomemap backup:" << backupPath;
        else
            qInfo() << "Biomemap backup created:" << backupPath;
    }
    if (!savePngAtomically(mSourceImage, mFilePath, error))
        return false;
    if (!saveTiles(error))
        return false;
    qInfo() << "Biomemap channels saved:" << mFilePath
            << "and biomemap tiles from origin"
            << mScene->world()->getGenerateLotsSettings().worldOrigin;
    return true;
}
QString BiomeMapItem::mapsDirectoryPath() const
{
    const GenerateLotsSettings &settings =
            mScene->world()->getGenerateLotsSettings();
    if (!settings.biomeMap.trimmed().isEmpty())
        return QFileInfo(configuredFilePath()).absolutePath();
    const QString exportRoot = settings.exportDir.isEmpty()
            ? PortableSettings::installRootPath()
            : settings.exportDir;
    return QDir::cleanPath(QDir(exportRoot).filePath(QLatin1String("maps")));
}
QString BiomeMapItem::configuredFilePath() const
{
    const QString configured =
            mScene->world()->getGenerateLotsSettings().biomeMap.trimmed();
    if (!configured.isEmpty()) {
        const QFileInfo configuredInfo(configured);
        if (configuredInfo.isAbsolute())
            return QDir::cleanPath(configured);
        if (!mScene->worldDocument()->fileName().isEmpty()) {
            return QDir(QFileInfo(mScene->worldDocument()->fileName())
                        .absolutePath()).absoluteFilePath(configured);
        }
        return QDir::current().absoluteFilePath(configured);
    }
    return QDir(mapsDirectoryPath()).filePath(QLatin1String("biome.png"));
}
QSize BiomeMapItem::expectedImageSize() const
{
    return mScene->world()->size() * mPixelsPerCell;
}
bool BiomeMapItem::loadTiles(QImage *image) const
{
    if (!image)
        return false;
    const QSize expected = expectedImageSize();
    if (expected.isEmpty())
        return false;
    const int tileSize = 256;
    const QPoint origin =
            mScene->world()->getGenerateLotsSettings().worldOrigin;
    const QPoint sourceWorldOrigin(origin.x() * mPixelsPerCell,
                                   origin.y() * mPixelsPerCell);
    const QRect sourceWorldRect(sourceWorldOrigin, expected);
    const int firstTileX = int(std::floor(sourceWorldRect.left()
                                          / double(tileSize)));
    const int firstTileY = int(std::floor(sourceWorldRect.top()
                                          / double(tileSize)));
    const int lastTileX = int(std::floor(sourceWorldRect.right()
                                         / double(tileSize)));
    const int lastTileY = int(std::floor(sourceWorldRect.bottom()
                                         / double(tileSize)));
    const QDir mapsDir(mapsDirectoryPath());
    QImage assembled(expected, QImage::Format_ARGB32);
    assembled.fill(qRgba(64, 64, 0, 255));
    QPainter painter(&assembled);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    bool found = false;
    for (int tileY = firstTileY; tileY <= lastTileY; ++tileY) {
        for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
            const QString fileName =
                    QStringLiteral("biomemap_%1_%2.png")
                    .arg(tileX).arg(tileY);
            const QImage tile(mapsDir.filePath(fileName));
            if (tile.isNull() || tile.size() != QSize(tileSize, tileSize))
                continue;
            const QRect tileWorldRect(tileX * tileSize, tileY * tileSize,
                                      tileSize, tileSize);
            const QRect covered = tileWorldRect.intersected(sourceWorldRect);
            const QRect sourceRect = covered.translated(
                        -tileWorldRect.topLeft());
            const QPoint destination = covered.topLeft() - sourceWorldOrigin;
            painter.drawImage(destination, tile, sourceRect);
            found = true;
        }
    }
    painter.end();
    if (found)
        *image = assembled;
    return found;
}
bool BiomeMapItem::savePngAtomically(const QImage &image,
                                     const QString &filePath,
                                     QString *error) const
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, "png");
    writer.setCompression(6);
    if (!writer.write(image)) {
        if (error)
            *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
bool BiomeMapItem::saveTiles(QString *error) const
{
    const int tileSize = 256;
    const QPoint origin =
            mScene->world()->getGenerateLotsSettings().worldOrigin;
    const QPoint sourceWorldOrigin(origin.x() * mPixelsPerCell,
                                   origin.y() * mPixelsPerCell);
    const QRect sourceWorldRect(sourceWorldOrigin, mSourceImage.size());
    const int firstTileX = int(std::floor(sourceWorldRect.left()
                                          / double(tileSize)));
    const int firstTileY = int(std::floor(sourceWorldRect.top()
                                          / double(tileSize)));
    const int lastTileX = int(std::floor(sourceWorldRect.right()
                                         / double(tileSize)));
    const int lastTileY = int(std::floor(sourceWorldRect.bottom()
                                         / double(tileSize)));
    const QDir mapsDir(mapsDirectoryPath());
    if (!QDir().mkpath(mapsDir.absolutePath())) {
        if (error)
            *error = QObject::tr("The Biomemap tile directory could not be created:\n%1")
                    .arg(QDir::toNativeSeparators(mapsDir.absolutePath()));
        return false;
    }
    for (int tileY = firstTileY; tileY <= lastTileY; ++tileY) {
        for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
            const QString fileName =
                    QStringLiteral("biomemap_%1_%2.png")
                    .arg(tileX).arg(tileY);
            const QString filePath = mapsDir.filePath(fileName);
            QImage tile(filePath);
            if (tile.isNull() || tile.size() != QSize(tileSize, tileSize)) {
                tile = QImage(tileSize, tileSize, QImage::Format_ARGB32);
                tile.fill(qRgba(64, 64, 0, 255));
            } else {
                tile = tile.convertToFormat(QImage::Format_ARGB32);
            }
            const QRect tileWorldRect(tileX * tileSize, tileY * tileSize,
                                      tileSize, tileSize);
            const QRect covered = tileWorldRect.intersected(sourceWorldRect);
            const QRect sourceRect = covered.translated(-sourceWorldOrigin);
            const QPoint destination =
                    covered.topLeft() - tileWorldRect.topLeft();
            QPainter painter(&tile);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(destination, mSourceImage, sourceRect);
            painter.end();
            if (!savePngAtomically(tile, filePath, error))
                return false;
        }
    }
    return true;
}
void BiomeMapItem::rebuildPreview()
{
    if (mSourceImage.isNull()) {
        mPreviewImage = QImage();
        return;
    }
    QImage visual(mSourceImage.size(), QImage::Format_ARGB32);
    static const auto colors = [] {
        std::array<QRgb, 256> result{};
        for (int value = 0; value < int(result.size()); ++value)
            result[value] = BiomeMapImageProcessor::displayColor(value).rgb();
        return result;
    }();
    for (int y = 0; y < mSourceImage.height(); ++y) {
        const QRgb *source =
                reinterpret_cast<const QRgb *>(mSourceImage.constScanLine(y));
        QRgb *target = reinterpret_cast<QRgb *>(visual.scanLine(y));
        for (int x = 0; x < mSourceImage.width(); ++x) {
            const int value = mDisplayZoneChannel
                    ? qGreen(source[x]) : qRed(source[x]);
            target[x] = colors[value];
        }
    }
    QTransform transform;
    transform.scale(0.5, 0.25);
    transform.shear(-1, 1);
    mPreviewImage = visual.transformed(transform, Qt::FastTransformation);
}
void BiomeMapItem::synchWithImage()
{
    const QRectF bounds = polygon().boundingRect();
    if (bounds != mMapImageBounds) {
        prepareGeometryChange();
        mMapImageBounds = bounds;
    }
}
QRectF BiomeMapItem::imageBounds() const
{
    if (mSourceImage.isNull())
        return QRectF();
    return QRectF(0, 0,
                  mSourceImage.width() / qreal(mPixelsPerCell),
                  mSourceImage.height() / qreal(mPixelsPerCell));
}
QPolygonF BiomeMapItem::polygon() const
{
    return mScene->cellRectToPolygon(imageBounds());
}
