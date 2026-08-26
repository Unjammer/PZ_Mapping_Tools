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

#include "chunkdataoverride.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>

namespace
{
QString translated(const char *text)
{
    return QCoreApplication::translate("ChunkDataOverride", text);
}
}

namespace ChunkDataOverride
{
QString defaultFilePath(const QString &projectFilePath,
                        const QPoint &displayCellPosition)
{
    const QDir projectDirectory = QFileInfo(projectFilePath).absoluteDir();
    return projectDirectory.filePath(
                QStringLiteral("chunkdata-overrides/chunkdata_%1_%2.png")
                .arg(displayCellPosition.x())
                .arg(displayCellPosition.y()));
}

bool validateImage(const QImage &image, QString *error)
{
    if (image.isNull()) {
        if (error)
            *error = translated("The chunk data override image is empty or unreadable.");
        return false;
    }
    if (image.size() != QSize(ImageSize, ImageSize)) {
        if (error) {
            *error = translated("Chunk data override images must be exactly 256 x 256 pixels. This image is %1 x %2.")
                    .arg(image.width()).arg(image.height());
        }
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha != 0 && alpha != 255) {
                if (error) {
                    *error = translated("Pixel %1,%2 has alpha %3. Alpha must be 0 for automatic generation or 255 for an explicit override.")
                            .arg(x).arg(y).arg(alpha);
                }
                return false;
            }
            if (alpha == 0)
                continue;
            const int value = qRed(pixel);
            if (qGreen(pixel) != 0 || qBlue(pixel) != 0
                    || (value & ~SupportedMask) != 0) {
                if (error) {
                    *error = translated("Pixel %1,%2 is not a supported chunk data value. Opaque pixels must store a value from 0 to 31 in the red channel, with green and blue set to zero.")
                            .arg(x).arg(y);
                }
                return false;
            }
        }
    }
    return true;
}

bool loadImage(const QString &filePath, QImage *image, QString *error)
{
    if (!image) {
        if (error)
            *error = translated("No destination image was supplied.");
        return false;
    }
    QImageReader reader(filePath);
    reader.setAutoTransform(false);
    const QImage loaded = reader.read();
    if (loaded.isNull()) {
        if (error) {
            *error = translated("Could not read chunk data override %1: %2")
                    .arg(QDir::toNativeSeparators(filePath),
                         reader.errorString());
        }
        return false;
    }
    if (!validateImage(loaded, error))
        return false;
    *image = loaded.convertToFormat(QImage::Format_ARGB32);
    return true;
}

bool saveImage(const QString &filePath, const QImage &image, QString *error)
{
    if (!validateImage(image, error))
        return false;
    const QFileInfo fileInfo(filePath);
    QDir directory = fileInfo.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = translated("Could not create chunk data override directory %1.")
                    .arg(QDir::toNativeSeparators(directory.absolutePath()));
        }
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, QByteArrayLiteral("png"));
    writer.setCompression(9);
    if (!writer.write(image.convertToFormat(QImage::Format_ARGB32))) {
        if (error)
            *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool hasExplicitPixels(const QImage &image)
{
    if (image.isNull())
        return false;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 255)
                return true;
        }
    }
    return false;
}

quint8 mergedBits(const QImage &image, int x, int y, quint8 generatedBits)
{
    if (image.isNull() || !image.rect().contains(x, y))
        return generatedBits;
    const QRgb pixel = image.pixel(x, y);
    if (qAlpha(pixel) == 0)
        return generatedBits;
    return quint8(qRed(pixel) & SupportedMask);
}

QString valueDescription(quint8 value)
{
    const quint8 supported = value & SupportedMask;
    if (supported == 0)
        return translated("0: no collision, wall, water, or room flags");
    QStringList flags;
    if (supported & Solid)
        flags += translated("Solid");
    if (supported & WallNorth)
        flags += translated("North wall");
    if (supported & WallWest)
        flags += translated("West wall");
    if (supported & Water)
        flags += translated("Water");
    if (supported & Room)
        flags += translated("Room");
    return QStringLiteral("%1: %2").arg(supported).arg(flags.join(
                translated(" + ")));
}

QColor valueColor(quint8 value)
{
    int red = 52;
    int green = 56;
    int blue = 62;
    if (value & Solid)
        red += 170;
    if (value & WallNorth) {
        red += 115;
        green += 105;
    }
    if (value & WallWest) {
        red += 140;
        green += 48;
    }
    if (value & Water) {
        green += 70;
        blue += 175;
    }
    if (value & Room) {
        red += 65;
        blue += 105;
    }
    return QColor(qMin(red, 255), qMin(green, 255), qMin(blue, 255), 225);
}

bool validateWorkflow(QString *summary, QString *error)
{
    QImage image(ImageSize, ImageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setPixel(3, 4, qRgba(31, 0, 0, 255));
    image.setPixel(5, 6, qRgba(0, 0, 0, 255));
    if (!validateImage(image, error)
            || mergedBits(image, 3, 4, 32) != 31
            || mergedBits(image, 5, 6, 31) != 0
            || mergedBits(image, 7, 8, 16) != 16
            || !hasExplicitPixels(image)) {
        if (error && error->isEmpty())
            *error = translated("Chunk data override merge behavior is incorrect.");
        return false;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        if (error)
            *error = translated("Could not create the chunk data validation directory.");
        return false;
    }
    const QString filePath = temporaryDirectory.filePath(
                QStringLiteral("chunkdata_10_20.png"));
    QImage loaded;
    if (!saveImage(filePath, image, error)
            || !loadImage(filePath, &loaded, error)
            || loaded != image) {
        if (error && error->isEmpty())
            *error = translated("Chunk data override PNG round trip failed.");
        return false;
    }

    QImage invalidSize(255, 256, QImage::Format_ARGB32);
    invalidSize.fill(Qt::transparent);
    QString expectedError;
    if (validateImage(invalidSize, &expectedError)) {
        if (error)
            *error = translated("An invalid chunk data image size was accepted.");
        return false;
    }
    QImage invalidValue(ImageSize, ImageSize, QImage::Format_ARGB32);
    invalidValue.fill(Qt::transparent);
    invalidValue.setPixel(1, 1, qRgba(32, 0, 0, 255));
    if (validateImage(invalidValue, &expectedError)) {
        if (error)
            *error = translated("An unsupported chunk data flag was accepted.");
        return false;
    }
    if (summary) {
        *summary = translated("256 x 256 RGBA validation, atomic PNG round trip, inherit, explicit empty, and combined values 0 through 31 passed");
    }
    return true;
}
}
