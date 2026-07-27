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

#include "buildingfurnituredock.h"

#include "buildingeditorwindow.h"
#include "buildingfloor.h"
#include "buildingpreferences.h"
#include "buildingtiles.h"
#include "buildingtilesdialog.h"
#include "buildingtiletools.h"
#include "furnituregroups.h"
#include "furnitureview.h"

#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zoomable.h"

#include "tile.h"
#include "tileset.h"

#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QListWidget>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

using namespace BuildingEditor;

BuildingFurnitureDock::BuildingFurnitureDock(QWidget *parent) :
    QDockWidget(parent),
    mGroupList(new QListWidget(this)),
    mFurnitureView(new FurnitureView(this)),
    mCurrentGroup(0),
    mCurrentTile(0)
{
    setObjectName(QLatin1String("FurnitureDock"));

    mGroupList->setObjectName(QLatin1String("FurnitureDock.groupList"));
    mFurnitureView->setObjectName(QLatin1String("FurnitureDock.furnitureView"));

    QHBoxLayout *comboLayout = new QHBoxLayout;
    comboLayout->setObjectName(QLatin1String("FurnitureDock.comboLayout"));
    QComboBox *scaleCombo = new QComboBox;
    scaleCombo->setObjectName(QLatin1String("FurnitureDock.scaleComboBox"));
    scaleCombo->setEditable(true);
    comboLayout->addStretch(1);
    comboLayout->addWidget(scaleCombo);

    QWidget *rightWidget = new QWidget(this);
    rightWidget->setObjectName(QLatin1String("FurnitureDock.rightWidget"));
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setObjectName(QLatin1String("FurnitureDock.rightLayout"));
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(mFurnitureView, 1);
    rightLayout->addLayout(comboLayout);

    QSplitter *splitter = mSplitter = new QSplitter;
    splitter->setObjectName(QLatin1String("FurnitureDock.splitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(mGroupList);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(1, 1);

    QWidget *outer = new QWidget(this);
    outer->setObjectName(QLatin1String("FurnitureDock.contents"));
    QHBoxLayout *outerLayout = new QHBoxLayout(outer);
    outerLayout->setObjectName(QLatin1String("FurnitureDock.contentsLayout"));
    outerLayout->setSpacing(5);
    outerLayout->setContentsMargins(5, 5, 5, 5);
    outerLayout->addWidget(splitter);
    setWidget(outer);

    BuildingPreferences *prefs = BuildingPreferences::instance();
    mFurnitureView->zoomable()->setScale(prefs->tileScale());
    mFurnitureView->zoomable()->connectToComboBox(scaleCombo);
    connect(prefs, &BuildingPreferences::tileScaleChanged,
            this, &BuildingFurnitureDock::tileScaleChanged);
    connect(mFurnitureView->zoomable(), &Tiled::Internal::Zoomable::scaleChanged,
            prefs, &BuildingPreferences::setTileScale);

    connect(mGroupList, &QListWidget::currentRowChanged, this, &BuildingFurnitureDock::currentGroupChanged);
    connect(mFurnitureView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &BuildingFurnitureDock::currentFurnitureChanged);

    connect(BuildingTilesDialog::instance(), &BuildingTilesDialog::edited,
            this, &BuildingFurnitureDock::tilesDialogEdited);
    connect(FurnitureGroups::instance(), &FurnitureGroups::furnitureCatalogLoaded,
            this, &BuildingFurnitureDock::setGroupsList);

    retranslateUi();
}

void BuildingFurnitureDock::readSettings(QSettings &settings)
{
    settings.beginGroup(QLatin1String("FurnitureDock"));
    BuildingEditorWindow::instance()->restoreSplitterSizes(mSplitter);
    settings.endGroup();
}

void BuildingFurnitureDock::writeSettings(QSettings &settings)
{
    settings.beginGroup(QLatin1String("FurnitureDock"));
    BuildingEditorWindow::instance()->saveSplitterSizes(mSplitter);
    settings.endGroup();
}

void BuildingFurnitureDock::switchTo()
{
    setGroupsList();
}

bool BuildingFurnitureDock::validateFurnitureCatalog(QString *errorString)
{
    FurnitureGroups *catalog = FurnitureGroups::instance();
    const int expectedGroups = catalog->groupCount();
    if (expectedGroups <= 0) {
        *errorString = tr("The furniture catalog is empty.");
        return false;
    }
    if (mGroupList->count() != expectedGroups) {
        *errorString = tr("Furniture mode displays %1 of %2 groups.")
                .arg(mGroupList->count()).arg(expectedGroups);
        return false;
    }

    int furnitureDefinitions = 0;
    int tileReferences = 0;
    QStringList invalidReferences;
    for (FurnitureGroup *group : catalog->groups()) {
        furnitureDefinitions += group->mTiles.count();
        for (FurnitureTiles *furniture : group->mTiles) {
            for (FurnitureTile *orientation : furniture->tiles()) {
                if (!orientation)
                    continue;
                for (BuildingTile *buildingTile : orientation->tiles()) {
                    if (!buildingTile || buildingTile->isNone())
                        continue;
                    ++tileReferences;
                    Tiled::Tileset *tileset =
                            Tiled::Internal::TileMetaInfoMgr::instance()
                            ->tileset(buildingTile->mTilesetName);
                    // Tilesets.txt dimensions may lag behind newer PNGs.
                    // Validate an index only after that image has been loaded
                    // and its real dimensions are known.
                    if (!tileset || buildingTile->mIndex < 0
                            || (tileset->isLoaded()
                                && buildingTile->mIndex
                                   >= tileset->tileCount())) {
                        invalidReferences += buildingTile->name();
                    }
                }
            }
        }
    }
    if (!invalidReferences.isEmpty()) {
        invalidReferences.removeDuplicates();
        qWarning().noquote()
                << tr("Furniture catalog contains %1 unavailable tile "
                      "references; first: %2. These entries will use the "
                      "missing-tile placeholder until matching game tiles "
                      "are installed.")
                   .arg(invalidReferences.count())
                   .arg(invalidReferences.first());
    }

    mGroupList->setCurrentRow(0);
    if (!mCurrentGroup || mFurnitureView->model()->rowCount() <= 0) {
        *errorString = tr("The first furniture group has no visible entries.");
        return false;
    }

    // Rendering the first non-empty orientation verifies that the preloaded
    // catalog can supply usable furniture images.
    for (FurnitureTiles *furniture : mCurrentGroup->mTiles) {
        for (FurnitureTile *orientation : furniture->tiles()) {
            if (!orientation || orientation->isEmpty())
                continue;
            for (BuildingTile *buildingTile : orientation->tiles()) {
                if (!buildingTile || buildingTile->isNone())
                    continue;
                Tiled::Tile *tile =
                        BuildingTilesMgr::instance()->tileFor(buildingTile);
                if (!tile
                        || tile == Tiled::Internal::TilesetManager::instance()
                           ->missingTile()
                        || tile->image().isNull()) {
                    *errorString = tr("Furniture tile could not be loaded: %1")
                            .arg(buildingTile->name());
                    return false;
                }
            }
            qInfo() << "Validated furniture catalog:"
                    << expectedGroups << "groups,"
                    << furnitureDefinitions << "definitions,"
                    << tileReferences << "tile references";
            return true;
        }
    }

    *errorString = tr("The first furniture group contains no tile images.");
    return false;
}

void BuildingFurnitureDock::changeEvent(QEvent *e)
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

void BuildingFurnitureDock::retranslateUi()
{
    setWindowTitle(tr("Furniture"));
}

void BuildingFurnitureDock::setGroupsList()
{
    FurnitureGroup *previousGroup = mCurrentGroup;
    mGroupList->clear();
    foreach (FurnitureGroup *group, FurnitureGroups::instance()->groups())
        mGroupList->addItem(group->mLabel);

    int row = FurnitureGroups::instance()->indexOf(previousGroup);
    if (row < 0 && mGroupList->count() > 0)
        row = 0;
    mGroupList->setCurrentRow(row);
}

void BuildingFurnitureDock::setFurnitureList()
{
    QList<FurnitureTiles*> ftiles;
    if (mCurrentGroup) {
        ftiles = mCurrentGroup->mTiles;
    }
    mFurnitureView->setTiles(ftiles);
}

void BuildingFurnitureDock::currentGroupChanged(int row)
{
    mCurrentGroup = 0;
    mCurrentTile = 0;
    if (row >= 0)
        mCurrentGroup = FurnitureGroups::instance()->group(row);
    setFurnitureList();
}

void BuildingFurnitureDock::currentFurnitureChanged()
{
    QModelIndexList indexes = mFurnitureView->selectionModel()->selectedIndexes();
    if (indexes.count() == 1) {
        QModelIndex index = indexes.first();
        if (FurnitureTile *ftile = mFurnitureView->model()->tileAt(index)) {
            ftile = ftile->resolved();
            mCurrentTile = ftile;

            if (!DrawTileTool::instance()->action()->isEnabled())
                return;

            QRegion rgn;
            FloorTileGrid *tiles = ftile->toFloorTileGrid(rgn);
            if (!tiles) { // empty
                DrawTileTool::instance()->setTile(QString());
                return;
            }

            DrawTileTool::instance()->setCaptureTiles(tiles, rgn);
        }
    }
}

void BuildingFurnitureDock::tileScaleChanged(qreal scale)
{
    mFurnitureView->zoomable()->setScale(scale);
}

void BuildingFurnitureDock::tilesDialogEdited()
{
    FurnitureGroup *group = mCurrentGroup;
    setGroupsList();
    if (group) {
        int row = FurnitureGroups::instance()->indexOf(group);
        if (row >= 0)
            mGroupList->setCurrentRow(row);
    }
}
