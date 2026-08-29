/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
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

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QXmlStreamReader>
#include <limits>
#include "mainwindow.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"

#ifdef ZOMBOID
#include "documentmanager.h"
#include "document.h"
#include "bmptotmx.h"
#include "biomemapgeneratordialog.h"
#include "biomemapimageprocessor.h"
#include "biomemapitem.h"
#include "chunkdataoverride.h"
#include "osmterrainimportdialog.h"
#include "osmterrainimporter.h"
#include "otherworldsdialog.h"
#include "cellscene.h"
#include "defaultsfile.h"
#include "expectedpropertiesdialog.h"
#include "generatelotsdialog.h"
#include "scenetools.h"
#include "toolmanager.h"
#include "preferences.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "lotfilesmanager256.h"
#include "luawriter.h"
#include "progress.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "vehiclemeshpreview.h"
#include "streetnamesdock.h"
#include "regionsdock.h"
#include "InGameMap/ingamemapreader.h"
#include "InGameMap/ingamemapfeaturegenerator.h"
#include "InGameMap/ingamemapwriter.h"
#include "InGameMap/ingamemapwriterbinary.h"
#include "world.h"
#include "worlddocument.h"
#include "worldreader.h"
#include "worldwriter.h"
#include "worldobjectvalidation.h"
#include "worldscene.h"
#include "worldview.h"
#include "zlevelrenderer.h"
#include "worldgenpreviewdialog.h"
#include "tilesetcleanupdialog.h"
using namespace Tiled;
using namespace Tiled::Internal;
#endif

