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
#include "worldobjectvalidation.h"
#include "worldscene.h"

#include <QAbstractListModel>
#include <QItemSelectionModel>
#include <QScopedValueRollback>
#include <QSignalBlocker>

class SearchResultsModel : public QAbstractListModel
{
public:
    explicit SearchResultsModel(QObject *parent) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : mCells.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= mCells.size())
            return QVariant();
        const QPoint cell = mCells.at(index.row());
        if (role == Qt::DisplayRole)
            return SearchDock::tr("Cell %1,%2").arg(cell.x()).arg(cell.y());
        if (role == Qt::UserRole)
            return cell;
        return QVariant();
    }

    void setCells(const QList<QPoint> &cells)
    {
        if (mCells == cells)
            return;
        beginResetModel();
        mCells = cells;
        endResetModel();
    }

private:
    QList<QPoint> mCells;
};

SearchDock::SearchDock(QWidget* parent)
    : QDockWidget(parent)
    , ui(new Ui::SearchDock)
    , mModel(new SearchResultsModel(this))
    , mSynching(false)
{
    ui->setupUi(this);
    ui->listView->setModel(mModel);
    mSearchDelay.setObjectName(QStringLiteral("SearchDelay"));
    mSearchDelay.setParent(this);
    mSearchDelay.setSingleShot(true);

    setObjectName(QLatin1String("SearchDock"));

    ui->lineEdit->setVisible(false);

    connect(ui->combo1, QOverload<int>::of(&QComboBox::activated), this, &SearchDock::comboActivated1);
    connect(ui->combo2, QOverload<int>::of(&QComboBox::activated), this, &SearchDock::comboActivated2);
    connect(ui->lineEdit, &QLineEdit::textChanged, this, &SearchDock::textChanged);
    connect(ui->lineEdit, &QLineEdit::returnPressed, this, &SearchDock::runTextSearch);
    connect(&mSearchDelay, &QTimer::timeout, this, &SearchDock::runTextSearch);

    connect(ui->listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SearchDock::listSelectionChanged);
    connect(ui->listView, &QListView::activated, this, &SearchDock::listActivated);

    connect(DocumentManager::instance(), &DocumentManager::documentAboutToClose, this, &SearchDock::documentAboutToClose);

    setEnabled(false);
}

SearchDock::~SearchDock()
{
    qDeleteAll(mResults);
    delete ui;
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
    mSearchDelay.stop();
    if (!doc) {
        clearDocument();
        return;
    }
    const QSignalBlocker blockText(ui->lineEdit);
    mWorldDoc = doc->asWorldDocument();
    mCellDoc = doc->asCellDocument();
    WorldDocument *worldDoc = worldDocument();
    if (!worldDoc) {
        clearDocument();
        return;
    }
    setEnabled(true);
    connect(worldDoc, &WorldDocument::worldResized,
            this, &SearchDock::worldResized, Qt::UniqueConnection);
    SearchResults *results = searchResultsFor(worldDoc);
    ui->combo1->setCurrentIndex(static_cast<int>(results->searchBy));
    setCombo2(results->searchBy);
    setList(results);
    if (results->searchBy == SearchResults::SearchBy::ObjectType) {
        ui->combo2->setCurrentIndex(qMax(0, ui->combo2->findText(results->searchStringObjectType)));
        ui->combo2->setVisible(true);
        ui->lineEdit->setVisible(false);
    } else if (results->searchBy == SearchResults::SearchBy::LotFileName) {
        ui->lineEdit->setText(results->searchStringLotFileName);
        ui->combo2->setVisible(false);
        ui->lineEdit->setVisible(true);
    } else {
        ui->combo2->setCurrentIndex(qMax(0, ui->combo2->findText(results->searchStringPropertyDef)));
        ui->combo2->setVisible(true);
        ui->lineEdit->setText(results->searchStringPropertyValue);
        ui->lineEdit->setVisible(true);
    }
    if (results->pending)
        mSearchDelay.start(0);
}

