#ifndef BUILDINGKEYVALUESDIALOG_H
#define BUILDINGKEYVALUESDIALOG_H

#include "propertiesmodel.h"

#include <QDialog>

namespace Ui {
class BuildingKeyValuesDialog;
}

namespace BuildingEditor
{
class BuildingDocument;

class BuildingKeyValuesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BuildingKeyValuesDialog(BuildingDocument *doc, QWidget *parent = nullptr);
    ~BuildingKeyValuesDialog();

public slots:
    void accept() override;

private:
    Ui::BuildingKeyValuesDialog *ui;
    BuildingDocument *mDocument;
    Tiled::Internal::PropertiesModel *mModel;
};

}

#endif // BUILDINGKEYVALUESDIALOG_H
