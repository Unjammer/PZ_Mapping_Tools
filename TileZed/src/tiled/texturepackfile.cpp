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

#include "texturepackfile.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>

static const int VERSION1 = 1;
static const int VERSION_LATEST = VERSION1;
static const int MAX_PAGE_COUNT = 10000;
static const int MAX_ENTRY_COUNT = 1000000;
static const int MAX_STRING_LENGTH = 1024 * 1024;
static const int MAX_IMAGE_DIMENSION = 32768;
static const qint64 MAX_IMAGE_PIXELS = 128LL * 1024 * 1024;

PackFile::PackFile()
{
}

PackFile::~PackFile()
{
}

static bool readInt(QDataStream &stream, qint32 *value)
{
    stream >> *value;
    return stream.status() == QDataStream::Ok;
}

static bool readString(QDataStream &stream, QString *value)
{
    qint32 length = 0;
    if (!readInt(stream, &length) ||
            length < 0 || length > MAX_STRING_LENGTH) {
        return false;
    }

    QByteArray bytes;
    bytes.resize(length);
    if (length > 0 &&
            stream.readRawData(bytes.data(), length) != length) {
        return false;
    }
    *value = QString::fromLatin1(bytes);
    return stream.status() == QDataStream::Ok;
}

static void saveString(QDataStream &stream, const QString &value)
{
    const QByteArray bytes = value.toLatin1();
    stream << qint32(bytes.size());
    if (!bytes.isEmpty())
        stream.writeRawData(bytes.constData(), bytes.size());
}

static bool validImageSize(const QSize &size)
{
    return size.width() > 0 && size.height() > 0 &&
            size.width() <= MAX_IMAGE_DIMENSION &&
            size.height() <= MAX_IMAGE_DIMENSION &&
            qint64(size.width()) * size.height() <= MAX_IMAGE_PIXELS;
}

static bool validTextureGeometry(const PackPage &page,
                                 const PackSubTexInfo &texture)
{
    return texture.x >= 0 && texture.y >= 0 &&
            texture.w > 0 && texture.h > 0 &&
            texture.ox >= 0 && texture.oy >= 0 &&
            texture.fx > 0 && texture.fy > 0 &&
            validImageSize(QSize(texture.fx, texture.fy)) &&
            qint64(texture.x) + texture.w <= page.image.width() &&
            qint64(texture.y) + texture.h <= page.image.height() &&
            qint64(texture.ox) + texture.w <= texture.fx &&
            qint64(texture.oy) + texture.h <= texture.fy;
}

