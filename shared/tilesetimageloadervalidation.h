#ifndef TILESETIMAGELOADERVALIDATION_H
#define TILESETIMAGELOADERVALIDATION_H

#include "tilesetimageloader.h"
#include "tile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

namespace TilesetImageLoaderValidation {

inline bool run(QString *summary, QString *error)
{
    QTemporaryDir directory;
    if (!directory.isValid()) {
        *error = QStringLiteral("Cannot create image validation directory");
        return false;
    }
    QImage png(259, 263, QImage::Format_ARGB32);
    for (int y = 0; y < png.height(); ++y) {
        for (int x = 0; x < png.width(); ++x) {
            png.setPixel(x, y, x < 18 || y < 20 ? 0
                         : x % 7 == 0 ? qRgba(255, 0, 255, 255)
                         : qRgba(x % 256, y % 256, (x + y) % 256,
                                 (x * 11 + y * 17) % 256));
        }
    }
    const QString path = directory.filePath(QStringLiteral("sheet.png"));
    if (!png.save(path)) {
        *error = QStringLiteral("Cannot write image validation sheet");
        return false;
    }
    const QString invalidPath = directory.filePath(QStringLiteral("invalid.png"));
    QFile invalidFile(invalidPath);
    if (!invalidFile.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Cannot write invalid image fixture");
        return false;
    }
    invalidFile.write("invalid image");
    invalidFile.close();

    TilesetImageLoader loader;
    int completed = 0;
    bool valid = true;
    const int count = 48;
    for (int index = 0; index < count; ++index) {
        const bool doubled = index % 2 != 0;
        const QString file = index % 13 == 0 ? invalidPath
                           : index % 17 == 0 ? path + QStringLiteral(".missing") : path;
        Tiled::Tileset source(QStringLiteral("source"), 32, 64,
                              index % 3, index % 4);
        source.setImageSource(file);
        source.setImageSource2x(doubled ? file : QString());
        if (index % 5 == 0)
            source.setTransparentColor(QColor(255, 0, 255));
        auto expected = std::make_shared<Tiled::Tileset>(
                    QStringLiteral("expected"), source.tileWidth(), source.tileHeight(),
                    source.tileSpacing(), source.margin());
        expected->setImageSource2x(source.imageSource2x());
        expected->setTransparentColor(source.transparentColor());
        expected->loadFromImage(QImage(file), file);
        loader.enqueue(&source, [&, expected](const TilesetImageLoader::Image &actual) {
            ++completed;
            valid = valid && QThread::currentThread() == QCoreApplication::instance()->thread()
                    && actual->isLoaded() == expected->isLoaded()
                    && actual->tileCount() == expected->tileCount()
                    && actual->columnCount() == expected->columnCount()
                    && actual->imageWidth() == expected->imageWidth()
                    && actual->imageHeight() == expected->imageHeight()
                    && actual->imageSource2x() == expected->imageSource2x();
            for (int i = 0; valid && i < actual->tileCount(); ++i)
                valid = actual->tileAt(i)->hasSameImageData(expected->tileAt(i));
            if (valid && actual->isLoaded()) {
                Tiled::Tileset installed(QStringLiteral("installed"),
                                         actual->tileWidth(), actual->tileHeight(),
                                         actual->tileSpacing(), actual->margin());
                installed.setTransparentColor(actual->transparentColor());
                valid = installed.loadFromCache(actual.get())
                        && installed.tileCount() == expected->tileCount();
                for (int i = 0; valid && i < installed.tileCount(); ++i)
                    valid = installed.tileAt(i)->hasSameImageData(expected->tileAt(i));
#ifdef WORLDED
                valid = valid && actual->image() == expected->image()
                        && installed.image() == expected->image();
#endif
            }
        });
        source.setImageSource(invalidPath);
    }
    int progressCalls = 0;
    loader.waitForDone([&](int ready, int total) {
        valid = valid && total == count && ready == completed
                && ready == progressCalls
                && QThread::currentThread() == loader.thread();
        ++progressCalls;
    });
    valid = valid && completed == count && progressCalls == count + 1;

    Tiled::Tileset source(QStringLiteral("source"), 32, 64);
    source.setImageSource(path);
    loader.enqueue(&source, [&](const TilesetImageLoader::Image &image) {
        ++completed;
        valid = valid && image->isLoaded();
    });
    QElapsedTimer timeout;
    timeout.start();
    while (completed == count && timeout.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    valid = valid && completed == count + 1;
    int emptyProgressCalls = 0;
    loader.waitForDone([&](int ready, int total) {
        valid = valid && ready == 0 && total == 0;
        ++emptyProgressCalls;
    });
    valid = valid && emptyProgressCalls == 1;

    int abandoned = 0;
    {
        TilesetImageLoader closing;
        for (int i = 0; i < 24; ++i) {
            closing.enqueue(&source, [&](const TilesetImageLoader::Image &) {
                ++abandoned;
            });
        }
    }
    QCoreApplication::processEvents();
    valid = valid && abandoned == 0;
    if (!valid) {
        *error = QStringLiteral("Image pixels, geometry, completion thread or shutdown differed");
        return false;
    }
    *summary = QStringLiteral("48 image and cache comparisons, completion progress, queued completion, input isolation, shutdown, %1 workers")
            .arg(loader.workerCount());
    return true;
}

}

#endif
