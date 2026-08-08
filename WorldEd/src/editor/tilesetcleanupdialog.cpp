/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tilesetcleanupdialog.h"

#include "BuildingEditor/building.h"
#include "BuildingEditor/buildingreader.h"
#include "BuildingEditor/buildingwriter.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"

#include "compression.h"
#include "map.h"
#include "mapobject.h"
#include "mapreader.h"
#include "objectgroup.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedPointer>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

using namespace BuildingEditor;
using namespace Tiled;
using namespace Tiled::Internal;

namespace {

struct TmxDeclaration
{
    QString name;
    QString source;
    uint firstGid = 0;
    bool external = false;
};

struct TmxDecision
{
    QString name;
    uint firstGid = 0;
    uint nextFirstGid = 0;
    bool keep = true;
    bool clearReferences = false;
    QString replacementSource;
};

struct TmxDependency
{
    QString source;
    QString resolvedPath;
    QString normalizedSource;
    bool missing = false;
    bool outsideProject = false;
};

struct TbxStats
{
    int tileEntries = 0;
    int furniture = 0;
    int userTiles = 0;
    int version = 0;
    int nonCanonicalTileNames = 0;
};

QString normalizedTileName(const QString &tileName)
{
    const int separator = tileName.lastIndexOf(QLatin1Char('_'));
    if (separator <= 0 || separator + 1 >= tileName.size())
        return tileName;

    bool ok = false;
    const int index = tileName.mid(separator + 1).toInt(&ok);
    if (!ok || index < 0)
        return tileName;

    // BuildingEd deliberately pads these indices so the ID-backed tile list
    // remains naturally sorted by tileset and numeric tile index.
    return tileName.left(separator + 1)
            + QStringLiteral("%1").arg(index, 3, 10, QLatin1Char('0'));
}

QString tilesetNameFromTile(const QString &tileName)
{
    const int separator = tileName.lastIndexOf(QLatin1Char('_'));
    if (separator <= 0 || separator + 1 >= tileName.size())
        return QString();

    bool ok = false;
    tileName.mid(separator + 1).toInt(&ok);
    return ok ? tileName.left(separator) : QString();
}

QString referencedTilesetName(
        const QString &tileName,
        const QSet<QString> &declaredNames)
{
    if (declaredNames.contains(tileName))
        return tileName;
    const QString tilesetName = tilesetNameFromTile(tileName);
    return declaredNames.contains(tilesetName)
            ? tilesetName : QString();
}

void addTileReference(const QString &tileName,
                      const QSet<QString> &declaredNames,
                      QSet<QString> *names)
{
    const QString tilesetName =
            referencedTilesetName(tileName, declaredNames);
    if (!tilesetName.isEmpty())
        names->insert(tilesetName);
}

QSet<QString> protectedTmxTilesets(Map *map)
{
    QSet<QString> names;
    QSet<QString> declaredNames;
    for (Tileset *tileset : map->tilesets()) {
        if (tileset)
            declaredNames.insert(tileset->name());
    }
    const QSet<Tileset *> used = map->usedTilesets();
    for (Tileset *tileset : used) {
        if (tileset)
            names.insert(tileset->name());
    }

    const BmpSettings *settings = map->bmpSettings();
    for (const BmpAlias *alias : settings->aliases()) {
        for (const QString &tile : alias->tiles)
            addTileReference(tile, declaredNames, &names);
    }
    for (const BmpRule *rule : settings->rules()) {
        for (const QString &tile : rule->tileChoices)
            addTileReference(tile, declaredNames, &names);
    }
    for (const BmpBlend *blend : settings->blends()) {
        addTileReference(blend->mainTile, declaredNames, &names);
        addTileReference(blend->blendTile, declaredNames, &names);
        for (const QString &tile : blend->ExclusionList)
            addTileReference(tile, declaredNames, &names);
        for (int index = 0; index < blend->exclude2.size();
             index += 2) {
            addTileReference(
                        blend->exclude2.at(index),
                        declaredNames, &names);
        }
    }
    return names;
}

struct TmxReferenceCounts
{
    QHash<QString, int> tileCells;
    QHash<QString, int> tileObjects;
    QHash<QString, int> rulesAndBlends;
};

void countConfiguredReference(
        const QString &tileName,
        const QSet<QString> &declaredNames,
        QHash<QString, int> *counts)
{
    const QString tilesetName =
            referencedTilesetName(tileName, declaredNames);
    if (!tilesetName.isEmpty())
        ++(*counts)[tilesetName];
}

TmxReferenceCounts tmxReferenceCounts(Map *map)
{
    TmxReferenceCounts counts;
    QSet<QString> declaredNames;
    for (Tileset *tileset : map->tilesets()) {
        if (tileset)
            declaredNames.insert(tileset->name());
    }

    for (TileLayer *layer : map->tileLayers()) {
        if (!layer)
            continue;
        for (int y = 0; y < layer->height(); ++y) {
            for (int x = 0; x < layer->width(); ++x) {
                Tile *tile = layer->cellAt(x, y).tile;
                if (tile && tile->tileset())
                    ++counts.tileCells[tile->tileset()->name()];
            }
        }
    }
    for (ObjectGroup *group : map->objectGroups()) {
        if (!group)
            continue;
        for (MapObject *object : group->objects()) {
            Tile *tile = object ? object->tile() : nullptr;
            if (tile && tile->tileset())
                ++counts.tileObjects[tile->tileset()->name()];
        }
    }

    const BmpSettings *settings = map->bmpSettings();
    for (const BmpAlias *alias : settings->aliases()) {
        for (const QString &tile : alias->tiles) {
            countConfiguredReference(
                        tile, declaredNames,
                        &counts.rulesAndBlends);
        }
    }
    for (const BmpRule *rule : settings->rules()) {
        for (const QString &tile : rule->tileChoices) {
            countConfiguredReference(
                        tile, declaredNames,
                        &counts.rulesAndBlends);
        }
    }
    for (const BmpBlend *blend : settings->blends()) {
        countConfiguredReference(
                    blend->mainTile, declaredNames,
                    &counts.rulesAndBlends);
        countConfiguredReference(
                    blend->blendTile, declaredNames,
                    &counts.rulesAndBlends);
        for (const QString &tile : blend->ExclusionList) {
            countConfiguredReference(
                        tile, declaredNames,
                        &counts.rulesAndBlends);
        }
        for (int index = 0; index < blend->exclude2.size();
             index += 2) {
            countConfiguredReference(
                        blend->exclude2.at(index),
                        declaredNames,
                        &counts.rulesAndBlends);
        }
    }
    return counts;
}

QVector<TmxDeclaration> readTmxDeclarations(const QByteArray &bytes,
                                            QString *error)
{
    QVector<TmxDeclaration> declarations;
    QXmlStreamReader xml(bytes);
    int depth = 0;
    int current = -1;
    int tilesetDepth = -1;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (depth == 1 && xml.name() == QLatin1String("tileset")) {
                TmxDeclaration declaration;
                declaration.name = xml.attributes()
                        .value(QLatin1String("name")).toString();
                declaration.source = xml.attributes()
                        .value(QLatin1String("source")).toString();
                declaration.firstGid = xml.attributes()
                        .value(QLatin1String("firstgid")).toUInt();
                declaration.external = !declaration.source.isEmpty();
                declarations += declaration;
                current = declarations.size() - 1;
                tilesetDepth = depth;
            } else if (current >= 0 && depth == tilesetDepth + 1
                       && xml.name() == QLatin1String("image")) {
                declarations[current].source = xml.attributes()
                        .value(QLatin1String("source")).toString();
            }
            ++depth;
        } else if (xml.isEndElement()) {
            --depth;
            if (current >= 0 && depth == tilesetDepth) {
                current = -1;
                tilesetDepth = -1;
            }
        }
    }

    if (xml.hasError() && error)
        *error = xml.errorString();
    return declarations;
}

bool pathIsInside(const QString &fileName, const QString &root)
{
    const QString relative = QDir(root).relativeFilePath(fileName);
    return relative != QLatin1String("..")
            && !relative.startsWith(QLatin1String("../"))
            && !relative.startsWith(QLatin1String("..\\"));
}

QVector<TmxDependency> readTmxDependencies(
        const QByteArray &bytes,
        const QString &mapDirectory,
        const QString &scanRoot,
        QString *error)
{
    QVector<TmxDependency> dependencies;
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()
                || xml.name() != QLatin1String("object")) {
            continue;
        }

        const QString source = xml.attributes()
                .value(QLatin1String("type")).toString().trimmed();
        if (!source.endsWith(QLatin1String(".tbx"),
                             Qt::CaseInsensitive)) {
            continue;
        }

        TmxDependency dependency;
        dependency.source = source;
        const QString nativeSource =
                QDir::fromNativeSeparators(source);
        const QFileInfo sourceInfo(nativeSource);
        const QString absolute = sourceInfo.isAbsolute()
                ? sourceInfo.absoluteFilePath()
                : QDir(mapDirectory).absoluteFilePath(nativeSource);
        const QFileInfo resolvedInfo(QDir::cleanPath(absolute));
        dependency.missing = !resolvedInfo.exists()
                || !resolvedInfo.isFile();
        dependency.resolvedPath = resolvedInfo.absoluteFilePath();
        if (!dependency.missing) {
            const QString canonical = resolvedInfo.canonicalFilePath();
            if (!canonical.isEmpty())
                dependency.resolvedPath = canonical;
        }
        dependency.outsideProject =
                !pathIsInside(dependency.resolvedPath, scanRoot);
        dependency.normalizedSource = QDir::fromNativeSeparators(
                    QDir(mapDirectory).relativeFilePath(
                        dependency.resolvedPath));
        dependencies += dependency;
    }

    if (xml.hasError() && error)
        *error = xml.errorString();
    return dependencies;
}