bool PackFile::read(const QString &fileName)
{
    mPages.clear();
    mError.clear();
    mFileName.clear();
    mVersion = 0;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        mError = tr("Error opening file for reading.\n%1").arg(fileName);
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    const QByteArray magic = file.peek(4);
    qint32 version = 0;
    qint32 pageCount = 0;
    if (magic == QByteArrayLiteral("PZPK")) {
        char header[4];
        if (stream.readRawData(header, 4) != 4 ||
                !readInt(stream, &version) ||
                !readInt(stream, &pageCount)) {
            mError = tr("Truncated .pack header.\n%1").arg(fileName);
            return false;
        }
        if (version < VERSION1 || version > VERSION_LATEST) {
            mError = tr("Invalid version number %1.\n%2")
                    .arg(version).arg(fileName);
            return false;
        }
    } else if (!readInt(stream, &pageCount)) {
        mError = tr("Truncated legacy .pack header.\n%1").arg(fileName);
        return false;
    }

    if (pageCount < 0 || pageCount > MAX_PAGE_COUNT) {
        mError = tr("Invalid page count %1.\n%2")
                .arg(pageCount).arg(fileName);
        return false;
    }

    qDebug() << "PackFile: reading" << pageCount << "pages";
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        PackPage page;
        qint32 entryCount = 0;
        qint32 maskValue = 0;
        if (!readString(stream, &page.name) ||
                !readInt(stream, &entryCount) ||
                !readInt(stream, &maskValue)) {
            mError = tr("Truncated metadata for page %1.\n%2")
                    .arg(pageIndex).arg(fileName);
            return false;
        }
        if (entryCount < 0 || entryCount > MAX_ENTRY_COUNT) {
            mError = tr("Invalid texture count %1 on page %2.\n%3")
                    .arg(entryCount).arg(page.name).arg(fileName);
            return false;
        }
        page.mask = maskValue != 0;
        qDebug() << "PackFile: page=" << page.name
                 << "numEntries=" << entryCount;

        for (int entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
            QString entryName;
            qint32 x = 0;
            qint32 y = 0;
            qint32 w = 0;
            qint32 h = 0;
            qint32 ox = 0;
            qint32 oy = 0;
            qint32 fx = 0;
            qint32 fy = 0;
            if (!readString(stream, &entryName) ||
                    !readInt(stream, &x) || !readInt(stream, &y) ||
                    !readInt(stream, &w) || !readInt(stream, &h) ||
                    !readInt(stream, &ox) || !readInt(stream, &oy) ||
                    !readInt(stream, &fx) || !readInt(stream, &fy)) {
                mError = tr("Truncated texture metadata on page %1, "
                            "entry %2.\n%3")
                        .arg(page.name).arg(entryIndex).arg(fileName);
                return false;
            }
            page.mInfo += PackSubTexInfo(
                        x, y, w, h, ox, oy, fx, fy, entryName);
        }

        QByteArray pngData;
        if (version == 0) {
            const QByteArray marker = QByteArray::fromHex("efbeadde");
            bool markerFound = false;
            pngData.reserve(250 * 1024);
            while (!stream.atEnd()) {
                char byte = 0;
                if (stream.readRawData(&byte, 1) != 1)
                    break;
                pngData.append(byte);
                if (pngData.size() >= marker.size() &&
                        pngData.endsWith(marker)) {
                    pngData.chop(marker.size());
                    markerFound = true;
                    break;
                }
            }
            if (!markerFound) {
                mError = tr("Missing legacy page terminator for %1.\n%2")
                        .arg(page.name).arg(fileName);
                return false;
            }
        } else {
            qint32 pngLength = 0;
            if (!readInt(stream, &pngLength) || pngLength <= 0 ||
                    qint64(pngLength) > file.size() - file.pos()) {
                mError = tr("Invalid PNG length on page %1.\n%2")
                        .arg(page.name).arg(fileName);
                return false;
            }
            pngData.resize(pngLength);
            if (stream.readRawData(pngData.data(), pngLength) != pngLength) {
                mError = tr("Truncated PNG on page %1.\n%2")
                        .arg(page.name).arg(fileName);
                return false;
            }
        }

        QBuffer pngBuffer(&pngData);
        if (pngData.isEmpty() ||
                !pngBuffer.open(QIODevice::ReadOnly)) {
            mError = tr("Invalid PNG on page %1.\n%2")
                    .arg(page.name).arg(fileName);
            return false;
        }
        QImageReader imageReader(&pngBuffer, "PNG");
        if (!validImageSize(imageReader.size())) {
            mError = tr("PNG dimensions on page %1 exceed safe limits.\n%2")
                    .arg(page.name).arg(fileName);
            return false;
        }
        page.image = imageReader.read();
        if (page.image.isNull()) {
            mError = tr("Invalid PNG on page %1: %2\n%3")
                    .arg(page.name, imageReader.errorString(), fileName);
            return false;
        }
        qDebug() << "PackFile: decoded" << page.name
                 << page.image.size() << "bytes=" << pngData.size();

        for (const PackSubTexInfo &texture : std::as_const(page.mInfo)) {
            if (!validTextureGeometry(page, texture)) {
                mError = tr("Texture %1 has invalid geometry on page %2.\n%3")
                        .arg(texture.name).arg(page.name).arg(fileName);
                return false;
            }
        }
        mPages += page;
    }

    mVersion = version;
    mFileName = QFileInfo(fileName).absoluteFilePath();
    return true;
}

