#include "tilesetzeditor.h"
#include "tilesetz.h"
#include "tileset.h"
#include "tilemetainfomgr.h"
#include "map.h"
#include "mapreader.h"
#include "tilelayer.h"
#include "tile.h"
#include "zlevelrenderer.h"
#include <QBuffer>

#include <QAbstractListModel>
#include <QAbstractButton>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImageReader>
#include <QLabel>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QQueue>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTabWidget>
#include <QToolButton>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDebug>
#include <QTimer>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <functional>

static QBrush checkerBrush()
{
    QPixmap pattern(16, 16);
    pattern.fill(QColor(62, 66, 72));
    QPainter painter(&pattern);
    painter.fillRect(0, 0, 8, 8, QColor(83, 88, 95));
    painter.fillRect(8, 8, 8, 8, QColor(83, 88, 95));
    return QBrush(pattern);
}

class TileSetZCanvas : public QWidget
{
public:
    enum Tool { Paint, Erase, Pick, Fill, Line, Rectangle, Ellipse, Pan };
    explicit TileSetZCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("TileSetZCanvas"));
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void(const QImage &, const QImage &)> committed;
    std::function<void(const QColor &)> picked;
    std::function<void(int)> zoomChanged;
    std::function<void(QPoint)> moved;
    Tool tool = Paint;
    QColor color = Qt::white;
    int brushSize = 1;
    bool alphaLock = false;
    bool grid = true;
    bool filled = false;

    void setImage(const QImage &image)
    {
        mImage = image;
        resize(mImage.size() * mZoom);
        update();
    }

    void setZoom(int zoom)
    {
        finishStroke();
        mZoom = qBound(1, zoom, 32);
        resize(mImage.size() * mZoom);
        update();
        if (zoomChanged)
            zoomChanged(mZoom);
    }

    int zoom() const { return mZoom; }

    void finishStroke()
    {
        if (!mDrawing)
            return;
        mDrawing = false;
        if (mBefore != mImage && committed)
            committed(mBefore, mImage);
        mBefore = QImage();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), checkerBrush());
        painter.drawImage(rect(), mImage);
        if (grid && mZoom >= 5) {
            painter.setPen(QColor(0, 0, 0, 55));
            const QRect visible = visibleRegion().boundingRect();
            for (int x = (visible.left() / mZoom) * mZoom; x <= visible.right(); x += mZoom)
                painter.drawLine(x, visible.top(), x, visible.bottom());
            for (int y = (visible.top() / mZoom) * mZoom; y <= visible.bottom(); y += mZoom)
                painter.drawLine(visible.left(), y, visible.right(), y);
        }
        if (underMouse() && (tool == Paint || tool == Erase)) {
            const QRect outline((mHover - QPoint(brushSize / 2, brushSize / 2)) * mZoom,
                                 QSize(brushSize, brushSize) * mZoom);
            painter.setPen(QPen(Qt::black, 3));
            painter.drawRect(outline);
            painter.setPen(QPen(Qt::white, 1));
            painter.drawRect(outline);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        setFocus();
        if (event->button() == Qt::MiddleButton || mSpace || tool == Pan) {
            finishStroke();
            mPanning = true;
            mPanPosition = event->globalPos();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
            return;
        const QPoint point = event->pos() / mZoom;
        if (!mImage.rect().contains(point))
            return;
        if (tool == Pick || (event->modifiers() & Qt::ControlModifier)) {
            if (picked)
                picked(QColor::fromRgba(mImage.pixel(point)));
            return;
        }
        mBefore = mImage;
        mStart = point;
        mDrawing = true;
        mErasing = event->button() == Qt::RightButton || tool == Erase;
        if (tool == Fill && !mErasing) {
            flood(point);
            finishStroke();
        } else if (tool >= Line && tool <= Ellipse && !mErasing) {
            shape(point);
        } else {
            dab(point);
            mLast = point;
        }
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        mHover = event->pos() / mZoom;
        if (moved)
            moved(mHover);
        update();
        if (mPanning) {
            if (QScrollArea *area = scrollArea()) {
                const QPoint delta = event->globalPos() - mPanPosition;
                area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() - delta.x());
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() - delta.y());
            }
            mPanPosition = event->globalPos();
            return;
        }
        if (!mDrawing)
            return;
        QPoint next = event->pos() / mZoom;
        next.setX(qBound(-brushSize, next.x(), mImage.width() + brushSize));
        next.setY(qBound(-brushSize, next.y(), mImage.height() + brushSize));
        if (tool >= Line && tool <= Ellipse && !mErasing) {
            shape(next);
            return;
        }
        const int steps = qMax(qAbs(next.x() - mLast.x()), qAbs(next.y() - mLast.y()));
        for (int step = 1; step <= steps; ++step)
            dab(mLast + QPoint(qRound((next.x() - mLast.x()) * qreal(step) / steps),
                               qRound((next.y() - mLast.y()) * qreal(step) / steps)));
        mLast = next;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        mPanning = false;
        setCursor(tool == Pan || mSpace ? Qt::OpenHandCursor : Qt::CrossCursor);
        finishStroke();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        QScrollArea *area = scrollArea();
        const QPointF pixel = QPointF(event->pos()) / mZoom;
        const QPoint anchor = area ? mapTo(area->viewport(), event->pos()) : QPoint();
        setZoom(mZoom + (event->angleDelta().y() > 0 ? 1 : -1));
        if (area) {
            area->horizontalScrollBar()->setValue(qRound(pixel.x() * mZoom) - anchor.x());
            area->verticalScrollBar()->setValue(qRound(pixel.y() * mZoom) - anchor.y());
        }
        event->accept();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Space) {
            mSpace = true;
            setCursor(Qt::OpenHandCursor);
        } else if (event->key() == Qt::Key_Escape && mDrawing) {
            mImage = mBefore;
            mDrawing = false;
            mBefore = QImage();
            update();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    void keyReleaseEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Space) {
            mSpace = false;
            setCursor(tool == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
        } else {
            QWidget::keyReleaseEvent(event);
        }
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        finishStroke();
        mSpace = mPanning = false;
        QWidget::focusOutEvent(event);
    }

private:
    QScrollArea *scrollArea() const
    {
        QWidget *parent = parentWidget();
        while (parent && !qobject_cast<QScrollArea *>(parent))
            parent = parent->parentWidget();
        return qobject_cast<QScrollArea *>(parent);
    }

    void shape(const QPoint &point)
    {
        mImage = mBefore;
        QImage mask = TileSetZ::blank(mImage.size());
        {
            QPainter painter(&mask);
            painter.setPen(QPen(Qt::white, brushSize, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            painter.setBrush(filled ? QBrush(Qt::white) : Qt::NoBrush);
            const QRect bounds = QRect(mStart, point).normalized();
            if (tool == Line)
                painter.drawLine(mStart, point);
            else if (tool == Rectangle)
                painter.drawRect(bounds);
            else
                painter.drawEllipse(bounds);
        }
        for (int y = 0; y < mImage.height(); ++y) {
            QRgb *out = reinterpret_cast<QRgb *>(mImage.scanLine(y));
            const QRgb *ink = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
            for (int x = 0; x < mImage.width(); ++x)
                if (qAlpha(ink[x]) && (!alphaLock || qAlpha(out[x])))
                    out[x] = alphaLock ? qRgba(color.red(), color.green(), color.blue(), qAlpha(out[x])) : color.rgba();
        }
        update();
    }
    void dab(const QPoint &point)
    {
        const QRect area = QRect(point - QPoint(brushSize / 2, brushSize / 2),
                                 QSize(brushSize, brushSize)).intersected(mImage.rect());
        for (int y = area.top(); y <= area.bottom(); ++y) {
            QRgb *pixels = reinterpret_cast<QRgb *>(mImage.scanLine(y));
            for (int x = area.left(); x <= area.right(); ++x) {
                if (alphaLock && !qAlpha(pixels[x]))
                    continue;
                pixels[x] = mErasing ? 0 : (alphaLock
                           ? qRgba(color.red(), color.green(), color.blue(), qAlpha(pixels[x])) : color.rgba());
            }
        }
    }

    void flood(const QPoint &point)
    {
        const QRgb target = mImage.pixel(point);
        const QRgb replacement = alphaLock ? qRgba(color.red(), color.green(), color.blue(), qAlpha(target)) : color.rgba();
        if (target == replacement || (alphaLock && !qAlpha(target)))
            return;
        QQueue<QPoint> points;
        points.enqueue(point);
        mImage.setPixel(point, replacement);
        const QPoint offsets[] = {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)};
        while (!points.isEmpty()) {
            const QPoint current = points.dequeue();
            for (const QPoint &offset : offsets) {
                const QPoint next = current + offset;
                if (mImage.rect().contains(next) && mImage.pixel(next) == target) {
                    mImage.setPixel(next, replacement);
                    points.enqueue(next);
                }
            }
        }
    }

    QImage mImage;
    QImage mBefore;
    int mZoom = 2;
    QPoint mLast;
    QPoint mStart;
    QPoint mHover;
    QPoint mPanPosition;
    bool mDrawing = false;
    bool mErasing = false;
    bool mPanning = false;
    bool mSpace = false;
};

class TileSetZModel : public QAbstractListModel
{
public:
    TileSetZModel(const QImage *image, const int *scale, QObject *parent)
        : QAbstractListModel(parent), mImage(image), mScale(scale) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        const QSize size = TileSetZ::tileSize(*mScale);
        return parent.isValid() ? 0 : (mImage->width() / size.width()) * (mImage->height() / size.height());
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rowCount())
            return QVariant();
        if (role == Qt::DecorationRole)
            return mImage->copy(TileSetZ::tileRect(*mImage, *mScale, index.row()));
        if (role == Qt::DisplayRole)
            return QString::number(index.row());
        return QVariant();
    }
    void refresh()
    {
        if (mRows != rowCount()) {
            beginResetModel();
            mRows = rowCount();
            endResetModel();
        } else {
            emit dataChanged(index(0), index(qMax(0, rowCount() - 1)));
        }
    }
private:
    const QImage *mImage;
    const int *mScale;
    int mRows = 0;
};

class TileSetZDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(72, 150); }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        const QRect imageRect(option.rect.topLeft() + QPoint(4, 4), QSize(64, 128));
        painter->fillRect(imageRect, checkerBrush());
        painter->drawImage(imageRect, qvariant_cast<QImage>(index.data(Qt::DecorationRole)));
        const bool selected = option.state & QStyle::State_Selected;
        painter->setPen(selected ? option.palette.highlight().color() : option.palette.mid().color());
        painter->drawRect(imageRect.adjusted(-1, -1, 0, 0));
        const QRect labelRect(option.rect.left(), imageRect.bottom() + 2, option.rect.width(), 18);
        if (selected)
            painter->fillRect(labelRect, option.palette.highlight());
        painter->setPen(selected ? option.palette.highlightedText().color() : option.palette.text().color());
        painter->drawText(labelRect, Qt::AlignCenter, index.data().toString());
        painter->restore();
    }
};

