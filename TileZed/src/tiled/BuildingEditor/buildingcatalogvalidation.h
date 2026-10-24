#ifndef BUILDINGCATALOGVALIDATION_H
#define BUILDINGCATALOGVALIDATION_H
#include "windowtrimvalidation.h"

#include "buildingtilesfile.h"
#include "buildingtiles.h"
#include "simplefile.h"
#include "building.h"
#include "buildingdocument.h"
#include "buildingeditorwindow.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingreader.h"
#include "buildingtemplates.h"
#include "buildingwriter.h"
#include "buildingisoview.h"
#include "buildinglayersdock.h"
#include "buildingpreferences.h"
#include "buildingtilesdialog.h"
#include "furnitureview.h"
#include "furnituregroups.h"
#include "mapcomposite.h"
#include "preferences.h"
#include "shortcut/actionmanager.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QListWidget>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>

namespace BuildingEditor {

inline bool validateBuildingCatalogIdentity(QString *error)
{
    QTemporaryDir directory;
    BuildingTilesFile file;
    BuildingTileCategory *walls = file.categories().at(BuildingTilesMgr::ExteriorWalls);
    BuildingTileCategory *inside = file.categories().at(BuildingTilesMgr::InteriorWalls);
    const auto append = [](BuildingTileCategory *category, const QString &north, QPoint offset) {
        auto *entry = new BuildingTileEntry(category);
        entry->mTiles[BTC_Walls::West] = BuildingTilesMgr::instance()->get(QStringLiteral("walls_exterior_house_01_000"));
        entry->mTiles[BTC_Walls::North] = BuildingTilesMgr::instance()->get(north);
        entry->mOffsets[BTC_Walls::West] = offset;
        category->insertEntry(category->entryCount(), entry);
    };
    const QString north = QStringLiteral("walls_exterior_house_01_001");
    append(walls, north, QPoint());
    append(walls, north, QPoint());
    append(walls, QStringLiteral("walls_exterior_house_01_005"), QPoint());
    append(walls, north, QPoint(2, -3));
    append(inside, north, QPoint());
    const QString path = directory.filePath(QStringLiteral("BuildingTiles.txt"));
    if (!directory.isValid() || !file.write(path, 42, 39, file.categories())) {
        *error = file.errorString();
        return false;
    }
    SimpleFile serialized;
    if (!serialized.read(path)) {
        *error = serialized.errorString();
        return false;
    }
    SimpleFileBlock extra;
    for (SimpleFileBlock &category : serialized.blocks) {
        if (category.value(QStringLiteral("name")) != QStringLiteral("exterior_walls"))
            continue;
        if (category.blocks.size() != 3) {
            *error = QStringLiteral("Saving did not retain exactly three distinct wall definitions");
            return false;
        }
        extra = category;
        category.blocks.insert(1, category.blocks.first());
        SimpleFileBlock reordered = category.blocks.first();
        std::reverse(reordered.values.begin(), reordered.values.end());
        category.blocks.append(reordered);
    }
    serialized.blocks.append(extra);
    if (!serialized.write(path)) {
        *error = serialized.errorString();
        return false;
    }
    BuildingTilesFile loaded;
    for (int pass = 0; pass < 3; ++pass) {
        if (!loaded.read(path)) {
            *error = loaded.errorString();
            return false;
        }
        BuildingTileCategory *result = loaded.categories().at(BuildingTilesMgr::ExteriorWalls);
        if (result->entryCount() != 3
                || loaded.categories().at(BuildingTilesMgr::InteriorWalls)->entryCount() != 1
                || result->entry(0)->tile(BTC_Walls::North)->name() != north
                || result->entry(1)->tile(BTC_Walls::North)->name() == north
                || result->entry(2)->offset(BTC_Walls::West) != QPoint(2, -3)
                || result->defaultEntry() != result->entry(0)) {
            *error = QStringLiteral("Category identity, orientation, offsets or first-entry order changed");
            return false;
        }
        if (!loaded.write(path)) {
            *error = loaded.errorString();
            return false;
        }
    }
    return true;
}

inline bool validateBuildingEditorWorkflows(BuildingEditorWindow *window, QString *error)
{
    QStringList failures;
    int checks = 0;
    const auto check = [&](bool valid, const QString &name) {
        ++checks;
        if (!valid)
            failures.append(name);
    };
    FurnitureTiles emptyFurniture(false);
    FurnitureTile *emptyWest = emptyFurniture.tile(FurnitureTile::FurnitureW);
    check(emptyFurniture.isEmpty(), QStringLiteral("New furniture is empty"));
    emptyWest->setTile(2, 2, nullptr);
    check(emptyWest->size() == QSize(1, 1) && emptyWest->tile(0, 0) == nullptr,
          QStringLiteral("Clearing outside furniture bounds leaves its grid intact"));
    emptyWest->setTile(2, 2, BuildingTilesMgr::instance()->get(
                           QStringLiteral("blends_natural_01_0")));
    check(!emptyFurniture.isEmpty() && emptyWest->size() == QSize(3, 3),
          QStringLiteral("Furniture detects a tile beyond the initial grid"));
    emptyWest->setTile(2, 2, nullptr);
    check(emptyFurniture.isEmpty() && emptyWest->size() == QSize(1, 1),
          QStringLiteral("Furniture becomes empty after removing its last tile"));
    BTC_Walls wallCategory(QStringLiteral("exterior_walls"), QStringLiteral("Walls"));
    BTC_GrimeWall grimeCategory(QStringLiteral("Grime"));
    BuildingTileEntry walls(&wallCategory);
    BuildingTileEntry grime(&grimeCategory);
    for (int i = 0; i < walls.mTiles.size(); ++i)
        walls.mTiles[i] = BuildingTilesMgr::instance()->get(QStringLiteral("blends_natural_01_0"));
    for (int i = 0; i < grime.mTiles.size(); ++i)
        grime.mTiles[i] = BuildingTilesMgr::instance()->get(QStringLiteral("blends_natural_01_1"));
    for (int frame = BTC_Walls::WestWindow1; frame < BTC_Walls::EnumCount; ++frame) {
        for (bool second : {false, true}) {
            BuildingFloor::Square square;
            const auto wallSection = second ? BuildingFloor::Square::SectionWall2 : BuildingFloor::Square::SectionWall;
            square.mEntries[BuildingFloor::Square::SectionWall] = &walls;
            square.mEntryEnum[BuildingFloor::Square::SectionWall] = BTC_Walls::West;
            square.mEntries[wallSection] = &walls;
            square.mEntryEnum[wallSection] = frame;
            square.ReplaceWallGrime(&grime, QString(), QString());
            const bool west = walls.isWest(frame);
            const auto section = west ? BuildingFloor::Square::SectionWallGrime : BuildingFloor::Square::SectionWallGrime2;
            check(square.mEntries[section] == &grime &&
                  square.mEntryEnum[section] == (west ? BTC_GrimeWall::WestWindow : BTC_GrimeWall::NorthWindow),
                  QStringLiteral("Extended window grime %1/%2").arg(frame).arg(second));
        }
    }
    QString trimError;
    check(validateWindowTrim(&trimError),
          QStringLiteral("Extended window trim: %1").arg(trimError));
    BuildingPreferences *prefs = BuildingPreferences::instance();
    QSettings &settings = prefs->settings();
    const QString visibilityKey = QStringLiteral("Layers/Visibility");
    const bool hadVisibility = settings.contains(visibilityKey);
    const QVariant previousVisibility = settings.value(visibilityKey);
    settings.setValue(visibilityKey, 0);
    {
        auto *building = new Building(4, 4, nullptr);
        building->insertFloor(0, new BuildingFloor(building, 0));
        building->insertFloor(1, new BuildingFloor(building, 1));
        BuildingDocument document(building, QString());
        BuildingLayersDock dock;
        check(QMetaObject::invokeMethod(
                  &dock, "currentDocumentChanged", Qt::DirectConnection,
                  Q_ARG(BuildingEditor::BuildingDocument *, &document)),
              QStringLiteral("Building layers dock accepts the document"));
        document.setCurrentFloor(building->floor(1));
        check(building->floor(1)->layerVisibility(QStringLiteral("RoofTop")),
              QStringLiteral("Switching floors keeps RoofTop visible despite an old global cutoff"));
        document.setLayerVisibility(building->floor(0), QStringLiteral("RoofTop"), false);
        document.setCurrentFloor(building->floor(0));
        check(!building->floor(0)->layerVisibility(QStringLiteral("RoofTop")),
              QStringLiteral("The first floor retains its hidden RoofTop"));
        document.setCurrentFloor(building->floor(1));
        check(building->floor(1)->layerVisibility(QStringLiteral("RoofTop")),
              QStringLiteral("The second floor retains its visible RoofTop"));
    }
    if (hadVisibility)
        settings.setValue(visibilityKey, previousVisibility);
    else
        settings.remove(visibilityKey);
    const bool highlight = prefs->highlightFloor();
    const bool lower = prefs->showLowerFloors();
    const bool onlyFloors = prefs->showOnlyFloors();
    prefs->setHighlightFloor(true);
    prefs->setShowLowerFloors(false);
    prefs->setShowOnlyFloors(false);
    {
        auto *building = new Building(4, 4, nullptr);
        for (int level = 0; level < 2; ++level) {
            auto *floor = new BuildingFloor(building, level);
            floor->setGrime(QStringLiteral("Furniture"), 1, 1, QStringLiteral("location_restaurant_bar_01_000"));
            building->insertFloor(level, floor);
        }
        BuildingDocument document(building, QString());
        BuildingIsoScene scene;
        QGraphicsView view(&scene);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(480, 480);
        view.show();
        scene.setDocument(&document);
        document.setCurrentLayer(QStringLiteral("Furniture"));
        for (int pass = 0; pass < 3; ++pass) {
            for (int level : {0, 1}) {
                document.setCurrentFloor(building->floor(level));
                for (bool editing : {true, false}) {
                    scene.setEditingTiles(editing);
                    scene.applyDeferredUpdates();
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    int visibleGroups = 0;
                    bool correctFloor = true;
                    QImage first(480, 480, QImage::Format_ARGB32_Premultiplied);
                    first.fill(Qt::transparent);
                    QPainter painter(&first);
                    for (QGraphicsItem *item : scene.items()) {
                        auto *group = dynamic_cast<CompositeLayerGroupItem *>(item);
                        if (!group || !group->isVisible())
                            continue;
                        ++visibleGroups;
                        correctFloor = correctFloor && group->layerGroup()->level() == level;
                    }
                    scene.render(&painter);
                    painter.end();
                    check(visibleGroups == 1 && correctFloor,
                          QStringLiteral("First mode/floor transition %1/%2/%3").arg(pass).arg(level).arg(editing));
                    scene.setEditingTiles(!editing);
                    scene.setEditingTiles(editing);
                    scene.applyDeferredUpdates();
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    QImage repeated(first.size(), first.format());
                    repeated.fill(Qt::transparent);
                    QPainter secondPainter(&repeated);
                    scene.render(&secondPainter);
                    secondPainter.end();
                    check(first == repeated, QStringLiteral("First and repeated mode render agree"));
                }
            }
        }
        scene.clearDocument();
    }
    prefs->setHighlightFloor(highlight);
    prefs->setShowLowerFloors(lower);
    prefs->setShowOnlyFloors(onlyFloors);

    {
        const QString removedSheet = QStringLiteral("blends_natural_01");
        const QString keptSheet = QStringLiteral("location_restaurant_bar_01");
        BuildingTilesMgr *manager = BuildingTilesMgr::instance();
        auto *building = new Building(4, 4, nullptr);
        auto *floor = new BuildingFloor(building, 0);
        building->insertFloor(0, floor);
        auto *wallEntry = new BuildingTileEntry(manager->catEWalls());
        wallEntry->setTile(BTC_Walls::West, manager->get(removedSheet + QStringLiteral("_0")));
        wallEntry->setTile(BTC_Walls::North, manager->get(keptSheet + QStringLiteral("_0")));
        auto *roomEntry = new BuildingTileEntry(manager->catIWalls());
        roomEntry->setTile(BTC_Walls::West, manager->get(removedSheet + QStringLiteral("_4")));
        roomEntry->setTile(BTC_Walls::North, manager->get(keptSheet + QStringLiteral("_4")));
        auto *onlyRemovedEntry = new BuildingTileEntry(manager->catEWalls());
        onlyRemovedEntry->setTile(BTC_Walls::West,
                                  manager->get(removedSheet + QStringLiteral("_5")));
        building->retainTileEntries(QList<BuildingTileEntry*>()
                                    << wallEntry << roomEntry << onlyRemovedEntry);
        building->setTile(Building::ExteriorWall, wallEntry);
        building->setUsedTiles(QList<BuildingTileEntry*>() << wallEntry << roomEntry);
        auto *room = new Room;
        room->Name = QStringLiteral("Removal test");
        room->internalName = QStringLiteral("removal_test");
        room->Color = qRgb(120, 120, 120);
        room->setTile(Room::InteriorWall, roomEntry);
        building->insertRoom(0, room);
        auto *wall = new WallObject(floor, 2, 2, BuildingObject::W, 1);
        wall->setTile(wallEntry);
        floor->insertObject(0, wall);
        auto *onlyRemovedWall = new WallObject(floor, 3, 2, BuildingObject::W, 1);
        onlyRemovedWall->setTile(onlyRemovedEntry);
        floor->insertObject(1, onlyRemovedWall);
        auto *furniture = new FurnitureTiles(false);
        furniture->tile(FurnitureTile::FurnitureW)->setTile(
                    0, 0, manager->get(removedSheet + QStringLiteral("_1")));
        furniture->tile(FurnitureTile::FurnitureW)->setTile(
                    1, 0, manager->get(keptSheet + QStringLiteral("_1")));
        building->retainFurnitureTiles(furniture);
        auto *onlyRemovedFurniture = new FurnitureTiles(false);
        onlyRemovedFurniture->tile(FurnitureTile::FurnitureW)->setTile(
                    0, 0, manager->get(removedSheet + QStringLiteral("_6")));
        building->retainFurnitureTiles(onlyRemovedFurniture);
        building->setUsedFurniture(QList<FurnitureTiles*>()
                                   << furniture << onlyRemovedFurniture);
        auto *placedFurniture = new FurnitureObject(floor, 1, 1);
        placedFurniture->setFurnitureTile(furniture->tile(FurnitureTile::FurnitureW));
        floor->insertObject(2, placedFurniture);
        auto *onlyRemovedPlacedFurniture = new FurnitureObject(floor, 3, 3);
        onlyRemovedPlacedFurniture->setFurnitureTile(
                    onlyRemovedFurniture->tile(FurnitureTile::FurnitureW));
        floor->insertObject(3, onlyRemovedPlacedFurniture);
        floor->setGrime(QStringLiteral("Furniture"), 0, 0,
                        removedSheet + QStringLiteral("_2"));
        floor->setGrime(QStringLiteral("Furniture"), 4, 4,
                        removedSheet + QStringLiteral("_3"));
        floor->setGrime(QStringLiteral("Furniture"), 0, 1,
                        keptSheet + QStringLiteral("_2"));
        BuildingDocument document(building, QString());
        check(building->tilesetNames().contains(removedSheet),
              QStringLiteral("Tilesheet removal fixture includes the selected sheet"));
        BuildingEditorWindow::removeTilesetReferences(&document, removedSheet);
        check(!building->tilesetNames().contains(removedSheet),
              QStringLiteral("Tilesheet removal clears all building references"));
        check(building->tilesetNames().contains(keptSheet)
              && floor->objectCount() == 2,
              QStringLiteral("Tilesheet removal keeps other placed tiles"));
        check(room->tile(Room::InteriorWall)->tile(BTC_Walls::West)->isNone()
              && room->tile(Room::InteriorWall)->tile(BTC_Walls::North)->mTilesetName
                    == keptSheet,
              QStringLiteral("Tilesheet removal filters mixed room materials"));
        check(floor->grimeAt(QStringLiteral("Furniture"), 4, 4).isEmpty()
              && floor->grimeAt(QStringLiteral("Furniture"), 0, 1)
                    == keptSheet + QStringLiteral("_2"),
              QStringLiteral("Tilesheet removal clears boundary paint and keeps other paint"));
        check(wallEntry->tile(BTC_Walls::West)->mTilesetName == removedSheet
              && furniture->tile(FurnitureTile::FurnitureW)->tile(0, 0)->mTilesetName
                    == removedSheet,
              QStringLiteral("Tilesheet removal does not edit shared definitions"));
        document.undoStack()->undo();
        check(building->tilesetNames().contains(removedSheet)
              && floor->objectCount() == 4
              && floor->grimeAt(QStringLiteral("Furniture"), 4, 4)
                    == removedSheet + QStringLiteral("_3"),
              QStringLiteral("Tilesheet removal undo restores all references"));
        document.undoStack()->redo();
        check(!building->tilesetNames().contains(removedSheet),
              QStringLiteral("Tilesheet removal redo clears the sheet again"));
        QTemporaryDir roundTripDirectory;
        const QString tbxPath = roundTripDirectory.filePath(QStringLiteral("tilesheet-removal.tbx"));
        BuildingWriter writer;
        const bool saved = roundTripDirectory.isValid() && writer.write(building, tbxPath);
        check(saved, QStringLiteral("Tilesheet removal TBX saves: ") + writer.errorString());
        if (saved) {
            BuildingReader reader;
            Building *loaded = reader.read(tbxPath);
            check(loaded && !loaded->tilesetNames().contains(removedSheet)
                  && loaded->tilesetNames().contains(keptSheet),
                  QStringLiteral("Tilesheet removal TBX reopens without the selected sheet"));
            delete loaded;
        }
    }

    BuildingTilesDialog *dialog = BuildingTilesDialog::instance();
    FurnitureGroups *groups = FurnitureGroups::instance();
    auto *group = new FurnitureGroup;
    group->mLabel = QStringLiteral("Catalog reload");
    auto *furniture = new FurnitureTiles(false);
    furniture->setGroup(group);
    auto *west = furniture->tile(FurnitureTile::FurnitureW);
    west->setTile(0, 0, BuildingTilesMgr::instance()->get(QStringLiteral("location_restaurant_bar_01_000")));
    group->mTiles.append(furniture);
    groups->addGroup(group);
    groups->furnitureCatalogLoaded();
    dialog->selectCategory(group);
    auto *list = dialog->findChild<QListWidget *>(QStringLiteral("categoryList"));
    auto *furnitureView = dialog->findChild<FurnitureView *>(QStringLiteral("furnitureView"));
    check(list && list->currentItem() && list->currentItem()->text() == group->mLabel,
          QStringLiteral("Late furniture collection is selected without New"));
    check(furnitureView && furnitureView->model()->index(west).isValid(),
          QStringLiteral("Late furniture contents are available immediately"));
    groups->removeGroup(group);
    groups->furnitureCatalogLoaded();
    dialog->selectCategory(BuildingTilesMgr::instance()->catEWalls());
    delete furniture;
    delete group;

    ActionManager *registered = window->findChild<ActionManager *>();
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("shortcuts.txt"));
    QSet<QString> identifiers;
    QList<QAction *> originals = registered ? registered->registeredActions() : QList<QAction *>();
    check(!originals.isEmpty(), QStringLiteral("BuildingEd shortcuts are registered"));
    {
        ActionManager saved(path);
        for (QAction *original : originals) {
            const QString id = registered->fileIDForAction(original);
            check(!id.isEmpty() && !identifiers.contains(id), QStringLiteral("Unique shortcut identity: ") + id);
            identifiers.insert(id);
            auto *action = new QAction(&saved);
            action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+9")));
            saved.registerAction(action, registered->contextForAction(original), registered->categoryForAction(original), id);
        }
        QString saveError;
        check(saved.save(saveError), QStringLiteral("Shortcut save: ") + saveError);
    }
    {
        ActionManager restored(path);
        for (QAction *original : originals) {
            auto *action = new QAction(&restored);
            restored.registerAction(action, registered->contextForAction(original), registered->categoryForAction(original), registered->fileIDForAction(original));
        }
        QString loadError;
        check(restored.load(loadError), QStringLiteral("Shortcut reload: ") + loadError);
        for (QAction *action : restored.registeredActions())
            check(action->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+9")),
                  QStringLiteral("Shortcut survives a fresh action manager"));
    }
    qInfo() << "BuildingEd workflow validation:" << checks << "checks," << failures.size() << "failures";
    *error = failures.join(QLatin1Char('\n'));
    return failures.isEmpty();
}

}

#endif
