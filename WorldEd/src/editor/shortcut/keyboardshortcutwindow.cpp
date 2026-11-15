/*
 * Copyright 2025, Tim Baker <treectrl@users.sf.net>
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

#include "keyboardshortcutwindow.h"
#include "ui_keyboardshortcutwindow.h"

#include "actionmanager.h"
#include "shortcuteditorwidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>

#include <algorithm>

KeyboardShortcutWindow::KeyboardShortcutWindow(ActionManager *actionManager, QSettings *settings, const QString &settingsKey, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::KeyboardShortcutWindow),
    mActionManager(actionManager),
    mSettings(settings),
    mSettingsKey(settingsKey)
{
    ui->setupUi(this);

    mEditorWidget = new ShortcutEditorWidget(mActionManager);
    connect(mEditorWidget, &ShortcutEditorWidget::shortcutEdited, actionManager, &ActionManager::shortcutEdited);
    connect(mEditorWidget, &ShortcutEditorWidget::closeRequested, this, &QWidget::close);
    ui->centralLayout->insertWidget(0, mEditorWidget, 1);

    connect(ui->actionReloadFromDisk, &QAction::triggered, this, &KeyboardShortcutWindow::reloadFromDisk);
    ui->actionClose->setShortcuts(QList<QKeySequence>() << QKeySequence::StandardKey::Close << Qt::Key_Escape);
    connect(ui->actionClose, &QAction::triggered, this, &QMainWindow::close);
    ui->closeButton->setFocusPolicy(Qt::TabFocus);
    connect(ui->closeButton, &QPushButton::clicked, ui->actionClose, &QAction::trigger);

    captureSavedShortcuts();
    readSettings();
}

KeyboardShortcutWindow::~KeyboardShortcutWindow()
{
    delete ui;
}

void KeyboardShortcutWindow::closeEvent(QCloseEvent *event)
{
    if (!resolveShortcutChanges(false)) {
        event->ignore();
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

bool KeyboardShortcutWindow::resolveShortcutChanges(bool reload)
{
    mEditorWidget->finishShortcutEdit();
    const auto &actions = mActionManager->registeredActions();
    const auto edited = std::any_of(actions.cbegin(), actions.cend(), [this](QAction *action) {
        return mSavedShortcuts.value(action) != action->shortcuts();
    });
    QString error;
    if (edited) {
        const auto answer = QMessageBox::question(
                    this, tr("Unsaved Keyboard Shortcuts"),
                    reload ? tr("Save your changes before reloading keyboard shortcuts?")
                           : tr("Save your changes to the keyboard shortcuts?"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Cancel);
        if (answer == QMessageBox::Save) {
            if (!mActionManager->save(error)) {
                QMessageBox::critical(this, tr("Cannot Save Keyboard Shortcuts"), error);
                return false;
            }
            captureSavedShortcuts();
            return true;
        }
        if (answer != QMessageBox::Discard)
            return false;
    }

    QHash<QAction *, QList<QKeySequence>> stagedShortcuts;
    if (edited) {
        for (QAction *action : actions) {
            if (reload)
                stagedShortcuts.insert(action, action->shortcuts());
            action->setShortcuts(mSavedShortcuts.value(action));
        }
    }

    if (reload) {
        if (!mActionManager->load(error)) {
            for (auto it = stagedShortcuts.cbegin(); it != stagedShortcuts.cend(); ++it)
                it.key()->setShortcuts(it.value());
            QMessageBox::warning(this, tr("Cannot Load Keyboard Shortcuts"), error);
            return false;
        }
        captureSavedShortcuts();
    }
    if (edited || reload) {
        mEditorWidget->setModelData();
        mActionManager->emitShortcutEditedForAllActions();
    }
    return true;
}

void KeyboardShortcutWindow::captureSavedShortcuts()
{
    mSavedShortcuts.clear();
    for (QAction *action : mActionManager->registeredActions())
        mSavedShortcuts.insert(action, action->shortcuts());
}

void KeyboardShortcutWindow::saveSettings()
{
    mSettings->beginGroup(mSettingsKey);
    mSettings->setValue(QStringLiteral("geometry"), saveGeometry());
    mEditorWidget->saveSettings(*mSettings);
    mSettings->endGroup();
}

void KeyboardShortcutWindow::readSettings()
{
    mSettings->beginGroup(mSettingsKey);
    QByteArray geom = mSettings->value(QStringLiteral("geometry")).toByteArray();
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    }
    mEditorWidget->readSettings(*mSettings);
    mSettings->endGroup();
}

void KeyboardShortcutWindow::reloadFromDisk()
{
    resolveShortcutChanges(true);
}
