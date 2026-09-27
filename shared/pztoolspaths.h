#ifndef PZTOOLSPATHS_H
#define PZTOOLSPATHS_H

#include <QCoreApplication>
#include <QDir>
#include <QString>

namespace PZToolsPaths {

inline QString editorExecutable(const QString &editorName)
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
                editorName + QStringLiteral(".exe"));
}

}

#endif
