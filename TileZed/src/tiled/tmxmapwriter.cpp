/*
 * tmxmapwriter.cpp
 * Copyright 2008-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tmxmapwriter.h"

#include "mapwriter.h"
#include "preferences.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

using namespace Tiled;
using namespace Tiled::Internal;

bool TmxMapWriter::write(const Map *map, const QString &fileName)
{
    Preferences *prefs = Preferences::instance();

    MapWriter writer;
    writer.setLayerDataFormat(prefs->layerDataFormat());
    writer.setDtdEnabled(prefs->dtdEnabled());

    QSaveFile file(fileName);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        mError = tr("Could not open the temporary save file for '%1':\n%2")
                .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    writer.writeMap(map, &file, QFileInfo(fileName).absolutePath());
    if (file.error() != QFileDevice::NoError) {
        mError = tr("Could not write the complete map to '%1':\n%2\n\n"
                    "The original file was left unchanged.")
                .arg(QDir::toNativeSeparators(fileName), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        mError = tr("Could not replace '%1' with the newly saved map:\n%2\n\n"
                    "The original file was left unchanged.")
                .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    mError.clear();
    return true;
}

#ifdef ZOMBOID
#include <QFile>
bool TmxMapWriter::write(const Map *map, QFile &file, const QString &path)
{
    Preferences *prefs = Preferences::instance();

    MapWriter writer;
    writer.setLayerDataFormat(prefs->layerDataFormat());
    writer.setDtdEnabled(prefs->dtdEnabled());

    writer.writeMap(map, &file, path);
    if (file.error() != QFile::NoError) {
        mError = file.errorString();
        return false;
    }
    mError.clear();
    return true;
}
#endif // ZOMBOID

bool TmxMapWriter::writeTileset(const Tileset *tileset,
                                const QString &fileName)
{
    Preferences *prefs = Preferences::instance();

    MapWriter writer;
    writer.setDtdEnabled(prefs->dtdEnabled());

    QSaveFile file(fileName);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        mError = tr("Could not open the temporary save file for '%1':\n%2")
                .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    writer.writeTileset(tileset, &file, QFileInfo(fileName).absolutePath());
    if (file.error() != QFileDevice::NoError) {
        mError = tr("Could not write the complete tileset to '%1':\n%2\n\n"
                    "The original file was left unchanged.")
                .arg(QDir::toNativeSeparators(fileName), file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        mError = tr("Could not replace '%1' with the newly saved tileset:\n%2\n\n"
                    "The original file was left unchanged.")
                .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    mError.clear();
    return true;
}

QByteArray TmxMapWriter::toByteArray(const Map *map)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);

    MapWriter writer;
    writer.setLayerDataFormat(MapWriter::Base64Zlib);
    writer.writeMap(map, &buffer);

    return bytes;
}
