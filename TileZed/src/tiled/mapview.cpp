/*
 * mapview.cpp
 * Copyright 2008-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
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

#include "mapview.h"

#include "mapscene.h"
#include "preferences.h"
#include "zoomable.h"
#ifdef ZOMBOID
#include "mainwindow.h"
#include "map.h"
#include "mapdocument.h"
#include "maprenderer.h"
#include "tilelayerspanel.h"
#include "zlevelrenderer.h"
#include "../portablesettings.h"
#endif

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QSettings>
#include <QWheelEvent>
#include <QScrollBar>

#ifndef QT_NO_OPENGL
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QOpenGLWidget>
#else
#include <QtOpenGLWidgets/QOpenGLWidget>
#endif
#endif

using namespace Tiled::Internal;

#ifdef ZOMBOID
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
#endif

MapView::MapView(QWidget *parent)
    : QGraphicsView(parent)
    , mHandScrolling(false)
    , mZoomable(new Zoomable(this))
#ifdef ZOMBOID
    , mMiniMap(0)
    , mRenderDiagnosticsEnabled(QSettings().value(
          QStringLiteral("RenderDiagnostics/Enabled"), true).toBool())
    , mRenderDiagnosticsLabel(new QLabel(this))
    , mDiagnosticsFps(0.0)
    , mDiagnosticsRenderMs(0.0)
    , mDiagnosticsRenderedTiles(0)
    , mDiagnosticsMemoryBytes(0)
#endif
{
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
#ifdef Q_OS_MACOS
    setFrameStyle(QFrame::NoFrame);
#endif

#ifndef QT_NO_OPENGL
    Preferences *prefs = Preferences::instance();
#if 0
    // The minimap isn't displayed for documents after the first, for some reason, when using OpenGL acceleration.
    // If the OpenGL preference is changed after a document is loaded, the minimap is displayed just fine.
    // This started happening with Qt 5.15, I believe.
    // See currentDocumentChanged().
    setUseOpenGL(prefs->useOpenGL());
#endif
    connect(prefs, &Preferences::useOpenGLChanged, this, &MapView::setUseOpenGL);
#endif

    QWidget *v = viewport();

    /* Since Qt 4.5, setting this attribute yields significant repaint
     * reduction when the view is being resized. */
    v->setAttribute(Qt::WA_StaticContents);

    /* Since Qt 4.6, mouse tracking is disabled when no graphics item uses
     * hover events. We need to set it since our scene wants the events. */
    v->setMouseTracking(true);

    // Adjustment for antialiasing is done by the items that need it
    setOptimizationFlags(QGraphicsView::DontAdjustForAntialiasing);

    connect(mZoomable, &Zoomable::scaleChanged, this, &MapView::adjustScale);

#ifdef ZOMBOID
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
#endif
}

MapView::~MapView()
{
    setHandScrolling(false); // Just in case we didn't get a hide event
}

#ifdef ZOMBOID
#include "documentmanager.h"
#include "ZomboidScene.h"
void MapView::currentDocumentChanged(MapDocument *doc)
{
    if (mFixedMiniMap) {
        return;
    }
    ZomboidScene *scene2 = static_cast<ZomboidScene*>(scene());
    if ((doc == nullptr) || (scene2 == nullptr) || (scene2->mapDocument() != doc)) {
        return;
    }
    mFixedMiniMap = true;
    setUseOpenGL(Preferences::instance()->useOpenGL());
}

void MapView::setMapScene(MapScene *scene)
{
    QGraphicsView::setScene(scene);

    mMiniMap = new MiniMap(this);
    mMiniMap->setMapScene(scene);

    mMiniMapItem = new MiniMapItem(static_cast<ZomboidScene*>(scene));
    mMiniMap->setExtraItem(mMiniMapItem);

    connect(DocumentManager::instance(), &DocumentManager::currentDocumentChanged, this, &MapView::currentDocumentChanged);
}
#endif

MapScene *MapView::mapScene() const
{
    return static_cast<MapScene*>(scene());
}

void MapView::adjustScale(qreal scale)
{
    setTransform(QTransform::fromScale(scale, scale));
    setRenderHint(QPainter::SmoothPixmapTransform,
                  mZoomable->smoothTransform());
}

