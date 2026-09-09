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

#ifndef PROGRESS_H
#define PROGRESS_H

#include <QString>
#include <QPointer>

namespace PZTools { class LoadingDialog; }
class QWidget;

class Progress
{
public:
    static Progress *instance();
    static void deleteInstance();

    void begin(const QString &text);
    void update(const QString &text);
    void updateTilesets(int completed, int total);
    void end();

    QWidget *mainWindow() const
    { return mMainWindow; }

    void setMainWindow(QWidget *parent);

    bool isVisible();
    bool isActive() const
    { return mDepth > 0; }
    void hide();
    void show();

private:
    QWidget *mMainWindow;
    QPointer<PZTools::LoadingDialog> mDialog;
    int mDepth;

    Progress();
    static Progress *mInstance;
    Q_DISABLE_COPY(Progress)
};

class PROGRESS
{
public:
    PROGRESS(const QString &text, QWidget *parent = nullptr) :
        mMainWindow(nullptr)
    {
        if (parent) {
            mMainWindow = Progress::instance()->mainWindow();
            Progress::instance()->setMainWindow(parent);
        }
        Progress::instance()->begin(text);
    }

    void update(const QString &text)
    {
        Progress::instance()->update(text);
    }

    void release()
    {
        if (mReleased) {
            return;
        }
        mReleased = true;
        Progress::instance()->end();
        if (mMainWindow != nullptr) {
            Progress::instance()->setMainWindow(mMainWindow);
        }
    }

    void updateTilesets(int completed, int total)
    {
        Progress::instance()->updateTilesets(completed, total);
    }

    ~PROGRESS()
    {
        release();
    }

private:
    QWidget *mMainWindow;
    bool mReleased = false;
};

class PROGRESS_HIDER
{
public:
    PROGRESS_HIDER()
        : mVisible(Progress::instance()->isVisible())
    {
        if (mVisible) {
            Progress::instance()->hide();
        }
    }

    ~PROGRESS_HIDER()
    {
        if (mVisible) {
            Progress::instance()->show();
        }
    }

private:
    bool mVisible;
};

#endif // PROGRESS_H
