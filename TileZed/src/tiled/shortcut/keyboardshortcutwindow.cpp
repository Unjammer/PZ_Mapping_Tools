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
#include "../toolwindowclose.h"

#include <QAction>
#include <QSettings>
#include <QCloseEvent>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QStatusBar>

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
    connect(mEditorWidget, &ShortcutEditorWidget::shortcutEdited, this, [this]() {
        if (!mFinishingEdit)
            saveShortcuts();
    });
    connect(mEditorWidget, &ShortcutEditorWidget::closeRequested,
            this, &QWidget::close);

    auto *layout = new QVBoxLayout(ui->centralwidget);
    layout->addWidget(mEditorWidget, 1);
    auto *buttons = toolWindowCloseButtons(this);
    buttons->button(QDialogButtonBox::Close)->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(buttons);

    connect(ui->actionReloadFromDisk, &QAction::triggered, this, &KeyboardShortcutWindow::reloadFromDisk);
    ui->actionClose->setShortcuts(QKeySequence::Close);
    connect(ui->actionClose, &QAction::triggered, this, &QMainWindow::close);

    mSavedShortcuts = currentShortcuts();
    readSettings();
}

KeyboardShortcutWindow::~KeyboardShortcutWindow()
{
    delete ui;
}

void KeyboardShortcutWindow::closeEvent(QCloseEvent *event)
{
    if (!resolvePendingEdits()) {
        event->ignore();
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

bool KeyboardShortcutWindow::resolvePendingEdits()
{
    {
        const QScopedValueRollback<bool> finishingEdit(mFinishingEdit, true);
        mEditorWidget->finishShortcutEdit();
    }
    if (currentShortcuts() == mSavedShortcuts)
        return true;

    const auto answer = QMessageBox::question(
                this, tr("Unsaved Keyboard Shortcuts"),
                tr("Save changes to keyboard shortcuts?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveShortcuts();
    if (answer != QMessageBox::Discard)
        return false;

    for (QAction *action : mActionManager->registeredActions()) {
        const auto saved = mSavedShortcuts.constFind(action);
        if (saved != mSavedShortcuts.constEnd())
            action->setShortcuts(saved.value());
    }
    mEditorWidget->setModelData();
    mActionManager->emitShortcutEditedForAllActions();
    statusBar()->clearMessage();
    return true;
}

QHash<QAction *, QList<QKeySequence>> KeyboardShortcutWindow::currentShortcuts() const
{
    QHash<QAction *, QList<QKeySequence>> shortcuts;
    for (QAction *action : mActionManager->registeredActions())
        shortcuts.insert(action, action->shortcuts());
    return shortcuts;
}

bool KeyboardShortcutWindow::saveShortcuts()
{
    QString error;
    if (!mActionManager->save(error)) {
        statusBar()->showMessage(tr("Cannot save keyboard shortcuts: %1").arg(error));
        return false;
    }
    mSavedShortcuts = currentShortcuts();
    statusBar()->clearMessage();
    return true;
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
    if (!resolvePendingEdits())
        return;

    QString error;
    if (!mActionManager->load(error)) {
        statusBar()->showMessage(tr("Cannot load keyboard shortcuts: %1").arg(error));
        return;
    }
    mSavedShortcuts = currentShortcuts();
    statusBar()->clearMessage();
    mEditorWidget->setModelData();
    mActionManager->emitShortcutEditedForAllActions();
}
