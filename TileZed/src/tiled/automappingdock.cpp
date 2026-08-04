/*
 * Copyright 2026, Unjammer
 *
 * This file is part of TileZed.
 */

#include "automappingdock.h"

#include "automappingmanager.h"
#include "preferences.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QVBoxLayout>

using namespace Tiled;
using namespace Tiled::Internal;

AutomappingDock::AutomappingDock(QWidget *parent)
    : QDockWidget(parent)
    , mStatusLabel(new QLabel)
    , mRulesPathLabel(new QLabel)
    , mRulesList(new QListWidget)
    , mDetails(new QTextEdit)
    , mInteractiveCheckBox(new QCheckBox(tr("Apply while drawing or editing objects")))
    , mReloadButton(new QPushButton(tr("Load / Reload")))
    , mApplyButton(new QPushButton(tr("Apply to Entire Map")))
{
    setObjectName(QLatin1String("AutomappingDock"));
    setWindowTitle(tr("Automapper"));

    QWidget *contents = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(contents);
    layout->setContentsMargins(6, 6, 6, 6);

    mStatusLabel->setWordWrap(true);
    mRulesPathLabel->setWordWrap(true);
    mRulesPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mRulesList->setAlternatingRowColors(true);
    mDetails->setReadOnly(true);
    mDetails->setAcceptRichText(false);
    mDetails->setMinimumHeight(130);

    QLabel *objectHint = new QLabel(tr(
        "Tile layers and object layers are supported. Object rules can target "
        "Zones, RoomDefs and their custom properties."));
    objectHint->setWordWrap(true);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(mReloadButton);
    buttons->addWidget(mApplyButton);

    layout->addWidget(mStatusLabel);
    layout->addWidget(mRulesPathLabel);
    layout->addWidget(mRulesList, 1);
    layout->addWidget(mDetails);
    layout->addWidget(objectHint);
    layout->addWidget(mInteractiveCheckBox);
    layout->addLayout(buttons);
    setWidget(contents);

    AutomappingManager *manager = AutomappingManager::instance();
    connect(manager, &AutomappingManager::rulesChanged,
            this, &AutomappingDock::refresh);
    connect(mRulesList, &QListWidget::currentRowChanged,
            this, &AutomappingDock::showRuleDetails);
    connect(mReloadButton, &QPushButton::clicked,
            this, &AutomappingDock::reloadRules);
    connect(mApplyButton, &QPushButton::clicked,
            this, &AutomappingDock::applyRules);
    connect(mInteractiveCheckBox, &QCheckBox::toggled,
            this, &AutomappingDock::interactiveChanged);

    refresh();
}

void AutomappingDock::refresh()
{
    AutomappingManager *manager = AutomappingManager::instance();
    const QString rulesPath = manager->rulesFilePath();
    const QString worldEdRulesPath =
            manager->worldEdRulesFilePath();
    const QVector<AutomappingRuleInfo> rules = manager->ruleInfos();

    mRulesPathLabel->setText(rulesPath.isEmpty()
            ? tr("Automapper manifest: save or open a TMX first")
            : tr("Automapper manifest: %1")
              .arg(QDir::toNativeSeparators(rulesPath)));
    mRulesPathLabel->setToolTip(QString());

    if (!manager->hasMapDocument()) {
        mStatusLabel->setText(tr("No map is open."));
    } else if (!worldEdRulesPath.isEmpty()
               && !QFileInfo::exists(rulesPath)) {
        mStatusLabel->setText(
                    tr("The nearby Rules.txt is a WorldEd terrain/BMP "
                       "definition and is intentionally ignored. Create "
                       "automapping-rules.txt beside this TMX to use "
                       "Automapper."));
        mRulesPathLabel->setToolTip(
                    tr("Ignored WorldEd file: %1")
                    .arg(QDir::toNativeSeparators(
                             worldEdRulesPath)));
    } else if (!manager->isLoaded()) {
        mStatusLabel->setText(QFileInfo::exists(rulesPath)
                ? tr("An Automapper manifest is available but not loaded.")
                : tr("No automapping-rules.txt or compatible legacy "
                     "rules.txt exists beside this TMX."));
    } else {
        int patternCount = 0;
        for (const AutomappingRuleInfo &rule : rules)
            patternCount += rule.patternCount;
        mStatusLabel->setText(tr("Loaded %1 rule maps containing %2 patterns.")
                              .arg(rules.size()).arg(patternCount));
    }

    const int previousRow = mRulesList->currentRow();
    mRulesList->clear();
    for (const AutomappingRuleInfo &rule : rules) {
        QListWidgetItem *item = new QListWidgetItem(
                    QFileInfo(rule.filePath).fileName(), mRulesList);
        item->setToolTip(QDir::toNativeSeparators(rule.filePath));
    }
    if (!rules.isEmpty())
        mRulesList->setCurrentRow(qBound(0, previousRow, rules.size() - 1));
    else
        mDetails->clear();

    {
        const QSignalBlocker blocker(mInteractiveCheckBox);
        mInteractiveCheckBox->setChecked(
                    Preferences::instance()->automappingDrawing());
    }
    mInteractiveCheckBox->setEnabled(
                manager->hasMapDocument()
                && QFileInfo::exists(rulesPath)
                && worldEdRulesPath.isEmpty());
    mReloadButton->setEnabled(manager->hasMapDocument() && !rulesPath.isEmpty());
    mApplyButton->setEnabled(manager->hasMapDocument());
}

void AutomappingDock::showRuleDetails(int row)
{
    const QVector<AutomappingRuleInfo> rules =
            AutomappingManager::instance()->ruleInfos();
    if (row < 0 || row >= rules.size()) {
        mDetails->clear();
        return;
    }

    const AutomappingRuleInfo &rule = rules.at(row);
    QStringList details;
    details << tr("File: %1").arg(QDir::toNativeSeparators(rule.filePath));
    details << tr("Patterns: %1").arg(rule.patternCount);
    details << tr("Input layers: %1").arg(rule.inputLayers.isEmpty()
                 ? tr("none") : rule.inputLayers.join(QLatin1String(", ")));
    details << tr("Output layers: %1").arg(rule.outputLayers.isEmpty()
                 ? tr("none") : rule.outputLayers.join(QLatin1String(", ")));
    details << tr("Delete tiles/objects first: %1")
               .arg(rule.deleteTiles ? tr("yes") : tr("no"));
    details << tr("Interactive radius: %1").arg(rule.radius);
    details << tr("Prevent overlapping outputs: %1")
               .arg(rule.noOverlappingRules ? tr("yes") : tr("no"));
    mDetails->setPlainText(details.join(QLatin1Char('\n')));
}

void AutomappingDock::reloadRules()
{
    AutomappingManager::instance()->reloadRules();
}

void AutomappingDock::applyRules()
{
    AutomappingManager::instance()->autoMap();
}

void AutomappingDock::interactiveChanged(bool enabled)
{
    Preferences::instance()->setAutomappingDrawing(enabled);
}
