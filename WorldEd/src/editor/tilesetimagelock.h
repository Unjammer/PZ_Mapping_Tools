#ifndef TILESETIMAGELOCK_H
#define TILESETIMAGELOCK_H

#include <QReadWriteLock>

namespace Tiled {
namespace Internal {

inline QReadWriteLock &tilesetImageLock()
{
    static QReadWriteLock lock;
    return lock;
}

} // namespace Internal
} // namespace Tiled

#endif // TILESETIMAGELOCK_H
