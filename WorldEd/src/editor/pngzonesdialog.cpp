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

#include "pngzonesdialog.h"
#include "ui_pngzonesdialog.h"

#include "simplefile.h"
#include "world.h"
#include "worldcell.h"

#include "InGameMap/clipper.hpp"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>

static QLatin1String KEY_SETTINGS_FILE_PATH("PNGZonesDialog/SettingsFilePath");

PNGZonesDialog::PNGZonesDialog(World *world, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PNGZonesDialog),
    mWorld(world),
    mPainter(nullptr)
{
    ui->setupUi(this);

    connect(ui->pngBrowse, &QAbstractButton::clicked, this, &PNGZonesDialog::browse);
    connect(ui->colorButton, &Tiled::Internal::ColorButton::colorChanged,
            this, &PNGZonesDialog::colorChanged);
    connect(ui->imageBGButton, &Tiled::Internal::ColorButton::colorChanged,
            this, &PNGZonesDialog::imageBGColorChanged);
    connect(ui->imageTransparentBG, &QCheckBox::stateChanged, this, &PNGZonesDialog::imageTransparentChanged);
    connect(ui->saveAsButton, &QAbstractButton::clicked, this, &PNGZonesDialog::saveAs);
    connect(ui->loadButton, &QAbstractButton::clicked, this, &PNGZonesDialog::load);

    connect(ui->listWidget, &QListWidget::itemSelectionChanged, this, &PNGZonesDialog::selectionChanged);

    ui->imageBGButton->setColor(Qt::lightGray);

    for (ObjectType *ot : world->objectTypes()) {
        if (ot->isNull()) {
            continue;
        }
        QListWidgetItem *item = new QListWidgetItem(ot->name());
        QColor color = Qt::white;
        if (WorldObjectGroup *og = world->objectGroups().find(ot->name())) {
            color = og->color();
        }
        item->setData(Qt::DecorationRole, color);
        item->setCheckState(Qt::CheckState::Unchecked);
        ui->listWidget->addItem(item);
    }
    ui->listWidget->sortItems();

    QSettings qSettings;
    mSettingsFilePath = qSettings.value(KEY_SETTINGS_FILE_PATH).toString();
}

PNGZonesDialog::~PNGZonesDialog()
{
    delete ui;
}

bool PNGZonesDialog::generateWorld(World *world)
{
    QColor backgroundColor = ui->imageBGButton->color();
    if (ui->imageTransparentBG->isChecked()) {
        backgroundColor = Qt::transparent;
    }
    mImage = QImage(world->size() * world->cellSize(), QImage::Format_RGBA8888);
    mImage.fill(backgroundColor);

    QStringList validZones;
    QList<QColor> colors;
    for (int i = 0; i < ui->listWidget->count(); i++) {
        QListWidgetItem *item = ui->listWidget->item(i);
        if (item->checkState() != Qt::CheckState::Checked) {
            continue;
        }
        validZones += item->text();
        colors += item->data(Qt::DecorationRole).value<QColor>();
    }

    QPainter painter(&mImage);
    mPainter = &painter;

    for (int y = 0; y < world->height(); y++) {
        for (int x = 0; x < world->width(); x++) {
            if (!generateCell(world->cellAt(x, y), validZones, colors))
                goto errorExit;
        }
    }

    painter.end();

    if (!mImage.save(ui->pngEdit->text()))
        goto errorExit;

    return true;

errorExit:
    return false;
}

