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

BiomeMapItem::BiomeMapItem(WorldScene *scene)
    : QGraphicsItem()
    , mScene(scene)
    , mPixelsPerCell(scene->world()->cellSize())
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
    // 64 is the neutral navigation value used by the generator for both
    // Biome and Zone when no more specific data exists.
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

void BiomeMapItem::paintStroke(const QPoint &from, const QPoint &to,
                               int radius, int biomeValue)
{
    if (mSourceImage.isNull())
        return;

    const QRect affected = QRect(from, to).normalized()
            .adjusted(-radius - 1, -radius - 1, radius + 1, radius + 1)
            .intersected(mSourceImage.rect());
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
                reinterpret_cast<QRgb *>(mSourceImage.scanLine(y));
        for (int x = affected.left(); x <= affected.right(); ++x) {
            if (qAlpha(maskLine[x - affected.left()]) == 0)
                continue;
            const QRgb old = target[x];
            target[x] = qRgba(value, qGreen(old), qBlue(old), qAlpha(old));
        }
    }

    rebuildPreview();
    update();
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

    qInfo() << "Biomemap Biome channel saved:" << mFilePath
            << "and biomemap tiles from origin"
            << mScene->world()->getGenerateLotsSettings().worldOrigin;
    return true;
}

QString BiomeMapItem::mapsDirectoryPath() const
{
    const GenerateLotsSettings &settings =
            mScene->world()->getGenerateLotsSettings();
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
    const int tilesX = (expected.width() + tileSize - 1) / tileSize;
    const int tilesY = (expected.height() + tileSize - 1) / tileSize;
    const QPoint origin =
            mScene->world()->getGenerateLotsSettings().worldOrigin;
    const QDir mapsDir(mapsDirectoryPath());

    QImage assembled(expected, QImage::Format_ARGB32);
    assembled.fill(qRgba(64, 64, 0, 255));
    QPainter painter(&assembled);
    bool found = false;
    for (int y = 0; y < tilesY; ++y) {
        for (int x = 0; x < tilesX; ++x) {
            const QString fileName =
                    QStringLiteral("biomemap_%1_%2.png")
                    .arg(origin.x() + x).arg(origin.y() + y);
            const QImage tile(mapsDir.filePath(fileName));
            if (tile.isNull())
                continue;
            painter.drawImage(QPoint(x * tileSize, y * tileSize), tile);
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
    const int tilesX = (mSourceImage.width() + tileSize - 1) / tileSize;
    const int tilesY = (mSourceImage.height() + tileSize - 1) / tileSize;
    const QPoint origin =
            mScene->world()->getGenerateLotsSettings().worldOrigin;
    const QDir mapsDir(mapsDirectoryPath());

    for (int y = 0; y < tilesY; ++y) {
        for (int x = 0; x < tilesX; ++x) {
            const QImage tile = mSourceImage.copy(
                        x * tileSize, y * tileSize, tileSize, tileSize);
            const QString fileName =
                    QStringLiteral("biomemap_%1_%2.png")
                    .arg(origin.x() + x).arg(origin.y() + y);
            if (!savePngAtomically(tile, mapsDir.filePath(fileName), error))
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
    for (int y = 0; y < mSourceImage.height(); ++y) {
        const QRgb *source =
                reinterpret_cast<const QRgb *>(mSourceImage.constScanLine(y));
        QRgb *target = reinterpret_cast<QRgb *>(visual.scanLine(y));
        for (int x = 0; x < mSourceImage.width(); ++x) {
            const QColor color =
                    BiomeMapImageProcessor::displayColor(qRed(source[x]));
            target[x] = qRgba(color.red(), color.green(), color.blue(), 255);
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
