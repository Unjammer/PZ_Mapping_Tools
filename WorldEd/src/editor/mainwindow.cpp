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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "appissuenotifier.h"
#include "pztoolsbuild.h"

#include "fromtodialog.h"
#include "bmptotmx.h"
#include "bmptotmxdialog.h"
#include "celldocument.h"
#include "cellscene.h"
#include "cellview.h"
#include "clipboard.h"
#include "copypastedialog.h"
#include "defaultsfile.h"
#include "documentmanager.h"
#include "generatelotsdialog.h"
#include "gotodialog.h"
#include "layersdock.h"
#include "lootwindow.h"
#include "lotsdock.h"
#include "lotfilesmanager.h"
#include "lotfilesmanager256.h"
#include "lotpackwindow.h"
#include "luawriter.h"
#include "mapcomposite.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "mapsdock.h"
#include "newworlddialog.h"
#include "objectsdock.h"
#include "objectgroupsdialog.h"
#include "objecttypesdialog.h"
#include "otherworldsdialog.h"
#include "pngbuildingdialog.h"
#include "pngzonesdialog.h"
#include "preferences.h"
#include "preferencesdialog.h"
#include "progress.h"
#include "propertiesdock.h"
#include "pztoolsabout.h"
#include "propertydefinitionsdialog.h"
#include "propertyenumdialog.h"
#include "resizeworlddialog.h"
#include "road.h"
#include "roadsdock.h"
#include "scenetools.h"
#include "searchdock.h"
#include "streetnamesdock.h"
#include "regionsdock.h"
#include "simplefile.h"
#include "templatesdialog.h"
#include "thumbnailsettingsmgr.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "tmxtobmp.h"
#include "tmxtobmpdialog.h"
#include "toolmanager.h"
#include "undoredo.h"
#include "undodock.h"
#include "world.h"
#include "worlddocument.h"
#include "worldreader.h"
#include "worldscene.h"
#include "worldobjectvalidation.h"
#include "worldview.h"
#include "worldwriter.h"
#include "writeroomtonesdialog.h"
#include "writespawnpointsdialog.h"
#include "writeworldobjectsdialog.h"
#include "zoomable.h"
#include "biomemapgeneratordialog.h"
#include "chunkdataoverridedialog.h"
#include "osmterrainimportdialog.h"
#include "osmprojectdata.h"
#include "biomemapimageprocessor.h"
#include "biomemapitem.h"
#include "terrainimageeditordialog.h"
#include "worldgenpreviewdialog.h"
#include "tilesetcleanupdialog.h"
#include "../portablesettings.h"

#include "InGameMap/ingamemapfeaturegenerator.h"
#include "InGameMap/ingamemapdock.h"
#include "InGameMap/ingamemapimagedialog.h"
#include "InGameMap/ingamemapimagepyramidwindow.h"
#include "InGameMap/ingamemapreader.h"
#include "InGameMap/ingamemapscene.h"
#include "InGameMap/ingamemapwriter.h"
#include "InGameMap/ingamemapwriterbinary.h"
#include "InGameMap/worldmapannotationsdialog.h"

#include <quazip.h>
#include <quazipfile.h>
#include "shortcut/actionmanager.h"
#include "shortcut/shortcuteditorwidget.h"
#include "shortcut/keyboardshortcutwindow.h"

#include "layer.h"
#include "mapobject.h"
#include "maprenderer.h"
#include "objectgroup.h"
#include "tileset.h"

#include <QtCore/qmath.h>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QComboBox>
#include <QDebug>
#include <QDataStream>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QGraphicsSceneMouseEvent>
#include <QMessageBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QPointer>
#include <QPushButton>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QWidgetAction>
#include <QSpinBox>
#include <QTimer>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QToolBar>
#include <QUndoGroup>
#include <QUndoCommand>
#include <QUndoStack>
#include <QXmlStreamReader>

#include <limits>

using namespace Tiled;
using namespace Tiled::Internal;

namespace {
QPoint projectCellForPoint(const QPointF &point, int cellSize,
                           const QPoint &worldOrigin)
{
    return QPoint(qFloor(point.x() / cellSize) - worldOrigin.x(),
                  qFloor(point.y() / cellSize) - worldOrigin.y());
}

QPoint localCellForPoint(const QPoint &point, int cellSize)
{
    return QPoint(qFloor(qreal(point.x()) / cellSize),
                  qFloor(qreal(point.y()) / cellSize));
}

quint64 cellPositionKey(const QPoint &cell)
{
    return (quint64(quint32(cell.x())) << 32)
            | quint64(quint32(cell.y()));
}

QVector<StreetNameRecord> translatedStreetRecords(
        const QVector<StreetNameRecord> &records,
        const QSet<quint64> &sourceCells, const QPoint &cellOffset,
        const QPoint &worldOrigin, int cellSize)
{
    QVector<StreetNameRecord> translated = records;
    const QPointF squareOffset(cellOffset.x() * cellSize,
                               cellOffset.y() * cellSize);
    for (StreetNameRecord &record : translated) {
        for (QPointF &point : record.points) {
            if (sourceCells.contains(cellPositionKey(projectCellForPoint(
                                         point, cellSize, worldOrigin))))
                point += squareOffset;
        }
    }
    return translated;
}

QVector<RegionRecord> translatedRegionRecords(
        const QVector<RegionRecord> &records,
        const QSet<quint64> &sourceCells, const QPoint &cellOffset,
        const QPoint &worldOrigin, int cellSize)
{
    QVector<RegionRecord> translated = records;
    const QPoint squareOffset(cellOffset.x() * cellSize,
                              cellOffset.y() * cellSize);
    for (RegionRecord &record : translated) {
        if (sourceCells.contains(cellPositionKey(projectCellForPoint(
                                     QPointF(record.x, record.y),
                                     cellSize, worldOrigin)))) {
            record.x += squareOffset.x();
            record.y += squareOffset.y();
        }
    }
    return translated;
}

class MoveExternalCoordinatesCommand : public QUndoCommand
{
public:
    MoveExternalCoordinatesCommand(
            StreetNamesDock *streetsDock,
            const QVector<StreetNameRecord> &streetsBefore,
            const QVector<StreetNameRecord> &streetsAfter,
            int selectedStreet,
            RegionsDock *regionsDock,
            const QVector<RegionRecord> &regionsBefore,
            const QVector<RegionRecord> &regionsAfter,
            int selectedRegion)
        : QUndoCommand(QObject::tr("Move Cell Coordinate Data"))
        , mStreetsDock(streetsDock)
        , mStreetsBefore(streetsBefore)
        , mStreetsAfter(streetsAfter)
        , mSelectedStreet(selectedStreet)
        , mRegionsDock(regionsDock)
        , mRegionsBefore(regionsBefore)
        , mRegionsAfter(regionsAfter)
        , mSelectedRegion(selectedRegion)
    {
    }

    void undo() override
    {
        if (mStreetsDock)
            mStreetsDock->applySnapshot(mStreetsBefore, mSelectedStreet);
        if (mRegionsDock)
            mRegionsDock->applySnapshot(mRegionsBefore, mSelectedRegion);
    }

    void redo() override
    {
        if (mStreetsDock)
            mStreetsDock->applySnapshot(mStreetsAfter, mSelectedStreet);
        if (mRegionsDock)
            mRegionsDock->applySnapshot(mRegionsAfter, mSelectedRegion);
    }

private:
    QPointer<StreetNamesDock> mStreetsDock;
    QVector<StreetNameRecord> mStreetsBefore;
    QVector<StreetNameRecord> mStreetsAfter;
    int mSelectedStreet;
    QPointer<RegionsDock> mRegionsDock;
    QVector<RegionRecord> mRegionsBefore;
    QVector<RegionRecord> mRegionsAfter;
    int mSelectedRegion;
};

bool moveFile(const QString &source, const QString &destination,
              QString *error)
{
    QFile file(source);
    if (file.rename(destination))
        return true;
    if (error) {
        *error = QObject::tr("Could not rename:\n%1\n\nto:\n%2\n\n%3")
                .arg(QDir::toNativeSeparators(source),
                     QDir::toNativeSeparators(destination),
                     file.errorString());
    }
    return false;
}
bool commitFilePair(const QStringList &temporaryFiles,
                    const QStringList &destinationFiles,
                    QString *error)
{
    Q_ASSERT(temporaryFiles.size() == destinationFiles.size());
    QStringList backupFiles;
    QVector<bool> hadDestination;
    for (const QString &destination : destinationFiles) {
        const QString backup =
                destination + QLatin1String(".pzworlded-pair-backup");
        if (QFileInfo::exists(backup)) {
            if (error) {
                *error = QObject::tr(
                            "A recovery backup from an earlier export still "
                            "exists and was not overwritten:\n%1\n\n"
                            "Inspect or restore that file, then rename or "
                            "remove it before exporting again.")
                        .arg(QDir::toNativeSeparators(backup));
            }
            return false;
        }
        backupFiles += backup;
        hadDestination += QFileInfo::exists(destination);
    }
    int backedUp = 0;
    for (; backedUp < destinationFiles.size(); ++backedUp) {
        if (!hadDestination.at(backedUp))
            continue;
        if (!moveFile(destinationFiles.at(backedUp),
                      backupFiles.at(backedUp), error)) {
            for (int index = backedUp - 1; index >= 0; --index) {
                if (hadDestination.at(index))
                    moveFile(backupFiles.at(index),
                             destinationFiles.at(index), nullptr);
            }
            return false;
        }
    }
    int committed = 0;
    for (; committed < temporaryFiles.size(); ++committed) {
        if (!moveFile(temporaryFiles.at(committed),
                      destinationFiles.at(committed), error)) {
            for (int index = 0; index < committed; ++index)
                QFile::remove(destinationFiles.at(index));
            for (int index = destinationFiles.size() - 1;
                 index >= 0; --index) {
                if (hadDestination.at(index))
                    moveFile(backupFiles.at(index),
                             destinationFiles.at(index), nullptr);
            }
            return false;
        }
    }
    for (int index = 0; index < backupFiles.size(); ++index) {
        if (hadDestination.at(index)
                && !QFile::remove(backupFiles.at(index))) {
            qWarning() << "Could not remove completed export backup:"
                       << backupFiles.at(index);
        }
    }
    return true;
}
bool validateInGameMapBinaryFile(const QString &fileName, QString *error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.read(4) != QByteArrayLiteral("IGMB")) {
        if (error)
            *error = QObject::tr("The binary header does not start with IGMB.");
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    qint32 version = 0;
    qint32 cellSize = 0;
    qint32 width = 0;
    qint32 height = 0;
    stream >> version >> cellSize >> width >> height;
    if (stream.status() != QDataStream::Ok) {
        if (error)
            *error = QObject::tr("The binary header is incomplete.");
        return false;
    }
    if (version != 2) {
        if (error)
            *error = QObject::tr("The binary version is %1 instead of 2.")
                    .arg(version);
        return false;
    }
    if (cellSize != 256) {
        if (error)
            *error = QObject::tr("The binary cell size is %1 instead of 256.")
                    .arg(cellSize);
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (error)
            *error = QObject::tr("The binary world dimensions are invalid.");
        return false;
    }
    return true;
}
bool writeInGameMapFilePair(
        World *world, const QString &xmlFileName,
        QString *error,
        InGameMapFeatureScope scope =
            InGameMapFeatureScope::AllFeatures)
{
    const QFileInfo destinationInfo(xmlFileName);
    QDir destinationDirectory(destinationInfo.absolutePath());
    if (!destinationDirectory.exists()) {
        if (error) {
            *error = QObject::tr("The destination directory does not exist:\n%1")
                    .arg(QDir::toNativeSeparators(
                             destinationDirectory.absolutePath()));
        }
        return false;
    }
    QTemporaryFile xmlTemporary(destinationDirectory.filePath(
            QLatin1String(".pzworlded-worldmap-XXXXXX.xml")));
    QTemporaryFile binaryTemporary(destinationDirectory.filePath(
            QLatin1String(".pzworlded-worldmap-XXXXXX.bin")));
    if (!xmlTemporary.open() || !binaryTemporary.open()) {
        if (error) {
            *error = QObject::tr(
                        "Could not create temporary files in:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(
                             destinationDirectory.absolutePath()),
                         !xmlTemporary.errorString().isEmpty()
                         ? xmlTemporary.errorString()
                         : binaryTemporary.errorString());
        }
        return false;
    }
    const QString xmlTemporaryName = xmlTemporary.fileName();
    const QString binaryTemporaryName = binaryTemporary.fileName();
    xmlTemporary.close();
    binaryTemporary.close();
    xmlTemporary.remove();
    binaryTemporary.remove();
    InGameMapWriter writer;
    writer.setFeatureScope(scope);
    if (!writer.writeWorld(world, xmlTemporaryName)) {
        QFile::remove(xmlTemporaryName);
        QFile::remove(binaryTemporaryName);
        if (error) {
            *error = QObject::tr("Could not write the XML map data.\n\n%1")
                    .arg(writer.errorString());
        }
        return false;
    }
    InGameMapWriterBinary binaryWriter;
    binaryWriter.setFeatureScope(scope);
    if (!binaryWriter.writeWorld(world, binaryTemporaryName)) {
        QFile::remove(xmlTemporaryName);
        QFile::remove(binaryTemporaryName);
        if (error) {
            *error = QObject::tr("Could not write the binary map data.\n\n%1")
                    .arg(binaryWriter.errorString());
        }
        return false;
    }
    QString binaryValidationError;
    if (!validateInGameMapBinaryFile(
                binaryTemporaryName, &binaryValidationError)) {
        QFile::remove(xmlTemporaryName);
        QFile::remove(binaryTemporaryName);
        if (error) {
            *error = QObject::tr(
                        "The generated binary map data is not compatible "
                        "with Build 42.20.\n\n%1")
                    .arg(binaryValidationError);
        }
        return false;
    }
    const bool committed = commitFilePair(
                { xmlTemporaryName, binaryTemporaryName },
                { xmlFileName, xmlFileName + QLatin1String(".bin") },
                error);
    if (!committed) {
        QFile::remove(xmlTemporaryName);
        QFile::remove(binaryTemporaryName);
    }
    return committed;
}
QImage createForestFeatureImage(
        World *world, int *featureCount, QString *error)
{
    if (featureCount)
        *featureCount = 0;
    if (!world) {
        if (error)
            *error = QObject::tr("No world is open.");
        return QImage();
    }
    const int cellSize = world->cellSize();
    const QSize imageSize(world->size() * cellSize);
    QImage image(imageSize, QImage::Format_ARGB32);
    if (image.isNull()) {
        if (error) {
            *error = QObject::tr(
                        "Could not allocate the %1 x %2 Forest image.")
                    .arg(imageSize.width()).arg(imageSize.height());
        }
        return QImage();
    }
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const QBrush brush(Qt::white);
    int drawnFeatures = 0;
    for (int cellY = 0; cellY < world->height(); ++cellY) {
        for (int cellX = 0; cellX < world->width(); ++cellX) {
            WorldCell *cell = world->cellAt(cellX, cellY);
            if (!cell)
                continue;
            for (InGameMapFeature *feature :
                 cell->inGameMap().features()) {
                if (!inGameMapFeatureMatchesScope(
                            feature,
                            InGameMapFeatureScope::ForestFeatures)) {
                    continue;
                }
                if (!feature->mGeometry.isPolygon()
                        || feature->mGeometry.mCoordinates.isEmpty()) {
                    continue;
                }
                QPolygonF polygon;
                QVector<QPolygonF> holes;
                const InGameMapCoordinates &outer =
                        feature->mGeometry.mCoordinates.first();
                for (const InGameMapPoint &point : outer)
                    polygon += QPointF(point.x, point.y);
                if (polygon.isEmpty())
                    continue;
                const bool clockwise = outer.isClockwise();
                for (int index = 1;
                     index < feature->mGeometry.mCoordinates.size();
                     ++index) {
                    const InGameMapCoordinates &inner =
                            feature->mGeometry.mCoordinates.at(index);
                    QPolygonF hole;
                    if (clockwise == inner.isClockwise()) {
                        for (int pointIndex = inner.size() - 1;
                             pointIndex >= 0; --pointIndex) {
                            const InGameMapPoint &point =
                                    inner.at(pointIndex);
                            hole += QPointF(point.x, point.y);
                        }
                    } else {
                        for (const InGameMapPoint &point : inner)
                            hole += QPointF(point.x, point.y);
                    }
                    holes += hole;
                }
                if (!polygon.isClosed())
                    polygon += polygon.first();
                QPainterPath path;
                path.addPolygon(polygon);
                QPainterPath holesPath;
                for (QPolygonF hole : std::as_const(holes)) {
                    if (!hole.isEmpty() && !hole.isClosed())
                        hole += hole.first();
                    if (!hole.isEmpty())
                        holesPath.addPolygon(hole);
                }
                path = path.subtracted(holesPath);
                path.translate(cellX * cellSize, cellY * cellSize);
                painter.fillPath(path, brush);
                ++drawnFeatures;
            }
        }
    }
    painter.end();
    if (featureCount)
        *featureCount = drawnFeatures;
    return image;
}
bool writeInGameMapForestBundle(
        World *world, const QString &outputDirectory,
        QStringList *writtenFiles, QString *error)
{
    QDir destination(outputDirectory);
    if (!destination.exists()) {
        if (error) {
            *error = QObject::tr("The destination directory does not exist:\n%1")
                    .arg(QDir::toNativeSeparators(outputDirectory));
        }
        return false;
    }
    int forestFeatureCount = 0;
    QString imageError;
    const QImage forestImage = createForestFeatureImage(
                world, &forestFeatureCount, &imageError);
    if (forestImage.isNull()) {
        if (error)
            *error = imageError;
        return false;
    }
    if (forestFeatureCount == 0) {
        if (error) {
            *error = QObject::tr(
                        "No natural=forest polygon is available.\n\n"
                        "Run Generate Tree Features first, then write "
                        "Worldmap-Forest again.");
        }
        return false;
    }
    QTemporaryDir staging(destination.filePath(
            QStringLiteral(".pzworlded-forest-XXXXXX")));
    if (!staging.isValid()) {
        if (error) {
            *error = QObject::tr("Could not create a staging directory in:\n%1")
                    .arg(QDir::toNativeSeparators(
                             destination.absolutePath()));
        }
        return false;
    }
    const QDir stagingDirectory(staging.path());
    const QString xmlTemporary = stagingDirectory.filePath(
                QStringLiteral("worldmap-forest.xml"));
    const QString binaryTemporary =
            xmlTemporary + QStringLiteral(".bin");
    const QString imageTemporary = stagingDirectory.filePath(
                QStringLiteral("forest.png"));
    const QString pyramidTemporary = stagingDirectory.filePath(
                QStringLiteral("forest.pyramid.zip"));
    if (!writeInGameMapFilePair(
                world, xmlTemporary, error,
                InGameMapFeatureScope::ForestFeatures)) {
        return false;
    }
    if (!forestImage.save(imageTemporary, "PNG")) {
        if (error) {
            *error = QObject::tr("Could not write the Forest PNG:\n%1")
                    .arg(QDir::toNativeSeparators(imageTemporary));
        }
        return false;
    }
    const GenerateLotsSettings settings =
            world->getGenerateLotsSettings();
    const int cellSize = world->cellSize();
    const QRect worldBounds(
                settings.worldOrigin.x() * cellSize,
                settings.worldOrigin.y() * cellSize,
                forestImage.width(), forestImage.height());
    QString pyramidError;
    if (!InGameMapImagePyramidWindow::createPyramidZip(
                forestImage, worldBounds, pyramidTemporary,
                &pyramidError,
                [](const QString &message) {
        qInfo().noquote() << "Forest pyramid:" << message;
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    })) {
        if (error) {
            *error = QObject::tr("Could not create the Forest pyramid.\n\n%1")
                    .arg(pyramidError);
        }
        return false;
    }
    const QStringList temporaryFiles = {
        xmlTemporary,
        binaryTemporary,
        imageTemporary,
        pyramidTemporary
    };
    const QStringList destinationFiles = {
        destination.filePath(QStringLiteral("worldmap-forest.xml")),
        destination.filePath(QStringLiteral("worldmap-forest.xml.bin")),
        destination.filePath(QStringLiteral("forest.png")),
        destination.filePath(QStringLiteral("forest.pyramid.zip"))
    };
    if (!commitFilePair(temporaryFiles, destinationFiles, error))
        return false;
    if (writtenFiles)
        *writtenFiles = destinationFiles;
    qInfo() << "Worldmap-Forest export completed:"
            << forestFeatureCount << "forest feature(s), bounds"
            << worldBounds << "files" << destinationFiles;
    return true;
}
bool tmxContainsMapContent(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const QStringRef name = xml.name();
        if (name == QLatin1String("layer") ||
                name == QLatin1String("objectgroup") ||
                name == QLatin1String("imagelayer") ||
                name == QLatin1String("group")) {
            return true;
        }
    }
    return xml.hasError();
}
int floorDivision(int value, int divisor)
{
    int result = value / divisor;
    if (value < 0 && value % divisor)
        --result;
    return result;
}
QDockWidget *visibleDockInTabGroup(QMainWindow *window,
                                   QDockWidget *reference)
{
    QList<QDockWidget*> group = window->tabifiedDockWidgets(reference);
    group.prepend(reference);
    for (QDockWidget *dock : group) {
        if (!dock->isFloating() && !dock->visibleRegion().isEmpty())
            return dock;
    }
    QDockWidget *largestDock = nullptr;
    int largestArea = -1;
    for (QDockWidget *dock : group) {
        if (!dock->isVisible() || dock->isFloating())
            continue;
        const int area = dock->width() * dock->height();
        if (area > largestArea) {
            largestArea = area;
            largestDock = dock;
        }
    }
    if (largestDock)
        return largestDock;
    return reference;
}
}
bool MainWindow::validateInGameMapForestExport(
        QString *summary, QString *error)
{
    World world(2, 2, WorldGridFormat::Native256);
    GenerateLotsSettings settings;
    settings.worldOrigin = QPoint(5, 7);
    world.setGenerateLotsSettings(settings);
    WorldCell *cell = world.cellAt(0, 0);
    InGameMapFeature *forest = new InGameMapFeature(&cell->inGameMap());
    forest->mGeometry.mType = QStringLiteral("Polygon");
    InGameMapCoordinates forestCoordinates;
    forestCoordinates += InGameMapPoint(16, 16);
    forestCoordinates += InGameMapPoint(120, 16);
    forestCoordinates += InGameMapPoint(120, 120);
    forestCoordinates += InGameMapPoint(16, 120);
    forest->mGeometry.mCoordinates += forestCoordinates;
    forest->mProperties.set(
                QStringLiteral("natural"), QStringLiteral("forest"));
    cell->inGameMap().features() += forest;
    InGameMapFeature *building =
            new InGameMapFeature(&cell->inGameMap());
    building->mGeometry.mType = QStringLiteral("Polygon");
    InGameMapCoordinates buildingCoordinates;
    buildingCoordinates += InGameMapPoint(180, 180);
    buildingCoordinates += InGameMapPoint(240, 180);
    buildingCoordinates += InGameMapPoint(240, 240);
    buildingCoordinates += InGameMapPoint(180, 240);
    building->mGeometry.mCoordinates += buildingCoordinates;
    building->mProperties.set(
                QStringLiteral("building"),
                QStringLiteral("Residential"));
    cell->inGameMap().features() += building;
    QTemporaryDir output;
    if (!output.isValid()) {
        if (error)
            *error = tr("Could not create the Forest export test folder.");
        return false;
    }
    QStringList forestFiles;
    QString exportError;
    if (!writeInGameMapForestBundle(
                &world, output.path(), &forestFiles, &exportError)) {
        if (error)
            *error = exportError;
        return false;
    }
    const QString worldMapPath = QDir(output.path()).filePath(
                QStringLiteral("worldmap.xml"));
    if (!writeInGameMapFilePair(
                &world, worldMapPath, &exportError,
                InGameMapFeatureScope::NonForestFeatures)) {
        if (error)
            *error = exportError;
        return false;
    }
    const auto readFile = [](const QString &path, QByteArray *data) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        *data = file.readAll();
        return file.error() == QFile::NoError;
    };
    const QDir directory(output.path());
    const QString forestXmlPath = directory.filePath(
                QStringLiteral("worldmap-forest.xml"));
    const QString forestBinaryPath = forestXmlPath
            + QStringLiteral(".bin");
    const QString worldBinaryPath = worldMapPath
            + QStringLiteral(".bin");
    QByteArray forestXml;
    QByteArray worldXml;
    QByteArray forestBinary;
    QByteArray worldBinary;
    if (!readFile(forestXmlPath, &forestXml)
            || !readFile(worldMapPath, &worldXml)
            || !readFile(forestBinaryPath, &forestBinary)
            || !readFile(worldBinaryPath, &worldBinary)) {
        if (error)
            *error = tr("One or more Forest export test files could not be read.");
        return false;
    }
    QString binaryHeaderError;
    if (!validateInGameMapBinaryFile(
                forestBinaryPath, &binaryHeaderError)
            || !validateInGameMapBinaryFile(
                worldBinaryPath, &binaryHeaderError)) {
        if (error) {
            *error = tr("An exported binary map header is invalid.\n\n%1")
                    .arg(binaryHeaderError);
        }
        return false;
    }
    if (!forestXml.contains("value=\"forest\"")
            || forestXml.contains("value=\"Residential\"")
            || !worldXml.contains("value=\"Residential\"")
            || worldXml.contains("value=\"forest\"")
            || !forestXml.contains("cellSize=\"256\"")
            || !worldXml.contains("cellSize=\"256\"")
            || !forestBinary.contains("forest")
            || forestBinary.contains("Residential")
            || !worldBinary.contains("Residential")
            || worldBinary.contains("forest")) {
        if (error) {
            *error = tr("Forest and non-Forest features were not separated "
                        "consistently in XML and binary output.");
        }
        return false;
    }
    World legacyWorld(1, 1, WorldGridFormat::Legacy300);
    WorldCell *legacyCell = legacyWorld.cellAt(0, 0);
    InGameMapFeature *legacyFeature =
            new InGameMapFeature(&legacyCell->inGameMap());
    legacyFeature->mGeometry.mType = QStringLiteral("Polygon");
    InGameMapCoordinates legacyCoordinates;
    legacyCoordinates += InGameMapPoint(250, 10);
    legacyCoordinates += InGameMapPoint(290, 10);
    legacyCoordinates += InGameMapPoint(290, 40);
    legacyCoordinates += InGameMapPoint(250, 40);
    legacyFeature->mGeometry.mCoordinates += legacyCoordinates;
    legacyFeature->mProperties.set(
                QStringLiteral("building"), QStringLiteral("Legacy"));
    legacyCell->inGameMap().features() += legacyFeature;
    const QString legacyXmlPath = directory.filePath(
                QStringLiteral("legacy-worldmap.xml"));
    InGameMapWriter legacyWriter;
    if (!legacyWriter.writeWorld(&legacyWorld, legacyXmlPath)) {
        if (error)
            *error = legacyWriter.errorString();
        return false;
    }
    QByteArray legacyXml;
    if (!readFile(legacyXmlPath, &legacyXml)) {
        if (error)
            *error = tr("The Legacy 300 XML conversion could not be read.");
        return false;
    }
    QXmlStreamReader legacyReader(legacyXml);
    QSet<int> legacyCells;
    int legacyCellX = 0;
    int legacyCellY = 0;
    bool legacyCellSizeValid = false;
    qreal legacyMinX = std::numeric_limits<qreal>::max();
    qreal legacyMinY = std::numeric_limits<qreal>::max();
    qreal legacyMaxX = std::numeric_limits<qreal>::lowest();
    qreal legacyMaxY = std::numeric_limits<qreal>::lowest();
    while (!legacyReader.atEnd()) {
        legacyReader.readNext();
        if (!legacyReader.isStartElement())
            continue;
        if (legacyReader.name() == QLatin1String("world")) {
            legacyCellSizeValid = legacyReader.attributes().value(
                        QLatin1String("cellSize")) == QLatin1String("256");
        } else if (legacyReader.name() == QLatin1String("cell")) {
            legacyCellX = legacyReader.attributes().value(
                        QLatin1String("x")).toInt();
            legacyCellY = legacyReader.attributes().value(
                        QLatin1String("y")).toInt();
            legacyCells.insert(legacyCellX);
        } else if (legacyReader.name() == QLatin1String("point")) {
            const qreal localX = legacyReader.attributes().value(
                        QLatin1String("x")).toDouble();
            const qreal localY = legacyReader.attributes().value(
                        QLatin1String("y")).toDouble();
            const qreal absoluteX = legacyCellX * 256 + localX;
            const qreal absoluteY = legacyCellY * 256 + localY;
            legacyMinX = qMin(legacyMinX, absoluteX);
            legacyMinY = qMin(legacyMinY, absoluteY);
            legacyMaxX = qMax(legacyMaxX, absoluteX);
            legacyMaxY = qMax(legacyMaxY, absoluteY);
        }
    }
    if (legacyReader.hasError() || !legacyCellSizeValid
            || !legacyCells.contains(0) || !legacyCells.contains(1)
            || qAbs(legacyMinX - 250.0) > 0.01
            || qAbs(legacyMinY - 10.0) > 0.01
            || qAbs(legacyMaxX - 290.0) > 0.01
            || qAbs(legacyMaxY - 40.0) > 0.01) {
        if (error) {
            *error = tr("Legacy 300 world-map geometry was not converted "
                        "to the Build 42.20 256-square XML grid.");
        }
        return false;
    }
    const QString forestImagePath = directory.filePath(
                QStringLiteral("forest.png"));
    const QImage forestImage(forestImagePath);
    if (forestImage.size() != QSize(512, 512)
            || qAlpha(forestImage.pixel(32, 32)) == 0
            || qAlpha(forestImage.pixel(210, 210)) != 0) {
        if (error) {
            *error = tr("forest.png does not represent only the Forest "
                        "feature geometry.");
        }
        return false;
    }
    const QString pyramidPath = directory.filePath(
                QStringLiteral("forest.pyramid.zip"));
    QuaZip zip(pyramidPath);
    if (!zip.open(QuaZip::Mode::mdUnzip)) {
        if (error)
            *error = tr("forest.pyramid.zip could not be opened.");
        return false;
    }
    const QStringList entries = zip.getFileNameList();
    if (!entries.contains(QStringLiteral("0/tile0x0.png"))
            || !entries.contains(QStringLiteral("1/tile0x0.png"))
            || !entries.contains(QStringLiteral("pyramid.txt"))
            || !zip.setCurrentFile(QStringLiteral("pyramid.txt"))) {
        zip.close();
        if (error) {
            *error = tr("The Forest pyramid is missing its image levels or "
                        "pyramid.txt.");
        }
        return false;
    }
    QuaZipFile pyramidTextFile(&zip);
    if (!pyramidTextFile.open(QIODevice::ReadOnly)) {
        zip.close();
        if (error)
            *error = tr("pyramid.txt could not be read from the Forest pyramid.");
        return false;
    }
    const QByteArray pyramidText = pyramidTextFile.readAll();
    pyramidTextFile.close();
    zip.close();
    if (!pyramidText.contains("VERSION=1")
            || !pyramidText.contains("bounds=1280 1792 1792 2304")
            || !pyramidText.contains("imageSize=512 512")) {
        if (error) {
            *error = tr("pyramid.txt does not contain the expected world "
                        "bounds and image size.");
        }
        return false;
    }
    if (forestFiles.size() != 4) {
        if (error)
            *error = tr("The Forest export did not report all four output files.");
        return false;
    }
    if (summary) {
        *summary = tr("Build 42.20 IGMB headers, Native 256 XML metadata, "
                      "Legacy 300 to 256 XML conversion, forest/non-Forest "
                      "filtering, Forest PNG, two image levels, and "
                      "pyramid.txt bounds verified");
    }
    return true;
}
MainWindow *MainWindow::mInstance = 0;

