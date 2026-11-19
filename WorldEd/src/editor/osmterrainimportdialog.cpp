#include "osmterrainimportdialog.h"

#include "../portablesettings.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"
#include "terrainimageeditordialog.h"

#include <QtConcurrentRun>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSslSocket>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace {

QString compactLocaleNumber(double value, int decimals = 3)
{
    const QLocale locale;
    QString text = locale.toString(value, 'f', decimals);
    const QChar zero = locale.zeroDigit();
    const QChar decimalPoint = locale.decimalPoint();
    while (text.contains(decimalPoint) && text.endsWith(zero))
        text.chop(1);
    if (text.endsWith(decimalPoint))
        text.chop(1);
    return text;
}

class CompactDoubleSpinBox : public QDoubleSpinBox
{
public:
    explicit CompactDoubleSpinBox(QWidget *parent = nullptr)
        : QDoubleSpinBox(parent)
    {
    }

protected:
    QString textFromValue(double value) const override
    {
        return compactLocaleNumber(value, decimals());
    }
};

QString formatCoordinate(double value)
{
    return QString::number(value, 'f', 6);
}

QString formatDistance(double meters)
{
    if (meters >= 1000.0)
        return QStringLiteral("%1 km").arg(meters / 1000.0, 0, 'f', 2);
    return QStringLiteral("%1 m").arg(meters, 0, 'f', 0);
}

int retryAfterMilliseconds(const QByteArray &header)
{
    bool secondsOk = false;
    const int seconds = header.trimmed().toInt(&secondsOk);
    if (secondsOk)
        return qBound(15000, seconds * 1000, 120000);
    const QDateTime retryAt = QDateTime::fromString(
                QString::fromLatin1(header), Qt::RFC2822Date);
    if (retryAt.isValid()) {
        return qBound(
                    15000,
                    int(QDateTime::currentDateTimeUtc().msecsTo(
                            retryAt.toUTC())),
                    120000);
    }
    return 0;
}

}

