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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QRect>
#include <QSettings>

namespace Ui {
class MainWindow;
}

class ActionManager;
class Document;
class DocumentManager;
class KeyboardShortcutWindow;
class LayersDock;
class LotsDock;
class LotPackWindow;
class LuaTable;
class MapsDock;
class InGameMapDock;
class ObjectsDock;
class PropertiesDock;
class RoadsDock;
class SearchDock;
class StreetNamesDock;
class UndoDock;
class World;
class WorldDocument;
//class WorldScene;
class Zoomable;

class QComboBox;
class QMenu;
class QToolButton;

namespace Lua {
class LuaTable;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    static MainWindow *instance();

    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    DocumentManager *docman() const;

    bool saveFile(const QString &fileName);
    bool openFile(const QString &fileName);

    void openLastFiles();
    void startSettingsAutoSave();

    bool InitConfigFiles();

    void readSettings();
    bool canRemoveEmptyBorderCells() const;

protected:
    void closeEvent(QCloseEvent *event);
    void changeEvent(QEvent *event);
    void retranslateUi();

public slots:
    void updateActions();
    void openFile();
    void newWorld();
    void editCell();
    void goToXY();
    void setShowGrid(bool show);

    void documentAdded(Document *doc);
    void documentAboutToClose(int index, Document *doc);
    void currentDocumentTabChanged(int tabIndex);
    void currentDocumentChanged(Document *doc);
    void documentCloseRequested(int tabIndex);

    void updateZoom();

    void selectLevelAbove();
    void selectLevelBelow();

    void zoomIn();
    void zoomOut();
    void zoomNormal();

    bool saveFile();
    bool saveFileAs();
    void closeFile();
    void closeAllFiles();

    void WriteSpawnPoints();
    void WriteWorldObjects();
    void ReadWorldObjects();
    void WriteRoomTones();

    void updateWindowTitle();

    void generateLotsAll8x8();
    void generateLotsSelected8x8();
    void exportModAll8x8();
    void generateLotSettingsChanged();

    void overwriteSpawnMap_AllCells_256();
    void overwriteSpawnMap_SelectedCells_256();

    void BMPToTMXAll();
    void BMPToTMXSelected();

    void TMXToBMPAll();
    void TMXToBMPSelected();

    void resizeWorld();

    void preferencesDialog();
    void keyboardShortcuts();

    void objectGroupsDialog();
    void objectTypesDialog();
    void propertyEnumsDialog();
    void properyDefinitionsDialog();
    void templatesDialog();

    void copy();
    void paste();
    void showClipboard();

    void removeRoad();
    void removeBMP();

    void removeLot();
    void removeObject();
    void splitObjectPolygon();
    void extractLots();
    void extractObjects();
    void clearCells();
    void clearMapOnly();
    void removeEmptyBorderCells();
    void checkForHoles();

    void generateInGameMapBuildingFeatures();
    void generateInGameMapTreeFeatures();
    void generateInGameMapWaterFeatures();
    void generateInGameMapRoadFeatures();
    void removeInGameMapFeatures();
    void splitInGameMapPolygon();
    void convertInGameMapPolylineToPolygon();
    void addInGameMapHole();
    void removeInGameMapHole();
    void removeInGameMapPoint();
    void readInGameMapFeaturesXML();
    void createInGameMapFeatureImage();
    void createInGameMapImage();
    void createInGameMapImagePyramid();

    void setStatusBarCoords(int x, int y);

    void aboutToShowCurrentLevelMenu();
    void currentLevelMenuTriggered(QAction *action);

    void aboutToShowObjGrpMenu();
    void objGrpMenuTriggered(QAction *action);

    void lotpackviewer();

    void FromToAll();
    void FromToSelected();

    void BuildingsToPNG();
    void ZonesToPNG();

    void lootInspector();

    void generateBiomeMap();
    void terrainImageEditor();
    void worldGenPreview();
    void worldGenPrefabEditor();
    void tilesetCleanup();

    void readOldWaterDotLua();

private:
    QRect retainedWorldBounds(WorldDocument *worldDoc) const;
    void FromToAux(bool selectedOnly);

    void initActionManager();

    bool confirmSave();
    bool confirmAllSave();

    void writeSettings();
    void writeWindowSettings();

    void enableDeveloperFeatures();

    WorldDocument *currentWorldDocument();

    bool canSplitObjectPolygon();

    bool canSplitInGameMapPolygon();
    bool canRemoveInGameMapPoint();
    bool canAddInGameMapHole();
    bool canRemoveInGameMapHole();
    bool canConvertToInGameMapPolygon();
    void writeInGameMapFeaturesXML();
    void overwriteInGameMapFeaturesXML();

    struct ViewHint
    {
        qreal scale;
        int scrollX;
        int scrollY;
        bool valid;
    } mViewHint;
    void setDocumentViewHint(qreal scale, int scrollX, int scrollY)
    {
        mViewHint.scale = scale;
        mViewHint.scrollX = scrollX;
        mViewHint.scrollY = scrollY;
        mViewHint.valid = true;
    }

    void addWorldObjectsFromLuaTable(Lua::LuaTable *table);

private:
    Ui::MainWindow *ui;
    UndoDock *mUndoDock;
    LayersDock *mLayersDock;
    LotsDock *mLotsDock;
    MapsDock *mMapsDock;
    ObjectsDock *mObjectsDock;
    PropertiesDock *mPropertiesDock;
    SearchDock* mSearchDock;
    StreetNamesDock *mStreetNamesDock;
    InGameMapDock* mInGameMapDock;
#ifdef ROAD_UI
    RoadsDock *mRoadsDock;
#endif
    Document *mCurrentDocument;
    QComboBox *mZoomComboBox;
    QMenu *mCurrentLevelMenu;
    QMenu *mObjectGroupMenu;
    Zoomable *mZoomable;
    QSettings mSettings;
    LotPackWindow *mLotPackWindow;
    ActionManager *mActionManager = nullptr;
    KeyboardShortcutWindow *mKeyboardShortcutWindow = nullptr;
    QAction *mUndoAction = nullptr;
    QAction *mRedoAction = nullptr;
    QAction *mNightPreviewAction = nullptr;
    QToolButton *mNightPreviewButton = nullptr;
    QToolButton *mPoweredPreviewButton = nullptr;
    QToolButton *mSnowPreviewButton = nullptr;
    QToolButton *mJumboPreviewButton = nullptr;

    static MainWindow *mInstance;
};

#endif // MAINWINDOW_H
