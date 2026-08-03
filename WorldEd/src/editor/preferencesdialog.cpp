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

#include "preferencesdialog.h"
#include "ui_preferencesdialog.h"

#include "preferences.h"
#include "tilemetainfomgr.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"

#include <QFileDialog>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(WorldDocument *worldDoc, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PreferencesDialog)
    , mWorldDoc(worldDoc)
    , mRoadSimplificationHighway(new QDoubleSpinBox(this))
    , mRoadPointSpacingHighway(new QSpinBox(this))
    , mRoadSimplificationTrail(new QDoubleSpinBox(this))
    , mRoadPointSpacingTrail(new QSpinBox(this))
    , mRoadSimplificationRailway(new QDoubleSpinBox(this))
    , mRoadPointSpacingRailway(new QSpinBox(this))
    , mSyncThemeCheckBox(new QCheckBox(
          tr("Apply to TileZed, BuildingEd and WorldEd"), this))
{
    ui->setupUi(this);
    connect(ui->sharedPathsButton, &QAbstractButton::clicked,
            this, [this]() {
        if (!FirstLaunchDialog::configureSharedPaths(this))
            return;
        mTilesDirectory = PortableSettings::sharedTilesPath();
        ui->tilesDirectory->setText(
                    QDir::toNativeSeparators(mTilesDirectory));
        ui->configDirectory->setText(QDir::toNativeSeparators(
                    PortableSettings::sharedConfigurationPath()));
        QMessageBox::information(
                    this, tr("Shared Paths Updated"),
                    tr("The shared paths were saved. The Tiles path will be "
                       "applied when you confirm Preferences. Restart all "
                       "PZTools applications to reload configuration catalogs "
                       "from the new location."));
    });

    Preferences *prefs = Preferences::instance();

    ui->themeCombo->clear();
    ui->themeCombo->addItems(prefs->availableThemes());
    const int themeIndex = ui->themeCombo->findText(prefs->theme(), Qt::MatchFixedString);
    ui->themeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    mSyncThemeCheckBox->setChecked(
                PortableSettings::syncThemeAcrossApplications());
    mSyncThemeCheckBox->setToolTip(tr(
                "Store the selected theme in the shared portable settings "
                "and apply it to all three applications on their next start."));
    ui->themeLayout->addWidget(mSyncThemeCheckBox);
    connect(ui->themeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PreferencesDialog::themeChanged);
    connect(mSyncThemeCheckBox, &QAbstractButton::toggled,
            this, [this](bool enabled) {
        PortableSettings::setSyncThemeAcrossApplications(
                    enabled, ui->themeCombo->currentText());
    });

    mTilesDirectory = prefs->tilesDirectory();
    ui->tilesDirectory->setText(QDir::toNativeSeparators(mTilesDirectory));
    connect(ui->browseTilesDirectory, &QAbstractButton::clicked,
            this, &PreferencesDialog::browseTilesDirectory);

    QString configPath = prefs->configPath();
    ui->configDirectory->setText(QDir::toNativeSeparators(configPath));

    mGridColor = prefs->gridColor();
    ui->gridColor->setColor(mGridColor);
    connect(ui->gridColor, &Tiled::Internal::ColorButton::colorChanged,
            this, &PreferencesDialog::gridColorChanged);
    ui->gridWidth->setValue(prefs->gridWidth());
    ui->thumbnailWidth->setValue(prefs->thumbnailWidth());
    ui->terrainImageMemoryLimit->setValue(
                prefs->terrainImageMemoryLimitMiB());
    ui->restoreLastSession->setChecked(prefs->restoreLastSession());

    ui->openGL->setChecked(prefs->useOpenGL());
    ui->thumbnails->setChecked(prefs->loadAllWorldThumbnails());
    ui->showAdjacent->setChecked(prefs->showAdjacentMaps());
    ui->zombieSpawnImageOpacity->setValue(int(prefs->zombieSpawnImageOpacity() * 100));

    QWidget *featuresTab = new QWidget(ui->tabWidget);
    QVBoxLayout *featuresLayout = new QVBoxLayout(featuresTab);
    QLabel *description = new QLabel(tr(
            "These settings control how generated road polygons are simplified. "
            "A higher tolerance produces fewer points. Maximum point spacing "
            "keeps long contours from becoming too coarse."), featuresTab);
    description->setWordWrap(true);
    featuresLayout->addWidget(description);

    auto addFeatureGroup = [featuresLayout](const QString &title,
                                             QDoubleSpinBox *tolerance,
                                             QSpinBox *spacing) {
        QGroupBox *group = new QGroupBox(title);
        QFormLayout *form = new QFormLayout(group);
        tolerance->setDecimals(2);
        tolerance->setRange(0.0, 32.0);
        tolerance->setSingleStep(0.25);
        tolerance->setSuffix(QObject::tr(" tiles"));
        tolerance->setToolTip(QObject::tr(
                "Maximum deviation allowed by Douglas-Peucker simplification. "
                "Set to 0 to preserve every contour corner."));
        spacing->setRange(1, 300);
        spacing->setSuffix(QObject::tr(" points"));
        spacing->setToolTip(QObject::tr(
                "Force at least one retained point within this many source contour points."));
        form->addRow(QObject::tr("Simplification tolerance:"), tolerance);
        form->addRow(QObject::tr("Maximum point spacing:"), spacing);
        featuresLayout->addWidget(group);
    };

    addFeatureGroup(tr("Primary / Secondary / Tertiary Roads"),
                    mRoadSimplificationHighway, mRoadPointSpacingHighway);
    addFeatureGroup(tr("Trails"),
                    mRoadSimplificationTrail, mRoadPointSpacingTrail);
    addFeatureGroup(tr("Railways"),
                    mRoadSimplificationRailway, mRoadPointSpacingRailway);

    mRoadSimplificationHighway->setValue(prefs->roadSimplificationHighway());
    mRoadPointSpacingHighway->setValue(prefs->roadPointSpacingHighway());
    mRoadSimplificationTrail->setValue(prefs->roadSimplificationTrail());
    mRoadPointSpacingTrail->setValue(prefs->roadPointSpacingTrail());
    mRoadSimplificationRailway->setValue(prefs->roadSimplificationRailway());
    mRoadPointSpacingRailway->setValue(prefs->roadPointSpacingRailway());

    QPushButton *defaults = new QPushButton(tr("Restore road-generation defaults"), featuresTab);
    connect(defaults, &QAbstractButton::clicked, this, [this]() {
        mRoadSimplificationHighway->setValue(2.0);
        mRoadPointSpacingHighway->setValue(40);
        mRoadSimplificationTrail->setValue(2.0);
        mRoadPointSpacingTrail->setValue(40);
        mRoadSimplificationRailway->setValue(2.0);
        mRoadPointSpacingRailway->setValue(40);
    });
    featuresLayout->addWidget(defaults);
    featuresLayout->addStretch();
    ui->tabWidget->addTab(featuresTab, tr("Feature Generation"));

}

