#ifndef PORTABLESETTINGS_H
#define PORTABLESETTINGS_H

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThread>

#include <cstdio>
#include <cstdlib>

namespace PortableSettings {

inline QString executableDirectoryPath()
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

inline bool usesBinLayout()
{
    return QDir(executableDirectoryPath()).dirName().compare(
                QLatin1String("bin"), Qt::CaseInsensitive) == 0;
}

inline QString installRootPath()
{
    QDir directory(executableDirectoryPath());
    if (usesBinLayout())
        directory.cdUp();
    return directory.absolutePath();
}

inline QString installPath(const QString &relativePath)
{
    return QDir(installRootPath()).filePath(relativePath);
}

inline QString applicationConfigPath()
{
    const QString configDirectory = installPath(QLatin1String("config"));
    if (usesBinLayout() || QDir(configDirectory).exists())
        return configDirectory;
    return executableDirectoryPath();
}

inline QString rootPath()
{
    return installPath(QLatin1String("settings"));
}

inline QString path(const QString &relativePath)
{
    return QDir(rootPath()).filePath(relativePath);
}

inline void installDefaultConfigurationFiles()
{
    const QDir sourceDirectory(applicationConfigPath());
    const QDir destinationDirectory(rootPath());
    if (sourceDirectory.absolutePath().compare(destinationDirectory.absolutePath(),
                                               Qt::CaseInsensitive) == 0)
        return;

    const QStringList fileNames = sourceDirectory.entryList(
                QStringList() << QLatin1String("*.txt"), QDir::Files | QDir::Readable);
    for (const QString &fileName : fileNames) {
        const QString destination = destinationDirectory.filePath(fileName);
        if (!QFile::exists(destination))
            QFile::copy(sourceDirectory.filePath(fileName), destination);
    }
}

inline void configure()
{
    const QString path = rootPath();
    QDir().mkpath(path);
    installDefaultConfigurationFiles();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
}

inline QFile &messageLogFile()
{
    static QFile file;
    return file;
}

inline QMutex &messageLogMutex()
{
    static QMutex mutex;
    return mutex;
}

inline const char *messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARNING";
    case QtCriticalMsg: return "CRITICAL";
    case QtFatalMsg: return "FATAL";
    }
    return "UNKNOWN";
}

inline void messageHandler(QtMsgType type,
                           const QMessageLogContext &context,
                           const QString &message)
{
    const QString source = context.file
            ? QStringLiteral(" (%1:%2)")
              .arg(QString::fromUtf8(context.file))
              .arg(context.line)
            : QString();
    const QString line = QStringLiteral("%1 [%2] [pid:%3 thread:%4] %5%6\n")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
            .arg(QString::fromLatin1(messageTypeName(type)))
            .arg(QCoreApplication::applicationPid())
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
            .arg(message, source);
    const QByteArray encoded = line.toUtf8();

    {
        QMutexLocker locker(&messageLogMutex());
        QFile &file = messageLogFile();
        if (file.isOpen()) {
            file.write(encoded);
            file.flush();
        }
    }

    std::fwrite(encoded.constData(), 1, size_t(encoded.size()), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg)
        std::abort();
}

inline QString installLogging()
{
    const QString logDirectory = path(QStringLiteral("logs"));
    if (!QDir().mkpath(logDirectory))
        return QString();

    QString applicationName = QCoreApplication::applicationName();
    for (int i = 0; i < applicationName.size(); ++i) {
        const QChar c = applicationName.at(i);
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            applicationName[i] = QLatin1Char('_');
    }
    if (applicationName.isEmpty())
        applicationName = QStringLiteral("application");

    const QString fileName = QStringLiteral("%1-%2-%3.log")
            .arg(applicationName)
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyyMMdd-HHmmss-zzz")))
            .arg(QCoreApplication::applicationPid());
    const QString filePath = QDir(logDirectory).filePath(fileName);

    QFile &file = messageLogFile();
    file.setFileName(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return QString();

    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "Logging to" << QDir::toNativeSeparators(filePath);
    qInfo().noquote() << "Installation root" << QDir::toNativeSeparators(installRootPath());
    qInfo().noquote() << "Application configuration" << QDir::toNativeSeparators(applicationConfigPath());
    QSettings settings;
    qInfo().noquote() << "Settings file" << QDir::toNativeSeparators(settings.fileName());
    return filePath;
}

} // namespace PortableSettings

#endif // PORTABLESETTINGS_H
