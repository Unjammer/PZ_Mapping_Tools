#include "appissuenotifier.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEasingCurve>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct IssueState
{
    QMutex mutex;
    QVector<AppIssue> issues;
    int warningCount = 0;
    int errorCount = 0;
    quint64 nextSequence = 1;
    quint64 revision = 0;
};

IssueState &issueState()
{
    static IssueState *state = new IssueState;
    return *state;
}

QString threadLabel()
{
    QThread *thread = QThread::currentThread();
    if (!thread)
        return QString();
    if (!thread->objectName().isEmpty())
        return thread->objectName();
    return QStringLiteral("0x%1").arg(
                reinterpret_cast<quintptr>(QThread::currentThreadId()),
                0, 16);
}

void appendIssue(AppIssueSeverity severity,
                 const QString &message,
                 const QString &source,
                 bool dialogOrigin)
{
    const QString cleanedMessage = message.trimmed();
    if (cleanedMessage.isEmpty())
        return;
    IssueState &state = issueState();
    QMutexLocker locker(&state.mutex);
    if (dialogOrigin) {
        const QDateTime threshold = QDateTime::currentDateTime().addMSecs(-1500);
        for (int index = state.issues.size() - 1; index >= 0; --index) {
            const AppIssue &recent = state.issues.at(index);
            if (recent.timestamp < threshold)
                break;
            if (recent.severity == severity &&
                    recent.message == cleanedMessage) {
                return;
            }
        }
    }
    AppIssue issue;
    issue.sequence = state.nextSequence++;
    issue.severity = severity;
    issue.timestamp = QDateTime::currentDateTime();
    issue.message = cleanedMessage;
    issue.source = source.trimmed();
    issue.thread = threadLabel();
    state.issues.append(issue);
    if (state.issues.size() > 1000)
        state.issues.remove(0, state.issues.size() - 1000);
    if (severity == AppIssueSeverity::Error)
        ++state.errorCount;
    else
        ++state.warningCount;
    ++state.revision;
}

QString severityText(AppIssueSeverity severity)
{
    return severity == AppIssueSeverity::Error
            ? QApplication::translate("AppIssueNotifier", "Error")
            : QApplication::translate("AppIssueNotifier", "Warning");
}

QColor severityColor(AppIssueSeverity severity)
{
    return severity == AppIssueSeverity::Error
            ? QColor(204, 20, 20)
            : QColor(230, 132, 18);
}

QString logDirectoryPath()
{
    QDir directory = QFileInfo(QSettings().fileName()).absoluteDir();
    if (directory.dirName().compare(
                QStringLiteral("TheIndieStone"),
                Qt::CaseInsensitive) == 0) {
        directory.cdUp();
    }
    return directory.filePath(QStringLiteral("logs"));
}

class IssueDetailsDialog : public QDialog
{
public:
    explicit IssueDetailsDialog(AppIssueSeverity initialSeverity,
                                QWidget *parent = nullptr)
        : QDialog(parent)
        , mFilter(new QComboBox(this))
        , mSummary(new QLabel(this))
        , mTable(new QTableWidget(this))
        , mDetails(new QPlainTextEdit(this))
    {
        setWindowTitle(tr("Application messages"));
        resize(860, 560);
        mFilter->addItem(tr("All messages"), -1);
        mFilter->addItem(tr("Errors"), int(AppIssueSeverity::Error));
        mFilter->addItem(tr("Warnings"), int(AppIssueSeverity::Warning));
        mFilter->setCurrentIndex(initialSeverity == AppIssueSeverity::Error
                                 ? 1 : 2);
        mTable->setColumnCount(4);
        mTable->setHorizontalHeaderLabels(QStringList()
                << tr("Time") << tr("Severity") << tr("Message")
                << tr("Source"));
        mTable->horizontalHeader()->setStretchLastSection(false);
        mTable->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::ResizeToContents);
        mTable->horizontalHeader()->setSectionResizeMode(
                    1, QHeaderView::ResizeToContents);
        mTable->horizontalHeader()->setSectionResizeMode(
                    2, QHeaderView::Stretch);
        mTable->horizontalHeader()->setSectionResizeMode(
                    3, QHeaderView::ResizeToContents);
        mTable->verticalHeader()->hide();
        mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        mTable->setSelectionMode(QAbstractItemView::SingleSelection);
        mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        mDetails->setReadOnly(true);
        mDetails->setMinimumHeight(130);

