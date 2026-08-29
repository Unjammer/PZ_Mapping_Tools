/*
 * main.cpp
 * Copyright 2008-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2011, Ben Longbons <b.r.longbons@gmail.com>
 * Copyright 2011, Stefan Beller <stefanbeller@googlemail.com>
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

#include "commandlineparser.h"
#include "automappingmanager.h"
#include "bmpblender.h"
#include "bucketfilltool.h"
#include "bmptool.h"
#include "bmptooldialog.h"
#include "depthmapeditor.h"
#include "lootdistributiondialog.h"
#include "mainwindow.h"
#include "minimap.h"
#include "packcompare.h"
#include "packextractdialog.h"
#include "languagemanager.h"
#include "preferences.h"
#include "stampbrush.h"
#include "tiledapplication.h"
#include "tiledefcompare.h"
#include "tilesetdock.h"
#include "tmxmapreader.h"
#include "../firstlaunchdialog.h"
#include "../portablesettings.h"
#ifdef ZOMBOID
#include "BuildingEditor/building.h"
#include "BuildingEditor/buildingdocument.h"
#include "BuildingEditor/buildingdocumentmgr.h"
#include "BuildingEditor/buildingeditorwindow.h"
#include "BuildingEditor/buildingfloor.h"
#include "BuildingEditor/buildingisoview.h"
#include "BuildingEditor/buildingfurnituredock.h"
#include "BuildingEditor/buildinglua.h"
#include "BuildingEditor/buildingmap.h"
#include "BuildingEditor/buildingobjects.h"
#include "BuildingEditor/buildingpreferences.h"
#include "BuildingEditor/buildingreader.h"
#include "BuildingEditor/buildingtemplates.h"
#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/buildingtilesdialog.h"
#include "BuildingEditor/buildingtilesetdock.h"
#include "BuildingEditor/buildingtiletools.h"
#include "BuildingEditor/mixedtilesetview.h"
#include "BuildingEditor/objecteditmode.h"
#include "BuildingEditor/tileeditmode.h"
#include "BuildingEditor/attributeeditmode.h"
#include "tileselectionscope.h"
#include "BuildingEditor/buildingwriter.h"
#include "BuildingEditor/categorydock.h"
#include "BuildingEditor/furnituregroups.h"
#include "BuildingEditor/newbuildingdialog.h"
#include "tilemetainfomgr.h"
#include "tiletoolpreviewcache.h"
#include "tiledeffile.h"
#include "tilesetmanager.h"
#include "shortcut/keyboardshortcutfile.h"
#include "worlded/worldedmgr.h"
#include "zprogress.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"
#include "map.h"
#include <QAbstractButton>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QImage>
#include <QMessageBox>
#include <QSettings>
#include <QSet>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QUndoStack>
#include <algorithm>
#endif

#include <QDebug>
#include <QEventLoop>
#include <QIcon>
#include <QTimer>
#include <QtPlugin>

#ifdef STATIC_BUILD
Q_IMPORT_PLUGIN(qgif)
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qtiff)
#endif

#define STRINGIFY(x) #x
#define AS_STRING(x) STRINGIFY(x)

using namespace Tiled::Internal;

#ifdef ZOMBOID
bool gStartupBlockRendering = true;
#endif

namespace {

#ifdef ZOMBOID
static Tiled::Tileset *createToolReferenceTileset(const QString &name)
{
    Tiled::Tileset *tileset = new Tiled::Tileset(name, 64, 128);
    QImage image(64, 128, QImage::Format_ARGB32);
    image.fill(Qt::white);
    if (!tileset->loadFromImage(image, QString())) {
        delete tileset;
        return nullptr;
    }
    return tileset;
}

static bool validateToolTilesetReferences(QString *errorString)
{
    TilesetManager *manager = TilesetManager::instance();
    {
        Tiled::Tileset *tileset = createToolReferenceTileset(
                    QStringLiteral("stamp-reference-validation"));
        if (!tileset) {
            *errorString = QStringLiteral(
                        "Could not create the stamp reference tileset");
            return false;
        }
        manager->addReference(tileset, false);
        Tiled::TileLayer *layer = new Tiled::TileLayer(
                    QStringLiteral("Floor"), 0, 0, 1, 1);
        layer->setCell(0, 0, Tiled::Cell(tileset->tileAt(0)));
        StampBrush brush;
        brush.setLayerStamps(QList<Tiled::TileLayer *>() << layer, 0);
        manager->removeReference(tileset);
        if (!manager->tilesets().contains(tileset) ||
                !brush.stamp() || brush.stamp()->cellAt(0, 0).isEmpty()) {
            *errorString = QStringLiteral(
                        "The stamp preview released its live tileset");
            return false;
        }
        brush.setStamp(nullptr);
        if (manager->tilesets().contains(tileset)) {
            *errorString = QStringLiteral(
                        "The cleared stamp retained its tileset");
            return false;
        }
    }
    {
        Tiled::Tileset *tileset = createToolReferenceTileset(
                    QStringLiteral("fill-reference-validation"));
        if (!tileset) {
            *errorString = QStringLiteral(
                        "Could not create the fill reference tileset");
            return false;
        }
        manager->addReference(tileset, false);
        Tiled::TileLayer *layer = new Tiled::TileLayer(
                    QStringLiteral("Floor"), 0, 0, 1, 1);
        layer->setCell(0, 0, Tiled::Cell(tileset->tileAt(0)));
        BucketFillTool fill;
        fill.setStamp(layer);
        manager->removeReference(tileset);
        if (!manager->tilesets().contains(tileset) ||
                !fill.stamp() || fill.stamp()->cellAt(0, 0).isEmpty()) {
            *errorString = QStringLiteral(
                        "The fill preview released its live tileset");
            return false;
        }
        fill.setStamp(nullptr);
        if (manager->tilesets().contains(tileset)) {
            *errorString = QStringLiteral(
                        "The cleared fill preview retained its tileset");
            return false;
        }
    }
    return true;
}

static bool validateSingleRowTilesetCatalog(QString *errorString)
{
    TileMetaInfoMgr *manager = TileMetaInfoMgr::instance();
    Tiled::Tileset *giblet =
            manager->tileset(QStringLiteral("Giblet_00"));
    if (!giblet) {
        *errorString = QStringLiteral(
                    "Giblet_00 is absent from the loaded tileset catalogue");
        return false;
    }
    if (giblet->tileCount() != 8) {
        *errorString = QStringLiteral(
                    "Giblet_00 has %1 tiles instead of 8")
                .arg(giblet->tileCount());
        return false;
    }
    giblet->setColumnCount(0);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create a temporary catalogue directory");
        return false;
    }
    const QString outputFile =
            temporary.filePath(QStringLiteral("Tilesets.txt"));
    if (!manager->writeTxt(outputFile,
                           manager->revision() + 1,
                           manager->sourceRevision())) {
        *errorString = manager->errorString();
        return false;
    }
    if (giblet->columnCount() != 8) {
        *errorString = QStringLiteral(
                    "Giblet_00 recovery returned %1 columns instead of 8")
                .arg(giblet->columnCount());
        return false;
    }
    return true;
}
static bool validateKeyboardShortcutFormat(QString *errorString)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create a temporary shortcut directory");
        return false;
    }
    QList<KeyboardShortcut> expected;
    const auto appendShortcut = [&expected](const QString &id,
                                             const QString &sequence) {
        KeyboardShortcut shortcut;
        shortcut.id = id;
        shortcut.sequence = QKeySequence(
                    sequence, QKeySequence::PortableText);
        expected.append(shortcut);
    };
    appendShortcut(QStringLiteral("Other.BMP.BrushSizeMinus"),
                   QStringLiteral("["));
    appendShortcut(QStringLiteral("Other.BMP.BrushSizePlus"),
                   QStringLiteral("]"));
    appendShortcut(QStringLiteral("Empty"), QString());
    appendShortcut(QStringLiteral("Modified"), QStringLiteral("Ctrl+Shift+["));

    const QString version2Path = temporary.filePath(
                QStringLiteral("shortcuts-v2.txt"));
    KeyboardShortcutFile writer;
    if (!writer.write(version2Path, expected)) {
        *errorString = writer.errorString();
        return false;
    }
    KeyboardShortcutFile reader;
    if (!reader.read(version2Path)) {
        *errorString = reader.errorString();
        return false;
    }
    QMap<QString, QString> actual;
    for (const KeyboardShortcut &shortcut : reader.shortcuts()) {
        actual.insert(shortcut.id, shortcut.sequence.toString(
                          QKeySequence::PortableText));
    }
    for (const KeyboardShortcut &shortcut : expected) {
        const QString sequence = shortcut.sequence.toString(
                    QKeySequence::PortableText);
        if (!actual.contains(shortcut.id)
                || actual.value(shortcut.id) != sequence) {
            *errorString = QStringLiteral(
                        "Shortcut round trip failed for %1")
                    .arg(shortcut.id);
            return false;
        }
    }

    const QString version1Path = temporary.filePath(
                QStringLiteral("shortcuts-v1.txt"));
    QFile legacy(version1Path);
    if (!legacy.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *errorString = legacy.errorString();
        return false;
    }
    legacy.write("version = 1\n\nshortcuts\n{\n"
                 "    Other.BMP.BrushSizeMinus = [\n"
                 "    Other.BMP.BrushSizePlus = ]\n}\n");
    legacy.close();
    KeyboardShortcutFile legacyReader;
    if (!legacyReader.read(version1Path)) {
        *errorString = legacyReader.errorString();
        return false;
    }
    for (const KeyboardShortcut &shortcut : legacyReader.shortcuts()) {
        if (shortcut.id == QLatin1String("Other.BMP.BrushSizeMinus")
                && shortcut.sequence == QKeySequence(
                    QStringLiteral("["), QKeySequence::PortableText)) {
            return true;
        }
    }
    *errorString = QStringLiteral(
                "The legacy BMP brush shortcut was not recovered");
    return false;
}
static bool validateTransparentTileContract(QString *errorString)
{
    MixedTilesetModel browserModel;
    if (!browserModel.showTransparentTiles()) {
        *errorString = QStringLiteral(
                    "BuildingEd tile browsers hide transparent source cells");
        return false;
    }
    QImage transparentImage(64, 128, QImage::Format_ARGB32_Premultiplied);
    transparentImage.fill(Qt::transparent);
    Tiled::Tileset tileset(QStringLiteral("transparent-validation"),
                           64, 128);
    if (!tileset.loadFromImage(transparentImage,
                               QStringLiteral("transparent-validation.png"))) {
        *errorString = QStringLiteral("Could not load transparent test image");
        return false;
    }
    Tiled::Tile *tile = tileset.tileAt(0);
    if (!tile || !tile->image().isNull() || !tile->hasResolvedSource()) {
        *errorString = QStringLiteral(
                    "Transparent source cell was not retained as a valid tile");
        return false;
    }
    const QImage invisible(QStringLiteral(":/images/invisible-tile.svg"));
    const QImage missing(QStringLiteral(":/images/missing-tile.svg"));
    if (invisible.size() != QSize(64, 128)
            || missing.size() != QSize(64, 128)) {
        *errorString = QStringLiteral(
                    "Diagnostic tile resources are unavailable or invalid");
        return false;
    }
    tileset.setMissing(true);
    if (tile->hasResolvedSource()) {
        *errorString = QStringLiteral(
                    "Missing tileset was reported as a resolved source");
        return false;
    }
    return true;
}
static bool validateNewBuildingDialogLayout(QWidget *parent,
                                            QString *errorString)
{
    BuildingEditor::NewBuildingDialog dialog(parent);
    dialog.show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const QList<QGroupBox *> groups = dialog.findChildren<QGroupBox *>();
    for (QGroupBox *group : groups) {
        if (!group || group->height() < group->minimumSizeHint().height()) {
            *errorString = QStringLiteral(
                        "New Building dialog contains a clipped group box");
            dialog.close();
            return false;
        }
    }
    if (groups.count() != 2) {
        *errorString = QStringLiteral(
                    "New Building dialog does not expose both option groups");
        dialog.close();
        return false;
    }
    dialog.close();
    return true;
}
static void addEntryTiles(QSet<BuildingEditor::BuildingTile *> &tiles,
                          BuildingEditor::BuildingTileEntry *entry)
{
    if (!entry || entry->isNone())
        return;
    for (BuildingEditor::BuildingTile *tile : entry->mTiles) {
        if (tile && !tile->isNone())
            tiles += tile;
    }
}
static bool validateBuildingTemplateTiles(BuildingEditor::Building *building,
                                          QString *errorString)
{
    QSet<BuildingEditor::BuildingTile *> buildingTiles;
    for (BuildingEditor::BuildingTileEntry *entry : building->tiles())
        addEntryTiles(buildingTiles, entry);
    for (BuildingEditor::Room *room : building->rooms()) {
        for (BuildingEditor::BuildingTileEntry *entry : room->tiles())
            addEntryTiles(buildingTiles, entry);
    }
    for (BuildingEditor::BuildingTileEntry *entry : building->usedTiles())
        addEntryTiles(buildingTiles, entry);
    for (BuildingEditor::FurnitureTiles *furniture : building->usedFurniture()) {
        for (BuildingEditor::FurnitureTile *orientation : furniture->tiles()) {
            if (!orientation)
                continue;
            for (BuildingEditor::BuildingTile *tile : orientation->tiles()) {
                if (tile && !tile->isNone())
                    buildingTiles += tile;
            }
        }
    }
    for (BuildingEditor::BuildingTile *buildingTile : buildingTiles) {
        Tiled::Tileset *tileset = TileMetaInfoMgr::instance()
                ->tileset(buildingTile->mTilesetName);
        if (!tileset) {
            *errorString = QStringLiteral("Unknown tileset: %1")
                    .arg(buildingTile->mTilesetName);
            return false;
        }
        if (tileset->isMissing()) {
            *errorString = QStringLiteral("Missing tileset image: %1")
                    .arg(buildingTile->mTilesetName);
            return false;
        }
        if (!tileset->isLoaded()) {
            *errorString = QStringLiteral(
                        "Required template tileset was not loaded: %1")
                    .arg(buildingTile->mTilesetName);
            return false;
        }
        if (buildingTile->mIndex < 0
                || buildingTile->mIndex >= tileset->tileCount()) {
            *errorString = QStringLiteral("Invalid template tile: %1")
                    .arg(buildingTile->name());
            return false;
        }
        Tiled::Tile *tile = tileset->tileAt(buildingTile->mIndex);
        if (!tile || !tile->hasResolvedSource()) {
            *errorString = QStringLiteral("Template tile has no image: %1")
                    .arg(buildingTile->name());
            return false;
        }
    }
    qInfo() << "Validated building template:"
            << buildingTiles.count() << "tile references";
    return true;
}
static bool validateAllBuildingTemplates(QString *errorString)
{
    BuildingEditor::BuildingTemplates *templates =
            BuildingEditor::BuildingTemplates::instance();
    for (int index = 0; index < templates->templateCount(); ++index) {
        BuildingEditor::BuildingTemplate *buildingTemplate =
                templates->templateAt(index);
        BuildingEditor::Building *building =
                new BuildingEditor::Building(17, 23, buildingTemplate);
        building->insertFloor(
                    0, new BuildingEditor::BuildingFloor(building, 0));
        const QStringList unresolved =
                BuildingEditor::BuildingMap::loadNeededTilesets(building);
        if (!unresolved.isEmpty()) {
            *errorString = QStringLiteral(
                        "Template \"%1\" requires unavailable tilesets: %2")
                    .arg(buildingTemplate->name())
                    .arg(unresolved.join(QStringLiteral(", ")));
            delete building;
            return false;
        }
        QString templateError;
        if (!validateBuildingTemplateTiles(building, &templateError)) {
            *errorString = QStringLiteral("Template \"%1\": %2")
                    .arg(buildingTemplate->name())
                    .arg(templateError);
            delete building;
            return false;
        }
        delete building;
    }
    qInfo() << "Validated all building templates:"
            << templates->templateCount();
    return true;
}
static QString roomFloorTileName(BuildingEditor::Room *room)
{
    BuildingEditor::BuildingTileEntry *entry =
            room ? room->tile(BuildingEditor::Room::Floor) : nullptr;
    if (!entry || entry->isNone())
        return QString();
    BuildingEditor::BuildingTile *tile = entry->displayTile();
    return tile && !tile->isNone() ? tile->name() : QString();
}
static bool validateRoomFloorRoundTrip(QString *errorString)
{
    using namespace BuildingEditor;
    BuildingTileCategory *floors = BuildingTilesMgr::instance()->catFloors();
    BuildingTileEntry *floorEntry = nullptr;
    for (BuildingTileEntry *entry : floors->entries()) {
        if (entry && !entry->isNone() && entry->displayTile()
                && !entry->displayTile()->isNone()) {
            floorEntry = entry;
            break;
        }
    }
    if (!floorEntry) {
        *errorString = QStringLiteral(
                    "The floor category has no usable tile entry");
        return false;
    }
    BuildingTileEntry *duplicateEntry = floorEntry->createCopy(floors);
    Building *source = new Building(4, 4, nullptr);
    Room *firstRoom = new Room();
    firstRoom->Name = QStringLiteral("Round-trip A");
    firstRoom->internalName = QStringLiteral("test");
    firstRoom->Color = qRgb(255, 0, 0);
    firstRoom->setTile(Room::Floor, floorEntry);
    source->insertRoom(source->roomCount(), firstRoom);
    Room *secondRoom = new Room();
    secondRoom->Name = QStringLiteral("Round-trip B");
    secondRoom->internalName = QStringLiteral("test");
    secondRoom->Color = qRgb(0, 255, 0);
    secondRoom->setTile(Room::Floor, duplicateEntry);
    source->insertRoom(source->roomCount(), secondRoom);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        delete source;
        delete duplicateEntry;
        *errorString = QStringLiteral(
                    "Could not create the room-floor test directory");
        return false;
    }
    const QString testPath = temporary.filePath(
                QStringLiteral("room-floor-roundtrip.tbx"));
    BuildingWriter writer;
    if (!writer.write(source, testPath)) {
        delete source;
        delete duplicateEntry;
        *errorString = QStringLiteral("Could not save the room-floor test: %1")
                .arg(writer.errorString());
        return false;
    }
    BuildingReader reader;
    Building *reopened = reader.read(testPath);
    if (!reopened) {
        delete source;
        delete duplicateEntry;
        *errorString = QStringLiteral(
                    "Could not reopen the room-floor test: %1")
                .arg(reader.errorString());
        return false;
    }
    reader.fix(reopened);
    const QString expected = floorEntry->displayTile()->name();
    bool valid = reopened->roomCount() == 2;
    if (valid) {
        valid = roomFloorTileName(reopened->room(0)) == expected
                && roomFloorTileName(reopened->room(1)) == expected;
    }
    if (!valid) {
        *errorString = QStringLiteral(
                    "A duplicated floor definition was saved as no floor");
    }
    delete reopened;
    delete source;
    delete duplicateEntry;
    return valid;
}
static bool validateRoomFloorsInFixture(const QString &fixturePath,
                                        QString *errorString)
{
    using namespace BuildingEditor;
    BuildingReader fixtureReader;
    Building *source = fixtureReader.read(fixturePath);
    if (!source) {
        *errorString = QStringLiteral("Could not read fixture %1: %2")
                .arg(fixturePath, fixtureReader.errorString());
        return false;
    }
    fixtureReader.fix(source);
    int changedRoom = -1;
    BuildingTileEntry *duplicateEntry = nullptr;
    for (int first = 0; first < source->roomCount() && changedRoom == -1;
         ++first) {
        BuildingTileEntry *entry = source->room(first)->tile(Room::Floor);
        if (!entry || entry->isNone())
            continue;
        for (int second = first + 1; second < source->roomCount(); ++second) {
            if (source->room(second)->tile(Room::Floor) == entry) {
                duplicateEntry = entry->createCopy(entry->category());
                source->room(first)->setTile(Room::Floor, duplicateEntry);
                changedRoom = first;
                break;
            }
        }
    }
    if (changedRoom == -1) {
        delete source;
        *errorString = QStringLiteral(
                    "Fixture has no two rooms sharing a floor definition");
        return false;
    }
    QStringList expectedFloors;
    for (Room *room : source->rooms())
        expectedFloors += roomFloorTileName(room);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        delete source;
        delete duplicateEntry;
        *errorString = QStringLiteral(
                    "Could not create the fixture round-trip directory");
        return false;
    }
    const QString testPath = temporary.filePath(
                QFileInfo(fixturePath).fileName());
    BuildingWriter writer;
    if (!writer.write(source, testPath)) {
        delete source;
        delete duplicateEntry;
        *errorString = QStringLiteral("Could not save fixture copy: %1")
                .arg(writer.errorString());
        return false;
    }
    BuildingReader reopenedReader;
    Building *reopened = reopenedReader.read(testPath);
    if (!reopened) {
        delete source;
        delete duplicateEntry;
        *errorString = QStringLiteral("Could not reopen fixture copy: %1")
                .arg(reopenedReader.errorString());
        return false;
    }
    reopenedReader.fix(reopened);
    bool valid = reopened->roomCount() == expectedFloors.count();
    for (int index = 0; valid && index < reopened->roomCount(); ++index) {
        if (roomFloorTileName(reopened->room(index))
                != expectedFloors.at(index)) {
            *errorString = QStringLiteral(
                        "Room %1 (%2) lost floor %3 during save and reopen")
                    .arg(index)
                    .arg(reopened->room(index)->Name,
                         expectedFloors.at(index));
            valid = false;
        }
    }
    delete reopened;
    delete source;
    delete duplicateEntry;
    if (valid) {
        qInfo() << "Validated fixture room floors after changing room"
                << changedRoom << QFileInfo(fixturePath).fileName();
    }
    return valid;
}

static bool validateBuildingClipboard(
        BuildingEditor::BuildingEditorWindow *window,
        BuildingEditor::BuildingDocument *document,
        QString *errorString)
{
    using namespace BuildingEditor;
    Building *building = document->building();
    BuildingFloor *floor = building->floor(0);
    const QSize originalSize = building->size();
    const int originalRoomCount = building->roomCount();

    Room *sourceRoom = new Room();
    sourceRoom->Name = QStringLiteral("Clipboard room");
    sourceRoom->internalName = QStringLiteral("clipboard_test");
    sourceRoom->Color = qRgb(19, 83, 151);
    document->insertRoom(building->roomCount(), sourceRoom);
    floor->SetRoomAt(1, 1, sourceRoom);

    int colorChangeCount = 0;
    int tileChangeCount = 0;
    const QMetaObject::Connection colorConnection = QObject::connect(
                document, &BuildingDocument::roomColorChanged,
                [&colorChangeCount](Room *) { ++colorChangeCount; });
    const QMetaObject::Connection tileConnection = QObject::connect(
                document, &BuildingDocument::roomTilesChanged,
                [&tileChangeCount](Room *) { ++tileChangeCount; });
    Room *renamedRoom = new Room(sourceRoom);
    renamedRoom->Name += QStringLiteral(" validation");
    Room *originalRoom = document->changeRoom(sourceRoom, renamedRoom);
    Room *temporaryRoom = document->changeRoom(sourceRoom, originalRoom);
    delete temporaryRoom;
    QObject::disconnect(colorConnection);
    QObject::disconnect(tileConnection);
    if (colorChangeCount != 0 || tileChangeCount != 0) {
        *errorString = QStringLiteral(
                    "Room metadata changes triggered an isometric rebuild");
        return false;
    }

    QList<BuildingDocument::ClipboardTileLayer> tileLayers;
    BuildingDocument::ClipboardTileLayer tileLayer;
    tileLayer.level = 0;
    tileLayer.layerName = QStringLiteral("Floor");
    tileLayer.tiles = new FloorTileGrid(2, 2);
    tileLayer.tiles->replace(0, 0,
                             QStringLiteral("blends_natural_01_0"));
    tileLayers.append(tileLayer);
    QList<Room *> rooms;
    rooms.append(new Room(sourceRoom));
    BuildingDocument::ClipboardRoomLayer roomLayer;
    roomLayer.level = 0;
    roomLayer.rooms.resize(2);
    roomLayer.rooms[0] = QVector<int>(2, -2);
    roomLayer.rooms[1] = QVector<int>(2, -2);
    roomLayer.rooms[0][0] = 0;
    roomLayer.rooms[1][0] = -1;
    document->setClipboardTileLayers(
                tileLayers, QRegion(QRect(0, 0, 2, 2)), 0,
                rooms,
                QList<BuildingDocument::ClipboardRoomLayer>() << roomLayer);
    DrawTileTool::instance()->makeCurrent();
    DrawTileTool::instance()->setClipboardPlacement();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const bool pasted = window->pasteClipboardAt(QPoint(-1, -1));
    Room *pastedRoom = building->roomCount() > originalRoomCount + 1
            ? building->room(originalRoomCount + 1) : nullptr;
    bool valid = pasted
            && building->size() == originalSize + QSize(1, 1)
            && building->roomCount() == originalRoomCount + 2
            && floor->GetRoomAt(0, 0) == pastedRoom
            && floor->GetRoomAt(2, 2) == sourceRoom
            && pastedRoom
            && pastedRoom->Name == sourceRoom->Name
            && pastedRoom->internalName == sourceRoom->internalName
            && pastedRoom->Color == sourceRoom->Color;
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QSet<Room *> validRooms(building->rooms().cbegin(),
                            building->rooms().cend());
    for (BuildingFloor *candidateFloor : building->floors()) {
        for (const QVector<Room *> &column : candidateFloor->grid()) {
            for (Room *room : column) {
                valid = valid && (!room || validRooms.contains(room));
            }
        }
    }
    if (!valid) {
        *errorString = QStringLiteral(
                    "Free paste did not clone rooms or expand the building");
    }

    QList<Room *> replacementRooms;
    replacementRooms.append(new Room(sourceRoom));
    QList<BuildingDocument::ClipboardTileLayer> replacementTileLayers;
    BuildingDocument::ClipboardTileLayer replacementTileLayer;
    replacementTileLayer.level = tileLayer.level;
    replacementTileLayer.layerName = tileLayer.layerName;
    replacementTileLayer.tiles = tileLayer.tiles->clone();
    replacementTileLayers.append(replacementTileLayer);
    document->setClipboardTileLayers(
                replacementTileLayers, QRegion(QRect(0, 0, 2, 2)), 0,
                replacementRooms,
                QList<BuildingDocument::ClipboardRoomLayer>() << roomLayer);
    DrawTileTool::instance()->setClipboardPlacement();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    valid = valid && floor->GetRoomAt(0, 0) == pastedRoom
            && pastedRoom && pastedRoom->Color == sourceRoom->Color;

    const int roomsBeforeRepeat = building->roomCount();
    valid = valid && window->pasteClipboardAt(QPoint(4, 4));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    validRooms = QSet<Room *>(building->rooms().cbegin(),
                              building->rooms().cend());
    valid = valid && building->roomCount() == roomsBeforeRepeat + 1;
    for (BuildingFloor *candidateFloor : building->floors()) {
        for (const QVector<Room *> &column : candidateFloor->grid()) {
            for (Room *room : column) {
                valid = valid && (!room || validRooms.contains(room));
            }
        }
    }
    document->undoStack()->undo();
    document->undoStack()->redo();
    document->undoStack()->undo();

    document->undoStack()->undo();
    valid = valid
            && building->size() == originalSize
            && building->roomCount() == originalRoomCount + 1
            && floor->GetRoomAt(1, 1) == sourceRoom;
    if (!valid && errorString->isEmpty()) {
        *errorString = QStringLiteral(
                    "Undo did not restore the complete pre-paste state");
    }

    document->undoStack()->redo();
    Room *redoneRoom = building->roomCount() > originalRoomCount + 1
            ? building->room(originalRoomCount + 1) : nullptr;
    valid = valid
            && building->size() == originalSize + QSize(1, 1)
            && building->roomCount() == originalRoomCount + 2
            && floor->GetRoomAt(0, 0) == redoneRoom;
    if (!valid && errorString->isEmpty()) {
        *errorString = QStringLiteral(
                    "Redo did not restore the complete pasted state");
    }
    document->undoStack()->undo();

    const QSize sizeBeforeLimitedPaste = building->size();
    const int roomsBeforeLimitedPaste = building->roomCount();
    QTimer::singleShot(0, []() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            QMessageBox *box = qobject_cast<QMessageBox *>(widget);
            if (box && box->standardButtons().testFlag(QMessageBox::Yes)) {
                if (QAbstractButton *button = box->button(QMessageBox::Yes)) {
                    button->click();
                    return;
                }
            }
        }
    });
    const bool limitedPaste = window->pasteClipboardAt(
                QPoint(MAX_BUILDING_DIMENSION - 1,
                       MAX_BUILDING_DIMENSION - 1));
    Room *limitedRoom = building->roomCount() > roomsBeforeLimitedPaste
            ? building->room(roomsBeforeLimitedPaste) : nullptr;
    valid = valid
            && limitedPaste
            && building->size() == QSize(MAX_BUILDING_DIMENSION,
                                         MAX_BUILDING_DIMENSION)
            && building->roomCount() == roomsBeforeLimitedPaste + 1
            && floor->GetRoomAt(MAX_BUILDING_DIMENSION - 1,
                                MAX_BUILDING_DIMENSION - 1) == limitedRoom;
    document->undoStack()->undo();
    valid = valid
            && building->size() == sizeBeforeLimitedPaste
            && building->roomCount() == roomsBeforeLimitedPaste
            && floor->GetRoomAt(1, 1) == sourceRoom;
    document->undoStack()->redo();
    valid = valid
            && building->size() == QSize(MAX_BUILDING_DIMENSION,
                                         MAX_BUILDING_DIMENSION)
            && floor->GetRoomAt(MAX_BUILDING_DIMENSION - 1,
                                MAX_BUILDING_DIMENSION - 1) != nullptr;
    document->undoStack()->undo();
    if (!valid && errorString->isEmpty()) {
        *errorString = QStringLiteral(
                    "Maximum-size paste did not crop or restore correctly");
    }
    return valid;
}

static bool triggerBuildingSelectionScope(
        BuildingEditor::BuildingEditorWindow *window,
        const QString &text)
{
    for (QAction *action : window->findChildren<QAction *>()) {
        if (action->text() == text) {
            action->trigger();
            return true;
        }
    }
    return false;
}

static bool validateBuildingCutScope(
        BuildingEditor::BuildingEditorWindow *window,
        BuildingEditor::BuildingDocument *document,
        QString *errorString)
{
    using namespace BuildingEditor;
    BuildingFloor *floor = document->currentFloor();
    if (!floor) {
        *errorString = QStringLiteral("Current floor is unavailable");
        return false;
    }
    QPoint source(-1, -1);
    Room *sourceRoom = nullptr;
    for (int y = 0; y < floor->height() && !sourceRoom; ++y) {
        for (int x = 0; x < floor->width(); ++x) {
            if (Room *room = floor->GetRoomAt(x, y)) {
                source = QPoint(x, y);
                sourceRoom = room;
                break;
            }
        }
    }
    if (!sourceRoom) {
        *errorString = QStringLiteral("Fixture has no room square");
        return false;
    }
    const QStringList layerNames = BuildingMap::layerNames(floor->level());
    if (layerNames.size() < 2) {
        *errorString = QStringLiteral("Fixture floor has fewer than two layers");
        return false;
    }
    const QString currentLayerName = layerNames.contains(
                QStringLiteral("Floor"))
            ? QStringLiteral("Floor") : layerNames.first();
    QString otherLayerName;
    for (const QString &layerName : layerNames) {
        if (layerName != currentLayerName) {
            otherLayerName = layerName;
            break;
        }
    }
    const QString originalCurrentLayer = document->currentLayer();
    const QRegion originalSelection = document->tileSelection();
    const QString originalCurrentTile = floor->grimeAt(
                currentLayerName, source.x(), source.y());
    const QString originalOtherTile = floor->grimeAt(
                otherLayerName, source.x(), source.y());
    const QString currentMarker = QStringLiteral("blends_natural_01_0");
    const QString otherMarker = QStringLiteral("blends_natural_01_1");
    floor->setGrime(currentLayerName, source.x(), source.y(), currentMarker);
    floor->setGrime(otherLayerName, source.x(), source.y(), otherMarker);
    document->setCurrentLayer(currentLayerName);
    document->setTileSelection(QRegion(QRect(source, QSize(1, 1))));

    bool valid = triggerBuildingSelectionScope(
                window, QStringLiteral("Current Layer"));
    const int currentLayerUndoIndex = document->undoStack()->index();
    valid = valid && QMetaObject::invokeMethod(
                window, "editCut", Qt::DirectConnection);
    valid = valid
            && floor->GetRoomAt(source) == sourceRoom
            && floor->grimeAt(currentLayerName, source.x(), source.y()).isEmpty()
            && floor->grimeAt(otherLayerName, source.x(), source.y()) == otherMarker
            && !document->clipboardRoomLayers().isEmpty();
    while (document->undoStack()->index() > currentLayerUndoIndex)
        document->undoStack()->undo();
    valid = valid
            && floor->GetRoomAt(source) == sourceRoom
            && floor->grimeAt(currentLayerName, source.x(), source.y()) == currentMarker
            && floor->grimeAt(otherLayerName, source.x(), source.y()) == otherMarker;

    valid = valid && triggerBuildingSelectionScope(
                window, QStringLiteral("All Layers"));
    const int allLayersUndoIndex = document->undoStack()->index();
    valid = valid && QMetaObject::invokeMethod(
                window, "editCut", Qt::DirectConnection);
    valid = valid
            && floor->GetRoomAt(source) == nullptr
            && floor->grimeAt(currentLayerName, source.x(), source.y()).isEmpty()
            && floor->grimeAt(otherLayerName, source.x(), source.y()).isEmpty();
    while (document->undoStack()->index() > allLayersUndoIndex)
        document->undoStack()->undo();
    valid = valid
            && floor->GetRoomAt(source) == sourceRoom
            && floor->grimeAt(currentLayerName, source.x(), source.y()) == currentMarker
            && floor->grimeAt(otherLayerName, source.x(), source.y()) == otherMarker;

    floor->setGrime(currentLayerName, source.x(), source.y(),
                    originalCurrentTile);
    floor->setGrime(otherLayerName, source.x(), source.y(), originalOtherTile);
    document->setCurrentLayer(originalCurrentLayer);
    document->setTileSelection(originalSelection);
    triggerBuildingSelectionScope(window, QStringLiteral("Current Layer"));
    if (!valid)
        *errorString = QStringLiteral("Cut did not preserve its selected scope");
    return valid;
}

static bool validateBuildingClipboardFixture(
        BuildingEditor::BuildingEditorWindow *window,
        const QString &fileName,
        QString *errorString)
{
    using namespace BuildingEditor;
    QString readError;
    BuildingDocument *document = BuildingDocument::read(fileName, readError);
    if (!document) {
        *errorString = readError;
        return false;
    }
    QSet<Room *> initialRooms(document->building()->rooms().cbegin(),
                              document->building()->rooms().cend());
    int initialInvalidRooms = 0;
    for (BuildingFloor *candidateFloor : document->building()->floors()) {
        for (const QVector<Room *> &column : candidateFloor->grid()) {
            for (Room *room : column) {
                if (room && !initialRooms.contains(room))
                    ++initialInvalidRooms;
            }
        }
    }
    qInfo() << "BuildingEd fixture initial room references"
            << "rooms" << initialRooms.size()
            << "invalid cells" << initialInvalidRooms;
    BuildingDocumentMgr::instance()->addDocument(document);
    BuildingDocumentMgr::instance()->setCurrentDocument(document);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    TileEditMode *tileMode = window->findChild<TileEditMode *>();
    IsoObjectEditMode *objectMode = window->findChild<IsoObjectEditMode *>();
    if (!tileMode || !objectMode) {
        *errorString = QStringLiteral("Building edit modes are unavailable");
        return false;
    }
    ModeManager::instance().setCurrentMode(objectMode);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    BuildingIsoScene *benchmarkScene = nullptr;
    const QList<BuildingIsoScene *> benchmarkScenes =
            window->findChildren<BuildingIsoScene *>();
    for (BuildingIsoScene *scene : benchmarkScenes) {
        if (scene->document() == document &&
                !scene->hasDeferredStructuralMapRebuild()) {
            benchmarkScene = scene;
            break;
        }
    }
    if (!benchmarkScene) {
        *errorString = QStringLiteral("Visible structural map is unavailable");
        return false;
    }
    BuildingMap *benchmarkMap = benchmarkScene->buildingMap();
    QStringList benchmarkSnapshot;
    for (BuildingFloor *candidateFloor : document->building()->floors()) {
        const QRect candidateBounds = candidateFloor->bounds(1, 1);
        const QStringList layerNames = BuildingMap::layerNames(
                    candidateFloor->level());
        for (const QString &layerName : layerNames) {
            for (int x = candidateBounds.left(); x <= candidateBounds.right(); ++x) {
                for (int y = candidateBounds.top(); y <= candidateBounds.bottom(); ++y) {
                    benchmarkSnapshot += benchmarkMap->buildingTileAt(
                                x, y, candidateFloor->level(), layerName);
                }
            }
        }
    }
    BuildingObject *benchmarkObject = nullptr;
    for (BuildingFloor *candidateFloor : document->building()->floors()) {
        for (BuildingObject *object : candidateFloor->objects()) {
            if (!benchmarkObject || object->affectsFloorAbove())
                benchmarkObject = object;
            if (object->affectsFloorAbove())
                break;
        }
        if (benchmarkObject && benchmarkObject->affectsFloorAbove())
            break;
    }
    if (benchmarkObject) {
        for (int iteration = 0; iteration < 3; ++iteration) {
            document->moveObject(benchmarkObject, benchmarkObject->pos());
            QCoreApplication::processEvents(QEventLoop::AllEvents);
        }
    }
    int snapshotIndex = 0;
    for (BuildingFloor *candidateFloor : document->building()->floors()) {
        const QRect candidateBounds = candidateFloor->bounds(1, 1);
        const QStringList layerNames = BuildingMap::layerNames(
                    candidateFloor->level());
        for (const QString &layerName : layerNames) {
            for (int x = candidateBounds.left(); x <= candidateBounds.right(); ++x) {
                for (int y = candidateBounds.top(); y <= candidateBounds.bottom(); ++y) {
                    const QString incrementalTile = benchmarkMap->buildingTileAt(
                                x, y, candidateFloor->level(), layerName);
                    const QString baselineTile = benchmarkSnapshot.at(snapshotIndex++);
                    if (incrementalTile != baselineTile) {
                        *errorString = QStringLiteral(
                                    "Incremental map mismatch at %1,%2,%3 in %4: %5 versus %6")
                                .arg(x).arg(y).arg(candidateFloor->level())
                                .arg(layerName, incrementalTile, baselineTile);
                        return false;
                    }
                }
            }
        }
    }
    ModeManager::instance().setCurrentMode(tileMode);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (!validateBuildingCutScope(window, document, errorString))
        return false;

    triggerBuildingSelectionScope(window, QStringLiteral("Visible Layers"));

    BuildingFloor *floor = document->currentFloor();
    const QSize selectionSize(qMin(14, floor->width()),
                              qMin(16, floor->height()));
    const QRect bounds(QPoint(qMax(0, floor->width()
                                  - selectionSize.width()), 0),
                       selectionSize);
    const QRegion selection(bounds);
    document->setTileSelection(selection);
    if (!QMetaObject::invokeMethod(window, "editCopy",
                                   Qt::DirectConnection)
            || !document->clipboardHasContent()) {
        *errorString = QStringLiteral("Tile-mode copy did not capture content");
        return false;
    }

    DrawTileTool *tool = DrawTileTool::instance();
    QPointF previous;
    for (int pass = 0; pass < 48; ++pass) {
        const QPointF position(80 + (pass % 12) * 45,
                               60 + (pass / 12) * 90);
        QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
        move.setLastScenePos(previous);
        move.setScenePos(position);
        tool->mouseMoveEvent(&move);
        previous = position;
        for (QGraphicsView *view : window->findChildren<QGraphicsView *>()) {
            view->viewport()->update();
            view->viewport()->repaint();
        }
        QCoreApplication::processEvents();
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const int roomCount = document->building()->roomCount();
    const QSize initialSize = document->building()->size();
    bool valid = true;
    const QList<QPoint> targets = {
        bounds.topLeft(),
        QPoint(16, 48),
        QPoint(0, 38),
        QPoint(1, 55),
        QPoint(initialSize.width() + 4, initialSize.height() + 4)
    };
    for (int cycle = 0; cycle < 3 && valid; ++cycle) {
        for (const QPoint &target : targets) {
            if (!valid)
                break;
            valid = valid && window->pasteClipboardAt(target);
            QSet<Room *> validRooms(document->building()->rooms().cbegin(),
                                    document->building()->rooms().cend());
            int invalidRoomCells = 0;
            for (BuildingFloor *candidateFloor :
                 document->building()->floors()) {
                for (const QVector<Room *> &column : candidateFloor->grid()) {
                    for (Room *room : column) {
                        if (room && !validRooms.contains(room))
                            ++invalidRoomCells;
                    }
                }
            }
            valid = valid && invalidRoomCells == 0;
            if (!valid)
                break;
            for (QGraphicsView *view : window->findChildren<QGraphicsView *>()) {
                view->viewport()->update();
                view->viewport()->repaint();
            }
            QCoreApplication::processEvents();
            const QSet<Room *> roomsAfterEvents(
                        document->building()->rooms().cbegin(),
                        document->building()->rooms().cend());
            int retainedInitialRooms = 0;
            for (Room *room : initialRooms) {
                if (roomsAfterEvents.contains(room))
                    ++retainedInitialRooms;
            }
            int invalidAfterEvents = 0;
            for (BuildingFloor *candidateFloor :
                 document->building()->floors()) {
                for (const QVector<Room *> &column : candidateFloor->grid()) {
                    for (Room *room : column) {
                        if (room && !roomsAfterEvents.contains(room))
                            ++invalidAfterEvents;
                    }
                }
            }
            valid = valid
                    && retainedInitialRooms == initialRooms.size()
                    && invalidAfterEvents == 0;
        }
        for (int index = 0; index < targets.size(); ++index) {
            document->undoStack()->undo();
            QCoreApplication::processEvents();
        }
        for (int index = 0; index < targets.size(); ++index) {
            document->undoStack()->redo();
            QCoreApplication::processEvents();
        }
    }
    valid = valid && document->building()->roomCount() >= roomCount;
    while (document->undoStack()->canUndo()) {
        document->undoStack()->undo();
        QCoreApplication::processEvents();
    }
    QList<BuildingIsoScene *> isoScenes;
    for (BuildingIsoScene *scene : window->findChildren<BuildingIsoScene *>()) {
        if (scene->document() == document)
            isoScenes += scene;
    }
    int deferredSceneCount = 0;
    for (BuildingIsoScene *scene : isoScenes) {
        if (scene->hasDeferredStructuralMapRebuild())
            ++deferredSceneCount;
    }
    valid = valid && isoScenes.size() == 3 && deferredSceneCount == 2;
    AttributeEditMode *attributeMode = window->findChild<AttributeEditMode *>();
    valid = valid && objectMode && attributeMode;
    if (objectMode && attributeMode) {
        ModeManager::instance().setCurrentMode(objectMode);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        ModeManager::instance().setCurrentMode(attributeMode);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        ModeManager::instance().setCurrentMode(tileMode);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        for (BuildingIsoScene *scene : isoScenes)
            valid = valid && !scene->hasDeferredStructuralMapRebuild();
    }
    if (!valid && errorString->isEmpty())
        *errorString = QStringLiteral("Fixture copy-paste state is invalid");
    return valid;
}
static bool validateBrushUndoBuffers(QString *errorString)
{
    if (!validateToolTilesetReferences(errorString))
        return false;
    const QString testSheetName = QStringLiteral("blends_natural_01_TEST");
    if (BuildingEditor::BuildingTilesMgr::normalizeTileName(testSheetName)
            != testSheetName) {
        *errorString = QStringLiteral(
                    "A non-tile sheet name was normalized as a tile reference");
        return false;
    }
    Tiled::Tileset tileset(QStringLiteral("brush-validation"), 64, 128);
    Tiled::Tile tile(64, 128, 0, &tileset);
    Tiled::TileLayer layer(QStringLiteral("brush-undo"), 0, 0, 1, 1);
    const Tiled::Cell paintedCell(&tile);
    layer.setCell(0, 0, paintedCell);
    QElapsedTimer elapsed;
    elapsed.start();
    for (int size = 2; size <= 300; ++size) {
        layer.resize(QSize(size, size), QPoint(1, 1));
        layer.setCell(0, 0, paintedCell);
    }
    const qint64 elapsedMs = elapsed.elapsed();
    for (int i = 0; i < 300; ++i) {
        if (layer.cellAt(i, i) != paintedCell) {
            *errorString = QStringLiteral(
                        "Merged brush undo buffer lost the tile at %1,%1")
                    .arg(i);
            return false;
        }
    }
    if (!layer.cellAt(0, 1).isEmpty() ||
            !layer.cellAt(298, 299).isEmpty()) {
        *errorString = QStringLiteral(
                    "Merged brush undo buffer moved a tile off the stroke");
        return false;
    }
    if (!layer.usedTilesets().contains(&tileset)) {
        *errorString = QStringLiteral(
                    "Merged brush undo buffer lost its tileset references");
        return false;
    }
    const QRgb imageStrokeColor = qRgb(40, 120, 220);
    ResizableImage imageStroke(QSize(1, 1));
    imageStroke.fill(Qt::black);
    imageStroke.setPixel(0, 0, imageStrokeColor);
    for (int size = 2; size <= 300; ++size) {
        imageStroke.resize(QSize(size, size), QPoint(1, 1));
        imageStroke.setPixel(0, 0, imageStrokeColor);
    }
    for (int index = 0; index < 300; ++index) {
        if (imageStroke.pixel(index, index) != imageStrokeColor) {
            *errorString = QStringLiteral(
                        "Merged BMP undo buffer lost pixel %1,%1")
                    .arg(index);
            return false;
        }
    }
    ResizableImage imagePatch(QSize(4, 4));
    imagePatch.fill(Qt::black);
    for (int y = 1; y <= 2; ++y)
        for (int x = 1; x <= 2; ++x)
            imagePatch.setPixel(x, y, imageStrokeColor);
    ResizableImage mergedImage(QSize(16, 16));
    mergedImage.fill(Qt::black);
    mergedImage.merge(QPoint(5, 6), &imagePatch,
                      QRegion(QRect(1, 1, 2, 2)));
    if (mergedImage.pixel(6, 7) != imageStrokeColor
            || mergedImage.pixel(5, 6) != qRgb(0, 0, 0)) {
        *errorString = QStringLiteral(
                    "BMP undo patch merge changed the wrong pixels");
        return false;
    }
    if (!MiniMapItem::validateBmpPatchTransfer(errorString))
        return false;
    QImage brushImage(32, 32, QImage::Format_ARGB32);
    brushImage.fill(Qt::transparent);
    brushImage.setPixel(0, 0, qRgba(0, 0, 0, 255));
    brushImage.setPixel(16, 16, qRgba(16, 16, 16, 255));
    brushImage.setPixel(31, 31, qRgba(0, 0, 0, 255));
    brushImage.setPixel(2, 2, qRgba(255, 255, 255, 255));
    const QRegion mask = BmpBrushTool::regionFromBrushImage(brushImage);
    if (!mask.contains(QPoint(0, 0))
            || !mask.contains(QPoint(16, 16))
            || !mask.contains(QPoint(31, 31))
            || mask.contains(QPoint(2, 2))
            || mask.contains(QPoint(3, 3))) {
        *errorString = QStringLiteral(
                    "Custom PNG brush mask did not preserve dark/transparent pixels");
        return false;
    }
    BmpBrushTool *brushTool = BmpBrushTool::instance();
    brushTool->setCustomBrush(mask, brushImage.size(),
                             QStringLiteral("self-test"));
    const QRegion translated = brushTool->brushRegionAt(QPoint(100, 100));
    if (!translated.contains(QPoint(84, 84))
            || !translated.contains(QPoint(100, 100))
            || !translated.contains(QPoint(115, 115))
            || translated.contains(QPoint(86, 86))) {
        *errorString = QStringLiteral(
                    "Custom PNG brush mask was not centered correctly");
        brushTool->setBrushShape(BmpBrushTool::BrushShape::Square);
        return false;
    }
    brushTool->setBrushShape(BmpBrushTool::BrushShape::Square);
    if (!BmpToolDialog::validateReloadEquality(errorString))
        return false;
    if (!BmpBlender::validateUnavailableTilesetFiltering(errorString))
        return false;
    qInfo() << "Validated 300-tile diagonal brush undo growth in"
            << elapsedMs << "ms and a centered 32x32 PNG brush mask";
    return true;
}
static qreal percentileMilliseconds(QVector<qint64> values, qreal percentile)
{
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const int index = qBound(
                0, int((values.size() - 1) * percentile),
                values.size() - 1);
    return values.at(index) / 1000000.0;
}

static bool compareBmpBlenderOutput(BmpBlender &first,
                                    BmpBlender &second,
                                    const QString &label,
                                    QString *errorString)
{
    const QStringList firstNames = first.tileLayerNames();
    const QStringList secondNames = second.tileLayerNames();
    if (firstNames != secondNames) {
        *errorString = QStringLiteral("%1 has different layer names")
                .arg(label);
        return false;
    }
    const QList<Tiled::TileLayer*> firstLayers = first.tileLayers();
    const QList<Tiled::TileLayer*> secondLayers = second.tileLayers();
    for (int index = 0; index < firstLayers.size(); ++index) {
        if (!firstLayers.at(index)->computeDiffRegion(
                    secondLayers.at(index)).isEmpty()) {
            *errorString = QStringLiteral("%1 differs on layer %2")
                    .arg(label, firstNames.at(index));
            return false;
        }
    }
    return true;
}

static bool validateBrushMapPerformance(const QString &fileName,
                                        QString *summary,
                                        QString *errorString)
{
    TmxMapReader reader;
    QScopedPointer<Tiled::Map> map(reader.read(fileName));
    if (!map) {
        *errorString = QStringLiteral("Could not read brush benchmark map: %1")
                .arg(reader.errorString());
        return false;
    }
    QList<QRgb> groundColors;
    for (Tiled::BmpRule *rule : map->bmpSettings()->rules()) {
        if (rule && rule->bitmapIndex == 0 &&
                rule->targetLayer == QLatin1String("0_Floor") &&
                !groundColors.contains(rule->color)) {
            groundColors += rule->color;
        }
    }
    if (groundColors.isEmpty()) {
        *errorString = QStringLiteral(
                    "Brush benchmark map has no ground Rules");
        return false;
    }
    Tiled::BmpRule *sandRule = nullptr;
    for (Tiled::BmpRule *rule : map->bmpSettings()->rules()) {
        if (rule && rule->bitmapIndex == 0
                && rule->targetLayer == QLatin1String("0_Floor")
                && rule->label.compare(
                    QStringLiteral("Sand"), Qt::CaseInsensitive) == 0) {
            sandRule = rule;
            break;
        }
    }
    if (sandRule) {
        QSet<QString> expectedSandTiles;
        for (const QString &choice : sandRule->tileChoices) {
            if (BuildingEditor::BuildingTilesMgr::legalTileName(choice)) {
                expectedSandTiles += BuildingEditor::BuildingTilesMgr::
                        normalizeTileName(choice);
                continue;
            }
            for (Tiled::BmpAlias *alias : map->bmpSettings()->aliases()) {
                if (!alias || alias->name != choice)
                    continue;
                for (const QString &tileName : alias->tiles) {
                    if (BuildingEditor::BuildingTilesMgr::
                            legalTileName(tileName))
                        expectedSandTiles +=
                                BuildingEditor::BuildingTilesMgr::
                                normalizeTileName(tileName);
                }
            }
        }
        Tiled::Map sandMap(
                    map->orientation(), 8, 8,
                    map->tileWidth(), map->tileHeight());
        sandMap.rbmpSettings()->clone(*map->bmpSettings());
        for (Tiled::Tileset *tileset : map->tilesets())
            sandMap.addTileset(tileset);
        sandMap.rbmpMain().rimage().fill(sandRule->color);
        BmpBlender sandBlender(&sandMap);
        if (expectedSandTiles.isEmpty()) {
            *errorString = QStringLiteral(
                        "Sand brush validation could not resolve its floor tiles");
            return false;
        }
        const auto validateSandFloor = [&sandBlender, &expectedSandTiles,
                errorString]() {
            const QStringList layerNames = sandBlender.tileLayerNames();
            const int floorLayerIndex = layerNames.indexOf(
                        QStringLiteral("0_Floor"));
            if (floorLayerIndex == -1) {
                *errorString = QStringLiteral(
                            "Sand brush validation has no floor layer");
                return false;
            }
            Tiled::TileLayer *sandFloor =
                    sandBlender.tileLayers().at(floorLayerIndex);
            for (int y = 2; y < 6; ++y) {
                for (int x = 2; x < 6; ++x) {
                    Tiled::Tile *tile = sandFloor->cellAt(x, y).tile;
                    const QString actual = tile
                            ? BuildingEditor::BuildingTilesMgr::nameForTile(tile)
                            : QString();
                    if (!expectedSandTiles.contains(actual)) {
                        *errorString = QStringLiteral(
                                    "Sand brush resolved unexpected tile %1 at %2,%3")
                                .arg(actual).arg(x).arg(y);
                        return false;
                    }
                }
            }
            return true;
        };

        sandBlender.flush(QRect(QPoint(), sandMap.size()));
        if (!validateSandFloor())
            return false;

        QString sandTilesetName;
        int sandTileID;
        for (const QString &tileName : expectedSandTiles) {
            if (BuildingEditor::BuildingTilesMgr::parseTileName(
                        tileName, sandTilesetName, sandTileID))
                break;
        }
        if (sandTilesetName.isEmpty()
                || !sandBlender.referencesTileset(sandTilesetName)) {
            *errorString = QStringLiteral(
                        "Sand BMP tileset reference was not detected");
            return false;
        }

        const QString testSheetName =
                QStringLiteral("blends_natural_01_TEST");
        int testSheetIndex = -1;
        Tiled::Tileset *testSheet = nullptr;
        for (int index = 0; index < sandMap.tilesets().size(); ++index) {
            Tiled::Tileset *tileset = sandMap.tilesets().at(index);
            if (tileset && tileset->name() == testSheetName) {
                testSheetIndex = index;
                testSheet = tileset;
                break;
            }
        }
        if (testSheet) {
            if (sandBlender.referencesTileset(testSheetName)) {
                *errorString = QStringLiteral(
                            "Unrelated TEST sheet was reported as a Sand reference");
                return false;
            }
            sandMap.removeTilesetAt(testSheetIndex);
            sandBlender.tilesetRemoved(testSheetName);
            sandBlender.flush(QRect(QPoint(), sandMap.size()));
            if (!validateSandFloor())
                return false;
            sandMap.insertTileset(testSheetIndex, testSheet);
            sandBlender.tilesetAdded(testSheet);
            sandBlender.flush(QRect(QPoint(), sandMap.size()));
            if (!validateSandFloor())
                return false;
        }
    }
    const QRect mapBounds(QPoint(), map->size());
    BmpBlender blender(map.data());
    QElapsedTimer timer;
    timer.start();
    blender.flush(mapBounds);
    const qint64 coldNanoseconds = timer.nsecsElapsed();
    BmpBlender linearBlender(map.data());
    linearBlender.setHack(true);
    linearBlender.setUseBlendCandidateIndex(false);
    linearBlender.flush(mapBounds);
    if (!compareBmpBlenderOutput(
                blender, linearBlender,
                QStringLiteral("Indexed blend output"), errorString))
        return false;
    QVector<qint64> redrawSamples;
    const int sampleCount = 48;
    for (int index = 0; index < sampleCount; ++index) {
        const QPoint center(
                    map->width() / 2 - sampleCount / 4 + index / 2,
                    map->height() / 2 - sampleCount / 4 + index / 2);
        const QRect region(center - QPoint(1, 1), QSize(3, 3));
        const QRgb color = index % 2
                ? groundColors.first() : qRgb(0, 0, 0);
        for (int y = region.top(); y <= region.bottom(); ++y)
            for (int x = region.left(); x <= region.right(); ++x)
                map->rbmpMain().setPixel(x, y, color);
        blender.markDirty(region);
        timer.restart();
        blender.flush(mapBounds);
        redrawSamples += timer.nsecsElapsed();
    }
    QVector<qint64> temporarySamples;
    const int floorIndex = map->indexOfLayer(
                QStringLiteral("0_Floor"), Tiled::Layer::TileLayerType);
    const QRect paintRegion(
                QPoint(map->width() / 2 - 2, map->height() / 2 - 2),
                QSize(5, 5));
    for (int index = 0; index < 12; ++index) {
        timer.restart();
        Tiled::Map temporary(
                    map->orientation(), map->width(), map->height(),
                    map->tileWidth(), map->tileHeight());
        temporary.rbmpSettings()->clone(*map->bmpSettings());
        for (Tiled::Tileset *tileset : map->tilesets())
            temporary.addTileset(tileset);
        if (floorIndex != -1)
            temporary.addLayer(map->layerAt(floorIndex)->clone());
        for (int y = paintRegion.top(); y <= paintRegion.bottom(); ++y)
            for (int x = paintRegion.left(); x <= paintRegion.right(); ++x)
                temporary.rbmpMain().setPixel(
                            x, y, groundColors.first());
        BmpBlender temporaryBlender(&temporary);
        temporaryBlender.setHack(true);
        temporaryBlender.setUseBlendCandidateIndex(false);
        temporaryBlender.tilesToPixels(
                    paintRegion.left() - 2, paintRegion.top() - 2,
                    paintRegion.right() + 2, paintRegion.bottom() + 2);
        temporaryBlender.flush(paintRegion);
        temporarySamples += timer.nsecsElapsed();
    }
    BmpBlender boundedBlender(map.data());
    boundedBlender.setHack(true);
    boundedBlender.setUseSparseWorkRegions(false);
    boundedBlender.flush(mapBounds);
    QRegion sparseRegion;
    const int sparseCount = qMax(2, qMin(64,
                qMin(map->width(), map->height()) - 16));
    for (int index = 0; index < sparseCount; ++index) {
        const int x = 8 + index * (map->width() - 17)
                / (sparseCount - 1);
        const int y = map->height() - 9
                - index * (map->height() - 17) / (sparseCount - 1);
        sparseRegion += QRect(x, y, 1, 1);
        const QRgb oldColor = map->rbmpMain().pixel(x, y);
        map->rbmpMain().setPixel(
                    x, y, oldColor == groundColors.first()
                    ? qRgb(0, 0, 0) : groundColors.first());
    }
    blender.markDirty(sparseRegion);
    timer.restart();
    blender.flush(mapBounds);
    const qint64 sparseNanoseconds = timer.nsecsElapsed();
    boundedBlender.markDirty(sparseRegion);
    timer.restart();
    boundedBlender.flush(mapBounds);
    const qint64 boundedNanoseconds = timer.nsecsElapsed();
    if (!compareBmpBlenderOutput(
                blender, boundedBlender,
                QStringLiteral("Sparse-region output"), errorString))
        return false;
    qint64 redrawTotal = 0;
    for (qint64 value : redrawSamples)
        redrawTotal += value;
    qint64 temporaryTotal = 0;
    for (qint64 value : temporarySamples)
        temporaryTotal += value;
    *summary = QStringLiteral(
                "%1x%2, %3 tilesets, %4 rules, %5 blends, indexed and sparse outputs verified, cold %6 ms, small redraw avg %7 ms p95 %8 ms, temporary ground-paint blender avg %9 ms p95 %10 ms, sparse stroke %11 ms versus bounded %12 ms")
            .arg(map->width()).arg(map->height())
            .arg(map->tilesets().size())
            .arg(map->bmpSettings()->rules().size())
            .arg(map->bmpSettings()->blends().size())
            .arg(coldNanoseconds / 1000000.0, 0, 'f', 2)
            .arg(redrawTotal / qreal(redrawSamples.size()) / 1000000.0,
                 0, 'f', 2)
            .arg(percentileMilliseconds(redrawSamples, 0.95), 0, 'f', 2)
            .arg(temporaryTotal / qreal(temporarySamples.size()) / 1000000.0,
                  0, 'f', 2)
            .arg(percentileMilliseconds(temporarySamples, 0.95), 0, 'f', 2)
            .arg(sparseNanoseconds / 1000000.0, 0, 'f', 2)
            .arg(boundedNanoseconds / 1000000.0, 0, 'f', 2);
    return true;
}
static bool validateTileDefSplitting(QString *errorString)
{
    QTemporaryDir malformedDirectory;
    if (!malformedDirectory.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the malformed tiledef test directory");
        return false;
    }
    const QString malformedPath =
            malformedDirectory.filePath(
                QStringLiteral("invalid-values.tiles"));
    QFile malformedFile(malformedPath);
    if (!malformedFile.open(QIODevice::WriteOnly)) {
        *errorString = QStringLiteral(
                    "Could not create the malformed tiledef test file");
        return false;
    }
    QDataStream malformedStream(&malformedFile);
    malformedStream.setByteOrder(QDataStream::LittleEndian);
    malformedStream.writeRawData("tdef", 4);
    malformedStream << qint32(1) << qint32(1);
    const QByteArray invalidName("validation_invalid\n");
    const QByteArray invalidImage("validation_invalid.png\n");
    malformedStream.writeRawData(
                invalidName.constData(), invalidName.size());
    malformedStream.writeRawData(
                invalidImage.constData(), invalidImage.size());
    malformedStream << qint32(2) << qint32(2)
                    << qint32(0) << qint32(5);
    malformedFile.close();
    TileDefFile malformed;
    if (malformed.read(malformedPath)) {
        *errorString = QStringLiteral(
                    "A tiledef with invalid ID/count unexpectedly loaded");
        return false;
    }
    const QString malformedError = malformed.errorString();
    if (!malformedError.contains(
                QStringLiteral(
                    "Stored tile count 5 exceeds the grid capacity of 4 "
                    "(2 columns x 2 rows).")) ||
            !malformedError.contains(
                QStringLiteral(
                    "Tileset ID is 0; version-1 IDs must start at 1.")) ||
            !malformedError.contains(
                QStringLiteral("Values stored in the .tiles file:"))) {
        *errorString = QStringLiteral(
                    "Detailed tiledef diagnostics are incomplete:\n%1")
                .arg(malformedError);
        return false;
    }
    const QString truncatedPath =
            malformedDirectory.filePath(
                QStringLiteral("truncated-field.tiles"));
    QFile truncatedFile(truncatedPath);
    if (!truncatedFile.open(QIODevice::WriteOnly)) {
        *errorString = QStringLiteral(
                    "Could not create the truncated tiledef test file");
        return false;
    }
    QDataStream truncatedStream(&truncatedFile);
    truncatedStream.setByteOrder(QDataStream::LittleEndian);
    truncatedStream.writeRawData("tdef", 4);
    truncatedStream << qint32(1) << qint32(1);
    truncatedStream.writeRawData("unterminated-name", 17);
    truncatedFile.close();
    TileDefFile truncated;
    if (truncated.read(truncatedPath) ||
            !truncated.errorString().contains(
                QStringLiteral("truncated name or image-source field"))) {
        *errorString = QStringLiteral(
                    "A truncated tiledef string did not produce the "
                    "targeted diagnostic:\n%1")
                .arg(truncated.errorString());
        return false;
    }
    TileDefFile oversized;
    for (int index = 0;
         index < TileDefFile::MAX_TILESET_ID_MODS + 1;
         ++index) {
        TileDefTileset *tileset = new TileDefTileset;
        tileset->mName = QStringLiteral("validation_%1")
                .arg(index, 4, 10, QLatin1Char('0'));
        tileset->mImageSource = tileset->mName
                + QLatin1String(".png");
        tileset->mColumns = 1;
        tileset->mRows = 1;
        tileset->mID = index + 1;
        tileset->mTiles += new TileDefTile(tileset, 0);
        oversized.insertTileset(
                    oversized.tilesets().size(), tileset);
    }
    QString validationError;
    if (oversized.validate(TileDefFile::ModFormat,
                           &validationError)) {
        *errorString = QStringLiteral(
                    "An oversized mod tiledef unexpectedly validated");
        return false;
    }
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the temporary tiledef directory");
        return false;
    }
    const QString outputFile =
            QDir(temporaryDirectory.path()).filePath(
                QStringLiteral("legacy.tiles"));
    QStringList outputs;
    if (!oversized.writeModSeries(outputFile, &outputs)) {
        *errorString = oversized.errorString();
        return false;
    }
    if (outputs.size() != 2
            || !outputs.at(0).endsWith(
                QLatin1String("legacy_1.tiles"))
            || !outputs.at(1).endsWith(
                QLatin1String("legacy_2.tiles"))) {
        *errorString = QStringLiteral(
                    "Unexpected split output names: %1")
                .arg(outputs.join(QStringLiteral(", ")));
        return false;
    }
    const int expectedCounts[] = {
        TileDefFile::MAX_TILESET_ID_MODS, 1
    };
    for (int part = 0; part < outputs.size(); ++part) {
        TileDefFile repaired;
        if (!repaired.read(outputs.at(part))) {
            *errorString = repaired.errorString();
            return false;
        }
        if (repaired.tilesets().size() != expectedCounts[part]
                || !repaired.validate(TileDefFile::ModFormat,
                                      &validationError)) {
            *errorString = QStringLiteral(
                        "Split part %1 is invalid: %2")
                    .arg(part + 1).arg(validationError);
            return false;
        }
        if (repaired.tilesets().first()->mID != 1
                || repaired.tilesets().last()->mID
                != expectedCounts[part]) {
            *errorString = QStringLiteral(
                        "Split part %1 did not receive sequential IDs")
                    .arg(part + 1);
            return false;
        }
        if (!QFileInfo(outputs.at(part)
                       + QLatin1String(".txt")).isFile()) {
            *errorString = QStringLiteral(
                        "Split part %1 has no text companion")
                    .arg(part + 1);
            return false;
        }
    }
    QString comparatorSummary;
    if (!TileDefCompare::runSelfTest(
            &comparatorSummary, errorString)) {
        return false;
    }
    qInfo() << "Validated detailed malformed-value/truncated-field "
               "diagnostics, enhanced comparator analysis, and B42 "
               "tiledef split:"
            << oversized.tilesets().size() << "tilesets ->"
            << outputs.size() << "files;"
            << comparatorSummary;
    return true;
}
static bool validateBuildingLuaFurniture(
        BuildingEditor::BuildingDocument *document, QString *errorString)
{
    static const char scriptSource[] =
            "assert(building.apiVersion >= 3)\n"
            "local groups = building:furnitureGroupNames()\n"
            "assert(#groups > 0, 'empty furniture catalog')\n"
            "local function findPlaceable()\n"
            "  for groupIndex = 0, #groups - 1 do\n"
            "    local count = building:furnitureCount(groupIndex)\n"
            "    for furnitureIndex = 0, count - 1 do\n"
            "      local orientations = building:furnitureOrientations(\n"
            "          groupIndex, furnitureIndex)\n"
            "      for _, orientation in ipairs(orientations) do\n"
            "        local width, height = building:furnitureSize(\n"
            "            groupIndex, furnitureIndex, orientation)\n"
            "        if width <= building:width() - 2 and\n"
            "            height <= building:height() - 2 then\n"
            "          for y = 0, height - 1 do\n"
            "            for x = 0, width - 1 do\n"
            "              local tileName = building:furnitureTileAt(\n"
            "                  groupIndex, furnitureIndex, orientation, x, y)\n"
            "              if tileName ~= '' then\n"
            "                local matches = building:findFurniture(tileName)\n"
            "                assert(#matches > 0,\n"
            "                    'reverse furniture lookup returned no match')\n"
            "                return groupIndex, furnitureIndex, orientation\n"
            "              end\n"
            "            end\n"
            "          end\n"
            "        end\n"
            "      end\n"
            "    end\n"
            "  end\n"
            "end\n"
            "local groupIndex, furnitureIndex, orientation = findPlaceable()\n"
            "assert(groupIndex, 'no placeable furniture definition')\n"
            "local level = building:currentLevel()\n"
            "local before = building:objectCount(level)\n"
            "local objectIndex = building:placeFurniture(\n"
            "    level, 1, 1, groupIndex, furnitureIndex, orientation)\n"
            "assert(objectIndex == before, 'unexpected furniture object index')\n"
            "assert(building:objectCount(level) == before + 1)\n"
            "assert(building:objectType(level, objectIndex) == 'Furniture')\n";
    QTemporaryFile scriptFile(
                QDir::tempPath()
                + QLatin1String("/buildinged-lua-validation-XXXXXX.lua"));
    if (!scriptFile.open()) {
        *errorString = QStringLiteral("Could not create the temporary Lua test");
        return false;
    }
    if (scriptFile.write(scriptSource) != qint64(sizeof(scriptSource) - 1)
            || !scriptFile.flush()) {
        *errorString = QStringLiteral("Could not write the temporary Lua test");
        return false;
    }
    const int objectCountBefore =
            document->building()->floor(0)->objectCount();
    BuildingEditor::BuildingLuaScript script(document);
    QString luaError;
    if (!script.run(scriptFile.fileName(), &luaError)) {
        *errorString = QStringLiteral("Lua execution failed: %1").arg(luaError);
        return false;
    }
    if (!script.applyChanges(QStringLiteral(
                                 "BuildingEd Lua furniture validation"))) {
        *errorString = QStringLiteral(
                    "Lua furniture placement produced no document change");
        return false;
    }
    QUndoStack *undoStack = document->undoStack();
    if (document->building()->floor(0)->objectCount()
            != objectCountBefore + 1 || !undoStack->canUndo()) {
        *errorString = QStringLiteral(
                    "Lua furniture placement was not committed correctly");
        return false;
    }
    undoStack->undo();
    if (document->building()->floor(0)->objectCount()
            != objectCountBefore || !undoStack->canRedo()) {
        *errorString = QStringLiteral(
                    "Lua furniture placement Undo validation failed");
        return false;
    }
    undoStack->redo();
    if (document->building()->floor(0)->objectCount()
            != objectCountBefore + 1) {
        *errorString = QStringLiteral(
                    "Lua furniture placement Redo validation failed");
        return false;
    }
    undoStack->undo();
    undoStack->clear();
    qInfo() << "BuildingEd Lua furniture validation: placement, Undo and Redo PASS";
    return true;
}
#endif
class CommandLineHandler : public CommandLineParser
{
public:
    CommandLineHandler();

    bool quit;
    bool showedVersion;
    bool disableOpenGL;
    bool validateBuildingCategories;
    bool validateBuildingClipboard;
    bool buildingDebugKeys;
    bool validateBrushPerformance;
    bool validateDepthMapEditor;
    bool validateLootDistributions;
    bool validatePackTools;
    bool validateAutomapperRules;
    bool renderPackComparator;
    bool renderPackExtractor;
    bool renderTileDefComparator;
    bool renderLootDistributions;
    bool validateTileDefSplit;
    bool validateTilesetCatalog;

private:
    void showVersion();
    void justQuit();
    void setDisableOpenGL();
    void setValidateBuildingCategories();
    void setValidateBuildingClipboard();
    void setBuildingDebugKeys();
    void setValidateBrushPerformance();
    void setValidateDepthMapEditor();
    void setValidateLootDistributions();
    void setValidatePackTools();
    void setValidateAutomapperRules();
    void setRenderPackComparator();
    void setRenderPackExtractor();
    void setRenderTileDefComparator();
    void setRenderLootDistributions();
    void setValidateTileDefSplit();
    void setValidateTilesetCatalog();

    // Convenience wrapper around registerOption
    template <void (CommandLineHandler::*memberFunction)()>
    void option(QChar shortName,
                const QString &longName,
                const QString &help)
    {
        registerOption<CommandLineHandler, memberFunction>(this,
                                                           shortName,
                                                           longName,
                                                           help);
    }
};

} // anonymous namespace


CommandLineHandler::CommandLineHandler()
    : quit(false)
    , showedVersion(false)
    , disableOpenGL(false)
    , validateBuildingCategories(false)
    , validateBuildingClipboard(false)
    , buildingDebugKeys(false)
    , validateBrushPerformance(false)
    , validateDepthMapEditor(false)
    , validateLootDistributions(false)
    , validatePackTools(false)
    , validateAutomapperRules(false)
    , renderPackComparator(false)
    , renderPackExtractor(false)
    , renderTileDefComparator(false)
    , renderLootDistributions(false)
    , validateTileDefSplit(false)
    , validateTilesetCatalog(false)
{
    option<&CommandLineHandler::showVersion>(
                QLatin1Char('v'),
                QLatin1String("--version"),
                QLatin1String("Display the version"));

    option<&CommandLineHandler::justQuit>(
                QChar(),
                QLatin1String("--quit"),
                QLatin1String("Only check validity of arguments, "
                              "don't actually load any files"));

    option<&CommandLineHandler::setDisableOpenGL>(
                QChar(),
                QLatin1String("--disable-opengl"),
                QLatin1String("Disable hardware accelerated rendering"));
    option<&CommandLineHandler::setValidateBuildingCategories>(
                QChar(),
                QLatin1String("--validate-building-categories"),
                QLatin1String("Load and validate every BuildingEd tile category"));
    option<&CommandLineHandler::setValidateBuildingClipboard>(
                QChar(),
                QLatin1String("--validate-building-clipboard"),
                QLatin1String("Validate BuildingEd clipboard placement with a TBX fixture"));
    option<&CommandLineHandler::setBuildingDebugKeys>(
                QChar(),
                QLatin1String("--building-debug-keys"),
                QLatin1String("Enable BuildingEd diagnostic placement shortcuts"));
    option<&CommandLineHandler::setValidateBrushPerformance>(
                QChar(),
                QLatin1String("--validate-brush-performance"),
                QLatin1String("Validate brush buffers and optional TMX Rules/Blends performance"));
    option<&CommandLineHandler::setValidateDepthMapEditor>(
                QChar(),
                QLatin1String("--validate-depthmap-editor"),
                QLatin1String("Validate Build 42 depth atlas editing and PNG output"));
    option<&CommandLineHandler::setValidateLootDistributions>(
                QChar(),
                QLatin1String("--validate-loot-distributions"),
                QLatin1String("Validate game loot registries; pass game root "
                              "and optional project root as file arguments"));
    option<&CommandLineHandler::setValidatePackTools>(
                QChar(),
                QLatin1String("--validate-pack-tools"),
                QLatin1String("Validate .pack reading, comparison, hashing "
                              "and extraction"));
    option<&CommandLineHandler::setValidateAutomapperRules>(
                QChar(),
                QLatin1String("--validate-automapper-rules"),
                QLatin1String("Validate Automapper manifest selection and "
                              "WorldEd Rules.txt isolation"));
    option<&CommandLineHandler::setRenderPackComparator>(
                QChar(),
                QLatin1String("--render-pack-comparator"),
                QLatin1String("Render the enhanced .pack comparator; pass "
                              "the output PNG as a file argument"));
    option<&CommandLineHandler::setRenderPackExtractor>(
                QChar(),
                QLatin1String("--render-pack-extractor"),
                QLatin1String("Render the versatile .pack extractor; pass "
                              "the output PNG as a file argument"));
    option<&CommandLineHandler::setRenderTileDefComparator>(
                QChar(),
                QLatin1String("--render-tiledef-comparator"),
                QLatin1String("Render the enhanced .tiles comparator; pass "
                              "the output PNG as a file argument"));
    option<&CommandLineHandler::setRenderLootDistributions>(
                QChar(),
                QLatin1String("--render-loot-distributions"),
                QLatin1String("Render the loot editor; pass game root, "
                              "output PNG and optional project root"));
    option<&CommandLineHandler::setValidateTileDefSplit>(
                QChar(),
                QLatin1String("--validate-tiledef-split"),
                QLatin1String("Validate B42 mod tiledef limits and splitting"));
    option<&CommandLineHandler::setValidateTilesetCatalog>(
                QChar(),
                QLatin1String("--validate-tileset-catalog"),
                QLatin1String("Validate one-row tileset catalogue recovery"));
}

void CommandLineHandler::showVersion()
{
    if (!showedVersion) {
        showedVersion = true;
        qInfo() << "Tiled (Qt) Map Editor"
                << qPrintable(QApplication::applicationVersion());
        quit = true;
    }
}

void CommandLineHandler::justQuit()
{
    quit = true;
}

void CommandLineHandler::setDisableOpenGL()
{
    disableOpenGL = true;
}

void CommandLineHandler::setValidateBuildingCategories()
{
    validateBuildingCategories = true;
}
void CommandLineHandler::setValidateBuildingClipboard()
{
    validateBuildingClipboard = true;
}
void CommandLineHandler::setBuildingDebugKeys()
{
    buildingDebugKeys = true;
}
void CommandLineHandler::setValidateBrushPerformance()
{
    validateBrushPerformance = true;
}
void CommandLineHandler::setValidateDepthMapEditor()
{
    validateDepthMapEditor = true;
}
void CommandLineHandler::setValidateLootDistributions()
{
    validateLootDistributions = true;
}
void CommandLineHandler::setValidatePackTools()
{
    validatePackTools = true;
}
void CommandLineHandler::setValidateAutomapperRules()
{
    validateAutomapperRules = true;
}
void CommandLineHandler::setRenderPackComparator()
{
    renderPackComparator = true;
}
void CommandLineHandler::setRenderPackExtractor()
{
    renderPackExtractor = true;
}
void CommandLineHandler::setRenderTileDefComparator()
{
    renderTileDefComparator = true;
}
void CommandLineHandler::setRenderLootDistributions()
{
    renderLootDistributions = true;
}
void CommandLineHandler::setValidateTileDefSplit()
{
    validateTileDefSplit = true;
}
void CommandLineHandler::setValidateTilesetCatalog()
{
    validateTilesetCatalog = true;
}
#if !defined(QT_NO_DEBUG) && defined(ZOMBOID) && defined(_MSC_VER)
static void __cdecl invalid_parameter_handler(
   const wchar_t * expression,
   const wchar_t * function,
   const wchar_t * file,
   unsigned int line,
   uintptr_t pReserved)
{
    qDebug() << expression << function << file << line;
}

#endif

int main(int argc, char *argv[])
{
#if !defined(QT_NO_DEBUG) && defined(ZOMBOID) && defined(_MSC_VER)
    _set_invalid_parameter_handler(invalid_parameter_handler);
#endif

    /*
     * On X11, Tiled uses the 'raster' graphics system by default, because the
     * X11 native graphics system has performance problems with drawing the
     * tile grid.
     */
