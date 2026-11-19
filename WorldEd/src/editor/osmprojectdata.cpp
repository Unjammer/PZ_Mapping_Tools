#include "osmprojectdata.h"
#include "BuildingEditor/building.h"
#include "BuildingEditor/buildingreader.h"
#include "InGameMap/ingamemapcell.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"
#include "worldreader.h"
#include "worldwriter.h"
#include "luawriter.h"
#include "road.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSaveFile>
#include <QSet>
#include <QUndoStack>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
namespace {
const QString GENERATED_GROUP = QStringLiteral("OSM Generated");
const QString GENERATED_GROUP_PREFIX = QStringLiteral("OSM Generated - ");
const int DETAILED_TOWN_ZONE_BUILDING_LIMIT = 512;
const int CITY_TOWN_ZONE_GRID = 8;
const int SAFE_CITY_PROXY_BUILDING_LIMIT = 2048;
const int MAX_PROXY_BUILDING_DIMENSION = 1024;
const qint64 MAX_PROXY_BUILDING_AREA = 256LL * 1024;
const int ROAD_MARKING_BUCKET_SIZE = 64;
const QString ROAD_MARKING_STYLE = QStringLiteral("Double Yellow (Faded)");
QString tagValue(const OsmProjectFeature &feature, const QString &key)
{
    return feature.tags.value(key).trimmed();
}
QString normalizedTagValue(const OsmProjectFeature &feature,
                           const QString &key)
{
    return tagValue(feature, key).toLower();
}
QPointF projectOffset(const OsmProjectDataOptions &options, int cellSize)
{
    return QPointF(options.cellOrigin.x() * cellSize,
                   options.cellOrigin.y() * cellSize);
}
QPolygonF translated(const QPolygonF &source, const QPointF &offset)
{
    QPolygonF result = source;
    result.translate(offset);
    return result;
}
bool closeEnough(const QPointF &left, const QPointF &right,
                 double tolerance = 1.5)
{
    return QLineF(left, right).length() <= tolerance;
}
QPolygonF simplifyPolyline(const QPolygonF &source)
{
    QPolygonF result;
    for (const QPointF &point : source) {
        if (result.isEmpty() || !closeEnough(result.last(), point, 0.05))
            result += point;
    }
    for (int index = result.size() - 2; index > 0; --index) {
        const QPointF a = result.at(index - 1);
        const QPointF b = result.at(index);
        const QPointF c = result.at(index + 1);
        const double cross = (b.x() - a.x()) * (c.y() - b.y())
                - (b.y() - a.y()) * (c.x() - b.x());
        if (std::abs(cross) < 0.01)
            result.removeAt(index);
    }
    return result;
}
bool clipSegment(const QPointF &start, const QPointF &end,
                 const QRectF &rect, QPointF *clippedStart,
                 QPointF *clippedEnd)
{
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {
        start.x() - rect.left(), rect.right() - start.x(),
        start.y() - rect.top(), rect.bottom() - start.y()
    };
    for (int index = 0; index < 4; ++index) {
        if (qFuzzyIsNull(p[index])) {
            if (q[index] < 0.0)
                return false;
            continue;
        }
        const double ratio = q[index] / p[index];
        if (p[index] < 0.0)
            t0 = qMax(t0, ratio);
        else
            t1 = qMin(t1, ratio);
        if (t0 > t1)
            return false;
    }
    if (clippedStart)
        *clippedStart = start + (end - start) * t0;
    if (clippedEnd)
        *clippedEnd = start + (end - start) * t1;
    return true;
}
QVector<QPolygonF> clipPolyline(const QPolygonF &source,
                                const QRectF &rect)
{
    QVector<QPolygonF> parts;
    QPolygonF current;
    for (int index = 1; index < source.size(); ++index) {
        QPointF start;
        QPointF end;
        if (!clipSegment(source.at(index - 1), source.at(index), rect,
                         &start, &end)) {
            if (current.size() >= 2)
                parts += simplifyPolyline(current);
            current.clear();
            continue;
        }
        if (current.isEmpty() || !closeEnough(current.last(), start, 0.05)) {
            if (current.size() >= 2)
                parts += simplifyPolyline(current);
            current.clear();
            current += start;
        }
        current += end;
    }
    if (current.size() >= 2)
        parts += simplifyPolyline(current);
    return parts;
}
QPainterPath polygonPath(const OsmProjectFeature &feature,
                         const QPointF &offset)
{
    QPainterPath result;
    result.setFillRule(Qt::OddEvenFill);
    for (QPolygonF polygon : feature.geometries) {
        if (polygon.size() < 3)
            continue;
        polygon.translate(offset);
        if (polygon.first() != polygon.last())
            polygon += polygon.first();
        result.addPolygon(polygon);
    }
    return result;
}
QPainterPath roadPath(const OsmProjectFeature &feature,
                      const QPointF &offset)
{
    QPainterPath centerLine;
    for (const QPolygonF &geometry : feature.geometries) {
        if (geometry.size() < 2)
            continue;
        centerLine.moveTo(geometry.first() + offset);
        for (int index = 1; index < geometry.size(); ++index)
            centerLine.lineTo(geometry.at(index) + offset);
    }
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(1.0, feature.lineWidthSquares));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(centerLine);
}
QPainterPath buildingTownZonePath(const QPainterPath &buildingPath)
{
    if (buildingPath.isEmpty())
        return QPainterPath();
    QPainterPathStroker stroker;
    stroker.setWidth(2.0);
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::MiterJoin);
    stroker.setMiterLimit(2.0);
    return stroker.createStroke(buildingPath)
            .united(buildingPath).simplified();
}
double polygonArea(const QPolygonF &polygon);
QVector<QPolygonF> clippedFillPolygons(const QPainterPath &source,
                                       const QRectF &rect)
{
    QPainterPath clip;
    clip.addRect(rect);
    const QList<QPolygonF> polygons = source.intersected(clip).toFillPolygons();
    QVector<QPolygonF> result;
    for (QPolygonF polygon : polygons) {
        if (polygon.size() > 1 && closeEnough(
                    polygon.first(), polygon.last(), 0.05)) {
            polygon.removeLast();
        }
        polygon = simplifyPolyline(polygon);
        if (polygon.size() >= 3 && polygonArea(polygon) > 0.5)
            result += polygon;
    }
    return result;
}
QPolygonF integerLocalPolygon(const QPolygonF &source,
                              const QPointF &cellTopLeft)
{
    QPolygonF result;
    for (const QPointF &point : source) {
        const QPointF local(qRound(point.x() - cellTopLeft.x()),
                            qRound(point.y() - cellTopLeft.y()));
        if (result.isEmpty() || result.last() != local)
            result += local;
    }
    if (result.size() > 1 && result.first() == result.last())
        result.removeLast();
    return simplifyPolyline(result);
}
QPolygonF integerLocalPolyline(const QPolygonF &source,
                               const QPointF &cellTopLeft)
{
    QPolygonF result;
    for (const QPointF &point : source) {
        const QPointF local(qRound(point.x() - cellTopLeft.x()),
                            qRound(point.y() - cellTopLeft.y()));
        if (result.isEmpty() || result.last() != local)
            result += local;
    }
    return simplifyPolyline(result);
}
double polygonArea(const QPolygonF &polygon)
{
    double area = 0.0;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &first = polygon.at(index);
        const QPointF &second = polygon.at((index + 1) % polygon.size());
        area += first.x() * second.y() - second.x() * first.y();
    }
    return std::abs(area) / 2.0;
}
bool polygonIsRectangle(const QPolygonF &polygon, QRectF *rectangle)
{
    if (polygon.size() < 4)
        return false;
    const QRectF bounds = polygon.boundingRect();
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0)
        return false;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &first = polygon.at(index);
        const QPointF &second = polygon.at((index + 1) % polygon.size());
        if (!qFuzzyCompare(first.x() + 1.0, second.x() + 1.0)
                && !qFuzzyCompare(first.y() + 1.0, second.y() + 1.0)) {
            return false;
        }
    }
    if (std::abs(polygonArea(polygon)
                 - bounds.width() * bounds.height()) > 0.5) {
        return false;
    }
    if (rectangle)
        *rectangle = bounds;
    return true;
}
QString streetSignature(const StreetNameRecord &street)
{
    QByteArray source = street.name.trimmed().toUtf8();
    source += '|';
    source += QByteArray::number(street.width);
    for (const QPointF &point : street.points) {
        source += '|';
        source += QByteArray::number(qRound64(point.x() * 10.0));
        source += ',';
        source += QByteArray::number(qRound64(point.y() * 10.0));
    }
    return QString::fromLatin1(QCryptographicHash::hash(
                source, QCryptographicHash::Sha256).toHex());
}
void stitchStreet(QVector<StreetNameRecord> *streets,
                  StreetNameRecord street)
{
    if (!streets || street.points.size() < 2)
        return;
    street.points = simplifyPolyline(street.points);
    bool joined = true;
    while (joined) {
        joined = false;
        for (int index = 0; index < streets->size(); ++index) {
            StreetNameRecord &other = (*streets)[index];
            if (other.name.compare(street.name, Qt::CaseInsensitive) != 0
                    || other.width != street.width) {
                continue;
            }
            if (closeEnough(other.points.last(), street.points.first())) {
                street.points.removeFirst();
                other.points += street.points;
            } else if (closeEnough(other.points.last(), street.points.last())) {
                std::reverse(street.points.begin(), street.points.end());
                street.points.removeFirst();
                other.points += street.points;
            } else if (closeEnough(other.points.first(), street.points.last())) {
                street.points.removeLast();
                street.points += other.points;
                other.points = street.points;
            } else if (closeEnough(other.points.first(), street.points.first())) {
                std::reverse(street.points.begin(), street.points.end());
                street.points.removeLast();
                street.points += other.points;
                other.points = street.points;
            } else {
                continue;
            }
            other.points = simplifyPolyline(other.points);
            street = other;
            streets->removeAt(index);
            joined = true;
            break;
        }
    }
    streets->append(street);
}
struct NavPolylineRecord
{
    int width = 1;
    QPolygonF points;
};
using CellNavPolylines = QHash<int, QVector<NavPolylineRecord>>;
bool samePolyline(const QPolygonF &left, const QPolygonF &right)
{
    if (left.size() != right.size())
        return false;
    bool forward = true;
    bool reverse = true;
    for (int index = 0; index < left.size(); ++index) {
        forward = forward && closeEnough(
                    left.at(index), right.at(index), 0.05);
        reverse = reverse && closeEnough(
                    left.at(index), right.at(right.size() - 1 - index), 0.05);
    }
    return forward || reverse;
}
bool mergeStraightPolylines(QPolygonF *left, const QPolygonF &right)
{
    if (!left || left->size() != 2 || right.size() != 2)
        return false;
    const QPointF leftVector = left->at(1) - left->at(0);
    const QPointF rightVector = right.at(1) - right.at(0);
    const double directionCross = leftVector.x() * rightVector.y()
            - leftVector.y() * rightVector.x();
    const QPointF offset = right.at(0) - left->at(0);
    const double lineCross = leftVector.x() * offset.y()
            - leftVector.y() * offset.x();
    if (std::abs(directionCross) > 0.01 || std::abs(lineCross) > 0.01)
        return false;
    const bool useX = std::abs(leftVector.x()) >= std::abs(leftVector.y());
    const auto axis = [useX](const QPointF &point) {
        return useX ? point.x() : point.y();
    };
    QVector<QPointF> endpoints = {
        left->at(0), left->at(1), right.at(0), right.at(1)
    };
    const double leftMinimum = qMin(axis(left->at(0)), axis(left->at(1)));
    const double leftMaximum = qMax(axis(left->at(0)), axis(left->at(1)));
    const double rightMinimum = qMin(axis(right.at(0)), axis(right.at(1)));
    const double rightMaximum = qMax(axis(right.at(0)), axis(right.at(1)));
    if (rightMinimum > leftMaximum + 0.05
            || leftMinimum > rightMaximum + 0.05) {
        return false;
    }
    std::sort(endpoints.begin(), endpoints.end(),
              [axis](const QPointF &first, const QPointF &second) {
        return axis(first) < axis(second);
    });
    *left = QPolygonF({endpoints.first(), endpoints.last()});
    return true;
}
void stitchNavPolyline(QVector<NavPolylineRecord> *polylines,
                       NavPolylineRecord polyline)
{
    if (!polylines || polyline.points.size() < 2)
        return;
    polyline.points = simplifyPolyline(polyline.points);
    for (const NavPolylineRecord &other : qAsConst(*polylines)) {
        if (other.width == polyline.width
                && samePolyline(other.points, polyline.points)) {
            return;
        }
    }
    bool joined = true;
    while (joined) {
        joined = false;
        for (int index = 0; index < polylines->size(); ++index) {
            NavPolylineRecord &other = (*polylines)[index];
            if (other.width != polyline.width)
                continue;
            if (mergeStraightPolylines(&other.points, polyline.points)) {
            } else if (closeEnough(other.points.last(),
                            polyline.points.first(), 0.05)) {
                polyline.points.removeFirst();
                other.points += polyline.points;
            } else if (closeEnough(other.points.last(),
                                   polyline.points.last(), 0.05)) {
                std::reverse(polyline.points.begin(), polyline.points.end());
                polyline.points.removeFirst();
                other.points += polyline.points;
            } else if (closeEnough(other.points.first(),
                                   polyline.points.last(), 0.05)) {
                polyline.points.removeLast();
                polyline.points += other.points;
                other.points = polyline.points;
            } else if (closeEnough(other.points.first(),
                                   polyline.points.first(), 0.05)) {
                std::reverse(polyline.points.begin(), polyline.points.end());
                polyline.points.removeLast();
                polyline.points += other.points;
                other.points = polyline.points;
            } else {
                continue;
            }
            other.points = simplifyPolyline(other.points);
            polyline = other;
            polylines->removeAt(index);
            joined = true;
            break;
        }
    }
    polylines->append(polyline);
}
QSet<QString> previousManifestSignatures(const QString &manifestPath,
                                         const QString &key)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QSet<QString> result;
    for (const QJsonValue &value : document.object().value(
             key).toArray()) {
        result += value.toString();
    }
    return result;
}
QSet<QString> previousStreetSignatures(const QString &manifestPath)
{
    return previousManifestSignatures(
                manifestPath, QStringLiteral("streetSignatures"));
}
QString roadSignature(const Road *road)
{
    if (!road)
        return QString();
    QPoint start = road->start();
    QPoint end = road->end();
    if (end.x() < start.x()
            || (end.x() == start.x() && end.y() < start.y())) {
        std::swap(start, end);
    }
    QByteArray source;
    source += QByteArray::number(start.x());
    source += ',';
    source += QByteArray::number(start.y());
    source += '|';
    source += QByteArray::number(end.x());
    source += ',';
    source += QByteArray::number(end.y());
    source += '|';
    source += QByteArray::number(road->width());
    source += '|';
    source += road->tileName().toUtf8();
    source += '|';
    if (road->trafficLines())
        source += road->trafficLines()->name.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(
                source, QCryptographicHash::Sha256).toHex());
}
bool tagDisablesMarking(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QLatin1String("no")
            || normalized == QLatin1String("none")
            || normalized == QLatin1String("false")
            || normalized == QLatin1String("0");
}
bool tagEnablesOneWay(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QLatin1String("yes")
            || normalized == QLatin1String("true")
            || normalized == QLatin1String("1")
            || normalized == QLatin1String("-1")
            || normalized == QLatin1String("reverse");
}
bool roadSupportsCenterMarking(const OsmProjectFeature &feature,
                               bool safeCityMode)
{
    if (!feature.road || feature.lineWidthSquares < 5.5)
        return false;
    const QString highway = normalizedTagValue(
                feature, QStringLiteral("highway"));
    static const QSet<QString> supportedHighways = {
        QStringLiteral("primary"), QStringLiteral("secondary"),
        QStringLiteral("tertiary"), QStringLiteral("residential"),
        QStringLiteral("unclassified")
    };
    if (!supportedHighways.contains(highway))
        return false;
    if (safeCityMode
            && highway != QLatin1String("primary")
            && highway != QLatin1String("secondary")) {
        return false;
    }
    if (tagEnablesOneWay(tagValue(feature, QStringLiteral("oneway")))
            || normalizedTagValue(feature, QStringLiteral("junction"))
               == QLatin1String("roundabout")) {
        return false;
    }
    const QStringList disablingKeys = {
        QStringLiteral("lane_markings"),
        QStringLiteral("road_marking"),
        QStringLiteral("markings"),
        QStringLiteral("centerline"),
        QStringLiteral("centre_line")
    };
    for (const QString &key : disablingKeys) {
        if (tagDisablesMarking(tagValue(feature, key)))
            return false;
    }
    const QString divider = normalizedTagValue(
                feature, QStringLiteral("divider"));
    if (!divider.isEmpty() && !tagDisablesMarking(divider))
        return false;
    const QString dualCarriageway = normalizedTagValue(
                feature, QStringLiteral("dual_carriageway"));
    if (!dualCarriageway.isEmpty()
            && !tagDisablesMarking(dualCarriageway)) {
        return false;
    }
    const QString surface = normalizedTagValue(
                feature, QStringLiteral("surface"));
    static const QSet<QString> unpavedSurfaces = {
        QStringLiteral("unpaved"), QStringLiteral("gravel"),
        QStringLiteral("fine_gravel"), QStringLiteral("dirt"),
        QStringLiteral("earth"), QStringLiteral("ground"),
        QStringLiteral("grass"), QStringLiteral("sand"),
        QStringLiteral("mud")
    };
    if (unpavedSurfaces.contains(surface))
        return false;
    bool lanesOk = false;
    const int lanes = tagValue(feature, QStringLiteral("lanes")).toInt(
                &lanesOk);
    if (lanesOk && lanes <= 1)
        return false;
    if ((highway == QLatin1String("residential")
         || highway == QLatin1String("unclassified"))
            && !(lanesOk && lanes >= 2)
            && tagValue(feature, QStringLiteral("width")).isEmpty()) {
        return false;
    }
    return true;
}
struct RoadMarkingSegment
{
    QPoint start;
    QPoint end;
    int width = 1;
    bool vertical() const
    {
        return start.x() == end.x();
    }
    int axis() const
    {
        return vertical() ? start.x() : start.y();
    }
    int first() const
    {
        return vertical() ? start.y() : start.x();
    }
    int last() const
    {
        return vertical() ? end.y() : end.x();
    }
};
RoadMarkingSegment canonicalRoadMarkingSegment(
        const QPoint &first, const QPoint &second, int width)
{
    RoadMarkingSegment result;
    result.start = first;
    result.end = second;
    result.width = width;
    if (result.end.x() < result.start.x()
            || (result.end.x() == result.start.x()
                && result.end.y() < result.start.y())) {
        std::swap(result.start, result.end);
    }
    return result;
}
quint64 roadMarkingBucketKey(int x, int y)
{
    return (quint64(quint32(x)) << 32) | quint32(y);
}
QVector<RoadMarkingSegment> generateRoadMarkingSegments(
        World *world, const OsmTerrainImportResult &generated,
        const OsmProjectDataOptions &options,
        int *nonOrthogonalSegments)
{
    QVector<RoadMarkingSegment> candidates;
    if (nonOrthogonalSegments)
        *nonOrthogonalSegments = 0;
    const int cellSize = world->cellSize();
    const QPointF offset = projectOffset(options, cellSize);
    const int maximumX = qMax(0, world->width() * cellSize - 1);
    const int maximumY = qMax(0, world->height() * cellSize - 1);
    const QRectF projectBounds(0.0, 0.0, maximumX, maximumY);
    const QRectF importedBounds(
                offset,
                QSizeF(qMax(1, generated.groundImage.width()) - 1,
                       qMax(1, generated.groundImage.height()) - 1));
    const QRectF markingBounds = projectBounds.intersected(importedBounds);
    if (markingBounds.isEmpty())
        return candidates;
    for (const OsmProjectFeature &feature : generated.projectFeatures) {
        if (!roadSupportsCenterMarking(feature, options.safeCityMode))
            continue;
        const int width = qMax(1, qRound(feature.lineWidthSquares));
        for (const QPolygonF &geometry : feature.geometries) {
            const QVector<QPolygonF> parts = clipPolyline(
                        translated(geometry, offset), markingBounds);
            for (const QPolygonF &part : parts) {
                for (int index = 1; index < part.size(); ++index) {
                    const QPointF first = part.at(index - 1);
                    const QPointF second = part.at(index);
                    const double dx = std::abs(second.x() - first.x());
                    const double dy = std::abs(second.y() - first.y());
                    QPoint start;
                    QPoint end;
                    if (dx >= 2.0 && dy <= 1.25) {
                        const int y = qBound(
                                    0, qRound((first.y() + second.y()) / 2.0),
                                    maximumY);
                        start = QPoint(qBound(0, qRound(first.x()), maximumX), y);
                        end = QPoint(qBound(0, qRound(second.x()), maximumX), y);
                    } else if (dy >= 2.0 && dx <= 1.25) {
                        const int x = qBound(
                                    0, qRound((first.x() + second.x()) / 2.0),
                                    maximumX);
                        start = QPoint(x, qBound(0, qRound(first.y()), maximumY));
                        end = QPoint(x, qBound(0, qRound(second.y()), maximumY));
                    } else {
                        if (nonOrthogonalSegments)
                            ++*nonOrthogonalSegments;
                        continue;
                    }
                    RoadMarkingSegment segment = canonicalRoadMarkingSegment(
                                start, end, width);
                    if (segment.last() - segment.first() >= 2)
                        candidates += segment;
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const RoadMarkingSegment &left,
                 const RoadMarkingSegment &right) {
        if (left.vertical() != right.vertical())
            return left.vertical() < right.vertical();
        if (left.axis() != right.axis())
            return left.axis() < right.axis();
        if (left.width != right.width)
            return left.width < right.width;
        if (left.first() != right.first())
            return left.first() < right.first();
        return left.last() < right.last();
    });
    QVector<RoadMarkingSegment> merged;
    for (const RoadMarkingSegment &candidate : qAsConst(candidates)) {
        if (!merged.isEmpty()) {
            RoadMarkingSegment &previous = merged.last();
            if (previous.vertical() == candidate.vertical()
                    && previous.axis() == candidate.axis()
                    && previous.width == candidate.width
                    && candidate.first() <= previous.last() + 1) {
                if (candidate.last() > previous.last())
                    previous.end = candidate.end;
                continue;
            }
        }
        merged += candidate;
    }
    QVector<QVector<int>> cuts(merged.size());
    QHash<quint64, QVector<int>> verticalBuckets;
    for (int index = 0; index < merged.size(); ++index) {
        const RoadMarkingSegment &segment = merged.at(index);
        cuts[index] += segment.first();
        cuts[index] += segment.last();
        if (!segment.vertical())
            continue;
        const int bucketX = segment.axis() / ROAD_MARKING_BUCKET_SIZE;
        const int firstBucket = segment.first() / ROAD_MARKING_BUCKET_SIZE;
        const int lastBucket = segment.last() / ROAD_MARKING_BUCKET_SIZE;
        for (int bucketY = firstBucket; bucketY <= lastBucket; ++bucketY) {
            verticalBuckets[roadMarkingBucketKey(bucketX, bucketY)] += index;
        }
    }
    for (int horizontalIndex = 0; horizontalIndex < merged.size();
         ++horizontalIndex) {
        const RoadMarkingSegment &horizontal = merged.at(horizontalIndex);
        if (horizontal.vertical())
            continue;
        const int bucketY = horizontal.axis() / ROAD_MARKING_BUCKET_SIZE;
        const int firstBucket = horizontal.first() / ROAD_MARKING_BUCKET_SIZE;
        const int lastBucket = horizontal.last() / ROAD_MARKING_BUCKET_SIZE;
        QSet<int> visitedVerticals;
        for (int bucketX = firstBucket; bucketX <= lastBucket; ++bucketX) {
            const QVector<int> verticals = verticalBuckets.value(
                        roadMarkingBucketKey(bucketX, bucketY));
            for (int verticalIndex : verticals) {
                if (visitedVerticals.contains(verticalIndex))
                    continue;
                visitedVerticals += verticalIndex;
                const RoadMarkingSegment &vertical = merged.at(verticalIndex);
                const int crossingX = vertical.axis();
                const int crossingY = horizontal.axis();
                if (crossingX < horizontal.first()
                        || crossingX > horizontal.last()
                        || crossingY < vertical.first()
                        || crossingY > vertical.last()) {
                    continue;
                }
                cuts[horizontalIndex] += crossingX;
                cuts[verticalIndex] += crossingY;
            }
        }
    }
    QVector<RoadMarkingSegment> result;
    for (int index = 0; index < merged.size(); ++index) {
        QVector<int> positions = cuts.at(index);
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()),
                        positions.end());
        const RoadMarkingSegment &source = merged.at(index);
        for (int cutIndex = 1; cutIndex < positions.size(); ++cutIndex) {
            const int first = positions.at(cutIndex - 1);
            const int last = positions.at(cutIndex);
            if (last - first < 2)
                continue;
            const QPoint start = source.vertical()
                    ? QPoint(source.axis(), first)
                    : QPoint(first, source.axis());
            const QPoint end = source.vertical()
                    ? QPoint(source.axis(), last)
                    : QPoint(last, source.axis());
            result += canonicalRoadMarkingSegment(start, end, source.width);
        }
    }
    return result;
}
QSet<QString> addRoadMarkings(WorldDocument *document,
                              const OsmTerrainImportResult &generated,
                              const OsmProjectDataOptions &options,
                              OsmProjectDataSummary *summary)
{
    QSet<QString> signatures;
    TrafficLines *lines = RoadTemplates::instance()->findLines(
                ROAD_MARKING_STYLE);
    if (!lines || lines == RoadTemplates::instance()->nullTrafficLines()) {
        if (summary) {
            summary->warnings += QObject::tr(
                        "Road markings were not generated because %1 is missing from Roads.txt.")
                    .arg(ROAD_MARKING_STYLE);
        }
        return signatures;
    }
    int nonOrthogonalSegments = 0;
    const QVector<RoadMarkingSegment> segments = generateRoadMarkingSegments(
                document->world(), generated, options,
                &nonOrthogonalSegments);
    for (const RoadMarkingSegment &segment : segments) {
        Road *road = new Road(document->world(),
                              segment.start.x(), segment.start.y(),
                              segment.end.x(), segment.end.y(),
                              segment.width, -1);
        road->setTileName(QString());
        road->setTrafficLines(lines);
        document->insertRoad(document->world()->roads().size(), road);
        signatures += roadSignature(road);
    }
    if (summary) {
        summary->roadMarkings = segments.size();
        if (nonOrthogonalSegments > 0) {
            summary->warnings += QObject::tr(
                        "%1 diagonal or curved OSM road segment(s) were left unmarked. Use Detect Road Grid before import to align the dominant street grid.")
                    .arg(nonOrthogonalSegments);
        }
        if (options.safeCityMode) {
            summary->warnings += QObject::tr(
                        "Safe City Mode limited road markings to primary and secondary roads.");
        }
    }
    return signatures;
}
QVector<StreetNameRecord> generateStreets(
        World *world, const OsmTerrainImportResult &generated,
        const OsmProjectDataOptions &options)
{
    QVector<StreetNameRecord> result;
    const int cellSize = world->cellSize();
    const QPointF offset = projectOffset(options, cellSize);
    const QPoint worldOrigin = world->getGenerateLotsSettings().worldOrigin;
    const QRectF projectBounds(0.0, 0.0,
                               world->width() * cellSize,
                               world->height() * cellSize);
    for (const OsmProjectFeature &feature : generated.projectFeatures) {
        if (!feature.road)
            continue;
        const QString name = tagValue(feature, QStringLiteral("name"));
        if (name.isEmpty())
            continue;
        for (const QPolygonF &geometry : feature.geometries) {
            const QVector<QPolygonF> parts = clipPolyline(
                        translated(geometry, offset), projectBounds);
            for (QPolygonF points : parts) {
                points.translate(QPointF(worldOrigin.x() * cellSize,
                                          worldOrigin.y() * cellSize));
                StreetNameRecord street;
                street.name = name;
                street.width = qMax(1, qRound(feature.lineWidthSquares));
                street.points = points;
                stitchStreet(&result, street);
            }
        }
    }
    return result;
}
QString csvGrid(int width, int height, const QString &value)
{
    QString result;
    result.reserve(qint64(width) * height * (value.size() + 1) + height + 2);
    result += QLatin1Char('\n');
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result += value;
            if (x + 1 < width || y + 1 < height)
                result += QLatin1Char(',');
        }
        result += QLatin1Char('\n');
    }
    return result;
}
QByteArray footprintMask(const QPainterPath &path, int left, int top,
                         int width, int height)
{
    QByteArray mask(width * height, '\0');
    int occupied = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!path.contains(QPointF(left + x + 0.5,
                                       top + y + 0.5))) {
                continue;
            }
            mask[y * width + x] = '\1';
            ++occupied;
        }
    }
    if (occupied == 0 && width > 0 && height > 0) {
        const QPointF center = path.boundingRect().center();
        const int x = qBound(0, int(std::floor(center.x())) - left,
                             width - 1);
        const int y = qBound(0, int(std::floor(center.y())) - top,
                             height - 1);
        mask[y * width + x] = '\1';
    }
    return mask;
}
QString roomGrid(int width, int height, const QByteArray &mask)
{
    QString result;
    result.reserve(qint64(width) * height * 2 + height + 2);
    result += QLatin1Char('\n');
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result += mask.at(y * width + x)
                    ? QLatin1Char('1') : QLatin1Char('0');
            if (x + 1 < width || y + 1 < height)
                result += QLatin1Char(',');
        }
        result += QLatin1Char('\n');
    }
    return result;
}
QString roofGrid(int width, int height, const QByteArray &mask)
{
    QString result;
    result.reserve(qint64(width + 1) * (height + 1) * 2 + height + 3);
    result += QLatin1Char('\n');
    for (int y = 0; y <= height; ++y) {
        for (int x = 0; x <= width; ++x) {
            result += x < width && y < height
                    && mask.at(y * width + x)
                    ? QLatin1Char('1') : QLatin1Char('0');
            if (x < width || y < height)
                result += QLatin1Char(',');
        }
        result += QLatin1Char('\n');
    }
    return result;
}
QByteArray proxyTbxData(int width, int height, int levels,
                        const QByteArray &mask)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter xml(&buffer);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("building"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("4"));
    xml.writeAttribute(QStringLiteral("width"), QString::number(width));
    xml.writeAttribute(QStringLiteral("height"), QString::number(height));
    const QStringList buildingTiles = {
        QStringLiteral("ExteriorWall"), QStringLiteral("ExteriorWallTrim"),
        QStringLiteral("Door"), QStringLiteral("DoorFrame"),
        QStringLiteral("Window"), QStringLiteral("Curtains"),
        QStringLiteral("Shutters"), QStringLiteral("Stairs"),
        QStringLiteral("RoofCap"), QStringLiteral("RoofSlope"),
        QStringLiteral("RoofTop"), QStringLiteral("GrimeWall")
    };
    for (int index = 0; index < buildingTiles.size(); ++index)
        xml.writeAttribute(buildingTiles.at(index), index == 0 ? QStringLiteral("1") : QStringLiteral("0"));
    xml.writeStartElement(QStringLiteral("properties"));
    xml.writeEmptyElement(QStringLiteral("property"));
    xml.writeAttribute(QStringLiteral("name"), QStringLiteral("OSMProxy"));
    xml.writeAttribute(QStringLiteral("value"), QStringLiteral("true"));
    xml.writeEndElement();
    const auto writeWallEntry = [&xml](const QString &category) {
        xml.writeStartElement(QStringLiteral("tile_entry"));
        xml.writeAttribute(QStringLiteral("category"), category);
        const QStringList enums = {QStringLiteral("West"), QStringLiteral("North"),
                                   QStringLiteral("NorthWest"), QStringLiteral("SouthEast")};
        const QStringList tiles = {QStringLiteral("walls_exterior_house_01_016"),
                                   QStringLiteral("walls_exterior_house_01_017"),
                                   QStringLiteral("walls_exterior_house_01_018"),
                                   QStringLiteral("walls_exterior_house_01_019")};
        for (int index = 0; index < enums.size(); ++index) {
            xml.writeEmptyElement(QStringLiteral("tile"));
            xml.writeAttribute(QStringLiteral("enum"), enums.at(index));
            xml.writeAttribute(QStringLiteral("tile"), tiles.at(index));
        }
        xml.writeEndElement();
    };
    writeWallEntry(QStringLiteral("exterior_walls"));
    writeWallEntry(QStringLiteral("interior_walls"));
    xml.writeStartElement(QStringLiteral("tile_entry"));
    xml.writeAttribute(QStringLiteral("category"), QStringLiteral("floors"));
    xml.writeEmptyElement(QStringLiteral("tile"));
    xml.writeAttribute(QStringLiteral("enum"), QStringLiteral("Floor"));
    xml.writeAttribute(QStringLiteral("tile"), QStringLiteral("floors_exterior_natural_01_008"));
    xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("tile_entry"));
    xml.writeAttribute(QStringLiteral("category"), QStringLiteral("ceiling"));
    xml.writeEmptyElement(QStringLiteral("tile"));
    xml.writeAttribute(QStringLiteral("enum"), QStringLiteral("Ceiling"));
    xml.writeAttribute(QStringLiteral("tile"), QStringLiteral("ceilings_01_000"));
    xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("user_tiles"));
    xml.writeEmptyElement(QStringLiteral("tile"));
    xml.writeAttribute(QStringLiteral("tile"), QStringLiteral("roofs_01_022"));
    xml.writeEndElement();
    xml.writeTextElement(QStringLiteral("used_tiles"), QStringLiteral("1 2 3 4"));
    xml.writeEmptyElement(QStringLiteral("used_furniture"));
    xml.writeEmptyElement(QStringLiteral("room"));
    xml.writeAttribute(QStringLiteral("Name"), QStringLiteral("OSM Proxy"));
    xml.writeAttribute(QStringLiteral("InternalName"), QStringLiteral("generic"));
    xml.writeAttribute(QStringLiteral("Color"), QStringLiteral("160 160 160"));
    xml.writeAttribute(QStringLiteral("InteriorWall"), QStringLiteral("2"));
    xml.writeAttribute(QStringLiteral("InteriorWallTrim"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("Floor"), QStringLiteral("3"));
    xml.writeAttribute(QStringLiteral("GrimeFloor"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("GrimeWall"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("Ceiling"), QStringLiteral("4"));
    const QString rooms = roomGrid(width, height, mask);
    const QString emptyRooms = csvGrid(width, height, QStringLiteral("0"));
    const QString attributes = csvGrid(width, height, QStringLiteral("0"));
    const QString roof = roofGrid(width, height, mask);
    for (int level = 0; level < levels; ++level) {
        xml.writeStartElement(QStringLiteral("floor"));
        xml.writeTextElement(QStringLiteral("rooms"), rooms);
        xml.writeTextElement(QStringLiteral("attributes"), attributes);
        xml.writeEndElement();
    }
    xml.writeStartElement(QStringLiteral("floor"));
    xml.writeTextElement(QStringLiteral("rooms"), emptyRooms);
    xml.writeTextElement(QStringLiteral("attributes"), attributes);
    xml.writeStartElement(QStringLiteral("tiles"));
    xml.writeAttribute(QStringLiteral("layer"), QStringLiteral("RoofTop"));
    xml.writeCharacters(roof);
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    buffer.close();
    return data;
}
bool writeGeneratedFile(const QString &path, const QByteArray &data,
                        QString *error, bool backupExisting = true)
{
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == data)
        return true;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    if (backupExisting && QFileInfo::exists(path)) {
        const QString backup = path + QStringLiteral(".backup-")
                + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QFile::copy(path, backup)) {
            if (error)
                *error = QStringLiteral("Could not back up %1").arg(path);
            return false;
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()
            || !output.commit()) {
        if (error)
            *error = QStringLiteral("Could not write %1: %2").arg(path, output.errorString());
        return false;
    }
    return true;
}
ObjectType *ensureObjectType(WorldDocument *document, const QString &name)
{
    World *world = document->world();
    ObjectType *type = world->objectType(name);
    if (!type) {
        type = new ObjectType(name);
        document->addObjectType(type);
    }
    return type;
}
QColor generatedZoneColor(const QString &typeName)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("TownZone"), QColor(QStringLiteral("#aa0000"))},
        {QStringLiteral("Forest"), QColor(QStringLiteral("#00aa00"))},
        {QStringLiteral("Nav"), QColor(QStringLiteral("#55aaff"))},
        {QStringLiteral("DeepForest"), QColor(QStringLiteral("#003500"))},
        {QStringLiteral("Vegitation"), QColor(QStringLiteral("#b3b300"))},
        {QStringLiteral("TrailerPark"), QColor(QStringLiteral("#f50000"))},
        {QStringLiteral("Farm"), QColor(QStringLiteral("#55ff7f"))},
        {QStringLiteral("FarmLand"), QColor(QStringLiteral("#bcff7d"))},
        {QStringLiteral("Water"), QColor(QStringLiteral("#0000ff"))}
    };
    return colors.value(typeName, QColor(82, 145, 214));
}
bool isGeneratedZoneGroup(const WorldObjectGroup *group)
{
    if (!group)
        return false;
    return group->name() == GENERATED_GROUP
            || group->name().startsWith(GENERATED_GROUP_PREFIX);
}
WorldObjectGroup *ensureGeneratedGroup(WorldDocument *document,
                                       const QString &typeName)
{
    World *world = document->world();
    const QString groupName = GENERATED_GROUP_PREFIX + typeName;
    WorldObjectGroup *group = world->objectGroups().find(groupName);
    if (!group) {
        ObjectType *type = ensureObjectType(document, typeName);
        group = new WorldObjectGroup(type, groupName,
                                     generatedZoneColor(typeName));
        document->addObjectGroup(group);
    }
    return group;
}
void removePreviousGeneratedData(WorldDocument *document,
                                 const OsmProjectDataOptions &options,
                                 const OsmTerrainImportResult &generated,
                                 const QSet<QString> &roadSignatures,
                                 int *removedRoadMarkings)
{
    if (removedRoadMarkings)
        *removedRoadMarkings = 0;
    World *world = document->world();
    const int cellSize = world->cellSize();
    const QSize affectedSize(
                qMax(1, generated.groundImage.width() / cellSize),
                qMax(1, generated.groundImage.height() / cellSize));
    const QRect affectedCells(options.cellOrigin, affectedSize);
    for (WorldCell *cell : world->cells()) {
        if (!affectedCells.contains(cell->pos()))
            continue;
        for (int index = cell->objects().size() - 1; index >= 0; --index) {
            WorldCellObject *object = cell->objects().at(index);
            if (!isGeneratedZoneGroup(object->group()))
                continue;
            const bool nav = object->type()
                    && object->type()->name() == QLatin1String("Nav");
            if ((nav && options.generateNavZones)
                    || (!nav && options.generateForagingZones)) {
                document->removeCellObject(cell, index);
            }
        }
        if (options.generateProxyBuildings) {
            for (int index = cell->lots().size() - 1; index >= 0; --index) {
                const QString normalized = QDir::fromNativeSeparators(
                            cell->lots().at(index)->mapName());
                if (normalized.contains(QStringLiteral("/osm-generated/"),
                                        Qt::CaseInsensitive)
                        || normalized.startsWith(QStringLiteral("osm-generated/"),
                                                 Qt::CaseInsensitive)) {
                    document->removeCellLot(cell, index);
                }
            }
        }
        if (options.generateInGameMapFeatures) {
            for (int index = cell->inGameMap().features().size() - 1;
                 index >= 0; --index) {
                InGameMapFeature *feature = cell->inGameMap().features().at(index);
                if (feature->properties().contains(
                            QStringLiteral("source"), QStringLiteral("osm"))) {
                    document->removeInGameMapFeature(cell, index);
                }
            }
        }
    }
    if (options.generateRoadMarkings && !roadSignatures.isEmpty()) {
        for (int index = world->roads().size() - 1; index >= 0; --index) {
            if (!roadSignatures.contains(
                        roadSignature(world->roads().at(index)))) {
                continue;
            }
            document->removeRoad(index);
            if (removedRoadMarkings)
                ++*removedRoadMarkings;
        }
    }
}
void cellRange(World *world, const QRectF &bounds,
               int *left, int *top, int *right, int *bottom)
{
    const int size = world->cellSize();
    *left = qBound(0, int(std::floor(bounds.left() / size)), world->width() - 1);
    *top = qBound(0, int(std::floor(bounds.top() / size)), world->height() - 1);
    *right = qBound(0, int(std::floor((bounds.right() - 0.0001) / size)), world->width() - 1);
    *bottom = qBound(0, int(std::floor((bounds.bottom() - 0.0001) / size)), world->height() - 1);
}
void addInGameMapPolygons(WorldDocument *document,
                          const OsmProjectFeature &sourceFeature,
                          const QPainterPath &path,
                          const QString &propertyKey,
                          const QString &propertyValue,
                          OsmProjectDataSummary *summary)
{
    if (path.isEmpty())
        return;
    World *world = document->world();
    int left, top, right, bottom;
    cellRange(world, path.boundingRect(), &left, &top, &right, &bottom);
    const int cellSize = world->cellSize();
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            WorldCell *cell = world->cellAt(x, y);
            const QRectF cellRect(x * cellSize, y * cellSize,
                                  cellSize, cellSize);
            const QVector<QPolygonF> polygons = clippedFillPolygons(path, cellRect);
            for (const QPolygonF &polygon : polygons) {
                const QPolygonF local = integerLocalPolygon(
                            polygon, cellRect.topLeft());
                if (local.size() < 3)
                    continue;
                InGameMapFeature *feature = new InGameMapFeature(
                            &cell->inGameMap());
                feature->mGeometry.mType = QStringLiteral("Polygon");
                InGameMapCoordinates coordinates;
                for (const QPointF &point : local)
                    coordinates += InGameMapPoint(point.x(), point.y());
                feature->mGeometry.mCoordinates += coordinates;
                feature->properties().set(propertyKey, propertyValue);
                feature->properties().set(QStringLiteral("source"),
                                          QStringLiteral("osm"));
                feature->properties().set(QStringLiteral("source:id"),
                                          sourceFeature.sourceKey());
                const QStringList retainedOsmKeys = {
                    QStringLiteral("highway"),
                    QStringLiteral("railway"),
                    QStringLiteral("footway"),
                    QStringLiteral("sidewalk"),
                    QStringLiteral("crossing"),
                    QStringLiteral("crossing:markings"),
                    QStringLiteral("road_marking"),
                    QStringLiteral("lane_markings"),
                    QStringLiteral("lanes"),
                    QStringLiteral("surface"),
                    QStringLiteral("width")
                };
                for (const QString &osmKey : retainedOsmKeys) {
                    const QString value = tagValue(sourceFeature, osmKey);
                    if (!value.isEmpty()) {
                        feature->properties().set(
                                    QStringLiteral("osm:") + osmKey, value);
                    }
                }
                const QString name = tagValue(sourceFeature,
                                              QStringLiteral("name"));
                if (!name.isEmpty())
                    feature->properties().set(QStringLiteral("name"), name);
                InGameMapGeometry sanitized;
                QStringList diagnostics;
                if (!sanitizeInGameMapGeometryForExport(
                            feature->mGeometry, sanitized, diagnostics)) {
                    if (summary)
                        summary->warnings += diagnostics;
                    delete feature;
                    continue;
                }
                feature->mGeometry = sanitized;
                document->addInGameMapFeature(
                            cell, cell->inGameMap().features().size(), feature);
                if (summary)
                    ++summary->inGameMapFeatures;
            }
        }
    }
}
QString inGameRoadClass(const QString &highway)
{
    if (highway == QLatin1String("motorway")
            || highway == QLatin1String("trunk")
            || highway == QLatin1String("primary")) {
        return QStringLiteral("primary");
    }
    if (highway == QLatin1String("secondary"))
        return QStringLiteral("secondary");
    if (highway == QLatin1String("path")
            || highway == QLatin1String("track")
            || highway == QLatin1String("footway")
            || highway == QLatin1String("cycleway")
            || highway == QLatin1String("bridleway")) {
        return QStringLiteral("trail");
    }
    return QStringLiteral("tertiary");
}
bool isNavMeshRoad(const OsmProjectFeature &feature)
{
    const QString highway = normalizedTagValue(
                feature, QStringLiteral("highway"));
    return highway == QLatin1String("motorway")
            || highway == QLatin1String("trunk")
            || highway == QLatin1String("primary")
            || highway == QLatin1String("secondary");
}
bool isSafeCityInGameRoad(const OsmProjectFeature &feature)
{
    const QString highway = normalizedTagValue(
                feature, QStringLiteral("highway"));
    return highway == QLatin1String("motorway")
            || highway == QLatin1String("trunk")
            || highway == QLatin1String("primary")
            || highway == QLatin1String("secondary")
            || highway == QLatin1String("tertiary")
            || highway == QLatin1String("residential");
}
using CellRasterMasks = QHash<int, QImage>;
void rasterizeArea(World *world, const QPainterPath &path,
                   CellRasterMasks *masks)
{
    if (!world || !masks || path.isEmpty())
        return;
    int left, top, right, bottom;
    cellRange(world, path.boundingRect(), &left, &top, &right, &bottom);
    const int cellSize = world->cellSize();
    const int worldWidth = world->width();
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const QRectF cellRect(x * cellSize, y * cellSize,
                                  cellSize, cellSize);
            if (!path.intersects(cellRect) && !path.contains(cellRect.center()))
                continue;
            const int key = y * worldWidth + x;
            QImage &mask = (*masks)[key];
            if (mask.isNull()) {
                mask = QImage(cellSize, cellSize, QImage::Format_Grayscale8);
                mask.fill(0);
            }
            QPainter painter(&mask);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.translate(-cellRect.left(), -cellRect.top());
            painter.drawPath(path);
        }
    }
}
bool maskHasPixels(const QImage &mask)
{
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *line = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (line[x] != 0)
                return true;
        }
    }
    return false;
}
void mergeMasks(CellRasterMasks *target, const CellRasterMasks &source)
{
    if (!target)
        return;
    for (auto sourceIt = source.constBegin();
         sourceIt != source.constEnd(); ++sourceIt) {
        QImage &targetMask = (*target)[sourceIt.key()];
        if (targetMask.isNull()) {
            targetMask = sourceIt.value().copy();
            continue;
        }
        const QImage &sourceMask = sourceIt.value();
        const int width = qMin(targetMask.width(), sourceMask.width());
        const int height = qMin(targetMask.height(), sourceMask.height());
        for (int y = 0; y < height; ++y) {
            uchar *targetLine = targetMask.scanLine(y);
            const uchar *sourceLine = sourceMask.constScanLine(y);
            for (int x = 0; x < width; ++x) {
                if (sourceLine[x] != 0)
                    targetLine[x] = 255;
            }
        }
    }
}
void subtractMasks(CellRasterMasks *target, const CellRasterMasks &exclusion)
{
    if (!target || target->isEmpty() || exclusion.isEmpty())
        return;
    QList<int> emptyKeys;
    for (auto targetIt = target->begin(); targetIt != target->end();
         ++targetIt) {
        const auto exclusionIt = exclusion.constFind(targetIt.key());
        if (exclusionIt == exclusion.constEnd())
            continue;
        QImage &targetMask = targetIt.value();
        const QImage &exclusionMask = exclusionIt.value();
        const int width = qMin(targetMask.width(), exclusionMask.width());
        const int height = qMin(targetMask.height(), exclusionMask.height());
        for (int y = 0; y < height; ++y) {
            uchar *targetLine = targetMask.scanLine(y);
            const uchar *exclusionLine = exclusionMask.constScanLine(y);
            for (int x = 0; x < width; ++x) {
                if (exclusionLine[x] != 0)
                    targetLine[x] = 0;
            }
        }
        if (!maskHasPixels(targetMask))
            emptyKeys += targetIt.key();
    }
    for (int key : emptyKeys)
        target->remove(key);
}
void coarsenMasks(CellRasterMasks *masks, int gridSize)
{
    if (!masks || gridSize <= 1)
        return;
    for (auto maskIt = masks->begin(); maskIt != masks->end(); ++maskIt) {
        const QImage source = maskIt.value();
        QImage coarse(source.size(), QImage::Format_Grayscale8);
        coarse.fill(0);
        for (int top = 0; top < source.height(); top += gridSize) {
            for (int left = 0; left < source.width(); left += gridSize) {
                const int right = qMin(source.width(), left + gridSize);
                const int bottom = qMin(source.height(), top + gridSize);
                bool occupied = false;
                for (int y = top; y < bottom && !occupied; ++y) {
                    const uchar *sourceLine = source.constScanLine(y);
                    for (int x = left; x < right; ++x) {
                        if (sourceLine[x] != 0) {
                            occupied = true;
                            break;
                        }
                    }
                }
                if (!occupied)
                    continue;
                for (int y = top; y < bottom; ++y) {
                    uchar *coarseLine = coarse.scanLine(y);
                    for (int x = left; x < right; ++x)
                        coarseLine[x] = 255;
                }
            }
        }
        maskIt.value() = coarse;
    }
}
QVector<QRect> mergedMaskRectangles(const QImage &mask)
{
    if (mask.isNull())
        return {};
    QImage remaining = mask.convertToFormat(QImage::Format_Grayscale8);
    QVector<QRect> completed;
    for (int top = 0; top < remaining.height(); ++top) {
        int left = 0;
        while (left < remaining.width()) {
            if (remaining.constScanLine(top)[left] == 0) {
                ++left;
                continue;
            }
            int bestWidth = 1;
            int bestHeight = 1;
            int minimumWidth = remaining.width() - left;
            qint64 bestArea = 1;
            for (int bottom = top; bottom < remaining.height(); ++bottom) {
                const uchar *line = remaining.constScanLine(bottom);
                if (line[left] == 0)
                    break;
                int runWidth = 0;
                while (left + runWidth < remaining.width()
                       && line[left + runWidth] != 0) {
                    ++runWidth;
                }
                minimumWidth = qMin(minimumWidth, runWidth);
                const int height = bottom - top + 1;
                const qint64 area = qint64(minimumWidth) * height;
                if (area > bestArea
                        || (area == bestArea
                            && height > bestHeight)) {
                    bestArea = area;
                    bestWidth = minimumWidth;
                    bestHeight = height;
                }
            }
            const QRect rectangle(left, top, bestWidth, bestHeight);
            completed += rectangle;
            for (int y = rectangle.top(); y <= rectangle.bottom(); ++y) {
                uchar *line = remaining.scanLine(y);
                std::fill(line + rectangle.left(),
                          line + rectangle.right() + 1, uchar(0));
            }
            left += bestWidth;
        }
    }
    return completed;
}
QVector<QPolygonF> mergedMaskPolygons(const QImage &mask)
{
    QVector<QPolygonF> polygons;
    if (mask.isNull())
        return polygons;
    QPainterPath combined;
    combined.setFillRule(Qt::WindingFill);
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *line = mask.constScanLine(y);
        int x = 0;
        while (x < mask.width()) {
            while (x < mask.width() && line[x] == 0)
                ++x;
            const int start = x;
            while (x < mask.width() && line[x] != 0)
                ++x;
            if (x > start)
                combined.addRect(QRectF(start, y, x - start, 1));
        }
    }
    if (combined.isEmpty())
        return polygons;
    const QList<QPolygonF> fills = combined.simplified().toFillPolygons();
    int totalPoints = 0;
    for (QPolygonF polygon : fills) {
        if (polygon.size() > 1
                && closeEnough(polygon.first(), polygon.last(), 0.05)) {
            polygon.removeLast();
        }
        polygon = simplifyPolyline(polygon);
        if (polygon.size() < 3 || polygonArea(polygon) <= 0.5)
            continue;
        totalPoints += polygon.size();
        if (totalPoints > 4096)
            return {};
        polygons += polygon;
    }
    return polygons;
}
void addRasterObjects(WorldDocument *document,
                      const QString &typeName,
                      const QString &name,
                      const CellRasterMasks &masks,
                      WorldObjectGroup *group,
                      int *objectCount)
{
    if (!document || masks.isEmpty())
        return;
    World *world = document->world();
    ObjectType *type = ensureObjectType(document, typeName);
    QList<int> keys = masks.keys();
    std::sort(keys.begin(), keys.end());
    for (int key : keys) {
        const int x = key % world->width();
        const int y = key / world->width();
        WorldCell *cell = world->cellAt(x, y);
        if (!cell)
            continue;
        const QVector<QRect> rectangles = mergedMaskRectangles(
                    masks.value(key));
        for (const QRect &rectangle : rectangles) {
            WorldCellObject *object = new WorldCellObject(
                        cell, name, type, group,
                        rectangle.x(), rectangle.y(), 0,
                        rectangle.width(), rectangle.height());
            document->addCellObject(cell, cell->objects().size(), object);
            if (objectCount)
                ++*objectCount;
        }
    }
}
void addRasterZoneObjects(WorldDocument *document,
                          const QString &typeName,
                          const QString &name,
                          const CellRasterMasks &masks,
                          WorldObjectGroup *group,
                          int *objectCount)
{
    if (!document || masks.isEmpty())
        return;
    World *world = document->world();
    ObjectType *type = ensureObjectType(document, typeName);
    QList<int> keys = masks.keys();
    std::sort(keys.begin(), keys.end());
    for (int key : keys) {
        const int x = key % world->width();
        const int y = key / world->width();
        WorldCell *cell = world->cellAt(x, y);
        if (!cell)
            continue;
        const QImage &mask = masks.value(key);
        const QVector<QPolygonF> polygons = mergedMaskPolygons(mask);
        if (polygons.isEmpty()) {
            addRasterObjects(document, typeName, name,
                             CellRasterMasks{{key, mask}}, group,
                             objectCount);
            continue;
        }
        for (const QPolygonF &polygon : polygons) {
            QRectF rectangle;
            WorldCellObject *object = nullptr;
            if (polygonIsRectangle(polygon, &rectangle)) {
                object = new WorldCellObject(
                            cell, name, type, group,
                            rectangle.x(), rectangle.y(), 0,
                            rectangle.width(), rectangle.height());
            } else {
                object = new WorldCellObject(
                            cell, name, type, group, 0, 0, 0, 1, 1);
                object->setGeometryType(ObjectGeometryType::Polygon);
                WorldCellObjectPoints points;
                for (const QPointF &point : polygon) {
                    points += WorldCellObjectPoint(qRound(point.x()),
                                                   qRound(point.y()));
                }
                object->setPoints(points);
                object->calculateBounds();
            }
            document->addCellObject(cell, cell->objects().size(), object);
            if (objectCount)
                ++*objectCount;
        }
    }
}
void addAreaObjects(WorldDocument *document,
                    const QString &typeName,
                    const QString &name,
                    const QPainterPath &path,
                    WorldObjectGroup *group,
                    int *objectCount)
{
    if (path.isEmpty())
        return;
    World *world = document->world();
    ObjectType *type = ensureObjectType(document, typeName);
    const int cellSize = world->cellSize();
    int left, top, right, bottom;
    cellRange(world, path.boundingRect(), &left, &top, &right, &bottom);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            WorldCell *cell = world->cellAt(x, y);
            const QRectF cellRect(x * cellSize, y * cellSize,
                                  cellSize, cellSize);
            const QVector<QPolygonF> polygons = clippedFillPolygons(
                        path, cellRect);
            for (const QPolygonF &polygon : polygons) {
                const QPolygonF local = integerLocalPolygon(
                            polygon, cellRect.topLeft());
                if (local.size() < 3)
                    continue;
                QRectF rectangle;
                WorldCellObject *object = nullptr;
                if (polygonIsRectangle(local, &rectangle)) {
                    object = new WorldCellObject(
                                cell, name, type, group,
                                rectangle.x(), rectangle.y(), 0,
                                rectangle.width(), rectangle.height());
                } else {
                    object = new WorldCellObject(
                                cell, name, type, group, 0, 0, 0, 1, 1);
                    object->setGeometryType(ObjectGeometryType::Polygon);
                    WorldCellObjectPoints points;
                    for (const QPointF &point : local)
                        points += WorldCellObjectPoint(qRound(point.x()),
                                                       qRound(point.y()));
                    object->setPoints(points);
                    object->calculateBounds();
                }
                document->addCellObject(cell, cell->objects().size(), object);
                if (objectCount)
                    ++*objectCount;
            }
        }
    }
}
void addNavMeshes(WorldDocument *document,
                  const CellRasterMasks &masks,
                  WorldObjectGroup *group,
                  OsmProjectDataSummary *summary)
{
    addRasterObjects(document, QStringLiteral("Nav"),
                     QStringLiteral("OSM Road Nav Surface"), masks, group,
                     summary ? &summary->navZones : nullptr);
}
QPainterPath navRoadPath(const OsmProjectFeature &feature,
                         const QPointF &offset)
{
    return roadPath(feature, offset);
}
void addForagingZone(WorldDocument *document,
                     const QString &typeName,
                     const CellRasterMasks &masks,
                     WorldObjectGroup *group,
                     OsmProjectDataSummary *summary,
                     const QString &name = QString())
{
    int count = 0;
    addRasterZoneObjects(document, typeName, name, masks, group, &count);
    if (summary && count > 0) {
        summary->foragingZones += count;
        summary->zoneTypeCounts[typeName] += count;
    }
}
bool addProxyBuilding(WorldDocument *document,
                      const OsmProjectFeature &feature,
                      const QPainterPath &path,
                      const QString &outputDirectory,
                      QSet<QString> *generatedFiles,
                      bool addLot,
                      OsmProjectDataSummary *summary,
                      QString *error)
{
    World *world = document->world();
    const QRectF projectBounds(0, 0,
                               world->width() * world->cellSize(),
                               world->height() * world->cellSize());
    const QRectF bounds = path.boundingRect().intersected(projectBounds);
    if (bounds.isEmpty())
        return true;
    const int left = qMax(0, int(std::floor(bounds.left())));
    const int top = qMax(0, int(std::floor(bounds.top())));
    const int width = qMax(1, int(std::ceil(bounds.right())) - left);
    const int height = qMax(1, int(std::ceil(bounds.bottom())) - top);
    if (width > MAX_PROXY_BUILDING_DIMENSION
            || height > MAX_PROXY_BUILDING_DIMENSION
            || qint64(width) * height > MAX_PROXY_BUILDING_AREA) {
        if (summary) {
            summary->warnings += QStringLiteral(
                        "Skipped oversized OSM proxy building %1 (%2 x %3 squares, maximum area %4).")
                    .arg(feature.sourceKey()).arg(width).arg(height)
                    .arg(MAX_PROXY_BUILDING_AREA);
        }
        return true;
    }
    const QByteArray mask = footprintMask(path, left, top, width, height);
    const QString maskHash = QString::fromLatin1(
                QCryptographicHash::hash(mask, QCryptographicHash::Sha1)
                .toHex().left(10));
    const QString fileName = QStringLiteral("osm_proxy_%1x%2x%3_%4.tbx")
            .arg(width).arg(height).arg(feature.buildingLevels, 0, 10)
            .arg(maskHash);
    const QString pathName = QDir(outputDirectory).filePath(fileName);
    if (!generatedFiles->contains(pathName)) {
        if (!writeGeneratedFile(
                    pathName, proxyTbxData(width, height,
                                           feature.buildingLevels, mask),
                    error)) {
            return false;
        }
        *generatedFiles += pathName;
    }
    if (!addLot)
        return true;
    const int cellSize = world->cellSize();
    const int cellX = left / cellSize;
    const int cellY = top / cellSize;
    WorldCell *cell = world->cellAt(cellX, cellY);
    if (!cell)
        return true;
    WorldCellLot *lot = new WorldCellLot(
                cell, pathName,
                left - cellX * cellSize,
                top - cellY * cellSize,
                0, width, height);
    document->addCellLot(cell, cell->lots().size(), lot);
    if (summary)
        ++summary->proxyBuildings;
    return true;
}
bool writeManifest(const QString &path,
                   const OsmTerrainImportResult &generated,
                   const OsmProjectDataOptions &options,
                   const OsmProjectDataSummary &summary,
                   const QSet<QString> &streetSignaturesForScope,
                   const QSet<QString> &roadSignaturesForScope,
                   const QSet<QString> &generatedFiles,
                   QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 4);
    root.insert(QStringLiteral("source"), QStringLiteral("OpenStreetMap"));
    root.insert(QStringLiteral("generatedUtc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("originCellX"), options.cellOrigin.x());
    root.insert(QStringLiteral("originCellY"), options.cellOrigin.y());
    root.insert(QStringLiteral("streets"), summary.streets);
    root.insert(QStringLiteral("inGameMapFeatures"), summary.inGameMapFeatures);
    root.insert(QStringLiteral("proxyBuildings"), summary.proxyBuildings);
    root.insert(QStringLiteral("roadMarkings"),
                roadSignaturesForScope.size());
    root.insert(QStringLiteral("navZones"), summary.navZones);
    root.insert(QStringLiteral("foragingZones"), summary.foragingZones);
    QJsonObject zoneTypeCounts;
    QStringList zoneTypes = summary.zoneTypeCounts.keys();
    zoneTypes.sort(Qt::CaseInsensitive);
    for (const QString &zoneType : zoneTypes)
        zoneTypeCounts.insert(zoneType, summary.zoneTypeCounts.value(zoneType));
    root.insert(QStringLiteral("zoneTypeCounts"), zoneTypeCounts);
    root.insert(QStringLiteral("safeCityMode"), summary.safeCityMode);
    root.insert(QStringLiteral("skippedProxyBuildings"),
                summary.skippedProxyBuildings);
    root.insert(QStringLiteral("skippedInGameMapFeatures"),
                summary.skippedInGameMapFeatures);
    const QJsonDocument sourceDocument = QJsonDocument::fromJson(
                generated.sourceMetadata);
    if (sourceDocument.isObject())
        root.insert(QStringLiteral("sourceMetadata"), sourceDocument.object());
    QJsonArray streetSignatures;
    for (const QString &signature : streetSignaturesForScope)
        streetSignatures += signature;
    root.insert(QStringLiteral("streetSignatures"), streetSignatures);
    QJsonArray roadSignatures;
    for (const QString &signature : roadSignaturesForScope)
        roadSignatures += signature;
    root.insert(QStringLiteral("roadSignatures"), roadSignatures);
    QJsonArray files;
    for (const QString &fileName : generatedFiles)
        files += QDir(QFileInfo(path).absolutePath()).relativeFilePath(fileName);
    root.insert(QStringLiteral("generatedFiles"), files);
    return writeGeneratedFile(path,
                              QJsonDocument(root).toJson(QJsonDocument::Indented),
                              error, false);
}
}
bool OsmProjectData::apply(
        WorldDocument *document,
        const OsmTerrainImportResult &generated,
        const OsmProjectDataOptions &options,
        const QVector<StreetNameRecord> &currentStreets,
        QVector<StreetNameRecord> *mergedStreets,
        OsmProjectDataSummary *summary,
        QString *error,
        const OsmProjectProgress &progress)
{
    if (!document || !document->world()) {
        if (error)
            *error = QStringLiteral("No WorldEd project is available for OSM project data.");
        return false;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    OsmProjectDataSummary localSummary;
    localSummary.safeCityMode = options.safeCityMode;
    const int featureCount = generated.projectFeatures.size();
    int buildingCount = 0;
    int roadCount = 0;
    int railwayCount = 0;
    int navRoadCount = 0;
    int typedZoneFeatureCount = 0;
    for (const OsmProjectFeature &feature : generated.projectFeatures) {
        if (feature.building)
            ++buildingCount;
        if (feature.road) {
            ++roadCount;
            if (isNavMeshRoad(feature))
                ++navRoadCount;
        }
        if (feature.railway)
            ++railwayCount;
        if (feature.waterArea || feature.waterway
                || !feature.foragingZone.isEmpty())
            ++typedZoneFeatureCount;
    }
    QSet<int> proxyBuildingFeatureIndexes;
    if (options.generateProxyBuildings) {
        if (!options.safeCityMode) {
            for (int index = 0; index < featureCount; ++index) {
                if (generated.projectFeatures.at(index).building)
                    proxyBuildingFeatureIndexes += index;
            }
        } else {
            QVector<QPair<QByteArray, int>> candidates;
            candidates.reserve(buildingCount);
            for (int index = 0; index < featureCount; ++index) {
                const OsmProjectFeature &feature =
                        generated.projectFeatures.at(index);
                if (!feature.building)
                    continue;
                const QByteArray rank = QCryptographicHash::hash(
                            feature.sourceKey().toUtf8()
                            + QByteArray::number(index),
                            QCryptographicHash::Sha1);
                candidates += qMakePair(rank, index);
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const QPair<QByteArray, int> &left,
                         const QPair<QByteArray, int> &right) {
                if (left.first != right.first)
                    return left.first < right.first;
                return left.second < right.second;
            });
            const int selectedCount = qMin(
                        SAFE_CITY_PROXY_BUILDING_LIMIT,
                        candidates.size());
            for (int index = 0; index < selectedCount; ++index)
                proxyBuildingFeatureIndexes += candidates.at(index).second;
        }
    }
    const bool generateProxyBuildings =
            !proxyBuildingFeatureIndexes.isEmpty();
    const bool detailedBuildingTownZones = buildingCount
            <= DETAILED_TOWN_ZONE_BUILDING_LIMIT;
    const int totalProgress = 5 + featureCount
            + (generateProxyBuildings ? featureCount : 0);
    int progressValue = 0;
    const auto reportProgress = [&progress, totalProgress](
            int value, const QString &message) {
        if (progress)
            progress(value, totalProgress, message);
    };
    reportProgress(progressValue,
                   QObject::tr("Preparing OpenStreetMap project data..."));
    qInfo().noquote()
            << QStringLiteral(
                   "OSM project-data preflight: features %1, roads %2, railways %3, major-road Nav sources %4, buildings %5, typed zone features %6, safe city mode %7, proxy TBX selection %8, detailed building TownZone footprints %9")
               .arg(featureCount).arg(roadCount).arg(railwayCount)
               .arg(navRoadCount)
               .arg(buildingCount).arg(typedZoneFeatureCount)
               .arg(options.safeCityMode ? QStringLiteral("on")
                                         : QStringLiteral("off"))
               .arg(QStringLiteral("%1 of %2")
                    .arg(proxyBuildingFeatureIndexes.size())
                    .arg(buildingCount))
               .arg(detailedBuildingTownZones
                    ? QStringLiteral("on") : QStringLiteral("off"));
    const QString projectPath = options.projectFilePath.isEmpty()
            ? document->fileName() : options.projectFilePath;
    if (projectPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Save the PZW project before generating OSM project data.");
        return false;
    }
    const QString generatedDirectory = QDir(
                QFileInfo(projectPath).absolutePath()).filePath(
                QStringLiteral("osm-generated"));
    if (!QDir().mkpath(generatedDirectory)) {
        if (error)
            *error = QStringLiteral("Could not create the OSM output directory: %1")
                    .arg(generatedDirectory);
        return false;
    }
    const int cellSizeForScope = document->world()->cellSize();
    const int scopeWidth = qMax(
                1, generated.groundImage.width() / cellSizeForScope);
    const int scopeHeight = qMax(
                1, generated.groundImage.height() / cellSizeForScope);
    const QString manifestPath = QDir(generatedDirectory).filePath(
                QStringLiteral("manifest_%1_%2_%3_%4.json")
                .arg(options.cellOrigin.x()).arg(options.cellOrigin.y())
                .arg(scopeWidth).arg(scopeHeight));
    localSummary.manifestPath = manifestPath;
    QVector<StreetNameRecord> importedStreets;
    QVector<StreetNameRecord> merged = currentStreets;
    QSet<QString> manifestStreetSignatures =
            previousStreetSignatures(manifestPath);
    const QSet<QString> previousRoadSignatures =
            previousManifestSignatures(
                manifestPath, QStringLiteral("roadSignatures"));
    if (options.generateStreets) {
        for (int index = merged.size() - 1; index >= 0; --index) {
            if (manifestStreetSignatures.contains(
                        streetSignature(merged.at(index))))
                merged.removeAt(index);
        }
        importedStreets = generateStreets(
                    document->world(), generated, options);
        QSet<QString> present;
        for (const StreetNameRecord &street : merged)
            present += streetSignature(street);
        for (const StreetNameRecord &street : importedStreets) {
            const QString signature = streetSignature(street);
            if (!present.contains(signature)) {
                merged += street;
                present += signature;
            }
        }
        manifestStreetSignatures.clear();
        for (const StreetNameRecord &street : importedStreets)
            manifestStreetSignatures += streetSignature(street);
        localSummary.streets = importedStreets.size();
    }
    reportProgress(++progressValue,
                   QObject::tr("Prepared OSM street records."));
    QSet<QString> generatedFiles;
    if (generateProxyBuildings) {
        const int cellSize = document->world()->cellSize();
        const QPointF offset = projectOffset(options, cellSize);
        for (int featureIndex = 0; featureIndex < featureCount;
             ++featureIndex) {
            const OsmProjectFeature &feature = generated.projectFeatures.at(
                        featureIndex);
            if (feature.building
                    && proxyBuildingFeatureIndexes.contains(featureIndex)
                    && !addProxyBuilding(document, feature,
                                         polygonPath(feature, offset),
                                         generatedDirectory, &generatedFiles,
                                         false,
                                         &localSummary, error)) {
                return false;
            }
            ++progressValue;
            if ((featureIndex % 32) == 0
                    || featureIndex + 1 == featureCount) {
                reportProgress(
                            progressValue,
                            QObject::tr("Preparing proxy TBX files (%1 of %2)...")
                            .arg(featureIndex + 1).arg(featureCount));
            }
        }
    }
    document->undoStack()->beginMacro(
                QObject::tr("Import OpenStreetMap project data"));
    int removedRoadMarkings = 0;
    removePreviousGeneratedData(document, options, generated,
                                previousRoadSignatures,
                                &removedRoadMarkings);
    reportProgress(++progressValue,
                   QObject::tr("Removed previously generated OSM data."));
    QSet<QString> generatedRoadSignatures = previousRoadSignatures;
    if (options.generateRoadMarkings) {
        generatedRoadSignatures = addRoadMarkings(
                    document, generated, options, &localSummary);
        if (removedRoadMarkings < previousRoadSignatures.size()) {
            localSummary.warnings += QObject::tr(
                        "%1 previously generated road-marking object(s) were edited and were preserved during reimport.")
                    .arg(previousRoadSignatures.size()
                         - removedRoadMarkings);
        }
    }
    const int cellSize = document->world()->cellSize();
    const QPointF offset = projectOffset(options, cellSize);
    CellRasterMasks navMasks;
    CellRasterMasks zoneExclusionMasks;
    QHash<QString, CellRasterMasks> zoneMasks;
    const auto addZoneSurface = [document, &zoneMasks](
            const QString &typeName, const QPainterPath &path) {
        if (typeName.isEmpty() || path.isEmpty())
            return;
        rasterizeArea(document->world(), path, &zoneMasks[typeName]);
    };
    for (int featureIndex = 0; featureIndex < featureCount;
         ++featureIndex) {
        const OsmProjectFeature &feature = generated.projectFeatures.at(
                    featureIndex);
        const QPainterPath areaPath = polygonPath(feature, offset);
        const QPainterPath linearPath = feature.road || feature.railway
                || feature.waterway || feature.vegetationLine
                ? roadPath(feature, offset) : QPainterPath();
        if (generateProxyBuildings && feature.building
                && proxyBuildingFeatureIndexes.contains(featureIndex)) {
            if (!addProxyBuilding(document, feature, areaPath,
                                  generatedDirectory, &generatedFiles,
                                  true, &localSummary, error)) {
                document->undoStack()->endMacro();
                document->undoStack()->undo();
                return false;
            }
        }
        if (options.generateInGameMapFeatures) {
            if (feature.road
                    && (!options.safeCityMode
                        || isSafeCityInGameRoad(feature))) {
                addInGameMapPolygons(
                            document, feature, roadPath(feature, offset),
                            QStringLiteral("highway"),
                            inGameRoadClass(normalizedTagValue(
                                                feature, QStringLiteral("highway"))),
                            &localSummary);
            }
            if (feature.railway && !options.safeCityMode) {
                QString railway = normalizedTagValue(
                            feature, QStringLiteral("railway"));
                if (railway.isEmpty())
                    railway = QStringLiteral("rail");
                addInGameMapPolygons(
                            document, feature, roadPath(feature, offset),
                            QStringLiteral("railway"), railway,
                            &localSummary);
            } else if (feature.railway) {
                ++localSummary.skippedInGameMapFeatures;
            }
            if (feature.building
                    && (!options.safeCityMode
                        || proxyBuildingFeatureIndexes.contains(
                            featureIndex))) {
                const QString building = normalizedTagValue(
                            feature, QStringLiteral("building"));
                addInGameMapPolygons(
                            document, feature, areaPath,
                            QStringLiteral("building"),
                            building.isEmpty() ? QStringLiteral("yes") : building,
                            &localSummary);
            } else if (feature.building) {
                ++localSummary.skippedInGameMapFeatures;
            } else if (feature.waterArea) {
                QString water = normalizedTagValue(
                            feature, QStringLiteral("water"));
                if (water.isEmpty())
                    water = QStringLiteral("water");
                addInGameMapPolygons(document, feature, areaPath,
                                     QStringLiteral("water"), water,
                                     &localSummary);
            } else if (feature.waterway) {
                QString waterway = normalizedTagValue(
                            feature, QStringLiteral("waterway"));
                if (waterway.isEmpty())
                    waterway = QStringLiteral("stream");
                addInGameMapPolygons(document, feature, linearPath,
                                     QStringLiteral("waterway"), waterway,
                                     &localSummary);
            } else if (feature.forestArea) {
                addInGameMapPolygons(document, feature, areaPath,
                                     QStringLiteral("natural"),
                                     QStringLiteral("forest"),
                                     &localSummary);
            }
            if (feature.road && options.safeCityMode
                    && !isSafeCityInGameRoad(feature)) {
                ++localSummary.skippedInGameMapFeatures;
            }
        }
        if (options.generateNavZones && feature.road
                && isNavMeshRoad(feature)) {
            rasterizeArea(document->world(), navRoadPath(feature, offset),
                          &navMasks);
        }
        if (options.generateForagingZones) {
            if (feature.road || feature.railway)
                rasterizeArea(document->world(), linearPath,
                              &zoneExclusionMasks);
            const QString natural = normalizedTagValue(
                        feature, QStringLiteral("natural"));
            const QString landcover = normalizedTagValue(
                        feature, QStringLiteral("landcover"));
            if (natural == QLatin1String("sand")
                    || natural == QLatin1String("beach")
                    || landcover == QLatin1String("sand")) {
                rasterizeArea(document->world(), areaPath,
                              &zoneExclusionMasks);
            }
            const QString zoneType = feature.waterArea || feature.waterway
                    ? QStringLiteral("Water") : feature.foragingZone;
            const QPainterPath zonePath = feature.waterway
                    || feature.vegetationLine ? linearPath : areaPath;
            addZoneSurface(zoneType, zonePath);
            if (feature.building) {
                if (detailedBuildingTownZones) {
                    addZoneSurface(QStringLiteral("TownZone"),
                                   buildingTownZonePath(areaPath));
                } else {
                    const QRectF bounds = areaPath.boundingRect().adjusted(
                                -1.0, -1.0, 1.0, 1.0);
                    const int left = int(std::floor(
                                bounds.left() / CITY_TOWN_ZONE_GRID))
                            * CITY_TOWN_ZONE_GRID;
                    const int top = int(std::floor(
                                bounds.top() / CITY_TOWN_ZONE_GRID))
                            * CITY_TOWN_ZONE_GRID;
                    const int right = int(std::ceil(
                                bounds.right() / CITY_TOWN_ZONE_GRID))
                            * CITY_TOWN_ZONE_GRID;
                    const int bottom = int(std::ceil(
                                bounds.bottom() / CITY_TOWN_ZONE_GRID))
                            * CITY_TOWN_ZONE_GRID;
                    QPainterPath densityArea;
                    densityArea.addRect(QRectF(left, top,
                                               qMax(CITY_TOWN_ZONE_GRID,
                                                    right - left),
                                               qMax(CITY_TOWN_ZONE_GRID,
                                                    bottom - top)));
                    addZoneSurface(QStringLiteral("TownZone"), densityArea);
                }
            }
        }
        ++progressValue;
        if ((featureIndex % 32) == 0
                || featureIndex + 1 == featureCount) {
            reportProgress(
                        progressValue,
                        QObject::tr("Creating OSM project objects (%1 of %2)...")
                        .arg(featureIndex + 1).arg(featureCount));
        }
    }
    if (options.generateForagingZones && options.safeCityMode) {
        coarsenMasks(&zoneExclusionMasks, CITY_TOWN_ZONE_GRID);
        for (auto zoneIt = zoneMasks.begin(); zoneIt != zoneMasks.end();
             ++zoneIt) {
            coarsenMasks(&zoneIt.value(), CITY_TOWN_ZONE_GRID);
        }
        localSummary.warnings += QObject::tr(
                    "Safe City Mode snapped typed ground zones to an %1 x %1 square grid to keep large projects responsive.")
                .arg(CITY_TOWN_ZONE_GRID);
    }
    if (options.generateNavZones) {
        addNavMeshes(document, navMasks,
                     ensureGeneratedGroup(document, QStringLiteral("Nav")),
                     &localSummary);
    }
    if (options.generateForagingZones) {
        const int scopeWidthPixels = generated.groundImage.width() > 0
                ? generated.groundImage.width()
                : qMax(1, scopeWidth * cellSize);
        const int scopeHeightPixels = generated.groundImage.height() > 0
                ? generated.groundImage.height()
                : qMax(1, scopeHeight * cellSize);
        QPainterPath baseVegetation;
        baseVegetation.addRect(QRectF(offset,
                                      QSizeF(scopeWidthPixels,
                                             scopeHeightPixels)));
        CellRasterMasks baseVegetationMasks;
        rasterizeArea(document->world(), baseVegetation,
                      &baseVegetationMasks);
        QHash<QString, CellRasterMasks> resolvedZoneMasks;
        CellRasterMasks occupiedMasks;
        const auto resolveZone = [&zoneMasks, &resolvedZoneMasks,
                                  &occupiedMasks](const QString &zoneType) {
            CellRasterMasks masks = zoneMasks.take(zoneType);
            subtractMasks(&masks, occupiedMasks);
            if (masks.isEmpty())
                return;
            resolvedZoneMasks.insert(zoneType, masks);
            mergeMasks(&occupiedMasks, masks);
        };
        resolveZone(QStringLiteral("Water"));
        mergeMasks(&occupiedMasks, zoneExclusionMasks);
        const QStringList priorityZoneTypes = {
            QStringLiteral("TownZone"),
            QStringLiteral("Farm"),
            QStringLiteral("FarmLand"),
            QStringLiteral("DeepForest"),
            QStringLiteral("Forest")
        };
        for (const QString &zoneType : priorityZoneTypes)
            resolveZone(zoneType);
        zoneMasks.remove(QStringLiteral("Vegitation"));
        QStringList remainingZoneTypes = zoneMasks.keys();
        remainingZoneTypes.sort(Qt::CaseInsensitive);
        for (const QString &zoneType : remainingZoneTypes)
            resolveZone(zoneType);
        subtractMasks(&baseVegetationMasks, occupiedMasks);
        if (!baseVegetationMasks.isEmpty()) {
            resolvedZoneMasks.insert(QStringLiteral("Vegitation"),
                                     baseVegetationMasks);
        }
        QStringList outputZoneTypes = resolvedZoneMasks.keys();
        outputZoneTypes.sort(Qt::CaseInsensitive);
        for (const QString &zoneType : outputZoneTypes) {
            addForagingZone(
                        document, zoneType,
                        resolvedZoneMasks.value(zoneType),
                        ensureGeneratedGroup(document, zoneType),
                        &localSummary,
                        QStringLiteral("OSM %1").arg(zoneType));
        }
    }
    if (options.safeCityMode && options.generateProxyBuildings) {
        localSummary.skippedProxyBuildings = qMax(
                    0, buildingCount - proxyBuildingFeatureIndexes.size());
        localSummary.warnings += QObject::tr(
                    "Safe City Mode retained %1 proxy TBX building(s), including cross-cell footprints, and skipped %2 to keep the project responsive.")
                .arg(proxyBuildingFeatureIndexes.size())
                .arg(localSummary.skippedProxyBuildings);
    }
    if (options.safeCityMode
            && localSummary.skippedInGameMapFeatures > 0) {
        localSummary.warnings += QObject::tr(
                    "Safe city mode skipped %1 detailed building, service-road, path, or trail InGameMap feature(s). streets.xml and primary road data were preserved.")
                .arg(localSummary.skippedInGameMapFeatures);
    }
    if (!detailedBuildingTownZones && options.generateForagingZones) {
        localSummary.warnings += QObject::tr(
                    "Generated city-scale TownZone coverage on an %1 x %1 square grid for %2 buildings instead of thousands of overlapping one-square perimeter rings.")
                .arg(CITY_TOWN_ZONE_GRID).arg(buildingCount);
    }
    document->undoStack()->endMacro();
    reportProgress(++progressValue,
                   QObject::tr("Created OSM zones and project objects."));
    QString manifestError;
    if (!writeManifest(manifestPath, generated, options, localSummary,
                       manifestStreetSignatures,
                       generatedRoadSignatures, generatedFiles,
                       &manifestError)) {
        localSummary.warnings += manifestError;
    }
    reportProgress(++progressValue,
                   QObject::tr("Wrote the OSM source manifest."));
    if (mergedStreets)
        *mergedStreets = merged;
    if (summary)
        *summary = localSummary;
    reportProgress(++progressValue,
                   QObject::tr("OpenStreetMap project generation complete."));
    QStringList zoneCounts;
    QStringList loggedZoneTypes = localSummary.zoneTypeCounts.keys();
    loggedZoneTypes.sort(Qt::CaseInsensitive);
    for (const QString &zoneType : loggedZoneTypes) {
        zoneCounts += QStringLiteral("%1=%2")
                .arg(zoneType)
                .arg(localSummary.zoneTypeCounts.value(zoneType));
    }
    qInfo().noquote()
            << QStringLiteral(
                   "OSM project-data complete: streets %1, InGameMap features %2, proxy TBX buildings %3, road-marking segments %4, Nav rectangles %5, merged typed zone objects %6 [%7], skipped proxies %8, skipped InGameMap details %9, elapsed %10 ms")
               .arg(localSummary.streets)
               .arg(localSummary.inGameMapFeatures)
               .arg(localSummary.proxyBuildings)
               .arg(localSummary.roadMarkings)
               .arg(localSummary.navZones)
               .arg(localSummary.foragingZones)
               .arg(zoneCounts.join(QStringLiteral(", ")))
               .arg(localSummary.skippedProxyBuildings)
               .arg(localSummary.skippedInGameMapFeatures)
               .arg(elapsed.elapsed());
    return true;
}
