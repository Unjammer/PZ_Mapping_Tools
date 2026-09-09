#ifndef PZTOOLS_LOADINGDIALOG_H
#define PZTOOLS_LOADINGDIALOG_H

#include <QCloseEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace PZTools {

class LoadingDialog : public QDialog
{
public:
    static Qt::WindowFlags flags()
    {
        return Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint;
    }

    explicit LoadingDialog(QWidget *parent)
        : QDialog(parent, flags())
        , mLabel(new QLabel(this))
        , mProgress(new QProgressBar(this))
        , mCancel(new QPushButton(QObject::tr("Cancel"), this))
    {
        setObjectName(QStringLiteral("PZToolsLoadingDialog"));
        setWindowTitle(QObject::tr("Loading resources"));
        setWindowModality(Qt::WindowModal);
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(12);
        mLabel->setTextFormat(Qt::PlainText);
        mLabel->setWordWrap(true);
        mLabel->setFixedHeight(mLabel->fontMetrics().lineSpacing() * 3);
        mProgress->setFixedHeight(24);
        mProgress->setRange(0, 0);
        mProgress->setTextVisible(false);
        mCancel->hide();
        layout->addWidget(mLabel);
        layout->addWidget(mProgress);
        layout->addWidget(mCancel, 0, Qt::AlignRight);
        const QScreen *screen = parent ? parent->screen()
                                      : QGuiApplication::primaryScreen();
        const int width = screen ? qMin(520, screen->availableGeometry().width() - 40)
                                 : 520;
        mLabel->setFixedWidth(width - 40);
        layout->setSizeConstraint(QLayout::SetFixedSize);
    }

    QPushButton *cancelButton() const { return mCancel; }

    void setStatus(const QString &text)
    {
        mLabel->setText(text);
        mProgress->setRange(0, 0);
        mProgress->setTextVisible(false);
        mRefresh.invalidate();
    }

    void setTilesetProgress(int completed, int total)
    {
        if (completed != 0 && completed != total
                && mRefresh.isValid() && mRefresh.elapsed() < 100)
            return;
        mRefresh.start();
        mLabel->setText(QObject::tr("Loading tilesets..."));
        mProgress->setRange(0, qMax(1, total));
        mProgress->setValue(total ? completed : 1);
        mProgress->setFormat(QStringLiteral("%1 / %2 (%p%)")
                             .arg(completed).arg(total));
        mProgress->setTextVisible(true);
        repaint();
    }

protected:
    void reject() override
    {
        if (mCancel->isVisible() && mCancel->isEnabled())
            mCancel->click();
    }

    void closeEvent(QCloseEvent *event) override
    {
        reject();
        event->ignore();
    }

private:
    QLabel *mLabel;
    QProgressBar *mProgress;
    QPushButton *mCancel;
    QElapsedTimer mRefresh;
};

}

#endif
