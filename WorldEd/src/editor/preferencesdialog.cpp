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

#include "appissuenotifier.h"
#include "preferences.h"
#include "tilemetainfomgr.h"
#include "vehiclemeshpreview.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"
#include "../sharedmainwindowgeometrywidget.h"

#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(WorldDocument *worldDoc, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PreferencesDialog)
    , mWorldDoc(worldDoc)
    , mRoadSimplificationHighway(new QDoubleSpinBox(this))
    , mRoadPointSpacingHighway(new QSpinBox(this))
    , mRoadSimplificationTrail(new QDoubleSpinBox(this))
    , mRoadPointSpacingTrail(new QSpinBox(this))
    , mGenerateTrailFeatures(new QCheckBox(this))
    , mRoadSimplificationRailway(new QDoubleSpinBox(this))
    , mRoadPointSpacingRailway(new QSpinBox(this))
    , mSyncThemeCheckBox(new QCheckBox(
          tr("Apply to TileZed, BuildingEd and WorldEd"), this))
    , mErrorsOnlyNotifications(new QCheckBox(
          tr("Show only errors in application notifications"), this))
    , mAutoSaveCombo(new QComboBox(this))
    , mTreeFeatureTiles(new QListWidget(this))
    , mPrimaryRoadFeatureTiles(new QListWidget(this))
    , mSecondaryRoadFeatureTiles(new QListWidget(this))
    , mTertiaryRoadFeatureTiles(new QListWidget(this))
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

    mProjectZomboidDirectory = prefs->projectZomboidDirectory();
    ui->projectZomboidDirectory->setText(QDir::toNativeSeparators(
                                             mProjectZomboidDirectory));
    connect(ui->browseProjectZomboidDirectory,
            &QAbstractButton::clicked,
            this, &PreferencesDialog::browseProjectZomboidDirectory);
    connect(ui->clearProjectZomboidDirectory,
            &QAbstractButton::clicked, this, [this]() {
        mProjectZomboidDirectory.clear();
        ui->projectZomboidDirectory->clear();
        updateVehicleAtlasStatus();
    });

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
    ui->vehicleMeshPreviewScale->setValue(
                prefs->vehicleMeshPreviewScale());
    ui->vehicleMeshPreviewQuality->setValue(
                prefs->vehicleMeshPreviewQuality());
    connect(ui->vehicleMeshPreviewQuality,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
        updateVehicleAtlasStatus();
    });
    connect(ui->vehicleMeshPreviewScale,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
        updateVehicleAtlasStatus();
    });
    connect(ui->rebuildVehicleAtlas, &QAbstractButton::clicked,
            this, &PreferencesDialog::rebuildVehicleAtlas);
    updateVehicleAtlasStatus();

    QWidget *vehiclesTab = new QWidget(ui->tabWidget);
    QVBoxLayout *vehiclesLayout = new QVBoxLayout(vehiclesTab);
    ui->vehiclePreviewGroup->setParent(vehiclesTab);
    vehiclesLayout->addWidget(ui->vehiclePreviewGroup);
    vehiclesLayout->addStretch();
    ui->tabWidget->addTab(vehiclesTab, tr("Vehicles"));

    ui->restoreLastSession->setChecked(prefs->restoreLastSession());

    ui->openGL->setChecked(prefs->useOpenGL());
    ui->thumbnails->setChecked(prefs->loadAllWorldThumbnails());
    ui->showAdjacent->setChecked(prefs->showAdjacentMaps());
    ui->zombieSpawnImageOpacity->setValue(int(prefs->zombieSpawnImageOpacity() * 100));

    QScrollArea *featuresTab = new QScrollArea(ui->tabWidget);
    QWidget *featuresPage = new QWidget(featuresTab);
    QVBoxLayout *featuresLayout = new QVBoxLayout(featuresPage);
    featuresTab->setWidgetResizable(true);
    featuresTab->setFrameShape(QFrame::NoFrame);
    featuresTab->setWidget(featuresPage);
    QLabel *description = new QLabel(tr(
            "These settings control how generated road polygons are simplified. "
            "A higher tolerance produces fewer points. Maximum point spacing "
            "keeps long contours from becoming too coarse."), featuresPage);
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
    mGenerateTrailFeatures->setText(
                tr("Generate Trail features from dirt tiles"));
    mGenerateTrailFeatures->setToolTip(tr(
                "Disabled by default. Dirt tiles can also represent bare ground, "
                "riverbeds, fields, or construction areas, so this heuristic can "
                "create false trails and unusually large world-map features."));
    featuresLayout->addWidget(mGenerateTrailFeatures);
    addFeatureGroup(tr("Trails"),
                    mRoadSimplificationTrail, mRoadPointSpacingTrail);
    addFeatureGroup(tr("Railways"),
                    mRoadSimplificationRailway, mRoadPointSpacingRailway);

    QGroupBox *detectionGroup = new QGroupBox(
                tr("Tiles used for feature detection"), featuresPage);
    QVBoxLayout *detectionLayout = new QVBoxLayout(detectionGroup);
    QLabel *detectionDescription = new QLabel(tr(
            "Each list contains complete tile names, not tileset names. "
            "An empty list disables that detection when features are generated."),
            detectionGroup);
    detectionDescription->setWordWrap(true);
    detectionLayout->addWidget(detectionDescription);
    QTabWidget *detectionTabs = new QTabWidget(detectionGroup);
    detectionTabs->setMinimumHeight(280);
    detectionLayout->addWidget(detectionTabs);
    featuresLayout->addWidget(detectionGroup, 1);

    auto addTileDetectionPage = [this, detectionTabs](
            const QString &title, const QString &descriptionText,
            QListWidget *list, const QStringList &currentTiles,
            const QStringList &defaultTiles) {
        QWidget *page = new QWidget(detectionTabs);
        QVBoxLayout *layout = new QVBoxLayout(page);
        QLabel *descriptionLabel = new QLabel(descriptionText, page);
        descriptionLabel->setWordWrap(true);
        layout->addWidget(descriptionLabel);
        list->clear();
        list->addItems(currentTiles);
        list->setAlternatingRowColors(true);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        layout->addWidget(list, 1);
        QHBoxLayout *entryLayout = new QHBoxLayout;
        QLineEdit *entry = new QLineEdit(page);
        entry->setPlaceholderText(tr("Exact tile name, for example blends_street_01_32"));
        QPushButton *add = new QPushButton(tr("Add Tile"), page);
        entryLayout->addWidget(entry, 1);
        entryLayout->addWidget(add);
        layout->addLayout(entryLayout);
        QHBoxLayout *actionsLayout = new QHBoxLayout;
        QPushButton *remove = new QPushButton(tr("Remove Selected"), page);
        QPushButton *restore = new QPushButton(tr("Restore Defaults"), page);
        QLabel *status = new QLabel(page);
        actionsLayout->addWidget(remove);
        actionsLayout->addWidget(restore);
        actionsLayout->addStretch();
        actionsLayout->addWidget(status);
        layout->addLayout(actionsLayout);
        auto updateStatus = [list, status]() {
            if (list->count() == 0)
                status->setText(QObject::tr("Detection disabled"));
            else
                status->setText(QObject::tr("%1 tile(s)").arg(list->count()));
        };
        auto addEntry = [list, entry, updateStatus]() {
            const QString tileName = Preferences::canonicalFeatureTileName(
                        entry->text());
            if (tileName.isEmpty())
                return;
            const QList<QListWidgetItem*> matches = list->findItems(
                        tileName, Qt::MatchFixedString);
            if (matches.isEmpty()) {
                list->addItem(tileName);
                list->sortItems(Qt::AscendingOrder);
            } else {
                list->setCurrentItem(matches.first());
            }
            entry->clear();
            updateStatus();
        };
        connect(add, &QAbstractButton::clicked, this, addEntry);
        connect(entry, &QLineEdit::returnPressed, this, addEntry);
        connect(remove, &QAbstractButton::clicked, this,
                [list, updateStatus]() {
            qDeleteAll(list->selectedItems());
            updateStatus();
        });
        connect(restore, &QAbstractButton::clicked, this,
                [list, defaultTiles, updateStatus]() {
            list->clear();
            list->addItems(defaultTiles);
            updateStatus();
        });
        updateStatus();
        detectionTabs->addTab(page, title);
    };

    addTileDetectionPage(
                tr("Trees"),
                tr("Tiles that create natural=forest Tree Features."),
                mTreeFeatureTiles, prefs->treeFeatureTiles(),
                Preferences::defaultTreeFeatureTiles());
    addTileDetectionPage(
                tr("Primary Roads"),
                tr("Tiles that create highway=primary Road Features."),
                mPrimaryRoadFeatureTiles, prefs->primaryRoadFeatureTiles(),
                Preferences::defaultPrimaryRoadFeatureTiles());
    addTileDetectionPage(
                tr("Secondary Roads"),
                tr("Tiles that create highway=secondary Road Features."),
                mSecondaryRoadFeatureTiles, prefs->secondaryRoadFeatureTiles(),
                Preferences::defaultSecondaryRoadFeatureTiles());
    addTileDetectionPage(
                tr("Tertiary Roads"),
                tr("Tiles that create highway=tertiary Road Features."),
                mTertiaryRoadFeatureTiles, prefs->tertiaryRoadFeatureTiles(),
                Preferences::defaultTertiaryRoadFeatureTiles());

    mRoadSimplificationHighway->setValue(prefs->roadSimplificationHighway());
    mRoadPointSpacingHighway->setValue(prefs->roadPointSpacingHighway());
    mRoadSimplificationTrail->setValue(prefs->roadSimplificationTrail());
    mRoadPointSpacingTrail->setValue(prefs->roadPointSpacingTrail());
    mGenerateTrailFeatures->setChecked(prefs->generateTrailFeatures());
    mRoadSimplificationTrail->setEnabled(mGenerateTrailFeatures->isChecked());
    mRoadPointSpacingTrail->setEnabled(mGenerateTrailFeatures->isChecked());
    connect(mGenerateTrailFeatures, &QCheckBox::toggled,
            mRoadSimplificationTrail, &QWidget::setEnabled);
    connect(mGenerateTrailFeatures, &QCheckBox::toggled,
            mRoadPointSpacingTrail, &QWidget::setEnabled);
    mRoadSimplificationRailway->setValue(prefs->roadSimplificationRailway());
    mRoadPointSpacingRailway->setValue(prefs->roadPointSpacingRailway());

    QPushButton *defaults = new QPushButton(tr("Restore road-generation defaults"), featuresPage);
    connect(defaults, &QAbstractButton::clicked, this, [this]() {
        mRoadSimplificationHighway->setValue(2.0);
        mRoadPointSpacingHighway->setValue(40);
        mRoadSimplificationTrail->setValue(2.0);
        mRoadPointSpacingTrail->setValue(40);
        mGenerateTrailFeatures->setChecked(false);
        mRoadSimplificationRailway->setValue(2.0);
        mRoadPointSpacingRailway->setValue(40);
    });
    featuresLayout->addWidget(defaults);
    featuresLayout->addStretch();
    ui->tabWidget->addTab(featuresTab, tr("Feature Generation"));

    ui->tabWidget->addTab(
                new SharedMainWindowGeometryWidget(parent, ui->tabWidget),
                tr("Window Setup"));

    QWidget *savingTab = new QWidget(ui->tabWidget);
    QVBoxLayout *savingLayout = new QVBoxLayout(savingTab);
    QGroupBox *autoSaveGroup = new QGroupBox(tr("Automatic Save"), savingTab);
    QFormLayout *autoSaveLayout = new QFormLayout(autoSaveGroup);
    mAutoSaveCombo->addItem(tr("Disabled"), 0);
    for (int minutes : {1, 5, 10, 20, 60})
        mAutoSaveCombo->addItem(tr("Every %1 minute(s)").arg(minutes),
                                minutes);
    const int autoSaveIndex = mAutoSaveCombo->findData(
                prefs->autoSaveIntervalMinutes());
    mAutoSaveCombo->setCurrentIndex(autoSaveIndex >= 0 ? autoSaveIndex : 0);
    autoSaveLayout->addRow(tr("Save modified projects:"), mAutoSaveCombo);
    QLabel *autoSaveDescription = new QLabel(tr(
            "Only an existing project with a file path is saved. New "
            "untitled projects still require Save As."), autoSaveGroup);
    autoSaveDescription->setWordWrap(true);
    autoSaveLayout->addRow(autoSaveDescription);
    savingLayout->addWidget(autoSaveGroup);
    savingLayout->addStretch();
    ui->tabWidget->addTab(savingTab, tr("Saving"));

    QWidget *notificationsTab = new QWidget(ui->tabWidget);
    QVBoxLayout *notificationsLayout = new QVBoxLayout(notificationsTab);
    QGroupBox *notificationsGroup = new QGroupBox(
                tr("Application Notifications"), notificationsTab);
    QVBoxLayout *notificationsGroupLayout = new QVBoxLayout(
                notificationsGroup);
    mErrorsOnlyNotifications->setChecked(
                AppIssueCenter::errorsOnlyNotifications());
    notificationsGroupLayout->addWidget(mErrorsOnlyNotifications);
    QLabel *notificationsDescription = new QLabel(tr(
            "Warnings remain available in the log files. Warning dialogs "
            "that require a response are not hidden."), notificationsGroup);
    notificationsDescription->setWordWrap(true);
    notificationsGroupLayout->addWidget(notificationsDescription);
    notificationsLayout->addWidget(notificationsGroup);
    notificationsLayout->addStretch();
    ui->tabWidget->addTab(notificationsTab, tr("Notifications"));

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

