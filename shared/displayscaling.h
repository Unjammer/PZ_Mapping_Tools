#ifndef PZTOOLS_DISPLAYSCALING_H
#define PZTOOLS_DISPLAYSCALING_H

#include <QGuiApplication>

namespace PZTools {

inline void configureDisplayScaling()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
                Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
}

}

#endif
