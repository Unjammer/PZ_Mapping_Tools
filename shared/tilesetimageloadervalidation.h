#ifndef TILESETIMAGELOADERVALIDATION_H
#define TILESETIMAGELOADERVALIDATION_H

#include "tilesetimageloader.h"
#include "tilesetimagepaths.h"
#include "tile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

namespace TilesetImageLoaderValidation {

inline bool validatePaths(const QTemporaryDir &directory, QString *error)
{
    const QString root = directory.path();
    const QString doubled = directory.filePath(QStringLiteral("2x"));
    const QString game = doubled + QStringLiteral("/Tiles2x.pack");
    const QString overlays = doubled + QStringLiteral("/Overlays2x.pack");
    const QString custom = root + QStringLiteral("/custom");
    for (const QString &path : {game, overlays, custom}) {
        if (!QDir().mkpath(path)) {
            *error = QStringLiteral("Cannot create a tileset resolution fixture directory");
            return false;
        }
    }
    const QString name = QStringLiteral("fixtures_overlay_counters_01");
    const QString filename = name + QStringLiteral(".png");
    const QString original = game + QLatin1Char('/') + filename;
    const QString secondary = overlays + QLatin1Char('/') + filename;
    const QString loose = doubled + QLatin1Char('/') + filename;
    const QString single = custom + QLatin1Char('/') + filename;
    QImage image(128, 256, QImage::Format_ARGB32);
    image.fill(Qt::red);
    if (!image.save(original) || !image.save(secondary) || !image.save(single)) {
        *error = QStringLiteral("Cannot write tileset resolution fixtures");
        return false;
    }

    TilesetImagePaths paths;
    const auto resolves = [&](const QString &expected, int expectedScale) {
        QString path1x, path2x;
        int scale = 0;
        return paths.find(root, doubled, name, path1x, path2x, &scale)
                && scale == expectedScale && (scale == 2 ? path2x : path1x) == expected;
    };
    if (!resolves(original, 2) || !image.save(loose)) {
        *error = QStringLiteral("Duplicate stock sheets did not follow the game pack order");
        return false;
    }
    paths.clear();
    if (!resolves(loose, 2) || !QFile::remove(loose)) {
        *error = QStringLiteral("A loose 2x sheet did not override the extracted packs");
        return false;
    }
    QFile corrupt(original);
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("invalid image") != 13) {
        *error = QStringLiteral("Cannot create the unreadable 2x fixture");
        return false;
    }
    corrupt.close();
    paths.clear();
    if (!resolves(secondary, 2) || !QFile::remove(secondary)) {
        *error = QStringLiteral("A readable duplicate did not replace the unreadable primary sheet");
        return false;
    }
    paths.clear();
    if (!resolves(single, 1)) {
        *error = QStringLiteral("The nested 1x fallback was not resolved after removing the 2x sheets");
        return false;
    }
    return true;
}

inline bool run(QString *summary, QString *error)
{
    QTemporaryDir directory;
    if (!directory.isValid()) {
        *error = QStringLiteral("Cannot create image validation directory");
        return false;
    }
    if (!validatePaths(directory, error))
        return false;
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
    *summary = QStringLiteral("Pack order, loose overrides, 1x fallback, 48 image and cache comparisons, completion progress, queued completion, input isolation, shutdown, %1 workers")
            .arg(loader.workerCount());
    return true;
}

}

#endif
