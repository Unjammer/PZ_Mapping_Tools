/*
 * Copyright 2026, Alree / Unjammer
 *
 * This file is part of PZ Mapping Tools.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef CHUNKDATAOVERRIDE_H
#define CHUNKDATAOVERRIDE_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QString>

namespace ChunkDataOverride
{
enum Flag
{
    Solid = 1,
    WallNorth = 2,
    WallWest = 4,
    Water = 8,
    Room = 16,
    SupportedMask = Solid | WallNorth | WallWest | Water | Room
};

enum
{
    ImageSize = 256
};

QString defaultFilePath(const QString &projectFilePath,
                        const QPoint &displayCellPosition);
bool validateImage(const QImage &image, QString *error);
bool loadImage(const QString &filePath, QImage *image, QString *error);
bool saveImage(const QString &filePath, const QImage &image, QString *error);
bool hasExplicitPixels(const QImage &image);
quint8 mergedBits(const QImage &image, int x, int y, quint8 generatedBits);
QString valueDescription(quint8 value);
QColor valueColor(quint8 value);
bool validateWorkflow(QString *summary, QString *error);
}

#endif