OsmTerrainImportDialog::OsmTerrainImportDialog(
        WorldDocument *worldDocument, QWidget *parent)
    : QDialog(parent)
    , mWorldDocument(worldDocument)
    , mLocationInput(new QLineEdit(this))
    , mLatitude(new QDoubleSpinBox(this))
    , mLongitude(new QDoubleSpinBox(this))
    , mOriginX(new QSpinBox(this))
    , mOriginY(new QSpinBox(this))
    , mCellsWide(new QSpinBox(this))
    , mCellsHigh(new QSpinBox(this))
    , mAreaWidthKm(new QDoubleSpinBox(this))
    , mAreaHeightKm(new QDoubleSpinBox(this))
    , mGridFormat(new QComboBox(this))
    , mMetersPerSquare(new CompactDoubleSpinBox(this))
    , mScalePreset(new QComboBox(this))
    , mRotation(new CompactDoubleSpinBox(this))
    , mMarginPercent(new QSpinBox(this))
    , mRoadWidthPercent(new QSpinBox(this))
    , mLand(new QCheckBox(tr("Land cover and vegetation"), this))
    , mWater(new QCheckBox(tr("Water polygons and waterways"), this))
    , mRoads(new QCheckBox(tr("Roads, paths and railways"), this))
    , mGenerateStreets(new QCheckBox(
                           tr("Generate streets.xml from named roads"), this))
    , mGenerateInGame(new QCheckBox(
                          tr("Create InGameMap features"), this))
    , mGenerateProxyBuildings(new QCheckBox(
                                  tr("Create simple TBX proxy buildings"), this))
    , mGenerateRoadMarkings(new QCheckBox(
                                tr("Create road markings"), this))
    , mGenerateNavZones(new QCheckBox(
                            tr("Create major-road Nav meshes"), this))
    , mGenerateForagingZones(new QCheckBox(
                                 tr("Create typed ground zones from land use"), this))
    , mUseCache(new QCheckBox(tr("Reuse cached OSM response"), this))
    , mClearCacheButton(new QPushButton(tr("Clear Cache"), this))
    , mResetParametersButton(new QPushButton(tr("Reset Parameters"), this))
    , mEndpoint(new QLineEdit(this))
    , mGeocoderEndpoint(new QLineEdit(this))
    , mProjectPath(new QLineEdit(this))
    , mOutputPath(new QLineEdit(this))
    , mGeometrySummary(new QLabel(this))
    , mProjectFormat(new QLabel(this))
    , mScaleExplanation(new QLabel(this))
    , mPreview(new QLabel(this))
    , mPreviewScroll(new QScrollArea(this))
    , mPreviewZoom(new QSpinBox(this))
    , mStatus(new QLabel(this))
    , mProgress(new QProgressBar(this))
    , mGenerateButton(new QPushButton(tr("Download and Generate"), this))
    , mCancelButton(new QPushButton(tr("Cancel Request"), this))
    , mOpenButton(new QPushButton(tr("Open in Terrain Editor"), this))
    , mApplyLocationButton(new QPushButton(tr("Use Location"), this))
    , mOpenMapButton(new QPushButton(tr("Find on OpenStreetMap"), this))
    , mDetectOrientationButton(new QPushButton(
                                   tr("Detect Road Grid"), this))
    , mNetwork(new QNetworkAccessManager(this))
    , mReply(nullptr)
    , mLocationReply(nullptr)
    , mLocationRequestKind(NoLocationRequest)
    , mTimeout(new QTimer(this))
    , mRetryTimer(new QTimer(this))
    , mWatcher(new QFutureWatcher<OsmTerrainGenerationTaskResult>(this))
    , mActiveQueryIndex(-1)
    , mOverpassEndpointIndex(-1)
    , mRateLimitRetryCount(0)
    , mTransientRetryCount(0)
    , mOrientationOnlyRequest(false)
    , mAllResponsesFromCache(true)
    , mRetryCurrentEndpoint(false)
    , mCancelRequested(false)
    , mRequestTimedOut(false)
{
    setWindowTitle(mWorldDocument
                   ? tr("Import OpenStreetMap Terrain")
                   : tr("Create Project from OpenStreetMap"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    const QRect availableScreen = QApplication::primaryScreen()
            ? QApplication::primaryScreen()->availableGeometry()
            : QRect(0, 0, 1920, 1080);
    resize(qMin(1680, qMax(960, availableScreen.width() - 60)),
           qMin(980, qMax(640, availableScreen.height() - 40)));

    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    const bool creatingProject = world == nullptr;
    const int worldWidth = world ? world->width() : 10000;
    const int worldHeight = world ? world->height() : 10000;
    int originX = 0;
    int originY = 0;
    int cellsWide = qMin(2, worldWidth);
    int cellsHigh = qMin(2, worldHeight);
    const QRect selection = selectedCellArea();
    if (selection.isValid()) {
        originX = selection.x();
        originY = selection.y();
        cellsWide = selection.width();
        cellsHigh = selection.height();
    }

    QSettings settings;
    mLocationInput->setPlaceholderText(
                tr("Paste coordinates or an OpenStreetMap / Google Maps link"));
    mLocationInput->setClearButtonEnabled(true);
    mLocationInput->setToolTip(
                tr("Examples: 47.8032879, -3.7209987 or a full map URL."));
    mLatitude->setDecimals(7);
    mLatitude->setRange(-85.0, 85.0);
    mLatitude->setValue(settings.value(
                            QLatin1String("OSMImporter/Latitude"),
                            0.0).toDouble());
    mLongitude->setDecimals(7);
    mLongitude->setRange(-180.0, 180.0);
    mLongitude->setValue(settings.value(
                             QLatin1String("OSMImporter/Longitude"),
                             0.0).toDouble());
    mOriginX->setRange(0, qMax(0, worldWidth - 1));
    mOriginY->setRange(0, qMax(0, worldHeight - 1));
    mOriginX->setValue(originX);
    mOriginY->setValue(originY);
    mCellsWide->setRange(1, qMax(1, worldWidth));
    mCellsHigh->setRange(1, qMax(1, worldHeight));
    mCellsWide->setValue(cellsWide);
    mCellsHigh->setValue(cellsHigh);
    if (creatingProject) {
        mCellsWide->setToolTip(
                    tr("Edit the project width in cells. The real-world width "
                       "is updated to match the complete-cell coverage."));
        mCellsHigh->setToolTip(
                    tr("Edit the project height in cells. The real-world "
                       "height is updated to match the complete-cell coverage."));
    }
    mGridFormat->addItem(
                tr("300 x 300 squares per cell (Legacy)"),
                int(WorldGridFormat::Legacy300));
    mGridFormat->addItem(
                tr("256 x 256 squares per cell (Native)"),
                int(WorldGridFormat::Native256));
    mGridFormat->setCurrentIndex(settings.value(
                                     QLatin1String(
                                         "OSMImporter/NewProjectGridFormat"),
                                     1).toInt() == 0 ? 0 : 1);
    mAreaWidthKm->setDecimals(4);
    mAreaHeightKm->setDecimals(4);
    mAreaWidthKm->setRange(0.001, 40000.0);
    mAreaHeightKm->setRange(0.001, 40000.0);
    mAreaWidthKm->setSingleStep(0.25);
    mAreaHeightKm->setSingleStep(0.25);
    mAreaWidthKm->setSuffix(tr(" km"));
    mAreaHeightKm->setSuffix(tr(" km"));
    mAreaWidthKm->setValue(settings.value(
                               QLatin1String("OSMImporter/NewProjectWidthKm"),
                               2.0).toDouble());
    mAreaHeightKm->setValue(settings.value(
                                QLatin1String("OSMImporter/NewProjectHeightKm"),
                                2.0).toDouble());
    mMetersPerSquare->setDecimals(3);
    mMetersPerSquare->setRange(0.1, 100.0);
    mMetersPerSquare->setSingleStep(0.25);
    mMetersPerSquare->setSuffix(
                tr(" m = 1 pixel = 1 map square"));
    mMetersPerSquare->setValue(settings.value(
                                  QLatin1String("OSMImporter/MetersPerSquare"),
                                  1.0).toDouble());
    mScalePreset->addItem(tr("Very detailed: 0.5 m = 1 pixel"), 0.5);
    mScalePreset->addItem(tr("1:1 reference: 1 m = 1 pixel"), 1.0);
    mScalePreset->addItem(tr("Neighborhood: 2 m = 1 pixel"), 2.0);
    mScalePreset->addItem(tr("Regional: 5 m = 1 pixel"), 5.0);
    mScalePreset->addItem(tr("Overview: 10 m = 1 pixel"), 10.0);
    mScalePreset->addItem(tr("Custom scale"));
    int scalePresetIndex = mScalePreset->count() - 1;
    for (int index = 0; index < mScalePreset->count() - 1; ++index) {
        if (qAbs(mScalePreset->itemData(index).toDouble()
                 - mMetersPerSquare->value()) < 0.0001) {
            scalePresetIndex = index;
            break;
        }
    }
    mScalePreset->setCurrentIndex(scalePresetIndex);
    mRotation->setDecimals(1);
    mRotation->setRange(-45.0, 45.0);
    mRotation->setSingleStep(1.0);
    mRotation->setSuffix(QString::fromUtf8("°"));
    mRotation->setValue(settings.value(
                            QLatin1String("OSMImporter/RotationDegreesClockwise"),
                            0.0).toDouble());
    mRotation->setToolTip(
                tr("Rotate the generated map around its center. Positive "
                   "values rotate clockwise and negative values rotate "
                   "counter-clockwise. The useful range repeats every 90 "
                   "degrees for an orthogonal road grid."));
    mDetectOrientationButton->setToolTip(
                tr("Analyze a lightweight OpenStreetMap road request and "
                   "suggest the rotation that aligns the dominant orthogonal "
                   "street grid with WorldEd X and Y axes."));
    mMarginPercent->setRange(0, 200);
    mMarginPercent->setSuffix(tr(" %"));
    mMarginPercent->setValue(settings.value(
                                 QLatin1String("OSMImporter/MarginPercent"),
                                 20).toInt());
    mRoadWidthPercent->setRange(10, 500);
    mRoadWidthPercent->setSuffix(tr(" %"));
    mRoadWidthPercent->setValue(settings.value(
                                    QLatin1String(
                                        "OSMImporter/RoadWidthPercent"),
                                    100).toInt());
    mLand->setChecked(true);
    mWater->setChecked(true);
    mRoads->setChecked(true);
    mGenerateStreets->setChecked(settings.value(
                QLatin1String("OSMImporter/GenerateStreets"),
                creatingProject).toBool());
    mGenerateInGame->setChecked(settings.value(
                QLatin1String("OSMImporter/GenerateInGameMap"),
                creatingProject).toBool());
    mGenerateProxyBuildings->setChecked(settings.value(
                QLatin1String("OSMImporter/GenerateProxyBuildings"),
                creatingProject).toBool());
    mGenerateRoadMarkings->setChecked(settings.value(
                QLatin1String("OSMImporter/GenerateRoadMarkings"),
                creatingProject).toBool());
    mGenerateNavZones->setChecked(settings.value(
                QLatin1String("OSMImporter/GenerateNavZones"),
                creatingProject).toBool());
    mGenerateForagingZones->setChecked(settings.value(
                QLatin1String("OSMImporter/GenerateForagingZones"),
                creatingProject).toBool());
    mUseCache->setChecked(true);
    mEndpoint->setText(settings.value(
                           QLatin1String("OSMImporter/Endpoint"),
                           QStringLiteral(
                               "https://overpass-api.de/api/interpreter"))
                       .toString());
    mGeocoderEndpoint->setText(settings.value(
                           QLatin1String("OSMImporter/GeocoderEndpoint"),
                           QStringLiteral(
                               "https://nominatim.openstreetmap.org/search"))
                       .toString());

    const int projectCellSize = world ? world->cellSize()
                                      : currentCellSize();
    mProjectFormat->setText(creatingProject
                ? tr("No project is open. WorldEd will create a new PZW "
                     "from the selected geographic area.")
                : (projectCellSize == 256
                   ? tr("Project format: Native 256 x 256 squares per cell")
                   : tr("Project format: Legacy 300 x 300 squares per cell")));
    mProjectFormat->setWordWrap(true);
    mProjectFormat->setToolTip(creatingProject
                ? tr("The calculated cell grid becomes the size of the new "
                     "WorldEd project.")
                : tr("The importer follows the loaded WorldEd project. The "
                     "cell format is not converted during import."));
    mScaleExplanation->setWordWrap(true);
    mScaleExplanation->setText(
                tr("WorldEd always writes one image pixel for one map square. "
                   "At the 1:1 preset, one real-world meter becomes one pixel "
                   "and one map square. This output scale is chosen here and "
                   "does not come from the map link. Use Detect Road Grid for "
                   "cities whose streets are not aligned with true North."));

    QString projectRoot = PortableSettings::installRootPath();
    if (mWorldDocument && !mWorldDocument->fileName().isEmpty())
        projectRoot = QFileInfo(mWorldDocument->fileName()).absolutePath();
    QString defaultProjectPath = settings.value(
                QLatin1String("OSMImporter/NewProjectPath"),
                QDir(projectRoot).filePath(
                    QStringLiteral("OSM_Project.pzw"))).toString();
    mProjectPath->setText(QDir::toNativeSeparators(defaultProjectPath));
    mOutputPath->setText(QDir::toNativeSeparators(
                             QDir(creatingProject
                                  ? QFileInfo(defaultProjectPath).absolutePath()
                                  : projectRoot).filePath(
                                 QStringLiteral("map/OSM_Map.png"))));

    mLocationInput->setPlaceholderText(
                tr("Place name, coordinates, OpenStreetMap or Google Maps link"));
    mApplyLocationButton->setText(tr("Find / Use Location"));
    if (creatingProject)
        mOpenButton->setText(tr("Create Project and Open Terrain Editor"));

    QHBoxLayout *locationInputLayout = new QHBoxLayout;
    locationInputLayout->addWidget(mLocationInput, 1);
    locationInputLayout->addWidget(mApplyLocationButton);
    locationInputLayout->addWidget(mOpenMapButton);
    QLabel *locationHelp = new QLabel(
                tr("Enter a place such as New York, paste coordinates, or "
                   "paste a map link. On OpenStreetMap, right-click the "
                   "target and choose Show address to copy the exact point. "
                   "Full Google Maps URLs containing coordinates also work. "
                   "Short Google share links are resolved automatically. "
                   "Google links supply only the center coordinates. All "
                   "imported map geometry still comes from OpenStreetMap."),
                this);
    locationHelp->setWordWrap(true);

    QPushButton *selectionButton = new QPushButton(
                tr("Use Selected Cells"), this);
    QPushButton *wholeProjectButton = new QPushButton(
                tr("Use Entire Project"), this);
    selectionButton->setEnabled(selection.isValid());
    QWidget *areaButtonsWidget = new QWidget(this);
    QHBoxLayout *areaButtons = new QHBoxLayout(areaButtonsWidget);
    areaButtons->setContentsMargins(0, 0, 0, 0);
    areaButtons->addWidget(selectionButton);
    areaButtons->addWidget(wholeProjectButton);
    areaButtons->addStretch(1);
    QLabel *areaHelp = new QLabel(creatingProject
                ? tr("Choose the real-world area first. WorldEd rounds it up "
                     "to complete cells and displays the resulting grid in "
                     "the preview.")
                : tr("One generated PNG can cover any rectangular group of "
                     "project cells. For example, a 10 x 15 Legacy project "
                     "produces a 3000 x 4500 pixel image."), this);
    areaHelp->setWordWrap(true);

    QHBoxLayout *scaleLayout = new QHBoxLayout;
    scaleLayout->addWidget(mScalePreset, 1);
    scaleLayout->addWidget(mMetersPerSquare);
    QHBoxLayout *orientationLayout = new QHBoxLayout;
    orientationLayout->addWidget(mRotation);
    QPushButton *northUpButton = new QPushButton(tr("North Up"), this);
    northUpButton->setToolTip(tr("Reset the map rotation to 0 degrees."));
    orientationLayout->addWidget(northUpButton);
    orientationLayout->addWidget(mDetectOrientationButton);
    orientationLayout->addStretch(1);

    QFormLayout *locationForm = new QFormLayout;
    locationForm->addRow(tr("Location or map link:"), locationInputLayout);
    locationForm->addRow(QString(), locationHelp);
    QHBoxLayout *coordinatesLayout = new QHBoxLayout;
    coordinatesLayout->addWidget(new QLabel(tr("Latitude:"), this));
    coordinatesLayout->addWidget(mLatitude);
    coordinatesLayout->addSpacing(18);
    coordinatesLayout->addWidget(new QLabel(tr("Longitude:"), this));
    coordinatesLayout->addWidget(mLongitude);
    locationForm->addRow(tr("Center:"), coordinatesLayout);
    locationForm->addRow(QString(), mProjectFormat);
    QLabel *projectAreaLabel = new QLabel(tr("Project area:"), this);
    QLabel *originLabel = new QLabel(tr("Origin cell:"), this);
    QLabel *gridFormatLabel = new QLabel(tr("Cell format:"), this);
    QLabel *areaSizeLabel = new QLabel(tr("Selected area:"), this);
    QLabel *originXInlineLabel = new QLabel(tr("X:"), this);
    QLabel *originYInlineLabel = new QLabel(tr("Y:"), this);
    QLabel *areaWidthInlineLabel = new QLabel(tr("Width:"), this);
    QLabel *areaHeightInlineLabel = new QLabel(tr("Height:"), this);
    QHBoxLayout *originLayout = new QHBoxLayout;
    originLayout->addWidget(originXInlineLabel);
    originLayout->addWidget(mOriginX);
    originLayout->addSpacing(18);
    originLayout->addWidget(originYInlineLabel);
    originLayout->addWidget(mOriginY);
    QHBoxLayout *areaSizeLayout = new QHBoxLayout;
    areaSizeLayout->addWidget(areaWidthInlineLabel);
    areaSizeLayout->addWidget(mAreaWidthKm);
    areaSizeLayout->addSpacing(18);
    areaSizeLayout->addWidget(areaHeightInlineLabel);
    areaSizeLayout->addWidget(mAreaHeightKm);
    QHBoxLayout *cellCountLayout = new QHBoxLayout;
    cellCountLayout->addWidget(new QLabel(tr("Wide:"), this));
    cellCountLayout->addWidget(mCellsWide);
    cellCountLayout->addSpacing(18);
    cellCountLayout->addWidget(new QLabel(tr("High:"), this));
    cellCountLayout->addWidget(mCellsHigh);
    locationForm->addRow(projectAreaLabel, areaButtonsWidget);
    locationForm->addRow(originLabel, originLayout);
    locationForm->addRow(gridFormatLabel, mGridFormat);
    locationForm->addRow(areaSizeLabel, areaSizeLayout);
    locationForm->addRow(tr("Project cells:"), cellCountLayout);
    locationForm->addRow(QString(), areaHelp);
    locationForm->addRow(tr("Output scale:"), scaleLayout);
    locationForm->addRow(tr("Map orientation:"), orientationLayout);
    locationForm->addRow(QString(), mScaleExplanation);
    QHBoxLayout *percentLayout = new QHBoxLayout;
    percentLayout->addWidget(new QLabel(tr("Download margin:"), this));
    percentLayout->addWidget(mMarginPercent);
    percentLayout->addSpacing(18);
    percentLayout->addWidget(new QLabel(tr("Road width:"), this));
    percentLayout->addWidget(mRoadWidthPercent);
    percentLayout->addStretch(1);
    locationForm->addRow(tr("Adjustments:"), percentLayout);
    QGroupBox *locationGroup = new QGroupBox(
                tr("Geographic Area and Project Placement"), this);
    locationGroup->setLayout(locationForm);
    projectAreaLabel->setVisible(!creatingProject);
    areaButtonsWidget->setVisible(!creatingProject);
    originLabel->setVisible(!creatingProject);
    originXInlineLabel->setVisible(!creatingProject);
    originYInlineLabel->setVisible(!creatingProject);
    mOriginX->setVisible(!creatingProject);
    mOriginY->setVisible(!creatingProject);
    gridFormatLabel->setVisible(creatingProject);
    mGridFormat->setVisible(creatingProject);
    areaSizeLabel->setVisible(creatingProject);
    areaWidthInlineLabel->setVisible(creatingProject);
    areaHeightInlineLabel->setVisible(creatingProject);
    mAreaWidthKm->setVisible(creatingProject);
    mAreaHeightKm->setVisible(creatingProject);

    mGenerateStreets->setText(tr("streets.xml"));
    mGenerateInGame->setText(tr("InGameMap"));
    mGenerateProxyBuildings->setText(tr("Proxy TBX"));
    mGenerateRoadMarkings->setText(tr("Road markings"));
    mGenerateNavZones->setText(tr("Road Nav Mesh"));
    mGenerateForagingZones->setText(tr("Ground zones"));
    mGenerateStreets->setToolTip(tr("Generate streets.xml from named roads."));
    mGenerateInGame->setToolTip(tr("Create road, building, water, and forest InGameMap features."));
    mGenerateProxyBuildings->setToolTip(tr("Create editable brick-walled TBX volumes from OSM building footprints and heights, with a flat roof on the highest level. Safe City Mode keeps a distributed sample of up to 2,048 buildings instead of removing them all. Footprints may cross cell boundaries."));
    mGenerateRoadMarkings->setToolTip(tr("Create editable WorldEd road-line objects over reliable orthogonal two-way roads. The OSM terrain remains the road surface. One-way, divided, unpaved, narrow, and diagonal roads are left unmarked."));
    mGenerateNavZones->setToolTip(tr("Rasterize OSM motorway, trunk, primary, and secondary road surfaces to map squares, then merge them into non-overlapping rectangular Nav zones. Trails, paths, tracks, and land-use areas are ignored."));
    mGenerateForagingZones->setToolTip(tr("Create exclusive Water, TownZone, Farm, FarmLand, DeepForest, Forest, and Vegitation coverage from OSM land use. Roads, railways, and bare sand are removed from ground zones. TownZone remains under building footprints, while all other uncovered grass becomes Vegitation. Raster areas are clipped per cell and merged into compact polygon or rectangle objects."));
    mClearCacheButton->setObjectName(QStringLiteral("clearOsmCacheButton"));
    mClearCacheButton->setToolTip(
                tr("Delete cached Overpass responses and cached OpenStreetMap place searches."));
    mResetParametersButton->setObjectName(
                QStringLiteral("resetOsmParametersButton"));
    mResetParametersButton->setToolTip(
                tr("Restore the OpenStreetMap importer defaults, including a 0,0 center. Cached responses are not deleted."));
    QGridLayout *layersLayout = new QGridLayout;
    layersLayout->addWidget(mLand, 0, 0);
    layersLayout->addWidget(mWater, 0, 1);
    layersLayout->addWidget(mRoads, 0, 2);
    layersLayout->addWidget(mUseCache, 0, 3);
    layersLayout->addWidget(mClearCacheButton, 0, 4);
    layersLayout->addWidget(mGenerateStreets, 1, 0);
    layersLayout->addWidget(mGenerateInGame, 1, 1);
    layersLayout->addWidget(mGenerateProxyBuildings, 1, 2);
    layersLayout->addWidget(mGenerateRoadMarkings, 2, 0);
    layersLayout->addWidget(mGenerateNavZones, 2, 1);
    layersLayout->addWidget(mGenerateForagingZones, 2, 2);
    QGroupBox *layersGroup = new QGroupBox(
                tr("OSM Layers and Generated Project Data"), this);
    layersGroup->setToolTip(
                tr("Generated PZW objects use the OSM Generated group. "
                   "Reimport replaces only OSM-generated data in the same "
                   "cell area. Proxy TBX files are editable rectangular "
                   "cubes based on building footprints and building:levels "
                   "when available. Large city imports offer Safe City Mode, "
                   "which retains a stable distributed sample of up to 2,048 "
                   "editable buildings and compacts generated zones."));
    layersGroup->setLayout(layersLayout);

    QToolButton *browseButton = new QToolButton(this);
    browseButton->setText(QStringLiteral("..."));
    browseButton->setToolTip(tr("Choose the suggested ground PNG"));
    QHBoxLayout *outputLayout = new QHBoxLayout;
    outputLayout->addWidget(mOutputPath, 1);
    outputLayout->addWidget(browseButton);

    QToolButton *browseProjectButton = new QToolButton(this);
    browseProjectButton->setText(QStringLiteral("..."));
    browseProjectButton->setToolTip(tr("Choose the new WorldEd project file"));
    QHBoxLayout *projectPathLayout = new QHBoxLayout;
    projectPathLayout->addWidget(mProjectPath, 1);
    projectPathLayout->addWidget(browseProjectButton);

    QFormLayout *sourceForm = new QFormLayout;
    sourceForm->addRow(tr("Overpass endpoint:"), mEndpoint);
    sourceForm->addRow(tr("Place-search endpoint:"), mGeocoderEndpoint);
    QLabel *projectPathLabel = new QLabel(tr("New project PZW:"), this);
    sourceForm->addRow(projectPathLabel, projectPathLayout);
    sourceForm->addRow(tr("Suggested ground PNG:"), outputLayout);
    QGroupBox *sourceGroup = new QGroupBox(tr("Source and Output"), this);
    sourceGroup->setLayout(sourceForm);
    projectPathLabel->setVisible(creatingProject);
    mProjectPath->setVisible(creatingProject);
    browseProjectButton->setVisible(creatingProject);

    mGeometrySummary->setWordWrap(true);
    mGeometrySummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mPreview->setAlignment(Qt::AlignCenter);
    mPreview->setFrameShape(QFrame::StyledPanel);
    mPreview->setText(tr("Choose an area to preview its project grid."));
    mPreviewScroll->setWidget(mPreview);
    mPreviewScroll->setWidgetResizable(false);
    mPreviewScroll->setAlignment(Qt::AlignCenter);
    mPreviewScroll->setMinimumSize(520, 420);
    mPreviewZoom->setRange(25, 400);
    mPreviewZoom->setSingleStep(25);
    mPreviewZoom->setSuffix(tr(" %"));
    mPreviewZoom->setValue(settings.value(
                               QLatin1String("OSMImporter/PreviewZoom"),
                               100).toInt());
    QPushButton *previewZoomOut = new QPushButton(tr("Zoom -"), this);
    QPushButton *previewZoomReset = new QPushButton(tr("100%"), this);
    QPushButton *previewZoomIn = new QPushButton(tr("Zoom +"), this);
    QHBoxLayout *previewTools = new QHBoxLayout;
    previewTools->addWidget(new QLabel(tr("Image zoom:"), this));
    previewTools->addWidget(previewZoomOut);
    previewTools->addWidget(previewZoomReset);
    previewTools->addWidget(previewZoomIn);
    previewTools->addWidget(mPreviewZoom);
    previewTools->addStretch(1);
    QVBoxLayout *previewLayout = new QVBoxLayout;
    previewLayout->addLayout(previewTools);
    previewLayout->addWidget(mPreviewScroll, 1);
    mStatus->setWordWrap(true);
    mProgress->setRange(0, 100);
    mProgress->setValue(0);

    QLabel *attribution = new QLabel(
                tr("Data %1 OpenStreetMap contributors, available under the "
                   "Open Database License. Place searches are sent only when "
                   "you press Find and are cached locally. WorldEd stores "
                   "source attribution beside the generated PNG files.")
                .arg(QChar(0x00a9)),
                this);
    attribution->setWordWrap(true);
    attribution->setOpenExternalLinks(true);
    QLabel *workflowBackground = new QLabel(
                tr("Workflow background: the broader map-style import "
                   "concept existed by at least 2023 using the "
                   "<a href=\"https://mapstyle.withgoogle.com/\">Google Maps "
                   "Styling Wizard</a> and custom JSON. "
                   "<a href=\"https://github.com/SadPeanut/Pz-RealLifeMap\">"
                   "SadPeanut / Pz-RealLifeMap</a> is credited for the idea "
                   "of using OpenStreetMap instead of Google. The idea has "
                   "continued to evolve in this native implementation."),
                this);
    workflowBackground->setWordWrap(true);
    workflowBackground->setTextFormat(Qt::RichText);
    workflowBackground->setOpenExternalLinks(true);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(mGenerateButton);
    buttons->addWidget(mCancelButton);
    buttons->addWidget(mResetParametersButton);
    buttons->addStretch(1);
    buttons->addWidget(mOpenButton);
    QPushButton *closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(closeButton);

    QVBoxLayout *left = new QVBoxLayout;
    left->setSpacing(4);
    left->addWidget(locationGroup);
    left->addWidget(layersGroup);
    left->addWidget(sourceGroup);
    left->addWidget(mGeometrySummary);
    left->addWidget(attribution);
    left->addWidget(workflowBackground);
    left->addStretch(1);
    QWidget *leftWidget = new QWidget(this);
    leftWidget->setLayout(left);
    QScrollArea *leftScroll = new QScrollArea(this);
    leftScroll->setObjectName(QStringLiteral("osmSettingsScroll"));
    leftScroll->setWidget(leftWidget);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setMinimumWidth(780);
    leftScroll->setMaximumWidth(900);
    QHBoxLayout *content = new QHBoxLayout;
    content->addWidget(leftScroll, 0);
    content->addLayout(previewLayout, 1);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(content, 1);
    mainLayout->addWidget(mStatus);
    mainLayout->addWidget(mProgress);
    mainLayout->addLayout(buttons);

    mCancelButton->setEnabled(false);
    mOpenButton->setEnabled(false);
    mTimeout->setSingleShot(true);
    mTimeout->setInterval(180000);
    mRetryTimer->setSingleShot(true);

    connect(browseProjectButton, &QToolButton::clicked,
            this, &OsmTerrainImportDialog::browseProject);
    connect(browseButton, &QToolButton::clicked,
            this, &OsmTerrainImportDialog::browseOutput);
    connect(mApplyLocationButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::applyLocationInput);
    connect(mLocationInput, &QLineEdit::returnPressed,
            this, &OsmTerrainImportDialog::applyLocationInput);
    connect(mOpenMapButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::openLocationMap);
    connect(selectionButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::useSelectedCells);
    connect(wholeProjectButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::useEntireProject);
    connect(mScalePreset,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OsmTerrainImportDialog::scalePresetChanged);
    connect(northUpButton, &QPushButton::clicked,
            this, [this]() { mRotation->setValue(0.0); });
    connect(mDetectOrientationButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::detectRoadOrientation);
    connect(mGridFormat,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
        updateStandaloneProjectSize();
        updateGeometrySummary();
    });
    connect(mMetersPerSquare,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        int matchingIndex = mScalePreset->count() - 1;
        for (int index = 0; index < mScalePreset->count() - 1; ++index) {
            if (qAbs(mScalePreset->itemData(index).toDouble() - value)
                    < 0.0001) {
                matchingIndex = index;
                break;
            }
        }
        if (mScalePreset->currentIndex() != matchingIndex) {
            const QSignalBlocker blocker(mScalePreset);
            mScalePreset->setCurrentIndex(matchingIndex);
        }
        updateStandaloneProjectSize();
    });
    connect(mGenerateButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::startImport);
    connect(mClearCacheButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::clearOsmCache);
    connect(mResetParametersButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::resetParameters);
    connect(mCancelButton, &QPushButton::clicked,
            this, &OsmTerrainImportDialog::cancelImport);
    connect(mOpenButton, &QPushButton::clicked,
            this, &QDialog::accept);
    connect(mPreviewZoom,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OsmTerrainImportDialog::previewZoomChanged);
    connect(previewZoomOut, &QPushButton::clicked,
            this, [this]() {
        mPreviewZoom->setValue(mPreviewZoom->value() - 25);
    });
    connect(previewZoomReset, &QPushButton::clicked,
            this, [this]() { mPreviewZoom->setValue(100); });
    connect(previewZoomIn, &QPushButton::clicked,
            this, [this]() {
        mPreviewZoom->setValue(mPreviewZoom->value() + 25);
    });
    connect(closeButton, &QPushButton::clicked,
            this, &QDialog::reject);
    connect(mTimeout, &QTimer::timeout,
            this, &OsmTerrainImportDialog::networkTimeout);
    connect(mRetryTimer, &QTimer::timeout,
            this, &OsmTerrainImportDialog::startOverpassRequest);
    connect(mWatcher, &QFutureWatcher<OsmTerrainGenerationTaskResult>::finished,
            this, &OsmTerrainImportDialog::generationFinished);
    connect(mAreaWidthKm,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this]() { updateStandaloneProjectSize(); });
    connect(mAreaHeightKm,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this]() { updateStandaloneProjectSize(); });
    connect(mCellsWide, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]() { updateStandaloneAreaFromCells(); });
    connect(mCellsHigh, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]() { updateStandaloneAreaFromCells(); });

    const QList<QObject *> geometryControls = {
        mLatitude, mLongitude, mOriginX, mOriginY,
        mCellsWide, mCellsHigh, mAreaWidthKm, mAreaHeightKm,
        mMetersPerSquare, mRotation,
        mMarginPercent, mRoadWidthPercent,
        mLand, mWater, mRoads,
        mGenerateStreets, mGenerateInGame, mGenerateProxyBuildings,
        mGenerateRoadMarkings,
        mGenerateNavZones, mGenerateForagingZones,
        mEndpoint
    };
    for (QObject *control : geometryControls) {
        if (QSpinBox *spin = qobject_cast<QSpinBox *>(control)) {
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                    this, &OsmTerrainImportDialog::updateGeometrySummary);
        } else if (QDoubleSpinBox *spin =
                   qobject_cast<QDoubleSpinBox *>(control)) {
            connect(spin,
                    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &OsmTerrainImportDialog::updateGeometrySummary);
        } else if (QCheckBox *check = qobject_cast<QCheckBox *>(control)) {
            connect(check, &QCheckBox::toggled,
                    this, &OsmTerrainImportDialog::updateGeometrySummary);
        } else if (QLineEdit *edit = qobject_cast<QLineEdit *>(control)) {
            connect(edit, &QLineEdit::textChanged,
                    this, &OsmTerrainImportDialog::updateGeometrySummary);
        }
    }
    updateStandaloneProjectSize();
    updateGeometrySummary();
    qInfo().noquote()
            << QStringLiteral(
                   "OSM importer opened: mode %1, center %2,%3, cells %4x%5, cell size %6")
               .arg(creatingProject ? QStringLiteral("new-project")
                                    : QStringLiteral("existing-project"))
               .arg(mLatitude->value(), 0, 'f', 7)
               .arg(mLongitude->value(), 0, 'f', 7)
               .arg(mCellsWide->value()).arg(mCellsHigh->value())
               .arg(currentCellSize());
}

