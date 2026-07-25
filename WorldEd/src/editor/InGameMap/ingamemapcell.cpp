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

#include "ingamemapcell.h"

#include <QtMath>

#include <cmath>
#include <limits>

namespace {

bool normalizeCoordinates(const InGameMapCoordinates &source,
                          InGameMapCoordinates &result,
                          bool polygon,
                          QString &error)
{
    result.clear();
    for (const InGameMapPoint &point : source) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            error = QStringLiteral("contains a non-finite coordinate");
            return false;
        }

        const qint64 x = qRound64(point.x);
        const qint64 y = qRound64(point.y);
        if (x < std::numeric_limits<qint16>::min() ||
                x > std::numeric_limits<qint16>::max() ||
                y < std::numeric_limits<qint16>::min() ||
                y > std::numeric_limits<qint16>::max()) {
            error = QStringLiteral("contains a coordinate outside the signed 16-bit game format");
            return false;
        }

        InGameMapPoint normalized(x, y);
        if (result.isEmpty() || result.last() != normalized)
            result += normalized;
    }

    if (polygon && result.size() > 1 && result.first() == result.last())
        result.removeLast();

    if (polygon) {
        bool removed = true;
        while (removed && result.size() >= 3) {
            removed = false;
            for (int i = 0; i < result.size(); ++i) {
                const InGameMapPoint &previous = result.at((i + result.size() - 1) % result.size());
                const InGameMapPoint &current = result.at(i);
                const InGameMapPoint &next = result.at((i + 1) % result.size());
                const qint64 cross =
                        qint64(current.x - previous.x) * qint64(next.y - current.y) -
                        qint64(current.y - previous.y) * qint64(next.x - current.x);
                if (cross == 0) {
                    result.removeAt(i);
                    removed = true;
                    break;
                }
            }
        }

        if (result.size() < 3) {
            error = QStringLiteral("has fewer than 3 distinct non-collinear vertices");
            return false;
        }

        qint64 twiceArea = 0;
        for (int i = 0; i < result.size(); ++i) {
            const InGameMapPoint &a = result.at(i);
            const InGameMapPoint &b = result.at((i + 1) % result.size());
            twiceArea += qint64(a.x) * qint64(b.y) - qint64(b.x) * qint64(a.y);
        }
        if (twiceArea == 0) {
            error = QStringLiteral("has zero area");
            return false;
        }
    }

    return true;
}

} // namespace

bool sanitizeInGameMapGeometryForExport(const InGameMapGeometry &source,
                                        InGameMapGeometry &result,
                                        QStringList &diagnostics)
{
    result.mType = source.mType;
    result.mCoordinates.clear();
    diagnostics.clear();

    const bool polygon = source.isPolygon();
    const int minimumPoints = polygon ? 3 : (source.isLineString() ? 2 : 1);
    if (!polygon && !source.isLineString() && !source.isPoint()) {
        diagnostics += QStringLiteral("unsupported geometry type '%1'").arg(source.mType);
        return false;
    }
    if (source.mCoordinates.isEmpty()) {
        diagnostics += QStringLiteral("geometry contains no coordinate list");
        return false;
    }

    for (int index = 0; index < source.mCoordinates.size(); ++index) {
        InGameMapCoordinates normalized;
        QString error;
        if (!normalizeCoordinates(source.mCoordinates.at(index), normalized, polygon, error) ||
                normalized.size() < minimumPoints) {
            if (polygon && index > 0) {
                diagnostics += QStringLiteral("dropped invalid hole %1: %2")
                        .arg(index).arg(error);
                continue;
            }
            diagnostics += QStringLiteral("invalid coordinate list %1: %2")
                    .arg(index).arg(error);
            return false;
        }
        if (normalized.size() != source.mCoordinates.at(index).size())
            diagnostics += QStringLiteral("normalized coordinate list %1 from %2 to %3 vertices")
                    .arg(index)
                    .arg(source.mCoordinates.at(index).size())
                    .arg(normalized.size());
        result.mCoordinates += normalized;
    }

    return !result.mCoordinates.isEmpty();
}