bool writeStartElement(QXmlStreamWriter *writer,
                       const QXmlStreamReader &reader,
                       const QString &replacementAttribute = QString(),
                       const QString &replacementValue = QString())
{
    if (reader.namespaceUri().isEmpty())
        writer->writeStartElement(reader.name().toString());
    else
        writer->writeStartElement(reader.namespaceUri().toString(),
                                  reader.name().toString());

    for (const QXmlStreamNamespaceDeclaration &declaration
         : reader.namespaceDeclarations()) {
        writer->writeNamespace(declaration.namespaceUri().toString(),
                               declaration.prefix().toString());
    }

    for (const QXmlStreamAttribute &attribute : reader.attributes()) {
        QString value = attribute.value().toString();
        if (!replacementAttribute.isEmpty()
                && attribute.name() == replacementAttribute) {
            value = replacementValue;
        }
        if (attribute.namespaceUri().isEmpty()) {
            writer->writeAttribute(attribute.name().toString(), value);
        } else {
            writer->writeAttribute(attribute.namespaceUri().toString(),
                                   attribute.name().toString(), value);
        }
    }
    return true;
}

bool gidBelongsToRemovedTileset(
        uint rawGid,
        const QVector<TmxDecision> &decisions)
{
    const uint gid = rawGid & 0x1fffffffU;
    if (gid == 0)
        return false;

    for (const TmxDecision &decision : decisions) {
        if (!decision.clearReferences || decision.firstGid == 0
                || gid < decision.firstGid) {
            continue;
        }
        if (decision.nextFirstGid == 0
                || gid < decision.nextFirstGid) {
            return true;
        }
    }
    return false;
}

bool transformEncodedLayerData(
        const QString &text,
        const QString &encoding,
        const QString &compression,
        const QVector<TmxDecision> &decisions,
        QString *output,
        int *clearedCells,
        QString *error)
{
    if (encoding == QLatin1String("csv")) {
        QStringList gids = text.split(QLatin1Char(','));
        for (QString &token : gids) {
            bool ok = false;
            const uint gid = token.trimmed().toUInt(&ok);
            if (!ok) {
                if (error)
                    *error = QStringLiteral(
                            "Could not parse CSV tile GID while removing "
                            "an unresolved tileset.");
                return false;
            }
            if (gidBelongsToRemovedTileset(gid, decisions)) {
                token = QStringLiteral("0");
                ++(*clearedCells);
            }
        }
        *output = gids.join(QLatin1Char(','));
        return true;
    }

    if (encoding != QLatin1String("base64")) {
        if (error) {
            *error = QStringLiteral(
                    "Unsupported TMX layer encoding '%1' while removing "
                    "an unresolved tileset.")
                    .arg(encoding);
        }
        return false;
    }

    QByteArray bytes = QByteArray::fromBase64(text.toLatin1());
    if (compression == QLatin1String("gzip")
            || compression == QLatin1String("zlib")) {
        bytes = decompress(bytes);
    } else if (!compression.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                    "Unsupported TMX layer compression '%1' while removing "
                    "an unresolved tileset.")
                    .arg(compression);
        }
        return false;
    }
    if (bytes.isNull() || bytes.size() % 4 != 0) {
        if (error) {
            *error = QStringLiteral(
                    "Could not decode TMX layer data while removing "
                    "an unresolved tileset.");
        }
        return false;
    }

    for (int offset = 0; offset < bytes.size(); offset += 4) {
        const unsigned char *data =
                reinterpret_cast<const unsigned char *>(
                    bytes.constData() + offset);
        const uint gid = data[0]
                | (uint(data[1]) << 8)
                | (uint(data[2]) << 16)
                | (uint(data[3]) << 24);
        if (!gidBelongsToRemovedTileset(gid, decisions))
            continue;
        bytes[offset] = 0;
        bytes[offset + 1] = 0;
        bytes[offset + 2] = 0;
        bytes[offset + 3] = 0;
        ++(*clearedCells);
    }

    if (compression == QLatin1String("gzip"))
        bytes = compress(bytes, Gzip);
    else if (compression == QLatin1String("zlib"))
        bytes = compress(bytes, Zlib);
    if (bytes.isNull()) {
        if (error) {
            *error = QStringLiteral(
                    "Could not re-encode TMX layer data while removing "
                    "an unresolved tileset.");
        }
        return false;
    }
    *output = QString::fromLatin1(bytes.toBase64());
    return true;
}

bool writeTransformedLayerData(
        QXmlStreamReader *xml,
        QXmlStreamWriter *writer,
        const QVector<TmxDecision> &decisions,
        int *clearedCells,
        QString *error)
{
    const QString encoding = xml->attributes()
            .value(QLatin1String("encoding")).toString();
    const QString compression = xml->attributes()
            .value(QLatin1String("compression")).toString();
    writeStartElement(writer, *xml);

    if (encoding.isEmpty()) {
        while (!xml->atEnd()) {
            xml->readNext();
            if (xml->isEndElement()
                    && xml->name() == QLatin1String("data")) {
                writer->writeCurrentToken(*xml);
                return true;
            }
            if (xml->isStartElement()
                    && xml->name() == QLatin1String("tile")) {
                const QString raw = xml->attributes()
                        .value(QLatin1String("gid")).toString();
                bool ok = false;
                const uint gid = raw.toUInt(&ok);
                if (ok && gidBelongsToRemovedTileset(gid, decisions)) {
                    writeStartElement(
                                writer, *xml,
                                QStringLiteral("gid"),
                                QStringLiteral("0"));
                    ++(*clearedCells);
                    continue;
                }
            }
            writer->writeCurrentToken(*xml);
        }
    } else {
        QString encodedText;
        while (!xml->atEnd()) {
            xml->readNext();
            if (xml->isEndElement()
                    && xml->name() == QLatin1String("data")) {
                QString transformed;
                if (!transformEncodedLayerData(
                            encodedText, encoding, compression,
                            decisions, &transformed,
                            clearedCells, error)) {
                    return false;
                }
                writer->writeCharacters(transformed);
                writer->writeCurrentToken(*xml);
                return true;
            }
            if (xml->isCharacters())
                encodedText += xml->text().toString();
            else if (!xml->isWhitespace()) {
                if (error) {
                    *error = QStringLiteral(
                            "Unexpected XML inside encoded TMX layer data.");
                }
                return false;
            }
        }
    }

    if (error)
        *error = QStringLiteral("Unexpected end of TMX layer data.");
    return false;
}

bool transformTmx(const QByteArray &input,
                  const QVector<TmxDecision> &decisions,
                  const QHash<QString, QString> &tbxPathReplacements,
                  QByteArray *output,
                  int *clearedCells,
                  int *clearedObjects,
                  QString *error)
{
    QXmlStreamReader xml(input);
    QXmlStreamWriter writer(output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(1);
    bool clearTileReferences = false;
    for (const TmxDecision &decision : decisions) {
        if (decision.clearReferences) {
            clearTileReferences = true;
            break;
        }
    }

    int depth = 0;
    int decisionIndex = -1;
    int tilesetDepth = -1;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (depth == 1 && xml.name() == QLatin1String("tileset")) {
                ++decisionIndex;
                if (decisionIndex >= decisions.size()) {
                    if (error)
                        *error = QStringLiteral(
                                "TMX declaration count changed while rewriting.");
                    return false;
                }
                if (!decisions[decisionIndex].keep) {
                    xml.skipCurrentElement();
                    continue;
                }
                tilesetDepth = depth;
                writeStartElement(&writer, xml);
                ++depth;
                continue;
            }

            if (clearTileReferences
                    && xml.name() == QLatin1String("data")) {
                if (!writeTransformedLayerData(
                            &xml, &writer, decisions,
                            clearedCells, error)) {
                    return false;
                }
                continue;
            }

            if (clearTileReferences
                    && xml.name() == QLatin1String("object")) {
                bool ok = false;
                const uint gid = xml.attributes()
                        .value(QLatin1String("gid")).toUInt(&ok);
                if (ok && gidBelongsToRemovedTileset(
                            gid, decisions)) {
                    xml.skipCurrentElement();
                    ++(*clearedObjects);
                    continue;
                }
            }

            QString replacementAttribute;
            QString replacementValue;
            if (decisionIndex >= 0 && decisionIndex < decisions.size()
                    && tilesetDepth >= 0
                    && depth == tilesetDepth + 1
                    && xml.name() == QLatin1String("image")
                    && !decisions[decisionIndex]
                        .replacementSource.isEmpty()) {
                replacementAttribute = QStringLiteral("source");
                replacementValue =
                        decisions[decisionIndex].replacementSource;
            } else if (xml.name() == QLatin1String("object")) {
                const QString type = xml.attributes()
                        .value(QLatin1String("type")).toString();
                if (tbxPathReplacements.contains(type)) {
                    replacementAttribute = QStringLiteral("type");
                    replacementValue = tbxPathReplacements.value(type);
                }
            }
            writeStartElement(&writer, xml,
                              replacementAttribute, replacementValue);
            ++depth;
        } else if (xml.isEndElement()) {
            writer.writeCurrentToken(xml);
            --depth;
            if (tilesetDepth >= 0 && depth == tilesetDepth)
                tilesetDepth = -1;
        } else {
            writer.writeCurrentToken(xml);
        }
    }

    if (xml.hasError()) {
        if (error)
            *error = xml.errorString();
        return false;
    }
    if (decisionIndex + 1 != decisions.size()) {
        if (error)
            *error = QStringLiteral(
                    "TMX declaration count did not match the parsed map.");
        return false;
    }
    return true;
}

