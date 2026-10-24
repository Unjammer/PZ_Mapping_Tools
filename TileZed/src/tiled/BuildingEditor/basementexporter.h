#ifndef BASEMENTEXPORTER_H
#define BASEMENTEXPORTER_H

#include <QList>
#include <QSet>
#include <QString>

namespace Tiled {
namespace Internal {
class TileDefFile;
}
}

namespace BuildingEditor {

struct BasementStairTiles
{
    QSet<QString> north;
    QSet<QString> west;
};

struct BasementExportRequest
{
    QString sourceFile;
    QString outputDirectory;
    BasementStairTiles stairTiles;
};

struct BasementExportResult
{
    QString outputFile;
    QString luaCode;
    QString error;
    bool written = false;
};

class BasementExporter
{
public:
    static BasementStairTiles staircaseTiles(
            const QList<Tiled::Internal::TileDefFile *> &definitions);
    static BasementExportResult exportFile(const BasementExportRequest &request);
};

}

#endif
