#ifndef OSMPROJECTDATA_H
#define OSMPROJECTDATA_H
#include "osmterrainimporter.h"
#include "streetnamesdock.h"
#include <QPoint>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <functional>
class WorldDocument;
using OsmProjectProgress = std::function<void(
        int current, int total, const QString &message)>;
struct OsmProjectDataOptions
{
    QPoint cellOrigin;
    QString projectFilePath;
    bool generateStreets = true;
    bool generateInGameMapFeatures = true;
    bool generateProxyBuildings = true;
    bool generateRoadMarkings = true;
    bool generateNavZones = true;
    bool generateForagingZones = true;
    bool safeCityMode = false;
};
struct OsmProjectDataSummary
{
    int streets = 0;
    int inGameMapFeatures = 0;
    int proxyBuildings = 0;
    int roadMarkings = 0;
    int navZones = 0;
    int navPolylineZones = 0;
    int navPolygonZones = 0;
    int navRectangleZones = 0;
    int foragingZones = 0;
    int foragingPolygonZones = 0;
    int foragingRectangleZones = 0;
    QHash<QString, int> zoneTypeCounts;
    int skippedProxyBuildings = 0;
    int skippedInGameMapFeatures = 0;
    bool safeCityMode = false;
    QString manifestPath;
    QStringList warnings;
};
class OsmProjectData
{
public:
    static bool apply(
            WorldDocument *document,
            const OsmTerrainImportResult &generated,
            const OsmProjectDataOptions &options,
            const QVector<StreetNameRecord> &currentStreets,
            QVector<StreetNameRecord> *mergedStreets,
            OsmProjectDataSummary *summary,
            QString *error,
            const OsmProjectProgress &progress = OsmProjectProgress());
};
#endif
