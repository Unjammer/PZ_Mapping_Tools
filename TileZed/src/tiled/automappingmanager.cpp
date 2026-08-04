/*
 * automappingmanager.cpp
 * Copyright 2010-2011, Stefan Beller, stefanbeller@googlemail.com
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "automappingmanager.h"

#include "automapperwrapper.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "mapobjectmodel.h"
#include "objectgroup.h"
#include "tilelayer.h"
#include "tilesetmanager.h"
#include "tmxmapreader.h"
#include "preferences.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTemporaryDir>
#include <QTextStream>

using namespace Tiled;
using namespace Tiled::Internal;

namespace {

const char kPreferredRulesFile[] = "automapping-rules.txt";
const char kLegacyRulesFile[] = "rules.txt";

struct RuleListResolution
{
    QString manifestPath;
    QString worldEdRulesPath;
    bool legacyManifest = false;
};

QString normalizedFilePath(const QFileInfo &info)
{
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool isWorldEdRulesLine(const QString &line);

bool fileLooksLikeWorldEdRules(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString trimmed = stream.readLine().trimmed();
        if (trimmed.isEmpty()
                || trimmed.startsWith(QLatin1Char('#'))
                || trimmed.startsWith(QLatin1String("//"))) {
            continue;
        }
        if (trimmed.endsWith(QLatin1String(".tmx"),
                             Qt::CaseInsensitive)
                || trimmed.endsWith(QLatin1String(".txt"),
                                    Qt::CaseInsensitive)) {
            continue;
        }
        if (isWorldEdRulesLine(trimmed))
            return true;
    }
    return false;
}

bool isWorldEdRulesLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()
            || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.startsWith(QLatin1String("//"))) {
        return false;
    }

    const QString lower = trimmed.toLower();
    if (lower.startsWith(QLatin1String("version"))
            && lower.contains(QLatin1Char('='))) {
        return true;
    }
    if (lower == QLatin1String("alias")
            || lower == QLatin1String("rule")
            || lower.startsWith(QLatin1String("alias {"))
            || lower.startsWith(QLatin1String("rule {"))) {
        return true;
    }
    if (trimmed == QLatin1String("{")
            || trimmed == QLatin1String("}")) {
        return true;
    }
    return lower.startsWith(QLatin1String("name ="))
            || lower.startsWith(QLatin1String("tiles ="))
            || lower.startsWith(QLatin1String("color ="))
            || lower.startsWith(QLatin1String("when ="));
}

bool looksLikeWorldEdRules(const QStringList &lines)
{
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()
                || trimmed.startsWith(QLatin1Char('#'))
                || trimmed.startsWith(QLatin1String("//"))) {
            continue;
        }

        // A valid Automapper entry remains authoritative even when a
        // directory in its path happens to contain words such as "rules".
        if (trimmed.endsWith(QLatin1String(".tmx"),
                             Qt::CaseInsensitive)
                || trimmed.endsWith(QLatin1String(".txt"),
                                    Qt::CaseInsensitive)) {
            continue;
        }
        if (isWorldEdRulesLine(trimmed))
            return true;
    }
    return false;
}

RuleListResolution resolveRuleList(const QString &directoryPath)
{
    const QDir directory(directoryPath);
    const QFileInfo preferred(
                directory.filePath(QLatin1String(kPreferredRulesFile)));
    const QFileInfo legacy(
                directory.filePath(QLatin1String(kLegacyRulesFile)));

    RuleListResolution resolution;
    if (preferred.exists()) {
        resolution.manifestPath = normalizedFilePath(preferred);
        return resolution;
    }

    if (legacy.exists()) {
        const QString legacyPath = normalizedFilePath(legacy);
        if (fileLooksLikeWorldEdRules(legacyPath)) {
            resolution.manifestPath = preferred.absoluteFilePath();
            resolution.worldEdRulesPath = legacyPath;
            return resolution;
        }
        resolution.manifestPath = legacyPath;
        resolution.legacyManifest = true;
        return resolution;
    }

    resolution.manifestPath = preferred.absoluteFilePath();
    return resolution;
}

bool writeFixture(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
            && file.write(contents) == contents.size();
}

} // anonymous namespace

AutomappingManager *AutomappingManager::mInstance = 0;

AutomappingManager::AutomappingManager(QObject *parent)
    : QObject(parent)
    , mMapDocument(0)
    , mLoaded(false)
    , mLoadAttempted(false)
    , mApplying(false)
{
}

AutomappingManager::~AutomappingManager()
{
    cleanUp();
}

AutomappingManager *AutomappingManager::instance()
{
    if (!mInstance)
        mInstance = new AutomappingManager(0);

    return mInstance;
}

void AutomappingManager::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

void AutomappingManager::autoMap()
{
    if (!mMapDocument)
        return;

    Map *map = mMapDocument->map();
    int w = map->width();
    int h = map->height();

    autoMapInternal(QRect(0, 0, w, h), 0);
}

void AutomappingManager::autoMap(QRegion where, Layer *touchedLayer)
{
    if (!mApplying && Preferences::instance()->automappingDrawing())
        autoMapInternal(where, touchedLayer);
}

void AutomappingManager::autoMapObjects(const QList<MapObject*> &objects)
{
    if (mApplying || !Preferences::instance()->automappingDrawing()
            || !mMapDocument)
        return;

    QRegion region;
    ObjectGroup *group = nullptr;
    bool mixedGroups = false;
    foreach (MapObject *object, objects) {
        if (!object)
            continue;
        region += object->bounds().toAlignedRect();
        if (!group)
            group = object->objectGroup();
        else if (group != object->objectGroup())
            mixedGroups = true;
    }

    Map *map = mMapDocument->map();
    if (region.isEmpty())
        region = QRect(0, 0, map->width(), map->height());
    autoMapInternal(region, mixedGroups ? nullptr : group);
}

void AutomappingManager::autoMapInternal(QRegion where, Layer *touchedLayer)
{
    mError.clear();
    mWarning.clear();
    if (!mMapDocument) {
        mError = tr("No map document found!") + QLatin1Char('\n');
        emit errorsOccurred();
        return;
    }

    if (!mLoaded) {
        if (mLoadAttempted)
            return;
        if (!loadRules()) {
            if (!mError.isEmpty())
                emit errorsOccurred();
            return;
        }
    }

    Map *map = mMapDocument->map();
    QString layer = map->layerAt(mMapDocument->currentLayerIndex())->name();

    // use a pointer to the region, so each automapper can manipulate it and the
    // following automappers do see the impact
    QRegion *passedRegion = new QRegion(where);

    QVector<AutoMapper*> passedAutoMappers;
    if (touchedLayer) {
        foreach (AutoMapper *a, mAutoMappers) {
            if (a->ruleLayerNameUsed(touchedLayer->name()))
                passedAutoMappers.append(a);
        }
    } else {
        passedAutoMappers = mAutoMappers;
    }
    if (!passedAutoMappers.isEmpty()) {
        QUndoStack *undoStack = mMapDocument->undoStack();
        undoStack->beginMacro(tr("Apply AutoMap rules"));
        mApplying = true;
        AutoMapperWrapper *aw = new AutoMapperWrapper(mMapDocument, passedAutoMappers, passedRegion);
        undoStack->push(aw);
        mApplying = false;
        undoStack->endMacro();
    }
    foreach (AutoMapper *automapper, mAutoMappers) {
        mWarning += automapper->warningString();
        mError += automapper->errorString();
    }

#ifdef ZOMBOID
    // AutoMapperWrapper calls this for each edited layer...
    mMapDocument->emitRegionChanged(*passedRegion,
                                    map->layerAt(map->indexOfLayer(layer)));
#else
    mMapDocument->emitRegionChanged(*passedRegion);
#endif
    delete passedRegion;
    mMapDocument->setCurrentLayerIndex(map->indexOfLayer(layer));

    if (!mWarning.isEmpty())
        emit warningsOccurred();

    if (!mError.isEmpty())
        emit errorsOccurred();
}

QString AutomappingManager::rulesFilePath() const
{
    if (!mMapDocument || mMapDocument->fileName().isEmpty())
        return QString();

    return resolveRuleList(
                QFileInfo(mMapDocument->fileName()).path())
            .manifestPath;
}

QString AutomappingManager::worldEdRulesFilePath() const
{
    if (!mMapDocument || mMapDocument->fileName().isEmpty())
        return QString();

    return resolveRuleList(
                QFileInfo(mMapDocument->fileName()).path())
            .worldEdRulesPath;
}

bool AutomappingManager::loadRules()
{
    mLoadAttempted = true;
    const QString filePath = rulesFilePath();
    if (filePath.isEmpty()) {
        mError += tr("Save the map before loading Automapping rules.")
                + QLatin1Char('\n');
        emit rulesChanged();
        return false;
    }

    const QString worldEdPath = worldEdRulesFilePath();
    if (!worldEdPath.isEmpty() && !QFileInfo::exists(filePath)) {
        qInfo().noquote()
                << tr("Automapper ignored WorldEd terrain Rules.txt %1; "
                      "create %2 to enable Automapper for this TMX.")
                   .arg(QDir::toNativeSeparators(worldEdPath),
                        QDir::toNativeSeparators(filePath));
        emit rulesChanged();
        return false;
    }
    if (!QFileInfo::exists(filePath)) {
        qInfo().noquote()
                << tr("No Automapper manifest exists beside this TMX. "
                      "Expected %1")
                   .arg(QDir::toNativeSeparators(filePath));
        emit rulesChanged();
        return false;
    }

    const bool ok = loadFile(filePath);
    mLoaded = ok;
    emit rulesChanged();
    return ok;
}

bool AutomappingManager::reloadRules()
{
    mError.clear();
    mWarning.clear();
    cleanUp();
    mLoaded = false;
    mLoadAttempted = false;

    if (!mMapDocument) {
        mError = tr("No map document found!") + QLatin1Char('\n');
        emit rulesChanged();
        emit errorsOccurred();
        return false;
    }

    const bool ok = loadRules();
    if (!mWarning.isEmpty())
        emit warningsOccurred();
    if (!mError.isEmpty())
        emit errorsOccurred();
    return ok;
}

bool AutomappingManager::loadFile(const QString &filePath)
{
    bool ret = true;
    const QFileInfo rulesInfo(filePath);
    const QString normalizedPath = rulesInfo.canonicalFilePath().isEmpty()
            ? rulesInfo.absoluteFilePath() : rulesInfo.canonicalFilePath();
    const QString absPath = QFileInfo(normalizedPath).path();
    QFile rulesFile(normalizedPath);

    if (normalizedPath.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)) {
        if (mVisitedRuleLists.contains(normalizedPath)) {
            mWarning += tr("Rules list already included; skipping recursive include:\n%1")
                    .arg(normalizedPath) + QLatin1Char('\n');
            return true;
        }
        mVisitedRuleLists.insert(normalizedPath);
    }

    if (!rulesFile.exists()) {
        mError += tr("No rules file found at:\n%1").arg(filePath)
                  + QLatin1Char('\n');
        return false;
    }
    if (!rulesFile.open(QIODevice::ReadOnly)) {
        mError += tr("Error opening rules file:\n%1").arg(filePath)
                  + QLatin1Char('\n');
        return false;
    }

    QTextStream in(&rulesFile);
    QStringList lines;
    while (!in.atEnd())
        lines += in.readLine();

    if (looksLikeWorldEdRules(lines)) {
        mError += tr(
                    "Automapper did not load this file:\n%1\n\n"
                    "It uses WorldEd terrain/BMP Rules.txt syntax "
                    "(version, alias, rule and tile blocks), not the "
                    "Automapper manifest format.\n\n"
                    "Create automapping-rules.txt beside the target TMX. "
                    "Each non-comment line must reference a rule-map .tmx "
                    "or another Automapper .txt list.")
                .arg(QDir::toNativeSeparators(normalizedPath))
                + QLatin1Char('\n');
        return false;
    }

    for (const QString &line : lines) {
        QString rulePath = line.trimmed();
        if (rulePath.isEmpty()
                || rulePath.startsWith(QLatin1Char('#'))
                || rulePath.startsWith(QLatin1String("//")))
            continue;

        if (QFileInfo(rulePath).isRelative())
            rulePath = absPath + QLatin1Char('/') + rulePath;

        const QFileInfo ruleInfo(rulePath);
        rulePath = ruleInfo.canonicalFilePath().isEmpty()
                ? ruleInfo.absoluteFilePath() : ruleInfo.canonicalFilePath();

        if (!QFileInfo(rulePath).exists()) {
            mError += tr("File not found:\n%1").arg(rulePath) + QLatin1Char('\n');
            ret = false;
            continue;
        }
        if (rulePath.endsWith(QLatin1String(".tmx"), Qt::CaseInsensitive)){
            if (mLoadedRuleFiles.contains(rulePath)) {
                mWarning += tr("Rule map already included; skipping duplicate:\n%1")
                        .arg(rulePath) + QLatin1Char('\n');
                continue;
            }
            mLoadedRuleFiles.insert(rulePath);

            TmxMapReader mapReader;

            Map *rules = mapReader.read(rulePath);

            if (!rules) {
                mError += tr("Opening rules map failed:\n%1").arg(
                        mapReader.errorString()) + QLatin1Char('\n');
                ret = false;
                continue;
            }

            TilesetManager *tilesetManager = TilesetManager::instance();
            tilesetManager->addReferences(rules->tilesets());

            AutoMapper *autoMapper;
            autoMapper = new AutoMapper(mMapDocument, rules, rulePath);

            mWarning += autoMapper->warningString();
            const QString error = autoMapper->errorString(); 
            if (error.isEmpty()) {
                mAutoMappers.append(autoMapper);

                AutomappingRuleInfo info;
                info.filePath = rulePath;
                info.inputLayers = autoMapper->inputLayerNames();
                info.outputLayers = autoMapper->outputLayerNames();
                info.patternCount = autoMapper->ruleCount();
                info.deleteTiles = autoMapper->deletesTiles();
                info.radius = autoMapper->automappingRadius();
                info.noOverlappingRules = autoMapper->preventsOverlappingRules();
                mRuleInfos.append(info);
            } else {
                mError += error;
                delete autoMapper;
            }
        }
        if (rulePath.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)){
            if (!loadFile(rulePath))
                ret = false;
        }
        if (!rulePath.endsWith(QLatin1String(".tmx"), Qt::CaseInsensitive)
                && !rulePath.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)) {
            mWarning += tr("Unsupported Automapping entry; expected .tmx or .txt:\n%1")
                    .arg(rulePath) + QLatin1Char('\n');
        }
    }
    return ret;
}

void AutomappingManager::setMapDocument(MapDocument *mapDocument)
{
    cleanUp();
    if (mMapDocument) {
        mMapDocument->disconnect(this);
        mMapDocument->mapObjectModel()->disconnect(this);
    }

    mMapDocument = mapDocument;

    if (mMapDocument) {
        connect(mMapDocument, &MapDocument::regionEdited,
                this, qOverload<QRegion,Tiled::Layer*>(&AutomappingManager::autoMap));
        MapObjectModel *objectModel = mMapDocument->mapObjectModel();
        connect(objectModel, &MapObjectModel::objectsAdded,
                this, &AutomappingManager::autoMapObjects);
        connect(objectModel, &MapObjectModel::objectsChanged,
                this, &AutomappingManager::autoMapObjects);
        connect(objectModel, &MapObjectModel::objectsRemoved,
                this, &AutomappingManager::autoMapObjects);
    }
    mLoaded = false;
    mLoadAttempted = false;
    emit rulesChanged();
}

void AutomappingManager::cleanUp()
{
    foreach (const AutoMapper *autoMapper, mAutoMappers) {
        delete autoMapper;
    }
    mAutoMappers.clear();
    mRuleInfos.clear();
    mVisitedRuleLists.clear();
    mLoadedRuleFiles.clear();
    mLoadAttempted = false;
}

bool AutomappingManager::runRuleListSelfTest(
        QString *summary, QString *errorString)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create Automapper rule-list fixtures.");
        return false;
    }

    QDir root(temporary.path());
    if (!root.mkpath(QStringLiteral("worlded"))
            || !root.mkpath(QStringLiteral("legacy"))
            || !root.mkpath(QStringLiteral("preferred"))) {
        *errorString = QStringLiteral(
                    "Could not create Automapper fixture directories.");
        return false;
    }

    const QString worldedDir = root.filePath(
                QStringLiteral("worlded"));
    const QString legacyDir = root.filePath(
                QStringLiteral("legacy"));
    const QString preferredDir = root.filePath(
                QStringLiteral("preferred"));
    const QByteArray worldEdContents(
                "version = 1\n\nalias\n{\n"
                "    name = Ash\n"
                "    tiles = floors_burnt_01_0\n}\n");
    const QByteArray manifestContents(
                "# Automapper rules\nrules/roads.tmx\n"
                "rules/vegetation.txt\n");

    if (!writeFixture(QDir(worldedDir).filePath(
                          QLatin1String(kLegacyRulesFile)),
                      worldEdContents)
            || !writeFixture(QDir(legacyDir).filePath(
                                 QLatin1String(kLegacyRulesFile)),
                             manifestContents)
            || !writeFixture(QDir(preferredDir).filePath(
                                 QLatin1String(kLegacyRulesFile)),
                             worldEdContents)
            || !writeFixture(QDir(preferredDir).filePath(
                                 QLatin1String(kPreferredRulesFile)),
                             manifestContents)) {
        *errorString = QStringLiteral(
                    "Could not write Automapper rule-list fixtures.");
        return false;
    }

    const RuleListResolution worlded =
            resolveRuleList(worldedDir);
    const RuleListResolution legacy =
            resolveRuleList(legacyDir);
    const RuleListResolution preferred =
            resolveRuleList(preferredDir);
    if (worlded.worldEdRulesPath.isEmpty()
            || QFileInfo::exists(worlded.manifestPath)
            || QFileInfo(worlded.manifestPath).fileName()
               != QLatin1String(kPreferredRulesFile)) {
        *errorString = QStringLiteral(
                    "WorldEd Rules.txt was not isolated from Automapper.");
        return false;
    }
    if (!legacy.legacyManifest
            || QFileInfo(legacy.manifestPath).fileName()
               != QLatin1String(kLegacyRulesFile)
            || !legacy.worldEdRulesPath.isEmpty()) {
        *errorString = QStringLiteral(
                    "Legacy Automapper rules.txt compatibility failed.");
        return false;
    }
    if (preferred.legacyManifest
            || QFileInfo(preferred.manifestPath).fileName()
               != QLatin1String(kPreferredRulesFile)) {
        *errorString = QStringLiteral(
                    "Preferred automapping-rules.txt did not win.");
        return false;
    }

    *summary = QStringLiteral(
                "WorldEd Rules.txt detection, preferred "
                "automapping-rules.txt selection, and legacy "
                "Automapper rules.txt fallback verified");
    return true;
}