TbxStats readTbxStats(const QByteArray &bytes, QString *error)
{
    TbxStats stats;
    QXmlStreamReader xml(bytes);
    bool inUserTiles = false;
    int userTilesDepth = -1;
    int depth = 0;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("building")) {
                stats.version = xml.attributes()
                        .value(QLatin1String("version")).toInt();
            } else if (xml.name() == QLatin1String("tile_entry")) {
                ++stats.tileEntries;
            } else if (xml.name() == QLatin1String("furniture")) {
                ++stats.furniture;
            } else if (xml.name() == QLatin1String("user_tiles")) {
                inUserTiles = true;
                userTilesDepth = depth;
            } else if (inUserTiles && xml.name() == QLatin1String("tile")) {
                ++stats.userTiles;
            }

            QString tileName = xml.attributes()
                    .value(QLatin1String("tile")).toString();
            if (tileName.isEmpty()
                    && xml.name() == QLatin1String("tile")) {
                tileName = xml.attributes()
                        .value(QLatin1String("name")).toString();
            }
            if (!tileName.isEmpty()
                    && normalizedTileName(tileName) != tileName) {
                ++stats.nonCanonicalTileNames;
            }
            ++depth;
        } else if (xml.isEndElement()) {
            --depth;
            if (inUserTiles && depth == userTilesDepth) {
                inUserTiles = false;
                userTilesDepth = -1;
            }
        }
    }
    if (xml.hasError() && error)
        *error = xml.errorString();
    return stats;
}

QString actualTilesetPath(const QString &tilesetName)
{
    if (tilesetName == QLatin1String("INVISIBLE")
            || tilesetName == QLatin1String("MISSING")) {
        return QString();
    }

    QString path1x;
    QString path2x;
    if (!TilesetManager::instance()->getTilesetFileName(
                tilesetName, path1x, path2x)) {
        return QString();
    }
    if (QImageReader(path2x).size().isValid())
        return QFileInfo(path2x).absoluteFilePath();
    if (QImageReader(path1x).size().isValid())
        return QFileInfo(path1x).absoluteFilePath();
    return QString();
}

QString readableTilesetPath(Tileset *tileset,
                            const QString &tilesetName)
{
    QString path = actualTilesetPath(tilesetName);
    if (!path.isEmpty() || !tileset || tileset->isMissing())
        return path;

    const QStringList candidates =
            QStringList() << tileset->imageSource2x()
                          << tileset->imageSource();
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty()
                && QImageReader(candidate).size().isValid()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return QString();
}

bool isPlaceholderTileset(const QString &tilesetName)
{
    return tilesetName == QLatin1String("INVISIBLE")
            || tilesetName == QLatin1String("MISSING");
}

bool copyBackup(const QString &fileName,
                const QString &scanRoot,
                const QString &backupRoot,
                QString *error)
{
    if (backupRoot.isEmpty()) {
        if (error)
            *error = QStringLiteral("No backup directory was provided.");
        return false;
    }

    QString relative = QDir(scanRoot).relativeFilePath(fileName);
    if (relative.startsWith(QLatin1String("../"))
            || relative == QLatin1String("..")) {
        relative = QFileInfo(fileName).fileName();
    }
    const QString backupFile = QDir(backupRoot).filePath(relative);
    if (!QDir().mkpath(QFileInfo(backupFile).absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create backup directory: %1")
                    .arg(QFileInfo(backupFile).absolutePath());
        return false;
    }
    if (!QFile::copy(fileName, backupFile)) {
        if (error)
            *error = QStringLiteral("Could not back up %1 to %2")
                    .arg(fileName, backupFile);
        return false;
    }
    return true;
}

bool writeAtomic(const QString &fileName,
                 const QByteArray &bytes,
                 QString *error)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

TilesetCleanupResult processTmx(const QString &fileName,
                                const QString &scanRoot,
                                const TilesetCleanupOptions &options,
                                bool apply,
                                const QString &backupRoot)
{
    TilesetCleanupResult result;
    result.fileName = fileName;
    result.type = QStringLiteral("TMX");

    QFile source(fileName);
    if (!source.open(QIODevice::ReadOnly)) {
        result.error = source.errorString();
        return result;
    }
    const QByteArray input = source.readAll();
    source.close();

    QString declarationError;
    const QVector<TmxDeclaration> declarations =
            readTmxDeclarations(input, &declarationError);
    if (!declarationError.isEmpty()) {
        result.error = declarationError;
        return result;
    }

    const QString mapDirectory = QFileInfo(fileName).absolutePath();
    QString dependencyError;
    const QVector<TmxDependency> dependencies =
            readTmxDependencies(input, mapDirectory, scanRoot,
                                &dependencyError);
    if (!dependencyError.isEmpty()) {
        result.error = dependencyError;
        return result;
    }
    QHash<QString, QString> tbxPathReplacements;
    for (const TmxDependency &dependency : dependencies) {
        if (dependency.missing) {
            ++result.missingDependencies;
            result.dependencyWarnings += QStringLiteral(
                        "Missing TBX: %1 (resolved as %2)")
                    .arg(dependency.source,
                         QDir::toNativeSeparators(
                             dependency.resolvedPath));
            continue;
        }
        if (dependency.outsideProject) {
            ++result.externalDependencies;
            result.dependencyWarnings += QStringLiteral(
                        "TBX outside the selected project: %1 -> %2")
                    .arg(dependency.source,
                         QDir::toNativeSeparators(
                             dependency.resolvedPath));
            continue;
        }
        const QString current =
                QDir::fromNativeSeparators(dependency.source);
        if (options.normalizePaths
                && current != dependency.normalizedSource) {
            tbxPathReplacements[dependency.source] =
                    dependency.normalizedSource;
            ++result.normalized;
            result.pathChanges += QStringLiteral(
                        "TBX reference: %1 -> %2")
                    .arg(dependency.source,
                         dependency.normalizedSource);
        }
    }

    MapReader reader;
    QBuffer inputBuffer;
    inputBuffer.setData(input);
    inputBuffer.open(QIODevice::ReadOnly);
    QScopedPointer<Map> map(reader.readMap(
                &inputBuffer, QFileInfo(fileName).absolutePath()));
    if (!map) {
        result.error = reader.errorString();
        return result;
    }
    if (map->tilesets().size() != declarations.size()) {
        result.error = QStringLiteral(
                "Parsed %1 TMX declarations but the map exposes %2.")
                .arg(declarations.size()).arg(map->tilesets().size());
        return result;
    }

    const QSet<QString> protectedNames = protectedTmxTilesets(map.data());
    const TmxReferenceCounts referenceCounts =
            tmxReferenceCounts(map.data());
    QVector<TmxDecision> decisions;
    result.declared = declarations.size();

    for (int index = 0; index < declarations.size(); ++index) {
        Tileset *tileset = map->tilesets().at(index);
        const TmxDeclaration &declaration = declarations.at(index);
        TmxDecision decision;
        decision.name = tileset->name().isEmpty()
                ? declaration.name : tileset->name();
        decision.firstGid = declaration.firstGid;
        decision.nextFirstGid = index + 1 < declarations.size()
                ? declarations.at(index + 1).firstGid : 0;
        const bool used = protectedNames.contains(decision.name);
        const QString actual =
                readableTilesetPath(tileset, decision.name);
        const bool unresolved = actual.isEmpty()
                && !isPlaceholderTileset(decision.name);
        decision.keep = used || !actual.isEmpty()
                || isPlaceholderTileset(decision.name);

        // A valid but currently unused sheet remains in the complete ordered
        // TMX header for deterministic legacy-map compatibility. Cleanup only
        // removes a declaration when it is both unused and unresolved.
        if (!used && unresolved) {
            ++result.unused;
            result.removedNames += decision.name.isEmpty()
                    ? QStringLiteral("<external tileset %1>").arg(index + 1)
                    : decision.name;
            decisions += decision;
            continue;
        }

        if (used && unresolved
                && options.removeUnresolvedTilesets) {
            decision.keep = false;
            decision.clearReferences = true;
            ++result.unresolvedRemoved;
            result.unresolvedRemovedNames += decision.name;
            result.affectedTileCells +=
                    referenceCounts.tileCells.value(decision.name);
            result.affectedTileObjects +=
                    referenceCounts.tileObjects.value(decision.name);
            result.affectedRuleReferences +=
                    referenceCounts.rulesAndBlends.value(decision.name);
            decisions += decision;
            continue;
        }

        ++result.retained;
        if (!used)
            ++result.validUnusedKept;
        if (unresolved) {
            if (!isPlaceholderTileset(decision.name)) {
                ++result.missingUsed;
                result.missingNames += decision.name;
            }
        } else if (options.normalizePaths && !declaration.external) {
            const QString relative = QDir::fromNativeSeparators(
                        QDir(mapDirectory).relativeFilePath(actual));
            const QString current = QDir::fromNativeSeparators(
                        QDir::cleanPath(declaration.source));
            if (relative != current) {
                decision.replacementSource = relative;
                ++result.normalized;
                result.pathChanges += QStringLiteral("%1: %2 -> %3")
                        .arg(decision.name,
                             declaration.source.isEmpty()
                                ? QStringLiteral("<empty>")
                                : declaration.source,
                             relative);
            }
        }
        decisions += decision;
    }

    result.removedNames.removeDuplicates();
    result.removedNames.sort(Qt::CaseInsensitive);
    result.missingNames.removeDuplicates();
    result.missingNames.sort(Qt::CaseInsensitive);
    result.unresolvedRemovedNames.removeDuplicates();
    result.unresolvedRemovedNames.sort(Qt::CaseInsensitive);
    result.changed = result.unused > 0 || result.normalized > 0
            || result.unresolvedRemoved > 0;
    if (!apply || !result.changed)
        return result;

    QByteArray output;
    QString transformError;
    int clearedCells = 0;
    int clearedObjects = 0;
    if (!transformTmx(input, decisions, tbxPathReplacements,
                      &output, &clearedCells, &clearedObjects,
                      &transformError)) {
        result.error = transformError;
        return result;
    }
    if (clearedCells != result.affectedTileCells
            || clearedObjects != result.affectedTileObjects) {
        result.error = QStringLiteral(
                    "Reference-removal verification failed. Expected %1 "
                    "tile cell(s) and %2 tile object(s), but transformed "
                    "%3 and %4. No changes were written.")
                .arg(result.affectedTileCells)
                .arg(result.affectedTileObjects)
                .arg(clearedCells)
                .arg(clearedObjects);
        return result;
    }
    if (!copyBackup(fileName, scanRoot, backupRoot, &result.error))
        return result;
    if (!writeAtomic(fileName, output, &result.error))
        return result;
    result.applied = true;
    qInfo().noquote() << "Tileset cleanup applied to TMX:" << fileName
                      << "removed" << result.unused
                      << "unresolved-removed"
                      << result.unresolvedRemoved
                      << "normalized" << result.normalized
                      << "missing-used" << result.missingUsed;
    return result;
}

TilesetCleanupResult processTbx(const QString &fileName,
                                const QString &scanRoot,
                                bool apply,
                                const QString &backupRoot)
{
    TilesetCleanupResult result;
    result.fileName = fileName;
    result.type = QStringLiteral("TBX");

    QFile source(fileName);
    if (!source.open(QIODevice::ReadOnly)) {
        result.error = source.errorString();
        return result;
    }
    const QByteArray input = source.readAll();
    source.close();

    QString rawStatsError;
    const TbxStats rawStats = readTbxStats(input, &rawStatsError);
    if (!rawStatsError.isEmpty()) {
        result.error = rawStatsError;
        return result;
    }

    BuildingReader reader;
    QScopedPointer<Building> building(reader.read(fileName));
    if (!building) {
        result.error = reader.errorString();
        return result;
    }

    // TBX tile_entry, furniture and user_tiles elements are ordered ID tables.
    // Never remove XML elements directly: deserialize every numeric reference
    // to an object first, then let BuildingWriter rebuild all tables and remap
    // every reference exactly as a normal BuildingEd save does.
    reader.fix(building.data());
    QStringList tilesetNames = building->tilesetNames();
    tilesetNames.removeDuplicates();
    tilesetNames.sort(Qt::CaseInsensitive);
    for (const QString &name : tilesetNames) {
        if (actualTilesetPath(name).isEmpty()) {
            ++result.missingUsed;
            result.missingNames += name;
        }
    }

    QTemporaryDir canonicalDirectory;
    if (!canonicalDirectory.isValid()) {
        result.error = QStringLiteral(
                    "Could not create the temporary TBX output directory.");
        return result;
    }
    const QString canonicalPath = QDir(canonicalDirectory.path())
            .filePath(QFileInfo(fileName).fileName());
    BuildingWriter writer;
    if (!writer.write(building.data(), canonicalPath)) {
        result.error = writer.errorString();
        return result;
    }
    QFile canonicalFile(canonicalPath);
    if (!canonicalFile.open(QIODevice::ReadOnly)) {
        result.error = canonicalFile.errorString();
        return result;
    }
    const QByteArray canonical = canonicalFile.readAll();
    canonicalFile.close();

    // Prove that the regenerated ID tables are stable before offering them
    // for application. A second normal BuildingEd load/save must produce the
    // same bytes; otherwise an index/reference was not fully represented.
    BuildingReader verificationReader;
    QScopedPointer<Building> verificationBuilding(
                verificationReader.read(canonicalPath));
    if (!verificationBuilding) {
        result.error = QStringLiteral(
                    "The regenerated TBX could not be read: %1")
                .arg(verificationReader.errorString());
        return result;
    }
    verificationReader.fix(verificationBuilding.data());
    const QString verificationPath = QDir(canonicalDirectory.path())
            .filePath(QStringLiteral("verified-")
                      + QFileInfo(fileName).fileName());
    BuildingWriter verificationWriter;
    if (!verificationWriter.write(
                verificationBuilding.data(), verificationPath)) {
        result.error = verificationWriter.errorString();
        return result;
    }
    QFile verificationFile(verificationPath);
    if (!verificationFile.open(QIODevice::ReadOnly)) {
        result.error = verificationFile.errorString();
        return result;
    }
    const QByteArray verifiedCanonical = verificationFile.readAll();
    verificationFile.close();
    if (canonical != verifiedCanonical) {
        result.error = QStringLiteral(
                    "TBX ID-table remapping was not stable; no changes "
                    "were written.");
        return result;
    }

    QString canonicalStatsError;
    const TbxStats canonicalStats =
            readTbxStats(canonical, &canonicalStatsError);
    if (!canonicalStatsError.isEmpty()) {
        result.error = canonicalStatsError;
        return result;
    }

    result.declared = rawStats.tileEntries + rawStats.furniture
            + rawStats.userTiles;
    result.retained = canonicalStats.tileEntries + canonicalStats.furniture
            + canonicalStats.userTiles;
    result.unused = qMax(0, result.declared - result.retained);
    result.normalized = rawStats.nonCanonicalTileNames;
    result.formatUpdated = rawStats.version != 4;
    result.changed = result.unused > 0 || result.normalized > 0
            || result.formatUpdated;

    if (result.unused > 0) {
        result.removedNames += QStringLiteral(
                    "%1 unreferenced TBX tile/furniture definition(s)")
                .arg(result.unused);
    }
    if (!apply || !result.changed)
        return result;

    if (!copyBackup(fileName, scanRoot, backupRoot, &result.error))
        return result;
    if (!writeAtomic(fileName, canonical, &result.error))
        return result;
    result.applied = true;
    qInfo().noquote() << "Tileset cleanup applied to TBX:" << fileName
                      << "unreferenced definitions" << result.unused
                      << "normalized names" << result.normalized
                      << "missing-used" << result.missingUsed;
    return result;
}

QString plural(int count, const QString &singular, const QString &pluralText)
{
    return count == 1 ? singular : pluralText;
}

QString limitedList(const QStringList &values, int maximum = 25)
{
    if (values.size() <= maximum)
        return values.join(QStringLiteral(", "));
    return values.mid(0, maximum).join(QStringLiteral(", "))
            + QStringLiteral(", ... (%1 more)")
            .arg(values.size() - maximum);
}

} // namespace

