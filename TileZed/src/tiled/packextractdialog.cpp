#include "packextractdialog.h"
#include "ui_packextractdialog.h"

#include "texturepackfile.h"
#include "zprogress.h"

#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/furnituregroups.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSet>
#include <QVector>

#include <algorithm>
namespace {
enum ExtractMode {
    IndividualImages,
    ReconstructedSheets,
    AtlasPages,
    AssembledObjects
};
enum OutputLayout {
    FlatLayout,
    PageDirectories,
    TilesetDirectories
};
enum ConflictPolicy {
    RenameExisting,
    SkipExisting,
    OverwriteExisting
};
enum GeometryMode {
    AutomaticGeometry,
    OneXGeometry,
    TwoXGeometry,
    CustomGeometry
};
struct ObjectTilePlacement
{
    QString tileName;
    QString tileKey;
    int x = 0;
    int y = 0;
};
struct ObjectExportDefinition
{
    QString groupName;
    int variant = 0;
    QString orientation;
    QList<ObjectTilePlacement> placements;
};
struct ResolvedObjectTile
{
    ObjectTilePlacement placement;
    QImage image;
};
struct ExtractOptions
{
    ExtractMode mode = IndividualImages;
    OutputLayout layout = FlatLayout;
    ConflictPolicy conflict = RenameExisting;
    GeometryMode geometry = AutomaticGeometry;
    int customWidth = 128;
    int customHeight = 256;
    int columns = 8;
    bool writeManifest = true;
    bool repairOrphanPixels = false;
    bool excludeMultiTileObjectTiles = false;
    QString outputDirectory;
    QList<QPair<int, int>> textures;
    QList<ObjectExportDefinition> objectDefinitions;
};
struct ExtractResult
{
    int selectedTextures = 0;
    int filesWritten = 0;
    int skippedFiles = 0;
    int renamedFiles = 0;
    int unparseableTextures = 0;
    int assetFilesWritten = 0;
    int orphanPixelsRemoved = 0;
    int assembledObjects = 0;
    int incompleteObjects = 0;
    int ambiguousObjectTiles = 0;
    int excludedObjectTiles = 0;
    QStringList outputFiles;
    QJsonArray objectAssemblies;
};
struct DecodedImageReleaseGuard
{
    const PackFile &pack;
    ~DecodedImageReleaseGuard()
    {
        for (const PackPage &page : pack.pages())
            PackFile::releaseDecodedImage(page);
    }
};
QString safeFileName(QString value)
{
    value = value.trimmed();
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        value.replace(character, QLatin1Char('_'));
    for (int index = 0; index < value.size(); ++index) {
        if (value.at(index).unicode() < 32)
            value[index] = QLatin1Char('_');
    }
    while (value.endsWith(QLatin1Char('.')) ||
           value.endsWith(QLatin1Char(' '))) {
        value.chop(1);
    }
    if (value.isEmpty())
        value = QStringLiteral("unnamed");
    static const QSet<QString> reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"),
        QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"),
        QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"),
        QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    if (reserved.contains(value.toUpper()))
        value.prepend(QLatin1Char('_'));
    return value;
}
QString destinationPath(const QDir &directory, const QString &baseName,
                        const QString &extension,
                        ConflictPolicy policy, ExtractResult *result)
{
    const QString safeBase = safeFileName(baseName);
    QString path = directory.filePath(safeBase + extension);
    if (!QFileInfo::exists(path) || policy == OverwriteExisting)
        return path;
    if (policy == SkipExisting) {
        ++result->skippedFiles;
        return QString();
    }
    int suffix = 2;
    do {
        path = directory.filePath(
                    QStringLiteral("%1 (%2)%3")
                    .arg(safeBase).arg(suffix++).arg(extension));
    } while (QFileInfo::exists(path));
    ++result->renamedFiles;
    return path;
}
bool saveImageAtomically(const QImage &image, const QString &filePath,
                         QString *errorString)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) ||
            !image.save(&buffer, "PNG", -1)) {
        *errorString = QObject::tr("Could not encode %1 as PNG.")
                .arg(QDir::toNativeSeparators(filePath));
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) ||
            file.write(encoded) != encoded.size() ||
            !file.commit()) {
        *errorString = QObject::tr("Could not write %1.")
                .arg(QDir::toNativeSeparators(filePath));
        return false;
    }
    return true;
}
bool ensureDirectory(const QString &path, QDir *directory,
                     QString *errorString)
{
    if (!QDir().mkpath(path)) {
        *errorString = QObject::tr("Could not create %1.")
                .arg(QDir::toNativeSeparators(path));
        return false;
    }
    *directory = QDir(path);
    return true;
}
QString tilesetNameFor(const PackSubTexInfo &texture, int *tileIndex)
{
    QString tilesetName;
    int parsedIndex = -1;
    if (!BuildingEditor::BuildingTilesMgr::parseTileName(
            texture.name, tilesetName, parsedIndex)) {
        if (tileIndex)
            *tileIndex = -1;
        return QString();
    }
    if (tileIndex)
        *tileIndex = parsedIndex;
    return tilesetName;
}
QString canonicalTileKey(const QString &tileName)
{
    QString tilesetName;
    int tileIndex = -1;
    if (!BuildingEditor::BuildingTilesMgr::parseTileName(
            tileName, tilesetName, tileIndex)) {
        return QString();
    }
    return QStringLiteral("%1/%2")
            .arg(tilesetName.toCaseFolded())
            .arg(tileIndex);
}
int removeOrphanPixels(QImage *image)
{
    if (!image || image->isNull())
        return 0;
    const QImage source = image->convertToFormat(QImage::Format_ARGB32);
    QImage repaired = source;
    int removed = 0;
    for (int y = 0; y < source.height(); ++y) {
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(
                    source.constScanLine(y));
        QRgb *targetLine = reinterpret_cast<QRgb *>(
                    repaired.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const int alpha = qAlpha(sourceLine[x]);
            if (alpha == 0)
                continue;
            int visibleNeighbors = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int neighborX = x + dx;
                    const int neighborY = y + dy;
                    if (neighborX < 0 || neighborX >= source.width() ||
                            neighborY < 0 || neighborY >= source.height()) {
                        continue;
                    }
                    const QRgb neighbor = reinterpret_cast<const QRgb *>(
                                source.constScanLine(neighborY))[neighborX];
                    if (qAlpha(neighbor) >= 5)
                        ++visibleNeighbors;
                }
            }
            if (alpha < 5 || visibleNeighbors <= 2) {
                targetLine[x] = qRgba(0, 0, 0, 0);
                ++removed;
            }
        }
    }
    *image = repaired;
    return removed;
}
QImage preparedTexture(const PackPage &page,
                       const PackSubTexInfo &texture,
                       bool repairOrphanPixels,
                       int *removedPixels)
{
    QImage image = PackFile::extractTexture(page, texture);
    const int removed = repairOrphanPixels
            ? removeOrphanPixels(&image) : 0;
    if (removedPixels)
        *removedPixels = removed;
    return image;
}
QList<ObjectExportDefinition> furnitureObjectDefinitions()
{
    QList<ObjectExportDefinition> definitions;
    const QList<BuildingEditor::FurnitureGroup *> groups =
            BuildingEditor::FurnitureGroups::instance()->groups();
    for (BuildingEditor::FurnitureGroup *group : groups) {
        if (!group)
            continue;
        for (int variantIndex = 0;
             variantIndex < group->mTiles.size(); ++variantIndex) {
            BuildingEditor::FurnitureTiles *variant =
                    group->mTiles.at(variantIndex);
            if (!variant)
                continue;
            for (BuildingEditor::FurnitureTile *orientation :
                 variant->tiles()) {
                if (!orientation || orientation->isEmpty())
                    continue;
                ObjectExportDefinition definition;
                definition.groupName = group->mLabel;
                definition.variant = variantIndex + 1;
                definition.orientation = orientation->orientToString();
                for (int y = 0; y < orientation->height(); ++y) {
                    for (int x = 0; x < orientation->width(); ++x) {
                        BuildingEditor::BuildingTile *tile =
                                orientation->tile(x, y);
                        if (!tile || tile->isNone())
                            continue;
                        ObjectTilePlacement placement;
                        placement.tileName = tile->name();
                        placement.tileKey = canonicalTileKey(
                                    placement.tileName);
                        placement.x = x;
                        placement.y = y;
                        if (!placement.tileKey.isEmpty())
                            definition.placements += placement;
                    }
                }
                if (definition.placements.size() >= 2)
                    definitions += definition;
            }
        }
    }
    return definitions;
}
QSet<QString> multiTileObjectKeys(
        const QList<ObjectExportDefinition> &requestedDefinitions = {})
{
    QSet<QString> keys;
    const QList<ObjectExportDefinition> definitions =
            requestedDefinitions.isEmpty()
            ? furnitureObjectDefinitions() : requestedDefinitions;
    for (const ObjectExportDefinition &definition : definitions) {
        for (const ObjectTilePlacement &placement : definition.placements)
            keys += placement.tileKey;
    }
    return keys;
}
QImage assembleObjectImage(QList<ResolvedObjectTile> tiles,
                           QString *errorString)
{
    if (tiles.isEmpty())
        return QImage();
    int tileWidth = 0;
    for (const ResolvedObjectTile &tile : std::as_const(tiles))
        tileWidth = qMax(tileWidth, tile.image.width());
    if (tileWidth <= 0) {
        *errorString = QObject::tr("The object has invalid tile geometry.");
        return QImage();
    }
    const int halfWidth = qMax(1, tileWidth / 2);
    const int quarterWidth = qMax(1, tileWidth / 4);
    QRect bounds;
    for (const ResolvedObjectTile &tile : std::as_const(tiles)) {
        const int left = (tile.placement.x - tile.placement.y) * halfWidth;
        const int baseline =
                (tile.placement.x + tile.placement.y) * quarterWidth;
        const QRect imageRect(left, baseline - tile.image.height(),
                              tile.image.width(), tile.image.height());
        bounds = bounds.isNull() ? imageRect : bounds.united(imageRect);
    }
    const int padding = 4;
    const qint64 outputWidth = qint64(bounds.width()) + padding * 2;
    const qint64 outputHeight = qint64(bounds.height()) + padding * 2;
    if (outputWidth <= 0 || outputHeight <= 0 ||
            outputWidth > 32768 || outputHeight > 32768 ||
            outputWidth * outputHeight > 64LL * 1024 * 1024) {
        *errorString = QObject::tr(
                    "The assembled object would be %1x%2 pixels.")
                .arg(outputWidth).arg(outputHeight);
        return QImage();
    }
    std::sort(tiles.begin(), tiles.end(),
              [](const ResolvedObjectTile &left,
                 const ResolvedObjectTile &right) {
        const int leftDepth = left.placement.x + left.placement.y;
        const int rightDepth = right.placement.x + right.placement.y;
        if (leftDepth != rightDepth)
            return leftDepth < rightDepth;
        return left.placement.x < right.placement.x;
    });
    QImage result(int(outputWidth), int(outputHeight),
                  QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const ResolvedObjectTile &tile : std::as_const(tiles)) {
        const int left = (tile.placement.x - tile.placement.y) * halfWidth;
        const int baseline =
                (tile.placement.x + tile.placement.y) * quarterWidth;
        painter.drawImage(
                    QPoint(left - bounds.left() + padding,
                           baseline - tile.image.height() -
                           bounds.top() + padding),
                    tile.image);
    }
    painter.end();
    QRect visibleBounds;
    for (int y = 0; y < result.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(
                    result.constScanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            const QRect pixelRect(x, y, 1, 1);
            visibleBounds = visibleBounds.isNull()
                    ? pixelRect : visibleBounds.united(pixelRect);
        }
    }
    if (visibleBounds.isNull())
        return result;
    visibleBounds.adjust(-padding, -padding, padding, padding);
    visibleBounds = visibleBounds.intersected(result.rect());
    return result.copy(visibleBounds);
}
bool validSelection(const PackFile &pack, int pageIndex, int textureIndex)
{
    return pageIndex >= 0 && pageIndex < pack.pages().size() &&
            textureIndex >= 0 &&
            textureIndex < pack.pages().at(pageIndex).mInfo.size();
}
QJsonObject sourceJson(const PackPage &page,
                       const PackSubTexInfo &texture)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), texture.name);
    object.insert(QStringLiteral("page"), page.name);
    object.insert(QStringLiteral("pageHasAlpha"), page.mask);
    object.insert(QStringLiteral("x"), texture.x);
    object.insert(QStringLiteral("y"), texture.y);
    object.insert(QStringLiteral("width"), texture.w);
    object.insert(QStringLiteral("height"), texture.h);
    object.insert(QStringLiteral("offsetX"), texture.ox);
    object.insert(QStringLiteral("offsetY"), texture.oy);
    object.insert(QStringLiteral("canvasWidth"), texture.fx);
    object.insert(QStringLiteral("canvasHeight"), texture.fy);
    object.insert(QStringLiteral("metadataSha256"),
                  PackFile::sha256Text(
                      PackFile::metadataSha256(page, texture)));
    return object;
}
bool performExtraction(const PackFile &pack,
                       const ExtractOptions &options,
                       ExtractResult *result,
                       QString *errorString,
                       PROGRESS *progress = nullptr)
{
    const DecodedImageReleaseGuard decodedImageGuard{pack};
    result->selectedTextures = options.textures.size();
    if (progress) {
        progress->update(
                    QObject::tr("Validating %1 selected textures...")
                    .arg(options.textures.size()));
    }
    QDir output;
    if (!ensureDirectory(options.outputDirectory, &output, errorString))
        return false;
    QJsonArray manifestSources;
    QVector<int> remainingPageSelections(pack.pages().size(), 0);
    for (const QPair<int, int> &selection : options.textures) {
        if (!validSelection(pack, selection.first, selection.second)) {
            *errorString = QObject::tr("The texture selection is invalid.");
            return false;
        }
        ++remainingPageSelections[selection.first];
        const PackPage &page = pack.pages().at(selection.first);
        const PackSubTexInfo &texture =
                page.mInfo.at(selection.second);
        manifestSources.append(sourceJson(page, texture));
    }
    const auto consumePageSelection = [&pack, &remainingPageSelections](
            int pageIndex) {
        if (--remainingPageSelections[pageIndex] == 0)
            PackFile::releaseDecodedImage(pack.pages().at(pageIndex));
    };
    if (options.mode == IndividualImages) {
        const QSet<QString> objectTileKeys =
                options.excludeMultiTileObjectTiles
                ? multiTileObjectKeys(options.objectDefinitions)
                : QSet<QString>();
        int writtenIndex = 0;
        for (const QPair<int, int> &selection : options.textures) {
            const PackPage &page = pack.pages().at(selection.first);
            const PackSubTexInfo &texture =
                    page.mInfo.at(selection.second);
            if (progress && writtenIndex % 16 == 0) {
                progress->update(
                            QObject::tr(
                                "Extracting texture %1 of %2\n%3")
                            .arg(writtenIndex + 1)
                            .arg(options.textures.size())
                            .arg(texture.name));
            }
            ++writtenIndex;
            int tileIndex = -1;
            const QString tileset =
                    tilesetNameFor(texture, &tileIndex);
            if (!objectTileKeys.isEmpty()
                    && objectTileKeys.contains(
                        canonicalTileKey(texture.name))) {
                ++result->excludedObjectTiles;
                consumePageSelection(selection.first);
                continue;
            }
            QDir destination = output;
            if (options.layout == PageDirectories) {
                if (!ensureDirectory(
                        output.filePath(safeFileName(page.name)),
                        &destination, errorString)) {
                    return false;
                }
            } else if (options.layout == TilesetDirectories) {
                const QString group = tileset.isEmpty()
                        ? QStringLiteral("_unparsed") : tileset;
                if (!ensureDirectory(
                        output.filePath(safeFileName(group)),
                        &destination, errorString)) {
                    return false;
                }
            }
            const QString path = destinationPath(
                        destination, texture.name,
                        QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty()) {
                consumePageSelection(selection.first);
                continue;
            }
            int removedPixels = 0;
            const QImage image = preparedTexture(
                        page, texture, options.repairOrphanPixels,
                        &removedPixels);
            if (!saveImageAtomically(image, path, errorString)) {
                return false;
            }
            result->orphanPixelsRemoved += removedPixels;
            ++result->filesWritten;
            ++result->assetFilesWritten;
            result->outputFiles += path;
            consumePageSelection(selection.first);
        }
    } else if (options.mode == AtlasPages) {
        QSet<int> pageIndexes;
        for (const QPair<int, int> &selection : options.textures)
            pageIndexes += selection.first;
        QList<int> sortedPages = pageIndexes.values();
        std::sort(sortedPages.begin(), sortedPages.end());
        int outputIndex = 0;
        for (int pageIndex : sortedPages) {
            const PackPage &page = pack.pages().at(pageIndex);
            if (progress) {
                progress->update(
                            QObject::tr(
                                "Writing atlas page %1 of %2\n%3")
                            .arg(++outputIndex)
                            .arg(sortedPages.size())
                            .arg(page.name));
            }
            const QString path = destinationPath(
                        output, page.name, QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty())
                continue;
            const QImage pageImage = PackFile::pageImage(page);
            if (pageImage.isNull()) {
                *errorString = QObject::tr(
                            "Could not decode atlas page %1.")
                        .arg(page.name);
                return false;
            }
            if (!saveImageAtomically(pageImage, path, errorString))
                return false;
            ++result->filesWritten;
            ++result->assetFilesWritten;
            result->outputFiles += path;
            PackFile::releaseDecodedImage(page);
        }
    } else if (options.mode == ReconstructedSheets) {
        QMap<QString, QList<QPair<int, int>>> groups;
        for (const QPair<int, int> &selection : options.textures) {
            const PackSubTexInfo &texture = pack.pages()
                    .at(selection.first).mInfo.at(selection.second);
            int tileIndex = -1;
            const QString tileset =
                    tilesetNameFor(texture, &tileIndex);
            if (tileset.isEmpty() || tileIndex < 0) {
                ++result->unparseableTextures;
                continue;
            }
            groups[tileset].append(selection);
        }
        int groupIndex = 0;
        for (auto group = groups.constBegin();
             group != groups.constEnd(); ++group) {
            if (progress) {
                progress->update(
                            QObject::tr(
                                "Reconstructing tilesheet %1 of %2\n%3")
                            .arg(++groupIndex)
                            .arg(groups.size())
                            .arg(group.key()));
            }
            int cellWidth = options.customWidth;
            int cellHeight = options.customHeight;
            if (options.geometry == OneXGeometry) {
                cellWidth = 64;
                cellHeight = 128;
            } else if (options.geometry == TwoXGeometry) {
                cellWidth = 128;
                cellHeight = 256;
            } else if (options.geometry == AutomaticGeometry) {
                cellWidth = 0;
                cellHeight = 0;
                for (const QPair<int, int> &selection : group.value()) {
                    const PackSubTexInfo &texture = pack.pages()
                            .at(selection.first).mInfo.at(selection.second);
                    cellWidth = qMax(cellWidth, texture.fx);
                    cellHeight = qMax(cellHeight, texture.fy);
                }
            }
            if (cellWidth <= 0 || cellHeight <= 0) {
                *errorString = QObject::tr(
                            "Invalid cell geometry for %1.")
                        .arg(group.key());
                return false;
            }
            int maximumIndex = -1;
            QSet<int> usedIndexes;
            for (const QPair<int, int> &selection : group.value()) {
                const PackSubTexInfo &texture = pack.pages()
                        .at(selection.first).mInfo.at(selection.second);
                int tileIndex = -1;
                tilesetNameFor(texture, &tileIndex);
                if (usedIndexes.contains(tileIndex)) {
                    *errorString = QObject::tr(
                                "Duplicate tile index %1 in %2.")
                            .arg(tileIndex).arg(group.key());
                    return false;
                }
                usedIndexes += tileIndex;
                maximumIndex = qMax(maximumIndex, tileIndex);
                if (texture.fx > cellWidth ||
                        texture.fy > cellHeight) {
                    *errorString = QObject::tr(
                                "%1 needs a %2x%3 cell, larger than "
                                "the selected %4x%5 geometry.")
                            .arg(texture.name)
                            .arg(texture.fx).arg(texture.fy)
                            .arg(cellWidth).arg(cellHeight);
                    return false;
                }
            }
            const int rows = maximumIndex / options.columns + 1;
            const qint64 sheetWidth =
                    qint64(options.columns) * cellWidth;
            const qint64 sheetHeight = qint64(rows) * cellHeight;
            if (sheetWidth <= 0 || sheetHeight <= 0 ||
                    sheetWidth > 32768 || sheetHeight > 32768 ||
                    sheetWidth * sheetHeight > 64LL * 1024 * 1024) {
                *errorString = QObject::tr(
                            "The reconstructed sheet %1 would be "
                            "%2x%3 pixels.")
                        .arg(group.key())
                        .arg(sheetWidth).arg(sheetHeight);
                return false;
            }
            QImage sheet(int(sheetWidth), int(sheetHeight),
                         QImage::Format_ARGB32);
            if (sheet.isNull()) {
                *errorString = QObject::tr(
                            "Could not allocate the %1 tilesheet.")
                        .arg(group.key());
                return false;
            }
            sheet.fill(Qt::transparent);
            QPainter painter(&sheet);
            for (const QPair<int, int> &selection : group.value()) {
                const PackPage &page =
                        pack.pages().at(selection.first);
                const PackSubTexInfo &texture =
                        page.mInfo.at(selection.second);
                int tileIndex = -1;
                tilesetNameFor(texture, &tileIndex);
                int removedPixels = 0;
                const QImage tileImage = preparedTexture(
                            page, texture,
                            options.repairOrphanPixels,
                            &removedPixels);
                result->orphanPixelsRemoved += removedPixels;
                painter.drawImage(
                            QPoint((tileIndex % options.columns) *
                                   cellWidth,
                                   (tileIndex / options.columns) *
                                   cellHeight),
                            tileImage);
            }
            painter.end();
            const QString path = destinationPath(
                        output, group.key(), QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty())
                continue;
            if (!saveImageAtomically(sheet, path, errorString))
                return false;
            ++result->filesWritten;
            ++result->assetFilesWritten;
            result->outputFiles += path;
        }
    } else {
        QMap<QString, QPair<int, int>> texturesByKey;
        QSet<QString> ambiguousKeys;
        for (const QPair<int, int> &selection : options.textures) {
            const PackPage &page = pack.pages().at(selection.first);
            const PackSubTexInfo &texture =
                    page.mInfo.at(selection.second);
            const QString key = canonicalTileKey(texture.name);
            if (key.isEmpty())
                continue;
            if (!texturesByKey.contains(key)) {
                texturesByKey.insert(key, selection);
                continue;
            }
            const QPair<int, int> previous = texturesByKey.value(key);
            const PackPage &previousPage = pack.pages().at(previous.first);
            const PackSubTexInfo &previousTexture =
                    previousPage.mInfo.at(previous.second);
            if (PackFile::textureSha256(page, texture) !=
                    PackFile::textureSha256(previousPage,
                                            previousTexture)) {
                ambiguousKeys += key;
            }
        }
        result->ambiguousObjectTiles = ambiguousKeys.size();
        const QList<ObjectExportDefinition> definitions =
                options.objectDefinitions.isEmpty()
                ? furnitureObjectDefinitions()
                : options.objectDefinitions;
        if (definitions.isEmpty()) {
            *errorString = QObject::tr(
                        "BuildingFurniture.txt does not contain any usable "
                        "multi-tile object definitions.");
            return false;
        }
        int definitionIndex = 0;
        for (const ObjectExportDefinition &definition : definitions) {
            if (progress && (definitionIndex % 16 == 0 ||
                             definitionIndex + 1 == definitions.size())) {
                progress->update(
                            QObject::tr(
                                "Assembling object %1 of %2\n%3, %4")
                            .arg(definitionIndex + 1)
                            .arg(definitions.size())
                            .arg(definition.groupName,
                                 definition.orientation));
            }
            ++definitionIndex;
            QList<ResolvedObjectTile> resolvedTiles;
            bool complete = true;
            int removedForObject = 0;
            QJsonArray sourceTiles;
            for (const ObjectTilePlacement &placement :
                 definition.placements) {
                sourceTiles.append(placement.tileName);
                if (!texturesByKey.contains(placement.tileKey) ||
                        ambiguousKeys.contains(placement.tileKey)) {
                    complete = false;
                    break;
                }
                const QPair<int, int> selection =
                        texturesByKey.value(placement.tileKey);
                const PackPage &page = pack.pages().at(selection.first);
                const PackSubTexInfo &texture =
                        page.mInfo.at(selection.second);
                ResolvedObjectTile resolved;
                resolved.placement = placement;
                int removedPixels = 0;
                resolved.image = preparedTexture(
                            page, texture,
                            options.repairOrphanPixels,
                            &removedPixels);
                removedForObject += removedPixels;
                resolvedTiles += resolved;
            }
            if (!complete) {
                ++result->incompleteObjects;
                continue;
            }
            const QImage objectImage = assembleObjectImage(
                        resolvedTiles, errorString);
            if (objectImage.isNull())
                return false;
            QDir destination;
            if (!ensureDirectory(
                    output.filePath(safeFileName(
                                        definition.groupName)),
                    &destination, errorString)) {
                return false;
            }
            const QString baseName = QStringLiteral("%1_%2_%3")
                    .arg(definition.groupName)
                    .arg(definition.variant, 3, 10,
                         QLatin1Char('0'))
                    .arg(definition.orientation);
            const QString path = destinationPath(
                        destination, baseName,
                        QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty())
                continue;
            if (!saveImageAtomically(objectImage, path, errorString))
                return false;
            QJsonObject assembly;
            assembly.insert(QStringLiteral("group"),
                            definition.groupName);
            assembly.insert(QStringLiteral("variant"),
                            definition.variant);
            assembly.insert(QStringLiteral("orientation"),
                            definition.orientation);
            assembly.insert(QStringLiteral("tiles"), sourceTiles);
            assembly.insert(QStringLiteral("width"),
                            objectImage.width());
            assembly.insert(QStringLiteral("height"),
                            objectImage.height());
            assembly.insert(QStringLiteral("orphanPixelsRemoved"),
                            removedForObject);
            assembly.insert(QStringLiteral("output"),
                            QDir(output.path()).relativeFilePath(path));
            result->objectAssemblies.append(assembly);
            result->orphanPixelsRemoved += removedForObject;
            ++result->assembledObjects;
            ++result->filesWritten;
            ++result->assetFilesWritten;
            result->outputFiles += path;
        }
    }
    if (options.writeManifest) {
        if (progress)
            progress->update(QObject::tr("Writing extraction manifest..."));
        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"),
                        QStringLiteral("PZToolsPackExtraction"));
        manifest.insert(QStringLiteral("version"), 3);
        manifest.insert(QStringLiteral("sourcePack"),
                        pack.fileName());
        manifest.insert(QStringLiteral("sourcePackSha256"),
                        PackFile::sha256Text(pack.fileSha256()));
        manifest.insert(QStringLiteral("packVersion"),
                        pack.version());
        manifest.insert(QStringLiteral("selectedTextures"),
                        manifestSources);
        manifest.insert(QStringLiteral("mode"), int(options.mode));
        manifest.insert(QStringLiteral("orphanPixelCorrection"),
                        options.repairOrphanPixels);
        manifest.insert(QStringLiteral("orphanPixelsRemoved"),
                        result->orphanPixelsRemoved);
        manifest.insert(QStringLiteral("excludeMultiTileObjectTiles"),
                        options.excludeMultiTileObjectTiles);
        manifest.insert(QStringLiteral("excludedMultiTileObjectTiles"),
                        result->excludedObjectTiles);
        manifest.insert(QStringLiteral("assembledObjects"),
                        result->objectAssemblies);
        manifest.insert(QStringLiteral("incompleteObjectsSkipped"),
                        result->incompleteObjects);
        manifest.insert(QStringLiteral("ambiguousObjectTiles"),
                        result->ambiguousObjectTiles);
        QJsonArray outputs;
        for (const QString &path : result->outputFiles)
            outputs.append(QDir(output.path()).relativeFilePath(path));
        manifest.insert(QStringLiteral("outputs"), outputs);
        const QString manifestPath = destinationPath(
                    output, QStringLiteral("pack-extraction-manifest"),
                    QStringLiteral(".json"),
                    options.conflict, result);
        if (!manifestPath.isEmpty()) {
            QSaveFile file(manifestPath);
            const QByteArray json =
                    QJsonDocument(manifest).toJson(
                        QJsonDocument::Indented);
            if (!file.open(QIODevice::WriteOnly) ||
                    file.write(json) != json.size() ||
                    !file.commit()) {
                *errorString = QObject::tr(
                            "Could not write the extraction manifest.");
                return false;
            }
            ++result->filesWritten;
            result->outputFiles += manifestPath;
        }
    }
    return true;
}
}
PackExtractDialog::PackExtractDialog(PackFile &packFile,
                                     QWidget *parent,
                                     PROGRESS *initializationProgress)
    : QDialog(parent)
    , ui(new Ui::PackExtractDialog)
    , mPackFile(packFile)
{
    ui->setupUi(this);

    ui->matchModeCombo->addItem(tr("Contains"), 0);
    ui->matchModeCombo->addItem(tr("Starts with"), 1);
    ui->matchModeCombo->addItem(tr("Wildcard"), 2);
    ui->matchModeCombo->addItem(tr("Regular expression"), 3);
    ui->matchModeCombo->addItem(tr("Exact tileset name"), 4);
    ui->geometryCombo->addItem(tr("Automatic from texture canvases"),
                               int(AutomaticGeometry));
    ui->geometryCombo->addItem(tr("1x cells (64x128)"),
                               int(OneXGeometry));
    ui->geometryCombo->addItem(tr("2x cells (128x256)"),
                               int(TwoXGeometry));
    ui->geometryCombo->addItem(tr("Custom cells"),
                               int(CustomGeometry));
    ui->outputLayoutCombo->addItem(tr("Flat output directory"),
                                   int(FlatLayout));
    ui->outputLayoutCombo->addItem(tr("Subdirectory per atlas page"),
                                   int(PageDirectories));
    ui->outputLayoutCombo->addItem(tr("Subdirectory per tileset"),
                                   int(TilesetDirectories));
    ui->conflictCombo->addItem(tr("Rename new files safely"),
                               int(RenameExisting));
    ui->conflictCombo->addItem(tr("Skip existing files"),
                               int(SkipExisting));
    ui->conflictCombo->addItem(tr("Overwrite existing files"),
                               int(OverwriteExisting));
    ui->textureTable->setSelectionBehavior(
                QAbstractItemView::SelectRows);
    ui->textureTable->setEditTriggers(
                QAbstractItemView::NoEditTriggers);
    ui->textureTable->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::Stretch);
    ui->textureTable->verticalHeader()->setSectionResizeMode(
                QHeaderView::Fixed);
    ui->textureTable->verticalHeader()->setDefaultSectionSize(
                ui->textureTable->fontMetrics().height() + 6);
    for (int column = 0;
         column < ui->textureTable->columnCount(); ++column) {
        if (column != 1) {
            ui->textureTable->horizontalHeader()
                    ->setSectionResizeMode(
                        column, QHeaderView::ResizeToContents);
        }
    }

    connect(ui->outputBrowse, &QAbstractButton::clicked,
            this, &PackExtractDialog::browse);
    connect(ui->filterEdit, &QLineEdit::textChanged,
            this, &PackExtractDialog::filterChanged);
    connect(ui->matchModeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PackExtractDialog::filterChanged);
    connect(ui->caseSensitiveCheck, &QAbstractButton::toggled,
            this, &PackExtractDialog::filterChanged);
    connect(ui->selectVisibleButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::selectAllVisible);
    connect(ui->clearVisibleButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::clearVisible);
    connect(ui->allTilesButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::prepareAllTiles);
    connect(ui->allTilesetsButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::prepareAllTilesets);
    connect(ui->allObjectsButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::prepareAllObjects);
    connect(ui->textureTable, &QTableWidget::itemChanged,
            this, &PackExtractDialog::filterChanged);
    connect(ui->radioIndividual, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->radioSheets, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->radioObjects, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->radioPages, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->geometryCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PackExtractDialog::modeChanged);

    QSettings settings;
    settings.beginGroup(QStringLiteral("PackExtractDialog"));
    ui->filterEdit->setText(
                settings.value(QStringLiteral("Filter")).toString());
    ui->matchModeCombo->setCurrentIndex(
                settings.value(QStringLiteral("MatchMode"), 0).toInt());
    ui->caseSensitiveCheck->setChecked(
                settings.value(QStringLiteral("CaseSensitive"),
                               false).toBool());
    const int mode = settings.value(
                QStringLiteral("Mode"),
                int(IndividualImages)).toInt();
    ui->radioIndividual->setChecked(mode == IndividualImages);
    ui->radioSheets->setChecked(mode == ReconstructedSheets);
    ui->radioObjects->setChecked(mode == AssembledObjects);
    ui->radioPages->setChecked(mode == AtlasPages);
    ui->geometryCombo->setCurrentIndex(
                settings.value(QStringLiteral("Geometry"), 0).toInt());
    ui->cellWidthSpin->setValue(
                settings.value(QStringLiteral("CellWidth"), 128).toInt());
    ui->cellHeightSpin->setValue(
                settings.value(QStringLiteral("CellHeight"), 256).toInt());
    ui->columnSpin->setValue(
                settings.value(QStringLiteral("Columns"), 8).toInt());
    ui->outputLayoutCombo->setCurrentIndex(
                settings.value(QStringLiteral("OutputLayout"), 0).toInt());
    ui->conflictCombo->setCurrentIndex(
                settings.value(QStringLiteral("Conflict"), 0).toInt());
    ui->manifestCheck->setChecked(
                settings.value(QStringLiteral("Manifest"), true).toBool());
    ui->orphanPixelsCheck->setChecked(
                settings.value(QStringLiteral("RepairOrphanPixels"),
                               false).toBool());
    ui->excludeObjectTilesCheck->setChecked(
                settings.value(QStringLiteral("ExcludeObjectTiles"),
                               false).toBool());
    ui->outputEdit->setText(
                settings.value(
                    QStringLiteral("OutputDirectory")).toString());
    settings.endGroup();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Extract"));
    populateTextures(initializationProgress);
    filterChanged();
    modeChanged();
}

