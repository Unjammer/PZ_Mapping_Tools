/*
 * Copyright 2021, Tim Baker <treectrl@users.sf.net>
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

#include "ingamemapimagepyramidwindow.h"
#include "ui_ingamemapimagepyramidwindow.h"

#include "celldocument.h"
#include "documentmanager.h"
#include "worlddocument.h"
#include "world.h"

#include <quazip.h>
#include <quazipfile.h>

#include <QFileDialog>
#include <QPainter>
#include <QTextStream>

#include <cmath>

namespace {

void pyramidLog(
        const InGameMapImagePyramidWindow::LogFunction &logger,
        const QString &message)
{
    if (logger)
        logger(message);
}

bool writePyramidImage(
        QuaZip &zip, const QImage &image,
        int col, int row, int level,
        QString *error,
        const InGameMapImagePyramidWindow::LogFunction &logger)
{
    const QString fileName = QStringLiteral("%1/tile%2x%3.png")
            .arg(level).arg(col).arg(row);
    pyramidLog(logger, QObject::tr("Adding %1 to ZIP").arg(fileName));

    QuaZipFile file(&zip);
    if (!file.open(QIODevice::WriteOnly, QuaZipNewInfo(fileName))) {
        if (error)
            *error = QObject::tr("Could not create %1 in the pyramid ZIP.")
                    .arg(fileName);
        return false;
    }
    if (!image.save(&file, "PNG")) {
        if (error)
            *error = QObject::tr("Could not encode %1 in the pyramid ZIP.")
                    .arg(fileName);
        file.close();
        return false;
    }
    file.close();
    if (file.getZipError() != 0) {
        if (error)
            *error = QObject::tr("Could not finish %1 in the pyramid ZIP "
                                "(ZIP error %2).")
                    .arg(fileName).arg(file.getZipError());
        return false;
    }
    return true;
}

bool writePyramidDescription(
        QuaZip &zip, const QImage &image,
        const QRect &worldBounds, QString *error,
        const InGameMapImagePyramidWindow::LogFunction &logger)
{
    const QString fileName = QStringLiteral("pyramid.txt");
    pyramidLog(logger, QObject::tr("Writing %1").arg(fileName));
    QuaZipFile file(&zip);
    if (!file.open(QIODevice::WriteOnly, QuaZipNewInfo(fileName))) {
        if (error)
            *error = QObject::tr("Could not create pyramid.txt in the ZIP.");
        return false;
    }
    QTextStream stream(&file);
    stream << "VERSION=1\n";
    stream << QStringLiteral("bounds=%1 %2 %3 %4\n")
              .arg(worldBounds.x())
              .arg(worldBounds.y())
              .arg(worldBounds.x() + worldBounds.width())
              .arg(worldBounds.y() + worldBounds.height());
    stream << QStringLiteral("imageSize=%1 %2")
              .arg(image.width()).arg(image.height());
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        if (error)
            *error = QObject::tr("Could not write pyramid.txt in the ZIP.");
        file.close();
        return false;
    }
    file.close();
    if (file.getZipError() != 0) {
        if (error)
            *error = QObject::tr("Could not finish pyramid.txt in the ZIP "
                                "(ZIP error %1).")
                    .arg(file.getZipError());
        return false;
    }
    return true;
}

}

InGameMapImagePyramidWindow::InGameMapImagePyramidWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::InGameMapImagePyramidWindow)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose, true);

    connect(ui->inputBrowseButton, &QToolButton::clicked, this, &InGameMapImagePyramidWindow::chooseInputFile);
    connect(ui->outputBrowseButton, &QToolButton::clicked, this, &InGameMapImagePyramidWindow::chooseOutputFile);
    connect(ui->createZipButton, &QPushButton::clicked, this, &InGameMapImagePyramidWindow::createZip);

    if (Document *doc = DocumentManager::instance()->currentDocument()) {
        World *world = nullptr;
        if (doc->asWorldDocument()) {
            world = doc->asWorldDocument()->world();
        } else {
            world = doc->asCellDocument()->world();
        }
        auto settings = world->getGenerateLotsSettings();
        const int cellSize = world->cellSize();
        ui->xMin->setValue(settings.worldOrigin.x() * cellSize);
        ui->yMin->setValue(settings.worldOrigin.y() * cellSize);
        ui->xMax->setValue(
                    (settings.worldOrigin.x() + world->width()) * cellSize);
        ui->yMax->setValue(
                    (settings.worldOrigin.y() + world->height()) * cellSize);
    }
}

InGameMapImagePyramidWindow::~InGameMapImagePyramidWindow()
{
    delete ui;
}

void InGameMapImagePyramidWindow::chooseInputFile()
{
    QString caption = QStringLiteral("Choose Input Image File");
    QString dir;
    const QString fileName = QFileDialog::getOpenFileName(this, caption, dir, tr("PNG files (*.png)"));
    if (fileName.isEmpty()) {
        return;
    }
    ui->inputNameEdit->setText(fileName);
}

void InGameMapImagePyramidWindow::chooseOutputFile()
{
    QString caption = QStringLiteral("Choose Output ZIP File");
    QString suggestedFileName;
    const QString fileName = QFileDialog::getSaveFileName(this, caption, suggestedFileName, tr("ZIP files (*.zip)"));
    if (fileName.isEmpty()) {
        return;
    }
    ui->outputNameEdit->setText(fileName);
}

void InGameMapImagePyramidWindow::createZip()
{
    ui->logText->clear();
    const QString inputFileName = ui->inputNameEdit->text();
    log(QStringLiteral("Reading %1").arg(inputFileName));
    const QImage image(inputFileName);
    if (image.isNull()) {
        log(QStringLiteral("Error reading %1").arg(inputFileName));
        return;
    }
    const QRect worldBounds(
                ui->xMin->value(), ui->yMin->value(),
                ui->xMax->value() - ui->xMin->value(),
                ui->yMax->value() - ui->yMin->value());
    QString error;
    if (!createPyramidZip(
                image, worldBounds, ui->outputNameEdit->text(), &error,
                [this](const QString &message) { log(message); })) {
        log(tr("ERROR: %1").arg(error));
        return;
    }
    log(QStringLiteral("FINISHED."));
}

bool InGameMapImagePyramidWindow::createPyramidZip(
        const QImage &image,
        const QRect &worldBounds,
        const QString &outputFileName,
        QString *error,
        const LogFunction &logger)
{
    if (image.isNull()) {
        if (error)
            *error = tr("The source image is empty.");
        return false;
    }
    if (worldBounds.width() <= 0 || worldBounds.height() <= 0) {
        if (error)
            *error = tr("The pyramid world bounds are empty or inverted.");
        return false;
    }
    if (outputFileName.isEmpty()) {
        if (error)
            *error = tr("No output ZIP file was selected.");
        return false;
    }

    QuaZip zip(outputFileName);
    if (!zip.open(QuaZip::Mode::mdCreate)) {
        if (error)
            *error = tr("Could not create %1 (ZIP error %2).")
                    .arg(QDir::toNativeSeparators(outputFileName))
                    .arg(zip.getZipError());
        return false;
    }

    const QSize paddedSize(
                int(std::ceil(image.width() / 16.0f) * 16),
                int(std::ceil(image.height() / 16.0f) * 16));
    QImage paddedImage(paddedSize, QImage::Format_ARGB32);
    paddedImage.fill(Qt::transparent);
    QPainter painter(&paddedImage);
    painter.drawImage(QPoint(), image);
    painter.end();

    const int tileSize = 256;
    const int levels = 5;
    for (int level = 0; level < levels; ++level) {
        const int width = qMax(1, paddedImage.width() / (1 << level));
        const int height = qMax(1, paddedImage.height() / (1 << level));
        const QImage scaledImage = paddedImage.scaled(
                    width, height, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
        const int columns = int(std::ceil(width / qreal(tileSize)));
        const int rows = int(std::ceil(height / qreal(tileSize)));
        pyramidLog(logger,
                   tr("Creating level %1 with %2 x %3 tile(s)")
                   .arg(level).arg(columns).arg(rows));
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < columns; ++col) {
                const QImage tile = scaledImage.copy(
                            col * tileSize, row * tileSize,
                            tileSize, tileSize);
                if (!writePyramidImage(
                            zip, tile, col, row, level,
                            error, logger)) {
                    zip.close();
                    QFile::remove(outputFileName);
                    return false;
                }
            }
        }
        if (width <= tileSize && height <= tileSize)
            break;
    }

    if (!writePyramidDescription(
                zip, image, worldBounds, error, logger)) {
        zip.close();
        QFile::remove(outputFileName);
        return false;
    }
    zip.close();
    if (zip.getZipError() != 0) {
        if (error)
            *error = tr("Could not finish %1 (ZIP error %2).")
                    .arg(QDir::toNativeSeparators(outputFileName))
                    .arg(zip.getZipError());
        QFile::remove(outputFileName);
        return false;
    }
    return true;
}

void InGameMapImagePyramidWindow::log(const QString &str)
{
    ui->logText->appendPlainText(str);
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}
