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

#include "bmptotmxdialog.h"
#include "ui_bmptotmxdialog.h"

#include "bmpblender.h"
#include "bmptotmx.h"
#include "mainwindow.h"
#include "world.h"
#include "worlddocument.h"

#include "map.h"

#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSet>

using namespace Tiled;
using namespace Tiled::Internal;

BMPToTMXDialog::BMPToTMXDialog(WorldDocument *worldDoc, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BMPToTMXDialog),
    mWorldDoc(worldDoc)
{
    ui->setupUi(this);

    const BMPToTMXSettings &settings = worldDoc->world()->getBMPToTMXSettings();

    // Export directory
    mExportDir = settings.exportDir;
    if (mExportDir.isEmpty() && !worldDoc->fileName().isEmpty()) {
        QFileInfo info(worldDoc->fileName());
        mExportDir = info.absolutePath() + QLatin1Char('/')
                + QLatin1String("tmxexport");
        info.setFile(mExportDir);
        if (info.exists())
            mExportDir = info.canonicalFilePath();
    }
    ui->exportEdit->setText(QDir::toNativeSeparators(mExportDir));
    connect(ui->exportBrowse, &QAbstractButton::clicked, this, &BMPToTMXDialog::exportBrowse);

    // Rules.txt
    mRulesFile = settings.rulesFile;
    if (mRulesFile.isEmpty()) {
        mRulesFile = BMPToTMX::instance()->defaultRulesFile();
        QFileInfo info(mRulesFile);
        if (info.exists())
            mRulesFile = info.canonicalFilePath();
    }
    ui->rulesEdit->setText(QDir::toNativeSeparators(mRulesFile));
    connect(ui->rulesBrowse, &QAbstractButton::clicked, this, &BMPToTMXDialog::rulesBrowse);

    // Blends.txt
    mBlendsFile = settings.blendsFile;
    if (mBlendsFile.isEmpty()) {
        mBlendsFile = BMPToTMX::instance()->defaultBlendsFile();
        QFileInfo info(mBlendsFile);
        if (info.exists())
            mBlendsFile = info.canonicalFilePath();
    }
    ui->blendsEdit->setText(QDir::toNativeSeparators(mBlendsFile));
    connect(ui->blendsBrowse, &QAbstractButton::clicked, this, &BMPToTMXDialog::blendsBrowse);

    // MapBaseXML.txt
    mMapBaseFile = settings.mapbaseFile;
    if (mMapBaseFile.isEmpty()) {
        mMapBaseFile = BMPToTMX::instance()->defaultMapBaseXMLFile();
        QFileInfo info(mMapBaseFile);
        if (info.exists())
            mMapBaseFile = info.canonicalFilePath();
    }
    ui->mapbaseEdit->setText(QDir::toNativeSeparators(mMapBaseFile));
    connect(ui->mapbaseBrowse, &QAbstractButton::clicked, this, &BMPToTMXDialog::mapbaseBrowse);

    ui->assignMapCheckBox->setChecked(settings.assignMapsToWorld);
    ui->warnUnknownColors->setChecked(settings.warnUnknownColors);
    ui->repairUnknownColors->setChecked(settings.repairUnknownColors);
    populateFallbackColors(settings.unknownGroundFallback,
                           settings.unknownVegetationFallback);
    repairUnknownColorsToggled(settings.repairUnknownColors);
    connect(ui->repairUnknownColors, &QCheckBox::toggled,
            this, &BMPToTMXDialog::repairUnknownColorsToggled);
//    ui->compress->setChecked(settings.compress);
//    ui->copyPixels->setChecked(settings.copyPixels);
    ui->replaceExisting->setChecked(!settings.updateExisting);
    ui->updateExisting->setChecked(settings.updateExisting);

    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QAbstractButton::clicked,
            this, &BMPToTMXDialog::apply);
}

BMPToTMXDialog::~BMPToTMXDialog()
{
    delete ui;
}

void BMPToTMXDialog::exportBrowse()
{
    QString f = QFileDialog::getExistingDirectory(this, tr("Choose the BMP to TMX Folder"),
        ui->exportEdit->text());
    if (!f.isEmpty()) {
        mExportDir = f;
        ui->exportEdit->setText(QDir::toNativeSeparators(mExportDir));
    }
}

void BMPToTMXDialog::rulesBrowse()
{
    QString f = QFileDialog::getOpenFileName(this, tr("Choose the Rules.txt File"),
        ui->rulesEdit->text());
    if (!f.isEmpty()) {
        const quint32 groundColor = fallbackColor(0);
        const quint32 vegetationColor = fallbackColor(1);
        mRulesFile = f;
        ui->rulesEdit->setText(QDir::toNativeSeparators(mRulesFile));
        populateFallbackColors(groundColor, vegetationColor);
    }
}

void BMPToTMXDialog::blendsBrowse()
{
    QString f = QFileDialog::getOpenFileName(this, tr("Choose the Blends.txt File"),
        ui->blendsEdit->text());
    if (!f.isEmpty()) {
        mBlendsFile = f;
        ui->blendsEdit->setText(QDir::toNativeSeparators(mBlendsFile));
    }
}

void BMPToTMXDialog::mapbaseBrowse()
{
    QString f = QFileDialog::getOpenFileName(this, tr("Choose the MapBaseXML.txt File"),
        ui->mapbaseEdit->text());
    if (!f.isEmpty()) {
        mMapBaseFile = f;
        ui->mapbaseEdit->setText(QDir::toNativeSeparators(mMapBaseFile));
    }
}

void BMPToTMXDialog::repairUnknownColorsToggled(bool enabled)
{
    ui->groundFallbackLabel->setEnabled(enabled);
    ui->groundFallback->setEnabled(enabled);
    ui->vegetationFallbackLabel->setEnabled(enabled);
    ui->vegetationFallback->setEnabled(enabled);
}

