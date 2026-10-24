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

#include "ingamemapfeaturegenerator.h"

#include "generatelotsfailuredialog.h"
#include "lotfilesmanager.h"
#include "mainwindow.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "progress.h"
#include "preferences.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"

#include "BuildingEditor/roofhiding.h"

#include "bmpblender.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "tile.h"
#include "tileset.h"
#include "tilelayer.h"

#include <qmath.h>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QSet>
#include <QUndoStack>

#include "clipper.hpp"

using namespace Tiled;

namespace
{
struct pzPolygon
{
    ClipperLib::Path outer;
    ClipperLib::Paths inner; // holes
};

struct RoadMaskRules
{
    int minimumArea;
    int minimumSpan;
    int maximumHoleArea;
};

const RoadMaskRules highwayRoadMaskRules = {6, 6, 4};
const RoadMaskRules trailRoadMaskRules = {12, 10, 4};
const RoadMaskRules railwayRoadMaskRules = {4, 6, 2};

QSet<QString> featureTileSet(const QStringList &tiles)
{
    QSet<QString> result;
    for (const QString &tile : tiles)
        result.insert(tile);
    return result;
}

QString featureTileName(const Tiled::Tile *tile)
{
    if (!tile || !tile->tileset())
        return QString();
    return tile->tileset()->name() + QLatin1Char('_') +
            QString::number(tile->id());
}

struct RoadMaskCleanup
{
    int bridgedTiles = 0;
    int filledHoleTiles = 0;
    int removedComponents = 0;
    int removedTiles = 0;
};

class RoadMask
{
public:
    explicit RoadMask(const QSize &size) :
        mSize(size),
        mTiles(qMax(0, size.width() * size.height()), 0)
    {
    }

    QSize size() const
    {
        return mSize;
    }

    bool contains(int x, int y) const
    {
        return x >= 0 && y >= 0 &&
                x < mSize.width() && y < mSize.height();
    }

    bool value(int x, int y) const
    {
        return contains(x, y) && mTiles.at(index(x, y)) != 0;
    }

    void setValue(int x, int y, bool value = true)
    {
        if (contains(x, y))
            mTiles[index(x, y)] = value ? 1 : 0;
    }

    int index(int x, int y) const
    {
        return y * mSize.width() + x;
    }

    QPoint point(int index) const
    {
        return QPoint(index % mSize.width(), index / mSize.width());
    }

    int tileCount() const
    {
        int count = 0;
        for (quint8 value : mTiles)
            count += value != 0;
        return count;
    }

private:
    QSize mSize;
    QVector<quint8> mTiles;
};

void bridgeSingleTileBreaks(RoadMask &mask, RoadMaskCleanup &cleanup)
{
    RoadMask source = mask;
    for (int y = 0; y < source.size().height(); ++y) {
        for (int x = 0; x < source.size().width(); ++x) {
            if (source.value(x, y))
                continue;
            const bool horizontal = source.value(x - 1, y) &&
                    source.value(x + 1, y);
            const bool vertical = source.value(x, y - 1) &&
                    source.value(x, y + 1);
            if (horizontal || vertical) {
                mask.setValue(x, y);
                ++cleanup.bridgedTiles;
            }
        }
    }
}

QVector<int> connectedMaskRegion(const RoadMask &mask, int start,
                                 bool occupied, QVector<quint8> &visited,
                                 bool *touchesBoundary)
{
    QVector<int> region;
    QVector<int> pending;
    pending += start;
    visited[start] = 1;
    *touchesBoundary = false;
    const int width = mask.size().width();
    const int height = mask.size().height();
    const QPoint directions[] = {
        QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)
    };
    while (!pending.isEmpty()) {
        const int current = pending.takeLast();
        region += current;
        const QPoint point = mask.point(current);
        if (point.x() == 0 || point.y() == 0 ||
                point.x() == width - 1 || point.y() == height - 1)
            *touchesBoundary = true;
        for (const QPoint &direction : directions) {
            const QPoint adjacent = point + direction;
            if (!mask.contains(adjacent.x(), adjacent.y()))
                continue;
            const int adjacentIndex = mask.index(adjacent.x(), adjacent.y());
            if (visited.at(adjacentIndex) ||
                    mask.value(adjacent.x(), adjacent.y()) != occupied)
                continue;
            visited[adjacentIndex] = 1;
            pending += adjacentIndex;
        }
    }
    return region;
}

void removeSmallRoadComponents(RoadMask &mask, const RoadMaskRules &rules,
                               RoadMaskCleanup &cleanup)
{
    QVector<quint8> visited(mask.size().width() * mask.size().height(), 0);
    for (int y = 0; y < mask.size().height(); ++y) {
        for (int x = 0; x < mask.size().width(); ++x) {
            const int start = mask.index(x, y);
            if (!mask.value(x, y) || visited.at(start))
                continue;
            bool touchesBoundary = false;
            const QVector<int> region = connectedMaskRegion(
                        mask, start, true, visited, &touchesBoundary);
            int minimumX = x;
            int maximumX = x;
            int minimumY = y;
            int maximumY = y;
            for (int index : region) {
                const QPoint point = mask.point(index);
                minimumX = qMin(minimumX, point.x());
                maximumX = qMax(maximumX, point.x());
                minimumY = qMin(minimumY, point.y());
                maximumY = qMax(maximumY, point.y());
            }
            const int span = qMax(maximumX - minimumX + 1,
                                  maximumY - minimumY + 1);
            const bool keep = region.size() >= rules.minimumArea ||
                    span >= rules.minimumSpan ||
                    (touchesBoundary && region.size() >= 2);
            if (keep)
                continue;
            for (int index : region) {
                const QPoint point = mask.point(index);
                mask.setValue(point.x(), point.y(), false);
            }
            ++cleanup.removedComponents;
            cleanup.removedTiles += region.size();
        }
    }
}

void fillSmallRoadHoles(RoadMask &mask, const RoadMaskRules &rules,
                        RoadMaskCleanup &cleanup)
{
    QVector<quint8> visited(mask.size().width() * mask.size().height(), 0);
    for (int y = 0; y < mask.size().height(); ++y) {
        for (int x = 0; x < mask.size().width(); ++x) {
            const int start = mask.index(x, y);
            if (mask.value(x, y) || visited.at(start))
                continue;
            bool touchesBoundary = false;
            const QVector<int> region = connectedMaskRegion(
                        mask, start, false, visited, &touchesBoundary);
            if (touchesBoundary || region.size() > rules.maximumHoleArea)
                continue;
            for (int index : region) {
                const QPoint point = mask.point(index);
                mask.setValue(point.x(), point.y());
            }
            cleanup.filledHoleTiles += region.size();
        }
    }
}

RoadMaskCleanup normalizeRoadMask(RoadMask &mask, const RoadMaskRules &rules)
{
    RoadMaskCleanup cleanup;
    bridgeSingleTileBreaks(mask, cleanup);
    removeSmallRoadComponents(mask, rules, cleanup);
    fillSmallRoadHoles(mask, rules, cleanup);
    return cleanup;
}

