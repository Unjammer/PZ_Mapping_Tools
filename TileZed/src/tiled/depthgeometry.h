/*
 * Build 42 tileGeometry.txt support and geometry-to-depth rasterizer.
 *
 * The projection and depth normalization mirror
 * zombie.tileDepth.TileGeometryUtils.
 */

#ifndef DEPTHGEOMETRY_H
#define DEPTHGEOMETRY_H

#include <QImage>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QVector3D>

enum class DepthPrimitiveType {
    Box,
    Cylinder,
    Polygon
};

enum class DepthPolygonPlane {
    XY,
    XZ,
    YZ
};

struct DepthPrimitive
{
    DepthPrimitiveType type = DepthPrimitiveType::Box;
    QVector3D translate;
    QVector3D rotate;

    QVector3D minimum = QVector3D(-0.5f, 0.0f, -0.5f);
    QVector3D maximum = QVector3D(0.5f, 2.44949f, 0.5f);

    float radius1 = 0.5f;
    float radius2 = 0.5f;
    float height = 1.0f;

    DepthPolygonPlane plane = DepthPolygonPlane::XZ;
    QVector<QPointF> points;

    static DepthPrimitive makeBox();
    static DepthPrimitive makeCylinder();
    static DepthPrimitive makePolygon(DepthPolygonPlane plane);
    QString displayName(int index) const;
};

struct DepthGeometryTile
{
    QVector<DepthPrimitive> primitives;
    QString propertiesBlock;
};

class DepthGeometryDocument
{
public:
    bool load(const QString &filePath, const QString &tilesetName,
              QString *error);
    bool save(const QString &filePath, const QString &tilesetName,
              QString *error);

    QMap<int, DepthGeometryTile> &tiles() { return mTiles; }
    const QMap<int, DepthGeometryTile> &tiles() const { return mTiles; }
    QString sourceText() const { return mSourceText; }

private:
    QString buildTilesetBlock(const QString &tilesetName) const;

    QString mSourceText;
    QMap<int, DepthGeometryTile> mTiles;
};

class DepthGeometryRasterizer
{
public:
    static float depthAt(const DepthPrimitive &primitive,
                         float pixelX, float pixelY);
    static QImage rasterize(const QVector<DepthPrimitive> &primitives,
                            const QImage &sourceTile,
                            bool respectSourceAlpha,
                            const QImage &base = QImage());
    static QImage rasterize(const DepthPrimitive &primitive,
                            const QImage &sourceTile,
                            bool respectSourceAlpha,
                            const QImage &base = QImage());

    static QPointF project(const QVector3D &scenePoint);
    static QVector<QLineF> wireframe(const DepthPrimitive &primitive);
};

#endif // DEPTHGEOMETRY_H
