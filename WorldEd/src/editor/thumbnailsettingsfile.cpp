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

#include "thumbnailsettingsfile.h"
#include "simplefile.h"

#include <QFileInfo>

#define VERSION1 1
#define VERSION_LATEST VERSION1

ThumbnailSettingsFile::ThumbnailSettingsFile() {}

ThumbnailSettingsFile::~ThumbnailSettingsFile()
{
    qDeleteAll(mSettings);
}

bool ThumbnailSettingsFile::read(const QString &fileName)
{
    QSet<ThumbnailCell> test;
    test += ThumbnailCell();
    test += ThumbnailCell(1, 1);
    test += ThumbnailCell(2, 2);
    QSet<QString> test2;
    test2 += QStringLiteral("1,1");
    test2 += QStringLiteral("2,2");
    test2 += QStringLiteral("3,3");

    QFileInfo info(fileName);
    if (!info.exists()) {
        mError = QStringLiteral("The %1 file doesn't exist.").arg(fileName);
        return false;
    }

    QString path2 = info.canonicalFilePath();
    SimpleFile simple;
    if (!simple.read(path2)) {
        mError = simple.errorString();
        return false;
    }

    if (simple.version() != VERSION_LATEST) {
        mError = QStringLiteral("Expected version %1, got %2.\n%3").arg(VERSION_LATEST).arg(simple.version()).arg(path2);
        return false;
    }

    for (const SimpleFileBlock& block : std::as_const(simple.blocks)) {
        if (block.name == QStringLiteral("world")) {
            ThumbnailSettings settings;
            settings.mFileName = block.value("file");
            if (settings.mFileName.isEmpty()) {
                continue;
            }
            if (!settings.parseVisibleCells(block.value("visibleCells"))) {
                mError = QString::fromLatin1("Line %1: Invalid/missing visibleCells.\n%2").arg(block.lineNumber).arg(path2);
                return false;
            }
            mSettings += new ThumbnailSettings(settings);
        } else {
            mError = QString::fromLatin1("Line %1: Unknown block name '%2'.\n%3").arg(block.lineNumber).arg(block.name).arg(path2);
            return false;
        }
    }
    return true;
}

bool ThumbnailSettingsFile::write(const QString &fileName, const QList<ThumbnailSettings*> &settings)
{
    SimpleFile simpleFile;
    for (const ThumbnailSettings* settings1 : settings) {
        SimpleFileBlock worldBlock;
        worldBlock.name = QStringLiteral("world");
        worldBlock.values += SimpleFileKeyValue(QStringLiteral("file"), settings1->mFileName);
        worldBlock.values += SimpleFileKeyValue(QStringLiteral("visibleCells"), settings1->visibleCellsString());
        simpleFile.blocks += worldBlock;
    }
    simpleFile.setVersion(VERSION_LATEST);
    if (!simpleFile.write(fileName)) {
        mError = simpleFile.errorString();
        return false;
    }
    return true;
}

QList<ThumbnailSettings*> ThumbnailSettingsFile::takeSettings()
{
    QList<ThumbnailSettings*> result = mSettings;
    mSettings.clear();
    return result;
}

/////

ThumbnailSettings::ThumbnailSettings(const ThumbnailSettings &rhs) :
    mFileName(rhs.mFileName),
    mVisibleCells(rhs.mVisibleCells)
{

}

ThumbnailSettings::ThumbnailSettings(const QString &fileName, const QSet<ThumbnailCell> &visibleCells) :
    mFileName(fileName),
    mVisibleCells(visibleCells)
{

}

void ThumbnailSettings::setVisibleCells(const QSet<ThumbnailCell> &visibleCells)
{
    mVisibleCells = visibleCells;
}

bool ThumbnailSettings::cellVisible(const int x, const int y)
{
    return mVisibleCells.contains(ThumbnailCell(x, y));
}

QString ThumbnailSettings::visibleCellsString() const
{
    QString result;
    for (const ThumbnailCell& cell : mVisibleCells) {
        result += QStringLiteral("%1,%2 ").arg(cell.x()).arg(cell.y());
    }
    return result.trimmed();
}

static bool parse2Ints(const QString &s, int& pa, int& pb)
{
    QStringList coords = s.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (coords.size() != 2) {
        return false;
    }
    bool ok;
    int a = coords[0].toInt(&ok);
    if (!ok) {
        return false;
    }
    int b = coords[1].toInt(&ok);
    if (!ok) {
        return false;
    }
    pa = a, pb = b;
    return true;
}

bool ThumbnailSettings::parseVisibleCells(const QString &s)
{
    mVisibleCells.clear();
    const QStringList ss = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& cellStr : ss) {
        int x, y;
        if (!parse2Ints(cellStr, x, y)) {
            return false;
        }
        mVisibleCells.insert(ThumbnailCell(x, y));
    }
    return true;
}