MainWindow *MainWindow::instance()
{
    return mInstance;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mLayersDock(new LayersDock(this))
    , mLotsDock(new LotsDock(this))
    , mMapsDock(new MapsDock(this))
    , mObjectsDock(new ObjectsDock(this))
    , mPropertiesDock(new PropertiesDock(this))
    , mSearchDock(new SearchDock(this))
    , mStreetNamesDock(new StreetNamesDock(this))
    , mRegionsDock(new RegionsDock(this))
    , mInGameMapDock(new InGameMapDock(this))
#ifdef ROAD_UI
    , mRoadsDock(new RoadsDock(this))
#endif
    , mCurrentDocument(0)
    , mCurrentLevelMenu(new QMenu(this))
    , mObjectGroupMenu(new QMenu(this))
    , mZoomable(0)
    , mSettings(QSettings::IniFormat, QSettings::UserScope,
                QLatin1String("TheIndieStone"), QLatin1String("PZWorldEd"))
    , mLotPackWindow(0)
{
    ui->setupUi(this);
    AppIssueNotifier::attach(this);

    mPartialChunksMenu = new QMenu(tr("Partial Chunks"), this);
    ui->menuBar->insertMenu(ui->helpMenu->menuAction(),
                            mPartialChunksMenu);
    mPartialChunksAction = mPartialChunksMenu->addAction(
                tr("Enable Partial Chunks"));
    mPartialChunksAction->setCheckable(true);
    mPartialChunksAction->setIcon(QIcon(
                QLatin1String(":/images/22x22/stock-tool-rect-select.png")));
    mPartialChunksAction->setToolTip(tr(
                "Enable or disable Partial Chunks"));
    mPartialChunksAction->setStatusTip(tr(
                "Enable the Native256 8 x 8-square chunk export mask"));
    mSelectAllPartialChunksAction = mPartialChunksMenu->addAction(
                tr("Select All Chunks"));
    mSelectAllPartialChunksAction->setIcon(QIcon(
                QLatin1String(":/images/22x22/tool-select-objects.png")));
    mSelectAllPartialChunksAction->setToolTip(tr(
                "Select all chunks (Ctrl+A)"));
    mSelectAllPartialChunksAction->setShortcut(QKeySequence::SelectAll);
    mSelectAllPartialChunksAction->setStatusTip(tr(
                "Include all 1024 chunks in the current cell"));
    mClearPartialChunksAction = mPartialChunksMenu->addAction(
                tr("Clear Chunk Selection"));
    mClearPartialChunksAction->setIcon(QIcon(
                QLatin1String(":/images/24x24/edit-clear.png")));
    mClearPartialChunksAction->setToolTip(tr(
                "Clear the chunk selection"));
    mClearPartialChunksAction->setStatusTip(tr(
                "Omit all chunks from the current cell export"));
    mPartialChunksMenu->addSeparator();
    QAction *partialChunksHelpAction = mPartialChunksMenu->addAction(
                tr("How Partial Chunks Works..."));
    connect(mPartialChunksAction, &QAction::toggled,
            this, &MainWindow::setPartialChunksEnabled);
    connect(mSelectAllPartialChunksAction, &QAction::triggered,
            this, &MainWindow::selectAllPartialChunks);
    connect(mClearPartialChunksAction, &QAction::triggered,
            this, &MainWindow::clearPartialChunks);
    connect(partialChunksHelpAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, tr("Partial Chunks"), tr(
                    "Partial Chunks is a Native256 LOT export mask. The cell remains a normal 256 x 256 editing canvas.\n\n"
                    "Open a cell view and enable it from the Partial Chunks menu or its toolbar. The overlay is a 32 x 32 grid. Each grid square is one complete 8 x 8-square game chunk. Click a chunk to include or omit it. Drag across the grid to select or clear a rectangular group. The starting chunk determines whether the group is included or omitted. Included chunks use a light tint and omitted chunks are darkened. The line and tint color follows the Grid color in Preferences.\n\n"
                    "Chunk selection does not select, delete, or modify TMX tiles. Ctrl+A selects all 1024 chunks while the mode is active. Select All Chunks and Clear Chunk Selection change the export mask for the complete cell.\n\n"
                    "The mask is saved beside the TMX as map-name.tmx.pzchunks and is shared with TileZed. While the mode is enabled, Hole Detection and automatic hole filling are bypassed. Generate Lots writes selected chunks and encodes omitted chunks as absent LOT data and null navigation chunks. Legacy 300-square projects are not supported."));
    });
    mPartialChunksToolBar = addToolBar(tr("Partial Chunks"));
    mPartialChunksToolBar->setObjectName(
                QLatin1String("PartialChunksToolBar"));
    mPartialChunksToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mPartialChunksToolBar->addAction(mPartialChunksAction);
    mPartialChunksToolBar->addAction(mSelectAllPartialChunksAction);
    mPartialChunksToolBar->addAction(mClearPartialChunksAction);

    mInstance = this;

    Preferences *prefs = Preferences::instance();

    setStatusBar(0);
    mZoomComboBox = ui->zoomComboBox;

    QString coordString = QLatin1String("Cell x,y=300,300");
    int width = ui->coordinatesLabel->fontMetrics().horizontalAdvance(coordString);
    ui->coordinatesLabel->setMinimumWidth(width + 8);

    coordString = QLatin1String("World x,y=9999,9999");
    width = ui->coordinatesLabel->fontMetrics().horizontalAdvance(coordString);
    ui->worldCoordinatesLabel->setMinimumWidth(width + 8);

    ui->actionSave->setShortcuts(QKeySequence::Save);
    ui->actionSaveAs->setShortcuts(QKeySequence::SaveAs);
    ui->actionClose->setShortcuts(QKeySequence::Close);
    ui->actionQuit->setShortcuts(QKeySequence::Quit);

    ui->actionCopy->setShortcuts(QKeySequence::Copy);
    ui->actionPaste->setShortcuts(QKeySequence::Paste);

    ui->actionShowCellBorder->setChecked(prefs->showCellBorder());
    ui->actionSnapToGrid->setChecked(prefs->snapToGrid());
    ui->actionShowCoordinates->setChecked(prefs->showCoordinates());
    ui->actionShowGrid->setChecked(prefs->showWorldGrid());
    ui->actionShowInvisibleTiles->setChecked(prefs->showInvisibleTiles());
    ui->actionShowMiniMap->setChecked(prefs->showMiniMap());
    ui->actionShowObjects->setChecked(prefs->showObjects());
    ui->actionShowObjectNames->setChecked(prefs->showObjectNames());
    ui->actionShowVehicleMeshPreviews->setChecked(
                prefs->showVehicleMeshPreviews());
    ui->actionShowBMP->setChecked(prefs->showBMPs());
    ui->actionShowOtherWorlds->setChecked(prefs->showOtherWorlds());
    ui->actionShowWorldThumbnails->setChecked(prefs->showWorldThumbnails());
    ui->actionShowZombieSpawnImage->setChecked(prefs->showZombieSpawnImage());
    ui->actionShowBiomeMap->setChecked(prefs->showBiomeMap());
    ui->actionShowZonesInWorldView->setChecked(prefs->showZonesInWorldView());
    ui->actionHighlightCurrentLevel->setChecked(prefs->highlightCurrentLevel());
    ui->actionHighlightRoomUnderPointer->setChecked(prefs->highlightRoomUnderPointer());
    ui->actionShowLotFloorsOnly->setChecked(prefs->showLotFloorsOnly());

    // Make sure Ctrl+= also works for zooming in
    QList<QKeySequence> keys = QKeySequence::keyBindings(QKeySequence::ZoomIn);
    keys += QKeySequence(tr("Ctrl+="));
    keys += QKeySequence(tr("+"));
    keys += QKeySequence(tr("="));
    ui->actionZoomIn->setShortcuts(keys);
    keys = QKeySequence::keyBindings(QKeySequence::ZoomOut);
    keys += QKeySequence(tr("-"));
    ui->actionZoomOut->setShortcuts(keys);

    QUndoGroup *undoGroup = docman()->undoGroup();
    mUndoAction = undoGroup->createUndoAction(this, tr("Undo"));
    mRedoAction = undoGroup->createRedoAction(this, tr("Redo"));
    mRedoAction->setPriority(QAction::LowPriority);
    mUndoAction->setIconText(tr("Undo"));
    mUndoAction->setShortcuts(QKeySequence::Undo);
    mRedoAction->setIconText(tr("Redo"));
    mRedoAction->setShortcuts(QKeySequence::Redo);
    connect(undoGroup, &QUndoGroup::cleanChanged, this, &MainWindow::updateWindowTitle);
    QAction *separator = ui->editMenu->actions().first();
    ui->editMenu->insertAction(separator, mUndoAction);
    ui->editMenu->insertAction(separator, mRedoAction);

    ui->mainToolBar->addAction(mUndoAction);
    ui->mainToolBar->addAction(mRedoAction);

    QIcon newIcon = ui->actionNew->icon();
    QIcon openIcon = ui->actionOpen->icon();
    QIcon saveIcon = ui->actionSave->icon();
    QIcon redoIcon(QLatin1String(":images/16x16/edit-redo.png"));
    QIcon undoIcon(QLatin1String(":images/16x16/edit-undo.png"));
    newIcon.addFile(QLatin1String(":images/24x24/document-new.png"));
    openIcon.addFile(QLatin1String(":images/24x24/document-open.png"));
    saveIcon.addFile(QLatin1String(":images/24x24/document-save.png"));
    redoIcon.addFile(QLatin1String(":images/24x24/edit-redo.png"));
    undoIcon.addFile(QLatin1String(":images/24x24/edit-undo.png"));

    ui->actionNew->setIcon(newIcon);
    ui->actionOpen->setIcon(openIcon);
    ui->actionSave->setIcon(saveIcon);
    mUndoAction->setIcon(undoIcon);
    mRedoAction->setIcon(redoIcon);

    mUndoDock = new UndoDock(undoGroup, this);

    QMenu *worldMapOverlayMenu = new QMenu(
                tr("World Map Overlays"), ui->menuView);
    mLoadWorldMapOverlayAction = worldMapOverlayMenu->addAction(
                tr("Load worldmap.xml..."));
    mLoadWorldMapForestOverlayAction = worldMapOverlayMenu->addAction(
                tr("Load worldmap-forest.xml..."));
    worldMapOverlayMenu->addSeparator();
    mShowWorldMapOverlayAction = worldMapOverlayMenu->addAction(
                tr("Show worldmap overlay"));
    mShowWorldMapOverlayAction->setCheckable(true);
    mShowWorldMapForestOverlayAction = worldMapOverlayMenu->addAction(
                tr("Show worldmap-forest overlay"));
    mShowWorldMapForestOverlayAction->setCheckable(true);
    worldMapOverlayMenu->addSeparator();
    mClearWorldMapOverlaysAction = worldMapOverlayMenu->addAction(
                tr("Clear loaded overlays"));
    const QList<QAction *> viewActions = ui->menuView->actions();
    const int overlayPosition = viewActions.indexOf(
                ui->actionShowZonesInWorldView) + 1;
    QAction *viewMenuSeparator = overlayPosition > 0
            && overlayPosition < viewActions.size()
            ? viewActions.at(overlayPosition) : nullptr;
    ui->menuView->insertMenu(viewMenuSeparator, worldMapOverlayMenu);
    connect(mLoadWorldMapOverlayAction, &QAction::triggered,
            this, [this]() { loadWorldMapOverlay(false); });
    connect(mLoadWorldMapForestOverlayAction, &QAction::triggered,
            this, [this]() { loadWorldMapOverlay(true); });
    connect(mShowWorldMapOverlayAction, &QAction::toggled,
            this, [this](bool visible) {
        WorldDocument *worldDoc = currentWorldDocument();
        WorldView *view = worldDoc
                ? dynamic_cast<WorldView *>(worldDoc->view()) : nullptr;
        if (view)
            view->scene()->setWorldMapOverlayVisible(false, visible);
    });
    connect(mShowWorldMapForestOverlayAction, &QAction::toggled,
            this, [this](bool visible) {
        WorldDocument *worldDoc = currentWorldDocument();
        WorldView *view = worldDoc
                ? dynamic_cast<WorldView *>(worldDoc->view()) : nullptr;
        if (view)
            view->scene()->setWorldMapOverlayVisible(true, visible);
    });
    connect(mClearWorldMapOverlaysAction, &QAction::triggered,
            this, [this]() {
        WorldDocument *worldDoc = currentWorldDocument();
        WorldView *view = worldDoc
                ? dynamic_cast<WorldView *>(worldDoc->view()) : nullptr;
        if (view)
            view->scene()->clearWorldMapOverlays();
        updateActions();
    });

    ui->menuView->addAction(mLayersDock->toggleViewAction());
    ui->menuView->addAction(mLotsDock->toggleViewAction());
    ui->menuView->addAction(mInGameMapDock->toggleViewAction());
    ui->menuView->addAction(mMapsDock->toggleViewAction());
    ui->menuView->addAction(mObjectsDock->toggleViewAction());
    ui->menuView->addAction(mPropertiesDock->toggleViewAction());
    ui->menuView->addAction(mSearchDock->toggleViewAction());
    ui->menuView->addAction(mStreetNamesDock->toggleViewAction());
    ui->menuView->addAction(mRegionsDock->toggleViewAction());
    ui->menuView->addSeparator();
    QAction *renderDiagnosticsAction = new QAction(
                tr("Render Diagnostics"), this);
    renderDiagnosticsAction->setCheckable(true);
    renderDiagnosticsAction->setToolTip(tr(
        "Show FPS, render time, drawn tiles, memory, zoom and renderer mode"));
    renderDiagnosticsAction->setChecked(mSettings.value(
        QLatin1String("RenderDiagnostics/Enabled"), false).toBool());
    ui->menuView->addAction(renderDiagnosticsAction);
    connect(renderDiagnosticsAction, &QAction::toggled, this,
            [this](bool enabled) {
        mSettings.setValue(
                    QLatin1String("RenderDiagnostics/Enabled"), enabled);
        const QList<BaseGraphicsView *> views =
                findChildren<BaseGraphicsView *>();
        for (BaseGraphicsView *view : views)
            view->setRenderDiagnosticsEnabled(enabled);
    });
    mSettings.setValue(QLatin1String("EnvironmentPreview/Powered"), false);
    mSettings.setValue(QLatin1String("EnvironmentPreview/Snow"), false);
    mSettings.setValue(QLatin1String("EnvironmentPreview/Jumbo"), false);
    const QString previewStyle = QStringLiteral(
        "QToolButton { padding: 2px 7px; border: 1px solid #68717d;"
        " border-radius: 4px; background: rgba(32,36,42,220);"
        " color: #d9dde5; font-weight: bold; }"
        "QToolButton:checked { border: 2px solid #76c7ff;"
        " background: #235c84; color: white; }");
    const auto makePreviewButton = [this, previewStyle](
            const QString &text, const QString &toolTip) {
        QToolButton *button = new QToolButton(ui->viewTools);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setStyleSheet(previewStyle);
        button->setMinimumHeight(24);
        return button;
    };
    mPoweredPreviewButton = makePreviewButton(
        tr("POWER"), tr("Preview matching *_on tile variants"));
    mSnowPreviewButton = makePreviewButton(
        tr("SNOW"), tr("Preview SnowTile mappings and roof coverage"));
    mJumboPreviewButton = makePreviewButton(
        tr("JUMBO"), tr("Preview deterministic Jumbo XL/XXL variants"));
    mPoweredPreviewButton->setChecked(false);
    mSnowPreviewButton->setChecked(false);
    mJumboPreviewButton->setChecked(false);
    const int previewInsertIndex =
            qMax(0, ui->horizontalLayout->count() - 2);
    ui->horizontalLayout->insertWidget(
        previewInsertIndex, mPoweredPreviewButton);
    ui->horizontalLayout->insertWidget(
        previewInsertIndex + 1, mSnowPreviewButton);
    ui->horizontalLayout->insertWidget(
        previewInsertIndex + 2, mJumboPreviewButton);
#ifdef ROAD_UI
    ui->menuView->addAction(mRoadsDock->toggleViewAction());
#endif

    addDockWidget(Qt::LeftDockWidgetArea, mInGameMapDock);
    addDockWidget(Qt::LeftDockWidgetArea, mLotsDock);
    addDockWidget(Qt::LeftDockWidgetArea, mObjectsDock);
    addDockWidget(Qt::LeftDockWidgetArea, mSearchDock);
    addDockWidget(Qt::LeftDockWidgetArea, mStreetNamesDock);
    addDockWidget(Qt::LeftDockWidgetArea, mRegionsDock);
#ifdef ROAD_UI
    addDockWidget(Qt::LeftDockWidgetArea, mRoadsDock);
#endif
    addDockWidget(Qt::RightDockWidgetArea, mPropertiesDock);
    addDockWidget(Qt::RightDockWidgetArea, mLayersDock);
    addDockWidget(Qt::RightDockWidgetArea, mMapsDock);
    tabifyDockWidget(mPropertiesDock, mLayersDock);
    tabifyDockWidget(mLayersDock, mMapsDock);
    tabifyDockWidget(mObjectsDock, mLotsDock);
    tabifyDockWidget(mLotsDock, mInGameMapDock);
    tabifyDockWidget(mSearchDock, mStreetNamesDock);
    tabifyDockWidget(mStreetNamesDock, mRegionsDock);
    mSearchDock->raise();
    mObjectsDock->raise();

    addDockWidget(Qt::RightDockWidgetArea, mUndoDock);

    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newWorld);
    connect(ui->actionOpen, &QAction::triggered, this, qOverload<>(&MainWindow::openFile));
    connect(ui->actionEditCell, &QAction::triggered, this, &MainWindow::editCell);
    connect(ui->actionGoToXY, &QAction::triggered, this, &MainWindow::goToXY);
    connect(ui->actionSave, &QAction::triggered, this, qOverload<>(&MainWindow::saveFile));
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveFileAs);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::closeFile);
    connect(ui->actionCloseAll, &QAction::triggered, this, &MainWindow::closeAllFiles);
    connect(ui->actionGenerateLotsAll8x8, &QAction::triggered,
            this, &MainWindow::generateLotsAll8x8);
    connect(ui->actionGenerateLotsSelected8x8, &QAction::triggered,
            this, &MainWindow::generateLotsSelected8x8);
    connect(ui->actionExportModAll8x8, &QAction::triggered,
            this, &MainWindow::exportModAll8x8);
    connect(ui->actionOverwriteSpawnMap_AllCells_256, &QAction::triggered, this, &MainWindow::overwriteSpawnMap_AllCells_256);
    connect(ui->actionOverwriteSpawnMap_SelectedCells_256, &QAction::triggered, this, &MainWindow::overwriteSpawnMap_SelectedCells_256);
    connect(ui->actionBMPToTMXAll, &QAction::triggered,
            this, &MainWindow::BMPToTMXAll);
    connect(ui->actionBMPToTMXSelected, &QAction::triggered,
            this, &MainWindow::BMPToTMXSelected);
    connect(ui->actionTMXToBMPAll, &QAction::triggered,
            this, &MainWindow::TMXToBMPAll);
    connect(ui->actionTMXToBMPSelected, &QAction::triggered,
            this, &MainWindow::TMXToBMPSelected);
    connect(ui->actionLUAObjectDump, &QAction::triggered, this, &MainWindow::WriteSpawnPoints);
    connect(ui->actionWriteObjects, &QAction::triggered, this, &MainWindow::WriteWorldObjects);
    connect(ui->actionReadObjectsFromLua, &QAction::triggered, this, &MainWindow::ReadWorldObjects);
    connect(ui->actionWriteRoomTonesToLua, &QAction::triggered, this, &MainWindow::WriteRoomTones);
    connect(ui->actionFromToAll, &QAction::triggered,
            this, &MainWindow::FromToAll);
    connect(ui->actionFromToSelected, &QAction::triggered,
            this, &MainWindow::FromToSelected);
    connect(ui->actionBuildingsToPNG, &QAction::triggered, this, &MainWindow::BuildingsToPNG);
    connect(ui->actionZonesToPNG, &QAction::triggered, this, &MainWindow::ZonesToPNG);
    connect(ui->actionQuit, &QAction::triggered, this, &QWidget::close);

    connect(ui->actionCopy, &QAction::triggered, this, &MainWindow::copy);
    connect(ui->actionPaste, &QAction::triggered, this, &MainWindow::paste);
    connect(ui->actionClipboard, &QAction::triggered, this, &MainWindow::showClipboard);

    connect(ui->actionPreferences, &QAction::triggered, this, &MainWindow::preferencesDialog);
    QAction *rebuildTilesetsAction =
            new QAction(tr("Update Tilesets.txt from Tiles PNGs..."), this);
    ui->editMenu->insertAction(ui->actionPreferences,
                               rebuildTilesetsAction);
    ui->editMenu->insertSeparator(ui->actionPreferences);
    connect(rebuildTilesetsAction, &QAction::triggered, this, [this]() {
        const QString catalogPath =
                TileMetaInfoMgr::instance()->txtPath();
        if (QMessageBox::question(
                    this, tr("Update Tilesets.txt"),
                    tr("Scan the configured Tiles directory and add new PNG "
                       "sheets to:\n%1\n\nExisting tile meta-enums are "
                       "preserved. Paths and sheet dimensions are refreshed. "
                       "Entries with no readable PNG anywhere in the 2x or "
                       "1x Tiles trees are removed. The previous catalog is "
                       "kept as Tilesets.txt.bak.")
                    .arg(QDir::toNativeSeparators(catalogPath)))
                != QMessageBox::Yes) {
            return;
        }
        PROGRESS progress(tr("Scanning Tiles PNG sheets..."), this);
        int added = 0;
        int updated = 0;
        int removed = 0;
        if (!TileMetaInfoMgr::instance()->rebuildTilesetsTxt(
                    &added, &updated, &removed)) {
            QMessageBox::critical(
                        this, tr("Tilesets.txt Update Failed"),
                        TileMetaInfoMgr::instance()->errorString());
            return;
        }
        progress.release();
        TilesetManager::instance()->tilesetDirectoryChanged();
        TilesetManager::instance()->waitForTilesets(
                    TileMetaInfoMgr::instance()->tilesets(), this);
        QMessageBox::information(
                    this, tr("Tilesets.txt Updated"),
                    tr("%1 new sheet(s) added; %2 existing entries updated; "
                       "%3 missing entries removed."
                       "\n\n%4")
                    .arg(added).arg(updated).arg(removed)
                    .arg(updated > 0 || removed > 0
                         ? tr("Restart the PZTools applications before "
                              "editing maps that use the changed catalogue.")
                         : tr("The catalog and live tileset list are current.")));
    });
    connect(ui->actionKeyboardShortcuts, &QAction::triggered, this, &MainWindow::keyboardShortcuts);

    connect(ui->actionResizeWorld, &QAction::triggered, this, &MainWindow::resizeWorld);
    connect(ui->actionLinkedWorldProjects, &QAction::triggered,
            this, &MainWindow::linkedWorldProjects);
    connect(ui->actionObjectGroups, &QAction::triggered, this, &MainWindow::objectGroupsDialog);
    connect(ui->actionObjectTypes, &QAction::triggered, this, &MainWindow::objectTypesDialog);
    connect(ui->actionEnums, &QAction::triggered, this, &MainWindow::propertyEnumsDialog);
    connect(ui->actionProperties, &QAction::triggered, this, &MainWindow::properyDefinitionsDialog);
    connect(ui->actionTemplates, &QAction::triggered, this, &MainWindow::templatesDialog);
#ifdef ROAD_UI
    connect(ui->actionRemoveRoad, SIGNAL(triggered()), SLOT(removeRoad()));
#else
    ui->actionRemoveRoad->setVisible(false);
#endif
    connect(ui->actionRemoveBMP, &QAction::triggered, this, &MainWindow::removeBMP);

    connect(ui->actionRemoveLot, &QAction::triggered, this, &MainWindow::removeLot);
    connect(ui->actionRemoveObject, &QAction::triggered, this, &MainWindow::removeObject);
    connect(ui->actionSplitObjectPolygon, &QAction::triggered, this, &MainWindow::splitObjectPolygon);
    connect(ui->actionExtractLots, &QAction::triggered, this, &MainWindow::extractLots);
    connect(ui->actionExtractObjects, &QAction::triggered, this, &MainWindow::extractObjects);
    connect(ui->actionClearCell, &QAction::triggered, this, &MainWindow::clearCells);
    connect(ui->actionClearMapOnly, &QAction::triggered, this, &MainWindow::clearMapOnly);
    connect(ui->actionRemoveEmptyBorderCells, &QAction::triggered,
            this, &MainWindow::removeEmptyBorderCells);
    connect(ui->menuCell, &QMenu::aboutToShow, this, [this]() {
        ui->actionRemoveEmptyBorderCells->setEnabled(canRemoveEmptyBorderCells());
    });
    connect(ui->actionCheckForHoles, &QAction::triggered, this, &MainWindow::checkForHoles);

    connect(ui->actionGenerateInGameMapBuildingFeatures, &QAction::triggered, this, &MainWindow::generateInGameMapBuildingFeatures);
    connect(ui->actionGenerateInGameMapTreeFeatures, &QAction::triggered, this, &MainWindow::generateInGameMapTreeFeatures);
    connect(ui->actionGenerateInGameMapWaterFeatures, &QAction::triggered, this, &MainWindow::generateInGameMapWaterFeatures);
    connect(ui->actionGenerateInGameMapRoadFeatures, &QAction::triggered, this, &MainWindow::generateInGameMapRoadFeatures);
    connect(ui->actionWriteInGameMapForest, &QAction::triggered,
            this, &MainWindow::writeInGameMapForest);
    connect(ui->actionWriteInGameMapWorldMap, &QAction::triggered,
            this, &MainWindow::writeInGameMapWorldMap);
    connect(ui->actionEditWorldMapAnnotations, &QAction::triggered,
            this, &MainWindow::editWorldMapAnnotations);
    connect(ui->actionRemoveInGameMapFeatures, &QAction::triggered, this, &MainWindow::removeInGameMapFeatures);
    connect(ui->actionRemoveInGameMapPoints, &QAction::triggered, this, &MainWindow::removeInGameMapPoint);
    connect(ui->actionSplitInGameMapPolygon, &QAction::triggered, this, &MainWindow::splitInGameMapPolygon);
    connect(ui->actionConvertToPolygon, &QAction::triggered, this, &MainWindow::convertInGameMapPolylineToPolygon);
    connect(ui->actionAddInGameMapHole, &QAction::triggered, this, &MainWindow::addInGameMapHole);
    connect(ui->actionRemoveInGameMapHole, &QAction::triggered, this, &MainWindow::removeInGameMapHole);
    connect(ui->actionReadInGameMapFeaturesXML, &QAction::triggered, this, &MainWindow::readInGameMapFeaturesXML);
    connect(ui->actionWriteInGameMapFeaturesXML_256, &QAction::triggered, this, &MainWindow::writeInGameMapFeaturesXML);
    connect(ui->actionOverwriteInGameMapFeaturesXML_256, &QAction::triggered, this, &MainWindow::overwriteInGameMapFeaturesXML);
    connect(ui->actionCreateFeatureImage, &QAction::triggered, this, &MainWindow::createInGameMapFeatureImage);
    connect(ui->actionCreateWorldImage, &QAction::triggered, this, &MainWindow::createInGameMapImage);
    connect(ui->actionCreateImagePyramid, &QAction::triggered, this, &MainWindow::createInGameMapImagePyramid);

    connect(ui->actionShowCellBorder, &QAction::toggled, prefs, &Preferences::setShowCellBorder);
    connect(ui->actionSnapToGrid, &QAction::toggled, prefs, &Preferences::setSnapToGrid);
    connect(ui->actionShowCoordinates, &QAction::toggled, prefs, &Preferences::setShowCoordinates);
    connect(ui->actionShowGrid, &QAction::toggled, this, &MainWindow::setShowGrid);
    connect(ui->actionShowMiniMap, &QAction::toggled, prefs, &Preferences::setShowMiniMap);
    connect(ui->actionShowObjects, &QAction::toggled, prefs, &Preferences::setShowObjects);
    connect(ui->actionShowObjectNames, &QAction::toggled, prefs, &Preferences::setShowObjectNames);
    connect(ui->actionShowVehicleMeshPreviews, &QAction::toggled,
            prefs, &Preferences::setShowVehicleMeshPreviews);
    connect(ui->actionShowOtherWorlds, &QAction::toggled, prefs, &Preferences::setShowOtherWorlds);
    connect(ui->actionShowWorldThumbnails, &QAction::toggled, prefs, &Preferences::setShowWorldThumbnails);
    connect(ui->actionShowBMP, &QAction::toggled, prefs, &Preferences::setShowBMPs);
    connect(ui->actionShowZombieSpawnImage, &QAction::toggled, prefs, &Preferences::setShowZombieSpawnImage);
    connect(ui->actionShowBiomeMap, &QAction::toggled,
            prefs, &Preferences::setShowBiomeMap);
    connect(ui->actionShowZonesInWorldView, &QAction::toggled, prefs, &Preferences::setShowZonesInWorldView);
    connect(ui->actionHighlightCurrentLevel, &QAction::toggled, prefs, &Preferences::setHighlightCurrentLevel);
    connect(ui->actionHighlightRoomUnderPointer, &QAction::toggled, prefs, &Preferences::setHighlightRoomUnderPointer);
    connect(ui->actionShowLotFloorsOnly, &QAction::toggled, prefs, &Preferences::setShowLotFloorsOnly);
    connect(ui->actionLevelAbove, &QAction::triggered, this, &MainWindow::selectLevelAbove);
    connect(ui->actionLevelBelow, &QAction::triggered, this, &MainWindow::selectLevelBelow);
    connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::zoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::zoomOut);
    connect(ui->actionZoomNormal, &QAction::triggered, this, &MainWindow::zoomNormal);
    connect(ui->actionShowInvisibleTiles, &QAction::toggled, prefs, &Preferences::setShowInvisibleTiles);
    connect(mPoweredPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        mSettings.setValue(
                    QLatin1String("EnvironmentPreview/Powered"), enabled);
        if (mCurrentDocument) {
            if (CellDocument *cellDoc =
                    mCurrentDocument->asCellDocument())
                cellDoc->scene()->setPoweredPreviewEnabled(enabled);
        }
    });
    connect(mSnowPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        mSettings.setValue(
                    QLatin1String("EnvironmentPreview/Snow"), enabled);
        if (mCurrentDocument) {
            if (CellDocument *cellDoc =
                    mCurrentDocument->asCellDocument())
                cellDoc->scene()->setSnowPreviewEnabled(enabled);
        }
    });
    connect(mJumboPreviewButton, &QToolButton::toggled,
            this, [this](bool enabled) {
        mSettings.setValue(
                    QLatin1String("EnvironmentPreview/Jumbo"), enabled);
        if (mCurrentDocument) {
            if (CellDocument *cellDoc =
                    mCurrentDocument->asCellDocument())
                cellDoc->scene()->setJumboPreviewEnabled(enabled);
        }
    });

    connect(ui->actionLotPackViewer, &QAction::triggered, this, &MainWindow::lotpackviewer);
    connect(ui->actionLootInspector, &QAction::triggered, this, &MainWindow::lootInspector);
    connect(ui->actionGenerateBiomeMap, &QAction::triggered,
            this, &MainWindow::generateBiomeMap);
    connect(ui->actionTerrainImageEditor, &QAction::triggered,
            this, &MainWindow::terrainImageEditor);
    mChunkDataOverrideAction = new QAction(
                tr("Native256 Chunk Data Overrides..."), this);
    mChunkDataOverrideAction->setToolTip(
                tr("Paint optional per-square values merged into chunkdata during Generate Lots"));
    ui->menuTerrainEnvironment->addAction(mChunkDataOverrideAction);
    connect(mChunkDataOverrideAction, &QAction::triggered,
            this, &MainWindow::chunkDataOverrideEditor);
    connect(ui->actionImportOpenStreetMapTerrain, &QAction::triggered,
            this, &MainWindow::importOpenStreetMapTerrain);
    connect(ui->actionWorldGenPreview, &QAction::triggered,
            this, &MainWindow::worldGenPreview);
    connect(ui->actionWorldGenPrefabEditor, &QAction::triggered,
            this, &MainWindow::worldGenPrefabEditor);
    QAction *tilesetCleanupAction = new QAction(
                tr("Project Doctor: Tiles and Paths..."), this);
    tilesetCleanupAction->setToolTip(
                tr("Check Build 42 TMX maps, TBX buildings, dependencies, "
                   "and tile paths; preview safe fixes and create backups"));
    ui->menuProjectUtilities->insertAction(
                ui->actionLotPackViewer, tilesetCleanupAction);
    connect(tilesetCleanupAction, &QAction::triggered,
            this, &MainWindow::tilesetCleanup);
