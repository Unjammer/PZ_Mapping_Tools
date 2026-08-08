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

#include "basegraphicsview.h"

#include "basegraphicsscene.h"
#include "preferences.h"
#include "zoomable.h"
#include "../portablesettings.h"

#include "zlevelrenderer.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QLabel>
#include <QLocale>
#define PZ_OPENGL_WIDGET 1
#if PZ_OPENGL_WIDGET
#include <QOpenGLWidget>
#else
#include <QGLWidget>
#endif
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>

using namespace Tiled;

namespace {

QString diagnosticMemoryText(quint64 bytes)
{
    if (!bytes)
        return QObject::tr("unknown");
    const qreal gibibyte = 1024.0 * 1024.0 * 1024.0;
    if (bytes >= quint64(gibibyte))
        return QObject::tr("%1 GiB").arg(bytes / gibibyte, 0, 'f', 2);
    return QObject::tr("%1 MiB").arg(
                bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

}

BaseGraphicsView::BaseGraphicsView(AllowOpenGL openGL, QWidget *parent)
    : QGraphicsView(parent)
    , mHandScrolling(false)
    , mZoomable(new Zoomable(this))
    , mMousePressed(false)
    , mScrollTimer(this)
    , mScene(0)
    , mMiniMap(0)
    , mNightPreviewButton(new QToolButton(this))
    , mPoweredPreviewButton(new QToolButton(this))
    , mSnowPreviewButton(new QToolButton(this))
    , mJumboPreviewButton(new QToolButton(this))
    , mRenderDiagnosticsEnabled(QSettings().value(
          QStringLiteral("RenderDiagnostics/Enabled"), true).toBool())
    , mRenderDiagnosticsLabel(new QLabel(this))
    , mDiagnosticsFps(0.0)
    , mDiagnosticsFrameMs(0.0)
    , mDiagnosticsRenderedTiles(0)
    , mDiagnosticsMemoryBytes(0)
{
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
//    setDragMode(QGraphicsView::ScrollHandDrag);

    /* Since Qt 4.5, setting this attribute yields significant repaint
     * reduction when the view is being resized. */
    viewport()->setAttribute(Qt::WA_StaticContents);

    /* Since Qt 4.6, mouse tracking is disabled when no graphics item uses
     * hover events. We need to set it since our scene wants the events. */
    viewport()->setMouseTracking(true);

    // Adjustment for antialiasing is done by the items that need it
    setOptimizationFlags(QGraphicsView::DontAdjustForAntialiasing);

    connect(mZoomable, &Zoomable::scaleChanged, this, &BaseGraphicsView::adjustScale);

//    mScrollTimer.setSingleShot(true);
    mScrollTimer.setInterval(30);
    connect(&mScrollTimer, &QTimer::timeout, this, &BaseGraphicsView::autoScrollTimeout);

    mMiniMap = new MiniMap(this);

    mNightPreviewButton->setObjectName(
                QStringLiteral("NightPreviewCanvasButton"));
    mNightPreviewButton->setCheckable(true);
    mNightPreviewButton->setAutoRaise(false);
    mNightPreviewButton->setFixedSize(36, 32);
    mNightPreviewButton->setText(QString::fromUtf8("\xE2\x98\x80"));
    mNightPreviewButton->setToolTip(tr("Switch to night preview"));
    mNightPreviewButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(245,245,245,225); color: #202020;"
        " border: 2px solid #d88c28; border-radius: 6px;"
        " font-size: 19px; font-weight: bold; }"
        "QToolButton:checked { background: rgba(20,28,52,235);"
        " color: #ffe39a; border-color: #5d86d7; }"));
    mNightPreviewButton->raise();
    connect(mNightPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        mNightPreviewButton->setText(enabled
                ? QString::fromUtf8("\xE2\x98\xBE")
                : QString::fromUtf8("\xE2\x98\x80"));
        mNightPreviewButton->setToolTip(enabled
                ? tr("Switch to day preview")
                : tr("Switch to night preview"));
        emit nightPreviewToggled(enabled);
    });

    const QString previewStyle = QStringLiteral(
        "QToolButton { background: rgba(32,36,42,225); color: #d9dde5;"
        " border: 1px solid #68717d; border-radius: 5px;"
        " font-size: 11px; font-weight: bold; }"
        "QToolButton:checked { background: rgba(35,92,132,235);"
        " color: white; border: 2px solid #76c7ff; }");
    const auto setupPreviewButton = [previewStyle](
            QToolButton *button, const QString &text,
            const QString &toolTip) {
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setFixedSize(30, 28);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setStyleSheet(previewStyle);
        button->raise();
    };
    setupPreviewButton(mPoweredPreviewButton, QStringLiteral("ON"),
                       tr("Preview powered *_on tile variants"));
    setupPreviewButton(mSnowPreviewButton, QStringLiteral("SN"),
                       tr("Preview SnowTile mappings"));
    setupPreviewButton(mJumboPreviewButton, QStringLiteral("J"),
                       tr("Preview deterministic random Jumbo XL/XXL trees"));

    mPoweredPreviewButton->setChecked(false);
    mSnowPreviewButton->setChecked(false);
    mJumboPreviewButton->setChecked(false);

    connect(mPoweredPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        QSettings().setValue(
            QStringLiteral("EnvironmentPreview/Powered"), enabled);
        emit poweredPreviewToggled(enabled);
        viewport()->update();
    });
    connect(mSnowPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        QSettings().setValue(
            QStringLiteral("EnvironmentPreview/Snow"), enabled);
        emit snowPreviewToggled(enabled);
        viewport()->update();
    });
    connect(mJumboPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        QSettings().setValue(
            QStringLiteral("EnvironmentPreview/Jumbo"), enabled);
        emit jumboPreviewToggled(enabled);
        viewport()->update();
    });

    // Environment preview modes belong to the global view-state strip at the
    // bottom of MainWindow. Keep the view-owned controls non-visual so older
    // view/action synchronization remains source-compatible.
    mNightPreviewButton->hide();
    mPoweredPreviewButton->hide();
    mSnowPreviewButton->hide();
    mJumboPreviewButton->hide();

    mRenderDiagnosticsLabel->setObjectName(
                QStringLiteral("RenderDiagnosticsBubble"));
    mRenderDiagnosticsLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    mRenderDiagnosticsLabel->setStyleSheet(QStringLiteral(
        "QLabel#RenderDiagnosticsBubble {"
        " background-color: rgba(18, 23, 29, 225);"
        " color: #f2f5f7; border: 1px solid #5a6978;"
        " border-left: 3px solid #e49a35; border-radius: 6px;"
        " padding: 6px 9px; }"));
    mRenderDiagnosticsLabel->setVisible(mRenderDiagnosticsEnabled);
    mRenderDiagnosticsLabel->raise();
    mDiagnosticsMemoryTimer.start();

