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

#include "lotfilesmanager256.h"

#include "exportlotsprogressdialog.h"
#include "generatelotsfailuredialog.h"
#include "mainwindow.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "mapobject.h"
#include "progress.h"
#include "tiledeffile.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "world.h"
#include "worldcell.h"
#include "worldconstants.h"
#include "worlddocument.h"

#include "BuildingEditor/buildingfloor.h"
#include "BuildingEditor/roofhiding.h"

#include "InGameMap/clipper.hpp"

#include "navigation/chunkdatafile256.h"
#include "navigation/isogridsquare256.h"

#include "objectgroup.h"
#include "tile.h"
#include "tileset.h"

#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>

using namespace Tiled;

static int VERSION1 = 1; // Added 4-byte 'LOTH' at start of .lotheader files.
                         // Added 4-byte 'LOTP' at start of .lotpack files, followed by 4-byte version number.

static int VERSION_LATEST = VERSION1;

#define MERGE_ROOMS_ACROSS_CELL_BOUNDARIES 1

static QString nameOfTileset(const Tileset *tileset)
{
    QString name = tileset->imageSource();
    if (name.contains(QLatin1String("/")))
        name = name.mid(name.lastIndexOf(QLatin1String("/")) + 1);
    name.replace(QLatin1String(".png"), QLatin1String(""));
    return name;
}

static void SaveString(QDataStream& out, const QString& str)
{
    for (int i = 0; i < str.length(); i++) {
        if (str[i].toLatin1() == '\n') continue;
        out << quint8(str[i].toLatin1());
    }
    out << quint8('\n');
}

/////

LotFilesManager256 *LotFilesManager256::mInstance = nullptr;

LotFilesManager256 *LotFilesManager256::instance()
{
    if (mInstance == nullptr) {
        mInstance = new LotFilesManager256();
    }
    return mInstance;
}

void LotFilesManager256::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

LotFilesManager256::LotFilesManager256(QObject *parent) :
    QObject(parent),
    mProgressDialog(nullptr)
{
    mJumboZoneList += new JumboZone(QStringLiteral("DeepForest"), 100);
    mJumboZoneList += new JumboZone(QStringLiteral("Farm"), 80);
    mJumboZoneList += new JumboZone(QStringLiteral("FarmLand"), 80);
    mJumboZoneList += new JumboZone(QStringLiteral("Forest"), 50);
//    mJumboZoneList += new JumboZone(QStringLiteral("TownZone"), 80);
    mJumboZoneList += new JumboZone(QStringLiteral("Vegitation"), 10);
}

LotFilesManager256::~LotFilesManager256()
{
    //    stopThreads();
}

void LotFilesManager256::collectLotsOverlappingCellBounds()
{
    mLotsOverlappingCellBounds.clear();

    World *world = mWorldDoc->world();
    for (int y = 0; y < world->height(); y++) {
        for (int x = 0; x < world->width(); x++) {
            WorldCell *cell = world->cellAt(x, y);
            if (cell == nullptr) {
                continue;
            }
            cell->getLotsOverlappingCellBounds(mLotsOverlappingCellBounds);
        }
    }
}

void LotFilesManager256::startThreads(int numberOfThreads)
{
    stopThreads();

    mWorkerThreads.resize(numberOfThreads);
    mWorkers.resize(numberOfThreads);
    for (int i = 0; i < numberOfThreads; i++) {
        mWorkerThreads[i] = new InterruptibleThread;
        connect(mWorkerThreads[i], &QThread::finished, [this, i]() {
            this->workerThreadFinished(i);
        });
        mWorkers[i] = new LotFilesWorker256(this, mWorkerThreads[i]);
        mWorkers[i]->moveToThread(mWorkerThreads[i]);
        mWorkerThreads[i]->start();
    }

    mThreadsStopped = false;
}

void LotFilesManager256::stopThreads()
{
    for (int i = 0; i < mWorkerThreads.size(); i++) {
        qDebug() << "LotFilesManager256: quitting thread #" << i;
        mWorkerThreads[i]->interrupt();
        mWorkerThreads[i]->quit();
    }
#if 0
    while (true) {
        bool bAnyAlive = false;
        for (int i = 0; i < mWorkerThreads.size(); i++) {
            if (mWorkers[i] != nullptr) {
                qDebug() << "LotFilesManager256: bAnyAlive #" << i;
                bAnyAlive = true;
                break;
            }
        }
        if (bAnyAlive == false) {
            break;
        }
        qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
        QThread::msleep(1000 / 30);
    }
    mWorkerThreads.clear();
    mWorkers.clear();
#endif
}

bool LotFilesManager256::generateWorld(WorldDocument *worldDoc, GenerateMode mode)
{
    mWorldDoc = worldDoc;
    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();
    const WorldGridFormat gridFormat = mWorldDoc->world()->gridFormat();

    mCellBounds256 = CombinedCellMaps::outputCellRect(
            gridFormat,
            QRect(lotSettings.worldOrigin, QSize(mWorldDoc->world()->size())));
    qInfo() << "LOT export geometry:"
            << "grid" << worldGridFormatName(gridFormat)
            << "source-cell-size" << mWorldDoc->world()->cellSize()
            << "world-origin" << lotSettings.worldOrigin
            << "world-size" << mWorldDoc->world()->size()
            << "output-cell-bounds" << mCellBounds256;

    mDialog = new ExportLotsProgressDialog(MainWindow::instance());
    ExportLotsProgressDialog& progress = *mDialog;
    progress.setModal(true);
    mProgressDialog = &progress;
    connect(mProgressDialog, &ExportLotsProgressDialog::cancelled, this, &LotFilesManager256::cancel);
    progress.show();
    progress.activateWindow();
    progress.raise();
    qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
    progress.setWorldSize(mCellBounds256.width(), mCellBounds256.height());
    progress.setPrompt(QLatin1String("Reading Zombie Spawn Map"));

    mCancel = false;

    QString spawnMap = lotSettings.zombieSpawnMap;
    if (!QFileInfo::exists(spawnMap)) {
        mError = tr("Couldn't find the Zombie Spawn Map image.\n%1")
                .arg(spawnMap);
        delete mDialog;
        mDialog = nullptr;
        return false;
    }
    ZombieSpawnMap = QImage(spawnMap);
    if (ZombieSpawnMap.isNull()) {
        mError = tr("Couldn't read the Zombie Spawn Map image.\n%1")
                .arg(spawnMap);
        delete mDialog;
        mDialog = nullptr;
        return false;
    }

    if (!Navigate::IsoGridSquare256::loadTileDefFiles(lotSettings, mError)) {
        delete mDialog;
        mDialog = nullptr;
        return false;
    }

    QString tilesDirectory = TileMetaInfoMgr::instance()->tilesDirectory();
    if (tilesDirectory.isEmpty() || !QFileInfo::exists(tilesDirectory)) {
        mError = tr("The Tiles Directory could not be found.  Please set it in the Tilesets Dialog in TileZed.");
        delete mDialog;
        mDialog = nullptr;
        return false;
    }
#if 0
    if (!TileMetaInfoMgr::instance()->readTxt()) {
        mError = tr("%1\n(while reading %2)")
                .arg(TileMetaInfoMgr::instance()->errorString())
                .arg(TileMetaInfoMgr::instance()->txtName());
        return false;
    }
#endif

    mStats.reset();

    progress.setPrompt(QLatin1String("Generating .lot files"));

    World *world = worldDoc->world();

    // Legacy 300x300 cells may overlap several 256x256 output cells.
    // Native 256x256 cells map directly to one output cell.
    mDoneCells256.clear();
    mCell256Queue.clear();

    mFailures.clear();

    collectLotsOverlappingCellBounds();

    startThreads(lotSettings.numberOfThreads);

    if (mode == GenerateSelected) {
        for (WorldCell *cell : worldDoc->selectedCells()) {
            if (cell->mapFilePath().isEmpty()) {
                continue;
            }
            const int sourceCellX = lotSettings.worldOrigin.x() + cell->x();
            const int sourceCellY = lotSettings.worldOrigin.y() + cell->y();
            const QRect cellBounds256 = CombinedCellMaps::outputCellRect(
                    gridFormat, QRect(sourceCellX, sourceCellY, 1, 1));
            for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
                for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
                    mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Pending);
                }
            }
        }
        for (WorldCell *cell : worldDoc->selectedCells()) {
            if (mCancel) {
                break;
            }
            if (!generateCell(cell)) {
                if (mError.isEmpty() == false) { // mError is empty when cancelling
                    mFailures += GenerateCellFailure(cell, mError);
                }
//                return false;
            }
        }
    } else {
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell* cell = world->cellAt(x, y);
                if (cell->mapFilePath().isEmpty()) {
                    continue;
                }
                const QRect cellBounds256 = CombinedCellMaps::outputCellRect(
                        gridFormat,
                        QRect(lotSettings.worldOrigin.x() + x,
                              lotSettings.worldOrigin.y() + y, 1, 1));
                for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
                    for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
                        mProgressDialog->setCellStatus(cell256X - mCellBounds256.x(), cell256Y - mCellBounds256.y(), ExportLotsProgressDialog::CellStatus::Pending);
                    }
                }
            }
        }
        for (int y = 0; y < world->height(); y++) {
            if (mCancel) {
                break;
            }
            for (int x = 0; x < world->width(); x++) {
                WorldCell* cell = world->cellAt(x, y);
                if (mCancel) {
                    break;
                }
                if (!generateCell(cell)) {
                    if (mError.isEmpty() == false) { // mError is empty when cancelling
                        mFailures += GenerateCellFailure(cell, mError);
                    }
//                    return false;
                }
            }
        }
    }

    Tiled::Internal::TileDefWatcher *tileDefWatcher = BuildingEditor::getTileDefWatcher();
    tileDefWatcher->check();

#if 1
    // The application should continue normally with the modal dialog box visible until everything finishes.
    mTimer.setInterval(1000 / 30);
    connect(&mTimer, &QTimer::timeout, this, &LotFilesManager256::workTimerTimeout);
    mTimer.start();
#else
    while (true) {
        qDebug() << "LotFilesManager256: Waiting for workers to finish";
        updateWorkers();
        if (getBusyWorker() == nullptr) {
            break;
        }
        qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
        Sleep::msleep(1000 / 30);
    }

    qDebug() << "LotFilesManager256: Stopping threads";
    stopThreads();

    qDebug() << "LotFilesManager256: Stopped threads";
    qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
    progress.setVisible(false);

    if (!mFailures.isEmpty()) {
        QStringList errorList;
        for (GenerateCellFailure failure : mFailures) {
            errorList += QString(QStringLiteral("Cell %1,%2: %3")).arg(failure.cell->x()).arg(failure.cell->y()).arg(failure.error);
        }
        GenerateLotsFailureDialog dialog(errorList, MainWindow::instance());
        dialog.exec();
    }

    QString stats = tr("Finished!\n\nBuildings: %1\nRooms: %2\nRoom rects: %3\nRoom objects: %4")
            .arg(mStats.numBuildings)
            .arg(mStats.numRooms)
            .arg(mStats.numRoomRects)
            .arg(mStats.numRoomObjects);
    QMessageBox::information(MainWindow::instance(),
                             tr("Generate Lot Files"), stats);
#endif
    return true;
}

bool LotFilesManager256::generateCell(WorldCell *cell)
{
    if (cell == nullptr)
        return true;

    if (cell->mapFilePath().isEmpty())
        return true;

    const int samplesPerCell = mWorldDoc->world()->geometry().chunksPerCell;
    if ((cell->x() + 1) * samplesPerCell > ZombieSpawnMap.width() ||
            (cell->y() + 1) * samplesPerCell > ZombieSpawnMap.height()) {
        mError = tr("The Zombie Spawn Map doesn't cover cell %1,%2.")
                .arg(cell->x()).arg(cell->y());
        return false;
    }

    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();

    const int sourceCellX = lotSettings.worldOrigin.x() + cell->x();
    const int sourceCellY = lotSettings.worldOrigin.y() + cell->y();
    const QRect cellBounds256 = CombinedCellMaps::outputCellRect(
            mWorldDoc->world()->gridFormat(),
            QRect(sourceCellX, sourceCellY, 1, 1));
#if 1
    for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
        for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
            CellJob cellJob;
            cellJob.cell = cell;
            cellJob.cell256X = cell256X;
            cellJob.cell256Y = cell256Y;
            mCell256Queue += cellJob;
        }
    }
