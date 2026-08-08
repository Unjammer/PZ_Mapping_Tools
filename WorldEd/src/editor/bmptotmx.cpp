/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
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

#include "bmptotmx.h"

#include "bmpblender.h"
#include "bmptotmxconfirmdialog.h"
#include "mainwindow.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "preferences.h"
#include "progress.h"
#include "simplefile.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "undoredo.h"
#include "unknowncolorsdialog.h"
#include "worldcell.h"
#include "worlddocument.h"
#include "world.h"

#include "map.h"
#include "mapreader.h"
#include "mapwriter.h"
#include "objectgroup.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"

#include <QCoreApplication>
#include <QDebug>
#include <QtXml/QDomDocument>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPainter>
#include <QSet>
#include <QStringList>
#include <QUndoStack>
#include <QXmlStreamWriter>

using namespace Tiled;

namespace {

int cellSizeForImage(const QSize &imageSize, int requestedCellSize)
{
    if (requestedCellSize == 256 || requestedCellSize == 300)
        return requestedCellSize;
    if (imageSize.width() % 300 == 0 && imageSize.height() % 300 == 0)
        return 300;
    if (imageSize.width() % 256 == 0 && imageSize.height() % 256 == 0)
        return 256;
    return 0;
}

struct BitmapValidationResult
{
    int unknownPixels = 0;
    int repairedPixels = 0;
    QMap<QRgb, QList<QPoint> > samples;
};

BitmapValidationResult validateBitmapColors(
        QImage &image, const QSet<QRgb> &knownColors,
        QRgb fallbackColor, bool repair, const QPoint &sourceOrigin)
{
    BitmapValidationResult result;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb color = image.pixel(x, y);
            if (knownColors.contains(color))
                continue;
            ++result.unknownPixels;
            QList<QPoint> &points = result.samples[color];
            if (points.size() < 50)
                points += sourceOrigin + QPoint(x, y);
            if (repair) {
                image.setPixel(x, y, fallbackColor);
                ++result.repairedPixels;
            }
        }
    }
    return result;
}

bool validateBitmapColorRepair(QString *error)
{
    const QRgb black = qRgb(0, 0, 0);
    const QRgb known = qRgb(12, 34, 56);
    const QRgb unknown = qRgb(90, 80, 70);
    QSet<QRgb> knownColors;
    knownColors.insert(black);
    knownColors.insert(known);

    QImage image(3, 1, QImage::Format_ARGB32);
    image.setPixel(0, 0, black);
    image.setPixel(1, 0, known);
    image.setPixel(2, 0, unknown);
    const BitmapValidationResult result = validateBitmapColors(
                image, knownColors, known, true, QPoint(100, 200));
    if (result.unknownPixels != 1 ||
            result.repairedPixels != 1 ||
            image.pixel(2, 0) != known ||
            result.samples.value(unknown) !=
                QList<QPoint>({QPoint(102, 200)})) {
        if (error)
            *error = QStringLiteral(
                "BMP color validation and repair self-test failed");
        return false;
    }
    return true;
}

}

BMPToTMX *BMPToTMX::mInstance = 0;

BMPToTMX *BMPToTMX::instance()
{
    if (!mInstance)
        mInstance = new BMPToTMX();
    return mInstance;
}

void BMPToTMX::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

BMPToTMX::BMPToTMX(QObject *parent)
    : QObject(parent)
{
}

BMPToTMX::~BMPToTMX()
{
    qDeleteAll(mRules);
    qDeleteAll(mBlends);
}

bool BMPToTMX::validateGenerationInputs(WorldDocument *worldDoc)
{
    if (!worldDoc || !worldDoc->world()) {
        mError = tr("No WorldEd project is available for BMP to TMX validation.");
        return false;
    }

    mWorldDoc = worldDoc;
    mError.clear();
    TileMetaInfoMgr::instance()->resolveTilesets();
    if (!LoadBaseXML()) {
        mError += tr("\n(while reading MapBaseXML.txt)");
        return false;
    }
    if (!LoadRules()) {
        mError += tr("\n(while reading Rules.txt)");
        return false;
    }
    if (!LoadBlends()) {
        mError += tr("\n(while reading Blends.txt)");
        return false;
    }
    if (!loadGenerationTilesets())
        return false;
    QString repairError;
    if (!validateBitmapColorRepair(&repairError)) {
        mError = repairError;
        return false;
    }
    return true;
}

