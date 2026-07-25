/*
 * Copyright 2026, Tim Baker <treectrl@users.sf.net>
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

#include "thumbnailsettingsmgr.h"

#include "mainwindow.h"
#include "preferences.h"

#include <QFileInfo>
#include <QMessageBox>

SINGLETON_IMPL(ThumbnailSettingsMgr)

ThumbnailSettingsMgr::ThumbnailSettingsMgr() {

}

ThumbnailSettingsMgr::~ThumbnailSettingsMgr()
{
    qDeleteAll(mSettingsMap.values());
}

void ThumbnailSettingsMgr::readTxt()
{
    const QString fileName = this->fileName();
    QFileInfo info(fileName);
    if (!info.exists()) {
        return;
    }

    ThumbnailSettingsFile file;
    if (!file.read(fileName)) {
        QMessageBox::warning(MainWindow::instance(), QStringLiteral("Thumbnail Settings Error"), file.errorString());
        return;
    }
    const QList<ThumbnailSettings*> settings = file.takeSettings();
    for (ThumbnailSettings* settings1 : settings) {
        mSettingsMap[settings1->fileName()] = settings1;
    }
}

void ThumbnailSettingsMgr::saveWorld(const QString &fileName, const QSet<ThumbnailCell> &visibleCells)
{
    ThumbnailSettings *settings = mSettingsMap.value(fileName, nullptr);
    if (settings == nullptr) {
        settings = new ThumbnailSettings(fileName, visibleCells);
        mSettingsMap[fileName] = settings;
    } else {
        settings->setVisibleCells(visibleCells);
    }

    ThumbnailSettingsFile file;
    if (!file.write(this->fileName(), mSettingsMap.values())) {
        QMessageBox::warning(MainWindow::instance(), QStringLiteral("Thumbnail Settings Error"), file.errorString());
    }
}

void ThumbnailSettingsMgr::visibleCells(const QString &fileName, QSet<ThumbnailCell> &visibleCells)
{
    visibleCells.clear();
    ThumbnailSettings *settings = mSettingsMap.value(fileName, nullptr);
    if (settings == nullptr) {
        return;
    }
    visibleCells = settings->visibleCells();
}

QString ThumbnailSettingsMgr::fileName() const
{
    return Preferences::instance()->configPath(QStringLiteral("thumbnails.txt"));
}
