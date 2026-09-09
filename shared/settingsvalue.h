#ifndef PZTOOLS_SETTINGSVALUE_H
#define PZTOOLS_SETTINGSVALUE_H

#include <QSettings>

namespace PZTools {

inline void setValueIfChanged(QSettings &settings, const QString &key, const QVariant &value)
{
    if (!settings.contains(key) || settings.value(key) != value)
        settings.setValue(key, value);
}

}

#endif
