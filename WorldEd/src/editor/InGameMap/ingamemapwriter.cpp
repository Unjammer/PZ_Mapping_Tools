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

#include "ingamemapwriter.h"

#include "world.h"
#include "worldcell.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QXmlStreamWriter>
#include <QDebug>

class InGameMapWriterPrivate
{
    Q_DECLARE_TR_FUNCTIONS(InGameMapWriter)

public:
    InGameMapWriterPrivate()
        : mWorld(nullptr)
    {
    }

    bool openFile(QFile *file)
    {
        if (!file->open(QIODevice::WriteOnly)) {
            mError = tr("Could not open file for writing.");
            return false;
        }

        return true;
    }

    void writeWorld(World *world, QIODevice *device, const QString &absDirPath)
    {
        mMapDir = QDir(absDirPath);
        mWorld = world;
        mWrittenFeatures = 0;
        mRepairedFeatures = 0;
        mRejectedFeatures = 0;

        QXmlStreamWriter writer(device);
        writer.setAutoFormatting(true);
        writer.setAutoFormattingIndent(1);

        writer.writeStartDocument();

        writeWorld(writer, world);

        writer.writeEndDocument();

        qInfo() << "InGameMap XML geometry validation:"
                << "output" << mOutputPath
                << "written" << mWrittenFeatures
                << "repaired" << mRepairedFeatures
                << "rejected" << mRejectedFeatures;
    }

    void writeWorld(QXmlStreamWriter &w, World *world)
    {
        w.writeStartElement(QLatin1String("world"));

        w.writeAttribute(QLatin1String("version"), QLatin1String("1.0"));

        for (int y = 0; y < world->height(); y++) {
            for (int x = 0; x < world->width(); x++) {
                WorldCell *cell = world->cellAt(x, y);
                writeCell(w, cell);
            }
        }

        w.writeEndElement();
    }

    void writeCell(QXmlStreamWriter &w, WorldCell *cell)
    {
        struct ExportFeature {
            InGameMapFeature *feature;
            InGameMapGeometry geometry;
        };
        QList<ExportFeature> exportFeatures;
        int cellPointCount = 0;
        for (InGameMapFeature *feature : std::as_const(cell->inGameMap().mFeatures)) {
            ExportFeature item{feature, InGameMapGeometry()};
            QStringList diagnostics;
            if (!sanitizeInGameMapGeometryForExport(feature->mGeometry,
                                                    item.geometry,
                                                    diagnostics)) {
                ++mRejectedFeatures;
                qWarning().noquote()
                        << QStringLiteral("InGameMap XML rejected feature: output=\"%1\" cell=%2,%3 feature=%4 properties={%5} reason=\"%6\"")
                           .arg(mOutputPath)
                           .arg(cell->x()).arg(cell->y())
                           .arg(feature->index())
                           .arg(propertySummary(feature))
                           .arg(diagnostics.join(QStringLiteral("; ")));
                continue;
            }
            if (!diagnostics.isEmpty()) {
                ++mRepairedFeatures;
                qWarning().noquote()
                        << QStringLiteral("InGameMap XML repaired feature: output=\"%1\" cell=%2,%3 feature=%4 properties={%5} action=\"%6\"")
                           .arg(mOutputPath)
                           .arg(cell->x()).arg(cell->y())
                           .arg(feature->index())
                           .arg(propertySummary(feature))
                           .arg(diagnostics.join(QStringLiteral("; ")));
            }
            for (const InGameMapCoordinates &coordinates : std::as_const(item.geometry.mCoordinates))
                cellPointCount += coordinates.size();
            exportFeatures += item;
        }

        if (cellPointCount > 30000) {
            qWarning().noquote()
                    << QStringLiteral("InGameMap XML complexity warning: output=\"%1\" cell=%2,%3 points=%4; the game renderer uses signed 16-bit geometry indices")
                       .arg(mOutputPath)
                       .arg(cell->x()).arg(cell->y())
                       .arg(cellPointCount);
        }

        if (exportFeatures.isEmpty())
            return;

        const QPoint worldOrigin = cell->world()->getGenerateLotsSettings().worldOrigin;

        w.writeStartElement(QLatin1String("cell"));
        w.writeAttribute(QLatin1String("x"), QString::number(worldOrigin.x() + cell->x()));
        w.writeAttribute(QLatin1String("y"), QString::number(worldOrigin.y() + cell->y()));

        for (const ExportFeature &item : std::as_const(exportFeatures)) {
            writeFeature(w, item.feature, item.geometry);
            ++mWrittenFeatures;
        }

        w.writeEndElement();
    }