//    connect(ui->actionReadOldWaterDotLua, &QAction::triggered, this, &MainWindow::readOldWaterDotLua);

    QAction *aboutPZWorldEd = new QAction(tr("About PZWorldEd"), this);
    aboutPZWorldEd->setMenuRole(QAction::AboutRole);
    ui->helpMenu->insertAction(ui->actionAboutQt, aboutPZWorldEd);
    connect(aboutPZWorldEd, &QAction::triggered, this, [this]() {
        showPZToolsAbout(this, tr("PZWorldEd"), false);
    });
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    connect(docman(), &DocumentManager::documentAdded, this, &MainWindow::documentAdded);
    connect(docman(), &DocumentManager::documentAboutToClose, this, &MainWindow::documentAboutToClose);
    connect(docman(), &DocumentManager::currentDocumentChanged, this, &MainWindow::currentDocumentChanged);

    initActionManager();
    QString CONTEXT_TOOL = QStringLiteral("Tool");
    QString CATEGORY_TOOL_WORLD = QStringLiteral("World");
    QString CATEGORY_TOOL_CELL = QStringLiteral("Cell");
    QString CATEGORY_TOOL_OBJECT = QStringLiteral("Object");
    QString CATEGORY_TOOL_INGAME_MAP = QStringLiteral("InGameMap");
    QString CATEGORY_TOOL_OTHER = QStringLiteral("Other");

    ToolManager *toolManager = ToolManager::instance();
    toolManager->registerTool(WorldCellTool::instance(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_WORLD, QStringLiteral(""));
    toolManager->registerTool(PasteCellsTool::instance(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_WORLD, QStringLiteral(""));
    toolManager->registerTool(ZombieHeatMapTool::instance(), mActionManager,
                              CONTEXT_TOOL, CATEGORY_TOOL_WORLD,
                              QStringLiteral("Tool.World.PaintZombieHeatmap"));
    toolManager->registerTool(BiomeMapTool::instance(), mActionManager,
                              CONTEXT_TOOL, CATEGORY_TOOL_WORLD,
                              QStringLiteral("Tool.World.PaintBiomemapBiome"));
#ifdef ROAD_UI
    toolManager->registerTool(WorldSelectMoveRoadTool::instance());
    toolManager->registerTool(WorldCreateRoadTool::instance());
    toolManager->registerTool(WorldEditRoadTool::instance());
#endif
    toolManager->registerTool(WorldBMPTool::instance(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_WORLD, QStringLiteral(""));
    toolManager->addSeparator();
    toolManager->registerTool(SubMapTool::instance(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_CELL, QStringLiteral(""));
    toolManager->registerTool(SelectMoveObjectTool::instance(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
    toolManager->registerTool(CreateObjectTool::instance(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
#if 0
    new CreatePointObjectTool;
    toolManager->registerTool(CreatePointObjectTool::instancePtr());
#endif
    new CreatePolygonObjectTool;
    toolManager->registerTool(CreatePolygonObjectTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
#if 1
    new CreatePolylineObjectTool;
    toolManager->registerTool(CreatePolylineObjectTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
#endif
    new EditPolygonObjectTool;
    toolManager->registerTool(EditPolygonObjectTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
    new SpawnPointTool;
    toolManager->registerTool(SpawnPointTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
    new RoomToneTool;
    toolManager->registerTool(RoomToneTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_OBJECT, QStringLiteral(""));
#ifdef ROAD_UI
    toolManager->registerTool(CellSelectMoveRoadTool::instance());
    toolManager->registerTool(CellCreateRoadTool::instance());
    toolManager->registerTool(CellEditRoadTool::instance());
#endif
    new CreateInGameMapPointTool;
    new CreateInGameMapPolygonTool;
    new CreateInGameMapPolylineTool;
    new CreateInGameMapRectangleTool;
    new EditInGameMapFeatureTool;
    toolManager->addSeparator();
    toolManager->registerTool(CreateInGameMapPointTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_INGAME_MAP, QStringLiteral("Tool.InGameMap.CreatePoint"));
    toolManager->registerTool(CreateInGameMapPolygonTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_INGAME_MAP, QStringLiteral("Tool.InGameMap.CreatePolygon"));
    toolManager->registerTool(CreateInGameMapPolylineTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_INGAME_MAP, QStringLiteral("Tool.InGameMap.CreatePolyline"));
    toolManager->registerTool(CreateInGameMapRectangleTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_INGAME_MAP, QStringLiteral("Tool.InGameMap.CreateRectangle"));
    toolManager->registerTool(EditInGameMapFeatureTool::instancePtr(), mActionManager, CONTEXT_TOOL, CATEGORY_TOOL_INGAME_MAP, QStringLiteral("Tool.InGameMap.EditFeature"));
    addToolBar(toolManager->toolBar());

    ZombieHeatMapTool *heatTool = ZombieHeatMapTool::instance();
    heatTool->setBrushRadius(mSettings.value(
                                QLatin1String("ZombieHeatmap/BrushRadius"), 1).toInt());
    heatTool->setIntensity(mSettings.value(
                              QLatin1String("ZombieHeatmap/Intensity"), 6).toInt());
    heatTool->setPreviewB42x40(mSettings.value(
                                  QLatin1String("ZombieHeatmap/PreviewB42x40"),
                                  true).toBool());
    QToolBar *toolsBar = toolManager->toolBar();
    QList<QAction*> heatControlActions;
    heatControlActions += toolsBar->addSeparator();
    QLabel *radiusLabel = new QLabel(tr("Heat radius:"), toolsBar);
    heatControlActions += toolsBar->addWidget(radiusLabel);
    QSpinBox *radiusSpin = new QSpinBox(toolsBar);
    radiusSpin->setRange(0, 64);
    radiusSpin->setValue(heatTool->brushRadius());
    radiusSpin->setSuffix(tr(" px"));
    radiusSpin->setToolTip(tr("Brush radius in heatmap samples. "
                              "One sample covers 10x10 squares in a legacy "
                              "project or 8x8 squares in a native-256 project."));
    heatControlActions += toolsBar->addWidget(radiusSpin);
    QLabel *intensityLabel = new QLabel(tr("Raw intensity:"), toolsBar);
    heatControlActions += toolsBar->addWidget(intensityLabel);
    QSpinBox *intensitySpin = new QSpinBox(toolsBar);
    intensitySpin->setRange(0, 255);
    intensitySpin->setValue(heatTool->intensity());
    intensitySpin->setToolTip(tr("Raw red-channel value written to the PNG (0-255)."));
    heatControlActions += toolsBar->addWidget(intensitySpin);
    QLabel *hexLabel = new QLabel(QStringLiteral("0x%1")
                                  .arg(heatTool->intensity(), 2, 16,
                                       QLatin1Char('0')).toUpper(), toolsBar);
    hexLabel->setMinimumWidth(hexLabel->fontMetrics()
                              .horizontalAdvance(QStringLiteral("0xFF")) + 8);
    heatControlActions += toolsBar->addWidget(hexLabel);
    QCheckBox *previewCheck = new QCheckBox(tr("B42 preview x40"), toolsBar);
    previewCheck->setChecked(heatTool->previewB42x40());
    previewCheck->setToolTip(tr("Amplifies only the WorldView preview by 40, "
                                "matching the B42 debug renderer. "
                                "Saved values remain raw 0-255."));
    heatControlActions += toolsBar->addWidget(previewCheck);
    QPushButton *expandButton = new QPushButton(tr("Expand to world"), toolsBar);
    expandButton->setToolTip(tr("Zero-pad the Zombie Heatmap so it covers "
                                "every cell in the current project."));
    heatControlActions += toolsBar->addWidget(expandButton);
    connect(radiusSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, heatTool](int value) {
        heatTool->setBrushRadius(value);
        mSettings.setValue(QLatin1String("ZombieHeatmap/BrushRadius"), value);
    });
    connect(intensitySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, heatTool, hexLabel](int value) {
        heatTool->setIntensity(value);
        hexLabel->setText(QStringLiteral("0x%1")
                          .arg(value, 2, 16, QLatin1Char('0')).toUpper());
        mSettings.setValue(QLatin1String("ZombieHeatmap/Intensity"), value);
    });
    connect(previewCheck, &QCheckBox::toggled,
            this, [this, heatTool](bool checked) {
        heatTool->setPreviewB42x40(checked);
        mSettings.setValue(QLatin1String("ZombieHeatmap/PreviewB42x40"),
                           checked);
    });
    connect(expandButton, &QPushButton::clicked,
            heatTool, &ZombieHeatMapTool::expandImageToWorld);
    connect(prefs, &Preferences::showZombieSpawnImageChanged,
            heatTool, &ZombieHeatMapTool::updateEnabledState);
    connect(toolManager, &ToolManager::selectedToolChanged,
            this, [heatTool, heatControlActions](AbstractTool *selected) {
        const bool visible = selected == heatTool;
        for (QAction *action : heatControlActions)
            action->setVisible(visible);
    });
    for (QAction *action : heatControlActions)
        action->setVisible(false);
    BiomeMapTool *biomeTool = BiomeMapTool::instance();
    biomeTool->setBiomeBrushRadius(mSettings.value(
                                      QLatin1String("BiomeMap/BiomeBrushRadius"),
                                      mSettings.value(
                                          QLatin1String("BiomeMap/BrushRadius"),
                                          4)).toInt());
    biomeTool->setZoneBrushRadius(mSettings.value(
                                     QLatin1String("BiomeMap/ZoneBrushRadius"),
                                     0).toInt());
    biomeTool->setBiomeValue(mSettings.value(
                                QLatin1String("BiomeMap/BiomeValue"), 171).toInt());
    biomeTool->setZoneValue(mSettings.value(
                               QLatin1String("BiomeMap/ZoneValue"), 64).toInt());
    biomeTool->setPaintChannel(
                mSettings.value(QLatin1String("BiomeMap/PaintChannel"), 0)
                .toInt() == 1
                ? BiomeMapTool::ZoneChannel
                : BiomeMapTool::BiomeChannel);
    QList<QAction*> biomeControlActions;
    biomeControlActions += toolsBar->addSeparator();
    QLabel *biomeModeLabel = new QLabel(tr("Paint:"), toolsBar);
    biomeControlActions += toolsBar->addWidget(biomeModeLabel);
    QComboBox *biomeMode = new QComboBox(toolsBar);
    biomeMode->addItem(tr("Biome (red channel)"),
                       int(BiomeMapTool::BiomeChannel));
    biomeMode->addItem(tr("Zone (green channel)"),
                       int(BiomeMapTool::ZoneChannel));
    biomeMode->setCurrentIndex(
                biomeMode->findData(int(biomeTool->paintChannel())));
    biomeMode->setToolTip(
                tr("Biome mode paints only red. Zone mode paints only green "
                   "and always fills complete 8 x 8 chunks."));
    biomeControlActions += toolsBar->addWidget(biomeMode);
    QLabel *biomeRadiusLabel = new QLabel(toolsBar);
    biomeControlActions += toolsBar->addWidget(biomeRadiusLabel);
    QSpinBox *biomeRadiusSpin = new QSpinBox(toolsBar);
    biomeControlActions += toolsBar->addWidget(biomeRadiusSpin);
    QLabel *biomePaletteLabel = new QLabel(toolsBar);
    biomeControlActions += toolsBar->addWidget(biomePaletteLabel);
    QComboBox *biomePalette = new QComboBox(toolsBar);
    biomeControlActions += toolsBar->addWidget(biomePalette);
    auto updateBiomeControls = [this, biomeTool, biomeRadiusLabel,
            biomeRadiusSpin, biomePaletteLabel, biomePalette]() {
        const bool zoneMode =
                biomeTool->paintChannel() == BiomeMapTool::ZoneChannel;
        biomeRadiusLabel->setText(zoneMode
                                  ? tr("Chunk radius:")
                                  : tr("Biome radius:"));
        biomePaletteLabel->setText(zoneMode ? tr("Zone:") : tr("Biome:"));
        {
            const QSignalBlocker blocker(biomeRadiusSpin);
            biomeRadiusSpin->setRange(0, zoneMode ? 16 : 128);
            biomeRadiusSpin->setSuffix(zoneMode ? tr(" chunks") : tr(" px"));
            biomeRadiusSpin->setValue(biomeTool->brushRadius());
            biomeRadiusSpin->setToolTip(
                        zoneMode
                        ? tr("Radius in 8 x 8 chunks. Zero paints exactly "
                             "one complete chunk.")
                        : tr("Brush radius in map-square Biomemap pixels."));
        }
        const QSignalBlocker blocker(biomePalette);
        biomePalette->clear();
        for (const BiomeMapImageProcessor::PaletteEntry &entry :
             BiomeMapImageProcessor::palette()) {
            QPixmap swatch(16, 16);
            swatch.fill(entry.color);
            QString label;
            if (zoneMode) {
                label = tr("%1 (ID %2)").arg(entry.zone).arg(entry.value);
                if (entry.value == 254)
                    label += tr(" [Dirt mapping]");
            } else {
                label = tr("%1 (ID %2)").arg(entry.name).arg(entry.value);
            }
            if (!entry.enabledByDefault)
                label += tr(" [map override]");
            const int itemIndex = biomePalette->count();
            biomePalette->addItem(QIcon(swatch), label, entry.value);
            biomePalette->setItemData(
                        itemIndex,
                        zoneMode
                        ? tr("Green pixel ID: %1\nForaging zone: %2%3\n"
                             "The red Biome channel is preserved.")
                          .arg(entry.value)
                          .arg(entry.zone)
                          .arg(entry.enabledByDefault
                               ? QString()
                               : tr("\nAvailability: map-specific "
                                    "WorldGenOverride.lua required"))
                        : tr("Red pixel ID: %1\nBiome: %2\nOre selector: %3\n"
                             "Ore meaning: %4%5\nThe green Zone channel is "
                             "preserved.")
                          .arg(entry.value)
                          .arg(entry.biome.isEmpty()
                               ? tr("(none)") : entry.biome)
                          .arg(entry.ore.isEmpty()
                               ? tr("(none)") : entry.ore)
                          .arg(BiomeMapImageProcessor::oreSelectorDescription(
                                   entry.ore))
                          .arg(entry.enabledByDefault
                               ? QString()
                               : tr("\nAvailability: map-specific "
                                    "WorldGenOverride.lua required")),
                        Qt::ToolTipRole);
        }
        const int paletteIndex = biomePalette->findData(
                    biomeTool->paintValue());
        if (paletteIndex >= 0)
            biomePalette->setCurrentIndex(paletteIndex);
        biomePalette->setToolTip(
                    zoneMode
                    ? tr("Value written to the green Foraging Zone channel "
                         "in complete 8 x 8 chunks.")
                    : tr("Value written to the red Biome channel. "
                         "map_forest selects surface boulders, limestone, "
                         "or flint, not iron or copper."));
    };
    updateBiomeControls();
    QLabel *biomeOpacityLabel = new QLabel(tr("Opacity:"), toolsBar);
    biomeControlActions += toolsBar->addWidget(biomeOpacityLabel);
    QSpinBox *biomeOpacitySpin = new QSpinBox(toolsBar);
    biomeOpacitySpin->setRange(10, 100);
    biomeOpacitySpin->setSuffix(tr("%"));
    biomeOpacitySpin->setValue(qRound(prefs->biomeMapOpacity() * 100.0));
    biomeControlActions += toolsBar->addWidget(biomeOpacitySpin);
    connect(biomeRadiusSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, biomeTool](int value) {
        biomeTool->setBrushRadius(value);
        mSettings.setValue(
                    biomeTool->paintChannel() == BiomeMapTool::ZoneChannel
                    ? QLatin1String("BiomeMap/ZoneBrushRadius")
                    : QLatin1String("BiomeMap/BiomeBrushRadius"), value);
    });
    connect(biomeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, biomeTool, biomeMode, updateBiomeControls](int index) {
        biomeTool->setPaintChannel(
                    BiomeMapTool::PaintChannel(
                        biomeMode->itemData(index).toInt()));
        mSettings.setValue(QLatin1String("BiomeMap/PaintChannel"),
                           int(biomeTool->paintChannel()));
        updateBiomeControls();
    });
    connect(biomePalette, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, biomeTool, biomePalette](int index) {
        if (index < 0)
            return;
        const int value = biomePalette->itemData(index).toInt();
        if (biomeTool->paintChannel() == BiomeMapTool::ZoneChannel) {
            biomeTool->setZoneValue(value);
            mSettings.setValue(QLatin1String("BiomeMap/ZoneValue"), value);
        } else {
            biomeTool->setBiomeValue(value);
            mSettings.setValue(QLatin1String("BiomeMap/BiomeValue"), value);
        }
    });
    connect(biomeOpacitySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [prefs](int value) {
        prefs->setBiomeMapOpacity(value / 100.0);
    });
    connect(prefs, &Preferences::showBiomeMapChanged,
            biomeTool, &BiomeMapTool::updateEnabledState);
    connect(toolManager, &ToolManager::selectedToolChanged,
            this, [biomeTool, biomeControlActions](AbstractTool *selected) {
        const bool visible = selected == biomeTool;
        for (QAction *action : biomeControlActions)
            action->setVisible(visible);
    });
    for (QAction *action : biomeControlActions)
        action->setVisible(false);
    // Do this after all ToolManager::register() calls.
    QString error;
    mActionManager->load(error);
    mActionManager->emitShortcutEditedForAllActions();

    ui->currentLevelButton->setMenu(mCurrentLevelMenu);
    connect(mCurrentLevelMenu, &QMenu::aboutToShow, this, &MainWindow::aboutToShowCurrentLevelMenu);
    connect(mCurrentLevelMenu, &QMenu::triggered, this, &MainWindow::currentLevelMenuTriggered);

    ui->objectGroupButton->setMenu(mObjectGroupMenu);
    connect(mObjectGroupMenu, &QMenu::aboutToShow,
            this, &MainWindow::aboutToShowObjGrpMenu);
    connect(mObjectGroupMenu, &QMenu::triggered,
            this, &MainWindow::objGrpMenuTriggered);

    ui->documentTabWidget->clear(); // TODO: remove tabs from .ui
    ui->documentTabWidget->setDocumentMode(true);
    ui->documentTabWidget->setTabsClosable(true);

    connect(ui->documentTabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::currentDocumentTabChanged);
    connect(ui->documentTabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::documentCloseRequested);

//    enableDeveloperFeatures();

    Progress::instance()->setMainWindow(this);

    mViewHint.valid = false;

    updateActions();

}

MainWindow::~MainWindow()
{
#if 1
    // MapComposite's destructor calls MapManager::removeReferenceToMap().
    // But the MapComposite's aren't deleted till the base constructor has
    // run, which causes MapManager to be *recreated* and recreate its threads.
    // I think this leads to the application not terminating promptly.
    ToolManager::instance()->toolBar()->setParent(0);
#else
    DocumentManager::deleteInstance();
    ToolManager::deleteInstance();
    Preferences::deleteInstance();
    MapImageManager::deleteInstance();
    MapManager::deleteInstance();
    TileMetaInfoMgr::deleteInstance();
    TilesetManager::deleteInstance();
#endif
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    writeSettings();

    if (mLotPackWindow)
        mLotPackWindow->close();

    if (confirmAllSave()) {
        if (mKeyboardShortcutWindow != nullptr) {
            mKeyboardShortcutWindow->close();
        }
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    switch (event->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        retranslateUi();
        break;
    default:
        break;
    }
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("PZWorldEd - %1").arg(PZToolsBuild::label()));
}

void MainWindow::newWorld()
{
    NewWorldDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QSize size = dialog.worldSize();

    World *newWorld = new World(size.width(), size.height(), dialog.gridFormat());
    DefaultsFile::newWorld(newWorld);
    WorldDocument *newDoc = new WorldDocument(newWorld);
    docman()->addDocument(newDoc);
}

void MainWindow::editCell()
{
    Document *doc = docman()->currentDocument();
    if (WorldDocument *worldDoc = doc->asWorldDocument()) {
        foreach (WorldCell *cell, worldDoc->selectedCells())
            worldDoc->editCell(cell);
    }
}

void MainWindow::goToXY()
{
    Document *doc = docman()->currentDocument();
    WorldDocument *worldDoc = doc->asWorldDocument();
    QPoint initial;
    if (worldDoc) {
        const int cellSize = worldDoc->world()->cellSize();
        if (worldDoc->selectedCellCount() == 1)
            initial = worldDoc->selectedCells().first()->pos() * cellSize;
    } else {
        worldDoc = doc->asCellDocument()->worldDocument();
        initial = doc->asCellDocument()->cell()->pos()
                * worldDoc->world()->cellSize();
    }

    GoToDialog d(worldDoc->world(), initial, this);
    if (d.exec() != QDialog::Accepted)
        return;

    const int cellSize = worldDoc->world()->cellSize();
    if (WorldCell *cell = worldDoc->world()->cellAt(
                d.worldX() / cellSize, d.worldY() / cellSize)) {
        worldDoc->editCell(cell);
        if (CellDocument *cellDoc = docman()->findDocument(cell)) {
            docman()->setCurrentDocument(cellDoc);
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
            QPointF tilePos(d.worldX() - cell->x() * cellSize,
                            d.worldY() - cell->y() * cellSize);
            cellDoc->view()->centerOn(cellDoc->scene()->renderer()->tileToPixelCoords(tilePos));
        }
    }
}

void MainWindow::setShowGrid(bool show)
{
    if (mCurrentDocument && mCurrentDocument->isWorldDocument())
        Preferences::instance()->setShowWorldGrid(show);
    else if (mCurrentDocument && mCurrentDocument->isCellDocument())
        Preferences::instance()->setShowCellGrid(show);
}

void MainWindow::documentAdded(Document *doc)
{
    if (CellDocument *cellDoc = doc->asCellDocument()) {
        CellView *view = new CellView(this);
        CellScene *scene = new CellScene(view);
        view->setScene(scene);
        scene->setDocument(cellDoc);
        connect(scene, &CellScene::partialChunkSelectionChanged,
                this, &MainWindow::updateActions);
        connect(scene, &CellScene::partialChunkSaveFailed,
                this, [this](const QString &message) {
            QMessageBox::warning(this, tr("Partial Chunks"), message);
        });
        // Handle failure to load the map.
        // Currently this never happens as a placeholder map
        // will be used in case of failure.
        if (scene->mapComposite() == 0) {
            delete scene;
            delete view;
            docman()->setFailedToAdd();
            return;
        }
        doc->setView(view);
        scene->setPoweredPreviewEnabled(mSettings.value(
            QLatin1String("EnvironmentPreview/Powered"), false).toBool());
        scene->setSnowPreviewEnabled(mSettings.value(
            QLatin1String("EnvironmentPreview/Snow"), false).toBool());
        scene->setJumboPreviewEnabled(mSettings.value(
            QLatin1String("EnvironmentPreview/Jumbo"), false).toBool());
        cellDoc->setScene(scene);

        int pos = docman()->documents().indexOf(doc);
        ui->documentTabWidget->insertTab(pos, view,
            tr("Cell %1,%2").arg(cellDoc->cell()->displayPos().x()).arg(cellDoc->cell()->displayPos().y()));
        ui->documentTabWidget->setTabToolTip(pos, doc->fileName());

        if (mViewHint.valid) {
            view->zoomable()->setScale(mViewHint.scale);
            view->centerOn(mViewHint.scrollX, mViewHint.scrollY);
        } else {
            QPointF center = scene->renderer()->tileToPixelCoords(
                        scene->map()->width() / 2.0,
                        scene->map()->height() / 2.0);
            view->centerOn(center);
        }
    }
    if (WorldDocument *worldDoc = doc->asWorldDocument()) {
        WorldView *view = new WorldView(this);
        WorldScene *scene = new WorldScene(worldDoc, view);
        view->setScene(scene);
        doc->setView(view);

        QShortcut *selectAllShortcut = new QShortcut(QKeySequence::SelectAll, view);
        connect(selectAllShortcut, &QShortcut::activated, worldDoc, [worldDoc]() {
            worldDoc->setSelectedCells(worldDoc->world()->cells().toList());
        });
        int pos = docman()->documents().indexOf(doc);
        ui->documentTabWidget->insertTab(pos, view, tr("The World"));
        ui->documentTabWidget->setTabToolTip(pos, doc->fileName());

        if (mViewHint.valid) {
            view->zoomable()->setScale(mViewHint.scale);
            view->centerOn(mViewHint.scrollX, mViewHint.scrollY);
        } else
            view->centerOn(scene->cellToPixelCoords(0, 0));
    }
}

void MainWindow::documentAboutToClose(int index, Document *doc)
{
    // If there was an error adding the document (such as failure to
    // load the map of a cell) then there won't be a tab yet.

    // At this point, the document is not in the DocumentManager's list of documents.
    // Removing the current tab will cause another tab to be selected and
    // the current document to change.
    ui->documentTabWidget->removeTab(index);

    // Delete the QGraphicsView (and QGraphicsScene owned by the view)
    delete doc->view();
}

void MainWindow::currentDocumentTabChanged(int tabIndex)
{
    docman()->setCurrentDocument(tabIndex);
}

void MainWindow::currentDocumentChanged(Document *doc)
{
    if (mCurrentDocument) {
        mCurrentDocument->disconnect(this);
        mZoomable->connectToComboBox(0);
        mZoomable->disconnect(this);
        mZoomable = 0;
    }

    mCurrentDocument = doc;

    if (mCurrentDocument) {
        if (CellDocument *cellDoc = doc->asCellDocument()) {
            connect(cellDoc, &CellDocument::currentLevelChanged, this, &MainWindow::updateActions);
            connect(cellDoc, &CellDocument::selectedLotsChanged, this, &MainWindow::updateActions);
            connect(cellDoc, &CellDocument::selectedObjectsChanged, this, &MainWindow::updateActions);
            connect(cellDoc, &CellDocument::selectedObjectPointsChanged, this, &MainWindow::updateActions);
            connect(cellDoc, &CellDocument::currentObjectGroupChanged,
                    this, &MainWindow::updateActions);
            connect(cellDoc->view(), &BaseGraphicsView::statusBarCoordinatesChanged,
                    this, &MainWindow::setStatusBarCoords);
            connect(cellDoc->worldDocument(),
                    &WorldDocument::objectGroupNameChanged,
                    this, &MainWindow::updateActions);
            connect(cellDoc->worldDocument(), &WorldDocument::inGameMapGeometryChanged, this, &MainWindow::updateActions);
            connect(cellDoc->worldDocument(), &WorldDocument::selectedInGameMapFeaturesChanged, this, &MainWindow::updateActions);
            connect(cellDoc->worldDocument(), &WorldDocument::selectedInGameMapPointsChanged, this, &MainWindow::updateActions);
            connect(cellDoc->worldDocument(), &WorldDocument::generateLotSettingsChanged,
                    this, &MainWindow::generateLotSettingsChanged);
#ifdef ROAD_UI
            connect(cellDoc->worldDocument(), SIGNAL(selectedRoadsChanged()),
                    SLOT(updateActions()));
#endif
            connect(cellDoc, &CellDocument::selectedInGameMapFeaturesChanged, this, &MainWindow::updateActions);
            connect(cellDoc, &CellDocument::selectedInGameMapPointsChanged, this, &MainWindow::updateActions);
        }

        if (WorldDocument *worldDoc = doc->asWorldDocument()) {
            connect(worldDoc, &WorldDocument::selectedCellsChanged, this, &MainWindow::updateActions);
            connect(worldDoc, &WorldDocument::selectedLotsChanged, this, &MainWindow::updateActions);
            connect(worldDoc, &WorldDocument::selectedObjectsChanged, this, &MainWindow::updateActions);
            connect(worldDoc, &WorldDocument::selectedInGameMapFeaturesChanged, this, &MainWindow::updateActions);
            connect(worldDoc->view(), &BaseGraphicsView::statusBarCoordinatesChanged,
                    this, &MainWindow::setStatusBarCoords);
            connect(worldDoc, &WorldDocument::generateLotSettingsChanged,
                    this, &MainWindow::generateLotSettingsChanged);
#ifdef ROAD_UI
            connect(worldDoc, SIGNAL(selectedRoadsChanged()),
                    SLOT(updateActions()));
#endif
            connect(worldDoc, &WorldDocument::selectedBMPsChanged,
                    this, &MainWindow::updateActions);
        }

        mLotsDock->setDocument(doc);
        mInGameMapDock->setDocument(doc);
        mObjectsDock->setDocument(doc);
        mSearchDock->setDocument(doc);
        mStreetNamesDock->setDocument(doc);
        mRegionsDock->setDocument(doc);
#ifdef ROAD_UI
        mRoadsDock->setDocument(doc);
#endif

        mZoomable = mCurrentDocument->view()->zoomable();
        mZoomable->connectToComboBox(mZoomComboBox);
        connect(mZoomable, &Zoomable::scaleChanged, this, &MainWindow::updateZoom);

        // May be a WorldDocument, that's ok
        CellDocument *cellDoc = mCurrentDocument->asCellDocument();
        mLayersDock->setCellDocument(cellDoc);
    } else {
        mLayersDock->setCellDocument(0);
        mLotsDock->clearDocument();
        mInGameMapDock->clearDocument();
        mObjectsDock->clearDocument();
        mSearchDock->clearDocument();
        mStreetNamesDock->clearDocument();
        mRegionsDock->clearDocument();
#ifdef ROAD_UI
        mRoadsDock->clearDocument();
#endif
    }

    ToolManager::instance()->setScene(doc ? doc->view()->scene() : 0);
    mPropertiesDock->setDocument(doc);

    ui->documentTabWidget->setCurrentIndex(docman()->indexOf(doc));

    updateActions();
    updateWindowTitle();
}

void MainWindow::documentCloseRequested(int tabIndex)
{
    Document *doc = docman()->documentAt(tabIndex);
    if (doc->isModified()) {
        docman()->setCurrentDocument(tabIndex);
        if (!confirmSave())
            return;
    }

    WorldDocument *worldDoc = 0;
    if (doc->asCellDocument() && docman()->currentDocument() == doc)
        worldDoc = doc->asCellDocument()->worldDocument();

    docman()->closeDocument(tabIndex);

    if (worldDoc)
        docman()->setCurrentDocument(worldDoc);
}

void MainWindow::selectLevelAbove()
{
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        int level = cellDoc->currentLevel();
        if (level < MAX_WORLD_LEVEL /*cellDoc->scene()->mapComposite()->maxLevel()*/)
            cellDoc->setCurrentLevel(level + 1);
    }
}

void MainWindow::selectLevelBelow()
{
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        int level = cellDoc->currentLevel();
        if (level > MIN_WORLD_LEVEL)
            cellDoc->setCurrentLevel(level - 1);
    }
}

void MainWindow::zoomIn()
{
    if (mZoomable)
        mZoomable->zoomIn();
}

void MainWindow::zoomOut()
{
    if (mZoomable)
        mZoomable->zoomOut();
}

void MainWindow::zoomNormal()
{
    if (mZoomable)
        mZoomable->resetZoom();
}

DocumentManager *MainWindow::docman() const
{
    return DocumentManager::instance();
}

void MainWindow::openFile()
{
    QString filter = tr("All Files (*)");
    filter += QLatin1String(";;");

    QString selectedFilter = tr("PZWorldEd world files (*.pzw)");
    filter += selectedFilter;

    QStringList fileNames =
            QFileDialog::getOpenFileNames(this, tr("Open World"),
                                          Preferences::instance()->openFileDirectory(),
                                          filter, &selectedFilter);
    if (fileNames.isEmpty()) {
        return;
    }

    Preferences::instance()->setOpenFileDirectory(QFileInfo(fileNames[0]).absolutePath());

    foreach (const QString &fileName, fileNames) {
        openFile(fileName/*, mapReader*/);
    }
}

bool MainWindow::openFile(const QString &fileName)
{
    if (fileName.isEmpty())
        return false;

    // Select existing document if this file is already open
    int documentIndex = docman()->findDocument(fileName);
    if (documentIndex != -1) {
        ui->documentTabWidget->setCurrentIndex(documentIndex);
        return true;
    }

    QFileInfo fileInfo(fileName);
    PROGRESS progress(tr("Reading %1").arg(fileInfo.fileName()));

    WorldReader reader;
    World *world = reader.readWorld(fileName);
    if (!world) {
        QMessageBox::critical(this, tr("Error Reading World"),
                              reader.errorString());
        return false;
    }

    DefaultsFile::oldWorld(world);

    qint64 totalLots = 0;
    qint64 totalObjects = 0;
    qint64 totalInGameMapFeatures = 0;
    qint64 osmProxyLots = 0;
    qint64 osmGeneratedObjects = 0;
    qint64 osmInGameMapFeatures = 0;
    for (WorldCell *cell : world->cells()) {
        totalLots += cell->lots().size();
        totalObjects += cell->objects().size();
        totalInGameMapFeatures += cell->inGameMap().features().size();
        for (WorldCellLot *lot : cell->lots()) {
            const QString mapName = QDir::fromNativeSeparators(
                        lot->mapName());
            if (mapName.contains(QStringLiteral("/osm-generated/"),
                                 Qt::CaseInsensitive)
                    || mapName.startsWith(QStringLiteral("osm-generated/"),
                                          Qt::CaseInsensitive)) {
                ++osmProxyLots;
            }
        }
        for (WorldCellObject *object : cell->objects()) {
            if (object->group()
                    && object->group()->name()
                    == QLatin1String("OSM Generated")) {
                ++osmGeneratedObjects;
            }
        }
        for (InGameMapFeature *feature : cell->inGameMap().features()) {
            if (feature->properties().contains(
                        QStringLiteral("source"), QStringLiteral("osm"))) {
                ++osmInGameMapFeatures;
            }
        }
    }
    qInfo().noquote()
            << QStringLiteral(
                   "World project composition: cells %1, lots %2, objects %3, InGameMap features %4")
               .arg(world->width() * world->height())
               .arg(totalLots).arg(totalObjects)
               .arg(totalInGameMapFeatures);
    if (osmProxyLots > 0 || osmGeneratedObjects > 0
            || osmInGameMapFeatures > 0) {
        const bool highRisk = osmProxyLots > 2000
                || osmGeneratedObjects > 5000
                || osmInGameMapFeatures > 10000;
        const QString profile = QStringLiteral(
                    "OSM project load profile: proxy lots %1, generated zone objects %2, OSM InGameMap features %3, interactive risk %4")
                .arg(osmProxyLots).arg(osmGeneratedObjects)
                .arg(osmInGameMapFeatures)
                .arg(highRisk ? QStringLiteral("high")
                              : QStringLiteral("normal"));
        if (highRisk)
            qWarning().noquote() << profile;
        else
            qInfo().noquote() << profile;
    }
    docman()->addDocument(new WorldDocument(world, fileName));
    if (docman()->failedToAdd())
        return false;
//    setRecentFile(fileName);
    return true;
}

void MainWindow::openLastFiles()
{
    PROGRESS progress(tr("Restoring session"), this);

    // MapImageManager's threads will load in the thumbnail images in the
    // background.  But defer updating the display with those images until
    // all the documents are loaded.
//    MapImageManagerDeferral defer;

    mSettings.beginGroup(QLatin1String("openFiles"));

    int count = mSettings.value(QLatin1String("count")).toInt();

    for (int i = 0; i < count; i++) {
        QString key = QString::number(i); // openFiles/N/...
        if (!mSettings.childGroups().contains(key))
            continue;
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        mSettings.beginGroup(key);
        QString path = mSettings.value(QLatin1String("file")).toString();

        // Restore camera to the previous position
        qreal scale = mSettings.value(QLatin1String("scale")).toDouble();
        const int hor = mSettings.value(QLatin1String("scrollX")).toInt();
        const int ver = mSettings.value(QLatin1String("scrollY")).toInt();
        setDocumentViewHint(qMax(scale, 0.06), hor, ver);

        // This "recent file" could be a world or just a cell.
        // We require that the world be already open when editing a cell.
        if (openFile(path)) {
            if (mSettings.contains(QLatin1String("cellX"))) {
                int cellX = mSettings.value(QLatin1String("cellX")).toInt();
                int cellY = mSettings.value(QLatin1String("cellY")).toInt();
                WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
                if (!worldDoc) {
                    if (CellDocument *currentCell = mCurrentDocument->asCellDocument())
                        worldDoc = currentCell->worldDocument();
                }
                if (!worldDoc) {
                    qWarning() << "Skipping saved cell because its world is not open:"
                               << path << cellX << cellY;
                    mSettings.endGroup();
                    continue;
                }
                if (WorldCell *cell = worldDoc->world()->cellAt(cellX, cellY)) {
                    progress.update(tr("Loading cell %1,%2").arg(cellX).arg(cellY));
                    CellDocument *cellDoc = new CellDocument(worldDoc, cell);
                    docman()->addDocument(cellDoc); // switches document
                    // If the cell map couldn't be loaded, the document
                    // will have been deleted.
                    if (docman()->failedToAdd()) {
                        mSettings.endGroup();
                        continue;
                    }
                    int layerIndex = mSettings.value(QLatin1String("currentLayer")).toInt();
                    if (!cellDoc->scene() || !cellDoc->scene()->map()) {
                        qWarning() << "Skipping saved cell with no loaded map:"
                                   << path << cellX << cellY;
                        mSettings.endGroup();
                        continue;
                    }
                    if (layerIndex >= 0 && layerIndex < cellDoc->scene()->map()->layerCount())
                        cellDoc->setCurrentLayerIndex(layerIndex);
                } else {
                    mSettings.endGroup();
                    continue;
                }
            }
        }
        mSettings.endGroup();
    }
    mViewHint.valid = false;

    int lastActiveDocument =
            mSettings.value(QLatin1String("lastActive"), -1).toInt();
    if (lastActiveDocument >= 0 && lastActiveDocument < docman()->documentCount()) {
        ui->documentTabWidget->setCurrentIndex(lastActiveDocument);
    }

    mSettings.endGroup();
}

void MainWindow::startSettingsAutoSave()
{
    if (!findChild<QTimer*>(QStringLiteral("settingsAutoSaveTimer"))) {
        QTimer *settingsSaveTimer = new QTimer(this);
        settingsSaveTimer->setObjectName(QStringLiteral("settingsAutoSaveTimer"));
        settingsSaveTimer->setInterval(5000);
        connect(settingsSaveTimer, &QTimer::timeout,
                this, &MainWindow::writeSettings);
        settingsSaveTimer->start();
    }
    if (!findChild<QTimer*>(QStringLiteral("documentAutoSaveTimer"))) {
        QTimer *documentAutoSaveTimer = new QTimer(this);
        documentAutoSaveTimer->setObjectName(
                    QStringLiteral("documentAutoSaveTimer"));
        connect(documentAutoSaveTimer, &QTimer::timeout,
                this, &MainWindow::autoSaveCurrentDocument);
        connect(Preferences::instance(),
                &Preferences::autoSaveIntervalChanged,
                this, &MainWindow::updateDocumentAutoSaveTimer);
    }
    updateDocumentAutoSaveTimer();
}

void MainWindow::updateDocumentAutoSaveTimer()
{
    QTimer *timer = findChild<QTimer*>(
                QStringLiteral("documentAutoSaveTimer"));
    if (!timer)
        return;
    const int minutes = Preferences::instance()->autoSaveIntervalMinutes();
    if (minutes <= 0 || mDocumentTransactionDepth > 0) {
        timer->stop();
        return;
    }
    timer->start(minutes * 60 * 1000);
}

void MainWindow::autoSaveCurrentDocument()
{
    if (!mCurrentDocument || mDocumentTransactionDepth > 0 ||
            QApplication::activeModalWidget() ||
            Progress::instance()->isActive())
        return;
    WorldDocument *worldDocument = mCurrentDocument->asWorldDocument();
    if (!worldDocument && mCurrentDocument->isCellDocument())
        worldDocument = mCurrentDocument->asCellDocument()->worldDocument();
    if (!worldDocument || !worldDocument->isModified() ||
            worldDocument->fileName().isEmpty())
        return;
    if (saveFile(worldDocument->fileName()))
        qInfo().noquote() << "WorldEd auto-saved"
                          << QDir::toNativeSeparators(
                                 worldDocument->fileName());
}

void MainWindow::checkpointDocumentAutoSave()
{
    if (mDocumentTransactionDepth > 0 ||
            Preferences::instance()->autoSaveIntervalMinutes() <= 0)
        return;
    QTimer *timer = findChild<QTimer*>(
                QStringLiteral("documentAutoSaveTimer"));
    if (timer)
        timer->stop();
    autoSaveCurrentDocument();
    updateDocumentAutoSaveTimer();
}

void MainWindow::beginDocumentTransaction()
{
    ++mDocumentTransactionDepth;
    if (QTimer *timer = findChild<QTimer*>(
                QStringLiteral("documentAutoSaveTimer")))
        timer->stop();
}

void MainWindow::endDocumentTransaction()
{
    Q_ASSERT(mDocumentTransactionDepth > 0);
    if (mDocumentTransactionDepth <= 0)
        return;
    --mDocumentTransactionDepth;
    if (mDocumentTransactionDepth == 0)
        updateDocumentAutoSaveTimer();
}
#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/buildingtemplates.h"
#include "BuildingEditor/buildingtmx.h"
#include "BuildingEditor/furnituregroups.h"
using namespace BuildingEditor;

// All this is needed for .tbx lots.
bool MainWindow::InitConfigFiles()
{
    // Refresh the ui before blocking while loading tilesets etc
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    // Make sure the user has chosen the Tiles directory.
    QString tilesDirectory = TileMetaInfoMgr::instance()->tilesDirectory();
    if (tilesDirectory.isEmpty() || !QDir(tilesDirectory).exists()) {
        preferencesDialog();
        tilesDirectory = TileMetaInfoMgr::instance()->tilesDirectory();
        if (tilesDirectory.isEmpty() || !QDir(tilesDirectory).exists())
            return false;
    }

    PROGRESS progress(tr("Reading Tilesets.txt"), this);

    if (!TileMetaInfoMgr::instance()->readTxt()) {
        QMessageBox::critical(this, tr("Tileset Configuration Error"),
                              tr("%1\n(while reading %2)")
                              .arg(TileMetaInfoMgr::instance()->errorString())
                              .arg(TileMetaInfoMgr::instance()->txtName()));
        return false;
    }

    progress.update(tr("Discovering installed tilesets"));

    if (!TileMetaInfoMgr::instance()->addNewTilesets(false)) {
        QMessageBox::critical(this, tr("Tileset Configuration Error"),
                              tr("%1\n(while discovering new tilesets)")
                              .arg(TileMetaInfoMgr::instance()->errorString()));
        return false;
    }
    progress.update(tr("Preparing the complete tileset catalogue"));
    const QList<Tileset *> completeTilesetCatalog =
            TileMetaInfoMgr::instance()->tilesets();
    TileMetaInfoMgr::instance()->resolveTilesets(completeTilesetCatalog);
    TilesetManager::instance()->waitForTilesets(
                completeTilesetCatalog, this);
    qInfo() << "WorldEd loaded the complete tileset catalog before project startup:"
            << completeTilesetCatalog.size() << "entries";
    qInfo().noquote() << TilesetManager::instance()->cacheMemorySummary();

    progress.update(tr("Reading BuildingTMX.txt"));

    if (!BuildingTMX::instance()->readTxt()) {
        QMessageBox::critical(this, tr("Building Configuration Error"),
                              tr("Error while reading %1\n%2")
                              .arg(BuildingTMX::instance()->txtName())
                              .arg(BuildingTMX::instance()->errorString()));
        return false;
    }

    progress.update(tr("Reading BuildingTiles.txt"));

    if (!BuildingTilesMgr::instance()->readTxt()) {
        QMessageBox::critical(this, tr("Building Tiles Error"),
                              tr("Error while reading %1\n%2")
                              .arg(BuildingTilesMgr::instance()->txtName())
                              .arg(BuildingTilesMgr::instance()->errorString()));
        return false;
    }

    progress.update(tr("Reading FurnitureGroups.txt"));

    if (!FurnitureGroups::instance()->readTxt()) {
        QMessageBox::critical(this, tr("Furniture Configuration Error"),
                              tr("Error while reading %1\n%2")
                              .arg(FurnitureGroups::instance()->txtName())
                              .arg(FurnitureGroups::instance()->errorString()));
        return false;
    }

    progress.update(tr("Reading BuildingTemplates.txt"));

    if (!BuildingTemplates::instance()->readTxt()) {
        QMessageBox::critical(this, tr("Building Templates Error"),
                              tr("Error while reading %1\n%2")
                              .arg(BuildingTemplates::instance()->txtName())
                              .arg(BuildingTemplates::instance()->errorString()));
        return false;
    }

    progress.update(tr("Reading thumbnail settings"));

    new ThumbnailSettingsMgr();
    ThumbnailSettingsMgr::instance().readTxt();

    return true;
}

void MainWindow::setStatusBarCoords(int x, int y)
{
    if (mCurrentDocument) {
        WorldCell *cell = 0;
        WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
        if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
            cell = cellDoc->cell();
            worldDoc = cellDoc->worldDocument();
        }
        if (cell) {
            const int cellSize = worldDoc->world()->cellSize();
            ui->coordinatesLabel->setText(QString::fromLatin1("Cell x,y=%1,%2")
                                          .arg(x).arg(y));
            ui->worldCoordinatesLabel->setText(QString::fromLatin1("World x,y=%3,%4")
                                          .arg(cell->displayPos().x() * cellSize + x)
                                          .arg(cell->displayPos().y() * cellSize + y));
        } else if (/*WorldDocument *worldDoc = */mCurrentDocument->asWorldDocument()) {
            QPoint worldOrigin = worldDoc->world()->getGenerateLotsSettings().worldOrigin;
            const int cellSize = worldDoc->world()->cellSize();
            x += worldOrigin.x() * cellSize;
            y += worldOrigin.y() * cellSize;
            int cellX = qFloor(x / qreal(cellSize));
            int cellY = qFloor(y / qreal(cellSize));
            ui->coordinatesLabel->setText(QString(QLatin1String("Cell x,y=%1,%2")).arg(cellX).arg(cellY));
            ui->worldCoordinatesLabel->setText(QString(QLatin1String("World x,y=%1,%2")).arg(x).arg(y));
        }
    }
}

void MainWindow::aboutToShowCurrentLevelMenu()
{
    mCurrentLevelMenu->clear();
    CellDocument *cellDoc = mCurrentDocument->asCellDocument();
    if (!cellDoc)
        return;
    QStringList items;
    foreach (CompositeLayerGroup *layerGroup, cellDoc->scene()->mapComposite()->sortedLayerGroups())
        items.prepend(QString::number(layerGroup->level()));
    foreach (QString item, items) {
        QAction *action = mCurrentLevelMenu->addAction(item);
        if (item.toInt() == cellDoc->currentLevel()) {
            action->setCheckable(true);
            action->setChecked(true);
            action->setEnabled(false);
        }
    }
}

void MainWindow::currentLevelMenuTriggered(QAction *action)
{
    CellDocument *cellDoc = mCurrentDocument->asCellDocument();
    if (!cellDoc)
        return;
    int level = action->text().toInt();
    cellDoc->setCurrentLevel(level);
}

void MainWindow::aboutToShowObjGrpMenu()
{
    mObjectGroupMenu->clear();
    CellDocument *cellDoc = mCurrentDocument->asCellDocument();
    if (!cellDoc)
        return;
    const ObjectGroupList &groups = cellDoc->world()->objectGroups();
    QAction *before = 0;
    foreach (WorldObjectGroup *og, groups) {
        QString name = og->name();
        if (name.isEmpty())
            name = tr("<none>");
        // This extra space is so the down arrow doesn't overlap the text
        name += QLatin1Char(' ');
        QAction *action = new QAction(name, mObjectGroupMenu);
        if (og == cellDoc->currentObjectGroup()) {
            action->setCheckable(true);
            action->setChecked(true);
            action->setEnabled(false);
        }
        mObjectGroupMenu->insertAction(before, action);
        before = action;
    }
}

void MainWindow::objGrpMenuTriggered(QAction *action)
{
    CellDocument *cellDoc = mCurrentDocument->asCellDocument();
    if (!cellDoc)
        return;
    int index = mObjectGroupMenu->actions().indexOf(action);
    const ObjectGroupList &groups = cellDoc->world()->objectGroups();
    index = groups.size() - index - 1;
    cellDoc->setCurrentObjectGroup(groups.at(index));
}

void MainWindow::lotpackviewer()
{
    if (!mLotPackWindow)
        mLotPackWindow = new LotPackWindow(this);

    mLotPackWindow->show();
    mLotPackWindow->activateWindow();
    mLotPackWindow->raise();
}

class FromToFile
{
public:
    bool read(const QString &fileName);

    QString errorString() const { return mError; }

    class FromTo
    {
    public:
        QStringList layers;
        QStringList from;
        QStringList to;
    };
    QList<FromTo> fromtos;

    QString mError;
};

bool FromToFile::read(const QString &fileName)
{
    SimpleFile simple;
    if (!simple.read(fileName)) {
        mError = simple.errorString();
        return false;
    }

    foreach (SimpleFileBlock b, simple.blocks) {
        SimpleFileKeyValue kv;
        if (b.name == QLatin1String("fromto")) {
            if (!b.hasValue("layers") || !b.hasValue("from") || !b.hasValue("to")) {
                mError = simple.tr("Line %1: Missing layers/from/to value.").arg(b.lineNumber);
                return false;
            }
            FromTo fromto;
            if (b.keyValue("layers", kv))
                fromto.layers = kv.values();
            if (b.keyValue("from", kv))
                fromto.from = kv.values();
            if (b.keyValue("to", kv))
                fromto.to = kv.values();
            foreach (QString tileName, fromto.from + fromto.to) {
                if (!BuildingTilesMgr::legalTileName(tileName)) {
                    mError = simple.tr("Invalid tile name '%1'").arg(tileName);
                    return false;
                }
            }

            fromtos += fromto;
        }
    }

    return true;
}

void MainWindow::FromToAll()
{
    FromToAux(false);
}

void MainWindow::FromToSelected()
{
    FromToAux(true);
}

void MainWindow::BuildingsToPNG()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc)
        worldDoc = mCurrentDocument->asCellDocument()->worldDocument();
    PNGBuildingDialog d(worldDoc->world(), this);
    d.exec();
}

void MainWindow::ZonesToPNG()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc)
        worldDoc = mCurrentDocument->asCellDocument()->worldDocument();
    PNGZonesDialog d(worldDoc->world(), this);
    d.exec();
}

