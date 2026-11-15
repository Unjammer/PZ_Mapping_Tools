#ifndef DIRECTMAPPREVIEWRUNNER_H
#define DIRECTMAPPREVIEWRUNNER_H

#include <QPoint>
#include <QString>
#include <QVector>
#include <functional>

class DirectMapPreviewRunner
{
public:
    static bool isRunning();
    static bool start(const QString &project, const QString &gameDirectory, const QVector<QPoint> &selection,
                      const QString &validationScript, QString &error,
                      std::function<void(bool)> finished = {});
};

#endif
