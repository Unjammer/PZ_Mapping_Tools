#include "directmappreview.h"

#include "BuildingEditor/buildingfloor.h"
#include "lotfilesmanager256.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "tile.h"
#include "tiledeffile.h"
#include "tileset.h"
#include "tilesetmanager.h"
#include "world.h"
#include "worldconstants.h"
#include "worlddocument.h"
#include "worldreader.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSet>
#include <QThread>
#include <QtMath>

#include <limits>
#include <utility>

namespace {

constexpr int maxGameCell = 1023;
constexpr int gameSquareLimit = (maxGameCell + 1) * CELL_SIZE_256;

QString identifyCell(const WorldCell &cell, const QString &details)
{
    return QStringLiteral("Project cell (%1, %2): %3").arg(cell.x()).arg(cell.y()).arg(details);
}

int firstCellInRange(qint64 square, int totalCells)
{
    if (square < 0)
        return 0;
    return int(qMin(square / CELL_SIZE_256, qint64(totalCells)));
}

int lastCellInRange(qint64 square, int totalCells)
{
    if (square < 0)
        return -1;
    return int(qMin(square / CELL_SIZE_256, qint64(totalCells - 1)));
}

bool indexPlacements(World &world, const QSet<WorldCell *> &selection,
                     QHash<WorldCell *, WorldCellLotList> &placements, QString &error)
{
    const QPoint origin = world.getGenerateLotsSettings().worldOrigin;
    const QRect cellBounds(0, 0, CELL_SIZE_256, CELL_SIZE_256);
    for (WorldCell *cell : world.cells()) {
        const bool ownerSelected = selection.contains(cell);
        const bool hasMap = !cell->mapFilePath().isEmpty();
        for (WorldCellLot *lot : cell->lots()) {
            if (lot->width() <= 0 || lot->height() <= 0) {
                if (!ownerSelected)
                    continue;
                error = identifyCell(*cell, QStringLiteral("The placement for %1 has invalid dimensions.")
                                     .arg(QFileInfo(lot->mapName()).fileName()));
                return false;
            }
            const bool insideOwner = cellBounds.contains(lot->bounds());
            if (!ownerSelected && insideOwner)
                continue;

            bool used = ownerSelected;
            if (!hasMap || !insideOwner) {
                const qint64 left = qint64(cell->x()) * CELL_SIZE_256 + lot->x();
                const qint64 top = qint64(cell->y()) * CELL_SIZE_256 + lot->y();
                const qint64 right = left + lot->width() - 1;
                const qint64 bottom = top + lot->height() - 1;
                const int firstX = firstCellInRange(left, world.width());
                const int lastX = lastCellInRange(right, world.width());
                const int firstY = firstCellInRange(top, world.height());
                const int lastY = lastCellInRange(bottom, world.height());
                for (int cellY = firstY; cellY <= lastY; ++cellY) {
                    for (int cellX = firstX; cellX <= lastX; ++cellX) {
                        WorldCell *target = world.cellAt(cellX, cellY);
                        if ((target != cell || !hasMap) && selection.contains(target)) {
                            placements[target].append(lot);
                            used = true;
                        }
                    }
                }
                if (!used)
                    continue;
                const qint64 worldLeft = qint64(origin.x()) * CELL_SIZE_256 + left;
                const qint64 worldTop = qint64(origin.y()) * CELL_SIZE_256 + top;
                if (worldLeft < std::numeric_limits<int>::min()
                        || worldTop < std::numeric_limits<int>::min()
                        || worldLeft + lot->width() > std::numeric_limits<int>::max()
                        || worldTop + lot->height() > std::numeric_limits<int>::max()) {
                    error = identifyCell(*cell, QStringLiteral("A placed building extends beyond the supported coordinate range."));
                    return false;
                }
            }
            if (!QFileInfo(lot->mapName()).isFile()) {
                error = identifyCell(*cell, QStringLiteral("The placed building or map file is missing: %1")
                                     .arg(lot->mapName()));
                return false;
            }
        }
    }
    return true;
}

struct PreviewTile
{
    int index = -1;
    bool floor = false;
    bool blocked = false;
    bool unknown = false;
};

struct StartSquare
{
    QPoint pos;
    int level = 0;
    int score = std::numeric_limits<int>::max();

