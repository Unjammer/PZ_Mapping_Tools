#ifndef BUILDINGSTAIRVALIDATION_H
#define BUILDINGSTAIRVALIDATION_H

#include "building.h"
#include "buildingdocument.h"
#include "buildingdocumentmgr.h"
#include "buildingeditorwindow.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingorthoview.h"
#include "buildingreader.h"
#include "buildingtemplates.h"
#include "buildingtiles.h"
#include "buildingtools.h"
#include "objecteditmode.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QGraphicsSceneMouseEvent>
#include <QScopeGuard>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QUndoStack>

namespace BuildingEditor {

inline bool validateBuildingStairs(BuildingEditorWindow *window, const QString &outputDirectory,
                                   QString *error)
{
    const auto fail = [error](const QString &message) {
        *error = message;
        return false;
    };
    QTemporaryDir temporary;
    QDir directory(outputDirectory.isEmpty() ? temporary.path() : outputDirectory);
    if ((!temporary.isValid() && outputDirectory.isEmpty()) || !directory.exists()
            || !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty())
        return fail(QStringLiteral("Stair validation requires an existing empty output directory"));

    OrthoObjectEditMode *orthoMode = nullptr;
    for (IMode *mode : window->findChildren<IMode *>()) {
        if (auto *candidate = dynamic_cast<OrthoObjectEditMode *>(mode)) {
            orthoMode = candidate;
            break;
        }
    }
    if (!orthoMode)
        return fail(QStringLiteral("Ortho edit mode is unavailable"));

    BuildingDocumentMgr *documents = BuildingDocumentMgr::instance();
    BuildingDocument *previousDocument = documents->currentDocument();
    IMode *previousMode = ModeManager::instance().currentMode();
    const auto restore = qScopeGuard([&]() {
        if (previousDocument)
            documents->setCurrentDocument(previousDocument);
        ModeManager::instance().setCurrentMode(previousMode);
    });
    BTC_Stairs stairsCategory(QStringLiteral("Stairs"));
    BuildingTileEntry stairTiles(&stairsCategory);
    const int stairIds[] = {0, 1, 2, 8, 9, 10};
    for (int index = 0; index < BTC_Stairs::EnumCount; ++index)
        stairTiles.mTiles[index] = BuildingTilesMgr::instance()->get(
                    QStringLiteral("fixtures_stairs_01_%1").arg(stairIds[index]));
    BTC_Floors floorCategory(QStringLiteral("Floors"));
    BuildingTileEntry floorTiles(&floorCategory);
    floorTiles.mTiles[BTC_Floors::Floor] = BuildingTilesMgr::instance()->get(
                QStringLiteral("floors_interior_tilesandwood_01_0"));
    int checked = 0;
    for (BuildingObject::Direction direction : {BuildingObject::N, BuildingObject::W}) {
        for (bool validTiles : {true, false}) {
            auto *building = new Building(8, 8);
            auto *room = new Room;
            room->Name = QStringLiteral("Stair room");
            room->internalName = QStringLiteral("livingroom");
            room->Color = qRgb(160, 180, 200);
            room->setTile(Room::Floor, &floorTiles);
            building->insertRoom(0, room);
            for (int level = 0; level < 2; ++level) {
                auto *floor = new BuildingFloor(building, level);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        floor->SetRoomAt(x, y, room);
                building->insertFloor(level, floor);
            }
            auto *document = new BuildingDocument(building, QString());
            documents->addDocument(document);
            const auto closeDocument = qScopeGuard([&]() { documents->closeDocument(document); });
            ModeManager::instance().setCurrentMode(orthoMode);
            document->setCurrentFloor(building->floor(0));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            auto *scene = dynamic_cast<BuildingOrthoScene *>(ToolManager::instance()->currentEditor());
            if (!scene || scene->document() != document)
                return fail(QStringLiteral("Stair placement is not using the current Ortho scene"));

            building->setStairsTile(validTiles ? &stairTiles : BuildingTilesMgr::instance()->noneTileEntry());
            StairsTool::instance()->makeCurrent();
            const QPointF tilePosition = direction == BuildingObject::N
                    ? QPointF(1.1, 1.5) : QPointF(1.5, 1.1);
            const QPointF scenePosition = scene->tileToSceneF(tilePosition, 0);
            QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
            move.setScenePos(scenePosition);
            scene->mouseMoveEvent(&move);
            QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
            press.setScenePos(scenePosition);
            press.setButton(Qt::LeftButton);
            press.setButtons(Qt::LeftButton);
            scene->mousePressEvent(&press);
            QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
            release.setScenePos(scenePosition);
            release.setButton(Qt::LeftButton);
            scene->mouseReleaseEvent(&release);

            BuildingFloor *floor = building->floor(0);
            if (floor->objectCount() != 1 || document->undoStack()->count() != 1)
                return fail(QStringLiteral("Ortho stair click did not create one undoable object"));
            Stairs *stairs = floor->object(0)->asStairs();
            if (!stairs || stairs->pos() != QPoint(1, 1) || stairs->dir() != direction
                    || stairs->tile() != building->stairsTile())
                return fail(QStringLiteral("Ortho stair direction, position or tile selection changed"));
            document->undoStack()->undo();
            if (floor->objectCount() != 0 || !document->undoStack()->canRedo())
                return fail(QStringLiteral("Ortho stair Undo did not remove the object"));
            document->undoStack()->redo();
            if (floor->objectCount() != 1 || floor->object(0) != stairs)
                return fail(QStringLiteral("Ortho stair Redo did not restore the object"));

            const auto checkOpening = [direction](Building *candidate) {
                candidate->floor(0)->LayoutToSquares();
                candidate->floor(1)->LayoutToSquares();
                for (int offset = 1; offset <= 3; ++offset) {
                    const QPoint position = QPoint(1, 1) + (direction == BuildingObject::N
                                                           ? QPoint(0, offset) : QPoint(offset, 0));
                    if (candidate->floor(1)->squares[position.x()][position.y()]
                            .mEntries[BuildingFloor::Square::SectionFloor] != nullptr)
                        return false;
                }
                return candidate->floor(1)->squares[6][6]
                        .mEntries[BuildingFloor::Square::SectionFloor] != nullptr;
            };
            if (!checkOpening(building))
                return fail(QStringLiteral("Ortho stair lost its upper-floor opening"));
            const QString path = directory.filePath(QStringLiteral("ortho-stairs-%1-%2.tbx")
                       .arg(direction == BuildingObject::N ? QStringLiteral("N") : QStringLiteral("W"),
                            validTiles ? QStringLiteral("valid") : QStringLiteral("missing")));
            if (!document->write(path, *error))
                return false;
            {
                BuildingReader reader;
                QScopedPointer<Building> raw(reader.read(path));
                if (!raw || raw->floorCount() != 2 || raw->floor(0)->objectCount() != 1)
                    return fail(QStringLiteral("Ortho stair TBX could not be read before normalization"));
                Stairs *rawStairs = raw->floor(0)->object(0)->asStairs();
                if (!rawStairs || (rawStairs->tile() != nullptr) != validTiles)
                    return fail(QStringLiteral("Ortho stair TBX changed its raw graphics reference"));
            }
            QScopedPointer<BuildingDocument> reopened(BuildingDocument::read(path, *error));
            if (!reopened || reopened->building()->floorCount() != 2
                    || reopened->building()->floor(0)->objectCount() != 1)
                return fail(QStringLiteral("Ortho stair TBX did not reopen with both floors and its object"));
            Stairs *loaded = reopened->building()->floor(0)->object(0)->asStairs();
            if (!loaded || loaded->pos() != stairs->pos() || loaded->dir() != direction
                    || !loaded->tile() || loaded->tile()->isNone() == validTiles
                    || !checkOpening(reopened->building()))
                return fail(QStringLiteral("Ortho stair TBX changed its direction, graphics or floor opening"));
            if (validTiles) {
                for (int index = 0; index < BTC_Stairs::EnumCount; ++index) {
                    if (loaded->tile()->tile(index)->name() != stairTiles.tile(index)->name())
                        return fail(QStringLiteral("Ortho stair TBX changed a tile name"));
                }
            }
            ++checked;
        }
    }
    qInfo() << "BuildingEd Ortho stair validation:" << checked
            << "placements, Undo/Redo, TBX reopen and upper-floor openings PASS";
    return true;
}

}

#endif
