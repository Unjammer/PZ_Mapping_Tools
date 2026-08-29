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

#include "ingamemapwriter.h"
#include "ingamemapwriterbinary.h"

#include "world.h"
#include "worldcell.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTemporaryFile>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace {
bool hasInGameMapProperty(InGameMapFeature *feature,
                          const QString &propertyName)
{
    for (const InGameMapProperty &property : feature->mProperties) {
        if (property.mKey == propertyName)
            return true;
    }
    return false;
}

QString inGameMapPropertyValue(InGameMapFeature *feature,
                               const QString &propertyName)
{
    for (const InGameMapProperty &property : feature->mProperties) {
        if (property.mKey == propertyName)
            return property.mValue;
    }
    return QString();
}

double distanceToSegmentSquared(const InGameMapPoint &point,
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

void simplifySection(const QVector<InGameMapPoint> &points,
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

InGameMapCoordinates simplifyClosedCoordinates(
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

int rendererIndexEstimate(const QList<InGameMapExportFeature> &features)
{
    qint64 estimate = 0;
    for (const InGameMapExportFeature &item : features) {
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
        if (hasInGameMapProperty(item.feature,
                                 QStringLiteral("highway"))) {
            const qint64 offsetTriangleIndices = qMax<qint64>(
                        3, qint64(3) *
                        (pointCount * 2 + holeCount * 4 - 2));
            estimate += baseTriangleIndices + offsetTriangleIndices * 6;
        } else {
            estimate += baseTriangleIndices;
        }
    }
    return int(qMin<qint64>(estimate,
                std::numeric_limits<int>::max()));
}

int exportPointCount(const QList<InGameMapExportFeature> &features)
{
    int count = 0;
    for (const InGameMapExportFeature &item : features) {
        for (const InGameMapCoordinates &coordinates :
             item.geometry.mCoordinates)
            count += coordinates.size();
    }
    return count;
}

qint64 geometryAreaTwice(const InGameMapGeometry &geometry)
{
    qint64 area = 0;
    for (int coordinateIndex = 0;
         coordinateIndex < geometry.mCoordinates.size();
         ++coordinateIndex) {
        const InGameMapCoordinates &coordinates =
                geometry.mCoordinates.at(coordinateIndex);
        qint64 ringArea = 0;
        for (int pointIndex = 0;
             pointIndex < coordinates.size(); ++pointIndex) {
            const InGameMapPoint &first = coordinates.at(pointIndex);
            const InGameMapPoint &second = coordinates.at(
                        (pointIndex + 1) % coordinates.size());
            ringArea += qRound64(first.x) * qRound64(second.y) -
                    qRound64(second.x) * qRound64(first.y);
        }
        const qint64 absoluteArea = qAbs(ringArea);
        area += coordinateIndex == 0 ? absoluteArea : -absoluteArea;
    }
    return qMax<qint64>(0, area);
}

qint64 geometrySpan(const InGameMapGeometry &geometry)
{
    qint64 minimumX = std::numeric_limits<qint64>::max();
    qint64 minimumY = std::numeric_limits<qint64>::max();
    qint64 maximumX = std::numeric_limits<qint64>::min();
    qint64 maximumY = std::numeric_limits<qint64>::min();
    for (const InGameMapCoordinates &coordinates : geometry.mCoordinates) {
        for (const InGameMapPoint &point : coordinates) {
            const qint64 x = qRound64(point.x);
            const qint64 y = qRound64(point.y);
            minimumX = qMin(minimumX, x);
            minimumY = qMin(minimumY, y);
            maximumX = qMax(maximumX, x);
            maximumY = qMax(maximumY, y);
        }
    }
    if (minimumX > maximumX || minimumY > maximumY)
        return 0;
    return qMax(maximumX - minimumX, maximumY - minimumY);
}

bool geometryTouchesCellBoundary(const InGameMapGeometry &geometry,
                                 int cellSize)
{
    for (const InGameMapCoordinates &coordinates : geometry.mCoordinates) {
        for (const InGameMapPoint &point : coordinates) {
            const qint64 x = qRound64(point.x);
            const qint64 y = qRound64(point.y);
            if (x <= 0 || y <= 0 || x >= cellSize || y >= cellSize)
                return true;
        }
    }
    return false;
}

int highwayPriority(InGameMapFeature *feature)
{
    const QString value = inGameMapPropertyValue(
                feature, QStringLiteral("highway"));
    if (value == QLatin1String("primary"))
        return 3;
    if (value == QLatin1String("secondary"))
        return 2;
    if (value == QLatin1String("tertiary"))
        return 1;
    return 0;
}
}

class InGameMapWriterPrivate
{
    Q_DECLARE_TR_FUNCTIONS(InGameMapWriter)

public:
    InGameMapWriterPrivate()
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
        mWrittenFeatures = 0;
        mRepairedFeatures = 0;
        mRejectedFeatures = 0;

        QXmlStreamWriter writer(device);
        writer.setAutoFormatting(true);
        writer.setAutoFormattingIndent(1);

        writer.writeStartDocument();

        writeWorld(writer, world);

        writer.writeEndDocument();

        qInfo() << "InGameMap XML geometry validation:"
                << "output" << mOutputPath
                << "written" << mWrittenFeatures
                << "repaired" << mRepairedFeatures
                << "rejected" << mRejectedFeatures;
    }

    void writeWorld(QXmlStreamWriter &w, World *world)
    {
        w.writeStartElement(QLatin1String("world"));

        w.writeAttribute(QLatin1String("version"), QLatin1String("1.0"));
        w.writeAttribute(QLatin1String("cellSize"),
                         QString::number(world->cellSize()));

        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell *cell = world->cellAt(x, y);
                writeCell(w, cell);
                if (!mError.isEmpty()) {
                    w.writeEndElement();
                    return;
                }
            }
        }

        w.writeEndElement();
    }

    void writeCell(QXmlStreamWriter &w, WorldCell *cell)
    {
        QList<InGameMapExportFeature> exportFeatures;
        for (InGameMapFeature *feature : std::as_const(cell->inGameMap().mFeatures)) {
            if (!inGameMapFeatureMatchesScope(feature, mFeatureScope))
                continue;
            InGameMapExportFeature item{feature, InGameMapGeometry()};
            QStringList diagnostics;
            if (!sanitizeInGameMapGeometryForExport(feature->mGeometry,
                                                    item.geometry,
                                                    diagnostics)) {
                ++mRejectedFeatures;
                qWarning().noquote()
                        << QStringLiteral("InGameMap XML rejected feature: output=\"%1\" cell=%2,%3 feature=%4 properties={%5} reason=\"%6\"")
                           .arg(mOutputPath)
                           .arg(cell->x()).arg(cell->y())
                           .arg(feature->index())
                           .arg(propertySummary(feature))
                           .arg(diagnostics.join(QStringLiteral("; ")));
                continue;
            }
            if (!diagnostics.isEmpty()) {
                ++mRepairedFeatures;
                qWarning().noquote()
                        << QStringLiteral("InGameMap XML repaired feature: output=\"%1\" cell=%2,%3 feature=%4 properties={%5} action=\"%6\"")
                           .arg(mOutputPath)
                           .arg(cell->x()).arg(cell->y())
                           .arg(feature->index())
                           .arg(propertySummary(feature))
                           .arg(diagnostics.join(QStringLiteral("; ")));
            }
            exportFeatures += item;
        }

        if (!fitInGameMapRendererBudget(
                    exportFeatures, cell, mOutputPath,
                    QStringLiteral("XML"), &mRepairedFeatures, &mError))
            return;

        if (exportFeatures.isEmpty())
            return;

        const QPoint worldOrigin = cell->world()->getGenerateLotsSettings().worldOrigin;

        w.writeStartElement(QLatin1String("cell"));
        w.writeAttribute(QLatin1String("x"), QString::number(worldOrigin.x() + cell->x()));
        w.writeAttribute(QLatin1String("y"), QString::number(worldOrigin.y() + cell->y()));

        for (const InGameMapExportFeature &item : std::as_const(exportFeatures)) {
            writeFeature(w, item.feature, item.geometry);
            ++mWrittenFeatures;
        }

        w.writeEndElement();
    }

    QString propertySummary(InGameMapFeature *feature) const
    {
        QStringList properties;
        for (const InGameMapProperty &property : feature->mProperties)
            properties += property.mKey + QLatin1Char('=') + property.mValue;
        return properties.join(QLatin1Char(','));
    }

    void writeFeature(QXmlStreamWriter &w, InGameMapFeature* feature,
                      const InGameMapGeometry &geometry)
    {
        w.writeStartElement(QLatin1String("feature"));

        w.writeStartElement(QLatin1String("geometry"));
        w.writeAttribute(QLatin1String("type"), geometry.mType);
        for (const InGameMapCoordinates &coords : geometry.mCoordinates) {
            w.writeStartElement(QLatin1String("coordinates"));
            for (auto& point : coords) {
                w.writeStartElement(QLatin1String("point"));
                w.writeAttribute(QLatin1String("x"), QString::number(point.x));
                w.writeAttribute(QLatin1String("y"), QString::number(point.y));
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();

        w.writeStartElement(QLatin1String("properties"));
        for (auto& property : feature->mProperties) {
            w.writeStartElement(QLatin1String("property"));
            w.writeAttribute(QLatin1String("name"), property.mKey);
            w.writeAttribute(QLatin1String("value"), property.mValue);
            w.writeEndElement();
        }
        w.writeEndElement();

        w.writeEndElement();
    }

    World *mWorld;
    QString mError;
    QDir mMapDir;
    QString mOutputPath = QStringLiteral("<device>");
    int mWrittenFeatures = 0;
    int mRepairedFeatures = 0;
    int mRejectedFeatures = 0;
    InGameMapFeatureScope mFeatureScope;
};

/////

bool inGameMapFeatureMatchesScope(
        const InGameMapFeature *feature,
        InGameMapFeatureScope scope)
{
    if (scope == InGameMapFeatureScope::AllFeatures)
        return true;
    const bool forest = feature && feature->mProperties.contains(
                QStringLiteral("natural"), QStringLiteral("forest"));
    return scope == InGameMapFeatureScope::ForestFeatures
            ? forest
            : !forest;
}

bool fitInGameMapRendererBudget(
        QList<InGameMapExportFeature> &features,
        WorldCell *cell,
        const QString &outputPath,
        const QString &formatName,
        int *repairedFeatures,
        QString *error)
{
    static const int safeIndexBudget = 28000;
    static const int safePointBufferShorts = 28000;
    const int originalPoints = exportPointCount(features);
    const int originalEstimate = rendererIndexEstimate(features);
    if (originalPoints * 2 <= safePointBufferShorts &&
            originalEstimate <= safeIndexBudget)
        return true;
    QSet<InGameMapFeature *> repaired;
    double usedTolerance = 0.0;
    const QVector<double> tolerances =
            {1.0, 2.0, 3.0, 4.0};
    for (double tolerance : tolerances) {
        for (InGameMapExportFeature &item : features) {
            if (!item.geometry.isPolygon() ||
                    !hasInGameMapProperty(item.feature,
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
                        {InGameMapExportFeature{
                             item.feature, sanitized}}) <
                        rendererIndexEstimate({item})) {
                item.geometry = sanitized;
                repaired.insert(item.feature);
            }
        }
        usedTolerance = tolerance;
        if (exportPointCount(features) * 2 <= safePointBufferShorts &&
                rendererIndexEstimate(features) <= safeIndexBudget)
            break;
    }
    const QPoint worldCoordinates =
            cell->world()->getGenerateLotsSettings().worldOrigin +
            QPoint(cell->x(), cell->y());
    int currentPoints = exportPointCount(features);
    int currentEstimate = rendererIndexEstimate(features);
    if (!repaired.isEmpty()) {
        qWarning().noquote()
                << QStringLiteral("InGameMap %1 simplified renderer-heavy highways: output=\"%2\" cell=%3,%4 tolerance=%5 points=%6->%7 estimated-indices=%8->%9 changed-features=%10")
                   .arg(formatName, outputPath)
                   .arg(worldCoordinates.x()).arg(worldCoordinates.y())
                   .arg(usedTolerance)
                   .arg(originalPoints).arg(currentPoints)
                   .arg(originalEstimate).arg(currentEstimate)
                   .arg(repaired.size());
    }

    struct RemovalCandidate
    {
        int index;
        bool boundary;
        int priority;
        qint64 area;
        qint64 span;
        int cost;
    };
    QVector<RemovalCandidate> candidates;
    for (int index = 0; index < features.size(); ++index) {
        const InGameMapExportFeature &item = features.at(index);
        if (!item.geometry.isPolygon() ||
                !hasInGameMapProperty(item.feature,
                                      QStringLiteral("highway")))
            continue;
        candidates += RemovalCandidate{
                index,
                geometryTouchesCellBoundary(item.geometry,
                                            cell->world()->cellSize()),
                highwayPriority(item.feature),
                geometryAreaTwice(item.geometry),
                geometrySpan(item.geometry),
                rendererIndexEstimate({item})};
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const RemovalCandidate &first,
                 const RemovalCandidate &second) {
        if (first.boundary != second.boundary)
            return !first.boundary;
        if (first.priority != second.priority)
            return first.priority < second.priority;
        if (first.area != second.area)
            return first.area < second.area;
        if (first.span != second.span)
            return first.span < second.span;
        if (first.cost != second.cost)
            return first.cost < second.cost;
        return first.index < second.index;
    });

    QSet<int> removedIndexes;
    QMap<QString, int> removedTypes;
    for (const RemovalCandidate &candidate : std::as_const(candidates)) {
        if (currentPoints * 2 <= safePointBufferShorts &&
                currentEstimate <= safeIndexBudget)
            break;
        const InGameMapExportFeature &item = features.at(candidate.index);
        removedIndexes.insert(candidate.index);
        repaired.insert(item.feature);
        removedTypes[inGameMapPropertyValue(
                    item.feature, QStringLiteral("highway"))]++;
        currentEstimate -= candidate.cost;
        for (const InGameMapCoordinates &coordinates :
             item.geometry.mCoordinates)
            currentPoints -= coordinates.size();
    }
    if (!removedIndexes.isEmpty()) {
        QList<InGameMapExportFeature> retained;
        retained.reserve(features.size() - removedIndexes.size());
        for (int index = 0; index < features.size(); ++index) {
            if (!removedIndexes.contains(index))
                retained += features.at(index);
        }
        features.swap(retained);
        QStringList removedSummary;
        for (auto it = removedTypes.cbegin();
             it != removedTypes.cend(); ++it) {
            removedSummary += QStringLiteral("%1=%2")
                    .arg(it.key()).arg(it.value());
        }
        qWarning().noquote()
                << QStringLiteral("InGameMap %1 omitted smallest renderer-heavy highway fragments: output=\"%2\" cell=%3,%4 omitted=%5 types={%6} points=%7->%8 estimated-indices=%9->%10")
                   .arg(formatName, outputPath)
                   .arg(worldCoordinates.x()).arg(worldCoordinates.y())
                   .arg(removedIndexes.size())
                   .arg(removedSummary.join(QLatin1Char(',')))
                   .arg(originalPoints).arg(currentPoints)
                   .arg(originalEstimate).arg(currentEstimate);
    }

    if (repairedFeatures)
        *repairedFeatures += repaired.size();

    if (currentPoints * 2 > safePointBufferShorts ||
            currentEstimate > safeIndexBudget) {
        if (error) {
            *error = QCoreApplication::translate(
                        "InGameMapWriter",
                        "InGameMap cell %1,%2 is too complex for the "
                        "game's 16-bit world-map renderer after safe road "
                        "simplification (%3 points, estimated %4 indices).")
                    .arg(worldCoordinates.x()).arg(worldCoordinates.y())
                    .arg(currentPoints).arg(currentEstimate);
            qCritical().noquote() << *error;
        }
        return false;
    }
    return true;
}