bool PackFile::write(const QString &fileName)
{
    mError.clear();
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        mError = tr("Error opening file for writing.\n%1").arg(fileName);
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint8('P') << quint8('Z') << quint8('P') << quint8('K');
    stream << qint32(VERSION_LATEST);
    stream << qint32(mPages.size());

    for (const PackPage &page : std::as_const(mPages)) {
        if (page.image.isNull() || !validImageSize(page.image.size())) {
            mError = tr("Page %1 has no image or exceeds safe limits.\n%2")
                    .arg(page.name).arg(fileName);
            return false;
        }
        for (const PackSubTexInfo &texture : page.mInfo) {
            if (!validTextureGeometry(page, texture)) {
                mError = tr("Texture %1 has invalid geometry on page %2.\n%3")
                        .arg(texture.name).arg(page.name).arg(fileName);
                return false;
            }
        }

        saveString(stream, page.name);
        stream << qint32(page.mInfo.size());
        stream << qint32(page.mask ? 1 : 0);
        for (const PackSubTexInfo &texture : page.mInfo) {
            saveString(stream, texture.name);
            stream << qint32(texture.x) << qint32(texture.y)
                   << qint32(texture.w) << qint32(texture.h)
                   << qint32(texture.ox) << qint32(texture.oy)
                   << qint32(texture.fx) << qint32(texture.fy);
        }

        QByteArray pngData;
        QBuffer buffer(&pngData);
        if (!buffer.open(QIODevice::WriteOnly) ||
                !page.image.save(&buffer, "PNG", -1)) {
            mError = tr("Could not encode page %1 as PNG.\n%2")
                    .arg(page.name).arg(fileName);
            return false;
        }
        stream << qint32(pngData.size());
        if (stream.writeRawData(pngData.constData(), pngData.size()) !=
                pngData.size()) {
            mError = tr("Could not write page %1.\n%2")
                    .arg(page.name).arg(fileName);
            return false;
        }
    }

    if (stream.status() != QDataStream::Ok) {
        mError = tr("Error while writing .pack file.\n%1").arg(fileName);
        return false;
    }
    if (!file.commit()) {
        mError = tr("Could not commit .pack file.\n%1").arg(fileName);
        return false;
    }

    mVersion = VERSION_LATEST;
    mFileName = QFileInfo(fileName).absoluteFilePath();
    return true;
}

int PackFile::textureCount() const
{
    int count = 0;
    for (const PackPage &page : mPages)
        count += page.mInfo.size();
    return count;
}

QByteArray PackFile::fileSha256() const
{
    if (mFileName.isEmpty())
        return QByteArray();
    QFile file(mFileName);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QByteArray();
    return hash.result();
}

QImage PackFile::extractTexture(const PackPage &page,
                                const PackSubTexInfo &texture)
{
    if (texture.fx <= 0 || texture.fy <= 0 || page.image.isNull())
        return QImage();
    QImage image(texture.fx, texture.fy, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(texture.ox, texture.oy, page.image,
                      texture.x, texture.y, texture.w, texture.h);
    return image;
}

QByteArray PackFile::textureSha256(const PackPage &page,
                                   const PackSubTexInfo &texture)
{
    const QImage extracted = extractTexture(page, texture)
            .convertToFormat(QImage::Format_RGBA8888);
    if (extracted.isNull())
        return QByteArray();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint32 width = extracted.width();
    const qint32 height = extracted.height();
    hash.addData(reinterpret_cast<const char *>(&width), sizeof(width));
    hash.addData(reinterpret_cast<const char *>(&height), sizeof(height));
    for (int y = 0; y < extracted.height(); ++y) {
        hash.addData(
                    reinterpret_cast<const char *>(
                        extracted.constScanLine(y)),
                    extracted.width() * 4);
    }
    return hash.result();
}

QByteArray PackFile::metadataSha256(const PackPage &page,
                                    const PackSubTexInfo &texture)
{
    QByteArray metadata;
    QDataStream stream(&metadata, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << page.name << page.mask
           << qint32(texture.x) << qint32(texture.y)
           << qint32(texture.w) << qint32(texture.h)
           << qint32(texture.ox) << qint32(texture.oy)
           << qint32(texture.fx) << qint32(texture.fy);
    return QCryptographicHash::hash(
                metadata, QCryptographicHash::Sha256);
}

QString PackFile::sha256Text(const QByteArray &hash)
{
    return QString::fromLatin1(hash.toHex());
}