class TileSetZPreview : public QGraphicsView
{
public:
    explicit TileSetZPreview(QWidget *parent) : QGraphicsView(parent)
    {
        setScene(new QGraphicsScene(this));
        setBackgroundBrush(checkerBrush());
        setMinimumHeight(180);
        setDragMode(ScrollHandDrag);
        setTransformationAnchor(AnchorUnderMouse);
    }
    std::function<void(int)> tileActivated;
    std::function<void(qreal)> zoomChanged;
    void setImage(const QImage &image, const QSize &tileSize = QSize())
    {
        mTileSize = tileSize;
        scene()->clear();
        if (!image.isNull())
            scene()->addPixmap(QPixmap::fromImage(image));
        scene()->setSceneRect(image.rect());
        fit();
    }
    void setWall(const TileSetZ::WallResult &wall, int scale)
    {
        mTileSize = QSize();
        scene()->clear();
        for (const TileSetZ::WallPiece &piece : wall.pieces) {
            auto *item = scene()->addPixmap(QPixmap::fromImage(wall.sheet.copy(TileSetZ::tileRect(wall.sheet, scale, piece.id))));
            item->setPos(piece.screen);
        }
        scene()->setSceneRect(scene()->itemsBoundingRect());
        fit();
    }
    void setZoom(qreal zoom)
    {
        mAutoFit = false;
        resetTransform();
        scale(qBound(0.02, zoom, 32.0), qBound(0.02, zoom, 32.0));
        if (zoomChanged)
            zoomChanged(transform().m11());
    }
    void fit()
    {
        mAutoFit = true;
        if (!scene()->sceneRect().isEmpty())
            fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
        if (zoomChanged)
            zoomChanged(transform().m11());
    }
    void fitTile(int id)
    {
        if (mTileSize.isEmpty() || sceneRect().isEmpty())
            return;
        const int columns = (int(sceneRect().width()) + mTileSize.width() - 1) / mTileSize.width();
        const QRectF tile((id % columns) * mTileSize.width(), (id / columns) * mTileSize.height(),
                           mTileSize.width(), mTileSize.height());
        if (!sceneRect().intersects(tile))
            return;
        mAutoFit = false;
        fitInView(tile, Qt::KeepAspectRatio);
        if (zoomChanged)
            zoomChanged(transform().m11());
    }
    bool showGrid = true;
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        if (mAutoFit)
            fit();
    }
    void wheelEvent(QWheelEvent *event) override
    {
        const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        mAutoFit = false;
        if (transform().m11() * factor >= 0.02 && transform().m11() * factor <= 32)
            scale(factor, factor);
        if (zoomChanged)
            zoomChanged(transform().m11());
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        const QPointF point = mapToScene(event->pos());
        if (mTileSize.isEmpty() || !sceneRect().contains(point))
            return;
        const int columns = (int(sceneRect().width()) + mTileSize.width() - 1) / mTileSize.width();
        const int id = (int(point.y()) / mTileSize.height()) * columns
                       + int(point.x()) / mTileSize.width();
        fitTile(id);
        if (tileActivated)
            tileActivated(id);
    }
    void drawForeground(QPainter *painter, const QRectF &area) override
    {
        if (!showGrid || mTileSize.isEmpty())
            return;
        painter->save();
        const QRectF bounds = sceneRect();
        painter->setPen(QPen(QColor(180, 190, 205, 180), 0));
        for (int x = 0; x <= bounds.width(); x += mTileSize.width())
            if (x >= area.left() && x <= area.right())
                painter->drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
        for (int y = 0; y <= bounds.height(); y += mTileSize.height())
            if (y >= area.top() && y <= area.bottom())
                painter->drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
        painter->restore();
    }
private:
    QSize mTileSize;
    bool mAutoFit = true;
};

