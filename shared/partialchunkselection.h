/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef PARTIALCHUNKSELECTION_H
#define PARTIALCHUNKSELECTION_H

#include <QBitArray>
#include <QRect>
#include <QRegion>
#include <QString>

namespace PZTools
{

class PartialChunkSelection
{
public:
    static const int ChunksPerCell = 32;
    static const int ChunkSize = 8;

    PartialChunkSelection();

    bool enabled() const;
    void setEnabled(bool enabled);

    bool isSelected(int x, int y) const;
    void setSelected(int x, int y, bool selected);
    void selectAll();
    void clear();
    int selectedCount() const;
    const QBitArray &selectedChunks() const;

    bool load(const QString &mapPath, QString *error = nullptr);
    bool save(const QString &mapPath, QString *error = nullptr) const;

    static QString filePath(const QString &mapPath);
    static QRegion changedLassoRegion(const QRect &oldChunks,
                                      const QRect &newChunks);
    static bool validate(QString *error = nullptr);

private:
    bool mEnabled;
    QBitArray mSelectedChunks;
};

}

#endif
