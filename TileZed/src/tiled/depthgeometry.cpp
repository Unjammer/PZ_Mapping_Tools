#include "depthgeometry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QVector4D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int CoordMultiplier = 10000;
constexpr int DepthWidth = 128;
constexpr int DepthHeight = 256;
constexpr float Pi = 3.14159265358979323846f;

struct TextBlock
{
    int start = -1;
    int open = -1;
    int close = -1;

    bool isValid() const { return start >= 0 && open >= 0 && close > open; }
};

QString maskedText(const QString &text)
{
    QString result = text;
    bool lineComment = false;
    bool blockComment = false;
    bool quoted = false;
    for (int i = 0; i < result.size(); ++i) {
        const QChar c = result.at(i);
        const QChar next = i + 1 < result.size() ? result.at(i + 1) : QChar();
        if (lineComment) {
            if (c == QLatin1Char('\n'))
                lineComment = false;
            else
                result[i] = QLatin1Char(' ');
            continue;
        }
        if (blockComment) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                result[i] = result[i + 1] = QLatin1Char(' ');
                ++i;
                blockComment = false;
            } else if (c != QLatin1Char('\n')) {
                result[i] = QLatin1Char(' ');
            }
            continue;
        }
        if (!quoted && c == QLatin1Char('/') && next == QLatin1Char('/')) {
            result[i] = result[i + 1] = QLatin1Char(' ');
            ++i;
            lineComment = true;
            continue;
        }
        if (!quoted && c == QLatin1Char('/') && next == QLatin1Char('*')) {
            result[i] = result[i + 1] = QLatin1Char(' ');
            ++i;
            blockComment = true;
            continue;
        }
        if (c == QLatin1Char('"'))
            quoted = !quoted;
        if (quoted && c != QLatin1Char('\n'))
            result[i] = QLatin1Char(' ');
    }
    return result;
}

bool isWordCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

QVector<TextBlock> findBlocks(const QString &text, const QString &type,
                              int rangeStart = 0, int rangeEnd = -1)
{
    const QString mask = maskedText(text);
    if (rangeEnd < 0 || rangeEnd > mask.size())
        rangeEnd = mask.size();
    QVector<TextBlock> result;
    int cursor = qMax(0, rangeStart);
    while ((cursor = mask.indexOf(type, cursor, Qt::CaseSensitive)) >= 0 &&
           cursor < rangeEnd) {
        const int endWord = cursor + type.size();
        if ((cursor > 0 && isWordCharacter(mask.at(cursor - 1))) ||
                (endWord < mask.size() &&
                 isWordCharacter(mask.at(endWord)))) {
            cursor = endWord;
            continue;
        }
        int open = endWord;
        while (open < rangeEnd && mask.at(open).isSpace())
            ++open;
        if (open >= rangeEnd || mask.at(open) != QLatin1Char('{')) {
            cursor = endWord;
            continue;
        }
        int depth = 1;
        int close = open + 1;
        while (close < rangeEnd && depth > 0) {
            if (mask.at(close) == QLatin1Char('{'))
                ++depth;
            else if (mask.at(close) == QLatin1Char('}'))
                --depth;
            ++close;
        }
        if (depth == 0) {
            TextBlock block;
            block.start = cursor;
            block.open = open;
            block.close = close - 1;
            result += block;
            cursor = close;
        } else {
            break;
        }
    }
    return result;
}