static QIcon drawingIcon(int tool, const QColor &color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (tool == TileSetZCanvas::Paint || tool == TileSetZCanvas::Pick) {
        p.drawPolygon(QPolygonF() << QPointF(7, 22) << QPointF(21, 8) << QPointF(25, 12) << QPointF(11, 26) << QPointF(5, 28));
        p.drawLine(18, 11, 22, 15);
        if (tool == TileSetZCanvas::Pick) {
            p.setPen(QPen(color, 5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(21, 5, 28, 12);
        }
    } else if (tool == TileSetZCanvas::Erase) {
        p.drawPolygon(QPolygonF() << QPointF(5, 20) << QPointF(19, 6) << QPointF(28, 15) << QPointF(16, 27) << QPointF(12, 27));
        p.drawLine(12, 13, 21, 22);
    } else if (tool == TileSetZCanvas::Fill) {
        p.drawPolygon(QPolygonF() << QPointF(4, 17) << QPointF(16, 5) << QPointF(27, 16) << QPointF(15, 28));
        p.drawLine(6, 19, 24, 19);
        p.drawLine(15, 11, 15, 3);
        p.setBrush(color);
        p.drawEllipse(25, 23, 4, 6);
    } else if (tool == TileSetZCanvas::Line) {
        p.drawLine(6, 26, 26, 6);
    } else if (tool == TileSetZCanvas::Rectangle) {
        p.drawRect(5, 7, 22, 19);
    } else if (tool == TileSetZCanvas::Ellipse) {
        p.drawEllipse(5, 7, 22, 19);
    } else {
        p.drawPolyline(QPolygonF() << QPointF(10, 28) << QPointF(4, 17) << QPointF(7, 15) << QPointF(11, 19)
                       << QPointF(11, 7) << QPointF(14, 7) << QPointF(14, 16) << QPointF(14, 4)
                       << QPointF(18, 4) << QPointF(18, 16) << QPointF(18, 6) << QPointF(22, 6)
                       << QPointF(22, 17) << QPointF(22, 10) << QPointF(26, 10) << QPointF(26, 21) << QPointF(22, 28));
    }
    return QIcon(pixmap);
}

class TileSetZChange : public QUndoCommand
{
public:
    TileSetZChange(const QImage &before, const QImage &after,
                   const std::shared_ptr<qint64> &bytes,
                   const std::function<void(const QImage &)> &apply, const QString &text)
        : QUndoCommand(text), mBefore(before), mAfter(after), mBytes(bytes), mApply(apply)
    {
        mCost = before.sizeInBytes() + after.sizeInBytes();
        *mBytes += mCost;
    }
    ~TileSetZChange() override { *mBytes -= mCost; }
    void undo() override { mApply(mBefore); }
    void redo() override { mApply(mAfter); }
private:
    QImage mBefore;
    QImage mAfter;
    std::shared_ptr<qint64> mBytes;
    std::function<void(const QImage &)> mApply;
    qint64 mCost;
};

static QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

static QString pngSavePath(QWidget *parent, const QString &title, const QString &initial)
{
    QFileDialog dialog(parent, title, initial, QObject::tr("PNG image (*.png)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("png"));
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
}

TileSetZEditor::TileSetZEditor(QWidget *parent)
    : QMainWindow(parent, Qt::Window), mUndoBytes(std::make_shared<qint64>(0)), mUndo(new QUndoStack(this))
{
    setObjectName(QStringLiteral("TileSetZEditor"));
    resize(1180, 820);
    mUndo->setUndoLimit(200);
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Tilesheet..."), this, &TileSetZEditor::newSheet, QKeySequence::New);
    fileMenu->addAction(tr("&Open Tilesheet..."), this, &TileSetZEditor::openSheet, QKeySequence::Open);
    fileMenu->addAction(tr("&Save"), this, [this] { save(); }, QKeySequence::Save);
    fileMenu->addAction(tr("Save &As..."), this, [this] { save(true); }, QKeySequence::SaveAs);
    fileMenu->addAction(tr("Export Selected Tile..."), this, [this] {
        mCanvas->finishStroke();
        const QString path = pngSavePath(this, tr("Export Tile"), QString());
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
            showError(tr("Choose a filename ending in .png."));
            return;
        }
        QString error;
        if (!TileSetZ::saveImage(mImage.copy(TileSetZ::tileRect(mImage, mScale, mSelected)), path, &error))
            showError(error);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Close"), this, &QWidget::close, QKeySequence::Close);
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = mUndo->createUndoAction(this, tr("Undo"));
    undo->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undo);
    QAction *redo = mUndo->createRedoAction(this, tr("Redo"));
    redo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redo);
    editMenu->addSeparator();
    editMenu->addAction(tr("Flip Tile Horizontally"), this, [this] {
        changeTile(mImage.copy(TileSetZ::tileRect(mImage, mScale, mSelected)).mirrored(true, false), tr("Flip tile horizontally"));
    });
    editMenu->addAction(tr("Flip Tile Vertically"), this, [this] {
        changeTile(mImage.copy(TileSetZ::tileRect(mImage, mScale, mSelected)).mirrored(false, true), tr("Flip tile vertically"));
    });
    editMenu->addAction(tr("Clear Selected Tile"), this, [this] {
        changeTile(TileSetZ::blank(TileSetZ::tileSize(mScale)), tr("Clear tile"));
    }, QKeySequence(Qt::Key_Delete));
    editMenu->addAction(tr("Remove Nearly Transparent Pixels in Tile"), this, [this] {
        changeTile(TileSetZ::clean(mImage.copy(TileSetZ::tileRect(mImage, mScale, mSelected))), tr("Remove nearly transparent pixels"));
    });
    editMenu->addAction(tr("Use Selected Tile as Source Texture"), this, [this] {
        mCanvas->finishStroke();
        mSource = mImage.copy(TileSetZ::tileRect(mImage, mScale, mSelected));
        mSourceFile.clear();
        mSourceLabel->setText(tr("Source: tile %1 (%2 x %3)").arg(mSelected).arg(mSource.width()).arg(mSource.height()));
        invalidatePreview();
    });

    auto *fileTools = addToolBar(tr("Document"));
    fileTools->setMovable(false);
    fileTools->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const QStringList fileIcons = {QStringLiteral("document-new"), QStringLiteral("document-open"), QStringLiteral("document-save")};
    for (int i = 0; i < fileIcons.size(); ++i) {
        QAction *action = fileMenu->actions().at(i);
        action->setIcon(QIcon(QStringLiteral(":/images/24x24/%1.png").arg(fileIcons[i])));
        fileTools->addAction(action);
    }
    fileTools->addSeparator();
    undo->setIcon(QIcon(QStringLiteral(":/images/24x24/edit-undo.png")));
    redo->setIcon(QIcon(QStringLiteral(":/images/24x24/edit-redo.png")));
    fileTools->addAction(undo);
    fileTools->addAction(redo);
    mWorkspace = new QTabWidget(this);
    setCentralWidget(mWorkspace);
    auto *splitter = new QSplitter(mWorkspace);
    mWorkspace->addTab(splitter, tr("Draw tiles"));
    auto *sheetPane = new QWidget(splitter);
    auto *sheetLayout = new QVBoxLayout(sheetPane);
    sheetLayout->addWidget(new QLabel(tr("Tilesheet"), sheetPane));
    mModel = new TileSetZModel(&mImage, &mScale, this);
    mTiles = new QListView(sheetPane);
    mTiles->setModel(mModel);
    mTiles->setItemDelegate(new TileSetZDelegate(mTiles));
    mTiles->setViewMode(QListView::IconMode);
    mTiles->setResizeMode(QListView::Adjust);
    mTiles->setMovement(QListView::Static);
    mTiles->setGridSize(QSize(76, 154));
    mTiles->setUniformItemSizes(true);
    mTiles->setMinimumWidth(160);
    sheetLayout->addWidget(mTiles);
    auto *sheetNote = new QLabel(tr("IDs stay in row order. New tiles are appended without renumbering existing tiles."), sheetPane);
    sheetNote->setWordWrap(true);
    sheetLayout->addWidget(sheetNote);

    auto *paintPane = new QWidget(splitter);
    auto *paintLayout = new QVBoxLayout(paintPane);
    mTileLabel = new QLabel(paintPane);
    paintLayout->addWidget(mTileLabel);
    auto *tools = new QToolBar(paintPane);
    tools->setIconSize(QSize(32, 32));
    tools->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    auto *group = new QActionGroup(this);
    const QStringList names = {tr("Pencil"), tr("Eraser"), tr("Eyedropper"), tr("Fill"), tr("Line"), tr("Rectangle"), tr("Ellipse"), tr("Pan")};
    const int keys[] = {Qt::Key_B, Qt::Key_E, Qt::Key_I, Qt::Key_F, Qt::Key_L, Qt::Key_R, Qt::Key_O, Qt::Key_H};
    mCanvas = new TileSetZCanvas;
    for (int i = 0; i < names.size(); ++i) {
        QAction *action = tools->addAction(drawingIcon(i, palette().color(QPalette::Text)), names[i]);
        action->setToolTip(tr("%1 (%2)").arg(names[i], QKeySequence(keys[i]).toString()));
        action->setCheckable(true);
        action->setShortcut(QKeySequence(keys[i]));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        paintPane->addAction(action);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] {
            mCanvas->finishStroke();
            mCanvas->tool = TileSetZCanvas::Tool(i);
            mCanvas->setCursor(i == TileSetZCanvas::Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
        });
        action->setChecked(i == 0);
    }
    paintLayout->addWidget(tools);
    auto *paintControls = new QHBoxLayout;
    mPaintButton = new QPushButton(paintPane);
    paintControls->addWidget(mPaintButton);
    connect(mPaintButton, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(mPaintColor, this, tr("Paint Color"), QColorDialog::ShowAlphaChannel);
        if (color.isValid())
            setPaintColor(color);
    });
    paintControls->addWidget(new QLabel(tr("Brush"), paintPane));
    auto *brush = new QSpinBox(paintPane);
    brush->setRange(1, 32);
    brush->setSuffix(tr(" px"));
    brush->setMaximumWidth(85);
    paintControls->addWidget(brush);
    connect(brush, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int size) { mCanvas->brushSize = size; });
    paintControls->addWidget(new QLabel(tr("Opacity"), paintPane));
    mOpacity = new QSpinBox(paintPane);
    mOpacity->setRange(0, 255);
    mOpacity->setValue(255);
    mOpacity->setToolTip(tr("Pixel alpha, from 0 (transparent) to 255 (opaque)."));
    connect(mOpacity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        QColor color = mPaintColor;
        color.setAlpha(value);
        setPaintColor(color);
    });
    paintControls->addWidget(mOpacity);
    auto *filled = new QCheckBox(tr("Filled shapes"), paintPane);
    connect(filled, &QCheckBox::toggled, this, [this](bool checked) { mCanvas->filled = checked; });
    paintControls->addWidget(filled);
    paintControls->addStretch();
    paintLayout->addLayout(paintControls);
    auto *swatches = new QHBoxLayout;
    const QList<QColor> colors = {Qt::black, Qt::white, QColor(128, 128, 128), QColor(205, 67, 57),
                                QColor(220, 173, 67), QColor(87, 142, 73), QColor(67, 112, 181), QColor(130, 82, 167)};
    for (const QColor &color : colors) {
        auto *button = new QPushButton(paintPane);
        button->setFixedSize(24, 24);
        button->setAccessibleName(color.name());
        button->setToolTip(color.name());
        button->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
        connect(button, &QPushButton::clicked, this, [this, color] { setPaintColor(color); });
        swatches->addWidget(button);
    }
    swatches->addStretch();
    paintLayout->addLayout(swatches);
    auto *viewControls = new QHBoxLayout;
    auto *lock = new QCheckBox(tr("Lock alpha"), paintPane);
    viewControls->addWidget(lock);
    connect(lock, &QCheckBox::toggled, this, [this](bool checked) { mCanvas->alphaLock = checked; });
    auto *grid = new QCheckBox(tr("Pixel grid"), paintPane);
    grid->setChecked(true);
    viewControls->addWidget(grid);
    connect(grid, &QCheckBox::toggled, this, [this](bool checked) { mCanvas->grid = checked; mCanvas->update(); });
    auto *zoom = new QSpinBox(paintPane);
    zoom->setRange(1, 32);
    zoom->setValue(2);
    zoom->setSuffix(tr("x"));
    zoom->setToolTip(tr("Pixel zoom"));
    viewControls->addWidget(zoom);
    connect(zoom, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) { mCanvas->setZoom(value); });
    mCanvas->zoomChanged = [zoom](int value) { QSignalBlocker blocker(zoom); zoom->setValue(value); };
    auto *fitCanvas = new QPushButton(tr("Fit tile"), paintPane);
    viewControls->addWidget(fitCanvas);
    auto *actualCanvas = new QPushButton(tr("100%"), paintPane);
    viewControls->addWidget(actualCanvas);
    connect(actualCanvas, &QPushButton::clicked, this, [this] { mCanvas->setZoom(1); });
    viewControls->addStretch();
    auto *coordinates = new QLabel(paintPane);
    viewControls->addWidget(coordinates);
    mCanvas->moved = [coordinates](QPoint point) { coordinates->setText(QStringLiteral("X %1  Y %2").arg(point.x()).arg(point.y())); };
    paintLayout->addLayout(viewControls);
    auto *canvasScroll = new QScrollArea(paintPane);
    canvasScroll->setWidget(mCanvas);
    canvasScroll->setAlignment(Qt::AlignCenter);
    connect(fitCanvas, &QPushButton::clicked, this, [this, canvasScroll] {
        const QSize tile = TileSetZ::tileSize(mScale);
        mCanvas->setZoom(qMin((canvasScroll->viewport()->width() - 4) / tile.width(),
                              (canvasScroll->viewport()->height() - 4) / tile.height()));
    });
    paintLayout->addWidget(canvasScroll, 1);
    auto *paintNote = new QLabel(tr("Wheel: zoom  |  Space + drag or middle button: pan  |  Ctrl-click: pick color  |  Right-drag: erase  |  Esc: cancel stroke"), paintPane);
    paintNote->setWordWrap(true);
    paintLayout->addWidget(paintNote);

    auto *generatorSplit = new QSplitter(mWorkspace);
    mWorkspace->addTab(generatorSplit, tr("Tilesheets, textures && masks"));
    auto *generatorScroll = new QScrollArea(generatorSplit);
    generatorScroll->setWidgetResizable(true);
    generatorScroll->setMinimumWidth(300);
    auto *generatorPane = new QWidget;
    generatorScroll->setWidget(generatorPane);
    auto *generatorLayout = new QVBoxLayout(generatorPane);
    generatorLayout->addWidget(new QLabel(tr("Create from a texture"), generatorPane));
    auto *sourceButton = new QPushButton(tr("Open Source Texture..."), generatorPane);
    generatorLayout->addWidget(sourceButton);
    connect(sourceButton, &QPushButton::clicked, this, &TileSetZEditor::loadTexture);
    mSourceLabel = new QLabel(tr("Choose an image, or use Edit > Use Selected Tile as Source Texture."), generatorPane);
    mSourceLabel->setWordWrap(true);
    generatorLayout->addWidget(mSourceLabel);
    auto *form = new QFormLayout;
    mGeneratorForm = form;
    mMethod = new QComboBox(generatorPane);
    mMethod->addItems({tr("Rectangular sprite grid (keep pixels)"), tr("Fit inside one tile"), tr("Wall masks"), tr("Floor masks"),
                       tr("Street curbs"), tr("Roof walls"), tr("Roofs"), tr("Extended wall masks"), tr("Custom PNG mask"),
                       tr("Precut texture into wall tiles")});
    mMethod->setCurrentIndex(9);
    form->addRow(tr("Method"), mMethod);
    mScaleChoice = new QComboBox(generatorPane);
    mScaleChoice->addItems({tr("1x: 64 x 128"), tr("2x: 128 x 256")});
    mScaleChoice->setCurrentIndex(1);
    form->addRow(tr("Output tiles"), mScaleChoice);
    mOutputColumns = new QSpinBox(generatorPane);
    mOutputColumns->setRange(1, 128);
    mOutputColumns->setValue(8);
    form->addRow(tr("Sheet columns"), mOutputColumns);
    mWallDirection = new QComboBox(generatorPane);
    mWallDirection->addItems({tr("West face / rising right"), tr("North face / falling right")});
    form->addRow(tr("Wall direction"), mWallDirection);
    mWallLayout = new QLabel(generatorPane);
    mWallLayout->setWordWrap(true);
    form->addRow(tr("Automatic cut"), mWallLayout);
    mProjection = new QComboBox(generatorPane);
    mProjection->addItems({tr("Repeat source texture"), tr("Left wall"), tr("Right wall"), tr("Corner"), tr("Floor")});
    form->addRow(tr("Custom projection"), mProjection);
    mMaskChannel = new QComboBox(generatorPane);
    mMaskChannel->addItems({tr("Magenta #ff0069"), tr("Mask alpha")});
    form->addRow(tr("Custom mask area"), mMaskChannel);
    mTrimButton = new QPushButton(tr("Trim Color..."), generatorPane);
    form->addRow(tr("Wall trim"), mTrimButton);
    connect(mTrimButton, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(mTrimColor, this, tr("Trim Color"), QColorDialog::ShowAlphaChannel);
        if (color.isValid()) {
            mTrimColor = color;
            mTrimButton->setText(color.name(QColor::HexArgb));
            invalidatePreview();
        }
    });
    generatorLayout->addLayout(form);
    mMaskButton = new QPushButton(tr("Open Custom Mask..."), generatorPane);
    generatorLayout->addWidget(mMaskButton);
    connect(mMaskButton, &QPushButton::clicked, this, &TileSetZEditor::loadMask);
    mMaskLabel = new QLabel(tr("Custom masks use 2x coordinates, in multiples of 128 x 256."), generatorPane);
    mMaskLabel->setWordWrap(true);
    generatorLayout->addWidget(mMaskLabel);
    auto *previewButton = new QPushButton(tr("Generate Preview"), generatorPane);
    generatorLayout->addWidget(previewButton);
    connect(previewButton, &QPushButton::clicked, this, &TileSetZEditor::preview);
    mPreviewLabel = new QLabel(generatorPane);
    mPreviewLabel->setWordWrap(true);
    generatorLayout->addWidget(mPreviewLabel);
    auto *previewPane = new QWidget(generatorSplit);
    auto *previewLayout = new QVBoxLayout(previewPane);
    auto *previewTools = new QHBoxLayout;
    mPreviewMode = new QComboBox(previewPane);
    mPreviewMode->addItems({tr("Generated tilesheet"), tr("Assembly check"), tr("Source texture"), tr("Mask"), tr("Current tilesheet")});
    previewTools->addWidget(mPreviewMode);
    mMaskPreview = new QComboBox(previewPane);
    mMaskPreview->addItem(tr("Custom PNG mask"));
    for (const QString &mask : TileSetZ::maskFiles())
        mMaskPreview->addItem(QString(mask).remove(QStringLiteral("mask_")).replace(QLatin1Char('_'), QLatin1Char(' ')), mask);
    previewTools->addWidget(mMaskPreview);
    previewTools->addStretch();
    previewLayout->addLayout(previewTools);
    auto *zoomTools = new QHBoxLayout;
    auto *previewFit = new QPushButton(tr("Fit image"), previewPane);
    zoomTools->addWidget(previewFit);
    auto *previewActual = new QPushButton(tr("100%"), previewPane);
    zoomTools->addWidget(previewActual);
    auto *previewZoom = new QSpinBox(previewPane);
    previewZoom->setRange(2, 3200);
    previewZoom->setSuffix(QStringLiteral("%"));
    zoomTools->addWidget(previewZoom);
    zoomTools->addWidget(new QLabel(tr("Tile ID"), previewPane));
    mPreviewTile = new QSpinBox(previewPane);
    zoomTools->addWidget(mPreviewTile);
    auto *previewFitTile = new QPushButton(tr("Fit tile"), previewPane);
    mFitPreviewTile = previewFitTile;
    zoomTools->addWidget(previewFitTile);
    zoomTools->addStretch();
    previewLayout->addLayout(zoomTools);
    mPreview = new TileSetZPreview(previewPane);
    mPreview->setToolTip(tr("Mouse wheel zooms. Drag to pan."));
    previewLayout->addWidget(mPreview, 1);
    connect(previewFit, &QPushButton::clicked, this, [this] { mPreview->fit(); });
    connect(previewActual, &QPushButton::clicked, this, [this] { mPreview->setZoom(1); });
    connect(previewZoom, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) { mPreview->setZoom(value / 100.0); });
    mPreview->zoomChanged = [previewZoom](qreal zoom) {
        QSignalBlocker blocker(previewZoom);
        previewZoom->setValue(qRound(zoom * 100));
    };
    connect(previewFitTile, &QPushButton::clicked, this, [this] {
        if (mIsolateTile->isChecked())
            mPreview->fit();
        else
            mPreview->fitTile(mPreviewTile->value());
    });
    mPreview->tileActivated = [this](int id) {
        if (mIsolateTile->isChecked())
            id = mPreviewTile->value();
        mPreviewTile->setValue(id);
        if (mPreviewMode->currentIndex() == 4) {
            mTiles->setCurrentIndex(mModel->index(id));
            mWorkspace->setCurrentIndex(0);
        } else {
            mIsolateTile->setChecked(true);
        }
    };
    connect(mPreviewMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TileSetZEditor::refreshPreview);
    connect(mMaskPreview, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TileSetZEditor::refreshPreview);
    auto *previewGrid = new QCheckBox(tr("Preview tile grid"), generatorPane);
    mPreviewGrid = previewGrid;
    previewGrid->setChecked(true);
    connect(previewGrid, &QCheckBox::toggled, this, [this](bool checked) {
        mPreview->showGrid = checked;
        mPreview->viewport()->update();
    });
    auto *previewOptions = new QHBoxLayout;
    previewOptions->addWidget(previewGrid);
    mIsolateTile = new QCheckBox(tr("Isolate selected tile"), previewPane);
    previewOptions->addWidget(mIsolateTile);
    previewOptions->addStretch();
    previewLayout->addLayout(previewOptions);
    connect(mIsolateTile, &QCheckBox::toggled, this, &TileSetZEditor::refreshPreview);
    connect(mPreviewTile, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
        if (mIsolateTile->isChecked())
            refreshPreview();
    });
    previewLayout->addWidget(new QLabel(tr("Wheel to zoom, drag to pan. Double-click a tile to inspect it. Double-click a current-sheet tile to draw."), previewPane));
    mAppendButton = new QPushButton(tr("Append to Current Sheet"), generatorPane);
    mInsertButton = new QPushButton(tr("Replace from Selected Tile..."), generatorPane);
    mUseButton = new QPushButton(tr("Use as New Sheet"), generatorPane);
    generatorLayout->addWidget(mAppendButton);
    generatorLayout->addWidget(mInsertButton);
    generatorLayout->addWidget(mUseButton);
    mExportWall = new QPushButton(tr("Export Wall Assembly TMX..."), generatorPane);
    generatorLayout->addWidget(mExportWall);
    connect(mExportWall, &QPushButton::clicked, this, &TileSetZEditor::exportWall);
    generatorLayout->addStretch();
    connect(mAppendButton, &QPushButton::clicked, this, [this] { insertPreview(true); });
    connect(mInsertButton, &QPushButton::clicked, this, [this] { insertPreview(false); });
    connect(mUseButton, &QPushButton::clicked, this, &TileSetZEditor::usePreview);
    for (QComboBox *combo : {mMethod, mScaleChoice, mProjection, mMaskChannel, mWallDirection})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TileSetZEditor::invalidatePreview);
    connect(mOutputColumns, QOverload<int>::of(&QSpinBox::valueChanged), this, &TileSetZEditor::invalidatePreview);
    connect(mWorkspace, &QTabWidget::currentChanged, this, [this] { mCanvas->finishStroke(); refreshPreview(); });
    connect(mTiles->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (index.isValid())
            selectTile(index.row());
    });
    connect(mUndo, &QUndoStack::cleanChanged, this, [this] { updateDocument(); });
    mCanvas->committed = [this](const QImage &before, const QImage &after) {
        pushChange(before, after, TileSetZ::tileRect(mImage, mScale, mSelected).topLeft(), false, tr("Paint tile"));
    };
    mCanvas->picked = [this](const QColor &color) { setPaintColor(color); };
    splitter->setSizes({245, 935});
    generatorSplit->setSizes({350, 830});
    setPaintColor(Qt::white);
    setDocument(TileSetZ::blank(QSize(1024, 256)), 2, QString());
    invalidatePreview();
}

