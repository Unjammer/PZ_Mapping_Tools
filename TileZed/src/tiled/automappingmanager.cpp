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

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTextStream>

using namespace Tiled;
using namespace Tiled::Internal;

AutomappingManager *AutomappingManager::mInstance = 0;

AutomappingManager::AutomappingManager(QObject *parent)
    : QObject(parent)
    , mMapDocument(0)
    , mLoaded(false)
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
        if (!loadRules()) {
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

    return QFileInfo(mMapDocument->fileName()).path()
            + QLatin1String("/rules.txt");
}

bool AutomappingManager::loadRules()
{
    const QString filePath = rulesFilePath();
    if (filePath.isEmpty()) {
        mError += tr("Save the map before loading Automapping rules.")
                + QLatin1Char('\n');
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

    if (!mMapDocument) {
        mError = tr("No map document found!") + QLatin1Char('\n');
        emit rulesChanged();
        emit errorsOccurred();
        return false;
    }

    const bool ok = loadRules();
    if (!mWarning.isEmpty())
        emit warningsOccurred();
    if (!ok || !mError.isEmpty())
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
    QString line = in.readLine();

    for (; !line.isNull(); line = in.readLine()) {
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
}
