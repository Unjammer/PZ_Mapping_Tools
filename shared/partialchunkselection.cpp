/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include "partialchunkselection.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

using namespace PZTools;

PartialChunkSelection::PartialChunkSelection()
    : mEnabled(false)
    , mSelectedChunks(ChunksPerCell * ChunksPerCell, true)
{
}

bool PartialChunkSelection::enabled() const
{
    return mEnabled;
}

void PartialChunkSelection::setEnabled(bool enabled)
{
    mEnabled = enabled;
}

bool PartialChunkSelection::isSelected(int x, int y) const
{
    if (x < 0 || y < 0 || x >= ChunksPerCell || y >= ChunksPerCell)
        return false;
    return mSelectedChunks.testBit(x + y * ChunksPerCell);
}

void PartialChunkSelection::setSelected(int x, int y, bool selected)
{
    if (x < 0 || y < 0 || x >= ChunksPerCell || y >= ChunksPerCell)
        return;
    mSelectedChunks.setBit(x + y * ChunksPerCell, selected);
}

void PartialChunkSelection::selectAll()
{
    mSelectedChunks.fill(true);
}

void PartialChunkSelection::clear()
{
    mSelectedChunks.fill(false);
}

int PartialChunkSelection::selectedCount() const
{
    return mSelectedChunks.count(true);
}

const QBitArray &PartialChunkSelection::selectedChunks() const
{
    return mSelectedChunks;
}

QString PartialChunkSelection::filePath(const QString &mapPath)
{
    return mapPath + QStringLiteral(".pzchunks");
}

QRegion PartialChunkSelection::changedLassoRegion(
        const QRect &oldChunks, const QRect &newChunks)
{
    return QRegion(oldChunks).xored(QRegion(newChunks));
}

bool PartialChunkSelection::load(const QString &mapPath, QString *error)
{
    mEnabled = false;
    mSelectedChunks.fill(true);
    if (error)
        error->clear();
    if (mapPath.isEmpty())
        return true;

    QFile file(filePath(mapPath));
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
                file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        if (error)
            *error = QStringLiteral("Unsupported partial chunk selection version.");
        return false;
    }

    mEnabled = root.value(QStringLiteral("enabled")).toBool(false);
    mSelectedChunks.fill(false);
    const QJsonArray selected = root.value(QStringLiteral("selected")).toArray();
    for (const QJsonValue &value : selected) {
        const int index = value.toInt(-1);
        if (index >= 0 && index < mSelectedChunks.size())
            mSelectedChunks.setBit(index, true);
    }
    return true;
}

bool PartialChunkSelection::save(const QString &mapPath, QString *error) const
{
    if (error)
        error->clear();
    if (mapPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("The map must be saved before partial chunks can be configured.");
        return false;
    }

    QJsonArray selected;
    for (int index = 0; index < mSelectedChunks.size(); ++index) {
        if (mSelectedChunks.testBit(index))
            selected.append(index);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("enabled"), mEnabled);
    root.insert(QStringLiteral("selected"), selected);

    QSaveFile file(filePath(mapPath));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool PartialChunkSelection::validate(QString *error)
{
    if (error)
        error->clear();
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create the validation directory.");
        return false;
    }
    const QString mapPath = temporaryDirectory.filePath(
                QStringLiteral("cell.tmx"));
    PartialChunkSelection written;
    written.setEnabled(true);
    written.clear();
    written.setSelected(3, 4, true);
    written.setSelected(31, 31, true);
    if (!written.save(mapPath, error))
        return false;
    PartialChunkSelection read;
    if (!read.load(mapPath, error))
        return false;
    if (!read.enabled() || read.selectedCount() != 2
            || !read.isSelected(3, 4)
            || !read.isSelected(31, 31)
            || read.isSelected(0, 0)) {
        if (error)
            *error = QStringLiteral("The partial chunk selection did not round trip.");
        return false;
    }
    PartialChunkSelection missing;
    if (!missing.load(temporaryDirectory.filePath(
                          QStringLiteral("missing.tmx")), error))
        return false;
    if (missing.enabled()
            || missing.selectedCount() != ChunksPerCell * ChunksPerCell) {
        if (error)
            *error = QStringLiteral("A map without a sidecar did not use the safe defaults.");
        return false;
    }
    const QRect oldLasso(2, 3, 2, 2);
    const QRect newLasso(2, 3, 3, 2);
    if (!changedLassoRegion(oldLasso, oldLasso).isEmpty()
            || changedLassoRegion(oldLasso, newLasso)
               != QRegion(QRect(4, 3, 1, 2))) {
        if (error)
            *error = QStringLiteral("The partial chunk lasso invalidation region was incorrect.");
        return false;
    }
    return true;
}
