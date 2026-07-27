/*
 * Copyright 2014, Tim Baker <treectrl@users.sf.net>
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

#include "tmxtobmp.h"

#include "bmptotmx.h"
#include "lotfilesmanager.h"
#include "mainwindow.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "progress.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"

#include "mapobject.h"
#include "objectgroup.h"

#include <qmath.h>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QMessageBox>
#include <QPainter>

using namespace Tiled;

SINGLETON_IMPL(TMXToBMP)

TMXToBMP::TMXToBMP(QObject *parent) :
    QObject(parent)
{
}

#include "threads.h"
bool TMXToBMP::generateWorld(WorldDocument *worldDoc, TMXToBMP::GenerateMode mode)
{
    mWorldDoc = worldDoc;
    World *world = mWorldDoc->world();
    const int cellSize = world->cellSize();

    const TMXToBMPSettings &settings = world->getTMXToBMPSettings();
    mDoMain = settings.doMain;
    mDoVeg = settings.doVegetation;
    mDoBldg = settings.doBuildings;

    MapManager::instance()->purgeUnreferencedMaps();

    PROGRESS progress(QLatin1String("Setting up images"));

    QMap<QImage*,QImage::Format> originalFormat;
    QMap<QImage*,QVector<QRgb>> originalColorTable;
    const auto saveImage = [this](const QImage &image, const QString &path) {
        if (image.save(path))
            return true;
        mError = tr("The image could not be saved.\n\nFile: %1\n"
                    "Check that the destination is writable and that enough "
                    "disk space is available.")
                .arg(QDir::toNativeSeparators(path));
        return false;
    };

    foreach (WorldBMP *bmp, world->bmps()) {
        BMPToTMXImages *images = BMPToTMX::instance()->getImages(
                    bmp->filePath(), bmp->pos(), QImage::Format_ARGB32,
                    world->cellSize());
        if (!images) {
            goto errorExit;
        }
#if 1
        originalFormat[&images->mBmp] = images->mBmp.format();
        originalFormat[&images->mBmpVeg] = images->mBmpVeg.format();
        if (images->mBmp.colorCount() > 0) {
            originalColorTable[&images->mBmp] = images->mBmp.colorTable();
        }
        if (images->mBmpVeg.colorCount() > 0) {
            originalColorTable[&images->mBmpVeg] = images->mBmpVeg.colorTable();
        }

        // This is the fastest format for QImage::pixel() and QImage::setPixel().
        if (images->mBmp.format() != QImage::Format_ARGB32) {
            images->mBmp = images->mBmp.convertToFormat(QImage::Format_ARGB32);
            if (images->mBmp.isNull()) {
                mError = tr("The image%1 file couldn't be loaded.\n%2\n\nThere might not be enough memory.  Try closing any open Cells or restart the application.")
                        .arg(QLatin1String("")).arg(QDir::toNativeSeparators(bmp->filePath()));
                goto errorExit;
            }
        }
        if (images->mBmpVeg.format() != QImage::Format_ARGB32) {
            images->mBmpVeg = images->mBmpVeg.convertToFormat(QImage::Format_ARGB32);
            if (images->mBmpVeg.isNull()) {
                mError = tr("The image%1 file couldn't be loaded.\n%2\n\nThere might not be enough memory.  Try closing any open Cells or restart the application.")
                        .arg(QLatin1String("_veg")).arg(QDir::toNativeSeparators(bmp->filePath()));
                goto errorExit;
            }
        }
#endif
        mImages += images;
    }

    if (mode == GenerateSelected) {
#if 0
        // Merge the selected map BMP data with existing images
        if (mDoMain) {
            if (QFileInfo(settings.mainFile).exists()) {
                mImageMain = QImage(settings.mainFile);
                if (mImageMain.isNull() || (mImageMain.size() != world->size() * 300)) {
                    mError = tr("The existing 'main' image file could not be loaded or is the wrong size, can't merge.");
                    goto errorExit;
                }
            } else {
                mImageMain = QImage(world->size() * 300, QImage::Format_ARGB32);
            }
        }
        if (mDoVeg) {
            if (QFileInfo(settings.vegetationFile).exists()) {
                mImageVeg = QImage(settings.vegetationFile);
                if (mImageVeg.isNull() || (mImageVeg.size() != world->size() * 300)) {
                    mError = tr("The existing 'vegetation' image file could not be loaded or is the wrong size, can't merge.");
                    goto errorExit;
                }
            } else {
                mImageVeg = QImage(world->size() * 300, QImage::Format_ARGB32);
            }
        }
#endif
        if (mDoBldg) {
            if (QFileInfo(settings.buildingsFile).exists()) {
                mImageBldg = QImage(settings.buildingsFile);
                if (mImageBldg.isNull()
                        || mImageBldg.size() != world->size() * cellSize) {
                    mError = tr("The existing 'buildings' image file could not be loaded or is the wrong size, can't merge.");
                    goto errorExit;
                }
            } else {
                mImageBldg = QImage(
                            world->size() * cellSize,
                            QImage::Format_ARGB32);
                mImageBldg.fill(Qt::transparent);
            }
        }
    } else {
        // Don't bother loading the existing image since it will be replaced.
#if 0
        if (mDoMain)
            mImageMain = QImage(world->size() * 300, QImage::Format_ARGB32);
        if (mDoVeg)
            mImageVeg = QImage(world->size() * 300, QImage::Format_ARGB32);
#endif
        if (mDoBldg) {
            mImageBldg = QImage(
                        world->size() * cellSize,
                        QImage::Format_ARGB32);
            mImageBldg.fill(Qt::transparent);
        }
    }

    if (/*(mDoMain && mImageMain.isNull()) ||
            (mDoVeg && mImageVeg.isNull()) ||*/
            (mDoBldg && mImageBldg.isNull())) {
        mError = tr("Failed to create images.  There might not be enough memory.\nTry closing any open cell documents or restart the application.");
        goto errorExit;
    }

    if (mDoBldg) {
        if (!mBldgPainter.begin(&mImageBldg)) {
            mError = tr("Can't paint to the buildings image for some reason.");
            goto errorExit;
        }
        mBldgPainter.setCompositionMode(QPainter::CompositionMode_Source);
    }

    progress.update(QLatin1String("Reading maps"));

    mModifiedImages.clear();

    if (mode == GenerateSelected) {
        foreach (WorldCell *cell, worldDoc->selectedCells())
            if (!generateCell(cell))
                goto errorExit;
    } else {
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                if (!generateCell(world->cellAt(x, y)))
                    goto errorExit;
            }
        }
    }

    MapManager::instance()->purgeUnreferencedMaps();

