#ifndef TILESETZ_H
#define TILESETZ_H

#include <QColor>
#include <QImage>
#include <QStringList>
#include <QVector>

namespace TileSetZ {

enum Preset { Walls, Floors, Curbs, RoofWalls, Roofs, CustomWalls, ExternalMask };
enum Projection { Texture, LeftWall, RightWall, Corner, Floor };

struct WallPiece {
    int id;
    int x;
    int y;
    int level;
    QPoint screen;
};

struct WallResult {
    QImage sheet;
    QImage assembled;
    QVector<WallPiece> pieces;
    QSize sourceGrid;
};

QSize wallFaceSize(int scale);
WallResult wall(const QImage &source, int scale, bool left, int columns, QString *error);
QByteArray wallMap(const WallResult &wall, int scale, const QString &sheetName, const QString &imagePath);

QSize tileSize(int scale);
bool validSize(const QSize &size);
QImage readImage(const QString &path, QString *error);
bool saveImage(const QImage &image, const QString &path, QString *error);
QImage blank(const QSize &size);
QImage project(const QImage &texture, Projection projection);
QImage cut(const QImage &source, int scale, int columns, QSize *layout = nullptr);
QImage masked(const QImage &texture, const QImage &mask, bool alphaMask);
QImage generate(const QImage &source, Preset preset, int scale,
                const QColor &trim, const QImage &externalMask,
                Projection projection, bool alphaMask, QString *error);
QImage insert(const QImage &sheet, const QImage &tiles, int scale,
              int firstId, QString *error);
QRect tileRect(const QImage &sheet, int scale, int id);
bool hasPixels(const QImage &image);
QImage clean(const QImage &image);
QStringList maskFiles();

}

#endif