QString valueInBlock(const QString &text, const TextBlock &block,
                     const QString &key)
{
    if (!block.isValid())
        return QString();
    const QString body = text.mid(block.open + 1,
                                  block.close - block.open - 1);
    const QRegularExpression expression(
        QStringLiteral("(?:^|[,\\r\\n])\\s*%1\\s*=\\s*([^,\\r\\n}]*)")
        .arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch match = expression.match(body);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

int coordinate(const QString &text, bool version2)
{
    bool ok = false;
    const float value = version2
            ? text.trimmed().toInt(&ok) / float(CoordMultiplier)
            : text.trimmed().toFloat(&ok);
    return qRound(ok ? value * CoordMultiplier : 0.0f);
}

float parseCoordinate(const QString &text, bool version2)
{
    return coordinate(text, version2) / float(CoordMultiplier);
}

QVector3D parseVector3(const QString &text, bool version2)
{
    const QStringList values = text.trimmed().split(QLatin1Char('x'));
    if (values.size() != 3)
        return QVector3D();
    return QVector3D(parseCoordinate(values.at(0), version2),
                     parseCoordinate(values.at(1), version2),
                     parseCoordinate(values.at(2), version2));
}

QString formatCoordinate(float value)
{
    return QString::number(qRound(value * CoordMultiplier));
}

QString formatVector3(const QVector3D &value)
{
    return QStringLiteral("%1x%2x%3")
            .arg(formatCoordinate(value.x()),
                 formatCoordinate(value.y()),
                 formatCoordinate(value.z()));
}

QString planeName(DepthPolygonPlane plane)
{
    switch (plane) {
    case DepthPolygonPlane::XY: return QStringLiteral("XY");
    case DepthPolygonPlane::XZ: return QStringLiteral("XZ");
    case DepthPolygonPlane::YZ: return QStringLiteral("YZ");
    }
    return QStringLiteral("XZ");
}

DepthPolygonPlane parsePlane(const QString &value)
{
    if (value.trimmed() == QLatin1String("XY"))
        return DepthPolygonPlane::XY;
    if (value.trimmed() == QLatin1String("YZ"))
        return DepthPolygonPlane::YZ;
    return DepthPolygonPlane::XZ;
}

QMatrix4x4 projectionMatrix()
{
    const float s = std::sqrt(2.0f);
    QMatrix4x4 projection;
    projection.ortho(-s / 2.0f, s / 2.0f, -s, s, -2.0f, 2.0f);
    projection.translate(0.0f, -2.0f * s * 0.375f, 0.0f);
    return projection;
}

QMatrix4x4 modelViewMatrix()
{
    QMatrix4x4 modelView;
    modelView.rotate(30.0f, 1.0f, 0.0f, 0.0f);
    modelView.rotate(315.0f, 0.0f, 1.0f, 0.0f);
    return modelView;
}

QMatrix4x4 objectMatrix(const DepthPrimitive &primitive)
{
    QMatrix4x4 matrix;
    matrix.translate(primitive.translate);
    matrix.rotate(primitive.rotate.x(), 1.0f, 0.0f, 0.0f);
    matrix.rotate(primitive.rotate.y(), 0.0f, 1.0f, 0.0f);
    matrix.rotate(primitive.rotate.z(), 0.0f, 0.0f, 1.0f);
    return matrix;
}

QVector3D mappedPoint(const QMatrix4x4 &matrix, const QVector3D &point)
{
    const QVector4D mapped = matrix * QVector4D(point, 1.0f);
    if (!qFuzzyIsNull(mapped.w()))
        return mapped.toVector3DAffine();
    return mapped.toVector3D();
}

QVector3D mappedDirection(const QMatrix4x4 &matrix,
                          const QVector3D &direction)
{
    return (matrix * QVector4D(direction, 0.0f)).toVector3D();
}

bool cameraRay(float pixelX, float pixelY,
               QVector3D *origin, QVector3D *direction)
{
    const float uiX = pixelX / 128.0f;
    const float uiY = 2.0f - pixelY / 128.0f;
    const float ndcX = uiX * 2.0f - 1.0f;
    const float ndcY = uiY - 1.0f;
    bool invertible = false;
    const QMatrix4x4 inverse =
            (projectionMatrix() * modelViewMatrix()).inverted(&invertible);
    if (!invertible)
        return false;
    const QVector3D nearPoint =
            mappedPoint(inverse, QVector3D(ndcX, ndcY, -1.0f));
    const QVector3D farPoint =
            mappedPoint(inverse, QVector3D(ndcX, ndcY, 1.0f));
    *origin = nearPoint;
    *direction = (farPoint - nearPoint).normalized();
    return true;
}

bool rayBox(const QVector3D &origin, const QVector3D &direction,
            const QVector3D &minimum, const QVector3D &maximum,
            float *nearDistance)
{
    float nearValue = -std::numeric_limits<float>::infinity();
    float farValue = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = direction[axis];
        const float low = minimum[axis];
        const float high = maximum[axis];
        if (qAbs(d) < 0.000001f) {
            if (o < low || o > high)
                return false;
            continue;
        }
        float t1 = (low - o) / d;
        float t2 = (high - o) / d;
        if (t1 > t2)
            std::swap(t1, t2);
        nearValue = qMax(nearValue, t1);
        farValue = qMin(farValue, t2);
        if (nearValue > farValue)
            return false;
    }
    if (farValue < 0.0f)
        return false;
    *nearDistance = nearValue >= 0.0f ? nearValue : farValue;
    return true;
}

bool rayCylinder(const QVector3D &origin, const QVector3D &direction,
                 float radius, float height, float *distance)
{
    QVector<float> candidates;
    const float a = direction.x() * direction.x() +
                    direction.y() * direction.y();
    const float b = 2.0f * (origin.x() * direction.x() +
                            origin.y() * direction.y());
    const float c = origin.x() * origin.x() +
                    origin.y() * origin.y() - radius * radius;
    if (qAbs(a) > 0.000001f) {
        const float discriminant = b * b - 4.0f * a * c;
        if (discriminant >= 0.0f) {
            const float root = std::sqrt(discriminant);
            candidates += (-b - root) / (2.0f * a);
            candidates += (-b + root) / (2.0f * a);
        }
    }
    if (qAbs(direction.z()) > 0.000001f) {
        candidates += (height * 0.5f - origin.z()) / direction.z();
        candidates += (-height * 0.5f - origin.z()) / direction.z();
    }
    std::sort(candidates.begin(), candidates.end());
    for (float candidate : candidates) {
        if (candidate < 0.0f)
            continue;
        const QVector3D hit = origin + direction * candidate;
        const bool onSide =
                qAbs(hit.z()) <= height * 0.5f + 0.0001f &&
                qAbs(hit.x() * hit.x() + hit.y() * hit.y() -
                     radius * radius) < 0.002f;
        const bool onCap =
                hit.x() * hit.x() + hit.y() * hit.y() <=
                    radius * radius + 0.0001f &&
                (qAbs(hit.z() - height * 0.5f) < 0.002f ||
                 qAbs(hit.z() + height * 0.5f) < 0.002f);
        if (onSide || onCap) {
            *distance = candidate;
            return true;
        }
    }
    return false;
}

bool pointInPolygon(const QPointF &point, const QVector<QPointF> &polygon)
{
    bool inside = false;
    for (int i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const QPointF &a = polygon.at(i);
        const QPointF &b = polygon.at(j);
        if (((a.y() > point.y()) != (b.y() > point.y())) &&
                point.x() < (b.x() - a.x()) *
                (point.y() - a.y()) /
                ((b.y() - a.y()) + 0.0000001) + a.x()) {
            inside = !inside;
        }
    }
    return inside;
}

float normalizedDepth(const QVector3D &scenePoint)
{
    const QMatrix4x4 mvp = projectionMatrix() * modelViewMatrix();
    const float depth = mappedPoint(mvp, scenePoint).z();
    const float depthNW =
            qAbs(mappedPoint(mvp, QVector3D(-0.5f, 0.0f, -0.5f)).z());
    if (qFuzzyIsNull(depthNW))
        return -1.0f;
    return depth * ((1.0f / depthNW) * 0.25f) + 0.75f;
}

void appendLine(QVector<QLineF> *lines, const QMatrix4x4 &matrix,
                const QVector3D &a, const QVector3D &b)
{
    *lines += QLineF(DepthGeometryRasterizer::project(
                         mappedPoint(matrix, a)),
                     DepthGeometryRasterizer::project(
                         mappedPoint(matrix, b)));
}

}

