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
#include "BuildingEditor/building.h"
#include "BuildingEditor/buildingobjects.h"
#include "BuildingEditor/buildingreader.h"
#include "tiledeffile.h"
#include <QFile>
#include <QScopedPointer>
#include <QTemporaryDir>

namespace BuildingEditor {

inline bool validateWindowFrameBuilding(const QString &path, const QStringList &definitions, QString *error)
{
    auto *watcher = getTileDefWatcher();
    QStringList previous;
    for (auto *entry : watcher->mFiles)
        previous += entry->mFilePath;
    struct Restore {
        Tiled::Internal::TileDefWatcher *watcher;
        QStringList paths;
        ~Restore() { watcher->preferencesChanged(paths); watcher->check(); }
    } restore{watcher, previous};
    watcher->preferencesChanged({});
    watcher->check();
    watcher->preferencesChanged(definitions);
    BuildingReader reader;
    QScopedPointer<Building> building(reader.read(path));
    if (!building) {
        *error = reader.errorString();
        return false;
    }
    int windows = 0;
    for (auto *floor : building->floors()) {
        floor->LayoutToSquares();
        for (auto *object : floor->objects()) {
            Window *window = object->asWindow();
            if (!window)
                continue;
            const auto *entry = window->tile();
            const int offset = window->getOffset();
            const auto *tile = entry->tile(offset);
            auto *properties = watcher->tile(tile->mTilesetName, tile->mIndex);
            if (!properties) {
                *error = QStringLiteral("Missing window properties: %1").arg(tile->name());
                return false;
            }
            const QString shape = properties->mProperties.value(QStringLiteral("WindowShape"));
            const int expected = window->isN() ? BTC_Walls::windowShapeToEnumN(shape)
                                               : BTC_Walls::windowShapeToEnumW(shape);
            const auto &square = floor->squares[window->x()][window->y()];
            bool found = false;
            for (auto section : {BuildingFloor::Square::SectionWall, BuildingFloor::Square::SectionWall2}) {
                const auto *wall = square.mEntries[section];
                found |= wall && square.mEntryEnum[section] == expected
                        && !wall->tile(expected)->isNone();
            }
            if (!found) {
                *error = QStringLiteral("Window frame differs at %1,%2,%3: %4, shape %5")
                        .arg(window->x()).arg(window->y()).arg(floor->level()).arg(tile->name(), shape);
                return false;
            }
            ++windows;
        }
    }
    if (windows == 0) {
        *error = QStringLiteral("The building contains no windows");
        return false;
    }
    qInfo() << "Window frame building validation:" << windows << "windows";
    return true;
}

inline bool validateColdWindowShapes(QString *error)
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("windows.tiles.txt"));
    QFile file(path);
    QByteArray text = "version = 1\ntileset\n{\n file = windows_validation\n size = 8,8\n id = 1\n";
    for (int shape = 1; shape <= 19; ++shape) {
        for (int direction = 0; direction < 2; ++direction) {
            const int index = (shape - 1) * 2 + direction;
            text += QStringLiteral(" tile\n {\n xy = %1,%2\n WindowShape = %3\n }\n")
                    .arg(index % 8).arg(index / 8).arg(shape).toUtf8();
        }
    }
    text += "}\n";
    if (!directory.isValid() || !file.open(QIODevice::WriteOnly)
            || file.write(text) != text.size())
        return false;
    file.close();
    auto *watcher = getTileDefWatcher();
    QStringList previous;
    for (auto *entry : watcher->mFiles)
        previous += entry->mFilePath;
    struct Restore {
        Tiled::Internal::TileDefWatcher *watcher;
        QStringList paths;
        ~Restore() { watcher->preferencesChanged(paths); watcher->check(); }
    } restore{watcher, previous};
    BTC_Windows category(QStringLiteral("Windows"));
    for (int shape = 1; shape <= 19; ++shape) {
        watcher->preferencesChanged({});
        watcher->check();
        watcher->preferencesChanged({path});
        BuildingTileEntry entry(&category);
        for (int direction = 0; direction < 2; ++direction) {
            entry.setTile(direction, BuildingTilesMgr::instance()->get(
                              QStringLiteral("windows_validation_%1")
                              .arg((shape - 1) * 2 + direction)));
            const int expected = BTC_Walls::WestWindow1 + (shape - 1) * 2 + direction;
            if (category.wallEnum(&entry, direction) != expected) {
                *error = QStringLiteral("Cold TileDef lookup replaced window shape %1/%2 with the default")
                        .arg(shape).arg(direction);
                return false;
            }
        }
    }
    return true;
}

inline bool validateWindowTrim(QString *error)
{
    if (!validateColdWindowShapes(error))
        return false;
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
