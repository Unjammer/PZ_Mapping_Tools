/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "buildinglayersdock.h"
#include "ui_buildinglayersdock.h"

#include "buildingdocument.h"
#include "buildingdocumentmgr.h"
#include "buildingfloor.h"
#include "buildingisoview.h"
#include "buildingmap.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace BuildingEditor;

BuildingTileInspector::BuildingTileInspector(QWidget *parent)
    : QDockWidget(tr("Tile Layers at Cursor"), parent),
      mFollow(new QCheckBox(tr("Follow cursor"))),
      mPosition(new QLabel),
      mTiles(new QTreeWidget)
{
    setObjectName(QStringLiteral("TileEditMode.TileInspector"));
    QWidget *contents = new QWidget(this);
    auto *layout = new QVBoxLayout(contents);
    auto *controls = new QHBoxLayout;
    mFollow->setChecked(true);
    mFollow->setToolTip(tr("Use Shift+click with Pick Tiles to hold a square."));
    controls->addWidget(mFollow);
    controls->addStretch();
    controls->addWidget(mPosition);
    layout->addLayout(controls);
    mTiles->setHeaderLabels({tr("Layer"), tr("Tile")});
    mTiles->setRootIsDecorated(false);
    mTiles->setUniformRowHeights(true);
    mTiles->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mTiles->setToolTip(tr("Double-click a tile to show its tileset and select its layer."));
    layout->addWidget(mTiles);
    setWidget(contents);

    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        mShown = visible;
        if (visible) {
            if (mView && mFollow->isChecked())
                mSquare = mView->mouseTilePosition();
            refresh();
        }
    });
    connect(mFollow, &QCheckBox::toggled, this, [this](bool follow) {
        if (follow && mView)
            mSquare = mView->mouseTilePosition();
        refresh();
    });
    connect(mTiles, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit tileActivated(item->text(1), item->text(0));
    });
}

void BuildingTileInspector::setView(BuildingIsoView *view)
{
    if (mView) {
        mView->disconnect(this);
        mView->scene()->disconnect(this);
        if (auto *document = mView->scene()->document())
            document->disconnect(this);
    }
    mView = view;
    mSquare = view ? view->mouseTilePosition() : QPoint(-1, -1);
    mFollow->setChecked(true);
    if (view) {
        connect(view, &BuildingIsoView::mouseCoordinateChanged, this, [this](const QPoint &pos) {
            if (mShown && mFollow->isChecked() && mSquare != pos) {
                mSquare = pos;
                refresh();
            }
        });
        connect(view->scene(), &BuildingIsoScene::tileContentsChanged, this,
                [this](int level, const QRegion &region) {
            if (mShown && level == mView->scene()->currentLevel() && region.contains(mSquare))
                refresh();
        });
        connect(view->scene()->document(), &BuildingDocument::currentFloorChanged, this, [this] {
            if (mFollow->isChecked())
                mSquare = mView->mouseTilePosition();
            refresh();
        });
    }
    refresh();
}

void BuildingTileInspector::inspectSquare(const QPoint &pos)
{
    mSquare = pos;
    mFollow->setChecked(false);
    show();
    raise();
    refresh();
}

void BuildingTileInspector::refresh()
{
    if (!mShown)
        return;
    BuildingIsoScene *scene = mView ? mView->scene() : nullptr;
    QVector<QPair<QString, QString>> rows;
    QString position;
    if (scene && scene->currentFloorContains(mSquare, 1, 1) && scene->buildingMap()) {
        rows = scene->buildingMap()->tilesAtSquare(scene->currentLevel(), mSquare);
        position = tr("%1, %2, %3").arg(mSquare.x()).arg(mSquare.y()).arg(scene->currentLevel());
    }
    mPosition->setText(position);
    if (mRows == rows)
        return;
    mRows = rows;
    mTiles->clear();
    for (const auto &row : rows)
        new QTreeWidgetItem(mTiles, {row.first, row.second});
}

BuildingLayersDock::BuildingLayersDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::BuildingLayersDock),
    mDocument(0),
    mSynching(false)
{
    ui->setupUi(this);

    connect(ui->visibility, &QAbstractSlider::valueChanged, this, &BuildingLayersDock::visibilityChanged);
    connect(ui->opacity, &QAbstractSlider::valueChanged, this, &BuildingLayersDock::opacityChanged);

    connect(ui->layers, &QListWidget::currentRowChanged,
            this, qOverload<int>(&BuildingLayersDock::currentLayerChanged));
    connect(ui->layers, &QListWidget::itemChanged,
            this, &BuildingLayersDock::layerItemChanged);

    connect(BuildingDocumentMgr::instance(), &BuildingDocumentMgr::currentDocumentChanged,
            this, &BuildingLayersDock::currentDocumentChanged);
    updateActions();
}

