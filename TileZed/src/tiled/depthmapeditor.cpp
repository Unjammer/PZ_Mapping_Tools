#include "depthmapeditor.h"

#include "tile.h"
#include "tilemetainfomgr.h"
#include "tileset.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFrame>
#include <QFontMetrics>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Tiled;
using namespace Tiled::Internal;

namespace {

QImage normalisedDepthTile(const QImage &image)
{
    QImage result(128, 256, QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    if (image.isNull())
        return result;

    const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
    const int width = qMin(result.width(), converted.width());
    const int height = qMin(result.height(), converted.height());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = converted.pixel(x, y);
            if (qAlpha(pixel) == 0)
                continue;
            const int depth = qBlue(pixel);
            result.setPixel(x, y, qRgba(depth, depth, depth, 255));
        }
    }
    return result;
}

bool imagesEqual(const QImage &left, const QImage &right)
{
    return left.size() == right.size() && left == right;
}

QString nativePath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(path);
}

QRectF pointsBounds(const QVector<QPointF> &points)
{
    if (points.isEmpty())
        return QRectF();
    qreal minimumX = points.first().x();
    qreal maximumX = minimumX;
    qreal minimumY = points.first().y();
    qreal maximumY = minimumY;
    for (const QPointF &point : points) {
        minimumX = qMin(minimumX, point.x());
        maximumX = qMax(maximumX, point.x());
        minimumY = qMin(minimumY, point.y());
        maximumY = qMax(maximumY, point.y());
    }
    return QRectF(QPointF(minimumX, minimumY),
                  QPointF(maximumX, maximumY));
}

class ScopedDepthMapSettings
{
public:
    ScopedDepthMapSettings()
    {
        QSettings settings;
        mHadDirectory = settings.contains(
                    QStringLiteral("DepthMapEditor/LastDirectory"));
        mDirectory = settings.value(
                    QStringLiteral("DepthMapEditor/LastDirectory"));
        mHadGeometryFile = settings.contains(
                    QStringLiteral("DepthMapEditor/LastGeometryFile"));
        mGeometryFile = settings.value(
                    QStringLiteral("DepthMapEditor/LastGeometryFile"));
    }

    ~ScopedDepthMapSettings()
    {
        QSettings settings;
        restore(settings,
                QStringLiteral("DepthMapEditor/LastDirectory"),
                mHadDirectory, mDirectory);
        restore(settings,
                QStringLiteral("DepthMapEditor/LastGeometryFile"),
                mHadGeometryFile, mGeometryFile);
        settings.sync();
    }

private:
    static void restore(QSettings &settings, const QString &key,
                        bool existed, const QVariant &value)
    {
        if (existed)
            settings.setValue(key, value);
        else
            settings.remove(key);
    }

    bool mHadDirectory = false;
    QVariant mDirectory;
    bool mHadGeometryFile = false;
    QVariant mGeometryFile;
};

}

DepthMapCanvas::DepthMapCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setZoom(2);
}

void DepthMapCanvas::setImages(const QImage &source, const QImage &depth)
{
    mSource = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (mSource.size() != QSize(128, 256)) {
        QImage fitted(128, 256, QImage::Format_ARGB32_Premultiplied);
        fitted.fill(Qt::transparent);
        QPainter painter(&fitted);
        painter.drawImage(QRect(0, 0, 128, 256), mSource);
        mSource = fitted;
    }
    setDepthImage(depth);
}

void DepthMapCanvas::setDepthImage(const QImage &depth)
{
    mDepth = normalisedDepthTile(depth);
    rebuildOverlay();
    update();
}

void DepthMapCanvas::setZoom(int zoom)
{
    mZoom = qBound(1, zoom, 6);
    setFixedSize(128 * mZoom, 256 * mZoom);
    updateGeometry();
    update();
}

void DepthMapCanvas::setGeometry(
        const QVector<DepthPrimitive> &geometry, int selectedIndex)
{
    mGeometry = geometry;
    mSelectedGeometry = selectedIndex;
    update();
}

void DepthMapCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const int checker = 8 * mZoom;
    const QColor checkerA(58, 61, 66);
    const QColor checkerB(78, 82, 88);
    for (int y = 0; y < height(); y += checker) {
        for (int x = 0; x < width(); x += checker) {
            painter.fillRect(x, y, checker, checker,
                             ((x / checker) + (y / checker)) % 2
                             ? checkerA : checkerB);
        }
    }

    painter.setOpacity(0.82);
    painter.drawImage(rect(), mSource);
    painter.setOpacity(1.0);
    painter.drawImage(rect(), mOverlay);

    for (int index = 0; index < mGeometry.size(); ++index) {
        const bool selected = index == mSelectedGeometry;
        QPen pen(selected ? QColor(255, 196, 32)
                          : QColor(30, 220, 255, 210),
                 selected ? 2.5 : 1.25);
        pen.setCosmetic(true);
        painter.setPen(pen);
        const QVector<QLineF> lines =
                DepthGeometryRasterizer::wireframe(mGeometry.at(index));
        for (const QLineF &line : lines) {
            painter.drawLine(QPointF(line.p1().x() * mZoom,
                                     line.p1().y() * mZoom),
                             QPointF(line.p2().x() * mZoom,
                                     line.p2().y() * mZoom));
        }
        if (selected) {
            painter.setBrush(QColor(255, 196, 32));
            painter.setPen(Qt::NoPen);
            for (const QLineF &line : lines) {
                painter.drawEllipse(
                    QPointF(line.p1().x() * mZoom,
                            line.p1().y() * mZoom),
                    2.5, 2.5);
            }

            const QVector3D center3D = mGeometry.at(index).translate;
            const QPointF center =
                    DepthGeometryRasterizer::project(center3D) * mZoom;
            const QVector<QPair<QVector3D, QColor>> axes = {
                qMakePair(QVector3D(0.35f, 0.0f, 0.0f),
                          QColor(235, 72, 72)),
                qMakePair(QVector3D(0.0f, 0.35f, 0.0f),
                          QColor(84, 220, 110)),
                qMakePair(QVector3D(0.0f, 0.0f, 0.35f),
                          QColor(70, 145, 255))
            };
            painter.setBrush(Qt::NoBrush);
            for (const auto &axis : axes) {
                painter.setPen(QPen(axis.second, 2.0));
                const QPointF end = DepthGeometryRasterizer::project(
                            center3D + axis.first) * mZoom;
                painter.drawLine(center, end);
            }
            painter.setPen(QPen(Qt::white, 1.5));
            painter.setBrush(QColor(30, 33, 38, 220));
            painter.drawEllipse(center, 4.0, 4.0);

            const QRectF bounds = geometryBounds(index);
            if (bounds.isValid()) {
                const QRectF screenBounds(
                    bounds.topLeft() * mZoom,
                    bounds.size() * mZoom);
                const QVector<QPointF> handles = {
                    screenBounds.topLeft(), screenBounds.topRight(),
                    screenBounds.bottomLeft(), screenBounds.bottomRight()
                };
                painter.setPen(QPen(QColor(35, 35, 35), 1.0));
                painter.setBrush(QColor(255, 196, 32));
                for (const QPointF &handle : handles)
                    painter.drawRect(QRectF(handle.x() - 4.0,
                                            handle.y() - 4.0,
                                            8.0, 8.0));
            }
        }
    }
    // Selected-geometry handles use a solid brush. Reset it before drawing
    // the canvas border, otherwise QPainter fills the entire tile with the
    // selection color and hides both the source sprite and the wireframe.
    painter.setBrush(Qt::NoBrush);

    if (mZoom >= 4) {
        painter.setPen(QColor(255, 255, 255, 28));
        for (int x = 0; x <= 128; ++x)
            painter.drawLine(x * mZoom, 0, x * mZoom, height());
        for (int y = 0; y <= 256; ++y)
            painter.drawLine(0, y * mZoom, width(), y * mZoom);
    }

    painter.setPen(QPen(QColor(190, 198, 210), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void DepthMapCanvas::mousePressEvent(QMouseEvent *event)
{
    if (mGeometryEditing && event->button() == Qt::LeftButton) {
        const QPointF point = QPointF(event->pos()) / mZoom;
        if (mSelectedGeometry >= 0 &&
                isOnResizeHandle(point, mSelectedGeometry)) {
            const QRectF bounds = geometryBounds(mSelectedGeometry);
            mGeometryResizing = true;
            mGeometryDragging = false;
            mLastGeometryPoint = point;
            mLastResizeDistance =
                    QLineF(bounds.center(), point).length();
            return;
        }
        const int picked = pickGeometry(point);
        if (picked >= 0) {
            mSelectedGeometry = picked;
            mGeometryDragging = true;
            mGeometryResizing = false;
            mLastGeometryPoint = point;
            emit geometryPicked(picked);
            update();
        }
        return;
    }
    if (event->button() != Qt::LeftButton &&
            event->button() != Qt::RightButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    beginStroke(imagePoint(event->pos()), event->button());
}

void DepthMapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (mGeometryEditing) {
        const QPointF point = QPointF(event->pos()) / mZoom;
        if (mGeometryResizing && mSelectedGeometry >= 0) {
            const QRectF bounds = geometryBounds(mSelectedGeometry);
            const qreal distance =
                    QLineF(bounds.center(), point).length();
            if (mLastResizeDistance > 0.001 && distance > 0.001) {
                const float factor = float(
                            distance / mLastResizeDistance);
                if (!qFuzzyCompare(factor, 1.0f))
                    emit geometryScaled(mSelectedGeometry, factor);
            }
            mLastResizeDistance = distance;
        } else if (mGeometryDragging && mSelectedGeometry >= 0) {
            const QVector3D delta =
                    geometryDragDelta(mLastGeometryPoint, point);
            if (!delta.isNull())
                emit geometryTranslated(mSelectedGeometry, delta);
            mLastGeometryPoint = point;
        }
        return;
    }
    const QPoint point = imagePoint(event->pos());
    reportCursor(point);
    if (mStrokeActive)
        continueStroke(point);
}

void DepthMapCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (mGeometryEditing) {
        if (event->button() == Qt::LeftButton) {
            mGeometryDragging = false;
            mGeometryResizing = false;
        }
        return;
    }
    if (!mStrokeActive ||
            (event->button() != Qt::LeftButton &&
             event->button() != Qt::RightButton)) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    continueStroke(imagePoint(event->pos()));
    mStrokeActive = false;
    if (mStrokeChanged)
        emit editFinished(mDepth);
}

int DepthMapCanvas::pickGeometry(const QPointF &point) const
{
    int bestIndex = -1;
    qreal bestDistance = 9.0;
    int enclosedIndex = -1;
    qreal enclosedArea = std::numeric_limits<qreal>::max();
    for (int index = 0; index < mGeometry.size(); ++index) {
        const QVector<QLineF> lines =
                DepthGeometryRasterizer::wireframe(mGeometry.at(index));
        QRectF bounds;
        for (const QLineF &line : lines) {
            if (bounds.isNull())
                bounds = QRectF(line.p1(), QSizeF(0, 0));
            bounds = bounds.united(
                        QRectF(line.p1(), line.p2()).normalized());
            const QPointF a = line.p1();
            const QPointF b = line.p2();
            const QPointF segment = b - a;
            const qreal lengthSquared =
                    segment.x() * segment.x() +
                    segment.y() * segment.y();
            qreal amount = 0.0;
            if (lengthSquared > 0.0001) {
                amount = ((point.x() - a.x()) * segment.x() +
                          (point.y() - a.y()) * segment.y()) /
                         lengthSquared;
                amount = qBound(0.0, amount, 1.0);
            }
            const QPointF nearest = a + segment * amount;
            const qreal dx = point.x() - nearest.x();
            const qreal dy = point.y() - nearest.y();
            const qreal distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        if (bounds.adjusted(-4, -4, 4, 4).contains(point)) {
            const qreal area = qMax(1.0, bounds.width() * bounds.height());
            if (area < enclosedArea) {
                enclosedArea = area;
                enclosedIndex = index;
            }
        }
    }
    return bestIndex >= 0 ? bestIndex : enclosedIndex;
}

QRectF DepthMapCanvas::geometryBounds(int index) const
{
    if (index < 0 || index >= mGeometry.size())
        return QRectF();
    const QVector<QLineF> lines =
            DepthGeometryRasterizer::wireframe(mGeometry.at(index));
    if (lines.isEmpty())
        return QRectF();
    qreal minimumX = lines.first().p1().x();
    qreal maximumX = minimumX;
    qreal minimumY = lines.first().p1().y();
    qreal maximumY = minimumY;
    for (const QLineF &line : lines) {
        for (const QPointF &point : { line.p1(), line.p2() }) {
            minimumX = qMin(minimumX, point.x());
            maximumX = qMax(maximumX, point.x());
            minimumY = qMin(minimumY, point.y());
            maximumY = qMax(maximumY, point.y());
        }
    }
    return QRectF(QPointF(minimumX, minimumY),
                  QPointF(maximumX, maximumY));
}

bool DepthMapCanvas::isOnResizeHandle(
        const QPointF &point, int index) const
{
    const QRectF bounds = geometryBounds(index);
    if (!bounds.isValid())
        return false;
    const QVector<QPointF> handles = {
        bounds.topLeft(), bounds.topRight(),
        bounds.bottomLeft(), bounds.bottomRight()
    };
    const qreal radius = 7.0 / qMax(1, mZoom);
    for (const QPointF &handle : handles) {
        if (QLineF(handle, point).length() <= radius)
            return true;
    }
    return false;
}

QVector3D DepthMapCanvas::geometryDragDelta(
        const QPointF &from, const QPointF &to) const
{
    const QPointF origin = DepthGeometryRasterizer::project(QVector3D());
    const QPointF xAxis =
            DepthGeometryRasterizer::project(QVector3D(1.0f, 0.0f, 0.0f)) -
            origin;
    const QPointF zAxis =
            DepthGeometryRasterizer::project(QVector3D(0.0f, 0.0f, 1.0f)) -
            origin;
    const QPointF screenDelta = to - from;
    const qreal determinant =
            xAxis.x() * zAxis.y() - xAxis.y() * zAxis.x();
    if (qAbs(determinant) < 0.0001)
        return QVector3D();
    const float dx = float(
        (screenDelta.x() * zAxis.y() -
         screenDelta.y() * zAxis.x()) / determinant);
    const float dz = float(
        (xAxis.x() * screenDelta.y() -
         xAxis.y() * screenDelta.x()) / determinant);
    return QVector3D(dx, 0.0f, dz);
}

void DepthMapCanvas::leaveEvent(QEvent *event)
{
    emit cursorPixelChanged(-1, -1, 0, false);
    QWidget::leaveEvent(event);
}

QPoint DepthMapCanvas::imagePoint(const QPoint &widgetPoint) const
{
    return QPoint(widgetPoint.x() / mZoom, widgetPoint.y() / mZoom);
}

void DepthMapCanvas::beginStroke(const QPoint &point,
                                 Qt::MouseButton button)
{
    if (!QRect(0, 0, 128, 256).contains(point))
        return;

    if (mTool == PickTool && button == Qt::LeftButton) {
        const QRgb pixel = mDepth.pixel(point);
        if (qAlpha(pixel) != 0)
            emit depthPicked(qBlue(pixel));
        reportCursor(point);
        return;
    }

    mStrokeActive = true;
    mStrokeChanged = false;
    mTemporaryErase = button == Qt::RightButton;
    mLastPoint = point;
    emit editStarted(mDepth);
    applyBrush(point);
}

void DepthMapCanvas::continueStroke(const QPoint &point)
{
    if (!mStrokeActive)
        return;

    const QPoint bounded(qBound(0, point.x(), 127),
                         qBound(0, point.y(), 255));
    const int dx = bounded.x() - mLastPoint.x();
    const int dy = bounded.y() - mLastPoint.y();
    const int steps = qMax(qAbs(dx), qAbs(dy));
    if (steps == 0) {
        applyBrush(bounded);
        return;
    }

    for (int i = 1; i <= steps; ++i) {
        const QPoint interpolated(
            mLastPoint.x() + qRound(qreal(dx) * i / steps),
            mLastPoint.y() + qRound(qreal(dy) * i / steps));
        applyBrush(interpolated);
    }
    mLastPoint = bounded;
}

void DepthMapCanvas::applyBrush(const QPoint &point)
{
    const bool erase = mTemporaryErase || mTool == EraseTool;
    const int radius = qMax(1, mBrushRadius);
    const int radiusSquared = radius * radius;
    bool changed = false;

    for (int y = point.y() - radius + 1;
         y <= point.y() + radius - 1; ++y) {
        if (y < 0 || y >= mDepth.height())
            continue;
        for (int x = point.x() - radius + 1;
             x <= point.x() + radius - 1; ++x) {
            if (x < 0 || x >= mDepth.width())
                continue;
            const int ddx = x - point.x();
            const int ddy = y - point.y();
            if (ddx * ddx + ddy * ddy >= radiusSquared)
                continue;

            const QRgb replacement = erase
                    ? qRgba(0, 0, 0, 0)
                    : qRgba(mBrushDepth, mBrushDepth,
                            mBrushDepth, 255);
            if (mDepth.pixel(x, y) != replacement) {
                mDepth.setPixel(x, y, replacement);
                changed = true;
            }
        }
    }

    if (changed) {
        mStrokeChanged = true;
        rebuildOverlay();
        update();
    }
    reportCursor(point);
}

void DepthMapCanvas::rebuildOverlay()
{
    mOverlay = QImage(128, 256, QImage::Format_ARGB32_Premultiplied);
    mOverlay.fill(Qt::transparent);
    for (int y = 0; y < mDepth.height(); ++y) {
        for (int x = 0; x < mDepth.width(); ++x) {
            const QRgb pixel = mDepth.pixel(x, y);
            if (qAlpha(pixel) == 0)
                continue;
            const int depth = qBlue(pixel);
            QColor heat = QColor::fromHsv(
                        240 - qRound(depth * 240.0 / 255.0),
                        220, 255, 178);
            mOverlay.setPixelColor(x, y, heat);
        }
    }
}

void DepthMapCanvas::reportCursor(const QPoint &point)
{
    if (!QRect(0, 0, 128, 256).contains(point)) {
        emit cursorPixelChanged(-1, -1, 0, false);
        return;
    }
    const QRgb pixel = mDepth.pixel(point);
    emit cursorPixelChanged(point.x(), point.y(), qBlue(pixel),
                            qAlpha(pixel) != 0);
}

DepthMapEditor::DepthMapEditor(QWidget *parent)
    : QMainWindow(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(tr("Depth Map Editor"));
    const QRect available = QApplication::primaryScreen()
            ? QApplication::primaryScreen()->availableGeometry()
            : QRect(0, 0, 1920, 1080);
    resize(qMin(1760, available.width() - 80),
           qMin(940, available.height() - 80));

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *chooseDirectoryAction = fileMenu->addAction(
                tr("Choose Depthmaps Folder..."));
    QAction *openAction = fileMenu->addAction(tr("Open Depth PNG..."));
    mReloadAction = fileMenu->addAction(tr("Reload"));
    fileMenu->addSeparator();
    mSaveAction = fileMenu->addAction(tr("&Save"));
    mSaveAction->setShortcut(QKeySequence::Save);
    mSaveAsAction = fileMenu->addAction(tr("Save &As..."));
    fileMenu->addSeparator();
    QAction *closeAction = fileMenu->addAction(tr("Close"));
    closeAction->setShortcut(QKeySequence::Close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    mUndoAction = editMenu->addAction(tr("&Undo"));
    mUndoAction->setShortcut(QKeySequence::Undo);
    mRedoAction = editMenu->addAction(tr("&Redo"));
    mRedoAction->setShortcut(QKeySequence::Redo);
    editMenu->addSeparator();
    QAction *copyAction = editMenu->addAction(tr("Copy Tile Depth"));
    copyAction->setShortcut(QKeySequence::Copy);
    QAction *pasteAction = editMenu->addAction(tr("Paste Tile Depth"));
    pasteAction->setShortcut(QKeySequence::Paste);

    QWidget *central = new QWidget(this);
    QVBoxLayout *outerLayout = new QVBoxLayout(central);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    QGroupBox *sourceGroup = new QGroupBox(tr("Build 42 depth atlas"), central);
    QGridLayout *sourceLayout = new QGridLayout(sourceGroup);
    sourceLayout->setContentsMargins(7, 6, 7, 6);
    sourceLayout->setHorizontalSpacing(7);
    sourceLayout->setVerticalSpacing(3);
    mTilesetLabel = new QLabel(tr("No tileset selected"), sourceGroup);
    mTilesetLabel->setSizePolicy(
                QSizePolicy::Ignored, QSizePolicy::Preferred);
    QPushButton *chooseTilesetButton =
            new QPushButton(tr("Choose Tileset..."), sourceGroup);
    mDirectoryEdit = new QLineEdit(sourceGroup);
    mDirectoryEdit->setReadOnly(true);
    QPushButton *directoryButton =
            new QPushButton(tr("Depthmaps Folder..."), sourceGroup);
    mFileLabel = new QLabel(sourceGroup);
    mFileLabel->setSizePolicy(
                QSizePolicy::Ignored, QSizePolicy::Preferred);
    mFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mGeometryFileLabel = new QLabel(sourceGroup);
    mGeometryFileLabel->setSizePolicy(
                QSizePolicy::Ignored, QSizePolicy::Preferred);
    mGeometryFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *geometryFileButton =
            new QPushButton(tr("Geometry File..."), sourceGroup);
    sourceLayout->addWidget(new QLabel(tr("Tileset:"), sourceGroup), 0, 0);
    sourceLayout->addWidget(mTilesetLabel, 0, 1);
    sourceLayout->addWidget(chooseTilesetButton, 0, 2);
    sourceLayout->addWidget(new QLabel(tr("Folder:"), sourceGroup), 1, 0);
    sourceLayout->addWidget(mDirectoryEdit, 1, 1);
    sourceLayout->addWidget(directoryButton, 1, 2);
    sourceLayout->addWidget(new QLabel(tr("Atlas:"), sourceGroup), 2, 0);
    sourceLayout->addWidget(mFileLabel, 2, 1, 1, 2);
    sourceLayout->addWidget(new QLabel(tr("Geometry:"), sourceGroup), 3, 0);
    sourceLayout->addWidget(mGeometryFileLabel, 3, 1);
    sourceLayout->addWidget(geometryFileButton, 3, 2);
    outerLayout->addWidget(sourceGroup);

    mMainSplitter = new QSplitter(Qt::Horizontal, central);
    mMainSplitter->setChildrenCollapsible(false);
    mTileTable = new QTableWidget(mMainSplitter);
    mTileTable->setColumnCount(DepthColumns);
    mTileTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mTileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mTileTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    mTileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTileTable->horizontalHeader()->hide();
    mTileTable->verticalHeader()->hide();
    mTileTable->setShowGrid(false);
    mTileTable->setIconSize(QSize(48, 96));
    mTileTable->setMinimumWidth(8 * 52 + 18);

    QWidget *editorPanel = new QWidget(mMainSplitter);
    editorPanel->setMinimumWidth(360);
    QVBoxLayout *editorLayout = new QVBoxLayout(editorPanel);
    editorLayout->setContentsMargins(8, 0, 0, 0);

    mModeTabs = new QTabWidget(mMainSplitter);
    mModeTabs->setMinimumWidth(560);
    QWidget *geometryTab = new QWidget(mModeTabs);
    QHBoxLayout *geometryTabLayout = new QHBoxLayout(geometryTab);
    geometryTabLayout->setContentsMargins(6, 6, 6, 6);

    QVBoxLayout *geometryListLayout = new QVBoxLayout;
    mGeometryList = new QListWidget(geometryTab);
    mGeometryList->setMinimumWidth(180);
    geometryListLayout->addWidget(new QLabel(
        tr("3D primitives for this tile"), geometryTab));
    geometryListLayout->addWidget(mGeometryList, 1);

    QGridLayout *addGeometryLayout = new QGridLayout;
    QPushButton *addXYButton = new QPushButton(tr("Add XY wall"), geometryTab);
    QPushButton *addXZButton = new QPushButton(tr("Add XZ floor"), geometryTab);
    QPushButton *addYZButton = new QPushButton(tr("Add YZ wall"), geometryTab);
    QPushButton *addBoxButton = new QPushButton(tr("Add Box"), geometryTab);
    QPushButton *addCylinderButton =
            new QPushButton(tr("Add Cylinder"), geometryTab);
    QPushButton *duplicateGeometryButton =
            new QPushButton(tr("Duplicate"), geometryTab);
    QPushButton *removeGeometryButton =
            new QPushButton(tr("Remove"), geometryTab);
    addGeometryLayout->addWidget(addXYButton, 0, 0);
    addGeometryLayout->addWidget(addXZButton, 0, 1);
    addGeometryLayout->addWidget(addYZButton, 0, 2);
    addGeometryLayout->addWidget(addBoxButton, 1, 0);
    addGeometryLayout->addWidget(addCylinderButton, 1, 1);
    addGeometryLayout->addWidget(duplicateGeometryButton, 2, 0);
    addGeometryLayout->addWidget(removeGeometryButton, 2, 1);
    geometryListLayout->addLayout(addGeometryLayout);

    QGroupBox *presetGroup = new QGroupBox(
                tr("Reusable primitive presets"), geometryTab);
    QVBoxLayout *presetLayout = new QVBoxLayout(presetGroup);
    mPresetCombo = new QComboBox(presetGroup);
    presetLayout->addWidget(mPresetCombo);
    QHBoxLayout *presetButtons = new QHBoxLayout;
    QPushButton *savePresetButton =
            new QPushButton(tr("Save Selected..."), presetGroup);
    mInsertPresetButton =
            new QPushButton(tr("Insert Preset"), presetGroup);
    mDeletePresetButton =
            new QPushButton(tr("Delete"), presetGroup);
    presetButtons->addWidget(savePresetButton);
    presetButtons->addWidget(mInsertPresetButton);
    presetButtons->addWidget(mDeletePresetButton);
    presetLayout->addLayout(presetButtons);
    presetGroup->setToolTip(tr(
        "Presets are stored in portable settings and can be inserted "
        "into any similar tile, including tiles from another tileset."));
    geometryListLayout->addWidget(presetGroup);
    geometryTabLayout->addLayout(geometryListLayout, 1);

    QWidget *geometryProperties = new QWidget(geometryTab);
    QVBoxLayout *geometryPropertiesLayout =
            new QVBoxLayout(geometryProperties);
    geometryPropertiesLayout->setContentsMargins(0, 0, 0, 0);
    QGridLayout *transformLayout = new QGridLayout;
    const QStringList axes = {
        QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")
    };
    transformLayout->addWidget(new QLabel(tr("Transform")), 0, 0);
    for (int axis = 0; axis < 3; ++axis) {
        transformLayout->addWidget(
            new QLabel(axes.at(axis), geometryProperties), 0, axis + 1);
        mTranslateSpins[axis] = new QDoubleSpinBox(geometryProperties);
        mTranslateSpins[axis]->setRange(-20.0, 20.0);
        mTranslateSpins[axis]->setDecimals(4);
        mTranslateSpins[axis]->setSingleStep(axis == 1
                                            ? 1.0 / 96.0 : 1.0 / 64.0);
        mRotateSpins[axis] = new QDoubleSpinBox(geometryProperties);
        mRotateSpins[axis]->setRange(-360.0, 360.0);
        mRotateSpins[axis]->setDecimals(2);
        mRotateSpins[axis]->setSingleStep(1.0);
        transformLayout->addWidget(mTranslateSpins[axis], 1, axis + 1);
        transformLayout->addWidget(mRotateSpins[axis], 2, axis + 1);
    }
    transformLayout->addWidget(new QLabel(tr("Translate")), 1, 0);
    transformLayout->addWidget(new QLabel(tr("Rotate °")), 2, 0);
    geometryPropertiesLayout->addLayout(transformLayout);

    mShapeStack = new QStackedWidget(geometryProperties);
    QWidget *boxPage = new QWidget(mShapeStack);
    QGridLayout *boxLayout = new QGridLayout(boxPage);
    boxLayout->addWidget(new QLabel(tr("Box bounds")), 0, 0);
    for (int axis = 0; axis < 3; ++axis) {
        boxLayout->addWidget(new QLabel(axes.at(axis), boxPage),
                             0, axis + 1);
        mMinimumSpins[axis] = new QDoubleSpinBox(boxPage);
        mMaximumSpins[axis] = new QDoubleSpinBox(boxPage);
        for (QDoubleSpinBox *spin :
             { mMinimumSpins[axis], mMaximumSpins[axis] }) {
            spin->setRange(-20.0, 20.0);
            spin->setDecimals(4);
            spin->setSingleStep(1.0 / 64.0);
        }
        boxLayout->addWidget(mMinimumSpins[axis], 1, axis + 1);
        boxLayout->addWidget(mMaximumSpins[axis], 2, axis + 1);
    }
    boxLayout->addWidget(new QLabel(tr("Minimum")), 1, 0);
    boxLayout->addWidget(new QLabel(tr("Maximum")), 2, 0);
    mShapeStack->addWidget(boxPage);

    QWidget *cylinderPage = new QWidget(mShapeStack);
    QGridLayout *cylinderLayout = new QGridLayout(cylinderPage);
    mRadiusSpins[0] = new QDoubleSpinBox(cylinderPage);
    mRadiusSpins[1] = new QDoubleSpinBox(cylinderPage);
    mHeightSpin = new QDoubleSpinBox(cylinderPage);
    for (QDoubleSpinBox *spin :
         { mRadiusSpins[0], mRadiusSpins[1], mHeightSpin }) {
        spin->setRange(0.001, 20.0);
        spin->setDecimals(4);
        spin->setSingleStep(1.0 / 64.0);
    }
    cylinderLayout->addWidget(new QLabel(tr("Base radius")), 0, 0);
    cylinderLayout->addWidget(mRadiusSpins[0], 0, 1);
    cylinderLayout->addWidget(new QLabel(tr("Top radius")), 1, 0);
    cylinderLayout->addWidget(mRadiusSpins[1], 1, 1);
    cylinderLayout->addWidget(new QLabel(tr("Height")), 2, 0);
    cylinderLayout->addWidget(mHeightSpin, 2, 1);
    mShapeStack->addWidget(cylinderPage);

    QWidget *polygonPage = new QWidget(mShapeStack);
    QGridLayout *polygonLayout = new QGridLayout(polygonPage);
    mPlaneCombo = new QComboBox(polygonPage);
    mPlaneCombo->addItems({
        QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ")
    });
    mPointsEdit = new QPlainTextEdit(polygonPage);
    mPointsEdit->setMaximumHeight(72);
    mPointsEdit->setPlaceholderText(
        tr("-0.5,-0.5; 0.5,-0.5; 0.5,0.5; -0.5,0.5"));
    polygonLayout->addWidget(new QLabel(tr("Plane")), 0, 0);
    polygonLayout->addWidget(mPlaneCombo, 0, 1);
    polygonLayout->addWidget(new QLabel(tr("Points")), 1, 0);
    polygonLayout->addWidget(mPointsEdit, 1, 1);
    mShapeStack->addWidget(polygonPage);
    geometryPropertiesLayout->addWidget(mShapeStack);

    QGroupBox *pixelSizeGroup =
            new QGroupBox(tr("Primitive size in pixels"),
                          geometryProperties);
    QGridLayout *pixelSizeLayout = new QGridLayout(pixelSizeGroup);
    for (int dimension = 0; dimension < 3; ++dimension) {
        mPixelSizeLabels[dimension] = new QLabel(pixelSizeGroup);
        mPixelSizeSpins[dimension] =
                new QDoubleSpinBox(pixelSizeGroup);
        mPixelSizeSpins[dimension]->setRange(0.0, 4096.0);
        mPixelSizeSpins[dimension]->setDecimals(0);
        mPixelSizeSpins[dimension]->setSingleStep(1.0);
        mPixelSizeSpins[dimension]->setSuffix(tr(" px"));
        pixelSizeLayout->addWidget(
                    mPixelSizeLabels[dimension], dimension, 0);
        pixelSizeLayout->addWidget(
                    mPixelSizeSpins[dimension], dimension, 1);
    }
    mSnapPixelCheck = new QCheckBox(
                tr("Snap move and resize to the pixel grid"),
                pixelSizeGroup);
    QSettings depthSettings;
    mSnapPixelCheck->setChecked(depthSettings.value(
                QLatin1String("DepthMapEditor/SnapToPixelGrid"),
                true).toBool());
    pixelSizeLayout->addWidget(mSnapPixelCheck, 3, 0, 1, 2);
    QLabel *pixelSizeHelp = new QLabel(
        tr("Tile-plane units use 64 pixels. Vertical height uses "
           "96 pixels. Drag a gold corner handle to resize the "
           "selected primitive."),
        pixelSizeGroup);
    pixelSizeHelp->setWordWrap(true);
    pixelSizeLayout->addWidget(pixelSizeHelp, 4, 0, 1, 2);
    geometryPropertiesLayout->addWidget(pixelSizeGroup);

    mRespectAlphaCheck = new QCheckBox(
        tr("Restrict generated pixels to the source tile opacity"),
        geometryProperties);
    mRespectAlphaCheck->setChecked(true);
    geometryPropertiesLayout->addWidget(mRespectAlphaCheck);
    QHBoxLayout *generateLayout = new QHBoxLayout;
    QPushButton *generateSelectedButton =
            new QPushButton(tr("Primitive to Pixels"), geometryProperties);
    QPushButton *generateAllButton =
            new QPushButton(tr("Rebuild Tile from Geometry"), geometryProperties);
    generateSelectedButton->setToolTip(
        tr("Add the selected primitive to the current depth pixels."));
    generateAllButton->setToolTip(
        tr("Clear this depth tile and rasterize all its 3D primitives."));
    generateLayout->addWidget(generateSelectedButton);
    generateLayout->addWidget(generateAllButton);
    geometryPropertiesLayout->addLayout(generateLayout);
    geometryPropertiesLayout->addStretch();
    geometryTabLayout->addWidget(geometryProperties, 2);
    mModeTabs->addTab(geometryTab, tr("3D Geometry"));

    QWidget *pixelTab = new QWidget(mModeTabs);
    QVBoxLayout *pixelTabLayout = new QVBoxLayout(pixelTab);
    pixelTabLayout->setContentsMargins(6, 6, 6, 6);
    QHBoxLayout *toolButtonsLayout = new QHBoxLayout;
    mToolGroup = new QButtonGroup(this);
    mToolGroup->setExclusive(true);
    const auto makeToolButton = [this, toolButtonsLayout](
            const QString &text, int id, const QString &tooltip) {
        QToolButton *button = new QToolButton;
        button->setText(text);
        button->setToolTip(tooltip);
        button->setCheckable(true);
        button->setAutoRaise(false);
        mToolGroup->addButton(button, id);
        toolButtonsLayout->addWidget(button);
        return button;
    };
    QToolButton *paintButton = makeToolButton(
                tr("Paint"), DepthMapCanvas::PaintTool,
                tr("Paint an opaque grayscale depth value"));
    makeToolButton(tr("Erase"), DepthMapCanvas::EraseTool,
                   tr("Make depth pixels undefined (transparent)"));
    makeToolButton(tr("Pick"), DepthMapCanvas::PickTool,
                   tr("Pick a depth value from the atlas"));
    paintButton->setChecked(true);
    toolButtonsLayout->addStretch(1);
    pixelTabLayout->addLayout(toolButtonsLayout);

    QGridLayout *toolControlsLayout = new QGridLayout;
    toolControlsLayout->addWidget(new QLabel(tr("Depth:")), 0, 0);
    mDepthSlider = new QSlider(Qt::Horizontal);
    mDepthSlider->setRange(0, 255);
    mDepthSlider->setValue(128);
    mDepthSlider->setMinimumWidth(180);
    mDepthSpin = new QSpinBox;
    mDepthSpin->setRange(0, 255);
    mDepthSpin->setValue(128);
    toolControlsLayout->addWidget(mDepthSlider, 0, 1);
    toolControlsLayout->addWidget(mDepthSpin, 0, 2);

    toolControlsLayout->addWidget(new QLabel(tr("Brush:")), 1, 0);
    mBrushSpin = new QSpinBox;
    mBrushSpin->setRange(1, 32);
    mBrushSpin->setValue(2);
    mBrushSpin->setSuffix(tr(" px"));
    toolControlsLayout->addWidget(mBrushSpin, 1, 1);

    toolControlsLayout->addWidget(new QLabel(tr("Zoom:")), 2, 0);
    mZoomCombo = new QComboBox;
    for (int zoom = 1; zoom <= 6; ++zoom)
        mZoomCombo->addItem(QStringLiteral("%1x").arg(zoom), zoom);
    mZoomCombo->setCurrentIndex(1);
    toolControlsLayout->addWidget(mZoomCombo, 2, 1);
    toolControlsLayout->setColumnStretch(1, 1);
    pixelTabLayout->addLayout(toolControlsLayout);

    QHBoxLayout *operationLayout = new QHBoxLayout;
    QPushButton *clearButton = new QPushButton(tr("Clear Tile"));
    QPushButton *fillButton = new QPushButton(tr("Fill Source Mask"));
    QPushButton *seedButton = new QPushButton(tr("Seed Vertical Depth"));
    seedButton->setToolTip(
        tr("Create an editable Y-gradient inside the source alpha mask. "
           "This is a starting aid, not geometry-derived game depth."));
    QPushButton *copyButton = new QPushButton(tr("Copy"));
    mPasteButton = new QPushButton(tr("Paste"));
    operationLayout->addWidget(clearButton);
    operationLayout->addWidget(fillButton);
    operationLayout->addWidget(seedButton);
    operationLayout->addSpacing(12);
    operationLayout->addWidget(copyButton);
    operationLayout->addWidget(mPasteButton);
    operationLayout->addStretch();
    pixelTabLayout->addLayout(operationLayout);
    QLabel *pixelExplanation = new QLabel(
        tr("Pixel tools are for inspection and retouching after the "
           "geometry-derived depth has been generated."), pixelTab);
    pixelExplanation->setWordWrap(true);
    pixelTabLayout->addWidget(pixelExplanation);
    pixelTabLayout->addStretch(1);
    mModeTabs->addTab(pixelTab, tr("Pixel Retouch"));

    mTileLabel = new QLabel(editorPanel);
    QFont tileLabelFont = mTileLabel->font();
    tileLabelFont.setBold(true);
    mTileLabel->setFont(tileLabelFont);
    editorLayout->addWidget(mTileLabel);

    mCanvas = new DepthMapCanvas;
    mCanvasScrollArea = new QScrollArea(editorPanel);
    mCanvasScrollArea->setBackgroundRole(QPalette::Dark);
    mCanvasScrollArea->setAlignment(Qt::AlignCenter);
    mCanvasScrollArea->setWidget(mCanvas);
    mCanvasScrollArea->setWidgetResizable(false);
    editorLayout->addWidget(mCanvasScrollArea, 1);

    QLabel *legend = new QLabel(
        tr("Overlay: blue = near/low depth, red = far/high depth; "
           "transparent = undefined. Right-drag temporarily erases."),
        editorPanel);
    legend->setWordWrap(true);
    editorLayout->addWidget(legend);

    mMainSplitter->addWidget(mTileTable);
    mMainSplitter->addWidget(editorPanel);
    mMainSplitter->addWidget(mModeTabs);
    mMainSplitter->setStretchFactor(0, 0);
    mMainSplitter->setStretchFactor(1, 1);
    mMainSplitter->setStretchFactor(2, 0);
    mMainSplitter->setSizes({ 434, 600, 590 });
    outerLayout->addWidget(mMainSplitter, 1);
    setCentralWidget(central);

    mCursorLabel = new QLabel(this);
    statusBar()->addPermanentWidget(mCursorLabel);
    statusBar()->showMessage(
        tr("Select a TileZed tile, then open this editor from Tools."));

    connect(chooseTilesetButton, &QPushButton::clicked,
            this, &DepthMapEditor::chooseTileset);
    connect(directoryButton, &QPushButton::clicked,
            this, &DepthMapEditor::chooseDepthDirectory);
    connect(geometryFileButton, &QPushButton::clicked,
            this, &DepthMapEditor::chooseGeometryFile);
    connect(chooseDirectoryAction, &QAction::triggered,
            this, &DepthMapEditor::chooseDepthDirectory);
    connect(openAction, &QAction::triggered,
            this, &DepthMapEditor::openDepthFile);
    connect(mReloadAction, &QAction::triggered,
            this, &DepthMapEditor::reloadDepthFile);
    connect(mSaveAction, &QAction::triggered,
            this, &DepthMapEditor::saveDepthFile);
    connect(mSaveAsAction, &QAction::triggered,
            this, &DepthMapEditor::saveDepthFileAs);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);
    connect(mUndoAction, &QAction::triggered, this, &DepthMapEditor::undo);
    connect(mRedoAction, &QAction::triggered, this, &DepthMapEditor::redo);
    connect(copyAction, &QAction::triggered,
            this, &DepthMapEditor::copyCurrentDepth);
    connect(pasteAction, &QAction::triggered,
            this, &DepthMapEditor::pasteDepth);
    connect(copyButton, &QPushButton::clicked,
            this, &DepthMapEditor::copyCurrentDepth);
    connect(mPasteButton, &QPushButton::clicked,
            this, &DepthMapEditor::pasteDepth);
    connect(clearButton, &QPushButton::clicked,
            this, &DepthMapEditor::clearCurrentTile);
    connect(fillButton, &QPushButton::clicked,
            this, &DepthMapEditor::fillSourceMask);
    connect(seedButton, &QPushButton::clicked,
            this, &DepthMapEditor::seedVerticalDepth);
    connect(addXYButton, &QPushButton::clicked,
            this, &DepthMapEditor::addPolygonXY);
    connect(addXZButton, &QPushButton::clicked,
            this, &DepthMapEditor::addPolygonXZ);
    connect(addYZButton, &QPushButton::clicked,
            this, &DepthMapEditor::addPolygonYZ);
    connect(addBoxButton, &QPushButton::clicked,
            this, &DepthMapEditor::addBox);
    connect(addCylinderButton, &QPushButton::clicked,
            this, &DepthMapEditor::addCylinder);
    connect(duplicateGeometryButton, &QPushButton::clicked,
            this, &DepthMapEditor::duplicateGeometry);
    connect(removeGeometryButton, &QPushButton::clicked,
            this, &DepthMapEditor::removeGeometry);
    connect(savePresetButton, &QPushButton::clicked,
            this, &DepthMapEditor::savePrimitivePreset);
    connect(mInsertPresetButton, &QPushButton::clicked,
            this, &DepthMapEditor::insertPrimitivePreset);
    connect(mDeletePresetButton, &QPushButton::clicked,
            this, &DepthMapEditor::deletePrimitivePreset);
    connect(generateSelectedButton, &QPushButton::clicked,
            this, &DepthMapEditor::generateSelectedGeometry);
    connect(generateAllButton, &QPushButton::clicked,
            this, &DepthMapEditor::generateAllGeometry);
    connect(mGeometryList, &QListWidget::currentRowChanged,
            this, &DepthMapEditor::geometrySelectionChanged);
    connect(mTileTable, &QTableWidget::currentCellChanged,
            this, &DepthMapEditor::currentCellChanged);
    connect(mCanvas, &DepthMapCanvas::editStarted,
            this, &DepthMapEditor::canvasEditStarted);
    connect(mCanvas, &DepthMapCanvas::editFinished,
            this, &DepthMapEditor::canvasEditFinished);
    connect(mCanvas, &DepthMapCanvas::depthPicked,
            this, &DepthMapEditor::setPickedDepth);
    connect(mCanvas, &DepthMapCanvas::geometryPicked,
            this, &DepthMapEditor::canvasGeometryPicked);
    connect(mCanvas, &DepthMapCanvas::geometryTranslated,
            this, &DepthMapEditor::canvasGeometryTranslated);
    connect(mCanvas, &DepthMapCanvas::geometryScaled,
            this, &DepthMapEditor::canvasGeometryScaled);
    connect(mCanvas, &DepthMapCanvas::cursorPixelChanged,
            this, [this](int x, int y, int depth, bool defined) {
        if (x < 0) {
            mCursorLabel->clear();
            return;
        }
        mCursorLabel->setText(defined
                ? tr("x=%1  y=%2  depth=%3").arg(x).arg(y).arg(depth)
                : tr("x=%1  y=%2  undefined").arg(x).arg(y));
    });
    connect(mToolGroup,
            qOverload<int>(&QButtonGroup::buttonClicked),
            mCanvas, [this](int id) {
        mCanvas->setTool(static_cast<DepthMapCanvas::Tool>(id));
    });
    connect(mDepthSlider, &QSlider::valueChanged,
            mDepthSpin, &QSpinBox::setValue);
    connect(mDepthSpin,
            qOverload<int>(&QSpinBox::valueChanged),
            mDepthSlider, &QSlider::setValue);
    connect(mDepthSpin,
            qOverload<int>(&QSpinBox::valueChanged),
            mCanvas, &DepthMapCanvas::setBrushDepth);
    connect(mBrushSpin,
            qOverload<int>(&QSpinBox::valueChanged),
            mCanvas, &DepthMapCanvas::setBrushRadius);
    connect(mZoomCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        mCanvas->setZoom(mZoomCombo->itemData(index).toInt());
    });
    for (int axis = 0; axis < 3; ++axis) {
        connect(mTranslateSpins[axis],
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &DepthMapEditor::geometryValuesChanged);
        connect(mRotateSpins[axis],
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &DepthMapEditor::geometryValuesChanged);
        connect(mMinimumSpins[axis],
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &DepthMapEditor::geometryValuesChanged);
        connect(mMaximumSpins[axis],
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &DepthMapEditor::geometryValuesChanged);
        connect(mPixelSizeSpins[axis],
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &DepthMapEditor::geometryPixelSizeChanged);
    }
    connect(mRadiusSpins[0],
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &DepthMapEditor::geometryValuesChanged);
    connect(mRadiusSpins[1],
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &DepthMapEditor::geometryValuesChanged);
    connect(mHeightSpin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &DepthMapEditor::geometryValuesChanged);
    connect(mPlaneCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &DepthMapEditor::geometryValuesChanged);
    connect(mPointsEdit, &QPlainTextEdit::textChanged,
            this, &DepthMapEditor::geometryValuesChanged);
    connect(mSnapPixelCheck, &QCheckBox::toggled,
            this, &DepthMapEditor::snapToPixelGridToggled);
    connect(mModeTabs, &QTabWidget::currentChanged,
            this, [this](int index) {
        mCanvas->setGeometryEditing(index == 0);
        if (index == 0)
            statusBar()->showMessage(
                tr("Geometry mode: click a wireframe to select it; "
                   "drag it to move on X/Z, or drag a gold corner "
                   "handle to resize it."), 5000);
    });

    updatePrimitivePresetUi();
    updateWindowState();
}

DepthMapEditor::~DepthMapEditor() = default;

bool DepthMapEditor::runFormatSelfTest(QString *error)
{
    ScopedDepthMapSettings preservedSettings;

    QImage sourceAtlas(8 * 64, 128, QImage::Format_ARGB32);
    sourceAtlas.fill(Qt::transparent);
    QPainter sourcePainter(&sourceAtlas);
    sourcePainter.fillRect(QRect(3 * 64 + 8, 16, 40, 96),
                           QColor(210, 220, 230));
    sourcePainter.end();

    Tileset tileset(QStringLiteral("depthmap_editor_selftest"), 64, 128);
    if (!tileset.loadFromImage(sourceAtlas,
                               QStringLiteral("depthmap_editor_selftest.png"))) {
        if (error)
            *error = QStringLiteral("Cannot create the self-test tileset");
        return false;
    }

    QTemporaryDir output;
    if (!output.isValid()) {
        if (error)
            *error = QStringLiteral("Cannot create a temporary output folder");
        return false;
    }
    const QString geometryPath = QDir(output.path()).filePath(
                QStringLiteral("tileGeometry.txt"));
    {
        QSaveFile geometrySeed(geometryPath);
        if (!geometrySeed.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error)
                *error = QStringLiteral(
                            "Cannot seed the geometry self-test file");
            return false;
        }
        const QByteArray seed(
            "tileGeometry\n{\n"
            "\tVERSION = 2,\n"
            "\ttileset\n\t{\n"
            "\t\tname = untouched_tileset,\n"
            "\t\ttile\n\t\t{\n"
            "\t\t\txy = 0x0,\n"
            "\t\t\tproperties\n\t\t\t{\n"
            "\t\t\t\tSurface = 5,\n"
            "\t\t\t}\n"
            "\t\t}\n"
            "\t}\n"
            "}\n");
        if (geometrySeed.write(seed) != seed.size() ||
                !geometrySeed.commit()) {
            if (error)
                *error = QStringLiteral(
                            "Cannot commit the geometry self-test file");
            return false;
        }
    }

    DepthMapEditor editor;
    if (!editor.setTileset(&tileset, 3, false)) {
        if (error)
            *error = QStringLiteral("Cannot initialise the editor");
        return false;
    }
    editor.resize(1760, 940);
    editor.show();
    QApplication::processEvents();
    const QList<int> paneSizes = editor.mMainSplitter->sizes();
    if (paneSizes.size() != 3
            || paneSizes.at(0) < 420
            || paneSizes.at(1) < 340
            || paneSizes.at(2) < 550
            || editor.mCanvasScrollArea->viewport()->width() < 330
            || editor.mCanvasScrollArea->viewport()->height() < 480) {
        if (error) {
            *error = QStringLiteral(
                "1920x1080 layout collapsed: panes=%1 canvas=%2x%3")
                .arg(QStringList({
                    QString::number(paneSizes.value(0)),
                    QString::number(paneSizes.value(1)),
                    QString::number(paneSizes.value(2))
                }).join(QLatin1Char(',')))
                .arg(editor.mCanvasScrollArea->viewport()->width())
                .arg(editor.mCanvasScrollArea->viewport()->height());
        }
        editor.hide();
        return false;
    }
    editor.hide();
    editor.mDirectory = output.path();
    editor.mFilePath = QDir(output.path()).filePath(
                editor.expectedFileName());
    if (!editor.loadGeometryFile(geometryPath)) {
        if (error)
            *error = QStringLiteral("Cannot initialise tileGeometry.txt");
        return false;
    }
    DepthPrimitive testBox = DepthPrimitive::makeBox();
    editor.mGeometryDocument.tiles()[3].primitives += testBox;
    editor.mGeometryDirty = true;
    DepthPrimitive resizeProbe = DepthPrimitive::makeBox();
    resizeProbe.maximum.setY(1.0f);
    resizeProbe.translate = QVector3D(
                0.019f, 0.019f, 0.019f);
    editor.scalePrimitive(resizeProbe, 1.5f);
    editor.snapPrimitiveToPixelGrid(resizeProbe);
    const QVector3D resizePixels =
            editor.primitivePixelSize(resizeProbe);
    if (qRound(resizePixels.x()) != 96 ||
            qRound(resizePixels.y()) != 144 ||
            qRound(resizePixels.z()) != 96 ||
            qRound(resizeProbe.translate.x() * 64.0f) != 1 ||
            qRound(resizeProbe.translate.y() * 96.0f) != 2 ||
            qRound(resizeProbe.translate.z() * 64.0f) != 1) {
        if (error)
            *error = QStringLiteral(
                "Primitive pixel sizing or grid snap is incorrect");
        return false;
    }
    const QImage generated = DepthGeometryRasterizer::rasterize(
        testBox, QImage(), false);
    int generatedPixels = 0;
    for (int y = 0; y < generated.height(); ++y) {
        for (int x = 0; x < generated.width(); ++x) {
            if (qAlpha(generated.pixel(x, y)) != 0)
                ++generatedPixels;
        }
    }
    if (generatedPixels < 100) {
        if (error)
            *error = QStringLiteral(
                        "3D box rasterization produced no useful depth");
        return false;
    }
    DepthMapCanvas previewCanvas;
    previewCanvas.setImages(
        editor.sourceTileImage(3), QImage());
    previewCanvas.setGeometry({ testBox }, 0);
    QImage renderedPreview(
        previewCanvas.size(), QImage::Format_ARGB32);
    renderedPreview.fill(Qt::transparent);
    previewCanvas.render(&renderedPreview);
    int selectionPixels = 0;
    for (int y = 0; y < renderedPreview.height(); ++y) {
        for (int x = 0; x < renderedPreview.width(); ++x) {
            const QColor color = renderedPreview.pixelColor(x, y);
            if (color.red() > 210 && color.green() > 135 &&
                    color.blue() < 100)
                ++selectionPixels;
        }
    }
    if (selectionPixels < 10 ||
            selectionPixels > renderedPreview.width() *
                              renderedPreview.height() / 4) {
        if (error)
            *error = QStringLiteral(
                "3D preview is missing or covers the complete canvas");
        return false;
    }

    QImage edited(DepthTileWidth, DepthTileHeight, QImage::Format_ARGB32);
    edited.fill(Qt::transparent);
    edited.setPixel(10, 20, qRgba(173, 173, 173, 255));
    editor.commitCurrentOperation(edited);
    if (qBlue(editor.depthTileImage(3).pixel(10, 20)) != 173) {
        if (error)
            *error = QStringLiteral("Painting did not update the atlas");
        return false;
    }
    editor.undo();
    if (qAlpha(editor.depthTileImage(3).pixel(10, 20)) != 0) {
        if (error)
            *error = QStringLiteral("Undo did not restore transparency");
        return false;
    }
    editor.redo();
    if (qBlue(editor.depthTileImage(3).pixel(10, 20)) != 173) {
        if (error)
            *error = QStringLiteral("Redo did not restore depth");
        return false;
    }
    if (!editor.saveDepthFile()) {
        if (error)
            *error = QStringLiteral("Atomic PNG save failed");
        return false;
    }

    QImage saved(editor.mFilePath);
    if (saved.size() != QSize(1024, 256)) {
        if (error)
            *error = QStringLiteral("Saved atlas is not 1024x256");
        return false;
    }
    const QRgb pixel = saved.pixel(3 * DepthTileWidth + 10, 20);
    if (qAlpha(pixel) != 255 || qRed(pixel) != 173 ||
            qGreen(pixel) != 173 || qBlue(pixel) != 173) {
        if (error)
            *error = QStringLiteral(
                        "Saved depth pixel is not opaque grayscale 173");
        return false;
    }
    if (qAlpha(saved.pixel(3 * DepthTileWidth + 11, 20)) != 0) {
        if (error)
            *error = QStringLiteral(
                        "Undefined pixels did not remain transparent");
        return false;
    }
    QFile geometryFile(geometryPath);
    if (!geometryFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Saved tileGeometry.txt cannot be read");
        return false;
    }
    const QString geometryText = QString::fromUtf8(geometryFile.readAll());
    if (!geometryText.contains(QStringLiteral("untouched_tileset")) ||
            !geometryText.contains(QStringLiteral("Surface = 5")) ||
            !geometryText.contains(
                QStringLiteral("name = depthmap_editor_selftest")) ||
            !geometryText.contains(QStringLiteral("box"))) {
        if (error)
            *error = QStringLiteral(
                "Geometry save did not preserve unrelated data");
        return false;
    }
    DepthGeometryDocument reloadedGeometry;
    QString geometryError;
    if (!reloadedGeometry.load(
            geometryPath, QStringLiteral("depthmap_editor_selftest"),
            &geometryError) ||
            reloadedGeometry.tiles().value(3).primitives.size() != 1) {
        if (error)
            *error = QStringLiteral(
                "Saved geometry did not round-trip: %1")
                .arg(geometryError);
        return false;
    }

    // Optional real-data regression probe. This keeps the normal self-test
    // portable while allowing a packaged executable to verify the exact
    // Build 42 source file selected by a tester.
    const QString externalGeometry =
            qEnvironmentVariable("PZTOOLS_VALIDATE_GEOMETRY_FILE");
    if (!externalGeometry.isEmpty()) {
        const QString externalTileset = qEnvironmentVariable(
            "PZTOOLS_VALIDATE_GEOMETRY_TILESET",
            QStringLiteral("appliances_misc_01"));
        const int externalTile = qEnvironmentVariableIntValue(
            "PZTOOLS_VALIDATE_GEOMETRY_TILE");
        DepthGeometryDocument externalDocument;
        QString externalError;
        if (!externalDocument.load(
                externalGeometry, externalTileset, &externalError) ||
                externalDocument.tiles()
                .value(externalTile).primitives.isEmpty()) {
            if (error)
                *error = QStringLiteral(
                    "External geometry probe failed for %1_%2: %3")
                    .arg(externalTileset).arg(externalTile)
                    .arg(externalError);
            return false;
        }
        Tileset externalTilesetProbe(externalTileset, 64, 128);
        if (!externalTilesetProbe.loadFromImage(
                sourceAtlas, externalTileset + QStringLiteral(".png"))) {
            if (error)
                *error = QStringLiteral(
                    "Cannot create the external geometry UI probe tileset");
            return false;
        }
        DepthMapEditor externalEditorProbe;
        if (!externalEditorProbe.setTileset(
                &externalTilesetProbe, externalTile, false) ||
                !externalEditorProbe.loadGeometryFile(externalGeometry) ||
                externalEditorProbe.mGeometryList->count() < 1) {
            if (error)
                *error = QStringLiteral(
                    "External geometry is parsed but absent from the UI list");
            return false;
        }
    }
    return true;
}

