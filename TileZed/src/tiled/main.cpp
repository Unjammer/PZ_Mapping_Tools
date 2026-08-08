/*
 * main.cpp
 * Copyright 2008-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2011, Ben Longbons <b.r.longbons@gmail.com>
 * Copyright 2011, Stefan Beller <stefanbeller@googlemail.com>
 *
 * This file is part of Tiled.
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

#include "commandlineparser.h"
#include "automappingmanager.h"
#include "bmpblender.h"
#include "bmptool.h"
#include "bmptooldialog.h"
#include "depthmapeditor.h"
#include "lootdistributiondialog.h"
#include "mainwindow.h"
#include "packcompare.h"
#include "packextractdialog.h"
#include "languagemanager.h"
#include "preferences.h"
#include "tiledapplication.h"
#include "tiledefcompare.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"
#ifdef ZOMBOID
#include "BuildingEditor/building.h"
#include "BuildingEditor/buildingdocument.h"
#include "BuildingEditor/buildingdocumentmgr.h"
#include "BuildingEditor/buildingeditorwindow.h"
#include "BuildingEditor/buildingfloor.h"
#include "BuildingEditor/buildingfurnituredock.h"
#include "BuildingEditor/buildinglua.h"
#include "BuildingEditor/buildingmap.h"
#include "BuildingEditor/buildingpreferences.h"
#include "BuildingEditor/buildingtemplates.h"
#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/buildingtilesdialog.h"
#include "BuildingEditor/buildingtilesetdock.h"
#include "BuildingEditor/categorydock.h"
#include "BuildingEditor/furnituregroups.h"
#include "tilemetainfomgr.h"
#include "tiledeffile.h"
#include "tilesetmanager.h"
#include "worlded/worldedmgr.h"
#include "zprogress.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QSet>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QUndoStack>
#endif

#include <QDebug>
#include <QEventLoop>
#include <QIcon>
#include <QTimer>
#include <QtPlugin>

#ifdef STATIC_BUILD
Q_IMPORT_PLUGIN(qgif)
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qtiff)
#endif

#define STRINGIFY(x) #x
#define AS_STRING(x) STRINGIFY(x)

using namespace Tiled::Internal;

#ifdef ZOMBOID
bool gStartupBlockRendering = true;
#endif

namespace {

#ifdef ZOMBOID
static bool validateSingleRowTilesetCatalog(QString *errorString)
{
    TileMetaInfoMgr *manager = TileMetaInfoMgr::instance();
    Tiled::Tileset *giblet =
            manager->tileset(QStringLiteral("Giblet_00"));
    if (!giblet) {
        *errorString = QStringLiteral(
                    "Giblet_00 is absent from the loaded tileset catalogue");
        return false;
    }
    if (giblet->tileCount() != 8) {
        *errorString = QStringLiteral(
                    "Giblet_00 has %1 tiles instead of 8")
                .arg(giblet->tileCount());
        return false;
    }

    // Reproduce the legacy failure directly. Valid one-row sheets sometimes
    // reached the metadata writer with their column count reset to zero after
    // an image refresh even though all eight tiles and the PNG geometry were
    // still present.
    giblet->setColumnCount(0);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create a temporary catalogue directory");
        return false;
    }
    const QString outputFile =
            temporary.filePath(QStringLiteral("Tilesets.txt"));
    if (!manager->writeTxt(outputFile,
                           manager->revision() + 1,
                           manager->sourceRevision())) {
        *errorString = manager->errorString();
        return false;
    }
    if (giblet->columnCount() != 8) {
        *errorString = QStringLiteral(
                    "Giblet_00 recovery returned %1 columns instead of 8")
                .arg(giblet->columnCount());
        return false;
    }
    return true;
}

static void addEntryTiles(QSet<BuildingEditor::BuildingTile *> &tiles,
                          BuildingEditor::BuildingTileEntry *entry)
{
    if (!entry || entry->isNone())
        return;
    for (BuildingEditor::BuildingTile *tile : entry->mTiles) {
        if (tile && !tile->isNone())
            tiles += tile;
    }
}

static bool validateBuildingTemplateTiles(BuildingEditor::Building *building,
                                          QString *errorString)
{
    QSet<BuildingEditor::BuildingTile *> buildingTiles;
    for (BuildingEditor::BuildingTileEntry *entry : building->tiles())
        addEntryTiles(buildingTiles, entry);
    for (BuildingEditor::Room *room : building->rooms()) {
        for (BuildingEditor::BuildingTileEntry *entry : room->tiles())
            addEntryTiles(buildingTiles, entry);
    }
    for (BuildingEditor::BuildingTileEntry *entry : building->usedTiles())
        addEntryTiles(buildingTiles, entry);
    for (BuildingEditor::FurnitureTiles *furniture : building->usedFurniture()) {
        for (BuildingEditor::FurnitureTile *orientation : furniture->tiles()) {
            if (!orientation)
                continue;
            for (BuildingEditor::BuildingTile *tile : orientation->tiles()) {
                if (tile && !tile->isNone())
                    buildingTiles += tile;
            }
        }
    }

    for (BuildingEditor::BuildingTile *buildingTile : buildingTiles) {
        Tiled::Tileset *tileset = TileMetaInfoMgr::instance()
                ->tileset(buildingTile->mTilesetName);
        if (!tileset) {
            *errorString = QStringLiteral("Unknown tileset: %1")
                    .arg(buildingTile->mTilesetName);
            return false;
        }
        if (tileset->isMissing()) {
            *errorString = QStringLiteral("Missing tileset image: %1")
                    .arg(buildingTile->mTilesetName);
            return false;
        }
        if (!tileset->isLoaded()) {
            *errorString = QStringLiteral(
                        "Required template tileset was not loaded: %1")
                    .arg(buildingTile->mTilesetName);
            return false;
        }
        if (buildingTile->mIndex < 0
                || buildingTile->mIndex >= tileset->tileCount()) {
            *errorString = QStringLiteral("Invalid template tile: %1")
                    .arg(buildingTile->name());
            return false;
        }
        Tiled::Tile *tile = tileset->tileAt(buildingTile->mIndex);
        if (!tile || tile == TilesetManager::instance()->missingTile()
                || tile->image().isNull()) {
            *errorString = QStringLiteral("Template tile has no image: %1")
                    .arg(buildingTile->name());
            return false;
        }
    }

    qInfo() << "Validated building template:"
            << buildingTiles.count() << "tile references";
    return true;
}

static bool validateAllBuildingTemplates(QString *errorString)
{
    BuildingEditor::BuildingTemplates *templates =
            BuildingEditor::BuildingTemplates::instance();
    for (int index = 0; index < templates->templateCount(); ++index) {
        BuildingEditor::BuildingTemplate *buildingTemplate =
                templates->templateAt(index);
        BuildingEditor::Building *building =
                new BuildingEditor::Building(17, 23, buildingTemplate);
        building->insertFloor(
                    0, new BuildingEditor::BuildingFloor(building, 0));

        const QStringList unresolved =
                BuildingEditor::BuildingMap::loadNeededTilesets(building);
        if (!unresolved.isEmpty()) {
            *errorString = QStringLiteral(
                        "Template \"%1\" requires unavailable tilesets: %2")
                    .arg(buildingTemplate->name())
                    .arg(unresolved.join(QStringLiteral(", ")));
            delete building;
            return false;
        }

        QString templateError;
        if (!validateBuildingTemplateTiles(building, &templateError)) {
            *errorString = QStringLiteral("Template \"%1\": %2")
                    .arg(buildingTemplate->name())
                    .arg(templateError);
            delete building;
            return false;
        }
        delete building;
    }

    qInfo() << "Validated all building templates:"
            << templates->templateCount();
    return true;
}

static bool validateBrushUndoBuffers(QString *errorString)
{
    Tiled::Tileset tileset(QStringLiteral("brush-validation"), 64, 128);
    Tiled::Tile tile(64, 128, 0, &tileset);
    Tiled::TileLayer layer(QStringLiteral("brush-undo"), 0, 0, 1, 1);
    const Tiled::Cell paintedCell(&tile);
    layer.setCell(0, 0, paintedCell);

    QElapsedTimer elapsed;
    elapsed.start();
    for (int size = 2; size <= 300; ++size) {
        // This mirrors a diagonal brush stroke whose merged undo buffer grows
        // by one row and one column for each new tile.
        layer.resize(QSize(size, size), QPoint(1, 1));
        layer.setCell(0, 0, paintedCell);
    }
    const qint64 elapsedMs = elapsed.elapsed();

    for (int i = 0; i < 300; ++i) {
        if (layer.cellAt(i, i) != paintedCell) {
            *errorString = QStringLiteral(
                        "Merged brush undo buffer lost the tile at %1,%1")
                    .arg(i);
            return false;
        }
    }
    if (!layer.cellAt(0, 1).isEmpty() ||
            !layer.cellAt(298, 299).isEmpty()) {
        *errorString = QStringLiteral(
                    "Merged brush undo buffer moved a tile off the stroke");
        return false;
    }
    if (!layer.usedTilesets().contains(&tileset)) {
        *errorString = QStringLiteral(
                    "Merged brush undo buffer lost its tileset references");
        return false;
    }

    QImage brushImage(32, 32, QImage::Format_ARGB32);
    brushImage.fill(Qt::transparent);
    brushImage.setPixel(0, 0, qRgba(0, 0, 0, 255));
    brushImage.setPixel(16, 16, qRgba(16, 16, 16, 255));
    brushImage.setPixel(31, 31, qRgba(0, 0, 0, 255));
    brushImage.setPixel(2, 2, qRgba(255, 255, 255, 255));
    const QRegion mask = BmpBrushTool::regionFromBrushImage(brushImage);
    if (!mask.contains(QPoint(0, 0))
            || !mask.contains(QPoint(16, 16))
            || !mask.contains(QPoint(31, 31))
            || mask.contains(QPoint(2, 2))
            || mask.contains(QPoint(3, 3))) {
        *errorString = QStringLiteral(
                    "Custom PNG brush mask did not preserve dark/transparent pixels");
        return false;
    }

    BmpBrushTool *brushTool = BmpBrushTool::instance();
    brushTool->setCustomBrush(mask, brushImage.size(),
                             QStringLiteral("self-test"));
    const QRegion translated = brushTool->brushRegionAt(QPoint(100, 100));
    if (!translated.contains(QPoint(84, 84))
            || !translated.contains(QPoint(100, 100))
            || !translated.contains(QPoint(115, 115))
            || translated.contains(QPoint(86, 86))) {
        *errorString = QStringLiteral(
                    "Custom PNG brush mask was not centered correctly");
        brushTool->setBrushShape(BmpBrushTool::BrushShape::Square);
        return false;
    }
    brushTool->setBrushShape(BmpBrushTool::BrushShape::Square);

    if (!BmpToolDialog::validateReloadEquality(errorString))
        return false;
    if (!BmpBlender::validateUnavailableTilesetFiltering(errorString))
        return false;

    qInfo() << "Validated 300-tile diagonal brush undo growth in"
            << elapsedMs << "ms and a centered 32x32 PNG brush mask";
    return true;
}

static bool validateTileDefSplitting(QString *errorString)
{
    QTemporaryDir malformedDirectory;
    if (!malformedDirectory.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the malformed tiledef test directory");
        return false;
    }
    const QString malformedPath =
            malformedDirectory.filePath(
                QStringLiteral("invalid-values.tiles"));
    QFile malformedFile(malformedPath);
    if (!malformedFile.open(QIODevice::WriteOnly)) {
        *errorString = QStringLiteral(
                    "Could not create the malformed tiledef test file");
        return false;
    }
    QDataStream malformedStream(&malformedFile);
    malformedStream.setByteOrder(QDataStream::LittleEndian);
    malformedStream.writeRawData("tdef", 4);
    malformedStream << qint32(1) << qint32(1);
    const QByteArray invalidName("validation_invalid\n");
    const QByteArray invalidImage("validation_invalid.png\n");
    malformedStream.writeRawData(
                invalidName.constData(), invalidName.size());
    malformedStream.writeRawData(
                invalidImage.constData(), invalidImage.size());
    malformedStream << qint32(2) << qint32(2)
                    << qint32(0) << qint32(5);
    malformedFile.close();

    TileDefFile malformed;
    if (malformed.read(malformedPath)) {
        *errorString = QStringLiteral(
                    "A tiledef with invalid ID/count unexpectedly loaded");
        return false;
    }
    const QString malformedError = malformed.errorString();
    if (!malformedError.contains(
                QStringLiteral(
                    "Stored tile count 5 exceeds the grid capacity of 4 "
                    "(2 columns x 2 rows).")) ||
            !malformedError.contains(
                QStringLiteral(
                    "Tileset ID is 0; version-1 IDs must start at 1.")) ||
            !malformedError.contains(
                QStringLiteral("Values stored in the .tiles file:"))) {
        *errorString = QStringLiteral(
                    "Detailed tiledef diagnostics are incomplete:\n%1")
                .arg(malformedError);
        return false;
    }

    const QString truncatedPath =
            malformedDirectory.filePath(
                QStringLiteral("truncated-field.tiles"));
    QFile truncatedFile(truncatedPath);
    if (!truncatedFile.open(QIODevice::WriteOnly)) {
        *errorString = QStringLiteral(
                    "Could not create the truncated tiledef test file");
        return false;
    }
    QDataStream truncatedStream(&truncatedFile);
    truncatedStream.setByteOrder(QDataStream::LittleEndian);
    truncatedStream.writeRawData("tdef", 4);
    truncatedStream << qint32(1) << qint32(1);
    truncatedStream.writeRawData("unterminated-name", 17);
    truncatedFile.close();
    TileDefFile truncated;
    if (truncated.read(truncatedPath) ||
            !truncated.errorString().contains(
                QStringLiteral("truncated name or image-source field"))) {
        *errorString = QStringLiteral(
                    "A truncated tiledef string did not produce the "
                    "targeted diagnostic:\n%1")
                .arg(truncated.errorString());
        return false;
    }

    TileDefFile oversized;
    for (int index = 0;
         index < TileDefFile::MAX_TILESET_ID_MODS + 1;
         ++index) {
        TileDefTileset *tileset = new TileDefTileset;
        tileset->mName = QStringLiteral("validation_%1")
                .arg(index, 4, 10, QLatin1Char('0'));
        tileset->mImageSource = tileset->mName
                + QLatin1String(".png");
        tileset->mColumns = 1;
        tileset->mRows = 1;
        tileset->mID = index + 1;
        tileset->mTiles += new TileDefTile(tileset, 0);
        oversized.insertTileset(
                    oversized.tilesets().size(), tileset);
    }

    QString validationError;
    if (oversized.validate(TileDefFile::ModFormat,
                           &validationError)) {
        *errorString = QStringLiteral(
                    "An oversized mod tiledef unexpectedly validated");
        return false;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the temporary tiledef directory");
        return false;
    }
    const QString outputFile =
            QDir(temporaryDirectory.path()).filePath(
                QStringLiteral("legacy.tiles"));
    QStringList outputs;
    if (!oversized.writeModSeries(outputFile, &outputs)) {
        *errorString = oversized.errorString();
        return false;
    }
    if (outputs.size() != 2
            || !outputs.at(0).endsWith(
                QLatin1String("legacy_1.tiles"))
            || !outputs.at(1).endsWith(
                QLatin1String("legacy_2.tiles"))) {
        *errorString = QStringLiteral(
                    "Unexpected split output names: %1")
                .arg(outputs.join(QStringLiteral(", ")));
        return false;
    }

    const int expectedCounts[] = {
        TileDefFile::MAX_TILESET_ID_MODS, 1
    };
    for (int part = 0; part < outputs.size(); ++part) {
        TileDefFile repaired;
        if (!repaired.read(outputs.at(part))) {
            *errorString = repaired.errorString();
            return false;
        }
        if (repaired.tilesets().size() != expectedCounts[part]
                || !repaired.validate(TileDefFile::ModFormat,
                                      &validationError)) {
            *errorString = QStringLiteral(
                        "Split part %1 is invalid: %2")
                    .arg(part + 1).arg(validationError);
            return false;
        }
        if (repaired.tilesets().first()->mID != 1
                || repaired.tilesets().last()->mID
                != expectedCounts[part]) {
            *errorString = QStringLiteral(
                        "Split part %1 did not receive sequential IDs")
                    .arg(part + 1);
            return false;
        }
        if (!QFileInfo(outputs.at(part)
                       + QLatin1String(".txt")).isFile()) {
            *errorString = QStringLiteral(
                        "Split part %1 has no text companion")
                    .arg(part + 1);
            return false;
        }
    }

    QString comparatorSummary;
    if (!TileDefCompare::runSelfTest(
            &comparatorSummary, errorString)) {
        return false;
    }
    qInfo() << "Validated detailed malformed-value/truncated-field "
               "diagnostics, enhanced comparator analysis, and B42 "
               "tiledef split:"
            << oversized.tilesets().size() << "tilesets ->"
            << outputs.size() << "files;"
            << comparatorSummary;
    return true;
}

static bool validateBuildingLuaFurniture(
        BuildingEditor::BuildingDocument *document, QString *errorString)
{
    static const char scriptSource[] =
            "assert(building.apiVersion >= 3)\n"
            "local groups = building:furnitureGroupNames()\n"
            "assert(#groups > 0, 'empty furniture catalog')\n"
            "local function findPlaceable()\n"
            "  for groupIndex = 0, #groups - 1 do\n"
            "    local count = building:furnitureCount(groupIndex)\n"
            "    for furnitureIndex = 0, count - 1 do\n"
            "      local orientations = building:furnitureOrientations(\n"
            "          groupIndex, furnitureIndex)\n"
            "      for _, orientation in ipairs(orientations) do\n"
            "        local width, height = building:furnitureSize(\n"
            "            groupIndex, furnitureIndex, orientation)\n"
            "        if width <= building:width() - 2 and\n"
            "            height <= building:height() - 2 then\n"
            "          for y = 0, height - 1 do\n"
            "            for x = 0, width - 1 do\n"
            "              local tileName = building:furnitureTileAt(\n"
            "                  groupIndex, furnitureIndex, orientation, x, y)\n"
            "              if tileName ~= '' then\n"
            "                local matches = building:findFurniture(tileName)\n"
            "                assert(#matches > 0,\n"
            "                    'reverse furniture lookup returned no match')\n"
            "                return groupIndex, furnitureIndex, orientation\n"
            "              end\n"
            "            end\n"
            "          end\n"
            "        end\n"
            "      end\n"
            "    end\n"
            "  end\n"
            "end\n"
            "local groupIndex, furnitureIndex, orientation = findPlaceable()\n"
            "assert(groupIndex, 'no placeable furniture definition')\n"
            "local level = building:currentLevel()\n"
            "local before = building:objectCount(level)\n"
            "local objectIndex = building:placeFurniture(\n"
            "    level, 1, 1, groupIndex, furnitureIndex, orientation)\n"
            "assert(objectIndex == before, 'unexpected furniture object index')\n"
            "assert(building:objectCount(level) == before + 1)\n"
            "assert(building:objectType(level, objectIndex) == 'Furniture')\n";

    QTemporaryFile scriptFile(
                QDir::tempPath()
                + QLatin1String("/buildinged-lua-validation-XXXXXX.lua"));
    if (!scriptFile.open()) {
        *errorString = QStringLiteral("Could not create the temporary Lua test");
        return false;
    }
    if (scriptFile.write(scriptSource) != qint64(sizeof(scriptSource) - 1)
            || !scriptFile.flush()) {
        *errorString = QStringLiteral("Could not write the temporary Lua test");
        return false;
    }

    const int objectCountBefore =
            document->building()->floor(0)->objectCount();
    BuildingEditor::BuildingLuaScript script(document);
    QString luaError;
    if (!script.run(scriptFile.fileName(), &luaError)) {
        *errorString = QStringLiteral("Lua execution failed: %1").arg(luaError);
        return false;
    }
    if (!script.applyChanges(QStringLiteral(
                                 "BuildingEd Lua furniture validation"))) {
        *errorString = QStringLiteral(
                    "Lua furniture placement produced no document change");
        return false;
    }

    QUndoStack *undoStack = document->undoStack();
    if (document->building()->floor(0)->objectCount()
            != objectCountBefore + 1 || !undoStack->canUndo()) {
        *errorString = QStringLiteral(
                    "Lua furniture placement was not committed correctly");
        return false;
    }
    undoStack->undo();
    if (document->building()->floor(0)->objectCount()
            != objectCountBefore || !undoStack->canRedo()) {
        *errorString = QStringLiteral(
                    "Lua furniture placement Undo validation failed");
        return false;
    }
    undoStack->redo();
    if (document->building()->floor(0)->objectCount()
            != objectCountBefore + 1) {
        *errorString = QStringLiteral(
                    "Lua furniture placement Redo validation failed");
        return false;
    }
    undoStack->undo();
    undoStack->clear();
    qInfo() << "BuildingEd Lua furniture validation: placement, Undo and Redo PASS";
    return true;
}
#endif

class CommandLineHandler : public CommandLineParser
{
public:
    CommandLineHandler();

    bool quit;
    bool showedVersion;
    bool disableOpenGL;
    bool validateBuildingCategories;
    bool validateBrushPerformance;
    bool validateDepthMapEditor;
    bool validateLootDistributions;
    bool validatePackTools;
    bool validateAutomapperRules;
    bool renderPackComparator;
    bool renderPackExtractor;
    bool renderTileDefComparator;
    bool renderLootDistributions;
    bool validateTileDefSplit;
    bool validateTilesetCatalog;

private:
    void showVersion();
    void justQuit();
    void setDisableOpenGL();
    void setValidateBuildingCategories();
    void setValidateBrushPerformance();
    void setValidateDepthMapEditor();
    void setValidateLootDistributions();
    void setValidatePackTools();
    void setValidateAutomapperRules();
    void setRenderPackComparator();
    void setRenderPackExtractor();
    void setRenderTileDefComparator();
    void setRenderLootDistributions();
    void setValidateTileDefSplit();
    void setValidateTilesetCatalog();

    // Convenience wrapper around registerOption
    template <void (CommandLineHandler::*memberFunction)()>
    void option(QChar shortName,
                const QString &longName,
                const QString &help)
    {
        registerOption<CommandLineHandler, memberFunction>(this,
                                                           shortName,
                                                           longName,
                                                           help);
    }
};

} // anonymous namespace


CommandLineHandler::CommandLineHandler()
    : quit(false)
    , showedVersion(false)
    , disableOpenGL(false)
    , validateBuildingCategories(false)
    , validateBrushPerformance(false)
    , validateDepthMapEditor(false)
    , validateLootDistributions(false)
    , validatePackTools(false)
    , validateAutomapperRules(false)
    , renderPackComparator(false)
    , renderPackExtractor(false)
    , renderTileDefComparator(false)
    , renderLootDistributions(false)
    , validateTileDefSplit(false)
    , validateTilesetCatalog(false)
{
    option<&CommandLineHandler::showVersion>(
                QLatin1Char('v'),
                QLatin1String("--version"),
                QLatin1String("Display the version"));

    option<&CommandLineHandler::justQuit>(
                QChar(),
                QLatin1String("--quit"),
                QLatin1String("Only check validity of arguments, "
                              "don't actually load any files"));

    option<&CommandLineHandler::setDisableOpenGL>(
                QChar(),
                QLatin1String("--disable-opengl"),
                QLatin1String("Disable hardware accelerated rendering"));

    option<&CommandLineHandler::setValidateBuildingCategories>(
                QChar(),
                QLatin1String("--validate-building-categories"),
                QLatin1String("Load and validate every BuildingEd tile category"));

    option<&CommandLineHandler::setValidateBrushPerformance>(
                QChar(),
                QLatin1String("--validate-brush-performance"),
                QLatin1String("Validate sparse undo growth for tile painting"));

    option<&CommandLineHandler::setValidateDepthMapEditor>(
                QChar(),
                QLatin1String("--validate-depthmap-editor"),
                QLatin1String("Validate Build 42 depth atlas editing and PNG output"));

    option<&CommandLineHandler::setValidateLootDistributions>(
                QChar(),
                QLatin1String("--validate-loot-distributions"),
                QLatin1String("Validate game loot registries; pass game root "
                              "and optional project root as file arguments"));

    option<&CommandLineHandler::setValidatePackTools>(
                QChar(),
                QLatin1String("--validate-pack-tools"),
                QLatin1String("Validate .pack reading, comparison, hashing "
                              "and extraction"));

    option<&CommandLineHandler::setValidateAutomapperRules>(
                QChar(),
                QLatin1String("--validate-automapper-rules"),
                QLatin1String("Validate Automapper manifest selection and "
                              "WorldEd Rules.txt isolation"));

    option<&CommandLineHandler::setRenderPackComparator>(
                QChar(),
                QLatin1String("--render-pack-comparator"),
                QLatin1String("Render the enhanced .pack comparator; pass "
                              "the output PNG as a file argument"));

    option<&CommandLineHandler::setRenderPackExtractor>(
                QChar(),
                QLatin1String("--render-pack-extractor"),
                QLatin1String("Render the versatile .pack extractor; pass "
                              "the output PNG as a file argument"));

    option<&CommandLineHandler::setRenderTileDefComparator>(
                QChar(),
                QLatin1String("--render-tiledef-comparator"),
                QLatin1String("Render the enhanced .tiles comparator; pass "
                              "the output PNG as a file argument"));

    option<&CommandLineHandler::setRenderLootDistributions>(
                QChar(),
                QLatin1String("--render-loot-distributions"),
                QLatin1String("Render the loot editor; pass game root, "
                              "output PNG and optional project root"));

    option<&CommandLineHandler::setValidateTileDefSplit>(
                QChar(),
                QLatin1String("--validate-tiledef-split"),
                QLatin1String("Validate B42 mod tiledef limits and splitting"));

    option<&CommandLineHandler::setValidateTilesetCatalog>(
                QChar(),
                QLatin1String("--validate-tileset-catalog"),
                QLatin1String("Validate one-row tileset catalogue recovery"));
}

void CommandLineHandler::showVersion()
{
    if (!showedVersion) {
        showedVersion = true;
        qInfo() << "Tiled (Qt) Map Editor"
                << qPrintable(QApplication::applicationVersion());
        quit = true;
    }
}

void CommandLineHandler::justQuit()
{
    quit = true;
}

void CommandLineHandler::setDisableOpenGL()
{
    disableOpenGL = true;
}

void CommandLineHandler::setValidateBuildingCategories()
{
    validateBuildingCategories = true;
}

void CommandLineHandler::setValidateBrushPerformance()
{
    validateBrushPerformance = true;
}

void CommandLineHandler::setValidateDepthMapEditor()
{
    validateDepthMapEditor = true;
}

void CommandLineHandler::setValidateLootDistributions()
{
    validateLootDistributions = true;
}

void CommandLineHandler::setValidatePackTools()
{
    validatePackTools = true;
}

void CommandLineHandler::setValidateAutomapperRules()
{
    validateAutomapperRules = true;
}

void CommandLineHandler::setRenderPackComparator()
{
    renderPackComparator = true;
}

void CommandLineHandler::setRenderPackExtractor()
{
    renderPackExtractor = true;
}

void CommandLineHandler::setRenderTileDefComparator()
{
    renderTileDefComparator = true;
}

void CommandLineHandler::setRenderLootDistributions()
{
    renderLootDistributions = true;
}

void CommandLineHandler::setValidateTileDefSplit()
{
    validateTileDefSplit = true;
}

void CommandLineHandler::setValidateTilesetCatalog()
{
    validateTilesetCatalog = true;
}

#if !defined(QT_NO_DEBUG) && defined(ZOMBOID) && defined(_MSC_VER)
static void __cdecl invalid_parameter_handler(
   const wchar_t * expression,
   const wchar_t * function,
   const wchar_t * file,
   unsigned int line,
   uintptr_t pReserved)
{
    qDebug() << expression << function << file << line;
}

#endif

int main(int argc, char *argv[])
{
#if !defined(QT_NO_DEBUG) && defined(ZOMBOID) && defined(_MSC_VER)
    _set_invalid_parameter_handler(invalid_parameter_handler);
#endif

    /*
     * On X11, Tiled uses the 'raster' graphics system by default, because the
     * X11 native graphics system has performance problems with drawing the
     * tile grid.
     */
