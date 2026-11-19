#ifndef DIRECTMAPPREVIEWAGENT_H
#define DIRECTMAPPREVIEWAGENT_H

#include <QString>

namespace DirectMapPreviewAgent {
bool deploy(const QString &profileDirectory, QString &agentPath, QString &error);
}

#endif