    void consider(int x, int y, int z)
    {
        const int dx = x - CELL_SIZE_256 / 2;
        const int dy = y - CELL_SIZE_256 / 2;
        const int candidateScore = qAbs(z) * CELL_SIZE_256 * CELL_SIZE_256 + dx * dx + dy * dy;
        if (candidateScore < score) {
            pos = QPoint(x, y);
            level = z;
            score = candidateScore;
        }
    }

    QJsonArray coordinates(const QPoint &origin) const
    {
        if (score == std::numeric_limits<int>::max())
            return {};
        return QJsonArray{origin.x() + pos.x(), origin.y() + pos.y(), level};
    }
};

}

bool DirectMapPreview::read(const QString &projectPath, QVector<QByteArray> &cells, QJsonArray &spawn,
                            QString &error, const QVector<QPoint> &selection)
{
    cells.clear();
    spawn = QJsonArray();
    error.clear();
    WorldReader reader;
    World *world = reader.readWorld(projectPath);
    if (!world) {
        error = reader.errorString();
        return false;
    }
    WorldDocument document(world, projectPath);
    if (world->gridFormat() != WorldGridFormat::Native256) {
        error = QStringLiteral("Direct map preview requires a Native256 project.");
        return false;
    }

    QVector<WorldCell *> previewCells;
    QSet<WorldCell *> selectedCells;
    if (selection.isEmpty()) {
        previewCells = world->cells();
        selectedCells = QSet<WorldCell *>(previewCells.cbegin(), previewCells.cend());
    } else {
        for (const QPoint &position : selection) {
            WorldCell *cell = world->cellAt(position);
            if (!cell) {
                error = QStringLiteral("The selected cell (%1, %2) is outside this project.")
                        .arg(position.x()).arg(position.y());
                return false;
            }
            if (!selectedCells.contains(cell)) {
                selectedCells.insert(cell);
                previewCells.append(cell);
            }
        }
    }
    QHash<WorldCell *, WorldCellLotList> placements;
    if (!indexPlacements(*world, selectedCells, placements, error))
        return false;

    const QPoint origin = world->getGenerateLotsSettings().worldOrigin;
    Tiled::Internal::TileDefWatcher *tileDefinitions = BuildingEditor::getTileDefWatcher();
    tileDefinitions->check();
    QVector<QByteArray> loadedCells;
    QJsonArray authoredStart;
    QJsonArray fallbackStart;
    for (WorldCell *cell : previewCells) {
        WorldCellLotList overlappingLots = placements.take(cell);
        const QString &mapPath = cell->mapFilePath();
        if (mapPath.isEmpty() && overlappingLots.isEmpty())
            continue;
        if (!mapPath.isEmpty()
                && (!mapPath.endsWith(QLatin1String(".tmx"), Qt::CaseInsensitive)
                    || !QFileInfo(mapPath).isFile())) {
            error = identifyCell(*cell, QStringLiteral("Open or assign its saved TileZed map (.tmx). The current file is %1.")
                                 .arg(mapPath));
            return false;
        }
        const qint64 cellX = qint64(origin.x()) + cell->x();
        const qint64 cellY = qint64(origin.y()) + cell->y();
        if (cellX < 0 || cellX > maxGameCell || cellY < 0 || cellY > maxGameCell) {
            error = identifyCell(*cell, QStringLiteral("Its game cell (%1, %2) is out of range. Use a world origin that keeps cell coordinates from 0 to %3.")
                                 .arg(cellX).arg(cellY).arg(maxGameCell));
            return false;
        }
        QByteArray cellData;
        QJsonArray authored;
        QJsonArray fallback;
        if (!readCell(document, *cell, QPoint(int(cellX), int(cellY)), overlappingLots,
                      cellData, authored, fallback, error)) {
            error = identifyCell(*cell, error);
            return false;
        }
        if (cellData.isEmpty())
            continue;
        loadedCells.append(std::move(cellData));
        if (selection.isEmpty() || (authoredStart.isEmpty() && fallbackStart.isEmpty())) {
            if (authoredStart.isEmpty())
                authoredStart = authored;
            if (fallbackStart.isEmpty())
                fallbackStart = fallback;
        }
    }
    const QJsonArray chosenSpawn = authoredStart.isEmpty() ? fallbackStart : authoredStart;
    if (chosenSpawn.isEmpty()) {
        const QString noStart = QStringLiteral("No walkable start square was found. Assign a TileZed map (.tmx) or place a building with a floor.");
        error = previewCells.size() == 1 ? identifyCell(*previewCells.first(), noStart) : noStart;
        return false;
    }
    cells = std::move(loadedCells);
    spawn = chosenSpawn;
    return true;
}

