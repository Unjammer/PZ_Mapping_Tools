/*
 * Project Zomboid mapping-tools day/night preview overlay.
 */

#ifndef NIGHTPREVIEWITEM_H
#define NIGHTPREVIEWITEM_H

#include <QColor>
#include <QGraphicsItem>
#include <QImage>
#include <QPolygonF>
#include <QTransform>
#include <QVector>

struct NightPreviewLight
{
    QPointF center;
    QColor color;
    qreal radiusX = 1.0;
    qreal radiusY = 1.0;
};

class NightPreviewItem : public QGraphicsItem
{
public:
    explicit NightPreviewItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setBounds(const QRectF &bounds);
    void setDarkness(qreal darkness);
    void setLights(const QVector<NightPreviewLight> &lights);
    void setLitRooms(const QVector<QPolygonF> &rooms);

private:
    QRectF mBounds;
    qreal mDarkness = 0.68;
    QVector<NightPreviewLight> mLights;
    QVector<QPolygonF> mLitRooms;
};

struct EnvironmentPreviewSprite
{
    QImage image;
    QTransform transform;
};

class EnvironmentPreviewItem : public QGraphicsItem
{
public:
    explicit EnvironmentPreviewItem(QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setBounds(const QRectF &bounds);
    void setSprites(const QVector<EnvironmentPreviewSprite> &sprites);

private:
    QRectF mBounds;
    QVector<EnvironmentPreviewSprite> mSprites;
};

#endif // NIGHTPREVIEWITEM_H
