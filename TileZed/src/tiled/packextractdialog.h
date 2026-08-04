#ifndef PACKEXTRACTDIALOG_H
#define PACKEXTRACTDIALOG_H

class PackFile;
class PROGRESS;

#include <QDialog>
#include <QList>
#include <QPair>

namespace Ui {
class PackExtractDialog;
}

class PackExtractDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PackExtractDialog(PackFile &packFile,
                               QWidget *parent = nullptr,
                               PROGRESS *initializationProgress = nullptr);
    ~PackExtractDialog();

    static bool runSelfTest(QString *summary, QString *errorString);
    static bool renderValidation(const QString &outputFile,
                                 QString *errorString);

    bool initializationCanceled() const
    { return mInitializationCanceled; }

public slots:
    void browse();
    void accept() override;
    void filterChanged();
    void selectAllVisible();
    void clearVisible();
    void modeChanged();

private:
    void populateTextures(PROGRESS *progress);
    bool rowMatches(int row, QString *regularExpressionError) const;
    QList<QPair<int, int>> checkedVisibleTextures() const;
    void saveSettings();

    Ui::PackExtractDialog *ui;
    PackFile &mPackFile;
    bool mInitializationCanceled = false;
};

#endif // PACKEXTRACTDIALOG_H