#ifdef ZOMBOID
void MapView::setUseOpenGL(bool useOpenGL)
{
#ifndef QT_NO_OPENGL
    QWidget *oldViewport = viewport();
    QWidget *newViewport = viewport();
    if (useOpenGL) {
        if (!qobject_cast<QOpenGLWidget*>(viewport())) {
            QSurfaceFormat format = QSurfaceFormat::defaultFormat();
            format.setDepthBufferSize(0); // No need for a depth buffer
            format.setSamples(4); // Enable anti-aliasing
            newViewport = new QOpenGLWidget();
            ((QOpenGLWidget*) newViewport)->setFormat(format);
        }
    } else {
        if (qobject_cast<QOpenGLWidget*>(viewport()))
            newViewport = 0;
    }

    // Changing the viewport destroys its child widgets
    if (newViewport != oldViewport) {
        if (mMiniMap) {
            mMiniMap->setVisible(false);
            mMiniMap->setParent(static_cast<QWidget*>(parent()));
        }
        setViewport(newViewport);
        if (mMiniMap) {
            mMiniMap->setParent(this);
            mMiniMap->setVisible(Preferences::instance()->showMiniMap());
//            mMiniMap->sceneRectChanged(scene()->sceneRect());
        }
    }

    QWidget *v = viewport();
    v->setAttribute(Qt::WA_StaticContents);
    v->setMouseTracking(true);
    qInfo() << "TileZed MapView renderer:"
            << (qobject_cast<QOpenGLWidget *>(v)
                ? QStringLiteral("OpenGL viewport")
                : QStringLiteral("Qt raster viewport"));
#endif
}
#else
void MapView::setUseOpenGL(bool useOpenGL)
{
#ifndef QT_NO_OPENGL
    if (useOpenGL && QGLFormat::hasOpenGL()) {
        if (!qobject_cast<QGLWidget*>(viewport())) {
            QGLFormat format = QGLFormat::defaultFormat();
            format.setDepth(false); // No need for a depth buffer
            format.setSampleBuffers(true); // Enable anti-aliasing
            setViewport(new QGLWidget(format));
        }
    } else {
        if (qobject_cast<QGLWidget*>(viewport()))
            setViewport(0);
    }

    QWidget *v = viewport();
    v->setAttribute(Qt::WA_StaticContents);
    v->setMouseTracking(true);
#endif
}
#endif

void MapView::setHandScrolling(bool handScrolling)
{
    if (mHandScrolling == handScrolling)
        return;
    mHandScrolling = handScrolling;
    setInteractive(!mHandScrolling);

    mapScene()->setHandScrolling(handScrolling);

    if (mHandScrolling) {
        mLastMousePos = QCursor::pos();
        QApplication::setOverrideCursor(QCursor(Qt::ClosedHandCursor));
        viewport()->grabMouse();
    } else {
        viewport()->releaseMouse();
        QApplication::restoreOverrideCursor();
    }
}

bool MapView::event(QEvent *e)
{
    // Ignore space bar events since they're handled by the MainWindow
    if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Space) {
            e->ignore();
            return false;
        }
    }

    return QGraphicsView::event(e);
}

void MapView::hideEvent(QHideEvent *event)
{
    // Disable hand scrolling when the view gets hidden in any way
    setHandScrolling(false);
    QGraphicsView::hideEvent(event);
}

/**
 * Override to support zooming in and out using the mouse wheel.
 */
void MapView::wheelEvent(QWheelEvent *event)
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
        QPointF mouseScenePos = mapToScene(view->mapFromGlobal(mLastMousePos));
        QPointF diff = viewCenterScenePos - mouseScenePos;
        centerOn(mLastMouseScenePos + diff);

        // Restore the centering anchor
        setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        return;
    }

    QGraphicsView::wheelEvent(event);
}

/**
 * Activates hand scrolling when the middle mouse button is pressed.
 */
void MapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setHandScrolling(true);
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

/**
 * Deactivates hand scrolling when the middle mouse button is released.
 */
void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setHandScrolling(false);
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

/**
 * Moves the view with the mouse while hand scrolling.
 */