#else
    for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
        if (mCancel) {
            break;
        }
        for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
            if (mCancel) {
                break;
            }
            QPair<int, int> doneCell(cell256X, cell256Y);
            if (mDoneCells256.contains(doneCell)) {
                continue;
            }
            mDoneCells256.insert(doneCell);
            if (generateCell(cell, cell256X, cell256Y) == false) {
                mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Failed);
                return false;
            }
        }
    }
#endif
    return true;
}

void LotFilesManager256::updateWorkers()
{
    if (mCancel) {
        mCell256Queue.clear();
    }
    for (int i = 0; i < mWorkers.size(); i++) {
        LotFilesWorker256 *worker = mWorkers[i];
        if (worker == nullptr) {
            continue; // thread exited
        }
        if (worker->mStatus == LotFilesWorker256::Status::Idle) {
            if (mCell256Queue.isEmpty()) {
                // if all workers are idle, we are finished
            } else {
                CellJob cellJob = mCell256Queue.front();
                mCell256Queue.pop_front();
                QPair<int, int> doneCell(cellJob.cell256X, cellJob.cell256Y);
                if (mDoneCells256.contains(doneCell)) {
                    continue;
                }
                mDoneCells256.insert(doneCell);
                if (generateCell(worker, cellJob.cell, cellJob.cell256X, cellJob.cell256Y) == false) {
                    mFailures += GenerateCellFailure(cellJob.cell, mError);
                }
            }
        }
        if (worker->mStatus == LotFilesWorker256::Status::LoadingMaps) {
            int loadStatus = worker->mCombinedCellMaps->checkLoading(mWorldDoc);
            if (loadStatus == -1) {
                mFailures += GenerateCellFailure(worker->mCell, worker->mError);
                mProgressDialog->setCellStatus(worker->mCombinedCellMaps->mCell256X - mCellBounds256.left(),
                                               worker->mCombinedCellMaps->mCell256Y - mCellBounds256.top(),
                                               ExportLotsProgressDialog::CellStatus::Failed);
                delete worker->mCombinedCellMaps;
                worker->mCombinedCellMaps = nullptr;
                worker->mStatus = LotFilesWorker256::Status::Idle;
            }
            if (loadStatus == 1) {
                if (mCancel) {
                    delete worker->mCombinedCellMaps;
                    worker->mCombinedCellMaps = nullptr;
                    worker->mStatus = LotFilesWorker256::Status::Idle;
                    continue;
                }
                qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents); // handle any pending signal-to-slot before moving threads
                worker->mCombinedCellMaps->moveToThread(worker->mCombinedCellMaps->mMapComposite, mWorkerThreads[i]);
                worker->mStatus = LotFilesWorker256::Status::Working;
                QMetaObject::invokeMethod(worker, "addJob", Qt::QueuedConnection);
            }
        }
        if (worker->mStatus == LotFilesWorker256::Status::Error) {
            mFailures += GenerateCellFailure(worker->mCell, worker->mError);
            mProgressDialog->setCellStatus(worker->mCombinedCellMaps->mCell256X - mCellBounds256.left(),
                                           worker->mCombinedCellMaps->mCell256Y - mCellBounds256.top(),
                                           ExportLotsProgressDialog::CellStatus::Failed);
            delete worker->mCombinedCellMaps;
            worker->mCombinedCellMaps = nullptr;
            worker->mStatus = LotFilesWorker256::Status::Idle;
        }
        if (worker->mStatus == LotFilesWorker256::Status::Finished) {
            if (!worker->mHoleInFloor.isEmpty()) {
                const int sampleLimit = 24;
                QStringList samples;
                const int sampleCount =
                        std::min(sampleLimit, worker->mHoleInFloor.size());
                for (int holeIndex = 0;
                     holeIndex < sampleCount;
                     ++holeIndex) {
                    const QPoint holePos =
                            worker->mHoleInFloor.at(holeIndex);
                    samples += QStringLiteral("%1,%2,0")
                            .arg(holePos.x()).arg(holePos.y());
                }
                QString message =
                        QStringLiteral("%1 square(s) contain no tile. "
                                       "First coordinate(s): %2")
                        .arg(worker->mHoleInFloor.size())
                        .arg(samples.join(QStringLiteral(", ")));
                if (worker->mHoleInFloor.size() > sampleLimit) {
                    message += QStringLiteral(" ...");
                }
                mFailures += GenerateCellFailure(
                            worker->mCell, message);
            }
            mProgressDialog->setCellStatus(worker->mCombinedCellMaps->mCell256X - mCellBounds256.left(),
                                           worker->mCombinedCellMaps->mCell256Y - mCellBounds256.top(),
                                           ExportLotsProgressDialog::CellStatus::Exported);
            delete worker->mCombinedCellMaps;
            worker->mCombinedCellMaps = nullptr;
            worker->mStatus = LotFilesWorker256::Status::Idle;
            mStats.combine(worker->mStats);
        }
    }

    if (mCell256Queue.isEmpty() == false) {
        return;
    }

    if (getBusyWorker() != nullptr) {
        return;
    }

    if (mThreadsStopped == false) {
        qDebug() << "LotFilesManager256: Stopping threads";
        stopThreads();
        mThreadsStopped = true;
        return;
    }

    bool bThreadsRunning = false;
    for (int i = 0; i < mWorkers.size(); i++) {
        LotFilesWorker256 *worker = mWorkers[i];
        if (worker != nullptr) {
            bThreadsRunning = true;
            break;
        }
    }
    if (bThreadsRunning) {
        return;
    }

    mWorkers.clear();
    mWorkerThreads.clear();

    mTimer.stop();
    mTimer.disconnect(this);

    qDebug() << "LotFilesManager256: Stopped threads";
    qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
    mDialog->setVisible(false);

    if (!mFailures.isEmpty()) {
        QStringList errorList;
        for (const GenerateCellFailure& failure : qAsConst(mFailures)) {
            errorList += QString(QStringLiteral("Cell %1,%2: %3")).arg(failure.cell->x()).arg(failure.cell->y()).arg(failure.error);
        }
        GenerateLotsFailureDialog dialog(errorList, MainWindow::instance());
        dialog.exec();
    }

    QString stats = tr("Finished!\n\nBuildings: %1\nRooms: %2\nRoom rects: %3\nRoom objects: %4")
            .arg(mStats.numBuildings)
            .arg(mStats.numRooms)
            .arg(mStats.numRoomRects)
            .arg(mStats.numRoomObjects);
    QMessageBox::information(MainWindow::instance(),
                             tr("Generate Lot Files"), stats);
}

LotFilesWorker256 *LotFilesManager256::getFirstWorkerWithStatus(LotFilesWorker256::Status status)
{
    for (int i = 0; i < mWorkers.size(); i++) {
        LotFilesWorker256 *worker = mWorkers[i];
        if (worker == nullptr) {
            continue; // thread exited
        }
        if (worker->mStatus == status) {
            return worker;
        }
    }
    return nullptr;
}

LotFilesWorker256 *LotFilesManager256::getIdleWorker()
{
    return getFirstWorkerWithStatus(LotFilesWorker256::Status::Idle);
}

LotFilesWorker256 *LotFilesManager256::getBusyWorker()
{
    for (int i = 0; i < mWorkers.size(); i++) {
        LotFilesWorker256 *worker = mWorkers[i];
        if (worker == nullptr) {
            continue; // thread exited
        }
        if (worker->mStatus != LotFilesWorker256::Status::Idle) {
            return worker;
        }
    }
    return nullptr;
}

bool LotFilesManager256::generateCell(LotFilesWorker256 *worker, WorldCell *cell, int cell256X, int cell256Y)
{
#if 1
#else
    // Busy-wait until a worker is available.
    LotFilesWorker256 *worker = nullptr;
    while ((worker = getIdleWorker()) == nullptr) {
        updateWorkers();
        qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
        Sleep::msleep(1000 / 30);
    }
    if (mCancel) {
        mError.clear();
        return false;
    }
#endif
    mProgressDialog->setPrompt(tr("Loading maps (%1,%2)").arg(cell256X).arg(cell256Y));
    CombinedCellMaps *combinedMaps = new CombinedCellMaps();
    bool ok = combinedMaps->startLoading(mWorldDoc, cell256X, cell256Y, mLotsOverlappingCellBounds);
    qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
    if ((ok == false) || (combinedMaps->mError.isEmpty() == false)) {
        mError = combinedMaps->mError;
        delete combinedMaps;
        return false;
    }
    worker->mCombinedCellMaps = combinedMaps;
    worker->mCell = cell;
    worker->mHoleInFloor.clear();
    worker->mStatus = LotFilesWorker256::Status::LoadingMaps;
    return true;
}

bool LotFilesManager256::overwriteSpawnMap(WorldDocument *worldDoc, GenerateMode mode)
{
    mWorldDoc = worldDoc;
    World *world = worldDoc->world();
    const GenerateLotsSettings &lotSettings = world->getGenerateLotsSettings();
    mCellBounds256 = CombinedCellMaps::outputCellRect(
            world->gridFormat(),
            QRect(lotSettings.worldOrigin, QSize(world->size())));

    QScopedPointer<ExportLotsProgressDialog> scoped(new ExportLotsProgressDialog(MainWindow::instance()));
    mDialog = scoped.get();
    ExportLotsProgressDialog& progress = *mDialog;
    progress.setModal(true);
    mProgressDialog = &progress;
    mProgressDialog->setWindowTitle(QLatin1String("Overwrite SpawnMap"));
//    connect(mProgressDialog, &ExportLotsProgressDialog::cancelled, this, &LotFilesManager256::cancel);
    progress.show();
    progress.activateWindow();
    progress.raise();
    qApp->processEvents(QEventLoop::ProcessEventsFlag::AllEvents);
    progress.setWorldSize(mCellBounds256.width(), mCellBounds256.height());
    progress.setPrompt(QLatin1String("Reading Zombie Spawn Map"));

    QString spawnMap = lotSettings.zombieSpawnMap;
    if (!QFileInfo::exists(spawnMap)) {
        mError = tr("Couldn't find the Zombie Spawn Map image.\n%1")
                .arg(spawnMap);
        return false;
    }
    ZombieSpawnMap = QImage(spawnMap);
    if (ZombieSpawnMap.isNull()) {
        mError = tr("Couldn't read the Zombie Spawn Map image.\n%1")
                .arg(spawnMap);
        mDialog = nullptr;
        return false;
    }

    progress.setPrompt(QLatin1String("Running..."));

    // Legacy 300x300 cells may overlap several 256x256 output cells.
    // Native 256x256 cells map directly to one output cell.
    mDoneCells256.clear();

    if (mode == GenerateMode::GenerateAll) {
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                const int sourceCellX = lotSettings.worldOrigin.x() + x;
                const int sourceCellY = lotSettings.worldOrigin.y() + y;
                const QRect cellBounds256 = CombinedCellMaps::outputCellRect(
                        world->gridFormat(),
                        QRect(sourceCellX, sourceCellY, 1, 1));
                for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
                    for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
                        mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Pending);
                    }
                }
            }
        }
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                const int sourceCellX = lotSettings.worldOrigin.x() + x;
                const int sourceCellY = lotSettings.worldOrigin.y() + y;
                if (overwriteSpawnMapSourceCell(sourceCellX, sourceCellY) == false) {
                    mDialog = nullptr;
                    return false;
                }
                qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
            }
        }
    }
    if (mode == GenerateMode::GenerateSelected) {
        for (WorldCell *cell : worldDoc->selectedCells()) {
            const int sourceCellX = lotSettings.worldOrigin.x() + cell->x();
            const int sourceCellY = lotSettings.worldOrigin.y() + cell->y();
            const QRect cellBounds256 = CombinedCellMaps::outputCellRect(
                    world->gridFormat(),
                    QRect(sourceCellX, sourceCellY, 1, 1));
            for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
                for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
                    mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Pending);
                }
            }
        }
        for (WorldCell *cell : worldDoc->selectedCells()) {
            const int sourceCellX = lotSettings.worldOrigin.x() + cell->x();
            const int sourceCellY = lotSettings.worldOrigin.y() + cell->y();
            if (overwriteSpawnMapSourceCell(sourceCellX, sourceCellY) == false) {
                mDialog = nullptr;
                return false;
            }
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    mDialog = nullptr;

    return true;
}