OsmTerrainImportDialog::~OsmTerrainImportDialog()
{
    if (mReply)
        mReply->abort();
    if (mLocationReply)
        mLocationReply->abort();
    if (mWatcher->isRunning())
        mWatcher->waitForFinished();
}

QPoint OsmTerrainImportDialog::cellOrigin() const
{
    return QPoint(mOriginX->value(), mOriginY->value());
}

QSize OsmTerrainImportDialog::projectSize() const
{
    return QSize(mCellsWide->value(), mCellsHigh->value());
}

WorldGridFormat OsmTerrainImportDialog::gridFormat() const
{
    if (mWorldDocument)
        return mWorldDocument->world()->gridFormat();
    return WorldGridFormat(mGridFormat->currentData().toInt());
}

QString OsmTerrainImportDialog::projectFilePath() const
{
    QString path = QDir::cleanPath(QDir::fromNativeSeparators(
                                       mProjectPath->text().trimmed()));
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".pzw");
    return path;
}

QString OsmTerrainImportDialog::suggestedGroundPath() const
{
    return QDir::cleanPath(QDir::fromNativeSeparators(
                               mOutputPath->text().trimmed()));
}

bool OsmTerrainImportDialog::generatesStreets() const
{
    return mGenerateStreets->isChecked();
}