        QPushButton *copyButton = new QPushButton(tr("Copy selected"), this);
        QPushButton *copyAllButton = new QPushButton(tr("Copy all"), this);
        QPushButton *logsButton = new QPushButton(tr("Open logs folder"), this);
        QPushButton *clearButton = new QPushButton(tr("Clear"), this);
        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Close, this);

        QGridLayout *topLayout = new QGridLayout;
        topLayout->addWidget(new QLabel(tr("Show:"), this), 0, 0);
        topLayout->addWidget(mFilter, 0, 1);
        topLayout->addWidget(mSummary, 0, 2, 1, 4);
        topLayout->setColumnStretch(2, 1);
        topLayout->addWidget(copyButton, 1, 0);
        topLayout->addWidget(copyAllButton, 1, 1);
        topLayout->addWidget(logsButton, 1, 3);
        topLayout->addWidget(clearButton, 1, 4);

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addLayout(topLayout);
        layout->addWidget(mTable, 1);
        layout->addWidget(mDetails);
        layout->addWidget(buttons);

        connect(mFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() { refresh(); });
        connect(mTable, &QTableWidget::itemSelectionChanged,
                this, [this]() { updateDetails(); });
        connect(copyButton, &QPushButton::clicked,
                this, [this]() { copySelected(); });
        connect(copyAllButton, &QPushButton::clicked,
                this, [this]() { copyAll(); });
        connect(logsButton, &QPushButton::clicked,
                this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                logDirectoryPath()));
        });
        connect(clearButton, &QPushButton::clicked,
                this, [this]() {
            AppIssueCenter::clear();
            refresh();
        });
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        QTimer *refreshTimer = new QTimer(this);
        refreshTimer->setInterval(300);
        connect(refreshTimer, &QTimer::timeout, this, [this]() {
            if (AppIssueCenter::summary().revision != mRevision)
                refresh();
        });
        refreshTimer->start();
        refresh();
    }

