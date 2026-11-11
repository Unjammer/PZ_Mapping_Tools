/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef TILESETIMAGEPATHS_H
#define TILESETIMAGEPATHS_H

#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>

class TilesetImagePaths
{
public:
    void clear()
    {
        mRoot.clear();
        mRoot2x.clear();
        mFiles.clear();
        mResolved.clear();
    }

    bool find(const QString &root, const QString &root2x, const QString &name,
              QString &path1x, QString &path2x, int *scale = nullptr)
    {
        if (mRoot != root || mRoot2x != root2x)
            index(root, root2x);

        const QString key = name.toCaseFolded();
        const auto cached = mResolved.constFind(key);
        if (cached != mResolved.cend()) {
            path1x = cached->single;
            path2x = cached->doubled;
            if (scale)
                *scale = cached->scale;
            return true;
        }

        const QString filename = name + QStringLiteral(".png");
        const QDir dir1x(root);
        const QDir dir2x(root2x);
        path1x = dir1x.filePath(filename);
        path2x = dir2x.filePath(filename);
        const auto files = mFiles.constFind(key);
        if (files == mFiles.cend())
            return false;

        const QString doubled = firstReadable(files->doubled, dir2x, path2x, true);
        if (!doubled.isEmpty()) {
            path1x = dir1x.filePath(dir2x.relativeFilePath(doubled));
            path2x = doubled;
            mResolved.insert(key, {path1x, path2x, 2});
            if (scale)
                *scale = 2;
            return true;
        }

        const QString single = firstReadable(files->single, dir1x, path1x, false);
        if (single.isEmpty())
            return false;
        path1x = single;
        path2x = dir2x.filePath(dir1x.relativeFilePath(single));
        mResolved.insert(key, {path1x, path2x, 1});
        if (scale)
            *scale = 1;
        return true;
    }

private:
    struct Files {
        QStringList single;
        QStringList doubled;
    };

    struct Paths {
        QString single;
        QString doubled;
        int scale;
    };

    static int packPriority(const QDir &root, const QString &path)
    {
        static const QStringList packs = {
            QStringLiteral("Tiles2x.pack"), QStringLiteral("Overlays2x.pack"),
            QStringLiteral("JumboTrees2x.pack"), QStringLiteral("JumboTreesBigs2x.pack"),
            QStringLiteral("Tiles2x.floor.pack"), QStringLiteral("B42ChunkCaching2x.pack"),
            QStringLiteral("B42ChunkCaching2x.floor.pack"), QStringLiteral("Clock2x.pack")
        };
        const QString relative = root.relativeFilePath(path);
        const int separator = relative.indexOf(QLatin1Char('/'));
        if (separator < 0)
            return -1;
        const QString folder = relative.left(separator);
        for (int index = 0; index < packs.size(); ++index) {
            if (folder.compare(packs.at(index), Qt::CaseInsensitive) == 0)
                return index;
        }
        return -1;
    }

    static QString firstReadable(QStringList paths, const QDir &root,
                                 const QString &loosePath, bool preferGamePacks)
    {
        if (paths.contains(loosePath, Qt::CaseInsensitive) && QImageReader(loosePath).size().isValid())
            return loosePath;
        paths.sort(Qt::CaseInsensitive);
        for (const QString &path : paths) {
            if (!QImageReader(path).size().isValid())
                continue;
            QString selected = path;
            if (!preferGamePacks || paths.size() == 1)
                return selected;
            int priority = packPriority(root, path);
            for (const QString &other : paths) {
                const int otherPriority = packPriority(root, other);
                if (otherPriority >= 0 && otherPriority < priority && QImageReader(other).size().isValid()) {
                    selected = other;
                    priority = otherPriority;
                }
            }
            return selected;
        }
        return {};
    }

    void index(const QString &root, const QString &root2x)
    {
        clear();
        mRoot = root;
        mRoot2x = root2x;
        if (root.isEmpty())
            return;
        const QString prefix = QDir(root2x).absolutePath() + QLatin1Char('/');
        QDirIterator pngs(root, {QStringLiteral("*.png")}, QDir::Files,
                          QDirIterator::Subdirectories);
        while (pngs.hasNext()) {
            const QFileInfo info(pngs.next());
            const QString path = info.absoluteFilePath();
            auto &files = mFiles[info.completeBaseName().toCaseFolded()];
            if (path.startsWith(prefix, Qt::CaseInsensitive))
                files.doubled += path;
            else
                files.single += path;
        }
    }

    QString mRoot;
    QString mRoot2x;
    QHash<QString, Files> mFiles;
    QHash<QString, Paths> mResolved;
};

#endif