bool addMaskBoundariesToClipper(const RoadMask &mask, ClipperLib::Clipper &clipper)
{
    const int stride = mask.size().width() + 1;
    QVector<quint8> edges(stride * (mask.size().height() + 1), 0);
    for (int y = 0; y < mask.size().height(); ++y) {
        for (int x = 0; x < mask.size().width(); ++x) {
            if (!mask.value(x, y))
                continue;
            if (!mask.value(x, y - 1))
                edges[y * stride + x] |= 1;
            if (!mask.value(x + 1, y))
                edges[y * stride + x + 1] |= 2;
            if (!mask.value(x, y + 1))
                edges[(y + 1) * stride + x + 1] |= 4;
            if (!mask.value(x - 1, y))
                edges[(y + 1) * stride + x] |= 8;
        }
    }
    const QPoint directions[] = {
        QPoint(1, 0), QPoint(0, 1), QPoint(-1, 0), QPoint(0, -1)
    };
    clipper.StrictlySimple(true);
    for (int start = 0; start < edges.size(); ++start) {
        while (edges.at(start)) {
            int direction = 0;
            while (!(edges.at(start) & (1 << direction)))
                ++direction;
            int current = start;
            ClipperLib::Path path;
            do {
                const QPoint point(current % stride, current / stride);
                path.push_back({point.x(), point.y()});
                edges[current] &= ~(1 << direction);
                const QPoint next = point + directions[direction];
                current = next.y() * stride + next.x();
                if (current == start)
                    break;
                if (current < 0 || current >= edges.size() || !edges.at(current))
                    return false;
                bool found = false;
                for (int turn : {1, 0, 3, 2}) {
                    const int candidate = (direction + turn) % 4;
                    if (edges.at(current) & (1 << candidate)) {
                        direction = candidate;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            } while (current != start);
            ClipperLib::CleanPolygon(path, 0.1);
            clipper.AddPath(path, ClipperLib::ptSubject, true);
        }
    }
    return true;
}

int PIXELS_PER_CELL = 48;
bool tmxContainsRoomDefs(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray token("RoomDefs");
    QByteArray overlap;
    while (!file.atEnd()) {
        const QByteArray bytes = overlap + file.read(64 * 1024);
        if (bytes.contains(token))
            return true;
        overlap = bytes.right(token.size() - 1);
    }
    return false;
}
bool validateGeneratedPolygon(ClipperLib::Path &path, WorldCell *cell,
                              const QString &featureType, const QString &part,
                              int &cleanedCount, int &rejectedCount)
{
    const int originalSize = int(path.size());
    ClipperLib::CleanPolygon(path, 0.1);
    if (int(path.size()) != originalSize) {
        ++cleanedCount;
        qInfo().noquote()
                << QStringLiteral("Generate Features cleaned polygon: type=%1 cell=%2,%3 part=%4 vertices=%5->%6")
                   .arg(featureType)
                   .arg(cell->x()).arg(cell->y())
                   .arg(part)
                   .arg(originalSize).arg(path.size());
    }
    QString reason;
    if (path.size() < 3)
        reason = QStringLiteral("fewer than 3 distinct vertices");
    else if (qFuzzyIsNull(ClipperLib::Area(path)))
        reason = QStringLiteral("zero area");
    if (reason.isEmpty())
        return true;
    ++rejectedCount;
    qWarning().noquote()
            << QStringLiteral("Generate Features discarded polygon: type=%1 cell=%2,%3 part=%4 vertices=%5->%6 reason=\"%7\"")
               .arg(featureType)
               .arg(cell->x()).arg(cell->y())
               .arg(part)
               .arg(originalSize).arg(path.size())
               .arg(reason);
    return false;
}
}

InGameMapFeatureGenerator::InGameMapFeatureGenerator(QObject *parent) :
    QObject(parent)
{
}

static void simplifyRoadPolygon(ClipperLib::Path &nodes, int cellSize,
                                double tolerance, int maximumPointSpacing);
static void simplifyPolygon(ClipperLib::Path &nodes, int cellSize);

static bool validateSurfaceMaskBoundaries(QString *error)
{
    int cases = 0;
    for (int cellSize : {256, 300}) {
        for (int fixture = 0; fixture < 36; ++fixture) {
            RoadMask mask(QSize(cellSize, cellSize));
            for (int y = 0; y < cellSize; ++y) {
                for (int x = 0; x < cellSize; ++x) {
                    bool occupied = false;
                    if (fixture == 0)
                        occupied = true;
                    else if (fixture == 1)
                        occupied = x >= 4 && x < cellSize - 4 && y >= 4 && y < cellSize - 4
                                && !(x >= 20 && x < 28 && y >= 20 && y < 28);
                    else if (fixture == 2)
                        occupied = (x == 0 && y == 0) || (x == 1 && y == 1)
                                || (x == cellSize - 1 && y == cellSize - 1);
                    else if (fixture >= 4 && x >= 4 && x < 60 && y >= 4 && y < 60) {
                        const int pattern = fixture - 4;
                        const bool inside = pattern % 3 == 0 ? (y > 12 && y < 42)
                                : pattern % 3 == 1 ? (x - 32) * (x - 32) + (y - 32) * (y - 32) < 27 * 27
                                                  : (x < 25 || y > 29);
                        const bool hole = pattern < 4 ? (x > 20 && x < 25 && y > 20 && y < 25)
                                                       : ((x * 31 + y * 17 + pattern * 13) % 101 < pattern);
                        occupied = inside && !hole;
                    }
                    mask.setValue(x, y, occupied);
                }
            }
            ClipperLib::Clipper clipper;
            ClipperLib::PolyTree tree;
            if (!addMaskBoundariesToClipper(mask, clipper)
                    || (!clipper.Execute(ClipperLib::ctUnion, tree,
                                          ClipperLib::pftNonZero, ClipperLib::pftNonZero)
                        && mask.tileCount() != 0)) {
                *error = QStringLiteral("Surface boundary tracing failed");
                return false;
            }
            int expectedEdges = 0;
            for (int y = 0; y < cellSize; ++y) {
                for (int x = 0; x < cellSize; ++x) {
                    if (mask.value(x, y))
                        expectedEdges += !mask.value(x - 1, y) + !mask.value(x + 1, y)
                                + !mask.value(x, y - 1) + !mask.value(x, y + 1);
                }
            }
            QSet<quint64> boundaryEdges;
            double area = 0.0;
            int outerCount = 0;
            int holeCount = 0;
            for (ClipperLib::PolyNode *node = tree.GetFirst(); node; node = node->GetNext()) {
                ClipperLib::Path path = node->Contour;
                ClipperLib::CleanPolygon(path, 0.1);
                area += ClipperLib::Area(path);
                outerCount += !node->IsHole();
                holeCount += node->IsHole();
                QSet<quint64> vertices;
                for (size_t index = 0; index < path.size(); ++index) {
                    const auto &a = path[index];
                    const auto &b = path[(index + 1) % path.size()];
                    const quint64 vertex = quint64(a.Y * (cellSize + 1) + a.X);
                    if (vertices.contains(vertex) || (a.X != b.X && a.Y != b.Y)) {
                        *error = QStringLiteral("Surface contour contains a bridge or a non-grid edge");
                        return false;
                    }
                    vertices.insert(vertex);
                    const int dx = (b.X > a.X) - (b.X < a.X);
                    const int dy = (b.Y > a.Y) - (b.Y < a.Y);
                    const int direction = dx > 0 ? 0 : dy > 0 ? 1 : dx < 0 ? 2 : 3;
                    for (int x = int(a.X), y = int(a.Y); x != b.X || y != b.Y; x += dx, y += dy) {
                        const bool valid = direction == 0 ? mask.value(x, y) && !mask.value(x, y - 1)
                                : direction == 1 ? mask.value(x - 1, y) && !mask.value(x, y)
                                : direction == 2 ? mask.value(x - 1, y - 1) && !mask.value(x - 1, y)
                                                 : mask.value(x, y - 1) && !mask.value(x - 1, y - 1);
                        const quint64 edge = (quint64(y * (cellSize + 1) + x) << 2) | direction;
                        if (!valid || boundaryEdges.contains(edge)) {
                            *error = QStringLiteral("Surface contour changed a tile boundary");
                            return false;
                        }
                        boundaryEdges.insert(edge);
                    }
                }
                if (fixture <= 1) {
                    ClipperLib::Path water = path;
                    ClipperLib::Path road = path;
                    simplifyPolygon(water, cellSize);
                    simplifyRoadPolygon(road, cellSize, 2.0, 40);
                    if (qAbs(ClipperLib::Area(water)) != qAbs(ClipperLib::Area(path))
                            || qAbs(ClipperLib::Area(road)) != qAbs(ClipperLib::Area(path))) {
                        *error = QStringLiteral("Surface simplification changed a rectangular bank or hole");
                        return false;
                    }
                }
            }
            if (area != mask.tileCount() || boundaryEdges.size() != expectedEdges
                    || (fixture == 1 && (outerCount != 1 || holeCount != 1))) {
                *error = QStringLiteral("Surface mask topology mismatch for %1-cell fixture %2")
                        .arg(cellSize).arg(fixture);
                return false;
            }
            ++cases;
        }
    }
    qInfo() << "InGameMap surface-boundary validation:" << cases << "cases passed";
    return true;
}

bool InGameMapFeatureGenerator::validateRoadMaskProcessing(QString *summary,
                                                           QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    QString boundaryError;
    if (!validateSurfaceMaskBoundaries(&boundaryError))
        return fail(boundaryError);

    ClipperLib::Path angled = {{100, 100}, {100, 102}, {102, 100}, {102, 98}};
    const double angledArea = ClipperLib::Area(angled);
    simplifyRoadPolygon(angled, 300, 0.0, 1);
    if (angled.size() != 4 || ClipperLib::Area(angled) != angledArea)
        return fail(QStringLiteral("road simplification removed a non-collinear corner"));
    ClipperLib::Path aligned = {{100, 100}, {100, 101}, {100, 102}, {102, 102}, {102, 100}};
    simplifyRoadPolygon(aligned, 300, 0.0, 1);
    if (aligned.size() != 4 || qAbs(ClipperLib::Area(aligned)) != 4.0)
        return fail(QStringLiteral("road simplification changed an aligned outline"));

    RoadMask trail(QSize(24, 24));
    for (int x = 2; x <= 16; ++x) {
        if (x != 8)
            trail.setValue(x, 4);
    }
    for (int y = 14; y <= 15; ++y) {
        for (int x = 14; x <= 15; ++x)
            trail.setValue(x, y);
    }
    const RoadMaskCleanup trailCleanup = normalizeRoadMask(
                trail, trailRoadMaskRules);
    if (!trail.value(8, 4) || trail.tileCount() != 15)
        return fail(QStringLiteral("a one-tile trail break was not closed"));
    if (trail.value(14, 14) || trailCleanup.removedComponents != 1 ||
            trailCleanup.removedTiles != 4)
        return fail(QStringLiteral("a short isolated trail was retained"));

    RoadMask road(QSize(12, 12));
    for (int y = 3; y <= 6; ++y) {
        for (int x = 3; x <= 6; ++x) {
            if (x == 3 || x == 6 || y == 3 || y == 6)
                road.setValue(x, y);
        }
    }
    const RoadMaskCleanup roadCleanup = normalizeRoadMask(
                road, highwayRoadMaskRules);
    if (roadCleanup.filledHoleTiles != 4 || road.tileCount() != 16)
        return fail(QStringLiteral("a four-tile enclosed road hole was not filled"));

    ClipperLib::Clipper clipper;
    if (!addMaskBoundariesToClipper(trail, clipper))
        return fail(QStringLiteral("the trail mask has an open boundary"));
    ClipperLib::PolyTree tree;
    if (!clipper.Execute(ClipperLib::ctUnion, tree,
                         ClipperLib::pftNonZero, ClipperLib::pftNonZero))
        return fail(QStringLiteral("the normalized trail mask could not be unioned"));
    int outerPolygons = 0;
    int holes = 0;
    for (ClipperLib::PolyNode *node = tree.GetFirst(); node;
         node = node->GetNext()) {
        if (node->IsHole())
            ++holes;
        else
            ++outerPolygons;
    }
    if (outerPolygons != 1 || holes != 0)
        return fail(QStringLiteral("the normalized trail did not produce one continuous polygon"));

    const QSet<QString> treeDefaults = featureTileSet(
                Preferences::defaultTreeFeatureTiles());
    if (treeDefaults.size() != 53 ||
            !treeDefaults.contains(QStringLiteral("jumbo_tree_01_0")) ||
            !treeDefaults.contains(
                QStringLiteral("e_americanhollyJUMBOXL_1_0")) ||
            !treeDefaults.contains(
                QStringLiteral("e_yellowwoodJUMBOXXL_1_0"))) {
        return fail(QStringLiteral("the default Tree tile catalogue is incomplete"));
    }
    const QSet<QString> primaryDefaults = featureTileSet(
                Preferences::defaultPrimaryRoadFeatureTiles());
    const QSet<QString> secondaryDefaults = featureTileSet(
                Preferences::defaultSecondaryRoadFeatureTiles());
    const QSet<QString> tertiaryDefaults = featureTileSet(
                Preferences::defaultTertiaryRoadFeatureTiles());
    if (primaryDefaults.size() != 8 || secondaryDefaults.size() != 4 ||
            tertiaryDefaults.size() != 6 ||
            !primaryDefaults.contains(QStringLiteral("blends_street_01_32")) ||
            !secondaryDefaults.contains(QStringLiteral("blends_street_01_96")) ||
            !tertiaryDefaults.contains(QStringLiteral("blends_street_01_16"))) {
        return fail(QStringLiteral("the default Road tile catalogues are incomplete"));
    }
    if (!featureTileSet(QStringList()).isEmpty())
        return fail(QStringLiteral("an empty detection catalogue was not disabled"));
    if (Preferences::canonicalFeatureTileName(
                QStringLiteral("blends_street_01_032")) !=
            QStringLiteral("blends_street_01_32")) {
        return fail(QStringLiteral("a padded tile ID was not normalized"));
    }

    if (summary) {
        *summary = QStringLiteral("single-tile breaks closed, small enclosed holes filled, short isolated fragments removed, long narrow trails retained, one continuous polygon produced, configurable Tree and Road tile catalogues validated");
    }
    if (error)
        error->clear();
    return true;
}

bool InGameMapFeatureGenerator::generateWorld(WorldDocument *worldDoc, InGameMapFeatureGenerator::GenerateMode mode, FeatureType type)
{
    mFeatureType = type;

    mWorldDoc = worldDoc;
    World *world = mWorldDoc->world();

    MapManager::instance()->purgeUnreferencedMaps();

    QString typeStr;
    switch (type) {
    case FeatureBuilding: typeStr = QStringLiteral("building"); break;
    case FeatureTree: typeStr = QStringLiteral("trees"); break;
    case FeatureWater: typeStr = QStringLiteral("water"); break;
    case FeatureRoad: typeStr = QStringLiteral("road"); break;
    }
    PROGRESS progress(QStringLiteral("Generating %1 features").arg(typeStr));

    mFailures.clear();
    mCleanedPolygonCount = 0;
    mRejectedPolygonCount = 0;

    mWorldDoc->undoStack()->beginMacro(QStringLiteral("Generate InGameMap %1 Features").arg(typeStr));

    if (mode == GenerateSelected) {
        for (WorldCell *cell : worldDoc->selectedCells()) {
            if (!generateCell(cell)) {
                mWorldDoc->undoStack()->endMacro();
                goto errorExit;
            }
        }
    } else {
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                if (!generateCell(world->cellAt(x, y))) {
                    mWorldDoc->undoStack()->endMacro();
                    goto errorExit;
                }
            }
        }
    }

    mWorldDoc->undoStack()->endMacro();

    MapManager::instance()->purgeUnreferencedMaps();

    qInfo() << "Generate Features polygon validation:"
            << "type" << typeStr
            << "cleaned" << mCleanedPolygonCount
            << "discarded" << mRejectedPolygonCount;
    if (!mFailures.isEmpty()) {
        QStringList errorList;
        for (const GenerateCellFailure &failure : mFailures) {
            errorList += QString(QStringLiteral("Cell %1,%2: %3")).arg(failure.cell->x()).arg(failure.cell->y()).arg(failure.error);
        }
        GenerateLotsFailureDialog dialog(errorList, MainWindow::instance());
        dialog.exec();
    }

    return true;