void PreferencesDialog::browseTilesDirectory()
{
    QString f = QFileDialog::getExistingDirectory(this, tr("Tiles Directory"),
                                                  ui->tilesDirectory->text());
    if (!f.isEmpty()) {
        mTilesDirectory = f;
        ui->tilesDirectory->setText(QDir::toNativeSeparators(f));
    }
}

void PreferencesDialog::themeChanged(int)
{
    QString text = ui->themeCombo->currentText();
    Preferences::instance()->setTheme(text);
}

void PreferencesDialog::gridColorChanged(const QColor &gridColor)
{
    mGridColor = gridColor;
}

void PreferencesDialog::accept()
{
    QDialog::accept();

    Preferences *prefs = Preferences::instance();
    Tiled::TileMetaInfoMgr::instance()->changeTilesDirectory(mTilesDirectory);
    prefs->setUseOpenGL(ui->openGL->isChecked());
    prefs->setLoadAllWorldThumbnails(ui->thumbnails->isChecked());
    prefs->setGridColor(mGridColor);
    prefs->setGridWidth(ui->gridWidth->value());
    prefs->setThumbnailWidth(ui->thumbnailWidth->value());
    prefs->setTerrainImageMemoryLimitMiB(
                ui->terrainImageMemoryLimit->value());
    prefs->setRestoreLastSession(ui->restoreLastSession->isChecked());
    prefs->setRoadSimplificationHighway(mRoadSimplificationHighway->value());
    prefs->setRoadPointSpacingHighway(mRoadPointSpacingHighway->value());
    prefs->setRoadSimplificationTrail(mRoadSimplificationTrail->value());
    prefs->setRoadPointSpacingTrail(mRoadPointSpacingTrail->value());
    prefs->setRoadSimplificationRailway(mRoadSimplificationRailway->value());
    prefs->setRoadPointSpacingRailway(mRoadPointSpacingRailway->value());
    prefs->setShowAdjacentMaps(ui->showAdjacent->isChecked());
    prefs->setZombieSpawnImageOpacity(ui->zombieSpawnImageOpacity->value() / 100.0);
}
