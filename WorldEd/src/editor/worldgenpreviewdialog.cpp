/*
 * Copyright 2026 Alree / Unjammer
 *
 * This file is part of PZTools Unofficial.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "worldgenpreviewdialog.h"

#include "preferences.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "worlddocument.h"

#include "BuildingEditor/building.h"
#include "BuildingEditor/buildingmap.h"
#include "BuildingEditor/buildingreader.h"
#include "customtilesize.h"
#include "layer.h"
#include "map.h"
#include "mapreader.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

using namespace Tiled;
using namespace Tiled::Internal;

namespace {

const int PreviewSize = 16;
const int ChunkSize = 8;
const int TileWidth = 64;
const int TileHeight = 32;
const qreal PreviewScale = 0.7;

const QStringList FeatureCategories = {
    QStringLiteral("GROUND"),
    QStringLiteral("PLANT"),
    QStringLiteral("BUSH"),
    QStringLiteral("TREE"),
    QStringLiteral("ORE")
};

const QStringList FeatureRegistries =
        FeatureCategories + (QStringList() << QStringLiteral("NONE"));

const QStringList PrefabCategories = {
    QStringLiteral("Floor"),
    QStringLiteral("FloorFurniture"),
    QStringLiteral("FloorOverlay"),
    QStringLiteral("Furniture")
};

struct WorldGenPattern
{
    QVector<QVector<QString> > rows;

    int width() const
    {
        return rows.isEmpty() ? 0 : rows.first().size();
    }

    int height() const
    {
        return rows.size();
    }

    int minimumDimension() const
    {
        return qMin(width(), height());
    }
};

struct WorldGenFeature
{
    QString name;
    QString category;
    QList<WorldGenPattern> patterns;
    int minimumSize = 1;
    bool projectOwned = false;
};

struct WorldGenPrefab
{
    QString name;
    int width = 1;
    int height = 1;
    double zombies = 0.0;
    QStringList tiles;
    QMap<QString, QVector<int> > schematic;
    bool projectOwned = false;

    int tileRef(const QString &category, int x, int y) const
    {
        const QVector<int> refs = schematic.value(category);
        const int index = y * width + x;
        return index >= 0 && index < refs.size() ? refs.at(index) : 0;
    }
};

struct WeightedFeature
{
    QString featureName;
    double probability = 0.0;
    QString probabilityText;
};

struct WorldGenBiome
{
    QString name;
    QString parent;
    bool mapBiome = false;
    bool generate = true;
    bool hasGenerate = false;
    QMap<QString, QList<WeightedFeature> > declaredFeatures;
    QMap<QString, QList<WeightedFeature> > features;
    QMap<QString, QStringList> declaredParameters;
    QMap<QString, QStringList> parameters;
    int declaredSubBiomeLinks = 0;
    int subBiomeLinks = 0;
    int declaredPlacementRules = 0;
    int placementRules = 0;
    int declaredProtectedRules = 0;
    int protectedRules = 0;
    int declaredReplacementRules = 0;
    int replacementRules = 0;
    bool projectOwned = false;
};

struct WorldGenDefinitions
{
    QMap<QString, WorldGenFeature> features;
    QMap<QString, WorldGenPrefab> prefabs;
    QMap<QString, WorldGenBiome> proceduralBiomes;
    QMap<QString, WorldGenBiome> mapBiomes;
    int subBiomeCount = 0;
    QString rootPath;
    QString projectRootPath;
};

struct PreviewTile
{
    QString sprite;
    QString category;
    QString feature;
    double probability = 0.0;
};

struct PreviewGrid
{
    QVector<QVector<PreviewTile> > squares;
    int concreteTileCount = 0;
    int markerCount = 0;

    PreviewGrid()
        : squares(PreviewSize * PreviewSize)
    {
    }

    QVector<PreviewTile> &at(int x, int y)
    {
        return squares[y * PreviewSize + x];
    }

    const QVector<PreviewTile> &at(int x, int y) const
    {
        return squares[y * PreviewSize + x];
    }
};

struct PendingTile
{
    QString sprite;
    QString feature;
    double probability = 0.0;
};

QString luaString(lua_State *state, int index)
{
    const char *value = lua_tostring(state, index);
    return value ? QString::fromUtf8(value) : QString();
}

QString stringField(lua_State *state, int tableIndex, const char *name)
{
    tableIndex = lua_absindex(state, tableIndex);
    lua_getfield(state, tableIndex, name);
    const QString value = lua_isstring(state, -1)
            ? luaString(state, -1) : QString();
    lua_pop(state, 1);
    return value;
}

bool boolField(lua_State *state, int tableIndex, const char *name,
               bool *present)
{
    tableIndex = lua_absindex(state, tableIndex);
    lua_getfield(state, tableIndex, name);
    const bool hasValue = lua_isboolean(state, -1);
    const bool value = hasValue ? lua_toboolean(state, -1) != 0 : false;
    lua_pop(state, 1);
    if (present)
        *present = hasValue;
    return value;
}

QStringList stringSequence(lua_State *state, int tableIndex)
{
    QStringList result;
    tableIndex = lua_absindex(state, tableIndex);
    const int length = int(lua_rawlen(state, tableIndex));
    for (int index = 1; index <= length; ++index) {
        lua_rawgeti(state, tableIndex, index);
        if (lua_isstring(state, -1))
            result.append(luaString(state, -1));
        lua_pop(state, 1);
    }
    return result;
}

int nestedSequenceEntryCount(lua_State *state, int tableIndex, int depth = 0)
{
    if (depth > 5 || !lua_istable(state, tableIndex))
        return 0;
    tableIndex = lua_absindex(state, tableIndex);
    const int sequenceLength = int(lua_rawlen(state, tableIndex));
    if (sequenceLength > 0)
        return sequenceLength;

    int count = 0;
    lua_pushnil(state);
    while (lua_next(state, tableIndex) != 0) {
        if (lua_istable(state, -1))
            count += nestedSequenceEntryCount(state, -1, depth + 1);
        lua_pop(state, 1);
    }
    return count;
}

QString normalizedWorldGenRoot(const QString &path)
{
    if (path.trimmed().isEmpty())
        return QString();

    const QString cleanPath = QDir::cleanPath(
                QDir::fromNativeSeparators(path.trimmed()));
    const QStringList candidates = {
        cleanPath,
        QDir(cleanPath).filePath(
            QStringLiteral("media/lua/server/WorldGen")),
        QDir(cleanPath).filePath(
            QStringLiteral("lua/server/WorldGen"))
    };

    for (const QString &candidate : candidates) {
        const QDir directory(candidate);
        if (directory.exists(QStringLiteral("features"))
                && directory.exists(QStringLiteral("biomes"))) {
            return directory.absolutePath();
        }
    }
    return QString();
}

QString ownerRootForWorldGen(const QString &worldGenRoot)
{
    QDir owner(QDir::cleanPath(worldGenRoot));
    // WorldGen -> server -> lua -> either the owner root or "media".
    for (int level = 0; level < 3; ++level) {
        if (!owner.cdUp())
            return QString();
    }
    if (owner.dirName().compare(QStringLiteral("media"),
                                Qt::CaseInsensitive) == 0
            && !owner.cdUp()) {
        return QString();
    }
    return owner.absolutePath();
}

QStringList luaFiles(const QString &directory)
{
    QStringList result;
    QDirIterator iterator(directory, QStringList() << QStringLiteral("*.lua"),
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        result.append(QDir::cleanPath(iterator.next()));
    std::sort(result.begin(), result.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool executeLuaFile(lua_State *state, const QString &fileName, QString *error)
{
    const QByteArray encodedName = QFile::encodeName(fileName);
    int status = luaL_loadfile(state, encodedName.constData());
    if (status == LUA_OK)
        status = lua_pcall(state, 0, 0, 0);
    if (status == LUA_OK)
        return true;

    if (error) {
        *error = QObject::tr("%1\n\n%2")
                .arg(QDir::toNativeSeparators(fileName),
                     luaString(state, -1));
    }
    lua_pop(state, 1);
    return false;
}

void luaInstructionLimit(lua_State *state, lua_Debug *)
{
    luaL_error(state, "WorldGen definition exceeded the preview "
               "instruction limit");
}

WorldGenPattern readPattern(lua_State *state, int index)
{
    WorldGenPattern pattern;
    index = lua_absindex(state, index);
    const int outerLength = int(lua_rawlen(state, index));
    if (outerLength <= 0)
        return pattern;

    lua_rawgeti(state, index, 1);
    const bool containsRows = lua_istable(state, -1);
    lua_pop(state, 1);

    if (!containsRows) {
        pattern.rows.append(stringSequence(state, index).toVector());
        return pattern;
    }

    for (int row = 1; row <= outerLength; ++row) {
        lua_rawgeti(state, index, row);
        if (lua_istable(state, -1))
            pattern.rows.append(stringSequence(state, -1).toVector());
        lua_pop(state, 1);
    }
    return pattern;
}

WorldGenFeature readFeature(lua_State *state, int index,
                            const QString &category,
                            const QString &name)
{
    WorldGenFeature feature;
    feature.category = category;
    feature.name = name;
    feature.minimumSize = 8;
    index = lua_absindex(state, index);

    lua_getfield(state, index, "main");
    if (lua_istable(state, -1)) {
        const int length = int(lua_rawlen(state, -1));
        for (int item = 1; item <= length; ++item) {
            lua_rawgeti(state, -1, item);
            WorldGenPattern pattern;
            if (lua_isstring(state, -1)) {
                pattern.rows.append(
                            QVector<QString>() << luaString(state, -1));
            } else if (lua_istable(state, -1)) {
                pattern = readPattern(state, -1);
            }
            lua_pop(state, 1);
            if (pattern.width() <= 0 || pattern.height() <= 0)
                continue;
            feature.minimumSize = qMin(feature.minimumSize,
                                       pattern.minimumDimension());
            feature.patterns.append(pattern);
        }
    }
    lua_pop(state, 1);
    if (feature.patterns.isEmpty())
        feature.minimumSize = 1;
    return feature;
}

QHash<QString, quintptr> featureRegistryPointers(lua_State *state)
{
    QHash<QString, quintptr> pointers;
    lua_getglobal(state, "worldgen");
    lua_getfield(state, -1, "features");
    for (const QString &category : FeatureRegistries) {
        const QByteArray categoryName = category.toLatin1();
        lua_getfield(state, -1, categoryName.constData());
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            continue;
        }
        const int registryIndex = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, registryIndex) != 0) {
            if (lua_isstring(state, -2) && lua_istable(state, -1)) {
                pointers.insert(
                            category + QLatin1Char(':')
                            + luaString(state, -2),
                            reinterpret_cast<quintptr>(
                                lua_topointer(state, -1)));
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 2);
    return pointers;
}

QHash<QString, quintptr> biomeRegistryPointers(lua_State *state,
                                               const char *registryName)
{
    QHash<QString, quintptr> pointers;
    lua_getglobal(state, "worldgen");
    lua_getfield(state, -1, registryName);
    if (lua_istable(state, -1)) {
        const int registryIndex = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, registryIndex) != 0) {
            if (lua_isstring(state, -2) && lua_istable(state, -1)) {
                pointers.insert(
                            luaString(state, -2),
                            reinterpret_cast<quintptr>(
                                lua_topointer(state, -1)));
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 2);
    return pointers;
}

QVector<int> prefabRow(const QString &row, int width)
{
    QVector<int> result(width, 0);
    const QStringList values = row.split(QLatin1Char(','));
    for (int column = 0; column < qMin(width, values.size()); ++column) {
        bool ok = false;
        const int value = values.at(column).trimmed().toInt(&ok);
        result[column] = ok && value >= 0 ? value : 0;
    }
    return result;
}

WorldGenPrefab readPrefab(lua_State *state, int index, const QString &name)
{
    WorldGenPrefab prefab;
    prefab.name = name;
    index = lua_absindex(state, index);

    lua_getfield(state, index, "dimensions");
    if (lua_istable(state, -1)) {
        lua_rawgeti(state, -1, 1);
        if (lua_isnumber(state, -1))
            prefab.width = qMax(1, int(lua_tointeger(state, -1)));
        lua_pop(state, 1);
        lua_rawgeti(state, -1, 2);
        if (lua_isnumber(state, -1))
            prefab.height = qMax(1, int(lua_tointeger(state, -1)));
        lua_pop(state, 1);
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "zombies");
    if (lua_isnumber(state, -1))
        prefab.zombies = lua_tonumber(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, index, "tiles");
    if (lua_istable(state, -1))
        prefab.tiles = stringSequence(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, index, "schematic");
    if (lua_istable(state, -1)) {
        const int schematicIndex = lua_gettop(state);
        for (const QString &category : PrefabCategories) {
            QVector<int> refs(prefab.width * prefab.height, 0);
            const QByteArray categoryName = category.toLatin1();
            lua_getfield(state, schematicIndex, categoryName.constData());
            if (lua_istable(state, -1)) {
                for (int row = 0; row < prefab.height; ++row) {
                    lua_rawgeti(state, -1, row + 1);
                    if (lua_isstring(state, -1)) {
                        const QVector<int> parsed =
                                prefabRow(luaString(state, -1),
                                          prefab.width);
                        for (int column = 0; column < prefab.width; ++column)
                            refs[row * prefab.width + column] =
                                    parsed.at(column);
                    }
                    lua_pop(state, 1);
                }
            }
            lua_pop(state, 1);
            prefab.schematic.insert(category, refs);
        }
    }
    lua_pop(state, 1);
    return prefab;
}

void readPrefabRegistry(lua_State *state, WorldGenDefinitions *definitions,
                        const QSet<QString> &projectEntries)
{
    lua_getglobal(state, "worldgen");
    lua_getfield(state, -1, "prefabs");
    if (lua_istable(state, -1)) {
        const int registryIndex = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, registryIndex) != 0) {
            if (lua_isstring(state, -2) && lua_istable(state, -1)) {
                const QString name = luaString(state, -2);
                WorldGenPrefab prefab = readPrefab(state, -1, name);
                prefab.projectOwned = projectEntries.contains(name);
                definitions->prefabs.insert(name, prefab);
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 2);
}

QSet<QString> changedRegistryEntries(
        const QHash<QString, quintptr> &before,
        const QHash<QString, quintptr> &after)
{
    QSet<QString> changed;
    for (auto iterator = after.constBegin(); iterator != after.constEnd();
         ++iterator) {
        if (!before.contains(iterator.key())
                || before.value(iterator.key()) != iterator.value()) {
            changed.insert(iterator.key());
        }
    }
    return changed;
}

bool executeLuaFiles(lua_State *state, const QStringList &files,
                     QString *error)
{
    for (const QString &fileName : files) {
        if (!executeLuaFile(state, fileName, error))
            return false;
    }
    return true;
}

void readFeatureRegistry(lua_State *state, WorldGenDefinitions *definitions,
                         QHash<quintptr, QString> *featureNames,
                         const QSet<QString> &projectEntries)
{
    lua_getglobal(state, "worldgen");
    lua_getfield(state, -1, "features");
    for (const QString &category : FeatureRegistries) {
        const QByteArray categoryName = category.toLatin1();
        lua_getfield(state, -1, categoryName.constData());
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            continue;
        }
        const int registryIndex = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, registryIndex) != 0) {
            if (lua_isstring(state, -2) && lua_istable(state, -1)) {
                const QString name = luaString(state, -2);
                const WorldGenFeature feature =
                        readFeature(state, -1, category, name);
                WorldGenFeature sourcedFeature = feature;
                sourcedFeature.projectOwned = projectEntries.contains(
                            category + QLatin1Char(':') + name);
                definitions->features.insert(name, sourcedFeature);
                const quintptr pointer = reinterpret_cast<quintptr>(
                            lua_topointer(state, -1));
                featureNames->insert(pointer, name);
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 2);
}

QList<WeightedFeature> readWeightedFeatures(
        lua_State *state, int index,
        const QHash<quintptr, QString> &featureNames)
{
    QList<WeightedFeature> result;
    index = lua_absindex(state, index);
    const int length = int(lua_rawlen(state, index));
    for (int item = 1; item <= length; ++item) {
        lua_rawgeti(state, index, item);
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            continue;
        }

        WeightedFeature weighted;
        lua_getfield(state, -1, "f");
        if (lua_istable(state, -1)) {
            const quintptr pointer = reinterpret_cast<quintptr>(
                        lua_topointer(state, -1));
            weighted.featureName = featureNames.value(pointer);
        }
        lua_pop(state, 1);

        lua_getfield(state, -1, "p");
        if (lua_isnumber(state, -1)) {
            weighted.probability = lua_tonumber(state, -1);
            weighted.probabilityText =
                    QString::number(weighted.probability, 'g', 8);
        } else if (lua_isstring(state, -1)) {
            weighted.probabilityText = luaString(state, -1);
        }
        lua_pop(state, 1);
        lua_pop(state, 1);

        if (!weighted.featureName.isEmpty())
            result.append(weighted);
    }
    return result;
}

WorldGenBiome readBiome(lua_State *state, int index, const QString &name,
                        bool mapBiome,
                        const QHash<quintptr, QString> &featureNames)
{
    WorldGenBiome biome;
    biome.name = name;
    biome.mapBiome = mapBiome;
    index = lua_absindex(state, index);
    biome.parent = stringField(state, index, "parent");

    lua_getfield(state, index, "features");
    if (lua_istable(state, -1)) {
        for (const QString &category : FeatureCategories) {
            const QByteArray categoryName = category.toLatin1();
            lua_getfield(state, -1, categoryName.constData());
            if (lua_istable(state, -1)) {
                biome.declaredFeatures.insert(
                            category,
                            readWeightedFeatures(state, -1, featureNames));
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "params");
    if (lua_istable(state, -1)) {
        const int paramsIndex = lua_gettop(state);
        const QStringList sequenceFields = {
            QStringLiteral("landscape"),
            QStringLiteral("plant"),
            QStringLiteral("bush"),
            QStringLiteral("temperature"),
            QStringLiteral("hygrometry"),
            QStringLiteral("ore_level")
        };
        for (const QString &field : sequenceFields) {
            const QByteArray fieldName = field.toLatin1();
            lua_getfield(state, paramsIndex, fieldName.constData());
            if (lua_istable(state, -1))
                biome.declaredParameters.insert(
                            field, stringSequence(state, -1));
            lua_pop(state, 1);
        }

        lua_getfield(state, paramsIndex, "zombies");
        if (lua_isnumber(state, -1)) {
            biome.declaredParameters.insert(
                        QStringLiteral("zombies"),
                        QStringList() << QString::number(
                            lua_tonumber(state, -1), 'g', 8));
        }
        lua_pop(state, 1);

        const bool generateValue =
                boolField(state, paramsIndex, "generate",
                          &biome.hasGenerate);
        biome.generate = biome.hasGenerate ? generateValue : true;

        const char *countFields[] = {
            "subbiomes", "placements", "protected", "replacements"
        };
        int *countTargets[] = {
            &biome.declaredSubBiomeLinks,
            &biome.declaredPlacementRules,
            &biome.declaredProtectedRules,
            &biome.declaredReplacementRules
        };
        for (int field = 0; field < 4; ++field) {
            lua_getfield(state, paramsIndex, countFields[field]);
            if (lua_istable(state, -1))
                *countTargets[field] =
                        nestedSequenceEntryCount(state, -1);
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    return biome;
}

QMap<QString, WorldGenBiome> readBiomeRegistry(
        lua_State *state, const char *registryName, bool mapBiome,
        const QHash<quintptr, QString> &featureNames,
        const QSet<QString> &projectEntries)
{
    QMap<QString, WorldGenBiome> rawBiomes;
    lua_getglobal(state, "worldgen");
    lua_getfield(state, -1, registryName);
    if (lua_istable(state, -1)) {
        const int registryIndex = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, registryIndex) != 0) {
            if (lua_isstring(state, -2) && lua_istable(state, -1)) {
                const QString name = luaString(state, -2);
                WorldGenBiome biome = readBiome(
                            state, -1, name, mapBiome, featureNames);
                biome.projectOwned = projectEntries.contains(name);
                rawBiomes.insert(name, biome);
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 2);
    return rawBiomes;
}

WorldGenBiome resolveBiome(
        const QString &name,
        const QMap<QString, WorldGenBiome> &rawBiomes,
        QMap<QString, WorldGenBiome> *resolved,
        QSet<QString> *resolving)
{
    if (resolved->contains(name))
        return resolved->value(name);
    const WorldGenBiome raw = rawBiomes.value(name);
    if (resolving->contains(name))
        return raw;
    resolving->insert(name);

    WorldGenBiome result;
    if (!raw.parent.isEmpty() && rawBiomes.contains(raw.parent))
        result = resolveBiome(raw.parent, rawBiomes, resolved, resolving);

    result.name = raw.name;
    result.parent = raw.parent;
    result.mapBiome = raw.mapBiome;
    result.projectOwned = raw.projectOwned;
    result.declaredFeatures = raw.declaredFeatures;
    result.declaredParameters = raw.declaredParameters;
    for (auto iterator = raw.declaredFeatures.constBegin();
         iterator != raw.declaredFeatures.constEnd(); ++iterator) {
        result.features.insert(iterator.key(), iterator.value());
    }
    for (auto iterator = raw.declaredParameters.constBegin();
         iterator != raw.declaredParameters.constEnd(); ++iterator) {
        result.parameters.insert(iterator.key(), iterator.value());
    }
    // WorldGenReader inherits most biome fields through the parent chain,
    // but deliberately keeps the child's own generate flag. An omitted flag
    // defaults to true, allowing generate=false templates to feed selectable
    // ore-level children.
    result.generate = raw.generate;
    result.hasGenerate = raw.hasGenerate;

    result.declaredSubBiomeLinks = raw.declaredSubBiomeLinks;
    result.declaredPlacementRules = raw.declaredPlacementRules;
    result.declaredProtectedRules = raw.declaredProtectedRules;
    result.declaredReplacementRules = raw.declaredReplacementRules;
    if (raw.declaredSubBiomeLinks)
        result.subBiomeLinks = raw.declaredSubBiomeLinks;
    if (raw.declaredPlacementRules)
        result.placementRules = raw.declaredPlacementRules;
    if (raw.declaredProtectedRules)
        result.protectedRules = raw.declaredProtectedRules;
    if (raw.declaredReplacementRules)
        result.replacementRules = raw.declaredReplacementRules;

    resolving->remove(name);
    resolved->insert(name, result);
    return result;
}

QMap<QString, WorldGenBiome> resolveBiomes(
        const QMap<QString, WorldGenBiome> &rawBiomes)
{
    QMap<QString, WorldGenBiome> resolved;
    QSet<QString> resolving;
    for (const QString &name : rawBiomes.keys())
        resolveBiome(name, rawBiomes, &resolved, &resolving);
    return resolved;
}

bool loadDefinitions(const QString &requestedPath,
                     const QString &projectRootPath,
                     WorldGenDefinitions *definitions,
                     QString *error)
{
    const QString root = normalizedWorldGenRoot(requestedPath);
    if (root.isEmpty()) {
        if (error) {
            *error = QObject::tr(
                        "Select the Project Zomboid game directory or its "
                        "media/lua/server/WorldGen directory.");
        }
        return false;
    }

    lua_State *state = luaL_newstate();
    if (!state) {
        if (error)
            *error = QObject::tr("Could not create the isolated Lua state.");
        return false;
    }
    lua_sethook(state, luaInstructionLimit, LUA_MASKCOUNT, 1000000);

    const char initialization[] =
            "worldgen = {"
            " features = { GROUND={}, PLANT={}, BUSH={}, TREE={}, ORE={}, NONE={} },"
            " prefabs = {}, biomes = {}, biomes_map = {}, subbiomes = {}"
            "}";
    if (luaL_dostring(state, initialization) != LUA_OK) {
        if (error)
            *error = luaString(state, -1);
        lua_close(state);
        return false;
    }

    const QString projectRoot = projectRootPath.trimmed().isEmpty()
            ? QString()
            : QDir::cleanPath(QDir::fromNativeSeparators(projectRootPath));
    const QDir gameDirectory(root);
    const QDir projectDirectory(projectRoot);

    if (!executeLuaFiles(
                state,
                luaFiles(gameDirectory.filePath(QStringLiteral("features"))),
                error)) {
        lua_close(state);
        return false;
    }
    const QHash<QString, quintptr> gameFeaturePointers =
            featureRegistryPointers(state);
    if (!projectRoot.isEmpty()
            && !executeLuaFiles(
                state,
                luaFiles(projectDirectory.filePath(
                             QStringLiteral("features"))),
                error)) {
        lua_close(state);
        return false;
    }
    const QHash<QString, quintptr> finalFeaturePointers =
            featureRegistryPointers(state);
    const QSet<QString> projectFeatureEntries =
            changedRegistryEntries(gameFeaturePointers,
                                   finalFeaturePointers);

    if (!executeLuaFiles(
                state,
                luaFiles(gameDirectory.filePath(QStringLiteral("prefabs"))),
                error)) {
        lua_close(state);
        return false;
    }
    const QHash<QString, quintptr> gamePrefabPointers =
            biomeRegistryPointers(state, "prefabs");
    if (!projectRoot.isEmpty()
            && !executeLuaFiles(
                state,
                luaFiles(projectDirectory.filePath(
                             QStringLiteral("prefabs"))),
                error)) {
        lua_close(state);
        return false;
    }
    const QSet<QString> projectPrefabEntries =
            changedRegistryEntries(
                gamePrefabPointers,
                biomeRegistryPointers(state, "prefabs"));

    if (!executeLuaFiles(
                state,
                luaFiles(gameDirectory.filePath(
                             QStringLiteral("biomes/subbiomes"))),
                error)
            || (!projectRoot.isEmpty()
                && !executeLuaFiles(
                    state,
                    luaFiles(projectDirectory.filePath(
                                 QStringLiteral("biomes/subbiomes"))),
                    error))) {
        lua_close(state);
        return false;
    }

    QStringList gameBiomeFiles;
    gameBiomeFiles += luaFiles(gameDirectory.filePath(
                                   QStringLiteral("biomes/map")));
    gameBiomeFiles += luaFiles(gameDirectory.filePath(
                                   QStringLiteral("biomes/worldgen")));
    if (!executeLuaFiles(state, gameBiomeFiles, error)) {
        lua_close(state);
        return false;
    }
    const QHash<QString, quintptr> gameProceduralPointers =
            biomeRegistryPointers(state, "biomes");
    const QHash<QString, quintptr> gameMapPointers =
            biomeRegistryPointers(state, "biomes_map");

    if (!projectRoot.isEmpty()) {
        QStringList projectBiomeFiles;
        projectBiomeFiles += luaFiles(projectDirectory.filePath(
                                          QStringLiteral("biomes/map")));
        projectBiomeFiles += luaFiles(projectDirectory.filePath(
                                          QStringLiteral("biomes/worldgen")));
        if (!executeLuaFiles(state, projectBiomeFiles, error)) {
            lua_close(state);
            return false;
        }
    }
    const QSet<QString> projectProceduralEntries =
            changedRegistryEntries(
                gameProceduralPointers,
                biomeRegistryPointers(state, "biomes"));
    const QSet<QString> projectMapEntries =
            changedRegistryEntries(
                gameMapPointers,
                biomeRegistryPointers(state, "biomes_map"));

    WorldGenDefinitions result;
    result.rootPath = root;
    result.projectRootPath = projectRoot;
    QHash<quintptr, QString> featureNames;
    readFeatureRegistry(state, &result, &featureNames,
                        projectFeatureEntries);
    readPrefabRegistry(state, &result, projectPrefabEntries);

    const QMap<QString, WorldGenBiome> rawProcedural =
            readBiomeRegistry(state, "biomes", false, featureNames,
                              projectProceduralEntries);
    const QMap<QString, WorldGenBiome> rawMap =
            readBiomeRegistry(state, "biomes_map", true, featureNames,
                              projectMapEntries);
    result.proceduralBiomes = resolveBiomes(rawProcedural);
    result.mapBiomes = resolveBiomes(rawMap);

    lua_getglobal(state, "worldgen");
    lua_getfield(state, -1, "subbiomes");
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        while (lua_next(state, -2) != 0) {
            ++result.subBiomeCount;
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 2);
    lua_close(state);

    for (const WorldGenPrefab &prefab : result.prefabs) {
        if (prefab.width <= 0 || prefab.height <= 0) {
            if (error) {
                *error = QObject::tr(
                            "Prefab '%1' has invalid dimensions %2 x %3.")
                        .arg(prefab.name)
                        .arg(prefab.width)
                        .arg(prefab.height);
            }
            return false;
        }
        for (const QString &category : PrefabCategories) {
            const QVector<int> refs = prefab.schematic.value(category);
            if (refs.size() != prefab.width * prefab.height) {
                if (error) {
                    *error = QObject::tr(
                        "Prefab '%1' category %2 does not match its "
                        "dimensions.")
                            .arg(prefab.name, category);
                }
                return false;
            }
            for (int ref : refs) {
                if (ref < 0 || ref > prefab.tiles.size()) {
                    if (error) {
                        *error = QObject::tr(
                            "Prefab '%1' category %2 references tile %3, but "
                            "its tiles table contains %4 entries.")
                                .arg(prefab.name, category)
                                .arg(ref)
                                .arg(prefab.tiles.size());
                    }
                    return false;
                }
            }
        }
    }

    if (result.features.isEmpty()
            || (result.proceduralBiomes.isEmpty()
                && result.mapBiomes.isEmpty())) {
        if (error) {
            *error = QObject::tr(
                        "No WorldGen feature or biome definition was found "
                        "under %1.").arg(QDir::toNativeSeparators(root));
        }
        return false;
    }
    *definitions = result;
    return true;
}

const WorldGenBiome *findBiome(const WorldGenDefinitions &definitions,
                               const QString &key)
{
    if (key.startsWith(QStringLiteral("procedural:"))) {
        const auto iterator = definitions.proceduralBiomes.constFind(
                    key.mid(11));
        return iterator == definitions.proceduralBiomes.constEnd()
                ? nullptr : &iterator.value();
    }
    if (key.startsWith(QStringLiteral("map:"))) {
        const auto iterator = definitions.mapBiomes.constFind(key.mid(4));
        return iterator == definitions.mapBiomes.constEnd()
                ? nullptr : &iterator.value();
    }
    return nullptr;
}

PreviewGrid generatePreview(const WorldGenDefinitions &definitions,
                            const WorldGenBiome &biome,
                            quint32 seed)
{
    PreviewGrid grid;
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> realDistribution(0.0, 1.0);

    for (const QString &category : FeatureCategories) {
        const QList<WeightedFeature> weightedFeatures =
                biome.features.value(category);
        if (weightedFeatures.isEmpty())
            continue;

        QVector<PendingTile> pending(PreviewSize * PreviewSize);
        double allProbability = 0.0;
        for (const WeightedFeature &weighted : weightedFeatures)
            allProbability += qMax(0.0, weighted.probability);

        for (int x = 0; x < PreviewSize; ++x) {
            for (int y = 0; y < PreviewSize; ++y) {
                const int squareIndex = y * PreviewSize + x;
                PendingTile current = pending.at(squareIndex);
                if (current.sprite.isEmpty()) {
                    for (int targetSize : { 8, 4, 2, 1 }) {
                        QList<WeightedFeature> eligibleFeatures;
                        double eligibleProbability = 0.0;
                        for (const WeightedFeature &weighted
                             : weightedFeatures) {
                            const WorldGenFeature feature =
                                    definitions.features.value(
                                        weighted.featureName);
                            if (feature.minimumSize > targetSize)
                                continue;
                            eligibleFeatures.append(weighted);
                            eligibleProbability += qMax(
                                        0.0, weighted.probability);
                        }
                        if (eligibleFeatures.isEmpty()
                                || eligibleProbability <= 0.0)
                            continue;

                        const double randomValue = realDistribution(random);
                        double cumulative = 0.0;
                        WeightedFeature selected;
                        bool hasSelection = false;
                        for (const WeightedFeature &weighted
                             : eligibleFeatures) {
                            cumulative += qMax(0.0, weighted.probability)
                                    / eligibleProbability * allProbability;
                            if (randomValue < cumulative) {
                                selected = weighted;
                                hasSelection = true;
                                break;
                            }
                        }
                        if (!hasSelection)
                            break;

                        const WorldGenFeature feature =
                                definitions.features.value(
                                    selected.featureName);
                        QList<WorldGenPattern> eligiblePatterns;
                        for (const WorldGenPattern &pattern
                             : feature.patterns) {
                            if (pattern.width() <= targetSize
                                    && pattern.height() <= targetSize) {
                                eligiblePatterns.append(pattern);
                            }
                        }
                        if (eligiblePatterns.isEmpty())
                            continue;
                        std::uniform_int_distribution<int> patternDistribution(
                                    0, eligiblePatterns.size() - 1);
                        const WorldGenPattern pattern =
                                eligiblePatterns.at(
                                    patternDistribution(random));
                        if (x + pattern.width() > PreviewSize
                                || y + pattern.height() > PreviewSize)
                            continue;

                        for (int row = 0; row < pattern.height(); ++row) {
                            for (int column = 0;
                                 column < pattern.width(); ++column) {
                                PendingTile tile;
                                tile.sprite = pattern.rows.at(row).at(column);
                                tile.feature = selected.featureName;
                                tile.probability = selected.probability;
                                pending[(y + row) * PreviewSize
                                        + x + column] = tile;
                            }
                        }
                        current = pending.at(squareIndex);
                        break;
                    }
                }

                if (current.sprite.isEmpty())
                    continue;
                pending[squareIndex] = PendingTile();
                if (current.sprite.startsWith(QLatin1Char('$'))) {
                    ++grid.markerCount;
                    continue;
                }

                PreviewTile tile;
                tile.sprite = current.sprite;
                tile.category = category;
                tile.feature = current.feature;
                tile.probability = current.probability;
                grid.at(x, y).append(tile);
                ++grid.concreteTileCount;
            }
        }
    }
    return grid;
}

bool splitSpriteName(const QString &sprite, QString *tilesetName,
                     int *tileIndex)
{
    const int separator = sprite.lastIndexOf(QLatin1Char('_'));
    if (separator <= 0)
        return false;
    bool ok = false;
    const int index = sprite.mid(separator + 1).toInt(&ok);
    if (!ok || index < 0)
        return false;
    if (tilesetName)
        *tilesetName = sprite.left(separator);
    if (tileIndex)
        *tileIndex = index;
    return true;
}

bool hasCompatiblePreviewGeometry(Tileset *tileset)
{
    if (!tileset)
        return false;
    const QSize customSize =
            CustomTileSize::forTileset(tileset->name());
    if (customSize.isEmpty() || !tileset->isLoaded())
        return true;
    const int sourceScale =
            tileset->imageSource2x().isEmpty() ? 1 : 2;
    const int sourceTileWidth = customSize.width() * sourceScale;
    const int sourceTileHeight = customSize.height() * sourceScale;
    return sourceTileWidth > 0 && sourceTileHeight > 0
            && tileset->imageWidth() % sourceTileWidth == 0
            && tileset->imageHeight() % sourceTileHeight == 0;
}

Tile *tileForSprite(const QString &sprite)
{
    QString tilesetName;
    int tileIndex = -1;
    if (!splitSpriteName(sprite, &tilesetName, &tileIndex))
        return nullptr;
    Tileset *tileset = TileMetaInfoMgr::instance()->tileset(tilesetName);
    if (!tileset || !hasCompatiblePreviewGeometry(tileset)
            || tileIndex >= tileset->tileCount())
        return nullptr;
    return tileset->tileAt(tileIndex);
}

QPointF squareBase(int x, int y)
{
    // The margins include the maximum JUMBOXXL footprint. The complete
    // logical scene is scaled in paintEvent() to keep the 2x2-chunk block
    // and its largest overhanging sprites visible at once.
    const qreal originX = 736.0;
    const qreal originY = 550.0;
    return QPointF(originX + (x - y) * TileWidth / 2.0,
                   originY + (x + y) * TileHeight / 2.0);
}

QPointF squareImageBase(int x, int y)
{
    return squareBase(x, y)
            + QPointF(-TileWidth / 2.0, TileHeight);
}

QPainterPath squareDiamond(int x, int y)
{
    const QPointF top = squareBase(x, y);
    QPainterPath path;
    path.moveTo(top);
    path.lineTo(top + QPointF(TileWidth / 2.0,
                              TileHeight / 2.0));
    path.lineTo(top + QPointF(0.0, TileHeight));
    path.lineTo(top + QPointF(-TileWidth / 2.0,
                              TileHeight / 2.0));
    path.closeSubpath();
    return path;
}

QRectF previewTileTarget(Tile *tile, int x, int y)
{
    Tileset *tileset = tile->tileset();
    const qreal sourceScale = tileset->imageSource2x().isEmpty()
            ? 1.0 : 0.5;
    const QPointF base = squareImageBase(x, y);
    const QPoint offset = tileset->tileOffset() + tile->offset();
    QRectF target(
                base.x() + offset.x() * sourceScale,
                base.y() + offset.y() * sourceScale
                - tile->height() * sourceScale,
                tile->image().width() * sourceScale,
                tile->image().height() * sourceScale);
    const QSize customSize =
            CustomTileSize::forTileset(tileset->name());
    if (!customSize.isEmpty()) {
        // CellScene applies this translation in the source image's
        // coordinate system. Convert it along with a selected 2x PNG.
        target.translate(
                    -(customSize.width() - TileWidth)
                    / 2.0 * sourceScale,
                    0.0);
    }
    return target;
}

void drawPreviewTile(QPainter *painter, Tile *tile, int x, int y)
{
    if (!tile || !tile->tileset() || tile->image().isNull())
        return;
    painter->drawImage(previewTileTarget(tile, x, y), tile->image());
}

class WorldGenPreviewCanvas : public QWidget
{
public:
    explicit WorldGenPreviewCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(1120, 760);
        setMouseTracking(true);
        setAutoFillBackground(true);
        QPalette canvasPalette = palette();
        canvasPalette.setColor(QPalette::Window, QColor(32, 35, 38));
        setPalette(canvasPalette);
    }

    void setGrid(const PreviewGrid &grid)
    {
        mGrid = grid;
        update();
    }

    void setCategoryVisible(const QString &category, bool visible)
    {
        mCategoryVisibility[category] = visible;
        update();
    }

    std::function<void(int, int)> squareSelected;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().window());
        painter.save();
        painter.scale(PreviewScale, PreviewScale);

        painter.setPen(QPen(QColor(76, 84, 90), 1.0));
        painter.setBrush(QColor(44, 49, 53));
        for (int x = 0; x < PreviewSize; ++x) {
            for (int y = 0; y < PreviewSize; ++y)
                painter.drawPath(squareDiamond(x, y));
        }

        for (int diagonal = 0; diagonal <= (PreviewSize - 1) * 2;
             ++diagonal) {
            for (int x = 0; x < PreviewSize; ++x) {
                const int y = diagonal - x;
                if (y < 0 || y >= PreviewSize)
                    continue;
                const QVector<PreviewTile> &tiles = mGrid.at(x, y);
                for (const PreviewTile &placed : tiles) {
                    if (!mCategoryVisibility.value(
                                placed.category, true))
                        continue;
                    Tile *tile = tileForSprite(placed.sprite);
                    if (!tile || tile->image().isNull())
                        continue;
                    Tileset *tileset = tile->tileset();
                    drawPreviewTile(&painter, tile, x, y);

                    const QString tilesetName = tileset->name();
                    const bool splitJumbo =
                            tilesetName.contains(
                                QStringLiteral("JUMBOXL_"),
                                Qt::CaseInsensitive)
                            || tilesetName.contains(
                                QStringLiteral("JUMBOXXL_"),
                                Qt::CaseInsensitive);
                    if (splitJumbo && tile->id() < 6) {
                        // IsoTreeJumbo renders XL/XXL trees as the main
                        // sprite N plus its treetop sprite N+6.
                        drawPreviewTile(&painter,
                                        tileset->tileAt(tile->id() + 6),
                                        x, y);
                    }
                }
            }
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(112, 122, 130, 180), 1.0));
        for (int x = 0; x <= PreviewSize; ++x) {
            QPainterPath line;
            line.moveTo(squareBase(x, 0));
            line.lineTo(squareBase(x, PreviewSize));
            painter.drawPath(line);
        }
        for (int y = 0; y <= PreviewSize; ++y) {
            QPainterPath line;
            line.moveTo(squareBase(0, y));
            line.lineTo(squareBase(PreviewSize, y));
            painter.drawPath(line);
        }

        painter.setPen(QPen(QColor(255, 180, 45), 3.0));
        const QPointF top = squareBase(0, 0);
        const QPointF east = squareBase(PreviewSize, 0);
        const QPointF bottom = squareBase(PreviewSize, PreviewSize);
        const QPointF west = squareBase(0, PreviewSize);
        painter.drawPolygon(QPolygonF() << top << east << bottom << west);

        painter.setPen(QPen(QColor(82, 190, 255), 2.0,
                            Qt::DashLine));
        painter.drawLine(squareBase(ChunkSize, 0),
                         squareBase(ChunkSize, PreviewSize));
        painter.drawLine(squareBase(0, ChunkSize),
                         squareBase(PreviewSize, ChunkSize));
        painter.restore();

        painter.setPen(QColor(225, 229, 232));
        painter.drawText(QRectF(18, 16, width() - 36, 32),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         tr("16 x 16 squares  |  2 x 2 chunks  |  "
                            "orange: generation block  |  blue: chunk borders"));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return QWidget::mousePressEvent(event);
        const QPointF logicalPosition =
                QPointF(event->pos()) / PreviewScale;
        for (int y = 0; y < PreviewSize; ++y) {
            for (int x = 0; x < PreviewSize; ++x) {
                if (!squareDiamond(x, y).contains(logicalPosition))
                    continue;
                mSelectedSquare = QPoint(x, y);
                if (squareSelected)
                    squareSelected(x, y);
                update();
                return;
            }
        }
    }

private:
    PreviewGrid mGrid;
    QMap<QString, bool> mCategoryVisibility;
    QPoint mSelectedSquare = QPoint(-1, -1);
};

QString projectWorldGenRoot(WorldDocument *worldDocument)
{
    if (!worldDocument || worldDocument->fileName().isEmpty())
        return QString();
    return QDir(QFileInfo(worldDocument->fileName()).absolutePath())
            .filePath(QStringLiteral("media/lua/server/WorldGen"));
}

bool validDefinitionName(const QString &name)
{
    static const QRegularExpression expression(
                QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return expression.match(name).hasMatch();
}

QString luaQuoted(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString luaStringList(const QStringList &values)
{
    QStringList quoted;
    for (const QString &value : values)
        quoted.append(luaQuoted(value));
    return QStringLiteral("{ %1 }").arg(
                quoted.join(QStringLiteral(", ")));
}

bool writeProjectLuaFile(const QString &fileName, const QString &contents,
                         QString *error)
{
    const QFileInfo info(fileName);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) {
            *error = QObject::tr("Could not create project directory %1.")
                    .arg(QDir::toNativeSeparators(info.absolutePath()));
        }
        return false;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QObject::tr("Could not open %1 for writing: %2")
                    .arg(QDir::toNativeSeparators(fileName),
                         file.errorString());
        }
        return false;
    }
    const QByteArray utf8 = contents.toUtf8();
    if (file.write(utf8) != utf8.size() || !file.commit()) {
        if (error) {
            *error = QObject::tr("Could not save %1: %2")
                    .arg(QDir::toNativeSeparators(fileName),
                         file.errorString());
        }
        return false;
    }
    return true;
}

QString featureLua(const WorldGenFeature &feature)
{
    QString output;
    output += QStringLiteral(
                "-- Generated by PZWorldEd WorldGen Editor.\n"
                "-- Project-owned definition; game files are never modified.\n\n");
    output += QStringLiteral("local %1 = {\n    main = {\n")
            .arg(feature.name);
    for (const WorldGenPattern &pattern : feature.patterns) {
        if (pattern.width() == 1 && pattern.height() == 1) {
            output += QStringLiteral("        %1,\n")
                    .arg(luaQuoted(pattern.rows.first().first()));
            continue;
        }
        output += QStringLiteral("        {\n");
        for (const QVector<QString> &row : pattern.rows) {
            QStringList values;
            for (const QString &value : row)
                values.append(luaQuoted(value));
            output += QStringLiteral("            { %1 },\n")
                    .arg(values.join(QStringLiteral(", ")));
        }
        output += QStringLiteral("        },\n");
    }
    output += QStringLiteral(
                "    }\n"
                "}\n\n"
                "worldgen.features.%1[%2] = %3\n")
            .arg(feature.category, luaQuoted(feature.name), feature.name);
    return output;
}

QString prefabLua(const WorldGenPrefab &prefab)
{
    QString output;
    output += QStringLiteral(
                "-- Generated by PZWorldEd WorldGen Prefab Editor.\n"
                "-- Project/mod-owned definition; base-game files are never "
                "modified.\n\n");
    output += QStringLiteral(
                "local prefab = {\n"
                "    dimensions = { %1, %2 },\n"
                "    zombies = %3,\n"
                "    tiles = {\n")
            .arg(prefab.width)
            .arg(prefab.height)
            .arg(QString::number(prefab.zombies, 'g', 12));
    for (const QString &tile : prefab.tiles)
        output += QStringLiteral("        %1,\n").arg(luaQuoted(tile));
    output += QStringLiteral("    },\n    schematic = {\n");
    for (const QString &category : PrefabCategories) {
        const QVector<int> refs = prefab.schematic.value(category);
        bool hasTile = false;
        for (int value : refs) {
            if (value != 0) {
                hasTile = true;
                break;
            }
        }
        if (!hasTile && category != QStringLiteral("Floor"))
            continue;
        output += QStringLiteral("        %1 = {\n").arg(category);
        for (int row = 0; row < prefab.height; ++row) {
            QStringList values;
            for (int column = 0; column < prefab.width; ++column) {
                const int index = row * prefab.width + column;
                values.append(QString::number(
                                  index < refs.size() ? refs.at(index) : 0));
            }
            output += QStringLiteral("            %1,\n")
                    .arg(luaQuoted(values.join(QLatin1Char(','))));
        }
        output += QStringLiteral("        },\n");
    }
    output += QStringLiteral(
                "    }\n"
                "}\n\n"
                "worldgen.prefabs[%1] = prefab\n")
            .arg(luaQuoted(prefab.name));
    return output;
}

QString biomeLua(const WorldGenBiome &biome)
{
    QString output;
    output += QStringLiteral(
                "-- Generated by PZWorldEd WorldGen Editor.\n"
                "-- Project-owned definition; game files are never modified.\n\n");
    output += QStringLiteral("local %1 = {\n").arg(biome.name);
    if (!biome.parent.isEmpty()) {
        output += QStringLiteral("    parent = %1,\n")
                .arg(luaQuoted(biome.parent));
    }
    if (!biome.declaredFeatures.isEmpty()) {
        output += QStringLiteral("    features = {\n");
        for (const QString &category : FeatureCategories) {
            const QList<WeightedFeature> entries =
                    biome.declaredFeatures.value(category);
            if (entries.isEmpty())
                continue;
            output += QStringLiteral("        %1 = {\n").arg(category);
            for (const WeightedFeature &entry : entries) {
                output += QStringLiteral(
                    "            { f = worldgen.features.%1[%2], p = %3 },\n")
                        .arg(category, luaQuoted(entry.featureName),
                             QString::number(entry.probability, 'g', 12));
            }
            output += QStringLiteral("        },\n");
        }
        output += QStringLiteral("    },\n");
    }

    output += QStringLiteral("    params = {\n");
    const QStringList sequenceFields = {
        QStringLiteral("landscape"),
        QStringLiteral("plant"),
        QStringLiteral("bush"),
        QStringLiteral("temperature"),
        QStringLiteral("hygrometry"),
        QStringLiteral("ore_level")
    };
    for (const QString &field : sequenceFields) {
        const QStringList values = biome.declaredParameters.value(field);
        if (!values.isEmpty()) {
            output += QStringLiteral("        %1 = %2,\n")
                    .arg(field, luaStringList(values));
        }
    }
    if (biome.declaredParameters.contains(QStringLiteral("zombies"))) {
        output += QStringLiteral("        zombies = %1,\n")
                .arg(biome.declaredParameters.value(
                         QStringLiteral("zombies")).value(0,
                                                          QStringLiteral("0")));
    }
    output += QStringLiteral("        generate = %1,\n")
            .arg(biome.generate ? QStringLiteral("true")
                                : QStringLiteral("false"));
    output += QStringLiteral("    }\n}\n\n");
    output += QStringLiteral("worldgen.%1[%2] = %3\n")
            .arg(biome.mapBiome ? QStringLiteral("biomes_map")
                                : QStringLiteral("biomes"),
                 luaQuoted(biome.name), biome.name);
    return output;
}

class WorldGenFeatureEditorDialog : public QDialog
{
public:
    WorldGenFeatureEditorDialog(const WorldGenFeature &initial,
                                bool editingProjectDefinition,
                                QWidget *parent)
        : QDialog(parent)
        , mPatterns(initial.patterns)
    {
        setWindowTitle(editingProjectDefinition
                       ? tr("Edit Project Biome Feature")
                       : tr("Create Biome Feature"));
        resize(850, 620);
        if (mPatterns.isEmpty()) {
            WorldGenPattern pattern;
            pattern.rows.append(QVector<QString>() << QString());
            mPatterns.append(pattern);
        }

        QVBoxLayout *layout = new QVBoxLayout(this);
        QFormLayout *identity = new QFormLayout;
        mName = new QLineEdit(initial.name, this);
        mName->setEnabled(!editingProjectDefinition);
        identity->addRow(tr("Name:"), mName);
        mCategory = new QComboBox(this);
        mCategory->addItems(FeatureRegistries);
        int categoryIndex = mCategory->findText(initial.category);
        mCategory->setCurrentIndex(categoryIndex < 0 ? 0 : categoryIndex);
        mCategory->setEnabled(!editingProjectDefinition);
        identity->addRow(tr("Category:"), mCategory);
        layout->addLayout(identity);

        QLabel *help = new QLabel(
                    tr("Each pattern is a visual square layout. Enter a tile "
                       "sprite name in every cell, or a supported marker such "
                       "as $subbiome. The icon confirms a resolved tile."),
                    this);
        help->setWordWrap(true);
        layout->addWidget(help);

        QHBoxLayout *patternControls = new QHBoxLayout;
        patternControls->addWidget(new QLabel(tr("Pattern:"), this));
        mPatternCombo = new QComboBox(this);
        patternControls->addWidget(mPatternCombo);
        QPushButton *addPattern = new QPushButton(tr("Add"), this);
        QPushButton *removePattern = new QPushButton(tr("Remove"), this);
        patternControls->addWidget(addPattern);
        patternControls->addWidget(removePattern);
        patternControls->addSpacing(20);
        patternControls->addWidget(new QLabel(tr("Width:"), this));
        mWidth = new QSpinBox(this);
        mWidth->setRange(1, 8);
        patternControls->addWidget(mWidth);
        patternControls->addWidget(new QLabel(tr("Height:"), this));
        mHeight = new QSpinBox(this);
        mHeight->setRange(1, 8);
        patternControls->addWidget(mHeight);
        patternControls->addStretch(1);
        layout->addLayout(patternControls);

        mGrid = new QTableWidget(this);
        mGrid->setIconSize(QSize(48, 48));
        mGrid->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Stretch);
        mGrid->verticalHeader()->setSectionResizeMode(
                    QHeaderView::ResizeToContents);
        layout->addWidget(mGrid, 1);

        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        for (int index = 0; index < mPatterns.size(); ++index)
            mPatternCombo->addItem(tr("Pattern %1").arg(index + 1));

        connect(mPatternCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int index) {
            if (mLoading)
                return;
            storePattern();
            loadPattern(index);
        });
        connect(mWidth, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { resizeCurrentPattern(); });
        connect(mHeight, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { resizeCurrentPattern(); });
        connect(addPattern, &QPushButton::clicked, this, [this]() {
            storePattern();
            WorldGenPattern pattern;
            pattern.rows.append(QVector<QString>() << QString());
            mPatterns.append(pattern);
            mPatternCombo->addItem(
                        tr("Pattern %1").arg(mPatterns.size()));
            mPatternCombo->setCurrentIndex(mPatterns.size() - 1);
        });
        connect(removePattern, &QPushButton::clicked, this, [this]() {
            if (mPatterns.size() <= 1)
                return;
            const int index = mPatternCombo->currentIndex();
            mPatterns.removeAt(index);
            mPatternCombo->removeItem(index);
            for (int item = 0; item < mPatternCombo->count(); ++item)
                mPatternCombo->setItemText(
                            item, tr("Pattern %1").arg(item + 1));
            loadPattern(qMin(index, mPatterns.size() - 1));
        });
        connect(mGrid, &QTableWidget::itemChanged,
                this, [this](QTableWidgetItem *item) {
            if (!mLoading)
                updateTileIcon(item);
        });
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (validate())
                accept();
        });

        loadPattern(0);
    }

    WorldGenFeature feature()
    {
        storePattern();
        WorldGenFeature result;
        result.name = mName->text().trimmed();
        result.category = mCategory->currentText();
        result.patterns = mPatterns;
        result.minimumSize = 8;
        result.projectOwned = true;
        for (const WorldGenPattern &pattern : result.patterns)
            result.minimumSize = qMin(result.minimumSize,
                                      pattern.minimumDimension());
        return result;
    }

private:
    void updateTileIcon(QTableWidgetItem *item)
    {
        if (!item)
            return;
        const QString sprite = item->text().trimmed();
        Tile *tile = sprite.startsWith(QLatin1Char('$'))
                ? nullptr : tileForSprite(sprite);
        if (!tile || tile->image().isNull()) {
            item->setIcon(QIcon());
            return;
        }
        item->setIcon(QIcon(QPixmap::fromImage(tile->image()).scaled(
                                48, 48, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation)));
    }

    void storePattern()
    {
        if (mLoading || mCurrentPattern < 0
                || mCurrentPattern >= mPatterns.size())
            return;
        WorldGenPattern pattern;
        for (int row = 0; row < mGrid->rowCount(); ++row) {
            QVector<QString> values;
            for (int column = 0; column < mGrid->columnCount(); ++column) {
                QTableWidgetItem *item = mGrid->item(row, column);
                values.append(item ? item->text().trimmed() : QString());
            }
            pattern.rows.append(values);
        }
        mPatterns[mCurrentPattern] = pattern;
    }

    void loadPattern(int index)
    {
        if (index < 0 || index >= mPatterns.size())
            return;
        mLoading = true;
        mCurrentPattern = index;
        mPatternCombo->setCurrentIndex(index);
        const WorldGenPattern pattern = mPatterns.at(index);
        mWidth->setValue(qMax(1, pattern.width()));
        mHeight->setValue(qMax(1, pattern.height()));
        mGrid->setRowCount(qMax(1, pattern.height()));
        mGrid->setColumnCount(qMax(1, pattern.width()));
        for (int row = 0; row < mGrid->rowCount(); ++row) {
            for (int column = 0; column < mGrid->columnCount(); ++column) {
                const QString value =
                        row < pattern.rows.size()
                        && column < pattern.rows.at(row).size()
                        ? pattern.rows.at(row).at(column) : QString();
                QTableWidgetItem *item = new QTableWidgetItem(value);
                mGrid->setItem(row, column, item);
                updateTileIcon(item);
            }
        }
        mLoading = false;
    }

    void resizeCurrentPattern()
    {
        if (mLoading || mCurrentPattern < 0)
            return;
        storePattern();
        WorldGenPattern resized;
        const WorldGenPattern old = mPatterns.at(mCurrentPattern);
        for (int row = 0; row < mHeight->value(); ++row) {
            QVector<QString> values;
            for (int column = 0; column < mWidth->value(); ++column) {
                values.append(row < old.rows.size()
                              && column < old.rows.at(row).size()
                              ? old.rows.at(row).at(column) : QString());
            }
            resized.rows.append(values);
        }
        mPatterns[mCurrentPattern] = resized;
        loadPattern(mCurrentPattern);
    }

    bool validate()
    {
        storePattern();
        const QString name = mName->text().trimmed();
        if (!validDefinitionName(name)) {
            QMessageBox::warning(
                        this, tr("Invalid feature name"),
                        tr("Use a Lua-safe name containing only letters, "
                           "numbers, and underscores, beginning with a "
                           "letter or underscore."));
            return false;
        }
        for (int patternIndex = 0; patternIndex < mPatterns.size();
             ++patternIndex) {
            const WorldGenPattern &pattern = mPatterns.at(patternIndex);
            for (int row = 0; row < pattern.rows.size(); ++row) {
                for (int column = 0;
                     column < pattern.rows.at(row).size(); ++column) {
                    if (pattern.rows.at(row).at(column).trimmed().isEmpty()) {
                        QMessageBox::warning(
                                    this, tr("Incomplete pattern"),
                                    tr("Pattern %1 contains an empty cell at "
                                       "%2,%3.")
                                    .arg(patternIndex + 1)
                                    .arg(column)
                                    .arg(row));
                        return false;
                    }
                }
            }
        }
        return true;
    }

    QLineEdit *mName = nullptr;
    QComboBox *mCategory = nullptr;
    QComboBox *mPatternCombo = nullptr;
    QSpinBox *mWidth = nullptr;
    QSpinBox *mHeight = nullptr;
    QTableWidget *mGrid = nullptr;
    QList<WorldGenPattern> mPatterns;
    int mCurrentPattern = -1;
    bool mLoading = false;
};

int ensurePrefabTile(WorldGenPrefab *prefab, const QString &sprite)
{
    int index = prefab->tiles.indexOf(sprite);
    if (index < 0) {
        prefab->tiles.append(sprite);
        index = prefab->tiles.size() - 1;
    }
    return index + 1;
}

QString spriteForPrefabRef(const WorldGenPrefab &prefab, int ref)
{
    return ref > 0 && ref <= prefab.tiles.size()
            ? prefab.tiles.at(ref - 1) : QString();
}

bool mapToPrefab(Map *map, const QString &name,
                 WorldGenPrefab *prefab, QStringList *warnings,
                 QString *error)
{
    if (!map || map->width() <= 0 || map->height() <= 0) {
        if (error)
            *error = QObject::tr("The source map is empty.");
        return false;
    }
    if (map->width() > 256 || map->height() > 256) {
        if (error) {
            *error = QObject::tr(
                        "The source is %1 x %2. The prefab editor's explicit "
                        "safety limit is one Build 42 cell (256 x 256).")
                    .arg(map->width()).arg(map->height());
        }
        return false;
    }

    WorldGenPrefab result;
    result.name = name;
    result.width = map->width();
    result.height = map->height();
    result.projectOwned = true;
    for (const QString &category : PrefabCategories) {
        result.schematic.insert(
                    category, QVector<int>(result.width * result.height, 0));
    }

    QList<TileLayer *> floorLayers;
    QList<TileLayer *> otherLayers;
    int ignoredObjectLayers = 0;
    for (Layer *layer : map->layers()) {
        if (!layer || layer->isEmpty())
            continue;
        if (!layer->isTileLayer()) {
            ++ignoredObjectLayers;
            continue;
        }
        TileLayer *tileLayer = layer->asTileLayer();
        if (tileLayer->level() != 0) {
            if (error) {
                *error = QObject::tr(
                            "Layer '%1' contains level %2 tiles. WorldGen "
                            "prefabs are z=0-only; the conversion was stopped "
                            "instead of discarding an upper/lower level.")
                        .arg(layer->nameWithPrefix())
                        .arg(tileLayer->level());
            }
            return false;
        }
        QString layerName = tileLayer->name();
        if (layerName.startsWith(QStringLiteral("0_")))
            layerName.remove(0, 2);
        if (layerName.compare(QStringLiteral("Floor"),
                              Qt::CaseInsensitive) == 0) {
            floorLayers.append(tileLayer);
        } else {
            otherLayers.append(tileLayer);
        }
    }

    if (ignoredObjectLayers && warnings) {
        warnings->append(QObject::tr(
            "%1 non-tile/object layer(s) are not representable and were "
            "ignored.").arg(ignoredObjectLayers));
    }

    int concreteTiles = 0;
    auto tileAt = [](TileLayer *layer, int x, int y) -> Tile * {
        const int localX = x - layer->x();
        const int localY = y - layer->y();
        if (localX < 0 || localY < 0
                || localX >= layer->width()
                || localY >= layer->height()) {
            return nullptr;
        }
        return layer->cellAt(localX, localY).tile;
    };
    auto spriteName = [](Tile *tile) -> QString {
        return tile && tile->tileset()
                ? tile->tileset()->name() + QLatin1Char('_')
                  + QString::number(tile->id())
                : QString();
    };

    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            Tile *floorTile = nullptr;
            QString floorLayerName;
            for (TileLayer *layer : floorLayers) {
                Tile *tile = tileAt(layer, x, y);
                if (!tile)
                    continue;
                if (floorTile) {
                    if (error) {
                        *error = QObject::tr(
                            "Square %1,%2 contains more than one explicit "
                            "Floor tile ('%3' and '%4'). A prefab has only "
                            "one Floor slot.")
                                .arg(x).arg(y)
                                .arg(floorLayerName,
                                     layer->nameWithPrefix());
                    }
                    return false;
                }
                floorTile = tile;
                floorLayerName = layer->nameWithPrefix();
            }

            QList<Tile *> stack;
            QStringList stackLayers;
            for (TileLayer *layer : otherLayers) {
                Tile *tile = tileAt(layer, x, y);
                if (tile) {
                    stack.append(tile);
                    stackLayers.append(layer->nameWithPrefix());
                }
            }
            if (stack.size() > 3) {
                if (error) {
                    *error = QObject::tr(
                        "Square %1,%2 needs %3 non-floor tile layers (%4). "
                        "A WorldGen prefab only has three non-floor slots: "
                        "FloorFurniture, FloorOverlay and Furniture.")
                            .arg(x).arg(y).arg(stack.size())
                            .arg(stackLayers.join(QStringLiteral(", ")));
                }
                return false;
            }

            const int cell = y * result.width + x;
            if (floorTile) {
                const QString sprite = spriteName(floorTile);
                result.schematic[QStringLiteral("Floor")][cell] =
                        ensurePrefabTile(&result, sprite);
                ++concreteTiles;
            }
            for (int slot = 0; slot < stack.size(); ++slot) {
                const QString category = PrefabCategories.at(slot + 1);
                result.schematic[category][cell] =
                        ensurePrefabTile(&result, spriteName(stack.at(slot)));
                ++concreteTiles;
            }
        }
    }
    if (!concreteTiles) {
        if (error)
            *error = QObject::tr("No z=0 tile could be imported.");
        return false;
    }
    if (warnings) {
        warnings->append(QObject::tr(
            "Conversion keeps z=0 tile sprites only. Rooms, objects, walls "
            "as geometry, roofs, properties and building behavior are not "
            "part of the WorldGen prefab format."));
    }
    *prefab = result;
    return true;
}

bool importPrefabSource(const QString &fileName, WorldGenPrefab *prefab,
                        QStringList *warnings, QString *error)
{
    const QFileInfo info(fileName);
    QString name = info.completeBaseName();
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")),
                 QStringLiteral("_"));
    if (name.isEmpty() || name.at(0).isDigit())
        name.prepend(QStringLiteral("prefab_"));

    if (info.suffix().compare(QStringLiteral("tmx"),
                              Qt::CaseInsensitive) == 0) {
        qInfo() << "WorldGen prefab import: reading TMX" << fileName;
        MapReader reader;
        QScopedPointer<Map> map(reader.readMap(fileName));
        if (!map) {
            if (error)
                *error = reader.errorString();
            return false;
        }
        qInfo() << "WorldGen prefab import: TMX loaded"
                << map->width() << "x" << map->height()
                << "layers" << map->layerCount();
        const bool converted = mapToPrefab(
                    map.data(), name, prefab, warnings, error);
        qInfo() << "WorldGen prefab import: TMX conversion"
                << (converted ? "passed" : "failed");
        return converted;
    }

    if (info.suffix().compare(QStringLiteral("tbx"),
                              Qt::CaseInsensitive) == 0) {
        BuildingEditor::BuildingReader reader;
        QScopedPointer<BuildingEditor::Building> building(
                    reader.read(fileName));
        if (!building) {
            if (error)
                *error = reader.errorString();
            return false;
        }
        if (building->floorCount() != 1) {
            if (error) {
                *error = QObject::tr(
                    "The TBX has %1 floors. WorldGen prefabs only apply at "
                    "z=0, so this conversion accepts exactly one floor.")
                        .arg(building->floorCount());
            }
            return false;
        }
        BuildingEditor::BuildingMap::loadNeededTilesets(building.data());
        BuildingEditor::BuildingMap buildingMap(building.data());
        QScopedPointer<Map> map(buildingMap.mergedMap());
        return mapToPrefab(map.data(), name, prefab, warnings, error);
    }

    if (error)
        *error = QObject::tr("Choose a .tmx or .tbx source file.");
    return false;
}

class WorldGenPrefabPreview : public QWidget
{
public:
    explicit WorldGenPrefabPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(720, 460);
    }

    void setPrefab(const WorldGenPrefab &prefab)
    {
        mPrefab = prefab;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        if (mPrefab.width <= 0 || mPrefab.height <= 0)
            return;

        const qreal baseWidth = (mPrefab.width + mPrefab.height) * 32.0;
        const qreal baseHeight =
                (mPrefab.width + mPrefab.height) * 16.0 + 160.0;
        const qreal scale = qMin((width() - 30.0) / baseWidth,
                                 (height() - 30.0) / baseHeight);
        const qreal drawScale = qBound(0.08, scale, 1.25);
        const QPointF origin(width() / 2.0,
                             20.0 + 80.0 * drawScale);
        auto point = [origin, drawScale](qreal x, qreal y) {
            return QPointF(origin.x() + (x - y) * 32.0 * drawScale,
                           origin.y() + (x + y) * 16.0 * drawScale);
        };

        QPen gridPen(QColor(95, 105, 115, 115));
        gridPen.setWidthF(qMax(0.7, drawScale));
        painter.setPen(gridPen);
        for (int x = 0; x <= mPrefab.width; ++x)
            painter.drawLine(point(x, 0), point(x, mPrefab.height));
        for (int y = 0; y <= mPrefab.height; ++y)
            painter.drawLine(point(0, y), point(mPrefab.width, y));

        // Paint by isometric depth, not by rows.  This is required for tall
        // and XL/XXL sprites whose images extend over neighboring anchors.
        for (int depth = 0;
             depth <= mPrefab.width + mPrefab.height - 2;
             ++depth) {
            const int firstX = qMax(0, depth - (mPrefab.height - 1));
            const int lastX = qMin(mPrefab.width - 1, depth);
            for (int x = firstX; x <= lastX; ++x) {
                const int y = depth - x;
                for (const QString &category : PrefabCategories) {
                    const int ref = mPrefab.tileRef(category, x, y);
                    const QString sprite =
                            spriteForPrefabRef(mPrefab, ref);
                    Tile *tile = tileForSprite(sprite);
                    if (!tile || tile->image().isNull())
                        continue;
                    const QImage image = tile->image();
                    const QSizeF size(image.width() * drawScale,
                                      image.height() * drawScale);
                    const QPointF anchor = point(x, y + 1);
                    const QRectF target(
                                anchor.x() - size.width() / 2.0,
                                anchor.y() - size.height()
                                + 16.0 * drawScale,
                                size.width(), size.height());
                    painter.drawImage(target, image);
                }
            }
        }

        // Chunk limits are an inspection overlay.  Draw them last so a
        // complete Floor does not hide the very boundaries being checked.
        QPen chunkPen(QColor(238, 164, 70, 220));
        chunkPen.setWidthF(qMax(1.2, 2.0 * drawScale));
        painter.setPen(chunkPen);
        for (int x = 0; x <= mPrefab.width; x += ChunkSize)
            painter.drawLine(point(x, 0), point(x, mPrefab.height));
        for (int y = 0; y <= mPrefab.height; y += ChunkSize)
            painter.drawLine(point(0, y), point(mPrefab.width, y));

        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
                    QRect(10, height() - 28, width() - 20, 20),
                    Qt::AlignCenter,
                    tr("%1 x %2 squares — orange lines: 8 x 8 chunks")
                    .arg(mPrefab.width).arg(mPrefab.height));
    }

private:
    WorldGenPrefab mPrefab;
};

class WorldGenPrefabEditorDialog : public QDialog
{
public:
    WorldGenPrefabEditorDialog(const WorldGenPrefab &initial,
                               bool editingProjectDefinition,
                               QWidget *parent)
        : QDialog(parent)
        , mPrefab(initial)
    {
        setWindowTitle(editingProjectDefinition
                       ? tr("Edit Project WorldGen Prefab")
                       : tr("Create WorldGen Prefab"));
        resize(1280, 820);
        if (mPrefab.width < 1)
            mPrefab.width = 1;
        if (mPrefab.height < 1)
            mPrefab.height = 1;
        for (const QString &category : PrefabCategories) {
            mPrefab.schematic[category].resize(
                        mPrefab.width * mPrefab.height);
        }

        QVBoxLayout *layout = new QVBoxLayout(this);
        QFormLayout *identity = new QFormLayout;
        mName = new QLineEdit(mPrefab.name, this);
        mName->setEnabled(!editingProjectDefinition);
        identity->addRow(tr("Registry name:"), mName);
        QHBoxLayout *dimensions = new QHBoxLayout;
        mWidth = new QSpinBox(this);
        mWidth->setRange(1, 256);
        mWidth->setValue(mPrefab.width);
        mHeight = new QSpinBox(this);
        mHeight->setRange(1, 256);
        mHeight->setValue(mPrefab.height);
        mZombies = new QDoubleSpinBox(this);
        mZombies->setRange(0.0, 1.0);
        mZombies->setDecimals(6);
        mZombies->setSingleStep(0.001);
        mZombies->setValue(mPrefab.zombies);
        dimensions->addWidget(new QLabel(tr("Width:"), this));
        dimensions->addWidget(mWidth);
        dimensions->addWidget(new QLabel(tr("Height:"), this));
        dimensions->addWidget(mHeight);
        dimensions->addSpacing(18);
        dimensions->addWidget(new QLabel(tr("Zombie chance:"), this));
        dimensions->addWidget(mZombies);
        dimensions->addStretch(1);
        identity->addRow(tr("Runtime dimensions:"), dimensions);
        layout->addLayout(identity);

        QLabel *help = new QLabel(
            tr("A Build 42 WorldGen prefab is a z=0 static-module tile "
               "schematic, not a building and not a biome feature. Paint up "
               "to one sprite in each of the four engine slots. Zero Floor "
               "cells fall back to the active biome ground."),
            this);
        help->setWordWrap(true);
        layout->addWidget(help);

        QTabWidget *tabs = new QTabWidget(this);
        QWidget *editPage = new QWidget(tabs);
        QHBoxLayout *editLayout = new QHBoxLayout(editPage);
        QVBoxLayout *gridLayout = new QVBoxLayout;
        QHBoxLayout *layerControls = new QHBoxLayout;
        layerControls->addWidget(new QLabel(tr("Engine slot:"), editPage));
        mCategory = new QComboBox(editPage);
        mCategory->addItems(PrefabCategories);
        layerControls->addWidget(mCategory);
        mBrush = new QLineEdit(editPage);
        mBrush->setPlaceholderText(tr("tile_sheet_name_index"));
        layerControls->addWidget(mBrush, 1);
        QPushButton *paint = new QPushButton(tr("Paint selection"), editPage);
        QPushButton *erase = new QPushButton(tr("Erase selection"), editPage);
        layerControls->addWidget(paint);
        layerControls->addWidget(erase);
        gridLayout->addLayout(layerControls);
        mGrid = new QTableWidget(editPage);
        mGrid->setSelectionMode(QAbstractItemView::ExtendedSelection);
        mGrid->setSelectionBehavior(QAbstractItemView::SelectItems);
        mGrid->setIconSize(QSize(54, 54));
        mGrid->horizontalHeader()->setDefaultSectionSize(112);
        mGrid->verticalHeader()->setDefaultSectionSize(64);
        gridLayout->addWidget(mGrid, 1);
        editLayout->addLayout(gridLayout, 1);

        QVBoxLayout *paletteLayout = new QVBoxLayout;
        paletteLayout->addWidget(new QLabel(tr("Tiles palette:"), editPage));
        mTileset = new QComboBox(editPage);
        mTileset->addItems(TileMetaInfoMgr::instance()->tilesetNames());
        paletteLayout->addWidget(mTileset);
        mPalette = new QListWidget(editPage);
        mPalette->setViewMode(QListView::IconMode);
        mPalette->setIconSize(QSize(64, 96));
        mPalette->setGridSize(QSize(118, 126));
        mPalette->setResizeMode(QListView::Adjust);
        mPalette->setMinimumWidth(380);
        paletteLayout->addWidget(mPalette, 1);
        editLayout->addLayout(paletteLayout);
        tabs->addTab(editPage, tr("Paint schematic"));

        mPreview = new WorldGenPrefabPreview(tabs);
        tabs->addTab(mPreview, tr("Isometric preview"));
        layout->addWidget(tabs, 1);

        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        connect(mCategory,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() { loadGrid(); });
        connect(mTileset,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() { loadPalette(); });
        connect(mPalette, &QListWidget::itemClicked,
                this, [this](QListWidgetItem *item) {
            if (item)
                mBrush->setText(item->data(Qt::UserRole).toString());
        });
        connect(mPalette, &QListWidget::itemDoubleClicked,
                this, [this](QListWidgetItem *item) {
            if (item) {
                mBrush->setText(item->data(Qt::UserRole).toString());
                paintSelection(false);
            }
        });
        connect(paint, &QPushButton::clicked,
                this, [this]() { paintSelection(false); });
        connect(erase, &QPushButton::clicked,
                this, [this]() { paintSelection(true); });
        connect(mGrid, &QTableWidget::itemChanged,
                this, [this](QTableWidgetItem *item) {
            if (!mLoading)
                updateFromItem(item);
        });
        connect(mWidth, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { resizePrefab(); });
        connect(mHeight, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { resizePrefab(); });
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted,
                this, [this]() {
            if (validate())
                accept();
        });

        loadGrid();
        loadPalette();
        updatePreview();
    }

    WorldGenPrefab prefab()
    {
        mPrefab.name = mName->text().trimmed();
        mPrefab.zombies = mZombies->value();
        mPrefab.projectOwned = true;
        return mPrefab;
    }

private:
    void resizePrefab()
    {
        if (mLoading)
            return;
        const int oldWidth = mPrefab.width;
        const int oldHeight = mPrefab.height;
        const int newWidth = mWidth->value();
        const int newHeight = mHeight->value();
        if (oldWidth == newWidth && oldHeight == newHeight)
            return;
        for (const QString &category : PrefabCategories) {
            const QVector<int> old = mPrefab.schematic.value(category);
            QVector<int> resized(newWidth * newHeight, 0);
            for (int y = 0; y < qMin(oldHeight, newHeight); ++y) {
                for (int x = 0; x < qMin(oldWidth, newWidth); ++x)
                    resized[y * newWidth + x] = old.at(y * oldWidth + x);
            }
            mPrefab.schematic[category] = resized;
        }
        mPrefab.width = newWidth;
        mPrefab.height = newHeight;
        loadGrid();
        updatePreview();
    }

    void loadGrid()
    {
        mLoading = true;
        mGrid->clear();
        mGrid->setRowCount(mPrefab.height);
        mGrid->setColumnCount(mPrefab.width);
        const QString category = mCategory->currentText();
        const QVector<int> refs = mPrefab.schematic.value(category);
        for (int y = 0; y < mPrefab.height; ++y) {
            for (int x = 0; x < mPrefab.width; ++x) {
                const int ref = refs.value(y * mPrefab.width + x);
                if (!ref)
                    continue;
                const QString sprite = spriteForPrefabRef(mPrefab, ref);
                QTableWidgetItem *item = new QTableWidgetItem(sprite);
                setItemIcon(item, sprite);
                mGrid->setItem(y, x, item);
            }
        }
        mLoading = false;
    }

    void setItemIcon(QTableWidgetItem *item, const QString &sprite)
    {
        Tile *tile = tileForSprite(sprite);
        if (!tile || tile->image().isNull()) {
            item->setIcon(QIcon());
            item->setBackground(QColor(130, 40, 40, 80));
            return;
        }
        item->setBackground(QBrush());
        item->setIcon(QIcon(QPixmap::fromImage(tile->image()).scaled(
                                54, 54, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation)));
    }

    void updateFromItem(QTableWidgetItem *item)
    {
        if (!item)
            return;
        const QString sprite = item->text().trimmed();
        const QString category = mCategory->currentText();
        QVector<int> &refs = mPrefab.schematic[category];
        const int index = item->row() * mPrefab.width + item->column();
        refs[index] = sprite.isEmpty()
                ? 0 : ensurePrefabTile(&mPrefab, sprite);
        setItemIcon(item, sprite);
        updatePreview();
    }

    void paintSelection(bool erase)
    {
        const QString sprite = mBrush->text().trimmed();
        if (!erase && sprite.isEmpty()) {
            QMessageBox::warning(this, tr("No tile selected"),
                                 tr("Choose or enter a tile sprite first."));
            return;
        }
        QList<QTableWidgetItem *> selected;
        for (const QTableWidgetSelectionRange &range
             : mGrid->selectedRanges()) {
            for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
                for (int column = range.leftColumn();
                     column <= range.rightColumn(); ++column) {
                    QTableWidgetItem *item = mGrid->item(row, column);
                    if (!item) {
                        item = new QTableWidgetItem;
                        mGrid->setItem(row, column, item);
                    }
                    selected.append(item);
                }
            }
        }
        if (selected.isEmpty() && mGrid->currentRow() >= 0) {
            QTableWidgetItem *item = mGrid->item(
                        mGrid->currentRow(), mGrid->currentColumn());
            if (!item) {
                item = new QTableWidgetItem;
                mGrid->setItem(mGrid->currentRow(),
                               mGrid->currentColumn(), item);
            }
            selected.append(item);
        }
        if (selected.isEmpty()) {
            QMessageBox::information(this, tr("No squares selected"),
                                     tr("Select one or more grid squares."));
            return;
        }
        mLoading = true;
        const QString category = mCategory->currentText();
        QVector<int> &refs = mPrefab.schematic[category];
        const int ref = erase ? 0 : ensurePrefabTile(&mPrefab, sprite);
        for (QTableWidgetItem *item : selected) {
            const int index = item->row() * mPrefab.width + item->column();
            refs[index] = ref;
            item->setText(erase ? QString() : sprite);
            setItemIcon(item, erase ? QString() : sprite);
        }
        mLoading = false;
        updatePreview();
    }

    void loadPalette()
    {
        mPalette->clear();
        Tileset *tileset =
                TileMetaInfoMgr::instance()->tileset(mTileset->currentText());
        if (!tileset)
            return;
        QList<Tileset *> required;
        required.append(tileset);
        TileMetaInfoMgr::instance()->loadTilesets(required);
        TilesetManager::instance()->waitForTilesets(required, this);
        for (int id = 0; id < tileset->tileCount(); ++id) {
            Tile *tile = tileset->tileAt(id);
            if (!tile)
                continue;
            const QString sprite =
                    tileset->name() + QLatin1Char('_') + QString::number(id);
            QIcon icon;
            if (!tile->image().isNull()) {
                icon = QIcon(QPixmap::fromImage(tile->image()).scaled(
                                 64, 96, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation));
            }
            QListWidgetItem *item = new QListWidgetItem(
                        icon, QString::number(id), mPalette);
            item->setData(Qt::UserRole, sprite);
            item->setToolTip(sprite);
        }
    }

    void updatePreview()
    {
        mPreview->setPrefab(mPrefab);
    }

    bool validate()
    {
        const QString name = mName->text().trimmed();
        if (!validDefinitionName(name)) {
            QMessageBox::warning(
                        this, tr("Invalid prefab name"),
                        tr("Use a Lua-safe name containing only letters, "
                           "numbers and underscores."));
            return false;
        }
        QSet<QString> missing;
        for (const QString &category : PrefabCategories) {
            const QVector<int> refs = mPrefab.schematic.value(category);
            for (int ref : refs) {
                if (ref < 0 || ref > mPrefab.tiles.size()) {
                    QMessageBox::critical(
                                this, tr("Invalid tile reference"),
                                tr("%1 contains tile reference %2, but the "
                                   "tiles table has %3 entries.")
                                .arg(category).arg(ref)
                                .arg(mPrefab.tiles.size()));
                    return false;
                }
                const QString sprite = spriteForPrefabRef(mPrefab, ref);
                if (!sprite.isEmpty() && !tileForSprite(sprite))
                    missing.insert(sprite);
            }
        }
        if (!missing.isEmpty()) {
            QMessageBox::warning(
                        this, tr("Unresolved prefab tiles"),
                        tr("These sprite names do not resolve against the "
                           "configured Tiles catalogue:\n\n%1")
                        .arg(QStringList(missing.values()).join(
                                 QStringLiteral("\n"))));
            return false;
        }
        return true;
    }

    WorldGenPrefab mPrefab;
    QLineEdit *mName = nullptr;
    QSpinBox *mWidth = nullptr;
    QSpinBox *mHeight = nullptr;
    QDoubleSpinBox *mZombies = nullptr;
    QComboBox *mCategory = nullptr;
    QLineEdit *mBrush = nullptr;
    QTableWidget *mGrid = nullptr;
    QComboBox *mTileset = nullptr;
    QListWidget *mPalette = nullptr;
    WorldGenPrefabPreview *mPreview = nullptr;
    bool mLoading = false;
};

class WorldGenBiomeEditorDialog : public QDialog
{
public:
    WorldGenBiomeEditorDialog(const WorldGenDefinitions &definitions,
                              const WorldGenBiome &initial,
                              bool editingProjectDefinition,
                              QWidget *parent)
        : QDialog(parent)
        , mDefinitions(definitions)
    {
        setWindowTitle(editingProjectDefinition
                       ? tr("Edit Project Biome")
                       : tr("Create Project Biome"));
        resize(900, 720);
        QVBoxLayout *layout = new QVBoxLayout(this);

        QLabel *help = new QLabel(
                    tr("Only values declared here are written to the project "
                       "overlay. Advanced placement, protection, replacement, "
                       "and subbiome rules continue to come from the parent."),
                    this);
        help->setWordWrap(true);
        layout->addWidget(help);

        QFormLayout *identity = new QFormLayout;
        mName = new QLineEdit(initial.name, this);
        mName->setEnabled(!editingProjectDefinition);
        identity->addRow(tr("Name:"), mName);
        mRegistry = new QComboBox(this);
        mRegistry->addItem(tr("Procedural biome"), false);
        mRegistry->addItem(tr("Map biome"), true);
        mRegistry->setCurrentIndex(initial.mapBiome ? 1 : 0);
        mRegistry->setEnabled(!editingProjectDefinition);
        identity->addRow(tr("Registry:"), mRegistry);
        mParent = new QComboBox(this);
        mParent->setEditable(true);
        populateParents(initial.mapBiome);
        mParent->setCurrentText(initial.parent);
        identity->addRow(tr("Parent:"), mParent);
        mGenerate = new QCheckBox(
                    tr("Selectable/generating biome"), this);
        mGenerate->setChecked(initial.generate);
        identity->addRow(tr("Generate:"), mGenerate);
        layout->addLayout(identity);

        connect(mRegistry,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
            const QString previous = mParent->currentText();
            populateParents(mRegistry->currentData().toBool());
            mParent->setCurrentText(previous);
        });

        QGroupBox *parameters = new QGroupBox(tr("Declared parameters"), this);
        QFormLayout *parameterLayout = new QFormLayout(parameters);
        const QStringList fields = {
            QStringLiteral("landscape"),
            QStringLiteral("plant"),
            QStringLiteral("bush"),
            QStringLiteral("temperature"),
            QStringLiteral("hygrometry"),
            QStringLiteral("ore_level")
        };
        for (const QString &field : fields) {
            QLineEdit *edit = new QLineEdit(
                        initial.declaredParameters.value(field).join(
                            QStringLiteral(", ")),
                        parameters);
            edit->setPlaceholderText(tr("Comma-separated values; empty "
                                        "means inherit/omit"));
            mParameters.insert(field, edit);
            parameterLayout->addRow(field + QLatin1Char(':'), edit);
        }
        QHBoxLayout *zombieLayout = new QHBoxLayout;
        mWriteZombies = new QCheckBox(tr("Write"), parameters);
        mWriteZombies->setChecked(initial.declaredParameters.contains(
                                      QStringLiteral("zombies")));
        mZombies = new QDoubleSpinBox(parameters);
        mZombies->setDecimals(8);
        mZombies->setRange(0.0, 1000000.0);
        mZombies->setSingleStep(0.001);
        mZombies->setValue(
                    initial.declaredParameters.value(
                        QStringLiteral("zombies")).value(
                        0, QStringLiteral("0")).toDouble());
        zombieLayout->addWidget(mWriteZombies);
        zombieLayout->addWidget(mZombies, 1);
        parameterLayout->addRow(tr("zombies:"), zombieLayout);
        layout->addWidget(parameters);

        QGroupBox *features = new QGroupBox(
                    tr("Declared biome-feature weights"), this);
        QVBoxLayout *featureLayout = new QVBoxLayout(features);
        mWeights = new QTableWidget(features);
        mWeights->setColumnCount(3);
        mWeights->setHorizontalHeaderLabels(
                    QStringList() << tr("Category")
                                  << tr("Feature")
                                  << tr("Probability"));
        mWeights->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::ResizeToContents);
        mWeights->horizontalHeader()->setSectionResizeMode(
                    1, QHeaderView::Stretch);
        mWeights->horizontalHeader()->setSectionResizeMode(
                    2, QHeaderView::ResizeToContents);
        mWeights->setSelectionBehavior(QAbstractItemView::SelectRows);
        featureLayout->addWidget(mWeights, 1);
        QHBoxLayout *weightButtons = new QHBoxLayout;
        QPushButton *addWeight = new QPushButton(tr("Add weight"), features);
        QPushButton *removeWeight =
                new QPushButton(tr("Remove selected"), features);
        weightButtons->addWidget(addWeight);
        weightButtons->addWidget(removeWeight);
        weightButtons->addStretch(1);
        featureLayout->addLayout(weightButtons);
        layout->addWidget(features, 1);

        for (const QString &category : FeatureCategories) {
            for (const WeightedFeature &weighted
                 : initial.declaredFeatures.value(category)) {
                addWeightRow(category, weighted.featureName,
                             weighted.probability);
            }
        }
        connect(addWeight, &QPushButton::clicked, this, [this]() {
            addWeightRow(QStringLiteral("GROUND"), QString(), 0.1);
        });
        connect(removeWeight, &QPushButton::clicked, this, [this]() {
            const int row = mWeights->currentRow();
            if (row >= 0)
                mWeights->removeRow(row);
        });

        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (validate())
                accept();
        });
    }

    WorldGenBiome biome() const
    {
        WorldGenBiome result;
        result.name = mName->text().trimmed();
        result.mapBiome = mRegistry->currentData().toBool();
        result.parent = mParent->currentText().trimmed();
        result.generate = mGenerate->isChecked();
        result.hasGenerate = true;
        result.projectOwned = true;
        for (auto iterator = mParameters.constBegin();
             iterator != mParameters.constEnd(); ++iterator) {
            QStringList values;
            for (const QString &part
                 : iterator.value()->text().split(QLatin1Char(','),
                                                  Qt::SkipEmptyParts)) {
                const QString value = part.trimmed();
                if (!value.isEmpty())
                    values.append(value);
            }
            if (!values.isEmpty())
                result.declaredParameters.insert(iterator.key(), values);
        }
        if (mWriteZombies->isChecked()) {
            result.declaredParameters.insert(
                        QStringLiteral("zombies"),
                        QStringList()
                        << QString::number(mZombies->value(), 'g', 12));
        }
        for (int row = 0; row < mWeights->rowCount(); ++row) {
            QComboBox *category =
                    qobject_cast<QComboBox *>(mWeights->cellWidget(row, 0));
            QComboBox *feature =
                    qobject_cast<QComboBox *>(mWeights->cellWidget(row, 1));
            QDoubleSpinBox *probability =
                    qobject_cast<QDoubleSpinBox *>(
                        mWeights->cellWidget(row, 2));
            if (!category || !feature || !probability
                    || feature->currentText().trimmed().isEmpty())
                continue;
            WeightedFeature weighted;
            weighted.featureName = feature->currentText().trimmed();
            weighted.probability = probability->value();
            weighted.probabilityText =
                    QString::number(weighted.probability, 'g', 12);
            result.declaredFeatures[category->currentText()].append(weighted);
        }
        result.features = result.declaredFeatures;
        result.parameters = result.declaredParameters;
        return result;
    }

private:
    void populateParents(bool mapBiome)
    {
        mParent->clear();
        mParent->addItem(QString());
        const QMap<QString, WorldGenBiome> &biomes =
                mapBiome ? mDefinitions.mapBiomes
                         : mDefinitions.proceduralBiomes;
        mParent->addItems(biomes.keys());
    }

    QStringList featureNames(const QString &category) const
    {
        QStringList result;
        for (const WorldGenFeature &feature : mDefinitions.features) {
            if (feature.category == category)
                result.append(feature.name);
        }
        result.sort(Qt::CaseInsensitive);
        return result;
    }

    void populateFeatureCombo(QComboBox *combo, const QString &category,
                              const QString &preferred)
    {
        combo->clear();
        combo->addItems(featureNames(category));
        combo->setEditable(true);
        combo->setCurrentText(preferred);
    }

    void addWeightRow(const QString &categoryName,
                      const QString &featureName, double probability)
    {
        const int row = mWeights->rowCount();
        mWeights->insertRow(row);
        QComboBox *category = new QComboBox(mWeights);
        category->addItems(FeatureCategories);
        category->setCurrentText(categoryName);
        mWeights->setCellWidget(row, 0, category);
        QComboBox *feature = new QComboBox(mWeights);
        populateFeatureCombo(feature, categoryName, featureName);
        mWeights->setCellWidget(row, 1, feature);
        QDoubleSpinBox *probabilitySpin = new QDoubleSpinBox(mWeights);
        probabilitySpin->setDecimals(8);
        probabilitySpin->setRange(0.0, 1000000.0);
        probabilitySpin->setSingleStep(0.01);
        probabilitySpin->setValue(probability);
        mWeights->setCellWidget(row, 2, probabilitySpin);
        connect(category,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, category, feature](int) {
            populateFeatureCombo(feature, category->currentText(), QString());
        });
    }

    bool validate() const
    {
        const QString name = mName->text().trimmed();
        if (!validDefinitionName(name)) {
            QMessageBox::warning(
                        const_cast<WorldGenBiomeEditorDialog *>(this),
                        tr("Invalid biome name"),
                        tr("Use a Lua-safe name containing only letters, "
                           "numbers, and underscores, beginning with a "
                           "letter or underscore."));
            return false;
        }
        if (mParent->currentText().trimmed() == name) {
            QMessageBox::warning(
                        const_cast<WorldGenBiomeEditorDialog *>(this),
                        tr("Invalid parent"),
                        tr("A biome cannot inherit from itself."));
            return false;
        }
        const QString parent = mParent->currentText().trimmed();
        const bool mapBiome = mRegistry->currentData().toBool();
        const QMap<QString, WorldGenBiome> &parentRegistry =
                mapBiome ? mDefinitions.mapBiomes
                         : mDefinitions.proceduralBiomes;
        if (!parent.isEmpty() && !parentRegistry.contains(parent)) {
            QMessageBox::warning(
                        const_cast<WorldGenBiomeEditorDialog *>(this),
                        tr("Unknown parent"),
                        tr("The selected parent '%1' does not exist in the "
                           "same biome registry.").arg(parent));
            return false;
        }
        for (int row = 0; row < mWeights->rowCount(); ++row) {
            QComboBox *category =
                    qobject_cast<QComboBox *>(mWeights->cellWidget(row, 0));
            QComboBox *feature =
                    qobject_cast<QComboBox *>(mWeights->cellWidget(row, 1));
            if (!category || !feature)
                continue;
            const QString featureName = feature->currentText().trimmed();
            const auto iterator =
                    mDefinitions.features.constFind(featureName);
            if (featureName.isEmpty()
                    || iterator == mDefinitions.features.constEnd()
                    || iterator->category != category->currentText()) {
                QMessageBox::warning(
                            const_cast<WorldGenBiomeEditorDialog *>(this),
                            tr("Unknown feature"),
                            tr("Row %1 references feature '%2', which is not "
                               "available in category %3.")
                            .arg(row + 1)
                            .arg(featureName, category->currentText()));
                return false;
            }
        }
        return true;
    }

    const WorldGenDefinitions &mDefinitions;
    QLineEdit *mName = nullptr;
    QComboBox *mRegistry = nullptr;
    QComboBox *mParent = nullptr;
    QCheckBox *mGenerate = nullptr;
    QMap<QString, QLineEdit *> mParameters;
    QCheckBox *mWriteZombies = nullptr;
    QDoubleSpinBox *mZombies = nullptr;
    QTableWidget *mWeights = nullptr;
};

QString defaultWorldGenPath()
{
    QSettings settings;
    const QString saved = settings.value(
                QStringLiteral("WorldGenPreview/Root")).toString();
    if (!normalizedWorldGenRoot(saved).isEmpty())
        return normalizedWorldGenRoot(saved);

    const QString environment =
            qEnvironmentVariable("PZ_WORLDGEN_ROOT");
    if (!normalizedWorldGenRoot(environment).isEmpty())
        return normalizedWorldGenRoot(environment);

    const QString tilesDirectory =
            Preferences::instance()->tilesDirectory();
    const QString developmentCandidate =
            QDir(tilesDirectory).absoluteFilePath(
                QStringLiteral("../42.20/lua/server/WorldGen"));
    if (!normalizedWorldGenRoot(developmentCandidate).isEmpty())
        return normalizedWorldGenRoot(developmentCandidate);

    const QStringList commonGameDirectories = {
        QStringLiteral(
            "C:/Program Files (x86)/Steam/steamapps/common/"
            "ProjectZomboid"),
        QStringLiteral(
            "C:/Program Files/Steam/steamapps/common/ProjectZomboid")
    };
    for (const QString &candidate : commonGameDirectories) {
        if (!normalizedWorldGenRoot(candidate).isEmpty())
            return normalizedWorldGenRoot(candidate);
    }
    return QString();
}

} // namespace

class WorldGenPreviewDialogPrivate
{
public:
    enum Mode {
        BiomeMode,
        PrefabMode
    };

    explicit WorldGenPreviewDialogPrivate(QDialog *dialog,
                                          WorldDocument *document,
                                          Mode editorMode)
        : q(dialog)
        , worldDocument(document)
        , projectRootPath(projectWorldGenRoot(document))
        , mode(editorMode)
    {
    }

    void buildInterface()
    {
        q->setWindowTitle(mode == BiomeMode
                          ? q->tr("WorldGen Biome Editor / Preview")
                          : q->tr("WorldGen Prefab Editor"));
        q->setWindowFlags(q->windowFlags()
                          | Qt::WindowMaximizeButtonHint);
        q->resize(1500, 920);

        QVBoxLayout *mainLayout = new QVBoxLayout(q);
        QHBoxLayout *pathLayout = new QHBoxLayout;
        pathLayout->addWidget(new QLabel(
                                  q->tr("Game WorldGen definitions "
                                        "(read-only):"), q));
        pathEdit = new QLineEdit(q);
        pathEdit->setPlaceholderText(q->tr(
            "Project Zomboid directory or "
            "media/lua/server/WorldGen"));
        pathLayout->addWidget(pathEdit, 1);
        browseButton = new QPushButton(q->tr("Browse..."), q);
        reloadButton = new QPushButton(q->tr("Reload"), q);
        pathLayout->addWidget(browseButton);
        pathLayout->addWidget(reloadButton);
        mainLayout->addLayout(pathLayout);

        QHBoxLayout *projectPathLayout = new QHBoxLayout;
        projectPathLayout->addWidget(new QLabel(
                                         q->tr("Project WorldGen overlay:"),
                                         q));
        projectPathEdit = new QLineEdit(q);
        projectPathEdit->setReadOnly(true);
        projectPathEdit->setText(QDir::toNativeSeparators(projectRootPath));
        projectPathEdit->setPlaceholderText(
                    q->tr("Save and load a WorldEd project to enable editing"));
        projectPathLayout->addWidget(projectPathEdit, 1);
        mainLayout->addLayout(projectPathLayout);

        QFrame *projectBanner = new QFrame(q);
        projectBanner->setFrameShape(QFrame::StyledPanel);
        QHBoxLayout *bannerLayout = new QHBoxLayout(projectBanner);
        const QString bannerText = mode == BiomeMode
                ? q->tr(
                    "GAME BIOME DEFINITIONS STAY READ-ONLY. New biomes, "
                    "features and edited project variants are saved under "
                    "the map project's media/lua/server/WorldGen directory. "
                    "Static prefabs have their own window. Roads and erosion "
                    "remain excluded.")
                : q->tr(
                    "GAME PREFAB DEFINITIONS STAY READ-ONLY. New, imported "
                    "and edited prefabs are saved under the map project's "
                    "media/lua/server/WorldGen/prefabs directory. Biome "
                    "rules and previews have their own window.");
        QLabel *banner = new QLabel(bannerText, projectBanner);
        banner->setWordWrap(true);
        bannerLayout->addWidget(banner);
        mainLayout->addWidget(projectBanner);

        if (mode == BiomeMode)
            buildBiomeInterface(mainLayout);
        else
            buildPrefabInterface(mainLayout);

        statusLabel = new QLabel(q);
        statusLabel->setWordWrap(true);
        mainLayout->addWidget(statusLabel);

        QObject::connect(browseButton, &QPushButton::clicked, q,
                         [this]() { browse(); });
        QObject::connect(reloadButton, &QPushButton::clicked, q,
                         [this]() { reload(); });

        pathEdit->setText(QDir::toNativeSeparators(
                              defaultWorldGenPath()));
        if (!pathEdit->text().isEmpty())
            reload();
        else
            setStatus(q->tr("Choose the Project Zomboid WorldGen "
                            "definitions directory."), true);
    }

    void buildBiomeInterface(QVBoxLayout *mainLayout)
    {
        QHBoxLayout *controls = new QHBoxLayout;
        controls->addWidget(new QLabel(q->tr("Show:"), q));
        typeCombo = new QComboBox(q);
        typeCombo->addItem(q->tr("All biomes"), QStringLiteral("all"));
        typeCombo->addItem(q->tr("Procedural biomes"),
                           QStringLiteral("procedural"));
        typeCombo->addItem(q->tr("Map biomes"),
                           QStringLiteral("map"));
        controls->addWidget(typeCombo);
        controls->addWidget(new QLabel(q->tr("Biome:"), q));
        biomeCombo = new QComboBox(q);
        biomeCombo->setMinimumWidth(310);
        controls->addWidget(biomeCombo);
        controls->addWidget(new QLabel(q->tr("Preview seed:"), q));
        seedSpin = new QSpinBox(q);
        seedSpin->setRange(0, 2147483647);
        seedSpin->setValue(42020);
        controls->addWidget(seedSpin);
        regenerateButton = new QPushButton(q->tr("Regenerate"), q);
        controls->addWidget(regenerateButton);
        controls->addStretch(1);
        for (const QString &category : FeatureCategories) {
            QCheckBox *checkBox = new QCheckBox(category, q);
            checkBox->setChecked(true);
            categoryChecks.insert(category, checkBox);
            controls->addWidget(checkBox);
        }
        mainLayout->addLayout(controls);

        QHBoxLayout *editorControls = new QHBoxLayout;
        newBiomeButton = new QPushButton(q->tr("New Biome..."), q);
        editBiomeButton = new QPushButton(q);
        editorControls->addWidget(newBiomeButton);
        editorControls->addWidget(editBiomeButton);
        editorControls->addSpacing(20);
        editorControls->addWidget(new QLabel(q->tr("Biome feature:"), q));
        featureCombo = new QComboBox(q);
        featureCombo->setMinimumWidth(310);
        editorControls->addWidget(featureCombo);
        newFeatureButton = new QPushButton(q->tr("New Feature..."), q);
        editFeatureButton = new QPushButton(q);
        editorControls->addWidget(newFeatureButton);
        editorControls->addWidget(editFeatureButton);
        editorControls->addStretch(1);
        mainLayout->addLayout(editorControls);

        QSplitter *splitter = new QSplitter(Qt::Horizontal, q);
        canvas = new WorldGenPreviewCanvas(splitter);
        QScrollArea *scrollArea = new QScrollArea(splitter);
        scrollArea->setWidget(canvas);
        scrollArea->setWidgetResizable(false);
        splitter->addWidget(scrollArea);

        QWidget *inspectorWidget = new QWidget(splitter);
        QVBoxLayout *inspectorLayout = new QVBoxLayout(inspectorWidget);
        inspectorLayout->setContentsMargins(0, 0, 0, 0);
        QLabel *inspectorTitle =
                new QLabel(q->tr("Resolved biome definition"),
                           inspectorWidget);
        QFont titleFont = inspectorTitle->font();
        titleFont.setBold(true);
        inspectorTitle->setFont(titleFont);
        inspectorLayout->addWidget(inspectorTitle);
        inspector = new QTreeWidget(inspectorWidget);
        inspector->setColumnCount(2);
        inspector->setHeaderLabels(
                    QStringList() << q->tr("Property")
                                  << q->tr("Value"));
        inspector->header()->setSectionResizeMode(
                    0, QHeaderView::ResizeToContents);
        inspector->header()->setStretchLastSection(true);
        inspectorLayout->addWidget(inspector, 1);
        QLabel *squareTitle =
                new QLabel(q->tr("Selected square"), inspectorWidget);
        squareTitle->setFont(titleFont);
        inspectorLayout->addWidget(squareTitle);
        squareDetails = new QTreeWidget(inspectorWidget);
        squareDetails->setColumnCount(2);
        squareDetails->setHeaderLabels(
                    QStringList() << q->tr("Layer")
                                  << q->tr("Sprite / source"));
        squareDetails->header()->setSectionResizeMode(
                    0, QHeaderView::ResizeToContents);
        squareDetails->header()->setStretchLastSection(true);
        squareDetails->setMaximumHeight(240);
        inspectorLayout->addWidget(squareDetails);
        splitter->addWidget(inspectorWidget);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 0);
        splitter->setSizes(QList<int>() << 1040 << 430);
        mainLayout->addWidget(splitter, 1);

        QObject::connect(typeCombo,
                         QOverload<int>::of(&QComboBox::currentIndexChanged),
                         q, [this]() { populateBiomeCombo(); });
        QObject::connect(biomeCombo,
                         QOverload<int>::of(&QComboBox::currentIndexChanged),
                         q, [this]() { biomeChanged(); });
        QObject::connect(regenerateButton, &QPushButton::clicked, q,
                         [this]() { regenerate(); });
        QObject::connect(newBiomeButton, &QPushButton::clicked, q,
                         [this]() { createBiome(); });
        QObject::connect(editBiomeButton, &QPushButton::clicked, q,
                         [this]() { editOrCopyBiome(); });
        QObject::connect(newFeatureButton, &QPushButton::clicked, q,
                         [this]() { createFeature(); });
        QObject::connect(editFeatureButton, &QPushButton::clicked, q,
                         [this]() { editOrCopyFeature(); });
        QObject::connect(featureCombo,
                          QOverload<int>::of(&QComboBox::currentIndexChanged),
                          q, [this]() { featureChanged(); });
        QObject::connect(seedSpin,
                         QOverload<int>::of(&QSpinBox::valueChanged),
                         q, [this]() { regenerate(); });
        for (const QString &category : FeatureCategories) {
            QObject::connect(categoryChecks.value(category),
                             &QCheckBox::toggled, q,
                             [this, category](bool checked) {
                canvas->setCategoryVisible(category, checked);
            });
        }
        canvas->squareSelected = [this](int x, int y) {
            showSquare(x, y);
        };

        const bool editingEnabled = !projectRootPath.isEmpty();
        newBiomeButton->setEnabled(editingEnabled);
        editBiomeButton->setEnabled(editingEnabled);
        newFeatureButton->setEnabled(editingEnabled);
        editFeatureButton->setEnabled(editingEnabled);
    }

    void buildPrefabInterface(QVBoxLayout *mainLayout)
    {
        QHBoxLayout *prefabControls = new QHBoxLayout;
        prefabControls->addWidget(new QLabel(
                                      q->tr("Static WorldGen prefab:"), q));
        prefabCombo = new QComboBox(q);
        prefabCombo->setMinimumWidth(390);
        prefabControls->addWidget(prefabCombo);
        newPrefabButton = new QPushButton(q->tr("New Prefab..."), q);
        importPrefabButton =
                new QPushButton(q->tr("Import TMX/TBX..."), q);
        editPrefabButton = new QPushButton(q);
        stagePrefabButton =
                new QPushButton(q->tr("Stage for Game / Mod..."), q);
        prefabControls->addWidget(newPrefabButton);
        prefabControls->addWidget(importPrefabButton);
        prefabControls->addWidget(editPrefabButton);
        prefabControls->addWidget(stagePrefabButton);
        prefabControls->addStretch(1);
        mainLayout->addLayout(prefabControls);

        QLabel *explanation = new QLabel(
                    q->tr("Static WorldGen prefabs are independent from biome "
                          "rules. Select one to inspect it, or open the visual "
                          "schematic editor. TMX/TBX conversion enforces the "
                          "z=0 and WorldGen layer limits."),
                    q);
        explanation->setWordWrap(true);
        mainLayout->addWidget(explanation);

        prefabInspector = new QTreeWidget(q);
        prefabInspector->setColumnCount(2);
        prefabInspector->setHeaderLabels(
                    QStringList() << q->tr("Property")
                                  << q->tr("Value"));
        prefabInspector->header()->setSectionResizeMode(
                    0, QHeaderView::ResizeToContents);
        prefabInspector->header()->setStretchLastSection(true);
        mainLayout->addWidget(prefabInspector, 1);

        QObject::connect(prefabCombo,
                         QOverload<int>::of(&QComboBox::currentIndexChanged),
                         q, [this]() { prefabChanged(); });
        QObject::connect(newPrefabButton, &QPushButton::clicked, q,
                         [this]() { createPrefab(); });
        QObject::connect(importPrefabButton, &QPushButton::clicked, q,
                         [this]() { importPrefab(); });
        QObject::connect(editPrefabButton, &QPushButton::clicked, q,
                         [this]() { editOrCopyPrefab(); });
        QObject::connect(stagePrefabButton, &QPushButton::clicked, q,
                         [this]() { stagePrefab(); });

        const bool editingEnabled = !projectRootPath.isEmpty();
        newPrefabButton->setEnabled(editingEnabled);
        importPrefabButton->setEnabled(editingEnabled);
        editPrefabButton->setEnabled(editingEnabled);
        stagePrefabButton->setEnabled(editingEnabled);
    }

    void browse()
    {
        const QString start = pathEdit->text().isEmpty()
                ? QDir::homePath() : pathEdit->text();
        const QString directory = QFileDialog::getExistingDirectory(
                    q, q->tr("Choose Project Zomboid or WorldGen Directory"),
                    start);
        if (directory.isEmpty())
            return;
        pathEdit->setText(QDir::toNativeSeparators(directory));
        reload();
    }

    void reload()
    {
        WorldGenDefinitions loaded;
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = loadDefinitions(pathEdit->text(), projectRootPath,
                                        &loaded, &error);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            definitions = WorldGenDefinitions();
            if (biomeCombo)
                biomeCombo->clear();
            if (featureCombo)
                featureCombo->clear();
            if (prefabCombo)
                prefabCombo->clear();
            if (inspector)
                inspector->clear();
            if (prefabInspector)
                prefabInspector->clear();
            if (canvas)
                canvas->setGrid(PreviewGrid());
            setStatus(error, true);
            return;
        }

        definitions = loaded;
        pathEdit->setText(QDir::toNativeSeparators(
                              definitions.rootPath));
        QSettings settings;
        settings.setValue(QStringLiteral("WorldGenPreview/Root"),
                          definitions.rootPath);
        if (mode == BiomeMode) {
            setStatus(q->tr(
                "Loaded %1 biome features, %2 procedural biomes, %3 map "
                "biomes and %4 subbiomes from the read-only game path %5, "
                "then the project biome overlay %6.")
                      .arg(definitions.features.size())
                      .arg(definitions.proceduralBiomes.size())
                      .arg(definitions.mapBiomes.size())
                      .arg(definitions.subBiomeCount)
                      .arg(QDir::toNativeSeparators(definitions.rootPath))
                      .arg(projectRootPath.isEmpty()
                           ? q->tr("(disabled)")
                           : QDir::toNativeSeparators(projectRootPath)),
                      false);
            populateBiomeCombo();
            populateFeatureCombo();
        } else {
            setStatus(q->tr(
                "Loaded %1 static WorldGen prefabs from the read-only game "
                "path %2, then the project prefab overlay %3. Biome rules "
                "are intentionally not shown in this window.")
                      .arg(definitions.prefabs.size())
                      .arg(QDir::toNativeSeparators(definitions.rootPath))
                      .arg(projectRootPath.isEmpty()
                           ? q->tr("(disabled)")
                           : QDir::toNativeSeparators(projectRootPath)),
                      false);
            populatePrefabCombo();
        }
    }

    void populateBiomeCombo()
    {
        const QString previous = biomeCombo->currentData().toString();
        biomeCombo->blockSignals(true);
        biomeCombo->clear();
        const QString filter = typeCombo->currentData().toString();
        if (filter != QStringLiteral("map")) {
            for (const WorldGenBiome &biome
                 : definitions.proceduralBiomes) {
                biomeCombo->addItem(
                            q->tr("[%1] [Procedural] %2")
                            .arg(biome.projectOwned
                                 ? q->tr("Project") : q->tr("Game"),
                                 biome.name),
                            QStringLiteral("procedural:") + biome.name);
            }
        }
        if (filter != QStringLiteral("procedural")) {
            for (const WorldGenBiome &biome : definitions.mapBiomes) {
                biomeCombo->addItem(
                            q->tr("[%1] [Map] %2")
                            .arg(biome.projectOwned
                                 ? q->tr("Project") : q->tr("Game"),
                                 biome.name),
                            QStringLiteral("map:") + biome.name);
            }
        }
        int previousIndex = biomeCombo->findData(previous);
        if (previousIndex < 0) {
            previousIndex = biomeCombo->findData(
                        QStringLiteral(
                            "procedural:pine_forest_boulder_none"));
        }
        if (previousIndex < 0 && biomeCombo->count())
            previousIndex = 0;
        biomeCombo->setCurrentIndex(previousIndex);
        biomeCombo->blockSignals(false);
        biomeChanged();
    }

    QString uniqueBiomeName(const QString &base) const
    {
        QString candidate = base;
        int suffix = 2;
        while (definitions.proceduralBiomes.contains(candidate)
               || definitions.mapBiomes.contains(candidate)) {
            candidate = base + QStringLiteral("_%1").arg(suffix++);
        }
        return candidate;
    }

    QString uniqueFeatureName(const QString &base) const
    {
        QString candidate = base;
        int suffix = 2;
        while (definitions.features.contains(candidate))
            candidate = base + QStringLiteral("_%1").arg(suffix++);
        return candidate;
    }

    QString uniquePrefabName(const QString &base) const
    {
        QString candidate = base;
        int suffix = 2;
        while (definitions.prefabs.contains(candidate))
            candidate = base + QStringLiteral("_%1").arg(suffix++);
        return candidate;
    }

    void populateFeatureCombo()
    {
        const QString previous = featureCombo->currentData().toString();
        featureCombo->blockSignals(true);
        featureCombo->clear();
        QList<WorldGenFeature> features = definitions.features.values();
        std::sort(features.begin(), features.end(),
                  [](const WorldGenFeature &left,
                     const WorldGenFeature &right) {
            const int categoryOrder = left.category.compare(
                        right.category, Qt::CaseInsensitive);
            return categoryOrder == 0
                    ? left.name.compare(right.name,
                                        Qt::CaseInsensitive) < 0
                    : categoryOrder < 0;
        });
        for (const WorldGenFeature &feature : features) {
            featureCombo->addItem(
                        q->tr("[%1] [%2] %3")
                        .arg(feature.projectOwned
                             ? q->tr("Project") : q->tr("Game"),
                             feature.category, feature.name),
                        feature.name);
        }
        int index = featureCombo->findData(previous);
        if (index < 0 && featureCombo->count())
            index = 0;
        featureCombo->setCurrentIndex(index);
        featureCombo->blockSignals(false);
        featureChanged();
    }

    const WorldGenFeature *currentFeature() const
    {
        const auto iterator = definitions.features.constFind(
                    featureCombo->currentData().toString());
        return iterator == definitions.features.constEnd()
                ? nullptr : &iterator.value();
    }

    void featureChanged()
    {
        const WorldGenFeature *feature = currentFeature();
        editFeatureButton->setEnabled(
                    feature && !projectRootPath.isEmpty());
        editFeatureButton->setText(
                    feature && feature->projectOwned
                    ? q->tr("Edit Project Feature...")
                    : q->tr("Create Feature Variant..."));
    }

    bool saveFeatureDefinition(const WorldGenFeature &feature,
                               bool editing)
    {
        if (!editing && definitions.features.contains(feature.name)) {
            QMessageBox::warning(
                        q, q->tr("Feature already exists"),
                        q->tr("A feature named '%1' already exists. Choose a "
                              "new project feature name.")
                        .arg(feature.name));
            return false;
        }
        const QString directory = QDir(projectRootPath).filePath(
                    QStringLiteral("features/%1")
                    .arg(feature.category.toLower()));
        const QString fileName = QDir(directory).filePath(
                    feature.name + QStringLiteral(".lua"));
        QString error;
        if (!writeProjectLuaFile(fileName, featureLua(feature), &error)) {
            QMessageBox::critical(q, q->tr("Could not save feature"), error);
            return false;
        }
        reload();
        const int index = featureCombo->findData(feature.name);
        if (index >= 0)
            featureCombo->setCurrentIndex(index);
        setStatus(q->tr("Saved project feature %1 to %2.")
                  .arg(feature.name,
                       QDir::toNativeSeparators(fileName)), false);
        return true;
    }

    void populatePrefabCombo()
    {
        const QString previous = prefabCombo->currentData().toString();
        prefabCombo->blockSignals(true);
        prefabCombo->clear();
        for (const WorldGenPrefab &prefab : definitions.prefabs) {
            prefabCombo->addItem(
                        q->tr("[%1] %2  (%3 x %4)")
                        .arg(prefab.projectOwned
                             ? q->tr("Project") : q->tr("Game"))
                        .arg(prefab.name)
                        .arg(prefab.width)
                        .arg(prefab.height),
                        prefab.name);
        }
        int index = prefabCombo->findData(previous);
        if (index < 0 && prefabCombo->count())
            index = 0;
        prefabCombo->setCurrentIndex(index);
        prefabCombo->blockSignals(false);
        prefabChanged();
    }

    const WorldGenPrefab *currentPrefab() const
    {
        const auto iterator = definitions.prefabs.constFind(
                    prefabCombo->currentData().toString());
        return iterator == definitions.prefabs.constEnd()
                ? nullptr : &iterator.value();
    }

    void prefabChanged()
    {
        const WorldGenPrefab *prefab = currentPrefab();
        const bool enabled = prefab && !projectRootPath.isEmpty();
        editPrefabButton->setEnabled(enabled);
        stagePrefabButton->setEnabled(enabled);
        editPrefabButton->setText(
                    prefab && prefab->projectOwned
                    ? q->tr("Edit Project Prefab...")
                    : q->tr("View / Create Project Copy..."));
        if (!prefabInspector)
            return;
        prefabInspector->clear();
        if (!prefab)
            return;

        QTreeWidgetItem *identity =
                new QTreeWidgetItem(prefabInspector);
        identity->setText(0, q->tr("Prefab"));
        identity->setText(1, prefab->name);
        identity->setExpanded(true);
        new QTreeWidgetItem(
                    identity,
                    QStringList()
                    << q->tr("Source")
                    << (prefab->projectOwned
                        ? q->tr("Project overlay")
                        : q->tr("Game definitions (read-only)")));
        new QTreeWidgetItem(
                    identity,
                    QStringList() << q->tr("Dimensions")
                                  << q->tr("%1 x %2 squares, z=0")
                                     .arg(prefab->width)
                                     .arg(prefab->height));
        new QTreeWidgetItem(
                    identity,
                    QStringList() << q->tr("Zombie density")
                                  << QString::number(
                                      prefab->zombies, 'g', 8));
        new QTreeWidgetItem(
                    identity,
                    QStringList() << q->tr("Sprite catalogue")
                                  << q->tr("%1 unique entries")
                                     .arg(prefab->tiles.size()));

        for (const QString &category : PrefabCategories) {
            const QVector<int> refs = prefab->schematic.value(category);
            int placements = 0;
            QSet<int> usedRefs;
            for (int ref : refs) {
                if (ref > 0) {
                    ++placements;
                    usedRefs.insert(ref);
                }
            }
            QTreeWidgetItem *categoryItem =
                    new QTreeWidgetItem(prefabInspector);
            categoryItem->setText(0, category);
            categoryItem->setText(
                        1, q->tr("%1 placements, %2 sprites")
                        .arg(placements).arg(usedRefs.size()));
            QStringList sprites;
            for (int ref : usedRefs) {
                const QString sprite =
                        spriteForPrefabRef(*prefab, ref);
                if (!sprite.isEmpty())
                    sprites.append(sprite);
            }
            std::sort(sprites.begin(), sprites.end(),
                      [](const QString &left, const QString &right) {
                return left.compare(right, Qt::CaseInsensitive) < 0;
            });
            for (const QString &sprite : sprites) {
                new QTreeWidgetItem(
                            categoryItem,
                            QStringList() << q->tr("Sprite") << sprite);
            }
        }
    }

    bool savePrefabDefinition(const WorldGenPrefab &prefab, bool editing)
    {
        if (!editing && definitions.prefabs.contains(prefab.name)) {
            QMessageBox::warning(
                        q, q->tr("Prefab already exists"),
                        q->tr("A prefab named '%1' already exists. Choose a "
                              "new project prefab name.")
                        .arg(prefab.name));
            return false;
        }
        const QString fileName = QDir(projectRootPath).filePath(
                    QStringLiteral("prefabs/%1.lua").arg(prefab.name));
        QString error;
        if (!writeProjectLuaFile(fileName, prefabLua(prefab), &error)) {
            QMessageBox::critical(q, q->tr("Could not save prefab"), error);
            return false;
        }
        reload();
        const int index = prefabCombo->findData(prefab.name);
        if (index >= 0)
            prefabCombo->setCurrentIndex(index);
        setStatus(q->tr("Saved project prefab %1 to %2.")
                  .arg(prefab.name,
                       QDir::toNativeSeparators(fileName)), false);
        return true;
    }

    void createPrefab()
    {
        if (projectRootPath.isEmpty())
            return;
        WorldGenPrefab initial;
        initial.name = uniquePrefabName(QStringLiteral("custom_prefab"));
        for (const QString &category : PrefabCategories)
            initial.schematic.insert(category, QVector<int>(1, 0));
        WorldGenPrefabEditorDialog dialog(initial, false, q);
        if (dialog.exec() == QDialog::Accepted)
            savePrefabDefinition(dialog.prefab(), false);
    }

    void importPrefab()
    {
        if (projectRootPath.isEmpty())
            return;
        const QString source = QFileDialog::getOpenFileName(
                    q, q->tr("Import TMX/TBX as WorldGen Prefab"),
                    QFileInfo(worldDocument->fileName()).absolutePath(),
                    q->tr("PZ maps and buildings (*.tmx *.tbx)"));
        if (source.isEmpty())
            return;

        WorldGenPrefab imported;
        QStringList warnings;
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = importPrefabSource(
                    source, &imported, &warnings, &error);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::critical(q, q->tr("Prefab import stopped"), error);
            return;
        }
        imported.name = uniquePrefabName(imported.name);
        if (!warnings.isEmpty()) {
            const QMessageBox::StandardButton answer =
                    QMessageBox::warning(
                        q, q->tr("Lossy WorldGen conversion"),
                        warnings.join(QStringLiteral("\n\n"))
                        + q->tr("\n\nReview the four engine slots before "
                                "saving. Continue to the prefab editor?"),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        WorldGenPrefabEditorDialog dialog(imported, false, q);
        if (dialog.exec() == QDialog::Accepted)
            savePrefabDefinition(dialog.prefab(), false);
    }

    void editOrCopyPrefab()
    {
        const WorldGenPrefab *selected = currentPrefab();
        if (!selected || projectRootPath.isEmpty())
            return;
        WorldGenPrefab initial = *selected;
        const bool editing = selected->projectOwned;
        if (!editing) {
            initial.name = uniquePrefabName(
                        selected->name + QStringLiteral("_project"));
            initial.projectOwned = true;
        }
        WorldGenPrefabEditorDialog dialog(initial, editing, q);
        if (dialog.exec() == QDialog::Accepted)
            savePrefabDefinition(dialog.prefab(), editing);
    }

    void stagePrefab()
    {
        const WorldGenPrefab *selected = currentPrefab();
        if (!selected || projectRootPath.isEmpty())
            return;

        QDialog dialog(q);
        dialog.setWindowTitle(q->tr("Stage WorldGen Prefab for Game / Mod"));
        dialog.resize(760, 420);
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QLabel *explanation = new QLabel(q->tr(
            "This writes a game-ready mod/project layout outside the "
            "base-game directory: the prefab Lua file plus a map-specific "
            "WorldGenOverride.lua static-module placement. Existing override "
            "content is preserved; this prefab's marked block is replaced."),
            &dialog);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        QFormLayout *form = new QFormLayout;
        QHBoxLayout *targetLayout = new QHBoxLayout;
        QLineEdit *target = new QLineEdit(
                    ownerRootForWorldGen(projectRootPath), &dialog);
        QPushButton *browse = new QPushButton(q->tr("Browse..."), &dialog);
        targetLayout->addWidget(target, 1);
        targetLayout->addWidget(browse);
        form->addRow(q->tr("Project/mod root:"), targetLayout);
        QLineEdit *mapName = new QLineEdit(
                    QFileInfo(worldDocument->fileName()).completeBaseName(),
                    &dialog);
        form->addRow(q->tr("media/maps folder:"), mapName);
        QSpinBox *x = new QSpinBox(&dialog);
        QSpinBox *y = new QSpinBox(&dialog);
        x->setRange(0, 10000000);
        y->setRange(0, 10000000);
        QHBoxLayout *position = new QHBoxLayout;
        position->addWidget(new QLabel(q->tr("X:"), &dialog));
        position->addWidget(x);
        position->addSpacing(20);
        position->addWidget(new QLabel(q->tr("Y:"), &dialog));
        position->addWidget(y);
        position->addStretch(1);
        form->addRow(q->tr("Global square origin:"), position);
        layout->addLayout(form);
        QLabel *limits = new QLabel(q->tr(
            "One instance uses exactly %1 x %2 squares. Chunk = 8 x 8; "
            "Build 42 cell = 256 x 256. Crossing either boundary is supported "
            "and will be reported before writing.")
            .arg(selected->width).arg(selected->height), &dialog);
        limits->setWordWrap(true);
        layout->addWidget(limits);
        layout->addStretch(1);
        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                    &dialog);
        buttons->button(QDialogButtonBox::Save)->setText(
                    q->tr("Stage Files"));
        layout->addWidget(buttons);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&]() {
            const QString directory = QFileDialog::getExistingDirectory(
                        &dialog, q->tr("Choose Project or Mod Root"),
                        target->text());
            if (!directory.isEmpty())
                target->setText(QDir::toNativeSeparators(directory));
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected,
                         &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted,
                         &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted)
            return;

        const QString targetRoot = QDir::cleanPath(
                    QFileInfo(target->text().trimmed()).absoluteFilePath());
        const QString gameRoot = ownerRootForWorldGen(
                    definitions.rootPath);
        const QString targetFolded =
                QDir::fromNativeSeparators(targetRoot).toCaseFolded();
        QString gameFolded =
                QDir::fromNativeSeparators(gameRoot).toCaseFolded();
        if (!gameFolded.endsWith(QLatin1Char('/')))
            gameFolded.append(QLatin1Char('/'));
        if (targetFolded == gameFolded.left(gameFolded.size() - 1)
                || targetFolded.startsWith(gameFolded)) {
            QMessageBox::critical(
                        q, q->tr("Base-game directory refused"),
                        q->tr("Choose a map project or mod directory outside "
                              "the Project Zomboid installation. Base-game "
                              "WorldGen files are never modified."));
            return;
        }
        const QString mapFolder = mapName->text().trimmed();
        if (targetRoot.isEmpty() || mapFolder.isEmpty()
                || mapFolder.contains(QLatin1Char('/'))
                || mapFolder.contains(QLatin1Char('\\'))) {
            QMessageBox::warning(
                        q, q->tr("Invalid staging destination"),
                        q->tr("Choose a root directory and a single "
                              "media/maps folder name."));
            return;
        }

        const qint64 xmin = x->value();
        const qint64 ymin = y->value();
        const qint64 xmax = xmin + selected->width - 1;
        const qint64 ymax = ymin + selected->height - 1;
        const bool crossesChunk =
                xmin / ChunkSize != xmax / ChunkSize
                || ymin / ChunkSize != ymax / ChunkSize;
        const bool crossesCell =
                xmin / 256 != xmax / 256 || ymin / 256 != ymax / 256;
        const QString boundaryReport = q->tr(
            "Squares: (%1,%2) through (%3,%4)\n"
            "Chunks: (%5,%6) through (%7,%8)%9\n"
            "Cells: (%10,%11) through (%12,%13)%14")
                .arg(xmin).arg(ymin).arg(xmax).arg(ymax)
                .arg(xmin / ChunkSize).arg(ymin / ChunkSize)
                .arg(xmax / ChunkSize).arg(ymax / ChunkSize)
                .arg(crossesChunk ? q->tr(" — crosses boundary")
                                  : q->tr(" — contained"))
                .arg(xmin / 256).arg(ymin / 256)
                .arg(xmax / 256).arg(ymax / 256)
                .arg(crossesCell ? q->tr(" — crosses boundary")
                                 : q->tr(" — contained"));
        if (QMessageBox::question(
                    q, q->tr("Confirm static-module placement"),
                    boundaryReport + q->tr(
                        "\n\nThe engine applies this prefab at z=0 only. "
                        "Continue?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        const QString prefabFile = QDir(targetRoot).filePath(
                    QStringLiteral("media/lua/server/WorldGen/prefabs/%1.lua")
                    .arg(selected->name));
        const QString overrideFile = QDir(targetRoot).filePath(
                    QStringLiteral("media/maps/%1/WorldGenOverride.lua")
                    .arg(mapFolder));
        QString error;
        if (!writeProjectLuaFile(
                    prefabFile, prefabLua(*selected), &error)) {
            QMessageBox::critical(q, q->tr("Could not stage prefab"), error);
            return;
        }

        QString existing;
        QFile input(overrideFile);
        if (input.exists()) {
            if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox::critical(
                            q, q->tr("Could not read WorldGenOverride.lua"),
                            input.errorString());
                return;
            }
            existing = QString::fromUtf8(input.readAll());
        } else {
            existing = QStringLiteral(
                "-- WorldGenOverride.lua\n"
                "-- Static modules staged by PZWorldEd are marked below.\n");
        }
        const QString markerName =
                QStringLiteral("PZWORLDED PREFAB ") + selected->name;
        const QString block = QStringLiteral(
            "\n-- BEGIN %1\n"
            "require %2\n"
            "worldgen.static_modules = worldgen.static_modules or {}\n"
            "worldgen.static_modules[#worldgen.static_modules + 1] = {\n"
            "    position = { xmin = %3, xmax = %4, ymin = %5, ymax = %6 },\n"
            "    prefab = worldgen.prefabs[%7]\n"
            "}\n"
            "-- END %1\n")
                .arg(markerName,
                     luaQuoted(QStringLiteral("WorldGen/prefabs/")
                               + selected->name))
                .arg(xmin).arg(xmax).arg(ymin).arg(ymax)
                .arg(luaQuoted(selected->name));
        const QRegularExpression markedBlock(
                    QStringLiteral(
                        "\\n?-- BEGIN %1\\R.*?-- END %1\\R?")
                    .arg(QRegularExpression::escape(markerName)),
                    QRegularExpression::DotMatchesEverythingOption);
        if (existing.contains(markedBlock))
            existing.replace(markedBlock, block);
        else
            existing += block;
        if (!writeProjectLuaFile(overrideFile, existing, &error)) {
            QMessageBox::critical(
                        q, q->tr("Could not stage WorldGenOverride.lua"),
                        error);
            return;
        }
        QString compactBoundaryReport = boundaryReport;
        compactBoundaryReport.replace(QLatin1Char('\n'),
                                      QStringLiteral("; "));
        setStatus(q->tr(
            "Staged prefab %1 and its static-module placement under %2. %3")
                  .arg(selected->name,
                       QDir::toNativeSeparators(targetRoot),
                       compactBoundaryReport),
                  false);
    }

    bool saveBiomeDefinition(const WorldGenBiome &biome, bool editing)
    {
        if (!editing
                && (definitions.proceduralBiomes.contains(biome.name)
                    || definitions.mapBiomes.contains(biome.name))) {
            QMessageBox::warning(
                        q, q->tr("Biome already exists"),
                        q->tr("A biome named '%1' already exists. Choose a "
                              "new project biome name.")
                        .arg(biome.name));
            return false;
        }
        const QString relativeDirectory = biome.mapBiome
                ? QStringLiteral("biomes/map")
                : QStringLiteral("biomes/worldgen");
        const QString fileName = QDir(projectRootPath).filePath(
                    relativeDirectory + QLatin1Char('/') + biome.name
                    + QStringLiteral(".lua"));
        QString error;
        if (!writeProjectLuaFile(fileName, biomeLua(biome), &error)) {
            QMessageBox::critical(q, q->tr("Could not save biome"), error);
            return false;
        }
        const QString key = (biome.mapBiome
                             ? QStringLiteral("map:")
                             : QStringLiteral("procedural:")) + biome.name;
        reload();
        const int index = biomeCombo->findData(key);
        if (index >= 0)
            biomeCombo->setCurrentIndex(index);
        setStatus(q->tr("Saved project biome %1 to %2.")
                  .arg(biome.name,
                       QDir::toNativeSeparators(fileName)), false);
        return true;
    }

    void createFeature()
    {
        if (projectRootPath.isEmpty())
            return;
        WorldGenFeature initial;
        initial.name = uniqueFeatureName(QStringLiteral("custom_feature"));
        const WorldGenFeature *selected = currentFeature();
        initial.category = selected
                ? selected->category : QStringLiteral("GROUND");
        WorldGenPattern pattern;
        pattern.rows.append(QVector<QString>() << QString());
        initial.patterns.append(pattern);
        WorldGenFeatureEditorDialog dialog(initial, false, q);
        if (dialog.exec() == QDialog::Accepted)
            saveFeatureDefinition(dialog.feature(), false);
    }

    void editOrCopyFeature()
    {
        const WorldGenFeature *selected = currentFeature();
        if (!selected || projectRootPath.isEmpty())
            return;
        WorldGenFeature initial = *selected;
        const bool editing = selected->projectOwned;
        if (!editing) {
            initial.name = uniqueFeatureName(
                        selected->name + QStringLiteral("_project"));
            initial.projectOwned = true;
        }
        WorldGenFeatureEditorDialog dialog(initial, editing, q);
        if (dialog.exec() == QDialog::Accepted)
            saveFeatureDefinition(dialog.feature(), editing);
    }

    void createBiome()
    {
        if (projectRootPath.isEmpty())
            return;
        WorldGenBiome initial;
        const WorldGenBiome *selected = currentBiome();
        initial.mapBiome = selected && selected->mapBiome;
        initial.generate = true;
        if (selected) {
            initial.name = uniqueBiomeName(
                        selected->name + QStringLiteral("_custom"));
            initial.parent = selected->name;
        } else {
            initial.name = uniqueBiomeName(QStringLiteral("custom_biome"));
        }
        WorldGenBiomeEditorDialog dialog(definitions, initial, false, q);
        if (dialog.exec() == QDialog::Accepted)
            saveBiomeDefinition(dialog.biome(), false);
    }

    void editOrCopyBiome()
    {
        const WorldGenBiome *selected = currentBiome();
        if (!selected || projectRootPath.isEmpty())
            return;
        WorldGenBiome initial = *selected;
        const bool hasAdvancedRules =
                selected->declaredSubBiomeLinks > 0
                || selected->declaredPlacementRules > 0
                || selected->declaredProtectedRules > 0
                || selected->declaredReplacementRules > 0;
        const bool editing = selected->projectOwned && !hasAdvancedRules;
        if (!editing) {
            initial.name = uniqueBiomeName(
                        selected->name + QStringLiteral("_project"));
            initial.parent = selected->name;
            initial.declaredFeatures = selected->features;
            initial.declaredParameters = selected->parameters;
            initial.declaredSubBiomeLinks = 0;
            initial.declaredPlacementRules = 0;
            initial.declaredProtectedRules = 0;
            initial.declaredReplacementRules = 0;
            initial.projectOwned = true;
        }
        WorldGenBiomeEditorDialog dialog(definitions, initial, editing, q);
        if (dialog.exec() == QDialog::Accepted)
            saveBiomeDefinition(dialog.biome(), editing);
    }

    void biomeChanged()
    {
        const WorldGenBiome *biome = currentBiome();
        inspector->clear();
        squareDetails->clear();
        if (!biome) {
            canvas->setGrid(PreviewGrid());
            editBiomeButton->setEnabled(false);
            return;
        }
        editBiomeButton->setEnabled(!projectRootPath.isEmpty());
        const bool safeProjectEdit =
                biome->projectOwned
                && biome->declaredSubBiomeLinks == 0
                && biome->declaredPlacementRules == 0
                && biome->declaredProtectedRules == 0
                && biome->declaredReplacementRules == 0;
        editBiomeButton->setText(
                    safeProjectEdit
                    ? q->tr("Edit Project Biome...")
                    : q->tr("Create Biome Variant..."));
        populateInspector(*biome);
        regenerate();
    }

    const WorldGenBiome *currentBiome() const
    {
        return findBiome(definitions,
                         biomeCombo->currentData().toString());
    }

    void populateInspector(const WorldGenBiome &biome)
    {
        QTreeWidgetItem *identity = new QTreeWidgetItem(inspector);
        identity->setText(0, q->tr("Biome"));
        identity->setText(1, biome.name);
        identity->setExpanded(true);
        new QTreeWidgetItem(identity,
                            QStringList()
                            << q->tr("Registry")
                            << (biome.mapBiome
                                ? QStringLiteral("biomes_map")
                                : QStringLiteral("biomes")));
        new QTreeWidgetItem(identity,
                            QStringList()
                            << q->tr("Source")
                            << (biome.projectOwned
                                ? q->tr("Project overlay")
                                : q->tr("Game definitions (read-only)")));
        new QTreeWidgetItem(identity,
                            QStringList()
                            << q->tr("Parent")
                            << (biome.parent.isEmpty()
                                ? q->tr("(none)") : biome.parent));
        new QTreeWidgetItem(identity,
                            QStringList()
                            << q->tr("Selectable")
                            << (biome.generate
                                ? q->tr("yes") : q->tr("no (template)")));

        QTreeWidgetItem *parameters =
                new QTreeWidgetItem(inspector);
        parameters->setText(0, q->tr("Effective parameters"));
        parameters->setExpanded(true);
        for (auto iterator = biome.parameters.constBegin();
             iterator != biome.parameters.constEnd(); ++iterator) {
            new QTreeWidgetItem(parameters,
                                QStringList()
                                << iterator.key()
                                << iterator.value().join(
                                    QStringLiteral(", ")));
        }
        new QTreeWidgetItem(parameters,
                            QStringList()
                            << q->tr("Subbiome links")
                            << QString::number(biome.subBiomeLinks));
        new QTreeWidgetItem(parameters,
                            QStringList()
                            << q->tr("Placement rules")
                            << QString::number(biome.placementRules));
        new QTreeWidgetItem(parameters,
                            QStringList()
                            << q->tr("Protected rules")
                            << QString::number(biome.protectedRules));
        new QTreeWidgetItem(parameters,
                            QStringList()
                            << q->tr("Replacement rules")
                            << QString::number(biome.replacementRules));

        for (const QString &category : FeatureCategories) {
            if (!biome.features.contains(category))
                continue;
            const QList<WeightedFeature> list =
                    biome.features.value(category);
            double total = 0.0;
            for (const WeightedFeature &weighted : list)
                total += weighted.probability;
            QTreeWidgetItem *categoryItem =
                    new QTreeWidgetItem(inspector);
            categoryItem->setText(0, category);
            categoryItem->setText(
                        1, q->tr("%1 entries, total p=%2")
                        .arg(list.size())
                        .arg(total, 0, 'g', 6));
            categoryItem->setExpanded(category == QStringLiteral("GROUND"));
            for (const WeightedFeature &weighted : list) {
                const WorldGenFeature feature =
                        definitions.features.value(
                            weighted.featureName);
                QTreeWidgetItem *featureItem =
                        new QTreeWidgetItem(categoryItem);
                featureItem->setText(0, weighted.featureName);
                featureItem->setText(
                            1, q->tr("p=%1, %2 pattern(s)")
                            .arg(weighted.probabilityText)
                            .arg(feature.patterns.size()));
                for (const WorldGenPattern &pattern
                     : feature.patterns) {
                    new QTreeWidgetItem(
                                featureItem,
                                QStringList()
                                << q->tr("Pattern")
                                << q->tr("%1 x %2")
                                   .arg(pattern.width())
                                   .arg(pattern.height()));
                }
            }
        }
    }

    void regenerate()
    {
        const WorldGenBiome *biome = currentBiome();
        if (!biome)
            return;
        grid = generatePreview(definitions, *biome,
                               quint32(seedSpin->value()));

        QSet<Tileset *> requiredTilesets;
        QSet<QString> missingSprites;
        for (const QVector<PreviewTile> &square : grid.squares) {
            for (const PreviewTile &tile : square) {
                QString tilesetName;
                int tileIndex = -1;
                if (!splitSpriteName(tile.sprite, &tilesetName,
                                     &tileIndex)) {
                    missingSprites.insert(tile.sprite);
                    continue;
                }
                Tileset *tileset =
                        TileMetaInfoMgr::instance()->tileset(tilesetName);
                if (!tileset || tileIndex >= tileset->tileCount()) {
                    missingSprites.insert(tile.sprite);
                    continue;
                }
                requiredTilesets.insert(tileset);
            }
        }
        QList<Tileset *> required = requiredTilesets.values();
        if (!required.isEmpty()) {
            TileMetaInfoMgr::instance()->loadTilesets(required);
            TilesetManager::instance()->waitForTilesets(required, q);
        }

        for (const QVector<PreviewTile> &square : grid.squares) {
            for (const PreviewTile &tile : square) {
                Tile *resolved = tileForSprite(tile.sprite);
                if (!resolved || resolved->image().isNull())
                    missingSprites.insert(tile.sprite);
            }
        }
        canvas->setGrid(grid);
        squareDetails->clear();

        const QString warning = biome->mapBiome
                ? q->tr(" Map-biome preview is shown on an empty synthetic "
                        "surface; replacements and protections need a real "
                        "map selection for full fidelity.")
                : QString();
        const QString markerWarning = grid.markerCount
                ? q->tr(" %1 special marker(s), including subbiome/no_* "
                        "tokens, were not rendered in this first preview.")
                  .arg(grid.markerCount)
                : QString();
        const QString missingWarning = missingSprites.isEmpty()
                ? QString()
                : q->tr(" %1 sprite(s) could not be rendered: the referenced "
                        "Tiles sheet is missing, has incompatible JUMBO "
                        "geometry, or does not contain the requested index: "
                        "%2.")
                  .arg(missingSprites.size())
                  .arg(QStringList(missingSprites.values()).join(
                           QStringLiteral(", ")));
        setStatus(q->tr(
            "%1 features, %2 procedural biomes, %3 map biomes, "
            "%4 subbiomes. Representative preview seed %5 produced "
            "%6 concrete tile placements.%7%8%9")
                  .arg(definitions.features.size())
                  .arg(definitions.proceduralBiomes.size())
                  .arg(definitions.mapBiomes.size())
                  .arg(definitions.subBiomeCount)
                  .arg(seedSpin->value())
                  .arg(grid.concreteTileCount)
                  .arg(warning, markerWarning, missingWarning),
                  !missingSprites.isEmpty());
    }

    void showSquare(int x, int y)
    {
        squareDetails->clear();
        QTreeWidgetItem *position =
                new QTreeWidgetItem(squareDetails);
        position->setText(0, q->tr("Square"));
        position->setText(1, QStringLiteral("%1, %2").arg(x).arg(y));
        const QVector<PreviewTile> &tiles = grid.at(x, y);
        if (tiles.isEmpty()) {
            new QTreeWidgetItem(
                        squareDetails,
                        QStringList() << q->tr("Result")
                                      << q->tr("(no concrete tile)"));
            return;
        }
        for (const PreviewTile &tile : tiles) {
            QTreeWidgetItem *item =
                    new QTreeWidgetItem(squareDetails);
            item->setText(0, tile.category);
            item->setText(1, tile.sprite);
            item->setToolTip(
                        1, q->tr("Feature: %1\nProbability: %2")
                        .arg(tile.feature)
                        .arg(tile.probability, 0, 'g', 8));
            new QTreeWidgetItem(
                        item,
                        QStringList() << q->tr("Feature")
                                      << tile.feature);
            new QTreeWidgetItem(
                        item,
                        QStringList() << q->tr("Probability")
                                      << QString::number(
                                          tile.probability, 'g', 8));
            item->setExpanded(true);
        }
    }

    void setStatus(const QString &message, bool warning)
    {
        statusLabel->setText(message);
        QPalette statusPalette = statusLabel->palette();
        statusPalette.setColor(
                    QPalette::WindowText,
                    warning ? QColor(210, 95, 70)
                            : q->palette().color(QPalette::WindowText));
        statusLabel->setPalette(statusPalette);
    }

    QDialog *q;
    WorldDocument *worldDocument = nullptr;
    QString projectRootPath;
    Mode mode = BiomeMode;
    QLineEdit *pathEdit = nullptr;
    QLineEdit *projectPathEdit = nullptr;
    QPushButton *browseButton = nullptr;
    QPushButton *reloadButton = nullptr;
    QComboBox *typeCombo = nullptr;
    QComboBox *biomeCombo = nullptr;
    QComboBox *featureCombo = nullptr;
    QComboBox *prefabCombo = nullptr;
    QSpinBox *seedSpin = nullptr;
    QPushButton *regenerateButton = nullptr;
    QPushButton *newBiomeButton = nullptr;
    QPushButton *editBiomeButton = nullptr;
    QPushButton *newFeatureButton = nullptr;
    QPushButton *editFeatureButton = nullptr;
    QPushButton *newPrefabButton = nullptr;
    QPushButton *importPrefabButton = nullptr;
    QPushButton *editPrefabButton = nullptr;
    QPushButton *stagePrefabButton = nullptr;
    QMap<QString, QCheckBox *> categoryChecks;
    WorldGenPreviewCanvas *canvas = nullptr;
    QTreeWidget *inspector = nullptr;
    QTreeWidget *squareDetails = nullptr;
    QTreeWidget *prefabInspector = nullptr;
    QLabel *statusLabel = nullptr;
    WorldGenDefinitions definitions;
    PreviewGrid grid;
};

WorldGenPreviewDialog::WorldGenPreviewDialog(WorldDocument *worldDocument,
                                             QWidget *parent)
    : QDialog(parent)
    , d(new WorldGenPreviewDialogPrivate(
            this, worldDocument,
            WorldGenPreviewDialogPrivate::BiomeMode))
{
    d->buildInterface();
}

WorldGenPreviewDialog::~WorldGenPreviewDialog() = default;

WorldGenPrefabDialog::WorldGenPrefabDialog(
        WorldDocument *worldDocument, QWidget *parent)
    : QDialog(parent)
    , d(new WorldGenPreviewDialogPrivate(
            this, worldDocument,
            WorldGenPreviewDialogPrivate::PrefabMode))
{
    d->buildInterface();
}

WorldGenPrefabDialog::~WorldGenPrefabDialog() = default;

bool WorldGenPrefabDialog::renderValidationWindow(
        const QString &path, const QString &outputFile, QString *error)
{
    WorldGenPrefabDialog dialog(nullptr);
    dialog.d->pathEdit->setText(QDir::toNativeSeparators(path));
    dialog.d->reload();
    if (dialog.d->definitions.prefabs.isEmpty()) {
        if (error)
            *error = dialog.d->statusLabel->text();
        return false;
    }

    dialog.show();
    dialog.raise();
    QApplication::processEvents();
    const QPixmap preview = dialog.grab();
    if (preview.isNull() || !preview.save(outputFile, "PNG")) {
        if (error) {
            *error = tr("Could not save the WorldGen prefab-window "
                        "screenshot to %1.")
                    .arg(QDir::toNativeSeparators(outputFile));
        }
        return false;
    }
    dialog.close();
    return true;
}

bool WorldGenPreviewDialog::validateDefinitions(
        const QString &path, QString *summary, QString *error)
{
    WorldGenDefinitions definitions;
    if (!loadDefinitions(path, QString(), &definitions, error))
        return false;
    if (definitions.features.size() < 5
            || definitions.proceduralBiomes.isEmpty()
            || definitions.mapBiomes.isEmpty()) {
        if (error) {
            *error = tr("The definition set is incomplete: %1 features, "
                        "%2 procedural biomes, %3 map biomes.")
                    .arg(definitions.features.size())
                    .arg(definitions.proceduralBiomes.size())
                    .arg(definitions.mapBiomes.size());
        }
        return false;
    }

    const WorldGenBiome *sample = nullptr;
    if (definitions.proceduralBiomes.contains(
                QStringLiteral("pine_forest_boulder_none"))) {
        sample = &definitions.proceduralBiomes[
                QStringLiteral("pine_forest_boulder_none")];
    } else {
        sample = &definitions.proceduralBiomes.first();
    }
    const PreviewGrid grid =
            generatePreview(definitions, *sample, 42020);
    if (grid.concreteTileCount <= 0) {
        if (error)
            *error = tr("The sample biome produced no preview tiles.");
        return false;
    }

    if (summary) {
        *summary = tr("%1 biome features, %2 static prefabs, %3 procedural "
                     "biomes, %4 map biomes, %5 subbiomes; sample '%6': "
                     "%7 tiles")
                .arg(definitions.features.size())
                .arg(definitions.prefabs.size())
                .arg(definitions.proceduralBiomes.size())
                .arg(definitions.mapBiomes.size())
                .arg(definitions.subBiomeCount)
                .arg(sample->name)
                .arg(grid.concreteTileCount);
    }
    return true;
}

bool WorldGenPreviewDialog::validateProjectOverlay(
        const QString &gamePath, const QString &projectPath,
        QString *summary, QString *error)
{
    WorldGenDefinitions definitions;
    if (!loadDefinitions(gamePath, projectPath, &definitions, error))
        return false;

    int projectFeatures = 0;
    for (const WorldGenFeature &feature : definitions.features) {
        if (feature.projectOwned)
            ++projectFeatures;
    }
    int projectBiomes = 0;
    for (const WorldGenBiome &biome : definitions.proceduralBiomes) {
        if (biome.projectOwned)
            ++projectBiomes;
    }
    for (const WorldGenBiome &biome : definitions.mapBiomes) {
        if (biome.projectOwned)
            ++projectBiomes;
    }
    int projectPrefabs = 0;
    for (const WorldGenPrefab &prefab : definitions.prefabs) {
        if (prefab.projectOwned)
            ++projectPrefabs;
    }
    if (projectFeatures + projectPrefabs + projectBiomes <= 0) {
        if (error) {
            *error = tr("The project overlay loaded, but it did not provide "
                        "a biome feature, static prefab or biome.");
        }
        return false;
    }
    if (summary) {
        *summary = tr("%1 project biome feature(s), %2 project static "
                      "prefab(s), and %3 project biome(s) merged over %4 "
                      "game biome features, %5 game static prefabs, and %6 "
                      "total biomes")
                .arg(projectFeatures)
                .arg(projectPrefabs)
                .arg(projectBiomes)
                .arg(definitions.features.size() - projectFeatures)
                .arg(definitions.prefabs.size() - projectPrefabs)
                .arg(definitions.proceduralBiomes.size()
                      + definitions.mapBiomes.size());
    }
    return true;
}

bool WorldGenPreviewDialog::renderValidationPreview(
        const QString &path, const QString &outputFile, QString *error)
{
    WorldGenPreviewDialog dialog(nullptr);
    dialog.d->pathEdit->setText(QDir::toNativeSeparators(path));
    dialog.d->reload();
    if (dialog.d->definitions.features.isEmpty()) {
        if (error)
            *error = dialog.d->statusLabel->text();
        return false;
    }

    dialog.show();
    dialog.raise();
    QApplication::processEvents();
    const QPixmap preview = dialog.grab();
    if (preview.isNull() || !preview.save(outputFile, "PNG")) {
        if (error) {
            *error = tr("Could not save the WorldGen preview screenshot "
                        "to %1.")
                    .arg(QDir::toNativeSeparators(outputFile));
        }
        return false;
    }
    dialog.close();
    return true;
}

bool WorldGenPreviewDialog::renderValidationPrefabEditor(
        const QString &path, const QString &outputFile, QString *error)
{
    WorldGenDefinitions definitions;
    if (!loadDefinitions(path, QString(), &definitions, error))
        return false;
    if (definitions.prefabs.isEmpty()) {
        if (error)
            *error = tr("No static WorldGen prefab was loaded.");
        return false;
    }
    const QString preferred = QStringLiteral("highway_NS_00");
    const WorldGenPrefab prefab = definitions.prefabs.contains(preferred)
            ? definitions.prefabs.value(preferred)
            : definitions.prefabs.first();
    WorldGenPrefabEditorDialog dialog(prefab, false, nullptr);
    if (QTabWidget *tabs = dialog.findChild<QTabWidget *>())
        tabs->setCurrentIndex(1);
    dialog.show();
    dialog.raise();
    QApplication::processEvents();
    const QPixmap preview = dialog.grab();
    if (preview.isNull() || !preview.save(outputFile, "PNG")) {
        if (error) {
            *error = tr("Could not save the WorldGen prefab-editor screenshot "
                        "to %1.")
                    .arg(QDir::toNativeSeparators(outputFile));
        }
        return false;
    }
    dialog.close();
    return true;
}

bool WorldGenPreviewDialog::validatePrefabImport(
        const QString &fileName, QString *summary, QString *error)
{
    WorldGenPrefab prefab;
    QStringList warnings;
    if (!importPrefabSource(fileName, &prefab, &warnings, error))
        return false;
    int placements = 0;
    for (const QString &category : PrefabCategories) {
        for (int ref : prefab.schematic.value(category)) {
            if (ref > 0)
                ++placements;
        }
    }
    if (placements <= 0) {
        if (error)
            *error = tr("Imported prefab contains no tile placements.");
        return false;
    }
    if (summary) {
        *summary = tr("%1 x %2, %3 unique tiles, %4 placements, %5 warning(s)")
                .arg(prefab.width).arg(prefab.height)
                .arg(prefab.tiles.size()).arg(placements)
                .arg(warnings.size());
    }
    return true;
}