bool BMPToTMX::generateWorld(WorldDocument *worldDoc, BMPToTMX::GenerateMode mode)
{
    mWorldDoc = worldDoc;
    World *world = mWorldDoc->world();

    QString tilesDirectory = TileMetaInfoMgr::instance()->tilesDirectory();
    if (tilesDirectory.isEmpty() || !QFileInfo(tilesDirectory).exists()) {
        mError = tr("The Tiles Directory could not be found.  Please set it in the Preferences.");
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
    // Generation only needs the dimensions and resolved image paths. The
    // pixels are loaded later, for the tilesets actually used by the map.
    TileMetaInfoMgr::instance()->resolveTilesets();

    const BMPToTMXSettings &settings = world->getBMPToTMXSettings();

    // Figure out which files will be overwritten and give the user a chance to
    // cancel.
    QStringList fileNames;
    int bmpIndex;
    if (mode == GenerateSelected) {
        foreach (WorldCell *cell, mWorldDoc->selectedCells()) {
            if (shouldGenerateCell(cell, bmpIndex)) {
                QString fileName = tmxNameForCell(cell, world->bmps().at(bmpIndex));
                if (settings.updateExisting)
                    fileName = cell->mapFilePath();
                if (QFileInfo(fileName).exists()) {
                    Q_ASSERT(!fileNames.contains(fileName));
                    fileNames += fileName;
                }
            }
        }
    } else {
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell *cell = world->cellAt(x, y);
                if (shouldGenerateCell(cell, bmpIndex)) {
                    QString fileName = tmxNameForCell(cell, world->bmps().at(bmpIndex));
                    if (settings.updateExisting)
                        fileName = cell->mapFilePath();
                    if (QFileInfo(fileName).exists()) {
                        Q_ASSERT(!fileNames.contains(fileName));
                        fileNames += fileName;
                    }
                }
            }
        }
    }
    if (!fileNames.isEmpty()) {
        BMPToTMXConfirmDialog dialog(fileNames, MainWindow::instance());
        if (settings.updateExisting)
            dialog.updateExisting();
        if (dialog.exec() != QDialog::Accepted)
            return true;
    }

    if (!LoadBaseXML()) {
        mError += tr("\n(while reading MapBaseXML.txt)");
        return false;
    }
    if (!LoadRules()) {
        mError += tr("\n(while reading Rules.txt)");
        return false;
    }
    if (!LoadBlends()) {
        mError += tr("\n(while reading Blends.txt)");
        return false;
    }
    if (!loadGenerationTilesets())
        return false;

    // Try to free up some memory before loading large images.
    MapManager::instance()->purgeUnreferencedMaps();

    PROGRESS progress(QLatin1String("Reading BMP images"));

    foreach (WorldBMP *bmp, world->bmps()) {
        BMPToTMXImages *images = getImages(
                    bmp->filePath(), bmp->pos(), QImage::Format_ARGB32,
                    world->cellSize());
        if (!images) {
            goto errorExit;
        }
        mImages += images;
    }

    progress.update(QLatin1String("Generating TMX files"));

    mUnknownColors.clear();
    mUnknownVegColors.clear();
    mRepairedGroundPixels = 0;
    mRepairedVegetationPixels = 0;
    mNewFiles.clear();

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

    qDeleteAll(mImages);
    mImages.clear();

    if (settings.repairUnknownColors) {
        qInfo() << "BMP to TMX color repair replaced"
                << mRepairedGroundPixels << "ground pixel(s) and"
                << mRepairedVegetationPixels
                << "vegetation pixel(s) in generated TMX bitmap data";
    }
    reportUnknownColors();

    if (world->getBMPToTMXSettings().assignMapsToWorld)
        assignMapsToCells(worldDoc, mode);

    foreach (QString path, mNewFiles)
        MapManager::instance()->newMapFileCreated(path);

    // While displaying this, the MapManager's FileSystemWatcher might see some
    // changed .tmx files, which results in the PROGRESS dialog being displayed.
    // It's a bit odd to see the PROGRESS dialog blocked behind this messagebox.
    QMessageBox::information(MainWindow::instance(),
                             tr("BMP To TMX"), tr("Finished!"));
    return true;

errorExit:
    qDeleteAll(mImages);
    mImages.clear();
    return false;
}

bool BMPToTMX::generateCell(WorldCell *cell)
{
    int bmpIndex;
    if (!shouldGenerateCell(cell, bmpIndex))
        return true;

    if (mWorldDoc->world()->getBMPToTMXSettings().updateExisting) {
        PROGRESS progress(tr("Updating TMX files (%1,%2)")
                          .arg(cell->x()).arg(cell->y()));
        return UpdateMap(cell, bmpIndex);
    }

    PROGRESS progress(tr("Generating TMX files (%1,%2)")
                      .arg(cell->x()).arg(cell->y()));

    return WriteMap(cell, bmpIndex);
}

QStringList BMPToTMX::supportedImageFormats()
{
    QStringList ret;
    foreach (QByteArray format, QImageReader::supportedImageFormats())
        ret += QString::fromLatin1(format);
    return ret;
}