bool LotFilesManager256::overwriteSpawnMapSourceCell(int sourceCellX, int sourceCellY)
{
    const QRect cellBounds256 = CombinedCellMaps::outputCellRect(
            mWorldDoc->world()->gridFormat(),
            QRect(sourceCellX, sourceCellY, 1, 1));
    for (int cell256Y = cellBounds256.top(); cell256Y <= cellBounds256.bottom(); cell256Y++) {
        for (int cell256X = cellBounds256.left(); cell256X <= cellBounds256.right(); cell256X++) {
            if (mDoneCells256.contains({cell256X, cell256Y}))
                continue;
            mDoneCells256.insert({cell256X, cell256Y});
            if (overwriteSpawnMap256(cell256X, cell256Y) == false) {
                mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Failed);
                return false;
            }
        }
    }
    return true;
}

#undef CHUNKS_PER_CELL // Conflicts with IsoConstants.CHUNKS_PER_CELL
#include "chunkmap.h"
#include <QBuffer>

bool LotFilesManager256::overwriteSpawnMap256(int cell256X, int cell256Y)
{
    QString exportDir = mWorldDoc->world()->getGenerateLotsSettings().exportDir;
    QString filenameheader = QString::fromLatin1("%1/%2_%3.lotheader").arg(exportDir).arg(cell256X).arg(cell256Y);

    QFile fo(filenameheader);
    if (!fo.exists()) {
        mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Missing);
        return true;
    }
    if (!fo.open(QFile::ReadOnly)) {
        mError = fo.errorString();
        return false;
    }

    QBuffer buffer;
    buffer.open(QBuffer::ReadWrite);
    buffer.write(fo.readAll());
    fo.close();
    buffer.seek(0);

    QDataStream in(&buffer);
    in.setByteOrder(QDataStream::LittleEndian);

    char magic[4] = { 0 };
    in.readRawData(magic, 4);
    if (magic[0] == 'L' && magic[1] == 'O' && magic[2] == 'T' && magic[3] == 'H') {
        // Version 1
    } else {
        // Version 0
        buffer.seek(0);
    }

    int version = IsoLot::readInt(in);
    if (version < IsoLot::VERSION0 || version > IsoLot::VERSION_LATEST) {
        mError = tr("Unsupported .lotheader version");
        return false;
    }
    int tilecount = IsoLot::readInt(in);

    for (int n = 0; n < tilecount; ++n) {
        QString str = IsoLot::readString(in);
    }

    if (version == LotHeader::VERSION0) {
        quint8 alwaysZero = IsoLot::readByte(in);
        Q_UNUSED(alwaysZero);
    }

    IsoConstants isoConstants(true);

    int squaresPerChunkX = IsoLot::readInt(in);
    int squaresPerChunkY = IsoLot::readInt(in);
    if ((squaresPerChunkX != isoConstants.SQUARES_PER_CHUNK) || (squaresPerChunkY != isoConstants.SQUARES_PER_CHUNK)) {
        mError = tr("Unsupported .lotheader squares-per-chunk");
        return false;
    }

    int minLevel, maxLevel;
    if (version == LotHeader::VERSION0) {
        minLevel = 0;
        maxLevel = IsoLot::readInt(in) - 1; // Was always 15 but only data for levels 0-14
        Q_ASSERT(maxLevel == 14);
    } else {
        minLevel = IsoLot::readInt(in);
        maxLevel = IsoLot::readInt(in);
    }
    Q_UNUSED(minLevel)

    int numRooms = IsoLot::readInt(in);

    for (int n = 0; n < numRooms; ++n) {
        QString roomName = IsoLot::readString(in);
        int level = IsoLot::readInt(in);
        Q_UNUSED(level)
        int rects = IsoLot::readInt(in);
        for (int rc = 0; rc < rects; ++rc) {
            int x = IsoLot::readInt(in);
            int y = IsoLot::readInt(in);
            int w = IsoLot::readInt(in);
            int h = IsoLot::readInt(in);
            Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(w) Q_UNUSED(h)
        }
        int nObjects = IsoLot::readInt(in);
        for (int m = 0; m < nObjects; ++m) {
            int e = IsoLot::readInt(in);
            int x = IsoLot::readInt(in);
            int y = IsoLot::readInt(in);
            Q_UNUSED(e) Q_UNUSED(x) Q_UNUSED(y)
        }
    }

    int numBuildings = IsoLot::readInt(in);

    for (int n = 0; n < numBuildings; ++n) {
        int numbRooms = IsoLot::readInt(in);
        for (int x = 0; x < numbRooms; ++x) {
            int roomIndex = IsoLot::readInt(in);
            Q_UNUSED(roomIndex)
        }
    }

    writeZombieIntensity(in, cell256X, cell256Y);

    if (!fo.open(QFile::WriteOnly)) {
        mError = fo.errorString();
        return false;
    }
    qint64 length = buffer.pos();
    fo.write(buffer.buffer().constData(), length);
    fo.close();

    mProgressDialog->setCellStatus(cell256X - mCellBounds256.left(), cell256Y - mCellBounds256.top(), ExportLotsProgressDialog::CellStatus::Exported);

    return true;
}

#define CHUNKS_PER_CELL 30 // lotfilesmanager.h

void LotFilesManager256::writeZombieIntensity(QDataStream &out, int cell256X, int cell256Y)
{
    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();

    // Native-256 projects already have the exact 32x32 samples expected by
    // the game for each output cell.  Keep the stored raw red-channel values;
    // the B42 x40 factor is a renderer/debug-preview amplification only.
    if (mWorldDoc->world()->gridFormat() == WorldGridFormat::Native256) {
        const int imageCellX = cell256X - lotSettings.worldOrigin.x();
        const int imageCellY = cell256Y - lotSettings.worldOrigin.y();
        for (int x = 0; x < CHUNKS_PER_CELL_256; ++x) {
            for (int y = 0; y < CHUNKS_PER_CELL_256; ++y) {
                const int px = imageCellX * CHUNKS_PER_CELL_256 + x;
                const int py = imageCellY * CHUNKS_PER_CELL_256 + y;
                const quint8 intensity =
                        px >= 0 && py >= 0
                        && px < ZombieSpawnMap.width()
                        && py < ZombieSpawnMap.height()
                        ? quint8(qRed(ZombieSpawnMap.pixel(px, py)))
                        : quint8(0);
                out << intensity;
            }
        }
        return;
    }

    QRect cellBounds300 = CombinedCellMaps::toCellRect300(QRect(cell256X, cell256Y, 1, 1));

    // Set the zombie intensity on each square using the spawn image.
    const int MAX_300x300_CELLS = 3;
    quint8 ZombieIntensity[MAX_300x300_CELLS * CELL_WIDTH][MAX_300x300_CELLS * CELL_HEIGHT] = {};
    const QImage& ZombieSpawnMap = this->ZombieSpawnMap;
    QRect zombieSpawnMapBounds(lotSettings.worldOrigin.x() * CHUNKS_PER_CELL, lotSettings.worldOrigin.y() * CHUNKS_PER_CELL, ZombieSpawnMap.width(), ZombieSpawnMap.height());
    QRect combinedMapBounds(cellBounds300.x() * CHUNKS_PER_CELL, cellBounds300.y() * CHUNKS_PER_CELL, cellBounds300.width() * CHUNKS_PER_CELL, cellBounds300.height() * CHUNKS_PER_CELL);
    QRect bounds = zombieSpawnMapBounds & combinedMapBounds;
    for (int chunkY = bounds.top(); chunkY <= bounds.bottom(); chunkY++) {
        for (int chunkX = bounds.left(); chunkX <= bounds.right(); chunkX++) {
            QRgb pixel = ZombieSpawnMap.pixel(chunkX - zombieSpawnMapBounds.left(), chunkY - zombieSpawnMapBounds.top());
            quint8 chunkIntensity = qRed(pixel);
            for (int squareY = 0; squareY < CHUNK_HEIGHT; squareY++) {
                for (int squareX = 0; squareX < CHUNK_WIDTH; squareX++) {
                    int gx = (chunkX - combinedMapBounds.left()) * CHUNK_WIDTH + squareX;
                    int gy = (chunkY - combinedMapBounds.top()) * CHUNK_HEIGHT + squareY;
                    ZombieIntensity[gx][gy] = chunkIntensity;
                }
            }
        }
    }

    zombieSpawnMapBounds = QRect(lotSettings.worldOrigin.x() * CELL_WIDTH, lotSettings.worldOrigin.y() * CELL_HEIGHT, ZombieSpawnMap.width() * CHUNK_WIDTH, ZombieSpawnMap.height() * CHUNK_HEIGHT);
    combinedMapBounds = QRect(cellBounds300.x() * CELL_WIDTH, cellBounds300.y() * CELL_HEIGHT, cellBounds300.width() * CELL_WIDTH, cellBounds300.height() * CELL_HEIGHT);
    QRect combinedMapBounds256(cell256X * CELL_SIZE_256, cell256Y * CELL_SIZE_256, CELL_SIZE_256, CELL_SIZE_256);
    QRect validSquares = zombieSpawnMapBounds & combinedMapBounds256;
    QPoint p1 = combinedMapBounds256.topLeft();
    for (int x = 0; x < CHUNKS_PER_CELL_256; x++) {
        for (int y = 0; y < CHUNKS_PER_CELL_256; y++) {
            QRect chunkRect(p1.x() + x * CHUNK_SIZE_256, p1.y() + y * CHUNK_SIZE_256, CHUNK_SIZE_256, CHUNK_SIZE_256);
            chunkRect &= validSquares;
            if (chunkRect.isEmpty()) {
                out << quint8(0);
                continue;
            }
            int chunkIntensity = 0;
            for (int y3 = chunkRect.top(); y3 <= chunkRect.bottom(); y3++) {
                for (int x3 = chunkRect.left(); x3 <= chunkRect.right(); x3++) {
                    chunkIntensity += ZombieIntensity[x3 - combinedMapBounds.left()][y3 - combinedMapBounds.top()];
                }
            }
            float alpha = chunkIntensity / float(chunkRect.width() * chunkRect.height() * 255);
            out << quint8(alpha * 255);
        }
    }
}

void LotFilesManager256::workTimerTimeout()
{
    updateWorkers();
}

void LotFilesManager256::workerThreadFinished(int i)
{
    qDebug() << "LotFilesManager256: worker thread finished #" << i;
    mWorkers[i]->deleteLater();
    mWorkerThreads[i]->deleteLater();
    mWorkers[i] = nullptr;
    mWorkerThreads[i] = nullptr;
}

void LotFilesManager256::cancel()
{
    mCancel = true;
    mProgressDialog->setPrompt(tr("Stopping..."));
}

