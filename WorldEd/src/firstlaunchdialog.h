#ifndef FIRSTLAUNCHDIALOG_H
#define FIRSTLAUNCHDIALOG_H

#include <QDialog>

class QLineEdit;
class QPushButton;

class FirstLaunchDialog : public QDialog
{
public:
    explicit FirstLaunchDialog(QWidget *parent = nullptr);

    static bool ensureSharedPaths(QWidget *parent = nullptr);
    static bool configureSharedPaths(QWidget *parent = nullptr);

private:
    void browseConfiguration();
    void browseTiles();
    void updateAcceptButton();
    void saveAndAccept();

    QLineEdit *mConfigurationPath;
    QLineEdit *mTilesPath;
    QPushButton *mAcceptButton;
};

#endif // FIRSTLAUNCHDIALOG_H
