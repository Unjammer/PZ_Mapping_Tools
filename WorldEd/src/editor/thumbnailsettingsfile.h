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

#ifndef THUMBNAILSETTINGSFILE_H
#define THUMBNAILSETTINGSFILE_H

#include <QHash>
#include <QPoint>
#include <QSet>
#include <QString>

class ThumbnailSettingsFile;

// I tried using QSet<QPoint> with the hashing function defined below, but each point's y coordinate was always corrupted.
class ThumbnailCell
{
public:
    ThumbnailCell() :
        xp(0), yp(0)
    {

    }

    ThumbnailCell(int x, int y) :
        xp(x), yp(y)
    {

    }

    ThumbnailCell(const ThumbnailCell& rhs) :
        xp(rhs.xp), yp(rhs.yp)
    {

    }

    bool operator==(const ThumbnailCell& rhs) const
    {
        return xp == rhs.xp && yp == rhs.yp;
    }

    int x() const { return xp; }
    int y() const { return yp; }

private:
    int xp;
    int yp;
};

inline uint qHash(const ThumbnailCell& key, uint seed = 0)
{
    return qHash(QPair<int,int>(key.x(), key.y()), seed);
}

class ThumbnailSettings
{
public:
    ThumbnailSettings() {}
    ThumbnailSettings(const ThumbnailSettings& rhs);
    ThumbnailSettings(const QString& fileName, const QSet<ThumbnailCell> &visibleCells);

    const QString &fileName() const
    { return mFileName; }

    void setVisibleCells(const QSet<ThumbnailCell> &visibleCells);

    const QSet<ThumbnailCell> visibleCells() const
    { return mVisibleCells; }

    bool cellVisible(const int x, const int y);

protected:
    friend class ThumbnailSettingsFile;
    QString visibleCellsString() const;
    bool parseVisibleCells(const QString &s);

private:
    QString mFileName;
    QSet<ThumbnailCell> mVisibleCells;
};

class ThumbnailSettingsFile
{
public:
    ThumbnailSettingsFile();
    ~ThumbnailSettingsFile();

    bool read(const QString &fileName);
    bool write(const QString &fileName, const QList<ThumbnailSettings *> &settings);

    QList<ThumbnailSettings *> takeSettings();

    const QString errorString() const
    { return mError; }

private:
    QString mError;
    QList<ThumbnailSettings*> mSettings;
};

#endif // THUMBNAILSETTINGSFILE_H
