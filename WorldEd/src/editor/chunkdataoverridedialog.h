/*
 * Copyright 2026, Alree / Unjammer
 *
 * This file is part of PZ Mapping Tools.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef CHUNKDATAOVERRIDEDIALOG_H
#define CHUNKDATAOVERRIDEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class WorldCell;
class WorldDocument;

class ChunkDataOverrideCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit ChunkDataOverrideCanvas(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    const QImage &image() const { return mImage; }
    void setImage(const QImage &image);
    void setPaintValue(quint8 value);
    void setInheritMode(bool inherit);
    void setBrushRadius(int radius);
    void setZoomPercent(int percent);

signals:
    void editStarted();
    void editFinished();
    void imageChanged();
    void pointerMoved(const QPoint &point);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPoint imagePoint(const QPoint &widgetPoint) const;
    bool paintSegment(const QPoint &from, const QPoint &to);
    bool paintDisc(const QPoint &center);
    void rebuildPreview();
    void updateCanvasSize();

    QImage mImage;
    QImage mPreview;
    QPoint mLastPoint;
    QPoint mHoverPoint;
    quint8 mPaintValue = 0;
    int mBrushRadius = 0;
    int mZoomPercent = 200;
    bool mInheritMode = false;
    bool mStrokeInherit = false;
    bool mPainting = false;
};

class ChunkDataOverrideDialog : public QDialog
{
    Q_OBJECT
public:
    ChunkDataOverrideDialog(WorldDocument *worldDocument,
                            WorldCell *cell,
                            QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void loadInitialImage();
    void loadImageFromFile();
    bool saveImage();
    bool saveImageAs();
    bool saveToPath(const QString &filePath);
    void detachOverride();
    void resetToAutomatic();
    void updatePaintValue();
    void updatePointerStatus(const QPoint &point);
    void beginEdit();
    void endEdit();
    void undo();
    void redo();
    void setDirty(bool dirty);
    bool confirmClose();
    void updateHistoryActions();

    WorldDocument *mWorldDocument;
    WorldCell *mCell;
    ChunkDataOverrideCanvas *mCanvas;
    QLineEdit *mPath;
    QLabel *mValueLabel;
    QLabel *mPointerStatus;
    QRadioButton *mExplicitValue;
    QRadioButton *mAutomaticValue;
    QCheckBox *mSolid;
    QCheckBox *mWallNorth;
    QCheckBox *mWallWest;
    QCheckBox *mWater;
    QCheckBox *mRoom;
    QSpinBox *mBrushRadius;
    QSpinBox *mZoom;
    QPushButton *mUndo;
    QPushButton *mRedo;
    QPushButton *mSave;
    QString mCurrentPath;
    QImage mSavedImage;
    QImage mPendingImage;
    QList<QImage> mUndoImages;
    QList<QImage> mRedoImages;
    bool mDirty = false;
    bool mEditActive = false;
};

#endif
