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
#include "loadingdialog.h"

#include <QApplication>

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
    , mDepth(0)
    , mCancellableDepth(0)
    , mCanceled(false)
{
    mInstance = this;
}


void ZProgressManager::setMainWindow(QWidget *mainWindow)
{
    if (mDialog && mMainWindow == mainWindow)
        return;
    mMainWindow = mainWindow;
    if (mDialog) {
        mDialog->setParent(mainWindow, PZTools::LoadingDialog::flags());
        if (mDepth > 0)
            mDialog->show();
        return;
    }
    mDialog = new PZTools::LoadingDialog(mainWindow);
    connect(mDialog->cancelButton(), &QPushButton::clicked, this, [this]() {
        mCanceled = true;
        mDialog->cancelButton()->setEnabled(false);
        mDialog->setStatus(tr("Cancelling..."));
    });
}

void ZProgressManager::begin(const QString &text, bool cancellable)
{
    if (!mDialog)
        setMainWindow(mMainWindow);
    mDialog->setEnabled(true);
    mDialog->setStatus(text);
    if (mDepth == 0) {
        mCanceled = false;
        mCancellableDepth = 0;
    }
    if (cancellable)
        ++mCancellableDepth;
    mDialog->cancelButton()->setVisible(mCancellableDepth > 0);
    mDialog->cancelButton()->setEnabled(!mCanceled);
    if (mDepth++ == 0) {
        mDialog->adjustSize();
        mDialog->show();
        mDialog->raise();
    }
    qApp->processEvents(QEventLoop::AllEvents);
}

void ZProgressManager::update(const QString &text)
{
    Q_ASSERT(mDepth > 0);
    mDialog->setStatus(text);
    qApp->processEvents(QEventLoop::AllEvents);
}

void ZProgressManager::updateTilesets(int completed, int total)
{
    Q_ASSERT(mDepth > 0);
    mDialog->setTilesetProgress(completed, total);
}

void ZProgressManager::end(bool cancellable)
{
    Q_ASSERT(mDepth > 0);
    if (cancellable) {
        Q_ASSERT(mCancellableDepth > 0);
        --mCancellableDepth;
    }
    mDialog->cancelButton()->setVisible(mCancellableDepth > 0);
    qApp->processEvents(QEventLoop::AllEvents);
    if (--mDepth == 0)
        mDialog->hide();
}

