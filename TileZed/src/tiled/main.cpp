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
#include "pztoolsbuild.h"
#include "displayscaling.h"
#include <QtSingleApplication>
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"
#ifdef ZOMBOID
#include "BuildingEditor/buildingeditorwindow.h"
#include "BuildingEditor/buildingpreferences.h"
#include "worlded/worldedmgr.h"
#include "zprogress.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
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

class CommandLineHandler : public CommandLineParser
{
public:
    CommandLineHandler();

    bool quit;
    bool showedVersion;
    bool disableOpenGL;

private:
    void showVersion();
    void justQuit();
    void setDisableOpenGL();

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
    PZTools::configureDisplayScaling();
#if !defined(QT_NO_DEBUG) && defined(ZOMBOID) && defined(_MSC_VER)
    _set_invalid_parameter_handler(invalid_parameter_handler);
#endif

    QtSingleApplication a(argc, argv);

#ifdef ZOMBOID
    Q_INIT_RESOURCE(buildingeditor);
#endif

    a.setOrganizationName(QLatin1String("TheIndieStone"));
#ifdef ZOMBOID
#ifdef PZTOOLS_BUILDING_EDITOR_APP
    constexpr bool buildingEditorMode = true;
#else
    constexpr bool buildingEditorMode = false;
#endif
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
    qInfo().noquote() << "Application" << PZToolsBuild::label();
    PortableSettings::prepareVersionedSettings();
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.8.1"));
#endif

    LanguageManager *languageManager = LanguageManager::instance();
    languageManager->installTranslators();

    CommandLineHandler commandLine;
    if (!commandLine.parse(QCoreApplication::arguments()))
        return 0;
    if (commandLine.quit)
        return 0;
    if (buildingEditorMode && a.isRunning()) {
        const QStringList fileNames = commandLine.filesToOpen();
        bool delivered = fileNames.isEmpty()
                ? a.sendMessage(QString())
                : true;
        for (const QString &fileName : fileNames)
            delivered = a.sendMessage(fileName) && delivered;
        if (!delivered)
            qWarning() << "BuildingEd could not contact the running instance.";
        return delivered ? 0 : 1;
    }
    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;
    Preferences::instance()->reloadSharedPaths();
    if (commandLine.disableOpenGL) {
        Preferences::instance()->setUseOpenGL(false);
        if (buildingEditorMode)
            BuildingEditor::BuildingPreferences::instance()
                    ->setUseOpenGL(false);
    }

#ifdef ZOMBOID
    Preferences::instance()->applyTheme();
    if (buildingEditorMode) {
        QStringList pendingBuildingFiles = commandLine.filesToOpen();
        bool buildingStartupComplete = false;
        BuildingEditor::BuildingEditorWindow buildingEditor;
        a.setActivationWindow(&buildingEditor);
        QObject::connect(
                    &a, &QtSingleApplication::messageReceived,
                    &buildingEditor,
                    [&buildingEditor, &pendingBuildingFiles,
                     &buildingStartupComplete](const QString &fileName) {
            if (fileName.isEmpty())
                return;
            if (buildingStartupComplete)
                buildingEditor.openFile(fileName);
            else
                pendingBuildingFiles.append(fileName);
        });
        buildingEditor.show();
        buildingEditor.readSettings();
        QTimer::singleShot(0, &buildingEditor,
                           [&buildingEditor,
                            &pendingBuildingFiles,
                            &buildingStartupComplete]() {
            buildingEditor.setEnabled(false);
            qApp->processEvents(QEventLoop::AllEvents);
            const bool configLoaded =
                    MainWindow::InitConfigFiles(&buildingEditor);
            buildingEditor.setEnabled(true);
            if (!configLoaded ||
                    !buildingEditor.Startup()) {
                buildingEditor.close();
                QCoreApplication::quit();
                return;
            }
            buildingStartupComplete = true;
            foreach (const QString &fileName, pendingBuildingFiles)
                buildingEditor.openFile(fileName);
            pendingBuildingFiles.clear();
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
#endif
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
    w.readSettings();
    w.startSettingsAutoSave();
#endif

    const int result = a.exec();
    sessionSettings.setValue(cleanExitKey, true);
    sessionSettings.sync();
    return result;
}