bool DepthMapEditor::setTileset(
        Tileset *tileset, int tileId, bool loadExternalGeometry)
{
    if (!tileset || tileset->tileCount() <= 0)
        return false;
    if (mTileset == tileset) {
        selectTile(tileId);
        return true;
    }
    if (!confirmDiscardChanges())
        return false;

    mTileset = tileset;
    mDirectory = discoverDepthDirectory();
    mDirectoryEdit->setText(nativePath(mDirectory));
    mTilesetLabel->setText(
        tr("%1 — %2 tiles")
        .arg(mTileset->name())
        .arg(mTileset->tileCount()));
    loadExpectedDepthFile();
    if (loadExternalGeometry)
        loadExpectedGeometryFile();
    buildTileTable(tileId);
    updateWindowState();
    return true;
}

void DepthMapEditor::closeEvent(QCloseEvent *event)
{
    if (confirmDiscardChanges())
        event->accept();
    else
        event->ignore();
}

void DepthMapEditor::chooseTileset()
{
    const QList<Tileset *> allTilesets = TileMetaInfoMgr::instance()->tilesets();
    QStringList names;
    QList<Tileset *> candidates;
    for (Tileset *tileset : allTilesets) {
        if (!tileset || tileset->isMissing() || tileset->tileCount() <= 0)
            continue;
        names += tileset->name();
        candidates += tileset;
    }
    if (names.isEmpty()) {
        QMessageBox::information(
            this, tr("Depth Map Editor"),
            tr("No loaded tilesets are available."));
        return;
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, tr("Choose Tileset"), tr("Tileset:"), names,
        qMax(0, names.indexOf(mTileset ? mTileset->name() : QString())),
        false, &accepted);
    if (!accepted)
        return;
    const int index = names.indexOf(selected);
    if (index >= 0)
        setTileset(candidates.at(index));
}

