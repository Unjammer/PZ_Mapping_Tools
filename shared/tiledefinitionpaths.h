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
#include <QHash>
#include <QMap>
#include <QSettings>
#include <QSet>
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

inline bool isPatch(const QString &path)
{
    return logicalName(path).endsWith(QStringLiteral(".patch.tiles"), Qt::CaseInsensitive);
}

inline QString fileKey(const QString &path)
{
    const QFileInfo info(path);
    const QString parent = info.canonicalPath();
    return (parent.isEmpty() ? info.absolutePath() : parent).toLower()
            + QLatin1Char('/') + logicalName(path).toLower();
}

inline QStringList legacyOverrides(const QStringList &paths, const QString &gameDirectory,
                                   const QString &tilesDirectory)
{
    QStringList result;
    const QStringList builtIn = runtimeNames();
    for (const QString &path : paths) {
        if (builtIn.contains(logicalName(path), Qt::CaseInsensitive)) {
            QDir parent = QFileInfo(path).absoluteDir();
            const bool selectedText = path.endsWith(QStringLiteral(".tiles.txt"), Qt::CaseInsensitive)
                    && QFileInfo(path).isReadable()
                    && QFileInfo(path.left(path.size() - 4)).isReadable();
            if (!tilesDirectory.isEmpty() && parent == QDir(tilesDirectory) && !selectedText)
                continue;
            if (parent.dirName().compare(QStringLiteral("tile_definitions"), Qt::CaseInsensitive) == 0)
                parent = QDir(QDir::cleanPath(parent.absoluteFilePath(QStringLiteral(".."))));
            if (parent.dirName().compare(QStringLiteral("media"), Qt::CaseInsensitive) == 0) {
                const QDir root(QDir::cleanPath(parent.absoluteFilePath(QStringLiteral(".."))));
                const bool currentGame = !gameDirectory.isEmpty() && root == QDir(gameDirectory);
                if (currentGame ? !selectedText : root.exists(QStringLiteral("ProjectZomboid64.exe"))
                        || root.exists(QStringLiteral("projectzomboid.jar")))
                    continue;
            }
        }
        if (!path.isEmpty())
            result += path;
    }
    return result;
}

inline QStringList sharedOverrides(const QString &sharedSettingsFile, const QString &gameDirectory,
                                   const QString &tilesDirectory)
{
    QSettings shared(sharedSettingsFile, QSettings::IniFormat);
    const QString key = QStringLiteral("Paths/TileDefinitionOverrides");
    if (shared.contains(key))
        return shared.value(key).toStringList();
    QSettings portable(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("TheIndieStone"), QStringLiteral("TileZed"));
    const QString oldKey = QStringLiteral("TilePropertiesFiles");
    QSettings native(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("TheIndieStone"), QStringLiteral("TileZed"));
    return legacyOverrides(portable.value(oldKey, native.value(oldKey)).toStringList(),
                           gameDirectory, tilesDirectory);
}

inline void setSharedOverrides(const QString &sharedSettingsFile, const QStringList &paths)
{
    QSettings shared(sharedSettingsFile, QSettings::IniFormat);
    shared.setValue(QStringLiteral("Paths/TileDefinitionOverrides"), paths);
    shared.sync();
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
    QHash<QString, int> additions;
    for (const QString &candidate : configured) {
        if (builtIn.contains(logicalName(candidate), Qt::CaseInsensitive))
            continue;
        QString path = existingFile(candidate);
        if (path.isEmpty())
            path = candidate;
        path = QDir::toNativeSeparators(path);
        if (path.isEmpty())
            continue;
        const QString key = fileKey(path);
        const auto existing = additions.constFind(key);
        if (existing == additions.cend()) {
            additions.insert(key, result.size());
            result += path;
        } else {
            result[*existing] = path;
        }
    }
    return result;
}

inline QStringList files(const QString &path)
{
    if (path.isEmpty())
        return {};
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
    for (bool patches : {false, true}) {
        for (const QString &name : qAsConst(byName)) {
            if (isPatch(name) == patches)
                result += name;
        }
    }
    return result;
}

inline QStringList projectInputs(const QString &folder, const QStringList &effective,
                                 const QStringList &configured, const QString &gameDirectory)
{
    if (folder.isEmpty())
        return effective;
    if (!QFileInfo(folder).isDir())
        return {folder};
    const QDir project(directory(folder));
    const QStringList builtIn = runtimeNames();
    const bool useGame = !gameDirectory.isEmpty() && project == QDir(directory(gameDirectory));
    QHash<QString, QString> runtime;
    QSet<QString> listed;
    for (const QString &path : configured) {
        const QString resolved = existingFile(path);
        if (!resolved.isEmpty())
            listed.insert(fileKey(resolved));
    }
    for (const QString &path : effective) {
        const QString name = logicalName(path).toLower();
        if (builtIn.contains(name) && (useGame || listed.contains(fileKey(path))))
            runtime.insert(name, path);
    }
    QStringList extra;
    QStringList discoveredPatches;
    for (const QString &name : files(folder)) {
        const QString path = project.filePath(name);
        const QString logical = logicalName(name).toLower();
        if (builtIn.contains(logical)) {
            if (!runtime.contains(logical))
                runtime.insert(logical, path);
        } else if (!logical.endsWith(QStringLiteral("_4.tiles"))
                   && !listed.contains(fileKey(path))) {
            if (isPatch(name))
                discoveredPatches += path;
            else
                extra += path;
        }
    }
    QStringList result;
    for (const QString &name : builtIn) {
        if (runtime.contains(name))
            result += runtime.value(name);
    }
    result += extra;
    for (const QString &path : effective) {
        if (!builtIn.contains(logicalName(path), Qt::CaseInsensitive))
            result += path;
    }
    result += discoveredPatches;
    return result;
}

inline QStringList orderedFiles(const QStringList &inputs, QString &error)
{
    QStringList result;
    QHash<QString, int> positions;
    const auto append = [&](const QString &path, bool explicitFile) {
        const QString key = fileKey(path);
        const auto position = positions.constFind(key);
        if (position == positions.cend()) {
            positions.insert(key, result.size());
            result += path;
        } else if (explicitFile) {
            result[*position] = path;
        }
    };
    for (const QString &input : inputs) {
        if (QFileInfo(input).isDir()) {
            const QDir folder(directory(input));
            for (const QString &name : files(input))
                append(folder.filePath(name), false);
        } else {
            const QString path = existingFile(input);
            if (path.isEmpty()) {
                error = QStringLiteral("Tile definition file not found: %1").arg(input);
                return {};
            }
            append(path, true);
        }
    }
    return result;
}

}

#endif