DepthPrimitive DepthPrimitive::makeBox()
{
    DepthPrimitive primitive;
    primitive.type = DepthPrimitiveType::Box;
    return primitive;
}

DepthPrimitive DepthPrimitive::makeCylinder()
{
    DepthPrimitive primitive;
    primitive.type = DepthPrimitiveType::Cylinder;
    primitive.height = 3.0f * 0.8164966667f;
    primitive.translate.setY(primitive.height * 0.5f);
    primitive.rotate.setX(270.0f);
    return primitive;
}

DepthPrimitive DepthPrimitive::makePolygon(DepthPolygonPlane selectedPlane)
{
    DepthPrimitive primitive;
    primitive.type = DepthPrimitiveType::Polygon;
    primitive.plane = selectedPlane;
    if (selectedPlane == DepthPolygonPlane::XZ)
        primitive.rotate.setX(270.0f);
    else if (selectedPlane == DepthPolygonPlane::YZ)
        primitive.rotate.setY(90.0f);
    primitive.points = {
        QPointF(-0.5, -0.5), QPointF(0.5, -0.5),
        QPointF(0.5, 0.5), QPointF(-0.5, 0.5)
    };
    return primitive;
}

QString DepthPrimitive::displayName(int index) const
{
    switch (type) {
    case DepthPrimitiveType::Box:
        return QStringLiteral("Box %1").arg(index + 1);
    case DepthPrimitiveType::Cylinder:
        return QStringLiteral("Cylinder %1").arg(index + 1);
    case DepthPrimitiveType::Polygon:
        return QStringLiteral("%1 polygon %2")
                .arg(planeName(plane)).arg(index + 1);
    }
    return QString();
}

