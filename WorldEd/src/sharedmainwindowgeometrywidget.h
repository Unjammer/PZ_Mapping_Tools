#ifndef SHAREDMAINWINDOWGEOMETRYWIDGET_H
#define SHAREDMAINWINDOWGEOMETRYWIDGET_H
#include "portablesettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
class SharedMainWindowGeometryWidget : public QWidget
{
public:
    explicit SharedMainWindowGeometryWidget(
            QWidget *targetWindow, QWidget *parent = nullptr)
        : QWidget(parent)
        , mTargetWindow(targetWindow ? targetWindow->window() : nullptr)
        , mWidth(new QSpinBox(this))
        , mHeight(new QSpinBox(this))
    {
        QVBoxLayout *layout = new QVBoxLayout(this);
        QLabel *description = new QLabel(QObject::tr(
            "Open TileZed, BuildingEd and WorldEd once at the same centered "
            "size. This does not propagate or save a geometry preference. "
            "Their next normal start uses the standard 1920 x 1080 size, "
            "fitted to the available screen."), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        QFormLayout *dimensions = new QFormLayout;
        mWidth->setRange(800, 7680);
        mHeight->setRange(600, 4320);
        mWidth->setSuffix(QObject::tr(" px"));
        mHeight->setSuffix(QObject::tr(" px"));
        mWidth->setValue(1920);
        mHeight->setValue(1080);
        dimensions->addRow(QObject::tr("Window width:"), mWidth);
        dimensions->addRow(QObject::tr("Window height:"), mHeight);
        layout->addLayout(dimensions);
        QLabel *screenNote = new QLabel(QObject::tr(
            "The window is centered on the primary screen. If the requested "
            "size is larger than the usable desktop, it is reduced to fit "
            "without covering unavailable screen space."), this);
        screenNote->setWordWrap(true);
        layout->addWidget(screenNote);
        QHBoxLayout *buttons = new QHBoxLayout;
        QPushButton *fullHd = new QPushButton(
                    QObject::tr("Use 1920 x 1080"), this);
        fullHd->setToolTip(QObject::tr(
            "Apply the centered Full HD preset to the current application."));
        QPushButton *applyCurrent = new QPushButton(
                    QObject::tr("Apply to Current Application"), this);
        applyCurrent->setToolTip(QObject::tr(
            "Apply the entered size once to the current application only."));
        QPushButton *apply = new QPushButton(
                    QObject::tr("Apply to All Three Applications"), this);
        apply->setToolTip(QObject::tr(
            "Apply this size once and open the other applications without "
            "writing a shared geometry preference."));
        buttons->addWidget(fullHd);
        buttons->addStretch();
        buttons->addWidget(applyCurrent);
        buttons->addWidget(apply);
        layout->addLayout(buttons);
        layout->addStretch();
        connect(fullHd, &QPushButton::clicked, this, [this]() {
            mWidth->setValue(1920);
            mHeight->setValue(1080);
            applyCurrentGeometry(true);
        });
        connect(applyCurrent, &QPushButton::clicked, this, [this]() {
            applyCurrentGeometry(true);
        });
        connect(apply, &QPushButton::clicked, this, [this]() {
            const QSize requested(mWidth->value(), mHeight->value());
            const QSize applied =
                    PortableSettings::applyOneShotMainWindowGeometry(
                        mTargetWindow, requested);
            const QString sizeValue = QStringLiteral("%1x%2")
                    .arg(requested.width()).arg(requested.height());
            const QString executableDirectory =
                    QCoreApplication::applicationDirPath();
            const QString currentExecutable = QFileInfo(
                    QCoreApplication::applicationFilePath()).fileName();
            const QStringList applications = {
                QStringLiteral("TileZed.exe"),
                QStringLiteral("BuildingEd.exe"),
                QStringLiteral("PZWorldEd.exe")
            };
            QStringList started;
            QStringList failed;
            for (const QString &application : applications) {
                if (application.compare(currentExecutable,
                                        Qt::CaseInsensitive) == 0) {
                    continue;
                }
                const QString executable = QDir(executableDirectory)
                        .filePath(application);
                if (!QFileInfo::exists(executable)) {
                    failed += application;
                    continue;
                }
                QProcess process;
                QProcessEnvironment environment =
                        QProcessEnvironment::systemEnvironment();
                environment.insert(
                            QStringLiteral("PZTOOLS_ONESHOT_WINDOW_SIZE"),
                            sizeValue);
                process.setProcessEnvironment(environment);
                process.setProgram(executable);
                process.setWorkingDirectory(executableDirectory);
                if (process.startDetached())
                    started += application;
                else
                    failed += application;
            }
            QString adjustment;
            if (applied.isValid() && applied != requested) {
                adjustment = QObject::tr(
                    "\n\nOn this screen, the current window was fitted to "
                    "%1 x %2 pixels.").arg(applied.width()).arg(applied.height());
            }
            QMessageBox::information(
                this, QObject::tr("One-Time Window Setup"),
                QObject::tr(
                    "The current application now uses the centered %1 x %2 "
                    "setup. Started: %3.\n\nNo shared or per-application "
                    "geometry preference was written. Each application "
                    "returns to the standard 1920 x 1080 startup size on its "
                    "next normal start.%4")
                .arg(requested.width()).arg(requested.height())
                .arg(started.isEmpty()
                     ? QObject::tr("none") : started.join(QLatin1String(", ")))
                .arg(failed.isEmpty()
                     ? QString()
                     : QObject::tr("\n\nCould not start: %1. If an application "
                                  "is already open, close it before trying "
                                  "again.").arg(failed.join(
                                                     QLatin1String(", "))))
                + adjustment);
        });
    }
private:
    QSize applyCurrentGeometry(bool report)
    {
        const QSize requested(mWidth->value(), mHeight->value());
        const QSize applied = PortableSettings::applyOneShotMainWindowGeometry(
                    mTargetWindow, requested);
        if (report) {
            QMessageBox::information(
                this, QObject::tr("One-Time Window Setup"),
                QObject::tr(
                    "The current application now uses the centered %1 x %2 "
                    "setup. This temporary size was not saved. The next "
                    "normal start uses the standard 1920 x 1080 size, "
                    "fitted to the available screen.")
                .arg(applied.isValid() ? applied.width() : requested.width())
                .arg(applied.isValid() ? applied.height() : requested.height()));
        }
        return applied;
    }

    QWidget *mTargetWindow;
    QSpinBox *mWidth;
    QSpinBox *mHeight;
};
#endif