#ifdef Q_WS_X11
    QApplication::setGraphicsSystem(QLatin1String("raster"));
#endif

    TiledApplication a(argc, argv);

#ifdef ZOMBOID
    Q_INIT_RESOURCE(buildingeditor);
#endif

    a.setOrganizationName(QLatin1String("TheIndieStone"));
#ifdef ZOMBOID
    const bool buildingEditorMode = QFileInfo(QCoreApplication::applicationFilePath())
            .completeBaseName().compare(QLatin1String("BuildingEd"), Qt::CaseInsensitive) == 0;
    a.setApplicationName(buildingEditorMode
                         ? QLatin1String("BuildingEd")
                         : QLatin1String("TileZed"));
    if (buildingEditorMode) {
        QIcon buildingEditorIcon(QLatin1String(":images/buildinged-icon-16.png"));
        buildingEditorIcon.addFile(QLatin1String(":images/buildinged-icon-32.png"));
        a.setWindowIcon(buildingEditorIcon);
    }
#else
    a.setApplicationName(QLatin1String("TileZed"));
#endif
    PortableSettings::configure();
    PortableSettings::installLogging();
    PortableSettings::prepareVersionedSettings();
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.8.1"));
#endif

#ifdef Q_WS_MAC
    a.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    LanguageManager *languageManager = LanguageManager::instance();
    languageManager->installTranslators();

    CommandLineHandler commandLine;
    if (!commandLine.parse(QCoreApplication::arguments()))
        return 0;
    if (commandLine.quit)
        return 0;
    if (commandLine.validateBrushPerformance &&
            commandLine.filesToOpen().isEmpty()) {
        QString error;
        QString summary;
        const bool valid = validateBrushUndoBuffers(&error)
                && validateTileToolPointerPerformance(&summary, &error);
        qInfo().noquote()
                << "TileZed brush-performance validation:"
                << (valid ? QStringLiteral("PASS: %1").arg(summary)
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateDepthMapEditor) {
        QString error;
        const bool valid = DepthMapEditor::runFormatSelfTest(&error);
        qInfo().noquote()
                << "TileZed depth-map editor validation:"
                << (valid ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateLootDistributions) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << "Loot-distribution validation expects a game "
                           "directory argument.";
            return 1;
        }
        QString summary;
        QString error;
        const bool valid = LootDistributionDialog::validateDefinitions(
                    paths.first(),
                    paths.size() > 1 ? paths.at(1) : QString(),
                    &summary, &error);
        qInfo().noquote()
                << "Procedural loot validation:"
                << (valid ? QStringLiteral("PASS: %1").arg(summary)
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validatePackTools) {
        QString compareSummary;
        QString extractSummary;
        QString error;
        const bool compareValid =
                PackCompare::runSelfTest(&compareSummary, &error);
        if (!compareValid) {
            qInfo().noquote()
                    << "TileZed pack-tools validation:"
                    << QStringLiteral("FAIL: %1").arg(error);
            return 1;
        }
        const bool extractValid =
                PackExtractDialog::runSelfTest(
                    &extractSummary, &error);
        const bool valid = compareValid && extractValid;
        qInfo().noquote()
                << "TileZed pack-tools validation:"
                << (valid
                    ? QStringLiteral("PASS: %1; %2")
                      .arg(compareSummary, extractSummary)
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateAutomapperRules) {
        QString summary;
        QString error;
        const bool valid =
                AutomappingManager::runRuleListSelfTest(
                    &summary, &error);
        qInfo().noquote()
                << "TileZed Automapper-rules validation:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(summary)
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderPackComparator) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << "Pack-comparator rendering expects an "
                           "output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = PackCompare::renderValidation(
                    paths.first(), &error);
        qInfo().noquote()
                << "TileZed pack-comparator render:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(paths.first())
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderPackExtractor) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << "Pack-extractor rendering expects an "
                           "output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = PackExtractDialog::renderValidation(
                    paths.first(), &error);
        qInfo().noquote()
                << "TileZed pack-extractor render:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(paths.first())
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderTileDefComparator) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.isEmpty()) {
            qCritical() << ".tiles-comparator rendering expects an "
                           "output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = TileDefCompare::renderValidation(
                    paths.first(), &error);
        qInfo().noquote()
                << "TileZed .tiles-comparator render:"
                << (valid
                    ? QStringLiteral("PASS: %1").arg(paths.first())
                    : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.renderLootDistributions) {
        const QStringList paths = commandLine.filesToOpen();
        if (paths.size() < 2) {
            qCritical() << "Loot editor rendering expects a game directory "
                           "and output PNG argument.";
            return 1;
        }
        QString error;
        const bool valid = LootDistributionDialog::renderValidation(
                    paths.at(0),
                    paths.size() > 2 ? paths.at(2) : QString(),
                    paths.at(1), &error);
        qInfo().noquote()
                << "Procedural loot editor render:"
                << (valid ? QStringLiteral("PASS: %1").arg(paths.at(1))
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    if (commandLine.validateTileDefSplit) {
        QString error;
        const bool valid = validateTileDefSplitting(&error);
        qInfo().noquote()
                << "TileZed tiledef-split validation:"
                << (valid ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL: %1").arg(error));
        return valid ? 0 : 1;
    }
    const bool buildingEditorValidation =
            commandLine.validateBuildingCategories
            || commandLine.validateBuildingClipboard
            || commandLine.validateTilesetCatalog;
    if (buildingEditorMode && !buildingEditorValidation && a.isRunning()) {
        const QStringList fileNames = commandLine.filesToOpen();
        bool delivered = fileNames.isEmpty()
                ? a.sendMessage(QString())
                : true;
        for (const QString &fileName : fileNames)
            delivered = a.sendMessage(fileName) && delivered;
        if (!delivered)
            qWarning() << "BuildingEd could not contact the running instance.";
        return delivered ? 0 : 1;
    }
    if (!FirstLaunchDialog::ensureSharedPaths())
        return 0;
    if (commandLine.disableOpenGL) {
        Preferences::instance()->setUseOpenGL(false);
        if (buildingEditorMode)
            BuildingEditor::BuildingPreferences::instance()
                    ->setUseOpenGL(false);
    }

#ifdef ZOMBOID
    Preferences::instance()->applyTheme();
    if (buildingEditorMode) {
        QStringList pendingBuildingFiles = commandLine.filesToOpen();
        bool buildingStartupComplete = false;
        BuildingEditor::BuildingEditorWindow buildingEditor;
        buildingEditor.setDiagnosticShortcutsEnabled(
                    commandLine.buildingDebugKeys);
        a.setActivationWindow(&buildingEditor);
        QObject::connect(
                    &a, &QtSingleApplication::messageReceived,
                    &buildingEditor,
                    [&buildingEditor, &pendingBuildingFiles,
                     &buildingStartupComplete](const QString &fileName) {
            if (fileName.isEmpty())
                return;
            if (buildingStartupComplete)
                buildingEditor.openFile(fileName);
            else
                pendingBuildingFiles.append(fileName);
        });
        buildingEditor.show();
        buildingEditor.readSettings();
        QTimer::singleShot(0, &buildingEditor,
                           [&buildingEditor, &commandLine,
                            &pendingBuildingFiles,
                            &buildingStartupComplete]() {
            buildingEditor.setEnabled(false);
            qApp->processEvents(QEventLoop::AllEvents);
            const bool configLoaded =
                    MainWindow::InitConfigFiles(&buildingEditor);
            buildingEditor.setEnabled(true);
            if (!configLoaded ||
                    !buildingEditor.Startup()) {
                buildingEditor.close();
                QCoreApplication::quit();
                return;
            }
            if (commandLine.validateTilesetCatalog) {
                QString error;
                const bool valid =
                        validateSingleRowTilesetCatalog(&error)
                        && validateKeyboardShortcutFormat(&error);
                qInfo().noquote()
                        << "BuildingEd one-row tileset validation:"
                        << (valid ? QStringLiteral("PASS")
                                  : QStringLiteral("FAIL: %1").arg(error));
                QCoreApplication::exit(valid ? 0 : 3);
                return;
            }
            if (commandLine.validateBuildingClipboard) {
                QString error;
                const bool valid = !commandLine.filesToOpen().isEmpty()
                        && validateBuildingClipboardFixture(
                            &buildingEditor,
                            commandLine.filesToOpen().first(), &error);
                if (commandLine.filesToOpen().isEmpty())
                    error = QStringLiteral("A TBX fixture path is required");
                qInfo().noquote()
                        << "BuildingEd fixture copy-paste validation:"
                        << (valid ? QStringLiteral("PASS")
                                  : QStringLiteral("FAIL: %1").arg(error));
                BuildingEditor::BuildingDocumentMgr::instance()
                        ->closeAllDocuments();
                QCoreApplication::exit(valid ? 0 : 2);
                return;
            }
            if (commandLine.validateBuildingCategories) {
                BuildingEditor::BuildingTemplate *buildingTemplate = nullptr;
                BuildingEditor::BuildingTemplates *templates =
                        BuildingEditor::BuildingTemplates::instance();
                if (templates->templateCount() > 0) {
                    buildingTemplate = templates->templateAt(
                                templates->templateCount() / 2);
                }
                BuildingEditor::Building *building =
                        new BuildingEditor::Building(
                            17, 23, buildingTemplate);
                building->insertFloor(
                            0, new BuildingEditor::BuildingFloor(
                                building, 0));
                BuildingEditor::BuildingDocument *document =
                        new BuildingEditor::BuildingDocument(
                            building, QString());
                BuildingEditor::BuildingDocumentMgr::instance()
                        ->addDocument(document);
                QCoreApplication::processEvents(
                            QEventLoop::ExcludeUserInputEvents);
                qInfo().noquote()
                        << "BuildingEd category validation document:"
                        << "17x23, template="
                        << (buildingTemplate
                            ? buildingTemplate->name()
                            : QStringLiteral("<default>"));
                QString templateTilesError;
                const bool templateTilesValid =
                        validateAllBuildingTemplates(
                            &templateTilesError);
                qInfo().noquote()
                        << "BuildingEd all-template tile validation:"
                        << (templateTilesValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(templateTilesError));
                QString transparentTileError;
                const bool transparentTileValid =
                        validateTransparentTileContract(
                            &transparentTileError);
                qInfo().noquote()
                        << "BuildingEd transparent-tile validation:"
                        << (transparentTileValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(transparentTileError));
                QString newBuildingDialogError;
                const bool newBuildingDialogValid =
                        validateNewBuildingDialogLayout(
                            &buildingEditor, &newBuildingDialogError);
                qInfo().noquote()
                        << "BuildingEd New Building dialog validation:"
                        << (newBuildingDialogValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(newBuildingDialogError));
                QString roomFloorError;
                const bool roomFloorValid =
                        validateRoomFloorRoundTrip(&roomFloorError);
                qInfo().noquote()
                        << "BuildingEd room-floor round-trip validation:"
                        << (roomFloorValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(roomFloorError));
                bool fixtureFloorValid = true;
                QString fixtureFloorError;
                const QStringList validationFiles =
                        commandLine.filesToOpen();
                if (!validationFiles.isEmpty()) {
                    fixtureFloorValid = validateRoomFloorsInFixture(
                                validationFiles.first(),
                                &fixtureFloorError);
                    qInfo().noquote()
                            << "BuildingEd fixture room-floor validation:"
                            << (fixtureFloorValid
                                ? QStringLiteral("PASS: %1")
                                  .arg(validationFiles.first())
                                : QStringLiteral("FAIL: %1")
                                  .arg(fixtureFloorError));
                }
                BuildingEditor::BuildingTilesetDock *tilesetDock =
                        buildingEditor.findChild<
                            BuildingEditor::BuildingTilesetDock *>();
                QString tileModeError;
                const bool tileModeValid = tilesetDock
                        && tilesetDock->validateTilesetCatalog(
                            &tileModeError);
                qInfo().noquote()
                        << "BuildingEd Tile mode validation:"
                        << (tileModeValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(tileModeError));
                BuildingEditor::BuildingTilesDialog *tilesDialog =
                        BuildingEditor::BuildingTilesDialog::instance();
                QString tilesDialogError;
                const bool tilesDialogValid = tilesDialog
                        && tilesDialog->validateTilesetCatalog(
                            &tilesDialogError);
                qInfo().noquote()
                        << "BuildingEd Building > Tiles validation:"
                        << (tilesDialogValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(tilesDialogError));
                BuildingEditor::BuildingFurnitureDock *furnitureDock =
                        buildingEditor.findChild<
                            BuildingEditor::BuildingFurnitureDock *>();
                QString furnitureError;
                const bool furnitureValid = furnitureDock
                        && furnitureDock->validateFurnitureCatalog(
                            &furnitureError);
                qInfo().noquote()
                        << "BuildingEd Furniture validation:"
                        << (furnitureValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(furnitureError));
                const QList<BuildingEditor::CategoryDock *> categoryDocks =
                        buildingEditor.findChildren<
                            BuildingEditor::CategoryDock *>();
                bool categoriesValid = categoryDocks.count() == 2;
                int categoryDockIndex = 0;
                for (BuildingEditor::CategoryDock *categoryDock
                     : categoryDocks) {
                    const bool dockValid = categoryDock
                            && categoryDock->validateAllTileCategories();
                    qInfo() << "BuildingEd object-mode category dock"
                            << ++categoryDockIndex << "of"
                            << categoryDocks.count() << ":"
                            << (dockValid ? "PASS" : "FAIL");
                    categoriesValid = categoriesValid && dockValid;
                }
                const bool valid = tileModeValid
                        && tilesDialogValid
                        && templateTilesValid
                        && transparentTileValid
                        && newBuildingDialogValid
                        && roomFloorValid
                        && fixtureFloorValid
                        && furnitureValid
                        && categoriesValid;
                QString luaFurnitureError;
                const bool luaFurnitureValid =
                        validateBuildingLuaFurniture(
                            document, &luaFurnitureError);
                qInfo().noquote()
                        << "BuildingEd Lua furniture validation:"
                        << (luaFurnitureValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(luaFurnitureError));
                QString clipboardError;
                const bool clipboardValid = validateBuildingClipboard(
                            &buildingEditor, document, &clipboardError);
                qInfo().noquote()
                        << "BuildingEd copy-paste validation:"
                        << (clipboardValid
                            ? QStringLiteral("PASS")
                            : QStringLiteral("FAIL: %1")
                              .arg(clipboardError));
                bool fixtureClipboardValid = true;
                if (!commandLine.filesToOpen().isEmpty()) {
                    QString fixtureClipboardError;
                    fixtureClipboardValid = validateBuildingClipboardFixture(
                                &buildingEditor,
                                commandLine.filesToOpen().first(),
                                &fixtureClipboardError);
                    qInfo().noquote()
                            << "BuildingEd fixture copy-paste validation:"
                            << (fixtureClipboardValid
                                ? QStringLiteral("PASS")
                                : QStringLiteral("FAIL: %1")
                                  .arg(fixtureClipboardError));
                }
                const bool allValid = valid && luaFurnitureValid
                        && clipboardValid && fixtureClipboardValid;
                qInfo() << "BuildingEd category validation result:"
                        << (allValid ? "PASS" : "FAIL");
                BuildingEditor::BuildingDocumentMgr::instance()
                        ->closeAllDocuments();
                QCoreApplication::exit(allValid ? 0 : 2);
                return;
            }
            buildingStartupComplete = true;
            foreach (const QString &fileName, pendingBuildingFiles)
                buildingEditor.openFile(fileName);
            pendingBuildingFiles.clear();
            buildingEditor.readSettings();
            buildingEditor.startSettingsAutoSave();
            buildingEditor.raise();
            buildingEditor.activateWindow();
        });
        return a.exec();
    }
    if (a.isRunning()) {
        if (!commandLine.filesToOpen().isEmpty()) {
            foreach (const QString &fileName, commandLine.filesToOpen())
                a.sendMessage(fileName);
            return 0;
        }
    }
#endif

    MainWindow w;
#ifdef ZOMBOID
    ZProgressManager::instance()->setMainWindow(&w);
#endif
    w.show();
#ifdef ZOMBOID
    a.setActivationWindow(&w);
    w.connect(&a, &QtSingleApplication::messageReceived, &w, qOverload<const QString&>(&MainWindow::openFile));
    w.readSettings();

    if (!w.InitConfigFiles())
        return 0;
    if (commandLine.validateBrushPerformance) {
        QString error;
        QString summary;
        const bool coreValid = validateBrushUndoBuffers(&error);
        QString pointerSummary;
        const bool pointerValid = coreValid
                && validateTileToolPointerPerformance(
                    &pointerSummary, &error);
        const bool mapValid = pointerValid && validateBrushMapPerformance(
                    commandLine.filesToOpen().first(), &summary, &error);
        if (mapValid)
            summary = pointerSummary + QStringLiteral(", ") + summary;
        qInfo().noquote()
                << "TileZed brush-performance validation:"
                << (mapValid
                    ? QStringLiteral("PASS: %1").arg(summary)
                    : QStringLiteral("FAIL: %1").arg(error));
        return mapValid ? 0 : 1;
    }
    if (commandLine.validateTilesetCatalog) {
        QString catalogError;
        QString tagError;
        QString transparentTileError;
        const bool catalogValid = validateSingleRowTilesetCatalog(
                    &catalogError);
        const bool tagsValid = TilesetDock::validateResolutionTags(
                    &tagError);
        const bool transparentTileValid = validateTransparentTileContract(
                    &transparentTileError);
        qInfo().noquote()
                << "TileZed one-row tileset validation:"
                << (catalogValid ? QStringLiteral("PASS")
                                 : QStringLiteral("FAIL: %1")
                                   .arg(catalogError));
        qInfo().noquote()
                << "TileZed tileset resolution-tag validation:"
                << (tagsValid ? QStringLiteral("PASS")
                              : QStringLiteral("FAIL: %1").arg(tagError));
        qInfo().noquote()
                << "TileZed transparent-tile validation:"
                << (transparentTileValid
                    ? QStringLiteral("PASS")
                    : QStringLiteral("FAIL: %1")
                      .arg(transparentTileError));
        return catalogValid && tagsValid && transparentTileValid ? 0 : 3;
    }
    QSettings sessionSettings(QSettings::IniFormat, QSettings::UserScope,
                              QLatin1String("TheIndieStone"),
                              QLatin1String("TileZed"));
    const QString cleanExitKey =
            QLatin1String("Startup/PreviousSessionClosedCleanly");
    const bool previousSessionClosedCleanly =
            sessionSettings.value(cleanExitKey, true).toBool();
    sessionSettings.setValue(cleanExitKey, false);
    sessionSettings.sync();
    foreach (QString f, Preferences::instance()->worldedFiles()) {
        if (f.isEmpty())
            continue;
        if (QFileInfo::exists(f) == false) {
            QMessageBox::warning(&w, QLatin1String("Missing PZW"), QLatin1String("WorldEd project not found:\n%1").arg(f));
            continue;
        }
        WorldEd::WorldEdMgr::instance()->addProject(f);
    }
    for (const QString &f : Preferences::instance()->tilePropertiesFiles()) {
        if (f.isEmpty())
            continue;
        if (QFileInfo::exists(f) == false) {
            QMessageBox::warning(&w, QLatin1String("File Not Found"), QLatin1String("Tile properties file not found.\nChange this in the Preferences.\n%1").arg(f));
            continue;
        }
    }
#endif
    QObject::connect(&a, &TiledApplication::fileOpenRequest,
                     &w, qOverload<const QString&>(&MainWindow::openFile));

    if (!commandLine.filesToOpen().isEmpty()) {
#ifdef ZOMBOID
        gStartupBlockRendering = false;
#endif
        foreach (const QString &fileName, commandLine.filesToOpen())
            w.openFile(fileName);
    } else if (Preferences::instance()->restoreLastSession()) {
        if (previousSessionClosedCleanly) {
            w.openLastFiles();
        } else {
            qWarning() << "Automatic TileZed session restore skipped after "
                          "an unclean shutdown.";
            QMessageBox::warning(
                        &w, QObject::tr("TileZed Session Recovery"),
                        QObject::tr(
                            "TileZed did not close cleanly last time.\n\n"
                            "Automatic document restoration was skipped to "
                            "avoid repeating a startup crash. Your map files "
                            "were not changed; open the required file "
                            "manually after checking settings/logs."));
        }
    }
#ifdef ZOMBOID
    w.readSettings();
    w.startSettingsAutoSave();
#endif

    const int result = a.exec();
    sessionSettings.setValue(cleanExitKey, true);
    sessionSettings.sync();
    return result;
}