BMPToTMXImages *BMPToTMX::getImages(const QString &path, const QPoint &origin,
                                    QImage::Format format, int cellSize)
{
    QFileInfo info(path);
    if (!info.exists()) {
        mError = tr("The image file can't be found.\n%1").arg(path);
        return 0;
    }

    QFileInfo infoVeg(info.absolutePath() + QLatin1Char('/')
                      + info.completeBaseName() + QLatin1String("_veg.") + info.suffix());
    if (!infoVeg.exists()) {
        mError = tr("The image_veg file can't be found.\n%1").arg(path);
        return 0;
    }

    QImage image = loadImage(
                info.canonicalFilePath(), QString(), format, cellSize);
    if (image.isNull()) {
        return 0;
    }

    QImage imageVeg = loadImage(infoVeg.canonicalFilePath(),
                                QLatin1String("_veg"), format, cellSize);
    if (imageVeg.isNull()) {
        return 0;
    }

    if (image.size() != imageVeg.size()) {
        mError = tr("The images aren't the same size.\n%1\n%2")
                .arg(info.canonicalFilePath())
                .arg(infoVeg.canonicalFilePath());
        return 0;
    }

    const int resolvedCellSize = cellSizeForImage(image.size(), cellSize);
    if (resolvedCellSize == 0) {
        mError = tr("The image dimensions are not compatible with 256 x 256 "
                    "or 300 x 300 cells.\nCurrent size: %1 x %2 pixels.")
                .arg(image.width()).arg(image.height());
        return nullptr;
    }

    BMPToTMXImages *images = new BMPToTMXImages;
    images->mBmp = image;
    images->mBmpVeg = imageVeg;
    images->mPath = info.canonicalFilePath();
    images->mCellSize = resolvedCellSize;
    images->mBounds = QRect(origin, QSize(image.width() / resolvedCellSize,
                                          image.height() / resolvedCellSize));
    return images;
}

QSize BMPToTMX::validateImages(const QString &path, int cellSize)
{
    mError.clear();
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        mError = tr("The image file can't be found.\n%1").arg(path);
        return QSize();
    }

    QFileInfo infoVeg(info.absolutePath() + QLatin1Char('/')
                      + info.completeBaseName() + QLatin1String("_veg.") + info.suffix());
    if (!infoVeg.exists() || !infoVeg.isFile()) {
        mError = tr("The vegetation image file can't be found.\n%1")
                .arg(QDir::toNativeSeparators(infoVeg.absoluteFilePath()));
        return QSize();
    }

    QImageReader image(info.canonicalFilePath());
    if (image.size().isEmpty()) {
        mError = tr("The main image couldn't be read.\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(info.canonicalFilePath()))
                .arg(image.errorString());
        return QSize();
    }

    QImageReader imageVeg(infoVeg.canonicalFilePath());
    if (imageVeg.size().isEmpty()) {
        mError = tr("The vegetation image couldn't be read.\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(infoVeg.canonicalFilePath()))
                .arg(imageVeg.errorString());
        return QSize();
    }

    if (image.size() != imageVeg.size()) {
        mError = tr("The images aren't the same size.\n%1\n%2")
                .arg(info.canonicalFilePath())
                .arg(infoVeg.canonicalFilePath());
        return QSize();
    }

    const int resolvedCellSize = cellSizeForImage(image.size(), cellSize);
    if (resolvedCellSize == 0
            || image.size().width() % resolvedCellSize
            || image.size().height() % resolvedCellSize) {
        const QString expected = cellSize == 256 || cellSize == 300
                ? tr("%1 pixels").arg(cellSize)
                : tr("either 256 or 300 pixels");
        mError = tr("The image dimensions must be divisible by %1.\n"
                    "Current size: %2 x %3 pixels.\n\n%4\n%5")
                .arg(expected)
                .arg(image.size().width()).arg(image.size().height())
                .arg(QDir::toNativeSeparators(info.canonicalFilePath()))
                .arg(QDir::toNativeSeparators(infoVeg.canonicalFilePath()));
        return QSize();
    }

    return image.size();
}

void BMPToTMX::assignMapsToCells(WorldDocument *worldDoc, BMPToTMX::GenerateMode mode)
{
    mWorldDoc = worldDoc;

    mWorldDoc->undoStack()->beginMacro(tr("Assign Maps to Cells"));

    if (mode == GenerateSelected) {
        foreach (WorldCell *cell, worldDoc->selectedCells())
            assignMapToCell(cell);
    } else {
        World *world = worldDoc->world();
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                assignMapToCell(world->cellAt(x, y));
            }
        }
    }

    mWorldDoc->undoStack()->endMacro();
}

QString BMPToTMX::defaultRulesFile() const
{
    return Preferences::instance()->appConfigPath(QLatin1String("Rules.txt"));
}

QString BMPToTMX::defaultBlendsFile() const
{
    return Preferences::instance()->appConfigPath(QLatin1String("Blends.txt"));
}

QString BMPToTMX::defaultMapBaseXMLFile() const
{
    return Preferences::instance()->appConfigPath(QLatin1String("MapBaseXML.txt"));
}

bool BMPToTMX::shouldGenerateCell(WorldCell *cell, int &bmpIndex)
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