bool LotFilesWorker256::generateCell()
{
    mStats.reset();

    CombinedCellMaps& combinedMaps = *mCombinedCellMaps;
    mWorldDoc = mManager->mWorldDoc;

    int cell256X = combinedMaps.mCell256X;
    int cell256Y = combinedMaps.mCell256Y;

    MapComposite* mapComposite = combinedMaps.mMapComposite;
    MapInfo* mapInfo = mapComposite->mapInfo();

//    PROGRESS progress(tr("Generating .lot files (%1,%2)").arg(cell256X).arg(cell256Y));

    // Check for missing tilesets.
    for (MapComposite *mc : mapComposite->maps()) {
        if (mc->map()->hasUsedMissingTilesets()) {
            if (mc->mapInfo()->path().endsWith(QStringLiteral(".tbx"), Qt::CaseInsensitive)) {
                QString tilesetsHelp = QStringLiteral("Add missing tilesets to TileZed's list of tilesets.");
                mError = tr("Some tilesets are missing in a map in cell %1,%2:\n%3\n%4\n%5").arg(cell256X).arg(cell256Y).arg(mc->mapInfo()->path()).arg(tilesetsHelp).arg(missingTilesetsString(mc->map()));
            } else {
                mError = tr("Some tilesets are missing in a map in cell %1,%2:\n%3\n%4").arg(cell256X).arg(cell256Y).arg(mc->mapInfo()->path()).arg(missingTilesetsString(mc->map()));
            }
            mStatus = Status::Error;
//            qApp->processEvents(QEventLoop::ProcessEventsFlag::ExcludeUserInputEvents); // handle any pending signal-to-slot before moving threads
            mCombinedCellMaps->moveToThread(mCombinedCellMaps->mMapComposite, qApp->thread());
            return false;
        }
    }

    if (generateHeader(combinedMaps, mapComposite) == false) {
        mStatus = Status::Error;
        mCombinedCellMaps->moveToThread(mCombinedCellMaps->mMapComposite, qApp->thread());
        return false;
    }

    bool chunkDataOnly = false;
    if (chunkDataOnly) {
        for (CompositeLayerGroup *lg : mapComposite->layerGroups()) {
            lg->prepareDrawing2();
        }
        generateChunkData();
        clearRemovedBuildingsList();
        mStatus = Status::Finished;
        mCombinedCellMaps->moveToThread(mCombinedCellMaps->mMapComposite, qApp->thread());
        return true;
    }

    const int mapWidth = combinedMaps.mCellsWidth * combinedMaps.mSourceCellSize;
    const int mapHeight = combinedMaps.mCellsHeight * combinedMaps.mSourceCellSize;

    // Resize the grid and cleanup data from the previous cell.
    mGridData.resize(mapWidth);
    for (int x = 0; x < mapWidth; x++) {
        mGridData[x].resize(mapHeight);
        for (int y = 0; y < mapHeight; y++) {
            mGridData[x][y].fill(LotFile::Square(), MAX_WORLD_LEVELS);
        }
    }

    // FIXME: This is for all the 300x300 cells, not just the single 256x256 cell.
    mMinLevel = 10000;
    mMaxLevel = -10000;

    Tile *missingTile = Tiled::Internal::TilesetManager::instance()->missingTile();
    QRect cellBounds256(cell256X * CELL_SIZE_256
                                - combinedMaps.mMinSourceCellX * combinedMaps.mSourceCellSize,
                        cell256Y * CELL_SIZE_256
                                - combinedMaps.mMinSourceCellY * combinedMaps.mSourceCellSize,
                        CELL_SIZE_256, CELL_SIZE_256);
    QVector<const Tiled::Cell *> cells(40);
    OrderedCellsTemporaries vars;
    for (CompositeLayerGroup *lg : mapComposite->layerGroups()) {
        lg->prepareDrawing2();
        int d = (mapInfo->orientation() == Map::Isometric) ? -3 : 0;
        d *= lg->level();
        for (int y = d; y < mapHeight; y++) {
            for (int x = d; x < mapWidth; x++) {
                cells.resize(0);
                lg->orderedCellsAt2(QPoint(x, y), vars, cells);
                for (const Tiled::Cell *cell : std::as_const(cells)) {
                    if (cell->tile == missingTile) continue;
                    int lx = x, ly = y;
                    if (mapInfo->orientation() == Map::Isometric) {
                        lx = x + lg->level() * 3;
                        ly = y + lg->level() * 3;
                    }
                    if (lx >= mapWidth) continue;
                    if (ly >= mapHeight) continue;
                    LotFile::Entry *e = new LotFile::Entry(cellToGid(cell));
                    mGridData[lx][ly][lg->level() - MIN_WORLD_LEVEL].Entries.append(e);
                    if (cellBounds256.contains(lx, ly)) {
                        TileMap[e->gid]->used = true;
                        mMinLevel = std::min(mMinLevel, lg->level());
                        mMaxLevel = std::max(mMaxLevel, lg->level());
                    }
                }
            }
        }
    }

    checkHolesOnLevelZero();

    if (mMinLevel == 10000) {
        mMinLevel = mMaxLevel = 0;
    }

    generateBuildingObjects(mapWidth, mapHeight);

    // Native B42 maps use WorldGen + biomemap definitions for jumbo trees.
    // Keep the fake-jumbo randomizer only for legacy 300x300 projects.
    if (mWorldDoc->world()->gridFormat() != WorldGridFormat::Native256)
        generateJumboTrees(combinedMaps);

    generateHeaderAux(cell256X, cell256Y);

    /////

    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();

    QString fileName = tr("world_%1_%2.lotpack")
            .arg(cell256X)
            .arg(cell256Y);

    QString lotsDirectory = lotSettings.exportDir;
    QFile file(lotsDirectory + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::WriteOnly /*| QIODevice::Text*/)) {
        mError = tr("Could not open file for writing.");
        mStatus = Status::Error;
        mCombinedCellMaps->moveToThread(mCombinedCellMaps->mMapComposite, qApp->thread());
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out << quint8('L') << quint8('O') << quint8('T') << quint8('P');

    out << qint32(VERSION_LATEST);

    // C# 'long' is signed 64-bit integer
    out << qint32(CHUNKS_PER_CELL_256 * CHUNKS_PER_CELL_256);
    for (int m = 0; m < CHUNKS_PER_CELL_256 * CHUNKS_PER_CELL_256; m++) {
        out << qint64(m);
    }

    QList<qint64> PositionMap;

    for (int x = 0; x < CHUNKS_PER_CELL_256; x++) {
        for (int y = 0; y < CHUNKS_PER_CELL_256; y++) {
            PositionMap += file.pos();
            const int chunkX = cell256X * CELL_SIZE_256
                    - combinedMaps.mMinSourceCellX * combinedMaps.mSourceCellSize
                    + x * CHUNK_SIZE_256;
            const int chunkY = cell256Y * CELL_SIZE_256
                    - combinedMaps.mMinSourceCellY * combinedMaps.mSourceCellSize
                    + y * CHUNK_SIZE_256;
            if (generateChunk(out, chunkX, chunkY) == false) {
                mStatus = Status::Error;
                mCombinedCellMaps->moveToThread(mCombinedCellMaps->mMapComposite, qApp->thread());
                return false;
            }
        }
    }

    file.seek(4 + 4 + 4); // 'LOTS' + version + #chunks
    for (int m = 0; m < CHUNKS_PER_CELL_256 * CHUNKS_PER_CELL_256; m++) {
        out << qint64(PositionMap[m]);
    }

    file.close();

    generateChunkData();

    clearRemovedBuildingsList();

    mStatus = Status::Finished;
    mCombinedCellMaps->moveToThread(mCombinedCellMaps->mMapComposite, qApp->thread());
    return true;
}

void LotFilesWorker256::checkHolesOnLevelZero() {
    CombinedCellMaps& combinedMaps = *mCombinedCellMaps;
    MapComposite* mapComposite = combinedMaps.mMapComposite;
    CompositeLayerGroup *lg = mapComposite->layerGroupForLevel(0);
    if (lg == nullptr) {
        return;
    }
    lg->prepareDrawing2();
    const QPoint worldOrigin =
            mWorldDoc->world()->getGenerateLotsSettings().worldOrigin;
    const int boundsX =
            (mCell->x() + worldOrigin.x() - combinedMaps.mMinSourceCellX)
            * combinedMaps.mSourceCellSize;
    const int boundsY =
            (mCell->y() + worldOrigin.y() - combinedMaps.mMinSourceCellY)
            * combinedMaps.mSourceCellSize;
    QRect sourceCellBounds(boundsX, boundsY,
                           combinedMaps.mSourceCellSize,
                           combinedMaps.mSourceCellSize);
    QRect cellBounds256(
            combinedMaps.mCell256X * CELL_SIZE_256
                    - combinedMaps.mMinSourceCellX
                            * combinedMaps.mSourceCellSize,
            combinedMaps.mCell256Y * CELL_SIZE_256
                    - combinedMaps.mMinSourceCellY
                            * combinedMaps.mSourceCellSize,
                        CELL_SIZE_256, CELL_SIZE_256);
    sourceCellBounds &= cellBounds256;
    QSet<int> preparedLevels;
    QVector<const Tiled::Cell *> cells(40);
    OrderedCellsTemporaries vars;
    for (int y = sourceCellBounds.top(); y <= sourceCellBounds.bottom(); y++) {
        for (int x = sourceCellBounds.left(); x <= sourceCellBounds.right(); x++) {
            cells.resize(0);
            bool hasTile =
                    lg->orderedCellsAt2(QPoint(x, y), vars, cells);
            if (!hasTile) {
                for (int level = -1; level >= mapComposite->minLevel(); level--) {
                    if (CompositeLayerGroup* layerGroup2 = mapComposite->layerGroupForLevel(level)) {
                        if (!preparedLevels.contains(level)) {
                            layerGroup2->prepareDrawing2();
                            preparedLevels += level;
                        }
                        hasTile =
                                layerGroup2->orderedCellsAt2(
                                    QPoint(x, y), vars, cells);
                        if (hasTile) {
                            break;
                        }
                    }
                }
            }
            if (!hasTile) {
                mHoleInFloor += QPoint(x - boundsX, y - boundsY);
            }
        }
    }
}

LotFilesWorker256::LotFilesWorker256(LotFilesManager256 *manager, InterruptibleThread *thread) :
    BaseWorker(thread),
    mManager(manager),
    mWorldDoc(manager->mWorldDoc),
    mRoomRectLookup(),
    mRoomLookup()
{

}

void LotFilesWorker256::work()
{
    IN_WORKER_THREAD
    generateCell();
}