errorExit:
    QMessageBox::warning(MainWindow::instance(), tr("In-Game Map Generation Error"), mError);
    return false;
}

bool InGameMapFeatureGenerator::shouldGenerateCell(WorldCell *cell)
{
    switch (mFeatureType) {
    case FeatureBuilding:
        return !cell->lots().isEmpty()
                || tmxContainsRoomDefs(cell->mapFilePath());
    case FeatureTree:
        return true;
    case FeatureWater:
        return true;
    case FeatureRoad:
        return true;
    default:
        return false;
    }
}

bool InGameMapFeatureGenerator::generateCell(WorldCell *cell)
{
    if (!shouldGenerateCell(cell))
        return true;

    if (cell->mapFilePath().isEmpty()) {
        return true;
    }

    MapInfo *mapInfo = MapManager::instance()->loadMap(cell->mapFilePath(),
                                                       mWorldDoc->fileName());
    if (!mapInfo) {
        mError = MapManager::instance()->errorString();
        return false;
    }

    MapManager::instance()->addReferenceToMap(mapInfo);

    bool ok;
    switch (mFeatureType) {
    case FeatureBuilding:
        ok = doBuildings(cell, mapInfo);
        break;
    case FeatureTree:
        ok = doTrees(cell, mapInfo);
        break;
    case FeatureWater:
        ok = doWater(cell, mapInfo);
        break;
    case FeatureRoad:
        ok = doRoads(cell, mapInfo);
        break;
    }

    MapManager::instance()->removeReferenceToMap(mapInfo);

    return ok;
}