bool DepthGeometryDocument::load(const QString &filePath,
                                 const QString &tilesetName,
                                 QString *error)
{
    mTiles.clear();
    mSourceText.clear();
    QFile file(filePath);
    if (!file.exists()) {
        mSourceText = QStringLiteral(
            "tileGeometry\n{\n\tVERSION = 2,\n}\n");
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    mSourceText = QString::fromUtf8(file.readAll());
    const QRegularExpression versionExpression(
                QStringLiteral("\\bVERSION\\s*=\\s*(\\d+)"));
    const QRegularExpressionMatch versionMatch =
            versionExpression.match(maskedText(mSourceText));
    const int version = versionMatch.hasMatch()
            ? versionMatch.captured(1).toInt() : -1;
    if (version != 1 && version != 2) {
        if (error)
            *error = QStringLiteral(
                "Missing or unsupported tileGeometry VERSION (expected 1 or 2).");
        return false;
    }

    TextBlock selectedTileset;
    const QVector<TextBlock> tilesets =
            findBlocks(mSourceText, QStringLiteral("tileset"));
    for (const TextBlock &block : tilesets) {
        if (valueInBlock(mSourceText, block, QStringLiteral("name")) ==
                tilesetName) {
            selectedTileset = block;
            break;
        }
    }
    if (!selectedTileset.isValid())
        return true;

    const QVector<TextBlock> tileBlocks = findBlocks(
                mSourceText, QStringLiteral("tile"),
                selectedTileset.open + 1, selectedTileset.close);
    for (const TextBlock &tileBlock : tileBlocks) {
        const QStringList xy = valueInBlock(
                    mSourceText, tileBlock,
                    QStringLiteral("xy")).split(QLatin1Char('x'));
        if (xy.size() != 2)
            continue;
        bool columnOk = false;
        bool rowOk = false;
        const int column = xy.at(0).trimmed().toInt(&columnOk);
        const int row = xy.at(1).trimmed().toInt(&rowOk);
        if (!columnOk || !rowOk || column < 0 || row < 0)
            continue;
        DepthGeometryTile tile;

        const QVector<TextBlock> boxes = findBlocks(
                    mSourceText, QStringLiteral("box"),
                    tileBlock.open + 1, tileBlock.close);
        for (const TextBlock &block : boxes) {
            DepthPrimitive primitive = DepthPrimitive::makeBox();
            primitive.translate = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("translate")), version == 2);
            primitive.rotate = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("rotate")), version == 2);
            primitive.minimum = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("min")), version == 2);
            primitive.maximum = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("max")), version == 2);
            tile.primitives += primitive;
        }
        const QVector<TextBlock> cylinders = findBlocks(
                    mSourceText, QStringLiteral("cylinder"),
                    tileBlock.open + 1, tileBlock.close);
        for (const TextBlock &block : cylinders) {
            DepthPrimitive primitive = DepthPrimitive::makeCylinder();
            primitive.translate = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("translate")), version == 2);
            primitive.rotate = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("rotate")), version == 2);
            primitive.radius1 = parseCoordinate(valueInBlock(
                mSourceText, block, QStringLiteral("radius1")), version == 2);
            primitive.radius2 = parseCoordinate(valueInBlock(
                mSourceText, block, QStringLiteral("radius2")), version == 2);
            primitive.height = parseCoordinate(valueInBlock(
                mSourceText, block, QStringLiteral("height")), version == 2);
            tile.primitives += primitive;
        }
        const QVector<TextBlock> polygons = findBlocks(
                    mSourceText, QStringLiteral("polygon"),
                    tileBlock.open + 1, tileBlock.close);
        for (const TextBlock &block : polygons) {
            DepthPrimitive primitive = DepthPrimitive::makePolygon(
                parsePlane(valueInBlock(
                    mSourceText, block, QStringLiteral("plane"))));
            primitive.translate = parseVector3(valueInBlock(
                mSourceText, block, QStringLiteral("translate")), version == 2);
            const QString rotation = valueInBlock(
                        mSourceText, block, QStringLiteral("rotate"));
            if (!rotation.isEmpty())
                primitive.rotate = parseVector3(rotation, version == 2);
            primitive.points.clear();
            const QStringList points = valueInBlock(
                        mSourceText, block, QStringLiteral("points"))
                    .split(QRegularExpression(QStringLiteral("\\s+")),
                           Qt::SkipEmptyParts);
            for (const QString &point : points) {
                const QStringList values = point.split(QLatin1Char('x'));
                if (values.size() == 2) {
                    primitive.points += QPointF(
                        parseCoordinate(values.at(0), version == 2),
                        parseCoordinate(values.at(1), version == 2));
                }
            }
            if (primitive.points.size() >= 3)
                tile.primitives += primitive;
        }
        const QVector<TextBlock> properties = findBlocks(
                    mSourceText, QStringLiteral("properties"),
                    tileBlock.open + 1, tileBlock.close);
        if (!properties.isEmpty()) {
            const TextBlock &block = properties.first();
            tile.propertiesBlock = mSourceText.mid(
                        block.start, block.close - block.start + 1).trimmed();
        }
        mTiles.insert(column + row * 8, tile);
    }
    return true;
}