static QPolygonF createPolylineOutline(WorldCellObject *object)
{
    ClipperLib::ClipperOffset offset;
    ClipperLib::Path path;
    int SCALE = 100;
    for (int i = 0; i < object->points().size(); i++) {
        WorldCellObjectPoint p1 = object->points()[i];
        path << ClipperLib::IntPoint(p1.x * SCALE, p1.y * SCALE);
        if ((object->polylineWidth() % 2) != 0) {
            ClipperLib::IntPoint cp = path[path.size()-1];
            path[path.size()-1] = ClipperLib::IntPoint(cp.X + SCALE / 2, cp.Y + SCALE / 2);
        }
    }
    offset.AddPath(path, ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etOpenButt);
    ClipperLib::Paths paths;
    offset.Execute(paths, object->polylineWidth() * SCALE / 2.0);
    QPolygonF result;
    if (paths.empty()) {
        return result;
    }
    int cellX = object->cell()->x();
    int cellY = object->cell()->y();
    const int cellSize = object->cell()->world()->cellSize();
    ClipperLib::Path cPath = paths.at(0);
    for (const auto &cPoint : cPath) {
        result << QPointF(cellX * cellSize + cPoint.X / (qreal) SCALE,
                          cellY * cellSize + cPoint.Y / (qreal) SCALE);
    }
    return result;
}

bool PNGZonesDialog::generateCell(WorldCell *cell, const QStringList &objectTypes, const QList<QColor> &colors)
{
    QPainter *painter = mPainter;
    const int cellSize = cell->world()->cellSize();
#if 0
    QStringList validZones;
    validZones << QLatin1String("DeepForest");
    validZones << QLatin1String("Farm");
    validZones << QLatin1String("FarmLand");
    validZones << QLatin1String("Forest");
    validZones << QLatin1String("Nav");
    validZones << QLatin1String("TownZone");
    validZones << QLatin1String("TrailerPark");
    validZones << QLatin1String("Vegitation");
#endif
    for (WorldCellObject *object : cell->objects()) {
        if (object->type() == nullptr) {
            continue;
        }
        int index = objectTypes.indexOf(object->type()->name());
        if (index == -1)
            continue;
        QColor color = colors[index];
//        color.setAlpha(50);
        painter->setBrush(QBrush(color));
        painter->setPen(Qt::NoPen);
        if (object->isRectangle()) {
            QPointF p1(cell->x() * cellSize + object->x(), cell->y() * cellSize + object->y());
            QPointF p2(cell->x() * cellSize + (object->x() + object->width()),
                       cell->y() * cellSize + (object->y() + object->height()));
            painter->drawRect(QRectF(p1, p2));
        }
        if (object->isPolyline()) {
            painter->drawPolygon(createPolylineOutline(object));
        }
        if (object->isPolygon()) {
            QPolygonF poly;
            for (WorldCellObjectPoint pt : object->points()) {
                poly << QPointF(cell->x() * cellSize + pt.x, cell->y() * cellSize + pt.y);
            }
            painter->drawPolygon(poly);
        }
#if 0
        if (object->isPolyline() && (object->polylineWidth() > 0)) {
            if (mPolylineOutlines.contains(object) == false) {
                mPolylineOutlines[object] = createPolylineOutline(mScene, object);
            }
            QPolygonF poly = mPolylineOutlines[object];
            painter->drawPolygon(poly);
        }
#endif
    }
    return true;
}

void PNGZonesDialog::accept()
{
    if (ui->pngEdit->text().isEmpty())
        return;

    if (!generateWorld(mWorld)) {
        QMessageBox::warning(this, tr("PNG Generation Failed"), mError);
    }

    QDialog::accept();
}

void PNGZonesDialog::browse()
{
    QString f = QFileDialog::getSaveFileName(this, tr("Save PNG As..."),
                                             ui->pngEdit->text(), QLatin1String("PNG Files (*.png)"));
    if (f.isEmpty())
        return;
    ui->pngEdit->setText(QDir::toNativeSeparators(f));
}

void PNGZonesDialog::selectionChanged()
{
    QList<QListWidgetItem*> selection = ui->listWidget->selectedItems();
    if (selection.size() != 1) {
        return;
    }
    QListWidgetItem *item = selection.first();
    ui->colorButton->setColor(item->data(Qt::DecorationRole).value<QColor>());
}

void PNGZonesDialog::colorChanged(const QColor &color)
{
    QList<QListWidgetItem*> selection = ui->listWidget->selectedItems();
    if (selection.size() != 1) {
        return;
    }
    QListWidgetItem *item = selection.first();
    item->setData(Qt::DecorationRole, color);
}