bool LotFilesWorker256::generateHeader(CombinedCellMaps& combinedMaps, MapComposite *mapComposite)
{
    qDeleteAll(mRoomRects);
    qDeleteAll(roomList);
    qDeleteAll(buildingList);

    mRoomRects.clear();
    mRoomRectByLevel.clear();
    roomList.clear();
    buildingList.clear();

    // Create the set of all tilesets used by the map and its sub-maps.
    QList<Tileset*> tilesets;
    for (MapComposite *mc : mapComposite->maps())
        tilesets += mc->map()->tilesets();

    mJumboTreeTileset = nullptr;
    if (mJumboTreeTileset == nullptr) {
        mJumboTreeTileset = new Tiled::Tileset(QLatin1String("jumbo_tree_01"), 64, 128);
        mJumboTreeTileset->loadFromNothing(QSize(64, 128), QLatin1String("jumbo_tree_01"));
    }
    tilesets += mJumboTreeTileset;
    QScopedPointer<Tiled::Tileset> scoped(mJumboTreeTileset);

    qDeleteAll(TileMap.values());
    TileMap.clear();
    TileMap[0] = new LotFile::Tile;

    mTilesetToFirstGid.clear();
    mTilesetNameToFirstGid.clear();
    uint firstGid = 1;
    for (Tileset *tileset : qAsConst(tilesets)) {
        if (!handleTileset(tileset, firstGid))
            return false;
    }

//    const GenerateLotsSettings &lotSettings = combinedMaps.mCells[0]->world()->getGenerateLotsSettings();

    if (processObjectGroups(combinedMaps, mapComposite) == false) {
        return false;
    }
#if 0
    for (WorldCell *cell : combinedMaps.mCells) {
        for (MapComposite *subMap : mapComposite->subMaps()) {
#if 1
            if (!combinedMaps.mCellMaps.contains(subMap)) {
                continue;
            }
#else
            if (subMap->origin()
                    != (cell->pos() + lotSettings.worldOrigin
                        - QPoint(combinedMaps.mMinSourceCellX,
                                 combinedMaps.mMinSourceCellY))
                            * combinedMaps.mSourceCellSize)
                continue;
#endif
            if (processObjectGroups(combinedMaps, cell, subMap) == false) {
                return false;
            }
        }
    }
#endif

    // Merge adjacent RoomRects on the same level into rooms.
    // Only RoomRects with matching names and with # in the name are merged.
    QPoint relativeToCell256(
            -(combinedMaps.mCell256X * CELL_SIZE_256
              - combinedMaps.mMinSourceCellX
                      * combinedMaps.mSourceCellSize),
            -(combinedMaps.mCell256Y * CELL_SIZE_256
              - combinedMaps.mMinSourceCellY
                      * combinedMaps.mSourceCellSize));
    for (int level : mRoomRectByLevel.keys()) {
        QList<LotFile::RoomRect*> rrList = mRoomRectByLevel[level];
        // Use spatial partitioning to speed up the code below.
        mRoomRectLookup.clear(relativeToCell256.x(), relativeToCell256.y(),
                              combinedMaps.mCellsWidth
                                      * combinedMaps.mSourceChunksPerCell,
                              combinedMaps.mCellsHeight
                                      * combinedMaps.mSourceChunksPerCell,
                              combinedMaps.mSourceChunkSize);
        for (LotFile::RoomRect *rr : rrList) {
            mRoomRectLookup.add(rr, rr->bounds());
        }
        for (LotFile::RoomRect *rr : rrList) {
            if (rr->room == nullptr) {
                rr->room = new LotFile::Room(rr->nameWithoutSuffix(), rr->floor);
                rr->room->rects += rr;
                roomList += rr->room;
            }
            if (!rr->name.contains(QLatin1Char('#')))
                continue;
            QList<LotFile::RoomRect*> rrList2;
            mRoomRectLookup.overlapping(QRect(rr->bounds().adjusted(-1, -1, 1, 1)), rrList2);
            for (LotFile::RoomRect *comp : qAsConst(rrList2)) {
                if (comp == rr)
                    continue;
#if MERGE_ROOMS_ACROSS_CELL_BOUNDARIES == 0
                // Don't merge rects across 300x300 cell boundaries, like the south wall in the Studio map.
                if (rr->mCell != comp->mCell)
                    continue;
#endif
                if (comp->room == rr->room)
                    continue;
                if (rr->inSameRoom(comp)) {
                    if (comp->room != nullptr) {
                        LotFile::Room *room = comp->room;
                        for (LotFile::RoomRect *rr2 : qAsConst(room->rects)) {
                            Q_ASSERT(rr2->room == room);
                            Q_ASSERT(!rr->room->rects.contains(rr2));
                            rr2->room = rr->room;
                        }
                        rr->room->rects += room->rects;
                        roomList.removeOne(room);
                        delete room;
                    } else {
                        comp->room = rr->room;
                        rr->room->rects += comp;
                        Q_ASSERT(rr->room->rects.count(comp) == 1);
                    }
                }
            }
        }
    }

    mRoomLookup.clear(relativeToCell256.x(), relativeToCell256.y(),
                      combinedMaps.mCellsWidth
                              * combinedMaps.mSourceChunksPerCell,
                      combinedMaps.mCellsHeight
                              * combinedMaps.mSourceChunksPerCell,
                      combinedMaps.mSourceChunkSize);
    for (LotFile::Room *r : qAsConst(roomList)) {
        r->mBounds = r->calculateBounds();
        mRoomLookup.add(r, r->bounds());
    }

    // Merge adjacent rooms into buildings.
    // Rooms on different levels that overlap in x/y are merged into the
    // same buliding.
    for (LotFile::Room *r : qAsConst(roomList)) {
        if (r->building == nullptr) {
            r->building = new LotFile::Building();
            buildingList += r->building;
            r->building->RoomList += r;
        }
        QList<LotFile::Room*> roomList2;
        mRoomLookup.overlapping(r->bounds().adjusted(-1, -1, 1, 1), roomList2);
        for (LotFile::Room *comp : qAsConst(roomList2)) {
            if (comp == r)
                continue;
#if MERGE_ROOMS_ACROSS_CELL_BOUNDARIES == 0
            // Don't merge rooms across 300x300 cell boundaries, like the south wall in the Studio map.
            if (r->mCell != comp->mCell)
                continue;
#endif
            if (r->building == comp->building)
                continue;
            if ((r->floor < 0) != (comp->floor < 0)) {
                // Don't merge below-ground buildings with above-ground buildings.
                continue;
            }
            if (r->inSameBuilding(comp)) {
                if (comp->building != nullptr) {
                    LotFile::Building *b = comp->building;
                    for (LotFile::Room *r2 : qAsConst(b->RoomList)) {
                        Q_ASSERT(r2->building == b);
                        Q_ASSERT(!r->building->RoomList.contains(r2));
                        r2->building = r->building;
                    }
                    r->building->RoomList += b->RoomList;
                    buildingList.removeOne(b);
                    delete b;
                } else {
                    comp->building = r->building;
                    r->building->RoomList += comp;
                    Q_ASSERT(r->building->RoomList.count(comp) == 1);
                }
            }
        }
    }

    // Remove buildings with their north-west corner not in the cell.
    // Buildings may extend past the east and south edges of the 256x256 cell.
    QRect cellBounds256(0, 0, CELL_SIZE_256, CELL_SIZE_256);
    for (int i = buildingList.size() - 1; i >= 0; i--) {
        LotFile::Building* building = buildingList[i];
        QRect bounds = building->calculateBounds();
        if (bounds.isEmpty()) {
            continue;
        }
        if (cellBounds256.contains(bounds.topLeft())) {
            continue;
        }
        for (LotFile::Room *room : qAsConst(building->RoomList)) {
            for (LotFile::RoomRect *roomRect : qAsConst(room->rects)) {
                mRoomRects.removeOne(roomRect);
                mRoomRectByLevel[roomRect->floor].removeOne(roomRect);
//                delete roomRect;
            }
            roomList.removeOne(room);
//            delete room;
        }
        buildingList.removeAt(i);
//        delete building;
        mRemovedBuildingList += building;
    }

    for (int i = 0; i < roomList.size(); i++) {
        roomList[i]->ID = i;
    }
    mStats.numRoomRects += mRoomRects.size();
    mStats.numRooms += roomList.size();

    mStats.numBuildings += buildingList.size();

    return true;
}

bool LotFilesWorker256::generateHeaderAux(int cell256X, int cell256Y)
{
    QString fileName = tr("%1_%2.lotheader")
            .arg(cell256X)
            .arg(cell256Y);

    QString lotsDirectory = mWorldDoc->world()->getGenerateLotsSettings().exportDir;
    QFile file(lotsDirectory + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::WriteOnly /*| QIODevice::Text*/)) {
        mError = tr("Could not open file for writing.");
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out << quint8('L') << quint8('O') << quint8('T') << quint8('H');

    out << qint32(VERSION_LATEST);

    QList<LotFile::Tile*> usedTiles;
    for (LotFile::Tile *tile : qAsConst(TileMap)) {
        if (tile->used) {
            usedTiles += tile;
            if (tile->name.startsWith(QLatin1String("jumbo_tree_01"))) {
                int nnn = 0;
                (void) nnn;
            }
        }
    }
    out << qint32(usedTiles.size());
    std::sort(usedTiles.begin(), usedTiles.end(), [](const LotFile::Tile *a, const LotFile::Tile *b) {
        return QString::compare(a->name, b->name, Qt::CaseSensitive) < 0;
    });
    for (int i = 0; i < usedTiles.size(); i++) {
        LotFile::Tile *tile = usedTiles[i];
        SaveString(out, tile->name);
        tile->id = i;
    }

    out << qint32(CHUNK_SIZE_256);
    out << qint32(CHUNK_SIZE_256);
    out << qint32(mMinLevel);
    out << qint32(mMaxLevel);

    out << qint32(roomList.count());
    for (LotFile::Room *room : qAsConst(roomList)) {
        SaveString(out, room->name);
        out << qint32(room->floor);

        out << qint32(room->rects.size());
        for (LotFile::RoomRect *rr : qAsConst(room->rects)) {
            out << qint32(rr->x);
            out << qint32(rr->y);
            out << qint32(rr->w);
            out << qint32(rr->h);
        }

        out << qint32(room->objects.size());
        for (const LotFile::RoomObject &object : qAsConst(room->objects)) {
            out << qint32(object.metaEnum);
            out << qint32(object.x);
            out << qint32(object.y);
        }
    }

    out << qint32(buildingList.count());
    for (LotFile::Building *building : qAsConst(buildingList)) {
        out << qint32(building->RoomList.count());
        for (LotFile::Room *room : qAsConst(building->RoomList)) {
            out << qint32(room->ID);
        }
    }

    // Set the zombie intensity on each square using the spawn image.
    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();
    if (mWorldDoc->world()->gridFormat() == WorldGridFormat::Native256) {
        const int imageCellX = cell256X - lotSettings.worldOrigin.x();
        const int imageCellY = cell256Y - lotSettings.worldOrigin.y();
        for (int x = 0; x < CHUNKS_PER_CELL_256; ++x) {
            for (int y = 0; y < CHUNKS_PER_CELL_256; ++y) {
                const int px = imageCellX * CHUNKS_PER_CELL_256 + x;
                const int py = imageCellY * CHUNKS_PER_CELL_256 + y;
                const quint8 intensity =
                        px >= 0 && py >= 0
                        && px < mManager->ZombieSpawnMap.width()
                        && py < mManager->ZombieSpawnMap.height()
                        ? quint8(qRed(mManager->ZombieSpawnMap.pixel(px, py)))
                        : quint8(0);
                out << intensity;
            }
        }
        file.close();
        return true;
    }

    const int MAX_300x300_CELLS = 3;
    quint8 ZombieIntensity[MAX_300x300_CELLS * CELL_WIDTH][MAX_300x300_CELLS * CELL_HEIGHT] = {};
    const QImage& ZombieSpawnMap = mManager->ZombieSpawnMap;
    QRect zombieSpawnMapBounds(lotSettings.worldOrigin.x() * CHUNKS_PER_CELL, lotSettings.worldOrigin.y() * CHUNKS_PER_CELL, ZombieSpawnMap.width(), ZombieSpawnMap.height());
    QRect combinedMapBounds(
            mCombinedCellMaps->mMinSourceCellX * CHUNKS_PER_CELL,
            mCombinedCellMaps->mMinSourceCellY * CHUNKS_PER_CELL,
            mCombinedCellMaps->mCellsWidth * CHUNKS_PER_CELL,
            mCombinedCellMaps->mCellsHeight * CHUNKS_PER_CELL);
    QRect bounds = zombieSpawnMapBounds & combinedMapBounds;
    for (int chunkY = bounds.top(); chunkY <= bounds.bottom(); chunkY++) {
        for (int chunkX = bounds.left(); chunkX <= bounds.right(); chunkX++) {
            QRgb pixel = ZombieSpawnMap.pixel(chunkX - zombieSpawnMapBounds.left(), chunkY - zombieSpawnMapBounds.top());
            quint8 chunkIntensity = qRed(pixel);
            for (int squareY = 0; squareY < CHUNK_HEIGHT; squareY++) {
                for (int squareX = 0; squareX < CHUNK_WIDTH; squareX++) {
                    int gx = (chunkX - combinedMapBounds.left()) * CHUNK_WIDTH + squareX;
                    int gy = (chunkY - combinedMapBounds.top()) * CHUNK_HEIGHT + squareY;
                    ZombieIntensity[gx][gy] = chunkIntensity;
                }
            }
        }
    }

    zombieSpawnMapBounds = QRect(lotSettings.worldOrigin.x() * CELL_WIDTH, lotSettings.worldOrigin.y() * CELL_HEIGHT, ZombieSpawnMap.width() * CHUNK_WIDTH, ZombieSpawnMap.height() * CHUNK_HEIGHT);
    combinedMapBounds = QRect(
            mCombinedCellMaps->mMinSourceCellX * CELL_WIDTH,
            mCombinedCellMaps->mMinSourceCellY * CELL_HEIGHT,
            mCombinedCellMaps->mCellsWidth * CELL_WIDTH,
            mCombinedCellMaps->mCellsHeight * CELL_HEIGHT);
    QRect combinedMapBounds256(cell256X * CELL_SIZE_256, cell256Y * CELL_SIZE_256, CELL_SIZE_256, CELL_SIZE_256);
    QRect validSquares = zombieSpawnMapBounds & combinedMapBounds256;
    QPoint p1 = combinedMapBounds256.topLeft();
    for (int x = 0; x < CHUNKS_PER_CELL_256; x++) {
        for (int y = 0; y < CHUNKS_PER_CELL_256; y++) {
            QRect chunkRect(p1.x() + x * CHUNK_SIZE_256, p1.y() + y * CHUNK_SIZE_256, CHUNK_SIZE_256, CHUNK_SIZE_256);
            chunkRect &= validSquares;
            if (chunkRect.isEmpty()) {
                out << quint8(0);
                continue;
            }
            int chunkIntensity = 0;
            for (int y3 = chunkRect.top(); y3 <= chunkRect.bottom(); y3++) {
                for (int x3 = chunkRect.left(); x3 <= chunkRect.right(); x3++) {
                    chunkIntensity += ZombieIntensity[x3 - combinedMapBounds.left()][y3 - combinedMapBounds.top()];
                }
            }
            float alpha = chunkIntensity / float(chunkRect.width() * chunkRect.height() * 255);
            out << quint8(alpha * 255);
        }
    }

    file.close();

    return true;
}