void MainWindow::lootInspector()
{
    bool exists = LootWindow::hasInstance();
    if (!exists)
        new LootWindow(this);

    LootWindow::instance().show();
    LootWindow::instance().activateWindow();
    LootWindow::instance().raise();

    if (!exists) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        LootWindow::instance().setDocument(mCurrentDocument);
    }
}

void MainWindow::generateBiomeMap()
{
    WorldDocument *worldDocument = currentWorldDocument();
    if (!worldDocument
            || !ensureSavedProjectForAssetWorkflow(worldDocument))
        return;
    BiomeMapGeneratorDialog dialog(worldDocument->world(),
                                   worldDocument->fileName(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!dialog.generatedBiomeMapFile().isEmpty()) {
        GenerateLotsSettings settings =
                worldDocument->world()->getGenerateLotsSettings();
        settings.biomeMap = dialog.generatedBiomeMapFile();
        worldDocument->changeGenerateLotsSettings(settings);
    }
    if (worldDocument->view() && worldDocument->view()->scene()
            && worldDocument->view()->scene()->asWorldScene()) {
        WorldScene *scene = worldDocument->view()->scene()->asWorldScene();
        if (scene->biomeMapItem()) {
            scene->biomeMapItem()->reloadFromSettings(true);
            BiomeMapTool::instance()->updateEnabledState();
        }
    }
}
void MainWindow::terrainImageEditor()
{
    WorldDocument *worldDocument = currentWorldDocument();
    if (!worldDocument
            || !ensureSavedProjectForAssetWorkflow(worldDocument))
        return;
    TerrainImageEditorDialog dialog(worldDocument, this);
    dialog.exec();
}
void MainWindow::chunkDataOverrideEditor()
{
    WorldDocument *worldDocument = currentWorldDocument();
    if (!worldDocument)
        return;
    if (worldDocument->world()->gridFormat()
            != WorldGridFormat::Native256) {
        QMessageBox::information(
                    this, tr("Native256 Project Required"),
                    tr("Chunk data overrides are available only for projects created with the Native 256 x 256 grid."));
        return;
    }
    WorldCell *cell = nullptr;
    if (CellDocument *cellDocument = mCurrentDocument->asCellDocument()) {
        cell = cellDocument->cell();
    } else if (worldDocument->selectedCellCount() == 1) {
        cell = worldDocument->selectedCells().first();
    }
    if (!cell) {
        QMessageBox::information(
                    this, tr("Select One Cell"),
                    tr("Select exactly one Native256 cell before opening the chunk data override editor."));
        return;
    }
    if (!ensureSavedProjectForAssetWorkflow(worldDocument))
        return;
    ChunkDataOverrideDialog dialog(worldDocument, cell, this);
    dialog.exec();
}
void MainWindow::importOpenStreetMapTerrain()
{
    WorldDocument *worldDocument = currentWorldDocument();
    if (worldDocument
            && !ensureSavedProjectForAssetWorkflow(worldDocument))
        return;
    OsmTerrainImportDialog importDialog(worldDocument, this);
    if (importDialog.exec() != QDialog::Accepted)
        return;
    bool safeCityMode = false;
    const OsmTerrainImportResult &generatedResult =
            importDialog.generatedResult();
    const bool detailedCityDataRequested =
            importDialog.generatesProxyBuildings()
            || importDialog.generatesInGameMapFeatures();
    const bool largeCityImport = generatedResult.buildingCount > 2000
            || generatedResult.roadCount > 10000;
    if (detailedCityDataRequested && largeCityImport) {
        QMessageBox warning(
                    QMessageBox::Warning,
                    tr("Large OSM City Import"),
                    tr("This area contains %1 building footprints and %2 "
                       "road segments. Creating every proxy TBX and detailed "
                       "InGameMap feature would add thousands of files and "
                       "objects, make the PZW difficult to edit, and can "
                       "exhaust WorldEd while thumbnails are loaded.\n\n"
                       "Safe City Mode keeps streets.xml, major-road data, "
                       "water, compact terrain zones, rectangular road Nav "
                       "meshes, and TownZone coverage. It retains a stable "
                       "distributed sample of up to 2,048 editable proxy "
                       "buildings, including footprints that cross cell "
                       "boundaries. Use a smaller import area when every "
                       "building must remain editable.")
                    .arg(generatedResult.buildingCount)
                    .arg(generatedResult.roadCount),
                    QMessageBox::NoButton,
                    this);
        QPushButton *safeButton = warning.addButton(
                    tr("Use Safe City Mode"), QMessageBox::AcceptRole);
        warning.addButton(QMessageBox::Cancel);
        warning.setDefaultButton(safeButton);
        warning.exec();
        if (warning.clickedButton() != safeButton)
            return;
        safeCityMode = true;
        qInfo().noquote()
                << QStringLiteral(
                       "OSM Safe City Mode accepted: buildings %1, roads %2")
                   .arg(generatedResult.buildingCount)
                   .arg(generatedResult.roadCount);
    }
    const auto projectDataEnabled = [&importDialog]() {
        return importDialog.generatesStreets()
                || importDialog.generatesInGameMapFeatures()
                || importDialog.generatesProxyBuildings()
                || importDialog.generatesRoadMarkings()
                || importDialog.generatesNavZones()
                || importDialog.generatesForagingZones();
    };
    const auto projectDataOptions = [&importDialog, safeCityMode](
            const QString &projectPath, const QPoint &cellOrigin) {
        OsmProjectDataOptions options;
        options.projectFilePath = projectPath;
        options.cellOrigin = cellOrigin;
        options.generateStreets = importDialog.generatesStreets();
        options.generateInGameMapFeatures =
                importDialog.generatesInGameMapFeatures();
        options.generateProxyBuildings =
                importDialog.generatesProxyBuildings();
        options.generateRoadMarkings =
                importDialog.generatesRoadMarkings();
        options.generateNavZones = importDialog.generatesNavZones();
        options.generateForagingZones =
                importDialog.generatesForagingZones();
        options.safeCityMode = safeCityMode;
        return options;
    };
    const auto streetsFileName = [](WorldDocument *document) {
        const QString exportDirectory = document->world()
                ->getGenerateLotsSettings().exportDir;
        if (!exportDirectory.trimmed().isEmpty()) {
            return QDir(exportDirectory).absoluteFilePath(
                        QStringLiteral("streets.xml"));
        }
        return QDir(QFileInfo(document->fileName()).absolutePath())
                .absoluteFilePath(QStringLiteral("streets.xml"));
    };
    const auto backupStreetFile = [this](const QString &fileName) {
        if (!QFileInfo::exists(fileName))
            return true;
        QString backup = fileName + QStringLiteral(".osm-backup-")
                + QDateTime::currentDateTimeUtc().toString(
                    QStringLiteral("yyyyMMdd-HHmmss"));
        int suffix = 2;
        while (QFileInfo::exists(backup))
            backup = fileName + QStringLiteral(".osm-backup-")
                    + QString::number(suffix++);
        if (QFile::copy(fileName, backup))
            return true;
        QMessageBox::warning(
                    this, tr("Street Backup Failed"),
                    tr("WorldEd did not replace streets.xml because its "
                       "safety backup could not be created:\n%1")
                    .arg(QDir::toNativeSeparators(backup)));
        return false;
    };
    const auto installStreets = [this, &importDialog, &streetsFileName,
                                 &backupStreetFile](
            WorldDocument *document,
            const QVector<StreetNameRecord> &streets) {
        if (!importDialog.generatesStreets())
            return true;
        const QString fileName = streetsFileName(document);
        if (!backupStreetFile(fileName))
            return false;
        mStreetNamesDock->applySnapshot(streets, streets.isEmpty() ? -1 : 0);
        return mStreetNamesDock->saveForProject();
    };
    const auto showProjectDataSummary = [this](
            const OsmProjectDataSummary &summary) {
        QStringList zoneDetails;
        QStringList zoneTypes = summary.zoneTypeCounts.keys();
        zoneTypes.sort(Qt::CaseInsensitive);
        for (const QString &zoneType : zoneTypes) {
            zoneDetails += tr("%1: %2")
                    .arg(zoneType)
                    .arg(summary.zoneTypeCounts.value(zoneType));
        }
        QString message = tr(
                    "Generated %1 street record(s), %2 InGameMap feature(s), "
                    "%3 simple TBX building(s), %4 road-marking segment(s), "
                    "%5 Nav zone(s), and %6 typed ground zone(s).\n\n"
                    "The generated source manifest is:\n%7")
                .arg(summary.streets)
                .arg(summary.inGameMapFeatures)
                .arg(summary.proxyBuildings)
                .arg(summary.roadMarkings)
                .arg(summary.navZones)
                .arg(summary.foragingZones)
                .arg(QDir::toNativeSeparators(summary.manifestPath));
        if (!zoneDetails.isEmpty()) {
            message += tr("\n\nZone types:\n%1")
                    .arg(zoneDetails.join(QStringLiteral("\n")));
        }
        if (summary.safeCityMode) {
            message.prepend(tr("Safe City Mode was used.\n\n"));
            message += tr("\n\nSkipped %1 proxy building(s) and %2 "
                          "detailed InGameMap feature(s).")
                    .arg(summary.skippedProxyBuildings)
                    .arg(summary.skippedInGameMapFeatures);
        }
        if (!summary.warnings.isEmpty())
            message += QStringLiteral("\n\n") + summary.warnings.join(
                        QStringLiteral("\n"));
        QMessageBox::information(this, tr("OSM Project Data Generated"),
                                 message);
    };
    const auto applyProjectData = [this](
            WorldDocument *document,
            const OsmTerrainImportResult &generated,
            const OsmProjectDataOptions &options,
            const QVector<StreetNameRecord> &currentStreets,
            QVector<StreetNameRecord> *mergedStreets,
            OsmProjectDataSummary *summary,
            QString *error) {
        QProgressDialog progressDialog(
                    tr("Preparing OpenStreetMap project data..."),
                    QString(), 0, 1, this);
        progressDialog.setWindowTitle(tr("Creating OpenStreetMap Project"));
        progressDialog.setWindowModality(Qt::ApplicationModal);
        progressDialog.setCancelButton(nullptr);
        progressDialog.setMinimumDuration(0);
        progressDialog.setAutoClose(false);
        progressDialog.setAutoReset(false);
        progressDialog.resize(qMax(560, progressDialog.sizeHint().width()),
                              progressDialog.sizeHint().height());
        progressDialog.show();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        const OsmProjectProgress progress = [&progressDialog](
                int value, int maximum, const QString &message) {
            progressDialog.setRange(0, qMax(1, maximum));
            progressDialog.setLabelText(message);
            progressDialog.setValue(qBound(0, value, maximum));
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        };
        const bool succeeded = OsmProjectData::apply(
                    document, generated, options, currentStreets,
                    mergedStreets, summary, error, progress);
        progressDialog.setValue(progressDialog.maximum());
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        return succeeded;
    };
    if (importDialog.createsNewProject()) {
        const QString projectPath = importDialog.projectFilePath();
        if (projectPath.isEmpty())
            return;
        if (QFileInfo::exists(projectPath)) {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                        this, tr("Replace Existing Project"),
                        tr("The project already exists:\n%1\n\nReplace its "
                           "PZW file with the newly generated project?")
                        .arg(QDir::toNativeSeparators(projectPath)),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        if (!QDir().mkpath(QFileInfo(projectPath).absolutePath())) {
            QMessageBox::critical(
                        this, tr("Create Project"),
                        tr("The project directory could not be created:\n%1")
                        .arg(QDir::toNativeSeparators(
                                 QFileInfo(projectPath).absolutePath())));
            return;
        }
        const QSize size = importDialog.projectSize();
        World *newWorld = new World(size.width(), size.height(),
                                    importDialog.gridFormat());
        DefaultsFile::newWorld(newWorld);
        WorldDocument *newDocument = new WorldDocument(
                    newWorld, projectPath);
        TerrainImageEditorDialog editor(newDocument, this);
        if (!editor.importImages(
                    importDialog.groundImage(),
                    importDialog.vegetationImage(), QPoint(0, 0),
                    importDialog.suggestedGroundPath(),
                    importDialog.sourceMetadata())
                || !editor.saveImportedImages()) {
            delete newDocument;
            return;
        }
        QVector<StreetNameRecord> importedStreets;
        OsmProjectDataSummary projectSummary;
        if (projectDataEnabled()) {
            QString projectDataError;
            if (!applyProjectData(
                        newDocument, importDialog.generatedResult(),
                        projectDataOptions(projectPath, QPoint(0, 0)), {},
                        &importedStreets, &projectSummary,
                        &projectDataError)) {
                QMessageBox::critical(
                            this, tr("OSM Project Data Generation Failed"),
                            projectDataError);
                delete newDocument;
                return;
            }
        }
        QString error;
        if (!newDocument->save(projectPath, error)) {
            QMessageBox::critical(
                        this, tr("Create Project"),
                        tr("The terrain PNG files were saved, but the PZW "
                           "project could not be created.\n\n%1").arg(error));
            delete newDocument;
            return;
        }
        docman()->addDocument(newDocument);
        if (projectDataEnabled()) {
            installStreets(newDocument, importedStreets);
            showProjectDataSummary(projectSummary);
        }
        editor.exec();
        if (!newDocument->undoStack()->isClean())
            saveFile(projectPath);
        return;
    }
    TerrainImageEditorDialog editor(worldDocument, this);
    if (!editor.importImages(
                importDialog.groundImage(),
                importDialog.vegetationImage(),
                importDialog.cellOrigin(),
                importDialog.suggestedGroundPath(),
                importDialog.sourceMetadata())) {
        return;
    }
    if (projectDataEnabled()) {
        QVector<StreetNameRecord> mergedStreets;
        OsmProjectDataSummary projectSummary;
        QString projectDataError;
        if (!applyProjectData(
                    worldDocument, importDialog.generatedResult(),
                    projectDataOptions(worldDocument->fileName(),
                                       importDialog.cellOrigin()),
                    mStreetNamesDock->streets(), &mergedStreets,
                    &projectSummary, &projectDataError)) {
            QMessageBox::critical(
                        this, tr("OSM Project Data Generation Failed"),
                        projectDataError);
            return;
        }
        installStreets(worldDocument, mergedStreets);
        showProjectDataSummary(projectSummary);
    }
    editor.exec();
}
void MainWindow::worldGenPreview()
{
    WorldDocument *worldDocument = currentWorldDocument();
    if (!worldDocument || worldDocument->fileName().isEmpty())
        return;
    WorldGenPreviewDialog dialog(worldDocument, this);
    dialog.exec();
}
void MainWindow::worldGenPrefabEditor()
{
    WorldDocument *worldDocument = currentWorldDocument();
    if (!worldDocument || worldDocument->fileName().isEmpty())
        return;
    WorldGenPrefabDialog dialog(worldDocument, this);
    dialog.exec();
}
void MainWindow::tilesetCleanup()
{
    QString root;
    QString projectFile;
    if (WorldDocument *worldDocument = currentWorldDocument()) {
        if (!worldDocument->fileName().isEmpty()) {
            projectFile = worldDocument->fileName();
            root = QFileInfo(worldDocument->fileName()).absolutePath();
        }
    }
    if (root.isEmpty())
        root = mSettings.value(QLatin1String("TilesetCleanup/Root")).toString();
    if (root.isEmpty())
        root = QDir::currentPath();
    TilesetCleanupDialog dialog(root, projectFile, this);
    dialog.exec();
}
#include "waterflow.h"
void MainWindow::readOldWaterDotLua()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc)
        return;

    WaterFlow().readOldWaterDotLua(worldDoc);
}

