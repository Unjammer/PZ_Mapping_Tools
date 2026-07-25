#include "buildingkeyvaluesdialog.h"
#include "ui_buildingkeyvaluesdialog.h"

#include "building.h"
#include "buildingdocument.h"
#include "buildingundoredo.h"

using namespace BuildingEditor;

BuildingKeyValuesDialog::BuildingKeyValuesDialog(BuildingDocument *doc, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BuildingKeyValuesDialog),
    mDocument(doc)
{
    ui->setupUi(this);

    mModel = new Tiled::Internal::PropertiesModel(this);
    mModel->setProperties(doc->building()->properties());
    ui->propertiesView->setModel(mModel);
}

BuildingKeyValuesDialog::~BuildingKeyValuesDialog()
{
    delete ui;
}

void BuildingKeyValuesDialog::accept()
{
    // On OSX, the accept button doesn't receive focus when clicked, and
    // taking the focus off the currently-being-edited field is what causes
    // it to commit its data to the model. This work around makes sure that
    // the data being edited gets saved when the user clicks OK.
    ui->propertiesView->setFocus();

    const Tiled::Properties &properties = mModel->properties();
    if (mDocument->building()->properties() != properties) {
        mDocument->undoStack()->push(new ChangeBuildingKeyValues(mDocument, properties));
    }
    QDialog::accept();
}
