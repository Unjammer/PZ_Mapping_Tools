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
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QXmlStreamReader>

#include <limits>
#include "mainwindow.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"

#ifdef ZOMBOID
#include "documentmanager.h"
#include "document.h"
#include "bmptotmx.h"
#include "biomemapimageprocessor.h"
#include "cellscene.h"
#include "defaultsfile.h"
#include "toolmanager.h"
#include "preferences.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "lotfilesmanager256.h"
#include "progress.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "streetnamesdock.h"
#include "InGameMap/ingamemapreader.h"
#include "InGameMap/ingamemapwriterbinary.h"
#include "world.h"
#include "worlddocument.h"
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
    bool validateTilesetCleanup = false;
    for (const QString &argument : commandLineArguments) {
        if (argument == QLatin1String(
                    "--validate-biomemap-config")) {
            QString error;
            if (!BiomeMapImageProcessor::validateConfiguration(&error)) {
                qCritical().noquote()
                        << "BiomeMapConfig validation failed:" << error;
                return 32;
            }
            qInfo() << "BiomeMapConfig validation passed:"
                    << BiomeMapImageProcessor::palette().size()
                    << "entries including the map-override ID";
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
            qInfo() << "WorldDefaults.txt validation passed:"
                    << defaults.mEnums.size() << "enum(s),"
                    << defaults.mPropertyDefs.size() << "property definition(s),"
                    << defaults.mTemplates.size() << "template(s),"
                    << defaults.mObjectTypes.size() << "object type(s),"
                    << defaults.mObjectGroups.size() << "object group(s)";
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
            QXmlStreamReader scanner(&scanFile);
            while (!scanner.atEnd()) {
                scanner.readNext();
                if (!scanner.isStartElement() ||
                        scanner.name() != QLatin1String("cell"))
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
                        WorldGridFormat::Legacy300);
            GenerateLotsSettings settings;
            settings.worldOrigin = QPoint(minX, minY);
            world.setGenerateLotsSettings(settings);
            InGameMapReader reader;
            if (!reader.readWorld(fileName, &world)) {
                qCritical() << "InGameMap validation could not read:"
                            << reader.errorString();
                return 8;
            }

            const QString outputFile =
                    fileName + QLatin1String(".validated.bin");
            InGameMapWriterBinary writer;
            if (!writer.writeWorld(&world, outputFile)) {
                qCritical() << "InGameMap validation could not write:"
                            << writer.errorString();
                return 9;
            }
            qInfo() << "InGameMap validation passed:" << fileName
                    << "converted output:" << outputFile;
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
            || !renderTilesetCleanupRoot.isEmpty()
            || validateTilesetCleanup;
    if (configuredCommand && !w.InitConfigFiles())
        return 0;

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

    // Mark the interactive session dirty before restoring documents.  If a
    // malformed project or map terminates WorldEd, the next launch starts
    // safely instead of reopening the same file in an endless crash loop.
    QSettings sessionSettings(QSettings::IniFormat, QSettings::UserScope,
                              QLatin1String("TheIndieStone"),
                              QLatin1String("PZWorldEd"));
    const QString cleanExitKey =
            QLatin1String("Startup/PreviousSessionClosedCleanly");
    const bool previousSessionClosedCleanly =
            sessionSettings.value(cleanExitKey, true).toBool();
    sessionSettings.setValue(cleanExitKey, false);
    sessionSettings.sync();

    // Let the event loop paint the main window before the complete tileset
    // catalogue is decoded. InitConfigFiles keeps deterministic preloading,
    // while its progress dialog now remains visible and responsive.
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

        // Tile loading and session restoration can change dock size hints
        // after the initial restore. Reapply the persisted layout once
        // startup is done.
        w.readSettings();

        // Do not overwrite the saved layout/session while tilesets and the
        // previous session are still being restored.
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