bool LotFilesWorker256::generateChunk(QDataStream &out, int chunkX, int chunkY)
{

    int notdonecount = 0;
    for (int z = mMinLevel; z <= mMaxLevel; z++) {
        for (int x = 0; x < CHUNK_SIZE_256; x++) {
            for (int y = 0; y < CHUNK_SIZE_256; y++) {
                int gx = chunkX + x;
                int gy = chunkY + y;
                const QList<LotFile::Entry*> &entries = mGridData[gx][gy][z - MIN_WORLD_LEVEL].Entries;
                if (entries.count() == 0) {
                    notdonecount++;
                    continue;
                }
                if (notdonecount > 0) {
                    out << qint32(-1);
                    out << qint32(notdonecount);
                }
                notdonecount = 0;
                out << qint32(entries.count() + 1);
                out << qint32(getRoomID(gx, gy, z));
                for (const LotFile::Entry *entry : entries) {
                    Q_ASSERT(TileMap[entry->gid]);
                    Q_ASSERT(TileMap[entry->gid]->id != -1);
                    out << qint32(TileMap[entry->gid]->id);
                }
            }
        }
    }
    if (notdonecount > 0) {
        out << qint32(-1);
        out << qint32(notdonecount);
    }
    return true;
}

void LotFilesWorker256::generateBuildingObjects(int mapWidth, int mapHeight)
{
    for (LotFile::Room *room : qAsConst(roomList)) {
        for (LotFile::RoomRect *rr : qAsConst(room->rects)) {
            generateBuildingObjects(mapWidth, mapHeight, room, rr);
        }
    }
}

void LotFilesWorker256::generateBuildingObjects(int mapWidth, int mapHeight, LotFile::Room *room, LotFile::RoomRect *rr)
{
    for (int x = rr->x; x < rr->x + rr->w; x++) {
        for (int y = rr->y; y < rr->y + rr->h; y++) {
            if (x < 0 || y < 0 || x >= mapWidth || y >= mapHeight) {
                continue;
            }
            LotFile::Square& square = mGridData[x][y][room->floor - MIN_WORLD_LEVEL];

            // Remember the room at each position in the map.
            // TODO: Remove this, it isn't used by Java code now.
            square.roomID = room->ID;

            /* Examine every tile inside the room.  If the tile's metaEnum >= 0
               then create a new RoomObject for it. */
            for (LotFile::Entry *entry : qAsConst(square.Entries)) {
                int metaEnum = TileMap[entry->gid]->metaEnum;
                if (metaEnum >= 0) {
                    LotFile::RoomObject object;
                    object.x = x;
                    object.y = y;
                    object.metaEnum = metaEnum;
                    room->objects += object;
                    ++mStats.numRoomObjects;
                }
            }
        }
    }

    // Check south of the room for doors.
    int y = rr->y + rr->h;
    if (y >= 0 && y < mapHeight) {
        for (int x = rr->x; x < rr->x + rr->w; x++) {
            if (x < 0 || x >= mapWidth) {
                continue;
            }
            LotFile::Square& square = mGridData[x][y][room->floor - MIN_WORLD_LEVEL];
            for (LotFile::Entry *entry : qAsConst(square.Entries)) {
                int metaEnum = TileMap[entry->gid]->metaEnum;
                if (metaEnum >= 0 && TileMetaInfoMgr::instance()->isEnumNorth(metaEnum)) {
                    LotFile::RoomObject object;
                    object.x = x;
                    object.y = y - 1;
                    object.metaEnum = metaEnum + 1;
                    room->objects += object;
                    ++mStats.numRoomObjects;
                }
            }
        }
    }

    // Check east of the room for doors.
    int x = rr->x + rr->w;
    if (x >= 0 && x < mapWidth) {
        for (int y = rr->y; y < rr->y + rr->h; y++) {
            if (y < 0 || y >= mapHeight) {
                continue;
            }
            LotFile::Square& square = mGridData[x][y][room->floor - MIN_WORLD_LEVEL];
            for (LotFile::Entry *entry : qAsConst(square.Entries)) {
                int metaEnum = TileMap[entry->gid]->metaEnum;
                if (metaEnum >= 0 && TileMetaInfoMgr::instance()->isEnumWest(metaEnum)) {
                    LotFile::RoomObject object;
                    object.x = x - 1;
                    object.y = y;
                    object.metaEnum = metaEnum + 1;
                    room->objects += object;
                    ++mStats.numRoomObjects;
                }
            }
        }
    }
}

void LotFilesWorker256::generateJumboTrees(CombinedCellMaps& combinedMaps)
{
    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();

    const quint8 JUMBO_ZONE = 1;
    const quint8 PREVENT_JUMBO = 2;
    const quint8 REMOVE_TREE = 3;
    const quint8 JUMBO_TREE = 4;

    QSet<QString> treeTiles;
    QSet<QString> floorVegTiles;
    for (TileDefFile *tdf : qAsConst(Navigate::IsoGridSquare256::mTileDefFiles)) {
        for (TileDefTileset *tdts : tdf->tilesets()) {
            for (TileDefTile *tdt : qAsConst(tdts->mTiles)) {
                // Get the set of all tree tiles.
                if (tdt->mProperties.contains(QLatin1String("tree")) || (tdts->mName.startsWith(QLatin1String("vegetation_trees")))) {
                    treeTiles += QString::fromLatin1("%1_%2").arg(tdts->mName).arg(tdt->id());
                }
                // Get the set of all floor + vegetation tiles.
                if (tdt->mProperties.contains(QLatin1String("solidfloor")) ||
                        tdt->mProperties.contains(QLatin1String("FloorOverlay")) ||
                        tdt->mProperties.contains(QLatin1String("vegitation"))) {
                    floorVegTiles += QString::fromLatin1("%1_%2").arg(tdts->mName).arg(tdt->id());
                }
            }
        }
    }

    quint8 grid[CELL_SIZE_256][CELL_SIZE_256];
    quint8 densityGrid[CELL_SIZE_256][CELL_SIZE_256];
    for (int y = 0; y < CELL_SIZE_256; y++) {
        for (int x = 0; x < CELL_SIZE_256; x++) {
            grid[x][y] = PREVENT_JUMBO;
            densityGrid[x][y] = 0;
        }
    }

    QHash<ObjectType*,const JumboZone*> objectTypeMap;
    for (const JumboZone* jumboZone : std::as_const(mManager->mJumboZoneList)) {
        if (ObjectType *objectType = mWorldDoc->world()->objectType(jumboZone->zoneName)) {
            objectTypeMap[objectType] = jumboZone;
        }
    }

    PropertyDef *JumboDensity = mWorldDoc->world()->propertyDefinition(QStringLiteral("JumboDensity"));

    QRect cellBounds256(combinedMaps.mCell256X * CELL_SIZE_256
                                - combinedMaps.mMinSourceCellX
                                        * combinedMaps.mSourceCellSize,
                        combinedMaps.mCell256Y * CELL_SIZE_256
                                - combinedMaps.mMinSourceCellY
                                        * combinedMaps.mSourceCellSize,
                        CELL_SIZE_256, CELL_SIZE_256);

    ClipperLib::Path zonePath;
    for (WorldCell* cell : qAsConst(combinedMaps.mCells)) {
        QPoint sourceCellPos(
                (cell->x() + lotSettings.worldOrigin.x()
                 - combinedMaps.mMinSourceCellX)
                        * combinedMaps.mSourceCellSize,
                (cell->y() + lotSettings.worldOrigin.y()
                 - combinedMaps.mMinSourceCellY)
                        * combinedMaps.mSourceCellSize);
        for (WorldCellObject *obj : cell->objects()) {
            if ((obj->level() != 0) || !objectTypeMap.contains(obj->type())) {
                continue;
            }
            if (obj->isPoint() || obj->isPolyline()) {
                continue;
            }
            zonePath.clear();
            if (obj->isPolygon()) {
                for (const auto &pt : obj->points()) {
                    zonePath << ClipperLib::IntPoint(
                            sourceCellPos.x() + pt.x * 100,
                            sourceCellPos.y() + pt.y * 100);
                }
            }
            quint8 density = objectTypeMap[obj->type()]->density;
            Property* property = JumboDensity ? obj->properties().find(JumboDensity) : nullptr;
            if (property != nullptr) {
                bool ok = false;
                int value = property->mValue.toInt(&ok);
                if (ok && (value >= 0) && (value <= 100)) {
                    density = value;
                }
            }
            int ox = sourceCellPos.x() + obj->x();
            int oy = sourceCellPos.y() + obj->y();
            int ow = obj->width();
            int oh = obj->height();
            for (int y = oy; y < oy + oh; y++) {
                for (int x = ox; x < ox + ow; x++) {
                    if ((zonePath.empty() == false)) {
                        ClipperLib::IntPoint pt(x * 100 + 50, y * 100 + 50); // center of the square
                        if (ClipperLib::PointInPolygon(pt, zonePath) == 0) {
                            continue;
                        }
                    }
                    int gx = x - cellBounds256.x();
                    int gy = y - cellBounds256.y();
                    if ((gx >= 0) && (gx < CELL_SIZE_256) && (gy >= 0) && (gy < CELL_SIZE_256)) {
                        grid[gx][gy] = JUMBO_ZONE;
                        densityGrid[gx][gy] = std::max(densityGrid[gx][gy], density);
                    }
                }
            }
        }
    }

    for (int y = 0; y < CELL_SIZE_256; y++) {
        for (int x = 0; x < CELL_SIZE_256; x++) {
            // Prevent jumbo trees near any second-story tiles
            int wx = x + cellBounds256.x();
            int wy = y + cellBounds256.y();
            if (!mGridData[wx][wy][1 - MIN_WORLD_LEVEL].Entries.isEmpty()) {
                for (int yy = y; yy <= y + 4; yy++) {
                    for (int xx = x; xx <= x + 4; xx++) {
                        if (xx >= 0 && xx < CELL_SIZE_256 && yy >= 0 && yy < CELL_SIZE_256)
                            grid[xx][yy] = PREVENT_JUMBO;
                    }
                }
            }

            // Prevent jumbo trees near non-floor, non-vegetation (fences, etc)
            const auto& entries = mGridData[wx][wy][0 - MIN_WORLD_LEVEL].Entries;
            for (LotFile::Entry *e : entries) {
                LotFile::Tile *tile = TileMap[e->gid];
                if (!floorVegTiles.contains(tile->name)) {
                    for (int yy = y - 1; yy <= y + 1; yy++) {
                        for (int xx = x - 1; xx <= x + 1; xx++) {
                            if (xx >= 0 && xx < CELL_SIZE_256 && yy >= 0 && yy < CELL_SIZE_256)
                                grid[xx][yy] = PREVENT_JUMBO;
                        }
                    }
                    break;
                }
            }
        }
    }

    // Prevent jumbo trees near north/west edges of cells
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < CELL_SIZE_256; x++) {
            grid[x][y] = PREVENT_JUMBO;
        }
    }
    for (int x = 0; x < 4; x++) {
        for (int y = 0; y < CELL_SIZE_256; y++) {
            grid[x][y] = PREVENT_JUMBO;
        }
    }

    // Get a list of all tree positions in the cell.
    QList<QPoint> allTreePos;
    for (int y = 0; y < CELL_SIZE_256; y++) {
        for (int x = 0; x < CELL_SIZE_256; x++) {
            int wx = x + cellBounds256.x();
            int wy = y + cellBounds256.y();
            const auto& entries = mGridData[wx][wy][0 - MIN_WORLD_LEVEL].Entries;
            for (LotFile::Entry *e : entries) {
                LotFile::Tile *tile = TileMap[e->gid];
                if (treeTiles.contains(tile->name) == false) {
                    continue;
                }
                allTreePos += QPoint(x, y);
                break;
            }
        }
    }

    QRandomGenerator qrand;

    while (!allTreePos.isEmpty()) {
        int r = qrand() % allTreePos.size();
        QPoint treePos = allTreePos.takeAt(r);
        quint8 g = grid[treePos.x()][treePos.y()];
        quint8 density = densityGrid[treePos.x()][treePos.y()];
        if ((g == JUMBO_ZONE) && (qrand() % 100 < density)) {
            grid[treePos.x()][treePos.y()] = JUMBO_TREE;
            // Remove all trees surrounding a jumbo tree.
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0)
                        continue;
                    int x = treePos.x() + dx;
                    int y = treePos.y() + dy;
                    if (x >= 0 && x < CELL_SIZE_256 && y >= 0 && y < CELL_SIZE_256)
                        grid[x][y] = REMOVE_TREE;
                }
            }
        }
    }

    for (int y = 0; y < CELL_SIZE_256; y++) {
        for (int x = 0; x < CELL_SIZE_256; x++) {
            int wx = x + cellBounds256.x();
            int wy = y + cellBounds256.y();
            if (grid[x][y] == JUMBO_TREE) {
                QList<LotFile::Entry*>& squareEntries = mGridData[wx][wy][0 - MIN_WORLD_LEVEL].Entries;
                for (LotFile::Entry *e : squareEntries) {
                    LotFile::Tile *tile = TileMap[e->gid];
                    if (treeTiles.contains(tile->name)) {
                        e->gid = mTilesetToFirstGid[mJumboTreeTileset];
                        TileMap[e->gid]->used = true;
                        break;
                    }
                }
            }
            if (grid[x][y] == REMOVE_TREE) {
                QList<LotFile::Entry*>& squareEntries = mGridData[wx][wy][0 - MIN_WORLD_LEVEL].Entries;
                for (int i = 0; i < squareEntries.size(); i++) {
                    LotFile::Entry *e = squareEntries[i];
                    LotFile::Tile *tile = TileMap[e->gid];
                    if (treeTiles.contains(tile->name)) {
                        squareEntries.removeAt(i);
                        break;
                    }
                }
            }
        }
    }
}

