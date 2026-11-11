/*
 * Copyright 2023, Tim Baker <treectrl@users.sf.net>
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

#include "isogridsquare256.h"

#include "isochunk256.h"
#include "navigationflags.h"
#include "tiledeffile.h"

#include "tile.h"
#include "tileset.h"
#include "tilelayer.h"

using namespace Navigate;

IsoGridSquare256::Definitions IsoGridSquare256::mTileDefinitions;

IsoGridSquare256::IsoGridSquare256(int x, int y, int z, IsoChunk256 *chunk, TempVars256 &vars) :
    x(x),
    y(y),
    z(z),
    mChunk(chunk),
    mSolid(false),
    mBlockedWest(false),
    mBlockedNorth(false),
    mWater(false),
    mRoom(false)
{
    QVector<const Tiled::Cell *> &cells = vars.cells;
    mChunk->orderedCellsAt(x, y, vars.vars, cells);

    NavigationFlags flags;
    for (const Tiled::Cell *cell : cells) {
        const auto *tile = mTileDefinitions.tile(cell->tile->tileset()->name(), cell->tile->id());
        if (tile)
            flags.add(tile->mProperties);
    }
    const quint8 bits = flags.bits();
    mSolid = bits & 1;
    mBlockedNorth = bits & 2;
    mBlockedWest = bits & 4;
    mWater = bits & 8;
}

bool IsoGridSquare256::isSolid()
{
    return mSolid;
}

bool IsoGridSquare256::isBlockedWest()
{
    return mBlockedWest;
}

bool IsoGridSquare256::isBlockedNorth()
{
    return mBlockedNorth;
}

bool IsoGridSquare256::isWater()
{
    return mWater;
}

bool IsoGridSquare256::isRoom()
{
    return mRoom;
}