TileSetZEditor::~TileSetZEditor()
{
    disconnect(mUndo, nullptr, this, nullptr);
    mCanvas->committed = {};
    mCanvas->picked = {};
    mCanvas->moved = {};
    mCanvas->zoomChanged = {};
    mPreview->zoomChanged = {};
    mPreview->tileActivated = {};
    mTiles->setModel(nullptr);
    delete mUndo;
}

int TileSetZEditor::tileCount() const { return mModel->rowCount(); }

void TileSetZEditor::setPaintColor(const QColor &color)
{
    mPaintColor = color;
    mCanvas->color = color;
    mPaintButton->setText(tr("Color..."));
    mPaintButton->setToolTip(color.name(QColor::HexArgb));
    QSignalBlocker blocker(mOpacity);
    mOpacity->setValue(color.alpha());
    mPaintButton->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                               .arg(color.name(), qGray(color.rgb()) < 128 ? QStringLiteral("white") : QStringLiteral("black")));
}

void TileSetZEditor::setDocument(const QImage &image, int scale, const QString &path)
{
    mCanvas->finishStroke();
    mUndo->clear();
    statusBar()->clearMessage();
    mImage = image;
    mScale = scale;
    mFile = path;
    mDiskHash = path.isEmpty() ? QByteArray() : fileHash(path);
    mSelected = 0;
    mScaleChoice->setCurrentIndex(scale - 1);
    updateDocument();
}

void TileSetZEditor::updateDocument()
{
    setWindowModified(!mUndo->isClean());
    setWindowTitle(tr("TileSetZ - %1[*]").arg(mFile.isEmpty() ? tr("Untitled") : QFileInfo(mFile).fileName()));
    mModel->refresh();
    mSelected = qBound(0, mSelected, qMax(0, tileCount() - 1));
    mTiles->setCurrentIndex(mModel->index(mSelected));
    selectTile(mSelected);
    mAppendButton->setEnabled(!mPreviewImage.isNull() && mScaleChoice->currentIndex() + 1 == mScale);
    mInsertButton->setEnabled(mAppendButton->isEnabled());
}

void TileSetZEditor::selectTile(int id)
{
    mCanvas->finishStroke();
    mSelected = qBound(0, id, qMax(0, tileCount() - 1));
    mCanvas->setImage(mImage.copy(TileSetZ::tileRect(mImage, mScale, mSelected)));
    const QSize size = TileSetZ::tileSize(mScale);
    mTileLabel->setText(tr("Tile %1 / %2   |   %3 x %4 pixels").arg(mSelected).arg(qMax(0, tileCount() - 1)).arg(size.width()).arg(size.height()));
}

void TileSetZEditor::pushChange(const QImage &before, const QImage &after, const QPoint &position,
                               bool wholeSheet, const QString &text)
{
    if (after.isNull() || before == after)
        return;
    if (*mUndoBytes + before.sizeInBytes() + after.sizeInBytes() > 256 * 1024 * 1024) {
        mUndo->clear();
        statusBar()->showMessage(tr("Older undo history cleared at the 256 MB limit."), 10000);
    }
    mUndo->push(new TileSetZChange(before, after, mUndoBytes, [this, position, wholeSheet](const QImage &image) {
        if (wholeSheet) {
            mImage = image;
        } else {
            QPainter painter(&mImage);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(position, image);
        }
        updateDocument();
    }, text));
}

