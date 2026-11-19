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

#ifndef BUILDINGTILETOOLS_H
#define BUILDINGTILETOOLS_H

#include "buildingtools.h"

#include <QBrush>
#include <QColor>
#include <QGraphicsPolygonItem>
#include <QList>
#include <QPair>
#include <QPen>
#include <QRectF>
#include <QRegion>

class QAction;
class QGraphicsSceneMouseEvent;
class QUndoStack;

namespace BuildingEditor {

class BuildingBaseScene;
class BuildingDocument;
class BuildingFloor;
class BuildingIsoScene;
class FloorTileGrid;

/////

class DrawTileToolCursor : public QGraphicsItem
{
public:
    DrawTileToolCursor(BuildingBaseScene *editor, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget);

    void setColor(const QColor &color);

    void setTileRegion(const QRegion &tileRgn);

    void setRoomRegions(const QList<QPair<QRegion, QColor> > &regions);

    void setEditor(BuildingBaseScene *editor);

private:
    BuildingBaseScene *mEditor;
    QRegion mRegion;
    QList<QPair<QRegion, QColor> > mRoomRegions;
    QRectF mBoundingRect;
    QColor mColor;
};

class DrawTileTool : public BaseTool
{
    Q_OBJECT
public:
    static DrawTileTool *instance();

    DrawTileTool();

    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

    void currentModifiersChanged(Qt::KeyboardModifiers modifiers);

    void setTile(const QString &tileName);

    QString currentTile() const
    { return mTileName; }
    QString singleTileBrush() const;

    void setCaptureTiles(FloorTileGrid *tiles, const QRegion &rgn);
    void setClipboardPlacement();

signals:
    void brushChanged();
    void tilesPainted();

public slots:
    void activate();
    void deactivate();

private:
    void beginCapture();
    void endCapture();
    void clearCaptureTiles();
    void clearClipboardPreview();
    bool rebuildClipboardPreview();

    void updateCursor(const QPointF &scenePos, bool force = true);
    void updateStatusText();

private:
    Q_DISABLE_COPY(DrawTileTool)
    static DrawTileTool *mInstance;
    ~DrawTileTool();

    bool mMouseDown;
    bool mMouseMoved;
    bool mErasing;
    QPointF mMouseScenePos;
    QPointF mStartScenePos;
    QPoint mStartTilePos;
    QPoint mCursorTilePos;
    QRect mCursorTileBounds;
    DrawTileToolCursor *mCursor;
    bool mCapturing;
    bool mClipboardPlacement;
    FloorTileGrid *mCaptureTiles;
    QRegion mCaptureTilesRgn;
    FloorTileGrid *mClipboardPreviewTiles;
    QRegion mClipboardPreviewRegion;
    QList<QPair<QRegion, QColor> > mClipboardPreviewRoomRegions;
    int mClipboardPreviewLevel;

    QString mTileName;
};

class SelectTileTool : public BaseTool
{
    Q_OBJECT
public:
    static SelectTileTool *instance();

    SelectTileTool();

    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

    void currentModifiersChanged(Qt::KeyboardModifiers modifiers);

public slots:
    void activate();
    void deactivate();

private:
    void updateCursor(const QPointF &scenePos, bool force = true);
    void updateStatusText();

private:
    Q_DISABLE_COPY(SelectTileTool)
    static SelectTileTool *mInstance;
    ~SelectTileTool() { mInstance = nullptr; }

    enum SelectionMode {
        Replace,
        Add,
        Subtract,
        Intersect
    };

    SelectionMode mSelectionMode;
    bool mMouseDown;
    bool mMouseMoved;
    QPointF mMouseScenePos;
    QPointF mStartScenePos;
    QPoint mStartTilePos;
    QPoint mCursorTilePos;
    QRect mCursorTileBounds;
    DrawTileToolCursor *mCursor;
    QRegion mSelectedRegion;
};

class PickTileTool : public BaseTool
{
    Q_OBJECT
public:
    static PickTileTool *instance();

    PickTileTool();
    void setInspectorAction(QAction *action) { mInspectorAction = action; }

    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

signals:
    void tilePicked(const QString &tileName);
    void inspectSquare(const QPoint &position);

public slots:
    void activate() {}
    void deactivate() {}

private:
    Q_DISABLE_COPY(PickTileTool)
    QAction *mInspectorAction = nullptr;
    static PickTileTool *mInstance;
    ~PickTileTool() { mInstance = nullptr; }

};

class FloorGrimeTileTool : public BaseTool
{
    Q_OBJECT
public:
    static FloorGrimeTileTool *instance();

    FloorGrimeTileTool();

    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

    void currentModifiersChanged(Qt::KeyboardModifiers modifiers);

    void registerMaterial(const QString &name, const QStringList &tiles);

    QString currentTile() const
    { return mTileName; }

public slots:
    void activate();
    void deactivate();

private:
    void updateCursor(const QPointF &scenePos, bool force = true);
    int pickGrimeEnum(const QPointF &scenePos);
    void updateMaterialTile();
    void cycleGrime();
    void updateStatusText();

private:
    Q_DISABLE_COPY(FloorGrimeTileTool)
    static FloorGrimeTileTool *mInstance;
    ~FloorGrimeTileTool() { mInstance = nullptr; }

    bool mMouseDown;
    bool mMouseMoved;
    bool mErasing;
    Qt::KeyboardModifiers mModifiers;
    bool mRotating;
    int mFloorGrimeEntry = 0;
    int mCustomEntry = -1;
    int mFloorGrime = 0;
    QStringList mMaterialNames;
    QVector<QStringList> mMaterialTiles;
    QPointF mMouseScenePos;
    QPointF mStartScenePos;
    QPoint mStartTilePos;
    QPoint mCursorTilePos;
    QRect mCursorTileBounds;
    DrawTileToolCursor *mCursor;

    QString mTileName;
};

} // namespace BuildingEditor

#endif // BUILDINGTILETOOLS_H
