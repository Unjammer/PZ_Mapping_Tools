#ifndef TOOLWINDOWCLOSE_H
#define TOOLWINDOWCLOSE_H

#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QPushButton>
#include <QShortcut>

inline QDialogButtonBox *toolWindowCloseButtons(QWidget *window)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setObjectName(QStringLiteral("closeButton"));
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), window);
    escape->setAutoRepeat(false);
    QObject::connect(buttons, &QDialogButtonBox::rejected, window, &QWidget::close);
    QObject::connect(escape, &QShortcut::activated, window, &QWidget::close);
    return buttons;
}

inline void addToolWindowCloseButton(QWidget *window, QBoxLayout *layout)
{
    layout->addWidget(toolWindowCloseButtons(window));
}

inline void addToolWindowCloseButton(QWidget *window, QGridLayout *layout)
{
    layout->addWidget(toolWindowCloseButtons(window),
                      layout->rowCount(), 0, 1, layout->columnCount());
}

#endif