#if 1
    foreach (BMPToTMXImages *images, mImages) {
        if (!mModifiedImages.contains(images))
            continue;
        QFileInfo info(images->mPath);
        if (mDoMain) {
            progress.update(QLatin1String("Saving ") + info.fileName());
            if (originalFormat[&images->mBmp] == images->mBmp.format()) {
                if (!saveImage(images->mBmp, images->mPath))
                    goto errorExit;
            } else {
                Qt::ImageConversionFlags conversionFlags = Qt::ThresholdDither | Qt::AvoidDither;
                if (originalColorTable.contains(&images->mBmp)) {
                    QImage image = images->mBmp.convertToFormat(originalFormat[&images->mBmp], originalColorTable[&images->mBmp], conversionFlags);
                    if (!saveImage(image, images->mPath))
                        goto errorExit;
                } else {
                    QImage image = images->mBmp.convertToFormat(originalFormat[&images->mBmp], conversionFlags);
                    if (!saveImage(image, images->mPath))
                        goto errorExit;
                }
            }
//            Sleep::sleep(2);
        }
        if (mDoVeg) {
            progress.update(QLatin1String("Saving ") + info.baseName() + QLatin1String("_veg.") + info.suffix());
            QString path = info.absolutePath() + QLatin1String("/") + info.baseName() + QLatin1String("_veg.") + info.suffix();
            if (originalFormat[&images->mBmpVeg] == images->mBmpVeg.format()) {
                if (!saveImage(images->mBmpVeg, path))
                    goto errorExit;
            } else {
                if (originalColorTable.contains(&images->mBmpVeg)) {
                    Qt::ImageConversionFlags conversionFlags = Qt::ThresholdDither | Qt::AvoidDither;
                    QImage image = images->mBmpVeg.convertToFormat(originalFormat[&images->mBmpVeg], originalColorTable[&images->mBmpVeg], conversionFlags);
                    if (!saveImage(image, path))
                        goto errorExit;
                } else {
                    Qt::ImageConversionFlags conversionFlags = Qt::ThresholdDither | Qt::AvoidDither;
                    QImage image = images->mBmpVeg.convertToFormat(originalFormat[&images->mBmpVeg], conversionFlags);
                    if (!saveImage(image, path))
                        goto errorExit;
                }
            }
//            Sleep::sleep(2);
        }
    }

