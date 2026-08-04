/*
 * Copyright 2022, Tim Baker <treectrl@users.sf.net>
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

#include "snoweditor.h"
#include "ui_snoweditor.h"

#include "tiledeffile.h"
#include "tiledeftextfile.h"
#include "tilemetainfodialog.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zoomable.h"
#include "zprogress.h"

#include "tile.h"
#include "tileset.h"

#include "BuildingEditor/buildingtiles.h"

#include <QDebug>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>

using namespace Tiled;
using namespace Internal;
using namespace BuildingEditor;

SnowEditor::SnowEditor(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SnowEditor),
    mZoomable(new Zoomable(this))
{
    ui->setupUi(this);
    resize(qMax(width(), 1100), qMax(height(), 680));

    connect(ui->actionOpen, &QAction::triggered, this, qOverload<>(&SnowEditor::fileOpen));
    connect(ui->actionSave, &QAction::triggered, this, qOverload<>(&SnowEditor::fileSave));
    connect(ui->actionReset, &QAction::triggered, this, &SnowEditor::clearProperties);
    connect(ui->actionClose, &QAction::triggered, this, &QWidget::close);

    ui->propertyName->setClearButtonEnabled(true);
    ui->propertyName->setEnabled(false);
    connect(ui->propertyName, &QLineEdit::textEdited, this,
            &SnowEditor::propertyNameEdited);

    ui->horizontalLayout->addWidget(
                new QLabel(tr("Preset:"), this));
    mPropertyPreset = new QComboBox(this);
    mPropertyPreset->addItem(tr("Snow replacement"),
                             QStringLiteral("SnowTile"));
    mPropertyPreset->addItem(tr("Burnt replacement"),
                             QStringLiteral("BurntTile"));
    mPropertyPreset->addItem(tr("Custom property"), QString());
    ui->horizontalLayout->addWidget(mPropertyPreset);
    connect(mPropertyPreset,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SnowEditor::propertyPresetChanged);

    mAssignSelectedButton = new QPushButton(
                tr("Assign source to selected targets"), this);
    mAssignSelectedButton->setToolTip(
                tr("Assign the currently selected source tile to every "
                   "selected target tile."));
    ui->horizontalLayout->addWidget(mAssignSelectedButton);
    connect(mAssignSelectedButton, &QAbstractButton::clicked,
            this, &SnowEditor::assignSourceToSelected);

    mAssignMatchingButton = new QPushButton(
                tr("Match selected by tile ID"), this);
    mAssignMatchingButton->setToolTip(
                tr("For every selected target, use the tile with the same "
                   "numeric ID from the selected source tileset."));
    ui->horizontalLayout->addWidget(mAssignMatchingButton);
    connect(mAssignMatchingButton, &QAbstractButton::clicked,
            this, &SnowEditor::assignMatchingIds);

    QSettings snowSettings;
    mPropertyName = snowSettings.value(
                QStringLiteral("SnowEditor/PropertyName"),
                QStringLiteral("SnowTile")).toString().trimmed();
    if (mPropertyName.isEmpty())
        mPropertyName = QStringLiteral("SnowTile");
    ui->propertyName->setText(mPropertyName);
    mPropertyPreset->setCurrentIndex(
                mPropertyName == QStringLiteral("SnowTile") ? 0
                : mPropertyName == QStringLiteral("BurntTile") ? 1 : 2);

    ui->filterEditSource->setClearButtonEnabled(true);
    ui->filterEditSource->setEnabled(false);
    connect(ui->filterEditSource, &QLineEdit::textEdited, this,
            &SnowEditor::tilesetFilterSourceEdited);

    ui->filterEditTarget->setClearButtonEnabled(true);
    ui->filterEditTarget->setEnabled(false);
    connect(ui->filterEditTarget, &QLineEdit::textEdited, this,
            &SnowEditor::tilesetFilterTargetEdited);

    ui->targetView->model()->setShowHeaders(false);
    ui->targetView->setAcceptDrops(true);
    connect(ui->targetView->model(), &MixedTilesetModel::tileDroppedAt,
            this, &SnowEditor::tileDroppedAt);
    connect(ui->targetView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SnowEditor::syncUI);
    connect(ui->sourceView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SnowEditor::syncUI);

    ui->targetView->setZoomable(mZoomable);
    ui->sourceView->setZoomable(mZoomable);

    ui->tilesetListSource->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(ui->tilesetListSource, &QListWidget::itemSelectionChanged,
            this, &SnowEditor::tilesetSelectionChangedSource);

    ui->tilesetListTarget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(ui->tilesetListTarget, &QListWidget::itemSelectionChanged,
            this, &SnowEditor::tilesetSelectionChangedTarget);

    connect(ui->tilesetMgrSource, &QAbstractButton::clicked,
            this, &SnowEditor::manageTilesets);

    connect(ui->tilesetMgrTarget, &QAbstractButton::clicked,
            this, &SnowEditor::manageTilesets);

    ui->targetView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    ui->sourceView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->sourceView->setDragEnabled(true);

    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAdded,
            this, &SnowEditor::tilesetAdded);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAboutToBeRemoved,
            this, &SnowEditor::tilesetAboutToBeRemoved);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetRemoved,
            this, &SnowEditor::tilesetRemoved);

    connect(TilesetManager::instance(), &TilesetManager::tilesetChanged,
            this, &SnowEditor::tilesetChanged);

    syncUI();
}

SnowEditor::~SnowEditor()
{
    delete mTileDefFile;
    delete ui;
}

void SnowEditor::closeEvent(QCloseEvent *event)
{
    if (confirmSave())
        event->accept();
    else
        event->ignore();
}

void SnowEditor::manageTilesets()
{
    TileMetaInfoDialog dialog(this);
    dialog.exec();

    TileMetaInfoMgr *mgr = TileMetaInfoMgr::instance();
    if (!mgr->writeTxt()) {
        QMessageBox::warning(this, tr("Snow Rules Error"), mgr->errorString());
    }
}

void SnowEditor::tileDroppedAt(const QString &tilesetName, int tileId, int row, int column, const QModelIndex &parent)
{
    if (mTileDefFile == nullptr || mPropertyName.isEmpty())
        return;
    MixedTilesetModel *model = ui->targetView->model();
    QModelIndex index = model->index(row, column, parent);
    Tile* targetTile = model->tileAt(index);
    if (targetTile == nullptr)
        return;
    const QString replacementName =
            BuildingTilesMgr::instance()->nameForTile2(
                tilesetName, tileId);
    if (setTileMapping(targetTile, replacementName)) {
        markDirty();
        setOverlayTiles();
    } else {
        statusBar()->showMessage(
                    tr("Could not assign %1: the target tile is not present "
                       "in the loaded definition.")
                    .arg(replacementName), 7000);
    }
}

void SnowEditor::propertyNameEdited(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed == mPropertyName) {
        return;
    }
    mPropertyName = trimmed;
    QSettings().setValue(QStringLiteral("SnowEditor/PropertyName"),
                         mPropertyName);
    {
        const QSignalBlocker blocker(mPropertyPreset);
        mPropertyPreset->setCurrentIndex(
                    mPropertyName == QStringLiteral("SnowTile") ? 0
                    : mPropertyName == QStringLiteral("BurntTile") ? 1 : 2);
    }
    setOverlayTiles();
    syncUI();
}

void SnowEditor::propertyPresetChanged(int index)
{
    const QString property =
            mPropertyPreset->itemData(index).toString();
    if (property.isEmpty())
        return;
    ui->propertyName->setText(property);
    propertyNameEdited(property);
}

void SnowEditor::assignSourceToSelected()
{
    if (mTileDefFile == nullptr || mPropertyName.isEmpty())
        return;
    Tile *sourceTile = ui->sourceView->model()->tileAt(
                ui->sourceView->currentIndex());
    const QModelIndexList targets =
            ui->targetView->selectionModel()->selectedIndexes();
    if (sourceTile == nullptr || targets.isEmpty()) {
        statusBar()->showMessage(
                    tr("Select one source tile and at least one target tile."),
                    5000);
        return;
    }

    const QString replacementName =
            BuildingTilesMgr::instance()->nameForTile2(
                sourceTile->tileset()->name(), sourceTile->id());
    int changed = 0;
    for (const QModelIndex &index : targets) {
        if (setTileMapping(
                ui->targetView->model()->tileAt(index),
                replacementName)) {
            ++changed;
        }
    }
    if (changed > 0) {
        markDirty();
        setOverlayTiles();
    }
    statusBar()->showMessage(
                tr("Assigned %1 to %2 target tile(s).")
                .arg(replacementName).arg(changed), 5000);
}

void SnowEditor::assignMatchingIds()
{
    if (mTileDefFile == nullptr || mPropertyName.isEmpty() ||
            mCurrentTilesetSource == nullptr) {
        return;
    }
    const QModelIndexList targets =
            ui->targetView->selectionModel()->selectedIndexes();
    if (targets.isEmpty()) {
        statusBar()->showMessage(
                    tr("Select at least one target tile."), 5000);
        return;
    }

    int changed = 0;
    int unavailable = 0;
    for (const QModelIndex &index : targets) {
        Tile *targetTile = ui->targetView->model()->tileAt(index);
        Tile *sourceTile = targetTile == nullptr
                ? nullptr
                : mCurrentTilesetSource->tileAt(targetTile->id());
        if (sourceTile == nullptr) {
            ++unavailable;
            continue;
        }
        const QString replacementName =
                BuildingTilesMgr::instance()->nameForTile2(
                    mCurrentTilesetSource->name(), sourceTile->id());
        if (setTileMapping(targetTile, replacementName))
            ++changed;
    }
    if (changed > 0) {
        markDirty();
        setOverlayTiles();
    }
    statusBar()->showMessage(
                tr("Mapped %1 target tile(s) by ID; %2 source ID(s) "
                   "were unavailable.")
                .arg(changed).arg(unavailable), 7000);
}

void SnowEditor::tilesetFilterSourceEdited(const QString &text)
{
    tilesetFilterEdited(ui->tilesetListSource, text);
}

void SnowEditor::tilesetFilterTargetEdited(const QString &text)
{
    tilesetFilterEdited(ui->tilesetListTarget, text);
}

void SnowEditor::tilesetSelectionChangedSource()
{
    tilesetSelectionChanged(ui->tilesetListSource, ui->sourceView, &mCurrentTilesetSource);
}

void SnowEditor::tilesetSelectionChangedTarget()
{
    tilesetSelectionChanged(ui->tilesetListTarget, ui->targetView, &mCurrentTilesetTarget);
}

void SnowEditor::tilesetFilterEdited(QListWidget *tilesetNamesList, const QString &text)
{
    for (int row = 0; row < tilesetNamesList->count(); row++) {
        QListWidgetItem* item = tilesetNamesList->item(row);
        item->setHidden(text.trimmed().isEmpty()
                        ? false
                        : !item->text().contains(
                              text.trimmed(), Qt::CaseInsensitive));
    }

    QListWidgetItem* current = tilesetNamesList->currentItem();
    if (current != nullptr && current->isHidden()) {
        // Select previous visible row.
        int row = tilesetNamesList->row(current) - 1;
        while (row >= 0 && tilesetNamesList->item(row)->isHidden())
            row--;
        if (row >= 0) {
            current = tilesetNamesList->item(row);
            tilesetNamesList->setCurrentItem(current);
            tilesetNamesList->scrollToItem(current);
            return;
        }

        // Select next visible row.
        row = tilesetNamesList->row(current) + 1;
        while (row < tilesetNamesList->count() && tilesetNamesList->item(row)->isHidden())
            row++;
        if (row < tilesetNamesList->count()) {
            current = tilesetNamesList->item(row);
            tilesetNamesList->setCurrentItem(current);
            tilesetNamesList->scrollToItem(current);
            return;
        }

        // All items hidden
        tilesetNamesList->setCurrentItem(nullptr);
    }

    current = tilesetNamesList->currentItem();
    if (current != nullptr)
        tilesetNamesList->scrollToItem(current);
}

void SnowEditor::setTilesetSourceList()
{
    setTilesetList(ui->filterEditSource, ui->tilesetListSource);
}

void SnowEditor::setTilesetTargetList()
{
    setTilesetList(ui->filterEditTarget, ui->tilesetListTarget);
}

void SnowEditor::tilesetSelectionChanged(QListWidget *tilesetNamesList, Tiled::Internal::MixedTilesetView *tilesetView, Tileset **tilesetPtr)
{
    QList<QListWidgetItem*> selection = tilesetNamesList->selectedItems();
    QListWidgetItem *item = selection.count() ? selection.first() : nullptr;
    *tilesetPtr = nullptr;
    if (item) {
        int row = tilesetNamesList->row(item);
        *tilesetPtr = TileMetaInfoMgr::instance()->tileset(row);
        if ((*tilesetPtr)->isMissing()) {
            tilesetView->clear();
        } else {
            tilesetView->setTileset(*tilesetPtr);
            if (tilesetView == ui->targetView) {
                setOverlayTiles();
            }
        }
    } else {
        tilesetView->clear();
    }
    syncUI();
}

void SnowEditor::clearOverlayTiles()
{
    mMappedCurrent = 0;
    mUnresolvedCurrent = 0;
    if (mCurrentTilesetTarget == nullptr) {
        return;
    }
    MixedTilesetModel *model = ui->targetView->model();
    for (int tileId = 0; tileId < mCurrentTilesetTarget->tileCount(); tileId++) {
        Tile *tile = mCurrentTilesetTarget->tileAt(tileId);
        const QModelIndex index = model->index(tile);
        model->clearCategoryBounds(index);
        model->setOverlayTile(index, nullptr);
        model->setData(index, QString(), Qt::ToolTipRole);
    }
    ui->targetView->update();
}

void SnowEditor::setOverlayTiles()
{
    if (mCurrentTilesetTarget == nullptr) {
        return;
    }
    clearOverlayTiles();
    MixedTilesetModel *model = ui->targetView->model();
    if (mPropertyName.isEmpty()) {
        return;
    }
    TileDefTileset *tdts = mTileDefFile->tileset(mCurrentTilesetTarget->name());
    if (tdts == nullptr) {
        return;
    }
    for (int tileId = 0; tileId < mCurrentTilesetTarget->tileCount(); tileId++) {
        TileDefTile *tdt = tdts->tileAt(tileId);
        if (tdt == nullptr)
            continue;
        const QString replacementName =
                tdt->mProperties.value(mPropertyName).trimmed();
        if (replacementName.isEmpty())
            continue;
        ++mMappedCurrent;
        QString snowTilesetName;
        int snowTileId = -1;
        Tile *tile = mCurrentTilesetTarget->tileAt(tileId);
        const QModelIndex index = model->index(tile);
        bool resolved = false;
        if (BuildingTilesMgr::instance()->parseTileName(
                replacementName, snowTilesetName, snowTileId)) {
            if (Tileset *snowTileset = TileMetaInfoMgr::instance()->tileset(snowTilesetName)) {
                if (Tile *snowTile = snowTileset->tileAt(snowTileId)) {
                    model->setOverlayTile(index, snowTile);
                    model->setCategoryBounds(tile, QRect(0, 0, 1, 1));
                    model->setData(index, QBrush(QColor(96, 180, 255, 96)),
                                   MixedTilesetModel::CategoryBgRole);
                    model->setData(
                                index,
                                tr("%1 = %2").arg(
                                    mPropertyName, replacementName),
                                Qt::ToolTipRole);
                    resolved = true;
                }
            }
        }
        if (!resolved) {
            ++mUnresolvedCurrent;
            model->setCategoryBounds(tile, QRect(0, 0, 1, 1));
            model->setData(index, QBrush(QColor(230, 80, 80, 120)),
                           MixedTilesetModel::CategoryBgRole);
            model->setData(
                        index,
                        tr("Unresolved %1 reference: %2")
                        .arg(mPropertyName, replacementName),
                        Qt::ToolTipRole);
        }
    }
    ui->targetView->update();
    updateStatus();
}

void SnowEditor::syncUI()
{
    bool bHasFile = mTileDefFile != nullptr;
    ui->actionSave->setEnabled(bHasFile);
    ui->propertyName->setEnabled(bHasFile);
    mPropertyPreset->setEnabled(bHasFile);
    const bool hasTargets = bHasFile &&
            !mPropertyName.isEmpty() &&
            ui->targetView->selectionModel()->hasSelection();
    ui->actionReset->setEnabled(hasTargets);
    mAssignSelectedButton->setEnabled(
                hasTargets && ui->sourceView->currentIndex().isValid());
    mAssignMatchingButton->setEnabled(
                hasTargets && mCurrentTilesetSource != nullptr);
    updateWindowTitle();
    updateStatus();
}

void SnowEditor::setTilesetList(QLineEdit *lineEdit, QListWidget *tilesetNamesList)
{
    tilesetNamesList->clear();
    // Add the list of tilesets, and resize it to fit
    int width = 64;
    QFontMetrics fm = tilesetNamesList->fontMetrics();
    const QList<Tileset*> tilesets = TileMetaInfoMgr::instance()->tilesets();
    for (Tileset *tileset : tilesets) {
        QListWidgetItem *item = new QListWidgetItem();
        item->setText(tileset->name());
        if (tileset->isMissing())
            item->setForeground(Qt::red);
        tilesetNamesList->addItem(item);
        width = qMax(width, fm.horizontalAdvance(tileset->name()));
    }
    int sbw = tilesetNamesList->verticalScrollBar()->sizeHint().width();
    tilesetNamesList->setFixedWidth(width + 16 + sbw);

    lineEdit->setFixedWidth(tilesetNamesList->width());
    lineEdit->setEnabled(tilesetNamesList->count() > 0);
    tilesetFilterEdited(tilesetNamesList, lineEdit->text());
}

void SnowEditor::fileOpen(const QString &filePath)
{
    PROGRESS progress(tr("Reading %1")
                      .arg(QFileInfo(filePath).fileName()), this);
    TileDefFile *loadedFile = new TileDefFile();
    TileDefFileReader reader;
    if (!reader.read(filePath, *loadedFile)) {
        QMessageBox::warning(this, tr("Error"),
                             loadedFile->errorString());
        delete loadedFile;
        return;
    }

    delete mTileDefFile;
    mTileDefFile = loadedFile;
    mDirty = false;
    setTilesetTargetList();
    setTilesetSourceList();

    int initialRow = -1;
    for (TileDefTileset *definition : mTileDefFile->tilesets()) {
        if (Tileset *tileset =
                TileMetaInfoMgr::instance()->tileset(
                    definition->mName)) {
            initialRow = TileMetaInfoMgr::instance()->indexOf(tileset);
            if (!tileset->isMissing())
                break;
        }
    }
    if (initialRow >= 0) {
        ui->tilesetListTarget->setCurrentRow(initialRow);
        ui->tilesetListSource->setCurrentRow(initialRow);
    }
    updateWindowTitle();
    setOverlayTiles();
}

bool SnowEditor::fileSave(const QString &filePath)
{
    if (!mTileDefFile->write(filePath)) {
        QMessageBox::warning(this, tr("Error"), mTileDefFile->errorString());
        return false;
    }
    mTileDefFile->setFileName(filePath);

    TileDefTextFile textFile;
    if (!textFile.write(filePath + QLatin1String(".txt"), mTileDefFile->tilesets())) {
        QMessageBox::warning(this, tr("Error"), textFile.errorString());
        return false;
    }
    mDirty = false;
    updateWindowTitle();
    updateStatus();
    return true;
}

bool SnowEditor::confirmSave()
{
    if (mTileDefFile == nullptr || !mDirty)
        return true;

    int ret = QMessageBox::warning(
            this, tr("Unsaved Changes"),
            tr("There are unsaved changes. Do you want to save now?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    switch (ret) {
    case QMessageBox::Save:    return fileSave();
    case QMessageBox::Discard: return true;
    case QMessageBox::Cancel:
    default:
        return false;
    }
}

QString SnowEditor::getSaveLocation()
{
    QSettings settings;
    QString key = QLatin1String("SnowEditor/LastOpenPath");
    QString suggestedFileName = settings.value(
                key, QStringLiteral("newtiledefinitions.tiles")).toString();
    if (mTileDefFile != nullptr) {
        suggestedFileName = mTileDefFile->fileName();
    }
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                    suggestedFileName,
                                                    QLatin1String("Tile properties files (*.tiles)"));
    if (fileName.isEmpty())
        return QString();
    settings.setValue(key, QFileInfo(fileName).absolutePath());
    return fileName;
}

void SnowEditor::tilesetAdded(Tileset *tileset)
{
    setTilesetSourceList();
    setTilesetTargetList();
    int row = TileMetaInfoMgr::instance()->indexOf(tileset);
    ui->tilesetListSource->setCurrentRow(row);
    ui->tilesetListTarget->setCurrentRow(row);
}

void SnowEditor::tilesetAboutToBeRemoved(Tileset *tileset)
{
    int row = TileMetaInfoMgr::instance()->indexOf(tileset);
    delete ui->tilesetListSource->takeItem(row);
    delete ui->tilesetListTarget->takeItem(row);
}

void SnowEditor::tilesetRemoved(Tileset *tileset)
{
    Q_UNUSED(tileset)
}

// Called when a tileset image changes or a missing tileset was found.
void SnowEditor::tilesetChanged(Tileset *tileset)
{
    if (tileset == mCurrentTilesetTarget) {
        if (tileset->isMissing())
            ui->targetView->clear();
        else
            ui->targetView->setTileset(tileset);
    }

    if (tileset == mCurrentTilesetSource) {
        if (tileset->isMissing())
            ui->sourceView->clear();
        else
            ui->sourceView->setTileset(tileset);
    }

    int row = TileMetaInfoMgr::instance()->indexOf(tileset);
    if (QListWidgetItem *item = ui->tilesetListSource->item(row)) {
        item->setForeground(tileset->isMissing() ? Qt::red : QBrush());
    }
    if (QListWidgetItem *item = ui->tilesetListTarget->item(row)) {
        item->setForeground(tileset->isMissing() ? Qt::red : QBrush());
    }
}

void SnowEditor::fileOpen()
{
    if (!confirmSave())
        return;

    QSettings settings;
    QString key = QLatin1String("SnowEditor/LastOpenPath");
    QString lastPath = settings.value(key, QStringLiteral("newtiledefinitions.tiles")).toString();

    QString fileName = QFileDialog::getOpenFileName(this, tr("Choose .tiles file"),
                                                    lastPath,
                                                    QLatin1String("Binary property files (*.tiles);;Text property files (*.tiles.txt)"));
    if (fileName.isEmpty())
        return;

    settings.setValue(key, QFileInfo(fileName).absolutePath());

    fileOpen(fileName);

    syncUI();
}

bool SnowEditor::fileSave()
{
    QString fileName = getSaveLocation();
    if (fileName.isEmpty())
        return false;
    return fileSave(fileName);
}

void SnowEditor::clearProperties()
{
    if (mCurrentTilesetTarget == nullptr ||
            mTileDefFile == nullptr || mPropertyName.isEmpty())
        return;
    TileDefTileset *tdts = mTileDefFile->tileset(mCurrentTilesetTarget->name());
    if (tdts == nullptr)
        return;
    const QModelIndexList selection = ui->targetView->selectionModel()->selectedIndexes();
    int cleared = 0;
    for (const QModelIndex& index : selection) {
        Tile *tile = ui->targetView->model()->tileAt(index);
        if (tile == nullptr)
            continue;
        if (TileDefTile *tdt = tdts->tileAt(tile->id())) {
            if (tdt->mProperties.remove(mPropertyName) > 0) {
                tdt->mPropertyUI.FromProperties(tdt->mProperties);
                ++cleared;
            }
        }
    }
    if (cleared > 0)
        markDirty();
    setOverlayTiles();
    statusBar()->showMessage(
                tr("Cleared %1 %2 value(s).")
                .arg(cleared).arg(mPropertyName), 5000);
}

bool SnowEditor::setTileMapping(
        Tile *targetTile, const QString &replacementName)
{
    if (mTileDefFile == nullptr || targetTile == nullptr ||
            mPropertyName.isEmpty() || replacementName.isEmpty()) {
        return false;
    }
    TileDefTileset *definition = mTileDefFile->tileset(
                targetTile->tileset()->name());
    TileDefTile *definitionTile = definition == nullptr
            ? nullptr : definition->tileAt(targetTile->id());
    if (definitionTile == nullptr)
        return false;
    if (definitionTile->mProperties.value(mPropertyName) ==
            replacementName) {
        return false;
    }
    definitionTile->mProperties[mPropertyName] = replacementName;
    definitionTile->mPropertyUI.FromProperties(
                definitionTile->mProperties);
    return true;
}

void SnowEditor::markDirty()
{
    if (!mDirty) {
        mDirty = true;
        updateWindowTitle();
    }
    updateStatus();
}

void SnowEditor::updateWindowTitle()
{
    const QString fileName = mTileDefFile == nullptr
            ? tr("No definition loaded")
            : QFileInfo(mTileDefFile->fileName()).fileName();
    setWindowTitle(tr("%1%2 - Snow / Replacement Editor")
                   .arg(fileName, mDirty ? QStringLiteral("*")
                                         : QString()));
}

void SnowEditor::updateStatus()
{
    if (mTileDefFile == nullptr) {
        statusBar()->showMessage(
                    tr("Open a .tiles or .tiles.txt definition to begin."));
        return;
    }
    const int selectedTargets =
            ui->targetView->selectionModel()->selectedIndexes().size();
    const QString targetName = mCurrentTilesetTarget == nullptr
            ? tr("no target tileset")
            : mCurrentTilesetTarget->name();
    statusBar()->showMessage(
                tr("%1 | Property: %2 | %3 mapped, %4 unresolved | "
                   "%5 target tile(s) selected%6")
                .arg(targetName, mPropertyName)
                .arg(mMappedCurrent).arg(mUnresolvedCurrent)
                .arg(selectedTargets)
                .arg(mDirty ? tr(" | Modified") : QString()));
}