#ifndef QT_NO_OPENGL
    if (openGL == PreferenceGL) {
        Preferences *prefs = Preferences::instance();
        setUseOpenGL(prefs->useOpenGL());
        connect(prefs, &Preferences::useOpenGLChanged, this, &BaseGraphicsView::setUseOpenGL);
    } else if (openGL == AlwaysGL) {
        setUseOpenGL(true);
    }
#endif
}

void BaseGraphicsView::drawProjectGridBadge(QPainter *painter, int cellSize) const
{
    if (!painter || (cellSize != 256 && cellSize != 300))
        return;

    const bool legacy = cellSize == 300;
    const QString text = legacy
            ? tr("PROJECT GRID  -  300 x 300  -  LEGACY")
            : tr("PROJECT GRID  -  256 x 256  -  NATIVE");

    painter->save();
    painter->resetTransform();
    painter->setClipping(false);
    painter->setOpacity(1.0);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    QFont badgeFont = font();
    badgeFont.setBold(true);
    badgeFont.setPointSizeF(qMax(8.0, badgeFont.pointSizeF()));
    painter->setFont(badgeFont);

    const QFontMetrics metrics(badgeFont);
    const int horizontalPadding = 11;
    const int verticalPadding = 6;
    const QSize badgeSize(metrics.horizontalAdvance(text) + horizontalPadding * 2,
                          metrics.height() + verticalPadding * 2);
    const QRect badgeRect(
                QPoint(viewport()->width() - badgeSize.width() - 12, 12),
                badgeSize);
    mProjectGridBadgeRect = badgeRect.adjusted(-2, -2, 2, 2);

    QColor background = palette().color(QPalette::Window);
    background.setAlpha(235);
    const QColor accent = legacy ? QColor(220, 140, 40) : QColor(35, 155, 125);

    // Rasterize text before handing it to QOpenGLWidget. Direct glyph drawing
    // after the native VBO pass could reuse a corrupted OpenGL text cache,
    // producing striped or unreadable badge characters.
    QImage badgeImage(badgeSize, QImage::Format_ARGB32_Premultiplied);
    badgeImage.fill(Qt::transparent);
    QPainter badgePainter(&badgeImage);
    badgePainter.setRenderHint(QPainter::Antialiasing, true);
    badgePainter.setRenderHint(QPainter::TextAntialiasing, true);
    badgePainter.setFont(badgeFont);
    badgePainter.setPen(QPen(accent, 2));
    badgePainter.setBrush(background);
    badgePainter.drawRoundedRect(
                badgeImage.rect().adjusted(1, 1, -1, -1), 5, 5);
    badgePainter.setPen(palette().color(QPalette::WindowText));
    badgePainter.drawText(
                badgeImage.rect(), Qt::AlignCenter, text);
    badgePainter.end();
    painter->drawImage(badgeRect.topLeft(), badgeImage);
    painter->restore();
}

