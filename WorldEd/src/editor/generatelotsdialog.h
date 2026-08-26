#ifndef GENERATELOTSDIALOG_H
#define GENERATELOTSDIALOG_H

#include <QDialog>

class QComboBox;

namespace Ui {
class GenerateLotsDialog;
}

class WorldDocument;

class GenerateLotsDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit GenerateLotsDialog(WorldDocument *worldDoc, QWidget *parent = 0);
    ~GenerateLotsDialog();

    bool exportAsMod() const;
    void setExportAsMod(bool enabled);
    bool finalizeModExport(QString *error) const;
    void setModExportAvailable(bool available);
    bool fillHolesDuringExport() const;
    bool fillHolesWithNearestTile() const;
    QString holeFillTileName() const;
    static bool validatePathSelection(QString *error);

private slots:
    void exportBrowse();
    void exportChanged(const QString &text);
    void spawnBrowse();
    void spawnChanged(const QString &text);
    void tileDefBrowse();
    void tileDefChanged(const QString &text);
    void modRootBrowse();
    void posterBrowse();
    void modExportToggled(bool enabled);
    void accept();
    void apply();

private:
    static void selectComboItem(QComboBox *comboBox, const QString &text);
    QStringList comboboxStringList(QComboBox *comboBox) const;
    bool validate();
    bool prepareModExport(QString *error);
    QString modDirectory() const;
    QString modMapDirectory() const;

private:
    Ui::GenerateLotsDialog *ui;
    WorldDocument *mWorldDoc;
    QString mExportDir;
    QString mZombieSpawnMap;
    QString mTileDefFolder;
};

#endif // GENERATELOTSDIALOG_H
