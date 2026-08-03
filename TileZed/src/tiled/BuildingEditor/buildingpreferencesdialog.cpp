/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "buildingpreferencesdialog.h"
#include "ui_buildingpreferencesdialog.h"

#include "buildingpreferences.h"
#include "preferences.h"
#include "../../portablesettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>

using namespace BuildingEditor;

BuildingPreferencesDialog::BuildingPreferencesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BuildingPreferencesDialog),
    mThemeCombo(new QComboBox(this)),
    mSyncThemeCheckBox(new QCheckBox(
          tr("Apply to TileZed, BuildingEd and WorldEd"), this))
{
    ui->setupUi(this);

    QString configPath = prefs()->configPath();
    ui->configDirEdit->setText(QDir::toNativeSeparators(configPath));

    ui->gridColor->setColor(BuildingPreferences::instance()->gridColor());

    QHBoxLayout *themeLayout = new QHBoxLayout;
    themeLayout->addWidget(new QLabel(tr("Theme:"), this));
    mThemeCombo->addItems(
                Tiled::Internal::Preferences::instance()->availableThemes());
    const int themeIndex = mThemeCombo->findText(
                Tiled::Internal::Preferences::instance()->theme(),
                Qt::MatchFixedString);
    mThemeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    themeLayout->addWidget(mThemeCombo);
    mSyncThemeCheckBox->setChecked(
                PortableSettings::syncThemeAcrossApplications());
    mSyncThemeCheckBox->setToolTip(tr(
                "Store the selected theme in the shared portable settings "
                "and apply it to all three applications on their next start."));
    themeLayout->addWidget(mSyncThemeCheckBox);
    themeLayout->addStretch();
    ui->gridLayout->addLayout(themeLayout, 1, 0);
    connect(mThemeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        Tiled::Internal::Preferences::instance()->setTheme(
                    mThemeCombo->currentText());
    });
    connect(mSyncThemeCheckBox, &QAbstractButton::toggled,
            this, [this](bool enabled) {
        PortableSettings::setSyncThemeAcrossApplications(
                    enabled, mThemeCombo->currentText());
    });

    mUseOpenGL = prefs()->useOpenGL();
    ui->useOpenGL->setChecked(mUseOpenGL);
    connect(ui->useOpenGL, &QAbstractButton::toggled, this, &BuildingPreferencesDialog::setUseOpenGL);

    ui->isometric->setChecked(!prefs()->levelIsometric());
    ui->levelIsometric->setChecked(prefs()->levelIsometric());
}

BuildingPreferencesDialog::~BuildingPreferencesDialog()
{
    delete ui;
}

BuildingPreferences *BuildingPreferencesDialog::prefs() const
{
    return BuildingPreferences::instance();
}

void BuildingPreferencesDialog::setUseOpenGL(bool useOpenGL)
{
    mUseOpenGL = useOpenGL;
}

void BuildingPreferencesDialog::accept()
{
    prefs()->setGridColor(ui->gridColor->color());
    prefs()->setUseOpenGL(mUseOpenGL);
    prefs()->setLevelIsometric(ui->levelIsometric->isChecked());
    QDialog::accept();
}