BuildingLayersDock::~BuildingLayersDock()
{
    delete ui;
}

void BuildingLayersDock::currentDocumentChanged(BuildingDocument *doc)
{
    if (mDocument)
        mDocument->disconnect(this);

    mDocument = doc;

    if (mDocument) {
        connect(mDocument, &BuildingDocument::currentFloorChanged,
                this, &BuildingLayersDock::currentFloorChanged);
        connect(mDocument, &BuildingDocument::currentLayerChanged,
                this, qOverload<>(&BuildingLayersDock::currentLayerChanged));
        connect(mDocument, &BuildingDocument::layerVisibilityChanged,
                this, &BuildingLayersDock::layerVisibilityChanged);
    }

    setLayersList();
    currentLayerChanged();
}

void BuildingLayersDock::setLayersList()
{
    mSynching = true;

    ui->layers->clear();

    QString topVisibleLayer;

    if (mDocument) {
        const QStringList layerNames = BuildingMap::layerNames(mDocument->currentLevel());
        for (const QString& layerName : layerNames) {
            QListWidgetItem *item = new QListWidgetItem;
            item->setText(layerName);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            bool visible = mDocument->currentFloor()->layerVisibility(layerName);
            item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
            ui->layers->insertItem(0, item);
            if (visible)
                topVisibleLayer = layerName;
        }
    }

    ui->visibility->setRange(0, qMax(0, ui->layers->count() - 1));
    int visibility = ui->visibility->maximum();
    if (!topVisibleLayer.isEmpty()) {
        QStringList layerNames = BuildingMap::layerNames(mDocument->currentLevel());
        int n = layerNames.indexOf(topVisibleLayer);
        visibility = n;
    }
    ui->visibility->setValue(visibility);

    mSynching = false;

    updateActions();
}

void BuildingLayersDock::currentLayerChanged(int row)
{
    if (!mDocument || mSynching)
        return;
    QString layerName;
    if (row >= 0)
        layerName = ui->layers->item(row)->text();
    mDocument->setCurrentLayer(layerName);
}

void BuildingLayersDock::visibilityChanged(int value)
{
    if (mSynching || !mDocument)
        return;

    for (int i = ui->layers->count() - 1; i >= 0; i--)
        mDocument->setLayerVisibility(mDocument->currentFloor(),
                                      ui->layers->item(i)->text(),
                                      ui->layers->count() - i - 1 <= value);
}

void BuildingLayersDock::opacityChanged(int value)
{
    if (mSynching || !mDocument)
        return;

    mDocument->setLayerOpacity(mDocument->currentFloor(),
                               mDocument->currentLayer(),
                               qreal(value) / ui->opacity->maximum());
}

void BuildingLayersDock::layerItemChanged(QListWidgetItem *item)
{
    if (mSynching || !mDocument)
        return;

    mDocument->setLayerVisibility(mDocument->currentFloor(),
                                  item->text(),
                                  item->checkState() == Qt::Checked);
}

void BuildingLayersDock::currentFloorChanged()
{
    QString layerName = mDocument->currentLayer();
    setLayersList();

    if (mDocument) {
        QStringList layerNames = BuildingMap::layerNames(mDocument->currentLevel());
        if (layerNames.contains(layerName)) {
            int index = layerNames.indexOf(layerName);
            int row = ui->layers->count() - index - 1;
            ui->layers->setCurrentRow(row);
        } else {
            ui->layers->setCurrentRow(ui->layers->count() - 1);
        }
    }
    updateActions();
}

void BuildingLayersDock::currentLayerChanged()
{
    if (mDocument) {
        auto items = ui->layers->findItems(mDocument->currentLayer(), Qt::MatchExactly);
        if (items.size() == 1) {
            ui->layers->setCurrentItem(items.at(0));
        }
    }
    updateActions();
}

void BuildingLayersDock::layerVisibilityChanged(BuildingFloor *floor, const QString &layerName)
{
    if (mDocument && (floor == mDocument->currentFloor())) {
        int index = BuildingMap::layerNames(floor->level()).indexOf(layerName);
        if ((index >= 0) && (index < ui->layers->count())) {
            int row = ui->layers->count() - index - 1;
            ui->layers->item(row)->setCheckState(floor->layerVisibility(layerName) ?
                                                     Qt::Checked : Qt::Unchecked);
        }
    }
}

void BuildingLayersDock::updateActions()
{
    mSynching = true;

    QString currentLayerName = mDocument ? mDocument->currentLayer() : QString();
    qreal opacity = 1.0f;
    if (mDocument)
        opacity = mDocument->currentFloor()->layerOpacity(currentLayerName);
    ui->opacity->setValue(ui->opacity->maximum() * opacity);
    ui->opacity->setEnabled(!currentLayerName.isEmpty());

    ui->visibility->setEnabled(mDocument != 0);

    mSynching = false;
}