private:
    bool accepts(const AppIssue &issue) const
    {
        const int selected = mFilter->currentData().toInt();
        return selected < 0 || selected == int(issue.severity);
    }

    QString issueText(const AppIssue &issue) const
    {
        QString text = QStringLiteral("[%1] %2: %3")
                .arg(issue.timestamp.toString(
                         QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                     severityText(issue.severity), issue.message);
        if (!issue.source.isEmpty())
            text += QStringLiteral("\nSource: %1").arg(issue.source);
        if (!issue.thread.isEmpty())
            text += QStringLiteral("\nThread: %1").arg(issue.thread);
        return text;
    }

    void refresh()
    {
        const AppIssueSummary counts = AppIssueCenter::summary();
        mRevision = counts.revision;
        mSummary->setText(tr("%1 error(s), %2 warning(s)")
                          .arg(counts.errorCount)
                          .arg(counts.warningCount));
        const QVector<AppIssue> all = AppIssueCenter::issues();
        mVisible.clear();
        for (int index = all.size() - 1; index >= 0; --index) {
            if (accepts(all.at(index)))
                mVisible.append(all.at(index));
        }
        mTable->setRowCount(mVisible.size());
        for (int row = 0; row < mVisible.size(); ++row) {
            const AppIssue &issue = mVisible.at(row);
            QTableWidgetItem *time = new QTableWidgetItem(
                        issue.timestamp.toString(
                            QStringLiteral("HH:mm:ss.zzz")));
            QTableWidgetItem *severity = new QTableWidgetItem(
                        severityText(issue.severity));
            severity->setForeground(severityColor(issue.severity));
            QTableWidgetItem *message = new QTableWidgetItem(issue.message);
            QTableWidgetItem *source = new QTableWidgetItem(issue.source);
            mTable->setItem(row, 0, time);
            mTable->setItem(row, 1, severity);
            mTable->setItem(row, 2, message);
            mTable->setItem(row, 3, source);
        }
        if (mTable->rowCount() > 0)
            mTable->selectRow(0);
        else
            mDetails->clear();
    }

    void updateDetails()
    {
        const int row = mTable->currentRow();
        mDetails->setPlainText(row >= 0 && row < mVisible.size()
                               ? issueText(mVisible.at(row)) : QString());
    }

    void copySelected()
    {
        const int row = mTable->currentRow();
        if (row >= 0 && row < mVisible.size())
            QApplication::clipboard()->setText(issueText(mVisible.at(row)));
    }

    void copyAll()
    {
        QStringList text;
        for (const AppIssue &issue : mVisible)
            text.append(issueText(issue));
        QApplication::clipboard()->setText(text.join(QStringLiteral("\n\n")));
    }

    QComboBox *mFilter;
    QLabel *mSummary;
    QTableWidget *mTable;
    QPlainTextEdit *mDetails;
    QVector<AppIssue> mVisible;
    quint64 mRevision = quint64(-1);
};

