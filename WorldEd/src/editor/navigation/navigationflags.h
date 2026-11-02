/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef NAVIGATIONFLAGS_H
#define NAVIGATIONFLAGS_H

#include <QDataStream>
#include <QMap>
#include <QString>
#include <QVector>

namespace Navigate {

class NavigationFlags
{
public:
    void add(const QMap<QString, QString> &properties)
    {
        const auto has = [&](const char *name) {
            return properties.contains(QLatin1String(name));
        };
        const bool west = has("WallW") || has("WallNW") || has("WallWTrans")
                || has("WallNWTrans") || has("doorFrW") || has("DoorWallW")
                || has("windowW") || has("WindowW");
        const bool north = has("WallN") || has("WallNW") || has("WallNTrans")
                || has("WallNWTrans") || has("doorFrN") || has("DoorWallN")
                || has("windowN") || has("WindowN");
        const bool solid = has("solid") || has("solidtrans");
        mSolid |= solid;
        mWest |= west || has("collideW");
        mNorth |= north || has("collideN");
        mWater |= has("water");
        mTree |= has("tree");
        mHoppableWest |= has("HoppableW");
        mHoppableNorth |= has("HoppableN");
        mStairs |= has("stairsBN") || has("stairsMN") || has("stairsTN")
                || has("stairsBW") || has("stairsMW") || has("stairsTW");
        mLegacySolid |= solid;
        mLegacyWest |= west;
        mLegacyNorth |= north;
        if (has("water") || has("tree"))
            mLegacySolid = false;
        if (has("HoppableW"))
            mLegacyWest = false;
        if (has("HoppableN"))
            mLegacyNorth = false;
    }

    quint8 bits(bool legacy = false) const
    {
        if (legacy)
            return quint8(mLegacySolid | (mLegacyNorth << 1)
                          | (mLegacyWest << 2) | (mWater << 3));
        return quint8(((mSolid || mStairs) && !mWater && !mTree)
                      | ((mNorth && !mHoppableNorth) << 1)
                      | ((mWest && !mHoppableWest) << 2) | (mWater << 3));
    }

    static void writeChunk(QDataStream &out, const QVector<quint8> &bits)
    {
        const quint8 first = bits.first();
        bool uniform = true;
        for (quint8 value : bits)
            uniform &= value == first;
        if (uniform && (first == 0 || first == 1 || first == 8
                        || first == 16 || first == 32)) {
            const int type = first == 8 ? 3 : first == 16 ? 4 : first == 32 ? 5 : first;
            out << quint8(type);
        } else {
            out << quint8(2);
            for (quint8 value : bits)
                out << value;
        }
    }

private:
    bool mSolid = false;
    bool mWest = false;
    bool mNorth = false;
    bool mWater = false;
    bool mTree = false;
    bool mStairs = false;
    bool mHoppableWest = false;
    bool mHoppableNorth = false;
    bool mLegacySolid = false;
    bool mLegacyWest = false;
    bool mLegacyNorth = false;
};

}

#endif