void BMPToTMX::assignMapToCell(WorldCell *cell)
{
    // Get the top-most BMP covering the cell
    WorldBMP *bmp = 0;
    foreach (WorldBMP *bmp2, cell->world()->bmps()) {
        if (bmp2->bounds().contains(cell->pos()))
            bmp = bmp2;
    }
    if (bmp == 0)
        return;

#if 1
    QString fileName = tmxNameForCell(cell, bmp);
    if (cell->mapFilePath() != fileName)
        mWorldDoc->undoStack()->push(new SetCellMainMap(mWorldDoc, cell, fileName));
#else
    // QFileInfo::operator!= will fail if the files don't exist because it
    // uses canonicalFilePath() comparison
    QFileInfo infoCurrent(cell->mapFilePath());
    QFileInfo infoDesired(tmxNameForCell(cell, bmp));
    if (infoCurrent != infoDesired) {
        mWorldDoc->setCellMapName(cell, infoDesired.absoluteFilePath());
    }
#endif
}

QString BMPToTMX::tmxNameForCell(WorldCell *cell, WorldBMP *bmp)
{
    QString exportDir = mWorldDoc->world()->getBMPToTMXSettings().exportDir;
    QString prefix = QFileInfo(bmp->filePath()).completeBaseName();
    QPoint worldOrigin = mWorldDoc->world()->getGenerateLotsSettings().worldOrigin;
    QString filePath = exportDir + QLatin1Char('/')
            + tr("%1_%2_%3.tmx").arg(prefix).arg(worldOrigin.x() + cell->x()).arg(worldOrigin.y() + cell->y());
    return filePath;
}

void BMPToTMX::validateAndRepairBitmaps(
        QImage &ground, QImage &vegetation,
        const QString &sourcePath, const QPoint &sourceOrigin)
{
    const BMPToTMXSettings &settings =
            mWorldDoc->world()->getBMPToTMXSettings();
    if (!settings.warnUnknownColors && !settings.repairUnknownColors)
        return;

    const QRgb black = qRgb(0, 0, 0);
    QSet<QRgb> knownGround;
    QSet<QRgb> knownVegetation;
    knownGround.insert(black);
    knownVegetation.insert(black);
    for (auto iterator = mRulesByColor0.constBegin();
         iterator != mRulesByColor0.constEnd(); ++iterator) {
        knownGround.insert(iterator.key());
    }
    for (auto iterator = mRulesByColor1.constBegin();
         iterator != mRulesByColor1.constEnd(); ++iterator) {
        knownVegetation.insert(iterator.key());
    }

    QRgb groundFallback = QRgb(settings.unknownGroundFallback);
    QRgb vegetationFallback =
            QRgb(settings.unknownVegetationFallback);
    if (!knownGround.contains(groundFallback))
        groundFallback = black;
    if (!knownVegetation.contains(vegetationFallback))
        vegetationFallback = black;

    const BitmapValidationResult groundResult =
            validateBitmapColors(
                ground, knownGround, groundFallback,
                settings.repairUnknownColors, sourceOrigin);
    const BitmapValidationResult vegetationResult =
            validateBitmapColors(
                vegetation, knownVegetation, vegetationFallback,
                settings.repairUnknownColors, sourceOrigin);

    mRepairedGroundPixels += groundResult.repairedPixels;
    mRepairedVegetationPixels += vegetationResult.repairedPixels;
    if (!settings.warnUnknownColors)
        return;

    for (auto iterator = groundResult.samples.constBegin();
         iterator != groundResult.samples.constEnd(); ++iterator) {
        UnknownColor &unknown = mUnknownColors[sourcePath][iterator.key()];
        unknown.rgb = iterator.key();
        for (const QPoint &point : iterator.value()) {
            if (unknown.xy.size() >= 50)
                break;
            unknown.xy += point;
        }
    }
    for (auto iterator = vegetationResult.samples.constBegin();
         iterator != vegetationResult.samples.constEnd(); ++iterator) {
        UnknownColor &unknown =
                mUnknownVegColors[sourcePath][iterator.key()];
        unknown.rgb = iterator.key();
        for (const QPoint &point : iterator.value()) {
            if (unknown.xy.size() >= 50)
                break;
            unknown.xy += point;
        }
    }
}