bool InGameMapFeatureGenerator::doBuildings(WorldCell *cell, MapInfo *mapInfo)
{
    auto& features = cell->inGameMap().features();
    for (int i = features.size() - 1; i >= 0; i--) {
        auto* feature = features[i];
        bool isBuilding = false;
        for (auto& property : feature->properties()) {
            if (property.mKey == QStringLiteral("building")) {
                isBuilding = true;
                break;
            }
        }
        if (isBuilding)
            mWorldDoc->removeInGameMapFeature(cell, feature->index());
    }

    DelayedMapLoader mapLoader;
    mapLoader.addMap(mapInfo);

    WorldCellLotList lots;
    for (WorldCellLot *lot : cell->lots()) {
        if (MapInfo *info = MapManager::instance()->loadMap(lot->mapName(),
                                                            QString(), true,
                                                            MapManager::PriorityMedium)) {
            mapLoader.addMap(info);
            lots += lot;
        } else {
            mFailures += GenerateCellFailure(cell, MapManager::instance()->errorString());
        }
    }

    while (mapLoader.isLoading()) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (!mapLoader.errorString().isEmpty()) {
        mError = mapLoader.errorString();
        return false;
    }

    if (mapInfo->map() != nullptr) {
        QRect bounds;
        QVector<QRect> rects;
        for (ObjectGroup *og : mapInfo->map()->objectGroups()) {
            if (!processObjectGroup(cell, mapInfo, og, 0, QPoint(),
                                    bounds, rects)) {
                return false;
            }
        }
        if (!traceBuildingOutline(cell, mapInfo, bounds, rects))
            return false;
    }

    for (WorldCellLot *lot : lots) {
        MapInfo *info = MapManager::instance()->mapInfo(lot->mapName());
        if (info != nullptr && info->map() != nullptr) {
            QRect bounds;
            QVector<QRect> rects;
            for (ObjectGroup *og : info->map()->objectGroups()) {
                if (processObjectGroup(cell, info, og, lot->level(), lot->pos(), bounds, rects) == false) {
                    return false;
                }
            }
            if (traceBuildingOutline(cell, info, bounds, rects) == false) {
                return false;
            }
        }
    }

    return true;
}

bool InGameMapFeatureGenerator::processObjectGroup(WorldCell *cell, MapInfo *mapInfo, ObjectGroup *objectGroup, int levelOffset,
                                                   const QPoint &offset, QRect &bounds, QVector<QRect> &rects)
{
    Q_UNUSED(cell)
    Q_UNUSED(mapInfo)

    if (objectGroup->name().contains(QLatin1String("RoomDefs")) == false) {
        return true;
    }

    int level = objectGroup->level();
    level += levelOffset;

    if (level < 0) {
        return true;
    }

    for (const MapObject *mapObject : objectGroup->objects()) {
        if (mapObject->width() * mapObject->height() <= 0)
            continue;

        if ((level <= 0) && BuildingEditor::RoofHiding::isEmptyOutside(mapObject->name())) {
            continue;
        }

        int x = qFloor(mapObject->x());
        int y = qFloor(mapObject->y());
        int w = qCeil(mapObject->x() + mapObject->width()) - x;
        int h = qCeil(mapObject->y() + mapObject->height()) - y;

        if (objectGroup->map()->orientation() == Map::Isometric) {
            x += 3 * level;
            y += 3 * level;
        }

        x += offset.x();
        y += offset.y();

        if (bounds.isEmpty())
            bounds = { x, y, w, h };
        else
            bounds |= { x, y, w, h };
        rects += { x, y, w, h };
    }

    return true;
}