QStringList TilesetCleanup::filesUnder(const QString &root, bool recursive)
{
    QStringList files;
    const QDirIterator::IteratorFlags flags = recursive
            ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator iterator(root,
                          QStringList() << QStringLiteral("*.tmx")
                                        << QStringLiteral("*.tbx"),
                          QDir::Files, flags);
    while (iterator.hasNext())
    {
        const QString fileName =
                QFileInfo(iterator.next()).absoluteFilePath();
        const QString normalized =
                QDir::fromNativeSeparators(fileName);
        if (normalized.contains(
                    QLatin1String("/.pztools-backups/"),
                    Qt::CaseInsensitive)) {
            continue;
        }
        files += fileName;
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

TilesetCleanupResult TilesetCleanup::processFile(
        const QString &fileName,
        const QString &scanRoot,
        const TilesetCleanupOptions &options,
        bool apply,
        const QString &backupRoot)
{
    if (fileName.endsWith(QLatin1String(".tmx"),
                          Qt::CaseInsensitive)) {
        return processTmx(fileName, scanRoot, options, apply, backupRoot);
    }
    if (fileName.endsWith(QLatin1String(".tbx"),
                          Qt::CaseInsensitive)) {
        return processTbx(fileName, scanRoot, apply, backupRoot);
    }

    TilesetCleanupResult result;
    result.fileName = fileName;
    result.error = QStringLiteral("Unsupported file extension.");
    return result;
}

QString TilesetCleanup::report(
        const QList<TilesetCleanupResult> &results,
        const QString &backupRoot)
{
    int changed = 0;
    int applied = 0;
    int errors = 0;
    int unused = 0;
    int validUnusedKept = 0;
    int normalized = 0;
    int missing = 0;
    int unresolvedRemoved = 0;
    int affectedTileCells = 0;
    int affectedTileObjects = 0;
    int affectedRuleReferences = 0;
    int missingDependencies = 0;
    int externalDependencies = 0;
    QStringList details;

    for (const TilesetCleanupResult &result : results) {
        if (result.changed)
            ++changed;
        if (result.applied)
            ++applied;
        if (!result.error.isEmpty())
            ++errors;
        unused += result.unused;
        validUnusedKept += result.validUnusedKept;
        normalized += result.normalized;
        missing += result.missingUsed;
        unresolvedRemoved += result.unresolvedRemoved;
        affectedTileCells += result.affectedTileCells;
        affectedTileObjects += result.affectedTileObjects;
        affectedRuleReferences += result.affectedRuleReferences;
        missingDependencies += result.missingDependencies;
        externalDependencies += result.externalDependencies;

        if (!result.changed && result.error.isEmpty()
                && result.missingUsed == 0
                && result.missingDependencies == 0
                && result.externalDependencies == 0) {
            continue;
        }

        details += QStringLiteral("%1 [%2]")
                .arg(result.fileName, result.type);
        if (!result.error.isEmpty()) {
            details += QStringLiteral("  ERROR: %1").arg(result.error);
            continue;
        }
        details += QStringLiteral(
                    "  declared=%1 retained=%2 stale-unused=%3 "
                    "valid-unused-kept=%4 normalized=%5 missing-used=%6 "
                    "unresolved-removed=%7 affected-cells=%8 "
                    "affected-objects=%9 affected-rules-blends=%10 "
                    "missing-TBX=%11 external-TBX=%12%13")
                .arg(result.declared)
                .arg(result.retained)
                .arg(result.unused)
                .arg(result.validUnusedKept)
                .arg(result.normalized)
                .arg(result.missingUsed)
                .arg(result.unresolvedRemoved)
                .arg(result.affectedTileCells)
                .arg(result.affectedTileObjects)
                .arg(result.affectedRuleReferences)
                .arg(result.missingDependencies)
                .arg(result.externalDependencies)
                .arg(result.applied
                     ? QStringLiteral(" APPLIED") : QString());
        if (result.formatUpdated)
            details += QStringLiteral("  TBX format will be upgraded to v4.");
        if (!result.removedNames.isEmpty())
            details += QStringLiteral("  Remove: %1")
                    .arg(limitedList(result.removedNames));
        if (!result.missingNames.isEmpty()) {
            details += QStringLiteral(
                        "  Keep but unresolved: %1")
                    .arg(limitedList(result.missingNames));
        }
        if (!result.unresolvedRemovedNames.isEmpty()) {
            details += QStringLiteral(
                        "  Remove unresolved by explicit option: %1")
                    .arg(limitedList(
                             result.unresolvedRemovedNames));
        }
        const int pathLimit = qMin(50, result.pathChanges.size());
        for (int index = 0; index < pathLimit; ++index) {
            details += QStringLiteral("  Path: %1")
                    .arg(result.pathChanges.at(index));
        }
        if (result.pathChanges.size() > pathLimit) {
            details += QStringLiteral("  ... %1 more path change(s)")
                    .arg(result.pathChanges.size() - pathLimit);
        }
        const int warningLimit =
                qMin(50, result.dependencyWarnings.size());
        for (int index = 0; index < warningLimit; ++index) {
            details += QStringLiteral("  WARNING: %1")
                    .arg(result.dependencyWarnings.at(index));
        }
        if (result.dependencyWarnings.size() > warningLimit) {
            details += QStringLiteral(
                        "  ... %1 more dependency warning(s)")
                    .arg(result.dependencyWarnings.size()
                         - warningLimit);
        }
    }

    QStringList summary;
    summary += QStringLiteral("Scanned %1 %2.")
            .arg(results.size())
            .arg(plural(results.size(),
                        QStringLiteral("file"),
                        QStringLiteral("files")));
    summary += QStringLiteral(
                "%1 file(s) need cleanup; %2 stale unused declaration(s); "
                "%3 path/name normalization(s).")
            .arg(changed).arg(unused).arg(normalized);
    summary += QStringLiteral(
                "%1 valid unused TMX declaration(s) were kept for "
                "legacy compatibility.")
            .arg(validUnusedKept);
    summary += QStringLiteral(
                "%1 used missing tileset reference(s) were preserved.")
            .arg(missing);
    summary += QStringLiteral(
                "%1 explicitly selected unresolved tileset declaration(s) "
                "will be removed. %2 tile cell(s) will be cleared, %3 tile "
                "object(s) will be removed, and %4 Rules/Blends reference(s) "
                "will remain embedded but inactive.")
            .arg(unresolvedRemoved)
            .arg(affectedTileCells)
            .arg(affectedTileObjects)
            .arg(affectedRuleReferences);
    summary += QStringLiteral(
                "%1 missing TBX dependency/dependencies; "
                "%2 TBX dependency/dependencies outside the project.")
            .arg(missingDependencies).arg(externalDependencies);
    if (applied > 0)
        summary += QStringLiteral("Applied changes to %1 file(s).").arg(applied);
    if (!backupRoot.isEmpty())
        summary += QStringLiteral("Backups: %1").arg(backupRoot);
    if (errors > 0)
        summary += QStringLiteral("%1 file(s) could not be processed.").arg(errors);
    summary += QString();
    summary += details;
    return summary.join(QLatin1Char('\n'));
}

bool TilesetCleanup::validate(QString *summary, QString *error)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create validation directory.");
        return false;
    }

    QString usedName;
    QString actualPath;
    for (Tileset *tileset : TileMetaInfoMgr::instance()->tilesets()) {
        actualPath = actualTilesetPath(tileset->name());
        if (!actualPath.isEmpty()) {
            usedName = tileset->name();
            break;
        }
    }
    if (usedName.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                    "No readable catalogue sheet is available for validation.");
        return false;
    }

    const QString tmxPath =
            QDir(temporary.path()).filePath(QStringLiteral("cleanup.tmx"));
    const QString missingUsedName =
            QStringLiteral("PZTools_used_missing_sheet");
    const QString dependencyDirectory = QDir(temporary.path())
            .filePath(QStringLiteral("dependencies"));
    if (!QDir().mkpath(dependencyDirectory)
            || !writeAtomic(QDir(dependencyDirectory)
                            .filePath(QStringLiteral("used.tbx")),
                            QByteArray("<building/>"), error)) {
        return false;
    }
    const QByteArray tmx = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<map version=\"2.0\" orientation=\"levelisometric\" width=\"1\" "
        "height=\"1\" tilewidth=\"64\" tileheight=\"32\">\n"
        " <tileset firstgid=\"1\" name=\"%1\" tilewidth=\"64\" "
        "tileheight=\"128\"><image source=\"stale/%1.png\" width=\"64\" "
        "height=\"128\"/></tileset>\n"
        " <tileset firstgid=\"2\" name=\"%2\" "
        "tilewidth=\"64\" tileheight=\"128\"><image "
        "source=\"stale/used-missing.png\" width=\"64\" "
        "height=\"128\"/></tileset>\n"
        " <tileset firstgid=\"3\" name=\"PZTools_unused_missing_sheet\" "
        "tilewidth=\"64\" tileheight=\"128\"><image "
        "source=\"stale/unused.png\" width=\"64\" height=\"128\"/></tileset>\n"
        " <layer name=\"Floor\" width=\"1\" height=\"1\" level=\"0\">"
        "<data encoding=\"csv\">2</data></layer>\n"
        " <objectgroup name=\"Buildings\" level=\"0\">"
        "<object id=\"1\" type=\".\\dependencies\\used.tbx\" "
        "x=\"0\" y=\"0\"/>"
        "<object id=\"2\" type=\"missing.tbx\" x=\"0\" y=\"0\"/>"
        "<object id=\"3\" name=\"tile-object\" gid=\"2\" x=\"0\" y=\"0\"/>"
        "</objectgroup>\n"
        " <bmp-settings version=\"1\">"
        "<rules-file file=\"\"/><blends-file file=\"\"/>"
        "<edges-everywhere value=\"false\"/><aliases/>"
        "<rules><rule label=\"missing\" bitmapIndex=\"0\" "
        "color=\"1 2 3\" tileChoices=\"%2_000\" "
        "targetLayer=\"Floor\" condition=\"0 0 0\"/></rules>"
        "<blends/></bmp-settings>\n"
        "</map>\n").arg(usedName, missingUsedName).toUtf8();
    if (!writeAtomic(tmxPath, tmx, error))
        return false;

    TilesetCleanupOptions options;
    TilesetCleanupResult before =
            processFile(tmxPath, temporary.path(), options, false);
    if (!before.error.isEmpty() || before.unused != 1
            || before.normalized != 2 || before.missingUsed != 1
            || before.unresolvedRemoved != 0
            || before.missingDependencies != 1) {
        if (error) {
            *error = QStringLiteral(
                    "TMX analysis mismatch: unused=%1 normalized=%2 "
                    "missing=%3 unresolved-removed=%4 missing-TBX=%5 "
                    "error=%6")
                    .arg(before.unused).arg(before.normalized)
                    .arg(before.missingUsed)
                    .arg(before.unresolvedRemoved)
                    .arg(before.missingDependencies)
                    .arg(before.error);
        }
        return false;
    }

    const QString backupRoot =
            QDir(temporary.path()).filePath(
                QStringLiteral(".pztools-backups/validation"));
    TilesetCleanupResult applied =
            processFile(tmxPath, temporary.path(), options, true, backupRoot);
    if (!applied.applied || !applied.error.isEmpty()) {
        if (error)
            *error = QStringLiteral("TMX apply failed: %1").arg(applied.error);
        return false;
    }
    TilesetCleanupResult after =
            processFile(tmxPath, temporary.path(), options, false);
    if (!after.error.isEmpty() || after.unused != 0
            || after.normalized != 0 || after.missingUsed != 1
            || after.changed) {
        if (error)
            *error = QStringLiteral(
                    "TMX was not clean after apply: %1").arg(after.error);
        return false;
    }

    TilesetCleanupOptions advancedOptions = options;
    advancedOptions.removeUnresolvedTilesets = true;
    TilesetCleanupResult advancedBefore =
            processFile(tmxPath, temporary.path(),
                        advancedOptions, false);
    if (!advancedBefore.error.isEmpty()
            || advancedBefore.unresolvedRemoved != 1
            || advancedBefore.affectedTileCells != 1
            || advancedBefore.affectedTileObjects != 1
            || advancedBefore.affectedRuleReferences != 1) {
        if (error) {
            *error = QStringLiteral(
                    "Advanced TMX analysis mismatch: removed=%1 "
                    "cells=%2 objects=%3 rules=%4 error=%5")
                    .arg(advancedBefore.unresolvedRemoved)
                    .arg(advancedBefore.affectedTileCells)
                    .arg(advancedBefore.affectedTileObjects)
                    .arg(advancedBefore.affectedRuleReferences)
                    .arg(advancedBefore.error);
        }
        return false;
    }

    const QString advancedBackupRoot =
            QDir(temporary.path()).filePath(
                QStringLiteral(".pztools-backups/advanced-validation"));
    TilesetCleanupResult advancedApplied =
            processFile(tmxPath, temporary.path(),
                        advancedOptions, true,
                        advancedBackupRoot);
    if (!advancedApplied.applied
            || !advancedApplied.error.isEmpty()
            || !QFileInfo(QDir(advancedBackupRoot)
                          .filePath(QStringLiteral("cleanup.tmx")))
                    .isFile()) {
        if (error) {
            *error = QStringLiteral(
                    "Advanced TMX apply or backup failed: %1")
                    .arg(advancedApplied.error);
        }
        return false;
    }

    QFile advancedFile(tmxPath);
    if (!advancedFile.open(QIODevice::ReadOnly)) {
        if (error)
            *error = advancedFile.errorString();
        return false;
    }
    const QByteArray advancedBytes = advancedFile.readAll();
    advancedFile.close();
    if (advancedBytes.contains(
                QByteArray("name=\"")
                + missingUsedName.toUtf8() + QByteArray("\""))
            || advancedBytes.contains("name=\"tile-object\"")
            || !advancedBytes.contains(
                QByteArray("tileChoices=\"")
                + missingUsedName.toUtf8()
                + QByteArray("_000\""))) {
        if (error) {
            *error = QStringLiteral(
                    "Advanced TMX removal did not preserve the intended "
                    "Rules definition or remove the selected declaration "
                    "and tile object.");
        }
        return false;
    }
    TilesetCleanupResult advancedAfter =
            processFile(tmxPath, temporary.path(), options, false);
    if (!advancedAfter.error.isEmpty()
            || advancedAfter.missingUsed != 0
            || advancedAfter.changed) {
        if (error) {
            *error = QStringLiteral(
                    "TMX was not clean after advanced removal: %1")
                    .arg(advancedAfter.error);
        }
        return false;
    }

    Building building(1, 1);
    const QString fixtureSourcePath = QDir(temporary.path())
            .filePath(QStringLiteral("cleanup-source.tbx"));
    BuildingWriter buildingWriter;
    if (!buildingWriter.write(&building, fixtureSourcePath)) {
        if (error)
            *error = buildingWriter.errorString();
        return false;
    }
    QFile fixtureSource(fixtureSourcePath);
    if (!fixtureSource.open(QIODevice::ReadOnly)) {
        if (error)
            *error = fixtureSource.errorString();
        return false;
    }
    const QByteArray canonicalTbx = fixtureSource.readAll();
    fixtureSource.close();

    QRegularExpression entryExpression(
                QStringLiteral("<tile_entry\\b[\\s\\S]*?</tile_entry>"));
    const QRegularExpressionMatch entryMatch =
            entryExpression.match(QString::fromUtf8(canonicalTbx));
    if (!entryMatch.hasMatch()) {
        if (error)
            *error = QStringLiteral(
                    "Could not construct a TBX cleanup fixture.");
        return false;
    }
    QByteArray dirtyTbx = canonicalTbx;
    const QByteArray duplicate = entryMatch.captured(0).toUtf8();
    const int insertAt = dirtyTbx.lastIndexOf("</building>");
    dirtyTbx.insert(insertAt, duplicate);
    const QString tbxPath =
            QDir(temporary.path()).filePath(QStringLiteral("cleanup.tbx"));
    if (!writeAtomic(tbxPath, dirtyTbx, error))
        return false;

    TilesetCleanupResult tbxBefore =
            processFile(tbxPath, temporary.path(), options, false);
    if (!tbxBefore.error.isEmpty() || tbxBefore.unused < 1) {
        if (error) {
            *error = QStringLiteral(
                    "TBX analysis did not find the unused definition: %1")
                    .arg(tbxBefore.error);
        }
        return false;
    }
    TilesetCleanupResult tbxApplied =
            processFile(tbxPath, temporary.path(), options, true, backupRoot);
    if (!tbxApplied.applied || !tbxApplied.error.isEmpty()) {
        if (error)
            *error = QStringLiteral("TBX apply failed: %1")
                    .arg(tbxApplied.error);
        return false;
    }
    TilesetCleanupResult tbxAfter =
            processFile(tbxPath, temporary.path(), options, false);
    if (!tbxAfter.error.isEmpty() || tbxAfter.unused != 0
            || tbxAfter.changed) {
        if (error)
            *error = QStringLiteral(
                    "TBX was not clean after apply: %1").arg(tbxAfter.error);
        return false;
    }

    for (const QString &fileName : filesUnder(temporary.path(), true)) {
        if (QDir::fromNativeSeparators(fileName).contains(
                    QLatin1String("/.pztools-backups/"),
                    Qt::CaseInsensitive)) {
            if (error) {
                *error = QStringLiteral(
                        "Recursive scans included a cleanup backup: %1")
                        .arg(fileName);
            }
            return false;
        }
    }

    if (summary) {
        *summary = QStringLiteral(
                    "TMX unused declarations, active path normalization, "
                    "default missing-used and missing-TBX preservation, "
                    "explicit unresolved-reference removal, atomic backups, "
                    "and TBX "
                    "ordered ID-table remapping and canonical pruning "
                    "passed.");
    }
    return true;
}