void BaseGraphicsView::adjustScale(qreal scale)
{
    setTransform(QTransform::fromScale(scale, scale));
    setRenderHint(QPainter::SmoothPixmapTransform,
                  mZoomable->smoothTransform());

    if (mScene)
        mScene->viewTransformChanged(this);
}

// I put this in BaseGraphicsView so WorldScene could use it, but the OpenGL
// backend chokes on overly-large BMP images.  It uses QCache in the
// QGLTextureCache::insert() method which *deletes the texture* and other
// code doesn't test for that.
void BaseGraphicsView::setUseOpenGL(bool useOpenGL)
{
#ifndef QT_NO_OPENGL
    QWidget *oldViewport = viewport();
    QWidget *newViewport = viewport();
#if PZ_OPENGL_WIDGET
    if (useOpenGL) {
        if (!qobject_cast<QOpenGLWidget*>(viewport())) {
            QSurfaceFormat format = QSurfaceFormat::defaultFormat();
            format.setVersion(3, 3);
            // The legacy WorldEd VBO renderer uses the compatibility
            // profile's default vertex-array object. Forcing a CoreProfile
            // successfully compiled the shaders but rejected every vertex
            // attribute/draw call, leaving only object overlays visible.
            format.setProfile(QSurfaceFormat::CompatibilityProfile);
            format.setDepthBufferSize(0);
            format.setStencilBufferSize(0);
            QOpenGLWidget *openGLWidget = new QOpenGLWidget();
            openGLWidget->setFormat(format);
            qInfo() << "WorldEd CellView requested OpenGL format" << format;
            newViewport = openGLWidget;
        }
    } else {
        if (qobject_cast<QOpenGLWidget*>(viewport())) {
            newViewport = nullptr;
        }
    }
#else
    if (useOpenGL && QGLFormat::hasOpenGL()) {
        if (!qobject_cast<QGLWidget*>(viewport())) {
            QGLFormat format = QGLFormat::defaultFormat();
            format.setDepth(false); // No need for a depth buffer
            format.setSampleBuffers(true); // Enable anti-aliasing
            newViewport = new QGLWidget(format);
        }
    } else {
        if (qobject_cast<QGLWidget*>(viewport()))
            newViewport = 0;
    }
#endif
    if (newViewport != oldViewport) {
        if (mMiniMap) {
            mMiniMap->setVisible(false);
            mMiniMap->setParent(static_cast<QWidget*>(parent()));
        }
        setViewport(newViewport);
        if (mMiniMap) {
            mMiniMap->setParent(this);
            mMiniMap->setVisible(Preferences::instance()->showMiniMap());
            if (scene())
                mMiniMap->sceneRectChanged(scene()->sceneRect());
        }
    }

    QWidget *v = viewport();
    v->setAttribute(Qt::WA_StaticContents);
    v->setMouseTracking(true);
    qInfo() << "WorldEd CellView renderer:"
            << (qobject_cast<QOpenGLWidget*>(v)
                ? QStringLiteral("OpenGL 3.3 compatibility")
                : QStringLiteral("Qt raster (software)"));
#endif
}

