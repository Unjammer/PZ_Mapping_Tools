/*
 * Project Zomboid mapping-tools day/night preview overlay.
 */

#include "nightpreviewitem.h"

#include <QPainter>
#include <QRadialGradient>
#include <QSettings>
#include <QStyleOptionGraphicsItem>

using namespace Tiled::Internal;

NightPreviewItem::NightPreviewItem(QGraphicsItem *parent)
    : QGraphicsItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
}

QRectF NightPreviewItem::boundingRect() const
{
    return mBounds;
}

void NightPreviewItem::setBounds(const QRectF &bounds)
{
    if (bounds == mBounds)
        return;
    prepareGeometryChange();
    mBounds = bounds;
}

void NightPreviewItem::setDarkness(qreal darkness)
{
    mDarkness = qBound<qreal>(0.0, darkness, 0.92);
    update();
}

void NightPreviewItem::setLights(const QVector<NightPreviewLight> &lights)
{
    mLights = lights;
    update();
}

void NightPreviewItem::setLitRooms(const QVector<QPolygonF> &rooms)
{
    mLitRooms = rooms;
    update();
}

void NightPreviewItem::paint(
        QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    painter->save();
    const QSettings settings;
    const qreal darkness = qBound<qreal>(
                0.15,
                settings.value(QStringLiteral("NightPreview/Darkness"),
                               mDarkness).toReal(),
                0.92);
    const qreal lightIntensity = qBound<qreal>(
                0.05,
                settings.value(QStringLiteral(
                                   "NightPreview/LightIntensity"),
                               0.65).toReal(),
                1.0);
    painter->setPen(Qt::NoPen);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter->fillRect(option->exposedRect,
                      QColor(5, 9, 22, qRound(darkness * 255.0)));

    painter->setBrush(QColor(255, 225, 165,
                              qRound(42 * lightIntensity)));
    for (const QPolygonF &room : mLitRooms)
        painter->drawPolygon(room);

    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const NightPreviewLight &light : mLights) {
        if (light.radiusX <= 0.0 || light.radiusY <= 0.0)
            continue;
        painter->save();
        painter->translate(light.center);
        painter->scale(1.0, light.radiusY / light.radiusX);
        QColor centerColor = light.color;
        centerColor.setAlpha(qRound(150 * lightIntensity));
        QColor edgeColor = light.color;
        edgeColor.setAlpha(0);
        QRadialGradient gradient(QPointF(0, 0), light.radiusX);
        gradient.setColorAt(0.0, centerColor);
        gradient.setColorAt(0.35, QColor(centerColor.red(),
                                         centerColor.green(),
                                         centerColor.blue(),
                                qRound(78 * lightIntensity)));
        gradient.setColorAt(1.0, edgeColor);
        painter->setBrush(gradient);
        painter->drawEllipse(QPointF(0, 0),
                             light.radiusX, light.radiusX);
        painter->restore();
    }
    painter->restore();
}
