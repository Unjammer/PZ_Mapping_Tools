#ifndef PZTOOLSBUILD_H
#define PZTOOLSBUILD_H

#include <QString>

#define PZTOOLS_BUILD_ID "20260828"

namespace PZToolsBuild {

inline QString id()
{
    return QStringLiteral(PZTOOLS_BUILD_ID);
}

inline QString label()
{
    return QStringLiteral("Build " PZTOOLS_BUILD_ID);
}

}

#endif
