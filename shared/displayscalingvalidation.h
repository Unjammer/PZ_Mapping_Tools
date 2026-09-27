#ifndef PZTOOLS_DISPLAYSCALINGVALIDATION_H
#define PZTOOLS_DISPLAYSCALINGVALIDATION_H

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QScreen>
#include <QWindow>

namespace PZTools {

inline bool validateDisplayScaling(QWidget *window, const QString &name,
                                   QString *error)
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QScreen *screen = window->windowHandle()->screen();
    const QRect available = screen->availableGeometry();
    const QRect client(window->mapToGlobal(QPoint()), window->size());
    qInfo() << "Display scaling:" << name << "DPR" << window->devicePixelRatioF()
            << "desktop" << available << "client" << client
            << "minimum" << window->minimumSizeHint();
    for (QDockWidget *dock : window->findChildren<QDockWidget *>()) {
        if (dock->isVisible())
            qInfo() << "Dock layout:" << dock->objectName() << dock->size()
                    << "minimum" << dock->minimumSizeHint();
    }
    const QString outputDirectory = QString::fromLocal8Bit(
                qgetenv("PZTOOLS_VALIDATION_IMAGE_DIRECTORY"));
    if (!outputDirectory.isEmpty()
            && !window->grab().save(QDir(outputDirectory).filePath(name + QStringLiteral(".png")))) {
        *error = QStringLiteral("Could not save the window image: %1").arg(name);
        return false;
    }
    if (!available.adjusted(-1, -1, 1, 1).contains(client)) {
        *error = QStringLiteral("Window content exceeds the available desktop: %1").arg(name);
        return false;
    }
    bool expectedOk = false;
    const qreal expected = QString::fromLatin1(
                qgetenv("PZTOOLS_EXPECTED_DPR")).toDouble(&expectedOk);
    if (expectedOk && qAbs(window->devicePixelRatioF() - expected) > 0.01) {
        *error = QStringLiteral("Unexpected device pixel ratio: %1").arg(name);
        return false;
    }
    return true;
}

}

#endif
