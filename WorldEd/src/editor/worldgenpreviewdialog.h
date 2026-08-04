/*
 * Copyright 2026 Alree / Unjammer
 *
 * This file is part of PZTools Unofficial.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef WORLDGENPREVIEWDIALOG_H
#define WORLDGENPREVIEWDIALOG_H

#include <QDialog>
#include <QScopedPointer>

class WorldGenPreviewDialogPrivate;
class WorldDocument;

class WorldGenPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WorldGenPreviewDialog(WorldDocument *worldDocument,
                                   QWidget *parent = nullptr);
    ~WorldGenPreviewDialog() override;

    static bool validateDefinitions(const QString &path,
                                    QString *summary,
                                    QString *error);
    static bool validateProjectOverlay(const QString &gamePath,
                                       const QString &projectPath,
                                       QString *summary,
                                       QString *error);
    static bool renderValidationPreview(const QString &path,
                                        const QString &outputFile,
                                        QString *error);
    static bool renderValidationPrefabEditor(const QString &path,
                                             const QString &outputFile,
                                             QString *error);
    static bool validatePrefabImport(const QString &fileName,
                                     QString *summary,
                                     QString *error);

private:
    QScopedPointer<WorldGenPreviewDialogPrivate> d;
};

class WorldGenPrefabDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WorldGenPrefabDialog(WorldDocument *worldDocument,
                                  QWidget *parent = nullptr);
    ~WorldGenPrefabDialog() override;

    static bool renderValidationWindow(const QString &path,
                                       const QString &outputFile,
                                       QString *error);

private:
    QScopedPointer<WorldGenPreviewDialogPrivate> d;
};

#endif // WORLDGENPREVIEWDIALOG_H
