#ifndef TILEDEFWATCHERVALIDATION_H
#define TILEDEFWATCHERVALIDATION_H

#include "BuildingEditor/buildingfloor.h"
#include "tiledeffile.h"

#include <QElapsedTimer>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

namespace TileDefWatcherValidation {

inline bool run(QString *summary, QString *error)
{
    using namespace Tiled::Internal;
    QTemporaryDir directory;
    if (!directory.isValid()) {
        *error = QStringLiteral("Could not create tile-property validation directory");
        return false;
    }
    const QString sheet = QStringLiteral("walls_validation_01");
    const QString key = QStringLiteral("GrimeType");
    const QString removedKey = QStringLiteral("WallW");
    const QString base = directory.filePath(QStringLiteral("base.tiles"));
    const QString patch = directory.filePath(QStringLiteral("first.patch.tiles"));
    const QString second = directory.filePath(QStringLiteral("second.patch.tiles"));
    const auto write = [&](const QString &path, const QString &value,
                           bool extra) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        const auto string = [&](const QString &text) {
            const QByteArray bytes = text.toUtf8() + '\n';
            stream.writeRawData(bytes.constData(), bytes.size());
        };
        stream.writeRawData("tdef", 4);
        stream << qint32(1) << qint32(1);
        string(sheet);
        string(sheet + QStringLiteral(".png"));
        stream << qint32(8) << qint32(8) << qint32(1) << qint32(64);
        for (int i = 0; i < 64; ++i) {
            stream << qint32(extra ? 2 : 1);
            string(key);
            string(value);
            if (extra) {
                string(removedKey);
                string(QString());
            }
        }
        return stream.status() == QDataStream::Ok && file.flush();
    };
    if (!write(base, QStringLiteral("base"), false)
            || !write(patch, QStringLiteral("first"), true)
            || !write(second, QStringLiteral("second"), false)) {
        *error = QStringLiteral("Could not write tile-property fixtures");
        return false;
    }
    TileDefWatcher watcher;
    const QStringList paths = {base, patch, second};
    watcher.preferencesChanged(paths);
    watcher.check();
    const auto expect = [&](const QString &value, bool extra) {
        TileDefTile *tile = watcher.tile(sheet, 0);
        return tile && tile->mProperties.value(key) == value
                && tile->mProperties.contains(removedKey) == extra;
    };
    if (!expect(QStringLiteral("second"), true)) {
        *error = QStringLiteral("Ordered tile-property overlays differ");
        return false;
    }
    TileDefTile *original = watcher.tile(sheet, 0);
    const QMap<QString, QString> properties = original->mProperties;
    for (int i = 0; i < 10000; ++i)
        watcher.check();
    watcher.preferencesChanged(paths);
    watcher.check();
    if (watcher.tile(sheet, 0) != original
            || !original->mProperties.isSharedWith(properties)) {
        *error = QStringLiteral("Unchanged properties were rebuilt");
        return false;
    }
    watcher.preferencesChanged({base, second, patch});
    watcher.check();
    if (!expect(QStringLiteral("first"), true)) {
        *error = QStringLiteral("Reordered tile-property overlays differ");
        return false;
    }
    if (!write(patch, QStringLiteral("edited"), false))
        return false;
    watcher.fileChanged(patch);
    watcher.check();
    if (!expect(QStringLiteral("edited"), false)) {
        *error = QStringLiteral("Edited overlay retained removed properties");
        return false;
    }
    if (!QFile::remove(patch))
        return false;
    watcher.fileChanged(patch);
    watcher.check();
    if (!expect(QStringLiteral("second"), false)) {
        *error = QStringLiteral("Deleted overlay retained properties");
        return false;
    }
    watcher.preferencesChanged({base});
    watcher.check();
    if (!expect(QStringLiteral("base"), false)) {
        *error = QStringLiteral("Removed overlay retained properties");
        return false;
    }
    if (!write(base, QStringLiteral("changed base"), true))
        return false;
    watcher.fileChanged(base);
    watcher.check();
    if (!expect(QStringLiteral("changed base"), true)) {
        *error = QStringLiteral("Changed base properties were not refreshed");
        return false;
    }
    watcher.preferencesChanged({});
    watcher.check();
    if (watcher.tileset(sheet)) {
        *error = QStringLiteral("Removed property file is still available");
        return false;
    }

    TileDefWatcher configured;
    configured.check();
    QElapsedTimer timer;
    timer.start();
    for (int repetition = 0; repetition < 10000; ++repetition)
        configured.check();
    const double cachedMs = timer.nsecsElapsed() / 1000000.0;
    timer.restart();
    int mergedTiles = 0;
    for (int repetition = 0; repetition < 100; ++repetition) {
        for (TileDefWatcherFile *file : qAsConst(configured.mFiles))
            file->check(*configured.mWatcher);
        for (int i = 0; i < configured.mFiles.size(); ++i) {
            TileDefWatcherFile *overlay = configured.mFiles.at(i);
            if (!overlay->mFilePath.endsWith(QStringLiteral(".patch.tiles"),
                                             Qt::CaseInsensitive))
                continue;
            for (int j = 0; j < i; ++j)
                mergedTiles += configured.mFiles.at(j)->mTileDefFile
                        ->mergePropertiesFrom(*overlay->mTileDefFile);
        }
    }
    const double repeatedMs = timer.nsecsElapsed() / 1000000.0;
    *summary = QStringLiteral("overlay order, unchanged checks, edits, deletion, removal and base reload passed. "
                              "Configured files %1, cached 10000 checks %2 ms, repeated 100 merges %3 ms, merged tiles %4")
            .arg(configured.mFiles.size()).arg(cachedMs, 0, 'f', 3)
            .arg(repeatedMs, 0, 'f', 3).arg(mergedTiles);
    return true;
}

}

#endif