bool DirectMapPreview::readCell(WorldDocument &document, WorldCell &cell, const QPoint &worldCell,
                                WorldCellLotList &overlappingLots, QByteArray &payload,
                                QJsonArray &authoredSpawn, QJsonArray &fallbackSpawn, QString &error)
{
    const QPoint tileOrigin = worldCell * CELL_SIZE_256;
    CombinedCellMaps combined;
    if (!combined.startLoading(&document, worldCell.x(), worldCell.y(), overlappingLots)) {
        error = combined.mError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    int loadingState;
    while ((loadingState = combined.checkLoading(&document)) == 0) {
        if (timer.hasExpired(120000)) {
            error = QStringLiteral("Timed out loading the preview TMX or TBX.");
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(1);
    }
    if (loadingState < 0) {
        error = combined.mError;
        return false;
    }
    MapComposite *composite = combined.mMapComposite;
    for (MapComposite *map : composite->maps()) {
        const QString path = map->mapInfo()->path();
        if (map->map()->hasUsedMissingTilesets()) {
            error = QStringLiteral("The preview source uses missing tilesets: %1").arg(path);
            return false;
        }
    }

    LotFilesManager256 manager;
    manager.mWorldDoc = &document;
    LotFilesWorker256 worker(&manager, nullptr);
    const auto cleanup = qScopeGuard([&]() {
        qDeleteAll(manager.mJumboZoneList);
        worker.clearRemovedBuildingsList();
        qDeleteAll(worker.mRoomRects);
        qDeleteAll(worker.roomList);
        qDeleteAll(worker.buildingList);
    });
    if (!worker.mPartialChunks.load(cell.mapFilePath(), &error))
        return false;
    if (!worker.processObjectGroups(combined, composite)
            || !worker.groupRoomDefinitions(combined)) {
        error = worker.mError;
        return false;
    }
    QJsonArray buildings;
    const QRect gameBounds(0, 0, gameSquareLimit, gameSquareLimit);
    int minLevel = 0;
    int maxLevel = 0;
    for (const LotFile::Building *building : worker.buildingList) {
        QJsonArray rooms;
        for (const LotFile::Room *room : building->RoomList) {
            QJsonArray rectangles;
            for (const LotFile::RoomRect *rect : room->rects) {
                if (!gameBounds.contains(rect->bounds().translated(tileOrigin))
                        || room->floor < MIN_WORLD_LEVEL || room->floor > MAX_WORLD_LEVEL) {
                    error = QStringLiteral("The room '%1' lies outside the game's coordinate or floor range.")
                            .arg(room->name);
                    return false;
                }
                rectangles.append(QJsonArray{tileOrigin.x() + rect->x, tileOrigin.y() + rect->y,
                                             rect->w, rect->h});
            }
            rooms.append(QJsonObject{{QStringLiteral("name"), room->name},
                                     {QStringLiteral("z"), room->floor},
                                     {QStringLiteral("rects"), rectangles}});
            minLevel = qMin(minLevel, room->floor);
            maxLevel = qMax(maxLevel, room->floor);
        }
        buildings.append(QJsonObject{{QStringLiteral("rooms"), rooms}});
    }
    const auto *tileDefinitions = BuildingEditor::getTileDefWatcher();
    const auto *missingTile = Tiled::Internal::TilesetManager::instance()->missingTile();
    QHash<const Tiled::Tile *, PreviewTile> tileProperties;
    QHash<QString, int> indicesByName;
    QJsonArray tiles;
    QJsonArray squares;
    QVector<const Tiled::Cell *> orderedCells;
    OrderedCellsTemporaries temporaries;
    StartSquare knownFloor;
    StartSquare unknownFloor;
    for (CompositeLayerGroup *group : composite->layerGroups()) {
        const int level = group->level();
        if (level < MIN_WORLD_LEVEL || level > MAX_WORLD_LEVEL) {
            error = QStringLiteral("A map layer lies outside the game level range: %1").arg(level);
            return false;
        }
        group->prepareDrawing2();
        for (int y = 0; y < CELL_SIZE_256; ++y) {
            for (int x = 0; x < CELL_SIZE_256; ++x) {
                if (!worker.partialSquareSelected(x, y))
                    continue;
                orderedCells.clear();
                group->orderedCellsAt2(QPoint(x, y), temporaries, orderedCells);
                if (orderedCells.isEmpty())
                    continue;
                QJsonArray square{x, y, level};
                bool floor = false;
                bool blocked = false;
                bool unknown = false;
                for (const Tiled::Cell *entry : qAsConst(orderedCells)) {
                    const Tiled::Tile *tile = entry->tile;
                    if (!tile || tile == missingTile) {
                        error = QStringLiteral("A placed tile is missing at %1,%2,%3.").arg(x).arg(y).arg(level);
                        return false;
                    }
                    auto properties = tileProperties.constFind(tile);
                    if (properties == tileProperties.cend()) {
                        const Tiled::Tileset *tileset = tile->tileset();
                        const QString imageName = QFileInfo(tileset->imageSource()).completeBaseName();
                        const QString sheet = imageName.isEmpty() ? tileset->name() : imageName;
                        const QString name = QStringLiteral("%1_%2").arg(sheet).arg(tile->id());
                        PreviewTile info;
                        info.index = indicesByName.value(name, -1);
                        if (info.index < 0) {
                            info.index = tiles.size();
                            tiles.append(name);
                            indicesByName.insert(name, info.index);
                        }
                        if (const TileDefTile *definition = tileDefinitions->tile(sheet, tile->id())) {
                            const auto &props = definition->mProperties;
                            info.floor = props.contains(QLatin1String("solidfloor"));
                            info.blocked = props.contains(QLatin1String("water"))
                                    || props.contains(QLatin1String("solid"))
                                    || props.contains(QLatin1String("solidtrans"))
                                    || props.contains(QLatin1String("tree"));
                        } else {
                            info.unknown = true;
                        }
                        properties = tileProperties.insert(tile, info);
                    }
                    square.append(properties->index);
                    floor |= properties->floor;
                    blocked |= properties->blocked;
                    unknown |= properties->unknown;
                }
                squares.append(square);
                minLevel = qMin(minLevel, level);
                maxLevel = qMax(maxLevel, level);
                if (!blocked && floor)
                    knownFloor.consider(x, y, level);
                else if (!blocked && unknown && level == 0)
                    unknownFloor.consider(x, y, level);
            }
        }
    }
    if (squares.isEmpty())
        return true;

    for (WorldCellObject *object : cell.objects()) {
        const qreal x = object->x();
        const qreal y = object->y();
        const int level = object->level();
        if (!object->isSpawnPoint() || !object->isRectangle()
                || object->width() <= 0 || object->height() <= 0
                || level < minLevel || level > maxLevel
                || !(x >= 0 && x < CELL_SIZE_256 && y >= 0 && y < CELL_SIZE_256)
                || x != qFloor(x) || y != qFloor(y)) {
            continue;
        }
        if (!worker.partialSquareSelected(int(x), int(y)))
            continue;
        CompositeLayerGroup *group = composite->tileLayersForLevel(level);
        orderedCells.clear();
        if (!group || !group->orderedCellsAt2(QPoint(int(x), int(y)), temporaries, orderedCells))
            continue;
        bool floor = false;
        bool blocked = false;
        for (const Tiled::Cell *entry : qAsConst(orderedCells)) {
            const PreviewTile tile = tileProperties.value(entry->tile);
            floor |= tile.floor;
            blocked |= tile.blocked;
        }
        if (!floor || blocked)
            continue;
        authoredSpawn = QJsonArray{tileOrigin.x() + int(x), tileOrigin.y() + int(y), level};
        break;
    }
    fallbackSpawn = knownFloor.coordinates(tileOrigin);
    if (fallbackSpawn.isEmpty())
        fallbackSpawn = unknownFloor.coordinates(tileOrigin);

    payload = QJsonDocument(QJsonObject{
        {QStringLiteral("cellX"), worldCell.x()},
        {QStringLiteral("cellY"), worldCell.y()},
        {QStringLiteral("minLevel"), minLevel},
        {QStringLiteral("maxLevel"), maxLevel},
        {QStringLiteral("tiles"), tiles},
        {QStringLiteral("squares"), squares},
        {QStringLiteral("buildings"), buildings}
    }).toJson(QJsonDocument::Compact);
    if (payload.isEmpty()) {
        error = QStringLiteral("The cell contains too much data to prepare a playtest.");
        return false;
    }
    return true;
}
