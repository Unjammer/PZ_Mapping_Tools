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

#ifndef SNOWEDITOR_H
#define SNOWEDITOR_H

#include "qlineedit.h"
#include <QMainWindow>
#include <QMap>

class QListWidget;

namespace Ui {
class SnowEditor;
}

namespace Tiled {
class Tileset;
namespace Internal {
class MixedTilesetView;
class TileDefFile;
class Zoomable;
}
}

class SnowEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit SnowEditor(QWidget *parent = nullptr);
    ~SnowEditor();

private slots:
    void manageTilesets();
    void tileDroppedAt(const QString &tilesetName, int tileId, int row, int column, const QModelIndex &parent);
    void propertyNameEdited(const QString &text);
    void tilesetFilterSourceEdited(const QString &text);
    void tilesetFilterTargetEdited(const QString &text);
    void tilesetSelectionChangedSource();
    void tilesetSelectionChangedTarget();
    void syncUI();

    void tilesetAdded(Tiled::Tileset *tileset);
    void tilesetAboutToBeRemoved(Tiled::Tileset *tileset);
    void tilesetRemoved(Tiled::Tileset *tileset);
    void tilesetChanged(Tiled::Tileset *tileset);

    void fileOpen();
    bool fileSave();

    void clearProperties();

private:
    void tilesetFilterEdited(QListWidget *tilesetNamesList, const QString &text);
    void setTilesetSourceList();
    void setTilesetTargetList();
    void setTilesetList(QLineEdit *lineEdit, QListWidget *tilesetNamesList);
    void tilesetSelectionChanged(QListWidget *tilesetNamesList, Tiled::Internal::MixedTilesetView *tilesetView, Tiled::Tileset **tilesetPtr);
    void clearOverlayTiles();
    void setOverlayTiles();
    void fileOpen(const QString& filePath);
    bool fileSave(const QString& filePath);
    bool confirmSave();
    QString getSaveLocation();

private:
    Ui::SnowEditor *ui;
    Tiled::Internal::TileDefFile *mTileDefFile = nullptr;
    Tiled::Tileset *mCurrentTilesetTarget = nullptr;
    Tiled::Tileset *mCurrentTilesetSource = nullptr;
    Tiled::Internal::Zoomable *mZoomable = nullptr;
    QString mPropertyName;
};

#endif // SNOWEDITOR_H