TilesetCleanupDialog::TilesetCleanupDialog(
        const QString &initialRoot,
        const QString &projectFile,
        QWidget *parent)
    : QDialog(parent),
      mProjectFile(projectFile)
{
    setWindowTitle(tr("Project Doctor - Tiles and Paths"));
    resize(1080, 700);

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
                tr("<b>A plain-language health check for the mapping project.</b> "
                   "TMX files are maps opened in TileZed. TBX files are "
                   "buildings opened in BuildingEd. This WorldEd tool checks "
                   "both formats and their paths for Project Zomboid "
                   "Build 42. Start with the summary below; support details "
                   "stay hidden unless you need them."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    mStatusTitle = new QLabel(tr("Ready to check"), this);
    mStatusTitle->setStyleSheet(
                QStringLiteral("font-size: 20px; font-weight: 600; "
                               "color: #2f6fbb;"));
    layout->addWidget(mStatusTitle);
    mStatusDetails = new QLabel(
                tr("Nothing is changed during Check project."), this);
    mStatusDetails->setWordWrap(true);
    layout->addWidget(mStatusDetails);

    auto *rootRow = new QHBoxLayout;
    mRootEdit = new QLineEdit(QDir::toNativeSeparators(initialRoot), this);
    auto *browseButton = new QPushButton(tr("Browse..."), this);
    rootRow->addWidget(new QLabel(tr("Project folder:"), this));
    rootRow->addWidget(mRootEdit, 1);
    rootRow->addWidget(browseButton);
    layout->addLayout(rootRow);

    auto *optionsRow = new QHBoxLayout;
    mRecursiveCheck = new QCheckBox(
                tr("Check every map and building (recommended)"), this);
    mRecursiveCheck->setChecked(true);
    mNormalizeCheck = new QCheckBox(
                tr("Repair safe tile and TBX paths (recommended)"),
                this);
    mNormalizeCheck->setChecked(true);
    optionsRow->addWidget(mRecursiveCheck);
    optionsRow->addWidget(mNormalizeCheck);
    optionsRow->addStretch();
    layout->addLayout(optionsRow);

    mRemoveUnresolvedCheck = new QCheckBox(
                tr("Remove referenced tilesets whose PNG cannot be "
                   "resolved (advanced)"), this);
    mRemoveUnresolvedCheck->setChecked(false);
    mRemoveUnresolvedCheck->setToolTip(
                tr("Removes unresolved TMX tileset declarations even when "
                   "map cells, tile objects, Rules, or Blends still refer "
                   "to them. Affected cells are cleared, affected tile "
                   "objects are removed, and Rules/Blends definitions stay "
                   "embedded but inactive. A dated backup is mandatory."));
    layout->addWidget(mRemoveUnresolvedCheck);

    auto *safety = new QLabel(
                tr("Check is read-only. Applying fixes always creates a dated "
                   ".pztools-backups copy first and never changes game files. "
                   "Close open TMX/TBX tabs before applying a fix."),
                this);
    safety->setWordWrap(true);
    layout->addWidget(safety);

    auto *summaryLabel = new QLabel(tr("<b>What the doctor found</b>"), this);
    layout->addWidget(summaryLabel);
    mSummaryTable = new QTableWidget(this);
    mSummaryTable->setColumnCount(4);
    mSummaryTable->setHorizontalHeaderLabels(
                QStringList() << tr("Status") << tr("File")
                              << tr("What this means")
                              << tr("What you can do"));
    mSummaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mSummaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mSummaryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mSummaryTable->setAlternatingRowColors(true);
    mSummaryTable->setWordWrap(true);
    mSummaryTable->verticalHeader()->hide();
    mSummaryTable->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::ResizeToContents);
    mSummaryTable->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::ResizeToContents);
    mSummaryTable->horizontalHeader()->setSectionResizeMode(
                2, QHeaderView::Stretch);
    mSummaryTable->horizontalHeader()->setSectionResizeMode(
                3, QHeaderView::Stretch);
    layout->addWidget(mSummaryTable, 1);

    mDetailsButton = new QPushButton(tr("Show support details"), this);
    mDetailsButton->setCheckable(true);
    mDetailsButton->setToolTip(
                tr("Show the complete technical report to copy when asking "
                   "for support."));
    layout->addWidget(mDetailsButton);

    auto *technicalLabel = new QLabel(
                tr("<b>Technical details</b> - copy this section when "
                   "asking for support."), this);
    layout->addWidget(technicalLabel);
    mReport = new QPlainTextEdit(this);
    mReport->setReadOnly(true);
    mReport->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(mReport, 1);
    mTechnicalLabel = technicalLabel;
    mTechnicalLabel->setText(
                tr("<b>Technical support report</b> - copy this section when "
                   "asking for help."));
    mTechnicalLabel->hide();
    mReport->hide();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    mAnalyzeButton = buttons->addButton(
                tr("Check project"), QDialogButtonBox::ActionRole);
    mApplyButton = buttons->addButton(
                tr("Fix safely..."), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked,
            this, &TilesetCleanupDialog::browse);
    connect(mAnalyzeButton, &QPushButton::clicked,
            this, &TilesetCleanupDialog::analyze);
    connect(mApplyButton, &QPushButton::clicked,
            this, &TilesetCleanupDialog::applyCleanup);
    connect(mDetailsButton, &QPushButton::toggled,
            this, [this](bool shown) {
        mTechnicalLabel->setVisible(shown);
        mReport->setVisible(shown);
        mDetailsButton->setText(
                    shown ? tr("Hide support details")
                          : tr("Show support details"));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(mRootEdit, &QLineEdit::textChanged,
            this, [this]() {
        mResults.clear();
        mProjectWarnings.clear();
        mReport->clear();
        updateStatus();
        updateActions();
    });
    connect(mRecursiveCheck, &QCheckBox::toggled,
            this, &TilesetCleanupDialog::analyze);
    connect(mNormalizeCheck, &QCheckBox::toggled,
            this, &TilesetCleanupDialog::analyze);
    connect(mRemoveUnresolvedCheck, &QCheckBox::toggled,
            this, &TilesetCleanupDialog::analyze);
    updateActions();
    updateStatus();
}

void TilesetCleanupDialog::browse()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Select project or map folder"),
                mRootEdit->text());
    if (!directory.isEmpty()) {
        mRootEdit->setText(QDir::toNativeSeparators(directory));
        analyze();
    }
}

