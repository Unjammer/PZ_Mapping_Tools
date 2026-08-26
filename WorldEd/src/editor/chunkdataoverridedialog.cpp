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

#include "chunkdataoverridedialog.h"

#include "chunkdataoverride.h"
#include "worldcell.h"
#include "worlddocument.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtMath>

ChunkDataOverrideCanvas::ChunkDataOverrideCanvas(QWidget *parent)
    : QWidget(parent)
    , mImage(ChunkDataOverride::ImageSize,
             ChunkDataOverride::ImageSize,
             QImage::Format_ARGB32)
    , mHoverPoint(-1, -1)
{
    mImage.fill(Qt::transparent);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    rebuildPreview();
    updateCanvasSize();
}

QSize ChunkDataOverrideCanvas::sizeHint() const
{
    const qreal scale = mZoomPercent / 100.0;
    return QSize(qRound(ChunkDataOverride::ImageSize * scale),
                 qRound(ChunkDataOverride::ImageSize * scale));
}

void ChunkDataOverrideCanvas::setImage(const QImage &image)
{
    mImage = image.convertToFormat(QImage::Format_ARGB32);
    rebuildPreview();
    update();
}

void ChunkDataOverrideCanvas::setPaintValue(quint8 value)
{
    mPaintValue = value & ChunkDataOverride::SupportedMask;
}

void ChunkDataOverrideCanvas::setInheritMode(bool inherit)
{
    mInheritMode = inherit;
}

void ChunkDataOverrideCanvas::setBrushRadius(int radius)
{
    mBrushRadius = qBound(0, radius, 16);
}

void ChunkDataOverrideCanvas::setZoomPercent(int percent)
{
    mZoomPercent = qBound(50, percent, 800);
    updateCanvasSize();
    update();
}

void ChunkDataOverrideCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 33, 38));
    const int checker = qMax(4, qRound(4 * mZoomPercent / 100.0));
    for (int y = 0; y < height(); y += checker) {
        for (int x = 0; x < width(); x += checker) {
            if (((x / checker) + (y / checker)) & 1)
                painter.fillRect(x, y, checker, checker, QColor(43, 47, 53));
        }
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(QPoint(), sizeHint()), mPreview);

    const qreal scale = mZoomPercent / 100.0;
    painter.setPen(QPen(QColor(255, 255, 255, 95), 1));
    for (int value = 0; value <= ChunkDataOverride::ImageSize; value += 8) {
        const int coordinate = qRound(value * scale);
        painter.drawLine(coordinate, 0, coordinate, height());
        painter.drawLine(0, coordinate, width(), coordinate);
    }
    painter.setPen(QPen(QColor(255, 255, 255, 185), 2));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (mImage.rect().contains(mHoverPoint)) {
        painter.setPen(QPen(Qt::white, 1));
        painter.setBrush(Qt::NoBrush);
        const QRectF hoverRect((mHoverPoint.x() - mBrushRadius) * scale,
                               (mHoverPoint.y() - mBrushRadius) * scale,
                               (mBrushRadius * 2 + 1) * scale,
                               (mBrushRadius * 2 + 1) * scale);
        painter.drawRect(hoverRect);
    }
}

void ChunkDataOverrideCanvas::mousePressEvent(QMouseEvent *event)
{
    const QPoint point = imagePoint(event->pos());
    if (!mImage.rect().contains(point)
            || (event->button() != Qt::LeftButton
                && event->button() != Qt::RightButton))
        return;
    mStrokeInherit = event->button() == Qt::RightButton || mInheritMode;
    mPainting = true;
    mLastPoint = point;
    emit editStarted();
    if (paintSegment(point, point))
        emit imageChanged();
}

void ChunkDataOverrideCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint point = imagePoint(event->pos());
    mHoverPoint = point;
    emit pointerMoved(point);
    if (mPainting && mImage.rect().contains(point)) {
        if (paintSegment(mLastPoint, point))
            emit imageChanged();
        mLastPoint = point;
    }
    update();
}

void ChunkDataOverrideCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (mPainting && (event->button() == Qt::LeftButton
                      || event->button() == Qt::RightButton)) {
        mPainting = false;
        emit editFinished();
    }
}

void ChunkDataOverrideCanvas::leaveEvent(QEvent *)
{
    mHoverPoint = QPoint(-1, -1);
    emit pointerMoved(mHoverPoint);
    update();
}

QPoint ChunkDataOverrideCanvas::imagePoint(const QPoint &widgetPoint) const
{
    const qreal scale = mZoomPercent / 100.0;
    return QPoint(qFloor(widgetPoint.x() / scale),
                  qFloor(widgetPoint.y() / scale));
}

bool ChunkDataOverrideCanvas::paintSegment(const QPoint &from,
                                           const QPoint &to)
{
    const int steps = qMax(qAbs(to.x() - from.x()),
                           qAbs(to.y() - from.y()));
    bool changed = false;
    for (int step = 0; step <= steps; ++step) {
        const qreal ratio = steps == 0 ? 0.0 : qreal(step) / steps;
        const QPoint point(qRound(from.x() + (to.x() - from.x()) * ratio),
                           qRound(from.y() + (to.y() - from.y()) * ratio));
        changed = paintDisc(point) || changed;
    }
    if (changed) {
        rebuildPreview();
        update();
    }
    return changed;
}

bool ChunkDataOverrideCanvas::paintDisc(const QPoint &center)
{
    const QRgb replacement = mStrokeInherit
            ? qRgba(0, 0, 0, 0)
            : qRgba(mPaintValue, 0, 0, 255);
    bool changed = false;
    for (int y = center.y() - mBrushRadius;
         y <= center.y() + mBrushRadius; ++y) {
        for (int x = center.x() - mBrushRadius;
             x <= center.x() + mBrushRadius; ++x) {
            if (!mImage.rect().contains(x, y))
                continue;
            const int dx = x - center.x();
            const int dy = y - center.y();
            if (dx * dx + dy * dy > mBrushRadius * mBrushRadius)
                continue;
            if (mImage.pixel(x, y) == replacement)
                continue;
            mImage.setPixel(x, y, replacement);
            changed = true;
        }
    }
    return changed;
}

void ChunkDataOverrideCanvas::rebuildPreview()
{
    mPreview = QImage(mImage.size(), QImage::Format_ARGB32_Premultiplied);
    mPreview.fill(Qt::transparent);
    for (int y = 0; y < mImage.height(); ++y) {
        QRgb *target = reinterpret_cast<QRgb *>(mPreview.scanLine(y));
        for (int x = 0; x < mImage.width(); ++x) {
            const QRgb source = mImage.pixel(x, y);
            if (qAlpha(source) == 0) {
                target[x] = qRgba(0, 0, 0, 0);
                continue;
            }
            const QColor color = ChunkDataOverride::valueColor(
                        quint8(qRed(source)));
            target[x] = color.rgba();
        }
    }
}

void ChunkDataOverrideCanvas::updateCanvasSize()
{
    setFixedSize(sizeHint());
    updateGeometry();
}

