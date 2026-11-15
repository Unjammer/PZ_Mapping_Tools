#ifndef DIRECTMAPPREVIEW_H
#define DIRECTMAPPREVIEW_H

#include <QByteArray>
#include <QJsonArray>
#include <QPoint>
#include <QString>
#include <QVector>

class WorldCell;
class WorldCellLotList;
class WorldDocument;

class DirectMapPreview
{
public:
    static bool read(const QString &projectPath, QVector<QByteArray> &cells, QJsonArray &spawn,
                     QString &error, const QPoint &startCell = QPoint(-1, -1));

private:
    static bool readCell(WorldDocument &document, WorldCell &cell, const QPoint &worldCell,
                         WorldCellLotList &overlappingLots, QByteArray &payload,
                         QJsonArray &authoredSpawn, QJsonArray &fallbackSpawn, QString &error);
};

#endif