#include "mapwriter.h"
#include <QScopedPointer>
void MainWindow::FromToAux(bool selectedOnly)
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc)
        return;
    FromToDialog dialog(worldDoc, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString f = dialog.rulesFile();
    if (f.isEmpty()) return;
    FromToFile file;
    if (!file.read(f)) {
        QMessageBox::warning(this, tr("Error reading from-to file"),
                             file.errorString());
        return;
    }

    if (file.fromtos.size() == 0) {
        QMessageBox::information(this, tr("Alias Fixup Error"),
                                 tr("That file has no fromto definitions!"));
        return;
    }

    QStringList fileNames;
    if (selectedOnly) {
        foreach (WorldCell *cell, worldDoc->selectedCells()) {
            f = cell->mapFilePath();
            if (f.isEmpty()) continue;
            if (fileNames.contains(f)) continue;
            fileNames += f;
        }
    } else {
        World *world = worldDoc->world();
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell *cell = world->cellAt(x, y);
                f = cell->mapFilePath();
                if (f.isEmpty()) continue;
                if (fileNames.contains(f)) continue;
                fileNames += f;
            }
        }
    }

    PROGRESS progress(tr("Making a mess of things"));

    QRandomGenerator qrand;

    foreach (QString fileName, fileNames) {
        if (MapInfo *mapInfo = MapManager::instance()->loadMap(fileName)) {
            QScopedPointer<Map> map(mapInfo->map()->clone());

            QMap<QString,TileLayer*> layerMapping;
            foreach (FromToFile::FromTo fromto, file.fromtos) {
                foreach (QString layerName, fromto.layers) {
                    int index = mapInfo->map()->indexOfLayer(layerName, Layer::TileLayerType);
                    if (index == -1) continue;
                    TileLayer *tl = map->layerAt(index)->asTileLayer();
                    layerMapping[layerName] = tl;
                }
            }

            QMap<QString,Tileset*> tilesetByName;
            foreach (Tileset *ts, map->tilesets()) {
                tilesetByName[ts->name()] = ts;
            }

            QMap<QPair<TileLayer*,Tile*>,QList<Tile*> > tileMapping;
            foreach (FromToFile::FromTo fromto, file.fromtos) {
                QString tilesetName;
                int tileID;

                QList<Tile*> fromTiles;
                foreach (QString tileName, fromto.from) {
                    BuildingTilesMgr::parseTileName(tileName, tilesetName, tileID);
                    if (tilesetByName.contains(tilesetName) && tilesetByName[tilesetName]->tileAt(tileID)) {
                        fromTiles += tilesetByName[tilesetName]->tileAt(tileID);
                    } else if (Tileset *ts = TileMetaInfoMgr::instance()->tileset(tilesetName)) {
                        TileMetaInfoMgr::instance()->loadTilesets(QList<Tileset*>() << ts);
                        if (Tile *tile = ts->tileAt(tileID)) {
                            map->addTileset(ts);
                            tilesetByName[tilesetName] = ts;
                            fromTiles += tile;
                        }
                    } else {
                        QString mError = tr("Map '%1' is missing tileset '%2' needed by the FromTo.txt file.\nThe tileset is not one of those in Tilesets.txt.")
                                .arg(QFileInfo(fileName).fileName()).arg(tilesetName);
                        QMessageBox::warning(this, tr("From/To Failed"), mError);
                        return;
                    }
                }
                QList<Tile*> toTiles;
                foreach (QString tileName, fromto.to) {
                    BuildingTilesMgr::parseTileName(tileName, tilesetName, tileID);
                    if (tilesetByName.contains(tilesetName) && tilesetByName[tilesetName]->tileAt(tileID)) {
                        toTiles += tilesetByName[tilesetName]->tileAt(tileID);
                    } else if (Tileset *ts = TileMetaInfoMgr::instance()->tileset(tilesetName)) {
                        TileMetaInfoMgr::instance()->loadTilesets(QList<Tileset*>() << ts);
                        if (Tile *tile = ts->tileAt(tileID)) {
                            map->addTileset(ts);
                            tilesetByName[tilesetName] = ts;
                            toTiles += tile;
                        }
                    } else {
                        QString mError = tr("Map '%1' is missing tileset '%2' needed by the FromTo.txt file.\nThe tileset is not one of those in Tilesets.txt.")
                                .arg(QFileInfo(fileName).fileName()).arg(tilesetName);
                        QMessageBox::warning(this, tr("From/To Failed"), mError);
                        return;
                    }
                }
                foreach (QString layerName, fromto.layers) {
                    if (!layerMapping.contains(layerName)) continue;
                    TileLayer *tl = layerMapping[layerName];
                    foreach (Tile *tile, fromTiles)
                        tileMapping[qMakePair(tl,tile)] = toTiles;
                }

            }

            foreach (TileLayer *tl, layerMapping.values()) {
                for (int x = 0; x < tl->width(); x++) {
                    for (int y = 0; y < tl->height(); y++) {
                        Cell cell = tl->cellAt(x, y);
                        if (!cell.tile) continue;
                        if (tileMapping.contains(qMakePair(tl,cell.tile))) {
                            QList<Tile*> &choices = tileMapping[qMakePair(tl,cell.tile)];
                            tl->setCell(x, y, Cell(choices[qrand() % choices.size()]));
                        }
                    }
                }
            }

            if (!dialog.backupDir().isEmpty()) {
            }

            if (!dialog.destDir().isEmpty()) {
            }

            MapWriter writer;
            MapWriter::LayerDataFormat format = MapWriter::CSV;
            if (worldDoc->world()->getBMPToTMXSettings().compress)
                format = MapWriter::Base64Zlib;
            writer.setLayerDataFormat(format);
            writer.setDtdEnabled(false);
            if (!writer.writeMap(map.data(), mapInfo->path())) {
                QMessageBox::warning(this, tr("Error writing TMX"), writer.errorString());
                return;
            }
        }
    }
}

void MainWindow::enableDeveloperFeatures()
{
    // TOP SECRET: PLEASE DON'T LET PEOPLE KNOW ABOUT THIS BECAUSE THE DEVS
    // DO NOT WANT MASSIVE SPOILERS FOR FANS OF THE GAME.
    QString sourcePath = Preferences::instance()->appConfigPath(
                QLatin1String("EnableDeveloperFeatures.txt"));
    if (QFileInfo(sourcePath).exists()) {

    } else {
        ui->menuTools->menuAction()->setVisible(false);
//        ui->actionLotPackViewer->setVisible(false);
    }
}

WorldDocument *MainWindow::currentWorldDocument()
{
    if (mCurrentDocument) {
        if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
            return cellDoc->worldDocument();
        return mCurrentDocument->asWorldDocument();
    }
    return nullptr;
}

bool MainWindow::ensureSavedProjectForAssetWorkflow(
        WorldDocument *worldDocument)
{
    if (!worldDocument)
        return false;

    const auto isSavedProject = [worldDocument]() {
        const QFileInfo fileInfo(worldDocument->fileName());
        return !worldDocument->fileName().isEmpty()
                && fileInfo.suffix().compare(
                    QLatin1String("pzw"), Qt::CaseInsensitive) == 0
                && fileInfo.isFile();
    };
    if (isSavedProject())
        return true;

    QMessageBox prompt(
                QMessageBox::Information,
                tr("Save Project Before Creating Project Assets"),
                tr("This operation creates files that belong to a WorldEd "
                   "project. Save the PZW first so those files are created "
                   "inside the project folder."),
                QMessageBox::NoButton,
                this);
    QPushButton *saveButton = prompt.addButton(
                tr("Save PZW..."), QMessageBox::AcceptRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(saveButton);
    prompt.exec();
    if (prompt.clickedButton() != saveButton)
        return false;

    QString targetFileName = worldDocument->fileName();
    if (targetFileName.isEmpty()
            || QFileInfo(targetFileName).suffix().compare(
                QLatin1String("pzw"), Qt::CaseInsensitive) != 0) {
        QString directory = Preferences::instance()->openFileDirectory();
        if (directory.isEmpty() || !QDir(directory).exists())
            directory = QDir::currentPath();
        targetFileName = QFileDialog::getSaveFileName(
                    this, tr("Save WorldEd Project"),
                    QDir(directory).filePath(tr("untitled.pzw")),
                    tr("PZWorldEd world files (*.pzw)"));
        if (targetFileName.isEmpty())
            return false;
        if (QFileInfo(targetFileName).suffix().compare(
                    QLatin1String("pzw"), Qt::CaseInsensitive) != 0) {
            targetFileName += QLatin1String(".pzw");
        }
        Preferences::instance()->setOpenFileDirectory(
                    QFileInfo(targetFileName).absolutePath());
    }

    QString saveError;
    if (!worldDocument->save(targetFileName, saveError)) {
        QMessageBox::critical(this, tr("Error Saving Project"), saveError);
        return false;
    }
    if (mStreetNamesDock && !mStreetNamesDock->saveForProject())
        return false;
    if (mRegionsDock && !mRegionsDock->saveForProject())
        return false;
    updateWindowTitle();
    for (int index = 0; index < docman()->documentCount(); ++index) {
        Document *document = docman()->documentAt(index);
        CellDocument *cellDocument = document->asCellDocument();
        if (document == worldDocument
                || (cellDocument
                    && cellDocument->worldDocument() == worldDocument)) {
            ui->documentTabWidget->setTabToolTip(index, targetFileName);
        }
    }
    if (isSavedProject())
        return true;

    QMessageBox::critical(
                this, tr("Project Save Required"),
                tr("WorldEd could not confirm a valid PZW file. The requested "
                   "project asset workflow was not opened."));
    return false;
}

bool MainWindow::validateCellMoveCoordinateData(
        QString *summary, QString *error)
{
    const int cellSize = 256;
    const QPoint worldOrigin(10, 20);
    const QPoint sourceCell(1, 2);
    const QPoint cellOffset(2, -1);
    QSet<quint64> sourceCells;
    sourceCells.insert(cellPositionKey(sourceCell));

    StreetNameRecord street;
    street.name = QStringLiteral("Validation Street");
    street.points << QPointF((worldOrigin.x() + sourceCell.x()) * cellSize + 7,
                             (worldOrigin.y() + sourceCell.y()) * cellSize + 9)
                  << QPointF((worldOrigin.x() + sourceCell.x() + 1) * cellSize + 3,
                             (worldOrigin.y() + sourceCell.y()) * cellSize + 5);
    const QVector<StreetNameRecord> movedStreets = translatedStreetRecords(
                QVector<StreetNameRecord>() << street, sourceCells,
                cellOffset, worldOrigin, cellSize);
    const QPointF squareOffset(cellOffset.x() * cellSize,
                               cellOffset.y() * cellSize);
    if (movedStreets.first().points.first()
            != street.points.first() + squareOffset
            || movedStreets.first().points.last() != street.points.last()) {
        if (error)
            *error = tr("Street points were not translated by source cell.");
        return false;
    }

    RegionRecord region;
    region.name = QStringLiteral("Validation Region");
    region.x = (worldOrigin.x() + sourceCell.x()) * cellSize + 12;
    region.y = (worldOrigin.y() + sourceCell.y()) * cellSize + 14;
    const QVector<RegionRecord> movedRegions = translatedRegionRecords(
                QVector<RegionRecord>() << region, sourceCells,
                cellOffset, worldOrigin, cellSize);
    if (movedRegions.first().x != region.x + squareOffset.x()
            || movedRegions.first().y != region.y + squareOffset.y()) {
        if (error)
            *error = tr("Region coordinates were not translated by source cell.");
        return false;
    }

    if (localCellForPoint(QPoint(cellSize + 4, cellSize * 2 + 8),
                          cellSize) != sourceCell
            || projectCellForPoint(street.points.first(), cellSize,
                                   worldOrigin) != sourceCell) {
        if (error)
            *error = tr("Native 256 coordinate classification failed.");
        return false;
    }

    QString pasteSummary;
    if (!PasteCellsTool::validateCellPastePlacement(&pasteSummary, error))
        return false;

    World *world = new World(4, 4, WorldGridFormat::Native256);
    PropertyDef *propertyDefinition = new PropertyDef(
                QStringLiteral("ValidationProperty"), QString(), QString(),
                nullptr);
    world->addPropertyDefinition(0, propertyDefinition);
    PropertyTemplate *propertyTemplate = new PropertyTemplate;
    propertyTemplate->mName = QStringLiteral("ValidationTemplate");
    world->addPropertyTemplate(0, propertyTemplate);
    ObjectType *objectType = new ObjectType(QStringLiteral("ValidationType"));
    world->insertObjectType(0, objectType);
    WorldObjectGroup *objectGroup = new WorldObjectGroup(
                world, QStringLiteral("ValidationGroup"), Qt::red);
    world->insertObjectGroup(0, objectGroup);

    WorldCell *cell = world->cellAt(1, 2);
    cell->setMapFilePath(QStringLiteral("validation.tmx"));
    cell->setChunkDataOverrideFilePath(
                QStringLiteral("validation-chunkdata.png"));
    cell->addTemplate(0, propertyTemplate);
    Property *cellProperty = new Property(propertyDefinition,
                                          QStringLiteral("cell-value"));
    cellProperty->mNote = QStringLiteral("cell-note");
    cell->addProperty(0, cellProperty);
    cell->addLot(QStringLiteral("validation.tbx"), 5, 6, -1, 7, 8);

    WorldCellObject *object = new WorldCellObject(
                cell, QStringLiteral("ValidationObject"), objectType,
                objectGroup, 11.5, 12.5, 3, 13.5, 14.5);
    object->setGeometryType(ObjectGeometryType::Polyline);
    WorldCellObjectPoints points;
    points << WorldCellObjectPoint{1, 2}
           << WorldCellObjectPoint{17, 19};
    object->setPoints(points);
    object->setPolylineWidth(9);
    object->setVisible(false);
    object->addTemplate(0, propertyTemplate);
    Property *objectProperty = new Property(propertyDefinition,
                                            QStringLiteral("object-value"));
    objectProperty->mNote = QStringLiteral("object-note");
    object->addProperty(0, objectProperty);
    cell->insertObject(0, object);

    InGameMapFeature *feature = new InGameMapFeature(&cell->inGameMap());
    feature->mGeometry.mType = QStringLiteral("LineString");
    InGameMapCoordinates featureCoordinates;
    featureCoordinates << InGameMapPoint(260.0, 520.0)
                       << InGameMapPoint(270.0, 530.0);
    feature->mGeometry.mCoordinates << featureCoordinates;
    feature->mProperties << InGameMapProperty(
                                QStringLiteral("highway"),
                                QStringLiteral("primary"));
    cell->inGameMap().features().append(feature);

    WorldDocument document(world);
    const QPoint targetPosition(3, 1);
    document.moveCell(cell, targetPosition);
    WorldCell *target = world->cellAt(targetPosition);
    const QPoint featureOffset = (targetPosition - QPoint(1, 2)) * cellSize;

    auto verifyCell = [&](WorldCell *candidate,
                          const QPoint &expectedFeatureOffset) {
        if (!candidate
                || candidate->mapFilePath() != QStringLiteral("validation.tmx")
                || candidate->chunkDataOverrideFilePath()
                   != QStringLiteral("validation-chunkdata.png")
                || candidate->templates().size() != 1
                || candidate->templates().first()->mName
                   != QStringLiteral("ValidationTemplate")
                || candidate->properties().size() != 1
                || candidate->properties().first()->mValue
                   != QStringLiteral("cell-value")
                || candidate->properties().first()->mNote
                   != QStringLiteral("cell-note")
                || candidate->lots().size() != 1
                || candidate->lots().first()->mapName()
                   != QStringLiteral("validation.tbx")
                || candidate->objects().size() != 1
                || candidate->inGameMap().features().isEmpty())
            return false;
        WorldCellObject *candidateObject = candidate->objects().first();
        if (candidateObject->name() != QStringLiteral("ValidationObject")
                || candidateObject->geometryType()
                   != ObjectGeometryType::Polyline
                || candidateObject->points() != points
                || candidateObject->polylineWidth() != 9
                || candidateObject->isVisible()
                || candidateObject->templates().size() != 1
                || candidateObject->properties().size() != 1
                || candidateObject->properties().first()->mValue
                   != QStringLiteral("object-value")
                || candidateObject->properties().first()->mNote
                   != QStringLiteral("object-note"))
            return false;
        InGameMapFeature *primaryFeature = nullptr;
        for (InGameMapFeature *candidateFeature :
             candidate->inGameMap().features()) {
            if (candidateFeature->mProperties.contains(
                        QStringLiteral("highway"),
                        QStringLiteral("primary"))) {
                primaryFeature = candidateFeature;
                break;
            }
        }
        if (!primaryFeature || primaryFeature->mGeometry.mCoordinates.isEmpty())
            return false;
        const InGameMapCoordinates &candidateCoordinates =
                primaryFeature->mGeometry.mCoordinates.first();
        return candidateCoordinates.size() == 2
                && candidateCoordinates.first().x
                   == featureCoordinates.first().x
                      + expectedFeatureOffset.x()
                && candidateCoordinates.first().y
                   == featureCoordinates.first().y
                      + expectedFeatureOffset.y()
                && candidateCoordinates.last().x
                   == featureCoordinates.last().x
                      + expectedFeatureOffset.x()
                && candidateCoordinates.last().y
                   == featureCoordinates.last().y
                      + expectedFeatureOffset.y();
    };

    if (!cell->isEmpty() || !verifyCell(target, featureOffset)) {
        if (error)
            *error = tr("Moving a cell did not preserve its complete contents.");
        return false;
    }
    document.undoStack()->undo();
    if (!target->isEmpty() || !verifyCell(cell, QPoint())) {
        if (error)
            *error = tr("Undo did not restore the complete source cell.");
        return false;
    }
    document.undoStack()->redo();
    if (!cell->isEmpty() || !verifyCell(target, featureOffset)) {
        if (error)
            *error = tr("Redo did not restore the complete destination cell.");
        return false;
    }

    document.setSelectedCells(QList<WorldCell*>() << target);
    World *clipboardWorld = nullptr;
    {
        CopyPasteDialog dialog(&document);
        clipboardWorld = dialog.toWorld();
    }
    WorldCell *clipboardCell = clipboardWorld
            ? clipboardWorld->cellAt(targetPosition) : nullptr;
    if (!clipboardWorld
            || clipboardWorld->gridFormat() != WorldGridFormat::Native256
            || !verifyCell(clipboardCell, featureOffset)) {
        delete clipboardWorld;
        if (error)
            *error = tr("Copy omitted part of the selected cell contents.");
        return false;
    }

    WorldCell *pasteTarget = world->cellAt(0, 0);
    InGameMapFeature *retainedFeature = new InGameMapFeature(
                &pasteTarget->inGameMap());
    retainedFeature->mGeometry.mType = QStringLiteral("Point");
    InGameMapCoordinates retainedCoordinates;
    retainedCoordinates << InGameMapPoint(4.0, 5.0);
    retainedFeature->mGeometry.mCoordinates << retainedCoordinates;
    retainedFeature->mProperties << InGameMapProperty(
                                      QStringLiteral("validation"),
                                      QStringLiteral("retained"));
    pasteTarget->inGameMap().features().append(retainedFeature);

    WorldCellContents clipboardContents(clipboardCell, false);
    WorldCellContents *pasteContents = new WorldCellContents(
                &clipboardContents, pasteTarget);
    pasteContents->swapWorld(world);
    pasteContents->mergeOnto(pasteTarget);
    document.undoStack()->push(new ReplaceCell(
                                   &document, pasteTarget, pasteContents));
    delete clipboardWorld;

    auto hasRetainedFeature = [](WorldCell *candidate) {
        for (InGameMapFeature *candidateFeature :
             candidate->inGameMap().features()) {
            if (candidateFeature->mProperties.contains(
                        QStringLiteral("validation"),
                        QStringLiteral("retained")))
                return true;
        }
        return false;
    };
    const QPoint pastedFeatureOffset = QPoint(-1, -2) * cellSize;
    if (!verifyCell(pasteTarget, pastedFeatureOffset)
            || pasteTarget->inGameMap().features().size() != 2
            || !hasRetainedFeature(pasteTarget)) {
        if (error)
            *error = tr("Paste omitted copied data or replaced destination data.");
        return false;
    }
    document.undoStack()->undo();
    if (pasteTarget->inGameMap().features().size() != 1
            || !hasRetainedFeature(pasteTarget)) {
        if (error)
            *error = tr("Undo did not restore the pre-paste destination cell.");
        return false;
    }
    document.undoStack()->redo();
    if (!verifyCell(pasteTarget, pastedFeatureOffset)
            || pasteTarget->inGameMap().features().size() != 2
            || !hasRetainedFeature(pasteTarget)) {
        if (error)
            *error = tr("Redo did not restore the complete pasted cell.");
        return false;
    }

    if (summary) {
        *summary = tr("Native 256 street points, region anchors and local "
                      "road coordinates follow their source cells. Cell "
                      "properties, templates, lots, objects and InGameMap "
                      "features plus chunk data override references survive "
                      "Move, Copy, Paste, Undo and Redo, "
                      "including existing destination features. %1.")
                .arg(pasteSummary);
    }
    return true;
}

bool MainWindow::validateCellPasteInteraction(
        QString *summary, QString *error)
{
    if (docman()->documentCount() != 0) {
        if (error)
            *error = tr("The interaction validator requires an empty document list.");
        return false;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        if (error)
            *error = tr("The interaction validator could not create its temporary directory.");
        return false;
    }
    World *world = new World(5, 5, WorldGridFormat::Native256);
    DefaultsFile::newWorld(world);
    WorldDocument *document = new WorldDocument(
                world, temporaryDirectory.filePath(
                    QStringLiteral("cell-paste-interaction.pzw")));
    docman()->addDocument(document);
    WorldScene *scene = document->view()
            ? document->view()->scene()->asWorldScene() : nullptr;

    auto finish = [this, document]() {
        ToolManager::instance()->selectTool(WorldCellTool::instance());
        Clipboard::instance()->setWorld(nullptr);
        if (docman()->documents().contains(document))
            docman()->closeDocument(document);
    };

    if (!scene) {
        finish();
        if (error)
            *error = tr("WorldEd did not create the world scene used for paste.");
        return false;
    }

    const QPoint initialTarget(1, 1);
    const QPoint clickedTarget(3, 2);
    document->setSelectedCells(
                QList<WorldCell*>() << world->cellAt(initialTarget));

    World *clipboardWorld = new World(
                1, 1, WorldGridFormat::Native256);
    DefaultsFile::newWorld(clipboardWorld);
    clipboardWorld->cellAt(0, 0)->setMapFilePath(
                QStringLiteral("clipboard-interaction.tmx"));
    Clipboard::instance()->setWorld(clipboardWorld);

    scene->pasteCellsFromClipboard();
    QCoreApplication::processEvents();

    int previewCount = 0;
    for (QGraphicsItem *item : scene->items()) {
        if (dynamic_cast<PasteCellItem*>(item))
            ++previewCount;
    }
    if (ToolManager::instance()->selectedTool() != PasteCellsTool::instance()
            || previewCount != 1) {
        finish();
        if (error)
            *error = tr("Ctrl+V did not enter one visible cell-paste preview.");
        return false;
    }

    QGraphicsSceneMouseEvent moveEvent(QEvent::GraphicsSceneMouseMove);
    moveEvent.setScenePos(scene->cellToPixelCoords(
                              clickedTarget.x() + 0.5,
                              clickedTarget.y() + 0.5));
    QCoreApplication::sendEvent(scene, &moveEvent);

    const quint64 rebuildCount =
            PasteCellsTool::instance()->previewRebuildCount();
    QElapsedTimer sameCellTimer;
    sameCellTimer.start();
    for (int index = 0; index < 10000; ++index)
        QCoreApplication::sendEvent(scene, &moveEvent);
    const qint64 sameCellNanoseconds = sameCellTimer.nsecsElapsed();
    if (PasteCellsTool::instance()->previewRebuildCount()
            != rebuildCount) {
        finish();
        if (error)
            *error = tr("Pointer movement inside one cell rebuilt the paste preview.");
        return false;
    }

    QGraphicsSceneMouseEvent pressEvent(QEvent::GraphicsSceneMousePress);
    pressEvent.setButton(Qt::LeftButton);
    pressEvent.setButtons(Qt::LeftButton);
    pressEvent.setScenePos(moveEvent.scenePos());
    QCoreApplication::sendEvent(scene, &pressEvent);
    QCoreApplication::processEvents();

    const int commandCount = document->undoStack()->count();
    if (world->cellAt(clickedTarget)->mapFilePath()
            != QStringLiteral("clipboard-interaction.tmx")
            || !world->cellAt(initialTarget)->isEmpty()
            || ToolManager::instance()->selectedTool()
               != WorldCellTool::instance()
            || commandCount != 1) {
        finish();
        if (error)
            *error = tr("The first click did not paste once into the cell under the pointer.");
        return false;
    }

    QGraphicsSceneMouseEvent secondPressEvent(
                QEvent::GraphicsSceneMousePress);
    secondPressEvent.setButton(Qt::LeftButton);
    secondPressEvent.setButtons(Qt::LeftButton);
    secondPressEvent.setScenePos(scene->cellToPixelCoords(4.5, 4.5));
    QCoreApplication::sendEvent(scene, &secondPressEvent);
    QCoreApplication::processEvents();

    if (!world->cellAt(4, 4)->isEmpty()
            || document->undoStack()->count() != commandCount) {
        finish();
        if (error)
            *error = tr("Cell paste remained active after the confirmed placement.");
        return false;
    }

    finish();
    if (summary)
        *summary = tr("visible preview, pointer target, single placement and "
                      "automatic exit verified, with 10000 same-cell moves "
                      "causing no rebuild in %1 ms")
                .arg(sameCellNanoseconds / 1000000.0, 0, 'f', 3);
    return true;
}

void MainWindow::moveCellCoordinateData(
        WorldDocument *worldDocument,
        const QList<WorldCell *> &sourceCells,
        const QPoint &cellOffset)
{
    if (!worldDocument || sourceCells.isEmpty() || cellOffset.isNull())
        return;

    World *world = worldDocument->world();
    const int cellSize = world->cellSize();
    const QPoint worldOrigin =
            world->getGenerateLotsSettings().worldOrigin;
    const QPoint squareOffset(cellOffset.x() * cellSize,
                              cellOffset.y() * cellSize);
    QSet<quint64> sourcePositions;
    for (WorldCell *cell : sourceCells) {
        if (cell && cell->world() == world)
            sourcePositions.insert(cellPositionKey(cell->pos()));
    }
    if (sourcePositions.isEmpty())
        return;

    const QVector<StreetNameRecord> streetsBefore =
            mStreetNamesDock->streets();
    const QVector<StreetNameRecord> streetsAfter = translatedStreetRecords(
                streetsBefore, sourcePositions, cellOffset,
                worldOrigin, cellSize);
    const QVector<RegionRecord> regionsBefore = mRegionsDock->regions();
    const QVector<RegionRecord> regionsAfter = translatedRegionRecords(
                regionsBefore, sourcePositions, cellOffset,
                worldOrigin, cellSize);
    if (streetsBefore != streetsAfter || regionsBefore != regionsAfter) {
        worldDocument->undoStack()->push(
                    new MoveExternalCoordinatesCommand(
                        mStreetNamesDock, streetsBefore, streetsAfter,
                        mStreetNamesDock->selectedStreetIndex(),
                        mRegionsDock, regionsBefore, regionsAfter,
                        mRegionsDock->selectedRegionIndex()));
    }

    for (Road *road : world->roads()) {
        QPoint start = road->start();
        QPoint end = road->end();
        if (sourcePositions.contains(cellPositionKey(
                                         localCellForPoint(start, cellSize))))
            start += squareOffset;
        if (sourcePositions.contains(cellPositionKey(
                                         localCellForPoint(end, cellSize))))
            end += squareOffset;
        if (start != road->start() || end != road->end())
            worldDocument->changeRoadCoords(road, start, end);
    }

    for (WorldBMP *bmp : world->bmps()) {
        const QRect bounds = bmp->bounds();
        bool fullySelected = !bounds.isEmpty();
        for (int y = bounds.top(); fullySelected && y <= bounds.bottom(); ++y) {
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                if (!sourcePositions.contains(
                            cellPositionKey(QPoint(x, y)))) {
                    fullySelected = false;
                    break;
                }
            }
        }
        if (fullySelected)
            worldDocument->moveBMP(bmp, bmp->pos() + cellOffset);
    }
}

bool MainWindow::canSplitObjectPolygon()
{
    if (mCurrentDocument == nullptr) {
        return false;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    if (cellDoc == nullptr) {
        return false;
    }
    auto& objects = cellDoc->selectedObjects();
    if (objects.size() != 1) {
        return false;
    }
    WorldCellObject* object = objects.first();
    if (object->isPolygon() == false) {
        return false;
    }
    auto& selection = cellDoc->selectedObjectPoints();
    if (selection.size() != 2) {
        return false;
    }
    int index1 = selection.first();
    int index2 = selection.last();
    if (index1 > index2) {
        qSwap(index1, index2);
    }

    const WorldCellObjectPoints& points = object->points();
    int numCoords2 = index2 - index1 + 1;
    int numCoords1 = points.size() - numCoords2 + 2;

    if (numCoords1 < 3 || numCoords2 < 3) {
        return false;
    }
    return true;
}

bool MainWindow::saveFile()
{
    if (!mCurrentDocument)
        return false;

    const QString currentFileName = mCurrentDocument->fileName();

    if (!currentFileName.isEmpty())
        return saveFile(currentFileName);
    else
        return saveFileAs();
}

bool MainWindow::saveFileAs()
{
    QString suggestedFileName;
    if (mCurrentDocument && !mCurrentDocument->fileName().isEmpty()) {
        const QFileInfo fileInfo(mCurrentDocument->fileName());
        suggestedFileName = fileInfo.path();
        suggestedFileName += QLatin1Char('/');
        suggestedFileName += fileInfo.completeBaseName();
        suggestedFileName += QLatin1String(".pzw");
    } else {
        QString path = Preferences::instance()->openFileDirectory();
        if (path.isEmpty() || !QDir(path).exists())
            path = QDir::currentPath();
        suggestedFileName = path;
        suggestedFileName += QLatin1Char('/');
        suggestedFileName += tr("untitled.pzw");
    }

    QString fileName =
            QFileDialog::getSaveFileName(this, QString(), suggestedFileName,
                                         tr("PZWorldEd world files (*.pzw)"));
    if (!fileName.isEmpty()) {
        if (QFileInfo(fileName).suffix().compare(
                    QLatin1String("pzw"), Qt::CaseInsensitive) != 0) {
            fileName += QLatin1String(".pzw");
        }
        Preferences::instance()->setOpenFileDirectory(QFileInfo(fileName).absolutePath());
        return saveFile(fileName);
    }
    return false;
}

void MainWindow::closeFile()
{
    if (confirmSave())
        docman()->closeCurrentDocument();
}

void MainWindow::closeAllFiles()
{
    if (confirmAllSave())
        docman()->closeAllDocuments();
}

void MainWindow::WriteSpawnPoints()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();

    WriteSpawnPointsDialog d(worldDoc, this);
    d.exec();
}

void MainWindow::WriteWorldObjects()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();

    WriteWorldObjectsDialog d(worldDoc, this);
    d.exec();
}

extern "C" {
#include "lualib.h"
#include "lauxlib.h"

int traceback(lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg) {
    luaL_traceback(L, L, msg, 1);
  } else if (!lua_isnoneornil(L, 1)) {  /* is there an error object? */
    if (!luaL_callmeta(L, 1, "__tostring"))  /* try its 'tostring' metamethod */
      lua_pushliteral(L, "(no error message)");
  }
  return 1;
}

} // extern "C"

#include "luatable.h"

namespace Lua {
const char *cstring(const QString &qstring);
} // namespace Lua

const char *Lua::cstring(const QString &qstring)
{
    static QHash<QString,const char*> StringHash;
    if (!StringHash.contains(qstring)) {
        QByteArray b = qstring.toLatin1();
        char *s = new char[b.size() + 1];
        memcpy(s, (void*)b.data(), b.size() + 1);
        StringHash[qstring] = s;
    }
    return StringHash[qstring];
}