void MapView::mouseMoveEvent(QMouseEvent *event)
{
    if (mHandScrolling) {
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint d = event->globalPosition().toPoint() - mLastMousePos;
#else
        const QPoint d = event->globalPos() - mLastMousePos;
#endif
        hBar->setValue(hBar->value() + (isRightToLeft() ? d.x() : -d.x()));
        vBar->setValue(vBar->value() - d.y());

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        mLastMousePos = event->globalPosition().toPoint();
#else
        mLastMousePos = event->globalPos();
#endif
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    mLastMousePos = event->globalPosition().toPoint();
#else
    mLastMousePos = event->globalPos();
#endif
    mLastMouseScenePos = mapToScene(viewport()->mapFromGlobal(mLastMousePos));

#ifdef ZOMBOID
    if (!(event->modifiers() & Qt::AltModifier)) {
        MapDocument *doc = mapScene()->mapDocument();
        QPoint tilePos = doc->renderer()->pixelToTileCoordsInt(mLastMouseScenePos,
                                                               doc->currentLevel());
        MainWindow::instance()->tileLayersPanel()->setTilePosition(tilePos);
    }
#endif
}

#ifdef ZOMBOID

void MapView::setRenderDiagnosticsEnabled(bool enabled)
{
    if (mRenderDiagnosticsEnabled == enabled)
        return;
    mRenderDiagnosticsEnabled = enabled;
    mDiagnosticsPreviousFrame.invalidate();
    mDiagnosticsFps = 0.0;
    mDiagnosticsRenderMs = 0.0;
    mRenderDiagnosticsLabel->setVisible(enabled);
    if (enabled) {
        mDiagnosticsMemoryBytes = PortableSettings::currentProcessMemoryBytes();
        mDiagnosticsMemoryTimer.restart();
        mRenderDiagnosticsLabel->raise();
    }
    viewport()->update();
}

void MapView::paintEvent(QPaintEvent *event)
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
    mDiagnosticsRenderMs = mDiagnosticsRenderMs <= 0.0
            ? renderMs
            : mDiagnosticsRenderMs * 0.75 + renderMs * 0.25;

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

void MapView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (mMiniMap)
        mMiniMap->viewRectChanged();
    positionRenderDiagnosticsLabel();
}

void MapView::scrollContentsBy(int dx, int dy)
{
    QRegion badgeDirtyRegion;
    if (!mProjectGridBadgeRect.isEmpty()) {
        badgeDirtyRegion += mProjectGridBadgeRect;
        badgeDirtyRegion += mProjectGridBadgeRect.translated(dx, dy);
    }

    QGraphicsView::scrollContentsBy(dx, dy);
    if (!badgeDirtyRegion.isEmpty())
        viewport()->update(badgeDirtyRegion);
    if (mMiniMap)
        mMiniMap->viewRectChanged();
}

void MapView::drawForeground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawForeground(painter, rect);
    drawProjectGridBadge(painter);
}

void MapView::drawProjectGridBadge(QPainter *painter)
{
    if (!painter || !mapScene() || !mapScene()->mapDocument()
            || !mapScene()->mapDocument()->map()) {
        mProjectGridBadgeRect = QRect();
        return;
    }

    const QSize mapSize = mapScene()->mapDocument()->map()->size();
    if (mapSize != QSize(256, 256) && mapSize != QSize(300, 300)) {
        mProjectGridBadgeRect = QRect();
        return;
    }

    const bool legacy = mapSize == QSize(300, 300);
    const QString text = legacy
            ? tr("PROJECT GRID  ·  300 × 300  ·  LEGACY")
            : tr("PROJECT GRID  ·  256 × 256  ·  NATIVE");

    painter->save();
    painter->resetTransform();
    painter->setClipping(false);
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont badgeFont = font();
    badgeFont.setBold(true);
    badgeFont.setPointSizeF(qMax(8.0, badgeFont.pointSizeF()));
    painter->setFont(badgeFont);

    const QFontMetrics metrics(badgeFont);
    const int horizontalPadding = 11;
    const int verticalPadding = 6;
    const QSize badgeSize(metrics.horizontalAdvance(text) + horizontalPadding * 2,
                          metrics.height() + verticalPadding * 2);
    const QRect badgeRect(QPoint(viewport()->width() - badgeSize.width() - 12, 12),
                          badgeSize);
    mProjectGridBadgeRect = badgeRect.adjusted(-2, -2, 2, 2);

    QColor background = palette().color(QPalette::Window);
    background.setAlpha(235);
    const QColor accent = legacy ? QColor(220, 140, 40) : QColor(35, 155, 125);

    painter->setPen(QPen(accent, 2));
    painter->setBrush(background);
    painter->drawRoundedRect(badgeRect, 5, 5);
    painter->setPen(palette().color(QPalette::WindowText));
    painter->drawText(badgeRect, Qt::AlignCenter, text);
    painter->restore();
}

void MapView::updateRenderDiagnosticsLabel()
{
    bool openGL = false;
#ifndef QT_NO_OPENGL
    openGL = qobject_cast<QOpenGLWidget *>(viewport());
#endif
    const QString text = tr("FPS %1   Render %2 ms\n"
                            "Tiles drawn %3   RAM %4\n"
                            "Zoom %5%   %6   %7 x %8")
            .arg(mDiagnosticsFps, 0, 'f', 1)
            .arg(mDiagnosticsRenderMs, 0, 'f', 2)
            .arg(QLocale().toString(
                     qulonglong(mDiagnosticsRenderedTiles)))
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

void MapView::positionRenderDiagnosticsLabel()
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

#endif // ZOMBOID