void BaseGraphicsView::setRenderDiagnosticsEnabled(bool enabled)
{
    if (mRenderDiagnosticsEnabled == enabled)
        return;
    mRenderDiagnosticsEnabled = enabled;
    mDiagnosticsPreviousFrame.invalidate();
    mDiagnosticsFps = 0.0;
    mDiagnosticsFrameMs = 0.0;
    mRenderDiagnosticsLabel->setVisible(enabled);
    if (enabled) {
        mDiagnosticsMemoryBytes = PortableSettings::currentProcessMemoryBytes();
        mDiagnosticsMemoryTimer.restart();
        mRenderDiagnosticsLabel->raise();
    }
    viewport()->update();
}

void BaseGraphicsView::paintEvent(QPaintEvent *event)
{
    if (!mRenderDiagnosticsEnabled) {
        QGraphicsView::paintEvent(event);
        return;
    }

    ZLevelRenderer::resetRenderedTileCount();
    QElapsedTimer renderTimer;
    renderTimer.start();
    QGraphicsView::paintEvent(event);

    const qreal renderMs = qMax<qreal>(
                0.01, renderTimer.nsecsElapsed() / 1000000.0);
    mDiagnosticsFrameMs = mDiagnosticsFrameMs <= 0.0
            ? renderMs
            : mDiagnosticsFrameMs * 0.75 + renderMs * 0.25;

    if (mDiagnosticsPreviousFrame.isValid()) {
        const qint64 elapsed = mDiagnosticsPreviousFrame.restart();
        if (elapsed > 0) {
            const qreal currentFps = 1000.0 / elapsed;
            mDiagnosticsFps = mDiagnosticsFps <= 0.0
                    ? currentFps
                    : mDiagnosticsFps * 0.75 + currentFps * 0.25;
        }
    } else {
        mDiagnosticsPreviousFrame.start();
    }

    mDiagnosticsRenderedTiles = ZLevelRenderer::renderedTileCount();
    if (!mDiagnosticsMemoryTimer.isValid() ||
            mDiagnosticsMemoryTimer.elapsed() >= 1000) {
        mDiagnosticsMemoryBytes =
                PortableSettings::currentProcessMemoryBytes();
        mDiagnosticsMemoryTimer.restart();
    }
    updateRenderDiagnosticsLabel();
}

QString BaseGraphicsView::renderDiagnosticsWorkloadText(
        quint64 renderedTiles) const
{
    return tr("Tiles drawn %1").arg(
                QLocale().toString(qulonglong(renderedTiles)));
}

void BaseGraphicsView::updateRenderDiagnosticsLabel()
{
    const bool openGL = qobject_cast<QOpenGLWidget *>(viewport());
    const QString text = tr("FPS %1   Render %2 ms\n"
                            "%3   RAM %4\n"
                            "Zoom %5%   %6   %7 x %8")
            .arg(mDiagnosticsFps, 0, 'f', 1)
            .arg(mDiagnosticsFrameMs, 0, 'f', 2)
            .arg(renderDiagnosticsWorkloadText(mDiagnosticsRenderedTiles))
            .arg(diagnosticMemoryText(mDiagnosticsMemoryBytes))
            .arg(mZoomable->scale() * 100.0, 0, 'f', 0)
            .arg(openGL ? tr("OpenGL") : tr("Raster"))
            .arg(viewport()->width())
            .arg(viewport()->height());
    if (mRenderDiagnosticsLabel->text() != text) {
        mRenderDiagnosticsLabel->setText(text);
        mRenderDiagnosticsLabel->adjustSize();
    }
    positionRenderDiagnosticsLabel();
    mRenderDiagnosticsLabel->raise();
}