TilesetCleanupOptions TilesetCleanupDialog::options() const
{
    TilesetCleanupOptions value;
    value.recursive = mRecursiveCheck->isChecked();
    value.normalizePaths = mNormalizeCheck->isChecked();
    value.removeUnresolvedTilesets =
            mRemoveUnresolvedCheck->isChecked();
    return value;
}

QList<TilesetCleanupResult> TilesetCleanupDialog::run(
        bool apply, const QString &backupRoot)
{
    const QString root = QFileInfo(mRootEdit->text()).absoluteFilePath();
    const QStringList files =
            TilesetCleanup::filesUnder(root, mRecursiveCheck->isChecked());
    QList<TilesetCleanupResult> results;

    QProgressDialog progress(
                apply ? tr("Applying tileset cleanup...")
                      : tr("Analyzing tileset references..."),
                tr("Cancel"), 0, files.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    for (int index = 0; index < files.size(); ++index) {
        progress.setValue(index);
        progress.setLabelText(
                    tr("%1\n%2 / %3")
                    .arg(QDir::toNativeSeparators(files.at(index)))
                    .arg(index + 1).arg(files.size()));
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;
        results += TilesetCleanup::processFile(
                    files.at(index), root, options(), apply, backupRoot);
    }
    progress.setValue(files.size());
    return results;
}

QStringList TilesetCleanupDialog::projectPathWarnings() const
{
    QStringList warnings;
    const QString root =
            QFileInfo(mRootEdit->text()).absoluteFilePath();
    const QString normalizedRoot =
            QDir::fromNativeSeparators(root).toLower();
    if (normalizedRoot.contains(QLatin1String("/downloads/"))) {
        warnings += tr("The project is inside Downloads. Move it to a "
                       "dedicated working folder before editing.");
    }
    if (normalizedRoot.contains(QLatin1String("/onedrive/"))) {
        warnings += tr("The project is inside OneDrive. Sync can lock or "
                       "replace files while the tools are saving them.");
    }
    if (normalizedRoot.contains(
                QLatin1String("/steamapps/common/projectzomboid/"))) {
        warnings += tr("The project is inside the game installation. Keep "
                       "mapping sources in a separate project folder.");
    }

    QString projectFile = mProjectFile;
    if (projectFile.isEmpty()) {
        const QStringList projects = QDir(root).entryList(
                    QStringList() << QStringLiteral("*.pzw"),
                    QDir::Files);
        if (projects.size() == 1)
            projectFile = QDir(root).filePath(projects.first());
    }
    if (projectFile.isEmpty()) {
        warnings += tr("INFO: No .pzw project is loaded; this is a folder-only "
                       "check.");
        return warnings;
    }

    QFile source(projectFile);
    if (!source.open(QIODevice::ReadOnly)) {
        warnings += tr("The loaded .pzw project could not be read: %1")
                .arg(QDir::toNativeSeparators(projectFile));
        return warnings;
    }

    const QString projectDirectory =
            QFileInfo(projectFile).absolutePath();
    QXmlStreamReader xml(&source);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        QString label;
        QString path;
        bool mustExist = false;
        bool mustStayInProject = false;
        if (xml.name() == QLatin1String("cell")) {
            label = tr("TMX map");
            path = xml.attributes().value(
                        QLatin1String("map")).toString();
            mustExist = true;
            mustStayInProject = true;
        } else if (xml.name() == QLatin1String("bmp")) {
            label = tr("source BMP");
            path = xml.attributes().value(
                        QLatin1String("path")).toString();
            mustExist = true;
            mustStayInProject = true;
        } else if (xml.name() == QLatin1String("rulesfile")
                   || xml.name() == QLatin1String("blendsfile")
                   || xml.name() == QLatin1String("mapbasefile")) {
            label = xml.name().toString();
            path = xml.attributes().value(
                        QLatin1String("path")).toString();
            mustExist = !path.isEmpty();
            mustStayInProject = !path.isEmpty();
        } else if (xml.name() == QLatin1String("tmxexportdir")) {
            label = tr("TMX export folder");
            path = xml.attributes().value(
                        QLatin1String("path")).toString();
            mustStayInProject = !path.isEmpty();
        } else if (xml.name() == QLatin1String("TileDefFolder")) {
            label = tr("game media folder");
            path = xml.attributes().value(
                        QLatin1String("path")).toString();
            mustExist = !path.isEmpty();
        } else {
            continue;
        }
        if (path.isEmpty())
            continue;

        const QString portablePath =
                QDir::fromNativeSeparators(path);
        const QFileInfo pathInfo(portablePath);
        const QString resolved = QDir::cleanPath(
                    pathInfo.isAbsolute()
                    ? pathInfo.absoluteFilePath()
                    : QDir(projectDirectory).absoluteFilePath(
                        portablePath));
        if (mustExist && !QFileInfo::exists(resolved)) {
            warnings += tr("%1 is missing: %2")
                    .arg(label, QDir::toNativeSeparators(resolved));
        }
        if (mustStayInProject && !pathIsInside(resolved, root)) {
            warnings += tr("%1 is outside the project folder: %2")
                    .arg(label, QDir::toNativeSeparators(resolved));
        }
        if (mustStayInProject && pathInfo.isAbsolute()) {
            warnings += tr("%1 uses an absolute path and will break if the "
                           "project is moved: %2")
                    .arg(label, QDir::toNativeSeparators(path));
        }
    }
    if (xml.hasError()) {
        warnings += tr("The .pzw project path check stopped on invalid XML: %1")
                .arg(xml.errorString());
    }
    warnings.removeDuplicates();
    return warnings;
}