void MainWindow::ReadWorldObjects()
{
    QString filter = tr("Lua files (*.lua)");
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Building"), QString(), filter);
    if (fileName.isEmpty()) {
        return;
    }

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    int status = luaL_loadfile(L, Lua::cstring(fileName));
    if (status == LUA_OK) {
        int base = lua_gettop(L);
        lua_pushcfunction(L, traceback);
        lua_insert(L, base);
        status = lua_pcall(L, 0, 0, base);
        lua_remove(L, base);
    }

    if (status != LUA_OK) {
        QString output = QString::fromLatin1(lua_tostring(L, -1));
        lua_pop(L, -1); // pop error
        QMessageBox::warning(this, QStringLiteral("Read Objects from Lua"), output);
    }

    if (status == LUA_OK) {
        // regions = { 1={ name="", type="", ... }, 2={ name="", type="", ... }, 3={ name="", type="", ... } }
        lua_getglobal(L, "regions");
        if (lua_istable(L, -1)) {
            Lua::LuaTable *table = Lua::parseTable(L);
            addWorldObjectsFromLuaTable(table);
            delete table;
        }
        lua_pop(L, 1); // Pop "regions" from the stack

        lua_getglobal(L, "objects");
        if (lua_istable(L, -1)) {
            Lua::LuaTable *table = Lua::parseTable(L);
            addWorldObjectsFromLuaTable(table);
            delete table;
        }
        lua_pop(L, 1); // Pop "objects" from the stack
    }

    lua_close(L);
}

void MainWindow::addWorldObjectsFromLuaTable(Lua::LuaTable *regionsTable)
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    World *world = worldDoc->world();
    const GenerateLotsSettings &gls = world->getGenerateLotsSettings();
    const int cellSize = world->cellSize();
    worldDoc->undoStack()->beginMacro(tr("Read Objects from Lua"));

    for (Lua::LuaTableKeyValue *kv : std::as_const(regionsTable->kv)) {
        if (!kv->value.isTable()) {
            continue;
        }
        Lua::LuaTable *objectTable = kv->value.t.data();
        QString type;
        if (!objectTable->getString(QStringLiteral("type"), type)) {
            continue;
        }
        if (type.isEmpty()) {
            continue;
        }
        QString name;
        lua_Number x = 0.0, y = 0.0, z = 0.0, width = 1.0, height = 1.0;
        if (!objectTable->getString(QStringLiteral("name"), name)) {
            continue;
        }
        ObjectGeometryType geometryType = ObjectGeometryType::INVALID;
        WorldCellObjectPoints points;
        QString geometry;
        if (objectTable->getString(QStringLiteral("geometry"), geometry)) {
            if (geometry == QLatin1String("point")) {
                geometryType = ObjectGeometryType::Point;
            } else if (geometry == QLatin1String("polygon")) {
                geometryType = ObjectGeometryType::Polygon;
            } else if (geometry == QLatin1String("polyline")) {
                geometryType = ObjectGeometryType::Polyline;
            } else {
               continue;
            }
            if (Lua::LuaTable *pointsTable = objectTable->getTable(QStringLiteral("points"))) {
                int numPoints = pointsTable->kv.size() / 2;
                if (numPoints < 1) {
                    continue;
                }
                points.resize(numPoints);
                for (Lua::LuaTableKeyValue *kv : std::as_const(pointsTable->kv)) {
                    if (!kv->key.isNumber() || !kv->value.isNumber()) {
                        continue;
                    }
                    int k = int(std::roundl(kv->key.n)) - 1;
                    if (k < 0 || k >= numPoints * 2) {
                        continue;
                    }
                    int v = int(std::roundl(kv->value.n));
                    WorldCellObjectPoint &p = points[k / 2];
                    if (k % 2) {
                        p.y = v;
                    } else {
                        p.x = v;
                    }
                }
                if (points.size() < 1) {
                    continue;
                }
                QRect bounds = points.calculateBounds();
                x = bounds.x();
                y = bounds.y();
                width = bounds.width();
                height = bounds.height();
                const int globalCellX = floorDivision(qFloor(x), cellSize);
                const int globalCellY = floorDivision(qFloor(y), cellSize);
                points.translate(-globalCellX * cellSize,
                                 -globalCellY * cellSize);
            }
        } else {
            if (!objectTable->getNumber(QStringLiteral("x"), x)) {
                continue;
            }
            if (!objectTable->getNumber(QStringLiteral("y"), y)) {
                continue;
            }
            if (!objectTable->getNumber(QStringLiteral("width"), width)) {
                continue;
            }
            if (!objectTable->getNumber(QStringLiteral("height"), height)) {
                continue;
            }
        }
        if (!objectTable->getNumber(QStringLiteral("z"), z)) {
            continue;
        }
        const int globalCellX = floorDivision(qFloor(x), cellSize);
        const int globalCellY = floorDivision(qFloor(y), cellSize);
        WorldCell *cell = world->cellAt(
                    globalCellX - gls.worldOrigin.x(),
                    globalCellY - gls.worldOrigin.y());
        if (cell == nullptr) {
            continue;
        }
        ObjectType *objectType = worldDoc->world()->objectType(type);
        if (objectType == nullptr) {
            continue;
        }
        WorldObjectGroup *objectGroup = worldDoc->world()->objectGroups().find(type);
        if (objectGroup == nullptr) {
            continue;
        }
        WorldCellObject* object = new WorldCellObject(cell, name, objectType, objectGroup,
                                                      qreal(x - globalCellX * cellSize),
                                                      qreal(y - globalCellY * cellSize),
                                                      qreal(z),
                                                      qreal(width), qreal(height));
        if (geometryType != ObjectGeometryType::INVALID) {
            object->setGeometryType(geometryType);
            object->setPoints(points);
            object->calculateBounds(); // not needed
        }
        if (Lua::LuaTable *propertiesTable = objectTable->getTable(QStringLiteral("properties"))) {
            PropertyList propertyList;
            for (const Lua::LuaTableKeyValue *kv : std::as_const(propertiesTable->kv)) {
                if (!kv->key.isString()) {
                    continue;
                }
                QString value;
                if (kv->value.isBoolean()) {
                    value = kv->value.b ? QStringLiteral("true") : QStringLiteral("false");
                } else if (kv->value.isNumber()) {
                    value = QString::number(kv->value.n);
                } else if (kv->value.isString()) {
                    value = kv->value.s;
                } else {
                    // a table?
                    continue;
                }
                if (PropertyDef *propertyDef = worldDoc->world()->propertyDefinition(kv->key.s)) {
                    Property *property = new Property(propertyDef, value);
                    propertyList += property;
                }
            }
            object->setProperties(propertyList);
        }
        worldDoc->addCellObject(cell, cell->objects().size(), object);
    }
    worldDoc->undoStack()->endMacro();
}

void MainWindow::WriteRoomTones()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();

    WriteRoomTonesDialog d(worldDoc, this);
    int result = d.exec();
    if (result != QDialog::Accepted)
        return;

    QString luaFileName = worldDoc->world()->getLuaSettings().roomTonesFile;
    if (!luaFileName.isEmpty()) {
        LuaWriter writer;
        if (!writer.writeRoomTones(worldDoc->world(), luaFileName)) {
            QMessageBox::warning(MainWindow::instance(), tr("Error saving RoomTone objects"),
                                 tr("An error occurred saving the LUA objects file.\n%1\n\n%2")
                                 .arg(writer.errorString())
                                 .arg(QDir::toNativeSeparators(luaFileName)));
        }
    }
}

void MainWindow::updateWindowTitle()
{
    QString fileName = mCurrentDocument ? mCurrentDocument->fileName() : QString();
    if (fileName.isEmpty())
        fileName = tr("Untitled");
    else {
        fileName = QDir::toNativeSeparators(fileName);
    }
    setWindowTitle(tr("[*]%1 - PZWorldEd - %2")
                   .arg(fileName, PZToolsBuild::label()));
    setWindowFilePath(fileName);
    bool isModified = mCurrentDocument ? mCurrentDocument->isModified() : false;
    if (mCurrentDocument && mCurrentDocument->isCellDocument())
        isModified = mCurrentDocument->asCellDocument()->worldDocument()->isModified();
    setWindowModified(isModified);
}

static void generateLots8x8(MainWindow *mainWin, Document *doc,
                            LotFilesManager256::GenerateMode mode,
                            bool exportAsMod = false)
{
    if (!doc)
        return;
    WorldDocument *worldDoc = doc->asWorldDocument();
    if (!worldDoc)
        return;
    GenerateLotsDialog dialog(worldDoc, mainWin);
    if (exportAsMod) {
        dialog.setWindowTitle(mainWin->tr("Export Complete Project Zomboid Mod"));
        dialog.setExportAsMod(true);
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    LotFilesManager256 *lotManager = LotFilesManager256::instance();
    if (!dialog.fillHolesDuringExport()) {
        lotManager->setHoleFillMode(LotFilesManager256::ReportHoles);
    } else if (dialog.fillHolesWithNearestTile()) {
        lotManager->setHoleFillMode(
                    LotFilesManager256::FillHolesWithNearestTile);
    } else {
        lotManager->setHoleFillMode(
                    LotFilesManager256::FillHolesWithSpecificTile,
                    dialog.holeFillTileName());
    }
    if (lotManager->generateWorld(worldDoc, mode) == false) {
        QMessageBox::warning(mainWin, mainWin->tr("Lot Generation Failed!"), LotFilesManager256::instance()->errorString());
        return;
    }
    QString modError;
    if (!dialog.finalizeModExport(&modError)) {
        QMessageBox::warning(mainWin, mainWin->tr("Mod Export Failed!"), modError);
    } else if (dialog.exportAsMod()) {
        QMessageBox::information(mainWin, mainWin->tr("Mod Export Complete"),
                                 mainWin->tr("The LOT files and mod structure were generated successfully."));
    }
#if 0
    TileMetaInfoMgr::deleteInstance();
#endif
}

void MainWindow::generateLotsAll8x8()
{
    generateLots8x8(this, mCurrentDocument, LotFilesManager256::GenerateAll);
}

void MainWindow::generateLotsSelected8x8()
{
    generateLots8x8(this, mCurrentDocument, LotFilesManager256::GenerateSelected);
}

void MainWindow::exportModAll8x8()
{
    generateLots8x8(this, mCurrentDocument, LotFilesManager256::GenerateAll, true);
}
void MainWindow::generateLotSettingsChanged()
{
    // Update the tab names when worldOrigin changes.
    WorldDocument *worldDoc = currentWorldDocument();
    int pos = 0;
    foreach (Document *doc, docman()->documents()) {
        CellDocument *cellDoc = doc->asCellDocument();
        if (cellDoc && cellDoc->worldDocument() == worldDoc) {
            QPoint cellPos = cellDoc->cell()->displayPos();
            ui->documentTabWidget->setTabText(pos, tr("Cell %1,%2").arg(cellPos.x()).arg(cellPos.y()));
        }
        ++pos;
    }

    updateActions();
}

static void overwriteSpawnMap256(MainWindow *mainWin, Document *doc, LotFilesManager256::GenerateMode mode)
{
    if (!doc)
        return;
    WorldDocument *worldDoc = doc->asWorldDocument();
    if (!worldDoc)
        return;
    GenerateLotsDialog dialog(worldDoc, mainWin);
    dialog.setWindowTitle(QLatin1String("Overwrite SpawnMap"));
    dialog.setModExportAvailable(false);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (LotFilesManager256::instance()->overwriteSpawnMap(worldDoc, mode) == false) {
        QMessageBox::warning(mainWin, mainWin->tr("Overwrite SpawnMap Failed!"), LotFilesManager256::instance()->errorString());
    }
}

void MainWindow::overwriteSpawnMap_AllCells_256()
{
    overwriteSpawnMap256(this, mCurrentDocument, LotFilesManager256::GenerateAll);
}

void MainWindow::overwriteSpawnMap_SelectedCells_256()
{
    overwriteSpawnMap256(this, mCurrentDocument, LotFilesManager256::GenerateSelected);
}

static void _BMPToTMX(MainWindow *mainWin, Document *doc,
                      BMPToTMX::GenerateMode mode)
{
    WorldDocument *worldDoc = doc->asWorldDocument();
    if (!worldDoc)
        return;
    BMPToTMXDialog dialog(worldDoc, mainWin);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!BMPToTMX::instance()->generateWorld(worldDoc, mode)) {
        QMessageBox::warning(mainWin, mainWin->tr("BMP To TMX Failed!"),
                             BMPToTMX::instance()->errorString());
    }
#if 0
    TileMetaInfoMgr::deleteInstance();
#endif
}

void MainWindow::BMPToTMXAll()
{
    _BMPToTMX(this, mCurrentDocument, BMPToTMX::GenerateAll);
}

void MainWindow::BMPToTMXSelected()
{
    _BMPToTMX(this, mCurrentDocument, BMPToTMX::GenerateSelected);
}

static void _TMXToBMP(MainWindow *mainWin, Document *doc,
                      TMXToBMP::GenerateMode mode)
{
    WorldDocument *worldDoc = doc->asWorldDocument();
    if (!worldDoc)
        return;
    if (!TMXToBMP::hasInstance())
        new TMXToBMP();

    TMXToBMPDialog dialog(worldDoc, mainWin);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!TMXToBMP::instance().generateWorld(worldDoc, mode)) {
        QMessageBox::warning(mainWin, mainWin->tr("TMX To BMP Failed!"),
                             TMXToBMP::instance().errorString());
    }
}
void MainWindow::TMXToBMPAll()
{
    _TMXToBMP(this, mCurrentDocument, TMXToBMP::GenerateAll);
}

void MainWindow::TMXToBMPSelected()
{
    _TMXToBMP(this, mCurrentDocument, TMXToBMP::GenerateSelected);
}

