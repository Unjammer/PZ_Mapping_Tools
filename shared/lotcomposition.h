#ifndef LOTCOMPOSITION_H
#define LOTCOMPOSITION_H

#include <QString>

namespace PZTools {

inline bool lotLayerPreservesVegetation(const QString &name)
{
    return name.startsWith(QLatin1String("Walls")) ||
            name.startsWith(QLatin1String("WallTrim")) ||
            name.startsWith(QLatin1String("WallOverlay")) ||
            name.startsWith(QLatin1String("WallGrime")) ||
            name == QLatin1String("Frames") ||
            name == QLatin1String("Doors") ||
            name == QLatin1String("Windows");
}

inline bool lotLayerSuppressesLayer(const QString &replacement, const QString &existing)
{
    return replacement == QLatin1String("Floor") || replacement == existing ||
            (existing.startsWith(QLatin1String("Vegetation")) &&
             !lotLayerPreservesVegetation(replacement));
}

}

#endif
