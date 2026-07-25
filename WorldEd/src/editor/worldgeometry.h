/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef WORLDGEOMETRY_H
#define WORLDGEOMETRY_H

#include <QString>

enum class WorldGridFormat
{
    Legacy300,
    Native256
};

struct WorldGeometry
{
    WorldGridFormat format;
    int cellSize;
    int chunksPerCell;
    int chunkSize;
    bool directLotExport;

    static WorldGeometry forFormat(WorldGridFormat format)
    {
        if (format == WorldGridFormat::Native256)
            return { format, 256, 32, 8, true };
        return { format, 300, 30, 10, false };
    }
};

inline QString worldGridFormatName(WorldGridFormat format)
{
    return format == WorldGridFormat::Native256
            ? QStringLiteral("native-256")
            : QStringLiteral("legacy-300");
}

inline bool worldGridFormatFromName(const QString &name, WorldGridFormat &format)
{
    if (name.isEmpty() || name == QLatin1String("legacy-300")) {
        format = WorldGridFormat::Legacy300;
        return true;
    }
    if (name == QLatin1String("native-256")) {
        format = WorldGridFormat::Native256;
        return true;
    }
    return false;
}

#endif // WORLDGEOMETRY_H