void MainWindow::resizeWorld()
{
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc)
        return;
    ResizeWorldDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::linkedWorldProjects()
{
    if (!mCurrentDocument)
        return;
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();
    if (!worldDoc)
        return;
    OtherWorldsDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::preferencesDialog()
{
    WorldDocument *worldDoc = 0;
    if (mCurrentDocument) {
        worldDoc = mCurrentDocument->asWorldDocument();
        if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
            worldDoc = cellDoc->worldDocument();
    }
    PreferencesDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::initActionManager()
{
    const QString fileName = Preferences::instance()->userPath(QStringLiteral("shortcuts/WorldEd.txt"));
    mActionManager = new ActionManager(fileName, this);

    const QString CONTEXT_MENU = QStringLiteral("Menu");
    const QString CATEGORY_MENU_FILE = QStringLiteral("File");
    const QString CATEGORY_MENU_EDIT = QStringLiteral("Edit");
    const QString CATEGORY_MENU_VIEW = QStringLiteral("View");
    const QString CATEGORY_MENU_WORLD = QStringLiteral("World");
    const QString CATEGORY_MENU_CELL = QStringLiteral("Cell");
    const QString CATEGORY_MENU_INGAME_MAP = QStringLiteral("InGameMap");
    const QString CATEGORY_MENU_TOOLS = QStringLiteral("Tools");

    ActionManager *actionManager = mActionManager;
    actionManager->registerAction(ui->actionNew, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.New"));
    actionManager->registerAction(ui->actionOpen, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.Open"));
    actionManager->registerAction(ui->actionSave, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.Save"));
    actionManager->registerAction(ui->actionSaveAs, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.SaveAs"));
    actionManager->registerAction(ui->actionGenerateLotsAll8x8, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.GenerateLotsAll8x8"), QStringLiteral("Generate Lots | All 8x8"));
    actionManager->registerAction(ui->actionGenerateLotsSelected8x8, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.GenerateLotsSelected8x8"), QStringLiteral("Generate Lots | Selected 8x8"));
    actionManager->registerAction(ui->actionExportModAll8x8, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.ExportModAll8x8"), QStringLiteral("Export Complete Mod 8x8"));
    actionManager->registerAction(ui->actionClose, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.Close"));
    actionManager->registerAction(ui->actionCloseAll, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.CloseAll"));
    actionManager->registerAction(ui->actionQuit, CONTEXT_MENU, CATEGORY_MENU_FILE, QStringLiteral("Menu.File.Quit"));

    actionManager->registerAction(mUndoAction, CONTEXT_MENU, CATEGORY_MENU_EDIT, QStringLiteral("Menu.Edit.Undo"));
    actionManager->registerAction(mRedoAction, CONTEXT_MENU, CATEGORY_MENU_EDIT, QStringLiteral("Menu.Edit.Redo"));
    actionManager->registerAction(ui->actionCopy, CONTEXT_MENU, CATEGORY_MENU_EDIT, QStringLiteral("Menu.Edit.Copy"));
    actionManager->registerAction(ui->actionPaste, CONTEXT_MENU, CATEGORY_MENU_EDIT, QStringLiteral("Menu.Edit.Paste"));
    actionManager->registerAction(ui->actionPreferences, CONTEXT_MENU, CATEGORY_MENU_EDIT, QStringLiteral("Menu.Edit.KeyboardShortcuts"));
    actionManager->registerAction(ui->actionKeyboardShortcuts, CONTEXT_MENU, CATEGORY_MENU_EDIT, QStringLiteral("Menu.Edit.KeyboardShortcuts"));

    actionManager->registerAction(ui->actionShowCellBorder, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowCellBorder"));
    actionManager->registerAction(ui->actionShowCoordinates, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowCoordinates"));
    actionManager->registerAction(ui->actionShowGrid, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowGrid"));
#if 0
    actionManager->registerAction(ui->actionSnapToGrid, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.SnapToGrid"));
#endif
    actionManager->registerAction(ui->actionShowInvisibleTiles, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowInvisibleTiles"));
    actionManager->registerAction(ui->actionShowLotFloorsOnly, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowLotFloorsOnly"));
    actionManager->registerAction(ui->actionShowMiniMap, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowMiniMap"));
    actionManager->registerAction(ui->actionShowObjects, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowObjects"));
    actionManager->registerAction(ui->actionShowObjectNames, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowObjectNames"));
    actionManager->registerAction(ui->actionShowVehicleMeshPreviews,
                                  CONTEXT_MENU, CATEGORY_MENU_VIEW,
                                  QStringLiteral("Menu.View.ShowVehicleMeshPreviews"));
    actionManager->registerAction(ui->actionShowOtherWorlds, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowOtherWorlds"));
    actionManager->registerAction(ui->actionShowBMP, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowBMP"));
    actionManager->registerAction(ui->actionShowZombieSpawnImage, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowZombieSpawnImage"));
    actionManager->registerAction(ui->actionShowBiomeMap, CONTEXT_MENU,
                                  CATEGORY_MENU_VIEW,
                                  QStringLiteral("Menu.View.ShowBiomeMap"));
    actionManager->registerAction(ui->actionShowZonesInWorldView, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ShowZonesInWorldView"));
    actionManager->registerAction(ui->actionHighlightCurrentLevel, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.HighlightCurrentLevel"));
    actionManager->registerAction(ui->actionHighlightRoomUnderPointer, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.HighlightRoomUnderPointer"));
    actionManager->registerAction(ui->actionLevelAbove, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.LevelAbove"));
    actionManager->registerAction(ui->actionLevelBelow, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.LevelBelow"));
    actionManager->registerAction(ui->actionZoomIn, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ZoomIn"));
    actionManager->registerAction(ui->actionZoomOut, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ZoomOut"));
    actionManager->registerAction(ui->actionZoomNormal, CONTEXT_MENU, CATEGORY_MENU_VIEW, QStringLiteral("Menu.View.ZoomNormal"));

    actionManager->registerAction(ui->actionEditCell, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.EditCell"));
    actionManager->registerAction(ui->actionGoToXY, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.GoToXY"));
    actionManager->registerAction(ui->actionResizeWorld, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.ResizeWorld"));
    actionManager->registerAction(ui->actionLinkedWorldProjects, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.LinkedWorldProjects"));
    actionManager->registerAction(ui->actionObjectTypes, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.ObjectTypes"));
    actionManager->registerAction(ui->actionObjectGroups, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.ObjectGroups"));
    actionManager->registerAction(ui->actionEnums, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.Enums"));
    actionManager->registerAction(ui->actionProperties, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.Properties"));
    actionManager->registerAction(ui->actionTemplates, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.Templates"));
    actionManager->registerAction(ui->actionRemoveBMP, CONTEXT_MENU, CATEGORY_MENU_WORLD, QStringLiteral("Menu.World.RemoveBMP"));

    actionManager->registerAction(ui->actionRemoveLot, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.RemoveLot"));
    actionManager->registerAction(ui->actionRemoveObject, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.RemoveObject"));
    actionManager->registerAction(ui->actionSplitObjectPolygon, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.SplitPolygon"));
    actionManager->registerAction(ui->actionExtractLots, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.ExtractLots"));
    actionManager->registerAction(ui->actionExtractObjects, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.ExtractObjects"));
    actionManager->registerAction(ui->actionClearCell, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.ClearCell"));
    actionManager->registerAction(ui->actionClearMapOnly, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.ClearMapOnly"));
    actionManager->registerAction(ui->actionRemoveEmptyBorderCells, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.RemoveEmptyBorderCells"));
    actionManager->registerAction(ui->actionCheckForHoles, CONTEXT_MENU, CATEGORY_MENU_CELL, QStringLiteral("Menu.Cell.CheckForHoles"));

    actionManager->registerAction(ui->actionRemoveInGameMapFeatures, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.RemoveFeature"));
    actionManager->registerAction(ui->actionRemoveInGameMapPoints, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.RemovePoint"));
    actionManager->registerAction(ui->actionSplitInGameMapPolygon, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.SplitPolygon"));
    actionManager->registerAction(ui->actionAddInGameMapHole, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.AddHole"));
    actionManager->registerAction(ui->actionRemoveInGameMapHole, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.RemoveHole"));
    actionManager->registerAction(ui->actionConvertToPolygon, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.ConvertToPolygon"));
    actionManager->registerAction(ui->actionGenerateInGameMapBuildingFeatures, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.GenerateBuildingFeatures"));
    actionManager->registerAction(ui->actionGenerateInGameMapTreeFeatures, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.GenerateTreeFeatures"));
    actionManager->registerAction(ui->actionGenerateInGameMapWaterFeatures, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.GenerateWaterFeatures"));
    actionManager->registerAction(ui->actionGenerateInGameMapRoadFeatures, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.GenerateRoadFeatures"));
    actionManager->registerAction(ui->actionWriteInGameMapForest, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.WriteWorldmapForest"));
    actionManager->registerAction(ui->actionWriteInGameMapWorldMap, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.WriteWorldmap"));
    actionManager->registerAction(ui->actionEditWorldMapAnnotations, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.EditWorldmapAnnotations"));
    actionManager->registerAction(ui->actionReadInGameMapFeaturesXML, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.ReadXML"));
    actionManager->registerAction(ui->actionWriteInGameMapFeaturesXML_256, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.WriteXML8x8"));
    actionManager->registerAction(ui->actionOverwriteInGameMapFeaturesXML_256, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.OverwriteXML8x8"), QStringLiteral("Overwrite Features XML 8x8"));
    actionManager->registerAction(ui->actionCreateFeatureImage, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.CreateFeatureImage"));
    actionManager->registerAction(ui->actionCreateWorldImage, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.CreateWorldImage"));
    actionManager->registerAction(ui->actionCreateImagePyramid, CONTEXT_MENU, CATEGORY_MENU_INGAME_MAP, QStringLiteral("Menu.InGameMap.CreateImagePyramid"));

    actionManager->registerAction(ui->actionLotPackViewer, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.ProjectUtilities.LotPackViewer"));
    actionManager->registerAction(ui->actionLootInspector, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.ProjectUtilities.LootInspector"));
    actionManager->registerAction(ui->actionFromToAll, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.ProjectUtilities.TilesFromToAll"));
    actionManager->registerAction(ui->actionFromToSelected, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.ProjectUtilities.TilesFromToSelected"));
    actionManager->registerAction(ui->actionBuildingsToPNG, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.ProjectUtilities.BuildingsToPNG"));
    actionManager->registerAction(ui->actionZonesToPNG, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.ProjectUtilities.ZonesToPNG"));
    actionManager->registerAction(ui->actionImportOpenStreetMapTerrain, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.Terrain.ImportOpenStreetMap"));
    actionManager->registerAction(ui->actionTerrainImageEditor, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.Terrain.ImageEditor"));
    actionManager->registerAction(mChunkDataOverrideAction, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.Terrain.ChunkDataOverrides"));
    actionManager->registerAction(ui->actionGenerateBiomeMap, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.Terrain.GenerateBiomeMap"));
    actionManager->registerAction(ui->actionWorldGenPreview, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.WorldGen.Preview"));
    actionManager->registerAction(ui->actionWorldGenPrefabEditor, CONTEXT_MENU, CATEGORY_MENU_TOOLS, QStringLiteral("Menu.Tools.WorldGen.PrefabEditor"));
    connect(actionManager, &ActionManager::shortcutEdited, ToolManager::instance(), &ToolManager::shortcutEdited);
}

void MainWindow::keyboardShortcuts()
{
    QString error;
    mActionManager->load(error);
    mActionManager->emitShortcutEditedForAllActions();
    if (mKeyboardShortcutWindow == nullptr) {
        mKeyboardShortcutWindow = new KeyboardShortcutWindow(mActionManager, &mSettings, QStringLiteral("KeyboardShortcutsWindow"), this);
        mKeyboardShortcutWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    mKeyboardShortcutWindow->show();
    mKeyboardShortcutWindow->raise();
}
void MainWindow::objectGroupsDialog()
{
    if (!mCurrentDocument)
        return;
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();
    ObjectGroupsDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::objectTypesDialog()
{
    if (!mCurrentDocument)
        return;
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();
    ObjectTypesDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::propertyEnumsDialog()
{
    if (!mCurrentDocument)
        return;
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();
    PropertyEnumDialog d(worldDoc, this);
    d.exec();
}

void MainWindow::properyDefinitionsDialog()
{
    if (!mCurrentDocument)
        return;
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();
    PropertyDefinitionsDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::templatesDialog()
{
    if (!mCurrentDocument)
        return;
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument())
        worldDoc = cellDoc->worldDocument();
    TemplatesDialog dialog(worldDoc, this);
    dialog.exec();
}

void MainWindow::copy()
{
    if (!mCurrentDocument)
        return;
    World *world = 0;
    if (WorldDocument *worldDoc = mCurrentDocument->asWorldDocument()) {
        CopyPasteDialog dialog(worldDoc, this);
        if (dialog.exec() == QDialog::Accepted)
            world = dialog.toWorld();
    }
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        CopyPasteDialog dialog(cellDoc, this);
        if (dialog.exec() == QDialog::Accepted)
            world = dialog.toWorld();
    }
    if (world) {
        checkpointDocumentAutoSave();
        WorldWriter w;
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        w.writeWorld(world, &buffer, QDir::rootPath());
        qApp->clipboard()->setText(QString::fromUtf8(bytes.constData(),
                                                     bytes.size()));

        Clipboard::instance()->setWorld(world);
        updateActions();
    }
}

void MainWindow::paste()
{
    Q_ASSERT(mCurrentDocument && mCurrentDocument->isWorldDocument());
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (Clipboard::instance()->cellsInClipboardCount())
        worldDoc->view()->scene()->asWorldScene()->pasteCellsFromClipboard();
    else {
        beginDocumentTransaction();
        Clipboard::instance()->pasteEverythingButCells(worldDoc);
        endDocumentTransaction();
    }
}

void MainWindow::showClipboard()
{
    World *world = Clipboard::instance()->world();
    if (!world)
        return;
    CopyPasteDialog dialog(world, this);
    if (dialog.exec() == QDialog::Accepted) {
        world = dialog.toWorld();

        WorldWriter w;
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        w.writeWorld(world, &buffer, QDir::rootPath());
        qApp->clipboard()->setText(QString::fromUtf8(bytes.constData(),
                                                     bytes.size()));

        Clipboard::instance()->setWorld(world);
        updateActions();
    }
}

void MainWindow::removeRoad()
{
    Q_ASSERT(mCurrentDocument);
    WorldDocument *worldDoc = 0;
    if ((worldDoc = mCurrentDocument->asWorldDocument())) {
    }
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        worldDoc = cellDoc->worldDocument();
    }
    int count = worldDoc->selectedRoadCount();
    Q_ASSERT(count);

    QUndoStack *undoStack = worldDoc->undoStack();
    undoStack->beginMacro(tr("Remove %1 Road%2").arg(count)
                          .arg((count > 1) ? QLatin1String("s") : QLatin1String("")));
    foreach (Road *road, worldDoc->selectedRoads()) {
        int index = worldDoc->world()->roads().indexOf(road);
        Q_ASSERT(index >= 0);
        worldDoc->removeRoad(index);
    }
    undoStack->endMacro();
}

void MainWindow::removeBMP()
{
    Q_ASSERT(mCurrentDocument);
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc)
        return;
    int count = worldDoc->selectedBMPCount();
    Q_ASSERT(count);

    QUndoStack *undoStack = worldDoc->undoStack();
    undoStack->beginMacro(tr("Remove %1 BMP Image%2").arg(count)
                          .arg((count > 1) ? QLatin1String("s") : QLatin1String("")));
    foreach (WorldBMP *bmp, worldDoc->selectedBMPs())
        worldDoc->removeBMP(bmp);
    undoStack->endMacro();
}

void MainWindow::removeLot()
{
    Q_ASSERT(mCurrentDocument);
    WorldDocument *worldDoc = 0;
    WorldCell *cell = 0;
    QList<WorldCellLot*> lots;
    if ((worldDoc = mCurrentDocument->asWorldDocument())) {
        Q_ASSERT(worldDoc->selectedCellCount());
        cell = worldDoc->selectedCells().first();
        lots = worldDoc->selectedLots();
    }
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        cell = cellDoc->cell();
        lots = cellDoc->selectedLots();
        worldDoc = cellDoc->worldDocument();
    }
    int count = lots.size();
    if (!worldDoc || !cell || !count) // none of these should ever be true
        return;
    QUndoStack *undoStack = worldDoc->undoStack();
    undoStack->beginMacro(tr("Remove %1 Lot%2").arg(count).arg((count > 1) ? QLatin1String("s") : QLatin1String("")));
    foreach (WorldCellLot *lot, lots) {
        int index = cell->lots().indexOf(lot);
        Q_ASSERT(index >= 0);
        worldDoc->removeCellLot(cell, index);
    }
    undoStack->endMacro();
}

void MainWindow::removeObject()
{
    Q_ASSERT(mCurrentDocument);
    WorldDocument *worldDoc = 0;
    WorldCell *cell = 0;
    QList<WorldCellObject*> objects;
    if ((worldDoc = mCurrentDocument->asWorldDocument())) {
        Q_ASSERT(worldDoc->selectedCellCount());
        cell = worldDoc->selectedCells().first();
        objects = worldDoc->selectedObjects();
    }
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        cell = cellDoc->cell();
        objects = cellDoc->selectedObjects();
        worldDoc = cellDoc->worldDocument();
    }
    int count = objects.size();
    if (!worldDoc || !cell || !count) // none of these should ever be true
        return;
    QUndoStack *undoStack = worldDoc->undoStack();
    undoStack->beginMacro(tr("Remove %1 Object%2").arg(count).arg(count ? QLatin1String("s") : QLatin1String("")));
    foreach (WorldCellObject *obj, objects) {
        int index = cell->objects().indexOf(obj);
        Q_ASSERT(index >= 0);
        worldDoc->removeCellObject(cell, index);
    }
    undoStack->endMacro();
}

void MainWindow::splitObjectPolygon()
{
    if (canSplitObjectPolygon() == false) {
        return;
    }
    auto* worldDoc = currentWorldDocument();
    auto* cellDoc = mCurrentDocument->asCellDocument();
    WorldCellObject* object = cellDoc->selectedObjects().first();
    auto& selection = cellDoc->selectedObjectPoints();
    int index1 = selection.first();
    int index2 = selection.last();
    if (index1 > index2) {
        qSwap(index1, index2);
    }

    const WorldCellObjectPoints& points = object->points();
    int numPoints2 = index2 - index1 + 1;
    int numPoints1 = points.size() - numPoints2 + 2;

    WorldCellObjectPoints points1;
    for (int i = 0; i < numPoints1; i++) {
        int index = (index2 + i) % points.size();
        points1 << points[index];
    }

    WorldCellObjectPoints points2;
    std::copy(points.begin() + index1, points.begin() + index2 + 1, std::back_inserter(points2));

    qreal x = points2[0].x, y = points2[0].y, width = 1, height = 1;
    int level = object->level();
    WorldCellObject* object2 = new WorldCellObject(cellDoc->cell(), object->name(), object->type(), object->group(), x, y, level, width, height);
    object2->setGeometryType(object->geometryType());
    object2->setPoints(points2);
    object2->calculateBounds();
    object2->setProperties(object->properties().clone());

    worldDoc->undoStack()->beginMacro(tr("Split Object Polygon"));
    worldDoc->setCellObjectPoints(cellDoc->cell(), object->index(), points1);
    worldDoc->addCellObject(cellDoc->cell(), cellDoc->cell()->objects().size(), object2);
    worldDoc->undoStack()->endMacro();
}

void MainWindow::extractLots()
{
    Q_ASSERT(mCurrentDocument);
    Q_ASSERT(mCurrentDocument->isCellDocument());
    CellDocument *cellDoc = mCurrentDocument->asCellDocument();
    QFileInfo info(cellDoc->scene()->mapComposite()->mapInfo()->path());
    QString message = tr("This command will create a new Lot for each 'lot' object " \
                         "that is in the cell's map \"%1\".  You should then " \
                         "remove those objects from the map using the TileZed " \
                         "editor, otherwise the Lots will be loaded twice by the game.")
            .arg(info.completeBaseName());
    QMessageBox::StandardButton b =
            QMessageBox::information(this, tr("Extract Lots"), message,
                                     QMessageBox::Ok, QMessageBox::Cancel);
    if (b == QMessageBox::Cancel)
        return;

    WorldDocument *worldDoc = cellDoc->worldDocument();
    worldDoc->undoStack()->beginMacro(tr("Extract Lots"));

    WorldCell *cell = cellDoc->cell();
    Map *map = cellDoc->scene()->map();
    foreach (ObjectGroup *og, map->objectGroups()) {
        foreach (MapObject *o, og->objects()) {
            if (o->name() == QLatin1String("lot") && !o->type().isEmpty()) {
                int x = qFloor(o->x()), y = qFloor(o->y());
                // Adjust for map orientation
                if (map->orientation() == Map::Isometric)
                    x += 3 * og->level(), y += 3 * og->level();
                WorldCellLot *lot = new WorldCellLot(cell,
                                                     o->type(), x, y,
                                                     og->level(),
                                                     o->width(), o->height());
               worldDoc->addCellLot(cell, cell->lots().size(), lot);
            }
        }
    }

    worldDoc->undoStack()->endMacro();
}

void MainWindow::extractObjects()
{
    Q_ASSERT(mCurrentDocument);
    Q_ASSERT(mCurrentDocument->isCellDocument());
    CellDocument *cellDoc = mCurrentDocument->asCellDocument();
    QFileInfo info(cellDoc->scene()->mapComposite()->mapInfo()->path());
    QString message = tr("This command will create a new Object for each object " \
                         "that is in the cell's map \"%1\" (except 'lot' objects).  "
                         "You should then remove those objects from the map using "
                         "the TileZed editor, otherwise the Objects will be loaded "
                         "twice by the game.")
            .arg(info.completeBaseName());
    QMessageBox::StandardButton b =
            QMessageBox::information(this, tr("Extract Objects"), message,
                                     QMessageBox::Ok, QMessageBox::Cancel);
    if (b == QMessageBox::Cancel)
        return;

    WorldDocument *worldDoc = cellDoc->worldDocument();
    worldDoc->undoStack()->beginMacro(tr("Extract Objects"));

    World *world = worldDoc->world();
    WorldCell *cell = cellDoc->cell();
    Map *map = cellDoc->scene()->map();
    foreach (ObjectGroup *og, map->objectGroups()) {
        foreach (MapObject *o, og->objects()) {
            // Note: object name/type reversed in TileZed
            if (o->name() != QLatin1String("lot") && !o->name().isEmpty()) {
                // Create a new ObjectGroup if needed
                WorldObjectGroup *group = world->nullObjectGroup();
                QString name = MapComposite::layerNameWithoutPrefix(og->name());
                if (!name.isEmpty()) {
                    group = world->objectGroups().find(name);
                    if (!group) {
                        group = new WorldObjectGroup(world, name);
                        worldDoc->addObjectGroup(group);
                    }
                }
                // Create a new ObjectType if needed
                ObjectType *type = world->objectTypes().find(o->name());
                if (!type) {
                    type = new ObjectType(o->name());
                    worldDoc->addObjectType(type);
                }
                // Adjust coordinates to whole numbers
                // I'm trying to match what PZ's Lot Creator does.
                int x = qFloor(o->x()), y = qFloor(o->y());
                int x2 = qCeil(o->x() + o->width()), y2 = qCeil(o->y() + o->height());
                int width = x2 - x, height = y2 - y;
                width = qMax(MIN_OBJECT_SIZE, qreal(width));
                height = qMax(MIN_OBJECT_SIZE, qreal(height));

                // Adjust for map orientation
                if (map->orientation() == Map::Isometric) {
                    x += 3 * og->level();
                    y += 3 * og->level();
                }

                WorldCellObject *obj = new WorldCellObject(cell, o->type(),
                                                           type, group, x, y,
                                                           og->level(),
                                                           width, height);
                WorldObjectValidation::applyCreationDefaults(obj);
                worldDoc->addCellObject(cell, cell->objects().size(), obj);
            }
        }
    }

    worldDoc->undoStack()->endMacro();
}

void MainWindow::generateInGameMapBuildingFeatures()
{
    if (auto* cellDoc = mCurrentDocument->asCellDocument()) {
        cellDoc->worldDocument()->setSelectedCells(QList<WorldCell*>() << cellDoc->cell());
        InGameMapFeatureGenerator generator;
        generator.generateWorld(cellDoc->worldDocument(), InGameMapFeatureGenerator::GenerateSelected, InGameMapFeatureGenerator::FeatureBuilding);
    }

    if (auto* worldDoc = mCurrentDocument->asWorldDocument()) {
        InGameMapFeatureGenerator generator;
        generator.generateWorld(worldDoc, InGameMapFeatureGenerator::GenerateSelected, InGameMapFeatureGenerator::FeatureBuilding);
    }
}

void MainWindow::generateInGameMapTreeFeatures()
{
    if (auto* cellDoc = mCurrentDocument->asCellDocument()) {
        cellDoc->worldDocument()->setSelectedCells(QList<WorldCell*>() << cellDoc->cell());
        InGameMapFeatureGenerator generator;
        generator.generateWorld(cellDoc->worldDocument(), InGameMapFeatureGenerator::GenerateSelected, InGameMapFeatureGenerator::FeatureTree);
    }

    if (auto* worldDoc = mCurrentDocument->asWorldDocument()) {
        InGameMapFeatureGenerator generator;
        generator.generateWorld(worldDoc, InGameMapFeatureGenerator::GenerateSelected, InGameMapFeatureGenerator::FeatureTree);
    }
}

void MainWindow::generateInGameMapWaterFeatures()
{
    if (auto* cellDoc = mCurrentDocument->asCellDocument()) {
        cellDoc->worldDocument()->setSelectedCells(QList<WorldCell*>() << cellDoc->cell());
        InGameMapFeatureGenerator generator;
        generator.generateWorld(cellDoc->worldDocument(), InGameMapFeatureGenerator::GenerateSelected, InGameMapFeatureGenerator::FeatureWater);
    }

    if (auto* worldDoc = mCurrentDocument->asWorldDocument()) {
        InGameMapFeatureGenerator generator;
        generator.generateWorld(worldDoc, InGameMapFeatureGenerator::GenerateSelected, InGameMapFeatureGenerator::FeatureWater);
    }
}

void MainWindow::removeInGameMapFeatures()
{
    if (mCurrentDocument == nullptr) {
        return;
    }
    if (auto* cellDoc = mCurrentDocument->asCellDocument()) {
        cellDoc->undoStack()->beginMacro(tr("Remove InGameMap Features"));
        auto selected = cellDoc->selectedInGameMapFeatures();
        for (auto* feature : selected) {
            cellDoc->worldDocument()->removeInGameMapFeature(cellDoc->cell(), feature->index());
        }
        cellDoc->undoStack()->endMacro();
    }
    if (auto* worldDoc = mCurrentDocument->asWorldDocument()) {
        worldDoc->undoStack()->beginMacro(tr("Remove InGameMap Features"));
        auto selected = worldDoc->selectedInGameMapFeatures();
        for (auto* feature : selected) {
            worldDoc->removeInGameMapFeature(feature->cell(), feature->index());
        }
        worldDoc->undoStack()->endMacro();
    }
}

bool MainWindow::canSplitInGameMapPolygon()
{
    if (mCurrentDocument == nullptr) {
        return false;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    if (cellDoc == nullptr) {
        return false;
    }
    auto& features = cellDoc->selectedInGameMapFeatures();
    if (features.size() != 1) {
        return false;
    }
    InGameMapFeature* feature = features.first();
    if (feature->mGeometry.isPolygon() == false) {
        return false;
    }
    auto& selection = cellDoc->selectedInGameMapPoints();
    if (selection.size() != 2) {
        return false;
    }
    int index1 = selection.first();
    int index2 = selection.last();
    if (index1 > index2) {
        qSwap(index1, index2);
    }

    InGameMapFeatureItem *featureItem = cellDoc->scene()->itemForInGameMapFeature(feature);
    if (featureItem == nullptr) {
        return false;
    }
    int coordIndex = featureItem->selectedCoordIndex();
    if (coordIndex != 0) {
        return false;
    }
    const InGameMapCoordinates& srcCoords = feature->mGeometry.mCoordinates[coordIndex];
    int numCoords2 = index2 - index1 + 1;
    int numCoords1 = srcCoords.size() - numCoords2 + 2;

    if (numCoords1 < 3 || numCoords2 < 3) {
        return false;
    }
    return true;
}

void MainWindow::splitInGameMapPolygon()
{
    if (canSplitInGameMapPolygon() == false) {
        return;
    }
    auto* worldDoc = currentWorldDocument();
    auto* cellDoc = mCurrentDocument->asCellDocument();
    InGameMapFeature* feature = cellDoc->selectedInGameMapFeatures().first();
    InGameMapFeatureItem *featureItem = cellDoc->scene()->itemForInGameMapFeature(feature);
    auto& selection = cellDoc->selectedInGameMapPoints();
    int index1 = selection.first();
    int index2 = selection.last();
    if (index1 > index2) {
        qSwap(index1, index2);
    }

    int coordIndex = featureItem->selectedCoordIndex();
    const InGameMapCoordinates& srcCoords = feature->mGeometry.mCoordinates[coordIndex];
    int numCoords2 = index2 - index1 + 1;
    int numCoords1 = srcCoords.size() - numCoords2 + 2;

    InGameMapCoordinates coords1;
    for (int i = 0; i < numCoords1; i++) {
        int index = (index2 + i) % srcCoords.size();
        coords1 << srcCoords[index];
    }

    InGameMapCoordinates coords2;
    std::copy(srcCoords.begin() + index1, srcCoords.begin() + index2 + 1, std::back_inserter(coords2));

    InGameMapFeature* feature2 = new InGameMapFeature(&cellDoc->cell()->inGameMap());
    InGameMapGeometry& geom = feature2->mGeometry;
    geom.mType = QLatin1String("Polygon");
    geom.mCoordinates << coords2;
    feature2->mProperties = feature->properties();

    worldDoc->undoStack()->beginMacro(tr("Split Polygon"));
    worldDoc->setInGameMapCoordinates(cellDoc->cell(), feature->index(), coordIndex, coords1);
    worldDoc->addInGameMapFeature(cellDoc->cell(), cellDoc->cell()->inGameMap().features().size(), feature2);
    worldDoc->undoStack()->endMacro();
}

void MainWindow::convertInGameMapPolylineToPolygon()
{
    if (canConvertToInGameMapPolygon() == false) {
        return;
    }
    auto* worldDoc = currentWorldDocument();
    auto* cellDoc = mCurrentDocument->asCellDocument();
    InGameMapFeature* feature = cellDoc->selectedInGameMapFeatures().first();
    worldDoc->convertToInGameMapPolygon(cellDoc->cell(), feature->index());
}

void MainWindow::addInGameMapHole()
{
    if (canAddInGameMapHole() == false) {
        return;
    }
    auto* worldDoc = currentWorldDocument();
    auto* cellDoc = mCurrentDocument->asCellDocument();
    auto selectedFeatures = cellDoc->selectedInGameMapFeatures();
    InGameMapFeature* feature1 = selectedFeatures.first();
    InGameMapFeature* feature2 = selectedFeatures.last();

    worldDoc->undoStack()->beginMacro(tr("Add InGameMap Hole"));
    InGameMapCoordinates hole = feature2->mGeometry.mCoordinates.first();
    worldDoc->removeInGameMapFeature(cellDoc->cell(), feature2->index());
    worldDoc->addInGameMapHole(cellDoc->cell(), feature1->index(), feature1->mGeometry.mCoordinates.size(), hole);
    worldDoc->undoStack()->endMacro();
}

void MainWindow::removeInGameMapHole()
{
    if (canRemoveInGameMapHole() == false) {
        return;
    }
    auto* worldDoc = currentWorldDocument();
    auto* cellDoc = mCurrentDocument->asCellDocument();
    InGameMapFeature* feature = cellDoc->selectedInGameMapFeatures().first();
    InGameMapFeatureItem *featureItem = cellDoc->scene()->itemForInGameMapFeature(feature);
    int coordIndex = featureItem->selectedCoordIndex();
    worldDoc->removeInGameMapHole(cellDoc->cell(), feature->index(), coordIndex);
}

bool MainWindow::canRemoveInGameMapPoint()
{
    if (mCurrentDocument == nullptr) {
        return false;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    if (cellDoc == nullptr) {
        return false;
    }
    auto& features = cellDoc->selectedInGameMapFeatures();
    if (features.size() != 1) {
        return false;
    }
    InGameMapFeature* feature = features.first();
    bool isPolygon = feature->mGeometry.isPolygon();
    bool isLineString = feature->mGeometry.isLineString();
    if (isPolygon == false && isLineString == false) {
        return false;
    }
    auto& selection = cellDoc->selectedInGameMapPoints();
    if (selection.isEmpty()) {
        return false;
    }
    InGameMapFeatureItem *featureItem = cellDoc->scene()->itemForInGameMapFeature(feature);
    if (featureItem == nullptr) {
        return false;
    }
    int coordIndex = featureItem->selectedCoordIndex();
    if (coordIndex < 0 || coordIndex >= feature->mGeometry.mCoordinates.size()) {
        return false;
    }
    const InGameMapCoordinates& coords = feature->mGeometry.mCoordinates[coordIndex];
    if (isPolygon) {
        return coords.size() - selection.size() >= 3;
    }
    if (isLineString) {
        return coords.size() - selection.size() >= 2;
    }
    return true;
}

bool MainWindow::canAddInGameMapHole()
{
    if (mCurrentDocument == nullptr) {
        return false;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    if (cellDoc == nullptr) {
        return false;
    }
    auto& features = cellDoc->selectedInGameMapFeatures();
    if (features.size() != 2) {
        return false;
    }
    InGameMapFeature* feature1 = features.first();
    InGameMapFeature* feature2 = features.last();
    if ((feature1->mGeometry.isPolygon() == false) || (feature2->mGeometry.isPolygon() == false)) {
        return false;
    }
    // TODO: forbid holes in holes ad infinitum
    return true;
}

bool MainWindow::canRemoveInGameMapHole()
{
    if (mCurrentDocument == nullptr) {
        return false;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    if (cellDoc == nullptr) {
        return false;
    }
    auto& features = cellDoc->selectedInGameMapFeatures();
    if (features.size() != 1) {
        return false;
    }
    InGameMapFeature* feature = features.first();
    bool isPolygon = feature->mGeometry.isPolygon();
    if (isPolygon == false) {
        return false;
    }
    InGameMapFeatureItem *featureItem = cellDoc->scene()->itemForInGameMapFeature(feature);
    if (featureItem == nullptr) {
        return false;
    }
    int coordIndex = featureItem->selectedCoordIndex();
    if (coordIndex < 0 || coordIndex >= feature->mGeometry.mCoordinates.size()) {
        return false;
    }
    return coordIndex > 0;
}

bool MainWindow::canConvertToInGameMapPolygon()
{
    if (mCurrentDocument == nullptr) {
        return false;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    if (cellDoc == nullptr) {
        return false;
    }
    auto& features = cellDoc->selectedInGameMapFeatures();
    if (features.size() != 1) {
        return false;
    }
    InGameMapFeature* feature = features.first();
    return feature->mGeometry.isLineString();
}

void MainWindow::removeInGameMapPoint()
{
    if (canRemoveInGameMapPoint() == false) {
        return;
    }
    auto* cellDoc = mCurrentDocument->asCellDocument();
    InGameMapFeature* feature = cellDoc->selectedInGameMapFeatures().first();
    InGameMapFeatureItem *featureItem = cellDoc->scene()->itemForInGameMapFeature(feature);
    int coordIndex = featureItem->selectedCoordIndex();
    InGameMapCoordinates coords = feature->mGeometry.mCoordinates[coordIndex];
    QList<int> selection = cellDoc->selectedInGameMapPoints();
    std::sort(selection.begin(), selection.end());
    for (int i = selection.size() - 1; i >= 0; i--) {
        int index = selection[i];
        if (index >= 0 && index < coords.size()) {
            coords.removeAt(index);
        }
    }
    cellDoc->setSelectedInGameMapPoints(QList<int>());
    cellDoc->worldDocument()->setInGameMapCoordinates(cellDoc->cell(), feature->index(), coordIndex, coords);
}

void MainWindow::readInGameMapFeaturesXML()
{
    WorldDocument* worldDoc = currentWorldDocument();
    World* world = worldDoc->world();

    QString filter = tr("All Files (*)");
    filter += QLatin1String(";;");

    QString selectedFilter = tr("XML files (*.xml)");
    filter += selectedFilter;

    QString fileName = QFileDialog::getOpenFileName(this, tr("Read Features XML"),
                                                    worldDoc->getInGameMapXMLFileName(),
                                                    filter, &selectedFilter);
    if (fileName.isEmpty()) {
        return;
    }

    QString absolutePath = QFileInfo(fileName).absoluteFilePath();
    worldDoc->setInGameMapXMLFileName(absolutePath);
    Preferences::instance()->setWorldMapXMLFile(absolutePath);

    PROGRESS progress(QStringLiteral("Reading InGameMap XML"), this);

    worldDoc->undoStack()->beginMacro(tr("Read InGameMap XML"));
    for (auto* cell : world->cells()) {
        for (int i = cell->inGameMap().features().size() - 1; i >= 0; i--) {
            worldDoc->removeInGameMapFeature(cell, i);
        }
    }

    InGameMapReader mbreader;
    mbreader.readWorld(fileName, world);

    for (auto* cell : world->cells()) {
        InGameMapFeatures features = cell->inGameMap().features();
        cell->inGameMap().mFeatures.clear();
        for (int i = 0, n = features.size(); i < n; i++) {
            worldDoc->addInGameMapFeature(cell, i, features.at(i));
        }
        features.clear();
    }

    worldDoc->undoStack()->endMacro();

    bool bFeatureToolActive = dynamic_cast<BaseInGameMapFeatureTool*>(ToolManager::instance()->selectedTool()) != nullptr;
    if (bFeatureToolActive == false && EditInGameMapFeatureTool::instance().isEnabled()) {
        ToolManager::instance()->selectTool(EditInGameMapFeatureTool::instancePtr());
    }

    updateActions();
}

void MainWindow::loadWorldMapOverlay(bool forest)
{
    WorldDocument *worldDoc = currentWorldDocument();
    WorldView *view = worldDoc
            ? dynamic_cast<WorldView *>(worldDoc->view()) : nullptr;
    if (!view)
        return;

    const QString settingsKey = forest
            ? QLatin1String("WorldMapOverlay/ForestPath")
            : QLatin1String("WorldMapOverlay/WorldPath");
    QString suggested = mSettings.value(settingsKey).toString();
    if (suggested.isEmpty()) {
        const QString directory =
                worldDoc->world()->getGenerateLotsSettings().exportDir;
        suggested = QDir(directory).filePath(forest
                ? QStringLiteral("worldmap-forest.xml")
                : QStringLiteral("worldmap.xml"));
    }
    const QString title = forest
            ? tr("Load worldmap-forest.xml Overlay")
            : tr("Load worldmap.xml Overlay");
    const QString fileName = QFileDialog::getOpenFileName(
                this, title, suggested, tr("World map XML (*.xml)"));
    if (fileName.isEmpty())
        return;

    PROGRESS progress(tr("Loading world-map overlay"), this);
    QString error;
    if (!view->scene()->loadWorldMapOverlay(fileName, forest, &error)) {
        progress.release();
        QMessageBox::critical(
                    this, tr("Unable to Load World Map Overlay"),
                    tr("%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName), error));
        return;
    }
    mSettings.setValue(settingsKey, QFileInfo(fileName).absoluteFilePath());

    if (!forest) {
        const QString companion = QFileInfo(fileName).dir().filePath(
                    QStringLiteral("worldmap-forest.xml"));
        if (QFileInfo::exists(companion)) {
            QString companionError;
            if (view->scene()->loadWorldMapOverlay(
                        companion, true, &companionError)) {
                mSettings.setValue(
                            QLatin1String("WorldMapOverlay/ForestPath"),
                            QFileInfo(companion).absoluteFilePath());
            } else {
                qWarning().noquote()
                        << "Worldmap companion overlay was not loaded:"
                        << companionError;
            }
        }
    }

    progress.release();
    statusBar()->showMessage(
                tr("Loaded world-map overlay: %1")
                .arg(QFileInfo(fileName).fileName()), 5000);
    updateActions();
}

void MainWindow::clearCells()
{
    Q_ASSERT(mCurrentDocument);
    WorldDocument *worldDoc = nullptr;
    QList<WorldCell*> cells;
    if ((worldDoc = mCurrentDocument->asWorldDocument())) {
        Q_ASSERT(worldDoc->selectedCellCount());
        cells = worldDoc->selectedCells();
    }
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        cells += cellDoc->cell();
        worldDoc = cellDoc->worldDocument();
    }
    int count = cells.size();
    if (!worldDoc || !count) // none of these should ever be true
        return;
    QUndoStack *undoStack = worldDoc->undoStack();
    undoStack->beginMacro(tr("Clear %1 Cell%2").arg(count).arg((count > 1) ? QLatin1String("s") : QLatin1String("")));
    foreach (WorldCell *cell, cells)
        worldDoc->clearCell(cell);
    undoStack->endMacro();
}

void MainWindow::clearMapOnly()
{
    Q_ASSERT(mCurrentDocument);
    WorldDocument *worldDoc = nullptr;
    QList<WorldCell*> cells;
    if ((worldDoc = mCurrentDocument->asWorldDocument())) {
        Q_ASSERT(worldDoc->selectedCellCount());
        cells = worldDoc->selectedCells();
    }
    if (CellDocument *cellDoc = mCurrentDocument->asCellDocument()) {
        cells += cellDoc->cell();
        worldDoc = cellDoc->worldDocument();
    }
    int count = cells.size();
    if (!worldDoc || !count) // none of these should ever be true
        return;
    QUndoStack *undoStack = worldDoc->undoStack();
    undoStack->beginMacro(tr("Clear %1 Cell%2 Map").arg(count).arg((count > 1) ? QLatin1String("s") : QLatin1String("")));
    foreach (WorldCell *cell, cells)
        worldDoc->setCellMapName(cell, QString());
    undoStack->endMacro();
}

QRect MainWindow::retainedWorldBounds(WorldDocument *worldDoc) const
{
    if (!worldDoc)
        return QRect();

    World *world = worldDoc->world();
    QRect retained;
    bool hasContent = false;
    auto includeBounds = [&](const QRect &bounds) {
        const QRect clipped = bounds.intersected(world->bounds());
        if (clipped.isEmpty())
            return;
        retained = hasContent ? retained.united(clipped) : clipped;
        hasContent = true;
    };
    for (WorldCell *cell : world->cells()) {
        const bool hasProjectContent =
                !cell->lots().isEmpty() ||
                !cell->objects().isEmpty() ||
                !cell->properties().isEmpty() ||
                !cell->templates().isEmpty() ||
                !cell->inGameMap().features().isEmpty();
        if (hasProjectContent || tmxContainsMapContent(cell->mapFilePath()))
            includeBounds(QRect(cell->pos(), QSize(1, 1)));
    }
    const int cellSize = world->cellSize();
    for (Road *road : world->roads()) {
        const QRect bounds = road->bounds();
        const QPoint first(floorDivision(bounds.left(), cellSize),
                           floorDivision(bounds.top(), cellSize));
        const QPoint last(floorDivision(bounds.right(), cellSize),
                          floorDivision(bounds.bottom(), cellSize));
        includeBounds(QRect(first, last));
    }
    for (WorldBMP *bmp : world->bmps())
        includeBounds(bmp->bounds());
    return hasContent ? retained : QRect();
}

bool MainWindow::canRemoveEmptyBorderCells() const
{
    WorldDocument *worldDoc = mCurrentDocument
            ? mCurrentDocument->asWorldDocument() : nullptr;
    return worldDoc && worldDoc->world()
            && worldDoc->world()->width() * worldDoc->world()->height() > 1;
}

void MainWindow::removeEmptyBorderCells()
{
    WorldDocument *worldDoc = mCurrentDocument
            ? mCurrentDocument->asWorldDocument() : nullptr;
    if (!worldDoc)
        return;
    World *world = worldDoc->world();
    const QRect retained = retainedWorldBounds(worldDoc);
    if (retained.isEmpty()) {
        QMessageBox::information(
                    this, tr("Remove Empty Border Cells"),
                    tr("The world contains no identifiable map content. "
                       "At least one cell must remain, so nothing was removed."));
        return;
    }
    if (retained == world->bounds()) {
        QMessageBox::information(
                    this, tr("Remove Empty Border Cells"),
                    tr("No removable empty border cells were found. "
                       "Empty cells inside the rectangular world cannot be removed individually."));
        return;
    }
    const int removed = world->width() * world->height() -
            retained.width() * retained.height();
    const QPoint oldOrigin = world->getGenerateLotsSettings().worldOrigin;
    const QPoint newOrigin = oldOrigin + retained.topLeft();
    const QString message =
            tr("This will shrink the world from %1 × %2 to %3 × %4 cells and remove "
               "%5 empty border cell(s).\n\n"
               "World origin: %6,%7 → %8,%9\n\n"
               "A TMX is considered empty when it is missing or contains no map, "
               "object, image, or group layer. TMX files on disk are not deleted. "
               "Empty cells inside the resulting rectangle must remain because the "
               "PZW format stores a rectangular world.\n\nContinue?")
            .arg(world->width()).arg(world->height())
            .arg(retained.width()).arg(retained.height())
            .arg(removed)
            .arg(oldOrigin.x()).arg(oldOrigin.y())
            .arg(newOrigin.x()).arg(newOrigin.y());
    if (QMessageBox::question(this, tr("Remove Empty Border Cells"), message,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    qInfo() << "Removing empty PZW border cells:"
            << "old-size" << world->size()
            << "retained-bounds" << retained
            << "old-origin" << oldOrigin
            << "new-origin" << newOrigin;
    worldDoc->trimWorldToBounds(retained);
    statusBar()->showMessage(
                tr("Removed %1 empty border cell(s); world is now %2 × %3 at origin %4,%5.")
                .arg(removed)
                .arg(world->width()).arg(world->height())
                .arg(newOrigin.x()).arg(newOrigin.y()), 15000);
}

void MainWindow::checkForHoles()
{
    if (!mCurrentDocument) {
        return;
    }
    if (CellDocument *doc = mCurrentDocument->asCellDocument()) {
        CellScene *scene = doc->scene();
        if (scene->partialChunksEnabled())
            return;
        scene->checkHolesOnLevelZero();
        if (doc->view()->miniMap()) {
            doc->view()->miniMap()->update();
        }
        const int holeCount = scene->holeInFloor().size();
        if (holeCount == 0) {
            QMessageBox::information(
                        this, tr("Hole Detection"),
                        tr("Every level-zero coordinate contains at least "
                           "one tile."));
            return;
        }
        QMessageBox dialog(
                    QMessageBox::Warning,
                    tr("Hole Detection"),
                    tr("%1 coordinate(s) contain no tile on level zero or "
                       "an available basement level.\n\n"
                       "They are highlighted in red. Automatic repair copies "
                       "the nearest floor tile already present on the "
                       "current TMX Floor layer. Lots and building files are "
                       "not changed. A timestamped backup is created first.")
                    .arg(holeCount),
                    QMessageBox::NoButton,
                    this);
        QPushButton *fixButton = dialog.addButton(
                    tr("Fix Automatically"), QMessageBox::AcceptRole);
        dialog.addButton(tr("Keep Highlighted"), QMessageBox::RejectRole);
        dialog.setDefaultButton(fixButton);
        dialog.exec();
        if (dialog.clickedButton() != fixButton)
            return;
        QString backupPath;
        QString error;
        const int repaired =
                scene->autoFixHolesOnLevelZero(&backupPath, &error);
        if (repaired == 0) {
            QMessageBox::warning(
                        this, tr("Hole Repair Failed"), error);
            return;
        }
        scene->checkHolesOnLevelZero();
        if (doc->view()->miniMap())
            doc->view()->miniMap()->update();
        QMessageBox::information(
                    this, tr("Hole Repair Complete"),
                    tr("%1 square(s) were repaired.\n\nBackup:\n%2")
                    .arg(repaired)
                    .arg(QDir::toNativeSeparators(backupPath)));
    }
}

void MainWindow::setPartialChunksEnabled(bool enabled)
{
    CellDocument *doc = mCurrentDocument
            ? mCurrentDocument->asCellDocument() : nullptr;
    if (!doc)
        return;
    doc->scene()->setPartialChunksEnabled(enabled);
    updateActions();
}

void MainWindow::selectAllPartialChunks()
{
    CellDocument *doc = mCurrentDocument
            ? mCurrentDocument->asCellDocument() : nullptr;
    if (!doc)
        return;
    doc->scene()->selectAllPartialChunks();
    updateActions();
}

void MainWindow::clearPartialChunks()
{
    CellDocument *doc = mCurrentDocument
            ? mCurrentDocument->asCellDocument() : nullptr;
    if (!doc)
        return;
    doc->scene()->clearPartialChunks();
    updateActions();
}

void MainWindow::createInGameMapFeatureImage()
{
    WorldDocument *worldDoc = currentWorldDocument();
    QString suggestedFileName;
    if (suggestedFileName.isEmpty() || !QFileInfo::exists(suggestedFileName)) {
        if (worldDoc->fileName().isEmpty()) {
            suggestedFileName = QDir::currentPath();
            suggestedFileName += QLatin1String("/forest.png");
        } else {
            const QFileInfo fileInfo(worldDoc->fileName());
            suggestedFileName = fileInfo.path();
            suggestedFileName += QLatin1String("/forest.png");
        }
    }
    const QString fileName = QFileDialog::getSaveFileName(this, QString(), suggestedFileName, tr("PNG files (*.png)"));
    if (fileName.isEmpty()) {
        return;
    }
    QString imageError;
    const QImage image = createForestFeatureImage(
                worldDoc->world(), nullptr, &imageError);
    if (image.isNull()) {
        QMessageBox::critical(
                    this, tr("Unable to Create Feature Image"),
                    imageError);
        return;
    }
    if (!image.save(fileName)) {
        QMessageBox::critical(
                    this, tr("Unable to Save Feature Image"),
                    tr("WorldEd could not write the PNG image.\n\nFile: %1\n"
                       "Check that the destination is writable and that "
                       "enough disk space is available.")
                    .arg(QDir::toNativeSeparators(fileName)));
    }
}

void MainWindow::writeInGameMapFeaturesXML()
{
    WorldDocument *worldDoc = currentWorldDocument();

    QString suggestedFileName = worldDoc->getInGameMapXMLFileName();
    if (suggestedFileName.isEmpty()) {
        suggestedFileName = Preferences::instance()->worldMapXMLFile();
    }
    if (suggestedFileName.isEmpty() || !QFileInfo::exists(suggestedFileName)) {
        if (worldDoc->fileName().isEmpty()) {
            suggestedFileName = QDir::currentPath();
            suggestedFileName += QLatin1String("/worldmap.xml");
        } else {
            const QFileInfo fileInfo(worldDoc->fileName());
            suggestedFileName = fileInfo.path();
            suggestedFileName += QLatin1String("/worldmap.xml");
        }
    }

    const QString fileName = QFileDialog::getSaveFileName(this, QString(), suggestedFileName, tr("XML files (*.xml)"));
    if (fileName.isEmpty()) {
        return;
    }

    const QString absolutePath = QFileInfo(fileName).absoluteFilePath();

    PROGRESS progress(QStringLiteral("Writing InGameMap XML"), this);

    QString error;
    if (!writeInGameMapFilePair(worldDoc->world(), absolutePath, &error)) {
        QMessageBox::critical(
                    this, tr("Unable to Export In-Game Map"),
                    tr("Neither output file was replaced.\n\nXML: %1\n"
                       "Binary: %2\n\n%3")
                    .arg(QDir::toNativeSeparators(absolutePath),
                         QDir::toNativeSeparators(
                             absolutePath + QLatin1String(".bin")),
                         error));
        return;
    }
    worldDoc->setInGameMapXMLFileName(absolutePath);
    Preferences::instance()->setWorldMapXMLFile(absolutePath);
}

void MainWindow::overwriteInGameMapFeaturesXML()
{
    WorldDocument *worldDoc = currentWorldDocument();

    PROGRESS progress(QStringLiteral("Writing InGameMap XML"), this);

    const QString fileName = worldDoc->getInGameMapXMLFileName();
    if (fileName.isEmpty()) {
        QMessageBox::warning(
                    this, tr("No In-Game Map Export"),
                    tr("This project has no previous in-game map export to "
                       "overwrite. Use the export command first."));
        return;
    }
    const QFileInfo exportInfo(fileName);
    QString error;
    if (exportInfo.fileName().compare(
                QStringLiteral("worldmap-forest.xml"),
                Qt::CaseInsensitive) == 0) {
        QStringList writtenFiles;
        if (!writeInGameMapForestBundle(
                    worldDoc->world(), exportInfo.absolutePath(),
                    &writtenFiles, &error)) {
            QMessageBox::critical(
                        this, tr("Unable to Write Worldmap-Forest"),
                        tr("No Forest export file was replaced.\n\n%1")
                        .arg(error));
        }
        return;
    }

    const InGameMapFeatureScope scope =
            exportInfo.fileName().compare(
                QStringLiteral("worldmap.xml"),
                Qt::CaseInsensitive) == 0
            ? InGameMapFeatureScope::NonForestFeatures
            : InGameMapFeatureScope::AllFeatures;
    if (!writeInGameMapFilePair(
                worldDoc->world(), fileName, &error, scope)) {
        QMessageBox::critical(
                    this, tr("Unable to Export In-Game Map"),
                    tr("Neither output file was replaced.\n\nXML: %1\n"
                       "Binary: %2\n\n%3")
                    .arg(QDir::toNativeSeparators(fileName),
                         QDir::toNativeSeparators(
                             fileName + QLatin1String(".bin")),
                         error));
        return;
    }
}

void MainWindow::createInGameMapImage()
{
    InGameMapImageDialog dialog(this);
    dialog.exec();
}

void MainWindow::createInGameMapImagePyramid()
{
    InGameMapImagePyramidWindow *window = new InGameMapImagePyramidWindow(this);
    window->show();
}

bool MainWindow::confirmSave()
{
    if (!mCurrentDocument || !mCurrentDocument->isModified())
        return true;

    int ret = QMessageBox::warning(
            this, tr("Unsaved Changes"),
            tr("There are unsaved changes. Do you want to save now?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    switch (ret) {
    case QMessageBox::Save:    return saveFile();
    case QMessageBox::Discard: return true;
    case QMessageBox::Cancel:
    default:
        return false;
    }
}

bool MainWindow::confirmAllSave()
{
    foreach (Document *doc, docman()->documents()) {
        if (!doc->isModified())
            continue;
        docman()->setCurrentDocument(doc);
        if (!confirmSave())
            return false;
    }

    return true;
}

void MainWindow::writeSettings()
{
    writeWindowSettings();

    mSettings.beginGroup(QLatin1String("openFiles"));
    const auto setValueIfChanged =
            [this](const QString &key, const QVariant &value) {
        if (!mSettings.contains(key) || mSettings.value(key) != value) {
            mSettings.setValue(key, value);
        }
    };
    const auto removeIfPresent = [this](const QString &key) {
        if (mSettings.contains(key)) {
            mSettings.remove(key);
        }
    };

    if (mCurrentDocument) {
        setValueIfChanged(QLatin1String("lastActive"),
                          docman()->indexOf(mCurrentDocument));
    } else {
        removeIfPresent(QLatin1String("lastActive"));
    }

    int i = 0;
    foreach (Document *doc, docman()->documents()) {
        BaseGraphicsView *view = doc->view();
        if (!view || !view->viewport() || !view->zoomable()) {
            qWarning() << "Not saving an incomplete document in the session:"
                       << doc->fileName();
            continue;
        }
        mSettings.beginGroup(QString::number(i)); // openFiles/N/...

        setValueIfChanged(QLatin1String("file"), doc->fileName());

        setValueIfChanged(QLatin1String("scale"),
                          QString::number(view->zoomable()->scale()));

        QPointF centerScenePos = view->mapToScene(view->viewport()->width() / 2,
                                                  view->viewport()->height() / 2);
        setValueIfChanged(QLatin1String("scrollX"),
                          QString::number(int(centerScenePos.x())));
        setValueIfChanged(QLatin1String("scrollY"),
                          QString::number(int(centerScenePos.y())));

        if (CellDocument *cellDoc = doc->asCellDocument()) {
            setValueIfChanged(QLatin1String("cellX"),
                              QString::number(cellDoc->cell()->x()));
            setValueIfChanged(QLatin1String("cellY"),
                              QString::number(cellDoc->cell()->y()));
            const int currentLayerIndex = cellDoc->currentLayerIndex();
            setValueIfChanged(QLatin1String("currentLayer"),
                              QString::number(currentLayerIndex));
        } else {
            removeIfPresent(QLatin1String("cellX"));
            removeIfPresent(QLatin1String("cellY"));
            removeIfPresent(QLatin1String("currentLayer"));
        }
        mSettings.endGroup();
        ++i;
    }

    foreach (const QString &group, mSettings.childGroups()) {
        bool isDocumentGroup = false;
        const int documentIndex = group.toInt(&isDocumentGroup);
        if (isDocumentGroup && documentIndex >= i)
            mSettings.remove(group);
    }
    setValueIfChanged(QLatin1String("count"), i);
    mSettings.endGroup();
    mSettings.sync();
}
void MainWindow::generateInGameMapRoadFeatures()
{
    if (auto *cellDoc = mCurrentDocument->asCellDocument()) {
        cellDoc->worldDocument()->setSelectedCells(QList<WorldCell*>() << cellDoc->cell());
        InGameMapFeatureGenerator generator;
        generator.generateWorld(cellDoc->worldDocument(), InGameMapFeatureGenerator::GenerateSelected,
                                InGameMapFeatureGenerator::FeatureRoad);
    }
    if (auto *worldDoc = mCurrentDocument->asWorldDocument()) {
        InGameMapFeatureGenerator generator;
        generator.generateWorld(worldDoc, InGameMapFeatureGenerator::GenerateSelected,
                                InGameMapFeatureGenerator::FeatureRoad);
    }
}
void MainWindow::writeInGameMapForest()
{
    WorldDocument *worldDoc = currentWorldDocument();
    if (!worldDoc)
        return;
    QString suggestedDirectory = mSettings.value(
                QLatin1String("InGameMap/ForestOutputDirectory"))
            .toString();
    if (suggestedDirectory.isEmpty()) {
        const QString previousExport =
                worldDoc->getInGameMapXMLFileName();
        if (!previousExport.isEmpty())
            suggestedDirectory = QFileInfo(previousExport).absolutePath();
        else if (!worldDoc->fileName().isEmpty())
            suggestedDirectory = QFileInfo(worldDoc->fileName()).absolutePath();
        else
            suggestedDirectory = QDir::currentPath();
    }
    const QString outputDirectory = QFileDialog::getExistingDirectory(
                this, tr("Choose Worldmap-Forest Output Folder"),
                suggestedDirectory);
    if (outputDirectory.isEmpty())
        return;
    PROGRESS progress(QStringLiteral("Writing Worldmap-Forest"), this);
    QStringList writtenFiles;
    QString error;
    if (!writeInGameMapForestBundle(
                worldDoc->world(), outputDirectory,
                &writtenFiles, &error)) {
        QMessageBox::critical(
                    this, tr("Unable to Write Worldmap-Forest"),
                    tr("No Forest export file was replaced.\n\n%1")
                    .arg(error));
        return;
    }
    mSettings.setValue(
                QLatin1String("InGameMap/ForestOutputDirectory"),
                outputDirectory);
    QStringList displayedFiles;
    for (const QString &path : std::as_const(writtenFiles))
        displayedFiles += QDir::toNativeSeparators(path);
    QMessageBox::information(
                this, tr("Worldmap-Forest Complete"),
                tr("WorldEd wrote the Forest feature data and the actual "
                   "game-compatible Forest image pyramid:\n\n%1")
                .arg(displayedFiles.join(QLatin1Char('\n'))));
}
void MainWindow::writeInGameMapWorldMap()
{
    WorldDocument *worldDoc = currentWorldDocument();
    if (!worldDoc)
        return;
    QString suggestedDirectory;
    const QString previousExport = worldDoc->getInGameMapXMLFileName();
    if (!previousExport.isEmpty())
        suggestedDirectory = QFileInfo(previousExport).absolutePath();
    else if (!worldDoc->fileName().isEmpty())
        suggestedDirectory = QFileInfo(worldDoc->fileName()).absolutePath();
    else
        suggestedDirectory = QDir::currentPath();
    const QString outputDirectory = QFileDialog::getExistingDirectory(
                this, tr("Choose Worldmap Output Folder"),
                suggestedDirectory);
    if (outputDirectory.isEmpty())
        return;
    const QString xmlFileName = QDir(outputDirectory).filePath(
                QStringLiteral("worldmap.xml"));
    PROGRESS progress(QStringLiteral("Writing Worldmap"), this);
    QString error;
    if (!writeInGameMapFilePair(
                worldDoc->world(), xmlFileName, &error,
                InGameMapFeatureScope::NonForestFeatures)) {
        QMessageBox::critical(
                    this, tr("Unable to Write Worldmap"),
                    tr("Neither output file was replaced.\n\nXML: %1\n"
                       "Binary: %2\n\n%3")
                    .arg(QDir::toNativeSeparators(xmlFileName),
                         QDir::toNativeSeparators(
                             xmlFileName + QLatin1String(".bin")),
                         error));
        return;
    }
    worldDoc->setInGameMapXMLFileName(xmlFileName);
    Preferences::instance()->setWorldMapXMLFile(xmlFileName);
    QMessageBox::information(
                this, tr("Worldmap Complete"),
                tr("WorldEd wrote the non-Forest feature data:\n\n%1\n%2")
                .arg(QDir::toNativeSeparators(xmlFileName),
                     QDir::toNativeSeparators(
                         xmlFileName + QLatin1String(".bin"))));
}

void MainWindow::editWorldMapAnnotations()
{
    QString suggested = mSettings.value(
                QLatin1String("InGameMap/WorldMapAnnotationsFile"))
            .toString();
    if (WorldDocument *worldDoc = currentWorldDocument()) {
        const QDir projectDir(QFileInfo(worldDoc->fileName()).absolutePath());
        const QString direct = projectDir.filePath(
                    QLatin1String("worldmap-annotations.lua"));
        const QString lots = projectDir.filePath(
                    QLatin1String("lots/worldmap-annotations.lua"));
        if (QFileInfo::exists(direct))
            suggested = direct;
        else if (QFileInfo::exists(lots))
            suggested = lots;
        else if (suggested.isEmpty())
            suggested = direct;
    }
    WorldMapAnnotationsDialog dialog(suggested, this);
    dialog.exec();
    if (!dialog.fileName().isEmpty()) {
        mSettings.setValue(QLatin1String("InGameMap/WorldMapAnnotationsFile"),
                           dialog.fileName());
    }
}
void MainWindow::writeWindowSettings()
{
    if (!PortableSettings::shouldPersistMainWindowGeometry(this)) {
        qInfo() << "One-shot main-window session: persistent window layout skipped";
        return;
    }
    mSettings.beginGroup(QLatin1String("MainWindow"));
    const auto setValueIfChanged =
            [this](const QString &key, const QVariant &value) {
        if (!mSettings.contains(key) || mSettings.value(key) != value) {
            mSettings.setValue(key, value);
        }
    };
    setValueIfChanged(QLatin1String("geometry"), saveGeometry());
    setValueIfChanged(QLatin1String("state"), saveState());
    QDockWidget *leftTopDock = visibleDockInTabGroup(this, mObjectsDock);
    QDockWidget *leftBottomDock = visibleDockInTabGroup(this, mSearchDock);
    QDockWidget *rightTopDock = visibleDockInTabGroup(this, mPropertiesDock);
    QDockWidget *rightBottomDock = visibleDockInTabGroup(this, mUndoDock);
    const int leftWidth = qMax(leftTopDock->width(), leftBottomDock->width());
    const int rightWidth = qMax(rightTopDock->width(), rightBottomDock->width());
    setValueIfChanged(QLatin1String("leftDockWidth"), leftWidth);
    setValueIfChanged(QLatin1String("rightDockWidth"), rightWidth);
    setValueIfChanged(QLatin1String("leftTopDockHeight"), leftTopDock->height());
    setValueIfChanged(QLatin1String("leftBottomDockHeight"), leftBottomDock->height());
    setValueIfChanged(QLatin1String("rightTopDockHeight"), rightTopDock->height());
    setValueIfChanged(QLatin1String("rightBottomDockHeight"), rightBottomDock->height());
    mSettings.endGroup();
}

void MainWindow::readSettings()
{
    mSettings.beginGroup(QLatin1String("MainWindow"));
    QByteArray geom = mSettings.value(QLatin1String("geometry")).toByteArray();
    if (!geom.isEmpty()) {
        const bool restored = restoreGeometry(geom);
        qInfo() << "Main-window geometry restored:" << restored;
    }
    else
        resize(800, 600);
    const QByteArray state = mSettings.value(QLatin1String("state"),
                                             QByteArray()).toByteArray();
    if (!state.isEmpty())
        qInfo() << "Main-window dock layout restored:" << restoreState(state);
    const int leftWidth = mSettings.value(QLatin1String("leftDockWidth"), 0).toInt();
    const int rightWidth = mSettings.value(QLatin1String("rightDockWidth"), 0).toInt();
    const int leftTopHeight = mSettings.value(QLatin1String("leftTopDockHeight"), 0).toInt();
    const int leftBottomHeight = mSettings.value(QLatin1String("leftBottomDockHeight"), 0).toInt();
    const int rightTopHeight = mSettings.value(QLatin1String("rightTopDockHeight"), 0).toInt();
    const int rightBottomHeight = mSettings.value(QLatin1String("rightBottomDockHeight"), 0).toInt();
    const QString streetDockDefaultKey =
            QLatin1String("streetNamesDockDefaultApplied");
    const bool selectObjectsByDefault =
            !mSettings.value(streetDockDefaultKey, false).toBool();
    if (selectObjectsByDefault)
        mSettings.setValue(streetDockDefaultKey, true);
    const QString streetDockSearchTabKey =
            QLatin1String("streetNamesDockSearchTabApplied");
    const bool dockStreetNamesWithSearch =
            !mSettings.value(streetDockSearchTabKey, false).toBool();
    if (dockStreetNamesWithSearch)
        mSettings.setValue(streetDockSearchTabKey, true);
    const QString regionsDockSearchTabKey =
            QLatin1String("regionsDockSearchTabApplied");
    const bool dockRegionsWithSearch =
            !mSettings.value(regionsDockSearchTabKey, false).toBool();
    if (dockRegionsWithSearch)
        mSettings.setValue(regionsDockSearchTabKey, true);
    mSettings.endGroup();
    PortableSettings::applyOneShotMainWindowGeometry(this);
    QTimer::singleShot(0, this, [this, leftWidth, rightWidth,
                                leftTopHeight, leftBottomHeight,
                                rightTopHeight, rightBottomHeight,
                                selectObjectsByDefault,
                                dockStreetNamesWithSearch,
                                dockRegionsWithSearch]() {
        if (dockStreetNamesWithSearch) {
            mStreetNamesDock->setFloating(false);
            addDockWidget(Qt::LeftDockWidgetArea, mStreetNamesDock);
            tabifyDockWidget(mSearchDock, mStreetNamesDock);
        }
        if (dockRegionsWithSearch) {
            mRegionsDock->setFloating(false);
            addDockWidget(Qt::LeftDockWidgetArea, mRegionsDock);
            tabifyDockWidget(mStreetNamesDock, mRegionsDock);
        }
        if (dockStreetNamesWithSearch || dockRegionsWithSearch) {
            mSearchDock->raise();
        }
        if (selectObjectsByDefault)
            mObjectsDock->raise();
        if (leftWidth > 0) {
            QList<QDockWidget*> docks;
            docks << mObjectsDock;
            resizeDocks(docks, QList<int>() << leftWidth, Qt::Horizontal);
        }
        if (rightWidth > 0) {
            QList<QDockWidget*> docks;
            docks << mPropertiesDock;
            resizeDocks(docks, QList<int>() << rightWidth, Qt::Horizontal);
        }
        if (leftTopHeight > 0 && leftBottomHeight > 0) {
            QList<QDockWidget*> docks;
            docks << visibleDockInTabGroup(this, mObjectsDock)
                  << visibleDockInTabGroup(this, mSearchDock);
            resizeDocks(docks, QList<int>() << leftTopHeight << leftBottomHeight,
                        Qt::Vertical);
        }
        if (rightTopHeight > 0 && rightBottomHeight > 0) {
            QList<QDockWidget*> docks;
            docks << visibleDockInTabGroup(this, mPropertiesDock)
                  << visibleDockInTabGroup(this, mUndoDock);
            resizeDocks(docks, QList<int>() << rightTopHeight << rightBottomHeight,
                        Qt::Vertical);
        }
        qInfo() << "Dock dimensions restored:"
                << "left" << leftWidth << "right" << rightWidth
                << "left heights" << leftTopHeight << leftBottomHeight
                << "right heights" << rightTopHeight << rightBottomHeight;
    });
//    updateRecentFiles();
}

bool MainWindow::saveFile(const QString &fileName)
{
    if (!mCurrentDocument)
        return false;

    QString error;
    if (!mCurrentDocument->save(fileName, error)) {
        QMessageBox::critical(this, tr("Error Saving Map"), error);
        return false;
    }

    updateWindowTitle();

    // Update tab tooltips
    WorldDocument *worldDoc = mCurrentDocument->asWorldDocument();
    if (!worldDoc && mCurrentDocument->isCellDocument())
        worldDoc = mCurrentDocument->asCellDocument()->worldDocument();
    int pos = 0;
    foreach (Document *doc, docman()->documents()) {
        CellDocument *cellDoc = doc->asCellDocument();
        if ((doc == worldDoc) || (cellDoc && cellDoc->worldDocument() == worldDoc)) {
            ui->documentTabWidget->setTabToolTip(pos, fileName);
        }
        ++pos;
    }

    if (mStreetNamesDock && !mStreetNamesDock->saveForProject())
        return false;
    if (mRegionsDock && !mRegionsDock->saveForProject())
        return false;
//    setRecentFile(fileName);
    return true;
}

void MainWindow::updateActions()
{
    Document *doc = mCurrentDocument;
    bool hasDoc = doc != 0;
    CellDocument *cellDoc = hasDoc ? doc->asCellDocument() : 0;
    WorldDocument *worldDoc = hasDoc ? doc->asWorldDocument() : 0;
    WorldDocument *currentWorldDoc = cellDoc ? cellDoc->worldDocument() : worldDoc;
    World *world = worldDoc ? worldDoc->world() : (cellDoc ? cellDoc->world() : nullptr);
    bool hasCellDoc = cellDoc != nullptr;
    CellScene *partialScene = cellDoc ? cellDoc->scene() : nullptr;
    const bool partialSupported = partialScene
            && partialScene->supportsPartialChunks();
    const bool partialEnabled = partialSupported
            && partialScene->partialChunksEnabled();
    WorldView *overlayView = currentWorldDoc
            ? dynamic_cast<WorldView *>(currentWorldDoc->view()) : nullptr;
    WorldScene *overlayScene = overlayView ? overlayView->scene() : nullptr;
    const bool hasWorldOverlay = overlayScene
            && overlayScene->hasWorldMapOverlay(false);
    const bool hasForestOverlay = overlayScene
            && overlayScene->hasWorldMapOverlay(true);
    mLoadWorldMapOverlayAction->setEnabled(overlayScene != nullptr);
    mLoadWorldMapForestOverlayAction->setEnabled(overlayScene != nullptr);
    mShowWorldMapOverlayAction->setEnabled(hasWorldOverlay);
    mShowWorldMapForestOverlayAction->setEnabled(hasForestOverlay);
    mClearWorldMapOverlaysAction->setEnabled(
                hasWorldOverlay || hasForestOverlay);
    {
        QSignalBlocker worldOverlayBlocker(mShowWorldMapOverlayAction);
        QSignalBlocker forestOverlayBlocker(
                    mShowWorldMapForestOverlayAction);
        mShowWorldMapOverlayAction->setChecked(
                    hasWorldOverlay
                    && overlayScene->worldMapOverlayVisible(false));
        mShowWorldMapForestOverlayAction->setChecked(
                    hasForestOverlay
                    && overlayScene->worldMapOverlayVisible(true));
    }
    mPartialChunksMenu->setEnabled(partialSupported);
    mPartialChunksToolBar->setEnabled(partialSupported);
    {
        QSignalBlocker blocker(mPartialChunksAction);
        mPartialChunksAction->setChecked(partialEnabled);
    }
    mSelectAllPartialChunksAction->setEnabled(partialEnabled);
    mClearPartialChunksAction->setEnabled(partialEnabled);
    mPartialChunksMenu->setTitle(partialEnabled
            ? tr("Partial Chunks (%1 selected)")
                .arg(partialScene->selectedPartialChunkCount())
            : tr("Partial Chunks"));

    ui->actionSave->setEnabled(hasDoc);
    ui->actionSaveAs->setEnabled(hasDoc);
    ui->actionClose->setEnabled(hasDoc);
    ui->actionCloseAll->setEnabled(hasDoc);

    ui->menuGenerate_Lots_8x8->setEnabled(worldDoc != 0);
    ui->actionGenerateLotsAll8x8->setEnabled(worldDoc != 0);
    ui->actionGenerateLotsSelected8x8->setEnabled(worldDoc && worldDoc->selectedCellCount());
    ui->actionExportModAll8x8->setEnabled(worldDoc != 0);

    ui->menuOverwriteSpawnMap256->setEnabled(worldDoc != nullptr);
    ui->actionOverwriteSpawnMap_AllCells_256->setEnabled(worldDoc != nullptr);
    ui->actionOverwriteSpawnMap_SelectedCells_256->setEnabled(worldDoc && worldDoc->selectedCellCount());

    ui->menuBMP_To_TMX->setEnabled(worldDoc != 0);
    ui->actionBMPToTMXAll->setEnabled(worldDoc != 0);
    ui->actionBMPToTMXSelected->setEnabled(worldDoc &&
                                           worldDoc->selectedCellCount());

    ui->menuTMX_To_BMP->setEnabled(worldDoc != 0);
    ui->actionTMXToBMPAll->setEnabled(worldDoc != 0);
    ui->actionTMXToBMPSelected->setEnabled(worldDoc &&
                                           worldDoc->selectedCellCount());

    ui->actionLUAObjectDump->setEnabled(worldDoc != 0);
    ui->actionWriteObjects->setEnabled(worldDoc != 0);
    ui->actionReadObjectsFromLua->setEnabled(worldDoc != 0);
    ui->actionWriteRoomTonesToLua->setEnabled(worldDoc != 0);

    ui->actionCopy->setEnabled(worldDoc);
    ui->actionPaste->setEnabled(worldDoc && !Clipboard::instance()->isEmpty());

#ifdef ROAD_UI
    bool removeRoad = (worldDoc && worldDoc->selectedRoadCount()) ||
            (cellDoc && cellDoc->worldDocument()->selectedRoadCount());
    ui->actionRemoveRoad->setEnabled(removeRoad);
#endif

    ui->actionRemoveBMP->setEnabled(worldDoc && worldDoc->selectedBMPCount());

    ui->actionEditCell->setEnabled(false);
    ui->actionGoToXY->setEnabled(hasDoc);
    ui->actionResizeWorld->setEnabled(worldDoc);
    ui->actionLinkedWorldProjects->setEnabled(currentWorldDoc != nullptr);
    ui->actionObjectTypes->setEnabled(hasDoc);
    ui->actionEnums->setEnabled(hasDoc);
    ui->actionProperties->setEnabled(hasDoc);
    ui->actionTemplates->setEnabled(hasDoc);

    bool removeLot = (cellDoc && cellDoc->selectedLotCount())
            || (worldDoc && worldDoc->selectedLotCount());
    ui->actionRemoveLot->setEnabled(removeLot);

    bool removeObject = (cellDoc && cellDoc->selectedObjectCount())
            || (worldDoc && worldDoc->selectedObjectCount());
    ui->actionRemoveObject->setEnabled(removeObject);
    ui->actionSplitObjectPolygon->setEnabled(canSplitObjectPolygon());

    ui->actionExtractLots->setEnabled(cellDoc != 0);
    ui->actionExtractObjects->setEnabled(cellDoc != 0);
    ui->actionClearCell->setEnabled(false);
    ui->actionClearMapOnly->setEnabled(false);
    ui->actionRemoveEmptyBorderCells->setEnabled(
                worldDoc && worldDoc->world()->size() != QSize(1, 1));
    ui->actionCheckForHoles->setEnabled(hasCellDoc && !partialEnabled);

    bool selectedCells = (cellDoc != nullptr) || (worldDoc != nullptr && !worldDoc->selectedCells().isEmpty());
    ui->actionGenerateInGameMapBuildingFeatures->setEnabled(selectedCells);
    ui->actionGenerateInGameMapTreeFeatures->setEnabled(selectedCells);
    ui->actionGenerateInGameMapWaterFeatures->setEnabled(selectedCells);
    ui->actionGenerateInGameMapRoadFeatures->setEnabled(selectedCells);
    ui->actionWriteInGameMapForest->setEnabled(hasDoc);
    ui->actionWriteInGameMapWorldMap->setEnabled(hasDoc);
    ui->actionRemoveInGameMapFeatures->setEnabled(((worldDoc != nullptr) && (worldDoc->selectedInGameMapFeatureCount() > 0)) ||
                                               (cellDoc != nullptr && cellDoc->selectedInGameMapFeatures().isEmpty() == false));
    ui->actionRemoveInGameMapPoints->setEnabled(canRemoveInGameMapPoint());
    ui->actionSplitInGameMapPolygon->setEnabled(canSplitInGameMapPolygon());
    ui->actionAddInGameMapHole->setEnabled(canAddInGameMapHole());
    ui->actionRemoveInGameMapHole->setEnabled(canRemoveInGameMapHole());
    ui->actionConvertToPolygon->setEnabled(canConvertToInGameMapPolygon());
    ui->actionReadInGameMapFeaturesXML->setEnabled(hasDoc);
    ui->actionWriteInGameMapFeaturesXML_256->setEnabled(hasDoc);
    QString featuresXML = currentWorldDoc ? currentWorldDoc->getInGameMapXMLFileName() : QString(); // Preferences::instance()->worldMapXMLFile();
    bool hasReadFeaturesXML = false;
    if (hasDoc && !featuresXML.isEmpty()) {
        for (auto* cell : world->cells()) {
            const InGameMapFeatures &features = cell->inGameMap().features();
            if (features.isEmpty() == false) {
                hasReadFeaturesXML = true;
                break;
            }
        }
    }
    ui->actionOverwriteInGameMapFeaturesXML_256->setText(tr("Overwrite %1 8x8").arg(featuresXML.isEmpty() ? tr("features.xml") : QFileInfo(featuresXML).fileName()));
    ui->actionOverwriteInGameMapFeaturesXML_256->setEnabled(hasDoc && hasReadFeaturesXML);
    ui->actionCreateFeatureImage->setEnabled(hasDoc);
    ui->actionCreateWorldImage->setEnabled(hasDoc);
    ui->actionGenerateBiomeMap->setEnabled(hasDoc);
    ui->actionTerrainImageEditor->setEnabled(currentWorldDoc != nullptr);
    const bool oneChunkDataCell = cellDoc != nullptr
            || (worldDoc != nullptr && worldDoc->selectedCellCount() == 1);
    mChunkDataOverrideAction->setEnabled(
                currentWorldDoc != nullptr
                && world != nullptr
                && world->gridFormat() == WorldGridFormat::Native256
                && oneChunkDataCell);
    ui->actionImportOpenStreetMapTerrain->setEnabled(true);
    ui->actionWorldGenPreview->setEnabled(
                currentWorldDoc != nullptr
                && !currentWorldDoc->fileName().isEmpty());
    ui->actionWorldGenPrefabEditor->setEnabled(
                currentWorldDoc != nullptr
                && !currentWorldDoc->fileName().isEmpty());

    ui->actionSnapToGrid->setEnabled(cellDoc != 0);
    ui->actionShowCoordinates->setEnabled(worldDoc != 0);

    Preferences *prefs = Preferences::instance();
    ui->actionShowGrid->setChecked(worldDoc ? prefs->showWorldGrid() : cellDoc ? prefs->showCellGrid() : false);
    ui->actionShowGrid->setEnabled(hasDoc);

    ui->actionHighlightCurrentLevel->setEnabled(cellDoc != 0);

    ui->actionLevelAbove->setEnabled(false);
    ui->actionLevelBelow->setEnabled(false);

    updateZoom();

    if (worldDoc) {
        WorldCell *cell = worldDoc->selectedCellCount() ? worldDoc->selectedCells().first() : 0;
        if (cell) {
            ui->actionEditCell->setEnabled(true);
            ui->actionClearCell->setEnabled(true);
            ui->actionClearMapOnly->setEnabled(true);
            ui->currentCellLabel->setText(tr("Current cell: %1,%2").arg(cell->displayPos().x()).arg(cell->displayPos().y()));
        } else
            ui->currentCellLabel->setText(tr("Current cell: <none>"));
        ui->currentLevelButton->setText(tr("Level: ? ")); // extra space cuz of down-arrow placement on Windows
        ui->currentLevelButton->setEnabled(false);
        ui->objectGroupButton->setText(tr("Obj Grp: <none> "));
        ui->objectGroupButton->setEnabled(false);
    } else if (cellDoc) {
        ui->actionClearCell->setEnabled(true);
        ui->actionClearMapOnly->setEnabled(true);
        WorldCell *cell = cellDoc->cell();
        ui->currentCellLabel->setText(tr("Current cell: %1,%2").arg(cell->displayPos().x()).arg(cell->displayPos().y()));
        int level = cellDoc->currentLevel();
        ui->currentLevelButton->setText(tr("Level: %1 ").arg(level)); // extra space cuz of down-arrow placement on Windows
        ui->currentLevelButton->setEnabled(true);
        ui->actionLevelAbove->setEnabled(level < MAX_WORLD_LEVEL /*cellDoc->scene()->mapComposite()->maxLevel()*/);
        ui->actionLevelBelow->setEnabled(level > MIN_WORLD_LEVEL);
        WorldObjectGroup *og = cellDoc->currentObjectGroup();
        ui->objectGroupButton->setText(tr("Obj Grp: %1 ")
                                       .arg((og && !og->name().isEmpty())
                                       ? og->name() : tr("<none>")));
        ui->objectGroupButton->setEnabled(true);
    } else {
        ui->coordinatesLabel->clear();
        ui->worldCoordinatesLabel->clear();
        ui->currentCellLabel->setText(tr("Current cell: <none> "));
        ui->currentLevelButton->setText(tr("Level: ? ")); // extra space cuz of down-arrow placement on Windows
        ui->currentLevelButton->setEnabled(false);
        ui->objectGroupButton->setText(tr("Obj Grp: <none> "));
        ui->objectGroupButton->setEnabled(false);
    }
}

void MainWindow::updateZoom()
{
    const qreal scale = mZoomable ? mZoomable->scale() : 1;

    ui->actionZoomIn->setEnabled(mZoomable && mZoomable->canZoomIn());
    ui->actionZoomOut->setEnabled(mZoomable && mZoomable->canZoomOut());
    ui->actionZoomNormal->setEnabled(scale != 1);

    if (mZoomable) {
        mZoomComboBox->setEnabled(true);
    } else {
        int index = mZoomComboBox->findData((qreal)1.0);
        mZoomComboBox->setCurrentIndex(index);
        mZoomComboBox->setEnabled(false);
    }
}