bool OsmTerrainImportDialog::generatesInGameMapFeatures() const
{
    return mGenerateInGame->isChecked();
}

bool OsmTerrainImportDialog::generatesProxyBuildings() const
{
    return mGenerateProxyBuildings->isChecked();
}

bool OsmTerrainImportDialog::generatesRoadMarkings() const
{
    return mGenerateRoadMarkings->isChecked();
}

bool OsmTerrainImportDialog::generatesNavZones() const
{
    return mGenerateNavZones->isChecked();
}

bool OsmTerrainImportDialog::generatesForagingZones() const
{
    return mGenerateForagingZones->isChecked();
}

int OsmTerrainImportDialog::currentCellSize() const
{
    if (mWorldDocument && mWorldDocument->world())
        return mWorldDocument->world()->cellSize();
    return gridFormat() == WorldGridFormat::Native256 ? 256 : 300;
}

void OsmTerrainImportDialog::updateStandaloneProjectSize()
{
    if (mWorldDocument)
        return;
    const QSize size = OsmTerrainImporter::requiredCells(
                mAreaWidthKm->value() * 1000.0,
                mAreaHeightKm->value() * 1000.0,
                currentCellSize(), mMetersPerSquare->value());
    if (!size.isValid())
        return;
    const QSignalBlocker widthBlocker(mCellsWide);
    const QSignalBlocker heightBlocker(mCellsHigh);
    mCellsWide->setValue(size.width());
    mCellsHigh->setValue(size.height());
}

void OsmTerrainImportDialog::updateStandaloneAreaFromCells()
{
    if (mWorldDocument)
        return;
    const double kilometersPerCell = currentCellSize()
            * mMetersPerSquare->value() / 1000.0;
    const QSignalBlocker widthBlocker(mAreaWidthKm);
    const QSignalBlocker heightBlocker(mAreaHeightKm);
    mAreaWidthKm->setValue(mCellsWide->value() * kilometersPerCell);
    mAreaHeightKm->setValue(mCellsHigh->value() * kilometersPerCell);
}

void OsmTerrainImportDialog::browseProject()
{
    QString fileName = QFileDialog::getSaveFileName(
                this, tr("Create WorldEd Project"),
                projectFilePath(), tr("PZWorldEd world files (*.pzw)"));
    if (fileName.isEmpty())
        return;
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".pzw");
    mProjectPath->setText(QDir::toNativeSeparators(fileName));
    mOutputPath->setText(QDir::toNativeSeparators(
                             QDir(QFileInfo(fileName).absolutePath())
                             .filePath(QStringLiteral("map/OSM_Map.png"))));
}

QRect OsmTerrainImportDialog::selectedCellArea() const
{
    if (!mWorldDocument || mWorldDocument->selectedCells().isEmpty())
        return QRect();
    int minimumX = std::numeric_limits<int>::max();
    int minimumY = std::numeric_limits<int>::max();
    int maximumX = std::numeric_limits<int>::min();
    int maximumY = std::numeric_limits<int>::min();
    for (WorldCell *cell : mWorldDocument->selectedCells()) {
        if (!cell)
            continue;
        minimumX = qMin(minimumX, cell->x());
        minimumY = qMin(minimumY, cell->y());
        maximumX = qMax(maximumX, cell->x());
        maximumY = qMax(maximumY, cell->y());
    }
    if (minimumX > maximumX || minimumY > maximumY)
        return QRect();
    return QRect(minimumX, minimumY,
                 maximumX - minimumX + 1,
                 maximumY - minimumY + 1);
}

void OsmTerrainImportDialog::setProjectArea(
        const QRect &area, const QString &source)
{
    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    if (!world || !area.isValid()
            || area.x() < 0 || area.y() < 0
            || area.right() >= world->width()
            || area.bottom() >= world->height()) {
        QMessageBox::warning(
                    this, tr("Invalid Project Area"),
                    tr("The requested cell area is outside the loaded "
                       "WorldEd project."));
        return;
    }
    mOriginX->setValue(area.x());
    mOriginY->setValue(area.y());
    mCellsWide->setValue(area.width());
    mCellsHigh->setValue(area.height());
    updateGeometrySummary();
    mStatus->setText(
                tr("Using %1: %2 x %3 cells starting at %4,%5.")
                .arg(source)
                .arg(area.width()).arg(area.height())
                .arg(area.x()).arg(area.y()));
}

void OsmTerrainImportDialog::useSelectedCells()
{
    const QRect area = selectedCellArea();
    if (!area.isValid()) {
        QMessageBox::information(
                    this, tr("No Cells Selected"),
                    tr("Select one or more cells in WorldEd, then reopen this "
                       "window or use a custom rectangle here."));
        return;
    }
    setProjectArea(area, tr("the selected WorldEd cells"));
}

void OsmTerrainImportDialog::useEntireProject()
{
    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    if (!world)
        return;
    setProjectArea(QRect(0, 0, world->width(), world->height()),
                   tr("the entire project"));
}

void OsmTerrainImportDialog::applyLocation(
        double latitude, double longitude, const QString &source)
{
    invalidateGeneratedResult();
    mLatitude->setValue(latitude);
    mLongitude->setValue(longitude);
    mLocationInput->setText(
                QStringLiteral("%1, %2")
                .arg(latitude, 0, 'f', 7)
                .arg(longitude, 0, 'f', 7));
    updateGeometrySummary();
    mStatus->setText(
                tr("Location loaded from %1. Center: %2, %3.")
                .arg(source)
                .arg(latitude, 0, 'f', 7)
                .arg(longitude, 0, 'f', 7));
}

void OsmTerrainImportDialog::applyLocationInput()
{
    const QString input = mLocationInput->text().trimmed();
    double latitude = 0.0;
    double longitude = 0.0;
    QString parseError;
    if (OsmTerrainImporter::parseLocationText(
                input, &latitude, &longitude, &parseError)) {
        applyLocation(latitude, longitude, tr("the pasted value"));
        return;
    }

    const QUrl url(input);
    const QString host = url.host().toLower();
    const bool googleMapHost =
            host == QLatin1String("google.com")
            || host.endsWith(QLatin1String(".google.com"))
            || host.startsWith(QLatin1String("www.google."))
            || host.startsWith(QLatin1String("maps.google."));
    const bool supportedMapHost =
            host == QLatin1String("openstreetmap.org")
            || host.endsWith(QLatin1String(".openstreetmap.org"))
            || host == QLatin1String("osm.org")
            || host.endsWith(QLatin1String(".osm.org"))
            || googleMapHost
            || host == QLatin1String("goo.gl")
            || host.endsWith(QLatin1String(".goo.gl"));
    if (url.isValid() && url.scheme() == QLatin1String("https")
            && supportedMapHost) {
        resolveLocationLink(url);
        return;
    }
    if (!input.isEmpty()) {
        searchLocationName(input);
        return;
    }

    QMessageBox::information(this, tr("Location Required"),
                             tr("Enter a place, coordinates, or a map link."));
}

void OsmTerrainImportDialog::openLocationMap()
{
    const QUrl url(QStringLiteral(
                       "https://www.openstreetmap.org/#map=13/%1/%2")
                   .arg(mLatitude->value(), 0, 'f', 7)
                   .arg(mLongitude->value(), 0, 'f', 7));
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::warning(
                    this, tr("OpenStreetMap"),
                    tr("WorldEd could not open the web browser. Open "
                       "https://www.openstreetmap.org manually."));
        return;
    }
    mStatus->setText(
                tr("OpenStreetMap opened at the current center. Right-click "
                   "your target, choose Show address, then paste its "
                   "coordinates or the page link above."));
}

void OsmTerrainImportDialog::resolveLocationLink(const QUrl &url)
{
    if (mReply || mLocationReply || mWatcher->isRunning())
        return;
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::critical(
                    this, tr("HTTPS Is Unavailable"),
                    tr("This portable WorldEd runtime cannot initialize TLS. "
                       "Install the OpenSSL runtime supplied with PZTools, "
                       "then restart WorldEd."));
        return;
    }
    mProgress->setRange(0, 0);
    QNetworkRequest request(url);
    request.setAttribute(
                QNetworkRequest::RedirectPolicyAttribute,
                QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader(
                QByteArrayLiteral("User-Agent"),
                QByteArrayLiteral(
                    "PZTools-WorldEd/1.0 (https://github.com/Unjammer/"
                    "PZ_Mapping_Tools)"));
    mLocationRequestKind = LinkLocationRequest;
    mLocationReply = mNetwork->get(request);
    connect(mLocationReply, &QNetworkReply::finished,
            this, &OsmTerrainImportDialog::locationLinkFinished);
    mTimeout->start(30000);
    setBusy(true, tr("Resolving the pasted map link..."));
}

void OsmTerrainImportDialog::searchLocationName(const QString &query)
{
    if (mReply || mLocationReply || mWatcher->isRunning())
        return;
    const QUrl endpoint(mGeocoderEndpoint->text().trimmed());
    if (!endpoint.isValid() || endpoint.scheme() != QLatin1String("https")) {
        QMessageBox::warning(
                    this, tr("Invalid Place-Search Endpoint"),
                    tr("Enter a valid HTTPS Nominatim-compatible search "
                       "endpoint."));
        return;
    }
    QUrl url(endpoint);
    QUrlQuery parameters(url);
    parameters.addQueryItem(QStringLiteral("q"), query);
    parameters.addQueryItem(QStringLiteral("format"),
                            QStringLiteral("jsonv2"));
    parameters.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    url.setQuery(parameters);

    const QByteArray digest = QCryptographicHash::hash(
                url.toEncoded(), QCryptographicHash::Sha256).toHex();
    mLocationSearchCachePath = QDir(PortableSettings::path(
                    QStringLiteral("cache/geocoding")))
            .filePath(QString::fromLatin1(digest)
                      + QStringLiteral(".json"));
    QFile cached(mLocationSearchCachePath);
    if (cached.open(QIODevice::ReadOnly)
            && applyGeocodingResponse(cached.readAll(),
                                      tr("the cached place search"))) {
        mProgress->setRange(0, 100);
        mProgress->setValue(0);
        return;
    }

    if (mGeocodingRateLimit.isValid()
            && mGeocodingRateLimit.elapsed() < 1000) {
        QMessageBox::information(
                    this, tr("Place Search Rate Limit"),
                    tr("Wait one second before sending another place "
                       "search."));
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::critical(
                    this, tr("HTTPS Is Unavailable"),
                    tr("This portable WorldEd runtime cannot initialize TLS. "
                       "Install the OpenSSL runtime supplied with PZTools, "
                       "then restart WorldEd."));
        return;
    }

    mProgress->setRange(0, 0);
    QNetworkRequest request(url);
    request.setAttribute(
                QNetworkRequest::RedirectPolicyAttribute,
                QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader(
                QByteArrayLiteral("User-Agent"),
                QByteArrayLiteral(
                    "PZTools-WorldEd/1.0 (https://github.com/Unjammer/"
                    "PZ_Mapping_Tools)"));
    mLocationRequestKind = GeocoderLocationRequest;
    mLocationReply = mNetwork->get(request);
    mGeocodingRateLimit.restart();
    connect(mLocationReply, &QNetworkReply::finished,
            this, &OsmTerrainImportDialog::locationLinkFinished);
    mTimeout->start(30000);
    setBusy(true, tr("Searching OpenStreetMap places for %1...").arg(query));
}

bool OsmTerrainImportDialog::applyGeocodingResponse(
        const QByteArray &json, const QString &source)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError
            || !document.isArray() || document.array().isEmpty()) {
        return false;
    }
    const QJsonObject place = document.array().first().toObject();
    bool latitudeOk = false;
    bool longitudeOk = false;
    const double latitude = place.value(QStringLiteral("lat"))
            .toString().toDouble(&latitudeOk);
    const double longitude = place.value(QStringLiteral("lon"))
            .toString().toDouble(&longitudeOk);
    if (!latitudeOk || !longitudeOk)
        return false;
    applyLocation(latitude, longitude, source);
    const QString displayName = place.value(
                QStringLiteral("display_name")).toString();
    if (!displayName.isEmpty()) {
        mStatus->setText(
                    tr("Location found: %1\nCenter: %2, %3")
                    .arg(displayName)
                    .arg(latitude, 0, 'f', 7)
                    .arg(longitude, 0, 'f', 7));
    }
    return true;
}

