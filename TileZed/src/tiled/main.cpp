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
#include "mainwindow.h"
#include "languagemanager.h"
#include "preferences.h"
#include "tiledapplication.h"
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
#include "BuildingEditor/buildingtemplates.h"
#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/buildingtilesetdock.h"
#include "BuildingEditor/categorydock.h"
#include "BuildingEditor/furnituregroups.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "worlded/worldedmgr.h"
#include "zprogress.h"
#include "tile.h"
#include "tileset.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QSet>
#include <QTemporaryFile>
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

private:
    void showVersion();
    void justQuit();
    void setDisableOpenGL();
    void setValidateBuildingCategories();

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
    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;
    if (commandLine.disableOpenGL)
        Preferences::instance()->setUseOpenGL(false);

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