void BaseGraphicsView::positionRenderDiagnosticsLabel()
{
    if (!mRenderDiagnosticsLabel || !viewport())
        return;
    const QPoint viewportOrigin = viewport()->mapTo(this, QPoint(0, 0));
    const int x = viewportOrigin.x() + 10;
    const int y = qMax(viewportOrigin.y() + 10,
                       viewportOrigin.y() + viewport()->height()
                       - mRenderDiagnosticsLabel->height() - 10);
    mRenderDiagnosticsLabel->move(x, y);
}

void BaseGraphicsView::setNightPreviewEnabled(bool enabled)
{
    if (mNightPreviewButton->isChecked() == enabled)
        return;
    const QSignalBlocker blocker(mNightPreviewButton);
    mNightPreviewButton->setChecked(enabled);
    mNightPreviewButton->setText(enabled
            ? QString::fromUtf8("\xE2\x98\xBE")
            : QString::fromUtf8("\xE2\x98\x80"));
    mNightPreviewButton->setToolTip(enabled
            ? tr("Switch to day preview")
            : tr("Switch to night preview"));
}

void BaseGraphicsView::setPoweredPreviewEnabled(bool enabled)
{
    const QSignalBlocker blocker(mPoweredPreviewButton);
    mPoweredPreviewButton->setChecked(enabled);
}

void BaseGraphicsView::setSnowPreviewEnabled(bool enabled)
{
    const QSignalBlocker blocker(mSnowPreviewButton);
    mSnowPreviewButton->setChecked(enabled);
}

void BaseGraphicsView::setJumboPreviewEnabled(bool enabled)
{
    const QSignalBlocker blocker(mJumboPreviewButton);
    mJumboPreviewButton->setChecked(enabled);
}

void BaseGraphicsView::autoScrollTimeout()
{
    if(mScrollDirection & ScrollLeft) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()-mScrollMagnitude);
    }
    if(mScrollDirection & ScrollRight) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()+mScrollMagnitude);
    }
    if(mScrollDirection & ScrollUp) {
        verticalScrollBar()->setValue(verticalScrollBar()->value()-mScrollMagnitude);
    }
    if(mScrollDirection & ScrollDown) {
        verticalScrollBar()->setValue(verticalScrollBar()->value()+mScrollMagnitude);
    }
#if 0
    if(rubberBand->isVisible()) { // update the rubber band
        QPoint mouseDownView = mapFromScene(mouseDownPos);
        QPoint diff = (lastMouseViewPos-mouseDownView);
        rubberBand->setGeometry(qMin(lastMouseViewPos.x(), mouseDownView.x()), qMin(lastMouseViewPos.y(), mouseDownView.y()), qAbs(diff.x()), qAbs(diff.y()));
    }
#endif
}

void BaseGraphicsView::startAutoScroll()
{
    Q_ASSERT(mMousePressed);
    if (!mScrollTimer.isActive())
        mScrollTimer.start();
}

void BaseGraphicsView::stopAutoScroll()
{
    if (mScrollTimer.isActive())
        mScrollTimer.stop();
}

void BaseGraphicsView::setHandScrolling(bool handScrolling)
{
    if (mHandScrolling == handScrolling)
        return;

    mHandScrolling = handScrolling;
    setInteractive(!mHandScrolling);

    if (mHandScrolling) {
        mLastMouseGlobalPos = QCursor::pos();
        // FIXME: the cursor changes to arrow during drag
        QApplication::setOverrideCursor(QCursor(Qt::ClosedHandCursor));
        viewport()->grabMouse();
    } else {
        viewport()->releaseMouse();
        QApplication::restoreOverrideCursor();
    }
}

void BaseGraphicsView::hideEvent(QHideEvent *event)
{
    // Disable hand scrolling when the view gets hidden in any way
    setHandScrolling(false);
    QGraphicsView::hideEvent(event);
}

