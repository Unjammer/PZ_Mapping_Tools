#ifndef PZTOOLS_STARTUPWINDOWGEOMETRY_H
#define PZTOOLS_STARTUPWINDOWGEOMETRY_H

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

namespace PZTools {

inline QSize fitMainWindowToScreen(QWidget *window, const QSize &requested,
                                  QScreen *screen)
{
    if (!window || !screen || !screen->availableGeometry().isValid())
        return QSize();
    window->setWindowState(window->windowState()
                          & ~(Qt::WindowMaximized | Qt::WindowFullScreen
                              | Qt::WindowMinimized));
    const QRect available = screen->availableGeometry();
    const QMargins frame = window->windowHandle()
            ? window->windowHandle()->frameMargins() : QMargins();
    const QSize applied = requested.boundedTo(available.size());
    const QSize frameSize(frame.left() + frame.right(),
                          frame.top() + frame.bottom());
    window->resize(applied - frameSize);
    const QSize actual = window->size() + frameSize;
    if (actual.width() > available.width() || actual.height() > available.height()) {
        window->showMaximized();
        return available.size();
    }
    QRect outer(QPoint(), actual);
    outer.moveCenter(available.center());
    window->setGeometry(outer.marginsRemoved(frame));
    return actual;
}

inline QSize applyStartupMainWindowGeometry(QWidget *window)
{
    QScreen *screen = QGuiApplication::screenAt(window->frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return fitMainWindowToScreen(window, QSize(1920, 1080), screen);
}

}

#endif