void SearchDock::clearDocument()
{
    mSearchDelay.stop();
    const QSignalBlocker blockText(ui->lineEdit);
    mWorldDoc = nullptr;
    mCellDoc = nullptr;
    setList(nullptr);
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
    mSearchDelay.stop();
    const QSignalBlocker blockText(ui->lineEdit);
    ui->combo2->clear();

    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;

    SearchResults *results = searchResultsFor(worldDoc);

    if (index == static_cast<int>(SearchResults::SearchBy::ObjectType)) {
        setCombo2(SearchResults::SearchBy::ObjectType);
        int index1 = ui->combo2->findText(results->searchStringObjectType);
        if (index1 == -1)
            index1 = 0;
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
    if (!worldDoc)
        return;
    SearchResults *results = searchResultsFor(worldDoc);
    const int mode = ui->combo1->currentIndex();
    if (mode == static_cast<int>(SearchResults::SearchBy::LotFileName))
        results->searchStringLotFileName = ui->lineEdit->text();
    else if (mode == static_cast<int>(SearchResults::SearchBy::PropertyDef))
        results->searchStringPropertyValue = ui->lineEdit->text();
    else
        return;
    results->pending = true;
    ui->listView->setEnabled(false);
    const World *world = worldDoc->world();
    mSearchDelay.start(qint64(world->width()) * world->height() > 256 ? 150 : 0);
}

void SearchDock::runTextSearch()
{
    mSearchDelay.stop();
    if (!worldDocument())
        return;
    if (ui->combo1->currentIndex() == static_cast<int>(SearchResults::SearchBy::LotFileName))
        searchLotFileName();
    else if (ui->combo1->currentIndex() == static_cast<int>(SearchResults::SearchBy::PropertyDef))
        searchPropertyDef();
}

void SearchDock::searchObjectType()
{
    mSearchDelay.stop();
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    ObjectType* objType = ui->combo2->currentData().value<ObjectType*>();

    SearchResults* results = searchResultsFor(worldDoc);
    results->reset();
    results->searchBy = SearchResults::SearchBy::ObjectType;
    results->searchStringObjectType = objType ? objType->name() : QString();
    if (!objType) {
        setList(results);
        return;
    }

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
    mSearchDelay.stop();
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    SearchResults* results = searchResultsFor(worldDoc);
    results->reset();
    results->searchBy = SearchResults::SearchBy::LotFileName;
    results->searchStringLotFileName = ui->lineEdit->text();
    const bool emptyQuery = results->searchStringLotFileName.trimmed().isEmpty();

    for (int y = 0; y < world->height(); y++) {
        for (int x = 0; x < world->width(); x++) {
            WorldCell* cell = world->cellAt(x, y);
            if (cell == nullptr)
                continue;
            for (WorldCellLot* lot : cell->lots()) {
                if (emptyQuery) {
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
    mSearchDelay.stop();
    WorldDocument *worldDoc = worldDocument();
    if (worldDoc == nullptr)
        return;
    World* world = worldDoc->world();

    PropertyDef* propertyDef = ui->combo2->currentData().value<PropertyDef*>();

    SearchResults* results = searchResultsFor(worldDoc);
    results->reset();
    results->searchBy = SearchResults::SearchBy::PropertyDef;
    results->searchStringPropertyDef = propertyDef ? propertyDef->mName : QString();
    results->searchStringPropertyValue = ui->lineEdit->text();
    if (!propertyDef) {
        setList(results);
        return;
    }
    const bool emptyQuery = results->searchStringPropertyValue.trimmed().isEmpty();

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
                if (emptyQuery) {
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
    const QScopedValueRollback<bool> syncing(mSynching, true);
    WorldDocument *worldDoc = worldDocument();
    QList<QPoint> cells;
    int selectedRow = -1;
    if (results && worldDoc) {
        cells.reserve(results->cells.size());
        for (const QPoint &position : qAsConst(results->cells)) {
            if (!worldDoc->world()->cellAt(position.x(), position.y()))
                continue;
            if (position == results->selectedCell)
                selectedRow = cells.size();
            cells.append(position);
        }
    }
    mModel->setCells(cells);
    ui->listView->selectionModel()->clearSelection();
    if (selectedRow >= 0) {
        const QModelIndex index = mModel->index(selectedRow, 0);
        ui->listView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        ui->listView->scrollTo(index);
    }
    ui->listView->setEnabled(results && !results->pending);
}

void SearchDock::listSelectionChanged()
{
    if (mSynching)
        return;
    WorldDocument *worldDoc = worldDocument();
    if (!worldDoc)
        return;
    SearchResults *results = searchResultsFor(worldDoc, false);
    if (!results || results->pending)
        return;
    const QModelIndexList selected = ui->listView->selectionModel()->selectedIndexes();
    if (selected.size() != 1) {
        results->selectedCell = QPoint(-1, -1);
        return;
    }
    const QPoint position = selected.first().data(Qt::UserRole).toPoint();
    WorldCell *cell = worldDoc->world()->cellAt(position.x(), position.y());
    if (!cell) {
        results->selectedCell = QPoint(-1, -1);
        return;
    }
    results->selectedCell = position;
    if (worldDoc->view()) {
        WorldScene *scene = worldDoc->view()->scene()->asWorldScene();
        if (scene)
            worldDoc->view()->centerOn(scene->cellToPixelCoords(cell->x() + 0.5f, cell->y() + 0.5f));
    }
    worldDoc->setSelectedCells(QList<WorldCell *>() << cell);
}

void SearchDock::listActivated(const QModelIndex &index)
{
    WorldDocument *worldDoc = worldDocument();
    SearchResults *results = searchResultsFor(worldDoc, false);
    if (!worldDoc || !results || results->pending || !index.isValid() || index.model() != mModel)
        return;
    const QPoint position = index.data(Qt::UserRole).toPoint();
    WorldCell *cell = worldDoc->world()->cellAt(position.x(), position.y());
    if (!cell)
        return;
    worldDoc->setSelectedCells(QList<WorldCell *>() << cell);
    worldDoc->editCell(cell);
}

void SearchDock::worldResized()
{
    WorldDocument *worldDoc = qobject_cast<WorldDocument *>(sender());
    SearchResults *results = searchResultsFor(worldDoc, false);
    if (results == nullptr)
        return;

    results->reset();
    if (worldDoc == worldDocument()) {
        mSearchDelay.stop();
        setList(results);
    }
}

void SearchDock::documentAboutToClose(int index, Document *doc)
{
    Q_UNUSED(index);
    if (!doc)
        return;
    WorldDocument *worldDoc = doc->asWorldDocument();
    if (doc == mCellDoc || (worldDoc && worldDoc == worldDocument()))
        clearDocument();
    if (worldDoc)
        delete mResults.take(worldDoc);
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
        WorldObjectValidation::applyTypeTooltips(ui->combo2);
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