void BaseGraphicsView::wheelEvent(QWheelEvent *event)
{
    QPoint numDegrees = event->angleDelta() / 8;
    if ((event->modifiers() & Qt::ControlModifier) && (numDegrees.y() != 0))
    {
        QPoint numSteps = numDegrees / 15;

        // No automatic anchoring since we'll do it manually
        setTransformationAnchor(QGraphicsView::NoAnchor);

        mZoomable->handleWheelDelta(numSteps.y() * 120);

        // Place the last known mouse scene pos below the mouse again
        QWidget *view = viewport();
        QPointF viewCenterScenePos = mapToScene(view->rect().center());
        QPointF mouseScenePos = mapToScene(view->mapFromGlobal(mLastMouseGlobalPos));
        QPointF diff = viewCenterScenePos - mouseScenePos;
        centerOn(mLastMouseScenePos + diff);

        // Restore the centering anchor
        setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        return;
    }

    QGraphicsView::wheelEvent(event);
}

void BaseGraphicsView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setHandScrolling(true);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        mMousePressed = true;
    }

    mScene->setEventView(this);
    QGraphicsView::mousePressEvent(event);
}

void BaseGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    if (mHandScrolling) {
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
        const QPoint d = event->globalPos() - mLastMouseGlobalPos;
        hBar->setValue(hBar->value() + (isRightToLeft() ? d.x() : -d.x()));
        vBar->setValue(vBar->value() - d.y());

        mLastMouseGlobalPos = event->globalPos();
        return;
    }

    // If the Progress dialog pops up during the mousePressEvent it seems
    // the mouseReleaseEvent never gets sent.  This happened with the PasteCellsTool.
    if (mMousePressed && !event->buttons()) {
        qDebug() << "BaseGraphicsView::mouseMoveEvent un-pressing mouse";
        mMousePressed = false;
        stopAutoScroll();
    }

    if (mMousePressed) {
        QPoint pos = event->pos();
        int distance = 0;
        mScrollDirection = ScrollNone;
        mScrollMagnitude = 0;
#define SCROLL_DISTANCE 64
        // determine the direction of automatic scrolling
        if (pos.x() < SCROLL_DISTANCE) {
            mScrollDirection = ScrollLeft;
            distance = pos.x();
        }
        else if (width() - pos.x() < SCROLL_DISTANCE) {
            mScrollDirection = ScrollRight;
            distance = width()-pos.x();
        }
        if (pos.y() < SCROLL_DISTANCE) {
            mScrollDirection += ScrollUp;
            distance = pos.y();
        }
        else if(height() - pos.y() < SCROLL_DISTANCE) {
            mScrollDirection += ScrollDown;
            distance = height()-pos.y();
        }
        if(mScrollDirection) {
            mScrollMagnitude = qRound(float(SCROLL_DISTANCE-distance)/8);
            startAutoScroll();
        } else
            stopAutoScroll();
    }

    mScene->setEventView(this);
    QGraphicsView::mouseMoveEvent(event);
    mLastMouseGlobalPos = event->globalPos();
    mLastMouseScenePos = mapToScene(viewport()->mapFromGlobal(mLastMouseGlobalPos));
}

void BaseGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setHandScrolling(false);
        return;
    }

    mMousePressed = false;
    stopAutoScroll();

    mScene->setEventView(this);
    QGraphicsView::mouseReleaseEvent(event);
}

void BaseGraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    mMiniMap->viewRectChanged();
    const int right = qMax(6, viewport()->width() - 14);
    int x = right - mNightPreviewButton->width();
    mNightPreviewButton->move(x, 52);
    x -= mJumboPreviewButton->width() + 4;
    mJumboPreviewButton->move(x, 54);
    x -= mSnowPreviewButton->width() + 4;
    mSnowPreviewButton->move(x, 54);
    x -= mPoweredPreviewButton->width() + 4;
    mPoweredPreviewButton->move(x, 54);
    mNightPreviewButton->raise();
    mPoweredPreviewButton->raise();
    mSnowPreviewButton->raise();
    mJumboPreviewButton->raise();
    positionRenderDiagnosticsLabel();
    mRenderDiagnosticsLabel->raise();
}

void BaseGraphicsView::setScene(BaseGraphicsScene *scene)
{
    mScene = scene;
    QGraphicsView::setScene(scene);

    mMiniMap->setScene(scene);
}

