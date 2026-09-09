#ifndef PZTOOLSPATHS_H
#define PZTOOLSPATHS_H

#include <QCoreApplication>
#include <QDir>
#include <QString>

namespace PZToolsPaths {

inline QString siblingBundleExecutable(const QString &applicationDirectory,
                                       const QString &editorName)
{
    return QDir::cleanPath(QDir(applicationDirectory).absoluteFilePath(
                QStringLiteral("../../../%1.app/Contents/MacOS/%1").arg(editorName)));
}

inline QString editorExecutable(const QString &editorName)
{
#ifdef Q_OS_MACOS
    return siblingBundleExecutable(QCoreApplication::applicationDirPath(), editorName);
#elif defined(Q_OS_WIN)
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
                editorName + QStringLiteral(".exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(editorName);
#endif
}

}

#endif
