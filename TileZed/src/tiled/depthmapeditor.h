/*
 * Project Zomboid depth-map atlas editor.
 *
 * Depth atlases use the Build 42 format: eight 128x256 tiles per row.
 * Transparent pixels are undefined; opaque grayscale pixels store depth.
 */

#ifndef DEPTHMAPEDITOR_H
#define DEPTHMAPEDITOR_H

#include "depthgeometry.h"

#include <QImage>
#include <QMainWindow>
#include <QVector>
#include <QWidget>

class QAction;
class QButtonGroup;
class QCloseEvent;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

namespace Tiled {
class Tile;
class Tileset;
}

class DepthMapCanvas : public QWidget
{
    Q_OBJECT

public:
    enum Tool {
        PaintTool,
        EraseTool,
        PickTool
    };

    explicit DepthMapCanvas(QWidget *parent = nullptr);

    void setImages(const QImage &source, const QImage &depth);
    void setDepthImage(const QImage &depth);
    const QImage &depthImage() const { return mDepth; }
    const QImage &sourceImage() const { return mSource; }

    void setTool(Tool tool) { mTool = tool; }
    void setBrushDepth(int depth) { mBrushDepth = qBound(0, depth, 255); }
    void setBrushRadius(int radius) { mBrushRadius = qBound(1, radius, 32); }
    void setZoom(int zoom);
    void setGeometry(const QVector<DepthPrimitive> &geometry,
                     int selectedIndex);
    void setGeometryEditing(bool enabled) { mGeometryEditing = enabled; }

signals:
    void editStarted(const QImage &before);
    void editFinished(const QImage &after);
    void depthPicked(int depth);
    void cursorPixelChanged(int x, int y, int depth, bool defined);
    void geometryPicked(int index);
    void geometryTranslated(int index, const QVector3D &delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPoint imagePoint(const QPoint &widgetPoint) const;
    void beginStroke(const QPoint &point, Qt::MouseButton button);
    void continueStroke(const QPoint &point);
    void applyBrush(const QPoint &point);
    void rebuildOverlay();
    void reportCursor(const QPoint &point);
    int pickGeometry(const QPointF &imagePoint) const;
    QVector3D geometryDragDelta(const QPointF &from,
                                const QPointF &to) const;

    QImage mSource;
    QImage mDepth;
    QImage mOverlay;
    QVector<DepthPrimitive> mGeometry;
    int mSelectedGeometry = -1;
    Tool mTool = PaintTool;
    int mBrushDepth = 128;
    int mBrushRadius = 2;
    int mZoom = 2;
    bool mStrokeActive = false;
    bool mStrokeChanged = false;
    bool mTemporaryErase = false;
    QPoint mLastPoint;
    bool mGeometryEditing = true;
    bool mGeometryDragging = false;
    QPointF mLastGeometryPoint;
};

class DepthMapEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit DepthMapEditor(QWidget *parent = nullptr);
    ~DepthMapEditor() override;

    bool setTileset(Tiled::Tileset *tileset, int tileId = -1,
                    bool loadExternalGeometry = true);
    static bool runFormatSelfTest(QString *error);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseTileset();
    void chooseDepthDirectory();
    void openDepthFile();
    bool saveDepthFile();
    bool saveDepthFileAs();
    void reloadDepthFile();
    void chooseGeometryFile();

    void currentCellChanged(int currentRow, int currentColumn,
                            int previousRow, int previousColumn);
    void canvasEditStarted(const QImage &before);
    void canvasEditFinished(const QImage &after);
    void setPickedDepth(int depth);

    void clearCurrentTile();
    void fillSourceMask();
    void seedVerticalDepth();
    void copyCurrentDepth();
    void pasteDepth();
    void undo();
    void redo();
    void addPolygonXY();
    void addPolygonXZ();
    void addPolygonYZ();
    void addBox();
    void addCylinder();
    void duplicateGeometry();
    void removeGeometry();
    void geometrySelectionChanged();
    void geometryValuesChanged();
    void generateSelectedGeometry();
    void generateAllGeometry();
    void canvasGeometryPicked(int index);
    void canvasGeometryTranslated(int index, const QVector3D &delta);

private:
    struct Edit {
        int tileId = -1;
        QImage before;
        QImage after;
        int beforeRevision = 0;
        int afterRevision = 0;
    };

