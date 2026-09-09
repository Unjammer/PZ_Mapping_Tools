#ifndef TILESETZEDITOR_H
#define TILESETZEDITOR_H

#include <QMainWindow>
#include <QImage>
#include <memory>
#include "tilesetz.h"

class QComboBox;
class QFormLayout;
class QCheckBox;
class QLabel;
class QListView;
class QPushButton;
class QSpinBox;
class QUndoStack;
class QTabWidget;
class TileSetZCanvas;
class TileSetZModel;
class TileSetZPreview;

namespace Tiled { class Tileset; }

class TileSetZEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit TileSetZEditor(QWidget *parent = nullptr);
    ~TileSetZEditor() override;
    bool openTileset(Tiled::Tileset *tileset, int tileId);
    static bool runSelfTest(QString *error);

signals:
    void imageSaved(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void newSheet();
    void openSheet();
    bool loadSheet(const QString &path, int scale, int tileId, const QImage &loaded = QImage());
    void setDocument(const QImage &image, int scale, const QString &path);
    bool maybeSave();
    bool save(bool saveAs = false);
    void selectTile(int id);
    void updateDocument();
    void changeImage(const QImage &image, const QString &text);
    void changeTile(const QImage &image, const QString &text);
    void pushChange(const QImage &before, const QImage &after, const QPoint &position,
                    bool wholeSheet, const QString &text);
    void loadTexture();
    void loadMask();
    void preview();
    void invalidatePreview();
    void refreshPreview();
    void exportWall();
    void insertPreview(bool append);
    void usePreview();
    void setPaintColor(const QColor &color);
    void showError(const QString &message);
    int tileCount() const;

    QImage mImage;
    QImage mSource;
    QImage mMask;
    QImage mPreviewImage;
    QString mFile;
    QString mSourceFile;
    QByteArray mDiskHash;
    int mScale = 2;
    int mSelected = 0;
    QColor mPaintColor = Qt::white;
    QColor mTrimColor = QColor(99, 99, 99);
    std::shared_ptr<qint64> mUndoBytes;
    QUndoStack *mUndo;
    TileSetZModel *mModel;
    QListView *mTiles;
    TileSetZCanvas *mCanvas;
    TileSetZPreview *mPreview;
    QTabWidget *mWorkspace;
    QComboBox *mPreviewMode;
    QComboBox *mMaskPreview;
    QSpinBox *mPreviewTile;
    QCheckBox *mIsolateTile;
    QPushButton *mFitPreviewTile;
    QCheckBox *mPreviewGrid;
    QLabel *mWallLayout;
    QComboBox *mWallDirection;
    QPushButton *mExportWall;
    QSpinBox *mOpacity;
    TileSetZ::WallResult mWall;
    QComboBox *mMethod;
    QFormLayout *mGeneratorForm;
    QComboBox *mProjection;
    QComboBox *mMaskChannel;
    QComboBox *mScaleChoice;
    QSpinBox *mOutputColumns;
    QLabel *mSourceLabel;
    QLabel *mMaskLabel;
    QLabel *mPreviewLabel;
    QLabel *mTileLabel;
    QPushButton *mPaintButton;
    QPushButton *mTrimButton;
    QPushButton *mAppendButton;
    QPushButton *mInsertButton;
    QPushButton *mUseButton;
    QPushButton *mMaskButton;
};

#endif
