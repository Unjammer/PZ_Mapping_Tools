/*
 * Copyright 2026 PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef TILESETCLEANUPDIALOG_H
#define TILESETCLEANUPDIALOG_H

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

struct TilesetCleanupOptions
{
    bool recursive = true;
    bool normalizePaths = true;
    bool removeUnresolvedTilesets = false;
};

struct TilesetCleanupResult
{
    QString fileName;
    QString type;
    int declared = 0;
    int retained = 0;
    int unused = 0;
    int validUnusedKept = 0;
    int normalized = 0;
    int missingUsed = 0;
    int unresolvedRemoved = 0;
    int affectedTileCells = 0;
    int affectedTileObjects = 0;
    int affectedRuleReferences = 0;
    int missingDependencies = 0;
    int externalDependencies = 0;
    bool formatUpdated = false;
    bool changed = false;
    bool applied = false;
    QStringList removedNames;
    QStringList missingNames;
    QStringList unresolvedRemovedNames;
    QStringList pathChanges;
    QStringList dependencyWarnings;
    QString error;
};

class TilesetCleanup
{
public:
    static QStringList filesUnder(const QString &root, bool recursive);
    static TilesetCleanupResult processFile(
            const QString &fileName,
            const QString &scanRoot,
            const TilesetCleanupOptions &options,
            bool apply,
            const QString &backupRoot = QString());
    static QString report(const QList<TilesetCleanupResult> &results,
                          const QString &backupRoot = QString());
    static bool validate(QString *summary, QString *error);
};

class TilesetCleanupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TilesetCleanupDialog(const QString &initialRoot,
                                  const QString &projectFile = QString(),
                                  QWidget *parent = nullptr);

private slots:
    void browse();
    void analyze();
    void applyCleanup();
    void updateActions();
    void updateStatus();
    void updateSummaryTable();

private:
    QList<TilesetCleanupResult> run(bool apply,
                                    const QString &backupRoot = QString());
    TilesetCleanupOptions options() const;
    int changedFileCount() const;
    QStringList projectPathWarnings() const;
    QString fullReport(const QString &backupRoot = QString()) const;

    QLineEdit *mRootEdit = nullptr;
    QCheckBox *mRecursiveCheck = nullptr;
    QCheckBox *mNormalizeCheck = nullptr;
    QCheckBox *mRemoveUnresolvedCheck = nullptr;
    QPlainTextEdit *mReport = nullptr;
    QTableWidget *mSummaryTable = nullptr;
    QLabel *mStatusTitle = nullptr;
    QLabel *mStatusDetails = nullptr;
    QLabel *mTechnicalLabel = nullptr;
    QPushButton *mAnalyzeButton = nullptr;
    QPushButton *mApplyButton = nullptr;
    QPushButton *mDetailsButton = nullptr;
    QList<TilesetCleanupResult> mResults;
    QString mProjectFile;
    QStringList mProjectWarnings;
};

#endif // TILESETCLEANUPDIALOG_H