void BaseGraphicsView::scrollContentsBy(int dx, int dy)
{
    QRegion badgeDirtyRegion;
    if (!mProjectGridBadgeRect.isEmpty()) {
        badgeDirtyRegion += mProjectGridBadgeRect;
        badgeDirtyRegion += mProjectGridBadgeRect.translated(dx, dy);
    }

    QGraphicsView::scrollContentsBy(dx, dy);
    if (!badgeDirtyRegion.isEmpty())
        viewport()->update(badgeDirtyRegion);
    mMiniMap->viewRectChanged();
}

void BaseGraphicsView::addMiniMapItem(QGraphicsItem *item)
{
    mMiniMap->addItem(item);
}

void BaseGraphicsView::removeMiniMapItem(QGraphicsItem *item)
{
    mMiniMap->removeItem(item);
}

QRectF BaseGraphicsView::sceneRectForMiniMap() const
{
    return mScene->sceneRect();
}

// Wrapper around QGraphicsView::ensureVisible.  In ensureVisible, when the rectangle to
// view is larger than the viewport, the final position is often undesirable with multiple
// scrolls taking place.  In this implementation, when the rectangle to view (plus margins)
// does not fit in the current view or is not partially visible already, the view is centered
// on the rectangle's center.
void BaseGraphicsView::ensureRectVisible(const QRectF &rect, int xmargin, int ymargin)
{
    QRect rectToView = mapFromScene(rect).boundingRect();
    rectToView.adjust(-xmargin, -ymargin, xmargin, ymargin);
    QRect viewportRect = viewport()->rect(); // includes scrollbars?
    if (viewportRect.contains(rectToView, true))
        return;
    if (rectToView.width() > viewportRect.width() ||
            rectToView.height() > viewportRect.height() ||
            !viewportRect.intersects(rectToView)) {
        centerOn(rect.center());
    } else {
        ensureVisible(rect, xmargin, ymargin);
    }
}

/////

#include <QGraphicsPolygonItem>
#include <QHBoxLayout>
#include <QToolButton>
#include <cmath>

MiniMap::MiniMap(BaseGraphicsView *parent)
    : QGraphicsView(parent)
    , mParentView(parent)
    , mViewportItem(0)
    , mButtons(new QFrame(this))
    , mBiggerButton(new QToolButton(mButtons))
    , mSmallerButton(new QToolButton(mButtons))
{
    setFrameStyle(NoFrame);
    setMinimumWidth(20);
    setMinimumHeight(20);

    // For the smaller/bigger buttons
    setMouseTracking(true);

    Preferences *prefs = Preferences::instance();
    setVisible(prefs->showMiniMap());
    mWidth = prefs->miniMapWidth();
    connect(prefs, &Preferences::showMiniMapChanged, this, &QWidget::setVisible);
    connect(prefs, &Preferences::miniMapWidthChanged, this, &MiniMap::widthChanged);

    QGraphicsScene *scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(Qt::gray);
    QGraphicsView::setScene(scene);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    mViewportItem = new QGraphicsPolygonItem();
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    mViewportItem->setPen(pen);
    mViewportItem->setZValue(100);
    scene->addItem(mViewportItem);

    QHBoxLayout *layout = new QHBoxLayout(mButtons);
    layout->setContentsMargins(2, 2, 0, 0);
    layout->setSpacing(2);
    mButtons->setLayout(layout);
    mButtons->setVisible(false);

    QToolButton *button = mSmallerButton;
    button->setAutoRaise(true);
    button->setAutoRepeat(true);
    button->setIconSize(QSize(16, 16));
    button->setIcon(QIcon(QLatin1String(":/images/16x16/zoom-out.png")));
    button->setToolTip(tr("Make the MiniMap smaller"));
    connect(button, &QAbstractButton::clicked, this, &MiniMap::smaller);
    layout->addWidget(button);

    button = mBiggerButton;
    button->setAutoRaise(true);
    button->setAutoRepeat(true);
    button->setIconSize(QSize(16, 16));
    button->setIcon(QIcon(QLatin1String(":/images/16x16/zoom-in.png")));
    button->setToolTip(tr("Make the MiniMap larger"));
    connect(button, &QAbstractButton::clicked, this, &MiniMap::bigger);
    layout->addWidget(button);

#if 0
    button = new QToolButton(mButtons);
    button->setAutoRaise(true);
    button->setAutoRepeat(true);
    button->setIconSize(QSize(16, 16));
    button->setIcon(QIcon(QLatin1String(":/images/16x16/edit-redo.png")));
    button->setToolTip(tr("Refresh the MiniMap image"));
    connect(button, SIGNAL(clicked()), SLOT(updateImage()));
    layout->addWidget(button);
#endif

    setGeometry(20, 20, 220, 220);

    // When visible, the MiniMap obscures part of the scene, slowing down scrolling. :-{
//    setVisible(false);
}