void TileSetZEditor::changeImage(const QImage &image, const QString &text)
{
    mCanvas->finishStroke();
    pushChange(mImage, image, QPoint(), true, text);
}

void TileSetZEditor::changeTile(const QImage &image, const QString &text)
{
    mCanvas->finishStroke();
    const QRect area = TileSetZ::tileRect(mImage, mScale, mSelected);
    pushChange(mImage.copy(area), image, area.topLeft(), false, text);
}

void TileSetZEditor::newSheet()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Tilesheet"));
    QFormLayout layout(&dialog);
    QComboBox scale;
    scale.addItems({tr("1x: 64 x 128"), tr("2x: 128 x 256")});
    scale.setCurrentIndex(mScale - 1);
    QSpinBox columns;
    columns.setRange(1, 128);
    columns.setValue(8);
    QSpinBox rows;
    rows.setRange(1, 128);
    rows.setValue(1);
    layout.addRow(tr("Tile size"), &scale);
    layout.addRow(tr("Columns"), &columns);
    layout.addRow(tr("Rows"), &rows);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QSize tile = TileSetZ::tileSize(scale.currentIndex() + 1);
    const QImage image = TileSetZ::blank(QSize(columns.value() * tile.width(), rows.value() * tile.height()));
    if (image.isNull()) {
        showError(tr("This sheet exceeds the image size limit."));
        return;
    }
    if (maybeSave())
        setDocument(image, scale.currentIndex() + 1, QString());
}

void TileSetZEditor::openSheet()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Tilesheet"), mFile, tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Tilesheet Scale"));
    QFormLayout layout(&dialog);
    QComboBox scale;
    scale.addItems({tr("1x: 64 x 128"), tr("2x: 128 x 256")});
    const bool in2x = QDir::fromNativeSeparators(path).split(QLatin1Char('/')).contains(QStringLiteral("2x"), Qt::CaseInsensitive);
    scale.setCurrentIndex(in2x ? 1 : 0);
    layout.addRow(tr("Tile size in this PNG"), &scale);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        loadSheet(path, scale.currentIndex() + 1, 0);
}

bool TileSetZEditor::loadSheet(const QString &path, int scale, int tileId, const QImage &loaded)
{
    if (!mFile.isEmpty() && QFileInfo(path).canonicalFilePath() == QFileInfo(mFile).canonicalFilePath()) {
        mTiles->setCurrentIndex(mModel->index(qBound(0, tileId, tileCount() - 1)));
        return true;
    }
    QString error;
    const QImage image = loaded.isNull() ? TileSetZ::readImage(path, &error) : loaded;
    const QSize tile = TileSetZ::tileSize(scale);
    if (image.isNull()) {
        showError(error);
        return false;
    }
    if (image.width() % tile.width() || image.height() % tile.height()) {
        showError(tr("The PNG dimensions must be multiples of %1 x %2. To cut an arbitrary image, use Open Source Texture.").arg(tile.width()).arg(tile.height()));
        return false;
    }
    if (!maybeSave())
        return false;
    setDocument(image, scale, path);
    mTiles->setCurrentIndex(mModel->index(qBound(0, tileId, tileCount() - 1)));
    return true;
}

bool TileSetZEditor::openTileset(Tiled::Tileset *tileset, int tileId)
{
    if (tileset->tileWidth() != 64 || tileset->tileHeight() != 128 || tileset->tileSpacing() || tileset->margin()) {
        showError(tr("This sheet uses a custom tile size or spacing. Open its PNG as a source texture to cut it into standard tiles."));
        return false;
    }
    QString error;
    const QString source2x = tileset->imageSource2x();
    if (!source2x.isEmpty()) {
        const QImage image = TileSetZ::readImage(source2x, &error);
        if (!image.isNull())
            return loadSheet(source2x, 2, tileId, image);
        const QSize size = QImageReader(source2x).size();
        if (size.isValid() && !TileSetZ::validSize(size)) {
            showError(error);
            return false;
        }
    }
    return loadSheet(tileset->imageSource(), 1, tileId);
}

bool TileSetZEditor::maybeSave()
{
    mCanvas->finishStroke();
    if (mUndo->isClean())
        return true;
    const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Unsaved Tilesheet"),
                   tr("Save changes to %1?").arg(mFile.isEmpty() ? tr("the new tilesheet") : QFileInfo(mFile).fileName()),
                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

bool TileSetZEditor::save(bool saveAs)
{
    mCanvas->finishStroke();
    QString path = mFile;
    if (saveAs || path.isEmpty()) {
        path = pngSavePath(this, tr("Save Tilesheet"), path);
        if (path.isEmpty())
            return false;
    }
    const bool sameFile = QFileInfo(path).absoluteFilePath().compare(QFileInfo(mFile).absoluteFilePath(), Qt::CaseInsensitive) == 0;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        showError(tr("Choose a filename ending in .png."));
        return false;
    }
    if (sameFile && fileHash(path) != mDiskHash) {
        if (QMessageBox::warning(this, tr("File Changed on Disk"), tr("The PNG was changed or removed outside TileSetZ. Replace it with this edited sheet?"),
                                 QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save)
            return false;
    }
    QString error;
    if (!TileSetZ::saveImage(mImage, path, &error)) {
        showError(error);
        return false;
    }
    mFile = path;
    mDiskHash = fileHash(path);
    mUndo->setClean();
    updateDocument();
    statusBar()->showMessage(tr("Saved %1. Use the configured Tiles/2x folder for 2x sheets, or Tiles for 1x sheets.").arg(QDir::toNativeSeparators(path)), 15000);
    emit imageSaved(path);
    return true;
}

void TileSetZEditor::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        if (!mUndo->isClean()) {
            QString error;
            const QImage saved = mFile.isEmpty() ? QImage() : TileSetZ::readImage(mFile, &error);
            if (saved.isNull())
                setDocument(TileSetZ::blank(QSize(1024, 256)), 2, QString());
            else
                setDocument(saved, mScale, mFile);
        }
        event->accept();
    } else {
        event->ignore();
    }
}

void TileSetZEditor::loadTexture()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Source Texture"), mSourceFile,
                                                     tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty())
        return;
    QString error;
    const QImage source = TileSetZ::readImage(path, &error);
    if (source.isNull()) {
        showError(error);
        return;
    }
    mSource = source;
    mSourceFile = path;
    mSourceLabel->setText(tr("%1 (%2 x %3 pixels)").arg(QFileInfo(path).fileName()).arg(source.width()).arg(source.height()));
    mSourceLabel->setToolTip(path);
    invalidatePreview();
}

void TileSetZEditor::loadMask()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Custom Mask"), QString(), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    QString error;
    const QImage mask = TileSetZ::readImage(path, &error);
    if (mask.isNull()) {
        showError(error);
        return;
    }
    if (mask.width() % 128 || mask.height() % 256) {
        showError(tr("Custom mask dimensions must be multiples of 128 x 256."));
        return;
    }
    mMask = mask;
    mMaskLabel->setText(tr("%1 (%2 x %3)").arg(QFileInfo(path).fileName()).arg(mask.width()).arg(mask.height()));
    invalidatePreview();
}

void TileSetZEditor::invalidatePreview()
{
    mPreviewImage = QImage();
    mWall = TileSetZ::WallResult();
    mAppendButton->setEnabled(false);
    mInsertButton->setEnabled(false);
    mUseButton->setEnabled(false);
    mExportWall->setEnabled(false);
    const bool wall = mMethod->currentIndex() == 9;
    mExportWall->setVisible(wall);
    const bool custom = mMethod->currentIndex() == 8;
    auto showRow = [this](QWidget *field, bool visible) {
        field->setVisible(visible);
        if (QWidget *label = mGeneratorForm->labelForField(field))
            label->setVisible(visible);
    };
    showRow(mProjection, custom);
    showRow(mMaskChannel, custom);
    mMaskButton->setVisible(custom);
    mMaskLabel->setVisible(custom);
    showRow(mOutputColumns, mMethod->currentIndex() == 0 || wall);
    for (QWidget *field : QList<QWidget *>{mWallLayout, mWallDirection})
        showRow(field, wall);
    const QSize face = TileSetZ::wallFaceSize(mScaleChoice->currentIndex() + 1);
    const int columns = (mSource.width() + face.width() - 1) / face.width();
    const int rows = (mSource.height() + face.height() - 1) / face.height();
    mWallLayout->setText(mSource.isNull()
                        ? tr("%1 x %2 source pixels per wall face").arg(face.width()).arg(face.height())
                        : tr("%1 x %2 pixels per face. %3 columns x %4 rows = %5 wall tiles.")
                          .arg(face.width()).arg(face.height()).arg(columns).arg(rows).arg(columns * rows));
    showRow(mTrimButton, mMethod->currentIndex() == 2 || mMethod->currentIndex() == 5 || mMethod->currentIndex() == 7);
    mPreviewLabel->setText(wall
            ? tr("The source is cut at its original pixel size into consecutive wall faces, then each piece is projected into its own tile. Partial edges keep their pixels and are padded with transparency. Source texture shows the cut grid.")
            : tr("Generate a preview before inserting tiles. Predefined masks retain their original layout."));
    refreshPreview();
}

void TileSetZEditor::refreshPreview()
{
    const int mode = mPreviewMode->currentIndex();
    const int scale = mScaleChoice->currentIndex() + 1;
    QImage image;
    QSize tile;
    mMaskPreview->setVisible(mode == 3);
    if (mode == 0) {
        image = mPreviewImage;
        tile = TileSetZ::tileSize(scale);
    } else if (mode == 1) {
        image = mWall.assembled;
    } else if (mode == 2) {
        image = mSource;
        if (mMethod->currentIndex() == 9)
            tile = TileSetZ::wallFaceSize(scale);
    } else if (mode == 3) {
        image = mMaskPreview->currentIndex() == 0 ? mMask
                : QImage(QStringLiteral(":/tilesetz/%1.png").arg(mMaskPreview->currentData().toString()));
        tile = TileSetZ::tileSize(2);
    } else {
        image = mImage;
        tile = TileSetZ::tileSize(mScale);
    }
    const bool hasTiles = !tile.isEmpty() && !image.isNull();
    mPreviewTile->setEnabled(hasTiles);
    mFitPreviewTile->setEnabled(hasTiles);
    mPreviewGrid->setEnabled(hasTiles);
    mIsolateTile->setEnabled(hasTiles);
    {
        QSignalBlocker blocker(mPreviewTile);
        mPreviewTile->setRange(0, tile.isEmpty() ? 0 : qMax(0, ((image.width() + tile.width() - 1) / tile.width())
                                   * ((image.height() + tile.height() - 1) / tile.height()) - 1));
    }
    if (hasTiles && mIsolateTile->isChecked()) {
        const int columns = (image.width() + tile.width() - 1) / tile.width();
        const int id = mPreviewTile->value();
        image = image.copy(QRect(QPoint((id % columns) * tile.width(), (id / columns) * tile.height()), tile));
    }
    if (mode == 1)
        mPreview->setWall(mWall, scale);
    else
        mPreview->setImage(image, tile);
}