PackExtractDialog::~PackExtractDialog()
{
    delete ui;
}

void PackExtractDialog::populateTextures(PROGRESS *progress)
{
    QElapsedTimer elapsed;
    elapsed.start();
    const QSignalBlocker blocker(ui->textureTable);
    ui->textureTable->setUpdatesEnabled(false);
    ui->textureTable->setSortingEnabled(false);
    int textureCount = 0;
    for (const PackPage &page : mPackFile.pages())
        textureCount += page.mInfo.size();
    ui->textureTable->setRowCount(textureCount);
    if (progress) {
        progress->update(
                    tr("Preparing the Versatile .pack Extractor...\n"
                       "Indexing %1 textures from %2 atlas pages.")
                    .arg(textureCount)
                    .arg(mPackFile.pages().size()));
    }
    int processed = 0;
    for (int pageIndex = 0;
         pageIndex < mPackFile.pages().size(); ++pageIndex) {
        const PackPage &page = mPackFile.pages().at(pageIndex);
        for (int textureIndex = 0;
             textureIndex < page.mInfo.size(); ++textureIndex) {
            if (progress && (processed % 32 == 0 ||
                             processed + 1 == textureCount)) {
                progress->update(
                            tr("Preparing texture %1 of %2\n"
                               "Atlas page: %3")
                            .arg(processed + 1)
                            .arg(textureCount)
                            .arg(page.name));
                if (progress->wasCanceled()) {
                    mInitializationCanceled = true;
                    ui->textureTable->setSortingEnabled(true);
                    ui->textureTable->setUpdatesEnabled(true);
                    return;
                }
            }
            const PackSubTexInfo &texture =
                    page.mInfo.at(textureIndex);
            const int row = processed;
            QTableWidgetItem *check = new QTableWidgetItem;
            check->setFlags(Qt::ItemIsEnabled |
                            Qt::ItemIsSelectable |
                            Qt::ItemIsUserCheckable);
            check->setCheckState(Qt::Checked);
            check->setData(Qt::UserRole, pageIndex);
            check->setData(Qt::UserRole + 1, textureIndex);
            ui->textureTable->setItem(row, 0, check);
            QTableWidgetItem *name =
                    new QTableWidgetItem(texture.name);
            ui->textureTable->setItem(row, 1, name);
            int tileIndex = -1;
            const QString tileset =
                    tilesetNameFor(texture, &tileIndex);
            ui->textureTable->setItem(
                        row, 2,
                        new QTableWidgetItem(
                            tileset.isEmpty()
                            ? QStringLiteral("-") : tileset));
            ui->textureTable->setItem(
                        row, 3,
                        new QTableWidgetItem(
                            tileIndex < 0
                            ? QStringLiteral("-")
                            : QString::number(tileIndex)));
            ui->textureTable->setItem(
                        row, 4, new QTableWidgetItem(page.name));
            ui->textureTable->setItem(
                        row, 5,
                        new QTableWidgetItem(
                            QStringLiteral("%1x%2")
                            .arg(texture.fx).arg(texture.fy)));
            ui->textureTable->setItem(
                        row, 6,
                        new QTableWidgetItem(
                            QStringLiteral("%1,%2 %3x%4")
                            .arg(texture.x).arg(texture.y)
                            .arg(texture.w).arg(texture.h)));
            ++processed;
        }
    }
    ui->textureTable->setSortingEnabled(true);
    ui->textureTable->setUpdatesEnabled(true);
    if (progress) {
        progress->update(
                    tr("Finalizing filters and extraction options..."));
    }
    qInfo().noquote()
            << QStringLiteral(
                   "Pack extractor metadata index: textures %1 pages %2 elapsed %3 ms atlas pages decoded by indexer 0")
               .arg(textureCount)
               .arg(mPackFile.pages().size())
               .arg(elapsed.elapsed());
}