QString DepthGeometryDocument::buildTilesetBlock(
        const QString &tilesetName) const
{
    QString output;
    QTextStream stream(&output);
    stream << "\ttileset\n\t{\n";
    stream << "\t\tname = " << tilesetName << ",\n";
    for (auto it = mTiles.constBegin(); it != mTiles.constEnd(); ++it) {
        if (it.value().primitives.isEmpty() &&
                it.value().propertiesBlock.trimmed().isEmpty())
            continue;
        stream << "\t\ttile /* " << tilesetName << "_" << it.key()
               << " */\n\t\t{\n";
        stream << "\t\t\txy = " << (it.key() % 8) << "x"
               << (it.key() / 8) << ",\n";
        for (const DepthPrimitive &primitive : it.value().primitives) {
            const char *type = primitive.type == DepthPrimitiveType::Box
                    ? "box" : primitive.type == DepthPrimitiveType::Cylinder
                    ? "cylinder" : "polygon";
            stream << "\t\t\t" << type << "\n\t\t\t{\n";
            stream << "\t\t\t\ttranslate = "
                   << formatVector3(primitive.translate) << ",\n";
            stream << "\t\t\t\trotate = "
                   << formatVector3(primitive.rotate) << ",\n";
            if (primitive.type == DepthPrimitiveType::Box) {
                stream << "\t\t\t\tmin = "
                       << formatVector3(primitive.minimum) << ",\n";
                stream << "\t\t\t\tmax = "
                       << formatVector3(primitive.maximum) << ",\n";
            } else if (primitive.type == DepthPrimitiveType::Cylinder) {
                stream << "\t\t\t\tradius1 = "
                       << formatCoordinate(primitive.radius1) << ",\n";
                stream << "\t\t\t\tradius2 = "
                       << formatCoordinate(primitive.radius2) << ",\n";
                stream << "\t\t\t\theight = "
                       << formatCoordinate(primitive.height) << ",\n";
            } else {
                stream << "\t\t\t\tplane = "
                       << planeName(primitive.plane) << ",\n";
                stream << "\t\t\t\tpoints = ";
                for (const QPointF &point : primitive.points) {
                    stream << formatCoordinate(float(point.x())) << "x"
                           << formatCoordinate(float(point.y())) << " ";
                }
                stream << ",\n";
            }
            stream << "\t\t\t}\n";
        }
        if (!it.value().propertiesBlock.trimmed().isEmpty()) {
            QString properties = it.value().propertiesBlock.trimmed();
            properties.replace(QStringLiteral("\n"),
                               QStringLiteral("\n\t\t\t"));
            stream << "\t\t\t" << properties << "\n";
        }
        stream << "\t\t}\n";
    }
    stream << "\t}\n";
    return output;
}