void LotFilesWorker256::generateChunkData()
{
    mRoomRectLookup.clear(0, 0, CHUNKS_PER_CELL_256, CHUNKS_PER_CELL_256, CHUNK_SIZE_256);
    for (LotFile::RoomRect *rr : mRoomRectByLevel[0]) {
        mRoomRectLookup.add(rr, rr->bounds());
    }
    for (LotFile::Building *building : qAsConst(mRemovedBuildingList)) {
        for (LotFile::Room *room : qAsConst(building->RoomList)) {
            for (LotFile::RoomRect *rr : qAsConst(room->rects)) {
                if (rr->floor == 0) {
                    mRoomRectLookup.add(rr, rr->bounds());
                }
            }
        }
    }
    const GenerateLotsSettings &lotSettings = mWorldDoc->world()->getGenerateLotsSettings();
    Navigate::ChunkDataFile256 cdf;
    cdf.fromMap(*mCombinedCellMaps, mCombinedCellMaps->mMapComposite, mRoomRectLookup, lotSettings);
}

void LotFilesWorker256::clearRemovedBuildingsList()
{
    for (LotFile::Building *building : qAsConst(mRemovedBuildingList)) {
        for (LotFile::Room *room : qAsConst(building->RoomList)) {
            for (LotFile::RoomRect *rr : qAsConst(room->rects)) {
                delete rr;
            }
            delete room;
        }
        delete building;
    }
    mRemovedBuildingList.clear();
}

bool LotFilesWorker256::handleTileset(const Tiled::Tileset *tileset, uint &firstGid)
{
    if (!tileset->fileName().isEmpty()) {
        mError = tr("Only tileset image files supported, not external tilesets");
        return false;
    }

    QString name = nameOfTileset(tileset);

    // TODO: Verify that two tilesets sharing the same name are identical
    // between maps.
#if 1
    auto it = mTilesetNameToFirstGid.find(name);
    if (it != mTilesetNameToFirstGid.end()) {
        mTilesetToFirstGid.insert(tileset, it.value());
        return true;
    }
#else
    QMap<const Tileset*,uint>::const_iterator i = mTilesetToFirstGid.begin();
    QMap<const Tileset*,uint>::const_iterator i_end = mTilesetToFirstGid.end();
    while (i != i_end) {
        QString name2 = nameOfTileset(i.key());
        if (name == name2) {
            mTilesetToFirstGid.insert(tileset, i.value());
            return true;
        }
        ++i;
    }
#endif

    for (int i = 0; i < tileset->tileCount(); ++i) {
        int localID = i;
        int ID = firstGid + localID;
        LotFile::Tile *tile = new LotFile::Tile(QStringLiteral("%1_%2").arg(name).arg(localID));
        tile->metaEnum = TileMetaInfoMgr::instance()->tileEnumValue(tileset->tileAt(i));
        TileMap[ID] = tile;
    }

    mTilesetToFirstGid.insert(tileset, firstGid);
    mTilesetNameToFirstGid.insert(name, firstGid);
    firstGid += tileset->tileCount();

    return true;
}

int LotFilesWorker256::getRoomID(int x, int y, int z)
{
    return mGridData[x][y][z - MIN_WORLD_LEVEL].roomID;
}

uint LotFilesWorker256::cellToGid(const Cell *cell)
{
    Tileset *tileset = cell->tile->tileset();

#if 1
    auto v = mTilesetToFirstGid.find(tileset);
    if (v == mTilesetToFirstGid.end()) {
        // tileset not found
        return 0;
    }
    return v.value() + cell->tile->id();
#else
    QMap<const Tileset*,uint>::const_iterator i = mTilesetToFirstGid.begin();
    QMap<const Tileset*,uint>::const_iterator i_end = mTilesetToFirstGid.end();
    while (i != i_end && i.key() != tileset)
        ++i;
    if (i == i_end) // tileset not found
        return 0;

    return i.value() + cell->tile->id();
#endif
}

bool LotFilesWorker256::processObjectGroups(CombinedCellMaps &combinedMaps, MapComposite *mapComposite)
{
    for (ObjectGroup *og : mapComposite->map()->objectGroups()) {
        if (!processObjectGroup(combinedMaps, og, mapComposite->levelRecursive(), mapComposite->originRecursive())) {
            return false;
        }
    }

    for (MapComposite *subMap : mapComposite->subMaps()) {
        if (!processObjectGroups(combinedMaps, subMap)) {
            return false;
        }
    }

    return true;
}

bool LotFilesWorker256::processObjectGroup(CombinedCellMaps &combinedMaps, ObjectGroup *objectGroup, int levelOffset, const QPoint &offset)
{
    int level = objectGroup->level();
    level += levelOffset;

    // Align with the 256x256 cell.
    QPoint offset1 = offset;
    offset1.rx() -= combinedMaps.mCell256X * CELL_SIZE_256
            - combinedMaps.mMinSourceCellX
                    * combinedMaps.mSourceCellSize;
    offset1.ry() -= combinedMaps.mCell256Y * CELL_SIZE_256
            - combinedMaps.mMinSourceCellY
                    * combinedMaps.mSourceCellSize;

    for (const MapObject *mapObject : objectGroup->objects()) {
#if 0
        if (mapObject->name().isEmpty() || mapObject->type().isEmpty())
            continue;
#endif
        if (!mapObject->width() || !mapObject->height())
            continue;

        int x = std::floor(mapObject->x());
        int y = std::floor(mapObject->y());
        int w = std::ceil(mapObject->x() + mapObject->width()) - x;
        int h = std::ceil(mapObject->y() + mapObject->height()) - y;

        QString name = mapObject->name();
        if (name.isEmpty())
            name = QLatin1String("unnamed");
        if ((level <= 0) && BuildingEditor::RoofHiding::isEmptyOutside(name)) {
            continue;
        }
        if (objectGroup->map()->orientation() == Map::Isometric) {
            x += 3 * level;
            y += 3 * level;
        }

        if (objectGroup->name().contains(QLatin1String("RoomDefs"))) {
#if 0
            if (x < 0 || y < 0 || x + w > CELL_WIDTH || y + h > CELL_HEIGHT) {
                x = qBound(0, x, CELL_WIDTH);
                y = qBound(0, y, CELL_HEIGHT);
                mError = tr("A RoomDef in cell %1,%2 overlaps cell boundaries.\nNear x,y=%3,%4")
                        .arg(cell->x()).arg(cell->y()).arg(x).arg(y);
                return false;
            }
#endif
            // Apply the MapComposite offset in the top-level map.
            x += offset1.x();
            y += offset1.y();
            LotFile::RoomRect *rr = new LotFile::RoomRect(name, x, y, level, w, h);
            mRoomRects += rr;
            mRoomRectByLevel[level] += rr;
        }
    }
    return true;
}

void LotFilesWorker256::resolveProperties(PropertyHolder *ph, PropertyList &result)
{
    for (PropertyTemplate *pt : ph->templates())
        resolveProperties(pt, result);
    for (Property *p : ph->properties()) {
        result.removeAll(p->mDefinition);
        result += p;
    }
}

QString LotFilesWorker256::missingTilesetsString(Tiled::Map *map)
{
    QStringList tilesetNames;
    QString property = map->property(MISSING_TILESETS_PROPERTY);
    if (property.isEmpty()) {
        // A .tmx
        QSet<QString> tilesetNameSet;
        for (Tiled::Tileset *tileset : map->usedTilesets()) {
            if (tileset->isMissing()) {
                tilesetNameSet += tileset->name();
            }
        }
        tilesetNames = QStringList(tilesetNameSet.cbegin(), tilesetNameSet.cend());
    } else {
        // A .tbx
        tilesetNames = property.split(MISSING_TILESETS_SEPARATOR);
    }
    if (tilesetNames.isEmpty()) {
        return QString();
    }
    QString result;
    tilesetNames.sort();
    result += QStringLiteral("    ");
    return tilesetNames.join(QStringLiteral("\n    "));
}

void LotFilesWorker256::addJob()
{
    scheduleWork();
}

//const QString LotFilesWorker256::tr(const char *str) const
//{
//    return mManager->tr(str);
//}

/////

CombinedCellMaps::CombinedCellMaps()
    : mCell256X(0)
    , mCell256Y(0)
    , mMinSourceCellX(0)
    , mMinSourceCellY(0)
    , mCellsWidth(0)
    , mCellsHeight(0)
    , mSourceCellSize(CELL_WIDTH)
    , mSourceChunksPerCell(CHUNKS_PER_CELL)
    , mSourceChunkSize(CHUNK_WIDTH)
{
}

CombinedCellMaps::~CombinedCellMaps()
{
    if (mMapComposite == nullptr) {
        return;
    }
    MapInfo* mapInfo = mMapComposite->mapInfo(); // 256x256
    delete mMapComposite;
    delete mapInfo->map();
    delete mapInfo;
}

bool CombinedCellMaps::startLoading(WorldDocument *worldDoc, int cell256X, int cell256Y, WorldCellLotList &lotsOverlappingCellBounds)
{
    World *world = worldDoc->world();
    const GenerateLotsSettings &lotSettings = world->getGenerateLotsSettings();
    const WorldGeometry geometry = world->geometry();
    mCell256X = cell256X;
    mCell256Y = cell256Y;
    mSourceCellSize = geometry.cellSize;
    mSourceChunksPerCell = geometry.chunksPerCell;
    mSourceChunkSize = geometry.chunkSize;

    const QRect sourceBounds = sourceCellRect(
            geometry.format, QRect(cell256X, cell256Y, 1, 1));
    const int minSourceCellX = sourceBounds.x();
    const int minSourceCellY = sourceBounds.y();
    const int maxSourceCellX = sourceBounds.right() + 1;
    const int maxSourceCellY = sourceBounds.bottom() + 1;
    mMinSourceCellX = minSourceCellX;
    mMinSourceCellY = minSourceCellY;
    mCellsWidth = maxSourceCellX - minSourceCellX;
    mCellsHeight = maxSourceCellY - minSourceCellY;
    mCells.clear();
    QSet<WorldCellLot*> addedLots;
    for (int sourceCellY = minSourceCellY;
         sourceCellY < maxSourceCellY; ++sourceCellY) {
        for (int sourceCellX = minSourceCellX;
             sourceCellX < maxSourceCellX; ++sourceCellX) {
            WorldCell* cell = world->cellAt(
                    sourceCellX - lotSettings.worldOrigin.x(),
                    sourceCellY - lotSettings.worldOrigin.y());
            if (cell == nullptr) {
                continue;
            }
            if (cell->mapFilePath().isEmpty()) {
                continue;
            }
            MapInfo *mapInfo = MapManager::instance()->loadMap(cell->mapFilePath(), QString(), true);
            if (mapInfo == nullptr) {
                mError = MapManager::instance()->errorString();
                return false;
            }
            mLoader.addMap(mapInfo);
            for (WorldCellLot *lot : cell->lots()) {
                if (MapInfo *info = MapManager::instance()->loadMap(lot->mapName(), QString(), true, MapManager::PriorityMedium)) {
                    mLoader.addMap(info);
                    addedLots += lot;
                    continue;
                }
                mError = MapManager::instance()->errorString();
                return false;
            }
            mCells += cell;
        }
    }

    for (WorldCellLot *lot : lotsOverlappingCellBounds) {
        if (addedLots.contains(lot)) {
            continue;
        }
        if (!lotOverlaps(lot, cell256X, cell256Y, lotSettings.worldOrigin)) {
            continue;
        }
        mLotsOverlappingCellBounds += lot;
        if (MapInfo *info = MapManager::instance()->loadMap(lot->mapName(), QString(), true, MapManager::PriorityMedium)) {
            mLoader.addMap(info);
            continue;
        }
        mError = MapManager::instance()->errorString();
        return false;

    }
    return true;
}