void BMPToTMX::reportUnknownColors()
{
    if (!mWorldDoc->world()->getBMPToTMXSettings().warnUnknownColors)
        return;

    QList<QString> unknownColors = mUnknownColors.keys();
    QList<QString> unknownVegColors = mUnknownVegColors.keys();
    QSet<QString> imagePaths = QSet<QString>(unknownColors.begin(), unknownColors.end()) +
            QSet<QString>(unknownVegColors.begin(), unknownVegColors.end());

    const auto descriptionsFor =
            [this](const QMap<QRgb, UnknownColor> &colors) {
        QStringList descriptions;
        for (QRgb rgb : colors.keys()) {
            const QList<QPoint> &points = colors.value(rgb).xy;
            QStringList coordinates;
            const int displayed = qMin(8, points.size());
            for (int index = 0; index < displayed; ++index) {
                coordinates += tr("(%1, %2)")
                        .arg(points.at(index).x())
                        .arg(points.at(index).y());
            }
            if (points.size() > displayed) {
                coordinates += tr("+%1 more sampled pixel(s)")
                        .arg(points.size() - displayed);
            }
            const QString hex = QStringLiteral("#%1")
                    .arg(rgb & 0x00ffffff, 6, 16, QLatin1Char('0'))
                    .toUpper();
            descriptions +=
                    tr("%1 - RGB(%2, %3, %4) - sample pixels: %5")
                    .arg(hex)
                    .arg(qRed(rgb)).arg(qGreen(rgb)).arg(qBlue(rgb))
                    .arg(coordinates.join(QLatin1String(", ")));
        }
        return descriptions;
    };

    foreach (QString imagePath, imagePaths) {
        QMap<QRgb,UnknownColor> &map = mUnknownColors[imagePath];
        if (map.size()) {
            UnknownColorsDialog dialog(
                        QFileInfo(imagePath).absoluteFilePath(),
                        descriptionsFor(map), MainWindow::instance());
            dialog.exec();
        }
        QMap<QRgb,UnknownColor> &mapVeg = mUnknownVegColors[imagePath];
        if (mapVeg.size()) {
            const QFileInfo groundInfo(imagePath);
            const QString vegetationPath =
                    groundInfo.absolutePath() + QLatin1Char('/') +
                    groundInfo.completeBaseName() + QLatin1String("_veg.") +
                    groundInfo.suffix();
            UnknownColorsDialog dialog(
                        vegetationPath, descriptionsFor(mapVeg),
                        MainWindow::instance());
            dialog.exec();
        }
    }
}

QImage BMPToTMX::loadImage(const QString &path, const QString &suffix,
                           QImage::Format format, int cellSize)
{
    QImage image;
    if (!image.load(path)) {
        mError = tr("The image%1 file couldn't be loaded.\n%2\n\nThere might not be enough memory.  Try closing any open Cells or restart the application.")
                .arg(suffix).arg(QDir::toNativeSeparators(path));
        return QImage();
    }

    const int resolvedCellSize = cellSizeForImage(image.size(), cellSize);
    if (resolvedCellSize == 0
            || image.width() % resolvedCellSize
            || image.height() % resolvedCellSize) {
        mError = tr("The image%1 size is not divisible by the project cell "
                    "size (%2 pixels).")
                .arg(suffix)
                .arg(cellSize == 256 || cellSize == 300
                     ? QString::number(cellSize)
                     : tr("256 or 300"));
        return QImage();
    }

    Q_UNUSED(format)
#if 0
    // This is the fastest format for QImage::pixel() and QImage::setPixel().
    if (image.format() != format) {
        image = image.convertToFormat(format);
        if (image.isNull()) {
            mError = tr("The image%1 file couldn't be loaded.\n%2\n\nThere might not be enough memory.  Try closing any open Cells or restart the application.")
                    .arg(suffix).arg(QDir::toNativeSeparators(path));
        }
    }
#endif

    return image;
}

bool BMPToTMX::LoadBaseXML()
{
    QString path = mWorldDoc->world()->getBMPToTMXSettings().mapbaseFile;
    if (path.isEmpty())
        path = defaultMapBaseXMLFile();
    SimpleFile simple;
    if (!simple.read(path)) {
        mError = simple.errorString();
        return false;
    }

    // Some older or manually-edited projects ended up with Rules.txt in the
    // MapBaseXML setting.  "alias" and "rule" are valid Rules.txt blocks, but
    // they can never describe the TMX layer template.  Recover with the
    // portable MapBaseXML.txt instead of reporting the misleading
    // "Unknown block name 'alias'" error.
    bool looksLikeRulesFile = false;
    foreach (SimpleFileBlock block, simple.blocks) {
        if (block.name == QLatin1String("alias")
                || block.name == QLatin1String("rule")) {
            looksLikeRulesFile = true;
            break;
        }
    }
    if (looksLikeRulesFile) {
        const QString configuredPath = path;
        path = defaultMapBaseXMLFile();
        SimpleFile fallback;
        if (QFileInfo(configuredPath) == QFileInfo(path)
                || !fallback.read(path)) {
            mError = tr(
                "The MapBaseXML setting points to a Rules.txt definition "
                "(block '%1'). Choose MapBaseXML.txt for the map template "
                "and Rules.txt for terrain colors.\n%2")
                    .arg(simple.blocks.isEmpty()
                         ? QStringLiteral("rule")
                         : simple.blocks.first().name,
                         QDir::toNativeSeparators(configuredPath));
            return false;
        }
        qWarning() << "BMP to TMX: MapBaseXML setting pointed to Rules.txt;"
                   << "using portable template" << path;
        simple = fallback;
    }

    mLayers.clear();

    foreach (SimpleFileBlock block, simple.blocks) {
        if (block.name == QLatin1String("layers")) {
            foreach (SimpleFileKeyValue kv, block.values) {
                if (kv.name == QLatin1String("tile")) {
                    mLayers += LayerInfo(kv.value, LayerInfo::Tile);
                } else if (kv.name == QLatin1String("object")) {
                    mLayers += LayerInfo(kv.value, LayerInfo::Object);
                } else {
                    mError = tr("Unknown layer type '%1'.\n%2")
                            .arg(kv.name)
                            .arg(path);
                    return false;
                }
            }
        } else {
            mError = tr("Unknown block name '%1'.\n%2")
                    .arg(block.name)
                    .arg(path);
            return false;
        }
    }

    if (!mLayers.size()) {
        mError = tr("Failed to read any layers from MapBaseXML.txt");
        return false;
    }

    return true;
}

