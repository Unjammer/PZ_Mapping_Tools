/*
 * Copyright 2018, Tim Baker <treectrl@users.sf.net>
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

#include "ingamemapreader.h"

#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUndoStack>
#include <QXmlStreamReader>

#include <cmath>

class InGameMapReaderPrivate
{
    Q_DECLARE_TR_FUNCTIONS(InGameMapReader)

public:
    InGameMapReaderPrivate()
        : mWorld(nullptr)
    {

    }

    bool openFile(QFile *file)
    {
        if (!file->exists()) {
            mError = tr("File not found: %1").arg(file->fileName());
            return false;
        } else if (!file->open(QFile::ReadOnly | QFile::Text)) {
            mError = tr("Unable to read file: %1").arg(file->fileName());
            return false;
        }

        return true;
    }

    QString errorString() const
    {
        if (!mError.isEmpty()) {
            return mError;
        } else {
            return tr("%3\n\nLine %1, column %2")
                    .arg(xml.lineNumber())
                    .arg(xml.columnNumber())
                    .arg(xml.errorString());
        }
    }

    World *readWorld(QIODevice *device, const QString &path, World* world)
    {
        mError.clear();
        mPath = path;
        xml.clear();

        xml.setDevice(device);

        if (xml.readNextStartElement() && xml.name() == QLatin1String("world")) {
            readWorld(world);
        } else if (!xml.hasError()) {
            xml.raiseError(tr("Missing 'world' element."));
        }

        if (xml.hasError()) {
            for (const auto &features : mReadFeatures)
                qDeleteAll(features);
            mReadFeatures.clear();
            return nullptr;
        }

        for (auto it = mReadFeatures.cbegin(); it != mReadFeatures.cend(); ++it)
            it.key()->inGameMap().mFeatures.append(it.value());
        mReadFeatures.clear();
        return world;
    }

private:
    World *readWorld(World* world)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("world"));

        const QXmlStreamAttributes atts = xml.attributes();
//        const int width = atts.value(QLatin1String("width")).toString().toInt();
//        const int height = atts.value(QLatin1String("height")).toString().toInt();

        mWorld = world;
        mSourceCellSize = world->cellSize();
        const QString cellSize = atts.value(QLatin1String("cellSize")).toString();
        if (!cellSize.isEmpty()) {
            bool valid = false;
            const int declaredSize = cellSize.toInt(&valid);
            if (!valid || (declaredSize != 256 && declaredSize != 300)) {
                xml.raiseError(tr("Unsupported cellSize: %1").arg(cellSize));
                return nullptr;
            }
            mSourceCellSize = declaredSize;
        }
/*
        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                if (WorldCell* cell = world->cellAt(x, y)) {
                    cell->inGameMap() = WorldCellInGameCell(cell);
                }
            }
        }
*/
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("cell"))
                readCell();
            else
                readUnknownElement();
        }

        // Clean up in case of error
        if (xml.hasError()) {
            mWorld = nullptr;
        }

        return mWorld;
    }

    void readCell()
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("cell"));

        const QPoint& worldOrigin = mWorld->getGenerateLotsSettings().worldOrigin;

        const QXmlStreamAttributes atts = xml.attributes();
        const int sourceX = atts.value(QLatin1String("x")).toString().toInt();
        const int sourceY = atts.value(QLatin1String("y")).toString().toInt();
        if (mSourceCellSize == mWorld->cellSize()
                && !mWorld->contains(sourceX - worldOrigin.x(),
                                     sourceY - worldOrigin.y())) {
            xml.raiseError(tr("Invalid cell coordinates %1,%2")
                           .arg(sourceX).arg(sourceY));
            return;
        }

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("feature"))
                readFeature(sourceX, sourceY);
            else
                readUnknownElement();
        }
    }

    void readFeature(int sourceX, int sourceY)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("feature"));

        InGameMapFeature* feature = new InGameMapFeature(nullptr);

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("geometry"))
                readFeatureGeometry(*feature);
            else if (xml.name() == QLatin1String("properties"))
                readFeatureProperties(*feature);
            else
                readUnknownElement();
        }

        if (xml.hasError()) {
            delete feature;
            return;
        }