class IssueBadge : public QWidget
{
public:
    IssueBadge(AppIssueSeverity severity, QWidget *parent)
        : QWidget(parent)
        , mSeverity(severity)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(104, 48);
        setObjectName(severity == AppIssueSeverity::Error
                      ? QStringLiteral("ApplicationErrorBadge")
                      : QStringLiteral("ApplicationWarningBadge"));
        setAttribute(Qt::WA_StyledBackground, false);
        setToolTip(severity == AppIssueSeverity::Error
                   ? tr("Click to view application errors")
                   : tr("Click to view application warnings"));
        setAccessibleName(severity == AppIssueSeverity::Error
                          ? tr("Application errors")
                          : tr("Application warnings"));
    }

    void setCount(int count)
    {
        if (mCount == count)
            return;
        mCount = count;
        setVisible(count > 0);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QColor accent = severityColor(mSeverity);
        painter.fillRect(rect(), accent);
        painter.setPen(QPen(accent.lighter(125), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        painter.fillRect(QRect(1, 1, width() - 2, 22), QColor(18, 18, 18));
        painter.setPen(accent.lighter(115));
        QFont titleFont = font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(QRect(0, 1, width(), 21),
                         Qt::AlignCenter,
                         mSeverity == AppIssueSeverity::Error
                         ? tr("ERROR") : tr("WARNING"));
        painter.setPen(Qt::black);
        QFont countFont = font();
        countFont.setBold(true);
        painter.setFont(countFont);
        painter.drawText(QRect(0, 24, width(), 22),
                         Qt::AlignCenter,
                         QString::number(mCount));
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            IssueDetailsDialog dialog(mSeverity, window());
            dialog.exec();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    AppIssueSeverity mSeverity;
    int mCount = 0;
};

class IssueOverlay : public QWidget
{
public:
    explicit IssueOverlay(QMainWindow *window)
        : QWidget(window)
        , mWindow(window)
        , mErrorBadge(new IssueBadge(AppIssueSeverity::Error, this))
        , mWarningBadge(new IssueBadge(AppIssueSeverity::Warning, this))
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setObjectName(QStringLiteral("ApplicationIssueOverlay"));
        setFixedSize(104, 102);
        mErrorBadge->move(0, 0);
        mWarningBadge->move(0, 54);
        mErrorBadge->hide();
        mWarningBadge->hide();
        hide();
        mAnimation = new QPropertyAnimation(this, "pos", this);
        mAnimation->setDuration(500);
        mAnimation->setEasingCurve(QEasingCurve::OutCubic);
        mHoldTimer = new QTimer(this);
        mHoldTimer->setSingleShot(true);
        mHoldTimer->setInterval(3000);
        connect(mAnimation, &QPropertyAnimation::finished,
                this, [this]() {
            if (mHiding) {
                hide();
                mHiding = false;
                return;
            }
            if (isVisible())
                mHoldTimer->start();
        });
        connect(mHoldTimer, &QTimer::timeout,
                this, [this]() { slideOut(); });
        window->installEventFilter(this);
        QTimer *timer = new QTimer(this);
        timer->setInterval(150);
        connect(timer, &QTimer::timeout, this, [this]() { refresh(); });
        timer->start();
        refresh();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == mWindow &&
                (event->type() == QEvent::Resize ||
                 event->type() == QEvent::Show ||
                 event->type() == QEvent::WindowStateChange)) {
            QTimer::singleShot(0, this, [this]() {
                if (!isVisible())
                    return;
                mAnimation->stop();
                mHiding = false;
                move(shownPosition());
                raise();
                mHoldTimer->start();
            });
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void refresh()
    {
        const AppIssueSummary counts = AppIssueCenter::summary();
        if (counts.revision == mRevision)
            return;
        mRevision = counts.revision;
        mErrorBadge->setCount(counts.errorCount);
        mWarningBadge->setCount(counts.warningCount);
        mErrorBadge->move(0, 0);
        mWarningBadge->move(0, counts.errorCount > 0 ? 54 : 0);
        setFixedHeight(counts.errorCount > 0 && counts.warningCount > 0
                       ? 102 : 48);
        if (counts.errorCount == 0 && counts.warningCount == 0) {
            mAnimation->stop();
            mHoldTimer->stop();
            mHiding = false;
            hide();
            return;
        }
        showNotification();
    }

    int availableBottom() const
    {
        if (!mWindow)
            return 0;
        int bottom = mWindow->contentsRect().bottom() + 1;
        QWidget *bottomBar = mWindow->findChild<QStatusBar *>(
                    QString(), Qt::FindDirectChildrenOnly);
        if (!bottomBar) {
            bottomBar = mWindow->findChild<QWidget *>(
                        QStringLiteral("statusBarFrame"));
        }
        if (bottomBar && bottomBar->isVisible()) {
            const int barTop = bottomBar->mapTo(mWindow, QPoint()).y();
            if (barTop > 0 && barTop < bottom)
                bottom = barTop;
        }
        return bottom;
    }

    QPoint shownPosition() const
    {
        return QPoint(qMax(0, mWindow->contentsRect().right() - width() - 11),
                      qMax(0, availableBottom() - height() - 10));
    }

    QPoint hiddenPosition() const
    {
        const QPoint shown = shownPosition();
        return QPoint(shown.x(),
                      mWindow->contentsRect().bottom() + 2);
    }

    void showNotification()
    {
        mHoldTimer->stop();
        mAnimation->stop();
        const QPoint start = isVisible() ? pos() : hiddenPosition();
        const QPoint destination = shownPosition();
        mHiding = false;
        show();
        raise();
        if (start == destination) {
            move(destination);
            mHoldTimer->start();
            return;
        }
        move(start);
        mAnimation->setEasingCurve(QEasingCurve::OutCubic);
        mAnimation->setStartValue(start);
        mAnimation->setEndValue(destination);
        mAnimation->start();
    }

    void slideOut()
    {
        if (!isVisible())
            return;
        mAnimation->stop();
        mHiding = true;
        mAnimation->setEasingCurve(QEasingCurve::InCubic);
        mAnimation->setStartValue(pos());
        mAnimation->setEndValue(hiddenPosition());
        mAnimation->start();
    }

    QMainWindow *mWindow;
    IssueBadge *mErrorBadge;
    IssueBadge *mWarningBadge;
    QPropertyAnimation *mAnimation;
    QTimer *mHoldTimer;
    bool mHiding = false;
    quint64 mRevision = quint64(-1);
};

class MessageBoxCapture : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return QObject::eventFilter(watched, event);
        QMessageBox *box = qobject_cast<QMessageBox *>(watched);
        if (!box || box->property("PZToolsIssueCaptured").toBool())
            return QObject::eventFilter(watched, event);
        AppIssueSeverity severity;
        if (box->icon() == QMessageBox::Critical)
            severity = AppIssueSeverity::Error;
        else if (box->icon() == QMessageBox::Warning)
            severity = AppIssueSeverity::Warning;
        else
            return QObject::eventFilter(watched, event);
        box->setProperty("PZToolsIssueCaptured", true);
        QPointer<QMessageBox> guardedBox(box);
        QTimer::singleShot(0, qApp, [guardedBox, severity]() {
            if (!guardedBox)
                return;
            QString message = guardedBox->text().trimmed();
            if (!guardedBox->informativeText().trimmed().isEmpty()) {
                if (!message.isEmpty())
                    message += QLatin1Char('\n');
                message += guardedBox->informativeText().trimmed();
            }
            appendIssue(severity, message,
                        guardedBox->windowTitle(), true);
        });
        return QObject::eventFilter(watched, event);
    }
};