void PNGZonesDialog::imageBGColorChanged(const QColor &color)
{
    ui->imageTransparentBG->setChecked((color.isValid() == false) || (color.alpha() == 0));
}

void PNGZonesDialog::imageTransparentChanged(int state)
{
    if (state == Qt::CheckState::Checked) {
        ui->imageBGButton->setColor(Qt::transparent);
    } else if (ui->imageBGButton->color().alpha() == 0) {
        ui->imageBGButton->setColor(Qt::lightGray);
    }
}

void PNGZonesDialog::saveAs()
{
    QString defaultFile = mSettingsFilePath.isEmpty() ? QLatin1String("PNGZones.txt") : mSettingsFilePath;
    QString filePath = QFileDialog::getSaveFileName(this, tr("Choose PNGZones.txt File"), defaultFile, tr("Text files (*.txt)"));
    if (filePath.isEmpty())
        return;
    mSettingsFilePath = filePath;
    QSettings qSettings;
    qSettings.setValue(KEY_SETTINGS_FILE_PATH, mSettingsFilePath);
    QList<PNGObjectFileValue> values;
    for (int i = 0; i < ui->listWidget->count(); i++) {
        QListWidgetItem *item = ui->listWidget->item(i);
        if (item->checkState() != Qt::CheckState::Checked) {
            continue;
        }
        PNGObjectFileValue value;
        value.mName = item->text();
        value.mColor = item->data(Qt::DecorationRole).value<QColor>();
        values += value;
    }
    PNGObjectsFile file;
    PNGObjectFileSettings settings;
    settings.mOutputFilePath = ui->pngEdit->text();
    settings.mBackgroundColor = ui->imageTransparentBG->isChecked() ? QColor() : ui->imageBGButton->color();
    if (file.write(filePath, settings, values) == false) {
        QMessageBox::critical(this, tr("Zone Export Error"),
                              tr("Error while writing %1\n%2")
                              .arg(filePath)
                              .arg(file.mError));
    }
}

void PNGZonesDialog::load()
{
    QString defaultFile = mSettingsFilePath.isEmpty() ? QLatin1String("PNGZones.txt") : mSettingsFilePath;
    QString filePath = QFileDialog::getOpenFileName(this, tr("Choose PNGZones.txt File"), defaultFile, tr("Text files (*.txt)"));
    if (filePath.isEmpty())
        return;
    mSettingsFilePath = filePath;
    QSettings qSettings;
    qSettings.setValue(KEY_SETTINGS_FILE_PATH, mSettingsFilePath);
    PNGObjectsFile file;
    PNGObjectFileSettings settings;
    QList<PNGObjectFileValue> values;
    if (file.read(filePath, settings, values) == false) {
        QMessageBox::critical(this, tr("Zone Import Error"),
                              tr("Error while reading %1\n%2")
                              .arg(filePath)
                              .arg(file.mError));
        return;
    }
    ui->pngEdit->setText(settings.mOutputFilePath);
    QColor backgroundColor = settings.mBackgroundColor;
    ui->imageBGButton->setColor(backgroundColor);
    ui->imageTransparentBG->setChecked((backgroundColor.isValid() == false) || (backgroundColor.alpha() == 0));

    for (int i = 0; i < ui->listWidget->count(); i++) {
        QListWidgetItem *item = ui->listWidget->item(i);
        const auto it = std::find_if(values.cbegin(), values.cend(), [&item](const PNGObjectFileValue &value) {
            return item->text() == value.mName;
        });
        if (it == values.cend()) {
            item->setCheckState(Qt::CheckState::Unchecked);
            item->setData(Qt::DecorationRole, QColor(Qt::white));
            continue;
        }
        item->setCheckState(Qt::CheckState::Checked);
        item->setData(Qt::DecorationRole, it->mColor);
    }
}

/////

const int VERSION_LATEST = 1;