#ifdef Q_WS_X11
    QApplication::setGraphicsSystem(QLatin1String("raster"));
#endif

    TiledApplication a(argc, argv);

#ifdef ZOMBOID
    Q_INIT_RESOURCE(buildingeditor);
#endif

    a.setOrganizationName(QLatin1String("TheIndieStone"));
#ifdef ZOMBOID
    const bool buildingEditorMode = QFileInfo(QCoreApplication::applicationFilePath())
            .completeBaseName().compare(QLatin1String("BuildingEd"), Qt::CaseInsensitive) == 0;
    a.setApplicationName(buildingEditorMode
                         ? QLatin1String("BuildingEd")
                         : QLatin1String("TileZed"));
    if (buildingEditorMode) {
        QIcon buildingEditorIcon(QLatin1String(":images/buildinged-icon-16.png"));
        buildingEditorIcon.addFile(QLatin1String(":images/buildinged-icon-32.png"));
        a.setWindowIcon(buildingEditorIcon);
    }
#else
    a.setApplicationName(QLatin1String("TileZed"));
#endif
    PortableSettings::configure();
    PortableSettings::installLogging();
    PortableSettings::prepareVersionedSettings();
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.8.1"));
#endif

#ifdef Q_WS_MAC
    a.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    LanguageManager *languageManager = LanguageManager::instance();
    languageManager->installTranslators();

    CommandLineHandler commandLine;

    if (!commandLine.parse(QCoreApplication::arguments()))
        return 0;
    if (commandLine.quit)
        return 0;
    if (commandLine.validateBrushPerformance) {
        QString error;
        const bool valid = validateBrushUndoBuffers(&error);
        qInfo().noquote()
                << "TileZed brush-performance validation:"
                << (valid ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateDepthMapEditor) {
        QString error;
        const bool valid = DepthMapEditor::runFormatSelfTest(&error);
        qInfo().noquote()
                << "TileZed depth-map editor validation:"
                << (valid ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateLootDistributions) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << "Loot-distribution validation expects a game "
                           "directory argument.";
            return 1;
        }
        QString summary;
        QString error;
        const bool valid = LootDistributionDialog::validateDefinitions(
                    paths.first(),
                    paths.size() > 1 ? paths.at(1) : QString(),
                    &summary, &error);
        qInfo().noquote()
                << "Procedural loot validation:"
                << (valid ? QStringLiteral("PASS: %1").arg(summary)
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validatePackTools) {
        QString compareSummary;
        QString extractSummary;
        QString error;
        const bool compareValid =
                PackCompare::runSelfTest(&compareSummary, &error);
        if (!compareValid) {
            qInfo().noquote()
                    << "TileZed pack-tools validation:"
                    << QStringLiteral("FAIL: %1").arg(error);
            return 1;
        }
        const bool extractValid =
                PackExtractDialog::runSelfTest(
                    &extractSummary, &error);
        const bool valid = compareValid && extractValid;
        qInfo().noquote()
                << "TileZed pack-tools validation:"
                << (valid
                    ? QStringLiteral("PASS: %1; %2")
                      .arg(compareSummary, extractSummary)
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateAutomapperRules) {
        QString summary;
        QString error;
        const bool valid =
                AutomappingManager::runRuleListSelfTest(
                    &summary, &error);
        qInfo().noquote()
                << "TileZed Automapper-rules validation:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(summary)
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderPackComparator) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << "Pack-comparator rendering expects an "
                           "output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = PackCompare::renderValidation(
                    paths.first(), &error);
        qInfo().noquote()
                << "TileZed pack-comparator render:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(paths.first())
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderPackExtractor) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << "Pack-extractor rendering expects an "
                           "output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = PackExtractDialog::renderValidation(
                    paths.first(), &error);
        qInfo().noquote()
                << "TileZed pack-extractor render:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(paths.first())
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderTileDefComparator) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << ".tiles-comparator rendering expects an "
                           "output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = TileDefCompare::renderValidation(
                    paths.first(), &error);
        qInfo().noquote()
                << "TileZed .tiles-comparator render:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(paths.first())
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderLootDistributions) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.size() < 2) {
            qCritical() << "Loot editor rendering expects a game directory "
                           "and output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = LootDistributionDialog::renderValidation(
                    paths.at(0),
                    paths.size() > 2 ? paths.at(2) : QString(),
                    paths.at(1), &error);
        qInfo().noquote()
                << "Procedural loot editor render:"
                << (valid ? QStringLiteral("PASS: %1").arg(paths.at(1))
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateTileDefSplit) {
        QString error;
        const bool valid = validateTileDefSplitting(&error);
        qInfo().noquote()
                << "TileZed tiledef-split validation:"
                << (valid ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;
    if (commandLine.disableOpenGL) {
        Preferences::instance()->setUseOpenGL(false);
        if (buildingEditorMode)
            BuildingEditor::BuildingPreferences::instance()
                    ->setUseOpenGL(false);
    }

#ifdef ZOMBOID
    Preferences::instance()->applyTheme();
    if (buildingEditorMode) {
        BuildingEditor::BuildingEditorWindow buildingEditor;
        buildingEditor.show();
        buildingEditor.readSettings();

        // Let the event loop paint the window and progress dialog before the
        // potentially expensive tileset scan and image decoding begins.
        QTimer::singleShot(0, &buildingEditor,
                           [&buildingEditor, &commandLine]() {
            if (!MainWindow::InitConfigFiles(&buildingEditor) ||
                    !buildingEditor.Startup()) {
                buildingEditor.close();
                QCoreApplication::quit();
                return;
            }

            if (commandLine.validateTilesetCatalog) {
                QString error;
                const bool valid =
                        validateSingleRowTilesetCatalog(&error);
                qInfo().noquote()
                        << "BuildingEd one-row tileset validation:"
                        << (valid ? QStringLiteral("PASS")
                                  : QStringLiteral("FAIL: %1").arg(error));
                QCoreApplication::exit(valid ? 0 : 3);
                return;
            }

            if (commandLine.validateBuildingCategories) {
                BuildingEditor::BuildingTemplate *buildingTemplate = nullptr;
                BuildingEditor::BuildingTemplates *templates =
                        BuildingEditor::BuildingTemplates::instance();
                if (templates->templateCount() > 0) {
                    buildingTemplate = templates->templateAt(
                                templates->templateCount() / 2);
                }
                BuildingEditor::Building *building =
                        new BuildingEditor::Building(
                            17, 23, buildingTemplate);
                building->insertFloor(
                            0, new BuildingEditor::BuildingFloor(
                                building, 0));
                BuildingEditor::BuildingDocument *document =
                        new BuildingEditor::BuildingDocument(
                            building, QString());
                BuildingEditor::BuildingDocumentMgr::instance()
                        ->addDocument(document);
                QCoreApplication::processEvents(
                            QEventLoop::ExcludeUserInputEvents);
                qInfo().noquote()
                        << "BuildingEd category validation document:"
                        << "17x23, template="
                        << (buildingTemplate
                            ? buildingTemplate->name()
                            : QStringLiteral("<default>"));

                QString templateTilesError;
                const bool templateTilesValid =
                        validateAllBuildingTemplates(
                            &templateTilesError);
                qInfo().noquote()
                        << "BuildingEd all-template tile validation:"
                        << (templateTilesValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(templateTilesError));

                BuildingEditor::BuildingTilesetDock *tilesetDock =
                        buildingEditor.findChild<
                            BuildingEditor::BuildingTilesetDock *>();
                QString tileModeError;
                const bool tileModeValid = tilesetDock
                        && tilesetDock->validateTilesetCatalog(
                            &tileModeError);
                qInfo().noquote()
                        << "BuildingEd Tile mode validation:"
                        << (tileModeValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(tileModeError));

                BuildingEditor::BuildingTilesDialog *tilesDialog =
                        BuildingEditor::BuildingTilesDialog::instance();
                QString tilesDialogError;
                const bool tilesDialogValid = tilesDialog
                        && tilesDialog->validateTilesetCatalog(
                            &tilesDialogError);
                qInfo().noquote()
                        << "BuildingEd Building > Tiles validation:"
                        << (tilesDialogValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(tilesDialogError));

                BuildingEditor::BuildingFurnitureDock *furnitureDock =
                        buildingEditor.findChild<
                            BuildingEditor::BuildingFurnitureDock *>();
                QString furnitureError;
                const bool furnitureValid = furnitureDock
                        && furnitureDock->validateFurnitureCatalog(
                            &furnitureError);
                qInfo().noquote()
                        << "BuildingEd Furniture validation:"
                        << (furnitureValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(furnitureError));

                const QList<BuildingEditor::CategoryDock *> categoryDocks =
                        buildingEditor.findChildren<
                            BuildingEditor::CategoryDock *>();
                bool categoriesValid = categoryDocks.count() == 2;
                int categoryDockIndex = 0;
                for (BuildingEditor::CategoryDock *categoryDock
                     : categoryDocks) {
                    const bool dockValid = categoryDock
                            && categoryDock->validateAllTileCategories();
                    qInfo() << "BuildingEd object-mode category dock"
                            << ++categoryDockIndex << "of"
                            << categoryDocks.count() << ":"
                            << (dockValid ? "PASS" : "FAIL");
                    categoriesValid = categoriesValid && dockValid;
                }
                const bool valid = tileModeValid
                        && tilesDialogValid
                        && templateTilesValid
                        && furnitureValid
                        && categoriesValid;
                QString luaFurnitureError;
                const bool luaFurnitureValid =
                        validateBuildingLuaFurniture(
                            document, &luaFurnitureError);
                qInfo().noquote()
                        << "BuildingEd Lua furniture validation:"
                        << (luaFurnitureValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(luaFurnitureError));
                const bool allValid = valid && luaFurnitureValid;
                qInfo() << "BuildingEd category validation result:"
                        << (allValid ? "PASS" : "FAIL");
                BuildingEditor::BuildingDocumentMgr::instance()
                        ->closeAllDocuments();
                QCoreApplication::exit(allValid ? 0 : 2);
                return;
            }

            foreach (const QString &fileName, commandLine.filesToOpen())
                buildingEditor.openFile(fileName);

            // Startup and opening a building can recalculate embedded dock and
            // splitter sizes. Restore once more before autosave is enabled.
            buildingEditor.readSettings();
            buildingEditor.startSettingsAutoSave();
            buildingEditor.raise();
            buildingEditor.activateWindow();
        });

        return a.exec();
    }

    if (a.isRunning()) {
        if (!commandLine.filesToOpen().isEmpty()) {
            foreach (const QString &fileName, commandLine.filesToOpen())
                a.sendMessage(fileName);
            return 0;
        }
    }
#endif

    MainWindow w;
#ifdef ZOMBOID
    ZProgressManager::instance()->setMainWindow(&w);
#endif
    w.show();
#ifdef ZOMBOID
    a.setActivationWindow(&w);
    w.connect(&a, &QtSingleApplication::messageReceived, &w, qOverload<const QString&>(&MainWindow::openFile));
    w.readSettings();

    if (!w.InitConfigFiles())
        return 0;

    if (commandLine.validateTilesetCatalog) {
        QString error;
        const bool valid = validateSingleRowTilesetCatalog(&error);
        qInfo().noquote()
                << "TileZed one-row tileset validation:"
                << (valid ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 3;
    }

    QSettings sessionSettings(QSettings::IniFormat, QSettings::UserScope,
                              QLatin1String("TheIndieStone"),
                              QLatin1String("TileZed"));
    const QString cleanExitKey =
            QLatin1String("Startup/PreviousSessionClosedCleanly");
    const bool previousSessionClosedCleanly =
            sessionSettings.value(cleanExitKey, true).toBool();
    sessionSettings.setValue(cleanExitKey, false);
    sessionSettings.sync();

    foreach (QString f, Preferences::instance()->worldedFiles()) {
        if (f.isEmpty())
            continue;
        if (QFileInfo::exists(f) == false) {
            QMessageBox::warning(&w, QLatin1String("Missing PZW"), QLatin1String("WorldEd project not found:\n%1").arg(f));
            continue;
        }
        WorldEd::WorldEdMgr::instance()->addProject(f);
    }

    for (const QString &f : Preferences::instance()->tilePropertiesFiles()) {
        if (f.isEmpty())
            continue;
        if (QFileInfo::exists(f) == false) {
            QMessageBox::warning(&w, QLatin1String("File Not Found"), QLatin1String("Tile properties file not found.\nChange this in the Preferences.\n%1").arg(f));
            continue;
        }
    }
#endif // ZOMBOID

    QObject::connect(&a, &TiledApplication::fileOpenRequest,
                     &w, qOverload<const QString&>(&MainWindow::openFile));

    if (!commandLine.filesToOpen().isEmpty()) {
#ifdef ZOMBOID
        gStartupBlockRendering = false;
#endif
        foreach (const QString &fileName, commandLine.filesToOpen())
            w.openFile(fileName);
    } else if (Preferences::instance()->restoreLastSession()) {
        if (previousSessionClosedCleanly) {
            w.openLastFiles();
        } else {
            qWarning() << "Automatic TileZed session restore skipped after "
                          "an unclean shutdown.";
            QMessageBox::warning(
                        &w, QObject::tr("TileZed Session Recovery"),
                        QObject::tr(
                            "TileZed did not close cleanly last time.\n\n"
                            "Automatic document restoration was skipped to "
                            "avoid repeating a startup crash. Your map files "
                            "were not changed; open the required file "
                            "manually after checking settings/logs."));
        }
    }

#ifdef ZOMBOID
    // Tile loading and session restoration can change dock size hints after
    // the initial restore. Reapply the INI layout before enabling autosave.
    w.readSettings();
    w.startSettingsAutoSave();
#endif

    const int result = a.exec();
    sessionSettings.setValue(cleanExitKey, true);
    sessionSettings.sync();
    return result;
}