void TileSetZEditor::exportWall()
{
    if (mWall.sheet.isNull())
        return;
    QFileDialog dialog(this, tr("Export Assembled Wall"), QString(), tr("TileZed map (*.tmx)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("tmx"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString mapPath = dialog.selectedFiles().value(0);
    if (!mapPath.endsWith(QStringLiteral(".tmx"), Qt::CaseInsensitive)) {
        showError(tr("Choose a filename ending in .tmx."));
        return;
    }
    const int scale = mScaleChoice->currentIndex() + 1;
    QString tilesRoot = Tiled::Internal::TileMetaInfoMgr::instance()->tilesDirectory();
    if (scale == 2)
        tilesRoot = QDir(tilesRoot).filePath(QStringLiteral("2x"));
    const QString imagePath = pngSavePath(this, tr("Save Wall Tilesheet in the Tiles Folder"),
                                          QDir(tilesRoot).filePath(QFileInfo(mapPath).completeBaseName() + QStringLiteral(".png")));
    if (imagePath.isEmpty())
        return;
    if (!imagePath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        showError(tr("Choose a filename ending in .png."));
        return;
    }
    const QByteArray data = TileSetZ::wallMap(mWall, scale, QFileInfo(imagePath).completeBaseName(),
                                             QDir(QFileInfo(mapPath).absolutePath()).relativeFilePath(imagePath));
    QSaveFile mapFile(mapPath);
    if (!mapFile.open(QIODevice::WriteOnly) || mapFile.write(data) != data.size()) {
        showError(mapFile.errorString());
        return;
    }
    QString error;
    if (!TileSetZ::saveImage(mWall.sheet, imagePath, &error)) {
        showError(error);
        return;
    }
    emit imageSaved(imagePath);
    if (!mapFile.commit()) {
        showError(tr("The PNG was saved, but the TMX could not be saved: %1").arg(mapFile.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Wall exported. Open %1 in TileZed to copy or place the assembled wall.").arg(QDir::toNativeSeparators(mapPath)), 20000);
}

void TileSetZEditor::preview()
{
    if (mSource.isNull()) {
        showError(tr("Open a source texture first."));
        return;
    }
    QString error;
    QSize layout;
    const int scale = mScaleChoice->currentIndex() + 1;
    const int method = mMethod->currentIndex();
    if (method == 0) {
        mPreviewImage = TileSetZ::cut(mSource, scale, mOutputColumns->value(), &layout);
    } else if (method == 1) {
        mPreviewImage = TileSetZ::blank(TileSetZ::tileSize(scale));
        const QImage fitted = mSource.scaled(mPreviewImage.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
        QPainter painter(&mPreviewImage);
        painter.drawImage((mPreviewImage.width() - fitted.width()) / 2, mPreviewImage.height() - fitted.height(), fitted);
    } else if (method == 9) {
        mWall = TileSetZ::wall(mSource, scale, mWallDirection->currentIndex() == 0, mOutputColumns->value(), &error);
        mPreviewImage = mWall.sheet;
    } else {
        mPreviewImage = TileSetZ::generate(mSource, TileSetZ::Preset(method - 2), scale, mTrimColor,
                                         mMask, TileSetZ::Projection(mProjection->currentIndex()), mMaskChannel->currentIndex() == 1, &error);
    }
    if (mPreviewImage.isNull()) {
        showError(error.isEmpty() ? tr("The generated sheet exceeds the image size limit.") : error);
        return;
    }
    const QSize tile = TileSetZ::tileSize(scale);
    mPreviewMode->setCurrentIndex(0);
    refreshPreview();
    QString info = tr("%1 x %2 pixels, %3 sheet slots.").arg(mPreviewImage.width()).arg(mPreviewImage.height())
            .arg((mPreviewImage.width() / tile.width()) * (mPreviewImage.height() / tile.height()));
    if (method == 0)
        info += tr(" Source: %1 columns x %2 rows. Pieces run left to right, then top to bottom. Partial pieces keep their pixels at the top left.").arg(layout.width()).arg(layout.height());
    if (method == 9)
        info += tr(" %1 individual wall tiles from %2 source columns x %3 rows. IDs run left to right, then top to bottom. Extra sheet slots are empty. Assembly check and TMX export retain the source arrangement.")
                .arg(mWall.pieces.size()).arg(mWall.sourceGrid.width()).arg(mWall.sourceGrid.height());
    if (scale != mScale)
        info += tr(" Choose Use as New Sheet for this tile scale.");
    mPreviewLabel->setText(info);
    mAppendButton->setEnabled(scale == mScale);
    mInsertButton->setEnabled(scale == mScale);
    mUseButton->setEnabled(true);
    mExportWall->setEnabled(method == 9);
}

void TileSetZEditor::insertPreview(bool append)
{
    if (mPreviewImage.isNull() || mScaleChoice->currentIndex() + 1 != mScale)
        return;
    const int first = append ? tileCount() : mSelected;
    QString error;
    const QImage result = TileSetZ::insert(mImage, mPreviewImage, mScale, first, &error);
    if (result.isNull()) {
        showError(error);
        return;
    }
    if (!append && QMessageBox::question(this, tr("Replace Tiles"),
                   tr("Replace tiles starting at ID %1? Their current pixels will be replaced, including transparent areas. Undo is available.").arg(first),
                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    changeImage(result, append ? tr("Append generated tiles") : tr("Replace tiles"));
    mTiles->setCurrentIndex(mModel->index(first));
    mTiles->scrollTo(mModel->index(first));
}

void TileSetZEditor::usePreview()
{
    if (mPreviewImage.isNull() || !maybeSave())
        return;
    const QImage result = mPreviewImage;
    const int scale = mScaleChoice->currentIndex() + 1;
    setDocument(TileSetZ::blank(result.size()), scale, QString());
    changeImage(result, tr("Generate tilesheet"));
}

void TileSetZEditor::showError(const QString &message)
{
    QMessageBox::warning(this, tr("TileSetZ"), message);
}

bool TileSetZEditor::runSelfTest(QString *error)
{
    int checks = 0;
    auto require = [&](bool condition, const QString &message) {
        if (!condition)
            *error = message;
        else
            ++checks;
        return condition;
    };
    QElapsedTimer elapsed;
    elapsed.start();
    for (int scale : {1, 2}) {
        const int step = 32 * scale;
        const int rise = 96 * scale;
        QImage mural = TileSetZ::blank(QSize(step * 5, rise * 2));
        for (int y = 0; y < mural.height(); ++y)
            for (int x = 0; x < mural.width(); ++x)
                mural.setPixel(x, y, qRgb(x % 256, y % 256, (x * 7 + y * 11) % 256));
        for (bool left : {false, true}) {
            const TileSetZ::WallResult wall = TileSetZ::wall(mural, scale, left, 3, error);
            QImage expected = TileSetZ::blank(wall.assembled.size());
            if (!require(!expected.isNull() && wall.pieces.size() == 10, QStringLiteral("Wall generation failed")))
                return false;
            for (int y = 0; y < mural.height(); ++y)
                for (int x = 0; x < mural.width(); ++x)
                    expected.setPixel(left ? x : x + step,
                                      y + (left ? 5 * 16 * scale - x / 2 : x / 2), mural.pixel(x, y));
            if (!require(expected == wall.assembled, QStringLiteral("Wall reconstruction has a seam, stretched texture or incorrect floor alignment")))
                return false;
            QByteArray xml = TileSetZ::wallMap(wall, scale, QStringLiteral("mural"), QStringLiteral("2x/mural.png"));
            QBuffer buffer(&xml);
            buffer.open(QIODevice::ReadOnly);
            Tiled::MapReader reader;
            std::unique_ptr<Tiled::Map> map(reader.readMap(&buffer));
            if (!require(map && map->orientation() == Tiled::Map::LevelIsometric && map->layerCount() == 2
                         && map->tilesets().size() == 1 && map->tilesets().first()->tileCount() == 12,
                         QStringLiteral("Wall TMX could not be read with its original IDs: %1").arg(reader.errorString())))
                return false;
            Tiled::ZLevelRenderer renderer(map.get());
            renderer.set2x(scale == 2);
            const auto first = wall.pieces.first();
            const QPoint origin = renderer.tileToPixelCoords(first.x, first.y, first.level).toPoint() - first.screen;
            for (const TileSetZ::WallPiece &piece : wall.pieces) {
                const Tiled::TileLayer *layer = map->layerAt(piece.level)->asTileLayer();
                if (!require(layer && layer->name() == QStringLiteral("Walls") && layer->level() == piece.level
                             && layer->cellAt(piece.x, piece.y).tile && layer->cellAt(piece.x, piece.y).tile->id() == piece.id
                             && renderer.tileToPixelCoords(piece.x, piece.y, piece.level).toPoint() == piece.screen + origin,
                             QStringLiteral("Wall piece %1 on floor %2: layer %3, tile %4, position %5,%6, expected %7,%8")
                             .arg(piece.id).arg(piece.level).arg(layer ? layer->name() : QStringLiteral("missing"))
                             .arg(layer && layer->cellAt(piece.x, piece.y).tile ? layer->cellAt(piece.x, piece.y).tile->id() : -1)
                             .arg(renderer.tileToPixelCoords(piece.x, piece.y, piece.level).x())
                             .arg(renderer.tileToPixelCoords(piece.x, piece.y, piece.level).y())
                             .arg((piece.screen + origin).x()).arg((piece.screen + origin).y())))
                    return false;
            }
            const QList<Tiled::Tileset *> sets = map->tilesets();
            map.reset();
            qDeleteAll(sets);
        }
        for (const QSize size : {QSize(1, 1), QSize(step, rise), QSize(130, 200), QSize(step, rise * 9)}) {
            QImage source = TileSetZ::blank(size);
            for (int y = 0; y < source.height(); ++y)
                for (int x = 0; x < source.width(); ++x)
                    source.setPixel(x, y, qRgba(x % 256, y % 256, (x * 7 + y * 11) % 256, (x * 3 + y * 13) % 256));
            const int columns = (size.width() + step - 1) / step;
            const int rows = (size.height() + rise - 1) / rise;
            for (bool left : {false, true}) {
                const auto cut = TileSetZ::wall(source, scale, left, 8, error);
                if (!require(cut.sourceGrid == QSize(columns, rows) && cut.pieces.size() == columns * rows,
                             QStringLiteral("Wall tile count does not cover the complete source")))
                    return false;
                bool pixelsMatch = true;
                for (int id = 0; id < cut.pieces.size() && pixelsMatch; ++id) {
                    const QImage piece = cut.sheet.copy(TileSetZ::tileRect(cut.sheet, scale, id));
                    QImage expected = TileSetZ::blank(TileSetZ::tileSize(scale));
                    for (int v = 0; v < rise; ++v) {
                        for (int u = 0; u < step; ++u) {
                            const QPoint from((id % columns) * step + u, (id / columns) * rise + v);
                            if (source.rect().contains(from))
                                expected.setPixel(left ? u : step + u, v + (left ? 16 * scale - u / 2 : u / 2), source.pixel(from));
                        }
                    }
                    pixelsMatch = piece == expected && cut.pieces.at(id).id == id
                            && cut.pieces.at(id).level == rows - 1 - id / columns;
                }
                if (!require(pixelsMatch, QStringLiteral("Wall precut resampled, dropped, reordered or incorrectly padded source pixels")))
                    return false;
                for (int id = cut.pieces.size(); id < (cut.sheet.width() / (64 * scale)) * (cut.sheet.height() / (128 * scale)); ++id)
                    if (!require(!TileSetZ::hasPixels(cut.sheet.copy(TileSetZ::tileRect(cut.sheet, scale, id))),
                                 QStringLiteral("Unused wall sheet slots contain extra tiles")))
                        return false;
            }
        }
    }
    {
        QImage wide = TileSetZ::blank(QSize(8192, 192));
        wide.fill(Qt::red);
        const auto cut = TileSetZ::wall(wide, 2, true, 8, error);
        if (!require(cut.pieces.size() == 128 && cut.sheet.size() == QSize(1024, 4096),
                     QStringLiteral("A wide source was rejected because its assembled preview is too large")))
            return false;
        TileSetZPreview view(nullptr);
        view.setWall(cut, 2);
        if (!require(view.scene()->items().size() == 128 && view.sceneRect().size() == QSizeF(8256, 4320),
                     QStringLiteral("The assembly preview omitted individually cut wall tiles")))
            return false;
    }
    for (const QString &name : TileSetZ::maskFiles()) {
        const QImage mask(QStringLiteral(":/tilesetz/%1.png").arg(name));
        if (!require(!mask.isNull() && mask.width() == 1024 && mask.height() % 256 == 0,
                     QStringLiteral("Bundled mask unavailable: %1").arg(name)))
            return false;
    }
    QImage material = TileSetZ::blank(QSize(37, 53));
    material.fill(qRgb(180, 140, 100));
    const QImage left = TileSetZ::project(material, TileSetZ::LeftWall);
    const QImage right = TileSetZ::project(material, TileSetZ::RightWall);
    if (!require(left.pixel(2, 33) == qRgb(149, 109, 69)
                 && left.pixel(65, 2) == qRgb(149, 109, 69)
                 && right.pixel(60, 2) == qRgb(180, 140, 100)
                 && right.pixel(123, 33) == qRgb(180, 140, 100)
                 && !qAlpha(left.pixel(0, 0)), QStringLiteral("Wall projection or directional shading changed")))
        return false;
    const QImage floor = TileSetZ::project(material, TileSetZ::Floor);
    if (!require(TileSetZ::hasPixels(floor.copy(0, 192, 128, 64)) && !TileSetZ::hasPixels(floor.copy(0, 0, 128, 192)),
                 QStringLiteral("Floor is not anchored at the bottom of the tile")))
        return false;
    const int heights[] = {2048, 512, 2816, 1024, 1536, 4096};
    for (int preset = 0; preset < 6; ++preset) {
        const QImage generated = TileSetZ::generate(material, TileSetZ::Preset(preset), 2, QColor(45, 81, 123),
                                                    QImage(), TileSetZ::Texture, false, error);
        if (!require(generated.size() == QSize(1024, heights[preset]) && TileSetZ::hasPixels(generated),
                     QStringLiteral("Empty or incorrectly sized preset %1: %2").arg(preset).arg(*error)))
            return false;
        const QImage small = TileSetZ::generate(material, TileSetZ::Preset(preset), 1, QColor(45, 81, 123),
                                                QImage(), TileSetZ::Texture, false, error);
        if (!require(small == generated.scaled(generated.size() / 2, Qt::IgnoreAspectRatio, Qt::FastTransformation),
                     QStringLiteral("1x preset differs from its 2x projection")))
            return false;
    }
    const QImage trimMask(QStringLiteral(":/tilesetz/mask_trim.png"));
    const QColor trimColor(23, 79, 151);
    const QImage trimmed = TileSetZ::generate(material, TileSetZ::Walls, 2, trimColor, QImage(), TileSetZ::Texture, false, error);
    bool foundTrim = false;
    for (int y = 0; y < trimMask.height(); ++y) {
        for (int x = 0; x < trimMask.width(); ++x) {
            if (trimMask.pixel(x, y) == qRgb(99, 99, 99)) {
                foundTrim = true;
                if (!require(trimmed.pixel(x, y) == trimColor.rgba(), QStringLiteral("Trim color was not applied")))
                    return false;
                break;
            }
        }
        if (foundTrim)
            break;
    }
    if (!require(foundTrim, QStringLiteral("Trim palette entry is missing")))
        return false;
    QImage mask = TileSetZ::blank(QSize(128, 256));
    mask.setPixel(0, 0, qRgb(255, 0, 105));
    mask.setPixel(1, 0, qRgba(255, 0, 105, 128));
    mask.setPixel(2, 0, qRgb(0, 0, 0));
    material.fill(qRgba(100, 110, 120, 128));
    const QImage pink = TileSetZ::masked(material, mask, false);
    const QImage alpha = TileSetZ::masked(material, mask, true);
    if (!require(pink.pixel(0, 0) == qRgba(100, 110, 120, 128) && !qAlpha(pink.pixel(1, 0)) && !qAlpha(pink.pixel(2, 0))
                 && qAlpha(alpha.pixel(1, 0)) == 64 && qAlpha(alpha.pixel(2, 0)) == 128,
                 QStringLiteral("Mask selection or source alpha was lost")))
        return false;
    if (!require(TileSetZ::generate(material, TileSetZ::ExternalMask, 2, Qt::black, mask, TileSetZ::Texture, true, error) == alpha,
                 QStringLiteral("Custom mask generation differs from mask composition")))
        return false;
    QImage source = TileSetZ::blank(QSize(259, 517));
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixel(x, y, qRgba(x % 256, y % 256, (x + y) % 256, (x * 3 + y * 7) % 256));
    for (int scale : {1, 2}) {
        QSize layout;
        const QSize tile = TileSetZ::tileSize(scale);
        const QImage cut = TileSetZ::cut(source, scale, 8, &layout);
        bool pixelsMatch = !cut.isNull();
        for (int y = 0; y < source.height() && pixelsMatch; ++y) {
            for (int x = 0; x < source.width(); ++x) {
                const int id = (y / tile.height()) * layout.width() + x / tile.width();
                const QPoint location = TileSetZ::tileRect(cut, scale, id).topLeft() + QPoint(x % tile.width(), y % tile.height());
                if (cut.pixel(location) != source.pixel(x, y)) {
                    pixelsMatch = false;
                    break;
                }
            }
        }
        if (!require(pixelsMatch, QStringLiteral("Cutting lost pixels at scale %1").arg(scale)))
            return false;
        const QRect last = TileSetZ::tileRect(cut, scale, layout.width() * layout.height() - 1);
        if (!require(!qAlpha(cut.pixel(last.bottomRight())), QStringLiteral("Partial tiles were not padded transparently")))
            return false;
    }
    QImage sheet = TileSetZ::blank(QSize(384, 512));
    sheet.fill(qRgba(40, 80, 120, 127));
    QImage additions = TileSetZ::blank(QSize(256, 256));
    additions.fill(qRgb(30, 90, 180));
    const QImage appended = TileSetZ::insert(sheet, additions, 2, 6, error);
    if (!require(appended.size() == QSize(384, 768) && appended.copy(sheet.rect()) == sheet
                 && appended.copy(TileSetZ::tileRect(appended, 2, 7)) == additions.copy(128, 0, 128, 256),
                 QStringLiteral("Appending renumbered or altered an existing sheet")))
        return false;
    TileSetZEditor editor;
    editor.setAttribute(Qt::WA_DontShowOnScreen);
    editor.setDocument(sheet, 2, QString());
    editor.selectTile(4);
    editor.changeTile(additions.copy(0, 0, 128, 256), QStringLiteral("Replace tile"));
    if (!require(editor.mImage.copy(TileSetZ::tileRect(editor.mImage, 2, 3)) == sheet.copy(0, 256, 128, 256)
                 && !editor.mUndo->isClean(), QStringLiteral("Editing changed an adjacent tile or lost the modified state")))
        return false;
    editor.mUndo->undo();
    if (!require(editor.mImage == sheet && editor.mUndo->isClean(), QStringLiteral("Tile undo failed")))
        return false;
    editor.mUndo->redo();
    const QImage edited = editor.mImage;
    editor.changeImage(appended, QStringLiteral("Append tiles"));
    editor.mUndo->undo();
    if (!require(editor.mImage == edited, QStringLiteral("Sheet resize undo failed")))
        return false;
    editor.setDocument(TileSetZ::blank(QSize(128, 256)), 2, QString());
    editor.setPaintColor(QColor(40, 140, 210, 192));
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(4, 4), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.mCanvas, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(24, 4), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.mCanvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(24, 4), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editor.mCanvas, &release);
    bool continuous = true;
    for (int x = 2; x <= 12; ++x)
        continuous &= editor.mImage.pixel(x, 2) == editor.mPaintColor.rgba();
    if (!require(continuous && editor.mUndo->count() == 1, QStringLiteral("A brush stroke has gaps or creates multiple undo steps")))
        return false;
    editor.mUndo->undo();
    if (!require(!TileSetZ::hasPixels(editor.mImage), QStringLiteral("Brush undo did not restore transparent pixels")))
        return false;
    editor.mUndo->redo();
    editor.mCanvas->alphaLock = true;
    editor.setPaintColor(Qt::red);
    QApplication::sendEvent(editor.mCanvas, &press);
    QApplication::sendEvent(editor.mCanvas, &release);
    if (!require(qAlpha(editor.mImage.pixel(2, 2)) == 192 && qRed(editor.mImage.pixel(2, 2)) == 255,
                 QStringLiteral("Painting with locked alpha changed opacity")))
        return false;
    QMouseEvent pick(QEvent::MouseButtonPress, QPointF(4, 4), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    editor.setPaintColor(Qt::green);
    QApplication::sendEvent(editor.mCanvas, &pick);
    if (!require(editor.mPaintColor == QColor(255, 0, 0, 192), QStringLiteral("The color picker lost opacity")))
        return false;
    QMouseEvent erasePress(QEvent::MouseButtonPress, QPointF(4, 4), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(editor.mCanvas, &erasePress);
    QApplication::sendEvent(editor.mCanvas, &release);
    if (!require(!qAlpha(editor.mImage.pixel(2, 2)), QStringLiteral("Right-button erasing failed")))
        return false;
    editor.mUndo->undo();
    editor.mCanvas->alphaLock = false;
    editor.mCanvas->tool = TileSetZCanvas::Fill;
    editor.setPaintColor(Qt::blue);
    QMouseEvent fillPress(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor.mCanvas, &fillPress);
    if (!require(editor.mImage.pixel(0, 0) == qRgb(0, 0, 255) && editor.mImage.pixel(2, 2) == qRgba(255, 0, 0, 192),
                 QStringLiteral("Flood fill crossed the target color boundary")))
        return false;
    editor.setDocument(TileSetZ::blank(QSize(128, 256)), 2, QString());
    editor.mCanvas->tool = TileSetZCanvas::Line;
    editor.setPaintColor(Qt::red);
    QApplication::sendEvent(editor.mCanvas, &press);
    QApplication::sendEvent(editor.mCanvas, &move);
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(editor.mCanvas, &cancel);
    QApplication::sendEvent(editor.mCanvas, &release);
    if (!require(editor.mUndo->isClean() && !TileSetZ::hasPixels(editor.mImage), QStringLiteral("Cancelling a shape changed the document")))
        return false;
    for (auto tool : {TileSetZCanvas::Line, TileSetZCanvas::Rectangle, TileSetZCanvas::Ellipse}) {
        editor.mCanvas->tool = tool;
        QApplication::sendEvent(editor.mCanvas, &press);
        QMouseEvent diagonal(QEvent::MouseMove, QPointF(40, 40), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.mCanvas, &diagonal);
        QApplication::sendEvent(editor.mCanvas, &release);
        if (!require(editor.mUndo->count() == 1 && TileSetZ::hasPixels(editor.mImage), QStringLiteral("Shape drawing did not commit one undo operation")))
            return false;
        editor.mUndo->undo();
        if (!require(!TileSetZ::hasPixels(editor.mImage), QStringLiteral("Shape undo left pixels behind")))
            return false;
    }
    editor.mCanvas->tool = TileSetZCanvas::Paint;
    QTemporaryDir directory;
    if (!require(directory.isValid(), QStringLiteral("Unable to create a temporary image directory")))
        return false;
    const QString path = directory.filePath(QStringLiteral("sheet.png"));
    if (!TileSetZ::saveImage(source, path, error))
        return false;
    if (!require(TileSetZ::readImage(path, error) == source, QStringLiteral("PNG roundtrip changed RGBA values")))
        return false;
    editor.setDocument(sheet, 2, path);
    editor.selectTile(0);
    editor.changeTile(additions.copy(0, 0, 128, 256), QStringLiteral("Paint tile"));
    if (!require(editor.save() && editor.mUndo->isClean() && TileSetZ::readImage(path, error) == editor.mImage,
                 QStringLiteral("Saving the edited sheet failed")))
        return false;
    if (!TileSetZ::saveImage(source, path, error))
        return false;
    QTimer::singleShot(0, &editor, [&editor] {
        for (QMessageBox *box : editor.findChildren<QMessageBox *>())
            box->button(QMessageBox::Cancel)->click();
    });
    if (!require(!editor.save() && TileSetZ::readImage(path, error) == source,
                 QStringLiteral("External edits were overwritten after Cancel")))
        return false;
    const QByteArray unchanged = fileHash(path);
    QString writeError;
    if (!require(!TileSetZ::saveImage(source, path + QStringLiteral("/missing/file.png"), &writeError)
                 && fileHash(path) == unchanged, QStringLiteral("Failed saving damaged an existing PNG")))
        return false;
    const QString path1x = directory.filePath(QStringLiteral("material-1x.png"));
    const QString path2x = directory.filePath(QStringLiteral("material-2x.png"));
    QImage image1x = TileSetZ::blank(QSize(512, 128));
    image1x.fill(Qt::green);
    QImage image2x = TileSetZ::blank(QSize(1024, 256));
    image2x.fill(Qt::blue);
    if (!TileSetZ::saveImage(image1x, path1x, error) || !TileSetZ::saveImage(image2x, path2x, error))
        return false;
    Tiled::Tileset tileset(QStringLiteral("material"), 64, 128);
    tileset.setImageSource(path1x);
    tileset.setImageSource2x(path2x);
    editor.mUndo->setClean();
    if (!require(editor.openTileset(&tileset, 3) && editor.mScale == 2 && editor.mSelected == 3 && editor.mImage == image2x,
                 QStringLiteral("Opening from a tile did not prefer the readable 2x sheet and preserve its ID")))
        return false;
    editor.changeTile(TileSetZ::blank(QSize(128, 256)), QStringLiteral("Clear tile"));
    if (!require(editor.save() && TileSetZ::readImage(path1x, error) == image1x
                 && TileSetZ::readImage(path2x, error).copy(384, 0, 128, 256) == TileSetZ::blank(QSize(128, 256)),
                 QStringLiteral("Editing the 2x tile affected the wrong file or tile ID")))
        return false;
    tileset.setImageSource2x(directory.filePath(QStringLiteral("absent.png")));
    if (!require(editor.openTileset(&tileset, 5) && editor.mScale == 1 && editor.mSelected == 5 && editor.mImage == image1x,
                 QStringLiteral("Opening a 1x-only tile failed")))
        return false;
    editor.changeTile(TileSetZ::blank(QSize(64, 128)), QStringLiteral("Clear tile"));
    QTimer::singleShot(0, &editor, [&editor] {
        for (QMessageBox *box : editor.findChildren<QMessageBox *>())
            box->button(QMessageBox::Cancel)->click();
    });
    if (!require(!editor.close() && !editor.mUndo->isClean(), QStringLiteral("Closing with Cancel lost unsaved edits")))
        return false;
    QTimer::singleShot(0, &editor, [&editor] {
        for (QMessageBox *box : editor.findChildren<QMessageBox *>())
            box->button(QMessageBox::Discard)->click();
    });
    const bool discarded = editor.close();
    if (!require(discarded && editor.mUndo->isClean() && editor.mImage == image1x,
                 QStringLiteral("Closing with Discard retained discarded edits")))
        return false;
    editor.setDocument(sheet, 2, QString());
    editor.mSource = material;
    editor.mMethod->setCurrentIndex(9);
    editor.mSource = TileSetZ::blank(QSize(130, 200));
    editor.mSource.fill(Qt::red);
    editor.invalidatePreview();
    editor.preview();
    if (!require(editor.mWall.pieces.size() == 6 && editor.mPreviewMode->currentIndex() == 0,
                 QStringLiteral("Wall precut workflow does not start with six individual tiles for a 130x200 source")))
        return false;
    editor.mPreviewMode->setCurrentIndex(2);
    if (!require(editor.mPreviewTile->maximum() == 5,
                 QStringLiteral("Source cut preview omits a partial edge tile")))
        return false;
    editor.mPreviewTile->setValue(5);
    editor.mIsolateTile->setChecked(true);
    if (!require(editor.mPreview->sceneRect().size() == QSizeF(64, 192),
                 QStringLiteral("Isolated source cut does not retain the full wall face bounds")))
        return false;
    editor.mIsolateTile->setChecked(false);
    editor.mSource = material;
    editor.mMethod->setCurrentIndex(2);
    editor.preview();
    editor.usePreview();
    if (!require(editor.mImage.size() == QSize(1024, 2048) && !editor.mUndo->isClean(), QStringLiteral("Generated sheet was not marked unsaved")))
        return false;
    editor.mMethod->setCurrentIndex(3);
    if (!require(editor.mPreviewImage.isNull() && !editor.mAppendButton->isEnabled(), QStringLiteral("Changing generation settings left a stale insertable preview")))
        return false;
    editor.mUndo->setClean();
    editor.resize(1180, 820);
    editor.show();
    QApplication::processEvents();
    editor.mWorkspace->setCurrentIndex(1);
    editor.mPreviewMode->setCurrentIndex(4);
    editor.mPreview->setZoom(6);
    editor.resize(1200, 840);
    QApplication::processEvents();
    if (!require(qAbs(editor.mPreview->transform().m11() - 6.0) < 0.001, QStringLiteral("Resizing reset the preview zoom")))
        return false;
    editor.mPreview->fitTile(9);
    if (!require(editor.mPreview->transform().m11() >= 1, QStringLiteral("The preview cannot display a complete tile at native size")))
        return false;
    editor.mWorkspace->setCurrentIndex(0);
    const QString capture = qEnvironmentVariable("PZTOOLS_TILESETZ_CAPTURE");
    if (!capture.isEmpty() && !editor.grab().save(capture, "PNG")) {
        *error = QStringLiteral("Unable to save TileSetZ capture");
        return false;
    }
    if (!capture.isEmpty()) {
        QImage brick = TileSetZ::blank(QSize(64, 64));
        brick.fill(QColor(117, 73, 48));
        {
            QPainter painter(&brick);
            painter.setPen(QColor(210, 186, 148));
            for (int y = 0; y < 64; y += 16) {
                painter.drawLine(0, y, 63, y);
                for (int x = (y / 16 % 2) * 16; x < 64; x += 32)
                    painter.drawLine(x, y, x, y + 15);
            }
        }
        editor.mWorkspace->setCurrentIndex(1);
        editor.mSource = brick.scaled(384, 192);
        editor.mSourceLabel->setText(QStringLiteral("brick.png (384 x 192)"));
        editor.mMethod->setCurrentIndex(9);
        editor.preview();
        QApplication::processEvents();
        editor.grab().save(capture + QStringLiteral(".0.png"));
        editor.mPreviewMode->setCurrentIndex(3);
        editor.mMaskPreview->setCurrentIndex(1);
        editor.mPreviewTile->setValue(0);
        editor.mIsolateTile->setChecked(true);
        QApplication::processEvents();
        editor.grab().save(capture + QStringLiteral(".1.png"));
        editor.mPreviewMode->setCurrentIndex(0);
        editor.mIsolateTile->setChecked(false);
        QApplication::processEvents();
        editor.grab().save(capture + QStringLiteral(".2.png"));
    }
    editor.hide();
    qInfo().noquote() << QStringLiteral("TileSetZ: %1 checks passed in %2 ms").arg(checks).arg(elapsed.elapsed());
    return true;
}