#else
    if (mDoMain) {
        progress.update(QLatin1String("Saving main image"));
        mImageMain.save(settings.mainFile);
    }
    if (mDoVeg) {
        progress.update(QLatin1String("Saving vegetation image"));
        mImageVeg.save(settings.vegetationFile);
    }
#endif
    if (mDoBldg) {
        progress.update(QLatin1String("Saving buildings image"));
        mBldgPainter.end();
        if (!saveImage(mImageBldg, settings.buildingsFile))
            goto errorExit;
//        Sleep::sleep(2);
    }

    qDeleteAll(mImages);
    mImages.clear();
    if (mDoBldg) {
        mBldgPainter.end();
        mImageBldg = QImage();
    }

    // While displaying this, the MapManager's FileSystemWatcher might see some
    // changed .tmx files, which results in the PROGRESS dialog being displayed.
    // It's a bit odd to see the PROGRESS dialog blocked behind this messagebox.
    QMessageBox::information(MainWindow::instance(),
                             tr("TMX To BMP"), tr("Image export completed."));

    return true;

errorExit:
    qDeleteAll(mImages);
    mImages.clear();
    if (mDoBldg) {
        mBldgPainter.end();
        mImageBldg = QImage();
    }
    return false;
}

bool TMXToBMP::shouldGenerateCell(WorldCell *cell, int &bmpIndex)
{
    // Get the top-most BMP covering the cell
    int n = 0;
    bmpIndex = -1;
    foreach (WorldBMP *bmp, cell->world()->bmps()) {
        if (bmp->bounds().contains(cell->pos()))
            bmpIndex = n;
        n++;
    }
    if (bmpIndex == -1)
        return false;

    return true;
}

bool TMXToBMP::generateCell(WorldCell *cell)
{
    int bmpIndex;
    if (!shouldGenerateCell(cell, bmpIndex))
        return true;
    BMPToTMXImages *images = mImages[bmpIndex];
    const int cellSize = cell->world()->cellSize();
    const QPoint imageCell =
            cell->pos() - images->mBounds.topLeft();
    const QPoint imagePixel = imageCell * cellSize;

    if (cell->mapFilePath().isEmpty()) {
        if (mDoMain) {
            QPainter painter(&images->mBmp);
            painter.fillRect(QRect(imagePixel,
                                   QSize(cellSize, cellSize)), Qt::black);
            mModifiedImages.insert(images);
        }
        if (mDoVeg) {
            QPainter painter(&images->mBmpVeg);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(QRect(imagePixel,
                                   QSize(cellSize, cellSize)),
                             Qt::transparent);
            mModifiedImages.insert(images);
        }
        if (mDoBldg) {
            mBldgPainter.fillRect(
                        cell->x() * cellSize, cell->y() * cellSize,
                        cellSize, cellSize, Qt::transparent);
        }
        return true;
    }

    MapInfo *mapInfo = MapManager::instance()->loadMap(cell->mapFilePath(),
                                                       mWorldDoc->fileName());
    if (!mapInfo) {
        mError = MapManager::instance()->errorString();
        return false;
    }

    DelayedMapLoader mapLoader;
    mapLoader.addMap(mapInfo);

    while (mapLoader.isLoading()) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    if (!mapInfo->map()
            || mapInfo->map()->width() != cellSize
            || mapInfo->map()->height() != cellSize) {
        mError = tr("The TMX map for cell %1,%2 is not %3 x %3 tiles.\n\n%4")
                .arg(cell->x()).arg(cell->y()).arg(cellSize)
                .arg(QDir::toNativeSeparators(cell->mapFilePath()));
        return false;
    }

    QRgb black = qRgba(0, 0, 0, 255);
    QRgb transparent = qRgba(0, 0, 0, 0);

    if (mDoMain) {
        QPainter painter(&images->mBmp);
        painter.drawImage(imagePixel, mapInfo->map()->bmpMain().image());
        mModifiedImages.insert(images);
    }

    if (mDoVeg) {
        QPainter painter(&images->mBmpVeg);
        const QPoint p = imagePixel;
        painter.drawImage(p, mapInfo->map()->bmpVeg().image());
        for (int y = 0; y < cellSize; y++) {
            for (int x = 0; x < cellSize; x++) {
                if (images->mBmpVeg.pixel(p + QPoint(x, y)) == black)
                    images->mBmpVeg.setPixel(p + QPoint(x, y), transparent);
            }
        }
        mModifiedImages.insert(images);
    }

    bool ok = true;
    if (mDoBldg) {
        ok = doBuildings(cell, mapInfo);
    }

    return ok;
}

