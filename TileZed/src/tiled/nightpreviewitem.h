/*
 * Project Zomboid mapping-tools day/night preview overlay.
 */

#ifndef NIGHTPREVIEWITEM_H
#define NIGHTPREVIEWITEM_H

#include <QColor>
#include <QGraphicsItem>
#include <QPolygonF>
#include <QVector>

struct NightPreviewLight
{
    QPointF center;
    QColor color;
    qreal radiusX = 1.0;
    qreal radiusY = 1.0;
};

namespace Tiled {
namespace Internal {

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

} // namespace Internal
} // namespace Tiled

#endif // NIGHTPREVIEWITEM_H
