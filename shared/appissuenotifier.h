#ifndef APPISSUENOTIFIER_H
#define APPISSUENOTIFIER_H

#include <QDateTime>
#include <QMessageLogContext>
#include <QString>
#include <QVector>

class QMainWindow;

enum class AppIssueSeverity
{
    Warning,
    Error
};

struct AppIssue
{
    quint64 sequence = 0;
    AppIssueSeverity severity = AppIssueSeverity::Warning;
    QDateTime timestamp;
    QString message;
    QString source;
    QString thread;
};

struct AppIssueSummary
{
    int warningCount = 0;
    int errorCount = 0;
    quint64 revision = 0;
};

class AppIssueCenter
{
public:
    static void captureQtMessage(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &message);
    static void recordWarning(const QString &message,
                              const QString &source = QString());
    static void recordError(const QString &message,
                            const QString &source = QString());
    static AppIssueSummary summary();
    static QVector<AppIssue> issues();
    static void clear();
};

class AppIssueNotifier
{
public:
    static void initialize();
    static void attach(QMainWindow *window);
};

#endif
