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

#ifndef THUMBNAILSETTINGSMGR_H
#define THUMBNAILSETTINGSMGR_H

#include "singleton.h"
#include "thumbnailsettingsfile.h"

#include <QMap>

class World;

class ThumbnailSettingsMgr : public Singleton<ThumbnailSettingsMgr>
{
public:
    ThumbnailSettingsMgr();
    ~ThumbnailSettingsMgr();

    void readTxt();
    void saveWorld(const QString& fileName, const QSet<ThumbnailCell> &visibleCells);
    void visibleCells(const QString& fileName, QSet<ThumbnailCell>& visibleCells);

    QString fileName() const;

private:
    QMap<QString,ThumbnailSettings*> mSettingsMap;
};

#endif // THUMBNAILSETTINGSMGR_H