void OsmTerrainImportDialog::locationLinkFinished()
{
    mTimeout->stop();
    QNetworkReply *reply = mLocationReply;
    mLocationReply = nullptr;
    if (!reply)
        return;
    const LocationRequestKind requestKind = mLocationRequestKind;
    mLocationRequestKind = NoLocationRequest;
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QUrl finalUrl = reply->url();
    reply->deleteLater();
    setBusy(false);
    mProgress->setRange(0, 100);
    mProgress->setValue(0);
    if (networkError == QNetworkReply::OperationCanceledError) {
        mStatus->setText(tr("The map-link request was cancelled."));
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        QMessageBox::warning(
                    this, requestKind == GeocoderLocationRequest
                    ? tr("Place Search Failed")
                    : tr("Map Link Could Not Be Resolved"),
                    requestKind == GeocoderLocationRequest
                    ? tr("The place search failed. Try again later, change "
                         "the search endpoint, or paste coordinates.\n\n%1")
                      .arg(networkErrorText)
                    : tr("The map link could not be expanded. Paste the full "
                         "URL or the latitude and longitude instead.\n\n%1")
                    .arg(networkErrorText));
        return;
    }

    if (requestKind == GeocoderLocationRequest) {
        if (applyGeocodingResponse(body, tr("the OpenStreetMap place search"))) {
            QDir().mkpath(QFileInfo(
                              mLocationSearchCachePath).absolutePath());
            QSaveFile cache(mLocationSearchCachePath);
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(body);
                cache.commit();
            }
            return;
        }
        QMessageBox::information(
                    this, tr("Place Not Found"),
                    tr("No matching OpenStreetMap place was found. Add a "
                       "country or state to the name, or paste coordinates."));
        return;
    }

    double latitude = 0.0;
    double longitude = 0.0;
    QString parseError;
    if (OsmTerrainImporter::parseLocationText(
                finalUrl.toString(), &latitude, &longitude, &parseError)
            || OsmTerrainImporter::parseLocationText(
                QString::fromUtf8(body),
                &latitude, &longitude, &parseError)) {
        applyLocation(latitude, longitude, tr("the expanded map link"));
        return;
    }
    QMessageBox::information(
                this, tr("Coordinates Not Found"),
                tr("The link opened successfully, but it did not expose "
                   "coordinates. Open the location in the browser and paste "
                   "the full URL after the map has centered, or paste the "
                   "latitude and longitude shown by the map."));
}

void OsmTerrainImportDialog::scalePresetChanged(int index)
{
    const QVariant scale = mScalePreset->itemData(index);
    if (!scale.isValid())
        return;
    mMetersPerSquare->setValue(scale.toDouble());
    updateGeometrySummary();
}

void OsmTerrainImportDialog::detectRoadOrientation()
{
    mProgress->setRange(0, 100);
    mProgress->setValue(0);
    const OsmTerrainImportOptions value = options();
    const QUrl endpoint(value.endpoint);
    if (!endpoint.isValid() || endpoint.scheme() != QLatin1String("https")) {
        QMessageBox::warning(
                    this, tr("Invalid Overpass Endpoint"),
                    tr("Enter a valid HTTPS Overpass API endpoint before "
                       "detecting the road grid."));
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::critical(
                    this, tr("HTTPS Is Unavailable"),
                    tr("This portable WorldEd runtime cannot initialize TLS. "
                       "Install the OpenSSL runtime supplied with PZTools, "
                       "then restart WorldEd."));
        return;
    }
    saveSettings();
    beginOverpassRequest(
                OsmTerrainImporter::buildRoadOrientationQuery(value), true);
}

void OsmTerrainImportDialog::browseOutput()
{
    const QString fileName = QFileDialog::getSaveFileName(
                this, tr("Suggested OpenStreetMap Ground PNG"),
                suggestedGroundPath(), tr("PNG images (*.png)"));
    if (!fileName.isEmpty())
        mOutputPath->setText(QDir::toNativeSeparators(fileName));
}

OsmTerrainImportOptions OsmTerrainImportDialog::options() const
{
    OsmTerrainImportOptions value;
    const int cellSize = currentCellSize();
    value.centerLatitude = mLatitude->value();
    value.centerLongitude = mLongitude->value();
    value.widthPixels = mCellsWide->value() * cellSize;
    value.heightPixels = mCellsHigh->value() * cellSize;
    value.metersPerPixel = mMetersPerSquare->value();
    value.rotationDegreesClockwise = mRotation->value();
    value.marginPercent = mMarginPercent->value();
    value.roadWidthPercent = mRoadWidthPercent->value();
    value.includeLand = mLand->isChecked();
    value.includeWater = mWater->isChecked();
    value.includeRoads = mRoads->isChecked();
    value.includeBuildings = mGenerateProxyBuildings->isChecked()
            || mGenerateInGame->isChecked();
    value.endpoint = mEndpoint->text().trimmed();
    return value;
}

bool OsmTerrainImportDialog::validateOptions(
        const OsmTerrainImportOptions &value)
{
    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    if (world && (mOriginX->value() + mCellsWide->value() > world->width()
            || mOriginY->value() + mCellsHigh->value() > world->height())) {
        QMessageBox::warning(
                    this, tr("Invalid Project Area"),
                    tr("The requested %1 x %2 cell image starting at %3,%4 "
                       "extends outside the %5 x %6 cell project.")
                    .arg(mCellsWide->value()).arg(mCellsHigh->value())
                    .arg(mOriginX->value()).arg(mOriginY->value())
                    .arg(world->width()).arg(world->height()));
        return false;
    }
    if (!world && projectFilePath().isEmpty()) {
        QMessageBox::warning(
                    this, tr("Project File Required"),
                    tr("Choose where the new WorldEd PZW project will be "
                       "created."));
        return false;
    }
    if (!value.includeLand && !value.includeWater && !value.includeRoads
            && !value.includeBuildings) {
        QMessageBox::warning(this, tr("No OSM Layers"),
                             tr("Select at least one OSM layer."));
        return false;
    }
    const QUrl endpoint(value.endpoint);
    if (!endpoint.isValid() || endpoint.scheme() != QLatin1String("https")) {
        QMessageBox::warning(
                    this, tr("Invalid Overpass Endpoint"),
                    tr("Enter a valid HTTPS Overpass API endpoint."));
        return false;
    }
    const qint64 pixelCount = qint64(value.widthPixels)
            * value.heightPixels;
    if (pixelCount > 268435456LL) {
        QMessageBox::critical(
                    this, tr("OSM Import Too Large"),
                    tr("The requested %1 x %2 image exceeds the technical "
                       "268-million-pixel limit. This limit cannot be raised. "
                       "Reduce the project dimensions or use a wider "
                       "real-world scale.")
                    .arg(value.widthPixels).arg(value.heightPixels));
        return false;
    }
    if (!TerrainImageEditorDialog::ensureWorkingImageMemoryLimit(
                this, QSize(value.widthPixels, value.heightPixels),
                tr("Generate and edit OSM terrain images"),
                currentCellSize())) {
        return false;
    }
    return true;
}

void OsmTerrainImportDialog::startImport()
{
    mProgress->setRange(0, 100);
    mProgress->setValue(0);
    const OsmTerrainImportOptions value = options();
    if (!validateOptions(value))
        return;
    saveSettings();
    mActiveQueries = OsmTerrainImporter::buildOverpassQueries(value);
    mResponseChunks.clear();
    mActiveQueryIndex = -1;
    mAllResponsesFromCache = true;
    mWorkflowElapsed.restart();
    const QRectF bounds = OsmTerrainImporter::geographicBounds(value);
    qInfo().noquote()
            << QStringLiteral(
                   "OSM import started: center %1,%2, output %3x%4, cells %5x%6, scale %7 m/pixel, rotation %8, margin %9%, road width %10%, layers land=%11 water=%12 roads=%13 buildings=%14, bounds S%15 W%16 N%17 E%18, request parts %19")
               .arg(value.centerLatitude, 0, 'f', 7)
               .arg(value.centerLongitude, 0, 'f', 7)
               .arg(value.widthPixels).arg(value.heightPixels)
               .arg(mCellsWide->value()).arg(mCellsHigh->value())
               .arg(value.metersPerPixel, 0, 'f', 3)
               .arg(value.rotationDegreesClockwise, 0, 'f', 1)
               .arg(value.marginPercent).arg(value.roadWidthPercent)
               .arg(value.includeLand).arg(value.includeWater)
               .arg(value.includeRoads).arg(value.includeBuildings)
               .arg(bounds.top(), 0, 'f', 7)
               .arg(bounds.left(), 0, 'f', 7)
               .arg(bounds.bottom(), 0, 'f', 7)
               .arg(bounds.right(), 0, 'f', 7)
               .arg(mActiveQueries.size());
    startNextImportChunk();
}

void OsmTerrainImportDialog::startNextImportChunk()
{
    ++mActiveQueryIndex;
    if (mActiveQueryIndex >= mActiveQueries.size()) {
        qint64 totalBytes = 0;
        for (const QByteArray &chunk : mResponseChunks)
            totalBytes += chunk.size();
        qInfo().noquote()
                << QStringLiteral(
                       "OSM download complete: %1 parts, %2 bytes, cache-only %3, elapsed %4 ms")
                   .arg(mResponseChunks.size()).arg(totalBytes)
                   .arg(mAllResponsesFromCache)
                   .arg(mWorkflowElapsed.elapsed());
        startGeneration(mResponseChunks, mAllResponsesFromCache);
        return;
    }

    mActiveQuery = mActiveQueries.at(mActiveQueryIndex);
    const QString cachePath = cacheFilePath(mActiveQuery);
    if (mUseCache->isChecked() && QFileInfo::exists(cachePath)) {
        QFile cache(cachePath);
        if (cache.open(QIODevice::ReadOnly)) {
            const QByteArray data = cache.readAll();
            if (!data.isEmpty()) {
                mResponseChunks += data;
                mProgress->setRange(0, 100);
                mProgress->setValue((mActiveQueryIndex + 1) * 100
                                    / mActiveQueries.size());
                qInfo().noquote()
                        << QStringLiteral(
                               "OSM cache hit: part %1/%2, %3 bytes, %4")
                           .arg(mActiveQueryIndex + 1)
                           .arg(mActiveQueries.size()).arg(data.size())
                           .arg(QDir::toNativeSeparators(cachePath));
                QTimer::singleShot(
                            0, this,
                            &OsmTerrainImportDialog::startNextImportChunk);
                return;
            }
            qWarning().noquote()
                    << QStringLiteral("OSM cache file is empty: %1")
                       .arg(QDir::toNativeSeparators(cachePath));
        } else {
            qWarning().noquote()
                    << QStringLiteral("OSM cache read failed: %1, %2")
                       .arg(QDir::toNativeSeparators(cachePath),
                            cache.errorString());
        }
    } else {
        qInfo().noquote()
                << QStringLiteral("OSM cache miss: part %1/%2, %3")
                   .arg(mActiveQueryIndex + 1).arg(mActiveQueries.size())
                   .arg(QDir::toNativeSeparators(cachePath));
    }

    if (!QSslSocket::supportsSsl()) {
        qCritical() << "OSM import stopped because TLS is unavailable";
        QMessageBox::critical(
                    this, tr("HTTPS Is Unavailable"),
                    tr("This portable WorldEd runtime cannot initialize TLS. "
                       "Install the OpenSSL runtime supplied with PZTools, "
                       "then restart WorldEd.\n\nBuild: %1\nRuntime: %2")
                    .arg(QSslSocket::sslLibraryBuildVersionString(),
                         QSslSocket::sslLibraryVersionString()));
        return;
    }

    mAllResponsesFromCache = false;
    beginOverpassRequest(mActiveQuery, false);
}

