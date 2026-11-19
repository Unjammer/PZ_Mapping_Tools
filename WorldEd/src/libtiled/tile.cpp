/*
 * tile.cpp
 * Copyright 2008-2009, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2009, Edward Hutchins <eah1@yahoo.com>
 *
 * This file is part of libtiled.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tile.h"

#include "tileset.h"
#include "../../../shared/tileimagebounds.h"

#include <QMargins>
#include <QPainter>
#include <QHashFunctions>

#include <cstring>

using namespace Tiled;

bool Tile::hasResolvedSource() const
{
    return mTileset && mTileset->isLoaded() && !mTileset->isMissing();
}

void Tile::setImage(const QImage &image)
{
    setImage(image, image.rect());
}

void Tile::setImage(const QImage &sheet, const QRect &area)
{
    if (!area.isEmpty() && !sheet.rect().contains(area)) {
        setImage(sheet.copy(area));
        return;
    }
    const QRect bounds = TileImageBounds::visibleRect(sheet, area);
    mImage = bounds.isEmpty() ? QImage() : sheet.copy(bounds);
    mImageSize = area.size();
    mImageOffset = bounds.isEmpty() ? QPoint() : bounds.topLeft() - area.topLeft();
}

void Tile::setEmptyImage(int width, int height)
{
    mImage = QImage();
    mImageOffset = QPoint(0, 0);
    mImageSize = QSize(width, height);
}

QMargins Tile::drawMargins(float scale)
{
    float tileScale = tileset()->imageSource2x().isEmpty() ? 1.0f : 0.5f;
    return QMargins(mImageOffset.x(), mImageOffset.y(),
                    mImageSize.width() - (mImageOffset.x() + mImage.width()),
                    mImageSize.height() - (mImageOffset.y() + mImage.height())) * scale * tileScale;
}

QImage Tile::finalImage(int width, int height)
{
    QMargins margins = drawMargins(width / 64.0f);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QRect r(margins.left(), margins.top(), width - margins.left() - margins.right(), height - margins.top() - margins.bottom());
    painter.drawImage(r, this->image());
    painter.end();
    return image;
}

void Tile::setImage(const Tile *tile)
{
    mImage = tile->mImage;
    mImageOffset = tile->mImageOffset;
    mImageSize = tile->mImageSize;
}

quint64 Tile::imageHash() const
{
    uint hash = qHashBits(mImage.constBits(), size_t(mImage.sizeInBytes()));
    hash = qHash(mImage.width(), hash);
    hash = qHash(mImage.height(), hash);
    hash = qHash(int(mImage.format()), hash);
    hash = qHash(mImageOffset.x(), hash);
    hash = qHash(mImageOffset.y(), hash);
    hash = qHash(mImageSize.width(), hash);
    hash = qHash(mImageSize.height(), hash);
    return quint64(hash);
}

quint64 Tile::imageStorageBytes() const
{
    return quint64(mImage.sizeInBytes());
}

bool Tile::hasSameImageData(const Tile *tile) const
{
    if (!tile
            || mImage.size() != tile->mImage.size()
            || mImage.format() != tile->mImage.format()
            || mImageOffset != tile->mImageOffset
            || mImageSize != tile->mImageSize
            || mImage.sizeInBytes() != tile->mImage.sizeInBytes()) {
        return false;
    }
    return mImage.isNull()
            || std::memcmp(mImage.constBits(), tile->mImage.constBits(),
                           size_t(mImage.sizeInBytes())) == 0;
}