bool DepthGeometryDocument::save(const QString &filePath,
                                 const QString &tilesetName,
                                 QString *error)
{
    QString output = mSourceText;
    if (output.trimmed().isEmpty())
        output = QStringLiteral("tileGeometry\n{\n\tVERSION = 2,\n}\n");
    const QString replacement = buildTilesetBlock(tilesetName);
    TextBlock selectedTileset;
    const QVector<TextBlock> tilesets =
            findBlocks(output, QStringLiteral("tileset"));
    for (const TextBlock &block : tilesets) {
        if (valueInBlock(output, block, QStringLiteral("name")) ==
                tilesetName) {
            selectedTileset = block;
            break;
        }
    }
    if (selectedTileset.isValid()) {
        output.replace(selectedTileset.start,
                       selectedTileset.close - selectedTileset.start + 1,
                       replacement.trimmed());
    } else {
        const QVector<TextBlock> roots =
                findBlocks(output, QStringLiteral("tileGeometry"));
        if (roots.isEmpty()) {
            if (error)
                *error = QStringLiteral(
                    "Cannot locate the tileGeometry root block.");
            return false;
        }
        output.insert(roots.first().close, replacement);
    }

    const QFileInfo info(filePath);
    QDir directory = info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("Cannot create %1")
                    .arg(directory.absolutePath());
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray encoded = output.toUtf8();
    if (file.write(encoded) != encoded.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    mSourceText = output;
    return true;
}

float DepthGeometryRasterizer::depthAt(const DepthPrimitive &primitive,
                                       float pixelX, float pixelY)
{
    QVector3D origin;
    QVector3D direction;
    if (!cameraRay(pixelX, pixelY, &origin, &direction))
        return -1.0f;
    const QMatrix4x4 object = objectMatrix(primitive);
    bool invertible = false;
    const QMatrix4x4 inverse = object.inverted(&invertible);
    if (!invertible)
        return -1.0f;
    const QVector3D localOrigin = mappedPoint(inverse, origin);
    const QVector3D localDirection =
            mappedDirection(inverse, direction).normalized();
    float distance = 0.0f;
    QVector3D localHit;
    if (primitive.type == DepthPrimitiveType::Box) {
        if (!rayBox(localOrigin, localDirection,
                    primitive.minimum, primitive.maximum, &distance))
            return -1.0f;
        localHit = localOrigin + localDirection * distance;
    } else if (primitive.type == DepthPrimitiveType::Cylinder) {
        if (!rayCylinder(localOrigin, localDirection,
                         primitive.radius1, primitive.height, &distance))
            return -1.0f;
        localHit = localOrigin + localDirection * distance;
    } else {
        if (primitive.points.size() < 3 ||
                qAbs(localDirection.z()) < 0.000001f)
            return -1.0f;
        distance = -localOrigin.z() / localDirection.z();
        if (distance < 0.0f)
            return -1.0f;
        localHit = localOrigin + localDirection * distance;
        if (!pointInPolygon(QPointF(localHit.x(), localHit.y()),
                            primitive.points))
            return -1.0f;
    }
    return normalizedDepth(mappedPoint(object, localHit));
}

QImage DepthGeometryRasterizer::rasterize(
        const QVector<DepthPrimitive> &primitives,
        const QImage &sourceTile, bool respectSourceAlpha,
        const QImage &base)
{
    QImage output(DepthWidth, DepthHeight, QImage::Format_ARGB32);
    output.fill(Qt::transparent);
    if (!base.isNull()) {
        const QImage converted = base.convertToFormat(QImage::Format_ARGB32);
        QPainter painter(&output);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(QPoint(), converted);
    }
    const QImage source =
            sourceTile.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < DepthHeight; ++y) {
        for (int x = 0; x < DepthWidth; ++x) {
            if (respectSourceAlpha &&
                    (x >= source.width() || y >= source.height() ||
                     qAlpha(source.pixel(x, y)) == 0))
                continue;
            float nearest = 2.0f;
            for (const DepthPrimitive &primitive : primitives) {
                const float depth = depthAt(
                            primitive, x + 0.5f, y + 0.5f);
                if (depth >= 0.0f)
                    nearest = qMin(nearest, depth);
            }
            if (nearest <= 1.5f) {
                const int value = qBound(
                    0, qRound(qBound(0.0f, nearest, 1.0f) * 255.0f), 255);
                const QRgb existing = output.pixel(x, y);
                if (qAlpha(existing) == 0 || value < qBlue(existing))
                    output.setPixel(x, y,
                                    qRgba(value, value, value, 255));
            }
        }
    }
    return output;
}

