/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "luaconsole.h"
#include "ui_luaconsole.h"

#include "luamapsdialog.h"
#include "luaworlddialog.h"
#include "mainwindow.h"
#include "preferences.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

using namespace Tiled;
using namespace Internal;

LuaConsole *LuaConsole::mInstance = 0;

LuaConsole *LuaConsole::instance()
{
    if (!mInstance)
        mInstance = new LuaConsole;
    return mInstance;
}

void LuaConsole::clearScriptRunnerIfExists()
{
    if (mInstance)
        mInstance->clearScriptRunner();
}

LuaConsole::LuaConsole(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::LuaConsole),
    mBuildingEditorMode(false)
{
    setWindowFlags(windowFlags() | Qt::Tool);

    ui->setupUi(this);

    connect(ui->actionRun_Script, &QAction::triggered, this, &LuaConsole::runScript);
    connect(ui->actionRunAgain, &QAction::triggered, this, &LuaConsole::runAgain);
    connect(ui->btnRunAgain, &QAbstractButton::clicked, this, &LuaConsole::runAgain);
    connect(ui->actionRunInDirectory, &QAction::triggered, this, &LuaConsole::runInDirectory);
    connect(ui->actionRunOnWorld, &QAction::triggered, this, &LuaConsole::runOnWorld);

    connect(ui->clear, &QAbstractButton::clicked, this, &LuaConsole::clear);

    connect(ui->actionHelpContents, &QAction::triggered, this, &LuaConsole::helpContents);
}

LuaConsole::~LuaConsole()
{
    delete ui;
}

void LuaConsole::setFile(const QString &fileName)
{
    mFileName = fileName;
    setWindowTitle(tr("%1 - Lua Console").arg(QFileInfo(mFileName).fileName()));
}

void LuaConsole::setScriptRunner(
        const std::function<void (const QString &)> &runner,
        bool buildingEditorMode)
{
    mScriptRunner = runner;
    mBuildingEditorMode = buildingEditorMode;
    ui->actionRunInDirectory->setVisible(!buildingEditorMode);
    ui->actionRunOnWorld->setVisible(!buildingEditorMode);
}

void LuaConsole::clearScriptRunner()
{
    mScriptRunner = std::function<void (const QString &)>();
    mBuildingEditorMode = false;
    ui->actionRunInDirectory->setVisible(true);
    ui->actionRunOnWorld->setVisible(true);
}

void LuaConsole::runScript()
{
    if (mScriptRunner)
        mScriptRunner(QString());
    else if (MainWindow::instance())
        MainWindow::instance()->LuaScript(QString());
}

void LuaConsole::runAgain()
{
    if (mScriptRunner)
        mScriptRunner(mFileName);
    else if (MainWindow::instance())
        MainWindow::instance()->LuaScript(mFileName);
}

void LuaConsole::runInDirectory()
{
    LuaMapsDialog d(this);
    d.exec();

    // Not sure why MainWindow comes to the front, maybe due to this
    // window having the Qt::Tool flag?
    raise();
    activateWindow();
}

void LuaConsole::runOnWorld()
{
    LuaWorldDialog d(this);
    d.exec();

    // Not sure why MainWindow comes to the front, maybe due to this
    // window having the Qt::Tool flag?
    raise();
    activateWindow();
}

void LuaConsole::clear()
{
    ui->textEdit->clear();
}

void LuaConsole::writestring(const char *s, int len)
{
    ui->textEdit->moveCursor(QTextCursor::End);
    QString text = QString::fromUtf8(s, len);
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLatin1(s, len);
    ui->textEdit->insertPlainText(text);
}

void LuaConsole::writeline()
{
    ui->textEdit->moveCursor(QTextCursor::End);
    ui->textEdit->insertPlainText(QLatin1String("\n"));
}

void LuaConsole::write(const QString &s, QColor color)
{
    if (s.isEmpty()) return;

    ui->textEdit->moveCursor(QTextCursor::End);
    ui->textEdit->setTextColor(color);
    ui->textEdit->insertPlainText(s);

//    ui->textEdit->moveCursor(QTextCursor::End);
    ui->textEdit->setTextColor(Qt::black);
    ui->textEdit->insertPlainText(QLatin1String("\n"));
}

void LuaConsole::helpContents()
{
    QUrl url = QUrl::fromLocalFile(
            Preferences::instance()->docsPath(
                mBuildingEditorMode
                ? QLatin1String("BuildingEd/LuaScripting.html")
                : QLatin1String("TileZed/LuaScripting.html")));
    QDesktopServices::openUrl(url);
}