void OsmTerrainImportDialog::beginOverpassRequest(
        const QString &query, bool orientationOnly)
{
    const OsmTerrainImportOptions value = options();
    mActiveQuery = query;
    mOrientationOnlyRequest = orientationOnly;
    mOverpassEndpoints.clear();
    mOverpassFailures.clear();
    mOverpassEndpointIndex = -1;
    mRateLimitRetryCount = 0;
    mTransientRetryCount = 0;
    mRetryCurrentEndpoint = false;
    mCancelRequested = false;
    mRequestTimedOut = false;
    const auto addEndpoint = [this](const QString &endpoint) {
        if (!mOverpassEndpoints.contains(endpoint, Qt::CaseInsensitive))
            mOverpassEndpoints += endpoint;
    };
    addEndpoint(value.endpoint);
    const QString configuredHost = QUrl(value.endpoint).host().toLower();
    if (configuredHost == QLatin1String("overpass-api.de")) {
        addEndpoint(QStringLiteral(
                        "https://gall.openstreetmap.de/api/interpreter"));
        addEndpoint(QStringLiteral(
                        "https://lambert.openstreetmap.de/api/interpreter"));
    } else if (configuredHost == QLatin1String("gall.openstreetmap.de")) {
        addEndpoint(QStringLiteral(
                        "https://lambert.openstreetmap.de/api/interpreter"));
        addEndpoint(QStringLiteral(
                        "https://overpass-api.de/api/interpreter"));
    } else if (configuredHost
               == QLatin1String("lambert.openstreetmap.de")) {
        addEndpoint(QStringLiteral(
                        "https://gall.openstreetmap.de/api/interpreter"));
        addEndpoint(QStringLiteral(
                        "https://overpass-api.de/api/interpreter"));
    }
    startOverpassRequest();
}

void OsmTerrainImportDialog::startOverpassRequest()
{
    if (mRetryCurrentEndpoint) {
        mRetryCurrentEndpoint = false;
    } else {
        if (++mOverpassEndpointIndex >= mOverpassEndpoints.size())
            return;
        mRateLimitRetryCount = 0;
        mTransientRetryCount = 0;
    }

    const QString endpoint = mOverpassEndpoints.at(
                mOverpassEndpointIndex);
    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader(
                QByteArrayLiteral("User-Agent"),
                QByteArrayLiteral(
                    "PZTools-WorldEd/1.0 (https://github.com/Unjammer/"
                    "PZ_Mapping_Tools)"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("data"), mActiveQuery);
    mReply = mNetwork->post(
                request, form.toString(QUrl::FullyEncoded).toUtf8());
    mRequestElapsed.restart();
    qInfo().noquote()
            << QStringLiteral(
                   "OSM Overpass request: part %1/%2, endpoint %3/%4, rate retries %5, transient retries %6, query %7 bytes, URL %8")
               .arg(mOrientationOnlyRequest ? 1 : mActiveQueryIndex + 1)
               .arg(mOrientationOnlyRequest ? 1 : mActiveQueries.size())
               .arg(mOverpassEndpointIndex + 1)
               .arg(mOverpassEndpoints.size())
               .arg(mRateLimitRetryCount).arg(mTransientRetryCount)
               .arg(mActiveQuery.toUtf8().size()).arg(endpoint);
    connect(mReply, &QNetworkReply::finished,
            this, &OsmTerrainImportDialog::networkFinished);
    connect(mReply, &QNetworkReply::downloadProgress,
            this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            mProgress->setRange(0, 100);
            const int partCount = mOrientationOnlyRequest
                    ? 1 : qMax(1, mActiveQueries.size());
            const int partIndex = mOrientationOnlyRequest
                    ? 0 : qMax(0, mActiveQueryIndex);
            mProgress->setValue(int((partIndex * 100
                                     + received * 100 / total)
                                    / partCount));
        } else {
            mProgress->setRange(0, 0);
        }
    });
    mRequestTimedOut = false;
    mTimeout->start(180000);
    const QString requestStatus = mOrientationOnlyRequest
            ? tr("Analyzing OpenStreetMap road orientation on server "
                 "%1 of %2...")
              .arg(mOverpassEndpointIndex + 1)
              .arg(mOverpassEndpoints.size())
            : tr("Downloading OpenStreetMap part %3 of %4 from Overpass "
                 "server %1 of %2...")
              .arg(mOverpassEndpointIndex + 1)
              .arg(mOverpassEndpoints.size())
              .arg(mActiveQueryIndex + 1)
              .arg(mActiveQueries.size());
    setBusy(true, requestStatus);
}

bool OsmTerrainImportDialog::hasAnotherOverpassEndpoint() const
{
    return mOverpassEndpointIndex + 1 < mOverpassEndpoints.size();
}

void OsmTerrainImportDialog::cancelImport()
{
    mCancelRequested = true;
    bool cancelled = false;
    if (mRetryTimer->isActive()) {
        mRetryTimer->stop();
        cancelled = true;
    }
    if (mReply) {
        mReply->abort();
        cancelled = true;
    }
    if (mLocationReply) {
        mLocationReply->abort();
        cancelled = true;
    }
    if (cancelled) {
        qInfo().noquote()
                << QStringLiteral("OSM network workflow cancelled by user at part %1/%2")
                   .arg(mActiveQueryIndex + 1)
                   .arg(mActiveQueries.size());
        mStatus->setText(tr("The network request was cancelled."));
        mProgress->setRange(0, 100);
        mProgress->setValue(0);
        if (!mReply && !mLocationReply)
            setBusy(false);
    }
}

void OsmTerrainImportDialog::networkTimeout()
{
    if (mReply) {
        qWarning().noquote()
                << QStringLiteral("OSM Overpass request reached the 180 second client timeout at part %1/%2")
                   .arg(mActiveQueryIndex + 1)
                   .arg(mActiveQueries.size());
        mRequestTimedOut = true;
        mReply->abort();
    } else if (mLocationReply) {
        mLocationReply->abort();
    }
}

void OsmTerrainImportDialog::networkFinished()
{
    mTimeout->stop();
    QNetworkReply *reply = mReply;
    mReply = nullptr;
    if (!reply)
        return;
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const int httpStatus = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray retryAfterHeader = reply->rawHeader(
                QByteArrayLiteral("Retry-After"));
    const QString networkErrorText = reply->errorString();
    const QString failedEndpoint = mOverpassEndpointIndex >= 0
            && mOverpassEndpointIndex < mOverpassEndpoints.size()
            ? mOverpassEndpoints.at(mOverpassEndpointIndex) : QString();
    const bool timedOut = mRequestTimedOut;
    const qint64 requestMilliseconds = mRequestElapsed.isValid()
            ? mRequestElapsed.elapsed() : -1;
    mRequestTimedOut = false;
    reply->deleteLater();
    qInfo().noquote()
            << QStringLiteral(
                   "OSM Overpass response: part %1/%2, endpoint %3, HTTP %4, network error %5, %6 bytes, elapsed %7 ms, Retry-After '%8'")
               .arg(mOrientationOnlyRequest ? 1 : mActiveQueryIndex + 1)
               .arg(mOrientationOnlyRequest ? 1 : mActiveQueries.size())
               .arg(failedEndpoint).arg(httpStatus)
               .arg(int(networkError)).arg(data.size())
               .arg(requestMilliseconds)
               .arg(QString::fromLatin1(retryAfterHeader));
    if (networkError == QNetworkReply::OperationCanceledError
            && mCancelRequested && !timedOut) {
        setBusy(false, tr("The Overpass request was cancelled."));
        mProgress->setRange(0, 100);
        mProgress->setValue(0);
        return;
    }
    if (networkError != QNetworkReply::NoError
            || httpStatus < 200 || httpStatus >= 300) {
        const bool transientFailure = timedOut
                || httpStatus == 429
                || httpStatus == 502
                || httpStatus == 503
                || httpStatus == 504
                || networkError == QNetworkReply::ConnectionRefusedError
                || networkError == QNetworkReply::RemoteHostClosedError
                || networkError == QNetworkReply::HostNotFoundError
                || networkError == QNetworkReply::TimeoutError
                || networkError == QNetworkReply::TemporaryNetworkFailureError
                || networkError == QNetworkReply::NetworkSessionFailedError
                || networkError == QNetworkReply::ProxyTimeoutError
                || networkError == QNetworkReply::UnknownNetworkError;
        const QString reason = timedOut
                ? tr("client timeout after 180 seconds")
                : networkErrorText;
        mOverpassFailures += httpStatus > 0
                ? tr("%1: HTTP %2, %3")
                  .arg(failedEndpoint).arg(httpStatus).arg(reason)
                : tr("%1: %2").arg(failedEndpoint, reason);
        int retryDelay = 0;
        QString retryStatus;
        if (httpStatus == 429) {
            ++mRateLimitRetryCount;
            const int serverDelay = retryAfterMilliseconds(
                        retryAfterHeader);
            const int policyDelay = qMin(
                        120000, 15000 * (1 << qMin(
                                            2, mRateLimitRetryCount - 1)));
            retryDelay = qMax(serverDelay, policyDelay);
            if (mRateLimitRetryCount <= 2) {
                mRetryCurrentEndpoint = true;
                retryStatus = tr(
                            "OpenStreetMap rate-limited this request with "
                            "HTTP 429. Waiting %1 second(s), then retrying "
                            "the same request as recommended by Overpass. "
                            "Part %2 of %3 remains pending.")
                        .arg((retryDelay + 999) / 1000)
                        .arg(mOrientationOnlyRequest
                             ? 1 : mActiveQueryIndex + 1)
                        .arg(mOrientationOnlyRequest
                             ? 1 : mActiveQueries.size());
            } else if (hasAnotherOverpassEndpoint()) {
                retryStatus = tr(
                            "OpenStreetMap still returns HTTP 429. Waiting "
                            "%1 second(s), then trying the next configured "
                            "Overpass endpoint.")
                        .arg((retryDelay + 999) / 1000);
            } else {
                retryDelay = 0;
            }
        } else if (transientFailure) {
            ++mTransientRetryCount;
            if (mTransientRetryCount <= 1) {
                retryDelay = 5000;
                mRetryCurrentEndpoint = true;
                retryStatus = tr(
                            "The Overpass request failed temporarily. "
                            "Retrying the same endpoint in 5 seconds...");
            } else if (hasAnotherOverpassEndpoint()) {
                retryDelay = 3000;
                retryStatus = tr(
                            "The Overpass request failed again. Trying the "
                            "next configured endpoint in 3 seconds...");
            }
        }
        if (retryDelay > 0) {
            mProgress->setRange(0, 100);
            mRetryTimer->start(retryDelay);
            qWarning().noquote()
                    << QStringLiteral(
                           "OSM Overpass retry scheduled: HTTP %1, same endpoint %2, delay %3 ms, part %4/%5")
                       .arg(httpStatus).arg(mRetryCurrentEndpoint)
                       .arg(retryDelay)
                       .arg(mOrientationOnlyRequest
                            ? 1 : mActiveQueryIndex + 1)
                       .arg(mOrientationOnlyRequest
                            ? 1 : mActiveQueries.size());
            setBusy(true, retryStatus);
            return;
        }
        qWarning().noquote()
                << QStringLiteral(
                       "OSM Overpass request failed permanently after %1 attempt record(s): %2")
                   .arg(mOverpassFailures.size())
                   .arg(mOverpassFailures.join(QStringLiteral(" | ")));
        setBusy(false);
        mProgress->setRange(0, 100);
        mProgress->setValue(0);
        QMessageBox::warning(
                    this, mOrientationOnlyRequest
                    ? tr("Road Orientation Detection Failed")
                    : tr("OpenStreetMap Download Failed"),
                    (mOrientationOnlyRequest
                     ? tr("The public Overpass service could not download the "
                          "roads needed for orientation detection. The current "
                          "rotation was not changed. Retry later or select a "
                          "smaller area.\n\nAttempts:\n%1")
                     : tr("The public Overpass service could not complete the "
                          "request. No project or local image was changed. "
                          "WorldEd already tried every available endpoint. "
                          "Retry later, reduce the download margin, disable "
                          "unneeded layers, or select a smaller area."
                          "\n\nAttempts:\n%1"))
                    .arg(mOverpassFailures.join(QStringLiteral("\n"))));
        return;
    }

    if (mOrientationOnlyRequest) {
        double rotation = 0.0;
        double confidence = 0.0;
        int segments = 0;
        QString detectionError;
        if (!OsmTerrainImporter::suggestRoadGridRotation(
                    data, options(), &rotation, &segments,
                    &confidence, &detectionError)) {
            setBusy(false);
            mProgress->setRange(0, 100);
            mProgress->setValue(0);
            QMessageBox::information(
                        this, tr("No Clear Road Grid Found"),
                        detectionError);
            return;
        }
        setBusy(false);
        mProgress->setRange(0, 100);
        mProgress->setValue(100);
        mRotation->setValue(rotation);
        saveSettings();
        const QString direction = rotation < 0.0
                ? tr("counter-clockwise") : tr("clockwise");
        mStatus->setText(
                    tr("Suggested rotation: %1 degrees %2, calculated from "
                       "%3 road segments with %4% grid confidence. The "
                       "preview shows the resulting true-North direction.")
                    .arg(compactLocaleNumber(std::abs(rotation), 1), direction)
                    .arg(segments)
                    .arg(qRound(confidence * 100.0)));
        qInfo().noquote()
                << QStringLiteral(
                       "OSM road-grid detection complete: rotation %1, segments %2, confidence %3")
                   .arg(rotation, 0, 'f', 1).arg(segments)
                   .arg(confidence, 0, 'f', 3);
        return;
    }

    const QString cachePath = cacheFilePath(mActiveQuery);
    QDir().mkpath(QFileInfo(cachePath).absolutePath());
    QSaveFile cache(cachePath);
    if (cache.open(QIODevice::WriteOnly)) {
        cache.write(data);
        if (cache.commit()) {
            qInfo().noquote()
                    << QStringLiteral("OSM cache written: %1 bytes, %2")
                       .arg(data.size())
                       .arg(QDir::toNativeSeparators(cachePath));
        } else {
            qWarning().noquote()
                    << QStringLiteral("OSM cache commit failed: %1, %2")
                       .arg(QDir::toNativeSeparators(cachePath),
                            cache.errorString());
        }
    } else {
        qWarning().noquote()
                << QStringLiteral("OSM cache open failed: %1, %2")
                   .arg(QDir::toNativeSeparators(cachePath),
                        cache.errorString());
    }
    mResponseChunks += data;
    mProgress->setRange(0, 100);
    mProgress->setValue((mActiveQueryIndex + 1) * 100
                        / qMax(1, mActiveQueries.size()));
    QTimer::singleShot(
                0, this, &OsmTerrainImportDialog::startNextImportChunk);
}