void PreferencesDialog::browseProjectZomboidDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Project Zomboid Installation"),
                ui->projectZomboidDirectory->text());
    if (directory.isEmpty())
        return;
    const QString normalized =
            PortableSettings::normalizedGamePath(directory);
    if (normalized.isEmpty()) {
        QMessageBox::warning(
                    this, tr("Invalid Project Zomboid Installation"),
                    tr("Choose the Project Zomboid installation root or its "
                       "media directory. The selected location must contain "
                       "recognizable game data such as Lua, TileDefs, or "
                       "texture packs."));
        return;
    }
    mProjectZomboidDirectory = normalized;
    ui->projectZomboidDirectory->setText(
                QDir::toNativeSeparators(normalized));
    updateVehicleAtlasStatus();
}

void PreferencesDialog::updateVehicleAtlasStatus()
{
    const bool available = !mProjectZomboidDirectory.isEmpty();
    ui->rebuildVehicleAtlas->setEnabled(available);
    if (!available) {
        ui->vehicleAtlasStatus->setText(
                    tr("Configure the Project Zomboid installation first"));
        return;
    }
    ui->vehicleAtlasStatus->setText(
                VehicleMeshPreview::instance()->atlasStatus(
                    mProjectZomboidDirectory,
                    qMax(ui->vehicleMeshPreviewQuality->value(),
                         ui->vehicleMeshPreviewScale->value())));
}