bool BMPToTMX::LoadRules()
{
    QString path = mWorldDoc->world()->getBMPToTMXSettings().rulesFile;
    if (path.isEmpty())
        path = defaultRulesFile();

    Tiled::Internal::BmpRulesFile file;
    if (!file.read(path)) {
        mError = file.errorString();
        return false;
    }

    mRuleFileName = path;

    qDeleteAll(mRules);
    mRules.clear();
    mRulesByColor0.clear();
    mRulesByColor1.clear();
    qDeleteAll(mAliases);
    mAliasByName.clear();
    mAliases = file.aliasesCopy();
    foreach (BmpAlias *alias, mAliases)
        mAliasByName[alias->name] = alias;
    foreach (BmpRule *rule, file.rules())
        AddRule(rule);

    // Verify all the listed tiles exist.
    QString tileName;
    foreach (BmpAlias *alias, mAliases) {
        foreach (tileName, alias->tiles) {
            if (getTileFromTileName(tileName) == 0) {
                goto bogusTile;
            }
        }
    }

    foreach (BmpRule *rule, mRules) {
        foreach (tileName, rule->tileChoices) {
            if (mAliasByName.contains(tileName))
                continue;
            if (!tileName.isEmpty() && getTileFromTileName(tileName) == 0) {
                goto bogusTile;
            }
        }
    }

    return true;

bogusTile:
    mError = tr("A tile listed in Rules.txt could not be found.\n");
    mError += tr("The missing tile is called '%1'.\n\n").arg(tileName);
    mError += tr("Please fix the invalid tile index or add the tileset\nif it is missing using the Tilesets dialog in TileZed.\n");
    return false;
}

bool BMPToTMX::LoadBlends()
{
    qDeleteAll(mBlends);
    mBlends.clear();

    QString path = mWorldDoc->world()->getBMPToTMXSettings().blendsFile;
    if (path.isEmpty())
        path = defaultBlendsFile();

    Tiled::Internal::BmpBlendsFile file;
    if (!file.read(path, mAliases)) {
        mError = file.errorString();
        return false;
    }

    mBlendFileName = path;

    foreach (BmpBlend *blend, file.blends()) {
        BmpBlend *blendCopy = new BmpBlend(blend);
        mBlends += blendCopy;
    }

    // Verify all the listed tiles exist.
    QString tileName;
    foreach (BmpBlend *blend, mBlends) {
        tileName = blend->blendTile;
        if (!mAliasByName.contains(tileName) && !getTileFromTileName(tileName))
            goto bogusTile;
        tileName = blend->mainTile;
        if (!mAliasByName.contains(tileName) && !getTileFromTileName(tileName))
            goto bogusTile;
        foreach (tileName, blend->ExclusionList) {
            if (!mAliasByName.contains(tileName) && !getTileFromTileName(tileName))
                goto bogusTile;
        }
        for (int i = 0; i + 1 < blend->exclude2.size(); i += 2) {
            tileName = blend->exclude2.at(i);
            if (!mAliasByName.contains(tileName)
                    && !TileMetaInfoMgr::instance()->tileset(tileName)
                    && !getTileFromTileName(tileName))
                goto bogusTile;
        }
    }

    return true;

bogusTile:
    mError = tr("A tile listed in Blends.txt could not be found.\n");
    mError += tr("The missing tile is called '%1'.\n\n").arg(tileName);
    mError += tr("Please fix the invalid tile index or add the tileset\nif it is missing using the Tilesets dialog in TileZed.\n");
    return false;
}