QString TilesetCleanupDialog::fullReport(
        const QString &backupRoot) const
{
    QStringList sections;
    if (!mProjectWarnings.isEmpty()) {
        sections += tr("PROJECT PATH CHECK");
        for (const QString &warning : mProjectWarnings)
            sections += QStringLiteral("  ") + warning;
        sections += QString();
    }
    sections += TilesetCleanup::report(mResults, backupRoot);
    return sections.join(QLatin1Char('\n'));
}

void TilesetCleanupDialog::updateStatus()
{
    updateSummaryTable();
    if (mResults.isEmpty()) {
        mStatusTitle->setText(tr("Ready to check"));
        mStatusTitle->setStyleSheet(
                    QStringLiteral("font-size: 20px; font-weight: 600; "
                                   "color: #2f6fbb;"));
        mStatusDetails->setText(
                    tr("Choose the folder containing the .pzw project, then "
                       "click Check project. Nothing will be changed."));
        return;
    }

    int errors = 0;
    int changed = 0;
    int missingTilesets = 0;
    int unresolvedRemoved = 0;
    int affectedTileCells = 0;
    int affectedTileObjects = 0;
    int affectedRuleReferences = 0;
    int missingDependencies = 0;
    int externalDependencies = 0;
    int applied = 0;
    int tmxFiles = 0;
    int tbxFiles = 0;
    for (const TilesetCleanupResult &result : mResults) {
        errors += !result.error.isEmpty() ? 1 : 0;
        changed += result.changed ? 1 : 0;
        missingTilesets += result.missingUsed;
        unresolvedRemoved += result.unresolvedRemoved;
        affectedTileCells += result.affectedTileCells;
        affectedTileObjects += result.affectedTileObjects;
        affectedRuleReferences += result.affectedRuleReferences;
        missingDependencies += result.missingDependencies;
        externalDependencies += result.externalDependencies;
        applied += result.applied ? 1 : 0;
        tmxFiles += result.type == QLatin1String("TMX") ? 1 : 0;
        tbxFiles += result.type == QLatin1String("TBX") ? 1 : 0;
    }
    int projectWarnings = 0;
    for (const QString &warning : mProjectWarnings) {
        if (!warning.startsWith(QLatin1String("INFO:")))
            ++projectWarnings;
    }

    if (applied > 0) {
        mStatusTitle->setText(
                    unresolvedRemoved > 0
                    ? tr("Selected fixes applied")
                    : tr("Safe fixes applied"));
        mStatusTitle->setStyleSheet(
                    QStringLiteral("font-size: 20px; font-weight: 600; "
                                   "color: #26734d;"));
        mStatusDetails->setText(
                    tr("%1 file(s) fixed. A dated backup was created. "
                       "Close and reopen maps or buildings that were open.")
                    .arg(applied));
    } else if (unresolvedRemoved > 0) {
        mStatusTitle->setText(tr("Advanced removal selected"));
        mStatusTitle->setStyleSheet(
                    QStringLiteral("font-size: 20px; font-weight: 600; "
                                   "color: #a34b16;"));
        mStatusDetails->setText(
                    tr("%1 unresolved tileset declaration(s) can be removed. "
                       "%2 tile cell(s) will be cleared, %3 tile object(s) "
                       "will be removed, and %4 Rules/Blends reference(s) "
                       "will stay embedded but inactive. "
                       "Review the affected names before applying.")
                    .arg(unresolvedRemoved)
                    .arg(affectedTileCells)
                    .arg(affectedTileObjects)
                    .arg(affectedRuleReferences));
    } else if (errors + missingTilesets + missingDependencies
               + externalDependencies + projectWarnings > 0) {
        mStatusTitle->setText(tr("A few items need your help"));
        mStatusTitle->setStyleSheet(
                    QStringLiteral("font-size: 20px; font-weight: 600; "
                                   "color: #a34b16;"));
        mStatusDetails->setText(
                    tr("%1 missing tile reference(s), %2 missing TBX file(s), "
                       "%3 file(s) outside the project, %4 path warning(s), "
                       "and %5 unreadable file(s). They were preserved, and "
                       "the project was not modified. Read the recommended "
                       "next steps below.")
                    .arg(missingTilesets).arg(missingDependencies)
                    .arg(externalDependencies).arg(projectWarnings)
                    .arg(errors));
    } else if (changed > 0) {
        mStatusTitle->setText(tr("Safe cleanup available"));
        mStatusTitle->setStyleSheet(
                    QStringLiteral("font-size: 20px; font-weight: 600; "
                                   "color: #2f6fbb;"));
        mStatusDetails->setText(
                    tr("%1 of %2 checked files can be cleaned or repaired. "
                       "Review the summary below, then use Fix safely.")
                    .arg(changed).arg(mResults.size()));
    } else {
        mStatusTitle->setText(tr("Project looks clean"));
        mStatusTitle->setStyleSheet(
                    QStringLiteral("font-size: 20px; font-weight: 600; "
                                   "color: #26734d;"));
        mStatusDetails->setText(
                    tr("%1 TMX map(s) and %2 TBX building(s) checked. "
                       "No cleanup is needed.")
                    .arg(tmxFiles).arg(tbxFiles));
    }
}