QImage DepthGeometryRasterizer::rasterize(
        const DepthPrimitive &primitive, const QImage &sourceTile,
        bool respectSourceAlpha, const QImage &base)
{
    return rasterize(QVector<DepthPrimitive>{primitive}, sourceTile,
                     respectSourceAlpha, base);
}

QPointF DepthGeometryRasterizer::project(const QVector3D &scenePoint)
{
    const QVector3D ndc = mappedPoint(
                projectionMatrix() * modelViewMatrix(), scenePoint);
    return QPointF((ndc.x() + 1.0f) * 64.0f,
                   (1.0f - ndc.y()) * 128.0f);
}

QVector<QLineF> DepthGeometryRasterizer::wireframe(
        const DepthPrimitive &primitive)
{
    QVector<QLineF> lines;
    const QMatrix4x4 object = objectMatrix(primitive);
    if (primitive.type == DepthPrimitiveType::Box) {
        QVector<QVector3D> corners;
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    corners += QVector3D(
                        x ? primitive.maximum.x() : primitive.minimum.x(),
                        y ? primitive.maximum.y() : primitive.minimum.y(),
                        z ? primitive.maximum.z() : primitive.minimum.z());
        const int edges[][2] = {
            {0,1},{0,2},{0,4},{1,3},{1,5},{2,3},
            {2,6},{3,7},{4,5},{4,6},{5,7},{6,7}
        };
        for (const auto &edge : edges)
            appendLine(&lines, object,
                       corners.at(edge[0]), corners.at(edge[1]));
    } else if (primitive.type == DepthPrimitiveType::Cylinder) {
        const int segments = 24;
        for (int i = 0; i < segments; ++i) {
            const float a = float(i) * 2.0f * Pi / segments;
            const float b = float(i + 1) * 2.0f * Pi / segments;
            for (int cap = -1; cap <= 1; cap += 2) {
                appendLine(&lines, object,
                    QVector3D(std::cos(a) * primitive.radius1,
                              std::sin(a) * primitive.radius1,
                              cap * primitive.height * 0.5f),
                    QVector3D(std::cos(b) * primitive.radius1,
                              std::sin(b) * primitive.radius1,
                              cap * primitive.height * 0.5f));
            }
            if (i % 6 == 0) {
                appendLine(&lines, object,
                    QVector3D(std::cos(a) * primitive.radius1,
                              std::sin(a) * primitive.radius1,
                              -primitive.height * 0.5f),
                    QVector3D(std::cos(a) * primitive.radius1,
                              std::sin(a) * primitive.radius1,
                              primitive.height * 0.5f));
            }
        }
    } else if (primitive.points.size() >= 2) {
        for (int i = 0; i < primitive.points.size(); ++i) {
            const QPointF a = primitive.points.at(i);
            const QPointF b =
                    primitive.points.at((i + 1) % primitive.points.size());
            appendLine(&lines, object,
                       QVector3D(a.x(), a.y(), 0.0f),
                       QVector3D(b.x(), b.y(), 0.0f));
        }
    }
    return lines;
}