    QString propertySummary(InGameMapFeature *feature) const
    {
        QStringList properties;
        for (const InGameMapProperty &property : feature->mProperties)
            properties += property.mKey + QLatin1Char('=') + property.mValue;
        return properties.join(QLatin1Char(','));
    }

    void writeFeature(QXmlStreamWriter &w, InGameMapFeature* feature,
                      const InGameMapGeometry &geometry)
    {
        w.writeStartElement(QLatin1String("feature"));

        w.writeStartElement(QLatin1String("geometry"));
        w.writeAttribute(QLatin1String("type"), geometry.mType);
        for (const InGameMapCoordinates &coords : geometry.mCoordinates) {
            w.writeStartElement(QLatin1String("coordinates"));
            for (auto& point : coords) {
                w.writeStartElement(QLatin1String("point"));
                w.writeAttribute(QLatin1String("x"), QString::number(point.x));
                w.writeAttribute(QLatin1String("y"), QString::number(point.y));
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();

        w.writeStartElement(QLatin1String("properties"));
        for (auto& property : feature->mProperties) {
            w.writeStartElement(QLatin1String("property"));
            w.writeAttribute(QLatin1String("name"), property.mKey);
            w.writeAttribute(QLatin1String("value"), property.mValue);
            w.writeEndElement();
        }
        w.writeEndElement();

        w.writeEndElement();
    }

    World *mWorld;
    QString mError;
    QDir mMapDir;
    QString mOutputPath = QStringLiteral("<device>");
    int mWrittenFeatures = 0;
    int mRepairedFeatures = 0;
    int mRejectedFeatures = 0;
};

/////

InGameMapWriter::InGameMapWriter()
    : d(new InGameMapWriterPrivate)
{
}

InGameMapWriter::~InGameMapWriter()
{
    delete d;
}

bool InGameMapWriter::writeWorld(World *world, const QString &filePath)
{
    d->mOutputPath = QFileInfo(filePath).absoluteFilePath();
    QTemporaryFile tempFile;
    if (!d->openFile(&tempFile))
        return false;

    d->writeWorld(world, &tempFile, QFileInfo(filePath).absolutePath());

    if (tempFile.error() != QFile::NoError) {
        d->mError = tempFile.errorString();
        return false;
    }

    // foo.pzw -> foo.pzw.bak
    QFileInfo destInfo(filePath);
    QString backupPath = filePath + QLatin1String(".bak");
    QFile backupFile(backupPath);
    if (destInfo.exists()) {
        if (backupFile.exists()) {
            if (!backupFile.remove()) {
                d->mError = QString(QLatin1String("Error deleting file!\n%1\n\n%2"))
                        .arg(backupPath)
                        .arg(backupFile.errorString());
                return false;
            }
        }
        QFile destFile(filePath);
        if (!destFile.rename(backupPath)) {
            d->mError = QString(QLatin1String("Error renaming file!\nFrom: %1\nTo: %2\n\n%3"))
                    .arg(filePath)
                    .arg(backupPath)
                    .arg(destFile.errorString());
            return false;
        }
    }

    // /tmp/tempXYZ -> foo.pzw
    tempFile.close();
    if (tempFile.rename(filePath)) {
        // If anything above failed, the temp file should auto-remove, but not after
        // a successful save.
        tempFile.setAutoRemove(false);
    } else if (!tempFile.copy(filePath)) {
        d->mError = QString(QLatin1String("Error copying file!\nFrom: %1\nTo: %2\n\n%3"))
                .arg(tempFile.fileName())
                .arg(filePath)
                .arg(tempFile.errorString());
        // Try to un-rename the backup file
        if (backupFile.exists())
            backupFile.rename(filePath); // might fail
        return false;
    }

    return true;
}

void InGameMapWriter::writeWorld(World *world, QIODevice *device, const QString &absDirPath)
{
    d->mOutputPath = QStringLiteral("<device>");
    d->writeWorld(world, device, absDirPath);
}

QString InGameMapWriter::errorString() const
{
    return d->mError;
}
