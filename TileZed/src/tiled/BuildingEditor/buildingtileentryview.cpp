/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "buildingtileentryview.h"

#include "buildingpreferences.h"
#include "buildingtiles.h"

#include "tilemetainfomgr.h"
#include "tilesetmanager.h"

#include "tile.h"
#include "tileset.h"
#include <QToolTip>

using namespace BuildingEditor;
using namespace Tiled::Internal;

BuildingTileEntryView::BuildingTileEntryView(QWidget *parent) :
    MixedTilesetView(parent),
    mCategoryLabels(false),
    mSettingEntries(false)
{
    connect(TilesetManager::instance(), &TilesetManager::tilesetChanged,
            this, &BuildingTileEntryView::tilesetChanged);

    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAdded,
            this, &BuildingTileEntryView::tilesetAdded);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAboutToBeRemoved,
            this, &BuildingTileEntryView::tilesetAboutToBeRemoved);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetRemoved,
            this, &BuildingTileEntryView::tilesetRemoved);

    model()->setShowTransparentTiles(true);
    connect(this, &MixedTilesetView::tileEntered, this, &BuildingTileEntryView::showWallVariant);
    connect(BuildingPreferences::instance(), &BuildingPreferences::wallVariantDetailsChanged,
            this, [this](bool enabled) {
        if (enabled) {
            indexWallVariants();
        } else {
            for (const auto &variants : std::as_const(mWallVariants)) {
                if (variants.size() > 1) {
                    for (BuildingTileEntry *item : variants)
                        model()->setData(index(item), QString(), Qt::ToolTipRole);
                }
            }
            mWallVariants.clear();
            QToolTip::hideText();
        }
    });
}

void BuildingTileEntryView::clear()
{
    mEntries.clear();
    mWallVariants.clear();
    MixedTilesetView::clear();
}

void BuildingTileEntryView::setEntries(const QList<BuildingTileEntry*> &entries, bool categoryLabels)
{
    if (mSettingEntries)
        return;
    mSettingEntries = true;
    QList<Tiled::Tile*> tiles;
    QList<void*> userData;
    QStringList headers;

    mEntries = entries;
    mWallVariants.clear();
    if (BuildingPreferences::instance()->wallVariantDetails())
        indexWallVariants();

    QList<Tileset*> requiredTilesets;
    for (BuildingTileEntry *entry : std::as_const(mEntries)) {
        BuildingTile *displayTile = entry->displayTile();
        if (displayTile->isNone())
            continue;
        Tileset *tileset = TileMetaInfoMgr::instance()->tileset(
                    displayTile->mTilesetName);
        if (tileset && !tileset->isLoaded() && !tileset->isMissing()
                && !requiredTilesets.contains(tileset)) {
            requiredTilesets += tileset;
        }
    }
    if (!requiredTilesets.isEmpty()) {
        TileMetaInfoMgr::instance()->loadTilesets(requiredTilesets, true);
        TilesetManager::instance()->waitForTilesets(requiredTilesets);
    }
    for (BuildingTileEntry* entry : std::as_const(mEntries)) {
        if (Tiled::Tile *tile = BuildingTilesMgr::instance()->tileFor(entry->displayTile())) {
            tiles += tile;
            userData += entry;
            if (categoryLabels)
                headers += entry->category()->label();
            else if (tile == TilesetManager::instance()->missingTile())
                headers += entry->displayTile()->mTilesetName;
            else
                headers += tile->tileset()->name();
        }
    }
    setTiles(tiles, userData, headers);

    mCategoryLabels = categoryLabels;
    mSettingEntries = false;
}

QModelIndex BuildingTileEntryView::index(BuildingTileEntry *entry)
{
    return model()->index((void*)entry);
}

void BuildingTileEntryView::indexWallVariants()
{
    for (BuildingTileEntry *entry : std::as_const(mEntries)) {
        BuildingTileCategory *category = entry->category();
        if (!entry->isNone() && (category->asExteriorWalls() || category->asInteriorWalls()
                || category->asExteriorWallTrim() || category->asInteriorWallTrim()))
            mWallVariants[qMakePair(category, entry->displayTile())].append(entry);
    }
}

void BuildingTileEntryView::showWallVariant(const QModelIndex &index)
{
    if (mWallVariants.isEmpty())
        return;
    BuildingTileEntry *item = entry(index);
    if (!item || item->isNone())
        return;
    auto it = mWallVariants.constFind(qMakePair(item->category(), item->displayTile()));
    if (it == mWallVariants.constEnd() || it->size() < 2)
        return;

    const auto &variants = it.value();
    QStringList lines{item->displayTile()->name(),
                      tr("Variant %1 of %2").arg(variants.indexOf(item) + 1).arg(variants.size())};
    QVector<int> shown;
    for (BuildingTileEntry *other : variants) {
        if (item == other)
            continue;
        for (int i = 0; i < item->tileCount(); ++i) {
            if (item->tile(i) == other->tile(i) && item->offset(i) == other->offset(i))
                continue;
            if (!shown.contains(i)) {
                shown.append(i);
                const QPoint offset = item->offset(i);
                QString value = item->tile(i)->isNone() ? tr("Unassigned") : item->tile(i)->name();
                if (!offset.isNull())
                    value += tr(" (offset %1, %2)").arg(offset.x()).arg(offset.y());
                lines.append(item->category()->enumToString(i) + QStringLiteral(": ") + value);
            }
            break;
        }
    }
    model()->setData(index, lines.join(QLatin1Char('\n')), Qt::ToolTipRole);
}

BuildingTileEntry *BuildingTileEntryView::entry(const QModelIndex &index)
{
    if (model()->tileAt(index))
        return static_cast<BuildingTileEntry*>(model()->userDataAt(index));
    return 0;
}

void BuildingTileEntryView::tilesetChanged(Tileset *tileset)
{
    Q_UNUSED(tileset)
    if (mSettingEntries)
        return;
    setEntries(mEntries, mCategoryLabels);
}

void BuildingTileEntryView::tilesetAdded(Tileset *tileset)
{
    Q_UNUSED(tileset)
    if (mSettingEntries)
        return;
    setEntries(mEntries, mCategoryLabels);
}

void BuildingTileEntryView::tilesetAboutToBeRemoved(Tileset *tileset)
{
    QList<Tiled::Tile*> tiles;
    QList<void*> userData;
    QStringList headers;

    for (BuildingTileEntry* entry : std::as_const(mEntries)) {
        if (Tiled::Tile *tile = BuildingTilesMgr::instance()->tileFor(entry->displayTile())) {
            if (tile->tileset() == tileset)
                tile = TilesetManager::instance()->missingTile();
            tiles += tile;
            userData += entry;
            if (mCategoryLabels)
                headers += entry->category()->label();
            else if (tile == TilesetManager::instance()->missingTile())
                headers += entry->displayTile()->mTilesetName;
            else
                headers += tile->tileset()->name();
        }
    }
    setTiles(tiles, userData, headers);
}

void BuildingTileEntryView::tilesetRemoved(Tileset *tileset)
{
    Q_UNUSED(tileset)
//    setEntries(mEntries, mCategoryLabels);
}