bool BMPToTMX::loadGenerationTilesets()
{
    QSet<Tileset *> requiredTilesets;

    const auto addConcreteTile = [&requiredTilesets, this](
            const QString &tileName) {
        if (tileName.isEmpty())
            return;
        Tile *tile = getTileFromTileName(tileName);
        if (tile && tile->tileset())
            requiredTilesets.insert(tile->tileset());
    };
    const auto addTileOrAlias = [&addConcreteTile, &requiredTilesets, this](
            const QString &tileName) {
        BmpAlias *alias = mAliasByName.value(tileName, nullptr);
        if (alias) {
            for (const QString &aliasTile : alias->tiles)
                addConcreteTile(aliasTile);
        } else if (Tileset *tileset =
                   TileMetaInfoMgr::instance()->tileset(tileName)) {
            requiredTilesets.insert(tileset);
        } else {
            addConcreteTile(tileName);
        }
    };

    for (BmpAlias *alias : mAliases) {
        if (!alias)
            continue;
        for (const QString &tileName : alias->tiles)
            addConcreteTile(tileName);
    }
    for (BmpRule *rule : mRules) {
        if (!rule)
            continue;
        for (const QString &tileName : rule->tileChoices)
            addTileOrAlias(tileName);
    }
    for (BmpBlend *blend : mBlends) {
        if (!blend)
            continue;
        addTileOrAlias(blend->mainTile);
        addTileOrAlias(blend->blendTile);
        for (const QString &tileName : blend->ExclusionList)
            addTileOrAlias(tileName);
        for (int i = 0; i + 1 < blend->exclude2.size(); i += 2)
            addTileOrAlias(blend->exclude2.at(i));
    }

    QList<Tileset *> required = requiredTilesets.values();
    TileMetaInfoMgr::instance()->loadTilesets(required);
    Tiled::Internal::TilesetManager::instance()->waitForTilesets(
                required, MainWindow::instance());

    QStringList unavailable;
    for (Tileset *tileset : required) {
        if (!tileset || tileset->isMissing() || !tileset->isLoaded())
            unavailable += tileset ? tileset->name() : tr("<unknown>");
    }
    unavailable.removeDuplicates();
    unavailable.sort(Qt::CaseInsensitive);
    if (!unavailable.isEmpty()) {
        mError = tr(
                    "BMP to TMX cannot continue because %1 tileset image(s) "
                    "required by Rules.txt or Blends.txt could not be loaded:\n\n"
                    "%2\n\n"
                    "Restore these PNG files or update the rules before "
                    "generating the TMX cells.")
                .arg(unavailable.size())
                .arg(unavailable.join(QLatin1Char('\n')));
        qCritical().noquote() << mError;
        return false;
    }

    qInfo() << "BMP to TMX generation tilesets ready:"
            << required.size() << "required by rules and blends";
    return true;
}

void BMPToTMX::AddRule(BmpRule *rule)
{
    mRules += new BmpRule(rule);
    if (rule->bitmapIndex == 0)
        mRulesByColor0[rule->color] += mRules.last();
    else
        mRulesByColor1[rule->color] += mRules.last();
}

bool BMPToTMX::WriteMap(WorldCell *cell, int bmpIndex)
{
    const int cellSize = mWorldDoc->world()->cellSize();
    Map map(Map::LevelIsometric, cellSize, cellSize, 64, 32);
    foreach (Tiled::Tileset *ts, TileMetaInfoMgr::instance()->tilesets())
        map.addTileset(ts);

    map.rbmpSettings()->setBlendsFile(mBlendFileName);
    map.rbmpSettings()->setRulesFile(mRuleFileName);

    QList<BmpAlias*> aliases;
    foreach (BmpAlias *alias, mAliases)
        aliases += new BmpAlias(alias);
    map.rbmpSettings()->setAliases(aliases);

    QList<BmpRule*> rules;
    foreach (BmpRule *rule, mRules)
        rules += new BmpRule(rule);
    map.rbmpSettings()->setRules(rules);

    QList<BmpBlend*> blends;
    foreach (BmpBlend *blend, mBlends)
        blends += new BmpBlend(blend);
    map.rbmpSettings()->setBlends(blends);

    BMPToTMXSettings settings = mWorldDoc->world()->getBMPToTMXSettings();
    settings.copyPixels = true; // obsolete
    settings.compress = true; // obsolete

    if (bmpIndex != -1) {
        MapBmp &rbmpMain = map.rbmpMain();
        MapBmp &rbmpVeg = map.rbmpVeg();

        BMPToTMXImages *images = mImages[bmpIndex];
        QImage bmp = images->mBmp;
        QImage bmpVeg = images->mBmpVeg;
        int ix = (cell->x() - images->mBounds.x()) * cellSize;
        int iy = (cell->y() - images->mBounds.y()) * cellSize;
        rbmpMain.rimage() = bmp.copy(ix, iy, cellSize, cellSize)
                .convertToFormat(QImage::Format_ARGB32);
        rbmpVeg.rimage() = bmpVeg.copy(ix, iy, cellSize, cellSize)
                .convertToFormat(QImage::Format_ARGB32);

        validateAndRepairBitmaps(
                    rbmpMain.rimage(), rbmpVeg.rimage(),
                    images->mPath, QPoint(ix, iy));
    }

    Tiled::Internal::BmpBlender blender;
    QMap<QString,TileLayer*> blendLayers;
    if (!settings.copyPixels) {
        blender.setMap(&map);
        blender.flush(QRect(0, 0, map.width(), map.height()));
        foreach (TileLayer *blendLayer, blender.tileLayers())
            blendLayers[blendLayer->name()] = blendLayer;
    }

    for (const LayerInfo& layer : mLayers) {
        int level = 0;
        MapComposite::levelForLayer(layer.mName, &level);
        QString layerName = MapComposite::layerNameWithoutPrefix(layer.mName);
        if (layer.mType == LayerInfo::Tile) {
            TileLayer *tl = new TileLayer(layerName, 0, 0,
                                          map.width(), map.height());
            tl->setLevel(level);
            map.addLayer(tl);
            if (!settings.copyPixels) {
                if (TileLayer *blendLayer = blendLayers[tl->name()])
                    tl->setCells(0, 0, blendLayer);
            }
        } else if (layer.mType == LayerInfo::Object) {
            ObjectGroup *og = new ObjectGroup(layerName, 0, 0,
                                              map.width(), map.height());
            og->setLevel(level);
            map.addLayer(og);
        }
    }

    if (!settings.copyPixels) {
        map.rbmpMain().rimage().fill(qRgb(0, 0, 0));
        map.rbmpVeg().rimage().fill(qRgb(0, 0, 0));
    }

    QString filePath = tmxNameForCell(cell, cell->world()->bmps().at(bmpIndex));
    if (!QFileInfo(filePath).exists())
        mNewFiles += filePath;

    MapWriter writer;
    MapWriter::LayerDataFormat format = MapWriter::CSV;
    if (mWorldDoc->world()->getBMPToTMXSettings().compress)
        format = MapWriter::Base64Zlib;
    writer.setLayerDataFormat(format);
    writer.setDtdEnabled(false);
    if (!writer.writeMap(&map, filePath)) {
        mError = writer.errorString();
        return false;
    }
    return true;
}

