#include "mappingimageformat.h"

#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QObject>
#include <QSaveFile>

namespace {

QByteArray formatForPath(const QString &path)
{
    const QByteArray format = QFileInfo(path).suffix().toLatin1().toLower();
    if (format == "png" || format == "bmp")
        return format;
    return QByteArray();
}

}

bool MappingImageFormat::isSupportedPath(const QString &path)
{
    return !formatForPath(path).isEmpty();
}

bool MappingImageFormat::saveAtomically(const QImage &image,
                                        const QString &path,
                                        QString *error)
{
    const QByteArray format = formatForPath(path);
    if (format.isEmpty()) {
        if (error)
            *error = QObject::tr("Only PNG and BMP image files are supported.");
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QImage output = image;
    if (format == "bmp")
        output = image.convertToFormat(QImage::Format_RGB32);

    QImageWriter writer(&file, format);
    if (format == "png")
        writer.setCompression(6);
    if (!writer.write(output)) {
        if (error)
            *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