//        if (feature->properties().contains(QStringLiteral("natural"), QStringLiteral("forest"))) {
//            delete feature;
//            return;
//        }

        const QPoint &worldOrigin = mWorld->getGenerateLotsSettings().worldOrigin;
        const int targetCellSize = mWorld->cellSize();
        int targetX = sourceX;
        int targetY = sourceY;
        if (mSourceCellSize != targetCellSize) {
            bool foundCell = false;
            for (const InGameMapCoordinates &coordinates
                 : feature->mGeometry.mCoordinates) {
                for (const InGameMapPoint &point : coordinates) {
                    const double squareX =
                            double(sourceX) * mSourceCellSize + point.x;
                    const double squareY =
                            double(sourceY) * mSourceCellSize + point.y;
                    const int candidateX = int(std::floor(squareX / targetCellSize));
                    const int candidateY = int(std::floor(squareY / targetCellSize));
                    if (!mWorld->contains(candidateX - worldOrigin.x(),
                                          candidateY - worldOrigin.y()))
                        continue;
                    targetX = candidateX;
                    targetY = candidateY;
                    foundCell = true;
                    break;
                }
                if (foundCell)
                    break;
            }
            if (!foundCell) {
                targetX = int(std::floor(double(sourceX) * mSourceCellSize
                                         / targetCellSize));
                targetY = int(std::floor(double(sourceY) * mSourceCellSize
                                         / targetCellSize));
            }
        }
        const int localX = targetX - worldOrigin.x();
        const int localY = targetY - worldOrigin.y();
        if (!mWorld->contains(localX, localY)) {
            xml.raiseError(tr("Feature outside the current world at %1,%2")
                           .arg(sourceX).arg(sourceY));
            delete feature;
            return;
        }

        WorldCell *cell = mWorld->cellAt(localX, localY);
        feature->mOwner = &cell->inGameMap();
        if (mSourceCellSize != targetCellSize) {
            const double offsetX = double(sourceX) * mSourceCellSize
                    - double(targetX) * targetCellSize;
            const double offsetY = double(sourceY) * mSourceCellSize
                    - double(targetY) * targetCellSize;
            for (InGameMapCoordinates &coordinates : feature->mGeometry.mCoordinates) {
                for (InGameMapPoint &point : coordinates) {
                    point.x += offsetX;
                    point.y += offsetY;
                }
            }
        }
        mReadFeatures[cell].append(feature);
    }

    void readFeatureGeometry(InGameMapFeature& feature)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("geometry"));

        const QXmlStreamAttributes atts = xml.attributes();

        feature.mGeometry.mType = atts.value(QLatin1String("type")).toString();

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("coordinates"))
                readGeometryCoordinates(feature.mGeometry);
            else
                readUnknownElement();
        }
    }

    void readGeometryCoordinates(InGameMapGeometry& geometry)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("coordinates"));

        InGameMapCoordinates coordinates;

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("point"))
                readGeometryCoordinatePoint(coordinates);
            else
                readUnknownElement();
        }

        geometry.mCoordinates += coordinates;
    }

    void readGeometryCoordinatePoint(InGameMapCoordinates& coordinates)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("point"));

        const QXmlStreamAttributes atts = xml.attributes();

        InGameMapPoint point;
        point.x = atts.value(QLatin1String("x")).toDouble();
        point.y = atts.value(QLatin1String("y")).toDouble();

        coordinates += point;

        xml.skipCurrentElement();
    }

    void readFeatureProperties(InGameMapFeature& feature)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("properties"));

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("property"))
                readFeatureProperty(feature);
            else
                readUnknownElement();
        }
    }

    void readFeatureProperty(InGameMapFeature& feature)
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == QLatin1String("property"));

        const QXmlStreamAttributes atts = xml.attributes();
        InGameMapProperty property;
        property.mKey = atts.value(QLatin1String("name")).toString();
        property.mValue = atts.value(QLatin1String("value")).toString();

        feature.mProperties += property;

        xml.skipCurrentElement();
    }

    void readUnknownElement()
    {
        qDebug() << tr("Unknown element \"%3\"\n\nLine %1, column %2")
                    .arg(xml.lineNumber())
                    .arg(xml.columnNumber())
                    .arg(xml.name());
        xml.skipCurrentElement();
    }

    QString resolveReference(const QString &fileName, const QString &relativeTo)
    {
//        qDebug() << "resolveReference" << fileName << "relative to" << relativeTo;
        if (fileName.isEmpty())
            return fileName;
        if (fileName == QLatin1String("."))
            return relativeTo;
        if (QDir::isRelativePath(fileName)) {
            QString path = relativeTo + QLatin1Char('/') + fileName;
            QFileInfo info(path);
            if (info.exists())
                return info.canonicalFilePath();
            return QDir::cleanPath(path);
        }
        return fileName;
    }

private:
    QString mPath;
    World *mWorld;
    QString mError;
    int mSourceCellSize = 0;
    QHash<WorldCell*, QList<InGameMapFeature*>> mReadFeatures;
    QXmlStreamReader xml;
};

/////

InGameMapReader::InGameMapReader()
    : d(new InGameMapReaderPrivate)
{
}

InGameMapReader::~InGameMapReader()
{
    delete d;
}

World *InGameMapReader::readWorld(QIODevice *device, const QString &path, World* world)
{
    return d->readWorld(device, path, world);
}

World *InGameMapReader::readWorld(const QString &fileName, World* world)
{
    QFile file(fileName);
    if (!d->openFile(&file))
        return nullptr;

    return readWorld(&file, QFileInfo(fileName).absolutePath(), world);
}

bool InGameMapReader::replaceWorld(const QString &fileName, WorldDocument *document)
{
    World *world = document->world();
    const auto &cells = world->cells();
    QVector<int> previousCounts;
    previousCounts.reserve(cells.size());
    for (WorldCell *cell : cells)
        previousCounts.append(cell->inGameMap().features().size());

    if (!readWorld(fileName, world))
        return false;

    document->undoStack()->beginMacro(
                QCoreApplication::translate("InGameMapReader", "Read InGameMap XML"));
    for (int cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
        WorldCell *cell = cells.at(cellIndex);
        InGameMapFeatures &features = cell->inGameMap().features();
        const int previousCount = previousCounts.at(cellIndex);
        const QList<InGameMapFeature*> imported = features.mid(previousCount);
        features.erase(features.begin() + previousCount, features.end());
        for (int i = previousCount - 1; i >= 0; --i)
            document->removeInGameMapFeature(cell, i);
        for (int i = 0; i < imported.size(); ++i)
            document->addInGameMapFeature(cell, i, imported.at(i));
    }
    document->undoStack()->endMacro();
    return true;
}

QString InGameMapReader::errorString() const
{
    return d->errorString();
}