void DepthMapEditor::chooseDepthDirectory()
{
    const QString initial = !mDirectory.isEmpty()
            ? mDirectory : QDir::homePath();
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Choose Build 42 Depthmaps Folder"), initial);
    if (selected.isEmpty())
        return;
    if (!confirmDiscardChanges())
        return;

    mDirectory = QDir::cleanPath(selected);
    QSettings().setValue(
        QStringLiteral("DepthMapEditor/LastDirectory"), mDirectory);
    mDirectoryEdit->setText(nativePath(mDirectory));
    if (mTileset) {
        loadExpectedDepthFile();
        buildTileTable(currentTileId());
    }
    updateWindowState();
}

void DepthMapEditor::openDepthFile()
{
    if (!mTileset) {
        QMessageBox::information(
            this, tr("Depth Map Editor"),
            tr("Choose a source tileset before opening a depth atlas."));
        return;
    }
    if (!confirmDiscardChanges())
        return;

    const QString initial = !mFilePath.isEmpty()
            ? mFilePath
            : QDir(mDirectory).filePath(expectedFileName());
    const QString selected = QFileDialog::getOpenFileName(
        this, tr("Open Build 42 Depth Atlas"), initial,
        tr("PNG images (*.png)"));
    if (selected.isEmpty())
        return;
    if (loadDepthFile(selected)) {
        mDirectory = QFileInfo(selected).absolutePath();
        mDirectoryEdit->setText(nativePath(mDirectory));
        QSettings().setValue(
            QStringLiteral("DepthMapEditor/LastDirectory"), mDirectory);
        buildTileTable(currentTileId());
    }
    updateWindowState();
}

