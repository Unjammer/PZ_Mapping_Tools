/*
 * zprogress.h
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

#ifndef ZPROGRESSMANAGER_H
#define ZPROGRESSMANAGER_H

#include "tiled_global.h"

#include <QDialog>
#include <QPointer>

namespace PZTools { class LoadingDialog; }

class ZProgressManager : public QObject
{
    Q_OBJECT

public:
    static ZProgressManager *instance();

    void begin(const QString &text, bool cancellable = false);
    void update(const QString &text);
    void updateTilesets(int completed, int total);
    void end(bool cancellable = false);

    bool wasCanceled() const
    { return mCanceled; }

    bool isActive() const
    { return mDepth > 0; }

    QWidget *mainWindow() const
    { return mMainWindow; }

    void setMainWindow(QWidget *parent);

private:
    Q_DISABLE_COPY(ZProgressManager)

    ZProgressManager();

    QWidget *mMainWindow;
    QPointer<PZTools::LoadingDialog> mDialog;
    int mDepth;
    int mCancellableDepth;
    bool mCanceled;
    static ZProgressManager *mInstance;
};

class PROGRESS
{
public:
    PROGRESS(const QString &text, QWidget *parent = 0, bool cancellable = false) :
        mMainWindow(0),
        mCancellable(cancellable)
    {
        if (parent) {
            mMainWindow = ZProgressManager::instance()->mainWindow();
            ZProgressManager::instance()->setMainWindow(parent);
        }
        ZProgressManager::instance()->begin(text, mCancellable);
    }

    void update(const QString &text)
    {
        ZProgressManager::instance()->update(text);
    }

    void updateTilesets(int completed, int total)
    {
        ZProgressManager::instance()->updateTilesets(completed, total);
    }

    bool wasCanceled() const
    {
        return ZProgressManager::instance()->wasCanceled();
    }

    ~PROGRESS()
    {
        ZProgressManager::instance()->end(mCancellable);
        if (mMainWindow)
            ZProgressManager::instance()->setMainWindow(mMainWindow);
    }

    QWidget *mMainWindow;
    bool mCancellable;
};

#endif /* ZPROGRESSMANAGER_H */
