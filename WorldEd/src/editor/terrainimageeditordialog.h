#ifndef TERRAINIMAGEEDITORDIALOG_H
#define TERRAINIMAGEEDITORDIALOG_H
#include <QColor>
#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QWidget>
class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QSpinBox;
class QToolButton;
class WorldDocument;
struct TerrainPaletteEntry
{
    QString label;
    QColor color;
    int bitmapIndex = 0;
};
class TerrainImageCanvas : public QWidget
{
    Q_OBJECT
public:
    enum Tool {
        BrushTool,
        FillTool
    };
    explicit TerrainImageCanvas(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    const QImage &groundImage() const { return mGroundImage; }
    const QImage &vegetationImage() const { return mVegetationImage; }
    int cellSize() const { return mCellSize; }
    void setImages(const QImage &ground, const QImage &vegetation,
                   int cellSize);
    void setActiveLayer(int bitmapIndex);
    void setPaintColor(const QColor &color);
    void setBrushRadius(int radius);
    void setZoomPercent(int percent);
    void setTool(int tool);
    void setCompositePreview(bool composite);
    void setVegetationOpacity(int percent);
    void replaceImages(const QImage &ground, const QImage &vegetation);
signals:
    void editStarted(const QString &label);
    void editFinished();
    void imageChanged();
    void colorPicked(const QColor &color);
    void pointerMoved(const QPoint &point);
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
private:
    QPoint imagePoint(const QPoint &widgetPoint) const;
    bool containsImagePoint(const QPoint &point) const;
    void paintSegment(const QPoint &from, const QPoint &to);
    void floodFill(const QPoint &point);
    void rebuildVegetationOverlay(const QRect &area = QRect());
    void updateCanvasSize();
    QImage mGroundImage;
    QImage mVegetationImage;
    QImage mVegetationOverlay;
    QColor mPaintColor;
    QPoint mLastPoint;
    QPoint mHoverPoint;
    int mCellSize = 300;
    int mActiveLayer = 0;
    int mBrushRadius = 4;
    int mZoomPercent = 100;
    int mTool = BrushTool;
    int mVegetationOpacity = 70;
    bool mPainting = false;
    bool mCompositePreview = true;
};
class TerrainImageEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TerrainImageEditorDialog(WorldDocument *worldDocument,
                                      QWidget *parent = nullptr);
    bool importImages(const QImage &ground, const QImage &vegetation,
                      const QPoint &cellOrigin,
                      const QString &suggestedGroundPath,
                      const QByteArray &sourceMetadata = QByteArray());
    bool saveImportedImages();
    static bool ensureWorkingImageMemoryLimit(
            QWidget *parent, const QSize &size,
            const QString &operation, int cellSize);
    static int recommendedWorkingImageMemoryLimitMiB(const QSize &size);
protected:
    void closeEvent(QCloseEvent *event) override;
private slots:
    void browseRules();
    void reloadRules();
    void newImages();
    void openImages();
    bool saveImages();
    bool saveImagesAs();
    void activeLayerChanged(int bitmapIndex);
    void paletteChanged(int index);
    void pickedColor(const QColor &color);
    void eraseVegetationToggled(bool checked);
    void undoImageEdit();
    void redoImageEdit();
    void updateHistoryActions();
    void generateTerrainPatches();
    void generateVegetation();
    void generateRiver();
    void generateLake();
    void generateRoads();
    void updatePointerStatus(const QPoint &point);
private:
    QString defaultRulesPath() const;
    QString defaultGroundPath() const;
    QString normalizedGroundPath(const QString &path) const;
    QString vegetationPathFor(const QString &groundPath) const;
    bool loadRulesFile(const QString &path);
    bool loadImagePair(const QString &groundPath);
    bool validateWorkingImageSize(const QSize &size,
                                  const QString &operation);
    bool maybeDiscardChanges();
    bool validateAttachment(QString *error) const;
    bool attachImagesToWorld(const QString &groundPath, QString *error);
    void beginImageEdit(const QString &label);
    void endImageEdit();
    void trimHistoryToBudget();
    void resetHistory(bool dirty);
    void restoreHistoryState(const QImage &ground,
                             const QImage &vegetation);
    void populatePalette(int bitmapIndex);
    void setDirty(bool dirty);
    void updateImageStatus();
    QColor featureColor(int bitmapIndex, const QStringList &keywords,
                        const QColor &fallback) const;
    void applyGeneratedImages(const QImage &ground,
                              const QImage &vegetation,
                              const QString &label);
    struct HistoryState
    {
        QImage ground;
        QImage vegetation;
        QString label;
        int revision = 0;
    };
    WorldDocument *mWorldDocument;
    TerrainImageCanvas *mCanvas;
    QLineEdit *mRulesPath;
    QLineEdit *mGroundPath;
    QComboBox *mPalette;
    QLabel *mImageStatus;
    QLabel *mPointerStatus;
    QSpinBox *mCellsWide;
    QSpinBox *mCellsHigh;
    QSpinBox *mBrushRadius;
    QSpinBox *mZoom;
    QSpinBox *mSeed;
    QSpinBox *mFeatureWidth;
    QSpinBox *mFeatureCount;
    QSpinBox *mDensity;
    QSpinBox *mOriginX;
    QSpinBox *mOriginY;
    QCheckBox *mAttachToProject;
    QToolButton *mEraserButton;
    QPushButton *mUndoButton;
    QPushButton *mRedoButton;
    QList<TerrainPaletteEntry> mEntries;
    QList<HistoryState> mUndoHistory;
    QList<HistoryState> mRedoHistory;
    HistoryState mPendingHistory;
    QColor mLayerColors[2];
    int mActiveLayer = 0;
    int mRevision = 0;
    int mSavedRevision = 0;
    int mNextRevision = 1;
    bool mHistoryEditActive = false;
    bool mDirty = false;
    QByteArray mSourceMetadata;
};
#endif
