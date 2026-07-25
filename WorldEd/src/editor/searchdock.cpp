/*
 * Copyright 2018, Tim Baker <treectrl@users.sf.net>
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

#include "searchdock.h"
#include "ui_searchdock.h"

#include "basegraphicsview.h"
#include "celldocument.h"
#include "documentmanager.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"
#include "worldscene.h"

#include <QListWidget>
#include <QListWidgetItem>

SearchDock::SearchDock(QWidget* parent)
    : QDockWidget(parent)
    , ui(new Ui::SearchDock)
    , mSynching(false)
{
    ui->setupUi(this);

    setObjectName(QLatin1String("SearchDock"));

    ui->lineEdit->setVisible(false);

    connect(ui->combo1, QOverload<int>::of(&QComboBox::activated), this, &SearchDock::comboActivated1);
    connect(ui->combo2, QOverload<int>::of(&QComboBox::activated), this, &SearchDock::comboActivated2);
    connect(ui->lineEdit, &QLineEdit::textChanged, this, &SearchDock::textChanged);

    connect(ui->listWidget, &QListWidget::itemSelectionChanged, this, &SearchDock::listSelectionChanged);
    connect(ui->listWidget, &QListWidget::activated, this, &SearchDock::listActivated);

    connect(DocumentManager::instance(), &DocumentManager::documentAboutToClose, this, &SearchDock::documentAboutToClose);

    setEnabled(false);
}

void SearchDock::changeEvent(QEvent *e)
{
    QDockWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        retranslateUi();
        break;
    default:
        break;
    }
}

void SearchDock::retranslateUi()
{
    setWindowTitle(tr("Search"));
}

void SearchDock::setDocument(Document *doc)
{
    mWorldDoc = doc->asWorldDocument();
    mCellDoc = doc->asCellDocument();
    setEnabled(true);

    WorldDocument* worldDoc = worldDocument();
    connect(worldDoc, &WorldDocument::worldResized,
            this, &SearchDock::worldResized, Qt::UniqueConnection);
    SearchResults* results = searchResultsFor(worldDoc);
    ui->combo1->setCurrentIndex(static_cast<int>(results->searchBy));
    setCombo2(results->searchBy);
    setList(results);

    if (results->searchBy == SearchResults::SearchBy::ObjectType) {
        int index = ui->combo2->findText(results->searchStringObjectType);
        if (index == -1)
            index = 0; // Select <None>
        ui->combo2->setCurrentIndex(index);
        ui->combo2->setVisible(true);
        ui->lineEdit->setVisible(false);
    } else if (results->searchBy == SearchResults::SearchBy::LotFileName) {
        ui->lineEdit->setText(results->searchStringLotFileName);
        ui->combo2->setVisible(false);
        ui->lineEdit->setVisible(true);
    } else if (results->searchBy == SearchResults::SearchBy::PropertyDef) {
        int index = ui->combo2->findText(results->searchStringPropertyDef);
        if (index == -1)
            index = 0; // Select <None>
        ui->combo2->setCurrentIndex(index);
        ui->combo2->setVisible(true);
        ui->lineEdit->setText(results->searchStringPropertyValue);
        ui->lineEdit->setVisible(true);
    }
}

void SearchDock::clearDocument()
{
    mWorldDoc = nullptr;
    mCellDoc = nullptr;
    ui->listWidget->clear();
    ui->combo2->clear();
    ui->lineEdit->clear();
    setEnabled(false);
}

WorldDocument *SearchDock::worldDocument()
{
    if (mWorldDoc)
        return mWorldDoc;
    if (mCellDoc)
        return mCellDoc->worldDocument();
    return nullptr;
}

void SearchDock::comboActivated1(int index)
{
    ui->combo2->clear();

    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;

    SearchResults *results = searchResultsFor(worldDoc);

    if (index == static_cast<int>(SearchResults::SearchBy::ObjectType)) {
        setCombo2(SearchResults::SearchBy::ObjectType);
        int index1 = ui->combo2->findText(results->searchStringObjectType);
        if (index1 == -1)
            index1 = 0; // Select <None>
        ui->combo2->setCurrentIndex(index1);
        ui->combo2->setVisible(true);
        ui->lineEdit->setVisible(false);
        searchObjectType();
    }
    if (index == static_cast<int>(SearchResults::SearchBy::LotFileName)) {
        ui->lineEdit->setText(results->searchStringLotFileName);
        ui->combo2->setVisible(false);
        ui->lineEdit->setVisible(true);
        searchLotFileName();
    }
    if (index == static_cast<int>(SearchResults::SearchBy::PropertyDef)) {
        setCombo2(SearchResults::SearchBy::PropertyDef);
        int index1 = ui->combo2->findText(results->searchStringPropertyDef);
        if (index1 == -1)
            index1 = 0;
        ui->combo2->setCurrentIndex(index1);
        ui->combo2->setVisible(true);
        ui->lineEdit->setText(results->searchStringPropertyValue);
        ui->lineEdit->setVisible(true);
        searchPropertyDef();
    }
}

void SearchDock::comboActivated2(int index)
{
    Q_UNUSED(index);

    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;

    if (ui->combo1->currentIndex() == static_cast<int>(SearchResults::SearchBy::ObjectType)) {
        searchObjectType();
    }
    if (ui->combo1->currentIndex() == static_cast<int>(SearchResults::SearchBy::PropertyDef)) {
        searchPropertyDef();
    }
}

void SearchDock::textChanged()
{
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;

    if (ui->combo1->currentIndex() == static_cast<int>(SearchResults::SearchBy::LotFileName)) {
        searchLotFileName();
    }
    if (ui->combo1->currentIndex() == static_cast<int>(SearchResults::SearchBy::PropertyDef)) {
        searchPropertyDef();
    }
}

void SearchDock::searchObjectType()
{
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    ObjectType* objType = ui->combo2->currentData().value<ObjectType*>();

    SearchResults* results = searchResultsFor(worldDoc);
    results->reset();
    results->searchBy = SearchResults::SearchBy::ObjectType;
    results->searchStringObjectType = objType->name();

    for (int y = 0; y < world->height(); y++) {
        for (int x = 0; x < world->width(); x++) {
            WorldCell* cell = world->cellAt(x, y);
            if (cell == nullptr)
                continue;
            for (WorldCellObject* obj : cell->objects()) {
                if (obj->type() == objType) {
                    results->cells += QPoint(cell->x(), cell->y());
                    break;
                }
            }
        }
    }

    setList(results);
}

void SearchDock::searchLotFileName()
{
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    SearchResults* results = searchResultsFor(worldDoc);
    results->reset();
    results->searchBy = SearchResults::SearchBy::LotFileName;
    results->searchStringLotFileName = ui->lineEdit->text();

    for (int y = 0; y < world->height(); y++) {
        for (int x = 0; x < world->width(); x++) {
            WorldCell* cell = world->cellAt(x, y);
            if (cell == nullptr)
                continue;
            for (WorldCellLot* lot : cell->lots()) {
                if (results->searchStringLotFileName.trimmed().isEmpty()) {
                    if (lot->mapName().isEmpty() == false) {
                        continue;
                    }
                } else if (lot->mapName().contains(results->searchStringLotFileName, Qt::CaseInsensitive) == false) {
                    continue;
                }
                results->cells += QPoint(cell->x(), cell->y());
                break;
            }
        }
    }

    setList(results);
}

void SearchDock::searchPropertyDef()
{
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    PropertyDef* propertyDef = ui->combo2->currentData().value<PropertyDef*>();

    SearchResults* results = searchResultsFor(worldDoc);
    results->reset();
    results->searchBy = SearchResults::SearchBy::PropertyDef;
    results->searchStringPropertyDef = propertyDef->mName;
    results->searchStringPropertyValue = ui->lineEdit->text();

    for (int y = 0; y < world->height(); y++) {
        for (int x = 0; x < world->width(); x++) {
            WorldCell* cell = world->cellAt(x, y);
            if (cell == nullptr) {
                continue;
            }
            for (WorldCellObject* obj : cell->objects()) {
                Property *property = obj->properties().find(propertyDef);
                if (property == nullptr) {
                    continue;
                }
                if (results->searchStringPropertyValue.trimmed().isEmpty()) {
                    if (property->mValue.isEmpty() == false) {
                        continue;
                    }
                } else if (property->mValue.contains(results->searchStringPropertyValue, Qt::CaseInsensitive) == false) {
                    continue;
                }
                results->cells += QPoint(cell->x(), cell->y());
                break;
            }
        }
    }

    setList(results);
}

void SearchDock::setList(SearchResults *results)
{
    mSynching = true;
    ui->listWidget->clear();
    mSynching = false;

    if (results == nullptr)
        return;

    QListWidgetItem* selectedItem = nullptr;
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;

    for (const QPoint &cellPos : results->cells) {
        WorldCell *cell = worldDoc->world()->cellAt(cellPos.x(), cellPos.y());
        if (cell == nullptr)
            continue;
        QListWidgetItem* item = new QListWidgetItem(
                    tr("Cell %1,%2").arg(cellPos.x()).arg(cellPos.y()));
        item->setData(Qt::UserRole, cellPos);
        ui->listWidget->addItem(item);

        if (cellPos == results->selectedCell)
            selectedItem = item;
    }

    if (selectedItem != nullptr) {
        mSynching = true;
        selectedItem->setSelected(true);
        mSynching = false;
        ui->listWidget->scrollToItem(selectedItem);
    }
}

void SearchDock::listSelectionChanged()
{
    if (mSynching)
        return;

    WorldDocument* worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;

    SearchResults* results = searchResultsFor(worldDoc, false);
    if (results == nullptr)
        return;

    QList<QListWidgetItem*> selected = ui->listWidget->selectedItems();
    if (selected.size() != 1) {
        results->selectedCell = QPoint(-1, -1);
        return;
    }

    QListWidgetItem* item = selected.first();
    const QPoint cellPos = item->data(Qt::UserRole).toPoint();
    WorldCell* cell = worldDoc->world()->cellAt(cellPos.x(), cellPos.y());
    if (cell == nullptr) {
        results->selectedCell = QPoint(-1, -1);
        return;
    }
    results->selectedCell = cellPos;

    WorldScene* scene = worldDoc->view()->scene()->asWorldScene();
    worldDoc->view()->centerOn(scene->cellToPixelCoords(cell->x() + 0.5f, cell->y() + 0.5f));
    worldDoc->setSelectedCells(QList<WorldCell*>() << cell);
}

void SearchDock::listActivated(const QModelIndex &index)
{
    QListWidgetItem* item = ui->listWidget->item(index.row());
    WorldDocument* worldDoc = mWorldDoc ? mWorldDoc : mCellDoc->worldDocument();
    const QPoint cellPos = item->data(Qt::UserRole).toPoint();
    WorldCell* cell = worldDoc->world()->cellAt(cellPos.x(), cellPos.y());
    if (cell == nullptr)
        return;
    worldDoc->setSelectedCells(QList<WorldCell*>() << cell);
    worldDoc->editCell(cell);
}

void SearchDock::worldResized()
{
    WorldDocument *worldDoc = qobject_cast<WorldDocument *>(sender());
    SearchResults *results = searchResultsFor(worldDoc, false);
    if (results == nullptr)
        return;

    results->reset();
    if (worldDoc == worldDocument())
        setList(results);
}

void SearchDock::documentAboutToClose(int index, Document *doc)
{
    Q_UNUSED(index)
    WorldDocument* worldDoc = doc->asWorldDocument() ? doc->asWorldDocument() : doc->asCellDocument()->worldDocument();
    if (mResults.contains(worldDoc)) {
        SearchResults* results = mResults[worldDoc];
        mResults.remove(worldDoc);
        delete results;
    }
}

void SearchDock::setCombo2(SearchResults::SearchBy searchBy)
{
    ui->combo2->clear();

    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    if (searchBy == SearchResults::SearchBy::ObjectType) {
        for (ObjectType* objType : world->objectTypes()) {
            if (objType->isNull())
                ui->combo2->addItem(QLatin1String("<None>"), QVariant::fromValue(objType));
            else
                ui->combo2->addItem(objType->name(), QVariant::fromValue(objType));
        }
    }
    if (searchBy == SearchResults::SearchBy::PropertyDef) {
        for (PropertyDef *propertyDef : world->propertyDefinitions()) {
            ui->combo2->addItem(propertyDef->mName, QVariant::fromValue(propertyDef));
        }
    }
}

SearchResults *SearchDock::searchResultsFor(WorldDocument *worldDoc, bool create)
{
    if (worldDoc == nullptr)
        return nullptr;
    if (mResults.contains(worldDoc))
        return mResults[worldDoc];
    if (!create)
        return nullptr;
    return mResults[worldDoc] = new SearchResults();
}
