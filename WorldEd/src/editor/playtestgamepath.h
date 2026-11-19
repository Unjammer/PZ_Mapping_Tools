#ifndef PLAYTESTGAMEPATH_H
#define PLAYTESTGAMEPATH_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace PlaytestGamePath {

inline QString root(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    QDir directory(QDir::fromNativeSeparators(path.trimmed()));
    if (directory.dirName().compare(QStringLiteral("media"), Qt::CaseInsensitive) == 0)
        directory.cdUp();
    return QDir::cleanPath(directory.absolutePath());
}

inline QString installationError(const QString &path)
{
    const QString directory = root(path);
    if (directory.isEmpty())
        return QCoreApplication::translate("PlaytestGamePath", "Choose the Project Zomboid installation folder to start the playtest.");
    if (!QFileInfo(directory).isDir())
        return QCoreApplication::translate("PlaytestGamePath", "The Project Zomboid installation folder does not exist.\n%1")
                .arg(QDir::toNativeSeparators(directory));
    const QDir game(directory);
    for (const QString &file : {QStringLiteral("projectzomboid.jar"),
                               QStringLiteral("ProjectZomboid64.json"),
                               QStringLiteral("jre64/bin/java.exe")}) {
        const QFileInfo info(game.filePath(file));
        if (!info.isFile() || !info.isReadable())
            return QCoreApplication::translate("PlaytestGamePath", "The playtest requires this file from the Windows game installation. Check that it exists and is readable.\n%1")
                    .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
    }
    return {};
}

}

#endif