bool PackExtractDialog::rowMatches(
        int row, QString *regularExpressionError) const
{
    const QString filter = ui->filterEdit->text().trimmed();
    if (filter.isEmpty())
        return true;

    const Qt::CaseSensitivity sensitivity =
            ui->caseSensitiveCheck->isChecked()
            ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QString name =
            ui->textureTable->item(row, 1)->text();
    const QString tileset =
            ui->textureTable->item(row, 2)->text();
    const int mode = ui->matchModeCombo->currentData().toInt();
    if (mode == 3) {
        QRegularExpression expression(
                    filter,
                    sensitivity == Qt::CaseInsensitive
                    ? QRegularExpression::CaseInsensitiveOption
                    : QRegularExpression::NoPatternOption);
        if (!expression.isValid()) {
            *regularExpressionError = expression.errorString();
            return false;
        }
        return expression.match(name).hasMatch();
    }

    const QStringList patterns = filter.split(
                QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &rawPattern : patterns) {
        const QString pattern = rawPattern.trimmed();
        if (mode == 0 &&
                name.contains(pattern, sensitivity)) {
            return true;
        }
        if (mode == 1 &&
                name.startsWith(pattern, sensitivity)) {
            return true;
        }
        if (mode == 2) {
            QRegularExpression expression(
                        QRegularExpression::wildcardToRegularExpression(
                            pattern),
                        sensitivity == Qt::CaseInsensitive
                        ? QRegularExpression::CaseInsensitiveOption
                        : QRegularExpression::NoPatternOption);
            if (expression.match(name).hasMatch())
                return true;
        }
        if (mode == 4 &&
                tileset.compare(pattern, sensitivity) == 0) {
            return true;
        }
    }
    return false;
}

