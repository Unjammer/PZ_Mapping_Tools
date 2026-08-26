/*
 * zprogress.cpp
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "zprogress.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ZProgressManager *ZProgressManager::mInstance = 0;

ZProgressManager *ZProgressManager::instance()
{
    if (!mInstance)
        mInstance = new ZProgressManager();
    return mInstance;
}

ZProgressManager::ZProgressManager()
    : mMainWindow(0)
    , mDialog(0)
    , mProgressBar(0)
    , mCancelButton(0)
    , mDepth(0)
    , mCancellableDepth(0)
    , mCanceled(false)
{
    mInstance = this;
}


void ZProgressManager::setMainWindow(QWidget *mainWindow)
{
    if (mMainWindow) {
        mDialog->setParent(mMainWindow = mainWindow);
        mDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::Dialog);
        return;
    }
    mMainWindow = mainWindow;
    mDialog = new QDialog(mainWindow);
    QVBoxLayout *layout = new QVBoxLayout();
    mLabel = new QLabel();
    mLabel->setMinimumSize(420, 20);
    mLabel->setWordWrap(true);
    mProgressBar = new QProgressBar();
    mProgressBar->setRange(0, 0);
    mProgressBar->setTextVisible(false);
    layout->addWidget(mLabel);
    layout->addWidget(mProgressBar);
    mCancelButton = new QPushButton(tr("Cancel"));
    mCancelButton->setVisible(false);
    layout->addWidget(mCancelButton);
    connect(mCancelButton, &QPushButton::clicked, this, [this]() {
        mCanceled = true;
        mCancelButton->setEnabled(false);
        mLabel->setText(tr("Cancelling..."));
    });
    mDialog->setWindowModality(Qt::WindowModal);
    mDialog->setLayout(layout);
    mDialog->setWindowTitle(tr("Loading"));
    mDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::Dialog);
}

void ZProgressManager::begin(const QString &text, bool cancellable)
{
    mLabel->setText(text);
    if (mDepth == 0) {
        mCanceled = false;
        mCancellableDepth = 0;
    }
    if (cancellable)
        ++mCancellableDepth;
    mCancelButton->setVisible(mCancellableDepth > 0);
    mCancelButton->setEnabled(!mCanceled);
    if (mDepth++ == 0) {
        mDialog->show();
        mDialog->raise();
    }
    qApp->processEvents(QEventLoop::AllEvents);
}

void ZProgressManager::update(const QString &text)
{
    Q_ASSERT(mDepth > 0);
    mLabel->setText(text);
    qApp->processEvents(QEventLoop::AllEvents);
}

void ZProgressManager::end(bool cancellable)
{
    Q_ASSERT(mDepth > 0);
    if (cancellable) {
        Q_ASSERT(mCancellableDepth > 0);
        --mCancellableDepth;
    }
    mCancelButton->setVisible(mCancellableDepth > 0);
//    mDialog->setValue(mDialog->maximum()); // hides dialog!
    qApp->processEvents(QEventLoop::AllEvents);
    if (--mDepth == 0)
        mDialog->hide();
}

