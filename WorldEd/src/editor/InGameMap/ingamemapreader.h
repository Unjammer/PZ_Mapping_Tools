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

#ifndef INGAMEMAPREADER_H
#define INGAMEMAPREADER_H

#include "ingamemapcell.h"

#include <QPoint>
#include <QVector>

class World;
class WorldDocument;

class QIODevice;

class InGameMapReaderPrivate;

class InGameMapReader
{
public:
    struct OverlayCell {
        QPoint position;
        QVector<InGameMapFeature> features;
    };

    struct Overlay {
        int cellSize = 0;
        QVector<OverlayCell> cells;
    };

    InGameMapReader();
    ~InGameMapReader();

    World *readWorld(QIODevice *device, const QString &path, World *world);
    World *readWorld(const QString &fileName, World* world);
    bool readOverlay(const QString &fileName, Overlay &overlay);
    bool replaceWorld(const QString &fileName, WorldDocument *document);

    QString errorString() const;

private:
    friend class InGameMapReaderPrivate;
    InGameMapReaderPrivate *d;
};

#endif // INGAMEMAPREADER_H