bool InGameMapFeatureGenerator::traceBuildingOutline(WorldCell *cell, MapInfo *mapInfo, QRect &bounds, QVector<QRect> &rects)
{
    if (bounds.isEmpty())
        return true;
    ClipperLib::Clipper clipper;
    ClipperLib::Path path;

    for (const QRect box : rects) {
        path.clear();
        path << ClipperLib::IntPoint(box.left(), box.top());
        path << ClipperLib::IntPoint(box.right() + 1, box.top());
        path << ClipperLib::IntPoint(box.right() + 1, box.bottom() + 1);
        path << ClipperLib::IntPoint(box.left(), box.bottom() + 1);
        clipper.AddPath(path, ClipperLib::ptSubject, true);
    }

    ClipperLib::PolyTree polyTree;
    if (clipper.Execute(ClipperLib::ctDifference, polyTree, ClipperLib::PolyFillType::pftPositive) == false) {
        return true;
    }

    std::map<ClipperLib::PolyNode*,pzPolygon*> polyMap;
    std::vector<pzPolygon*> allPolygons;
    for (ClipperLib::PolyNode* node = polyTree.GetFirst(); node != nullptr; node = node->GetNext()) {
        if (node->IsHole()) {
            pzPolygon *outer = polyMap[node->Parent];
            outer->inner.push_back(node->Contour);
        } else {
            pzPolygon* poly = new pzPolygon();
            poly->outer = node->Contour;
            polyMap[node] = poly;
            allPolygons.push_back(poly);
        }
    }

    for (pzPolygon *poly : allPolygons) {
        ClipperLib::Path path = poly->outer;
        if (!validateGeneratedPolygon(path, cell, QStringLiteral("building"),
                                      QStringLiteral("outer"),
                                      mCleanedPolygonCount, mRejectedPolygonCount)) {
            continue;
        }

        InGameMapFeature* feature = new InGameMapFeature(&cell->inGameMap());
        InGameMapProperty property;
        property.mKey = QStringLiteral("building");
        QString LEGEND = QStringLiteral("Legend");
        if (mapInfo->map()->properties().contains(LEGEND)) {
            property.mValue = mapInfo->map()->property(LEGEND);
        } else {
            property.mValue = QStringLiteral("yes");
        }
        feature->properties() += property;
        for (auto it = mapInfo->map()->properties().cbegin(); it != mapInfo->map()->properties().cend(); it++) {
            if (it.key() == LEGEND) {
                continue;
            }
            property.mKey = it.key();
            property.mValue = it.value();
            feature->properties() += property;
        }
        feature->mGeometry.mType = QStringLiteral("Polygon");
        InGameMapCoordinates coords;
        for (auto& point : path) {
            coords += InGameMapPoint(point.X, point.Y);
        }
        feature->mGeometry.mCoordinates += coords;

        if (poly->inner.empty() == false) {
            for (auto& hole : poly->inner) {
                ClipperLib::Path cleanHole = hole;
                if (!validateGeneratedPolygon(cleanHole, cell,
                                              QStringLiteral("building"),
                                              QStringLiteral("hole"),
                                              mCleanedPolygonCount,
                                              mRejectedPolygonCount)) {
                    continue;
                }
                coords.clear();
                for (auto& point : cleanHole) {
                    coords += InGameMapPoint(point.X, point.Y);
                }
                feature->mGeometry.mCoordinates += coords;
            }
        }

        mWorldDoc->addInGameMapFeature(cell, cell->inGameMap().features().size(), feature);
    }

    qDeleteAll(allPolygons);
    return true;
}

#include <stack>

struct DPPoint {
    std::int64_t x;
    std::int64_t y;
    bool necessary;
};

// square_distance_from_line() and douglas_peucker() from tippecanoe.

static double square_distance_from_line(std::int64_t point_x, std::int64_t point_y, std::int64_t segA_x, std::int64_t segA_y, std::int64_t segB_x, std::int64_t segB_y) {
    double p2x = segB_x - segA_x;
    double p2y = segB_y - segA_y;
    double something = p2x * p2x + p2y * p2y;
    double u = (0 == something) ? 0 : ((point_x - segA_x) * p2x + (point_y - segA_y) * p2y) / something;

    if (u > 1) {
        u = 1;
    } else if (u < 0) {
        u = 0;
    }

    double x = segA_x + u * p2x;
    double y = segA_y + u * p2y;

    double dx = x - point_x;
    double dy = y - point_y;

    return dx * dx + dy * dy;
}

// https://github.com/Project-OSRM/osrm-backend/blob/733d1384a40f/Algorithms/DouglasePeucker.cpp
static void douglas_peucker(std::vector<DPPoint> &geom, size_t start, size_t n, double e, size_t kept, size_t retain) {
    e = e * e;
    std::stack<size_t> recursion_stack;

    {
        size_t left_border = 0;
        size_t right_border = 1;
        // Sweep linearly over array and identify those ranges that need to be checked
        do {
            if (geom[start + right_border].necessary) {
                recursion_stack.push(left_border);
                recursion_stack.push(right_border);
                left_border = right_border;
            }
            ++right_border;
        } while (right_border < n);
    }

    while (!recursion_stack.empty()) {
        // pop next element
        size_t second = recursion_stack.top();
        recursion_stack.pop();
        size_t first = recursion_stack.top();
        recursion_stack.pop();

        double max_distance = -1;
        size_t farthest_element_index = second;

        // find index idx of element with max_distance
        for (size_t i = first + 1; i < second; i++) {
            double temp_dist = square_distance_from_line(
                        geom[start + i].x, geom[start + i].y,
                        geom[start + first].x, geom[start + first].y,
                        geom[start + second].x, geom[start + second].y);

            double distance = std::fabs(temp_dist);

            if ((distance > e || kept < retain) && distance > max_distance) {
                farthest_element_index = i;
                max_distance = distance;
            }
        }

        if (max_distance >= 0) {
            // mark idx as necessary
            geom[start + farthest_element_index].necessary = true;
            kept++;

            if (1 < farthest_element_index - first) {
                recursion_stack.push(first);
                recursion_stack.push(farthest_element_index);
            }
            if (1 < second - farthest_element_index) {
                recursion_stack.push(farthest_element_index);
                recursion_stack.push(second);
            }
        }
    }
}

static void simplifyPolygon(ClipperLib::Path& nodes, int cellSize)
{
    if (nodes.size() < 3)
        return;
    // Simplification of the polygon using Ramer-Douglas-Peucker algorithm
    std::vector<DPPoint> points;
    std::int64_t SCALE = 1000;
    const size_t DI = 40;
    size_t lastNecessary = -1;
    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& node = nodes[i];
        bool necessary = i == 0 || i == nodes.size() - 1;

        // Keep points on cell borders
        if (node.X == 0 || node.X == cellSize
                || node.Y == 0 || node.Y == cellSize)
            necessary = true;

        if (i - lastNecessary >= DI)
            necessary = true;

        if (necessary)
            lastNecessary = i;

        points.push_back( { std::int64_t(node.X * SCALE), std::int64_t(node.Y * SCALE), necessary } );
    }

    double simplification = 2 * SCALE;
    douglas_peucker(points, 0, points.size(), simplification, 2, 0);

    nodes.clear();
    for (auto& point : points) {
        if (point.necessary)
            nodes.push_back({int(point.x / SCALE), int(point.y / SCALE)});
    }

    // Merge horizontal/vertical spans (on cell borders)
    for (size_t i = 0; i + 1 < nodes.size(); i++) {
        const auto& n0 = nodes[i];
        size_t end = i;
        for (size_t j = i + 1; j < nodes.size(); j++) {
            const auto& n1 = nodes[j];
            if (n0.Y != n1.Y)
                break;
            end = j;
        }
        if (i != end)
            nodes.erase(nodes.begin() + i + 1, nodes.begin() + end /*- i - 1*/);
    }
    for (size_t i = 0; i + 1 < nodes.size(); i++) {
        const auto& n0 = nodes[i];
        size_t end = i;
        for (size_t j = i + 1; j < nodes.size(); j++) {
            const auto& n1 = nodes[j];
            if (n0.X != n1.X)
                break;
            end = j;
        }
        if (i < end)
            nodes.erase(nodes.begin() + i + 1, nodes.begin() + end /*- i - 1*/);
    }
}

