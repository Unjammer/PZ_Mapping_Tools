#ifndef DIRECTMAPPREVIEWRUNNER_H
#define DIRECTMAPPREVIEWRUNNER_H

#include <QPoint>
#include <QString>
#include <functional>

class DirectMapPreviewRunner
{
public:
    static bool isRunning();
    static bool start(const QString &project, const QString &gameDirectory, const QPoint &startCell,
                      const QString &validationScript, QString &error,
                      std::function<void(bool)> finished = {});
};

#endif
