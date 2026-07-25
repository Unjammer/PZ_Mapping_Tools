/*
 * Copyright 2024, Tim Baker <treectrl@users.sf.net>
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

#ifndef PNGZONESDIALOG_H
#define PNGZONESDIALOG_H

#include <QDialog>
#include <QImage>

namespace Ui {
class PNGZonesDialog;
}

class World;
class WorldCell;

class QPainter;

class PNGZonesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PNGZonesDialog(World *world, QWidget *parent = nullptr);
    ~PNGZonesDialog();

private:
    bool generateWorld(World *world);
    bool generateCell(WorldCell *cell, const QStringList &objectTypes, const QList<QColor> &colors);

private slots:
    void accept();
    void browse();
    void selectionChanged();
    void colorChanged(const QColor &color);
    void imageBGColorChanged(const QColor &color);
    void imageTransparentChanged(int state);
    void saveAs();
    void load();

private:
    Ui::PNGZonesDialog *ui;
    World *mWorld;
    QImage mImage;
    QPainter *mPainter;
    QString mSettingsFilePath;
    QString mError;
};

class PNGObjectFileSettings
{
public:
    QString mOutputFilePath;
    QColor mBackgroundColor;
};

class PNGObjectFileValue
{
public:
    QString mName;
    QColor mColor;
};

class PNGObjectsFile
{
public:
    bool read(const QString& filePath, PNGObjectFileSettings &settings, QList<PNGObjectFileValue> &values);
    bool write(const QString& filePath, const PNGObjectFileSettings &settings, const QList<PNGObjectFileValue> &values);

    bool parseColor(const QString &colorStr, int lineNumber, QColor &result);
    QString colorString(const QColor &color);

    QString mError;
};

#endif // PNGZONESDIALOG_H