bool InGameMapFeatureGenerator::doWater(WorldCell *cell, MapInfo *mapInfo)
{
    // Remove all "water=" features
    auto& features = cell->inGameMap().features();
    for (int i = features.size() - 1; i >= 0; i--) {
        auto* feature = features[i];
        if (feature->properties().containsKey(QStringLiteral("water"))) {
            mWorldDoc->removeInGameMapFeature(cell, feature->index());
        }
    }

    DelayedMapLoader mapLoader;
    mapLoader.addMap(mapInfo);

    while (mapInfo->isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    MapComposite staticMapComposite(mapInfo);
    MapComposite *mapComposite = &staticMapComposite;
    while (mapComposite->waitingForMapsToLoad() || mapLoader.isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    const QRect bounds(QPoint(), mapInfo->map()->size());

    auto* layerGroup = mapComposite->layerGroupForLevel(0);
    if (!layerGroup)
        return true;
    layerGroup->prepareDrawing2();

    ClipperLib::Clipper clipper;
    RoadMask waterMask(bounds.size());

    static QVector<const Tiled::Cell*> cells(40);
    OrderedCellsTemporaries vars;

    auto isWaterAt = [&](int x, int y) {
        cells.resize(0);
        layerGroup->orderedCellsAt2({x, y}, vars, cells);
        for (auto* cell : std::as_const(cells)) {
            if (cell->isEmpty())
                continue;
            if ((cell->tile->id() < 8) && (cell->tile->tileset()->name() == QStringLiteral("blends_natural_02"))) {
                return true;
            }
        }
        return false;
    };

    for (int y = 0; y < bounds.height(); y++) {
        for (int x = 0; x < bounds.width(); x++) {
            if (isWaterAt(x, y))
                waterMask.setValue(x, y);
        }
    }
    if (!addMaskBoundariesToClipper(waterMask, clipper)) {
        mError = tr("Could not trace the water boundaries in cell %1,%2.")
                .arg(cell->x()).arg(cell->y());
        return false;
    }

    ClipperLib::PolyTree polyTree;
    if (!clipper.Execute(ClipperLib::ctUnion, polyTree,
                         ClipperLib::pftNonZero, ClipperLib::pftNonZero)) {
        return true;
    }

    std::map<ClipperLib::PolyNode*,pzPolygon*> polyMap;
    std::vector<pzPolygon*> allPolygons;
    for (ClipperLib::PolyNode* node = polyTree.GetFirst(); node != nullptr; node = node->GetNext()) {
        if (node->IsHole()) {
            pzPolygon *outer = polyMap[node->Parent];
            outer->inner.push_back(node->Contour);
        } else {
            pzPolygon* poly = new pzPolygon();
            poly->outer = node->Contour;
            polyMap[node] = poly;
            allPolygons.push_back(poly);
        }
    }

    for (pzPolygon *poly : allPolygons) {
        ClipperLib::Path simple = poly->outer;
        ClipperLib::CleanPolygon(simple, 0.1);
        simplifyPolygon(simple, bounds.width());
        if (!validateGeneratedPolygon(simple, cell, QStringLiteral("water"),
                                      QStringLiteral("outer"),
                                      mCleanedPolygonCount, mRejectedPolygonCount))
            continue;
        InGameMapFeature* feature = new InGameMapFeature(&cell->inGameMap());
        feature->properties().set(QStringLiteral("water"), QStringLiteral("river"));
        feature->mGeometry.mType = QStringLiteral("Polygon");
        InGameMapCoordinates coords;
        for (auto& point : simple) {
            coords += InGameMapPoint(point.X, point.Y);
        }
        feature->mGeometry.mCoordinates += coords;

        if (poly->inner.empty() == false) {
            for (auto& hole : poly->inner) {
                simple = hole;
                ClipperLib::CleanPolygon(simple, 0.1);
                simplifyPolygon(simple, bounds.width());
                if (!validateGeneratedPolygon(simple, cell,
                                              QStringLiteral("water"),
                                              QStringLiteral("hole"),
                                              mCleanedPolygonCount,
                                              mRejectedPolygonCount))
                    continue;
                coords.clear();
                for (auto& point : simple) {
                    coords += InGameMapPoint(point.X, point.Y);
                }
                feature->mGeometry.mCoordinates += coords;
            }
        }

        mWorldDoc->addInGameMapFeature(cell, cell->inGameMap().features().size(), feature);
    }

    qDeleteAll(allPolygons);
    return true;
}

static void simplifyRoadPolygon(ClipperLib::Path &nodes, int cellSize,
                                double tolerance, int maximumPointSpacing)
{
    if (nodes.size() < 3)
        return;
    std::vector<DPPoint> points;
    const std::int64_t scale = 1000;
    const size_t minimumSpacing = size_t(qMax(1, maximumPointSpacing));
    size_t lastNecessary = size_t(-1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        const ClipperLib::IntPoint &node = nodes[i];
        bool necessary = i == 0 || i == nodes.size() - 1;
        if (node.X == 0 || node.X == cellSize || node.Y == 0 || node.Y == cellSize)
            necessary = true;
        if (i - lastNecessary >= minimumSpacing)
            necessary = true;
        if (necessary)
            lastNecessary = i;
        points.push_back({node.X * scale, node.Y * scale, necessary});
    }
    douglas_peucker(points, 0, points.size(), qMax(0.0, tolerance) * scale, 2, 0);
    nodes.clear();
    for (const DPPoint &point : points) {
        if (point.necessary)
            nodes.push_back({point.x / scale, point.y / scale});
    }
    ClipperLib::CleanPolygon(nodes, 0.1);
}
bool InGameMapFeatureGenerator::doRoads(WorldCell *worldCell, MapInfo *mapInfo)
{
    auto &features = worldCell->inGameMap().features();
    for (int i = features.size() - 1; i >= 0; --i) {
        InGameMapFeature *feature = features[i];
        bool generatedRoad = false;
        for (const InGameMapProperty &property : feature->properties()) {
            generatedRoad |= property.mKey == QStringLiteral("highway") ||
                    property.mKey == QStringLiteral("railway");
        }
        if (generatedRoad)
            mWorldDoc->removeInGameMapFeature(worldCell, feature->index());
    }
    DelayedMapLoader mapLoader;
    mapLoader.addMap(mapInfo);
    while (mapInfo->isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    MapComposite mapComposite(mapInfo);
    while (mapComposite.waitingForMapsToLoad() || mapLoader.isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    if (!mapLoader.errorString().isEmpty()) {
        mError = mapLoader.errorString();
        return false;
    }
    auto *layerGroup = mapComposite.layerGroupForLevel(0);
    if (!layerGroup)
        return true;
    layerGroup->prepareDrawing2();
    const QSize mapSize = mapInfo->map()->size();
    RoadMask primaryMask(mapSize);
    RoadMask secondaryMask(mapSize);
    RoadMask tertiaryMask(mapSize);
    RoadMask trailMask(mapSize);
    RoadMask railwayMask(mapSize);
    const QSet<int> trailIds = {64, 69, 70, 71, 80, 85, 86, 87};
    const Preferences *preferences = Preferences::instance();
    const QSet<QString> primaryTiles = featureTileSet(
                preferences->primaryRoadFeatureTiles());
    const QSet<QString> secondaryTiles = featureTileSet(
                preferences->secondaryRoadFeatureTiles());
    const QSet<QString> tertiaryTiles = featureTileSet(
                preferences->tertiaryRoadFeatureTiles());
    const bool generateTrails = preferences->generateTrailFeatures();
    OrderedCellsTemporaries orderedCellsTemporaries;
    QVector<const Tiled::Cell*> cells;
    cells.reserve(40);
    for (int y = 0; y < mapSize.height(); ++y) {
        for (int x = 0; x < mapSize.width(); ++x) {
            cells.clear();
            layerGroup->orderedCellsAt2(QPoint(x, y), orderedCellsTemporaries, cells);
            bool trailCandidate = false;
            bool water = false;
            for (const Tiled::Cell *cell : qAsConst(cells)) {
                if (cell->isEmpty())
                    continue;
                const QString tilesetName = cell->tile->tileset()->name();
                const int tileId = cell->tile->id();
                const QString tileName = featureTileName(cell->tile);
                if (primaryTiles.contains(tileName)) {
                    primaryMask.setValue(x, y);
                } else if (secondaryTiles.contains(tileName)) {
                    secondaryMask.setValue(x, y);
                } else if (tertiaryTiles.contains(tileName)) {
                    tertiaryMask.setValue(x, y);
                } else if (tilesetName == QStringLiteral("blends_natural_01") &&
                           trailIds.contains(tileId)) {
                    trailCandidate = true;
                } else if (tilesetName == QStringLiteral("industry_railroad_01")) {
                    railwayMask.setValue(x, y);
                }
                const QString waterProperty =
                        cell->tile->property(QStringLiteral("water"))
                        .trimmed().toLower();
                if ((tilesetName == QStringLiteral("blends_natural_02") &&
                     tileId < 8) ||
                        (!waterProperty.isEmpty() &&
                         waterProperty != QStringLiteral("false") &&
                         waterProperty != QStringLiteral("0"))) {
                    water = true;
                }
            }
            if (generateTrails && trailCandidate && !water)
                trailMask.setValue(x, y);
        }
    }
    ClipperLib::Clipper primary, secondary, tertiary, trail, railway;
    auto prepareMask = [&](RoadMask &mask, const RoadMaskRules &rules,
                           const QString &type, ClipperLib::Clipper &clipper) {
        const int originalTiles = mask.tileCount();
        const RoadMaskCleanup cleanup = normalizeRoadMask(mask, rules);
        if (!addMaskBoundariesToClipper(mask, clipper)) {
            mError = tr("Could not trace the %1 boundaries in cell %2,%3.")
                    .arg(type).arg(worldCell->x()).arg(worldCell->y());
            return false;
        }
        if (cleanup.bridgedTiles || cleanup.filledHoleTiles ||
                cleanup.removedComponents) {
            qInfo().noquote()
                    << QStringLiteral("Generate Road Features normalized mask: type=%1 cell=%2,%3 tiles=%4->%5 bridged=%6 filled-holes=%7 removed-components=%8 removed-tiles=%9")
                       .arg(type)
                       .arg(worldCell->x()).arg(worldCell->y())
                       .arg(originalTiles).arg(mask.tileCount())
                       .arg(cleanup.bridgedTiles)
                       .arg(cleanup.filledHoleTiles)
                       .arg(cleanup.removedComponents)
                       .arg(cleanup.removedTiles);
        }
        return true;
    };
    if (!prepareMask(primaryMask, highwayRoadMaskRules,
                     QStringLiteral("primary"), primary)
            || !prepareMask(secondaryMask, highwayRoadMaskRules,
                            QStringLiteral("secondary"), secondary)
            || !prepareMask(tertiaryMask, highwayRoadMaskRules,
                            QStringLiteral("tertiary"), tertiary)
            || (generateTrails && !prepareMask(trailMask, trailRoadMaskRules,
                                               QStringLiteral("trail"), trail))
            || !prepareMask(railwayMask, railwayRoadMaskRules,
                            QStringLiteral("railway"), railway))
        return false;
    auto createFeatures = [&](ClipperLib::Clipper &clipper,
                              const QString &key, const QString &value,
                              double simplificationTolerance,
                              int maximumPointSpacing) {
        ClipperLib::PolyTree tree;
        if (!clipper.Execute(ClipperLib::ctUnion, tree,
                             ClipperLib::pftNonZero, ClipperLib::pftNonZero))
            return;
        QHash<ClipperLib::PolyNode*, pzPolygon*> polygonForNode;
        QList<pzPolygon*> polygons;
        for (ClipperLib::PolyNode *node = tree.GetFirst(); node; node = node->GetNext()) {
            if (node->IsHole()) {
                if (pzPolygon *outer = polygonForNode.value(node->Parent, nullptr))
                    outer->inner.push_back(node->Contour);
            } else {
                pzPolygon *polygon = new pzPolygon;
                polygon->outer = node->Contour;
                polygonForNode.insert(node, polygon);
                polygons += polygon;
            }
        }
        for (pzPolygon *polygon : qAsConst(polygons)) {
            ClipperLib::Path outer = polygon->outer;
            ClipperLib::CleanPolygon(outer, 0.1);
            simplifyRoadPolygon(outer, mapSize.width(),
                                simplificationTolerance, maximumPointSpacing);
            if (!validateGeneratedPolygon(outer, worldCell,
                                          QStringLiteral("road"),
                                          QStringLiteral("outer"),
                                          mCleanedPolygonCount,
                                          mRejectedPolygonCount))
                continue;
            InGameMapFeature *feature = new InGameMapFeature(&worldCell->inGameMap());
            feature->properties().set(key, value);
            feature->mGeometry.mType = QStringLiteral("Polygon");
            InGameMapCoordinates coordinates;
            for (const ClipperLib::IntPoint &point : outer)
                coordinates += InGameMapPoint(point.X, point.Y);
            feature->mGeometry.mCoordinates += coordinates;
            for (ClipperLib::Path hole : polygon->inner) {
                ClipperLib::CleanPolygon(hole, 0.1);
                simplifyRoadPolygon(hole, mapSize.width(),
                                    simplificationTolerance, maximumPointSpacing);
                if (!validateGeneratedPolygon(hole, worldCell,
                                              QStringLiteral("road"),
                                              QStringLiteral("hole"),
                                              mCleanedPolygonCount,
                                              mRejectedPolygonCount))
                    continue;
                coordinates.clear();
                for (const ClipperLib::IntPoint &point : hole)
                    coordinates += InGameMapPoint(point.X, point.Y);
                feature->mGeometry.mCoordinates += coordinates;
            }
            mWorldDoc->addInGameMapFeature(worldCell,
                                           worldCell->inGameMap().features().size(), feature);
        }
        qDeleteAll(polygons);
    };
    createFeatures(primary, QStringLiteral("highway"), QStringLiteral("primary"),
                   preferences->roadSimplificationHighway(),
                   preferences->roadPointSpacingHighway());
    createFeatures(secondary, QStringLiteral("highway"), QStringLiteral("secondary"),
                   preferences->roadSimplificationHighway(),
                   preferences->roadPointSpacingHighway());
    createFeatures(tertiary, QStringLiteral("highway"), QStringLiteral("tertiary"),
                   preferences->roadSimplificationHighway(),
                   preferences->roadPointSpacingHighway());
    if (generateTrails) {
        createFeatures(trail, QStringLiteral("highway"), QStringLiteral("trail"),
                       preferences->roadSimplificationTrail(),
                       preferences->roadPointSpacingTrail());
    }
    createFeatures(railway, QStringLiteral("railway"), QStringLiteral("*"),
                   preferences->roadSimplificationRailway(),
                   preferences->roadPointSpacingRailway());
    return true;
}

bool InGameMapFeatureGenerator::doTrees(WorldCell *cell, MapInfo *mapInfo)
{
    // Remove all "natural=forest" features
    auto& features = cell->inGameMap().features();
    for (int i = features.size() - 1; i >= 0; i--) {
        auto* feature = features[i];
        if (feature->properties().contains(QStringLiteral("natural"), QStringLiteral("forest"))) {
            mWorldDoc->removeInGameMapFeature(cell, feature->index());
        }
    }

    const QSet<QString> treeTiles = featureTileSet(
                Preferences::instance()->treeFeatureTiles());
    if (treeTiles.isEmpty())
        return true;

    DelayedMapLoader mapLoader;
    mapLoader.addMap(mapInfo);

    while (mapInfo->isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    MapComposite staticMapComposite(mapInfo);
    MapComposite *mapComposite = &staticMapComposite;
    while (mapComposite->waitingForMapsToLoad() || mapLoader.isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    const QRect bounds(QPoint(), mapInfo->map()->size());

    auto* layerGroup = mapComposite->layerGroupForLevel(0);
    layerGroup->prepareDrawing2();

    static QVector<const Tiled::Cell*> cells(40);
    OrderedCellsTemporaries vars;

    auto isTreeAt = [&](int _x, int _y) {
        cells.resize(0);
        layerGroup->orderedCellsAt2({_x, _y}, vars, cells);
        for (auto* cell : std::as_const(cells)) {
            if (cell->isEmpty())
                continue;
            if (treeTiles.contains(featureTileName(cell->tile))) {
                return true;
            }
        }
        return false;
    };

    QVector<bool> trees(bounds.width() * bounds.height());
    for (int y = 0; y < bounds.height(); y++) {
        for (int x = 0; x < bounds.width(); x++) {
            trees[x + y * bounds.width()] = isTreeAt(x, y);
        }
    }

    auto getTreesNear = [&](int _x, int _y) {
        QRect box = { _x, _y, 1, 1 };
        for (int y = _y - 4; y < _y + 4; y++) {
            for (int x = _x - 4; x < _x + 4; x++) {
                if (x == _x && y == _y)
                    continue;
                if (bounds.contains(x, y)
                        && trees[x + y * bounds.width()]) {
                    box |= { x, y, 1, 1 };
                }
            }
        }
        return box;

    };

    ClipperLib::Clipper clipper;
    ClipperLib::Path path;

    QHash<quint64, QRect> treeBoxes;
    for (int y = 0; y < bounds.height(); y++) {
        for (int x = 0; x < bounds.width(); x++) {
            if (trees[x + y * bounds.width()]) {
                QRect box = getTreesNear(x, y);
                if (box.size() != QSize(1, 1)) {
                    box.adjust(-1, -1, 1, 1);
                    box &= bounds;
                    const quint64 key = quint64(box.x()) |
                            (quint64(box.y()) << 10) |
                            (quint64(box.width()) << 20) |
                            (quint64(box.height()) << 30);
                    treeBoxes.insert(key, box);
                }
            }
        }
    }
    for (const QRect &box : treeBoxes) {
        path.clear();
        path << ClipperLib::IntPoint(box.left(), box.top());
        path << ClipperLib::IntPoint(box.right() + 1, box.top());
        path << ClipperLib::IntPoint(box.right() + 1, box.bottom() + 1);
        path << ClipperLib::IntPoint(box.left(), box.bottom() + 1);
        clipper.AddPath(path, ClipperLib::ptSubject, true);
    }

    ClipperLib::PolyTree polyTree;
    if (clipper.Execute(ClipperLib::ctDifference, polyTree, ClipperLib::PolyFillType::pftPositive) == false) {
        return true;
    }

    std::map<ClipperLib::PolyNode*,pzPolygon*> polyMap;
    std::vector<pzPolygon*> allPolygons;
    for (ClipperLib::PolyNode* node = polyTree.GetFirst(); node != nullptr; node = node->GetNext()) {
        if (node->IsHole()) {
            pzPolygon *outer = polyMap[node->Parent];
            outer->inner.push_back(node->Contour);
        } else {
            pzPolygon* poly = new pzPolygon();
            poly->outer = node->Contour;
            polyMap[node] = poly;
            allPolygons.push_back(poly);
        }
    }


    for (pzPolygon *poly : allPolygons) {
        ClipperLib::Path simple = poly->outer;
        simplifyPolygon(simple, bounds.width());
        if (!validateGeneratedPolygon(simple, cell, QStringLiteral("forest"),
                                      QStringLiteral("outer"),
                                      mCleanedPolygonCount, mRejectedPolygonCount)) {
            continue;
        }

        InGameMapFeature* feature = new InGameMapFeature(&cell->inGameMap());
        feature->properties().set(QStringLiteral("natural"), QStringLiteral("forest"));
        feature->mGeometry.mType = QStringLiteral("Polygon");
        InGameMapCoordinates coords;
        for (auto& point : simple) {
            coords += InGameMapPoint(point.X, point.Y);
        }
        feature->mGeometry.mCoordinates += coords;

        if (poly->inner.empty() == false) {
            for (auto& hole : poly->inner) {
                simple = hole;
                simplifyPolygon(simple, bounds.width());
                if (!validateGeneratedPolygon(simple, cell,
                                              QStringLiteral("forest"),
                                              QStringLiteral("hole"),
                                              mCleanedPolygonCount,
                                              mRejectedPolygonCount)) {
                    continue;
                }
                coords.clear();
                for (auto& point : simple) {
                    coords += InGameMapPoint(point.X, point.Y);
                }
                feature->mGeometry.mCoordinates += coords;
            }
        }

        mWorldDoc->addInGameMapFeature(cell, cell->inGameMap().features().size(), feature);

    }

    qDeleteAll(allPolygons);
    return true;
}
