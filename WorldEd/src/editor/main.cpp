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
#include "mainwindow.h"
#include "../portablesettings.h"

#ifdef ZOMBOID
#include "documentmanager.h"
#include "toolmanager.h"
#include "preferences.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
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
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.0.1"));
#endif

#ifdef Q_WS_MAC
    a.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    Preferences::instance()->applyTheme();

    MainWindow w;
    w.show();
    w.readSettings();

    if (!w.InitConfigFiles())
        return 0;

    if (Preferences::instance()->restoreLastSession())
        w.openLastFiles();

    // Tile loading and session restoration can change dock size hints after
    // the initial restore. Reapply the persisted layout once startup is done.
    w.readSettings();

    // Do not overwrite the saved layout/session while tilesets and the
    // previous session are still being restored.
    w.startSettingsAutoSave();

    int ret = a.exec();

    DocumentManager::deleteInstance();
    ToolManager::deleteInstance();
    Preferences::deleteInstance();
    MapImageManager::deleteInstance();
    MapManager::deleteInstance();
    TileMetaInfoMgr::deleteInstance();
    TilesetManager::deleteInstance();

    return ret;
}
