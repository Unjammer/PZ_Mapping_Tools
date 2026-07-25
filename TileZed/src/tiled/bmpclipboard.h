/*
 * Copyright 2022, Tim Baker <treectrl@users.sf.net>
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

#ifndef BMPCLIPBOARD_H
#define BMPCLIPBOARD_H

#include <QObject>
#include <QImage>
#include <QRegion>

namespace Tiled
{
namespace Internal
{

class MapDocument;

class BmpClipboard : public QObject
{
    Q_OBJECT
public:
    BmpClipboard(QObject* parent = nullptr);

    void copySelection(const MapDocument *mapDocument);
    void pasteSelection(MapDocument *mapDocument);
    void paint(MapDocument *mapDocument, const QPoint& topLeft, bool mergeable);

    bool canPaste() const;
    const QRegion& region() const;

private:
    QRegion mRegion;
    QImage mBmpMain;
    QImage mBmpVeg;
    bool mCanPaste;
};

} // namespace Internal

} // namespace Tiled

#endif // BMPCLIPBOARD_H
