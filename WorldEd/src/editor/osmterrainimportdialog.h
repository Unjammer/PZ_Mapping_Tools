#ifndef OSMTERRAINIMPORTDIALOG_H
#define OSMTERRAINIMPORTDIALOG_H
#include "osmterrainimporter.h"
#include "worldgeometry.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QStringList>
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTimer;
class QUrl;
class WorldDocument;
struct OsmTerrainGenerationTaskResult
{
    bool success = false;
    OsmTerrainImportResult generated;
    QString error;
};
template<typename T> class QFutureWatcher;
class OsmTerrainImportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OsmTerrainImportDialog(
            WorldDocument *worldDocument,
            QWidget *parent = nullptr);
    ~OsmTerrainImportDialog() override;
    const QImage &groundImage() const
    { return mGenerated.groundImage; }
    const QImage &vegetationImage() const
    { return mGenerated.vegetationImage; }
    QByteArray sourceMetadata() const
    { return mGenerated.sourceMetadata; }
    const OsmTerrainImportResult &generatedResult() const
    { return mGenerated; }
    bool createsNewProject() const
    { return mWorldDocument == nullptr; }
    QPoint cellOrigin() const;
    QSize projectSize() const;
    WorldGridFormat gridFormat() const;
    QString projectFilePath() const;
    QString suggestedGroundPath() const;
    bool generatesStreets() const;
    bool generatesInGameMapFeatures() const;
    bool generatesProxyBuildings() const;
    bool generatesRoadMarkings() const;
    bool generatesNavZones() const;
    bool generatesForagingZones() const;
private slots:
    void browseProject();
    void browseOutput();
    void applyLocationInput();
    void openLocationMap();
    void locationLinkFinished();
    void useSelectedCells();
    void useEntireProject();
    void scalePresetChanged(int index);
    void detectRoadOrientation();
    void startImport();
    void cancelImport();
    void networkFinished();
    void networkTimeout();
    void generationFinished();
    void clearOsmCache();
    void resetParameters();
    void updateGeometrySummary();
    void invalidateGeneratedResult();
    void previewZoomChanged(int percent);
private:
    OsmTerrainImportOptions options() const;
    bool validateOptions(const OsmTerrainImportOptions &options);
    void applyLocation(double latitude, double longitude,
                       const QString &source);
    void setProjectArea(const QRect &area, const QString &source);
    QRect selectedCellArea() const;
    void resolveLocationLink(const QUrl &url);
    void startGeneration(const QList<QByteArray> &chunks, bool fromCache);
    void startNextImportChunk();
    void setBusy(bool busy, const QString &status = QString());
    QString cacheFilePath(const QString &query) const;
    void saveSettings();
    void enrichMetadata(const QStringList &queries);
    int currentCellSize() const;
    void updateStandaloneProjectSize();
    void updateStandaloneAreaFromCells();
    void renderAreaPreview();
    void searchLocationName(const QString &query);
    bool applyGeocodingResponse(const QByteArray &json,
                                const QString &source);
    void startOverpassRequest();
    void beginOverpassRequest(const QString &query, bool orientationOnly);
    bool hasAnotherOverpassEndpoint() const;
    enum LocationRequestKind
    {
        NoLocationRequest,
        LinkLocationRequest,
        GeocoderLocationRequest
    };
    WorldDocument *mWorldDocument;
    QLineEdit *mLocationInput;
    QDoubleSpinBox *mLatitude;
    QDoubleSpinBox *mLongitude;
    QSpinBox *mOriginX;
    QSpinBox *mOriginY;
    QSpinBox *mCellsWide;
    QSpinBox *mCellsHigh;
    QDoubleSpinBox *mAreaWidthKm;
    QDoubleSpinBox *mAreaHeightKm;
    QComboBox *mGridFormat;
    QDoubleSpinBox *mMetersPerSquare;
    QComboBox *mScalePreset;
    QDoubleSpinBox *mRotation;
    QSpinBox *mMarginPercent;
    QSpinBox *mRoadWidthPercent;
    QCheckBox *mLand;
    QCheckBox *mWater;
    QCheckBox *mRoads;
    QCheckBox *mGenerateStreets;
    QCheckBox *mGenerateInGame;
    QCheckBox *mGenerateProxyBuildings;
    QCheckBox *mGenerateRoadMarkings;
    QCheckBox *mGenerateNavZones;
    QCheckBox *mGenerateForagingZones;
    QCheckBox *mUseCache;
    QPushButton *mClearCacheButton;
    QPushButton *mResetParametersButton;
    QLineEdit *mEndpoint;
    QLineEdit *mGeocoderEndpoint;
    QLineEdit *mProjectPath;
    QLineEdit *mOutputPath;
    QLabel *mGeometrySummary;
    QLabel *mProjectFormat;
    QLabel *mScaleExplanation;
    QLabel *mPreview;
    QScrollArea *mPreviewScroll;
    QSpinBox *mPreviewZoom;
    QLabel *mStatus;
    QProgressBar *mProgress;
    QPushButton *mGenerateButton;
    QPushButton *mCancelButton;
    QPushButton *mOpenButton;
    QPushButton *mApplyLocationButton;
    QPushButton *mOpenMapButton;
    QPushButton *mDetectOrientationButton;
    QNetworkAccessManager *mNetwork;
    QNetworkReply *mReply;
    QNetworkReply *mLocationReply;
    LocationRequestKind mLocationRequestKind;
    QString mLocationSearchCachePath;
    QElapsedTimer mGeocodingRateLimit;
    QTimer *mTimeout;
    QTimer *mRetryTimer;
    QFutureWatcher<OsmTerrainGenerationTaskResult> *mWatcher;
    OsmTerrainImportResult mGenerated;
    QImage mRenderedPreview;
    QString mActiveQuery;
    QStringList mActiveQueries;
    QList<QByteArray> mResponseChunks;
    QStringList mOverpassEndpoints;
    QStringList mOverpassFailures;
    QElapsedTimer mRequestElapsed;
    QElapsedTimer mWorkflowElapsed;
    QElapsedTimer mGenerationElapsed;
    int mActiveQueryIndex;
    int mOverpassEndpointIndex;
    int mRateLimitRetryCount;
    int mTransientRetryCount;
    bool mOrientationOnlyRequest;
    bool mAllResponsesFromCache;
    bool mRetryCurrentEndpoint;
    bool mCancelRequested;
    bool mRequestTimedOut;
};
#endif
