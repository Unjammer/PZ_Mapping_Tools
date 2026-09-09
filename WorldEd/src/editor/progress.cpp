/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
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

#include "progress.h"
#include "loadingdialog.h"

#include "mainwindow.h"

#include <QApplication>

Progress *Progress::mInstance = 0;

Progress *Progress::instance()
{
    if (!mInstance)
        mInstance = new Progress();
    return mInstance;
}

Progress::Progress()
    : mMainWindow(0)
    , mDialog(0)
    , mDepth(0)
{
    mInstance = this;
}

void Progress::setMainWindow(QWidget *mainWindow)
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
}

bool Progress::isVisible()
{
    return mDialog && mDialog->isVisible();
}

void Progress::hide()
{
    mDialog->hide();
}

void Progress::show()
{
    mDialog->show();
}

void Progress::begin(const QString &text)
{
    if (!mDialog)
        setMainWindow(mMainWindow);
    mDialog->setEnabled(true);
    mDialog->setStatus(text);
    if (mDepth++ == 0) {
        mDialog->adjustSize();
        mDialog->show();
    }
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}

void Progress::update(const QString &text)
{
    Q_ASSERT(mDepth > 0);
    mDialog->setStatus(text);
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}

void Progress::updateTilesets(int completed, int total)
{
    Q_ASSERT(mDepth > 0);
    mDialog->setTilesetProgress(completed, total);
}

void Progress::end()
{
    Q_ASSERT(mDepth > 0);
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    if (--mDepth == 0)
        mDialog->hide();
}

