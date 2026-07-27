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
#include <QMessageBox>
#include <QSettings>
#include "mainwindow.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"

#ifdef ZOMBOID
#include "documentmanager.h"
#include "toolmanager.h"
#include "preferences.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "progress.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
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

    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;

    Preferences::instance()->applyTheme();

    MainWindow w;
    w.show();
    w.readSettings();

    if (!w.InitConfigFiles())
        return 0;

    {
        PROGRESS progress(QObject::tr("Loading all tilesets..."), &w);
        TileMetaInfoMgr::instance()->loadTilesets(false);
        TilesetManager::instance()->waitForTilesets(
                    TileMetaInfoMgr::instance()->tilesets());
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

    if (Preferences::instance()->restoreLastSession()) {
        if (previousSessionClosedCleanly) {
            w.openLastFiles();
        } else {
            qWarning() << "Automatic session restore skipped after an "
                          "unclean WorldEd shutdown.";
            QMessageBox::warning(
                        &w, QObject::tr("WorldEd Session Recovery"),
                        QObject::tr(
                            "WorldEd did not close cleanly last time.\n\n"
                            "Automatic document restore was skipped to prevent "
                            "a startup crash loop. Your project files were not "
                            "changed. Open the required project manually after "
                            "checking the latest log in settings/logs."));
        }
    }

    // Tile loading and session restoration can change dock size hints after
    // the initial restore. Reapply the persisted layout once startup is done.
    w.readSettings();

    // Do not overwrite the saved layout/session while tilesets and the
    // previous session are still being restored.
    w.startSettingsAutoSave();

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