void MiniMap::setScene(BaseGraphicsScene *scene)
{
    mScene = scene;
    widthChanged(mWidth);
    connect(mScene, &QGraphicsScene::sceneRectChanged, this, &MiniMap::sceneRectChanged);
}

void MiniMap::viewRectChanged()
{
    QRect rect = mParentView->rect();

    int hsbh = mParentView->horizontalScrollBar()->isVisible() ? mParentView->horizontalScrollBar()->height() : 0;
    int vsbw = mParentView->verticalScrollBar()->isVisible() ? mParentView->verticalScrollBar()->width() : 0;
    rect.adjust(0, 0, -vsbw, -hsbh);

    QPolygonF polygon = mParentView->mapToScene(rect);
    mViewportItem->setPolygon(polygon);
    mViewportItem->setScale(scale());
}

void MiniMap::addItem(QGraphicsItem *item)
{
    item->setScale(scale());
    scene()->addItem(item);
    mExtraItems += item;
}

void MiniMap::removeItem(QGraphicsItem *item)
{
    if (mExtraItems.contains(item)) {
        mExtraItems.removeAll(item);
        scene()->removeItem(item);
    }
}

void MiniMap::sceneRectChanged(const QRectF &_sceneRect)
{
    Q_UNUSED(_sceneRect)

    QRectF sceneRect = mParentView->sceneRectForMiniMap();

    qreal scale = this->scale();
    QSizeF size = sceneRect.size();
    // No idea where the extra 3 pixels is coming from...
    setGeometry(20, 20, std::ceil(size.width() * scale) + 3, std::ceil(size.height() * scale) + 3);

    // The sceneRect may not start at 0,0.
    scene()->setSceneRect(QRectF(sceneRect.topLeft() * scale, sceneRect.size() * scale));

    viewRectChanged();

    foreach (QGraphicsItem *item, mExtraItems)
        item->setScale(scale);
}

void MiniMap::bigger()
{
    Preferences::instance()->setMiniMapWidth(qMin(mWidth + 32, MINIMAP_WIDTH_MAX));
}

void MiniMap::smaller()
{
    Preferences::instance()->setMiniMapWidth(qMax(mWidth - 32, MINIMAP_WIDTH_MIN));
}

void MiniMap::widthChanged(int width)
{
    mWidth = width;
    sceneRectChanged(mScene->sceneRect());

    mSmallerButton->setEnabled(mWidth > MINIMAP_WIDTH_MIN);
    mBiggerButton->setEnabled(mWidth < MINIMAP_WIDTH_MAX);
}

bool MiniMap::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::Enter:
        break;
    case QEvent::Leave:
        mButtons->setVisible(false);
        break;
    default:
        break;
    }

    return QGraphicsView::event(event);
}

void MiniMap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        mParentView->centerOn(mapToScene(event->pos()) / scale());
}

void MiniMap::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        mParentView->centerOn(mapToScene(event->pos()) / scale());
    else {
        QRect hotSpot = mButtons->rect().adjusted(0, 0, 12, 12); //(0, 0, 64, 32);
        mButtons->setVisible(hotSpot.contains(event->pos()));
    }
}

void MiniMap::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

qreal MiniMap::scale()
{
    QRectF sceneRect = mParentView->sceneRectForMiniMap();
    QSizeF size = sceneRect.size();
    if (size.isEmpty())
        return 1.0;
    return (size.width() > size.height()) ? mWidth / size.width() : mWidth / size.height();
}
