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
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

#include "../firstlaunchdialog.h"
#include "../portablesettings.h"
#include "directmappreviewrunner.h"
#include "displayscaling.h"
#include "document.h"
#include "documentmanager.h"
#include "lotsourcefiles.h"
#include "mainwindow.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "preferences.h"
#include "pztoolsbuild.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "toolmanager.h"
#include "world.h"
#include "worlddocument.h"

using namespace Tiled;
using namespace Tiled::Internal;

int main(int argc, char *argv[])
{
    PZTools::configureDisplayScaling();
#if ZOMBOID
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#endif
    QApplication a(argc, argv);

    a.setOrganizationName(QLatin1String("TheIndieStone"));
    a.setApplicationName(QLatin1String("PZWorldEd"));
    PortableSettings::configure();
    PortableSettings::installLogging();
    qInfo().noquote() << "Application" << PZToolsBuild::label();
    PortableSettings::prepareVersionedSettings();
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.0.1"));
#endif

    const QStringList commandLineArguments = a.arguments().mid(1);
    QString rebuildTilesetCatalogPath;
    for (const QString &argument : commandLineArguments) {
        const QString rebuildCatalogPrefix =
                QLatin1String("--rebuild-tileset-catalog=");
        if (argument.startsWith(rebuildCatalogPrefix)) {
            rebuildTilesetCatalogPath =
                    argument.mid(rebuildCatalogPrefix.size());
            continue;
        }
        const QString exportLotSourcesPrefix =
                QLatin1String("--export-lot-sources=");
        const QString importLotSourcesPrefix =
                QLatin1String("--import-lot-sources=");
        const QString rebuildLotSourcesPrefix =
                QLatin1String("--rebuild-lot-source-navigation=");
        if (argument.startsWith(exportLotSourcesPrefix)
                || argument.startsWith(importLotSourcesPrefix)
                || argument.startsWith(rebuildLotSourcesPrefix)) {
            const bool rebuilding = argument.startsWith(rebuildLotSourcesPrefix)
                    || commandLineArguments.contains(QStringLiteral("--rebuild-source-navigation"));
            if (rebuilding && argument.startsWith(exportLotSourcesPrefix)) {
                qCritical() << "Navigation rebuilding requires source import";
                return 58;
            }
            const bool importing = rebuilding || argument.startsWith(importLotSourcesPrefix);
            const QString directory = argument.mid(argument.startsWith(rebuildLotSourcesPrefix)
                    ? rebuildLotSourcesPrefix.size() : importing
                    ? importLotSourcesPrefix.size()
                    : exportLotSourcesPrefix.size());
            int cells = 0;
            QString error;
            LotSourceFiles::NavigationOptions navigation;
            if (rebuilding) {
                const QString definitionsPrefix = QStringLiteral("--source-tiledef=");
                const QString overridePrefix = QStringLiteral("--source-override=");
                for (const QString &option : commandLineArguments) {
                    if (option.startsWith(definitionsPrefix))
                        navigation.tileDefinitions.append(option.mid(definitionsPrefix.size()));
                    if (option.startsWith(overridePrefix)) {
                        const QString value = option.mid(overridePrefix.size());
                        const int separator = value.indexOf(QStringLiteral("::"));
                        const auto match = QRegularExpression(
                            QStringLiteral("^(-?\\d+),(-?\\d+)$")).match(value.left(separator));
                        bool validX = false, validY = false;
                        const int x = match.captured(1).toInt(&validX);
                        const int y = match.captured(2).toInt(&validY);
                        if (separator < 0 || !match.hasMatch() || !validX || !validY
                                || value.mid(separator + 2).isEmpty()) {
                            qCritical() << "Use --source-override=x,y::path.png";
                            return 58;
                        }
                        navigation.overrides.insert(QStringLiteral("%1_%2.lotheader").arg(x).arg(y),
                                                    value.mid(separator + 2));
                    }
                }
            }
            if (!LotSourceFiles::processDirectory(directory, importing,
                                                  &cells, &error, false,
                                                  rebuilding ? &navigation : nullptr)) {
                qCritical().noquote() << "LOT source conversion failed:"
                                      << error;
                return 58;
            }
            qInfo().noquote() << "LOT source conversion completed:"
                              << cells << "cell(s)";
            return 0;
        }
    }
    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;
    Preferences::instance()->reloadSharedPaths();
    for (const QString &argument : commandLineArguments) {
        if (argument == QLatin1String("--renderer=opengl"))
            Preferences::instance()->setUseOpenGL(true);
        else if (argument == QLatin1String("--renderer=raster"))
            Preferences::instance()->setUseOpenGL(false);
    }

    Preferences::instance()->applyTheme();

    const int previewIndex = commandLineArguments.indexOf(QStringLiteral("--play-map-source"));
    if (previewIndex >= 0) {
        const QString cellOption = QStringLiteral("--preview-cell");
        const QString project = commandLineArguments.value(previewIndex + 1);
        QVector<QPoint> selection;
        for (int cellIndex = commandLineArguments.indexOf(cellOption); cellIndex >= 0;
             cellIndex = commandLineArguments.indexOf(cellOption, cellIndex + 1)) {
            const QStringList coordinates = commandLineArguments.value(cellIndex + 1).split(QLatin1Char(','));
            bool validX = false;
            bool validY = false;
            const int x = coordinates.value(0).toInt(&validX);
            const int y = coordinates.value(1).toInt(&validY);
            if (coordinates.size() != 2 || !validX || !validY || x < 0 || y < 0) {
                qCritical() << "--preview-cell requires nonnegative project coordinates as X,Y";
                return 1;
            }
            selection.append(QPoint(x, y));
        }
        MainWindow previewWindow;
        QString error;
        if (project.isEmpty()) {
            qCritical() << "Play Map Source requires a project.";
            return 1;
        }
        const QString gameDirectory = Preferences::instance()->playtestGameDirectory(&error);
        if (gameDirectory.isEmpty() || !previewWindow.InitConfigFiles()
                || !DirectMapPreviewRunner::start(project, gameDirectory, selection, error,
                    [](bool success, const QString &detail) {
                        if (!success)
                            qCritical().noquote() << "Direct map preview:" << detail;
                        QCoreApplication::exit(success ? 0 : 1);
                    })) {
            qCritical().noquote() << "Direct map preview:" << error;
            return 1;
        }
        return a.exec();
    }

    MainWindow w;
    w.show();
    w.readSettings();

    if (!rebuildTilesetCatalogPath.isEmpty() && !w.InitConfigFiles())
        return 0;
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
