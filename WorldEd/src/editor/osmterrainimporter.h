#ifndef OSMTERRAINIMPORTER_H
#define OSMTERRAINIMPORTER_H
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
struct OsmTerrainImportOptions
{
    double centerLatitude = 0.0;
    double centerLongitude = 0.0;
    int widthPixels = 0;
    int heightPixels = 0;
    double metersPerPixel = 1.0;
    double rotationDegreesClockwise = 0.0;
    int marginPercent = 20;
    int roadWidthPercent = 100;
    bool includeLand = true;
    bool includeWater = true;
    bool includeRoads = true;
    bool includeBuildings = false;
    QString endpoint;
};
struct OsmProjectFeature
{
    qint64 osmId = 0;
    QString osmType;
    QHash<QString, QString> tags;
    QVector<QPolygonF> geometries;
    bool road = false;
    bool railway = false;
    bool building = false;
    bool waterArea = false;
    bool waterway = false;
    bool forestArea = false;
    bool vegetationLine = false;
    QString foragingZone;
    double lineWidthSquares = 0.0;
    int buildingLevels = 1;
    QString sourceKey() const
    {
        return osmType + QLatin1Char('/') + QString::number(osmId);
    }
};
struct OsmTerrainImportResult
{
    QImage groundImage;
    QImage vegetationImage;
    QByteArray sourceMetadata;
    int sourceElementCount = 0;
    int polygonCount = 0;
    int roadCount = 0;
    int railwayCount = 0;
    int farmAreaCount = 0;
    int waterLineCount = 0;
    int buildingCount = 0;
    int namedStreetCount = 0;
    QVector<OsmProjectFeature> projectFeatures;
    QStringList warnings;
};
class OsmTerrainImporter
{
public:
    static bool parseLocationText(
            const QString &text,
            double *latitude,
            double *longitude,
            QString *error = nullptr);
    static QRectF geographicBounds(const OsmTerrainImportOptions &options);
    static QSize requiredCells(
            double widthMeters,
            double heightMeters,
            int cellSize,
            double metersPerPixel);
    static QStringList buildOverpassQueries(
            const OsmTerrainImportOptions &options);
    static QString buildRoadOrientationQuery(
            const OsmTerrainImportOptions &options);
    static bool suggestRoadGridRotation(
            const QByteArray &json,
            const OsmTerrainImportOptions &options,
            double *rotationDegreesClockwise,
            int *segmentCount = nullptr,
            double *confidence = nullptr,
            QString *error = nullptr);
    static bool generateFromOverpassJsonChunks(
            const QList<QByteArray> &chunks,
            const OsmTerrainImportOptions &options,
            OsmTerrainImportResult *result,
            QString *error);
};
#endif
