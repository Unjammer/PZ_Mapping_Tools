/*
 * Copyright 2026, PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef LOOTDISTRIBUTIONDIALOG_H
#define LOOTDISTRIBUTIONDIALOG_H

#include <QDialog>
#include <QScopedPointer>
#include <QString>

namespace Tiled {
namespace Internal {

class LootDistributionDialogPrivate;

/**
 * Visual editor for the Build 42 room/container and procedural loot
 * registries.
 *
 * Game files are loaded as read-only reference data. Edits are written below
 * a separate project/mod root and compiled to a post-merge Lua override.
 */
class LootDistributionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LootDistributionDialog(
            QWidget *parent = nullptr,
            const QString &initialRoom = QString(),
            const QString &initialContainer = QString(),
            const QString &suggestedProjectRoot = QString());
    ~LootDistributionDialog() override;

    static bool validateDefinitions(const QString &gamePath,
                                    const QString &projectRoot,
                                    QString *summary,
                                    QString *error);
    static bool renderValidation(const QString &gamePath,
                                 const QString &projectRoot,
                                 const QString &outputFile,
                                 QString *error);

private:
    QScopedPointer<LootDistributionDialogPrivate> d;
};

} // namespace Internal
} // namespace Tiled

#endif // LOOTDISTRIBUTIONDIALOG_H