void BMPToTMXDialog::populateFallbackColors(
        quint32 groundColor, quint32 vegetationColor)
{
    ui->groundFallback->clear();
    ui->vegetationFallback->clear();

    const auto addColor = [](QComboBox *combo, QRgb color,
                             const QString &description) {
        QPixmap swatch(18, 18);
        swatch.fill(QColor::fromRgb(color));
        const QString hex = QStringLiteral("#%1")
                .arg(color & 0x00ffffff, 6, 16, QLatin1Char('0'))
                .toUpper();
        combo->addItem(
                    QIcon(swatch),
                    description.isEmpty()
                    ? hex
                    : QStringLiteral("%1 - %2").arg(hex, description),
                    QVariant::fromValue<quint32>(color));
    };

    const QRgb black = qRgb(0, 0, 0);
    addColor(ui->groundFallback, black, tr("Black / empty"));
    addColor(ui->vegetationFallback, black, tr("Black / empty"));

    BmpRulesFile file;
    if (file.read(mRulesFile)) {
        QSet<QRgb> groundColors;
        QSet<QRgb> vegetationColors;
        groundColors.insert(black);
        vegetationColors.insert(black);
        for (const BmpRule *rule : file.rules()) {
            if (!rule)
                continue;
            QComboBox *combo = rule->bitmapIndex == 0
                    ? ui->groundFallback
                    : rule->bitmapIndex == 1
                      ? ui->vegetationFallback : nullptr;
            QSet<QRgb> *colors = rule->bitmapIndex == 0
                    ? &groundColors
                    : rule->bitmapIndex == 1
                      ? &vegetationColors : nullptr;
            if (!combo || !colors || colors->contains(rule->color))
                continue;
            colors->insert(rule->color);
            addColor(combo, rule->color,
                     rule->label.isEmpty()
                     ? rule->tileChoices.mid(0, 2).join(
                         QStringLiteral(", "))
                     : rule->label);
        }
    }

    const auto restoreColor = [](QComboBox *combo, quint32 color) {
        for (int index = 0; index < combo->count(); ++index) {
            if (combo->itemData(index).toUInt() == color) {
                combo->setCurrentIndex(index);
                return;
            }
        }
        combo->setCurrentIndex(0);
    };
    restoreColor(ui->groundFallback, groundColor);
    restoreColor(ui->vegetationFallback, vegetationColor);
}

quint32 BMPToTMXDialog::fallbackColor(int bitmapIndex) const
{
    const QComboBox *combo = bitmapIndex == 0
            ? ui->groundFallback : ui->vegetationFallback;
    return combo && combo->currentIndex() >= 0
            ? combo->currentData().toUInt()
            : quint32(qRgb(0, 0, 0));
}

void BMPToTMXDialog::accept()
{
    if (!validate())
        return;

    toSettings();

    QDialog::accept();
}

void BMPToTMXDialog::apply()
{
    if (!validate())
        return;

    toSettings();

    QDialog::reject();
}

bool BMPToTMXDialog::validate()
{
    // Line edits are user-editable; do not rely solely on Browse button
    // callbacks to update the backing values.
    mExportDir = QDir::fromNativeSeparators(
                ui->exportEdit->text().trimmed());
    mRulesFile = QDir::fromNativeSeparators(
                ui->rulesEdit->text().trimmed());
    mBlendsFile = QDir::fromNativeSeparators(
                ui->blendsEdit->text().trimmed());
    mMapBaseFile = QDir::fromNativeSeparators(
                ui->mapbaseEdit->text().trimmed());

    QDir dir(mExportDir);
    if (mExportDir.isEmpty() || !dir.exists()) {
        QMessageBox::warning(this, tr("Map Generation Error"),
                             tr("Please choose a valid directory to save the .tmx files in."));
        return false;
    }

    QFileInfo info(mRulesFile);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("Map Generation Error"),
                             tr("Please choose a rules file."));
        return false;
    }

    info.setFile(mBlendsFile);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("Map Generation Error"),
                             tr("Please choose a blends file."));
        return false;
    }

    info.setFile(mMapBaseFile);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("Map Generation Error"),
                             tr("Please choose a map template file."));
        return false;
    }

    return true;
}

void BMPToTMXDialog::toSettings()
{
    if (QFileInfo(mRulesFile) == QFileInfo(BMPToTMX::instance()->defaultRulesFile()))
        mRulesFile.clear();
    if (QFileInfo(mBlendsFile) == QFileInfo(BMPToTMX::instance()->defaultBlendsFile()))
        mBlendsFile.clear();
    if (QFileInfo(mMapBaseFile) == QFileInfo(BMPToTMX::instance()->defaultMapBaseXMLFile()))
        mMapBaseFile.clear();

    BMPToTMXSettings settings;
    settings.exportDir = mExportDir;
    settings.rulesFile = mRulesFile;
    settings.blendsFile = mBlendsFile;
    settings.mapbaseFile = mMapBaseFile;
    settings.assignMapsToWorld = ui->assignMapCheckBox->isChecked();
    settings.warnUnknownColors = ui->warnUnknownColors->isChecked();
    settings.repairUnknownColors =
            ui->repairUnknownColors->isChecked();
    settings.unknownGroundFallback = fallbackColor(0);
    settings.unknownVegetationFallback = fallbackColor(1);
//    settings.compress = ui->compress->isChecked();
//    settings.copyPixels = ui->copyPixels->isChecked();
    settings.updateExisting = ui->updateExisting->isChecked();
    if (settings != mWorldDoc->world()->getBMPToTMXSettings())
        mWorldDoc->changeBMPToTMXSettings(settings);
}
