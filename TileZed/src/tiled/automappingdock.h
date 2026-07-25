/*
 * Copyright 2026, Unjammer
 *
 * This file is part of TileZed.
 */

#ifndef AUTOMAPPINGDOCK_H
#define AUTOMAPPINGDOCK_H

#include <QDockWidget>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTextEdit;

namespace Tiled {
namespace Internal {

class AutomappingDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AutomappingDock(QWidget *parent = nullptr);

private slots:
    void refresh();
    void showRuleDetails(int row);
    void reloadRules();
    void applyRules();
    void interactiveChanged(bool enabled);

private:
    QLabel *mStatusLabel;
    QLabel *mRulesPathLabel;
    QListWidget *mRulesList;
    QTextEdit *mDetails;
    QCheckBox *mInteractiveCheckBox;
    QPushButton *mReloadButton;
    QPushButton *mApplyButton;
};

} // namespace Internal
} // namespace Tiled

#endif // AUTOMAPPINGDOCK_H