void OsmTerrainImportDialog::startGeneration(
        const QList<QByteArray> &chunks, bool fromCache)
{
    const OsmTerrainImportOptions value = options();
    qint64 totalBytes = 0;
    for (const QByteArray &chunk : chunks)
        totalBytes += chunk.size();
    mGenerationElapsed.restart();
    qInfo().noquote()
            << QStringLiteral(
                   "OSM rendering started: %1 response parts, %2 bytes, output %3x%4")
               .arg(chunks.size()).arg(totalBytes)
               .arg(value.widthPixels).arg(value.heightPixels);
    setBusy(true, fromCache
            ? tr("Rendering terrain from the cached OSM response...")
            : tr("Rendering terrain and vegetation from OSM geometry..."));
    mProgress->setRange(0, 0);
    mWatcher->setFuture(QtConcurrent::run(
                            [chunks, value]() {
        OsmTerrainGenerationTaskResult task;
        task.success = OsmTerrainImporter::generateFromOverpassJsonChunks(
                    chunks, value, &task.generated, &task.error);
        return task;
    }));
}

void OsmTerrainImportDialog::generationFinished()
{
    const OsmTerrainGenerationTaskResult task = mWatcher->result();
    setBusy(false);
    if (!task.success) {
        qWarning().noquote()
                << QStringLiteral("OSM rendering failed after %1 ms: %2")
                   .arg(mGenerationElapsed.elapsed(), 0, 10)
                   .arg(task.error);
        mProgress->setRange(0, 100);
        mProgress->setValue(0);
        QMessageBox::warning(this, tr("OSM Terrain Generation Failed"),
                             task.error);
        return;
    }
    mGenerated = task.generated;
    enrichMetadata(mActiveQueries);
    mProgress->setRange(0, 100);
    mProgress->setValue(100);
    renderAreaPreview();
    mOpenButton->setEnabled(true);
    QString status = tr(
                "Generated %1 x %2 pixels from %3 OSM elements, with %4 "
                "polygon fills, %5 roads, %6 railways, %7 farm areas, "
                "%8 waterways, %9 named streets, and %10 building footprints.")
            .arg(mGenerated.groundImage.width())
            .arg(mGenerated.groundImage.height())
            .arg(mGenerated.sourceElementCount)
            .arg(mGenerated.polygonCount)
            .arg(mGenerated.roadCount)
            .arg(mGenerated.railwayCount)
            .arg(mGenerated.farmAreaCount)
            .arg(mGenerated.waterLineCount)
            .arg(mGenerated.namedStreetCount)
            .arg(mGenerated.buildingCount);
    if (!mGenerated.warnings.isEmpty())
        status += QStringLiteral("\n") + mGenerated.warnings.join(
                    QStringLiteral("\n"));
    mStatus->setText(status);
    qInfo().noquote()
            << QStringLiteral(
                   "OSM rendering complete: %1 elements, %2 polygons, %3 roads, %4 railways, %5 farm areas, %6 waterways, %7 streets, %8 buildings, render %9 ms, total workflow %10 ms")
               .arg(mGenerated.sourceElementCount)
               .arg(mGenerated.polygonCount).arg(mGenerated.roadCount)
               .arg(mGenerated.railwayCount)
               .arg(mGenerated.farmAreaCount)
               .arg(mGenerated.waterLineCount)
               .arg(mGenerated.namedStreetCount)
               .arg(mGenerated.buildingCount)
               .arg(mGenerationElapsed.elapsed())
               .arg(mWorkflowElapsed.isValid()
                    ? mWorkflowElapsed.elapsed() : -1);
}

void OsmTerrainImportDialog::updateGeometrySummary()
{
    invalidateGeneratedResult();
    const OsmTerrainImportOptions value = options();
    const QRectF bounds = OsmTerrainImporter::geographicBounds(value);
    const int cellSize = currentCellSize();
    const double realWidth = value.widthPixels * value.metersPerPixel;
    const double realHeight = value.heightPixels * value.metersPerPixel;
    const QString orientation = qAbs(value.rotationDegreesClockwise) < 0.05
            ? tr("North up (0 degrees)")
            : tr("%1 degrees %2")
              .arg(compactLocaleNumber(
                       qAbs(value.rotationDegreesClockwise), 1),
                   value.rotationDegreesClockwise < 0.0
                   ? tr("counter-clockwise") : tr("clockwise"));
    QString summary =
                tr("Project output: %1 x %2 cells at %3 x %3 squares per "
                   "cell = %4 x %5 pixels.\n"
                   "Output scale: %6 m = 1 pixel = 1 map square. "
                   "Covered area: %7 x %8.\n"
                   "Orientation: %9. Geographic query: south %10, west %11, "
                   "north %12, east %13.")
                .arg(mCellsWide->value()).arg(mCellsHigh->value())
                .arg(cellSize)
                .arg(value.widthPixels).arg(value.heightPixels)
                .arg(compactLocaleNumber(value.metersPerPixel))
                .arg(formatDistance(realWidth), formatDistance(realHeight))
                .arg(orientation)
                .arg(formatCoordinate(bounds.top()))
                .arg(formatCoordinate(bounds.left()))
                .arg(formatCoordinate(bounds.bottom()))
                .arg(formatCoordinate(bounds.right()));
    if (!mWorldDocument) {
        summary.prepend(
                    tr("Selected real-world area: %1 x %2. The project is "
                       "rounded up to complete cells.\n")
                    .arg(formatDistance(mAreaWidthKm->value() * 1000.0),
                         formatDistance(mAreaHeightKm->value() * 1000.0)));
        mProjectFormat->setText(
                    tr("New project: %1 x %2 cells, %3 format. Choose a "
                       "place such as New York, then adjust the area, output "
                       "scale, and road-grid orientation.")
                    .arg(mCellsWide->value()).arg(mCellsHigh->value())
                    .arg(cellSize == 256
                         ? tr("Native 256 x 256")
                         : tr("Legacy 300 x 300")));
    }
    mGeometrySummary->setText(summary);
    renderAreaPreview();
}

void OsmTerrainImportDialog::invalidateGeneratedResult()
{
    mProgress->setRange(0, 100);
    mProgress->setValue(0);
    if (!mGenerated.groundImage.isNull()) {
        mGenerated = OsmTerrainImportResult();
        mOpenButton->setEnabled(false);
    }
}