    static const int DepthTileWidth = 128;
    static const int DepthTileHeight = 256;
    static const int DepthColumns = 8;

    bool confirmDiscardChanges();
    bool loadExpectedDepthFile();
    bool loadDepthFile(const QString &filePath);
    bool loadExpectedGeometryFile();
    bool loadGeometryFile(const QString &filePath);
    bool saveGeometryFile();
    bool saveGeometryFileAs();
    bool initialiseAtlas(const QImage &sourceImage, QString *error);
    QString expectedFileName() const;
    QString discoverDepthDirectory() const;
    QString discoverGeometryFile() const;
    QString tilesetBaseName() const;
    QSize expectedAtlasSize() const;

    void buildTileTable(int selectedTileId);
    void selectTile(int tileId);
    int tileIdAt(int row, int column) const;
    int currentTileId() const;
    QImage sourceTileImage(int tileId) const;
    QImage depthTileImage(int tileId) const;
    void setDepthTileImage(int tileId, const QImage &image);
    bool tileHasDepth(int tileId) const;
    void updateTileIcon(int tileId);
    void updateCurrentTile();
    void updateGeometryList(int selectedIndex = -1);
    void updateGeometryControls();
    void updateCanvasGeometry();
    QVector<DepthPrimitive> &currentGeometry();
    const QVector<DepthPrimitive> currentGeometryValue() const;
    int selectedGeometryIndex() const;
    void addGeometry(const DepthPrimitive &primitive);
    void setGeometryDirty();
    void updateWindowState();
    void updateActions();
    void setDirtyRevision(int revision);
    void pushEdit(int tileId, const QImage &before, const QImage &after);
    void applyEditImage(int tileId, const QImage &image);
    void commitCurrentOperation(const QImage &after);

    Tiled::Tileset *mTileset = nullptr;
    QImage mAtlas;
    QString mFilePath;
    QString mDirectory;
    DepthGeometryDocument mGeometryDocument;
    QString mGeometryFilePath;
    bool mGeometryDirty = false;

    QLineEdit *mDirectoryEdit = nullptr;
    QLabel *mTilesetLabel = nullptr;
    QLabel *mFileLabel = nullptr;
    QLabel *mGeometryFileLabel = nullptr;
    QLabel *mTileLabel = nullptr;
    QLabel *mCursorLabel = nullptr;
    QTableWidget *mTileTable = nullptr;
    DepthMapCanvas *mCanvas = nullptr;
    QSplitter *mMainSplitter = nullptr;
    QScrollArea *mCanvasScrollArea = nullptr;
    QSlider *mDepthSlider = nullptr;
    QSpinBox *mDepthSpin = nullptr;
    QSpinBox *mBrushSpin = nullptr;
    QComboBox *mZoomCombo = nullptr;
    QTabWidget *mModeTabs = nullptr;
    QButtonGroup *mToolGroup = nullptr;
    QPushButton *mPasteButton = nullptr;
    QListWidget *mGeometryList = nullptr;
    QDoubleSpinBox *mTranslateSpins[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox *mRotateSpins[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox *mMinimumSpins[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox *mMaximumSpins[3] = { nullptr, nullptr, nullptr };
    QDoubleSpinBox *mRadiusSpins[2] = { nullptr, nullptr };
    QDoubleSpinBox *mHeightSpin = nullptr;
    QComboBox *mPlaneCombo = nullptr;
    QPlainTextEdit *mPointsEdit = nullptr;
    QStackedWidget *mShapeStack = nullptr;
    QCheckBox *mRespectAlphaCheck = nullptr;

    QAction *mSaveAction = nullptr;
    QAction *mSaveAsAction = nullptr;
    QAction *mReloadAction = nullptr;
    QAction *mUndoAction = nullptr;
    QAction *mRedoAction = nullptr;

    QImage mStrokeBefore;
    QImage mDepthClipboard;
    QVector<Edit> mEdits;
    int mEditIndex = 0;
    int mRevision = 0;
    int mSavedRevision = 0;
    int mNextRevision = 1;
    bool mUpdatingGeometryUi = false;
};

#endif // DEPTHMAPEDITOR_H