bool TMXToBMP::doBuildings(WorldCell *cell, MapInfo *mapInfo)
{
    DelayedMapLoader mapLoader;
    mapLoader.addMap(mapInfo);

    foreach (WorldCellLot *lot, cell->lots()) {
        if (MapInfo *info = MapManager::instance()->loadMap(lot->mapName(),
                                                            QString(), true,
                                                            MapManager::PriorityMedium)) {
            mapLoader.addMap(info);
        } else {
            mError = MapManager::instance()->errorString();
            return false;
        }
    }

    // The cell map must be loaded before creating the MapComposite, which will
    // possibly load embedded lots.
    while (mapInfo->isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    MapComposite staticMapComposite(mapInfo);
    MapComposite *mapComposite = &staticMapComposite;
    while (mapComposite->waitingForMapsToLoad() || mapLoader.isLoading())
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    if (!mapLoader.errorString().isEmpty()) {
        mError = mapLoader.errorString();
        return false;
    }

    foreach (WorldCellLot *lot, cell->lots()) {
        MapInfo *info = MapManager::instance()->mapInfo(lot->mapName());
        Q_ASSERT(info && info->map());
        mapComposite->addMap(info, lot->pos(), lot->level());
    }

    const int cellSize = cell->world()->cellSize();
    mBldgPainter.fillRect(cell->x() * cellSize,
                          cell->y() * cellSize,
                          cellSize, cellSize, Qt::transparent);

    return processObjectGroups(cell, mapComposite);
}

bool TMXToBMP::processObjectGroups(WorldCell *cell, MapComposite *mapComposite)
{
    foreach (Layer *layer, mapComposite->map()->layers()) {
        if (ObjectGroup *og = layer->asObjectGroup()) {
            if (!processObjectGroup(cell, og, mapComposite->levelRecursive(),
                                    mapComposite->originRecursive()))
                return false;
        }
    }

    foreach (MapComposite *subMap, mapComposite->subMaps())
        if (!processObjectGroups(cell, subMap))
            return false;

    return true;
}

bool TMXToBMP::processObjectGroup(WorldCell *cell, ObjectGroup *objectGroup,
                                  int levelOffset, const QPoint &offset)
{
    int level = objectGroup->level();
    level += levelOffset;

    foreach (const MapObject *mapObject, objectGroup->objects()) {
#if 0
        if (mapObject->name().isEmpty() || mapObject->type().isEmpty())
            continue;
#endif
        if (!mapObject->width() || !mapObject->height())
            continue;

        int x = qFloor(mapObject->x());
        int y = qFloor(mapObject->y());
        int w = qCeil(mapObject->x() + mapObject->width()) - x;
        int h = qCeil(mapObject->y() + mapObject->height()) - y;

        QString name = mapObject->name();
        if (name.isEmpty())
            name = QLatin1String("unnamed");

        if (objectGroup->map()->orientation() == Map::Isometric) {
            x += 3 * level;
            y += 3 * level;
        }

        // Apply the MapComposite offset in the top-level map.
        x += offset.x();
        y += offset.y();

        if (objectGroup->name().contains(QLatin1String("RoomDefs"))) {
            const int cellSize = cell->world()->cellSize();
            const QRect clipped = QRect(x, y, w, h)
                    .intersected(QRect(0, 0, cellSize, cellSize));
            if (clipped.isEmpty())
                continue;
            mBldgPainter.fillRect(
                        cell->x() * cellSize + clipped.x(),
                        cell->y() * cellSize + clipped.y(),
                        clipped.width(), clipped.height(), mBldgColor);
        }
    }
    return true;
}
