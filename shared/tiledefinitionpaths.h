/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef TILEDEFINITIONPATHS_H
#define TILEDEFINITIONPATHS_H

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QStringList>

namespace TileDefinitionPaths {

inline QString logicalName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QStringLiteral(".tiles.txt"), Qt::CaseInsensitive))
        name.chop(4);
    return name;
}

inline QString existingFile(const QString &path)
{
    if (path.isEmpty())
        return QString();
    QStringList candidates = {path};
    if (path.endsWith(QStringLiteral(".tiles"), Qt::CaseInsensitive))
        candidates += path + QStringLiteral(".txt");
    else if (path.endsWith(QStringLiteral(".tiles.txt"), Qt::CaseInsensitive))
        candidates += path.left(path.size() - 4);
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isReadable())
            return info.canonicalFilePath();
    }
    return QString();
}

inline QString gameFile(const QString &gameDirectory, const QString &name)
{
    if (gameDirectory.isEmpty())
        return QString();
    const QDir game(gameDirectory);
    for (const QString &folder : {QStringLiteral("media/tile_definitions/"),
                                  QStringLiteral("media/")}) {
        const QString path = existingFile(game.filePath(folder + name));
        if (!path.isEmpty())
            return path;
    }
    return QString();
}

inline QString directory(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QDir root(path);
    const QStringList candidates = {
        root.filePath(QStringLiteral("media/tile_definitions")),
        root.filePath(QStringLiteral("media")),
        root.filePath(QStringLiteral("tile_definitions")), path
    };
    for (const QString &candidate : candidates) {
        if (!existingFile(QDir(candidate).filePath(
                    QStringLiteral("newtiledefinitions.tiles"))).isEmpty())
            return QFileInfo(candidate).canonicalFilePath();
    }
    return path;
}

inline QStringList runtimeNames()
{
    return {QStringLiteral("newtiledefinitions.tiles"),
            QStringLiteral("tiledefinitions_erosion.tiles"),
            QStringLiteral("tiledefinitions_overlays.tiles"),
            QStringLiteral("tiledefinitions_b42chunkcaching.tiles"),
            QStringLiteral("tiledefinitions_noiseworks.patch.tiles"),
            QStringLiteral("jumbo_trees_big.tiles"),
            QStringLiteral("jumbo_trees.tiles")};
}

inline QStringList configuredFiles(const QStringList &configured,
                                   const QString &gameDirectory,
                                   const QString &tilesDirectory)
{
    QStringList result;
    const QStringList builtIn = runtimeNames();
    for (const QString &name : builtIn) {
        QString path;
        QString missing;
        for (const QString &candidate : configured) {
            if (logicalName(candidate).compare(name, Qt::CaseInsensitive) != 0)
                continue;
            if (missing.isEmpty())
                missing = candidate;
            path = existingFile(candidate);
            if (!path.isEmpty())
                break;
        }
        if (path.isEmpty())
            path = gameFile(gameDirectory, name);
        if (path.isEmpty() && !tilesDirectory.isEmpty())
            path = existingFile(QDir(tilesDirectory).filePath(name));
        if (path.isEmpty())
            path = missing;
        if (!path.isEmpty())
            result += QDir::toNativeSeparators(path);
    }
    for (const QString &candidate : configured) {
        if (builtIn.contains(logicalName(candidate), Qt::CaseInsensitive))
            continue;
        QString path = existingFile(candidate);
        if (path.isEmpty())
            path = candidate;
        path = QDir::toNativeSeparators(path);
        if (!path.isEmpty() && !result.contains(path, Qt::CaseInsensitive))
            result += path;
    }
    return result;
}

inline QStringList files(const QString &path)
{
    const QDir folder(directory(path));
    const QStringList discovered = folder.entryList(
                {QStringLiteral("*.tiles"), QStringLiteral("*.tiles.txt")},
                QDir::Files, QDir::Name);
    QMap<QString, QString> byName;
    for (const QString &name : discovered) {
        const QString logical = logicalName(name).toLower();
        if (!byName.contains(logical)
                || name.endsWith(QStringLiteral(".tiles"), Qt::CaseInsensitive))
            byName[logical] = name;
    }
    QStringList result;
    for (const QString &name : runtimeNames()) {
        const QString actual = byName.take(name);
        if (!actual.isEmpty())
            result += actual;
    }
    result += byName.values();
    return result;
}

}

#endif
