#include "osmterrainimporter.h"
#include <QColor>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>
namespace {
constexpr double EARTH_RADIUS_METERS = 6378137.0;
constexpr double PI = 3.14159265358979323846;
enum class GroundKind
{
    None,
    LightGrass,
    MediumGrass,
    DarkGrass,
    Dirt,
    Water,
    Sand
};
enum class VegetationKind
{
    None,
    Grass,
    SparseTrees,
    ModerateTrees,
    DenseTrees
};
struct OsmRenderFeature
{
    QHash<QString, QString> tags;
    QVector<QPolygonF> geometries;
    GroundKind groundKind = GroundKind::None;
    VegetationKind vegetationKind = VegetationKind::None;
    bool road = false;
    bool railway = false;
    bool waterLine = false;
    bool vegetationLine = false;
    int roadPriority = 0;
    double lineWidthMeters = 0.0;
    QColor lineColor;
};
double degreesToRadians(double degrees)
{
    return degrees * PI / 180.0;
}
double radiansToDegrees(double radians)
{
    return radians * 180.0 / PI;
}
QString tagValue(const QHash<QString, QString> &tags, const QString &name)
{
    return tags.value(name).trimmed().toLower();
}
QHash<QString, QString> readTags(const QJsonObject &element)
{
    QHash<QString, QString> tags;
    const QJsonObject object = element.value(
                QStringLiteral("tags")).toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        tags.insert(it.key(), it.value().toString());
    return tags;
}
QPointF projectCoordinate(double latitude, double longitude,
                          const OsmTerrainImportOptions &options)
{
    const double centerLatitudeRadians =
            degreesToRadians(options.centerLatitude);
    const double east = degreesToRadians(
                longitude - options.centerLongitude)
            * EARTH_RADIUS_METERS * std::cos(centerLatitudeRadians);
    const double north = degreesToRadians(
                latitude - options.centerLatitude)
            * EARTH_RADIUS_METERS;
    const double sourceX = east / options.metersPerPixel;
    const double sourceY = -north / options.metersPerPixel;
    const double rotation = degreesToRadians(
                options.rotationDegreesClockwise);
    const double cosine = std::cos(rotation);
    const double sine = std::sin(rotation);
    const double rotatedX = cosine * sourceX - sine * sourceY;
    const double rotatedY = sine * sourceX + cosine * sourceY;
    return QPointF(options.widthPixels / 2.0 + rotatedX,
                   options.heightPixels / 2.0 + rotatedY);
}
double normalizedGridAngle(double degrees)
{
    while (degrees < -45.0)
        degrees += 90.0;
    while (degrees >= 45.0)
        degrees -= 90.0;
    return degrees;
}
double gridAngleDistance(double left, double right)
{
    return std::abs(normalizedGridAngle(left - right));
}
double roadOrientationWeight(const QString &highway)
{
    if (highway == QLatin1String("primary")
            || highway == QLatin1String("secondary")
            || highway == QLatin1String("tertiary")) {
        return 1.25;
    }
    if (highway == QLatin1String("residential")
            || highway == QLatin1String("unclassified")
            || highway == QLatin1String("living_street")) {
        return 1.0;
    }
    if (highway == QLatin1String("service"))
        return 0.65;
    if (highway == QLatin1String("motorway")
            || highway == QLatin1String("trunk")) {
        return 0.45;
    }
    return 0.20;
}
QPolygonF readGeometry(const QJsonArray &geometry,
                       const OsmTerrainImportOptions &options)
{
    QPolygonF points;
    points.reserve(geometry.size());
    for (const QJsonValue &value : geometry) {
        const QJsonObject coordinate = value.toObject();
        if (!coordinate.contains(QStringLiteral("lat"))
                || !coordinate.contains(QStringLiteral("lon"))) {
            continue;
        }
        points += projectCoordinate(
                    coordinate.value(QStringLiteral("lat")).toDouble(),
                    coordinate.value(QStringLiteral("lon")).toDouble(),
                    options);
    }
    return points;
}
bool pointsMatch(const QPointF &left, const QPointF &right)
{
    const QPointF delta = left - right;
    return delta.x() * delta.x() + delta.y() * delta.y() < 0.0025;
}
QVector<QPolygonF> assembleRings(QVector<QPolygonF> segments,
                                 int *openSegmentCount)
{
    QVector<QPolygonF> rings;
    int open = 0;
    while (!segments.isEmpty()) {
        QPolygonF ring = segments.takeFirst();
        bool changed = true;
        while (!ring.isEmpty() && !pointsMatch(ring.first(), ring.last())
               && changed) {
            changed = false;
            for (int index = 0; index < segments.size(); ++index) {
                QPolygonF segment = segments.at(index);
                if (segment.isEmpty()) {
                    segments.removeAt(index);
                    changed = true;
                    break;
                }
                if (pointsMatch(ring.last(), segment.first())) {
                    segment.removeFirst();
                    ring += segment;
                } else if (pointsMatch(ring.last(), segment.last())) {
                    std::reverse(segment.begin(), segment.end());
                    segment.removeFirst();
                    ring += segment;
                } else if (pointsMatch(ring.first(), segment.last())) {
                    segment.removeLast();
                    segment += ring;
                    ring = segment;
                } else if (pointsMatch(ring.first(), segment.first())) {
                    std::reverse(segment.begin(), segment.end());
                    segment.removeLast();
                    segment += ring;
                    ring = segment;
                } else {
                    continue;
                }
                segments.removeAt(index);
                changed = true;
                break;
            }
        }
        if (ring.size() >= 4 && pointsMatch(ring.first(), ring.last())) {
            ring.last() = ring.first();
            rings += ring;
        } else {
            ++open;
        }
    }
    if (openSegmentCount)
        *openSegmentCount += open;
    return rings;
}
QVector<QPolygonF> readElementGeometry(
        const QJsonObject &element,
        const OsmTerrainImportOptions &options,
        bool polygon,
        int *openSegmentCount)
{
    QVector<QPolygonF> geometries;
    if (element.value(QStringLiteral("type")).toString()
            == QLatin1String("node")
            && element.contains(QStringLiteral("lat"))
            && element.contains(QStringLiteral("lon"))) {
        const QPointF center = projectCoordinate(
                    element.value(QStringLiteral("lat")).toDouble(),
                    element.value(QStringLiteral("lon")).toDouble(),
                    options);
        const double radius = 1.5;
        QPolygonF pointArea;
        pointArea << QPointF(center.x() - radius,
                             center.y() - radius)
                  << QPointF(center.x() + radius,
                             center.y() - radius)
                  << QPointF(center.x() + radius,
                             center.y() + radius)
                  << QPointF(center.x() - radius,
                             center.y() + radius)
                  << QPointF(center.x() - radius,
                             center.y() - radius);
        geometries += pointArea;
    }
    const QJsonArray direct = element.value(
                QStringLiteral("geometry")).toArray();
    if (!direct.isEmpty()) {
        QPolygonF geometry = readGeometry(direct, options);
        if (geometry.size() >= 2)
            geometries += geometry;
    }
    const QJsonArray members = element.value(
                QStringLiteral("members")).toArray();
    if (!members.isEmpty()) {
        QVector<QPolygonF> segments;
        for (const QJsonValue &memberValue : members) {
            const QJsonObject member = memberValue.toObject();
            if (member.value(QStringLiteral("type")).toString()
                    != QLatin1String("way")) {
                continue;
            }
            QPolygonF geometry = readGeometry(
                        member.value(QStringLiteral("geometry")).toArray(),
                        options);
            if (geometry.size() >= 2)
                segments += geometry;
        }
        if (polygon)
            geometries += assembleRings(segments, openSegmentCount);
        else
            geometries += segments;
    }
    return geometries;
}
QVector<QPolygonF> readBuildingOuterGeometry(
        const QJsonObject &element,
        const OsmTerrainImportOptions &options,
        int *openSegmentCount)
{
    QVector<QPolygonF> geometries;
    const QJsonArray direct = element.value(
                QStringLiteral("geometry")).toArray();
    if (!direct.isEmpty()) {
        const QPolygonF geometry = readGeometry(direct, options);
        if (geometry.size() >= 2)
            geometries += geometry;
    }
    QVector<QPolygonF> outerSegments;
    const QJsonArray members = element.value(
                QStringLiteral("members")).toArray();
    for (const QJsonValue &memberValue : members) {
        const QJsonObject member = memberValue.toObject();
        if (member.value(QStringLiteral("type")).toString()
                != QLatin1String("way")
                || member.value(QStringLiteral("role")).toString()
                == QLatin1String("inner")) {
            continue;
        }
        const QPolygonF geometry = readGeometry(
                    member.value(QStringLiteral("geometry")).toArray(),
                    options);
        if (geometry.size() >= 2)
            outerSegments += geometry;
    }
    if (!outerSegments.isEmpty())
        geometries += assembleRings(outerSegments, openSegmentCount);
    return geometries;
}
bool isClosed(const QPolygonF &geometry)
{
    return geometry.size() >= 4
            && pointsMatch(geometry.first(), geometry.last());
}
double polygonArea(const QPolygonF &polygon)
{
    double area = 0.0;
    for (int index = 0; index < polygon.size(); ++index) {
        const QPointF &first = polygon.at(index);
        const QPointF &second = polygon.at(
                    (index + 1) % polygon.size());
        area += first.x() * second.y() - second.x() * first.y();
    }
    return std::abs(area) / 2.0;
}
QPainterPath buildingPath(const OsmProjectFeature &feature)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (QPolygonF polygon : feature.geometries) {
        if (polygon.size() < 3)
            continue;
        if (polygon.first() != polygon.last())
            polygon += polygon.first();
        path.addPolygon(polygon);
    }
    return path;
}
double pathArea(const QPainterPath &path)
{
    double area = 0.0;
    for (const QPolygonF &polygon : path.toFillPolygons())
        area += polygonArea(polygon);
    return area;
}
int removeNestedBuildingFootprints(OsmTerrainImportResult *result)
{
    struct Candidate
    {
        int featureIndex = -1;
        QPainterPath path;
        double area = 0.0;
        bool relation = false;
    };
    QVector<Candidate> candidates;
    for (int index = 0; index < result->projectFeatures.size(); ++index) {
        const OsmProjectFeature &feature = result->projectFeatures.at(index);
        if (!feature.building)
            continue;
        Candidate candidate;
        candidate.featureIndex = index;
        candidate.path = buildingPath(feature);
        candidate.area = pathArea(candidate.path);
        candidate.relation = feature.osmType == QLatin1String("relation");
        if (!candidate.path.isEmpty() && candidate.area >= 1.0)
            candidates += candidate;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &left, const Candidate &right) {
        if (qAbs(left.area - right.area) < 0.01
                && left.relation != right.relation) {
            return left.relation;
        }
        return left.area > right.area;
    });
    QVector<Candidate> accepted;
    QSet<int> removed;
    for (const Candidate &candidate : candidates) {
        bool nested = false;
        for (const Candidate &outer : accepted) {
            if (!outer.path.boundingRect().intersects(
                        candidate.path.boundingRect())) {
                continue;
            }
            const double overlap = pathArea(
                        outer.path.intersected(candidate.path));
            if (overlap >= candidate.area * 0.80) {
                nested = true;
                break;
            }
        }
        if (nested)
            removed += candidate.featureIndex;
        else
            accepted += candidate;
    }
    if (removed.isEmpty())
        return 0;
    QVector<OsmProjectFeature> filtered;
    filtered.reserve(result->projectFeatures.size() - removed.size());
    for (int index = 0; index < result->projectFeatures.size(); ++index) {
        if (!removed.contains(index))
            filtered += result->projectFeatures.at(index);
    }
    result->projectFeatures = filtered;
    return removed.size();
}
GroundKind classifyGround(const QHash<QString, QString> &tags)
{
    const QString natural = tagValue(tags, QStringLiteral("natural"));
    const QString landuse = tagValue(tags, QStringLiteral("landuse"));
    const QString landcover = tagValue(tags, QStringLiteral("landcover"));
    const QString water = tagValue(tags, QStringLiteral("water"));
    const QString waterway = tagValue(tags, QStringLiteral("waterway"));
    const QString leisure = tagValue(tags, QStringLiteral("leisure"));
    if (!water.isEmpty()
            || natural == QLatin1String("water")
            || natural == QLatin1String("wetland")
            || natural == QLatin1String("bay")
            || landuse == QLatin1String("reservoir")
            || landuse == QLatin1String("basin")
            || waterway == QLatin1String("riverbank")) {
        return GroundKind::Water;
    }
    if (natural == QLatin1String("sand")
            || natural == QLatin1String("beach")
            || landcover == QLatin1String("sand")) {
        return GroundKind::Sand;
    }
    if (natural == QLatin1String("wood")
            || natural == QLatin1String("forest")
            || natural == QLatin1String("tree_group")
            || landuse == QLatin1String("forest")
            || landcover == QLatin1String("trees")
            || landcover == QLatin1String("forest")
            || landcover == QLatin1String("wood")) {
        return GroundKind::DarkGrass;
    }
    if (natural == QLatin1String("scrub")
            || natural == QLatin1String("shrubbery")
            || natural == QLatin1String("shrub")
            || natural == QLatin1String("heath")
            || natural == QLatin1String("fell")
            || landuse == QLatin1String("orchard")
            || landuse == QLatin1String("plant_nursery")
            || landcover == QLatin1String("scrub")
            || landcover == QLatin1String("shrubs")
            || landcover == QLatin1String("bushes")) {
        return GroundKind::MediumGrass;
    }
    if (landuse == QLatin1String("farmland")
            || landuse == QLatin1String("farmyard")
            || landuse == QLatin1String("greenhouse_horticulture")
            || landuse == QLatin1String("allotments")) {
        return GroundKind::Dirt;
    }
    if (natural == QLatin1String("grassland")
            || natural == QLatin1String("meadow")
            || landuse == QLatin1String("grass")
            || landuse == QLatin1String("meadow")
            || landuse == QLatin1String("vineyard")
            || landuse == QLatin1String("recreation_ground")
            || landuse == QLatin1String("village_green")
            || landuse == QLatin1String("cemetery")
            || landcover == QLatin1String("grass")
            || landcover == QLatin1String("meadow")
            || leisure == QLatin1String("park")
            || leisure == QLatin1String("garden")
            || leisure == QLatin1String("pitch")
            || leisure == QLatin1String("golf_course")) {
        return GroundKind::LightGrass;
    }
    return GroundKind::None;
}
VegetationKind classifyVegetation(const QHash<QString, QString> &tags)
{
    const QString natural = tagValue(tags, QStringLiteral("natural"));
    const QString landuse = tagValue(tags, QStringLiteral("landuse"));
    const QString landcover = tagValue(tags, QStringLiteral("landcover"));
    const QString leisure = tagValue(tags, QStringLiteral("leisure"));
    if (natural == QLatin1String("wood")
            || natural == QLatin1String("forest")
            || natural == QLatin1String("tree_group")
            || landuse == QLatin1String("forest")
            || landcover == QLatin1String("trees")
            || landcover == QLatin1String("forest")
            || landcover == QLatin1String("wood")) {
        return VegetationKind::DenseTrees;
    }
    if (natural == QLatin1String("scrub")
            || natural == QLatin1String("shrubbery")
            || natural == QLatin1String("shrub")
            || landuse == QLatin1String("orchard")
            || landuse == QLatin1String("plant_nursery")
            || landcover == QLatin1String("scrub")
            || landcover == QLatin1String("shrubs")
            || landcover == QLatin1String("bushes")) {
        return VegetationKind::ModerateTrees;
    }
    if (natural == QLatin1String("heath")
            || natural == QLatin1String("tree_row")
            || natural == QLatin1String("tree")
            || landuse == QLatin1String("vineyard")) {
        return VegetationKind::SparseTrees;
    }
    if (natural == QLatin1String("grassland")
            || natural == QLatin1String("meadow")
            || landuse == QLatin1String("grass")
            || landuse == QLatin1String("meadow")
            || landuse == QLatin1String("farmland")
            || landuse == QLatin1String("recreation_ground")
            || landuse == QLatin1String("village_green")
            || landuse == QLatin1String("cemetery")
            || landcover == QLatin1String("grass")
            || landcover == QLatin1String("meadow")
            || leisure == QLatin1String("park")
            || leisure == QLatin1String("garden")
            || leisure == QLatin1String("pitch")
            || leisure == QLatin1String("golf_course")) {
        return VegetationKind::Grass;
    }
    return VegetationKind::None;
}
QString classifyForagingZone(const QHash<QString, QString> &tags)
{
    const QString natural = tagValue(tags, QStringLiteral("natural"));
    const QString landuse = tagValue(tags, QStringLiteral("landuse"));
    const QString landcover = tagValue(tags, QStringLiteral("landcover"));
    const QString leisure = tagValue(tags, QStringLiteral("leisure"));
    const QString water = tagValue(tags, QStringLiteral("water"));
    const QString waterway = tagValue(tags, QStringLiteral("waterway"));
    if (!water.isEmpty()
            || natural == QLatin1String("water")
            || natural == QLatin1String("wetland")
            || natural == QLatin1String("bay")
            || landuse == QLatin1String("reservoir")
            || landuse == QLatin1String("basin")
            || waterway == QLatin1String("riverbank")) {
        return QStringLiteral("Water");
    }
    if (natural == QLatin1String("wood")
            || natural == QLatin1String("forest")
            || natural == QLatin1String("tree_group")
            || landuse == QLatin1String("forest")
            || landcover == QLatin1String("trees")
            || landcover == QLatin1String("forest")
            || landcover == QLatin1String("wood")) {
        return QStringLiteral("DeepForest");
    }
    if (landuse == QLatin1String("farmland"))
        return QStringLiteral("FarmLand");
    if (landuse == QLatin1String("farmyard")
            || landuse == QLatin1String("orchard")
            || landuse == QLatin1String("vineyard")
            || landuse == QLatin1String("greenhouse_horticulture")
            || landuse == QLatin1String("plant_nursery")
            || landuse == QLatin1String("allotments")) {
        return QStringLiteral("Farm");
    }
    if (landuse == QLatin1String("residential")
            || landuse == QLatin1String("commercial")
            || landuse == QLatin1String("retail")
            || landuse == QLatin1String("industrial")) {
        return QStringLiteral("TownZone");
    }
    if (natural == QLatin1String("scrub")
            || natural == QLatin1String("shrubbery")
            || natural == QLatin1String("shrub")
            || natural == QLatin1String("heath")
            || natural == QLatin1String("fell")
            || natural == QLatin1String("tree_row")
            || landcover == QLatin1String("scrub")
            || landcover == QLatin1String("shrubs")
            || landcover == QLatin1String("bushes")) {
        return QStringLiteral("Forest");
    }
    if (natural == QLatin1String("grassland")
            || natural == QLatin1String("meadow")
            || landuse == QLatin1String("grass")
            || landuse == QLatin1String("meadow")
            || landuse == QLatin1String("recreation_ground")
            || landuse == QLatin1String("village_green")
            || landuse == QLatin1String("cemetery")
            || landcover == QLatin1String("grass")
            || landcover == QLatin1String("meadow")
            || leisure == QLatin1String("park")
            || leisure == QLatin1String("garden")
            || leisure == QLatin1String("pitch")
            || leisure == QLatin1String("golf_course")) {
        return QStringLiteral("Vegitation");
    }
    return QString();
}
int buildingLevels(const QHash<QString, QString> &tags)
{
    bool ok = false;
    int levels = tagValue(tags, QStringLiteral("building:levels")).toInt(&ok);
    if (!ok || levels < 1) {
        QString heightText = tagValue(tags, QStringLiteral("height"));
        if (heightText.isEmpty())
            heightText = tagValue(tags, QStringLiteral("building:height"));
        heightText.replace(QLatin1Char(','), QLatin1Char('.'));
        const QRegularExpression heightPattern(
                    QStringLiteral("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*(m|meter|meters|metre|metres|ft|feet|')?"));
        const QRegularExpressionMatch match = heightPattern.match(heightText);
        double heightMeters = 0.0;
        if (match.hasMatch()) {
            heightMeters = match.captured(1).toDouble(&ok);
            const QString unit = match.captured(2);
            if (unit == QLatin1String("ft")
                    || unit == QLatin1String("feet")
                    || unit == QLatin1String("'")) {
                heightMeters *= 0.3048;
            }
        }
        levels = ok && heightMeters > 0.0
                ? qMax(1, qRound(heightMeters / 3.0)) : 1;
    }
    return qBound(1, levels, 32);
}
int roadPriority(const QString &highway)
{
    static const QHash<QString, int> priorities = {
        {QStringLiteral("path"), 1},
        {QStringLiteral("track"), 2},
        {QStringLiteral("footway"), 3},
        {QStringLiteral("cycleway"), 4},
        {QStringLiteral("bridleway"), 5},
        {QStringLiteral("service"), 6},
        {QStringLiteral("unclassified"), 7},
        {QStringLiteral("residential"), 8},
        {QStringLiteral("tertiary"), 9},
        {QStringLiteral("secondary"), 10},
        {QStringLiteral("primary"), 11},
        {QStringLiteral("trunk"), 12},
        {QStringLiteral("motorway"), 13}
    };
    return priorities.value(highway, 5);
}
double roadWidth(const QString &highway)
{
    static const QHash<QString, double> widths = {
        {QStringLiteral("motorway"), 20.0},
        {QStringLiteral("primary"), 15.0},
        {QStringLiteral("trunk"), 15.0},
        {QStringLiteral("secondary"), 10.0},
        {QStringLiteral("tertiary"), 8.0},
        {QStringLiteral("residential"), 7.0},
        {QStringLiteral("service"), 7.0},
        {QStringLiteral("unclassified"), 7.0},
        {QStringLiteral("path"), 2.0},
        {QStringLiteral("track"), 2.0},
        {QStringLiteral("bridleway"), 2.0},
        {QStringLiteral("cycleway"), 2.0},
        {QStringLiteral("footway"), 2.0}
    };
    return widths.value(highway, 8.0);
}
double roadWidth(const QHash<QString, QString> &tags,
                 const QString &highway)
{
    QString widthText = tagValue(tags, QStringLiteral("width"));
    widthText.replace(QLatin1Char(','), QLatin1Char('.'));
    const QRegularExpression widthPattern(
                QStringLiteral("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*(m|meter|meters|metre|metres|ft|feet|')?"));
    const QRegularExpressionMatch match = widthPattern.match(widthText);
    if (!match.hasMatch())
        return roadWidth(highway);
    bool ok = false;
    double widthMeters = match.captured(1).toDouble(&ok);
    if (!ok || widthMeters <= 0.0)
        return roadWidth(highway);
    const QString unit = match.captured(2);
    if (unit == QLatin1String("ft")
            || unit == QLatin1String("feet")
            || unit == QLatin1String("'")) {
        widthMeters *= 0.3048;
    }
    return qBound(0.5, widthMeters, 60.0);
}
bool isSupportedRailway(const QString &railway)
{
    static const QSet<QString> supported = {
        QStringLiteral("rail"),
        QStringLiteral("light_rail"),
        QStringLiteral("narrow_gauge"),
        QStringLiteral("subway"),
        QStringLiteral("tram"),
        QStringLiteral("monorail"),
        QStringLiteral("funicular")
    };
    return supported.contains(railway);
}
double railwayWidth(const QString &railway)
{
    return railway == QLatin1String("rail")
            || railway == QLatin1String("monorail") ? 4.0 : 2.5;
}
QColor roadColor(const QString &highway, const QString &surface)
{
    if (highway == QLatin1String("motorway")
            || highway == QLatin1String("primary")
            || highway == QLatin1String("trunk")) {
        return QColor(100, 100, 100);
    }
    if (highway == QLatin1String("path")
            || highway == QLatin1String("track")
            || highway == QLatin1String("bridleway")
            || highway == QLatin1String("cycleway")
            || highway == QLatin1String("footway")) {
        if (surface == QLatin1String("sand"))
            return QColor(210, 200, 160);
        if (surface == QLatin1String("gravel")
                || surface == QLatin1String("dirt")
                || surface == QLatin1String("earth")) {
            return QColor(140, 70, 15);
        }
        return QColor(120, 70, 20);
    }
    return QColor(120, 120, 120);
}
QColor groundColor(GroundKind kind)
{
    switch (kind) {
    case GroundKind::LightGrass: return QColor(145, 135, 60);
    case GroundKind::MediumGrass: return QColor(117, 117, 47);
    case GroundKind::DarkGrass: return QColor(90, 100, 35);
    case GroundKind::Dirt: return QColor(120, 70, 20);
    case GroundKind::Water: return QColor(0, 138, 255);
    case GroundKind::Sand: return QColor(210, 200, 160);
    case GroundKind::None: break;
    }
    return QColor();
}
QColor vegetationColor(VegetationKind kind)
{
    switch (kind) {
    case VegetationKind::Grass: return QColor(0, 255, 0);
    case VegetationKind::SparseTrees: return QColor(64, 0, 0);
    case VegetationKind::ModerateTrees: return QColor(127, 0, 0);
    case VegetationKind::DenseTrees: return QColor(200, 0, 0);
    case VegetationKind::None: break;
    }
    return QColor();
}
void fillFeature(QPainter &painter, const OsmRenderFeature &feature,
                 const QColor &color)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (QPolygonF geometry : feature.geometries) {
        if (geometry.size() < 4)
            continue;
        if (!pointsMatch(geometry.first(), geometry.last()))
            geometry += geometry.first();
        path.addPolygon(geometry);
    }
    if (!path.isEmpty())
        painter.fillPath(path, color);
}
void drawLines(QPainter &painter, const OsmRenderFeature &feature,
               double metersPerPixel, int widthPercent,
               const QColor &overrideColor = QColor())
{
    const double width = qMax(
                1.0, feature.lineWidthMeters / metersPerPixel
                * widthPercent / 100.0);
    painter.setPen(QPen(overrideColor.isValid()
                        ? overrideColor : feature.lineColor, width,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const QPolygonF &geometry : feature.geometries) {
        if (geometry.size() >= 2)
            painter.drawPolyline(geometry);
    }
}
QByteArray createMetadata(const OsmTerrainImportOptions &options,
                          const OsmTerrainImportResult &result)
{
    const QRectF bounds = OsmTerrainImporter::geographicBounds(options);
    QJsonObject object;
    object.insert(QStringLiteral("source"),
                  QStringLiteral("OpenStreetMap"));
    object.insert(QStringLiteral("attribution"),
                  QString(QChar(0x00a9))
                  + QStringLiteral(" OpenStreetMap contributors"));
    object.insert(QStringLiteral("license"),
                  QStringLiteral("https://www.openstreetmap.org/copyright"));
    object.insert(QStringLiteral("endpoint"), options.endpoint);
    object.insert(QStringLiteral("generatedUtc"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    object.insert(QStringLiteral("centerLatitude"), options.centerLatitude);
    object.insert(QStringLiteral("centerLongitude"), options.centerLongitude);
    object.insert(QStringLiteral("south"), bounds.top());
    object.insert(QStringLiteral("west"), bounds.left());
    object.insert(QStringLiteral("north"), bounds.bottom());
    object.insert(QStringLiteral("east"), bounds.right());
    object.insert(QStringLiteral("widthPixels"), options.widthPixels);
    object.insert(QStringLiteral("heightPixels"), options.heightPixels);
    object.insert(QStringLiteral("metersPerPixel"), options.metersPerPixel);
    object.insert(QStringLiteral("sourceElements"),
                  result.sourceElementCount);
    object.insert(QStringLiteral("projectVectorFeatures"),
                  result.projectFeatures.size());
    object.insert(QStringLiteral("buildings"), result.buildingCount);
    object.insert(QStringLiteral("namedStreets"), result.namedStreetCount);
    object.insert(QStringLiteral("railways"), result.railwayCount);
    object.insert(QStringLiteral("farmAreas"), result.farmAreaCount);
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}
QString buildOverpassQueryForBounds(
        const OsmTerrainImportOptions &options, const QRectF &bounds)
{
    const QString bbox = QStringLiteral("(%1,%2,%3,%4)")
            .arg(bounds.top(), 0, 'f', 7)
            .arg(bounds.left(), 0, 'f', 7)
            .arg(bounds.bottom(), 0, 'f', 7)
            .arg(bounds.right(), 0, 'f', 7);
    QStringList clauses;
    if (options.includeLand) {
        const QString naturalValues = QStringLiteral(
                    "^(sand|beach|wood|forest|tree_group|scrub|shrub|shrubbery|heath|fell|grassland|"
                    "meadow|tree_row)$");
        const QString landuseValues = QStringLiteral(
                    "^(forest|orchard|vineyard|greenhouse_horticulture|plant_nursery|allotments|grass|meadow|farmland|farmyard|"
                    "recreation_ground|village_green|cemetery|residential|commercial|"
                    "retail|industrial)$");
        const QString landcoverValues = QStringLiteral(
                    "^(trees|forest|wood|scrub|shrubs|bushes|grass|meadow|sand)$");
        const QString leisureValues = QStringLiteral(
                    "^(park|garden|pitch|golf_course)$");
        clauses += QStringLiteral("way[\"natural\"~\"%1\"]%2;")
                .arg(naturalValues, bbox);
        clauses += QStringLiteral("rel[\"natural\"~\"%1\"]%2;")
                .arg(naturalValues, bbox);
        clauses += QStringLiteral(
                    "node[\"natural\"=\"tree\"]%1;")
                .arg(bbox);
        clauses += QStringLiteral("way[\"barrier\"=\"hedge\"]%1;")
                .arg(bbox);
        clauses += QStringLiteral("way[\"landuse\"~\"%1\"]%2;")
                .arg(landuseValues, bbox);
        clauses += QStringLiteral("rel[\"landuse\"~\"%1\"]%2;")
                .arg(landuseValues, bbox);
        clauses += QStringLiteral("way[\"landcover\"~\"%1\"]%2;")
                .arg(landcoverValues, bbox);
        clauses += QStringLiteral("rel[\"landcover\"~\"%1\"]%2;")
                .arg(landcoverValues, bbox);
        clauses += QStringLiteral("way[\"leisure\"~\"%1\"]%2;")
                .arg(leisureValues, bbox);
        clauses += QStringLiteral("rel[\"leisure\"~\"%1\"]%2;")
                .arg(leisureValues, bbox);
    }
    if (options.includeWater) {
        const QString naturalWaterValues = QStringLiteral(
                    "^(water|wetland|bay)$");
        const QString landuseWaterValues = QStringLiteral(
                    "^(reservoir|basin)$");
        clauses += QStringLiteral("way[\"natural\"~\"%1\"]%2;")
                .arg(naturalWaterValues, bbox);
        clauses += QStringLiteral("rel[\"natural\"~\"%1\"]%2;")
                .arg(naturalWaterValues, bbox);
        clauses += QStringLiteral("way[\"landuse\"~\"%1\"]%2;")
                .arg(landuseWaterValues, bbox);
        clauses += QStringLiteral("rel[\"landuse\"~\"%1\"]%2;")
                .arg(landuseWaterValues, bbox);
        clauses += QStringLiteral("way[\"water\"]%1;").arg(bbox);
        clauses += QStringLiteral("rel[\"water\"]%1;").arg(bbox);
        clauses += QStringLiteral("way[\"waterway\"]%1;").arg(bbox);
        clauses += QStringLiteral(
                    "rel[\"waterway\"=\"riverbank\"]%1;").arg(bbox);
    }
    if (options.includeRoads) {
        const QString highwayValues = QStringLiteral(
                    "^(motorway|trunk|primary|secondary|tertiary|residential|"
                    "living_street|service|unclassified|path|track|bridleway|"
                    "cycleway|footway)$");
        clauses += QStringLiteral("way[\"highway\"~\"%1\"]%2;")
                .arg(highwayValues, bbox);
        const QString railwayValues = QStringLiteral(
                    "^(rail|light_rail|narrow_gauge|subway|tram|monorail|funicular)$");
        clauses += QStringLiteral("way[\"railway\"~\"%1\"]%2;")
                .arg(railwayValues, bbox);
    }
    if (options.includeBuildings) {
        clauses += QStringLiteral("way[\"building\"]%1;").arg(bbox);
        clauses += QStringLiteral("rel[\"building\"]%1;").arg(bbox);
    }
    return QStringLiteral(
                "[out:json][timeout:60][maxsize:134217728];\n"
                "(\n  %1\n);\n"
                "out geom qt;")
            .arg(clauses.join(QStringLiteral("\n  ")));
}
}
bool OsmTerrainImporter::parseLocationText(
        const QString &text, double *latitude, double *longitude,
        QString *error)
{
    const QString input = text.trimmed();
    if (input.isEmpty()) {
        if (error)
            *error = QStringLiteral("Paste coordinates or a map link.");
        return false;
    }
    auto accept = [latitude, longitude, error](
            const QString &latitudeText,
            const QString &longitudeText) {
        bool latitudeOk = false;
        bool longitudeOk = false;
        const double parsedLatitude = latitudeText.toDouble(&latitudeOk);
        const double parsedLongitude = longitudeText.toDouble(&longitudeOk);
        if (!latitudeOk || !longitudeOk
                || parsedLatitude < -85.0 || parsedLatitude > 85.0
                || parsedLongitude < -180.0
                || parsedLongitude > 180.0) {
            if (error) {
                *error = QStringLiteral(
                            "The link contains coordinates outside the "
                            "supported latitude or longitude range.");
            }
            return false;
        }
        if (latitude)
            *latitude = parsedLatitude;
        if (longitude)
            *longitude = parsedLongitude;
        return true;
    };
    const QString decoded = QUrl::fromPercentEncoding(input.toUtf8());
    const QList<QRegularExpression> linkPatterns = {
        QRegularExpression(QStringLiteral(
            "[#?&]map=[0-9.]+/(-?[0-9]+(?:\\.[0-9]+)?)/"
            "(-?[0-9]+(?:\\.[0-9]+)?)")),
        QRegularExpression(QStringLiteral(
            "@(-?[0-9]+(?:\\.[0-9]+)?),"
            "(-?[0-9]+(?:\\.[0-9]+)?)")),
        QRegularExpression(QStringLiteral(
            "!3d(-?[0-9]+(?:\\.[0-9]+)?)!4d"
            "(-?[0-9]+(?:\\.[0-9]+)?)")),
        QRegularExpression(QStringLiteral(
            "(?:^|[/=])(-?[0-9]+(?:\\.[0-9]+)?),\\+?"
            "(-?[0-9]+(?:\\.[0-9]+)?)(?:$|[/&,])"))
    };
    for (const QRegularExpression &pattern : linkPatterns) {
        const QRegularExpressionMatch match = pattern.match(decoded);
        if (match.hasMatch())
            return accept(match.captured(1), match.captured(2));
    }
    const QUrl url(input);
    if (url.isValid() && !url.scheme().isEmpty()) {
        const QUrlQuery query(url);
        const QString latitudeText = query.queryItemValue(
                    QStringLiteral("mlat"));
        const QString longitudeText = query.queryItemValue(
                    QStringLiteral("mlon"));
        if (!latitudeText.isEmpty() && !longitudeText.isEmpty())
            return accept(latitudeText, longitudeText);
        const QStringList coordinateKeys = {
            QStringLiteral("q"),
            QStringLiteral("query"),
            QStringLiteral("destination")
        };
        const QRegularExpression coordinatePair(QStringLiteral(
            "^\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*[, ]\\s*"
            "(-?[0-9]+(?:\\.[0-9]+)?)\\s*$"));
        for (const QString &key : coordinateKeys) {
            const QRegularExpressionMatch match = coordinatePair.match(
                        query.queryItemValue(key));
            if (match.hasMatch())
                return accept(match.captured(1), match.captured(2));
        }
    }
    const QRegularExpression plainPair(QStringLiteral(
        "^\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*[,; ]\\s*"
        "(-?[0-9]+(?:\\.[0-9]+)?)\\s*$"));
    const QRegularExpressionMatch plainMatch = plainPair.match(decoded);
    if (plainMatch.hasMatch())
        return accept(plainMatch.captured(1), plainMatch.captured(2));
    if (error) {
        *error = QStringLiteral(
                    "No coordinates were found. Paste latitude and longitude, "
                    "a full OpenStreetMap link, or a full Google Maps link.");
    }
    return false;
}
QRectF OsmTerrainImporter::geographicBounds(
        const OsmTerrainImportOptions &options)
{
    const double margin = 1.0 + qMax(0, options.marginPercent) / 100.0;
    const double outputHalfWidth = options.widthPixels
            * options.metersPerPixel / 2.0;
    const double outputHalfHeight = options.heightPixels
            * options.metersPerPixel / 2.0;
    const double rotation = degreesToRadians(
                options.rotationDegreesClockwise);
    const double absoluteCosine = std::abs(std::cos(rotation));
    const double absoluteSine = std::abs(std::sin(rotation));
    const double halfWidthMeters =
            (absoluteCosine * outputHalfWidth
             + absoluteSine * outputHalfHeight) * margin;
    const double halfHeightMeters =
            (absoluteSine * outputHalfWidth
             + absoluteCosine * outputHalfHeight) * margin;
    const double latitudeDelta = radiansToDegrees(
                halfHeightMeters / EARTH_RADIUS_METERS);
    const double cosine = qMax(
                0.000001, std::abs(std::cos(
                              degreesToRadians(options.centerLatitude))));
    const double longitudeDelta = radiansToDegrees(
                halfWidthMeters / (EARTH_RADIUS_METERS * cosine));
    return QRectF(options.centerLongitude - longitudeDelta,
                  options.centerLatitude - latitudeDelta,
                  longitudeDelta * 2.0, latitudeDelta * 2.0);
}
QSize OsmTerrainImporter::requiredCells(
        double widthMeters, double heightMeters,
        int cellSize, double metersPerPixel)
{
    if (widthMeters <= 0.0 || heightMeters <= 0.0
            || cellSize <= 0 || metersPerPixel <= 0.0) {
        return QSize();
    }
    const double metersPerCell = cellSize * metersPerPixel;
    const double cellsWide = std::ceil(widthMeters / metersPerCell);
    const double cellsHigh = std::ceil(heightMeters / metersPerCell);
    if (cellsWide > std::numeric_limits<int>::max()
            || cellsHigh > std::numeric_limits<int>::max()) {
        return QSize();
    }
    return QSize(qMax(1, int(cellsWide)), qMax(1, int(cellsHigh)));
}
QStringList OsmTerrainImporter::buildOverpassQueries(
        const OsmTerrainImportOptions &options)
{
    const QRectF bounds = geographicBounds(options);
    const double latitudeMeters = degreesToRadians(bounds.height())
            * EARTH_RADIUS_METERS;
    const double longitudeMeters = degreesToRadians(bounds.width())
            * EARTH_RADIUS_METERS
            * qMax(0.000001, std::abs(std::cos(
                         degreesToRadians(options.centerLatitude))));
    const double maximumChunkMeters = options.includeBuildings
            ? 3000.0 : 5000.0;
    const int columns = qMax(1, int(std::ceil(
                                        longitudeMeters
                                        / maximumChunkMeters)));
    const int rows = qMax(1, int(std::ceil(
                                     latitudeMeters
                                     / maximumChunkMeters)));
    QStringList queries;
    queries.reserve(columns * rows);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const QRectF chunkBounds(
                        bounds.left()
                        + bounds.width() * column / columns,
                        bounds.top()
                        + bounds.height() * row / rows,
                        bounds.width() / columns,
                        bounds.height() / rows);
            queries += buildOverpassQueryForBounds(options, chunkBounds);
        }
    }
    return queries;
}
QString OsmTerrainImporter::buildRoadOrientationQuery(
        const OsmTerrainImportOptions &options)
{
    const QRectF bounds = geographicBounds(options);
    const QString bbox = QStringLiteral("(%1,%2,%3,%4)")
            .arg(bounds.top(), 0, 'f', 7)
            .arg(bounds.left(), 0, 'f', 7)
            .arg(bounds.bottom(), 0, 'f', 7)
            .arg(bounds.right(), 0, 'f', 7);
    const QString highwayValues = QStringLiteral(
                "^(motorway|trunk|primary|secondary|tertiary|residential|"
                "living_street|service|unclassified|path|track|bridleway|"
                "cycleway|footway)$");
    return QStringLiteral(
                "[out:json][timeout:30][maxsize:67108864];\n"
                "way[\"highway\"~\"%1\"]%2;\n"
                "out geom qt;")
            .arg(highwayValues, bbox);
}
bool OsmTerrainImporter::suggestRoadGridRotation(
        const QByteArray &json, const OsmTerrainImportOptions &options,
        double *rotationDegreesClockwise, int *segmentCount,
        double *confidence, QString *error)
{
    if (rotationDegreesClockwise)
        *rotationDegreesClockwise = 0.0;
    if (segmentCount)
        *segmentCount = 0;
    if (confidence)
        *confidence = 0.0;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
        if (error) {
            *error = QStringLiteral(
                        "The road-orientation response is not valid JSON: %1")
                    .arg(parseError.errorString());
        }
        return false;
    }
    struct AngleSample
    {
        double angle = 0.0;
        double weight = 0.0;
    };
    QVector<AngleSample> samples;
    double totalWeight = 0.0;
    const double centerLatitudeRadians = degreesToRadians(
                options.centerLatitude);
    const QJsonArray elements = document.object().value(
                QStringLiteral("elements")).toArray();
    for (const QJsonValue &elementValue : elements) {
        const QJsonObject element = elementValue.toObject();
        const QHash<QString, QString> tags = readTags(element);
        const QString highway = tagValue(tags, QStringLiteral("highway"));
        if (highway.isEmpty())
            continue;
        const double classWeight = roadOrientationWeight(highway);
        const QJsonArray geometry = element.value(
                    QStringLiteral("geometry")).toArray();
        for (int index = 1; index < geometry.size(); ++index) {
            const QJsonObject first = geometry.at(index - 1).toObject();
            const QJsonObject second = geometry.at(index).toObject();
            if (!first.contains(QStringLiteral("lat"))
                    || !first.contains(QStringLiteral("lon"))
                    || !second.contains(QStringLiteral("lat"))
                    || !second.contains(QStringLiteral("lon"))) {
                continue;
            }
            const double east = degreesToRadians(
                        second.value(QStringLiteral("lon")).toDouble()
                        - first.value(QStringLiteral("lon")).toDouble())
                    * EARTH_RADIUS_METERS
                    * std::cos(centerLatitudeRadians);
            const double north = degreesToRadians(
                        second.value(QStringLiteral("lat")).toDouble()
                        - first.value(QStringLiteral("lat")).toDouble())
                    * EARTH_RADIUS_METERS;
            const double length = std::hypot(east, north);
            if (length < 3.0)
                continue;
            AngleSample sample;
            sample.angle = normalizedGridAngle(
                        radiansToDegrees(std::atan2(-north, east)));
            sample.weight = qMin(80.0, length) * classWeight;
            samples += sample;
            totalWeight += sample.weight;
        }
    }
    if (segmentCount)
        *segmentCount = samples.size();
    if (samples.size() < 4 || totalWeight < 80.0) {
        if (error) {
            *error = QStringLiteral(
                        "Not enough usable road segments were found in the "
                        "selected area. Keep north up or choose a rotation "
                        "manually.");
        }
        return false;
    }
    const int histogramSize = 180;
    QVector<double> histogram(histogramSize, 0.0);
    for (const AngleSample &sample : samples) {
        int bin = int((sample.angle + 45.0) * 2.0);
        bin = qBound(0, bin, histogramSize - 1);
        histogram[bin] += sample.weight;
    }
    int peakBin = 0;
    double peakWeight = -1.0;
    for (int bin = 0; bin < histogramSize; ++bin) {
        double smoothed = 0.0;
        for (int offset = -6; offset <= 6; ++offset) {
            const int wrapped = (bin + offset + histogramSize)
                    % histogramSize;
            smoothed += histogram[wrapped];
        }
        if (smoothed > peakWeight) {
            peakWeight = smoothed;
            peakBin = bin;
        }
    }
    const double peakAngle = -45.0 + (peakBin + 0.5) * 0.5;
    double sineSum = 0.0;
    double cosineSum = 0.0;
    double alignedWeight = 0.0;
    for (const AngleSample &sample : samples) {
        if (gridAngleDistance(sample.angle, peakAngle) > 12.0)
            continue;
        const double phase = degreesToRadians(sample.angle * 4.0);
        sineSum += std::sin(phase) * sample.weight;
        cosineSum += std::cos(phase) * sample.weight;
        alignedWeight += sample.weight;
    }
    if (alignedWeight <= 0.0) {
        if (error)
            *error = QStringLiteral("No dominant orthogonal road grid was found.");
        return false;
    }
    const double coherence = std::hypot(sineSum, cosineSum) / alignedWeight;
    const double detectedConfidence = qBound(
                0.0, alignedWeight / totalWeight * coherence, 1.0);
    if (detectedConfidence < 0.20) {
        if (error) {
            *error = QStringLiteral(
                        "The roads do not form one clear orthogonal grid. "
                        "Keep north up or choose a rotation manually.");
        }
        return false;
    }
    const double meanAngle = normalizedGridAngle(
                radiansToDegrees(std::atan2(sineSum, cosineSum)) / 4.0);
    if (rotationDegreesClockwise)
        *rotationDegreesClockwise = normalizedGridAngle(-meanAngle);
    if (confidence)
        *confidence = detectedConfidence;
    return true;
}
bool OsmTerrainImporter::generateFromOverpassJsonChunks(
        const QList<QByteArray> &chunks,
        const OsmTerrainImportOptions &options,
        OsmTerrainImportResult *result,
        QString *error)
{
    if (!result) {
        if (error)
            *error = QStringLiteral("No result object was supplied.");
        return false;
    }
    *result = OsmTerrainImportResult();
    if (options.widthPixels <= 0 || options.heightPixels <= 0
            || options.metersPerPixel <= 0.0) {
        if (error)
            *error = QStringLiteral("The requested image geometry is invalid.");
        return false;
    }
    const qint64 pixelCount = qint64(options.widthPixels)
            * qint64(options.heightPixels);
    if (pixelCount > 268435456LL) {
        if (error) {
            *error = QStringLiteral(
                        "The requested image exceeds the 268-million-pixel "
                        "safety limit.");
        }
        return false;
    }
    if (chunks.isEmpty()) {
        if (error)
            *error = QStringLiteral("No Overpass response was supplied.");
        return false;
    }
    QVector<QJsonObject> elements;
    QSet<QString> sourceKeys;
    for (int chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
                    chunks.at(chunkIndex), &parseError);
        if (parseError.error != QJsonParseError::NoError
                || !document.isObject()) {
            if (error) {
                *error = QStringLiteral(
                            "Overpass response part %1 of %2 is not valid "
                            "JSON: %3")
                        .arg(chunkIndex + 1).arg(chunks.size())
                        .arg(parseError.errorString());
            }
            return false;
        }
        const QJsonArray chunkElements = document.object().value(
                    QStringLiteral("elements")).toArray();
        for (const QJsonValue &value : chunkElements) {
            const QJsonObject element = value.toObject();
            const QString type = element.value(
                        QStringLiteral("type")).toString();
            const QVariant id = element.value(
                        QStringLiteral("id")).toVariant();
            const QString sourceKey = type.isEmpty() || !id.isValid()
                    ? QString()
                    : type + QLatin1Char('/') + id.toString();
            if (!sourceKey.isEmpty()) {
                if (sourceKeys.contains(sourceKey))
                    continue;
                sourceKeys.insert(sourceKey);
            }
            elements += element;
        }
    }
    result->sourceElementCount = elements.size();
    QSet<qint64> buildingRelationMemberWays;
    for (const QJsonObject &element : elements) {
        if (element.value(QStringLiteral("type")).toString()
                != QLatin1String("relation")) {
            continue;
        }
        const QHash<QString, QString> tags = readTags(element);
        const QString building = tagValue(tags, QStringLiteral("building"));
        if (building.isEmpty() || building == QLatin1String("no"))
            continue;
        for (const QJsonValue &memberValue : element.value(
             QStringLiteral("members")).toArray()) {
            const QJsonObject member = memberValue.toObject();
            if (member.value(QStringLiteral("type")).toString()
                    == QLatin1String("way")) {
                buildingRelationMemberWays.insert(member.value(
                            QStringLiteral("ref")).toVariant().toLongLong());
            }
        }
    }
    QVector<OsmRenderFeature> features;
    int openRelationSegments = 0;
    int suppressedRelationMembers = 0;
    for (const QJsonObject &element : elements) {
        OsmRenderFeature feature;
        feature.tags = readTags(element);
        const QString highway = tagValue(
                    feature.tags, QStringLiteral("highway"));
        const QString railway = tagValue(
                    feature.tags, QStringLiteral("railway"));
        const QString waterway = tagValue(
                    feature.tags, QStringLiteral("waterway"));
        const QString building = tagValue(
                    feature.tags, QStringLiteral("building"));
        const QString natural = tagValue(
                    feature.tags, QStringLiteral("natural"));
        const QString barrier = tagValue(
                    feature.tags, QStringLiteral("barrier"));
        feature.road = options.includeRoads && !highway.isEmpty();
        feature.railway = options.includeRoads && highway.isEmpty()
                && isSupportedRailway(railway);
        feature.waterLine = options.includeWater && !waterway.isEmpty()
                && waterway != QLatin1String("riverbank");
        feature.vegetationLine = options.includeLand
                && (natural == QLatin1String("tree_row")
                    || barrier == QLatin1String("hedge"));
        feature.groundKind = options.includeLand || options.includeWater
                ? classifyGround(feature.tags) : GroundKind::None;
        feature.vegetationKind = options.includeLand
                ? classifyVegetation(feature.tags) : VegetationKind::None;
        if (feature.road) {
            feature.roadPriority = roadPriority(highway);
            feature.lineWidthMeters = roadWidth(feature.tags, highway);
            feature.lineColor = roadColor(
                        highway,
                        tagValue(feature.tags, QStringLiteral("surface")));
        } else if (feature.railway) {
            feature.lineWidthMeters = railwayWidth(railway);
            feature.lineColor = QColor(140, 70, 15);
        } else if (feature.waterLine) {
            feature.lineWidthMeters =
                    waterway == QLatin1String("river") ? 5.0
                    : waterway == QLatin1String("canal") ? 4.0
                    : waterway == QLatin1String("stream") ? 2.0 : 1.0;
            feature.lineColor = QColor(0, 138, 255);
        } else if (feature.vegetationLine) {
            feature.lineWidthMeters = 2.0;
            feature.lineColor = vegetationColor(
                        VegetationKind::SparseTrees);
        }
        const bool isBuilding = options.includeBuildings
                && !building.isEmpty() && building != QLatin1String("no");
        if (isBuilding
                && element.value(QStringLiteral("type")).toString()
                == QLatin1String("way")
                && buildingRelationMemberWays.contains(element.value(
                    QStringLiteral("id")).toVariant().toLongLong())) {
            ++suppressedRelationMembers;
            continue;
        }
        const QString foragingZone = options.includeLand
                ? classifyForagingZone(feature.tags) : QString();
        const bool polygon = !feature.road && !feature.railway
                && !feature.waterLine && !feature.vegetationLine
                && (feature.groundKind != GroundKind::None
                    || feature.vegetationKind != VegetationKind::None
                    || !foragingZone.isEmpty() || isBuilding);
        feature.geometries = isBuilding
                ? readBuildingOuterGeometry(
                    element, options, &openRelationSegments)
                : readElementGeometry(
                    element, options, polygon, &openRelationSegments);
        if (polygon) {
            QVector<QPolygonF> closed;
            for (const QPolygonF &geometry : feature.geometries) {
                if (isClosed(geometry))
                    closed += geometry;
            }
            feature.geometries = closed;
        }
        if (!feature.geometries.isEmpty()) {
            features += feature;
            QVector<QVector<QPolygonF>> projectGeometryGroups;
            if (isBuilding) {
                for (const QPolygonF &geometry : feature.geometries) {
                    QVector<QPolygonF> singleGeometry;
                    singleGeometry += geometry;
                    projectGeometryGroups.append(singleGeometry);
                }
            } else {
                projectGeometryGroups.append(feature.geometries);
            }
            for (const QVector<QPolygonF> &projectGeometries
                 : projectGeometryGroups) {
                OsmProjectFeature projectFeature;
                projectFeature.osmId = element.value(
                            QStringLiteral("id")).toVariant().toLongLong();
                projectFeature.osmType = element.value(
                            QStringLiteral("type")).toString();
                projectFeature.tags = feature.tags;
                projectFeature.geometries = projectGeometries;
                projectFeature.road = feature.road;
                projectFeature.railway = feature.railway;
                projectFeature.building = isBuilding;
                projectFeature.waterArea = polygon
                        && feature.groundKind == GroundKind::Water;
                projectFeature.waterway = feature.waterLine;
                projectFeature.forestArea = polygon
                        && feature.vegetationKind == VegetationKind::DenseTrees;
                projectFeature.vegetationLine = feature.vegetationLine;
                projectFeature.foragingZone = foragingZone;
                projectFeature.lineWidthSquares = feature.road
                        || feature.railway || feature.waterLine
                        || feature.vegetationLine
                        ? qMax(1.0, feature.lineWidthMeters
                               / options.metersPerPixel
                               * ((feature.road || feature.railway)
                                  ? options.roadWidthPercent / 100.0 : 1.0))
                        : 0.0;
                projectFeature.buildingLevels = isBuilding
                        ? buildingLevels(feature.tags) : 1;
                result->projectFeatures += projectFeature;
                if (projectFeature.road
                        && !projectFeature.tags.value(
                            QStringLiteral("name")).trimmed().isEmpty()) {
                    ++result->namedStreetCount;
                }
            }
        }
    }
    const int nestedBuildingsRemoved = removeNestedBuildingFootprints(result);
    result->buildingCount = 0;
    result->farmAreaCount = 0;
    for (const OsmProjectFeature &feature : result->projectFeatures) {
        if (feature.building)
            ++result->buildingCount;
        if (feature.foragingZone == QLatin1String("Farm")
                || feature.foragingZone == QLatin1String("FarmLand")) {
            ++result->farmAreaCount;
        }
    }
    if (suppressedRelationMembers > 0 || nestedBuildingsRemoved > 0) {
        result->warnings += QStringLiteral(
                    "Filtered %1 building relation member(s) and %2 nested "
                    "or duplicate footprint(s).")
                .arg(suppressedRelationMembers)
                .arg(nestedBuildingsRemoved);
    }
    if (openRelationSegments > 0) {
        result->warnings += QStringLiteral(
                    "%1 open multipolygon segment(s) could not form a ring.")
                .arg(openRelationSegments);
    }
    result->groundImage = QImage(
                options.widthPixels, options.heightPixels,
                QImage::Format_ARGB32);
    result->vegetationImage = QImage(
                options.widthPixels, options.heightPixels,
                QImage::Format_ARGB32);
    if (result->groundImage.isNull() || result->vegetationImage.isNull()) {
        if (error)
            *error = QStringLiteral("The terrain images could not be allocated.");
        *result = OsmTerrainImportResult();
        return false;
    }
    result->groundImage.fill(QColor(145, 135, 60).rgb());
    result->vegetationImage.fill(Qt::black);
    QPainter groundPainter(&result->groundImage);
    QPainter vegetationPainter(&result->vegetationImage);
    groundPainter.setRenderHint(QPainter::Antialiasing, false);
    vegetationPainter.setRenderHint(QPainter::Antialiasing, false);
    const GroundKind groundOrder[] = {
        GroundKind::LightGrass,
        GroundKind::MediumGrass,
        GroundKind::DarkGrass,
        GroundKind::Dirt,
        GroundKind::Water,
        GroundKind::Sand
    };
    for (GroundKind kind : groundOrder) {
        for (const OsmRenderFeature &feature : features) {
            if (feature.groundKind != kind)
                continue;
            fillFeature(groundPainter, feature, groundColor(kind));
            ++result->polygonCount;
        }
    }
    const VegetationKind vegetationOrder[] = {
        VegetationKind::Grass,
        VegetationKind::SparseTrees,
        VegetationKind::ModerateTrees,
        VegetationKind::DenseTrees
    };
    for (VegetationKind kind : vegetationOrder) {
        for (const OsmRenderFeature &feature : features) {
            if (feature.vegetationKind == kind)
                fillFeature(vegetationPainter, feature,
                            vegetationColor(kind));
        }
    }
    for (const OsmRenderFeature &feature : features) {
        if (!feature.vegetationLine)
            continue;
        drawLines(vegetationPainter, feature,
                  options.metersPerPixel, 100);
    }
    for (const OsmRenderFeature &feature : features) {
        if (feature.groundKind == GroundKind::Water
                || feature.groundKind == GroundKind::Sand) {
            fillFeature(vegetationPainter, feature, Qt::black);
        }
    }
    for (const OsmRenderFeature &feature : features) {
        if (!feature.waterLine)
            continue;
        drawLines(groundPainter, feature,
                  options.metersPerPixel, 100);
        drawLines(vegetationPainter, feature,
                  options.metersPerPixel, 100, Qt::black);
        ++result->waterLineCount;
    }
    for (const OsmRenderFeature &feature : features) {
        if (!feature.railway)
            continue;
        drawLines(groundPainter, feature,
                  options.metersPerPixel,
                  options.roadWidthPercent);
        drawLines(vegetationPainter, feature,
                  options.metersPerPixel,
                  options.roadWidthPercent, Qt::black);
        ++result->railwayCount;
    }
    QVector<const OsmRenderFeature *> roads;
    for (const OsmRenderFeature &feature : features) {
        if (feature.road)
            roads += &feature;
    }
    std::sort(roads.begin(), roads.end(),
              [](const OsmRenderFeature *left,
                 const OsmRenderFeature *right) {
        return left->roadPriority < right->roadPriority;
    });
    for (const OsmRenderFeature *road : roads) {
        drawLines(groundPainter, *road,
                  options.metersPerPixel,
                  options.roadWidthPercent);
        drawLines(vegetationPainter, *road,
                  options.metersPerPixel,
                  options.roadWidthPercent, Qt::black);
        ++result->roadCount;
    }
    groundPainter.end();
    vegetationPainter.end();
    result->sourceMetadata = createMetadata(options, *result);
    return true;
}
