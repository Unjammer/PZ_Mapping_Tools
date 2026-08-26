#ifndef MAPPINGIMAGEFORMAT_H
#define MAPPINGIMAGEFORMAT_H

#include <QString>

class QImage;

namespace MappingImageFormat {

bool isSupportedPath(const QString &path);
bool saveAtomically(const QImage &image, const QString &path,
                    QString *error = nullptr);

}

#endif
