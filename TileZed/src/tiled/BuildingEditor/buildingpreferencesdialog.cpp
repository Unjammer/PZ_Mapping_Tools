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
#include "../../sharedmainwindowgeometrywidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

using namespace BuildingEditor;

BuildingPreferencesDialog::BuildingPreferencesDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BuildingPreferencesDialog),
    mThemeCombo(new QComboBox(this)),
    mSyncThemeCheckBox(new QCheckBox(
          tr("Apply to TileZed, BuildingEd and WorldEd"), this)),
    mProjectZomboidDirectory(new QLineEdit(this)),
    mAutoSaveCombo(new QComboBox(this))
{
    ui->setupUi(this);

    ui->tabWidget->addTab(
                new SharedMainWindowGeometryWidget(parent, ui->tabWidget),
                tr("Window Setup"));

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

    QHBoxLayout *gamePathLayout = new QHBoxLayout;
    gamePathLayout->addWidget(new QLabel(
                tr("Project Zomboid installation:"), this));
    mProjectZomboidDirectory->setReadOnly(true);
    mProjectZomboidDirectory->setText(QDir::toNativeSeparators(
        Tiled::Internal::Preferences::instance()
        ->projectZomboidDirectory()));
    mProjectZomboidDirectory->setToolTip(tr(
        "Shared read-only source for TileDefs, packs, WorldGen, procedural "
        "loot, and other installed-game data. Basement sources and PZBY "
        "files use the portable pzby_tbx directory beside bin."));
    gamePathLayout->addWidget(mProjectZomboidDirectory, 1);
    QPushButton *browseGamePath = new QPushButton(tr("Browse..."), this);
    QPushButton *clearGamePath = new QPushButton(tr("Clear"), this);
    gamePathLayout->addWidget(browseGamePath);
    gamePathLayout->addWidget(clearGamePath);
    ui->gridLayout->addLayout(gamePathLayout, 2, 0);
    QHBoxLayout *autoSaveLayout = new QHBoxLayout;
    autoSaveLayout->addWidget(new QLabel(tr("Autosave recovery copy:"), this));
    mAutoSaveCombo->addItem(tr("Disabled"), 0);
    for (int minutes : {1, 5, 10, 20, 60})
        mAutoSaveCombo->addItem(tr("Every %1 minute(s)").arg(minutes),
                                minutes);
    const int autoSaveIndex = mAutoSaveCombo->findData(
                prefs()->autoSaveIntervalMinutes());
    mAutoSaveCombo->setCurrentIndex(autoSaveIndex >= 0 ? autoSaveIndex : 0);
    autoSaveLayout->addWidget(mAutoSaveCombo);
    autoSaveLayout->addStretch();
    ui->gridLayout->addLayout(autoSaveLayout, 3, 0);
    connect(browseGamePath, &QAbstractButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(
                    this, tr("Project Zomboid Installation"),
                    mProjectZomboidDirectory->text());
        if (directory.isEmpty())
            return;
        const QString normalized =
                PortableSettings::normalizedGamePath(directory);
        if (normalized.isEmpty()) {
            QMessageBox::warning(
                        this, tr("Invalid Project Zomboid Installation"),
                        tr("Choose the Project Zomboid installation root or "
                           "its media directory."));
            return;
        }
        mProjectZomboidDirectory->setText(
                    QDir::toNativeSeparators(normalized));
    });
    connect(clearGamePath, &QAbstractButton::clicked,
            mProjectZomboidDirectory, &QLineEdit::clear);
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

    mUseOpenGL = false;
    ui->useOpenGL->setChecked(false);
    ui->useOpenGL->setVisible(false);

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
    prefs()->setAutoSaveIntervalMinutes(
                mAutoSaveCombo->currentData().toInt());
    Tiled::Internal::Preferences::instance()
            ->setProjectZomboidDirectory(
                mProjectZomboidDirectory->text().trimmed());
    QDialog::accept();
}
