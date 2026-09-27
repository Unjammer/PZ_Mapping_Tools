/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "choosebuildingtiledialog.h"
#include "ui_choosebuildingtiledialog.h"

#include "buildingeditorwindow.h"
#include "buildingpreferences.h"
#include "buildingtiles.h"
#include "buildingtilesdialog.h"

#include "zoomable.h"

#include <QSettings>

using namespace BuildingEditor;
using namespace Tiled::Internal;

ChooseBuildingTileDialog::ChooseBuildingTileDialog(const QString &prompt,
                                                   BuildingTileCategory *category,
                                                   BuildingTileEntry *initialTile,
                                                   QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ChooseBuildingTileDialog),
    mCategory(category),
    mZoomable(new Zoomable(this))
{
    ui->setupUi(this);

    ui->prompt->setText(prompt);

    ui->tableView->setZoomable(mZoomable);
    mZoomable->connectToComboBox(ui->scaleCombo);

    connect(ui->tilesButton, &QAbstractButton::clicked, this, &ChooseBuildingTileDialog::tilesDialog);

    setTilesList(mCategory, initialTile);

    connect(ui->tableView, &QAbstractItemView::activated, this, &ChooseBuildingTileDialog::accept);

    QSettings &settings = BuildingPreferences::instance()->settings();
    settings.beginGroup(QLatin1String("ChooseBuildingTileDialog"));
    QByteArray geom = settings.value(QLatin1String("geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    qreal scale = settings.value(QLatin1String("scale"),
                                 BuildingPreferences::instance()->tileScale()).toReal();
    mZoomable->setScale(scale);
    settings.endGroup();
}

ChooseBuildingTileDialog::~ChooseBuildingTileDialog()
{
    delete ui;
}

BuildingTileEntry *ChooseBuildingTileDialog::selectedTile() const
{
    QModelIndexList selection = ui->tableView->selectionModel()->selectedIndexes();
    if (selection.count()) {
        QModelIndex index = selection.first();
        return ui->tableView->entry(index);
    }
    return 0;
}

void ChooseBuildingTileDialog::setTilesList(BuildingTileCategory *category,
                                            BuildingTileEntry *initialTile)
{
    QList<BuildingTileEntry*> entries;

    if (category->canAssignNone())
        entries += BuildingTilesMgr::instance()->noneTileEntry();

    QMap<QString,BuildingTileEntry*> entryMap;
    int i = 0;
    foreach (BuildingTileEntry *entry, category->entries()) {
        QString key = entry->displayTile()->name() + QString::number(i++);
        entryMap[key] = entry;
    }
    entries += entryMap.values();

    ui->tableView->setEntries(entries);

    if (entries.contains(initialTile))
        ui->tableView->setCurrentIndex(ui->tableView->index(initialTile));
}

void ChooseBuildingTileDialog::saveSettings()
{
    QSettings &settings = BuildingPreferences::instance()->settings();
    settings.beginGroup(QLatin1String("ChooseBuildingTileDialog"));
    settings.setValue(QLatin1String("geometry"), saveGeometry());
    settings.setValue(QLatin1String("scale"), mZoomable->scale());
    settings.endGroup();
}

void ChooseBuildingTileDialog::tilesDialog()
{
    BuildingTilesDialog *dialog = BuildingTilesDialog::instance();
    dialog->selectCategory(mCategory);

    QWidget *saveParent = dialog->parentWidget();
    dialog->reparent(this);
    dialog->exec();
    dialog->reparent(saveParent);
    // BUG: The underlying window for 'dialog' is destroyed behind our back, without deleting the Qt object.
    // External WM_DESTROY received for  QWidgetWindow(0x28c00e56f90, name="BuildingTilesDialogWindow") , parent:  QWindow(0x0) , transient parent:  QWidgetWindow(0x28c27cb2f60, name="BuildingEditorWindowWindow")
    dialog->deleteInstance();

    if (dialog->changes()) {
        setTilesList(mCategory);
    }
}

void ChooseBuildingTileDialog::accept()
{
    saveSettings();
    QDialog::accept();
}

void ChooseBuildingTileDialog::reject()
{
    saveSettings();
    QDialog::reject();
}