void PackExtractDialog::filterChanged()
{
    int visible = 0;
    int checked = 0;
    QString regularExpressionError;
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        QString rowError;
        const bool matches = rowMatches(row, &rowError);
        if (!rowError.isEmpty())
            regularExpressionError = rowError;
        ui->textureTable->setRowHidden(row, !matches);
        if (matches) {
            ++visible;
            if (ui->textureTable->item(row, 0)->checkState() ==
                    Qt::Checked) {
                ++checked;
            }
        }
    }
    if (!regularExpressionError.isEmpty()) {
        ui->matchSummary->setStyleSheet(
                    QStringLiteral("color: #d9534f;"));
        ui->matchSummary->setText(
                    tr("Invalid regular expression: %1")
                    .arg(regularExpressionError));
    } else {
        ui->matchSummary->setStyleSheet(QString());
        ui->matchSummary->setText(
                    tr("%1 textures visible, %2 selected, %3 pages")
                    .arg(visible).arg(checked)
                    .arg(mPackFile.pages().size()));
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
                regularExpressionError.isEmpty() && checked > 0);
}
void PackExtractDialog::selectAllVisible()
{
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        if (!ui->textureTable->isRowHidden(row))
            ui->textureTable->item(row, 0)->setCheckState(Qt::Checked);
    }
    filterChanged();
}
void PackExtractDialog::clearVisible()
{
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        if (!ui->textureTable->isRowHidden(row))
            ui->textureTable->item(row, 0)->setCheckState(Qt::Unchecked);
    }
    filterChanged();
}
void PackExtractDialog::prepareAll(int mode)
{
    {
        const QSignalBlocker tableBlocker(ui->textureTable);
        const QSignalBlocker filterBlocker(ui->filterEdit);
        ui->filterEdit->clear();
        for (int row = 0; row < ui->textureTable->rowCount(); ++row)
            ui->textureTable->item(row, 0)->setCheckState(Qt::Checked);
    }
    ui->radioIndividual->setChecked(mode == IndividualImages);
    ui->radioSheets->setChecked(mode == ReconstructedSheets);
    ui->radioObjects->setChecked(mode == AssembledObjects);
    ui->radioPages->setChecked(mode == AtlasPages);
    filterChanged();
    modeChanged();
}
void PackExtractDialog::prepareAllTiles()
{
    prepareAll(IndividualImages);
    const int tilesetLayoutIndex = ui->outputLayoutCombo->findData(
                int(TilesetDirectories));
    if (tilesetLayoutIndex >= 0)
        ui->outputLayoutCombo->setCurrentIndex(tilesetLayoutIndex);
}
void PackExtractDialog::prepareAllTilesets()
{
    prepareAll(ReconstructedSheets);
}
void PackExtractDialog::prepareAllObjects()
{
    prepareAll(AssembledObjects);
}
QList<QPair<int, int>>
PackExtractDialog::checkedVisibleTextures() const
{
    QList<QPair<int, int>> selections;
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        QTableWidgetItem *item = ui->textureTable->item(row, 0);
        if (!ui->textureTable->isRowHidden(row) &&
                item->checkState() == Qt::Checked) {
            selections += qMakePair(
                        item->data(Qt::UserRole).toInt(),
                        item->data(Qt::UserRole + 1).toInt());
        }
    }
    return selections;
}
void PackExtractDialog::browse()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Choose output directory"),
                ui->outputEdit->text());
    if (!directory.isEmpty())
        ui->outputEdit->setText(QDir::toNativeSeparators(directory));
}
void PackExtractDialog::modeChanged()
{
    const bool sheets = ui->radioSheets->isChecked();
    ui->geometryCombo->setEnabled(sheets);
    ui->cellWidthSpin->setEnabled(
                sheets &&
                ui->geometryCombo->currentData().toInt() ==
                CustomGeometry);
    ui->cellHeightSpin->setEnabled(
                ui->cellWidthSpin->isEnabled());
    ui->columnSpin->setEnabled(sheets);
    ui->outputLayoutCombo->setEnabled(
                ui->radioIndividual->isChecked());
    ui->orphanPixelsCheck->setEnabled(
                !ui->radioPages->isChecked());
    ui->excludeObjectTilesCheck->setEnabled(
                ui->radioIndividual->isChecked());
}
void PackExtractDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("PackExtractDialog"));
    settings.setValue(QStringLiteral("Filter"),
                      ui->filterEdit->text());
    settings.setValue(QStringLiteral("MatchMode"),
                      ui->matchModeCombo->currentIndex());
    settings.setValue(QStringLiteral("CaseSensitive"),
                      ui->caseSensitiveCheck->isChecked());
    settings.setValue(QStringLiteral("Mode"),
                      ui->radioIndividual->isChecked()
                      ? int(IndividualImages)
                      : ui->radioSheets->isChecked()
                        ? int(ReconstructedSheets)
                        : ui->radioObjects->isChecked()
                          ? int(AssembledObjects)
                          : int(AtlasPages));
    settings.setValue(QStringLiteral("Geometry"),
                      ui->geometryCombo->currentIndex());
    settings.setValue(QStringLiteral("CellWidth"),
                      ui->cellWidthSpin->value());
    settings.setValue(QStringLiteral("CellHeight"),
                      ui->cellHeightSpin->value());
    settings.setValue(QStringLiteral("Columns"),
                      ui->columnSpin->value());
    settings.setValue(QStringLiteral("OutputLayout"),
                      ui->outputLayoutCombo->currentIndex());
    settings.setValue(QStringLiteral("Conflict"),
                      ui->conflictCombo->currentIndex());
    settings.setValue(QStringLiteral("Manifest"),
                      ui->manifestCheck->isChecked());
    settings.setValue(QStringLiteral("RepairOrphanPixels"),
                      ui->orphanPixelsCheck->isChecked());
    settings.setValue(QStringLiteral("ExcludeObjectTiles"),
                      ui->excludeObjectTilesCheck->isChecked());
    settings.setValue(QStringLiteral("OutputDirectory"),
                      ui->outputEdit->text().trimmed());
    settings.endGroup();
}
void PackExtractDialog::accept()
{
    const QList<QPair<int, int>> textures =
            checkedVisibleTextures();
    if (textures.isEmpty()) {
        QMessageBox::information(
                    this, tr("Nothing selected"),
                    tr("Select at least one visible texture."));
        return;
    }
    const QString outputPath = ui->outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(
                    this, tr("Output directory required"),
                    tr("Choose an output directory."));
        return;
    }
    ExtractOptions options;
    options.mode = ui->radioIndividual->isChecked()
            ? IndividualImages
            : ui->radioSheets->isChecked()
              ? ReconstructedSheets
              : ui->radioObjects->isChecked()
                ? AssembledObjects : AtlasPages;
    options.layout = OutputLayout(
                ui->outputLayoutCombo->currentData().toInt());
    options.conflict = ConflictPolicy(
                ui->conflictCombo->currentData().toInt());
    options.geometry = GeometryMode(
                ui->geometryCombo->currentData().toInt());
    options.customWidth = ui->cellWidthSpin->value();
    options.customHeight = ui->cellHeightSpin->value();
    options.columns = ui->columnSpin->value();
    options.writeManifest = ui->manifestCheck->isChecked();
    options.repairOrphanPixels =
            ui->orphanPixelsCheck->isChecked() &&
            options.mode != AtlasPages;
    options.excludeMultiTileObjectTiles =
            ui->excludeObjectTilesCheck->isChecked() &&
            options.mode == IndividualImages;
    options.outputDirectory = outputPath;
    options.textures = textures;
    saveSettings();
    ExtractResult result;
    QString error;
    {
        PROGRESS progress(
                    tr("Starting extraction of %1 textures...")
                    .arg(textures.size()), this);
        if (!performExtraction(
                mPackFile, options, &result, &error, &progress)) {
            QMessageBox::warning(this, tr("Extraction failed"), error);
            return;
        }
    }
    if (result.assetFilesWritten == 0) {
        QMessageBox::information(
                    this, tr("Nothing written"),
                    result.skippedFiles > 0
                    ? tr("All matching output files already exist and were "
                         "skipped by the selected conflict policy.")
                    : options.mode == AssembledObjects
                    ? tr("No complete multi-tile object could be assembled "
                         "from the selected pack textures.\n\n"
                         "Incomplete objects: %1 | Ambiguous tiles: %2")
                      .arg(result.incompleteObjects)
                      .arg(result.ambiguousObjectTiles)
                    : result.excludedObjectTiles > 0
                    ? tr("Every selected texture belongs to a multi-tile "
                         "object and was excluded.\n\nExcluded textures: %1")
                      .arg(result.excludedObjectTiles)
                    : tr("All matching output files were skipped."));
        return;
    }

    QMessageBox::information(
                this, tr("Extraction complete"),
                tr("%1 selected textures produced %2 asset files.\n"
                   "Renamed: %3 | Skipped: %4 | "
                   "Unparseable for tilesheets: %5\n"
                   "Objects assembled: %6 | Incomplete objects: %7\n"
                   "Multi-tile object textures excluded: %8 | "
                   "Orphan pixels removed: %9")
                .arg(result.selectedTextures)
                .arg(result.assetFilesWritten)
                .arg(result.renamedFiles)
                .arg(result.skippedFiles)
                .arg(result.unparseableTextures)
                .arg(result.assembledObjects)
                .arg(result.incompleteObjects)
                .arg(result.excludedObjectTiles)
                .arg(result.orphanPixelsRemoved));
    qInfo().noquote()
            << QStringLiteral(
                   "Pack extraction complete: selected %1 assets %2 "
                   "objects %3 incomplete-objects %4 object-tiles-excluded %5 "
                   "orphan-pixels %6 renamed %7 skipped %8 output %9")
               .arg(result.selectedTextures)
               .arg(result.assetFilesWritten)
               .arg(result.assembledObjects)
               .arg(result.incompleteObjects)
               .arg(result.excludedObjectTiles)
               .arg(result.orphanPixelsRemoved)
               .arg(result.renamedFiles)
               .arg(result.skippedFiles)
               .arg(QDir::toNativeSeparators(outputPath));
    QDialog::accept();
}

