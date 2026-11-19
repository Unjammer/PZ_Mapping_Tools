#ifndef TILEIMAGEBOUNDS_H
#define TILEIMAGEBOUNDS_H

#include <QImage>

namespace TileImageBounds {

template <class Visible>
QRect find(const QRect &area, Visible visible)
{
    const int first = area.left();
    const int last = area.right();
    const auto rowEmpty = [&](int y) {
        for (int x = first; x <= last; ++x) {
            if (visible(x, y))
                return false;
        }
        return true;
    };
    int top = area.top();
    int bottom = area.bottom();
    while (top <= bottom && rowEmpty(top))
        ++top;
    if (top > bottom)
        return {};
    while (bottom > top && rowEmpty(bottom))
        --bottom;

    int left = last;
    int right = first;
    for (int y = top; y <= bottom && (left > first || right < last); ++y) {
        for (int x = first; x < left; ++x) {
            if (visible(x, y)) {
                left = x;
                break;
            }
        }
        for (int x = last; x > right; --x) {
            if (visible(x, y)) {
                right = x;
                break;
            }
        }
    }
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

inline QRect visibleRect(const QImage &image, const QRect &area)
{
    if (area.isEmpty())
        return {};
    if ((image.format() == QImage::Format_ARGB32_Premultiplied || image.format() == QImage::Format_ARGB32)
            && image.bytesPerLine() % sizeof(QRgb) == 0) {
        const QRgb *pixels = reinterpret_cast<const QRgb *>(image.constBits());
        const qsizetype stride = image.bytesPerLine() / sizeof(QRgb);
        return find(area, [pixels, stride](int x, int y) { return qAlpha(pixels[y * stride + x]) != 0; });
    }
    return find(area, [&image](int x, int y) { return qAlpha(image.pixel(x, y)) != 0; });
}

}

#endif