int CombinedCellMaps::checkLoading(WorldDocument *worldDoc)
{
    if (mLoader.isLoading()) {
        return 0;
    }
    if (mLoader.errorString().isEmpty() == false) {
        mError = mLoader.errorString();
        return -1;
    }
    World *world = worldDoc->world();
    const GenerateLotsSettings &lotSettings = world->getGenerateLotsSettings();
    MapInfo* mapInfo = getCombinedMap();
    mMapComposite = new MapComposite(mapInfo);
    for (WorldCell* cell : qAsConst(mCells)) {
        MapInfo *info = MapManager::instance()->mapInfo(cell->mapFilePath());
        if (world->geometry().directLotExport
                && (info->map()->width() != mSourceCellSize
                    || info->map()->height() != mSourceCellSize)) {
            mError = QCoreApplication::translate(
                    "CombinedCellMaps",
                    "Native-256 project cell %1,%2 must use a "
                    "256x256 TMX map, but \"%3\" is %4x%5.")
                    .arg(cell->x()).arg(cell->y())
                    .arg(cell->mapFilePath())
                    .arg(info->map()->width()).arg(info->map()->height());
            return -1;
        }
        QPoint cellPos(
                (cell->x() + lotSettings.worldOrigin.x()
                 - mMinSourceCellX) * mSourceCellSize,
                (cell->y() + lotSettings.worldOrigin.y()
                 - mMinSourceCellY) * mSourceCellSize);
        if (world->geometry().directLotExport
                && (mMinSourceCellX != mCell256X
                    || mMinSourceCellY != mCell256Y
                    || cellPos != QPoint())) {
            mError = QCoreApplication::translate(
                    "CombinedCellMaps",
                    "Native-256 cell %1,%2 would be shifted to local square "
                    "%3,%4 while generating output cell %5,%6. Generation "
                    "was stopped to prevent a misaligned LOT export.")
                    .arg(cell->x()).arg(cell->y())
                    .arg(cellPos.x()).arg(cellPos.y())
                    .arg(mCell256X).arg(mCell256Y);
            return -1;
        }
        MapComposite* subMap = mMapComposite->addMap(info, cellPos, 0);
        subMap->setCellMap(true);
        mCellMaps += subMap;
    }
    for (WorldCell* cell : qAsConst(mCells)) {
        QPoint cellPos(
                (cell->x() + lotSettings.worldOrigin.x()
                 - mMinSourceCellX) * mSourceCellSize,
                (cell->y() + lotSettings.worldOrigin.y()
                 - mMinSourceCellY) * mSourceCellSize);
        for (WorldCellLot *lot : cell->lots()) {
            MapInfo *info = MapManager::instance()->mapInfo(lot->mapName());
            mMapComposite->addMap(info, lot->pos() + cellPos, lot->level());
        }
    }
#if 1
    for (WorldCellLot *lot : qAsConst(mLotsOverlappingCellBounds)) {
        MapInfo *info = MapManager::instance()->mapInfo(lot->mapName());
        WorldCell *cell = lot->cell();
        QPoint cellPos(
                (cell->x() + lotSettings.worldOrigin.x()
                 - mMinSourceCellX) * mSourceCellSize,
                (cell->y() + lotSettings.worldOrigin.y()
                 - mMinSourceCellY) * mSourceCellSize);
        mMapComposite->addMap(info, lot->pos() + cellPos, lot->level());
    }
#endif
    mMapComposite->synch(); //
    return 1;
}

MapInfo *CombinedCellMaps::getCombinedMap()
{
    QString mapFilePath(QLatin1String("<LotFilesManagerMap>"));
    Map *map = new Map(Map::LevelIsometric,
                       mCellsWidth * mSourceCellSize,
                       mCellsHeight * mSourceCellSize, 64, 32);
    MapInfo *mapInfo = new MapInfo(map);
    mapInfo->setFilePath(mapFilePath);
    return mapInfo;
}

void CombinedCellMaps::moveToThread(MapComposite *mapComposite, QThread *thread)
{
    mapComposite->moveToThread(thread);
    for (MapComposite *subMap : mapComposite->subMaps()) {
        moveToThread(subMap, thread);
    }
}

bool CombinedCellMaps::lotOverlaps(WorldCellLot *lot, int cell256X, int cell256Y, const QPoint &worldOrigin)
{
    QRect lotBounds(
            lot->x()
                    + (worldOrigin.x() + lot->cell()->x())
                            * mSourceCellSize,
            lot->y()
                    + (worldOrigin.y() + lot->cell()->y())
                            * mSourceCellSize,
            lot->width(), lot->height());
    QRect cellBounds(cell256X * CELL_SIZE_256, cell256Y * CELL_SIZE_256, CELL_SIZE_256, CELL_SIZE_256);
    return lotBounds.intersects(cellBounds);
}

QRect CombinedCellMaps::outputCellRect(
        WorldGridFormat format, const QRect &sourceCellRect)
{
    // Native256 LOT export is deliberately one source cell to one output
    // cell. Do not route it through the legacy 300-to-256 converter.
    // Bug reported by шакалоблок.
    if (WorldGeometry::forFormat(format).directLotExport)
        return sourceCellRect;
    return toCellRect256(sourceCellRect);
}

QRect CombinedCellMaps::sourceCellRect(
        WorldGridFormat format, const QRect &outputCellRect)
{
    if (WorldGeometry::forFormat(format).directLotExport)
        return outputCellRect;
    return toCellRect300(outputCellRect);
}

QRect CombinedCellMaps::toCellRect256(const QRect &cellRect300)
{
    int minCell256X = std::floor(cellRect300.x() * CELL_WIDTH / float(CELL_SIZE_256));
    int minCell256Y = std::floor(cellRect300.y() * CELL_HEIGHT / float(CELL_SIZE_256));
    int maxCell256X = std::ceil(((cellRect300.right() + 1) * CELL_WIDTH - 1) / float(CELL_SIZE_256));
    int maxCell256Y = std::ceil(((cellRect300.bottom() + 1) * CELL_HEIGHT - 1) / float(CELL_SIZE_256));
    return QRect(minCell256X, minCell256Y, maxCell256X - minCell256X, maxCell256Y - minCell256Y);
}

QRect CombinedCellMaps::toCellRect300(const QRect &cellRect256)
{
    int minCell300X = std::floor(cellRect256.x() * CELL_SIZE_256 / float(CELL_WIDTH));
    int minCell300Y = std::floor(cellRect256.y() * CELL_SIZE_256 / float(CELL_HEIGHT));
    int maxCell300X = std::ceil(((cellRect256.right() + 1) * CELL_SIZE_256 - 1) / float(CELL_WIDTH));
    int maxCell300Y = std::ceil(((cellRect256.bottom() + 1) * CELL_SIZE_256 - 1) / float(CELL_HEIGHT));
    return QRect(minCell300X, minCell300Y, maxCell300X - minCell300X, maxCell300Y - minCell300Y);
}

bool LotFilesManager256::validateNative256Geometry(QString *error)
{
    if (error)
        error->clear();

    const WorldGeometry geometry =
            WorldGeometry::forFormat(WorldGridFormat::Native256);
    if (!geometry.directLotExport
            || geometry.cellSize != CELL_SIZE_256
            || geometry.chunksPerCell != CHUNKS_PER_CELL_256
            || geometry.chunkSize != CHUNK_SIZE_256) {
        if (error) {
            *error = QStringLiteral(
                    "Native256 geometry is not configured as direct "
                    "256/32/8 export.");
        }
        return false;
    }

    const QList<QRect> worldRects = {
        QRect(0, 0, 1, 1),
        QRect(27, 39, 8, 6),
        QRect(-5, -3, 4, 7)
    };
    for (const QRect &worldRect : worldRects) {
        const QRect outputRect = CombinedCellMaps::outputCellRect(
                WorldGridFormat::Native256, worldRect);
        if (outputRect != worldRect) {
            if (error) {
                *error = QStringLiteral(
                        "Native256 world bounds changed from "
                        "(%1,%2 %3x%4) to (%5,%6 %7x%8).")
                        .arg(worldRect.x()).arg(worldRect.y())
                        .arg(worldRect.width()).arg(worldRect.height())
                        .arg(outputRect.x()).arg(outputRect.y())
                        .arg(outputRect.width()).arg(outputRect.height());
            }
            return false;
        }

        for (int y = worldRect.top(); y <= worldRect.bottom(); ++y) {
            for (int x = worldRect.left(); x <= worldRect.right(); ++x) {
                const QRect sourceCell(x, y, 1, 1);
                const QRect outputCell = CombinedCellMaps::outputCellRect(
                        WorldGridFormat::Native256, sourceCell);
                const QRect roundTrip = CombinedCellMaps::sourceCellRect(
                        WorldGridFormat::Native256, outputCell);
                if (outputCell != sourceCell || roundTrip != sourceCell) {
                    if (error) {
                        *error = QStringLiteral(
                                "Native256 cell %1,%2 is not mapped 1:1.")
                                .arg(x).arg(y);
                    }
                    return false;
                }
            }
        }
    }

    const QList<int> origins = { 0, 27, -5 };
    const QList<int> cellOffsets = { 0, 1, 31 };
    const QList<int> localSquares = { 0, 1, 7, 8, 127, 255 };
    for (int origin : origins) {
        for (int cellOffset : cellOffsets) {
            const int expectedCell = origin + cellOffset;
            for (int localSquare : localSquares) {
                const qint64 worldSquare =
                        qint64(expectedCell) * CELL_SIZE_256 + localSquare;
                const int outputCell = int(std::floor(
                        double(worldSquare) / CELL_SIZE_256));
                const int outputLocal = int(
                        worldSquare
                        - qint64(outputCell) * CELL_SIZE_256);
                const int outputChunk = outputLocal / CHUNK_SIZE_256;
                const int squareInChunk = outputLocal % CHUNK_SIZE_256;
                if (outputCell != expectedCell
                        || outputLocal != localSquare
                        || outputChunk != localSquare / CHUNK_SIZE_256
                        || squareInChunk != localSquare % CHUNK_SIZE_256) {
                    if (error) {
                        *error = QStringLiteral(
                                "Native256 square mapping shifted for "
                                "origin %1, cell %2, local square %3.")
                                .arg(origin).arg(cellOffset).arg(localSquare);
                    }
                    return false;
                }
            }
        }
    }

    const int lotSourceCell = 27;
    const int lotLocalX = 250;
    const int lotWidth = 20;
    const int lotWorldLeft =
            lotSourceCell * CELL_SIZE_256 + lotLocalX;
    const int firstLotCell = int(std::floor(
            lotWorldLeft / double(CELL_SIZE_256)));
    const int lastLotCell = int(std::floor(
            (lotWorldLeft + lotWidth - 1) / double(CELL_SIZE_256)));
    if (firstLotCell != 27 || lastLotCell != 28) {
        if (error) {
            *error = QStringLiteral(
                    "Native256 cross-cell lot bounds are shifted.");
        }
        return false;
    }

    const QRect legacyCell(0, 0, 1, 1);
    if (CombinedCellMaps::outputCellRect(
                WorldGridFormat::Legacy300, legacyCell)
            != CombinedCellMaps::toCellRect256(legacyCell)) {
        if (error) {
            *error = QStringLiteral(
                    "Legacy300 conversion no longer uses the historical "
                    "300-to-256 path.");
        }
        return false;
    }

    return true;
}
