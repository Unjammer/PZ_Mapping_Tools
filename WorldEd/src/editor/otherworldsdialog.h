#ifndef OTHERWORLDSDIALOG_H
#define OTHERWORLDSDIALOG_H

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;
class QTableWidget;
class WorldDocument;

class OtherWorldsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OtherWorldsDialog(WorldDocument *worldDoc,
                               QWidget *parent = nullptr);

private slots:
    void addProject();
    void replaceProject();
    void removeProject();
    void moveProjectUp();
    void moveProjectDown();
    void refreshProjects();
    void updateSelection();
    void applyChanges();
    void accept() override;

private:
    QString normalizedPath(const QString &path) const;
    QString pathIdentity(const QString &path) const;
    bool validateCandidate(const QString &path, int replacedRow,
                           QString *error) const;
    bool allProjectsValid() const;
    void updateTable();
    void selectRow(int row);

    WorldDocument *mWorldDoc;
    QStringList mPaths;
    QStringList mAppliedPaths;
    QTableWidget *mTable;
    QLabel *mSummaryLabel;
    QLabel *mDetailsLabel;
    QPushButton *mReplaceButton;
    QPushButton *mRemoveButton;
    QPushButton *mMoveUpButton;
    QPushButton *mMoveDownButton;
    QPushButton *mApplyButton;
    QPushButton *mOkButton;
};

#endif