bool DepthMapEditor::saveDepthFile()
{
    if (!mTileset || mAtlas.isNull())
        return false;
    if (mGeometryDirty && !saveGeometryFile())
        return false;
    if (mFilePath.isEmpty()) {
        if (mDirectory.isEmpty())
            return saveDepthFileAs();
        mFilePath = QDir(mDirectory).filePath(expectedFileName());
    }

    QDir destinationDir = QFileInfo(mFilePath).absoluteDir();
    if (!destinationDir.exists() &&
            !destinationDir.mkpath(QStringLiteral("."))) {
        QMessageBox::critical(
            this, tr("Depth Map Save Error"),
            tr("Cannot create the destination folder:\n%1")
            .arg(nativePath(destinationDir.absolutePath())));
        return false;
    }

    QSaveFile file(mFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(
            this, tr("Depth Map Save Error"),
            tr("Cannot open the atlas for writing:\n%1\n\n%2")
            .arg(nativePath(mFilePath), file.errorString()));
        return false;
    }
    if (!mAtlas.save(&file, "PNG")) {
        file.cancelWriting();
        QMessageBox::critical(
            this, tr("Depth Map Save Error"),
            tr("Qt could not encode the depth atlas as PNG:\n%1")
            .arg(nativePath(mFilePath)));
        return false;
    }
    if (!file.commit()) {
        QMessageBox::critical(
            this, tr("Depth Map Save Error"),
            tr("Cannot atomically replace the depth atlas:\n%1\n\n%2")
            .arg(nativePath(mFilePath), file.errorString()));
        return false;
    }

    mSavedRevision = mRevision;
    mDirectory = QFileInfo(mFilePath).absolutePath();
    mDirectoryEdit->setText(nativePath(mDirectory));
    QSettings().setValue(
        QStringLiteral("DepthMapEditor/LastDirectory"), mDirectory);
    statusBar()->showMessage(
        tr("Saved %1").arg(nativePath(mFilePath)), 6000);
    updateWindowState();
    return true;
}

bool DepthMapEditor::saveDepthFileAs()
{
    if (!mTileset || mAtlas.isNull())
        return false;
    const QString initial = !mFilePath.isEmpty()
            ? mFilePath
            : QDir(mDirectory).filePath(expectedFileName());
    QString selected = QFileDialog::getSaveFileName(
        this, tr("Save Build 42 Depth Atlas"), initial,
        tr("PNG images (*.png)"));
    if (selected.isEmpty())
        return false;
    if (!selected.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        selected += QLatin1String(".png");

    const QString expected = expectedFileName();
    if (QFileInfo(selected).fileName().compare(
            expected, Qt::CaseInsensitive) != 0) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, tr("Non-standard Depth Atlas Name"),
            tr("Build 42 expects this tileset depth atlas to be named:\n"
               "%1\n\nSave using the selected non-standard name anyway?")
            .arg(expected),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
    }

    mFilePath = QDir::cleanPath(selected);
    return saveDepthFile();
}

void DepthMapEditor::reloadDepthFile()
{
    if (!mTileset)
        return;
    if (!confirmDiscardChanges())
        return;
    const int selectedTile = currentTileId();
    if (mFilePath.isEmpty())
        loadExpectedDepthFile();
    else
        loadDepthFile(mFilePath);
    buildTileTable(selectedTile);
    updateWindowState();
}

void DepthMapEditor::chooseGeometryFile()
{
    if (!mTileset) {
        QMessageBox::information(
            this, tr("Depth Map Editor"),
            tr("Choose a source tileset before choosing tileGeometry.txt."));
        return;
    }
    if (mGeometryDirty) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, tr("Unsaved 3D Geometry"),
            tr("The current tile geometry has unsaved changes."),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save && !saveGeometryFile())
            return;
    }
    const QString initial = !mGeometryFilePath.isEmpty()
            ? mGeometryFilePath : discoverGeometryFile();
    QString selected = QFileDialog::getOpenFileName(
        this, tr("Choose Build 42 tileGeometry.txt"), initial,
        tr("Tile geometry (tileGeometry.txt);;Text files (*.txt)"));
    if (selected.isEmpty()) {
        selected = QFileDialog::getSaveFileName(
            this, tr("Create Build 42 tileGeometry.txt"), initial,
            tr("Tile geometry (tileGeometry.txt);;Text files (*.txt)"));
    }
    if (!selected.isEmpty())
        loadGeometryFile(QDir::cleanPath(selected));
}

bool DepthMapEditor::loadExpectedGeometryFile()
{
    if (!mTileset)
        return false;
    return loadGeometryFile(discoverGeometryFile());
}

bool DepthMapEditor::loadGeometryFile(const QString &filePath)
{
    QString error;
    if (!mGeometryDocument.load(
            filePath, tilesetBaseName(), &error)) {
        QMessageBox::critical(
            this, tr("Tile Geometry Load Error"),
            tr("Cannot load tileGeometry.txt:\n%1\n\n%2")
            .arg(nativePath(filePath), error));
        return false;
    }
    mGeometryFilePath = QDir::cleanPath(filePath);
    mGeometryDirty = false;
    QSettings().setValue(
        QStringLiteral("DepthMapEditor/LastGeometryFile"),
        mGeometryFilePath);
    updateGeometryList();
    updateWindowState();
    statusBar()->showMessage(
        QFileInfo::exists(filePath)
        ? tr("Loaded geometry from %1").arg(nativePath(filePath))
        : tr("New geometry file will be created at %1")
          .arg(nativePath(filePath)),
        6000);
    return true;
}

bool DepthMapEditor::saveGeometryFile()
{
    if (!mTileset)
        return false;
    if (mGeometryFilePath.isEmpty())
        return saveGeometryFileAs();
    QString error;
    if (!mGeometryDocument.save(
            mGeometryFilePath, tilesetBaseName(), &error)) {
        QMessageBox::critical(
            this, tr("Tile Geometry Save Error"),
            tr("Cannot save tileGeometry.txt:\n%1\n\n%2")
            .arg(nativePath(mGeometryFilePath), error));
        return false;
    }
    mGeometryDirty = false;
    QSettings().setValue(
        QStringLiteral("DepthMapEditor/LastGeometryFile"),
        mGeometryFilePath);
    updateWindowState();
    statusBar()->showMessage(
        tr("Saved geometry to %1")
        .arg(nativePath(mGeometryFilePath)), 6000);
    return true;
}

bool DepthMapEditor::saveGeometryFileAs()
{
    const QString initial = mGeometryFilePath.isEmpty()
            ? discoverGeometryFile() : mGeometryFilePath;
    const QString selected = QFileDialog::getSaveFileName(
        this, tr("Save Build 42 tileGeometry.txt"), initial,
        tr("Tile geometry (tileGeometry.txt);;Text files (*.txt)"));
    if (selected.isEmpty())
        return false;
    mGeometryFilePath = QDir::cleanPath(selected);
    return saveGeometryFile();
}

void DepthMapEditor::currentCellChanged(
        int currentRow, int currentColumn, int, int)
{
    const int tileId = tileIdAt(currentRow, currentColumn);
    if (tileId < 0)
        return;
    updateCurrentTile();
}

void DepthMapEditor::canvasEditStarted(const QImage &before)
{
    mStrokeBefore = before;
}

void DepthMapEditor::canvasEditFinished(const QImage &after)
{
    if (mStrokeBefore.isNull())
        return;
    pushEdit(currentTileId(), mStrokeBefore, after);
    mStrokeBefore = QImage();
}

void DepthMapEditor::setPickedDepth(int depth)
{
    mDepthSpin->setValue(qBound(0, depth, 255));
}

void DepthMapEditor::clearCurrentTile()
{
    if (currentTileId() < 0)
        return;
    QImage cleared(DepthTileWidth, DepthTileHeight, QImage::Format_ARGB32);
    cleared.fill(Qt::transparent);
    commitCurrentOperation(cleared);
}

void DepthMapEditor::fillSourceMask()
{
    if (currentTileId() < 0)
        return;
    const QImage source = mCanvas->sourceImage();
    QImage result(DepthTileWidth, DepthTileHeight, QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    const int depth = mDepthSpin->value();
    for (int y = 0; y < result.height(); ++y) {
        for (int x = 0; x < result.width(); ++x) {
            if (qAlpha(source.pixel(x, y)) != 0)
                result.setPixel(x, y, qRgba(depth, depth, depth, 255));
        }
    }
    commitCurrentOperation(result);
}

void DepthMapEditor::seedVerticalDepth()
{
    if (currentTileId() < 0)
        return;
    const QImage source = mCanvas->sourceImage();
    QImage result(DepthTileWidth, DepthTileHeight, QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    for (int y = 0; y < result.height(); ++y) {
        const int depth = 1 + qRound(254.0 * y /
                                     qMax(1, result.height() - 1));
        for (int x = 0; x < result.width(); ++x) {
            if (qAlpha(source.pixel(x, y)) != 0)
                result.setPixel(x, y, qRgba(depth, depth, depth, 255));
        }
    }
    commitCurrentOperation(result);
}

void DepthMapEditor::copyCurrentDepth()
{
    if (currentTileId() < 0)
        return;
    mDepthClipboard = mCanvas->depthImage();
    statusBar()->showMessage(
        tr("Copied depth for tile %1").arg(currentTileId()), 3000);
    updateActions();
}

void DepthMapEditor::pasteDepth()
{
    if (currentTileId() < 0 || mDepthClipboard.isNull())
        return;
    commitCurrentOperation(mDepthClipboard);
}

void DepthMapEditor::undo()
{
    if (mEditIndex <= 0)
        return;
    const Edit &edit = mEdits.at(mEditIndex - 1);
    --mEditIndex;
    applyEditImage(edit.tileId, edit.before);
    setDirtyRevision(edit.beforeRevision);
}

void DepthMapEditor::redo()
{
    if (mEditIndex >= mEdits.size())
        return;
    const Edit &edit = mEdits.at(mEditIndex);
    ++mEditIndex;
    applyEditImage(edit.tileId, edit.after);
    setDirtyRevision(edit.afterRevision);
}

void DepthMapEditor::addPolygonXY()
{
    addGeometry(DepthPrimitive::makePolygon(DepthPolygonPlane::XY));
}

void DepthMapEditor::addPolygonXZ()
{
    addGeometry(DepthPrimitive::makePolygon(DepthPolygonPlane::XZ));
}

void DepthMapEditor::addPolygonYZ()
{
    addGeometry(DepthPrimitive::makePolygon(DepthPolygonPlane::YZ));
}

void DepthMapEditor::addBox()
{
    addGeometry(DepthPrimitive::makeBox());
}

void DepthMapEditor::addCylinder()
{
    addGeometry(DepthPrimitive::makeCylinder());
}

void DepthMapEditor::duplicateGeometry()
{
    const int index = selectedGeometryIndex();
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index < 0 || index >= geometry.size())
        return;
    DepthPrimitive copy = geometry.at(index);
    copy.translate += QVector3D(1.0f / 64.0f, 0.0f, 1.0f / 64.0f);
    geometry.insert(index + 1, copy);
    setGeometryDirty();
    updateGeometryList(index + 1);
}

void DepthMapEditor::removeGeometry()
{
    const int index = selectedGeometryIndex();
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index < 0 || index >= geometry.size())
        return;
    geometry.removeAt(index);
    setGeometryDirty();
    updateGeometryList(qMin(index, geometry.size() - 1));
}

void DepthMapEditor::geometrySelectionChanged()
{
    updateGeometryControls();
    updateCanvasGeometry();
}