bool validateInGameMapRendererBudget(QString *summary, QString *error)
{
    World world(1, 1, WorldGridFormat::Native256);
    WorldCell *cell = world.cellAt(0, 0);
    QList<InGameMapExportFeature> firstPass;
    for (int index = 0; index < 400; ++index) {
        InGameMapFeature *feature = new InGameMapFeature(
                    &cell->inGameMap());
        feature->mGeometry.mType = QStringLiteral("Polygon");
        const int x = index == 0 ? 0 : 1 + index % 20 * 12;
        const int y = index == 0 ? 0 : 1 + index / 20 * 12;
        InGameMapCoordinates coordinates;
        coordinates += InGameMapPoint(x, y);
        coordinates += InGameMapPoint(x + 5, y);
        coordinates += InGameMapPoint(x, y + 5);
        feature->mGeometry.mCoordinates += coordinates;
        feature->mProperties += InGameMapProperty(
                    QStringLiteral("highway"),
                    index == 0
                    ? QStringLiteral("primary")
                    : QStringLiteral("tertiary"));
        cell->inGameMap().mFeatures += feature;
        firstPass += InGameMapExportFeature{
                feature, feature->mGeometry};
    }
    QList<InGameMapExportFeature> secondPass = firstPass;
    int firstRepairs = 0;
    int secondRepairs = 0;
    QString firstError;
    QString secondError;
    if (!fitInGameMapRendererBudget(
                firstPass, cell, QStringLiteral("<validation>"),
                QStringLiteral("first"), &firstRepairs, &firstError) ||
            !fitInGameMapRendererBudget(
                secondPass, cell, QStringLiteral("<validation>"),
                QStringLiteral("second"), &secondRepairs, &secondError)) {
        if (error)
            *error = !firstError.isEmpty() ? firstError : secondError;
        return false;
    }
    bool identical = firstPass.size() == secondPass.size();
    for (int index = 0; identical && index < firstPass.size(); ++index)
        identical = firstPass.at(index).feature ==
                secondPass.at(index).feature;
    const InGameMapFeature *boundaryFeature =
            cell->inGameMap().mFeatures.first();
    bool boundaryRetained = false;
    for (const InGameMapExportFeature &item : std::as_const(firstPass))
        boundaryRetained |= item.feature == boundaryFeature;
    if (!identical || !boundaryRetained ||
            firstPass.size() >= 400 || firstRepairs <= 0 ||
            firstRepairs != secondRepairs) {
        if (error) {
            *error = QCoreApplication::translate(
                        "InGameMapWriter",
                        "Renderer-budget repair was not deterministic or "
                        "did not preserve the boundary road.");
        }
        return false;
    }
    if (summary) {
        *summary = QCoreApplication::translate(
                    "InGameMapWriter",
                    "%1 renderer-heavy fragments retained from 400 with "
                    "deterministic boundary-road preservation")
                .arg(firstPass.size());
    }
    return true;
}

