#include "tilesetz.h"

#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <QtMath>

namespace TileSetZ {

QSize tileSize(int scale)
{
    return QSize(64, 128) * scale;
}

bool validSize(const QSize &size)
{
    return size.width() > 0 && size.height() > 0
            && size.width() <= 16384 && size.height() <= 16384
            && qint64(size.width()) * size.height() <= 16777216;
}

QImage blank(const QSize &size)
{
    if (!validSize(size))
        return QImage();
    QImage result(size, QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    return result;
}

QImage readImage(const QString &path, QString *error)
{
    QImageReader reader(path);
    if (!validSize(reader.size())) {
        *error = QStringLiteral("The image must contain at most 16,777,216 pixels and measure at most 16,384 pixels on either side.");
        return QImage();
    }
    QImage result = reader.read().convertToFormat(QImage::Format_ARGB32);
    if (result.isNull())
        *error = reader.errorString();
    return result;
}

bool saveImage(const QImage &image, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, "PNG");
    if (!writer.write(image)) {
        *error = writer.errorString();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

static QRgb shade(QRgb pixel)
{
    return qRgba(qMax(0, qRed(pixel) - 31), qMax(0, qGreen(pixel) - 31),
                 qMax(0, qBlue(pixel) - 31), qAlpha(pixel));
}

QImage project(const QImage &texture, Projection projection)
{
    if (texture.isNull())
        return QImage();
    if (projection == Texture)
        return texture.convertToFormat(QImage::Format_ARGB32);
    QImage result = blank(QSize(128, 256));
    if (projection == Corner) {
        QPainter painter(&result);
        painter.drawImage(0, 0, project(texture, RightWall));
        painter.drawImage(0, 0, project(texture, LeftWall));
    } else if (projection == Floor) {
        const QImage material = texture.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        QImage diamond = blank(QSize(256, 128));
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                diamond.setPixel(x - y + 127, (x + y) / 2, material.pixel(x, y));
        QPainter painter(&result);
        painter.drawImage(QRect(-1, 192, 128, 64), diamond);
    } else {
        const QImage material = texture.scaled(64, 193, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        for (int y = 0; y < 193; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool left = projection == LeftWall;
                result.setPixel(x + (left ? 2 : 60), y + 2 + (left ? 31 - x / 2 : x / 2),
                                left ? shade(material.pixel(x, y)) : material.pixel(x, y));
            }
        }
    }
    return result;
}

QImage cut(const QImage &source, int scale, int columns, QSize *layout)
{
    if (source.isNull() || (scale != 1 && scale != 2) || columns < 1 || columns > 128)
        return QImage();
    const QSize size = tileSize(scale);
    const int sourceColumns = (source.width() + size.width() - 1) / size.width();
    const int sourceRows = (source.height() + size.height() - 1) / size.height();
    if (layout)
        *layout = QSize(sourceColumns, sourceRows);
    QImage result = blank(QSize(columns * size.width(),
                               ((sourceColumns * sourceRows + columns - 1) / columns) * size.height()));
    if (result.isNull())
        return result;
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (int y = 0; y < sourceRows; ++y) {
        for (int x = 0; x < sourceColumns; ++x) {
            const int id = y * sourceColumns + x;
            const QRect from(QPoint(x * size.width(), y * size.height()), size);
            const QImage piece = source.copy(from.intersected(source.rect()));
            painter.drawImage((id % columns) * size.width(), (id / columns) * size.height(), piece);
        }
    }
    return result;
}

QSize wallFaceSize(int scale)
{
    return QSize(32, 96) * scale;
}

WallResult wall(const QImage &source, int scale, bool left, int columns, QString *error)
{
    WallResult result;
    if (!validSize(source.size()) || (scale != 1 && scale != 2) || columns < 1 || columns > 128) {
        *error = QStringLiteral("Choose a source texture, a valid tile scale and 1 to 128 sheet columns.");
        return result;
    }
    const QSize face = wallFaceSize(scale);
    const int stepX = face.width();
    const int stepY = 16 * scale;
    const int rise = face.height();
    const int length = (source.width() + stepX - 1) / stepX;
    const int levels = (source.height() + rise - 1) / rise;
    const QSize tile = tileSize(scale);
    const QSize atlasSize(columns * tile.width(), ((length * levels + columns - 1) / columns) * tile.height());
    const QSize assemblySize((length - 1) * stepX + tile.width(),
                              (length - 1) * stepY + (levels - 1) * rise + tile.height());
    if (!validSize(atlasSize)) {
        *error = QStringLiteral("The complete wall cut exceeds the image size limit. Use a smaller source or a different sheet column count.");
        return result;
    }
    result.sourceGrid = QSize(length, levels);
    result.sheet = blank(atlasSize);
    result.assembled = blank(assemblySize);
    QPainter assembly;
    if (!result.assembled.isNull())
        assembly.begin(&result.assembled);
    for (int row = 0; row < levels; ++row) {
        for (int segment = 0; segment < length; ++segment) {
            const int id = row * length + segment;
            const int level = levels - 1 - row;
            const QRect crop(segment * stepX, row * rise, stepX, rise);
            const QImage piece = source.copy(crop.intersected(source.rect())).convertToFormat(QImage::Format_ARGB32);
            const QPoint atlas((id % columns) * tile.width(), (id / columns) * tile.height());
            for (int v = 0; v < piece.height(); ++v) {
                const QRgb *pixels = reinterpret_cast<const QRgb *>(piece.constScanLine(v));
                for (int u = 0; u < piece.width(); ++u) {
                    const int x = left ? u : stepX + u;
                    const int y = v + (left ? stepY - u / 2 : u / 2);
                    result.sheet.setPixel(atlas + QPoint(x, y), pixels[u]);
                }
            }
            const int x = left ? 0 : segment;
            const int y = left ? length - 1 - segment : 0;
            const QPoint screen(segment * stepX, (left ? length - 1 - segment : segment) * stepY
                                + row * rise);
            result.pieces.append({id, x, y, level, screen});
            if (assembly.isActive())
                assembly.drawImage(screen, result.sheet.copy(QRect(atlas, tile)));
        }
    }
    return result;
}

QByteArray wallMap(const WallResult &wall, int scale, const QString &sheetName, const QString &imagePath)
{
    if (wall.sheet.isNull() || wall.pieces.isEmpty() || (scale != 1 && scale != 2))
        return QByteArray();
    int width = 1, height = 1, levels = 1;
    for (const WallPiece &piece : wall.pieces) {
        width = qMax(width, piece.x + 1);
        height = qMax(height, piece.y + 1);
        levels = qMax(levels, piece.level + 1);
    }
    QByteArray result;
    QXmlStreamWriter xml(&result);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("map"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeAttribute(QStringLiteral("orientation"), QStringLiteral("levelisometric"));
    auto number = [&xml](const QString &name, int value) { xml.writeAttribute(name, QString::number(value)); };
    number(QStringLiteral("width"), width);
    number(QStringLiteral("height"), height);
    number(QStringLiteral("tilewidth"), 64);
    number(QStringLiteral("tileheight"), 32);
    xml.writeStartElement(QStringLiteral("tileset"));
    number(QStringLiteral("firstgid"), 1);
    xml.writeAttribute(QStringLiteral("name"), sheetName);
    number(QStringLiteral("tilewidth"), 64);
    number(QStringLiteral("tileheight"), 128);
    xml.writeStartElement(QStringLiteral("image"));
    xml.writeAttribute(QStringLiteral("source"), imagePath);
    number(QStringLiteral("width"), wall.sheet.width() / scale);
    number(QStringLiteral("height"), wall.sheet.height() / scale);
    xml.writeEndElement();
    xml.writeEndElement();
    for (int level = 0; level < levels; ++level) {
        QVector<int> cells(width * height, 0);
        for (const WallPiece &piece : wall.pieces)
            if (piece.level == level)
                cells[piece.x + piece.y * width] = piece.id + 1;
        QStringList values;
        for (int value : cells)
            values.append(QString::number(value));
        xml.writeStartElement(QStringLiteral("layer"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("%1_Walls").arg(level));
        number(QStringLiteral("width"), width);
        number(QStringLiteral("height"), height);
        xml.writeStartElement(QStringLiteral("data"));
        xml.writeAttribute(QStringLiteral("encoding"), QStringLiteral("csv"));
        xml.writeCharacters(values.join(QLatin1Char(',')));
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return result;
}

QImage masked(const QImage &texture, const QImage &mask, bool alphaMask)
{
    if (texture.isNull() || mask.isNull())
        return QImage();
    const QImage material = texture.convertToFormat(QImage::Format_ARGB32);
    const QImage stencil = mask.convertToFormat(QImage::Format_ARGB32);
    QImage result = blank(stencil.size());
    if (result.isNull())
        return result;
    for (int y = 0; y < result.height(); ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        const QRgb *ink = reinterpret_cast<const QRgb *>(stencil.constScanLine(y));
        const QRgb *in = reinterpret_cast<const QRgb *>(material.constScanLine(y % material.height()));
        for (int x = 0; x < result.width(); ++x) {
            if (alphaMask || ink[x] == qRgb(255, 0, 105)) {
                const QRgb pixel = in[x % material.width()];
                const int alpha = alphaMask ? (qAlpha(pixel) * qAlpha(ink[x]) + 127) / 255 : qAlpha(pixel);
                out[x] = alpha ? qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), alpha) : 0;
            }
        }
    }
    return result;
}

static QImage roofTexture(const QImage &source)
{
    QImage clipped = blank(QSize(128, 128));
    {
        QPainter painter(&clipped);
        painter.setClipRect(0, 0, 80, 128);
        painter.drawImage(QRect(0, 0, 128, 256), source);
    }
    const qreal tangent = qTan(qDegreesToRadians(6.0));
    QImage skewed = blank(QSize(128 + int(128 * tangent), 128));
    {
        QPainter painter(&skewed);
        painter.shear(-tangent, 0);
        painter.drawImage(int(128 * tangent), 0, clipped);
    }
    const qreal radians = qDegreesToRadians(26.95);
    QImage rotated = blank(QSize(int(skewed.width() * qCos(radians) + skewed.height() * qSin(radians)),
                                int(skewed.width() * qSin(radians) + skewed.height() * qCos(radians))));
    {
        QPainter painter(&rotated);
        painter.translate(rotated.width() / 2.0, rotated.height() / 2.0);
        painter.rotate(26.95);
        painter.translate(-skewed.width() / 2.0, -skewed.height() / 2.0);
        painter.drawImage(0, 0, skewed);
    }
    QRect bounds;
    for (int y = 0; y < rotated.height(); ++y)
        for (int x = 0; x < rotated.width(); ++x)
            if (qAlpha(rotated.pixel(x, y)))
                bounds |= QRect(x, y, 1, 1);
    return bounds.isEmpty() ? blank(QSize(134, 134))
                           : rotated.copy(bounds).scaled(134, 134, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QStringList maskFiles()
{
    return { QStringLiteral("mask_left"), QStringLiteral("mask_right"), QStringLiteral("mask_corner"),
             QStringLiteral("mask_trim"), QStringLiteral("mask_floor"), QStringLiteral("mask_street_curbs_blend"),
             QStringLiteral("mask_left_roof"), QStringLiteral("mask_right_roof"), QStringLiteral("mask_trim_roof"),
             QStringLiteral("mask_roof_left_bottom"), QStringLiteral("mask_roof_left_middle"), QStringLiteral("mask_roof_left_top"),
             QStringLiteral("mask_roof_right_bottom"), QStringLiteral("mask_roof_right_middle"), QStringLiteral("mask_roof_right_top"),
             QStringLiteral("mask_custom_left"), QStringLiteral("mask_custom_right"), QStringLiteral("mask_custom_corner"),
             QStringLiteral("mask_custom_trim") };
}

QImage generate(const QImage &source, Preset preset, int scale,
                const QColor &trim, const QImage &externalMask,
                Projection projection, bool alphaMask, QString *error)
{
    if (source.isNull() || (scale != 1 && scale != 2)) {
        *error = QStringLiteral("Choose a source texture and a valid tile scale.");
        return QImage();
    }
    if (preset == ExternalMask) {
        QImage result = masked(project(source, projection), externalMask, alphaMask);
        if (result.isNull() || result.width() % 128 || result.height() % 256) {
            *error = QStringLiteral("A custom mask must be a PNG with dimensions divisible by 128 x 256. Masks use 2x coordinates.");
            return QImage();
        }
        return scale == 2 ? result : result.scaled(result.size() / 2, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    QImage result;
    auto add = [&](const QImage &texture, const QString &name, bool isTrim = false) {
        const QImage mask(QStringLiteral(":/tilesetz/%1.png").arg(name));
        if (mask.isNull()) {
            *error = QStringLiteral("Missing TileSetZ mask: %1").arg(name);
            return false;
        }
        if (result.isNull())
            result = blank(mask.size());
        if (result.isNull() || mask.size() != result.size()) {
            *error = QStringLiteral("Incompatible TileSetZ mask size: %1").arg(name);
            return false;
        }
        QImage layer;
        if (isTrim) {
            layer = mask.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < layer.height(); ++y) {
                QRgb *pixels = reinterpret_cast<QRgb *>(layer.scanLine(y));
                for (int x = 0; x < layer.width(); ++x)
                    if (pixels[x] == qRgb(99, 99, 99))
                        pixels[x] = trim.rgba();
            }
        } else {
            layer = masked(texture, mask, false);
        }
        QPainter painter(&result);
        painter.drawImage(0, 0, layer);
        return true;
    };
    bool ok = true;
    if (preset == Floors || preset == Curbs) {
        ok = add(project(source, Floor), preset == Floors ? QStringLiteral("mask_floor") : QStringLiteral("mask_street_curbs_blend"));
    } else if (preset == Roofs) {
        const QImage roof = roofTexture(source);
        const QStringList parts = { QStringLiteral("bottom"), QStringLiteral("middle"), QStringLiteral("top") };
        const int offsets[] = {124, 62, -2};
        for (int side = 0; side < 2 && ok; ++side) {
            for (int part = 0; part < 3 && ok; ++part) {
                QImage face = blank(QSize(128, 256));
                {
                    QPainter painter(&face);
                    painter.drawImage(0, offsets[part], roof);
                }
                if (side == 0) {
                    face = face.mirrored(true, false);
                    for (int y = 0; y < face.height(); ++y) {
                        QRgb *pixels = reinterpret_cast<QRgb *>(face.scanLine(y));
                        for (int x = 0; x < face.width(); ++x)
                            pixels[x] = shade(pixels[x]);
                    }
                }
                ok = add(face, QStringLiteral("mask_roof_%1_%2")
                         .arg(side == 0 ? QStringLiteral("right") : QStringLiteral("left"), parts[part]));
            }
        }
    } else {
        const QString prefix = preset == CustomWalls ? QStringLiteral("mask_custom_") : QStringLiteral("mask_");
        const QString suffix = preset == RoofWalls ? QStringLiteral("_roof") : QString();
        ok = add(project(source, LeftWall), prefix + QStringLiteral("left") + suffix)
                && add(project(source, RightWall), prefix + QStringLiteral("right") + suffix);
        if (ok && preset != RoofWalls)
            ok = add(project(source, Corner), prefix + QStringLiteral("corner"));
        if (ok)
            ok = add(QImage(), prefix + QStringLiteral("trim") + suffix, true);
    }
    if (!ok)
        return QImage();
    return scale == 2 ? result : result.scaled(result.size() / 2, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QRect tileRect(const QImage &sheet, int scale, int id)
{
    if (scale != 1 && scale != 2)
        return QRect();
    const QSize size = tileSize(scale);
    const int columns = sheet.width() / size.width();
    if (columns <= 0 || id < 0 || id >= columns * (sheet.height() / size.height()))
        return QRect();
    return QRect(QPoint((id % columns) * size.width(), (id / columns) * size.height()), size);
}

QImage insert(const QImage &sheet, const QImage &tiles, int scale, int firstId, QString *error)
{
    if (scale != 1 && scale != 2) {
        *error = QStringLiteral("Choose a valid tile scale.");
        return QImage();
    }
    const QSize size = tileSize(scale);
    if (sheet.isNull() || tiles.isNull() || firstId < 0
            || sheet.width() % size.width() || sheet.height() % size.height()
            || tiles.width() % size.width() || tiles.height() % size.height()) {
        *error = QStringLiteral("Both images must contain complete tiles.");
        return QImage();
    }
    const int columns = sheet.width() / size.width();
    const int count = (tiles.width() / size.width()) * (tiles.height() / size.height());
    const qint64 rows = (qint64(firstId) + count + columns - 1) / columns;
    if (rows * size.height() > 16384) {
        *error = QStringLiteral("The resulting sheet is too large.");
        return QImage();
    }
    QImage result = blank(QSize(sheet.width(), qMax(sheet.height(), int(rows) * size.height())));
    if (result.isNull()) {
        *error = QStringLiteral("The resulting sheet exceeds the image size limit.");
        return QImage();
    }
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, sheet);
    for (int i = 0; i < count; ++i)
        painter.drawImage(tileRect(result, scale, firstId + i).topLeft(), tiles.copy(tileRect(tiles, scale, i)));
    return result;
}

bool hasPixels(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)))
                return true;
    return false;
}

QImage clean(const QImage &image)
{
    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) < 5)
                out[x] = 0;
        }
    }
    return result;
}

}