void PreferencesDialog::rebuildVehicleAtlas()
{
    if (mProjectZomboidDirectory.isEmpty())
        return;
    const qreal atlasQuality = qMax(
                ui->vehicleMeshPreviewQuality->value(),
                ui->vehicleMeshPreviewScale->value());
    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Rebuild Vehicle Preview Atlas"),
                tr("The cached atlas for %1x quality will be replaced. "
                   "WorldEd will render every configured vehicle in sixteen "
                   "directions. This can take several minutes and "
                   "use significant memory and disk space. Continue?")
                .arg(atlasQuality, 0, 'f', 2),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QProgressDialog progress(
                tr("Preparing vehicle atlas..."), tr("Cancel"),
                0, 1, this);
    progress.setWindowTitle(tr("Vehicle Preview Atlas"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QString summary;
    QString error;
    const bool rebuilt = VehicleMeshPreview::instance()->rebuildAtlas(
                mProjectZomboidDirectory,
                atlasQuality,
                [&progress](int current, int total,
                const QString &vehicleName) {
        progress.setMaximum(qMax(1, total));
        progress.setValue(current);
        progress.setLabelText(QObject::tr("Rendering %1\n%2 of %3")
                              .arg(vehicleName)
                              .arg(current + 1)
                              .arg(total));
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    }, &summary, &error);
    progress.setValue(progress.maximum());
    updateVehicleAtlasStatus();
    if (!rebuilt) {
        QMessageBox::warning(this, tr("Vehicle Atlas Failed"), error);
        return;
    }
    Preferences::instance()->notifyVehicleMeshPreviewAtlasChanged();
    QMessageBox::information(this, tr("Vehicle Atlas"), summary);
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
    Preferences *prefs = Preferences::instance();
    Tiled::TileMetaInfoMgr::instance()->changeTilesDirectory(mTilesDirectory);
    prefs->setProjectZomboidDirectory(mProjectZomboidDirectory);
    prefs->setUseOpenGL(ui->openGL->isChecked());
    prefs->setLoadAllWorldThumbnails(ui->thumbnails->isChecked());
    prefs->setGridColor(mGridColor);
    prefs->setGridWidth(ui->gridWidth->value());
    prefs->setThumbnailWidth(ui->thumbnailWidth->value());
    prefs->setTerrainImageMemoryLimitMiB(
                ui->terrainImageMemoryLimit->value());
    prefs->setVehicleMeshPreviewScale(
                ui->vehicleMeshPreviewScale->value());
    prefs->setVehicleMeshPreviewQuality(
                ui->vehicleMeshPreviewQuality->value());
    prefs->setRestoreLastSession(ui->restoreLastSession->isChecked());
    prefs->setAutoSaveIntervalMinutes(
                mAutoSaveCombo->currentData().toInt());
    prefs->setRoadSimplificationHighway(mRoadSimplificationHighway->value());
    prefs->setRoadPointSpacingHighway(mRoadPointSpacingHighway->value());
    prefs->setRoadSimplificationTrail(mRoadSimplificationTrail->value());
    prefs->setRoadPointSpacingTrail(mRoadPointSpacingTrail->value());
    prefs->setGenerateTrailFeatures(mGenerateTrailFeatures->isChecked());
    prefs->setRoadSimplificationRailway(mRoadSimplificationRailway->value());
    prefs->setRoadPointSpacingRailway(mRoadPointSpacingRailway->value());
    prefs->setTreeFeatureTiles(tileNames(mTreeFeatureTiles));
    prefs->setPrimaryRoadFeatureTiles(tileNames(mPrimaryRoadFeatureTiles));
    prefs->setSecondaryRoadFeatureTiles(tileNames(mSecondaryRoadFeatureTiles));
    prefs->setTertiaryRoadFeatureTiles(tileNames(mTertiaryRoadFeatureTiles));
    prefs->setShowAdjacentMaps(ui->showAdjacent->isChecked());
    prefs->setZombieSpawnImageOpacity(ui->zombieSpawnImageOpacity->value() / 100.0);
    AppIssueCenter::setErrorsOnlyNotifications(
                mErrorsOnlyNotifications->isChecked());
    QDialog::accept();
}

QStringList PreferencesDialog::tileNames(QListWidget *list) const
{
    QStringList names;
    for (int row = 0; row < list->count(); ++row)
        names.append(list->item(row)->text());
    return names;
}
