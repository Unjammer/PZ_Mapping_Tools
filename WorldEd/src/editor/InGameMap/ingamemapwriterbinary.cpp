/*
 * Copyright 2018, Tim Baker <treectrl@users.sf.net>
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

#include "ingamemapwriterbinary.h"
#include "ingamemapwriter.h"

#include "world.h"
#include "worldcell.h"
#include "clipper.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QTemporaryFile>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QtMath>

#include <cmath>

#define VERSION1 1
#define VERSION2 2 // Added cell size (for 256x256 cells)
#define VERSION_LATEST VERSION2

namespace
{

class WorldConverter
{
public:
    World *convertWorld(World *worldOld, int cellSizeOld, int cellSizeNew)
    {
        mCellSizeOld = cellSizeOld;
        mCellSizeNew = cellSizeNew;

        const GenerateLotsSettings &generateLotsSettings = worldOld->getGenerateLotsSettings();
        mWorldBoundsOld = worldOld->bounds().translated(generateLotsSettings.worldOrigin);

        int minCell256X = std::floor(mWorldBoundsOld.left() * mCellSizeOld / float(mCellSizeNew));
        int minCell256Y = std::floor(mWorldBoundsOld.top() * mCellSizeOld / float(mCellSizeNew));
        int maxCell256X = std::ceil(((mWorldBoundsOld.right() + 1) * mCellSizeOld - 1) / float(mCellSizeNew));
        int maxCell256Y = std::ceil(((mWorldBoundsOld.bottom() + 1) * mCellSizeOld - 1) / float(mCellSizeNew));
        mWorldBoundsNew = QRect(minCell256X, minCell256Y, maxCell256X - minCell256X, maxCell256Y - minCell256Y);

        World *world256 = new World(mWorldBoundsNew.width(), mWorldBoundsNew.height());

        GenerateLotsSettings generateLotsSettingsNew;
        generateLotsSettingsNew.worldOrigin = mWorldBoundsNew.topLeft();
        world256->setGenerateLotsSettings(generateLotsSettingsNew);

        for (int cell300Y = 0; cell300Y < mWorldBoundsOld.height(); cell300Y++) {
            for (int cell300X = 0; cell300X < mWorldBoundsOld.width(); cell300X++) {
                WorldCell *cell300 = worldOld->cellAt(cell300X, cell300Y);
                for (InGameMapFeature *feature300 : cell300->inGameMap().features()) {
                    addFeature(world256, feature300);
                }
            }
        }
        return world256;
    }

    void addFeature(World *world256, InGameMapFeature *oldFeature)
    {
        const int minCellX = int(std::floor(
                getMinSquareX(oldFeature) / double(mCellSizeNew)));
        const int minCellY = int(std::floor(
                getMinSquareY(oldFeature) / double(mCellSizeNew)));
        const int maxCellX = int(std::floor(
                getMaxSquareX(oldFeature) / double(mCellSizeNew)));
        const int maxCellY = int(std::floor(
                getMaxSquareY(oldFeature) / double(mCellSizeNew)));
        for (int y = minCellY; y <= maxCellY; y++) {
            for (int x = minCellX; x <= maxCellX; x++) {
                InGameMapCell *newCell = &world256->cellAt(x - mWorldBoundsNew.x(), y - mWorldBoundsNew.y())->inGameMap();
                if (oldFeature->mGeometry.isPolygon()) {
                    addClippedPolygonFeatures(newCell, oldFeature, x, y);
                } else {
                    InGameMapFeature *newFeature = new InGameMapFeature(newCell);
                    convertFeature(newFeature, oldFeature);
                    newCell->mFeatures += newFeature;
                }
            }
        }
    }

    void addClippedPolygonFeatures(InGameMapCell *newCell,
                                   InGameMapFeature *oldFeature,
                                   int newCellX, int newCellY)
    {
        ClipperLib::Paths subject;
        const double oldOriginX =
                (mWorldBoundsOld.x() + oldFeature->cell()->x()) * mCellSizeOld;
        const double oldOriginY =
                (mWorldBoundsOld.y() + oldFeature->cell()->y()) * mCellSizeOld;
        for (const InGameMapCoordinates &coordinates :
             std::as_const(oldFeature->mGeometry.mCoordinates)) {
            ClipperLib::Path path;
            path.reserve(size_t(coordinates.size()));
            for (const InGameMapPoint &point : coordinates) {
                path << ClipperLib::IntPoint(
                            qRound64(oldOriginX + point.x),
                            qRound64(oldOriginY + point.y));
            }
            if (path.size() >= 3)
                subject.push_back(path);
        }
        if (subject.empty())
            return;

        const ClipperLib::cInt left = ClipperLib::cInt(newCellX) * mCellSizeNew;
        const ClipperLib::cInt top = ClipperLib::cInt(newCellY) * mCellSizeNew;
        const ClipperLib::cInt right = left + mCellSizeNew;
        const ClipperLib::cInt bottom = top + mCellSizeNew;
        ClipperLib::Path bounds;
        bounds << ClipperLib::IntPoint(left, top)
               << ClipperLib::IntPoint(right, top)
               << ClipperLib::IntPoint(right, bottom)
               << ClipperLib::IntPoint(left, bottom);

        ClipperLib::Clipper clipper;
        clipper.AddPaths(subject, ClipperLib::ptSubject, true);
        clipper.AddPath(bounds, ClipperLib::ptClip, true);
        ClipperLib::PolyTree tree;
        if (!clipper.Execute(ClipperLib::ctIntersection, tree,
                             ClipperLib::pftEvenOdd,
                             ClipperLib::pftEvenOdd))
            return;

        struct Polygon {
            ClipperLib::Path outer;
            ClipperLib::Paths holes;
        };
        QHash<ClipperLib::PolyNode *, Polygon *> polygonForNode;
        QList<Polygon *> polygons;
        for (ClipperLib::PolyNode *node = tree.GetFirst();
             node; node = node->GetNext()) {
            if (node->IsHole()) {
                if (Polygon *outer = polygonForNode.value(node->Parent, nullptr))
                    outer->holes.push_back(node->Contour);
            } else {
                Polygon *polygon = new Polygon;
                polygon->outer = node->Contour;
                polygonForNode.insert(node, polygon);
                polygons += polygon;
            }
        }

        auto relativeCoordinates = [left, top](const ClipperLib::Path &path) {
            InGameMapCoordinates result;
            for (const ClipperLib::IntPoint &point : path)
                result += InGameMapPoint(point.X - left, point.Y - top);
            return result;
        };

        for (Polygon *polygon : std::as_const(polygons)) {
            if (polygon->outer.size() < 3)
                continue;
            InGameMapFeature *newFeature = new InGameMapFeature(newCell);
            newFeature->mGeometry.mType = oldFeature->mGeometry.mType;
            newFeature->mGeometry.mCoordinates +=
                    relativeCoordinates(polygon->outer);
            for (const ClipperLib::Path &hole : polygon->holes) {
                if (hole.size() >= 3)
                    newFeature->mGeometry.mCoordinates +=
                            relativeCoordinates(hole);
            }
            newFeature->mProperties = oldFeature->mProperties;
            newCell->mFeatures += newFeature;
        }
        qDeleteAll(polygons);
    }

    double getMinSquareX(InGameMapFeature *feature)
    {
        double min = std::numeric_limits<double>::max();
        InGameMapGeometry &geometry = feature->mGeometry;
        for (InGameMapCoordinates &coords : geometry.mCoordinates) {
            for (InGameMapPoint &point : coords) {
                min = std::min(min, point.x);
            }
        }
        return (mWorldBoundsOld.x() + feature->cell()->x()) * mCellSizeOld + min;
    }

    double getMinSquareY(InGameMapFeature *feature)
    {
        double min = std::numeric_limits<double>::max();
        InGameMapGeometry &geometry = feature->mGeometry;
        for (InGameMapCoordinates &coords : geometry.mCoordinates) {
            for (InGameMapPoint &point : coords) {
                min = std::min(min, point.y);
            }
        }
        return (mWorldBoundsOld.y() + feature->cell()->y()) * mCellSizeOld + min;
    }

    double getMaxSquareX(InGameMapFeature *feature)
    {
        double max = std::numeric_limits<double>::lowest();
        InGameMapGeometry &geometry = feature->mGeometry;
        for (InGameMapCoordinates &coords : geometry.mCoordinates) {
            for (InGameMapPoint &point : coords) {
                max = std::max(max, point.x);
            }
        }
        return (mWorldBoundsOld.x() + feature->cell()->x()) * mCellSizeOld + max;
    }

    double getMaxSquareY(InGameMapFeature *feature)
    {
        double max = std::numeric_limits<double>::lowest();
        InGameMapGeometry &geometry = feature->mGeometry;
        for (InGameMapCoordinates &coords : geometry.mCoordinates) {
            for (InGameMapPoint &point : coords) {
                max = std::max(max, point.y);
            }
        }
        return (mWorldBoundsOld.y() + feature->cell()->y()) * mCellSizeOld + max;
    }

    void convertFeature(InGameMapFeature *newFeature, InGameMapFeature *oldFeature)
    {
        InGameMapGeometry &newGeometry = newFeature->mGeometry;
        InGameMapGeometry &oldGeometry = oldFeature->mGeometry;
        for (InGameMapCoordinates &oldCoords : oldGeometry.mCoordinates) {
            InGameMapCoordinates newCoordinates;
            newGeometry.mType = oldGeometry.mType;
            for (InGameMapPoint &oldPoint : oldCoords) {
                double oldX = (mWorldBoundsOld.x() + oldFeature->cell()->x()) * mCellSizeOld + oldPoint.x;
                double oldY = (mWorldBoundsOld.y() + oldFeature->cell()->y()) * mCellSizeOld + oldPoint.y;
                double newX = oldX - (mWorldBoundsNew.x() + newFeature->cell()->x()) * mCellSizeNew;
                double newY = oldY - (mWorldBoundsNew.y() + newFeature->cell()->y()) * mCellSizeNew;
                newCoordinates += InGameMapPoint(newX, newY);
            }
            newGeometry.mCoordinates += newCoordinates;
        }
        newFeature->mProperties = oldFeature->mProperties;
    }

    int mCellSizeOld;
    int mCellSizeNew;
    QRect mWorldBoundsOld;
    QRect mWorldBoundsNew;
};

} // namespace anonymous

class InGameMapWriterBinaryPrivate
{
    Q_DECLARE_TR_FUNCTIONS(InGameMapWriterBinary)

public:
    struct ExportFeature {
        InGameMapFeature *feature;
        InGameMapGeometry geometry;
    };

    InGameMapWriterBinaryPrivate()
        : mWorld(nullptr)
        , mFeatureScope(InGameMapFeatureScope::AllFeatures)
    {
    }

    bool openFile(QFile *file)
    {
        if (!file->open(QIODevice::WriteOnly)) {
            mError = tr("Could not open file for writing.");
            return false;
        }

        return true;
    }

    void writeWorld(World *world, QIODevice *device, const QString &absDirPath)
    {
        mError.clear();
        mMapDir = QDir(absDirPath);
        mWorld = world;
        mStringTable.clear();
        mWrittenFeatures = 0;
        mRepairedFeatures = 0;
        mRejectedFeatures = 0;

        QDataStream writer(device);
        writer.setByteOrder(QDataStream::LittleEndian);

        writeWorld(writer, world);

        qInfo() << "InGameMap binary geometry validation:"
                << "output" << mOutputPath
                << "written" << mWrittenFeatures
                << "repaired" << mRepairedFeatures
                << "rejected" << mRejectedFeatures;
    }

    void writeWorld(QDataStream &w, World *world)
    {
        w << quint8('I') << quint8('G') << quint8('M') << quint8('B');

        w << qint32(VERSION_LATEST);

        w << qint32(256); // cell size (8x8 chunks)
        w << qint32(world->width());
        w << qint32(world->height());

        writeStringTable(w, world);

        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell *cell = world->cellAt(x, y);
                writeCell(w, cell);
                if (!mError.isEmpty())
                    return;
            }
        }
    }

    void writeStringTable(QDataStream &w, World *world)
    {
        QStringList strings;

        auto addString = [&](const QString& str)
        {
            if (mStringTable.contains(str))
                return;
            mStringTable.insert(str, strings.size());
            strings += str;
        };

        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell *cell = world->cellAt(x, y);
                for (auto* feature : std::as_const(cell->inGameMap().mFeatures)) {
                    if (!inGameMapFeatureMatchesScope(feature, mFeatureScope))
                        continue;
                    addString(feature->mGeometry.mType);
                    for (auto& property : feature->mProperties) {
                        addString(property.mKey);
                        addString(property.mValue);
                    }
                }
            }
        }

        w << qint32(strings.size());
        for (const QString &str : std::as_const(strings)) {
            SaveString(w, str);
        }
    }

    static bool hasProperty(InGameMapFeature *feature,
                            const QString &propertyName)
    {
        for (const InGameMapProperty &property : feature->mProperties) {
            if (property.mKey == propertyName)
                return true;
        }
        return false;
    }

    static QPoint worldCellCoordinates(WorldCell *cell)
    {
        const QPoint origin =
                cell->world()->getGenerateLotsSettings().worldOrigin;
        return origin + QPoint(cell->x(), cell->y());
    }

    static double distanceToSegmentSquared(const InGameMapPoint &point,
                                           const InGameMapPoint &start,
                                           const InGameMapPoint &end)
    {
        const double dx = end.x - start.x;
        const double dy = end.y - start.y;
        if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) {
            const double px = point.x - start.x;
            const double py = point.y - start.y;
            return px * px + py * py;
        }
        const double lengthSquared = dx * dx + dy * dy;
        const double t = qBound(0.0,
                ((point.x - start.x) * dx +
                 (point.y - start.y) * dy) / lengthSquared,
                1.0);
        const double px = point.x - (start.x + t * dx);
        const double py = point.y - (start.y + t * dy);
        return px * px + py * py;
    }

    static void simplifySection(const QVector<InGameMapPoint> &points,
                                int first, int last,
                                double toleranceSquared,
                                QVector<bool> &keep)
    {
        if (last <= first + 1)
            return;
        double farthestDistance = -1.0;
        int farthestIndex = -1;
        for (int index = first + 1; index < last; ++index) {
            const double distance = distanceToSegmentSquared(
                        points.at(index), points.at(first), points.at(last));
            if (distance > farthestDistance) {
                farthestDistance = distance;
                farthestIndex = index;
            }
        }
        if (farthestIndex < 0 || farthestDistance <= toleranceSquared)
            return;
        keep[farthestIndex] = true;
        simplifySection(points, first, farthestIndex,
                        toleranceSquared, keep);
        simplifySection(points, farthestIndex, last,
                        toleranceSquared, keep);
    }

    static InGameMapCoordinates simplifyClosedCoordinates(
            const InGameMapCoordinates &source, double tolerance)
    {
        if (source.size() <= 3 || tolerance <= 0.0)
            return source;

        int opposite = 1;
        double farthestDistance = -1.0;
        for (int index = 1; index < source.size(); ++index) {
            const double dx = source.at(index).x - source.first().x;
            const double dy = source.at(index).y - source.first().y;
            const double distance = dx * dx + dy * dy;
            if (distance > farthestDistance) {
                farthestDistance = distance;
                opposite = index;
            }
        }
        if (opposite <= 0 || opposite >= source.size())
            return source;

        QVector<InGameMapPoint> unwrapped;
        unwrapped.reserve(source.size() + 1);
        for (int index = 0; index < source.size(); ++index)
            unwrapped += source.at(index);
        unwrapped += source.first();

        QVector<bool> keep(unwrapped.size(), false);
        keep[0] = true;
        keep[opposite] = true;
        keep[unwrapped.size() - 1] = true;
        const double toleranceSquared = tolerance * tolerance;
        simplifySection(unwrapped, 0, opposite,
                        toleranceSquared, keep);
        simplifySection(unwrapped, opposite, unwrapped.size() - 1,
                        toleranceSquared, keep);

        InGameMapCoordinates result;
        for (int index = 0; index < unwrapped.size() - 1; ++index) {
            if (keep.at(index))
                result += unwrapped.at(index);
        }
        return result.size() >= 3 ? result : source;
    }

    static int rendererIndexEstimate(const QList<ExportFeature> &features)
    {
        qint64 estimate = 0;
        for (const ExportFeature &item : features) {
            if (!item.geometry.isPolygon())
                continue;
            int pointCount = 0;
            for (const InGameMapCoordinates &coordinates :
                 item.geometry.mCoordinates)
                pointCount += coordinates.size();
            const int holeCount = qMax(0,
                    item.geometry.mCoordinates.size() - 1);
            const qint64 baseTriangleIndices = qMax<qint64>(
                        3, qint64(3) *
                        (pointCount + holeCount * 2 - 2));
            // WorldMapGeometry creates the normal triangulation plus six
            // zoom-dependent variants for every polygon tagged "highway".
            estimate += baseTriangleIndices *
                    (hasProperty(item.feature,
                                 QStringLiteral("highway")) ? 7 : 1);
        }
        return int(qMin<qint64>(estimate,
                    std::numeric_limits<int>::max()));
    }

    bool fitRendererBudget(QList<ExportFeature> &features,
                           WorldCell *cell)
    {
        static const int safeIndexBudget = 28000;
        static const int safePointBufferShorts = 30000;
        auto pointCount = [&features]() {
            int count = 0;
            for (const ExportFeature &item : std::as_const(features)) {
                for (const InGameMapCoordinates &coordinates :
                     item.geometry.mCoordinates)
                    count += coordinates.size();
            }
            return count;
        };

        const int originalPoints = pointCount();
        const int originalEstimate = rendererIndexEstimate(features);
        if (originalPoints * 2 <= safePointBufferShorts &&
                originalEstimate <= safeIndexBudget)
            return true;

        int changedFeatures = 0;
        double usedTolerance = 0.0;
        const QVector<double> tolerances =
                {1.0, 2.0, 3.0, 4.0, 6.0, 8.0,
                 12.0, 16.0, 24.0, 32.0};
        for (double tolerance : tolerances) {
            changedFeatures = 0;
            for (ExportFeature &item : features) {
                if (!item.geometry.isPolygon() ||
                        !hasProperty(item.feature,
                                     QStringLiteral("highway")))
                    continue;

                InGameMapGeometry simplified = item.geometry;
                for (InGameMapCoordinates &coordinates :
                     simplified.mCoordinates) {
                    coordinates = simplifyClosedCoordinates(
                                coordinates, tolerance);
                }
                InGameMapGeometry sanitized;
                QStringList diagnostics;
                if (!sanitizeInGameMapGeometryForExport(
                            simplified, sanitized, diagnostics))
                    continue;
                if (rendererIndexEstimate(
                            {ExportFeature{item.feature, sanitized}}) <
                        rendererIndexEstimate({item})) {
                    item.geometry = sanitized;
                    ++changedFeatures;
                }
            }
            usedTolerance = tolerance;
            if (pointCount() * 2 <= safePointBufferShorts &&
                    rendererIndexEstimate(features) <= safeIndexBudget)
                break;
        }

        const int finalPoints = pointCount();
        const int finalEstimate = rendererIndexEstimate(features);
        const QPoint cellCoordinates = worldCellCoordinates(cell);
        if (changedFeatures > 0) {
            mRepairedFeatures += changedFeatures;
            qWarning().noquote()
                    << QStringLiteral("InGameMap binary simplified renderer-heavy highways: output=\"%1\" cell=%2,%3 tolerance=%4 points=%5->%6 estimated-indices=%7->%8 changed-features=%9")
                       .arg(mOutputPath)
                       .arg(cellCoordinates.x()).arg(cellCoordinates.y())
                       .arg(usedTolerance)
                       .arg(originalPoints).arg(finalPoints)
                       .arg(originalEstimate).arg(finalEstimate)
                       .arg(changedFeatures);
        }

        if (finalPoints * 2 > safePointBufferShorts ||
                finalEstimate > safeIndexBudget) {
            mError = tr("InGameMap cell %1,%2 is too complex for the "
                        "game's 16-bit world-map renderer after safe "
                        "simplification (%3 points, estimated %4 indices).")
                    .arg(cellCoordinates.x()).arg(cellCoordinates.y())
                    .arg(finalPoints).arg(finalEstimate);
            qCritical().noquote() << mError;
            return false;
        }
        return true;
    }

    void writeCell(QDataStream &w, WorldCell *cell)
    {
        QList<ExportFeature> exportFeatures;
        const QPoint cellCoordinates = worldCellCoordinates(cell);
        for (InGameMapFeature *feature : std::as_const(cell->inGameMap().mFeatures)) {
            if (!inGameMapFeatureMatchesScope(feature, mFeatureScope))
                continue;
            ExportFeature item{feature, InGameMapGeometry()};
            QStringList diagnostics;
            if (!sanitizeInGameMapGeometryForExport(feature->mGeometry,
                                                    item.geometry,
                                                    diagnostics) ||
                    !isRepresentable(item.geometry, feature, diagnostics)) {
                ++mRejectedFeatures;
                qWarning().noquote()
                        << QStringLiteral("InGameMap binary rejected feature: output=\"%1\" cell=%2,%3 feature=%4 properties={%5} reason=\"%6\"")
                           .arg(mOutputPath)
                           .arg(cellCoordinates.x()).arg(cellCoordinates.y())
                           .arg(feature->index())
                           .arg(propertySummary(feature))
                           .arg(diagnostics.join(QStringLiteral("; ")));
                continue;
            }
            if (!diagnostics.isEmpty()) {
                ++mRepairedFeatures;
                qWarning().noquote()
                        << QStringLiteral("InGameMap binary repaired feature: output=\"%1\" cell=%2,%3 feature=%4 properties={%5} action=\"%6\"")
                           .arg(mOutputPath)
                           .arg(cellCoordinates.x()).arg(cellCoordinates.y())
                           .arg(feature->index())
                           .arg(propertySummary(feature))
                           .arg(diagnostics.join(QStringLiteral("; ")));
            }
            exportFeatures += item;
        }

        if (!fitRendererBudget(exportFeatures, cell))
            return;

        if (exportFeatures.isEmpty()) {
            w << qint32(-1);
            return;
        }

        w << qint32(cellCoordinates.x());
        w << qint32(cellCoordinates.y());

        w << qint32(exportFeatures.size());

        for (const ExportFeature &item : std::as_const(exportFeatures)) {
            writeFeature(w, item.feature, item.geometry);
            ++mWrittenFeatures;
        }
    }

    bool isRepresentable(const InGameMapGeometry &geometry,
                         InGameMapFeature *feature,
                         QStringList &diagnostics) const
    {
        if (geometry.mCoordinates.size() > std::numeric_limits<qint8>::max()) {
            diagnostics += QStringLiteral("has more than 127 coordinate lists");
            return false;
        }
        for (int index = 0; index < geometry.mCoordinates.size(); ++index) {
            if (geometry.mCoordinates.at(index).size() >
                    std::numeric_limits<qint16>::max()) {
                diagnostics += QStringLiteral("coordinate list %1 has more than 32767 vertices")
                        .arg(index);
                return false;
            }
        }
        if (feature->mProperties.size() > std::numeric_limits<qint8>::max()) {
            diagnostics += QStringLiteral("has more than 127 properties");
            return false;
        }
        return true;
    }

    QString propertySummary(InGameMapFeature *feature) const
    {
        QStringList properties;
        for (const InGameMapProperty &property : feature->mProperties)
            properties += property.mKey + QLatin1Char('=') + property.mValue;
        return properties.join(QLatin1Char(','));
    }

    void writeFeature(QDataStream &w, InGameMapFeature* feature,
                      const InGameMapGeometry &geometry)
    {
        SaveStringIndex(w, geometry.mType);

        w << qint8(geometry.mCoordinates.size());
        for (const InGameMapCoordinates &coords : geometry.mCoordinates) {
            w << qint16(coords.size());
            for (const InGameMapPoint &point : coords) {
                w << qint16(int(point.x));
                w << qint16(int(point.y));
            }
        }

        w << qint8(feature->mProperties.size());
        for (auto& property : feature->mProperties) {
            SaveStringIndex(w, property.mKey);
            SaveStringIndex(w, property.mValue);
        }
    }

    void SaveString(QDataStream& w, const QString& str)
    {
        QByteArray utf8 = str.toUtf8();
        w << qint16(utf8.length());
        for (int i = 0; i < utf8.length(); i++) {
            w << quint8(utf8.at(i));
        }
    }

    void SaveStringIndex(QDataStream& w, const QString& str)
    {
        w << qint16(mStringTable[str]);
    }

    World *mWorld;
    QString mError;
    QDir mMapDir;
    QMap<QString, int> mStringTable;
    QString mOutputPath = QStringLiteral("<device>");
    int mWrittenFeatures = 0;
    int mRepairedFeatures = 0;
    int mRejectedFeatures = 0;
    InGameMapFeatureScope mFeatureScope;
};

/////

InGameMapWriterBinary::InGameMapWriterBinary()
    : d(new InGameMapWriterBinaryPrivate)
{
}

InGameMapWriterBinary::~InGameMapWriterBinary()
{
    delete d;
}

bool InGameMapWriterBinary::writeWorld(World *world, const QString &filePath)
{
    d->mError.clear();
    d->mOutputPath = QFileInfo(filePath).absoluteFilePath();
    QTemporaryFile tempFile;
    if (!d->openFile(&tempFile))
        return false;

    WorldConverter converter;
    World *worldForExport = world;
    if (world->cellSize() != 256)
        worldForExport = converter.convertWorld(world, world->cellSize(), 256);
    d->writeWorld(worldForExport, &tempFile, QFileInfo(filePath).absolutePath());
    if (worldForExport != world)
        delete worldForExport;

    if (!d->mError.isEmpty())
        return false;

    if (tempFile.error() != QFile::NoError) {
        d->mError = tempFile.errorString();
        return false;
    }

    // foo.pzw -> foo.pzw.bak
    QFileInfo destInfo(filePath);
    QString backupPath = filePath + QLatin1String(".bak");
    QFile backupFile(backupPath);
    if (destInfo.exists()) {
        if (backupFile.exists()) {
            if (!backupFile.remove()) {
                d->mError = QString(QLatin1String("Error deleting file!\n%1\n\n%2"))
                        .arg(backupPath)
                        .arg(backupFile.errorString());
                return false;
            }
        }
        QFile destFile(filePath);
        if (!destFile.rename(backupPath)) {
            d->mError = QString(QLatin1String("Error renaming file!\nFrom: %1\nTo: %2\n\n%3"))
                    .arg(filePath)
                    .arg(backupPath)
                    .arg(destFile.errorString());
            return false;
        }
    }

    // /tmp/tempXYZ -> foo.pzw
    tempFile.close();
    if (tempFile.rename(filePath)) {
        // If anything above failed, the temp file should auto-remove, but not after
        // a successful save.
        tempFile.setAutoRemove(false);
    } else {
        QFile destination(filePath);
        if (destination.exists() && !destination.remove()) {
            d->mError = QString(QLatin1String("Error replacing file!\n%1\n\n%2"))
                    .arg(filePath).arg(destination.errorString());
            if (backupFile.exists())
                backupFile.rename(filePath);
            return false;
        }
        if (!tempFile.copy(filePath)) {
            d->mError = QString(QLatin1String("Error copying file!\nFrom: %1\nTo: %2\n\n%3"))
                    .arg(tempFile.fileName()).arg(filePath)
                    .arg(tempFile.errorString());
            if (backupFile.exists())
                backupFile.rename(filePath);
            return false;
        }
    }

    return true;
}

void InGameMapWriterBinary::writeWorld(World *world, QIODevice *device, const QString &absDirPath)
{
    d->mError.clear();
    d->mOutputPath = QStringLiteral("<device>");
    WorldConverter converter;
    World *worldForExport = world;
    if (world->cellSize() != 256)
        worldForExport = converter.convertWorld(world, world->cellSize(), 256);
    d->writeWorld(worldForExport, device, absDirPath);
    if (worldForExport != world)
        delete worldForExport;
}

void InGameMapWriterBinary::setFeatureScope(InGameMapFeatureScope scope)
{
    d->mFeatureScope = scope;
}

QString InGameMapWriterBinary::errorString() const
{
    return d->mError;
}