void OsmTerrainImportDialog::renderAreaPreview()
{
    QSize canvasSize = mPreviewScroll->viewport()->size()
            - QSize(24, 24);
    canvasSize = canvasSize.expandedTo(QSize(640, 520));
    canvasSize = canvasSize.boundedTo(QSize(1000, 760));
    QImage canvas(canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(QColor(25, 30, 34));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRect available = canvas.rect().adjusted(28, 52, -28, -48);
    QRect imageRect;
    if (!mGenerated.groundImage.isNull()) {
        const QSize scaledSize = mGenerated.groundImage.size().scaled(
                    available.size(), Qt::KeepAspectRatio);
        imageRect = QRect(QPoint(), scaledSize);
        imageRect.moveCenter(available.center());
        painter.drawImage(
                    imageRect,
                    mGenerated.groundImage.scaled(
                        scaledSize, Qt::IgnoreAspectRatio,
                        Qt::FastTransformation));
    } else {
        QSize areaSize(qMax(1, mCellsWide->value()),
                       qMax(1, mCellsHigh->value()));
        areaSize.scale(available.size(), Qt::KeepAspectRatio);
        imageRect = QRect(QPoint(), areaSize);
        imageRect.moveCenter(available.center());
        painter.fillRect(imageRect, QColor(71, 92, 62));
        painter.fillRect(QRect(imageRect.left(), imageRect.top(),
                               qMax(1, imageRect.width() / 4),
                               imageRect.height()), QColor(39, 85, 120));
    }

    const int cellsWide = qMax(1, mCellsWide->value());
    const int cellsHigh = qMax(1, mCellsHigh->value());
    const int horizontalStep = qMax(1, (cellsWide + 39) / 40);
    const int verticalStep = qMax(1, (cellsHigh + 39) / 40);
    painter.setPen(QPen(QColor(255, 255, 255, 115), 1));
    for (int x = 0; x <= cellsWide; x += horizontalStep) {
        const int pixelX = imageRect.left()
                + qRound(double(x) * imageRect.width() / cellsWide);
        painter.drawLine(pixelX, imageRect.top(),
                         pixelX, imageRect.bottom());
    }
    for (int y = 0; y <= cellsHigh; y += verticalStep) {
        const int pixelY = imageRect.top()
                + qRound(double(y) * imageRect.height() / cellsHigh);
        painter.drawLine(imageRect.left(), pixelY,
                         imageRect.right(), pixelY);
    }
    painter.setPen(QPen(Qt::white, 2));
    painter.drawRect(imageRect.adjusted(0, 0, -1, -1));
    painter.setPen(Qt::white);
    QFont headingFont = painter.font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    painter.setFont(headingFont);
    painter.drawText(QRect(8, 8, canvas.width() - 16, 34),
                     Qt::AlignCenter,
                     tr("%1 x %2 cells  |  %3 x %4 pixels")
                     .arg(cellsWide).arg(cellsHigh)
                     .arg(cellsWide * currentCellSize())
                     .arg(cellsHigh * currentCellSize()));
    painter.setFont(QFont());
    painter.drawText(QRect(8, canvas.height() - 38,
                           canvas.width() - 16, 28),
                     Qt::AlignCenter,
                     tr("Coverage %1 x %2  |  %3 m per pixel")
                     .arg(formatDistance(
                              cellsWide * currentCellSize()
                              * mMetersPerSquare->value()))
                     .arg(formatDistance(
                              cellsHigh * currentCellSize()
                              * mMetersPerSquare->value()))
                     .arg(mMetersPerSquare->value(), 0, 'f', 3));
    painter.end();
    mRenderedPreview = canvas;
    previewZoomChanged(mPreviewZoom->value());
}

void OsmTerrainImportDialog::previewZoomChanged(int percent)
{
    QSettings settings;
    settings.setValue(QLatin1String("OSMImporter/PreviewZoom"), percent);
    if (mRenderedPreview.isNull())
        return;
    const QSize zoomedSize(
                qMax(1, mRenderedPreview.width() * percent / 100),
                qMax(1, mRenderedPreview.height() * percent / 100));
    mPreview->setPixmap(QPixmap::fromImage(mRenderedPreview).scaled(
                            zoomedSize, Qt::IgnoreAspectRatio,
                            Qt::FastTransformation));
    mPreview->resize(zoomedSize);
}

void OsmTerrainImportDialog::setBusy(bool busy, const QString &status)
{
    mGenerateButton->setEnabled(!busy);
    mApplyLocationButton->setEnabled(!busy);
    mOpenMapButton->setEnabled(!busy);
    mDetectOrientationButton->setEnabled(!busy);
    mClearCacheButton->setEnabled(!busy);
    mResetParametersButton->setEnabled(!busy);
    mCancelButton->setEnabled(
                busy && (mReply || mLocationReply
                         || mRetryTimer->isActive()));
    mOpenButton->setEnabled(!busy && !mGenerated.groundImage.isNull());
    if (!status.isEmpty())
        mStatus->setText(status);
}

void OsmTerrainImportDialog::resetParameters()
{
    QSettings settings;
    settings.beginGroup(QLatin1String("OSMImporter"));
    settings.remove(QString());
    settings.endGroup();

    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    const bool creatingProject = world == nullptr;
    mLocationInput->clear();
    mLatitude->setValue(0.0);
    mLongitude->setValue(0.0);
    mMetersPerSquare->setValue(1.0);
    mScalePreset->setCurrentIndex(1);
    mRotation->setValue(0.0);
    mMarginPercent->setValue(20);
    mRoadWidthPercent->setValue(100);
    mLand->setChecked(true);
    mWater->setChecked(true);
    mRoads->setChecked(true);
    mGenerateStreets->setChecked(creatingProject);
    mGenerateInGame->setChecked(creatingProject);
    mGenerateProxyBuildings->setChecked(creatingProject);
    mGenerateRoadMarkings->setChecked(creatingProject);
    mGenerateNavZones->setChecked(creatingProject);
    mGenerateForagingZones->setChecked(creatingProject);
    mUseCache->setChecked(true);
    mEndpoint->setText(QStringLiteral(
                           "https://overpass-api.de/api/interpreter"));
    mGeocoderEndpoint->setText(QStringLiteral(
                                   "https://nominatim.openstreetmap.org/search"));
    mPreviewZoom->setValue(100);

    if (creatingProject) {
        mGridFormat->setCurrentIndex(1);
        mAreaWidthKm->setValue(2.0);
        mAreaHeightKm->setValue(2.0);
        updateStandaloneProjectSize();
    } else {
        const QRect selection = selectedCellArea();
        const int defaultWidth = qMin(2, world->width());
        const int defaultHeight = qMin(2, world->height());
        mOriginX->setValue(selection.isValid() ? selection.x() : 0);
        mOriginY->setValue(selection.isValid() ? selection.y() : 0);
        mCellsWide->setValue(selection.isValid()
                             ? selection.width() : defaultWidth);
        mCellsHigh->setValue(selection.isValid()
                             ? selection.height() : defaultHeight);
    }

    QString projectRoot = PortableSettings::installRootPath();
    if (mWorldDocument && !mWorldDocument->fileName().isEmpty())
        projectRoot = QFileInfo(mWorldDocument->fileName()).absolutePath();
    const QString defaultProjectPath = QDir(projectRoot).filePath(
                QStringLiteral("OSM_Project.pzw"));
    mProjectPath->setText(QDir::toNativeSeparators(defaultProjectPath));
    mOutputPath->setText(QDir::toNativeSeparators(
                             QDir(creatingProject
                                  ? QFileInfo(defaultProjectPath).absolutePath()
                                  : projectRoot).filePath(
                                 QStringLiteral("map/OSM_Map.png"))));
    mActiveQuery.clear();
    mActiveQueries.clear();
    mResponseChunks.clear();
    mActiveQueryIndex = -1;
    invalidateGeneratedResult();
    updateGeometrySummary();
    mStatus->setText(tr(
                         "OpenStreetMap parameters were reset. The center is "
                         "0,0. Cached responses were kept."));
    qInfo() << "OSM importer parameters reset to defaults with center 0,0";
}

void OsmTerrainImportDialog::clearOsmCache()
{
    const QStringList cacheDirectories = {
        PortableSettings::path(QStringLiteral("cache/osm")),
        PortableSettings::path(QStringLiteral("cache/geocoding"))
    };
    int cachedFiles = 0;
    qint64 cachedBytes = 0;
    for (const QString &path : cacheDirectories) {
        const QDir directory(path);
        for (const QFileInfo &file : directory.entryInfoList(
                 QStringList() << QStringLiteral("*.json"),
                 QDir::Files | QDir::NoSymLinks)) {
            ++cachedFiles;
            cachedBytes += file.size();
        }
    }
    if (cachedFiles == 0) {
        qInfo() << "OSM cache clear requested but cache was already empty";
        mStatus->setText(tr("The OpenStreetMap cache is already empty."));
        mProgress->setRange(0, 100);
        mProgress->setValue(0);
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Clear OpenStreetMap Cache"),
                tr("Delete %1 cached OpenStreetMap response file(s), using %2?\n\n"
                   "New place searches and geometry downloads will use the network again.")
                .arg(cachedFiles)
                .arg(QLocale().formattedDataSize(cachedBytes)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    int removedFiles = 0;
    QStringList failures;
    for (const QString &path : cacheDirectories) {
        QDir directory(path);
        for (const QFileInfo &file : directory.entryInfoList(
                 QStringList() << QStringLiteral("*.json"),
                 QDir::Files | QDir::NoSymLinks)) {
            if (QFile::remove(file.absoluteFilePath()))
                ++removedFiles;
            else
                failures += QDir::toNativeSeparators(file.absoluteFilePath());
        }
    }
    mLocationSearchCachePath.clear();
    mProgress->setRange(0, 100);
    mProgress->setValue(0);
    if (failures.isEmpty()) {
        qInfo().noquote()
                << QStringLiteral("OSM cache cleared: %1 files, %2 bytes")
                   .arg(removedFiles).arg(cachedBytes);
        mStatus->setText(tr("Cleared %1 OpenStreetMap cache file(s).")
                         .arg(removedFiles));
        return;
    }
    mStatus->setText(tr("Cleared %1 cache file(s), but %2 could not be removed.")
                     .arg(removedFiles).arg(failures.size()));
    qWarning().noquote()
            << QStringLiteral("OSM cache clear incomplete: removed %1, failures %2")
               .arg(removedFiles).arg(failures.size());
    QMessageBox::warning(
                this, tr("OpenStreetMap Cache Not Fully Cleared"),
                tr("These cache files could not be removed:\n%1")
                .arg(failures.join(QLatin1Char('\n'))));
}

QString OsmTerrainImportDialog::cacheFilePath(const QString &query) const
{
    const QByteArray digest = QCryptographicHash::hash(
                query.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(PortableSettings::path(
                    QStringLiteral("cache/osm")))
            .filePath(QString::fromLatin1(digest)
                      + QStringLiteral(".json"));
}

void OsmTerrainImportDialog::saveSettings()
{
    QSettings settings;
    settings.setValue(QLatin1String("OSMImporter/Latitude"),
                      mLatitude->value());
    settings.setValue(QLatin1String("OSMImporter/Longitude"),
                      mLongitude->value());
    settings.setValue(QLatin1String("OSMImporter/MetersPerSquare"),
                      mMetersPerSquare->value());
    settings.setValue(
                QLatin1String("OSMImporter/RotationDegreesClockwise"),
                mRotation->value());
    settings.setValue(QLatin1String("OSMImporter/MarginPercent"),
                      mMarginPercent->value());
    settings.setValue(QLatin1String("OSMImporter/RoadWidthPercent"),
                      mRoadWidthPercent->value());
    settings.setValue(QLatin1String("OSMImporter/GenerateStreets"),
                      mGenerateStreets->isChecked());
    settings.setValue(QLatin1String("OSMImporter/GenerateInGameMap"),
                      mGenerateInGame->isChecked());
    settings.setValue(QLatin1String("OSMImporter/GenerateProxyBuildings"),
                      mGenerateProxyBuildings->isChecked());
    settings.setValue(QLatin1String("OSMImporter/GenerateRoadMarkings"),
                      mGenerateRoadMarkings->isChecked());
    settings.setValue(QLatin1String("OSMImporter/GenerateNavZones"),
                      mGenerateNavZones->isChecked());
    settings.setValue(QLatin1String("OSMImporter/GenerateForagingZones"),
                      mGenerateForagingZones->isChecked());
    settings.setValue(QLatin1String("OSMImporter/Endpoint"),
                      mEndpoint->text().trimmed());
    settings.setValue(QLatin1String("OSMImporter/GeocoderEndpoint"),
                      mGeocoderEndpoint->text().trimmed());
    if (!mWorldDocument) {
        settings.setValue(QLatin1String("OSMImporter/NewProjectGridFormat"),
                          mGridFormat->currentIndex());
        settings.setValue(QLatin1String("OSMImporter/NewProjectWidthKm"),
                          mAreaWidthKm->value());
        settings.setValue(QLatin1String("OSMImporter/NewProjectHeightKm"),
                          mAreaHeightKm->value());
        settings.setValue(QLatin1String("OSMImporter/NewProjectPath"),
                          projectFilePath());
    }
}

void OsmTerrainImportDialog::enrichMetadata(const QStringList &queries)
{
    QJsonDocument document = QJsonDocument::fromJson(
                mGenerated.sourceMetadata);
    QJsonObject object = document.object();
    object.insert(QStringLiteral("projectCellSize"),
                  currentCellSize());
    object.insert(QStringLiteral("projectGridFormat"),
                  worldGridFormatName(gridFormat()));
    object.insert(QStringLiteral("createsNewProject"),
                  createsNewProject());
    object.insert(QStringLiteral("originCellX"), mOriginX->value());
    object.insert(QStringLiteral("originCellY"), mOriginY->value());
    object.insert(QStringLiteral("cellsWide"), mCellsWide->value());
    object.insert(QStringLiteral("cellsHigh"), mCellsHigh->value());
    object.insert(QStringLiteral("overpassQuery"),
                  queries.isEmpty() ? QString() : queries.first());
    QJsonArray queryParts;
    for (const QString &query : queries)
        queryParts.append(query);
    object.insert(QStringLiteral("overpassQueries"), queryParts);
    object.insert(QStringLiteral("overpassQueryPartCount"), queries.size());
    object.insert(QStringLiteral("generateStreets"),
                  mGenerateStreets->isChecked());
    object.insert(QStringLiteral("generateInGameMapFeatures"),
                  mGenerateInGame->isChecked());
    object.insert(QStringLiteral("generateProxyBuildings"),
                  mGenerateProxyBuildings->isChecked());
    object.insert(QStringLiteral("generateRoadMarkings"),
                  mGenerateRoadMarkings->isChecked());
    object.insert(QStringLiteral("generateNavZones"),
                  mGenerateNavZones->isChecked());
    object.insert(QStringLiteral("generateForagingZones"),
                  mGenerateForagingZones->isChecked());
    mGenerated.sourceMetadata = QJsonDocument(object).toJson(
                QJsonDocument::Indented);
}
