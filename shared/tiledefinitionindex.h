/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef TILEDEFINITIONINDEX_H
#define TILEDEFINITIONINDEX_H

#include "tiledefinitionpaths.h"

#include <QSharedPointer>
#include <QVector>
#include <utility>

template<class File, class Reader, class Tile>
class TileDefinitionIndex
{
public:
    using Sheets = QHash<QString, QVector<Tile *>>;

    bool load(const QStringList &inputs, QString &error, Reader reader = Reader())
    {
        error.clear();
        const QStringList paths = TileDefinitionPaths::orderedFiles(inputs, error);
        if (!error.isEmpty())
            return false;

        TileDefinitionIndex loaded;
        for (const QString &path : paths) {
            const QSharedPointer<File> file(new File);
            if (!reader.read(path, *file)) {
                error = file->errorString();
                return false;
            }
            const bool patch = TileDefinitionPaths::isPatch(path);
            loaded.add(*file, patch);
            if (!patch)
                loaded.mFiles.append(file);
        }
        *this = std::move(loaded);
        return true;
    }

    void add(const File &file, bool patch = false)
    {
        for (const auto *sheet : file.tilesets()) {
            const int count = sheet->mDeclaredTileCount < 0 ? sheet->mTiles.size()
                    : qMin(sheet->mDeclaredTileCount, sheet->mTiles.size());
            if (patch) {
                const auto existing = mSheets.constFind(sheet->mName);
                if (existing == mSheets.cend())
                    continue;
                const int end = qMin(count, existing->size());
                for (int i = 0; i < end; ++i) {
                    const auto &properties = sheet->mTiles.at(i)->mProperties;
                    for (auto property = properties.cbegin(); property != properties.cend(); ++property)
                        existing->at(i)->mProperties.insert(property.key(), property.value());
                }
            } else {
                auto &tiles = mSheets[sheet->mName];
                if (tiles.size() < count)
                    tiles.resize(count);
                for (int i = 0; i < count; ++i)
                    tiles[i] = sheet->mTiles.at(i);
            }
        }
    }

    const Sheets &sheets() const { return mSheets; }

    const QVector<Tile *> &tiles(const QString &name) const
    {
        static const QVector<Tile *> empty;
        const auto found = mSheets.constFind(name);
        return found == mSheets.cend() ? empty : *found;
    }

    Tile *tile(const QString &name, int index) const
    {
        const auto &sheet = tiles(name);
        return index >= 0 && index < sheet.size() ? sheet.at(index) : nullptr;
    }

private:
    QList<QSharedPointer<File>> mFiles;
    Sheets mSheets;
};

#endif