void DepthMapEditor::geometryValuesChanged()
{
    if (mUpdatingGeometryUi)
        return;
    const int index = selectedGeometryIndex();
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index < 0 || index >= geometry.size())
        return;
    DepthPrimitive &primitive = geometry[index];
    primitive.translate = QVector3D(
        float(mTranslateSpins[0]->value()),
        float(mTranslateSpins[1]->value()),
        float(mTranslateSpins[2]->value()));
    primitive.rotate = QVector3D(
        float(mRotateSpins[0]->value()),
        float(mRotateSpins[1]->value()),
        float(mRotateSpins[2]->value()));
    if (primitive.type == DepthPrimitiveType::Box) {
        primitive.minimum = QVector3D(
            float(mMinimumSpins[0]->value()),
            float(mMinimumSpins[1]->value()),
            float(mMinimumSpins[2]->value()));
        primitive.maximum = QVector3D(
            float(mMaximumSpins[0]->value()),
            float(mMaximumSpins[1]->value()),
            float(mMaximumSpins[2]->value()));
    } else if (primitive.type == DepthPrimitiveType::Cylinder) {
        primitive.radius1 = float(mRadiusSpins[0]->value());
        primitive.radius2 = float(mRadiusSpins[1]->value());
        primitive.height = float(mHeightSpin->value());
    } else {
        primitive.plane = static_cast<DepthPolygonPlane>(
                    mPlaneCombo->currentIndex());
        QVector<QPointF> points;
        const QStringList pairs = mPointsEdit->toPlainText().split(
                    QRegularExpression(QStringLiteral("[;\\r\\n]+")),
                    Qt::SkipEmptyParts);
        for (const QString &pair : pairs) {
            const QStringList values = pair.trimmed().split(
                        QRegularExpression(QStringLiteral("[,\\s]+")),
                        Qt::SkipEmptyParts);
            if (values.size() != 2)
                continue;
            bool xOk = false;
            bool yOk = false;
            const double x = values.at(0).toDouble(&xOk);
            const double y = values.at(1).toDouble(&yOk);
            if (xOk && yOk)
                points += QPointF(x, y);
        }
        if (points.size() >= 3)
            primitive.points = points;
    }
    if (mSnapPixelCheck && mSnapPixelCheck->isChecked())
        snapPrimitiveToPixelGrid(primitive);
    setGeometryDirty();
    updateGeometryControls();
    updateCanvasGeometry();
}

void DepthMapEditor::geometryPixelSizeChanged()
{
    if (mUpdatingGeometryUi)
        return;
    const int index = selectedGeometryIndex();
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index < 0 || index >= geometry.size())
        return;

    DepthPrimitive &primitive = geometry[index];
    for (int dimension = 0; dimension < 3; ++dimension) {
        if (mPixelSizeSpins[dimension]->isVisible() &&
                mPixelSizeSpins[dimension]->isEnabled()) {
            setPrimitivePixelSize(
                        primitive, dimension,
                        mPixelSizeSpins[dimension]->value());
        }
    }
    if (mSnapPixelCheck && mSnapPixelCheck->isChecked())
        snapPrimitiveToPixelGrid(primitive);
    setGeometryDirty();
    updateGeometryControls();
    updateCanvasGeometry();
}

void DepthMapEditor::snapToPixelGridToggled(bool enabled)
{
    QSettings settings;
    settings.setValue(
                QLatin1String("DepthMapEditor/SnapToPixelGrid"),
                enabled);
    for (QDoubleSpinBox *spin : mPixelSizeSpins)
        spin->setDecimals(enabled ? 0 : 2);

    if (!enabled)
        return;
    const int index = selectedGeometryIndex();
    if (index < 0 || currentTileId() < 0)
        return;
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index >= geometry.size())
        return;
    snapPrimitiveToPixelGrid(geometry[index]);
    setGeometryDirty();
    updateGeometryControls();
    updateCanvasGeometry();
}