InGameMapWriter::InGameMapWriter()
    : d(new InGameMapWriterPrivate)
{
}

InGameMapWriter::~InGameMapWriter()
{
    delete d;
}

bool InGameMapWriter::writeWorld(World *world, const QString &filePath)
{
    d->mOutputPath = QFileInfo(filePath).absoluteFilePath();
    QTemporaryFile tempFile;
    if (!d->openFile(&tempFile))
        return false;

    World *worldForExport = world;
    if (world->cellSize() != 256)
        worldForExport = convertInGameMapWorldCellSize(world, 256);
    d->writeWorld(worldForExport, &tempFile,
                  QFileInfo(filePath).absolutePath());
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

void InGameMapWriter::writeWorld(World *world, QIODevice *device, const QString &absDirPath)
{
    d->mOutputPath = QStringLiteral("<device>");
    World *worldForExport = world;
    if (world->cellSize() != 256)
        worldForExport = convertInGameMapWorldCellSize(world, 256);
    d->writeWorld(worldForExport, device, absDirPath);
    if (worldForExport != world)
        delete worldForExport;
}

void InGameMapWriter::setFeatureScope(InGameMapFeatureScope scope)
{
    d->mFeatureScope = scope;
}

QString InGameMapWriter::errorString() const
{
    return d->mError;
}
