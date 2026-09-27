/*
 * utils.h
 * Copyright 2009-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
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

#ifndef UTILS_H
#define UTILS_H

#include <QIcon>
#include <QString>

class QAction;
class QMenu;

namespace Tiled {
namespace Utils {

/**
 * Returns a file dialog filter that matches all readable image formats.
 */
QString readableImageFormatsFilter();

/**
 * Returns a file dialog filter that matches all writable image formats.
 */
QString writableImageFormatsFilter();

/**
 * Calculates all coherent regions occupied by the given \a region.
 * Returns a list of regions, where each region is coherent in itself.
 */
QList<QRegion> coherentRegions(const QRegion &region);

} // namespace Utils
} // namespace Tiled

#endif // UTILS_H