void DepthMapEditor::savePrimitivePreset()
{
    const int index = selectedGeometryIndex();
    const QVector<DepthPrimitive> geometry = currentGeometryValue();
    if (!mTileset || index < 0 || index >= geometry.size())
        return;

    bool accepted = false;
    const QString name = QInputDialog::getText(
                this, tr("Save Primitive Preset"),
                tr("Preset name for %1:")
                .arg(tilesetBaseName()),
                QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    QVector<PrimitivePreset> presets = readPrimitivePresets();
    int replaceIndex = -1;
    for (int presetIndex = 0;
         presetIndex < presets.size(); ++presetIndex) {
        if (presets.at(presetIndex).name.compare(
                    name, Qt::CaseInsensitive) == 0) {
            replaceIndex = presetIndex;
            break;
        }
    }
    if (replaceIndex >= 0 &&
            QMessageBox::question(
                this, tr("Replace Primitive Preset"),
                tr("A reusable preset named '%1' already exists. "
                   "Replace it?").arg(name),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    PrimitivePreset preset;
    preset.name = name;
    preset.tileset = tilesetBaseName();
    preset.primitive = geometry.at(index);
    if (replaceIndex >= 0)
        presets[replaceIndex] = preset;
    else
        presets += preset;
    writePrimitivePresets(presets);
    updatePrimitivePresetUi();
    const int comboIndex = mPresetCombo->findText(
                name, Qt::MatchContains);
    if (comboIndex >= 0)
        mPresetCombo->setCurrentIndex(comboIndex);
    statusBar()->showMessage(
                tr("Primitive preset '%1' saved from %2.")
                .arg(name, tilesetBaseName()), 5000);
}

void DepthMapEditor::insertPrimitivePreset()
{
    const int presetIndex = mPresetCombo
            ? mPresetCombo->currentIndex() : -1;
    if (presetIndex < 0 ||
            presetIndex >= mVisiblePresets.size() ||
            currentTileId() < 0) {
        return;
    }
    addGeometry(mVisiblePresets.at(presetIndex).primitive);
    statusBar()->showMessage(
                tr("Inserted primitive preset '%1'.")
                .arg(mVisiblePresets.at(presetIndex).name), 5000);
}

void DepthMapEditor::deletePrimitivePreset()
{
    const int presetIndex = mPresetCombo
            ? mPresetCombo->currentIndex() : -1;
    if (presetIndex < 0 ||
            presetIndex >= mVisiblePresets.size())
        return;
    const PrimitivePreset selected =
            mVisiblePresets.at(presetIndex);
    if (QMessageBox::question(
                this, tr("Delete Primitive Preset"),
                tr("Delete preset '%1' for %2?")
                .arg(selected.name, selected.tileset),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    QVector<PrimitivePreset> presets = readPrimitivePresets();
    for (int index = 0; index < presets.size(); ++index) {
        const PrimitivePreset &preset = presets.at(index);
        if (preset.tileset == selected.tileset &&
                preset.name == selected.name &&
                preset.primitive.type == selected.primitive.type) {
            presets.removeAt(index);
            break;
        }
    }
    writePrimitivePresets(presets);
    updatePrimitivePresetUi();
}

void DepthMapEditor::generateSelectedGeometry()
{
    const int index = selectedGeometryIndex();
    const QVector<DepthPrimitive> geometry = currentGeometryValue();
    if (index < 0 || index >= geometry.size())
        return;
    const QImage result = DepthGeometryRasterizer::rasterize(
        geometry.at(index), mCanvas->sourceImage(),
        mRespectAlphaCheck->isChecked(), depthTileImage(currentTileId()));
    commitCurrentOperation(result);
}

void DepthMapEditor::generateAllGeometry()
{
    const QVector<DepthPrimitive> geometry = currentGeometryValue();
    if (currentTileId() < 0 || geometry.isEmpty())
        return;
    const QImage result = DepthGeometryRasterizer::rasterize(
        geometry, mCanvas->sourceImage(),
        mRespectAlphaCheck->isChecked());
    commitCurrentOperation(result);
}

void DepthMapEditor::canvasGeometryPicked(int index)
{
    if (mGeometryList && index >= 0 &&
            index < mGeometryList->count())
        mGeometryList->setCurrentRow(index);
}

void DepthMapEditor::canvasGeometryTranslated(
        int index, const QVector3D &delta)
{
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index < 0 || index >= geometry.size())
        return;
    geometry[index].translate += delta;
    if (mSnapPixelCheck && mSnapPixelCheck->isChecked())
        snapPrimitiveToPixelGrid(geometry[index]);
    setGeometryDirty();
    updateGeometryControls();
    updateCanvasGeometry();
}

void DepthMapEditor::canvasGeometryScaled(int index, float factor)
{
    QVector<DepthPrimitive> &geometry = currentGeometry();
    if (index < 0 || index >= geometry.size())
        return;
    scalePrimitive(geometry[index], factor);
    if (mSnapPixelCheck && mSnapPixelCheck->isChecked())
        snapPrimitiveToPixelGrid(geometry[index]);
    setGeometryDirty();
    updateGeometryControls();
    updateCanvasGeometry();
}

bool DepthMapEditor::confirmDiscardChanges()
{
    if (mRevision == mSavedRevision && !mGeometryDirty)
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Unsaved Depth Data"),
        tr("The depth atlas or its 3D geometry has unsaved changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveDepthFile();
    return answer == QMessageBox::Discard;
}

bool DepthMapEditor::loadExpectedDepthFile()
{
    if (!mTileset)
        return false;
    if (mDirectory.isEmpty())
        mDirectory = discoverDepthDirectory();
    mDirectoryEdit->setText(nativePath(mDirectory));

    const QString expectedPath = mDirectory.isEmpty()
            ? QString()
            : QDir(mDirectory).filePath(expectedFileName());
    if (!expectedPath.isEmpty() && QFileInfo::exists(expectedPath))
        return loadDepthFile(expectedPath);

    mFilePath = expectedPath;
    mAtlas = QImage(expectedAtlasSize(), QImage::Format_ARGB32);
    mAtlas.fill(Qt::transparent);
    mEdits.clear();
    mEditIndex = 0;
    mRevision = 0;
    mSavedRevision = 0;
    mNextRevision = 1;
    statusBar()->showMessage(
        expectedPath.isEmpty()
        ? tr("New atlas. Choose a depthmaps folder before saving.")
        : tr("New atlas; %1 does not exist yet.")
          .arg(nativePath(expectedPath)),
        7000);
    return true;
}

bool DepthMapEditor::loadDepthFile(const QString &filePath)
{
    QImage image;
    if (!image.load(filePath)) {
        QMessageBox::critical(
            this, tr("Depth Map Load Error"),
            tr("Cannot load the depth atlas:\n%1")
            .arg(nativePath(filePath)));
        return false;
    }

    QString error;
    if (!initialiseAtlas(image, &error)) {
        QMessageBox::critical(
            this, tr("Depth Map Geometry Error"), error);
        return false;
    }

    mFilePath = QDir::cleanPath(filePath);
    mEdits.clear();
    mEditIndex = 0;
    mRevision = 0;
    mSavedRevision = 0;
    mNextRevision = 1;
    statusBar()->showMessage(
        tr("Loaded %1").arg(nativePath(mFilePath)), 5000);
    return true;
}

bool DepthMapEditor::initialiseAtlas(
        const QImage &sourceImage, QString *error)
{
    if (!mTileset) {
        if (error)
            *error = tr("No source tileset is selected.");
        return false;
    }

    const QSize expected = expectedAtlasSize();
    const bool exactGeometry = sourceImage.size() == expected;
    // The game loader accepts atlases with omitted empty columns/rows even
    // though its own save routine writes the complete eight-column image.
    const bool safelyTrimmed =
            sourceImage.width() > 0 &&
            sourceImage.height() > 0 &&
            sourceImage.width() <= expected.width() &&
            sourceImage.height() <= expected.height() &&
            sourceImage.width() % DepthTileWidth == 0 &&
            sourceImage.height() % DepthTileHeight == 0;
    if (!exactGeometry && !safelyTrimmed) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, tr("Depth Map Geometry Mismatch"),
            tr("This PNG is %1 × %2 pixels, but Build 42 expects "
               "%3 × %4 for %5 tiles (8 columns of 128 × 256).\n\n"
               "Adapt it to the expected size while preserving all "
               "overlapping pixels?")
            .arg(sourceImage.width()).arg(sourceImage.height())
            .arg(expected.width()).arg(expected.height())
            .arg(mTileset->tileCount()),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            if (error)
                *error = tr("The depth atlas geometry was not changed.");
            return false;
        }
    }

    mAtlas = QImage(expected, QImage::Format_ARGB32);
    mAtlas.fill(Qt::transparent);
    const QImage converted = sourceImage.convertToFormat(QImage::Format_ARGB32);
    const int copyWidth = qMin(mAtlas.width(), converted.width());
    const int copyHeight = qMin(mAtlas.height(), converted.height());
    for (int y = 0; y < copyHeight; ++y) {
        for (int x = 0; x < copyWidth; ++x) {
            const QRgb pixel = converted.pixel(x, y);
            if (qAlpha(pixel) == 0)
                continue;
            const int depth = qBlue(pixel);
            mAtlas.setPixel(x, y, qRgba(depth, depth, depth, 255));
        }
    }
    return true;
}

QString DepthMapEditor::expectedFileName() const
{
    return QStringLiteral("DEPTH_%1.png").arg(tilesetBaseName());
}

QString DepthMapEditor::discoverDepthDirectory() const
{
    const QString stored = QSettings().value(
        QStringLiteral("DepthMapEditor/LastDirectory")).toString();
    if (!stored.isEmpty() && QDir(stored).exists())
        return QDir::cleanPath(stored);
    if (!mTileset)
        return QString();

    QString imagePath = mTileset->imageSource2x();
    if (imagePath.isEmpty())
        imagePath = mTileset->imageSource();
    QDir cursor = QFileInfo(imagePath).absoluteDir();
    for (int level = 0; level < 6; ++level) {
        const QString direct = cursor.filePath(QStringLiteral("depthmaps"));
        if (QDir(direct).exists())
            return QDir::cleanPath(direct);
        const QString media = cursor.filePath(
                    QStringLiteral("media/depthmaps"));
        if (QDir(media).exists())
            return QDir::cleanPath(media);
        if (!cursor.cdUp())
            break;
    }

    QDir applicationDir(QApplication::applicationDirPath());
    const QString appCandidate =
            applicationDir.filePath(QStringLiteral("../depthmaps"));
    if (QDir(appCandidate).exists())
        return QDir::cleanPath(appCandidate);
    return QString();
}

QString DepthMapEditor::discoverGeometryFile() const
{
    const QString stored = QSettings().value(
        QStringLiteral("DepthMapEditor/LastGeometryFile")).toString();
    if (!stored.isEmpty() && QFileInfo::exists(stored))
        return QDir::cleanPath(stored);

    QString imagePath;
    if (mTileset) {
        imagePath = mTileset->imageSource2x();
        if (imagePath.isEmpty())
            imagePath = mTileset->imageSource();
    }
    QDir cursor = QFileInfo(imagePath).absoluteDir();
    if (!imagePath.isEmpty()) {
        for (int level = 0; level < 7; ++level) {
            const QString direct =
                    cursor.filePath(QStringLiteral("tileGeometry.txt"));
            if (QFileInfo::exists(direct))
                return QDir::cleanPath(direct);
            const QString media = cursor.filePath(
                        QStringLiteral("media/tileGeometry.txt"));
            if (QFileInfo::exists(media))
                return QDir::cleanPath(media);
            if (!cursor.cdUp())
                break;
        }
    }
    if (!mDirectory.isEmpty()) {
        QDir depthDirectory(mDirectory);
        const QString besideDepthmaps =
                depthDirectory.absoluteFilePath(
                    QStringLiteral("../tileGeometry.txt"));
        if (QFileInfo::exists(besideDepthmaps))
            return QDir::cleanPath(besideDepthmaps);
        return QDir::cleanPath(besideDepthmaps);
    }
    if (!stored.isEmpty())
        return QDir::cleanPath(stored);
    return QDir(QApplication::applicationDirPath()).filePath(
                QStringLiteral("../tileGeometry.txt"));
}

QString DepthMapEditor::tilesetBaseName() const
{
    if (!mTileset)
        return QStringLiteral("tileset");
    QString source = mTileset->imageSource2x();
    if (source.isEmpty())
        source = mTileset->imageSource();
    const QString sourceName = QFileInfo(source).completeBaseName();
    return sourceName.isEmpty() ? mTileset->name() : sourceName;
}

QSize DepthMapEditor::expectedAtlasSize() const
{
    const int tileCount = mTileset ? mTileset->tileCount() : 0;
    const int rows = qMax(1, (tileCount + DepthColumns - 1) / DepthColumns);
    return QSize(DepthColumns * DepthTileWidth,
                 rows * DepthTileHeight);
}

void DepthMapEditor::buildTileTable(int selectedTileId)
{
    mTileTable->clearContents();
    if (!mTileset) {
        mTileTable->setRowCount(0);
        updateCurrentTile();
        return;
    }

    const int rows = (mTileset->tileCount() +
                      DepthColumns - 1) / DepthColumns;
    mTileTable->setRowCount(rows);
    for (int column = 0; column < DepthColumns; ++column)
        mTileTable->setColumnWidth(column, 52);
    for (int row = 0; row < rows; ++row)
        mTileTable->setRowHeight(row, 112);

    for (int tileId = 0; tileId < mTileset->tileCount(); ++tileId) {
        QTableWidgetItem *item = new QTableWidgetItem;
        // The ID is painted into the icon itself in updateTileIcon(). The
        // native icon+text item layout clips labels once they reach two
        // digits in these intentionally narrow eight-column cells.
        item->setText(QString());
        item->setData(Qt::UserRole, tileId);
        item->setToolTip(
            tr("%1_%2%3")
            .arg(tilesetBaseName()).arg(tileId)
            .arg(tileHasDepth(tileId)
                 ? tr("\nOwn depth pixels present")
                 : tr("\nNo own depth pixels")));
        mTileTable->setItem(tileId / DepthColumns,
                            tileId % DepthColumns, item);
        updateTileIcon(tileId);
    }
    selectTile(selectedTileId);
}

void DepthMapEditor::selectTile(int tileId)
{
    if (!mTileset || mTileset->tileCount() <= 0)
        return;
    if (tileId < 0 || tileId >= mTileset->tileCount())
        tileId = 0;
    mTileTable->setCurrentCell(tileId / DepthColumns,
                               tileId % DepthColumns);
    mTileTable->scrollToItem(
        mTileTable->item(tileId / DepthColumns,
                         tileId % DepthColumns),
        QAbstractItemView::PositionAtCenter);
    updateCurrentTile();
}

int DepthMapEditor::tileIdAt(int row, int column) const
{
    if (!mTileset || row < 0 || column < 0)
        return -1;
    QTableWidgetItem *item = mTileTable->item(row, column);
    if (!item)
        return -1;
    const int tileId = item->data(Qt::UserRole).toInt();
    return tileId >= 0 && tileId < mTileset->tileCount()
            ? tileId : -1;
}

int DepthMapEditor::currentTileId() const
{
    return tileIdAt(mTileTable->currentRow(),
                    mTileTable->currentColumn());
}

QImage DepthMapEditor::sourceTileImage(int tileId) const
{
    if (!mTileset || tileId < 0 || tileId >= mTileset->tileCount())
        return QImage();
    Tile *tile = mTileset->tileAt(tileId);
    if (!tile)
        return QImage();
    return tile->finalImage(DepthTileWidth, DepthTileHeight);
}

QImage DepthMapEditor::depthTileImage(int tileId) const
{
    if (mAtlas.isNull() || tileId < 0 ||
            !mTileset || tileId >= mTileset->tileCount())
        return QImage();
    return normalisedDepthTile(mAtlas.copy(
        (tileId % DepthColumns) * DepthTileWidth,
        (tileId / DepthColumns) * DepthTileHeight,
        DepthTileWidth, DepthTileHeight));
}

void DepthMapEditor::setDepthTileImage(
        int tileId, const QImage &image)
{
    if (mAtlas.isNull() || tileId < 0 ||
            !mTileset || tileId >= mTileset->tileCount())
        return;
    QPainter painter(&mAtlas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(
        QPoint((tileId % DepthColumns) * DepthTileWidth,
               (tileId / DepthColumns) * DepthTileHeight),
        normalisedDepthTile(image));
}

bool DepthMapEditor::tileHasDepth(int tileId) const
{
    const QImage tile = depthTileImage(tileId);
    for (int y = 0; y < tile.height(); ++y) {
        const QRgb *line =
                reinterpret_cast<const QRgb *>(tile.constScanLine(y));
        for (int x = 0; x < tile.width(); ++x) {
            if (qAlpha(line[x]) != 0)
                return true;
        }
    }
    return false;
}

void DepthMapEditor::updateTileIcon(int tileId)
{
    if (!mTileset || tileId < 0 || tileId >= mTileset->tileCount())
        return;
    QTableWidgetItem *item = mTileTable->item(
                tileId / DepthColumns, tileId % DepthColumns);
    if (!item)
        return;

    QImage preview(52, 100, QImage::Format_ARGB32_Premultiplied);
    preview.fill(QColor(52, 55, 60));
    QPainter painter(&preview);
    const QImage source = sourceTileImage(tileId);
    const QImage scaled = source.scaled(
                48, 96, Qt::KeepAspectRatio,
                Qt::FastTransformation);
    painter.drawImage(
        QPoint((preview.width() - scaled.width()) / 2,
               (preview.height() - scaled.height()) / 2),
        scaled);
    painter.setPen(QPen(tileHasDepth(tileId)
                       ? QColor(55, 160, 255)
                       : QColor(105, 110, 118),
                       tileHasDepth(tileId) ? 3 : 1));
    painter.drawRect(preview.rect().adjusted(1, 1, -2, -2));
    QFont idFont = painter.font();
    idFont.setBold(true);
    idFont.setPixelSize(12);
    painter.setFont(idFont);
    const QString idText = QString::number(tileId);
    const QFontMetrics metrics(idFont);
    const int badgeWidth = qMax(18, metrics.horizontalAdvance(idText) + 8);
    const QRect badgeRect(3, 3, badgeWidth, 18);
    painter.setPen(QPen(QColor(230, 235, 242), 1));
    painter.setBrush(QColor(20, 23, 28, 225));
    painter.drawRoundedRect(badgeRect, 4, 4);
    painter.drawText(badgeRect, Qt::AlignCenter, idText);
    item->setIcon(QIcon(QPixmap::fromImage(preview)));
    item->setToolTip(
        tr("%1_%2%3")
        .arg(tilesetBaseName()).arg(tileId)
        .arg(tileHasDepth(tileId)
             ? tr("\nOwn depth pixels present")
             : tr("\nNo own depth pixels")));
}

void DepthMapEditor::updateCurrentTile()
{
    const int tileId = currentTileId();
    if (!mTileset || tileId < 0) {
        mCanvas->setImages(QImage(), QImage());
        mCanvas->setGeometry({}, -1);
        mTileLabel->setText(tr("No tile selected"));
        updateGeometryList();
        updatePrimitivePresetUi();
        updateActions();
        return;
    }
    mCanvas->setImages(sourceTileImage(tileId),
                       depthTileImage(tileId));
    mTileLabel->setText(
        tr("%1_%2 — depth tile [%3, %4]")
        .arg(tilesetBaseName()).arg(tileId)
        .arg(tileId % DepthColumns)
        .arg(tileId / DepthColumns));
    updateGeometryList();
    updatePrimitivePresetUi();
    updateActions();
}

void DepthMapEditor::updateGeometryList(int selectedIndex)
{
    if (!mGeometryList)
        return;
    if (selectedIndex < 0)
        selectedIndex = mGeometryList->currentRow();
    const QVector<DepthPrimitive> geometry = currentGeometryValue();
    mUpdatingGeometryUi = true;
    mGeometryList->clear();
    for (int index = 0; index < geometry.size(); ++index)
        mGeometryList->addItem(geometry.at(index).displayName(index));
    if (!geometry.isEmpty())
        mGeometryList->setCurrentRow(
            qBound(0, selectedIndex, geometry.size() - 1));
    mUpdatingGeometryUi = false;
    updateGeometryControls();
    updateCanvasGeometry();
}

void DepthMapEditor::updateGeometryControls()
{
    const int index = selectedGeometryIndex();
    const QVector<DepthPrimitive> geometry = currentGeometryValue();
    const bool valid = index >= 0 && index < geometry.size();
    if (mShapeStack)
        mShapeStack->setEnabled(valid);
    for (int axis = 0; axis < 3; ++axis) {
        mTranslateSpins[axis]->setEnabled(valid);
        mRotateSpins[axis]->setEnabled(valid);
        mPixelSizeSpins[axis]->setEnabled(valid);
        mPixelSizeSpins[axis]->setDecimals(
                    mSnapPixelCheck && mSnapPixelCheck->isChecked()
                    ? 0 : 2);
        mPixelSizeLabels[axis]->setVisible(true);
        mPixelSizeSpins[axis]->setVisible(true);
    }
    if (!valid) {
        for (int dimension = 0; dimension < 3; ++dimension)
            mPixelSizeLabels[dimension]->setText(
                        tr("Dimension %1")
                        .arg(dimension + 1));
        return;
    }

    const DepthPrimitive &primitive = geometry.at(index);
    if (primitive.type == DepthPrimitiveType::Box) {
        mPixelSizeLabels[0]->setText(tr("Width X"));
        mPixelSizeLabels[1]->setText(tr("Height Y"));
        mPixelSizeLabels[2]->setText(tr("Depth Z"));
    } else if (primitive.type == DepthPrimitiveType::Cylinder) {
        mPixelSizeLabels[0]->setText(tr("Diameter"));
        mPixelSizeLabels[1]->setText(tr("Height"));
        mPixelSizeLabels[2]->setVisible(false);
        mPixelSizeSpins[2]->setVisible(false);
    } else {
        mPixelSizeLabels[0]->setText(tr("Plane width"));
        mPixelSizeLabels[1]->setText(tr("Plane height"));
        mPixelSizeLabels[2]->setVisible(false);
        mPixelSizeSpins[2]->setVisible(false);
    }

    mUpdatingGeometryUi = true;
    for (int axis = 0; axis < 3; ++axis) {
        mTranslateSpins[axis]->setValue(primitive.translate[axis]);
        mRotateSpins[axis]->setValue(primitive.rotate[axis]);
        mMinimumSpins[axis]->setValue(primitive.minimum[axis]);
        mMaximumSpins[axis]->setValue(primitive.maximum[axis]);
    }
    mRadiusSpins[0]->setValue(primitive.radius1);
    mRadiusSpins[1]->setValue(primitive.radius2);
    mHeightSpin->setValue(primitive.height);
    const QVector3D pixelSize = primitivePixelSize(primitive);
    for (int dimension = 0; dimension < 3; ++dimension)
        mPixelSizeSpins[dimension]->setValue(pixelSize[dimension]);
    mPlaneCombo->setCurrentIndex(static_cast<int>(primitive.plane));
    QStringList points;
    for (const QPointF &point : primitive.points) {
        points += QStringLiteral("%1,%2")
                .arg(point.x(), 0, 'f', 4)
                .arg(point.y(), 0, 'f', 4);
    }
    mPointsEdit->setPlainText(points.join(QStringLiteral("; ")));
    if (primitive.type == DepthPrimitiveType::Box)
        mShapeStack->setCurrentIndex(0);
    else if (primitive.type == DepthPrimitiveType::Cylinder)
        mShapeStack->setCurrentIndex(1);
    else
        mShapeStack->setCurrentIndex(2);
    mUpdatingGeometryUi = false;
}

void DepthMapEditor::updateCanvasGeometry()
{
    if (mCanvas)
        mCanvas->setGeometry(
            currentGeometryValue(), selectedGeometryIndex());
}

void DepthMapEditor::updatePrimitivePresetUi()
{
    if (!mPresetCombo)
        return;
    const QString selectedName = mPresetCombo->currentData().toString();
    mVisiblePresets.clear();
    mPresetCombo->clear();
    if (mTileset) {
        const QVector<PrimitivePreset> presets =
                readPrimitivePresets();
        for (const PrimitivePreset &preset : presets) {
            mVisiblePresets += preset;
            QString typeName;
            switch (preset.primitive.type) {
            case DepthPrimitiveType::Box:
                typeName = tr("Box");
                break;
            case DepthPrimitiveType::Cylinder:
                typeName = tr("Cylinder");
                break;
            case DepthPrimitiveType::Polygon:
                typeName = tr("Polygon");
                break;
            }
            mPresetCombo->addItem(
                        tr("%1: %2").arg(typeName, preset.name),
                        preset.name);
            mPresetCombo->setItemData(
                        mPresetCombo->count() - 1,
                        tr("Saved from tileset %1")
                        .arg(preset.tileset),
                        Qt::ToolTipRole);
        }
    }
    const int selectedIndex =
            mPresetCombo->findData(selectedName);
    if (selectedIndex >= 0)
        mPresetCombo->setCurrentIndex(selectedIndex);
    const bool hasPreset = !mVisiblePresets.isEmpty();
    mPresetCombo->setEnabled(hasPreset);
    mInsertPresetButton->setEnabled(hasPreset &&
                                    currentTileId() >= 0);
    mDeletePresetButton->setEnabled(hasPreset);
    if (!hasPreset)
        mPresetCombo->addItem(
                    tr("No reusable presets saved"));
}

QVector3D DepthMapEditor::primitivePixelSize(
        const DepthPrimitive &primitive) const
{
    if (primitive.type == DepthPrimitiveType::Box) {
        return QVector3D(
            qAbs(primitive.maximum.x() - primitive.minimum.x()) * 64.0f,
            qAbs(primitive.maximum.y() - primitive.minimum.y()) * 96.0f,
            qAbs(primitive.maximum.z() - primitive.minimum.z()) * 64.0f);
    }
    if (primitive.type == DepthPrimitiveType::Cylinder) {
        return QVector3D(
            qMax(primitive.radius1, primitive.radius2) * 128.0f,
            primitive.height * 96.0f, 0.0f);
    }
    if (primitive.points.isEmpty())
        return QVector3D();
    const QRectF bounds = pointsBounds(primitive.points);
    const float heightScale =
            primitive.plane == DepthPolygonPlane::XZ
            ? 64.0f : 96.0f;
    return QVector3D(bounds.width() * 64.0f,
                     bounds.height() * heightScale, 0.0f);
}

void DepthMapEditor::setPrimitivePixelSize(
        DepthPrimitive &primitive, int dimension, double pixels) const
{
    pixels = qMax(1.0, pixels);
    if (primitive.type == DepthPrimitiveType::Box) {
        const float scales[] = { 64.0f, 96.0f, 64.0f };
        const float center =
                (primitive.minimum[dimension] +
                 primitive.maximum[dimension]) * 0.5f;
        const float halfSize =
                float(pixels / scales[dimension] * 0.5);
        primitive.minimum[dimension] = center - halfSize;
        primitive.maximum[dimension] = center + halfSize;
        return;
    }
    if (primitive.type == DepthPrimitiveType::Cylinder) {
        if (dimension == 0) {
            const float currentRadius =
                    qMax(primitive.radius1, primitive.radius2);
            const float targetRadius = float(pixels / 128.0);
            if (currentRadius > 0.000001f) {
                const float factor = targetRadius / currentRadius;
                primitive.radius1 *= factor;
                primitive.radius2 *= factor;
            } else {
                primitive.radius1 = targetRadius;
                primitive.radius2 = targetRadius;
            }
        } else if (dimension == 1) {
            primitive.height = float(pixels / 96.0);
        }
        return;
    }
    if (dimension > 1 || primitive.points.isEmpty())
        return;

    const QRectF bounds = pointsBounds(primitive.points);
    const double currentSize =
            dimension == 0 ? bounds.width() : bounds.height();
    if (currentSize <= 0.000001)
        return;
    const double scale =
            dimension == 0 ? 64.0
            : primitive.plane == DepthPolygonPlane::XZ
              ? 64.0 : 96.0;
    const double targetSize = pixels / scale;
    const double factor = targetSize / currentSize;
    const QPointF center = bounds.center();
    for (QPointF &point : primitive.points) {
        if (dimension == 0)
            point.setX(center.x() +
                       (point.x() - center.x()) * factor);
        else
            point.setY(center.y() +
                       (point.y() - center.y()) * factor);
    }
}

void DepthMapEditor::snapPrimitiveToPixelGrid(
        DepthPrimitive &primitive) const
{
    const auto snap = [](float value, float scale) {
        return qRound(value * scale) / scale;
    };
    primitive.translate.setX(snap(primitive.translate.x(), 64.0f));
    primitive.translate.setY(snap(primitive.translate.y(), 96.0f));
    primitive.translate.setZ(snap(primitive.translate.z(), 64.0f));

    if (primitive.type == DepthPrimitiveType::Box) {
        const float scales[] = { 64.0f, 96.0f, 64.0f };
        for (int dimension = 0; dimension < 3; ++dimension) {
            if (primitive.minimum[dimension] >
                    primitive.maximum[dimension]) {
                std::swap(primitive.minimum[dimension],
                          primitive.maximum[dimension]);
            }
            primitive.minimum[dimension] =
                    snap(primitive.minimum[dimension],
                         scales[dimension]);
            primitive.maximum[dimension] =
                    snap(primitive.maximum[dimension],
                         scales[dimension]);
            if (primitive.maximum[dimension] <=
                    primitive.minimum[dimension]) {
                primitive.maximum[dimension] =
                        primitive.minimum[dimension] +
                        1.0f / scales[dimension];
            }
        }
    } else if (primitive.type == DepthPrimitiveType::Cylinder) {
        primitive.radius1 = qMax(
            1.0f / 128.0f,
            qRound(primitive.radius1 * 128.0f) / 128.0f);
        primitive.radius2 = qMax(
            1.0f / 128.0f,
            qRound(primitive.radius2 * 128.0f) / 128.0f);
        primitive.height = qMax(
            1.0f / 96.0f,
            qRound(primitive.height * 96.0f) / 96.0f);
    } else {
        const float heightScale =
                primitive.plane == DepthPolygonPlane::XZ
                ? 64.0f : 96.0f;
        for (QPointF &point : primitive.points) {
            point.setX(snap(float(point.x()), 64.0f));
            point.setY(snap(float(point.y()), heightScale));
        }
    }
}

void DepthMapEditor::scalePrimitive(
        DepthPrimitive &primitive, float factor) const
{
    factor = qBound(0.05f, factor, 20.0f);
    if (primitive.type == DepthPrimitiveType::Box) {
        const QVector3D center =
                (primitive.minimum + primitive.maximum) * 0.5f;
        primitive.minimum =
                center + (primitive.minimum - center) * factor;
        primitive.maximum =
                center + (primitive.maximum - center) * factor;
    } else if (primitive.type == DepthPrimitiveType::Cylinder) {
        primitive.radius1 *= factor;
        primitive.radius2 *= factor;
        primitive.height *= factor;
    } else if (!primitive.points.isEmpty()) {
        const QRectF bounds = pointsBounds(primitive.points);
        const QPointF center = bounds.center();
        for (QPointF &point : primitive.points)
            point = center + (point - center) * factor;
    }
}

QVector<DepthMapEditor::PrimitivePreset>
DepthMapEditor::readPrimitivePresets() const
{
    QVector<PrimitivePreset> presets;
    QSettings settings;
    const int count = settings.beginReadArray(
                QLatin1String("DepthMapEditor/PrimitivePresets"));
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        PrimitivePreset preset;
        preset.name = settings.value(
                    QLatin1String("name")).toString();
        preset.tileset = settings.value(
                    QLatin1String("tileset")).toString();
        const int type = settings.value(
                    QLatin1String("type"), 0).toInt();
        if (type < int(DepthPrimitiveType::Box) ||
                type > int(DepthPrimitiveType::Polygon)) {
            continue;
        }
        preset.primitive.type =
                static_cast<DepthPrimitiveType>(type);
        preset.primitive.translate = QVector3D(
            settings.value(QLatin1String("translateX")).toFloat(),
            settings.value(QLatin1String("translateY")).toFloat(),
            settings.value(QLatin1String("translateZ")).toFloat());
        preset.primitive.rotate = QVector3D(
            settings.value(QLatin1String("rotateX")).toFloat(),
            settings.value(QLatin1String("rotateY")).toFloat(),
            settings.value(QLatin1String("rotateZ")).toFloat());
        preset.primitive.minimum = QVector3D(
            settings.value(QLatin1String("minimumX"), -0.5).toFloat(),
            settings.value(QLatin1String("minimumY"), 0.0).toFloat(),
            settings.value(QLatin1String("minimumZ"), -0.5).toFloat());
        preset.primitive.maximum = QVector3D(
            settings.value(QLatin1String("maximumX"), 0.5).toFloat(),
            settings.value(QLatin1String("maximumY"), 2.44949).toFloat(),
            settings.value(QLatin1String("maximumZ"), 0.5).toFloat());
        preset.primitive.radius1 = settings.value(
                    QLatin1String("radius1"), 0.5).toFloat();
        preset.primitive.radius2 = settings.value(
                    QLatin1String("radius2"), 0.5).toFloat();
        preset.primitive.height = settings.value(
                    QLatin1String("height"), 1.0).toFloat();
        preset.primitive.plane =
                static_cast<DepthPolygonPlane>(qBound(
                    0, settings.value(
                        QLatin1String("plane"), 1).toInt(), 2));
        const QStringList pointValues = settings.value(
                    QLatin1String("points")).toStringList();
        for (const QString &pointValue : pointValues) {
            const QStringList coordinates =
                    pointValue.split(QLatin1Char(','));
            if (coordinates.size() != 2)
                continue;
            bool xOk = false;
            bool yOk = false;
            const double x = coordinates.at(0).toDouble(&xOk);
            const double y = coordinates.at(1).toDouble(&yOk);
            if (xOk && yOk)
                preset.primitive.points += QPointF(x, y);
        }
        if (!preset.name.isEmpty() &&
                !preset.tileset.isEmpty())
            presets += preset;
    }
    settings.endArray();
    return presets;
}

void DepthMapEditor::writePrimitivePresets(
        const QVector<PrimitivePreset> &presets) const
{
    QSettings settings;
    settings.beginWriteArray(
                QLatin1String("DepthMapEditor/PrimitivePresets"),
                presets.size());
    for (int index = 0; index < presets.size(); ++index) {
        settings.setArrayIndex(index);
        const PrimitivePreset &preset = presets.at(index);
        const DepthPrimitive &primitive = preset.primitive;
        settings.setValue(QLatin1String("name"), preset.name);
        settings.setValue(QLatin1String("tileset"), preset.tileset);
        settings.setValue(QLatin1String("type"),
                          int(primitive.type));
        settings.setValue(QLatin1String("translateX"),
                          primitive.translate.x());
        settings.setValue(QLatin1String("translateY"),
                          primitive.translate.y());
        settings.setValue(QLatin1String("translateZ"),
                          primitive.translate.z());
        settings.setValue(QLatin1String("rotateX"),
                          primitive.rotate.x());
        settings.setValue(QLatin1String("rotateY"),
                          primitive.rotate.y());
        settings.setValue(QLatin1String("rotateZ"),
                          primitive.rotate.z());
        settings.setValue(QLatin1String("minimumX"),
                          primitive.minimum.x());
        settings.setValue(QLatin1String("minimumY"),
                          primitive.minimum.y());
        settings.setValue(QLatin1String("minimumZ"),
                          primitive.minimum.z());
        settings.setValue(QLatin1String("maximumX"),
                          primitive.maximum.x());
        settings.setValue(QLatin1String("maximumY"),
                          primitive.maximum.y());
        settings.setValue(QLatin1String("maximumZ"),
                          primitive.maximum.z());
        settings.setValue(QLatin1String("radius1"),
                          primitive.radius1);
        settings.setValue(QLatin1String("radius2"),
                          primitive.radius2);
        settings.setValue(QLatin1String("height"),
                          primitive.height);
        settings.setValue(QLatin1String("plane"),
                          int(primitive.plane));
        QStringList pointValues;
        for (const QPointF &point : primitive.points) {
            pointValues += QStringLiteral("%1,%2")
                    .arg(point.x(), 0, 'g', 12)
                    .arg(point.y(), 0, 'g', 12);
        }
        settings.setValue(QLatin1String("points"),
                          pointValues);
    }
    settings.endArray();
    settings.sync();
}

QVector<DepthPrimitive> &DepthMapEditor::currentGeometry()
{
    return mGeometryDocument.tiles()[currentTileId()].primitives;
}

const QVector<DepthPrimitive> DepthMapEditor::currentGeometryValue() const
{
    const int tileId = currentTileId();
    const auto it = mGeometryDocument.tiles().constFind(tileId);
    return tileId >= 0 && it != mGeometryDocument.tiles().constEnd()
            ? it.value().primitives : QVector<DepthPrimitive>();
}

int DepthMapEditor::selectedGeometryIndex() const
{
    return mGeometryList ? mGeometryList->currentRow() : -1;
}

void DepthMapEditor::addGeometry(const DepthPrimitive &primitive)
{
    if (currentTileId() < 0)
        return;
    QVector<DepthPrimitive> &geometry = currentGeometry();
    geometry += primitive;
    setGeometryDirty();
    updateGeometryList(geometry.size() - 1);
    if (mModeTabs)
        mModeTabs->setCurrentIndex(0);
}

void DepthMapEditor::setGeometryDirty()
{
    mGeometryDirty = true;
    updateWindowState();
}

void DepthMapEditor::updateWindowState()
{
    const QString baseTitle = tr("Depth Map Editor");
    setWindowTitle(mRevision == mSavedRevision && !mGeometryDirty
                   ? baseTitle
                   : baseTitle + QStringLiteral(" *"));
    mFileLabel->setText(mFilePath.isEmpty()
                        ? tr("Not assigned — save will ask for a destination")
                        : nativePath(mFilePath));
    if (mGeometryFileLabel) {
        mGeometryFileLabel->setText(
            mGeometryFilePath.isEmpty()
            ? tr("Not assigned")
            : nativePath(mGeometryFilePath));
    }
    mSaveAction->setEnabled(mTileset && !mAtlas.isNull());
    mSaveAsAction->setEnabled(mTileset && !mAtlas.isNull());
    mReloadAction->setEnabled(mTileset && !mAtlas.isNull());
    updateActions();
}

void DepthMapEditor::updateActions()
{
    mUndoAction->setEnabled(mEditIndex > 0);
    mRedoAction->setEnabled(mEditIndex < mEdits.size());
    if (mPasteButton)
        mPasteButton->setEnabled(
            currentTileId() >= 0 && !mDepthClipboard.isNull());
}

void DepthMapEditor::setDirtyRevision(int revision)
{
    mRevision = revision;
    updateWindowState();
}

void DepthMapEditor::pushEdit(
        int tileId, const QImage &before, const QImage &after)
{
    if (tileId < 0 || imagesEqual(before, after))
        return;
    while (mEdits.size() > mEditIndex)
        mEdits.removeLast();

    Edit edit;
    edit.tileId = tileId;
    edit.before = normalisedDepthTile(before);
    edit.after = normalisedDepthTile(after);
    edit.beforeRevision = mRevision;
    edit.afterRevision = mNextRevision++;
    mEdits += edit;
    ++mEditIndex;

    if (mEdits.size() > 100) {
        mEdits.removeFirst();
        --mEditIndex;
    }

    applyEditImage(tileId, edit.after);
    setDirtyRevision(edit.afterRevision);
}

void DepthMapEditor::applyEditImage(
        int tileId, const QImage &image)
{
    setDepthTileImage(tileId, image);
    updateTileIcon(tileId);
    if (tileId == currentTileId())
        mCanvas->setDepthImage(image);
    updateActions();
}

void DepthMapEditor::commitCurrentOperation(const QImage &after)
{
    const int tileId = currentTileId();
    if (tileId < 0)
        return;
    pushEdit(tileId, depthTileImage(tileId), after);
}