void TilesetCleanupDialog::updateSummaryTable()
{
    mSummaryTable->setRowCount(0);

    const QString root =
            QFileInfo(mRootEdit->text()).absoluteFilePath();
    int cleanFiles = 0;

    const auto addRow = [this](
            const QString &status, const QColor &color,
            const QString &file, const QString &meaning,
            const QString &nextStep) {
        const int row = mSummaryTable->rowCount();
        mSummaryTable->insertRow(row);
        const QStringList values =
                QStringList() << status << file << meaning << nextStep;
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setToolTip(values.at(column));
            if (column == 0) {
                item->setForeground(color);
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            mSummaryTable->setItem(row, column, item);
        }
    };

    for (const QString &warning : mProjectWarnings) {
        const bool information =
                warning.startsWith(QLatin1String("INFO:"));
        const QString message = information
                ? warning.mid(5).trimmed() : warning;
        addRow(information ? tr("Information") : tr("Your input needed"),
               information ? QColor(47, 111, 187) : QColor(163, 75, 22),
               tr("Project folder"), message,
               information
                    ? tr("Nothing to do.")
                    : tr("Move or repair this path before sharing or "
                         "updating the project."));
    }

    for (const TilesetCleanupResult &result : mResults) {
        QString displayName =
                QDir(root).relativeFilePath(result.fileName);
        if (displayName.startsWith(QLatin1String("../"))
                || displayName == QLatin1String("..")) {
            displayName = result.fileName;
        }
        displayName = QDir::toNativeSeparators(displayName);

        if (!result.error.isEmpty()) {
            addRow(tr("Could not check"), QColor(176, 45, 45),
                   displayName,
                   tr("The file could not be read safely: %1")
                        .arg(result.error),
                   tr("Open it in %1 and repair the reported error. Include "
                      "the support details if you ask for help.")
                        .arg(result.type == QLatin1String("TBX")
                             ? tr("BuildingEd") : tr("TileZed")));
            continue;
        }

        QStringList manualFindings;
        if (result.missingUsed > 0) {
            manualFindings += tr("%1 used tileset reference(s) cannot be "
                                 "resolved.")
                    .arg(result.missingUsed);
        }
        if (result.missingDependencies > 0) {
            manualFindings += tr("%1 referenced TBX building file(s) are "
                                 "missing.")
                    .arg(result.missingDependencies);
        }
        if (result.externalDependencies > 0) {
            manualFindings += tr("%1 TBX building reference(s) point outside "
                                 "the project.")
                    .arg(result.externalDependencies);
        }

        QStringList safeFindings;
        if (result.unused > 0) {
            safeFindings += tr("%1 stale unused definition(s) can be removed.")
                    .arg(result.unused);
        }
        if (result.normalized > 0) {
            safeFindings += tr("%1 safe path or tile-name repair(s) are "
                               "available.")
                    .arg(result.normalized);
        }
        if (result.formatUpdated)
            safeFindings += tr("The TBX can be upgraded to the current format.");

        QStringList advancedFindings;
        if (result.unresolvedRemoved > 0) {
            advancedFindings += tr(
                        "%1 referenced tileset declaration(s) have no "
                        "readable PNG and are selected for removal. "
                        "%2 tile cell(s) will be cleared, %3 tile object(s) "
                        "will be removed, and %4 Rules/Blends reference(s) "
                        "will remain embedded but inactive.")
                    .arg(result.unresolvedRemoved)
                    .arg(result.affectedTileCells)
                    .arg(result.affectedTileObjects)
                    .arg(result.affectedRuleReferences);
        }

        if (result.applied) {
            QStringList appliedFindings = safeFindings;
            appliedFindings += advancedFindings;
            addRow(result.unresolvedRemoved > 0
                        ? tr("Applied with backup")
                        : tr("Fixed safely"),
                   QColor(38, 115, 77),
                   displayName,
                   appliedFindings.isEmpty()
                        ? tr("Selected repairs were applied.")
                        : appliedFindings.join(QLatin1Char(' ')),
                   tr("Reopen this map or building if it was already open."));
        } else if (!advancedFindings.isEmpty()) {
            QString meaning =
                    advancedFindings.join(QLatin1Char(' '));
            if (!safeFindings.isEmpty())
                meaning += QLatin1Char(' ')
                        + safeFindings.join(QLatin1Char(' '));
            addRow(tr("Advanced removal"), QColor(163, 75, 22),
                   displayName, meaning,
                   tr("Apply only if these PNG files no longer exist and "
                      "the affected map content is intentionally obsolete. "
                      "A dated backup is created first."));
        } else if (!manualFindings.isEmpty()) {
            QString meaning = manualFindings.join(QLatin1Char(' '));
            if (!safeFindings.isEmpty())
                meaning += QLatin1Char(' ') + safeFindings.join(QLatin1Char(' '));
            addRow(tr("Your input needed"), QColor(163, 75, 22),
                   displayName, meaning,
                   result.changed
                        ? tr("Use Fix safely for the automatic repairs, then "
                             "restore or replace the missing references.")
                        : tr("Restore, copy into the project, or replace the "
                             "missing references."));
        } else if (result.changed) {
            addRow(tr("Safe fix available"), QColor(47, 111, 187),
                   displayName, safeFindings.join(QLatin1Char(' ')),
                   tr("Use Fix safely. A dated backup is created first."));
        } else {
            ++cleanFiles;
        }
    }

    if (cleanFiles > 0) {
        addRow(tr("Looks good"), QColor(38, 115, 77),
               tr("%1 other checked file(s)").arg(cleanFiles),
               tr("No problem or cleanup was found."),
               tr("Nothing to do."));
    }

    if (mSummaryTable->rowCount() == 0) {
        addRow(tr("Not checked yet"), QColor(47, 111, 187),
               tr("Project"), tr("No scan has been run."),
               tr("Click Check project. This first pass is read-only."));
    }
    mSummaryTable->resizeRowsToContents();
}

void TilesetCleanupDialog::analyze()
{
    const QFileInfo root(mRootEdit->text());
    if (!root.exists() || !root.isDir()) {
        mResults.clear();
        mProjectWarnings.clear();
        mReport->setPlainText(tr("Select an existing project or map folder."));
        updateStatus();
        updateActions();
        return;
    }
    mResults = run(false);
    mProjectWarnings = projectPathWarnings();
    mReport->setPlainText(fullReport());
    updateStatus();
    updateActions();
}

int TilesetCleanupDialog::changedFileCount() const
{
    int count = 0;
    for (const TilesetCleanupResult &result : mResults) {
        if (result.changed && !result.applied && result.error.isEmpty())
            ++count;
    }
    return count;
}

void TilesetCleanupDialog::applyCleanup()
{
    const int count = changedFileCount();
    if (count <= 0)
        return;

    int unresolvedRemoved = 0;
    int affectedTileCells = 0;
    int affectedTileObjects = 0;
    int affectedRuleReferences = 0;
    for (const TilesetCleanupResult &result : mResults) {
        unresolvedRemoved += result.unresolvedRemoved;
        affectedTileCells += result.affectedTileCells;
        affectedTileObjects += result.affectedTileObjects;
        affectedRuleReferences += result.affectedRuleReferences;
    }

    QString warning;
    if (unresolvedRemoved > 0) {
        warning = tr(
                    "Apply the selected fixes to %1 file(s)?\n\n"
                    "The advanced option will remove %2 referenced tileset "
                    "declaration(s) whose PNG cannot be resolved. It will "
                    "clear %3 tile cell(s) and remove %4 tile object(s). "
                    "%5 Rules/Blends reference(s) will remain embedded but "
                    "inactive unless the tileset declaration is restored."
                    "\n\nA dated backup is created first. Game files are "
                    "never changed."
                    "\n\nClose open TMX/TBX tabs before continuing.")
                .arg(count)
                .arg(unresolvedRemoved)
                .arg(affectedTileCells)
                .arg(affectedTileObjects)
                .arg(affectedRuleReferences);
    } else {
        warning = tr(
                    "Fix %1 file(s) safely?\n\n"
                    "A dated backup is created first. Missing references are "
                    "kept for you to resolve. Only proven-unused definitions "
                    "and safe paths are changed. Game files are never changed."
                    "\n\nClose open TMX/TBX tabs before continuing.")
                .arg(count);
    }
    if (QMessageBox::warning(
                this,
                unresolvedRemoved > 0
                    ? tr("Apply selected project fixes")
                    : tr("Fix project safely"),
                             warning, QMessageBox::Apply
                             | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Apply) {
        return;
    }

    const QString root = QFileInfo(mRootEdit->text()).absoluteFilePath();
    const QString stamp =
            QDateTime::currentDateTime().toString(
                QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString backupRoot = QDir(root).filePath(
                QStringLiteral(".pztools-backups/tileset-cleanup-%1")
                .arg(stamp));
    if (!QDir().mkpath(backupRoot)) {
        QMessageBox::critical(
                    this, tr("Tileset cleanup"),
                    tr("Could not create the backup directory:\n%1")
                    .arg(QDir::toNativeSeparators(backupRoot)));
        return;
    }

    mResults = run(true, backupRoot);
    mProjectWarnings = projectPathWarnings();
    mReport->setPlainText(fullReport(backupRoot));
    updateStatus();
    updateActions();
}

void TilesetCleanupDialog::updateActions()
{
    const QFileInfo root(mRootEdit->text());
    const bool valid = root.exists() && root.isDir();
    mAnalyzeButton->setEnabled(valid);
    mApplyButton->setEnabled(valid && changedFileCount() > 0);
    mApplyButton->setText(
                mRemoveUnresolvedCheck
                && mRemoveUnresolvedCheck->isChecked()
                ? tr("Apply selected fixes...")
                : tr("Fix safely..."));
}