bool PNGObjectsFile::read(const QString &filePath, PNGObjectFileSettings &settings, QList<PNGObjectFileValue> &values)
{
    SimpleFile simpleFile;
    if (!simpleFile.read(filePath)) {
        mError = simpleFile.errorString();
        return false;
    }
    for (SimpleFileBlock block : simpleFile.blocks) {
        SimpleFileKeyValue kv;
        if (block.name == QLatin1String("ObjectType")) {
            QString name = block.value("name").trimmed();
            QString colorStr = block.value("color").trimmed();
            if (name.isEmpty()) {
                mError = QString::fromLatin1("Line %1: Empty or missing name")
                        .arg(block.lineNumber);
                return false;
            }
            QColor color = Qt::white;
            if (parseColor(colorStr, block.lineNumber, color) == false) {
                return false;
            }
            PNGObjectFileValue value;
            value.mName = name;
            value.mColor = color;
            values += value;
        } else if (block.name == QLatin1String("Settings")) {
            settings.mOutputFilePath = block.value("outputPath").trimmed();
            QString colorStr = block.value("backgroundColor").trimmed();
            if (colorStr.isEmpty() == false) {
                if (parseColor(colorStr, block.lineNumber, settings.mBackgroundColor) == false) {
                    return false;
                }
            }
        } else {
            mError = QString::fromLatin1("Line %1: Unknown block name '%2'")
                    .arg(block.lineNumber)
                    .arg(block.name);
            return false;
        }
    }
    return true;
}

bool PNGObjectsFile::write(const QString &filePath, const PNGObjectFileSettings &settings, const QList<PNGObjectFileValue> &values)
{
    SimpleFile simpleFile;

    SimpleFileBlock settingsBlock;
    settingsBlock.name = QLatin1String("Settings");
    settingsBlock.addValue("outputPath", settings.mOutputFilePath);
    if (settings.mBackgroundColor.isValid()) {
        settingsBlock.addValue("backgroundColor", colorString(settings.mBackgroundColor));
    }
    simpleFile.blocks += settingsBlock;

    for (const PNGObjectFileValue &value : values) {
        SimpleFileBlock block;
        block.name = QLatin1String("ObjectType");
        block.addValue(QLatin1String("name"), value.mName);
        block.addValue(QLatin1String("color"), colorString(value.mColor));
        simpleFile.blocks += block;
    }

    simpleFile.setVersion(VERSION_LATEST);
    if (!simpleFile.write(filePath)) {
        mError = simpleFile.errorString();
        return false;
    }
    return true;
}

bool PNGObjectsFile::parseColor(const QString &colorStr, int lineNumber, QColor &result)
{
    QStringList rgb = colorStr.split(QLatin1String(","), Qt::SkipEmptyParts);
    if (rgb.size() < 3 || rgb.size() > 4) {
        mError = QString::fromLatin1(
                    "Line %1: Color '%2' has %3 component(s); expected "
                    "R,G,B or R,G,B,A with values from 0 to 255.")
                .arg(lineNumber)
                .arg(colorStr)
                .arg(rgb.size());
        return false;
    }
    const char *componentNames[] = {"red", "green", "blue", "alpha"};
    int components[] = {0, 0, 0, 255};
    for (int index = 0; index < rgb.size(); ++index) {
        bool ok = false;
        const QString token = rgb.at(index).trimmed();
        components[index] = token.toInt(&ok);
        if (!ok || components[index] < 0 || components[index] > 255) {
            mError = QString::fromLatin1(
                        "Line %1: The %2 color component '%3' is invalid; "
                        "expected an integer from 0 to 255 in '%4'.")
                    .arg(lineNumber)
                    .arg(QString::fromLatin1(componentNames[index]))
                    .arg(token)
                    .arg(colorStr);
            return false;
        }
    }
    result = QColor(components[0], components[1],
                    components[2], components[3]);
    return true;
}

QString PNGObjectsFile::colorString(const QColor &color)
{
    return QString::fromLatin1("%1,%2,%3,%4")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha());
}