namespace {
class BMPToTMX_MapReader : public MapReader
{
protected:
    /**
     * Overridden to make sure the resolved reference is canonical.
     */
    QString resolveReference(const QString &reference, const QString &mapPath)
    {
        QString resolved = MapReader::resolveReference(reference, mapPath);
        QString canonical = QFileInfo(resolved).canonicalFilePath();

        // Make sure that we're not returning an empty string when the file is
        // not found.
        return canonical.isEmpty() ? resolved : canonical;
    }
};
}

bool BMPToTMX::UpdateMap(WorldCell *cell, int bmpIndex)
{
    QString filePath = cell->mapFilePath();
    if (filePath.isEmpty() || !QFileInfo(filePath).exists())
        return true;

    BMPToTMX_MapReader reader;
    Map *map = reader.readMap(filePath);
    if (!map) {
        mError = reader.errorString();
        return false;
    }

    MapBmp &rbmpMain = map->rbmpMain();
    MapBmp &rbmpVeg = map->rbmpVeg();

    BMPToTMXImages *images = mImages[bmpIndex];
    QImage bmp = images->mBmp;
    QImage bmpVeg = images->mBmpVeg;

    const int cellSize = mWorldDoc->world()->cellSize();
    if (map->width() != cellSize || map->height() != cellSize) {
        mError = tr("The existing TMX map is %1 x %2 tiles, but this project "
                    "uses %3 x %3 cells.\n\n%4")
                .arg(map->width()).arg(map->height()).arg(cellSize)
                .arg(QDir::toNativeSeparators(filePath));
        delete map;
        return false;
    }
    int ix = (cell->x() - images->mBounds.x()) * cellSize;
    int iy = (cell->y() - images->mBounds.y()) * cellSize;
    QImage ground = bmp.copy(ix, iy, cellSize, cellSize)
            .convertToFormat(QImage::Format_ARGB32);
    QImage vegetation = bmpVeg.copy(ix, iy, cellSize, cellSize)
            .convertToFormat(QImage::Format_ARGB32);
    validateAndRepairBitmaps(
                ground, vegetation, images->mPath, QPoint(ix, iy));
    QPainter painter(&rbmpMain.rimage());
    painter.drawImage(0, 0, ground);
    painter.end();
    QPainter painter2(&rbmpVeg.rimage());
    painter2.drawImage(0, 0, vegetation);
    painter2.end();

    MapWriter writer;
    MapWriter::LayerDataFormat format = MapWriter::CSV;
//    if (mWorldDoc->world()->getBMPToTMXSettings().compress)
        format = MapWriter::Base64Zlib;
    writer.setLayerDataFormat(format);
    writer.setDtdEnabled(false);
    if (!writer.writeMap(map, filePath)) {
        delete map;
        mError = writer.errorString();
        return false;
    }
    delete map;
    return true;
}

#include "BuildingEditor/buildingtiles.h"
Tile *BMPToTMX::getTileFromTileName(const QString &tileName)
{
    if (tileName.isEmpty())
        return 0;
    QString tilesetName;
    int tileID;
    if (BuildingEditor::BuildingTilesMgr::parseTileName(tileName, tilesetName, tileID)) {
        if (Tileset *ts = TileMetaInfoMgr::instance()->tileset(tilesetName))
            return ts->tileAt(tileID);
    }
    return 0;
}
