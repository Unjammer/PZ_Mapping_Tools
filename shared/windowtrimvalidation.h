/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software. You can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or any later
 * version.
 */
#ifndef WINDOWTRIMVALIDATION_H
#define WINDOWTRIMVALIDATION_H

#include "BuildingEditor/buildingfloor.h"
#include "BuildingEditor/buildingtiles.h"

namespace BuildingEditor {

inline bool validateWindowTrim(QString *error)
{
    BTC_Walls category(QStringLiteral("exterior_walls"), QStringLiteral("Walls"));
    BuildingTileEntry walls(&category);
    BuildingTileEntry trim(&category);
    for (int i = 0; i < category.enumCount(); ++i) {
        walls.setTile(i, BuildingTilesMgr::instance()->get(
                          QStringLiteral("blends_natural_01_0")));
        trim.setTile(i, BuildingTilesMgr::instance()->get(
                         QStringLiteral("blends_natural_01_1")));
    }
    for (int frame = BTC_Walls::WestWindow1; frame < BTC_Walls::EnumCount; ++frame) {
        for (bool second : {false, true}) {
            BuildingFloor::Square square;
            const auto section = second ? BuildingFloor::Square::SectionWall2
                                        : BuildingFloor::Square::SectionWall;
            square.mEntries[section] = &walls;
            square.mEntryEnum[section] = frame;
            if (walls.isWest(frame))
                square.SetWallTrimW(&trim);
            else
                square.SetWallTrimN(&trim);
            square.ReplaceWallTrim();
            bool found = false;
            for (auto result : {BuildingFloor::Square::SectionWallTrim,
                                BuildingFloor::Square::SectionWallTrim2}) {
                found |= square.mEntries[result] == &trim
                        && square.mEntryEnum[result] == frame;
            }
            if (!found) {
                if (error)
                    *error = QStringLiteral("Missing window trim for %1 in section %2")
                            .arg(category.enumToString(frame)).arg(int(section));
                return false;
            }
        }
    }
    return true;
}

}

#endif
