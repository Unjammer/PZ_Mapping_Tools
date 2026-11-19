#include "directmappreviewagent.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSaveFile>

bool DirectMapPreviewAgent::deploy(const QString &profileDirectory, QString &agentPath, QString &error)
{
    QFile source(QStringLiteral(":/playtest/PZDirectMapPreview.jar"));
    if (!source.open(QIODevice::ReadOnly)) {
        error = QObject::tr("WorldEd could not read its embedded Play Map Source agent.");
        return false;
    }
    const QByteArray payload = source.readAll();
    if (payload.isEmpty() || payload.size() != source.size()) {
        error = QObject::tr("WorldEd could not read its complete Play Map Source agent.");
        return false;
    }
    const QString path = QDir(profileDirectory).absoluteFilePath(QStringLiteral("PZDirectMapPreview.jar"));
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly) || target.write(payload) != payload.size() || !target.commit()) {
        error = QObject::tr("Could not prepare the Play Map Source agent at %1.\n%2")
                .arg(QDir::toNativeSeparators(path), target.errorString());
        return false;
    }
    agentPath = path;
    return true;
}
