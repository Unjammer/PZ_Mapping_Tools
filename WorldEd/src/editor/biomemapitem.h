#ifndef BIOMEMAPITEM_H
#define BIOMEMAPITEM_H

#include <QGraphicsItem>
#include <QImage>

class WorldScene;

/**
 * Displays and edits the red (Biome) channel of maps/biome.png.
 *
 * The green zone channel and all other bytes are preserved. Saving also
 * refreshes the corresponding biomemap_X_Y.png files used by the game.
 */
class BiomeMapItem : public QGraphicsItem
{
public:
    explicit BiomeMapItem(WorldScene *scene);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override;

    bool isValid() const { return !mSourceImage.isNull(); }
    bool canEdit() const;
    const QImage &sourceImage() const { return mSourceImage; }
    int pixelsPerCell() const { return mPixelsPerCell; }
    QString filePath() const { return mFilePath; }

    QPoint imagePointAt(const QPointF &scenePos) const;
    bool containsImagePoint(const QPoint &point) const;
    int biomeValueAt(const QPoint &point) const;

    bool ensureEditable(QString *error = nullptr);
    bool reloadFromSettings(bool force = false, QString *error = nullptr);
    void paintStroke(const QPoint &from, const QPoint &to,
                     int radius, int biomeValue);
    bool replaceSourceImage(const QImage &image, bool saveToDisk,
                            QString *error = nullptr);
    bool save(QString *error = nullptr);

private:
    QString mapsDirectoryPath() const;
    QString configuredFilePath() const;
    QSize expectedImageSize() const;
    bool loadTiles(QImage *image) const;
    bool savePngAtomically(const QImage &image, const QString &filePath,
                           QString *error) const;
    bool saveTiles(QString *error) const;
    void rebuildPreview();
    void synchWithImage();
    QRectF imageBounds() const;
    QPolygonF polygon() const;

    WorldScene *mScene;
    QString mFilePath;
    QImage mSourceImage;
    QImage mPreviewImage;
    QRectF mMapImageBounds;
    int mPixelsPerCell;
};

#endif // BIOMEMAPITEM_H