int main(int argc, char *argv[])
{
#if ZOMBOID
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#endif
    QApplication a(argc, argv);

    a.setOrganizationName(QLatin1String("TheIndieStone"));
    a.setApplicationName(QLatin1String("PZWorldEd"));
    PortableSettings::configure();
    PortableSettings::installLogging();
    PortableSettings::prepareVersionedSettings();
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.0.1"));
#endif

#ifdef Q_WS_MAC
    a.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    const QStringList commandLineArguments = a.arguments().mid(1);
    QString validateBmpGenerationProject;
    QString validateWorldGenPrefabImport;
    QString renderWorldGenPreviewRoot;
    QString renderWorldGenPrefabRoot;
    QString renderWorldGenPrefabWindowRoot;
    QString renderTilesetCleanupRoot;
    QString renderWorldGenPreviewOutput;
    QString auditTilesetCleanupPath;
    QString rebuildTilesetCatalogPath;
    QString validateNative256RoomDefsTmx;
    QString validateInGameMapBuildingGeneration;
    QString validateWorldMapOverlays;
    QString validateThumbnailLifecycleProject;
    bool validateTilesetCleanup = false;
    bool validateCellMoveCoordinates = false;
    for (const QString &argument : commandLineArguments) {
        const QString thumbnailLifecyclePrefix =
                QLatin1String("--validate-thumbnail-lifecycle=");
        if (argument.startsWith(thumbnailLifecyclePrefix)) {
            validateThumbnailLifecycleProject =
                    argument.mid(thumbnailLifecyclePrefix.length());
            continue;
        }
        if (argument == QLatin1String(
                    "--validate-chunkdata-overrides")) {
            QString summary;
            QString error;
            if (!ChunkDataOverride::validateWorkflow(&summary, &error)) {
                qCritical().noquote()
                        << "Chunk data override validation failed:" << error;
                return 58;
            }
            QTemporaryDir directory;
            if (!directory.isValid()) {
                qCritical() << "Chunk data override PZW validation could not create a temporary directory";
                return 58;
            }
            const QString projectPath = directory.filePath(
                        QStringLiteral("project.pzw"));
            const QString overridePath = directory.filePath(
                        QStringLiteral("chunkdata-overrides/chunkdata_0_0.png"));
            QImage overrideImage(ChunkDataOverride::ImageSize,
                                 ChunkDataOverride::ImageSize,
                                 QImage::Format_ARGB32);
            overrideImage.fill(Qt::transparent);
            overrideImage.setPixel(8, 9, qRgba(17, 0, 0, 255));
            if (!ChunkDataOverride::saveImage(
                        overridePath, overrideImage, &error)) {
                qCritical().noquote()
                        << "Chunk data override PZW validation failed:" << error;
                return 58;
            }
            World *source = new World(1, 1, WorldGridFormat::Native256);
            source->cellAt(0, 0)->setChunkDataOverrideFilePath(overridePath);
            WorldWriter writer;
            if (!writer.writeWorld(source, projectPath)) {
                qCritical().noquote()
                        << "Chunk data override PZW write failed:"
                        << writer.errorString();
                delete source;
                return 58;
            }
            delete source;
            WorldReader reader;
            World *loaded = reader.readWorld(projectPath);
            if (!loaded
                    || loaded->gridFormat() != WorldGridFormat::Native256
                    || QFileInfo(loaded->cellAt(0, 0)
                                 ->chunkDataOverrideFilePath())
                       .absoluteFilePath()
                       != QFileInfo(overridePath).absoluteFilePath()) {
                qCritical().noquote()
                        << "Chunk data override PZW round trip failed:"
                        << reader.errorString();
                delete loaded;
                return 58;
            }
            delete loaded;
            qInfo().noquote()
                    << "Chunk data override validation passed:"
                    << summary
                    << ", PZW relative-path round trip passed";
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-spawnpoint-export")) {
            QString summary;
            QString error;
            if (!LuaWriter::validateSpawnPointExport(&summary, &error)) {
                qCritical().noquote()
                        << "SpawnPoint export validation failed:" << error;
                return 55;
            }
            qInfo().noquote()
                    << "SpawnPoint export validation passed:" << summary;
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-zone-export")) {
            QString summary;
            QString error;
            if (!LuaWriter::validateZoneExport(&summary, &error)) {
                qCritical().noquote()
                        << "Zone export validation failed:" << error;
                return 56;
            }
            qInfo().noquote()
                    << "Zone export validation passed:" << summary;
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-linked-world-projects")) {
            QString summary;
            QString error;
            if (!OtherWorldsDialog::validateWorkflow(&summary, &error)) {
                qCritical().noquote()
                        << "Linked-world validation failed:" << error;
                return 53;
            }
            qInfo().noquote()
                    << "Linked-world validation passed:" << summary;
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-bmp-metadata-only")) {
            QString summary;
            QString error;
            if (!BMPToTMX::validateMetadataOnly(&summary, &error)) {
                qCritical().noquote()
                        << "BMP to TMX metadata-only validation failed:"
                        << error;
                return 41;
            }
            qInfo().noquote()
                    << "BMP to TMX metadata-only validation passed:"
                    << summary;
            return 0;
        }
        const QString nativeRoomDefsPrefix =
                QLatin1String("--validate-native-256-roomdefs=");
        if (argument.startsWith(nativeRoomDefsPrefix)) {
            validateNative256RoomDefsTmx =
                    argument.mid(nativeRoomDefsPrefix.size());
            continue;
        }
        const QString buildingGenerationPrefix =
                QLatin1String("--validate-ingamemap-building-generation=");
        if (argument.startsWith(buildingGenerationPrefix)) {
            validateInGameMapBuildingGeneration =
                    argument.mid(buildingGenerationPrefix.size());
            continue;
        }
        const QString worldMapOverlaysPrefix =
                QLatin1String("--validate-worldmap-overlays=");
        if (argument.startsWith(worldMapOverlaysPrefix)) {
            validateWorldMapOverlays =
                    argument.mid(worldMapOverlaysPrefix.size());
            continue;
        }
        const QString osmWizardRenderPrefix =
                QLatin1String("--render-osm-project-wizard=");
        if (argument.startsWith(osmWizardRenderPrefix)) {
            const QString output =
                    argument.mid(osmWizardRenderPrefix.size());
            OsmTerrainImportDialog dialog(nullptr);
            dialog.show();
            a.processEvents();
            if (!dialog.grab().save(output)) {
                qCritical() << "Could not save OSM project-wizard render:"
                            << output;
                return 36;
            }
            qInfo() << "OSM project-wizard render saved:"
                    << output << dialog.size();
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-biomemap-config")) {
            QString error;
            if (!BiomeMapImageProcessor::validateConfiguration(&error)) {
                qCritical().noquote()
                        << "BiomeMapConfig validation failed:" << error;
                return 32;
            }
            QString fallbackSummary;
            if (!BiomeMapGeneratorDialog::validateFallbackBehavior(
                        &fallbackSummary, &error)) {
                qCritical().noquote()
                        << "BiomeMap fallback validation failed:" << error;
                return 32;
            }
            if (!BiomeMapItem::validateChannelPainting(&error)) {
                qCritical().noquote()
                        << "BiomeMap channel-paint validation failed:" << error;
                return 32;
            }
            qInfo().noquote() << "BiomeMapConfig validation passed:"
                              << BiomeMapImageProcessor::palette().size()
                              << "entries,"
                              << fallbackSummary
                              << ", red and green channel painting preserved "
                                 "the other bytes, and green strokes remained "
                                 "aligned to complete 8 x 8 chunks";
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-cell-move-coordinates")) {
            validateCellMoveCoordinates = true;
            continue;
        }
        if (argument == QLatin1String(
                    "--validate-ingamemap-road-generation")) {
            QString summary;
            QString error;
            if (!InGameMapFeatureGenerator::validateRoadMaskProcessing(
                        &summary, &error)) {
                qCritical().noquote()
                        << "InGameMap road-generation validation failed:"
                        << error;
                return 55;
            }
            QString budgetSummary;
            if (!validateInGameMapRendererBudget(
                        &budgetSummary, &error)) {
                qCritical().noquote()
                        << "InGameMap renderer-budget validation failed:"
                        << error;
                return 55;
            }
            qInfo().noquote()
                    << "InGameMap road-generation validation passed:"
                    << summary << ";" << budgetSummary;
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-preferences-noop")) {
            Preferences *preferences = Preferences::instance();
            if (TileMetaInfoMgr::instance()->changeTilesDirectory(
                        preferences->tilesDirectory())) {
                qCritical() << "Preferences no-op validation failed: unchanged Tiles directory triggered a reload";
                return 56;
            }
            qInfo() << "Preferences no-op validation passed: unchanged Tiles directory did not reload tilesets";
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-generate-lots-paths")) {
            QString error;
            if (!GenerateLotsDialog::validatePathSelection(&error)) {
                qCritical().noquote()
                        << "Generate Lots path validation failed:" << error;
                return 57;
            }
            qInfo() << "Generate Lots path validation passed";
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-ingamemap-forest-export")) {
            QString summary;
            QString error;
            if (!MainWindow::validateInGameMapForestExport(
                        &summary, &error)) {
                qCritical().noquote()
                        << "InGameMap Forest export validation failed:"
                        << error;
                return 33;
            }
            qInfo().noquote()
                    << "InGameMap Forest export validation passed:"
                    << summary;
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-osm-terrain-import")) {
            QString summary;
            QString error;
            if (!OsmTerrainImporter::validate(&summary, &error)) {
                qCritical().noquote()
                        << "OpenStreetMap terrain validation failed:"
                        << error;
                return 34;
            }
            if (!QSslSocket::supportsSsl()) {
                qCritical().noquote()
                        << "OpenStreetMap terrain validation failed: "
                           "HTTPS support is unavailable. Qt was built for"
                        << QSslSocket::sslLibraryBuildVersionString()
                        << "but no compatible SSL runtime was loaded.";
                return 35;
            }
            qInfo().noquote()
                    << "OpenStreetMap terrain validation passed:"
                    << summary
                    << "| SSL runtime:"
                    << QSslSocket::sslLibraryVersionString();
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-native-256-lot-geometry")) {
            QString error;
            if (!LotFilesManager256::validateNative256Geometry(&error)) {
                qCritical().noquote()
                        << "Native-256 lot geometry validation failed:"
                        << error;
                return 18;
            }
            qInfo() << "Native-256 lot geometry validation passed";
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-partial-chunks")) {
            QString error;
            if (!PZTools::PartialChunkSelection::validate(&error)) {
                qCritical().noquote()
                        << "Partial Chunks validation failed:" << error;
                return 42;
            }
            qInfo() << "Partial Chunks validation passed";
            return 0;
        }
        if (argument == QLatin1String("--validate-hole-repair")) {
            QString error;
            if (!CellScene::validateHoleRepair(&error)) {
                qCritical().noquote()
                        << "Hole Detection repair validation failed:"
                        << error;
                return 31;
            }
            qInfo() << "Hole Detection repair validation passed";
            return 0;
        }
        if (argument == QLatin1String(
                    "--validate-basement-placement")) {
            QString error;
            if (!CellScene::validateBasementPlacement(&error)) {
                qCritical().noquote()
                        << "Basement placement validation failed:" << error;
                return 39;
            }
            qInfo() << "Basement placement validation passed";
            return 0;
        }
        const QString defaultsValidationPrefix =
                QLatin1String("--validate-world-defaults=");
        if (argument.startsWith(defaultsValidationPrefix)) {
            const QString path =
                    argument.mid(defaultsValidationPrefix.size());
            DefaultsFile defaults;
            if (!defaults.read(path)) {
                qCritical().noquote()
                        << "WorldDefaults.txt validation failed:"
                        << defaults.errorString();
                return 25;
            }
            for (const QString &typeName :
                 WorldObjectValidation::expectedObjectTypes()) {
                const QStringList propertyNames =
                        WorldObjectValidation::expectedPropertyNames(typeName);
                for (const QString &propertyName : propertyNames) {
                    if (!defaults.mPropertyDefs.findPropertyDef(propertyName)) {
                        qCritical().noquote()
                                << "WorldDefaults.txt guided-property validation failed:"
                                << typeName << "requires" << propertyName;
                        return 25;
                    }
                }
            }
            QString guidedSummary;
            QString guidedError;
            if (!ExpectedPropertiesDialog::validate(
                    path, &guidedSummary, &guidedError)) {
                qCritical().noquote()
                        << "WorldDefaults.txt guided-property dialog validation failed:"
                        << guidedError;
                return 25;
            }
            QString contextMenuError;
            if (!SelectMoveObjectTool::validateContextMenuDispatch(
                    &contextMenuError)) {
                qCritical().noquote()
                        << "WorldDefaults.txt object context-menu validation failed:"
                        << contextMenuError;
                return 25;
            }
            qInfo() << "WorldDefaults.txt validation passed:"
                    << defaults.mEnums.size() << "enum(s),"
                    << defaults.mPropertyDefs.size() << "property definition(s),"
                    << defaults.mTemplates.size() << "template(s),"
                    << defaults.mObjectTypes.size() << "object type(s),"
                    << defaults.mObjectGroups.size() << "object group(s),"
                    << WorldObjectValidation::expectedObjectTypes().size()
                    << "guided property type(s)";
            qInfo().noquote()
                    << "Guided-property dialog validation passed:"
                    << guidedSummary;
            qInfo() << "Object context-menu dispatch validation passed";
            return 0;
        }
        if (argument == QLatin1String("--validate-tileset-cleanup")) {
            validateTilesetCleanup = true;
            continue;
        }
        const QString rebuildCatalogPrefix =
                QLatin1String("--rebuild-tileset-catalog=");
        if (argument.startsWith(rebuildCatalogPrefix)) {
            rebuildTilesetCatalogPath =
                    argument.mid(rebuildCatalogPrefix.size());
            continue;
        }
        const QString auditTilesetCleanupPrefix =
                QLatin1String("--audit-tileset-cleanup=");
        if (argument.startsWith(auditTilesetCleanupPrefix)) {
            auditTilesetCleanupPath =
                    argument.mid(auditTilesetCleanupPrefix.size());
            continue;
        }
        const QString renderTilesetCleanupPrefix =
                QLatin1String("--render-tileset-cleanup=");
        if (argument.startsWith(renderTilesetCleanupPrefix)) {
            renderTilesetCleanupRoot =
                    argument.mid(renderTilesetCleanupPrefix.size());
            continue;
        }
        const QString renderWorldGenPrefabWindowPrefix =
                QLatin1String("--render-worldgen-prefab-window=");
        if (argument.startsWith(renderWorldGenPrefabWindowPrefix)) {
            renderWorldGenPrefabWindowRoot =
                    argument.mid(renderWorldGenPrefabWindowPrefix.size());
            continue;
        }
        const QString renderWorldGenPreviewPrefix =
                QLatin1String("--render-worldgen-preview=");
        if (argument.startsWith(renderWorldGenPreviewPrefix)) {
            renderWorldGenPreviewRoot =
                    argument.mid(renderWorldGenPreviewPrefix.size());
            continue;
        }
        const QString renderWorldGenPrefabPrefix =
                QLatin1String("--render-worldgen-prefab=");
        if (argument.startsWith(renderWorldGenPrefabPrefix)) {
            renderWorldGenPrefabRoot =
                    argument.mid(renderWorldGenPrefabPrefix.size());
            continue;
        }
        const QString worldGenPreviewOutputPrefix =
                QLatin1String("--worldgen-preview-output=");
        if (argument.startsWith(worldGenPreviewOutputPrefix)) {
            renderWorldGenPreviewOutput =
                    argument.mid(worldGenPreviewOutputPrefix.size());
            continue;
        }
        const QString worldGenValidationPrefix =
                QLatin1String("--validate-worldgen-preview=");
        if (argument.startsWith(worldGenValidationPrefix)) {
            const QString path =
                    argument.mid(worldGenValidationPrefix.size());
            QString summary;
            QString error;
            if (!WorldGenPreviewDialog::validateDefinitions(
                        path, &summary, &error)) {
                qCritical().noquote()
                        << "WorldGen preview validation failed:"
                        << error;
                return 10;
            }
            qInfo().noquote()
                    << "WorldGen preview validation passed:"
                    << summary;
            return 0;
        }
        const QString prefabImportValidationPrefix =
                QLatin1String("--validate-worldgen-prefab-import=");
        if (argument.startsWith(prefabImportValidationPrefix)) {
            validateWorldGenPrefabImport =
                    argument.mid(prefabImportValidationPrefix.size());
            continue;
        }
        const QString worldGenOverlayValidationPrefix =
                QLatin1String("--validate-worldgen-project-overlay=");
        if (argument.startsWith(worldGenOverlayValidationPrefix)) {
            const QString paths =
                    argument.mid(worldGenOverlayValidationPrefix.size());
            const int separator = paths.indexOf(QLatin1String("::"));
            if (separator <= 0 || separator + 2 >= paths.size()) {
                qCritical() << "WorldGen project-overlay validation expects "
                               "<game-path>::<project-overlay-path>";
                return 13;
            }
            QString summary;
            QString error;
            if (!WorldGenPreviewDialog::validateProjectOverlay(
                        paths.left(separator), paths.mid(separator + 2),
                        &summary, &error)) {
                qCritical().noquote()
                        << "WorldGen project-overlay validation failed:"
                        << error;
                return 14;
            }
            qInfo().noquote()
                    << "WorldGen project-overlay validation passed:"
                    << summary;
            return 0;
        }
        const QString inGameMapValidationPrefix =
                QLatin1String("--validate-ingamemap=");
        if (argument.startsWith(inGameMapValidationPrefix)) {
            const QString fileName =
                    argument.mid(inGameMapValidationPrefix.size());
            QFile scanFile(fileName);
            if (!scanFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qCritical() << "InGameMap validation could not open:"
                            << fileName << scanFile.errorString();
                return 6;
            }
            int minX = std::numeric_limits<int>::max();
            int minY = std::numeric_limits<int>::max();
            int maxX = std::numeric_limits<int>::min();
            int maxY = std::numeric_limits<int>::min();
            int cellSize = 256;
            QXmlStreamReader scanner(&scanFile);
            while (!scanner.atEnd()) {
                scanner.readNext();
                if (!scanner.isStartElement())
                    continue;
                if (scanner.name() == QLatin1String("world")) {
                    const QString value = scanner.attributes().value(
                                QLatin1String("cellSize")).toString();
                    if (!value.isEmpty()) {
                        bool ok = false;
                        const int declaredCellSize = value.toInt(&ok);
                        if (!ok || (declaredCellSize != 256 &&
                                    declaredCellSize != 300)) {
                            qCritical() << "InGameMap validation found an "
                                           "unsupported cellSize:" << value;
                            return 7;
                        }
                        cellSize = declaredCellSize;
                    }
                    continue;
                }
                if (scanner.name() != QLatin1String("cell"))
                    continue;
                bool xOk = false;
                bool yOk = false;
                const int x = scanner.attributes().value(
                            QLatin1String("x")).toInt(&xOk);
                const int y = scanner.attributes().value(
                            QLatin1String("y")).toInt(&yOk);
                if (!xOk || !yOk)
                    continue;
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
            if (scanner.hasError() || minX > maxX || minY > maxY) {
                qCritical() << "InGameMap validation could not determine "
                               "the XML bounds:" << scanner.errorString();
                return 7;
            }
            World world(maxX - minX + 1, maxY - minY + 1,
                        cellSize == 300
                        ? WorldGridFormat::Legacy300
                        : WorldGridFormat::Native256);
            GenerateLotsSettings settings;
            settings.worldOrigin = QPoint(minX, minY);
            world.setGenerateLotsSettings(settings);
            InGameMapReader reader;
            if (!reader.readWorld(fileName, &world)) {
                qCritical() << "InGameMap validation could not read:"
                            << reader.errorString();
                return 8;
            }
            const QString outputXml =
                    fileName + QLatin1String(".validated.xml");
            InGameMapWriter xmlWriter;
            if (!xmlWriter.writeWorld(&world, outputXml)) {
                qCritical() << "InGameMap validation could not write XML:"
                            << xmlWriter.errorString();
                return 9;
            }
            const QString outputBinary =
                    fileName + QLatin1String(".validated.bin");
            InGameMapWriterBinary binaryWriter;
            if (!binaryWriter.writeWorld(&world, outputBinary)) {
                qCritical() << "InGameMap validation could not write binary:"
                            << binaryWriter.errorString();
                return 9;
            }
            qInfo() << "InGameMap validation passed:" << fileName
                    << "input cellSize:" << cellSize
                    << "converted XML:" << outputXml
                    << "converted binary:" << outputBinary;
            return 0;
        }
        const QString bmpValidationPrefix =
                QLatin1String("--validate-bmp-generation=");
        if (argument.startsWith(bmpValidationPrefix)) {
            validateBmpGenerationProject =
                    argument.mid(bmpValidationPrefix.size());
            continue;
        }
        if (argument == QLatin1String(
                    "--validate-preview-overlays")) {
            QString error;
            if (!ZLevelRenderer::validatePreviewOverlayAlignment(
                        &error)) {
                qCritical() << "Environment-preview overlay validation "
                               "failed:" << error;
                return 3;
            }
            qInfo() << "Environment-preview overlay validation passed";
            return 0;
        }
        const QString vehicleMeshPreviewPrefix =
                QLatin1String("--validate-vehicle-mesh-preview=");
        if (argument.startsWith(vehicleMeshPreviewPrefix)) {
            QString summary;
            QString error;
            const QString gameDirectory =
                    argument.mid(vehicleMeshPreviewPrefix.size());
            if (!VehicleMeshPreview::validate(
                        gameDirectory, &summary, &error)) {
                qCritical().noquote()
                        << "Vehicle mesh preview validation failed:"
                        << error;
                return 55;
            }
            qInfo().noquote()
                    << "Vehicle mesh preview validation passed:"
                    << summary;
            return 0;
        }
        const QString renderVehicleMeshPreviewPrefix =
                QLatin1String("--render-vehicle-mesh-preview=");
        if (argument.startsWith(renderVehicleMeshPreviewPrefix)) {
            const QString payload = argument.mid(
                        renderVehicleMeshPreviewPrefix.size());
            const int separator = payload.lastIndexOf(QLatin1Char('|'));
            if (separator <= 0 || separator >= payload.size() - 1) {
                qCritical() << "Vehicle mesh preview render requires a game directory and output path";
                return 56;
            }
            QString error;
            const QImage image = VehicleMeshPreview::validationImage(
                        payload.left(separator), &error);
            if (image.isNull()
                    || !image.save(payload.mid(separator + 1))) {
                qCritical().noquote()
                        << "Vehicle mesh preview render failed:"
                        << error;
                return 57;
            }
            qInfo().noquote()
                    << "Vehicle mesh preview image written to"
                    << payload.mid(separator + 1);
            return 0;
        }
        if (argument == QLatin1String("--validate-regions-editor")) {
            QString summary;
            QString error;
            if (!RegionsDock::validateEditor(&summary, &error)) {
                qCritical().noquote()
                        << "regions.lua editor validation failed:" << error;
                return 38;
            }
            qInfo().noquote()
                    << "regions.lua editor validation passed:" << summary;
            return 0;
        }
        if (argument.startsWith(QLatin1String("--validate-regions="))) {
            const QString fileName = argument.mid(19);
            RegionsDock validator;
            QString error;
            int regionCount = 0;
            if (!validator.validateRegionFile(
                        fileName, &regionCount, &error)) {
                qCritical().noquote()
                        << "regions.lua validation failed:" << error;
                return 39;
            }
            qInfo().noquote() << "regions.lua validation passed:"
                              << regionCount << "region(s)";
            return 0;
        }
        if (!argument.startsWith(QLatin1String("--validate-streets=")))
            continue;
        const QString fileName = argument.mid(19);
        StreetNamesDock validator;
        QString error;
        int streetCount = 0;
        if (!validator.validateStreetFile(fileName, &streetCount, &error)) {
            qCritical() << "streets.xml validation failed:" << error;
            return 2;
        }
        qInfo() << "streets.xml validation passed:" << streetCount
                << "street(s)";
        return 0;
    }
    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;
    for (const QString &argument : commandLineArguments) {
        if (argument == QLatin1String("--renderer=opengl"))
            Preferences::instance()->setUseOpenGL(true);
        else if (argument == QLatin1String("--renderer=raster"))
            Preferences::instance()->setUseOpenGL(false);
    }

    Preferences::instance()->applyTheme();

    MainWindow w;
    w.show();
    w.readSettings();

    const bool configuredCommand =
            !renderWorldGenPreviewRoot.isEmpty()
            || !renderWorldGenPrefabRoot.isEmpty()
            || !renderWorldGenPrefabWindowRoot.isEmpty()
            || !validateWorldGenPrefabImport.isEmpty()
            || !validateBmpGenerationProject.isEmpty()
            || !auditTilesetCleanupPath.isEmpty()
            || !rebuildTilesetCatalogPath.isEmpty()
            || !validateNative256RoomDefsTmx.isEmpty()
            || !validateInGameMapBuildingGeneration.isEmpty()
            || !validateWorldMapOverlays.isEmpty()
            || !validateThumbnailLifecycleProject.isEmpty()
            || !renderTilesetCleanupRoot.isEmpty()
            || validateTilesetCleanup
            || validateCellMoveCoordinates;
    if (configuredCommand && !w.InitConfigFiles())
        return 0;

    if (validateCellMoveCoordinates) {
        QString dataSummary;
        QString interactionSummary;
        QString error;
        if (!MainWindow::validateCellMoveCoordinateData(
                    &dataSummary, &error)
                || !w.validateCellPasteInteraction(
                    &interactionSummary, &error)) {
            qCritical().noquote()
                    << "Cell move coordinate validation failed:"
                    << error;
            return 54;
        }
        qInfo().noquote()
                << "Cell move coordinate validation passed:"
                << dataSummary << interactionSummary;
        return 0;
    }

    if (!validateThumbnailLifecycleProject.isEmpty()) {
        if (!w.openFile(validateThumbnailLifecycleProject)) {
            qCritical() << "Thumbnail lifecycle validation could not open"
                        << validateThumbnailLifecycleProject;
            return 60;
        }
        Document *document = DocumentManager::instance()->currentDocument();
        WorldDocument *worldDocument =
                document ? document->asWorldDocument() : nullptr;
        QString mapPath;
        if (worldDocument) {
            for (WorldCell *cell : worldDocument->world()->cells()) {
                if (cell && !cell->mapFilePath().isEmpty()) {
                    mapPath = cell->mapFilePath();
                    break;
                }
            }
        }
        if (!worldDocument || mapPath.isEmpty()) {
            qCritical() << "Thumbnail lifecycle validation found no cell map";
            DocumentManager::instance()->closeAllDocuments();
            return 60;
        }
        MapImageManager *imageManager = MapImageManager::instance();
        MapImage *mapImage = imageManager->getMapImage(
                    mapPath, QString(), worldDocument);
        if (!mapImage) {
            qCritical() << "Thumbnail lifecycle validation could not load"
                        << mapPath;
            DocumentManager::instance()->closeAllDocuments();
            return 60;
        }
        MapInfo *mapInfo = mapImage->mapInfo();
        if (!mapInfo
                || !imageManager->recreateMapImage(
                    mapPath, QString(), worldDocument)) {
            qCritical() << "Thumbnail lifecycle validation could not render"
                        << mapPath;
            DocumentManager::instance()->closeAllDocuments();
            return 60;
        }
        QElapsedTimer renderTimer;
        renderTimer.start();
        while (renderTimer.elapsed() < 60000
               && (!mapImage->isLoaded() || !mapInfo->map())) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }
        if (!mapImage->isLoaded() || !mapInfo->map()) {
            qCritical() << "Thumbnail lifecycle validation did not render"
                        << mapPath;
            DocumentManager::instance()->closeAllDocuments();
            return 60;
        }
        const int projectReferences =
                imageManager->mapImageReferenceCount(mapImage);
        QObject secondaryOwner;
        MapImage *sharedImage = imageManager->getMapImage(
                    mapPath, QString(), &secondaryOwner);
        const bool shared = sharedImage == mapImage
                && imageManager->mapImageReferenceCount(mapImage)
                == projectReferences + 1;
        imageManager->releaseOwner(&secondaryOwner);
        const bool secondaryReleased =
                imageManager->mapImageReferenceCount(mapImage)
                == projectReferences;
        DocumentManager::instance()->closeAllDocuments();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 30000
               && (imageManager->containsMapImage(mapImage)
                   || (mapInfo && mapInfo->map()))) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }
        const bool released = !imageManager->containsMapImage(mapImage);
        const bool mapPurged = !mapInfo || !mapInfo->map();
        if (!shared || !secondaryReleased || !released || !mapPurged) {
            qCritical() << "Thumbnail lifecycle validation failed"
                        << "shared" << shared
                        << "secondaryReleased" << secondaryReleased
                        << "thumbnailReleased" << released
                        << "mapPurged" << mapPurged;
            return 60;
        }
        qInfo() << "Thumbnail lifecycle validation passed for" << mapPath;
        return 0;
    }

    if (validateTilesetCleanup) {
        QString summary;
        QString error;
        if (!TilesetCleanup::validate(&summary, &error)) {
            qCritical().noquote()
                    << "Tileset cleanup validation failed:" << error;
            return 19;
        }
        qInfo().noquote()
                << "Tileset cleanup validation passed:" << summary;
        return 0;
    }
    if (!validateInGameMapBuildingGeneration.isEmpty()) {
        const int separator =
                validateInGameMapBuildingGeneration.lastIndexOf(
                    QLatin1String("::"));
        const QString coordinates = separator > 0
                ? validateInGameMapBuildingGeneration.mid(separator + 2)
                : QString();
        const QStringList coordinateParts =
                coordinates.split(QLatin1Char(','));
        bool xOk = false;
        bool yOk = false;
        const int x = coordinateParts.size() == 2
                ? coordinateParts.at(0).toInt(&xOk) : -1;
        const int y = coordinateParts.size() == 2
                ? coordinateParts.at(1).toInt(&yOk) : -1;
        if (separator <= 0 || !xOk || !yOk) {
            qCritical() << "InGameMap building-generation validation expects "
                           "<project.pzw>::<cell-x>,<cell-y>";
            return 43;
        }
        const QString projectFile =
                validateInGameMapBuildingGeneration.left(separator);
        if (!w.openFile(projectFile)) {
            qCritical() << "InGameMap building-generation validation could "
                           "not open:" << projectFile;
            return 44;
        }
        Document *document =
                DocumentManager::instance()->currentDocument();
        WorldDocument *worldDocument =
                document ? document->asWorldDocument() : nullptr;
        WorldCell *cell = worldDocument
                ? worldDocument->world()->cellAt(x, y) : nullptr;
        if (!cell || cell->mapFilePath().isEmpty()) {
            qCritical() << "InGameMap building-generation validation could "
                           "not find a mapped cell:" << x << y;
            return 45;
        }
        worldDocument->setSelectedCells(QList<WorldCell*>() << cell);
        InGameMapFeatureGenerator generator;
        if (!generator.generateWorld(
                    worldDocument,
                    InGameMapFeatureGenerator::GenerateSelected,
                    InGameMapFeatureGenerator::FeatureBuilding)) {
            qCritical().noquote()
                    << "InGameMap building-generation validation failed:"
                    << generator.errorString();
            return 46;
        }
        int buildingFeatureCount = 0;
        for (InGameMapFeature *feature : cell->inGameMap().features()) {
            if (feature->properties().containsKey(
                        QStringLiteral("building"))) {
                ++buildingFeatureCount;
            }
        }
        if (buildingFeatureCount == 0) {
            qCritical() << "InGameMap building-generation validation failed: "
                           "no building feature was generated for cell"
                        << x << y;
            return 47;
        }
        qInfo() << "InGameMap building-generation validation passed: cell"
                << x << y << "lots" << cell->lots().size()
                << "building features" << buildingFeatureCount;
        return 0;
    }
    if (!validateWorldMapOverlays.isEmpty()) {
        const QStringList paths = validateWorldMapOverlays.split(
                    QLatin1String("::"));
        if (paths.size() != 3) {
            qCritical() << "World-map overlay validation expects "
                           "<project.pzw>::<worldmap.xml>::"
                           "<worldmap-forest.xml>";
            return 48;
        }
        if (!w.openFile(paths.at(0))) {
            qCritical() << "World-map overlay validation could not open:"
                        << paths.at(0);
            return 49;
        }
        Document *document =
                DocumentManager::instance()->currentDocument();
        WorldDocument *worldDocument =
                document ? document->asWorldDocument() : nullptr;
        WorldView *view = worldDocument
                ? dynamic_cast<WorldView *>(worldDocument->view()) : nullptr;
        QString error;
        if (!view
                || !view->scene()->loadWorldMapOverlay(
                    paths.at(1), false, &error)
                || !view->scene()->loadWorldMapOverlay(
                    paths.at(2), true, &error)) {
            qCritical().noquote()
                    << "World-map overlay validation failed:" << error;
            return 50;
        }
        const int worldFeatureCount =
                view->scene()->worldMapOverlayFeatureCount(false);
        const int forestFeatureCount =
                view->scene()->worldMapOverlayFeatureCount(true);
        if (worldFeatureCount == 0 || forestFeatureCount == 0) {
            qCritical() << "World-map overlay validation failed: "
                           "one loaded overlay contained no visible features";
            return 51;
        }
        view->scene()->setWorldMapOverlayVisible(false, false);
        if (view->scene()->worldMapOverlayVisible(false)
                || !view->scene()->worldMapOverlayVisible(true)) {
            qCritical() << "World-map overlay validation failed: "
                           "independent visibility state was not retained";
            return 52;
        }
        view->scene()->setWorldMapOverlayVisible(false, true);
        view->scene()->clearWorldMapOverlays();
        if (view->scene()->hasWorldMapOverlay(false)
                || view->scene()->hasWorldMapOverlay(true)) {
            qCritical() << "World-map overlay validation failed: "
                           "clear did not remove both overlays";
            return 53;
        }
        qInfo() << "World-map overlay validation passed:"
                << QFileInfo(paths.at(1)).fileName()
                << worldFeatureCount
                << QFileInfo(paths.at(2)).fileName()
                << forestFeatureCount;
        return 0;
    }
    if (!validateNative256RoomDefsTmx.isEmpty()) {
        QString summary;
        QString error;
        if (!LotFilesManager256::validateReferencedRoomDefs(
                    validateNative256RoomDefsTmx, &summary, &error)) {
            qCritical().noquote()
                    << "Native-256 referenced RoomDefs validation failed:"
                    << error;
            return 40;
        }
        qInfo().noquote()
                << "Native-256 referenced RoomDefs validation passed:"
                << summary;
        return 0;
    }
    if (!rebuildTilesetCatalogPath.isEmpty()) {
        int added = 0;
        int updated = 0;
        int removed = 0;
        if (!TileMetaInfoMgr::instance()->rebuildTilesetsTxt(
                    &added, &updated, &removed, true, true,
                    rebuildTilesetCatalogPath)) {
            qCritical().noquote()
                    << "Tilesets.txt rebuild failed:"
                    << TileMetaInfoMgr::instance()->errorString();
            return 24;
        }
        qInfo() << "Tilesets.txt rebuild passed:"
                << added << "added,"
                << updated << "updated,"
                << removed << "removed";
        return 0;
    }
    if (!auditTilesetCleanupPath.isEmpty()) {
        const QFileInfo target(auditTilesetCleanupPath);
        QStringList files;
        QString scanRoot;
        if (target.isFile()) {
            files += target.absoluteFilePath();
            scanRoot = target.absolutePath();
        } else if (target.isDir()) {
            scanRoot = target.absoluteFilePath();
            files = TilesetCleanup::filesUnder(scanRoot, true);
        } else {
            qCritical().noquote()
                    << "Tileset cleanup audit target does not exist:"
                    << auditTilesetCleanupPath;
            return 20;
        }
        TilesetCleanupOptions options;
        QList<TilesetCleanupResult> results;
        bool hasErrors = false;
        for (const QString &fileName : files) {
            const TilesetCleanupResult result =
                    TilesetCleanup::processFile(
                        fileName, scanRoot, options, false);
            hasErrors = hasErrors || !result.error.isEmpty();
            results += result;
        }
        qInfo().noquote()
                << "Tileset cleanup audit:"
                << TilesetCleanup::report(results);
        return hasErrors ? 21 : 0;
    }
    if (!renderTilesetCleanupRoot.isEmpty()) {
        if (renderWorldGenPreviewOutput.isEmpty()) {
            qCritical() << "Project Doctor render requires "
                           "--worldgen-preview-output=<PNG>";
            return 22;
        }
        QString projectFile;
        const QStringList projects = QDir(renderTilesetCleanupRoot)
                .entryList(QStringList() << QStringLiteral("*.pzw"),
                           QDir::Files);
        if (projects.size() == 1) {
            projectFile = QDir(renderTilesetCleanupRoot)
                    .filePath(projects.first());
        }
        TilesetCleanupDialog dialog(
                    renderTilesetCleanupRoot, projectFile);
        dialog.show();
        QMetaObject::invokeMethod(
                    &dialog, "analyze", Qt::DirectConnection);
        a.processEvents();
        if (!dialog.grab().save(renderWorldGenPreviewOutput)) {
            qCritical() << "Could not save Project Doctor render:"
                        << renderWorldGenPreviewOutput;
            return 23;
        }
        qInfo() << "Project Doctor render saved:"
                << renderWorldGenPreviewOutput;
        return 0;
    }
    if (!renderWorldGenPreviewRoot.isEmpty()) {
        if (renderWorldGenPreviewOutput.isEmpty()) {
            qCritical() << "WorldGen preview render requires "
                           "--worldgen-preview-output=<PNG>";
            return 11;
        }
        QString error;
        if (!WorldGenPreviewDialog::renderValidationPreview(
                    renderWorldGenPreviewRoot,
                    renderWorldGenPreviewOutput, &error)) {
            qCritical().noquote()
                    << "WorldGen preview render failed:" << error;
            return 12;
        }
        qInfo() << "WorldGen preview render passed:"
                << renderWorldGenPreviewOutput;
        return 0;
    }
    if (!renderWorldGenPrefabRoot.isEmpty()) {
        if (renderWorldGenPreviewOutput.isEmpty()) {
            qCritical() << "WorldGen prefab render requires "
                           "--worldgen-preview-output=<PNG>";
            return 15;
        }
        QString error;
        if (!WorldGenPreviewDialog::renderValidationPrefabEditor(
                    renderWorldGenPrefabRoot,
                    renderWorldGenPreviewOutput, &error)) {
            qCritical().noquote()
                    << "WorldGen prefab-editor render failed:" << error;
            return 16;
        }
        qInfo() << "WorldGen prefab-editor render passed:"
                << renderWorldGenPreviewOutput;
        return 0;
    }
    if (!renderWorldGenPrefabWindowRoot.isEmpty()) {
        if (renderWorldGenPreviewOutput.isEmpty()) {
            qCritical() << "WorldGen prefab-window render requires "
                           "--worldgen-preview-output=<PNG>";
            return 18;
        }
        QString error;
        if (!WorldGenPrefabDialog::renderValidationWindow(
                    renderWorldGenPrefabWindowRoot,
                    renderWorldGenPreviewOutput, &error)) {
            qCritical().noquote()
                    << "WorldGen prefab-window render failed:" << error;
            return 19;
        }
        qInfo() << "WorldGen prefab-window render passed:"
                << renderWorldGenPreviewOutput;
        return 0;
    }
    if (!validateWorldGenPrefabImport.isEmpty()) {
        QString summary;
        QString error;
        if (!WorldGenPreviewDialog::validatePrefabImport(
                    validateWorldGenPrefabImport, &summary, &error)) {
            qCritical().noquote()
                    << "WorldGen prefab import validation failed:" << error;
            return 17;
        }
        qInfo().noquote()
                << "WorldGen prefab import validation passed:" << summary;
        return 0;
    }
    if (!validateBmpGenerationProject.isEmpty()) {
        if (!w.openFile(validateBmpGenerationProject)) {
            qCritical() << "BMP to TMX input validation could not open:"
                        << validateBmpGenerationProject;
            return 4;
        }
        Document *document =
                DocumentManager::instance()->currentDocument();
        WorldDocument *worldDocument =
                document ? document->asWorldDocument() : nullptr;
        if (!BMPToTMX::instance()->validateGenerationInputs(
                    worldDocument)) {
            qCritical().noquote()
                    << "BMP to TMX input validation failed:"
                    << BMPToTMX::instance()->errorString();
            return 5;
        }
        qInfo() << "BMP to TMX input validation passed:"
                << validateBmpGenerationProject;
        return 0;
    }
    QSettings sessionSettings(QSettings::IniFormat, QSettings::UserScope,
                              QLatin1String("TheIndieStone"),
                              QLatin1String("PZWorldEd"));
    const QString cleanExitKey =
            QLatin1String("Startup/PreviousSessionClosedCleanly");
    const bool previousSessionClosedCleanly =
            sessionSettings.value(cleanExitKey, true).toBool();
    sessionSettings.setValue(cleanExitKey, false);
    sessionSettings.sync();
    QTimer::singleShot(
                10, &w,
                [&w, commandLineArguments,
                 previousSessionClosedCleanly]() {
        qInfo() << "WorldEd interactive startup tasks begin";
        if (!w.InitConfigFiles()) {
            qCritical() << "WorldEd interactive startup configuration failed";
            qApp->quit();
            return;
        }
        bool openedCommandLineFile = false;
        QPoint commandLineCell(-1, -1);
        for (const QString &argument : commandLineArguments) {
            if (argument.startsWith(QLatin1String("--cell="))) {
                const QStringList coordinates =
                        argument.mid(7).split(QLatin1Char(','));
                bool xOk = false;
                bool yOk = false;
                if (coordinates.size() == 2) {
                    const int x = coordinates.at(0).toInt(&xOk);
                    const int y = coordinates.at(1).toInt(&yOk);
                    if (xOk && yOk)
                        commandLineCell = QPoint(x, y);
                }
                continue;
            }
            if (QFileInfo(argument).isFile()) {
                openedCommandLineFile = w.openFile(argument)
                        || openedCommandLineFile;
            }
        }
        if (openedCommandLineFile
                && commandLineCell.x() >= 0
                && commandLineCell.y() >= 0) {
            Document *document =
                    DocumentManager::instance()->currentDocument();
            WorldDocument *worldDocument =
                    document ? document->asWorldDocument() : nullptr;
            if (worldDocument
                    && worldDocument->world()->cellAt(
                        commandLineCell.x(), commandLineCell.y())) {
                qInfo() << "Command line opening cell"
                        << commandLineCell.x() << commandLineCell.y();
                worldDocument->editCell(
                            commandLineCell.x(), commandLineCell.y());
            } else {
                qCritical()
                        << "Command-line cell is outside the opened world:"
                        << commandLineCell;
            }
        }
        if (!openedCommandLineFile
                && Preferences::instance()->restoreLastSession()) {
            if (previousSessionClosedCleanly) {
                w.openLastFiles();
            } else {
                qWarning() << "Automatic session restore skipped after an "
                              "unclean WorldEd shutdown.";
                QMessageBox::warning(
                            &w, QObject::tr("WorldEd Session Recovery"),
                            QObject::tr(
                                "WorldEd did not close cleanly last time.\n\n"
                                "Automatic document restore was skipped to "
                                "prevent a startup crash loop. Your project "
                                "files were not changed. Open the required "
                                "project manually after checking the latest "
                                "log in settings/logs."));
            }
        }
        w.readSettings();
        w.startSettingsAutoSave();
        qInfo() << "WorldEd interactive startup tasks complete";
    });

    int ret = a.exec();

    sessionSettings.setValue(cleanExitKey, true);
    sessionSettings.sync();
    DocumentManager::deleteInstance();
    ToolManager::deleteInstance();
    Preferences::deleteInstance();
    MapImageManager::deleteInstance();
    MapManager::deleteInstance();
    TileMetaInfoMgr::deleteInstance();
    TilesetManager::deleteInstance();

    return ret;
}