ChunkDataOverrideDialog::ChunkDataOverrideDialog(
        WorldDocument *worldDocument, WorldCell *cell, QWidget *parent)
    : QDialog(parent)
    , mWorldDocument(worldDocument)
    , mCell(cell)
    , mCanvas(new ChunkDataOverrideCanvas(this))
    , mPath(new QLineEdit(this))
    , mValueLabel(new QLabel(this))
    , mPointerStatus(new QLabel(this))
    , mExplicitValue(new QRadioButton(tr("Paint explicit value"), this))
    , mAutomaticValue(new QRadioButton(tr("Restore automatic generation"), this))
    , mSolid(new QCheckBox(tr("Solid, value 1"), this))
    , mWallNorth(new QCheckBox(tr("North wall, value 2"), this))
    , mWallWest(new QCheckBox(tr("West wall, value 4"), this))
    , mWater(new QCheckBox(tr("Water, value 8"), this))
    , mRoom(new QCheckBox(tr("Room, value 16"), this))
    , mBrushRadius(new QSpinBox(this))
    , mZoom(new QSpinBox(this))
    , mUndo(new QPushButton(tr("Undo"), this))
    , mRedo(new QPushButton(tr("Redo"), this))
    , mSave(new QPushButton(tr("Save Override"), this))
{
    setWindowTitle(tr("Native256 Chunk Data Overrides"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(1120, 820);

    QVBoxLayout *root = new QVBoxLayout(this);
    QLabel *introduction = new QLabel(
                tr("Edit the selected Native256 cell before Generate Lots. "
                   "Transparent pixels inherit WorldEd's generated value. "
                   "Opaque pixels replace it. One image pixel equals one "
                   "map square. Right-drag always restores automatic generation."),
                this);
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    QHBoxLayout *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(new QLabel(tr("Override PNG:"), this));
    mPath->setReadOnly(true);
    pathLayout->addWidget(mPath, 1);
    QPushButton *load = new QPushButton(tr("Load PNG..."), this);
    QPushButton *saveAs = new QPushButton(tr("Save As..."), this);
    QPushButton *detach = new QPushButton(tr("Detach"), this);
    load->setToolTip(tr("Load and validate an RGBA chunk data override image"));
    saveAs->setToolTip(tr("Save this cell's override to another project file"));
    detach->setToolTip(tr("Stop using the current file without deleting it"));
    pathLayout->addWidget(load);
    pathLayout->addWidget(saveAs);
    pathLayout->addWidget(detach);
    root->addLayout(pathLayout);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    QScrollArea *scrollArea = new QScrollArea(splitter);
    scrollArea->setWidget(mCanvas);
    scrollArea->setWidgetResizable(false);
    scrollArea->setAlignment(Qt::AlignCenter);
    splitter->addWidget(scrollArea);

    QWidget *controls = new QWidget(splitter);
    QVBoxLayout *controlsLayout = new QVBoxLayout(controls);

    QGroupBox *paintGroup = new QGroupBox(tr("Paint value"), controls);
    QVBoxLayout *paintLayout = new QVBoxLayout(paintGroup);
    mExplicitValue->setChecked(true);
    mSolid->setChecked(true);
    paintLayout->addWidget(mExplicitValue);
    paintLayout->addWidget(mAutomaticValue);
    paintLayout->addSpacing(6);
    paintLayout->addWidget(mSolid);
    paintLayout->addWidget(mWallNorth);
    paintLayout->addWidget(mWallWest);
    paintLayout->addWidget(mWater);
    paintLayout->addWidget(mRoom);
    mValueLabel->setWordWrap(true);
    paintLayout->addWidget(mValueLabel);
    controlsLayout->addWidget(paintGroup);

    QGroupBox *viewGroup = new QGroupBox(tr("Brush and view"), controls);
    QFormLayout *viewLayout = new QFormLayout(viewGroup);
    mBrushRadius->setRange(0, 16);
    mBrushRadius->setValue(0);
    mBrushRadius->setSuffix(tr(" squares"));
    mZoom->setRange(50, 800);
    mZoom->setSingleStep(50);
    mZoom->setValue(200);
    mZoom->setSuffix(QLatin1String(" %"));
    viewLayout->addRow(tr("Brush radius:"), mBrushRadius);
    viewLayout->addRow(tr("Zoom:"), mZoom);
    controlsLayout->addWidget(viewGroup);

    QGroupBox *effectsGroup = new QGroupBox(
                tr("Values consumed by the game"), controls);
    QVBoxLayout *effectsLayout = new QVBoxLayout(effectsGroup);
    QLabel *effects = new QLabel(
                tr("<b>0</b> clears all five collision metadata flags.<br>"
                   "<b>1 Solid</b> marks the whole square as solid for the "
                   "native collision and path system.<br>"
                   "<b>2 North wall</b> blocks the north edge. The game "
                   "derives it from north collision, door-frame, door-wall, "
                   "and window flags unless the edge is hoppable.<br>"
                   "<b>4 West wall</b> applies the equivalent rule to the "
                   "west edge.<br>"
                   "<b>8 Water</b> marks the square as water in the native "
                   "collision data.<br>"
                   "<b>16 Room</b> marks the square as belonging to a room."
                   "<br><br>Values combine by addition, from 0 through 31. "
                   "Loaded chunks later refresh these flags from their real "
                   "level-0 squares. Omitted Partial Chunks remain absent "
                   "and ignore override pixels."), effectsGroup);
    effects->setWordWrap(true);
    effects->setTextFormat(Qt::RichText);
    effectsLayout->addWidget(effects);
    controlsLayout->addWidget(effectsGroup, 1);

    QHBoxLayout *historyLayout = new QHBoxLayout;
    historyLayout->addWidget(mUndo);
    historyLayout->addWidget(mRedo);
    QPushButton *reset = new QPushButton(
                tr("Reset All to Automatic"), controls);
    historyLayout->addWidget(reset);
    controlsLayout->addLayout(historyLayout);
    splitter->addWidget(controls);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    mPointerStatus->setText(tr("Move over the image to inspect a square."));
    root->addWidget(mPointerStatus);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close,
                                                     this);
    buttons->addButton(mSave, QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    const QList<QCheckBox *> flags = {
        mSolid, mWallNorth, mWallWest, mWater, mRoom
    };
    for (QCheckBox *flag : flags)
        connect(flag, &QCheckBox::toggled,
                this, &ChunkDataOverrideDialog::updatePaintValue);
    connect(mExplicitValue, &QRadioButton::toggled,
            this, &ChunkDataOverrideDialog::updatePaintValue);
    connect(mBrushRadius, qOverload<int>(&QSpinBox::valueChanged),
            mCanvas, &ChunkDataOverrideCanvas::setBrushRadius);
    connect(mZoom, qOverload<int>(&QSpinBox::valueChanged),
            mCanvas, &ChunkDataOverrideCanvas::setZoomPercent);
    connect(mCanvas, &ChunkDataOverrideCanvas::editStarted,
            this, &ChunkDataOverrideDialog::beginEdit);
    connect(mCanvas, &ChunkDataOverrideCanvas::editFinished,
            this, &ChunkDataOverrideDialog::endEdit);
    connect(mCanvas, &ChunkDataOverrideCanvas::imageChanged,
            this, [this]() { setDirty(true); });
    connect(mCanvas, &ChunkDataOverrideCanvas::pointerMoved,
            this, &ChunkDataOverrideDialog::updatePointerStatus);
    connect(load, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::loadImageFromFile);
    connect(saveAs, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::saveImageAs);
    connect(detach, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::detachOverride);
    connect(reset, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::resetToAutomatic);
    connect(mUndo, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::undo);
    connect(mRedo, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::redo);
    connect(mSave, &QPushButton::clicked,
            this, &ChunkDataOverrideDialog::saveImage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    loadInitialImage();
    updatePaintValue();
    updateHistoryActions();
}

void ChunkDataOverrideDialog::closeEvent(QCloseEvent *event)
{
    if (confirmClose())
        event->accept();
    else
        event->ignore();
}

void ChunkDataOverrideDialog::loadInitialImage()
{
    mCurrentPath = mCell ? mCell->chunkDataOverrideFilePath() : QString();
    QImage image(ChunkDataOverride::ImageSize,
                 ChunkDataOverride::ImageSize,
                 QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    if (!mCurrentPath.isEmpty()) {
        QString error;
        QImage loaded;
        if (ChunkDataOverride::loadImage(mCurrentPath, &loaded, &error)) {
            image = loaded;
        } else {
            QMessageBox::critical(this, tr("Invalid Chunk Data Override"),
                                  error + tr("\n\nThe invalid file was not overwritten. Load a valid image or use Save As to create a replacement."));
            mCurrentPath.clear();
        }
    }
    mCanvas->setImage(image);
    mSavedImage = image;
    mPath->setText(mCurrentPath.isEmpty()
                   ? tr("Not attached. Save uses the recommended project path.")
                   : QDir::toNativeSeparators(mCurrentPath));
    setDirty(false);
}

void ChunkDataOverrideDialog::loadImageFromFile()
{
    const QString start = mCurrentPath.isEmpty()
            ? QFileInfo(mWorldDocument->fileName()).absolutePath()
            : QFileInfo(mCurrentPath).absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(
                this, tr("Load Chunk Data Override"), start,
                tr("PNG images (*.png)"));
    if (filePath.isEmpty())
        return;
    QImage image;
    QString error;
    if (!ChunkDataOverride::loadImage(filePath, &image, &error)) {
        QMessageBox::critical(this, tr("Invalid Chunk Data Override"), error);
        return;
    }
    mUndoImages += mCanvas->image();
    mRedoImages.clear();
    mCanvas->setImage(image);
    mCurrentPath = QFileInfo(filePath).absoluteFilePath();
    mPath->setText(QDir::toNativeSeparators(mCurrentPath));
    setDirty(true);
    updateHistoryActions();
}

bool ChunkDataOverrideDialog::saveImage()
{
    QString filePath = mCurrentPath;
    if (filePath.isEmpty()) {
        filePath = ChunkDataOverride::defaultFilePath(
                    mWorldDocument->fileName(), mCell->displayPos());
    }
    return saveToPath(filePath);
}

bool ChunkDataOverrideDialog::saveImageAs()
{
    const QString suggested = mCurrentPath.isEmpty()
            ? ChunkDataOverride::defaultFilePath(
                mWorldDocument->fileName(), mCell->displayPos())
            : mCurrentPath;
    QString filePath = QFileDialog::getSaveFileName(
                this, tr("Save Chunk Data Override"), suggested,
                tr("PNG images (*.png)"));
    if (filePath.isEmpty())
        return false;
    if (QFileInfo(filePath).suffix().compare(
                QLatin1String("png"), Qt::CaseInsensitive) != 0)
        filePath += QLatin1String(".png");
    return saveToPath(filePath);
}

bool ChunkDataOverrideDialog::saveToPath(const QString &filePath)
{
    QString error;
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (!ChunkDataOverride::saveImage(
                absolutePath, mCanvas->image(), &error)) {
        QMessageBox::critical(this, tr("Chunk Data Override Save Failed"),
                              error);
        return false;
    }
    if (mCell->chunkDataOverrideFilePath() != absolutePath) {
        mWorldDocument->setCellChunkDataOverrideFilePath(
                    mCell, absolutePath);
    }
    mCurrentPath = absolutePath;
    mPath->setText(QDir::toNativeSeparators(mCurrentPath));
    mSavedImage = mCanvas->image();
    setDirty(false);
    QMessageBox::information(
                this, tr("Chunk Data Override Saved"),
                tr("WorldEd will merge this image over generated chunk data the next time Generate Lots exports cell %1,%2.\n\nExplicit pixels: %3")
                .arg(mCell->displayPos().x())
                .arg(mCell->displayPos().y())
                .arg(ChunkDataOverride::hasExplicitPixels(mSavedImage)
                     ? tr("present") : tr("none, all squares remain automatic")));
    return true;
}

void ChunkDataOverrideDialog::detachOverride()
{
    if (mCell->chunkDataOverrideFilePath().isEmpty())
        return;
    if (QMessageBox::question(
                this, tr("Detach Chunk Data Override"),
                tr("Stop using this override for the selected cell?\n\nThe PNG file will be kept and can be attached again later."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes)
        return;
    mWorldDocument->setCellChunkDataOverrideFilePath(mCell, QString());
    mCurrentPath.clear();
    mPath->setText(tr("Not attached. Save uses the recommended project path."));
    setDirty(mCanvas->image() != mSavedImage);
}

void ChunkDataOverrideDialog::resetToAutomatic()
{
    QImage image(ChunkDataOverride::ImageSize,
                 ChunkDataOverride::ImageSize,
                 QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    if (mCanvas->image() == image)
        return;
    mUndoImages += mCanvas->image();
    mRedoImages.clear();
    mCanvas->setImage(image);
    setDirty(true);
    updateHistoryActions();
}

void ChunkDataOverrideDialog::updatePaintValue()
{
    quint8 value = 0;
    if (mSolid->isChecked())
        value |= ChunkDataOverride::Solid;
    if (mWallNorth->isChecked())
        value |= ChunkDataOverride::WallNorth;
    if (mWallWest->isChecked())
        value |= ChunkDataOverride::WallWest;
    if (mWater->isChecked())
        value |= ChunkDataOverride::Water;
    if (mRoom->isChecked())
        value |= ChunkDataOverride::Room;
    mCanvas->setPaintValue(value);
    mCanvas->setInheritMode(mAutomaticValue->isChecked());
    mValueLabel->setText(mAutomaticValue->isChecked()
                         ? tr("Paint result: automatic generation")
                         : tr("Paint result: %1")
                           .arg(ChunkDataOverride::valueDescription(value)));
    const bool enabled = mExplicitValue->isChecked();
    mSolid->setEnabled(enabled);
    mWallNorth->setEnabled(enabled);
    mWallWest->setEnabled(enabled);
    mWater->setEnabled(enabled);
    mRoom->setEnabled(enabled);
}

void ChunkDataOverrideDialog::updatePointerStatus(const QPoint &point)
{
    if (!mCanvas->image().rect().contains(point)) {
        mPointerStatus->setText(
                    tr("Move over the image to inspect a square."));
        return;
    }
    const QRgb pixel = mCanvas->image().pixel(point);
    if (qAlpha(pixel) == 0) {
        mPointerStatus->setText(
                    tr("Square %1,%2: automatic value generated during LOT export")
                    .arg(point.x()).arg(point.y()));
        return;
    }
    mPointerStatus->setText(
                tr("Square %1,%2: explicit %3")
                .arg(point.x()).arg(point.y())
                .arg(ChunkDataOverride::valueDescription(
                         quint8(qRed(pixel)))));
}

void ChunkDataOverrideDialog::beginEdit()
{
    if (mEditActive)
        return;
    mPendingImage = mCanvas->image();
    mEditActive = true;
}

void ChunkDataOverrideDialog::endEdit()
{
    if (!mEditActive)
        return;
    mEditActive = false;
    if (mPendingImage != mCanvas->image()) {
        mUndoImages += mPendingImage;
        mRedoImages.clear();
    }
    mPendingImage = QImage();
    updateHistoryActions();
}

void ChunkDataOverrideDialog::undo()
{
    if (mUndoImages.isEmpty())
        return;
    mRedoImages += mCanvas->image();
    mCanvas->setImage(mUndoImages.takeLast());
    setDirty(mCanvas->image() != mSavedImage);
    updateHistoryActions();
}

void ChunkDataOverrideDialog::redo()
{
    if (mRedoImages.isEmpty())
        return;
    mUndoImages += mCanvas->image();
    mCanvas->setImage(mRedoImages.takeLast());
    setDirty(mCanvas->image() != mSavedImage);
    updateHistoryActions();
}

void ChunkDataOverrideDialog::setDirty(bool dirty)
{
    mDirty = dirty;
    setWindowTitle(tr("Native256 Chunk Data Overrides%1")
                   .arg(mDirty ? QLatin1String(" *") : QString()));
    mSave->setEnabled(mDirty || mCell->chunkDataOverrideFilePath().isEmpty());
}

bool ChunkDataOverrideDialog::confirmClose()
{
    if (!mDirty)
        return true;
    QMessageBox message(
                QMessageBox::Question,
                tr("Unsaved Chunk Data Override"),
                tr("Save the chunk data override before closing?"),
                QMessageBox::Save | QMessageBox::Discard
                | QMessageBox::Cancel,
                this);
    message.setDefaultButton(QMessageBox::Save);
    const int result = message.exec();
    if (result == QMessageBox::Cancel)
        return false;
    if (result == QMessageBox::Save)
        return saveImage();
    return true;
}

void ChunkDataOverrideDialog::updateHistoryActions()
{
    mUndo->setEnabled(!mUndoImages.isEmpty());
    mRedo->setEnabled(!mRedoImages.isEmpty());
}
