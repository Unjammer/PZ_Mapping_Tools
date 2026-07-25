#include "undoredobuttons.h"

#include <QApplication>
#include <QToolButton>
#include <QUndoStack>

UndoRedoButtons::UndoRedoButtons(QUndoStack *undoStack, QObject *parent) :
    QObject(parent),
    mUndoStack(undoStack)
{
    connect(mUndoStack, &QUndoStack::indexChanged, this, &UndoRedoButtons::updateActions);

    mUndo = new QToolButton();
    mUndo->setObjectName(QString::fromUtf8("undo"));
    QIcon icon2;
    icon2.addFile(QString::fromUtf8(":/images/16x16/edit-undo.png"), QSize(), QIcon::Normal, QIcon::Off);
    mUndo->setIcon(icon2);
    mUndo->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    mRedo = new QToolButton();
    mRedo->setObjectName(QString::fromUtf8("redo"));
    QIcon icon3;
    icon3.addFile(QString::fromUtf8(":/images/16x16/edit-redo.png"), QSize(), QIcon::Normal, QIcon::Off);
    mRedo->setIcon(icon3);
    mRedo->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    connect(mUndo, &QAbstractButton::clicked, mUndoStack, &QUndoStack::undo);
    connect(mRedo, &QAbstractButton::clicked, mUndoStack, &QUndoStack::redo);
    connect(mUndoStack, &QUndoStack::undoTextChanged, this, &UndoRedoButtons::textChanged);
    connect(mUndoStack, &QUndoStack::redoTextChanged, this, &UndoRedoButtons::textChanged);

    retranslateUi();

    mUndoIndex = mUndoStack->index();
    updateActions();
}

void UndoRedoButtons::retranslateUi()
{
    mUndo->setText(QApplication::translate("UndoRedoButtons", "Undo"));
    mRedo->setText(QApplication::translate("UndoRedoButtons", "Redo"));
}

void UndoRedoButtons::resetIndex()
{
    mUndoIndex = mUndoStack->index();
    updateActions();
}

void UndoRedoButtons::updateActions()
{
    mUndo->setEnabled(mUndoStack->index() > mUndoIndex);
    mRedo->setEnabled(mUndoStack->canRedo());
}

void UndoRedoButtons::textChanged()
{
    mUndo->setToolTip(mUndoStack->undoText());
    mRedo->setToolTip(mUndoStack->redoText());
}
