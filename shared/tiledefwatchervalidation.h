#ifndef TILEDEFWATCHERVALIDATION_H
#define TILEDEFWATCHERVALIDATION_H

#include "BuildingEditor/buildingfloor.h"
#include "tiledeffile.h"
#include "tiledefinitionpaths.h"
#include "map.h"
#include "mapwriter.h"
#include "tile.h"
#include "tileset.h"
#ifdef WORLDED
#include "navigation/isogridsquare256.h"
#include "world.h"
#endif

#include <QElapsedTimer>
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

namespace TileDefWatcherValidation {

inline bool validateMapProperties(QString *error)
{
    Tiled::Tileset tiles(QStringLiteral("property_validation"), 64, 128);
    QImage image(64, 128, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    if (!tiles.loadFromImage(image, QStringLiteral("property_validation.png")))
        return false;
    Tiled::Tile *tile = tiles.tileAt(0);
    tile->setProperty(QStringLiteral("invisible"), QString());
    tile->setProperty(QStringLiteral("custom"), QStringLiteral("retained"));
    Tiled::Map map(Tiled::Map::Isometric, 1, 1, 64, 128);
    map.addTileset(&tiles);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    Tiled::MapWriter writer;
    writer.writeMap(&map, &buffer);
    const QByteArray xml = buffer.data();
    if (xml.contains("name=\"invisible\"")
            || !xml.contains("name=\"custom\"")
            || !tile->properties().contains(QStringLiteral("invisible"))) {
        *error = QStringLiteral("TMX serialization changed persistent or cached tile properties");
        return false;
    }
    return true;
}

inline bool validateTextDefinitions(QString *error)
{
    using namespace Tiled::Internal;
#ifndef WORLDED
    if (TilePropertyMgr::instance()->properties().mProperties.isEmpty()
            && !TilePropertyMgr::instance()->readTxt()) {
        *error = TilePropertyMgr::instance()->errorString();
        return false;
    }
#endif
    QTemporaryDir directory;
    if (!directory.isValid())
        return false;
    const QString game = directory.filePath(QStringLiteral("game"));
    const QString media = game + QStringLiteral("/media");
    const QString definitions = media + QStringLiteral("/tile_definitions");
    if (!QDir().mkpath(definitions))
        return false;
    const QString primary = definitions + QStringLiteral("/newtiledefinitions.tiles.txt");
    const QString patch = definitions + QStringLiteral("/tiledefinitions_noiseworks.patch.tiles.txt");
    const auto writeText = [&](const QString &path, const QString &value) {
        QFile file(path);
        const QByteArray data = QStringLiteral(
                    "version = 1\ntileset\n{\n file = walls_validation_text\n"
                    " size = 8,8\n id = 1\n tile\n {\n xy = 0,0\n"
                    " GrimeType = %1\n ladderN = true\n }\n}\n")
                .arg(value).toUtf8();
        return file.open(QIODevice::WriteOnly)
                && file.write(data) == data.size() && file.flush();
    };
    if (!writeText(primary, QStringLiteral("base"))
            || !writeText(patch, QStringLiteral("patch")))
        return false;
    const QString name = QStringLiteral("newtiledefinitions.tiles");
    const QString selected = TileDefinitionPaths::gameFile(game, name);
    const QStringList files = TileDefinitionPaths::files(game);
    if (selected != QFileInfo(primary).canonicalFilePath()
            || TileDefinitionPaths::directory(media) != QFileInfo(definitions).canonicalFilePath()
            || files.size() != 2 || files.first() != name + QStringLiteral(".txt")) {
        *error = QStringLiteral("Text-only game definitions were not discovered in runtime order");
        return false;
    }
    const QStringList configured = TileDefinitionPaths::configuredFiles(
                {media + QLatin1Char('/') + name}, game, QString());
    if (configured.size() != 2
            || QDir::fromNativeSeparators(configured.first()) != selected) {
        *error = QStringLiteral("Missing legacy definition paths were not resolved to text");
        return false;
    }
    const QString oldGame = directory.filePath(QStringLiteral("old-game"));
    const QString oldMedia = oldGame + QStringLiteral("/media");
    const QString oldDefinitions = oldMedia + QStringLiteral("/tile_definitions");
    if (!QDir().mkpath(oldDefinitions))
        return false;
    QFile executable(oldGame + QStringLiteral("/ProjectZomboid64.exe"));
    if (!executable.open(QIODevice::WriteOnly))
        return false;
    executable.close();
    const QString legacyPrimary = oldMedia + QLatin1Char('/') + name;
    const QString installedPrimary = oldDefinitions + QLatin1Char('/') + name;
    const QString modPatch = directory.filePath(QStringLiteral("my-mod.patch.tiles"));
    if (TileDefinitionPaths::legacyOverrides({legacyPrimary, installedPrimary, modPatch}, game, QString())
            != QStringList{modPatch}) {
        *error = QStringLiteral("Migration retained a previous game's automatic definition path");
        return false;
    }
    const QString shared = directory.filePath(QStringLiteral("PZTools.ini"));
    TileDefinitionPaths::setSharedOverrides(shared, {modPatch, primary});
    if (TileDefinitionPaths::sharedOverrides(shared, game, QString())
            != QStringList{modPatch, primary}) {
        *error = QStringLiteral("Shared TileDef preferences lost their explicit file order");
        return false;
    }
    TileDefWatcher watcher;
    watcher.preferencesChanged({primary, patch});
    watcher.check();
    TileDefTile *tile = watcher.tile(QStringLiteral("walls_validation_text"), 0);
    if (!tile || tile->mProperties.value(QStringLiteral("GrimeType")) != QStringLiteral("patch")
            || !tile->mProperties.contains(QStringLiteral("ladderN"))) {
        *error = QStringLiteral("Text tile-property patches were not applied");
        return false;
    }
#ifndef WORLDED
    bool ladderKeys = false;
    for (TileDefProperty *property : TilePropertyMgr::instance()->properties().mProperties) {
        if (property->mName == QStringLiteral("Ladder direction")) {
            EnumTileDefProperty *ladder = property->asEnum();
            ladderKeys = ladder && ladder->mShortEnums.contains(QStringLiteral("ladderN"))
                    && ladder->mShortEnums.contains(QStringLiteral("ladderS"))
                    && ladder->mShortEnums.contains(QStringLiteral("ladderE"))
                    && ladder->mShortEnums.contains(QStringLiteral("ladderW"));
        }
    }
    if (!ladderKeys) {
        *error = QStringLiteral("Ladder controls do not use the runtime property names");
        return false;
    }
#endif
#ifdef WORLDED
    Navigate::IsoGridSquare256::Definitions exported;
    const bool loaded = exported.load({media}, *error);
    const auto *exportedTile = exported.tile(QStringLiteral("walls_validation_text"), 0);
    if (!loaded || !exportedTile || exportedTile->mProperties.value(
                QStringLiteral("GrimeType")) != QStringLiteral("patch")) {
        if (error->isEmpty())
            *error = QStringLiteral("Generate Lots did not load and merge text definitions");
        return false;
    }
#else
    TileDefFile decoded;
    TileDefFileReader reader;
    if (!reader.read(primary, decoded) || !decoded.write(definitions + QLatin1Char('/') + name)) {
        *error = decoded.errorString();
        return false;
    }
    if (TileDefinitionPaths::files(game).size() != 2
            || TileDefinitionPaths::gameFile(game, name).endsWith(QStringLiteral(".txt"))) {
        *error = QStringLiteral("Binary and text companions were loaded twice");
        return false;
    }
    const QString binary = definitions + QLatin1Char('/') + name;
    if (TileDefinitionPaths::legacyOverrides({binary, primary}, game, QString())
            != QStringList{primary}) {
        *error = QStringLiteral("Migration did not preserve the selected editable companion");
        return false;
    }
#endif
    return true;
}

inline bool run(QString *summary, QString *error)
{
    using namespace Tiled::Internal;
    if (!validateTextDefinitions(error) || !validateMapProperties(error))
        return false;
    QTemporaryDir directory;
    if (!directory.isValid()) {
        *error = QStringLiteral("Could not create tile-property validation directory");
        return false;
    }
    const QString sheet = QStringLiteral("walls_validation_01");
    const QString key = QStringLiteral("GrimeType");
    const QString removedKey = QStringLiteral("WallW");
    const QString base = directory.filePath(QStringLiteral("base.tiles"));
    const QString patch = directory.filePath(QStringLiteral("base.patch.tiles"));
    const QString second = directory.filePath(QStringLiteral("second.patch.tiles"));
    if (!QDir().mkpath(directory.filePath(QStringLiteral("mod"))))
        return false;
    const QString replacement = directory.filePath(QStringLiteral("mod/base.tiles"));
    const auto write = [&](const QString &path, const QString &value,
                           bool extra, int count = 64) {
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
        stream << qint32(8) << qint32(8) << qint32(1) << qint32(count);
        for (int i = 0; i < count; ++i) {
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
            || !write(second, QStringLiteral("second"), false)
            || !write(replacement, QStringLiteral("replacement"), false, 1)) {
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
    TileDefinitionIndex<TileDefFile, TileDefFileReader, TileDefTile> definitions;
    if (!definitions.load({directory.path(), replacement}, *error))
        return false;
    const auto *replaced = definitions.tile(sheet, 0);
    const auto *remaining = definitions.tile(sheet, 1);
    if (!replaced || !remaining || replaced->mProperties.contains(removedKey)
            || replaced->mProperties.value(key) != QStringLiteral("replacement")
            || remaining->mProperties.value(key) != QStringLiteral("second")
            || !remaining->mProperties.contains(removedKey)) {
        *error = QStringLiteral("Directory patches and partial sprite replacements loaded out of order");
        return false;
    }
    const QStringList reversePatches = {base, second, patch};
    const auto projectInputs = TileDefinitionPaths::projectInputs(directory.path(),
                reversePatches, reversePatches, QString());
    if (!definitions.load(projectInputs, *error))
        return false;
    const auto *first = definitions.tile(sheet, 0);
    if (!first || first->mProperties.value(key) != QStringLiteral("first")) {
        *error = QStringLiteral("Folder discovery overrode the configured patch order");
        return false;
    }
    const auto automaticPatch = TileDefinitionPaths::projectInputs(directory.path(),
                {base}, {base}, QString());
    if (!definitions.load(automaticPatch, *error))
        return false;
    const auto *automaticallyPatched = definitions.tile(sheet, 0);
    if (!automaticallyPatched || !automaticallyPatched->mProperties.contains(removedKey)) {
        *error = QStringLiteral("A discovered patch loaded before its explicitly selected base");
        return false;
    }
    watcher.preferencesChanged({patch});
    watcher.check();
    if (watcher.tile(sheet, 0)) {
        *error = QStringLiteral("A patch created a sprite without a base definition");
        return false;
    }
    watcher.preferencesChanged(paths);
    watcher.check();
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
    if (!watcher.tiles(sheet).isEmpty()) {
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
    for (int repetition = 0; repetition < 100; ++repetition) {
        TileDefinitionIndex<TileDefFile, TileDefFileReader, TileDefTile> index;
        for (const auto *file : qAsConst(configured.mFiles))
            index.add(*file->mTileDefFile, TileDefinitionPaths::isPatch(file->mFilePath));
    }
    const double repeatedMs = timer.nsecsElapsed() / 1000000.0;
    *summary = QStringLiteral("text definitions, paths, patches, ladder keys, overlay order, unchanged checks, edits, deletion, removal and base reload passed. "
                              "Configured files %1, cached 10000 checks %2 ms, repeated 100 indexes %3 ms")
            .arg(configured.mFiles.size()).arg(cachedMs, 0, 'f', 3)
            .arg(repeatedMs, 0, 'f', 3);
    return true;
}

}

#endif
