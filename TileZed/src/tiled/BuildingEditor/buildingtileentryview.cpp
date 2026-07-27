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

#include "buildingtiles.h"

#include "tilemetainfomgr.h"
#include "tilesetmanager.h"

#include "tile.h"
#include "tileset.h"

using namespace BuildingEditor;
using namespace Tiled::Internal;

BuildingTileEntryView::BuildingTileEntryView(QWidget *parent) :
    MixedTilesetView(parent),
    mCategoryLabels(false),
    mSettingEntries(false),
    mMissingEntryCount(0)
{
    connect(TilesetManager::instance(), &TilesetManager::tilesetChanged,
            this, &BuildingTileEntryView::tilesetChanged);

    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAdded,
            this, &BuildingTileEntryView::tilesetAdded);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAboutToBeRemoved,
            this, &BuildingTileEntryView::tilesetAboutToBeRemoved);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetRemoved,
            this, &BuildingTileEntryView::tilesetRemoved);

    model()->setShowEmptyTilesAsMissig(true);
}

void BuildingTileEntryView::clear()
{
    mEntries.clear();
    MixedTilesetView::clear();
}

void BuildingTileEntryView::setEntries(const QList<BuildingTileEntry*> &entries, bool categoryLabels)
{
    // Loading a previously-unused tileset emits tilesetChanged() while this
    // function is still resolving the category. Re-entering setEntries() for
    // every loaded image recursively restarts the complete category and can
    // overflow the stack for categories spanning many tilesets (Roof Caps is
    // a common example). The active pass already picks up each loaded image.
    if (mSettingEntries)
        return;
    mSettingEntries = true;

    QList<Tiled::Tile*> tiles;
    QList<void*> userData;
    QStringList headers;
    mMissingEntryCount = 0;

    mEntries = entries;
    mEntries.detach(); // userData points to individual elements of this list

    // Resolve and decode the category as one batch. Apart from avoiding one
    // wait cycle per entry, this guarantees that a category is not displayed
    // half-loaded when its entries span many tilesets.
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
            if (tile == TilesetManager::instance()->missingTile())
                ++mMissingEntryCount;
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
