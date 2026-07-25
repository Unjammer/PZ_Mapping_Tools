/*
 * Copyright 2023, Tim Baker <treectrl@users.sf.net>
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

#ifndef INGAMEMAPIMAGEDIALOG_H
#define INGAMEMAPIMAGEDIALOG_H

#include <QDialog>

class IsoMetaGrid;

namespace BuildingEditor {
class BuildingTile;
}

namespace Ui {
class InGameMapImageDialog;
}

class MapToPNGFileSettings
{
public:
};

class MapToPNGFileRule
{
public:
    QString mRuleName;
    QString mTilesetCompare;
    QString mTileset;
    int mTileIndexMin;
    int mTileIndexMax;
    QColor mColor;
};

class MapToPNGFile
{
public:
    bool read(const QString& filePath, MapToPNGFileSettings &settings, QList<MapToPNGFileRule> &rules);
    bool write(const QString& filePath, const MapToPNGFileSettings &settings, const QList<MapToPNGFileRule> &rules);

    bool parseColor(const QString &colorStr, int lineNumber, QColor &result);
    QString colorString(const QColor &color);

    QString mError;
};

class InGameMapImageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InGameMapImageDialog(QWidget *parent = nullptr);
    ~InGameMapImageDialog();

private slots:
    void chooseMapDirectory();
    void chooseRulesFile();
    void chooseOutputFile();
    void clickedTheButton();

private:
    void createImage();
    void cellToImage(QImage& image, const QString &mapDirectory, IsoMetaGrid &metaGrid, int cellX, int cellY);
    void tileToImage(QImage& image, const BuildingEditor::BuildingTile &buildingTile, int pixelX, int pixelY);

    Ui::InGameMapImageDialog *ui;
    bool mRunning = false;
    bool mStop = false;
    QList<MapToPNGFileRule> mRules;
};

#endif // INGAMEMAPIMAGEDIALOG_H