void installMessageBoxCapture()
{
    static MessageBoxCapture *capture = nullptr;
    if (!capture && qApp) {
        capture = new MessageBoxCapture;
        qApp->installEventFilter(capture);
    }
}

}

void AppIssueCenter::captureQtMessage(QtMsgType type,
                                      const QMessageLogContext &context,
                                      const QString &message)
{
    AppIssueSeverity severity;
    if (type == QtWarningMsg)
        severity = AppIssueSeverity::Warning;
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        severity = AppIssueSeverity::Error;
    else
        return;
    QString source;
    if (context.file) {
        source = QStringLiteral("%1:%2")
                .arg(QString::fromUtf8(context.file))
                .arg(context.line);
    }
    if (context.category && context.category[0]) {
        const QString category = QString::fromUtf8(context.category);
        if (category != QStringLiteral("default")) {
            source = source.isEmpty()
                    ? category : QStringLiteral("%1 | %2").arg(category, source);
        }
    }
    appendIssue(severity, message, source, false);
}

void AppIssueCenter::recordWarning(const QString &message,
                                   const QString &source)
{
    appendIssue(AppIssueSeverity::Warning, message, source, false);
}

void AppIssueCenter::recordError(const QString &message,
                                 const QString &source)
{
    appendIssue(AppIssueSeverity::Error, message, source, false);
}

AppIssueSummary AppIssueCenter::summary()
{
    IssueState &state = issueState();
    QMutexLocker locker(&state.mutex);
    AppIssueSummary summary;
    summary.warningCount = state.warningCount;
    summary.errorCount = state.errorCount;
    summary.revision = state.revision;
    return summary;
}

QVector<AppIssue> AppIssueCenter::issues()
{
    IssueState &state = issueState();
    QMutexLocker locker(&state.mutex);
    return state.issues;
}

void AppIssueCenter::clear()
{
    IssueState &state = issueState();
    QMutexLocker locker(&state.mutex);
    state.issues.clear();
    state.warningCount = 0;
    state.errorCount = 0;
    ++state.revision;
}

void AppIssueNotifier::attach(QMainWindow *window)
{
    if (!window || window->property("PZToolsIssueNotifier").toBool())
        return;
    window->setProperty("PZToolsIssueNotifier", true);
    initialize();
    new IssueOverlay(window);
}

void AppIssueNotifier::initialize()
{
    installMessageBoxCapture();
}
