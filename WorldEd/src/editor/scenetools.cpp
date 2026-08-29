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

#include "scenetools.h"

#include "basegraphicsview.h"
#include "biomemapimageprocessor.h"
#include "biomemapitem.h"
#include "bmptotmx.h"
#include "celldocument.h"
#include "cellscene.h"
#include "cellview.h"
#include "clipboard.h"
#include "expectedpropertiesdialog.h"
#include "mapcomposite.h"
#include "mapimagemanager.h"
#include "mapmanager.h"
#include "mainwindow.h"
#include "preferences.h"
#include "progress.h"
#include "spawntooldialog.h"
#include "thumbnailsettingsmgr.h"
#include "toolmanager.h"
#include "undoredo.h"
#include "world.h"
#include "worldcell.h"
#include "worldconstants.h"
#include "worlddocument.h"
#include "worldproperties.h"
#include "worldobjectvalidation.h"
#include "zoomable.h"

#include "../portablesettings.h"

#include "maprenderer.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtMath>
#include <QUndoStack>

using namespace Tiled;

namespace {
bool typeRequiresUnitRectangle(const QString &type)
{
    return type == QLatin1String("SpawnPoint")
            || type == QLatin1String("WaterFlow")
            || type == QLatin1String("RoomTone");
}

bool typeRequiresRectangle(const QString &type)
{
    return typeRequiresUnitRectangle(type)
            || type == QLatin1String("WaterZone");
}

void resolvedObjectProperties(PropertyHolder *holder, PropertyList &result)
{
    for (PropertyTemplate *propertyTemplate : holder->templates())
        resolvedObjectProperties(propertyTemplate, result);
    for (Property *property : holder->properties()) {
        result.removeAll(property->mDefinition);
        result.append(property);
    }
}

bool isBuildingPath(const QString &mapPath)
{
    return mapPath.endsWith(QLatin1String(".tbx"), Qt::CaseInsensitive);
}

QString editorNameForPath(const QString &mapPath)
{
    return isBuildingPath(mapPath)
            ? QStringLiteral("BuildingEd")
            : QStringLiteral("TileZed");
}

bool openInMapEditor(const QString &mapPath, QWidget *parent)
{
    const QString editorName = editorNameForPath(mapPath);
    const QString editorPath = QDir(QApplication::applicationDirPath())
            .absoluteFilePath(editorName + QLatin1String(".exe"));
    if (!QFileInfo::exists(editorPath)) {
        QMessageBox::warning(parent,
                             QObject::tr("%1 not found").arg(editorName),
                             QObject::tr("%1.exe was not found next to PZWorldEd.exe:\n%2")
                             .arg(editorName, editorPath));
        return false;
    }
    if (!QProcess::startDetached(editorPath, QStringList() << mapPath,
                                 QFileInfo(mapPath).absolutePath())) {
        QMessageBox::warning(parent, QObject::tr("Unable to open map"),
                             QObject::tr("%1 could not be started for:\n%2")
                             .arg(editorName, mapPath));
        return false;
    }
    return true;
}

QString chooseBasementAccess(QWidget *parent, const QString &currentAccess)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Choose Basement Access"));
    dialog.resize(920, 620);

    QVBoxLayout layout(&dialog);
    QLineEdit filter(&dialog);
    filter.setPlaceholderText(QObject::tr("Filter basement accesses..."));
    layout.addWidget(&filter);

    QSplitter splitter(Qt::Horizontal, &dialog);
    QListWidget accessList(&splitter);
    accessList.setAlternatingRowColors(true);
    accessList.setUniformItemSizes(true);
    QWidget previewPanel(&splitter);
    QVBoxLayout previewLayout(&previewPanel);
    QLabel preview(&previewPanel);
    preview.setAlignment(Qt::AlignCenter);
    preview.setMinimumSize(420, 420);
    preview.setFrameShape(QFrame::StyledPanel);
    QLabel details(&previewPanel);
    details.setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    details.setTextInteractionFlags(Qt::TextSelectableByMouse);
    details.setWordWrap(true);
    previewLayout.addWidget(&preview, 1);
    previewLayout.addWidget(&details);
    splitter.addWidget(&accessList);
    splitter.addWidget(&previewPanel);
    splitter.setStretchFactor(0, 2);
    splitter.setStretchFactor(1, 3);
    layout.addWidget(&splitter, 1);

    QDialogButtonBox buttons(QDialogButtonBox::Ok |
                             QDialogButtonBox::Cancel,
                             Qt::Horizontal, &dialog);
    QPushButton *okButton = buttons.button(QDialogButtonBox::Ok);
    okButton->setEnabled(false);
    layout.addWidget(&buttons);

    QMap<QString, QFileInfo> sources;
    const QDir sourceRoot(PortableSettings::basementSourcePath());
    if (sourceRoot.exists()) {
        QDirIterator iterator(sourceRoot.absolutePath(),
                              QStringList() << QStringLiteral("*.tbx")
                                            << QStringLiteral("*.tmx"),
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QFileInfo info(iterator.next());
            const QString key = info.completeBaseName().toCaseFolded();
            if (!sources.contains(key) ||
                    info.suffix().compare(QStringLiteral("tbx"),
                                          Qt::CaseInsensitive) == 0)
                sources.insert(key, info);
        }
    }
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        QListWidgetItem *listItem = new QListWidgetItem(
                    it.value().completeBaseName(), &accessList);
        listItem->setData(Qt::UserRole, it.value().absoluteFilePath());
        listItem->setToolTip(QDir::toNativeSeparators(
                                 it.value().absoluteFilePath()));
    }
    accessList.sortItems(Qt::AscendingOrder);

    const auto refreshPreview = [&]() {
        MapImageManager::instance()->releaseOwner(&dialog);
        QListWidgetItem *listItem = accessList.currentItem();
        okButton->setEnabled(listItem != nullptr);
        if (!listItem) {
            preview.clear();
            details.setText(sources.isEmpty()
                            ? QObject::tr("No TBX or TMX access was found in:\n%1")
                              .arg(QDir::toNativeSeparators(
                                       sourceRoot.absolutePath()))
                            : QString());
            return;
        }
        const QString path = listItem->data(Qt::UserRole).toString();
        MapInfo *mapInfo = MapManager::instance()->loadMap(path);
        if (!mapInfo || !mapInfo->isValid()) {
            preview.setText(QObject::tr("Preview unavailable"));
            details.setText(QDir::toNativeSeparators(path));
            return;
        }
        MapImage *mapImage = MapImageManager::instance()->getMapImage(
                    path, QString(), &dialog);
        if (mapImage && !mapImage->image().isNull()) {
            preview.setPixmap(QPixmap::fromImage(mapImage->image()).scaled(
                                  preview.size() - QSize(12, 12),
                                  Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation));
            listItem->setIcon(QIcon(QPixmap::fromImage(
                                        mapImage->image()).scaled(
                                        QSize(64, 64),
                                        Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation)));
        } else {
            preview.setText(QObject::tr("Loading preview..."));
        }
        details.setText(QObject::tr("%1\n%2 x %3 tiles")
                        .arg(QDir::toNativeSeparators(path))
                        .arg(mapInfo->width()).arg(mapInfo->height()));
    };

    QObject::connect(&accessList, &QListWidget::currentItemChanged,
                     &dialog, [&](QListWidgetItem *, QListWidgetItem *) {
        refreshPreview();
    });
    QObject::connect(&filter, &QLineEdit::textChanged,
                     &dialog, [&](const QString &text) {
        QListWidgetItem *firstVisible = nullptr;
        for (int index = 0; index < accessList.count(); ++index) {
            QListWidgetItem *listItem = accessList.item(index);
            const bool visible = listItem->text().contains(
                        text.trimmed(), Qt::CaseInsensitive);
            listItem->setHidden(!visible);
            if (visible && !firstVisible)
                firstVisible = listItem;
        }
        if (!accessList.currentItem() ||
                accessList.currentItem()->isHidden())
            accessList.setCurrentItem(firstVisible);
    });
    QObject::connect(MapImageManager::instance(),
                     &MapImageManager::mapImageChanged,
                     &dialog, [&](MapImage *mapImage) {
        QListWidgetItem *listItem = accessList.currentItem();
        if (!listItem || !mapImage || !mapImage->mapInfo())
            return;
        if (QFileInfo(listItem->data(Qt::UserRole).toString()) !=
                QFileInfo(mapImage->mapInfo()->path()))
            return;
        refreshPreview();
    });
    QObject::connect(&buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    QObject::connect(&accessList, &QListWidget::itemDoubleClicked,
                     &dialog, [&](QListWidgetItem *) {
        dialog.accept();
    });

    const QString currentBaseName =
            QFileInfo(currentAccess.trimmed()).completeBaseName();
    for (int index = 0; index < accessList.count(); ++index) {
        if (accessList.item(index)->text().compare(
                    currentBaseName, Qt::CaseInsensitive) == 0) {
            accessList.setCurrentRow(index);
            break;
        }
    }
    if (!accessList.currentItem() && accessList.count() > 0)
        accessList.setCurrentRow(0);
    refreshPreview();
    filter.setFocus();
    if (dialog.exec() != QDialog::Accepted || !accessList.currentItem())
        return QString();
    return accessList.currentItem()->text();
}

ObjectItem *contextObjectItemAt(CellScene *scene, const QPointF &scenePos)
{
    QTransform transform;
    if (!scene->views().isEmpty())
        transform = scene->views().first()->viewportTransform();
    for (QGraphicsItem *graphicsItem : scene->items(
             scenePos, Qt::IntersectsItemShape,
             Qt::DescendingOrder, transform)) {
        ObjectItem *objectItem = dynamic_cast<ObjectItem *>(graphicsItem);
        if (objectItem && !objectItem->isAdjacent())
            return objectItem;
    }
    return nullptr;
}

enum class ObjectContextChoice
{
    None,
    Properties,
    BasementAccess,
    Remove
};

ObjectContextChoice objectContextChoice(
        QAction *action, QAction *propertiesAction,
        QAction *accessAction, QAction *removeAction)
{
    if (!action)
        return ObjectContextChoice::None;
    if (propertiesAction && action == propertiesAction)
        return ObjectContextChoice::Properties;
    if (accessAction && action == accessAction)
        return ObjectContextChoice::BasementAccess;
    if (removeAction && action == removeAction)
        return ObjectContextChoice::Remove;
    return ObjectContextChoice::None;
}

QString removeObjectActionText(WorldCellObject *object)
{
    if (object->isRoomTone())
        return QObject::tr("Remove Room Tone");
    if (object->isSpawnPoint())
        return QObject::tr("Remove Spawn Point");
    return QObject::tr("Remove Object");
}

void showObjectContextMenu(CellScene *scene, ObjectItem *item,
                           const QPoint &screenPos)
{
    if (!item || !item->object())
        return;

    WorldCellObject *object = item->object();
    scene->document()->setSelectedObjects(
                WorldCellObjectList() << object);

    QMenu menu;
    QAction *propertiesAction = nullptr;
    if (ExpectedPropertiesDialog::canEdit(object)) {
        propertiesAction = menu.addAction(
                    QIcon(QLatin1String(
                              ":images/16x16/document-properties.png")),
                    QObject::tr("Edit %1 Properties...")
                    .arg(object->type()->name()));
    }

    QAction *accessAction = nullptr;
    if (WorldObjectValidation::supportsBasementAccess(object)) {
        accessAction = menu.addAction(
                    QIcon(QLatin1String(":images/tiled-icon-16.png")),
                    QObject::tr("Choose Basement Access..."));
    }

    if (propertiesAction || accessAction)
        menu.addSeparator();
    QAction *removeAction = menu.addAction(
                QIcon(QLatin1String(":images/16x16/edit-delete.png")),
                removeObjectActionText(object));

    const ObjectContextChoice choice = objectContextChoice(
                menu.exec(screenPos), propertiesAction,
                accessAction, removeAction);
    if (choice == ObjectContextChoice::Properties) {
        ExpectedPropertiesDialog::edit(
                    scene->worldDocument(), object,
                    MainWindow::instance());
    } else if (choice == ObjectContextChoice::BasementAccess) {
        PropertyDef *definition = scene->world()->propertyDefinition(
                    QStringLiteral("Access"));
        Property *property = definition
                ? object->properties().find(definition) : nullptr;
        QString currentAccess;
        if (property)
            currentAccess = property->mValue;
        else if (definition) {
            PropertyList resolved;
            resolvedObjectProperties(object, resolved);
            Property *resolvedProperty = resolved.find(definition);
            if (resolvedProperty)
                currentAccess = resolvedProperty->mValue;
        }
        const QString selectedAccess = chooseBasementAccess(
                    scene->views().isEmpty()
                    ? static_cast<QWidget *>(MainWindow::instance())
                    : scene->views().first(), currentAccess);
        if (!selectedAccess.isEmpty() && definition) {
            if (property)
                scene->worldDocument()->setPropertyValue(
                            object, property, selectedAccess);
            else
                scene->worldDocument()->addProperty(
                            object, QStringLiteral("Access"),
                            selectedAccess);
        }
    } else if (choice == ObjectContextChoice::Remove) {
        scene->worldDocument()->removeCellObject(
                    scene->cell(), object->index());
    }
}
}
/////

AbstractTool::AbstractTool(const QString &name, const QIcon &icon,
                           const QKeySequence &shortcut, ToolType type,
                           QObject *parent)
    : QObject(parent)
    , mName(name)
    , mIcon(icon)
    , mShortcut(shortcut)
    , mEnabled(false)
    , mType(type)
{
}

void AbstractTool::setStatusInfo(const QString &statusInfo)
{
    if (mStatusInfo == statusInfo)
        return;
    mStatusInfo = statusInfo;
    emit statusInfoChanged(mStatusInfo);
}
void AbstractTool::setEnabled(bool enabled)
{
    if (mEnabled == enabled)
        return;

    mEnabled = enabled;
    emit enabledChanged(mEnabled);
}

bool AbstractTool::isCurrent() const
{
    return ToolManager::instance()->selectedTool() == this;
}

BaseWorldSceneTool *AbstractTool::asWorldTool()
{
    return isWorldTool() ? static_cast<BaseWorldSceneTool*>(this) : 0;
}

BaseCellSceneTool *AbstractTool::asCellTool()
{
    return isCellTool() ? static_cast<BaseCellSceneTool*>(this) : 0;
}

/////

BaseCellSceneTool::BaseCellSceneTool(const QString &name, const QIcon &icon, const QKeySequence &shortcut, QObject *parent)
    : AbstractTool(name, icon, shortcut, CellToolType, parent)
    , mScene(0)
    , mEventView(0)
{

}

BaseCellSceneTool::~BaseCellSceneTool()
{
}

void BaseCellSceneTool::setScene(BaseGraphicsScene *scene)
{
    mScene = scene ? scene->asCellScene() : 0;
}

BaseGraphicsScene *BaseCellSceneTool::scene() const
{
    return mScene;
}

void BaseCellSceneTool::updateEnabledState()
{
    setEnabled(mScene != 0);
}

void BaseCellSceneTool::setEventView(BaseGraphicsView *view)
{
    mEventView = view;
}

/////

CreateObjectTool *CreateObjectTool::mInstance = 0;

CreateObjectTool *CreateObjectTool::instance()
{
    if (!mInstance)
        mInstance = new CreateObjectTool();
    return mInstance;
}

void CreateObjectTool::deleteInstance()
{
    delete mInstance;
}

CreateObjectTool::CreateObjectTool()
    : BaseCellSceneTool(QLatin1String("Create Object"),
                        QIcon(QLatin1String(":/images/24x24/insert-object.png")),
                        QKeySequence(QLatin1String("O")))
    , mItem(0)
{
}

void CreateObjectTool::activate()
{
    if (!mScene)
        return;
    WorldObjectGroup *group = mScene->document()->currentObjectGroup();
    QSettings settings;
    mObjectName = settings.value(
                QLatin1String("ObjectCreation/Name")).toString();
    mObjectTypeName = settings.value(
                QLatin1String("ObjectCreation/Type")).toString();
    const QList<WorldCellObject*> selected =
            mScene->document()->selectedObjects();
    if (selected.size() == 1) {
        if (!selected.first()->name().isEmpty())
            mObjectName = selected.first()->name();
        if (selected.first()->type()
                && !selected.first()->type()->isNull()) {
            mObjectTypeName = selected.first()->type()->name();
        }
    }
    if (group) {
        if (mObjectName.isEmpty())
            mObjectName = group->name();
        if ((mObjectTypeName.isEmpty()
             || !mScene->world()->objectTypes().contains(mObjectTypeName))
                && group->type()) {
            mObjectTypeName = group->type()->name();
        }
    }
    editObjectPreset();
}
void CreateObjectTool::editObjectPreset()
{
    QDialog dialog(MainWindow::instance());
    dialog.setWindowTitle(tr("New Object Defaults"));
    QFormLayout layout(&dialog);
    QLineEdit nameEdit(mObjectName, &dialog);
    QComboBox typeCombo(&dialog);
    QStringList typeNames = mScene->world()->objectTypes().names();
    typeNames.removeAll(QString());
    typeCombo.addItems(typeNames);
    const int typeIndex = typeCombo.findText(mObjectTypeName);
    if (typeIndex >= 0)
        typeCombo.setCurrentIndex(typeIndex);
    layout.addRow(tr("Name:"), &nameEdit);
    layout.addRow(tr("Type:"), &typeCombo);
    QDialogButtonBox buttons(QDialogButtonBox::Ok
                             | QDialogButtonBox::Cancel,
                             Qt::Horizontal, &dialog);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted,
            &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected,
            &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    mObjectName = nameEdit.text().trimmed();
    mObjectTypeName = typeCombo.currentText();
    QSettings settings;
    settings.setValue(QLatin1String("ObjectCreation/Name"), mObjectName);
    settings.setValue(QLatin1String("ObjectCreation/Type"),
                      mObjectTypeName);
}
void CreateObjectTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
#if 1
        mStartScenePos = event->scenePos();
        mMousePressed = true;
        mAnchorPos = mScene->renderer()->pixelToTileCoordsInt(event->scenePos(), mScene->document()->currentLevel());
        QString typeName = mObjectTypeName;
        if (typeName.isEmpty()) {
            WorldObjectGroup *group =
                    mScene->document()->currentObjectGroup();
            if (group && group->type())
                typeName = group->type()->name();
        }
        if (typeRequiresUnitRectangle(typeName))
            startNewMapObject(mAnchorPos);
#else
        mAnchorPos = mScene->renderer()->pixelToTileCoords(event->scenePos(), mScene->document()->currentLevel());

        bool snapToGrid = Preferences::instance()->snapToGrid();
        if (event->modifiers() & Qt::ControlModifier)
            snapToGrid = !snapToGrid;
        if (snapToGrid)
            mAnchorPos = mAnchorPos.toPoint();

        startNewMapObject(mAnchorPos);
#endif
        event->accept();
    }

    if (event->button() == Qt::RightButton) {
        if (mItem) {
            cancelNewMapObject();
            event->accept();
        } else if (ObjectItem *item = contextObjectItemAt(
                       mScene, event->scenePos())) {
            showObjectContextMenu(mScene, item, event->screenPos());
            event->accept();
        }
        mMousePressed = false;
    }
}

void CreateObjectTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (mItem) {
        if (WorldObjectValidation::requiresUnitRectangle(
                    mItem->object())) {
            mItem->object()->setPos(mAnchorPos);
            mItem->object()->setWidth(1);
            mItem->object()->setHeight(1);
            mItem->synchWithObject();
            event->accept();
            return;
        }
#if 1
        QPointF pos = mScene->renderer()->pixelToTileCoordsInt(event->scenePos(), mScene->document()->currentLevel());
#else
        QPointF pos = mScene->renderer()->pixelToTileCoords(event->scenePos(), mScene->document()->currentLevel());

        bool snapToGrid = Preferences::instance()->snapToGrid();
        if (event->modifiers() & Qt::ControlModifier)
            snapToGrid = !snapToGrid;
        if (snapToGrid)
            pos = pos.toPoint();
#endif

        QRectF bounds(mAnchorPos, pos);
        bounds = bounds.normalized();
        mItem->object()->setPos(bounds.topLeft());
        mItem->object()->setWidth(qMax(MIN_OBJECT_SIZE, bounds.width() + 1));
        mItem->object()->setHeight(qMax(MIN_OBJECT_SIZE, bounds.height() + 1));
        mItem->synchWithObject();
        event->accept();
        return;
    }
    if (mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            startNewMapObject(mAnchorPos);
        }
        event->accept();
    }
}

void CreateObjectTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton) && mItem) {
        if ((mItem->object()->width() < MIN_OBJECT_SIZE) || (mItem->object()->height() < MIN_OBJECT_SIZE))
            cancelNewMapObject();
        else {
            WorldCellObject *obj = mItem->object();
            finishNewMapObject();
            mScene->document()->setSelectedObjects(
                        QList<WorldCellObject*>() << obj);
        }
        event->accept();
    }
    mMousePressed = false;
}

void CreateObjectTool::startNewMapObject(const QPointF &pos)
{
    WorldObjectGroup *og = mScene->document()->currentObjectGroup();
    ObjectType *objectType =
            mScene->world()->objectTypes().find(mObjectTypeName);
    if (!objectType)
        objectType = og->type();
    QString objectName = mObjectName;
    if (objectName.isEmpty() && objectType)
        objectName = objectType->name();
    WorldCellObject *obj = new WorldCellObject(mScene->cell(),
                                               objectName, objectType, og,
                                               pos.x(), pos.y(),
                                               mScene->document()->currentLevel(),
                                               MIN_OBJECT_SIZE, MIN_OBJECT_SIZE);
#if 1
    if (obj->isBasement()) {
        if (PropertyTemplate *pt = mScene->world()->propertyTemplate(QStringLiteral("Basement"))) {
            obj->addTemplate(obj->templates().size(), pt);
            for (Property *property : pt->properties()) {
                PropertyDef *pd = mScene->world()->propertyDefinition(property->mDefinition->mName);
                obj->addProperty(obj->properties().size(), new Property(pd, pd->mDefaultValue));
            }
        }
    }
#endif
    WorldObjectValidation::applyCreationDefaults(obj);
    mItem = mScene->newObjectItem(obj, nullptr);
    mItem->labelItem()->setShowSize(true);
    mItem->setZValue(10000);
    mScene->addItem(mItem);
}

WorldCellObject *CreateObjectTool::clearNewMapObjectItem()
{
    WorldCellObject *obj = mItem->object();
    mScene->removeItem(mItem);
    delete mItem;
    mItem = nullptr;
    return obj;
}

void CreateObjectTool::cancelNewMapObject()
{
    WorldCellObject *obj = clearNewMapObjectItem();
    delete obj;
}

void CreateObjectTool::finishNewMapObject()
{
    WorldCellObject *obj = clearNewMapObjectItem();
    WorldObjectValidation::applyCreationDefaults(obj);
    mScene->worldDocument()->addCellObject(mScene->cell(),
                                           mScene->cell()->objects().size(),
                                           obj);
}

/////

SelectMoveObjectTool *SelectMoveObjectTool::mInstance = nullptr;

SelectMoveObjectTool *SelectMoveObjectTool::instance()
{
    if (!mInstance)
        mInstance = new SelectMoveObjectTool();
    return mInstance;
}

void SelectMoveObjectTool::deleteInstance()
{
    delete mInstance;
}

bool SelectMoveObjectTool::validateContextMenuDispatch(QString *error)
{
    QAction propertiesAction(nullptr);
    QAction accessAction(nullptr);
    QAction removeAction(nullptr);
    QAction unrelatedAction(nullptr);

    if (objectContextChoice(nullptr, &propertiesAction, nullptr,
                            &removeAction) != ObjectContextChoice::None
            || objectContextChoice(&propertiesAction, &propertiesAction,
                                   nullptr, &removeAction)
               != ObjectContextChoice::Properties
            || objectContextChoice(&accessAction, &propertiesAction,
                                   &accessAction, &removeAction)
               != ObjectContextChoice::BasementAccess
            || objectContextChoice(&removeAction, &propertiesAction,
                                   nullptr, &removeAction)
               != ObjectContextChoice::Remove
            || objectContextChoice(&unrelatedAction, &propertiesAction,
                                   nullptr, &removeAction)
               != ObjectContextChoice::None) {
        if (error)
            *error = tr("Object context-menu action dispatch is invalid");
        return false;
    }
    return true;
}

void SelectMoveObjectTool::deactivate()
{
    if (mResizeHandle) {
        delete mResizeHandle;
        mResizeHandle = nullptr;
    }
    BaseCellSceneTool::deactivate();
}

void SelectMoveObjectTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene) {
        mScene->worldDocument()->disconnect(this);
    }

    BaseCellSceneTool::setScene(scene);

    if (mScene) {
        connect(mScene->worldDocument(), &WorldDocument::cellObjectAboutToBeRemoved,
                this, &SelectMoveObjectTool::cellObjectAboutToBeRemoved);
        connect(mScene->worldDocument(), &WorldDocument::cellObjectMoved,
                this, &SelectMoveObjectTool::cellObjectMoved);
        connect(mScene->worldDocument(), &WorldDocument::cellObjectResized,
                this, &SelectMoveObjectTool::cellObjectResized);
    }
}

SelectMoveObjectTool::SelectMoveObjectTool()
    : BaseCellSceneTool(QLatin1String("Select and Move Objects"),
                        QIcon(QLatin1String(":/images/22x22/tool-select-objects.png")),
                        QKeySequence(QLatin1String("S")))
    , mMode(NoMode)
    , mMousePressed(false)
    , mClickedItem(nullptr)
    , mResizeHandle(nullptr)
{
}

void SelectMoveObjectTool::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (mMode == Moving) {
            foreach (ObjectItem *item, mMovingItems)
                item->setDragOffset(QPointF());
            mMovingItems.clear();
            mMode = CancelMoving;
            event->accept();
        }
    }
}

void SelectMoveObjectTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{

    switch (event->button()) {
    case Qt::LeftButton:
        if (mMode != NoMode) // Ignore additional presses during select/move
            return;
        mMousePressed = true;
        mStartScenePos = event->scenePos();
        mClickedItem = topmostItemAt(mStartScenePos);
        event->accept();
        break;
    case Qt::RightButton:
        // Right-clicks exits moving, same as the Escape key.
        if (mMode == Moving) {
            foreach (ObjectItem *item, mMovingItems)
                item->setDragOffset(QPointF());
            mMovingItems.clear();
            mMode = CancelMoving;
            event->accept();
        }
        break;
    default:
        break;
    }
}

void SelectMoveObjectTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (mMode == NoMode && mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (mClickedItem)
                startMoving();
            else
                startSelecting();
        }
    }

    switch (mMode) {
    case Selecting:
//        mSelectionRectangle->setRectangle(QRectF(mStart, pos).normalized());
        break;
    case Moving:
        updateMovingItems(event->scenePos(), event->modifiers());
        break;
    case NoMode:
    {
        ObjectItem *hoverItem = topmostItemAt(event->scenePos(), true);
        if ((hoverItem != nullptr) && (hoverItem->object()->isRectangle() == false)) {
            hoverItem = nullptr;
        }
        CellObjectEdgeResizeHandle::Edge edge = CellObjectEdgeResizeHandle::pickEdge(hoverItem, event->scenePos());
        if (edge != CellObjectEdgeResizeHandle::Edge::NONE) {
            if (mResizeHandle) {
                if (mResizeHandle->object() != hoverItem->object()) {
                    mResizeHandle->setObject(hoverItem->object());
                }
                if (mResizeHandle->edge() != edge) {
                    mResizeHandle->setEdge(edge);
                }
            } else {
                mResizeHandle = new CellObjectEdgeResizeHandle(mScene, hoverItem->object(), edge);
                mResizeHandle->setZValue(CellScene::ZVALUE_GRID + 1);
                mScene->addItem(mResizeHandle);
            }
        } else {
            if (mResizeHandle) {
                delete mResizeHandle;
                mResizeHandle = nullptr;
            }
        }
        break;
    }
    case CancelMoving:
        break;
    }

    if (mMode != NoMode)
        event->accept();
}

void SelectMoveObjectTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        if (mMode == NoMode) {
            showContextMenu(event->scenePos(), event->screenPos());
            event->accept();
        }
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
        if (mClickedItem) {
            QSet<ObjectItem*> selection = mScene->selectedObjectItems();
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier)) {
                if (selection.contains(mClickedItem))
                    selection.remove(mClickedItem);
                else
                    selection.insert(mClickedItem);
            } else {
                selection.clear();
                selection.insert(mClickedItem);
            }
            mScene->setSelectedObjectItems(selection);
        } else {
            mScene->setSelectedObjectItems(QSet<ObjectItem*>());
        }
        break;
    case Selecting:
//        updateSelection(event->scenePos(), event->modifiers());
//        mapScene()->removeItem(mSelectionRectangle);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case CancelMoving:
        mMode = NoMode;
        break;
    }

    mMousePressed = false;
    mClickedItem = nullptr;
}

void SelectMoveObjectTool::cellObjectAboutToBeRemoved(WorldCell *cell, int objectIndex)
{
    if (mResizeHandle == nullptr)
        return;
    if (cell->objects().at(objectIndex) == mResizeHandle->object()) {
        delete mResizeHandle;
        mResizeHandle = nullptr;
    }
}

void SelectMoveObjectTool::cellObjectMoved(WorldCellObject *object)
{
    if (mResizeHandle == nullptr)
        return;
    if (mResizeHandle->object() != object)
        return;
    mResizeHandle->synchWithObject();
}

void SelectMoveObjectTool::cellObjectResized(WorldCellObject *object)
{
    if (mResizeHandle == nullptr)
        return;
    if (mResizeHandle->object() != object)
        return;
    mResizeHandle->synchWithObject();
}

void SelectMoveObjectTool::startSelecting()
{
    mMode = Selecting;
//    mScene->addItem(mSelectionRectangle);
}

void SelectMoveObjectTool::startMoving()
{
    mMovingItems = mScene->selectedObjectItems();

    // Move only the clicked item, if it was not part of the selection
    if (!mMovingItems.contains(mClickedItem)) {
        mMovingItems.clear();
        mMovingItems.insert(mClickedItem);
        mScene->setSelectedObjectItems(mMovingItems);
    }

    mMode = Moving;
}

void SelectMoveObjectTool::updateMovingItems(const QPointF &pos,
                                   Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)
    MapRenderer *renderer = mScene->renderer();

    // When dragging multiple items, snap the top-left corner of the
    // collective bounding rectangle so the items stay in the same
    // position relative to each other.
    int level = mScene->document()->currentLevel();
    WorldCellObject *obj = mClickedItem->object();
    QRectF allBounds = obj->bounds().translated((level - obj->level()) * QPoint(3,3)); // Assumes LevelIsometric renderer
    foreach (ObjectItem *item, mMovingItems) {
        obj = item->object();
        allBounds |= obj->bounds().translated((level - obj->level()) * QPoint(3,3)); // Assumes LevelIsometric renderer
    }

    QPointF startTilePos = allBounds.topLeft();
    QPointF startScenePos = renderer->tileToPixelCoords(startTilePos, level);
    QPointF newTilePos = renderer->pixelToTileCoords(startScenePos + (pos - mStartScenePos), level);

#if 1
    newTilePos = newTilePos.toPoint();
#else
    bool snapToGrid = Preferences::instance()->snapToGrid();
    if (modifiers & Qt::ControlModifier)
        snapToGrid = !snapToGrid;
    if (snapToGrid)
        newTilePos = newTilePos.toPoint();
#endif

    foreach (ObjectItem *item, mMovingItems)
        item->setDragOffset(newTilePos - startTilePos);
}

void SelectMoveObjectTool::finishMoving(const QPointF &pos)
{
    Q_UNUSED(pos)
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    if (!mClickedItem->dragOffset().isNull()) {
        QUndoStack *undoStack = mScene->document()->undoStack();
        undoStack->beginMacro(tr("Move %n Object(s)", "", mMovingItems.size()));
        foreach (ObjectItem *item, mMovingItems) {
            QPointF tilePos = item->tileBounds().topLeft();
            item->setDragOffset(QPointF());
            mScene->worldDocument()->moveCellObject(item->object(), tilePos);
        }
        undoStack->endMacro();
    }

    mMovingItems.clear();
}

void SelectMoveObjectTool::showContextMenu(const QPointF &scenePos, const QPoint &screenPos)
{
    showObjectContextMenu(mScene,
                          contextObjectItemAt(mScene, scenePos),
                          screenPos);
}

ObjectItem *SelectMoveObjectTool::topmostItemAt(const QPointF &scenePos, bool editable)
{
    // ObjectLabelItem uses ItemIgnoresTransformations to keep its size the
    // same regardless of the view's scale.
    QTransform xform = mScene->views().at(0)->viewportTransform();
    foreach (QGraphicsItem *item, mScene->items(scenePos,
                                                Qt::IntersectsItemShape,
                                                Qt::DescendingOrder, xform)) {
        if (ObjectItem *objectItem = dynamic_cast<ObjectItem*>(item)) {
            if (editable && objectItem->isEditable() == false)
                continue;
            if (!objectItem->isAdjacent())
                return objectItem;
        }

        if (ObjectLabelItem *labelItem = dynamic_cast<ObjectLabelItem*>(item)) {
            if (editable)
                continue;
            if (!labelItem->objectItem()->isAdjacent())
                return labelItem->objectItem();
        }
    }
    return 0;
}

/////

SubMapTool *SubMapTool::mInstance = 0;

SubMapTool *SubMapTool::instance()
{
    if (!mInstance)
        mInstance = new SubMapTool();
    return mInstance;
}

void SubMapTool::deleteInstance()
{
    delete mInstance;
}

SubMapTool::SubMapTool()
    : BaseCellSceneTool(QLatin1String("Select and Move Lots"),
                        QIcon(QLatin1String(":/images/22x22/stock-tool-rect-select.png")),
                        QKeySequence())
    , mMode(NoMode)
    , mMousePressed(false)
    , mClickedItem(0)
    , mMapHighlightItem(new QGraphicsPolygonItem)
    , mHighlightedMap(0)
{
    mMapHighlightItem->setPen(Qt::NoPen);
    mMapHighlightItem->setBrush(QColor(128, 128, 128, 128));
    mMapHighlightItem->setZValue(CellScene::ZVALUE_GRID - 1);
}

void SubMapTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene) {
        mScene->worldDocument()->disconnect(this);
    }
    mScene = scene ? scene->asCellScene() : nullptr;
    if (mScene) {
        connect(mScene->worldDocument(), &WorldDocument::cellLotAboutToBeRemoved, this, &SubMapTool::cellLotAboutToBeRemoved);
    }
}


void SubMapTool::activate()
{
    mScene->addItem(mMapHighlightItem);
}

void SubMapTool::deactivate()
{
    mScene->removeItem(mMapHighlightItem);
}

void SubMapTool::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (mMode == Moving) {
            cancelMoving();
            mMode = CancelMoving;
            event->accept();
        }
    }
}

void SubMapTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        if (mMode != NoMode) // Ignore additional presses during select/move
            return;
        mMousePressed = true;
        mStartScenePos = event->scenePos();
        mClickedItem = topmostItemAt(mStartScenePos);
        event->accept();
        break;
    case Qt::RightButton:
        if (mMode == NoMode) {
            showContextMenu(event->scenePos(), event->screenPos());
            event->accept();
        }
        // Right-clicks exits moving, same as the Escape key.
        if (mMode == Moving) {
            cancelMoving();
            mMode = CancelMoving;
            event->accept();
        }
        break;
    default:
        break;
    }
}

static MapComposite *mapUnderPoint(CellScene *scene, MapComposite *mc, MapRenderer *renderer,
                                   const QPointF &scenePos)
{
    for (int i = mc->subMaps().size() - 1; i >= 0; i--) {
        MapComposite *subMap = mc->subMaps().at(i);
        if (subMap->isAdjacentMap()) {
            MapComposite *subSubMap = mapUnderPoint(scene, subMap, renderer, scenePos);
            if (subSubMap) {
                subMap = subSubMap;
            }
        }

        bool ignore = false;
        QList<SubMapItem*> items = scene->subMapItemsUsingMapInfo(subMap->mapInfo());
        for (SubMapItem *item : qAsConst(items)) {
            if (item->subMap() == subMap) {
                ignore = true;
                break;
            }
        }
        if (ignore) {
            continue;
        }

        if (Preferences::instance()->highlightCurrentLevel() && subMap->levelRecursive() > scene->document()->currentLevel()) {
            continue;
        }

        QRect tileBounds = subMap->mapInfo()->bounds().translated(subMap->originRecursive());
        QPolygonF scenePolygon = renderer->tileToPixelCoords(tileBounds);
        if (scenePolygon.containsPoint(scenePos, Qt::WindingFill)) {
            return subMap;
        }
    }
    return nullptr;
}

void SubMapTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    // Do mouse-over highlighting of Lots that are baked into a map.
    SubMapItem *item = topmostItemAt(event->scenePos());
    MapComposite *highlight = nullptr;
    if (!item && !mMousePressed) {
        if (MapComposite *mc = mapUnderPoint(mScene, mScene->mapComposite(), mScene->renderer(), event->scenePos())) {
            if (mc->isAdjacentMap()) {
                mc = nullptr;
            }
            highlight = mc;
        }
    }
    if (highlight != mHighlightedMap) {
        if (highlight) {
            QRect tileBounds = highlight->mapInfo()->bounds().translated(highlight->originRecursive());
            QPolygonF polygon = mScene->renderer()->tileToPixelCoords(tileBounds);
            mMapHighlightItem->setPolygon(polygon);
            mMapHighlightItem->setToolTip(QDir::toNativeSeparators(highlight->mapInfo()->path()));
        }
        mMapHighlightItem->setVisible(highlight != nullptr);
        mHighlightedMap = highlight;
    }

    if (mMode == NoMode && mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (mClickedItem) {
                startMoving();
            } else {
                startSelecting();
            }
        }
    }

    switch (mMode) {
    case Selecting:
//        mSelectionRectangle->setRectangle(QRectF(mStart, pos).normalized());
        break;
    case Moving:
        updateMovingItems(event->scenePos(), event->modifiers());
        break;
    case NoMode:
        break;
    case CancelMoving:
        break;
    }

    if (mMode != NoMode)
        event->accept();
}

void SubMapTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
        if (mClickedItem) {
            QSet<SubMapItem*> selection = mScene->selectedSubMapItems();
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier)) {
                if (selection.contains(mClickedItem))
                    selection.remove(mClickedItem);
                else
                    selection.insert(mClickedItem);
            } else {
                selection.clear();
                selection.insert(mClickedItem);
            }
            mScene->setSelectedSubMapItems(selection);
        } else {
            mScene->setSelectedSubMapItems(QSet<SubMapItem*>());
        }
        break;
    case Selecting:
//        updateSelection(event->scenePos(), event->modifiers());
//        mapScene()->removeItem(mSelectionRectangle);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case CancelMoving:
        mMode = NoMode;
        break;
    }

    mMousePressed = false;
    mClickedItem = nullptr;
}

void SubMapTool::cellLotAboutToBeRemoved(WorldCell *cell, int index)
{
    Q_UNUSED(cell)
    Q_UNUSED(index)
    if (mMapHighlightItem != nullptr) {
        mMapHighlightItem->setVisible(false);
    }
    mHighlightedMap = nullptr;
}

void SubMapTool::startSelecting()
{
    mMode = Selecting;
//    mScene->addItem(mSelectionRectangle);
}

void SubMapTool::startMoving()
{
    mMovingItems = mScene->selectedSubMapItems();

    // Move only the clicked item, if it was not part of the selection
    if (!mMovingItems.contains(mClickedItem)) {
        mMovingItems.clear();
        mMovingItems.insert(mClickedItem);
        mScene->setSelectedSubMapItems(mMovingItems);
    }

    mMode = Moving;

    foreach (SubMapItem *item, mMovingItems) {
        item->subMap()->setHiddenDuringDrag(true);
        DnDItem *dndItem = new DnDItem(
                    item->subMap()->mapInfo(), mScene->renderer(),
                    item->subMap()->levelOffset(), mScene->worldDocument());
        dndItem->setHotSpot(0, 0);
        mDnDItems.append(dndItem);
        dndItem->setZValue(10000);
        mScene->addItem(dndItem);
    }
}

void SubMapTool::updateMovingItems(const QPointF &pos,
                                   Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)
    MapRenderer *renderer = mScene->renderer();

    int i = 0;
    foreach (SubMapItem *item, mMovingItems) {
        int level = mScene->document()->currentLevel();
        QPoint startTilePos = renderer->pixelToTileCoordsInt(mStartScenePos, level);
        QPoint newTilePos = renderer->pixelToTileCoordsInt(pos, level);
        const QPoint diff = newTilePos - startTilePos;
        if (mDnDItems[i]) {
            mDnDItems[i]->setTilePosition(item->lot()->pos() + diff);
        }
        ++i;
    }
}

void SubMapTool::finishMoving(const QPointF &pos)
{
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    foreach (SubMapItem *item, mMovingItems)
        item->subMap()->setHiddenDuringDrag(false);

    int level = mScene->document()->currentLevel();
    QPoint startTilePos = mScene->renderer()->pixelToTileCoordsInt(mStartScenePos, level);
    QPoint endTilePos = mScene->renderer()->pixelToTileCoordsInt(pos, level);
    if (startTilePos != endTilePos) {
        QUndoStack *undoStack = mScene->document()->undoStack();
        undoStack->beginMacro(tr("Move %n Lots(s)", "", mMovingItems.size()));
        int i = 0;
        foreach (SubMapItem *item, mMovingItems) {
            DnDItem *dndItem = mDnDItems[i];
            mScene->worldDocument()->moveCellLot(item->lot(), dndItem->dropPosition());
            ++i;
        }
        undoStack->endMacro();
    }

    foreach (DnDItem *item, mDnDItems)
        mScene->removeItem(item);
    qDeleteAll(mDnDItems);
    mDnDItems.clear();
    mMovingItems.clear();
}

void SubMapTool::cancelMoving()
{
    foreach (SubMapItem *item, mMovingItems) {
        item->subMap()->setHiddenDuringDrag(false);
        item->update();
    }

    foreach (DnDItem *item, mDnDItems)
        mScene->removeItem(item);
    qDeleteAll(mDnDItems);
    mDnDItems.clear();
    mMovingItems.clear();
}

void SubMapTool::showContextMenu(const QPointF &scenePos, const QPoint &screenPos)
{
    SubMapItem *item = topmostItemAt(scenePos);
    if (!item) {
        MapComposite *subMap = mapUnderPoint(mScene,
                                             mScene->mapComposite(),
                                             mScene->renderer(),
                                             scenePos);
        QPoint tilePos(mScene->renderer()->pixelToTileCoordsInt(scenePos));
        if (!subMap && mScene->mapComposite()->mapInfo()->bounds().contains(tilePos))
            subMap = mScene->mapComposite();
        if (subMap) {
            QMenu menu;
            QIcon tiledIcon(QLatin1String(":images/tiled-icon-16.png"));
            const QString mapPath = subMap->mapInfo()->path();
            QAction *openAction = menu.addAction(
                        tiledIcon,
                        tr("Open in %1").arg(editorNameForPath(mapPath)));
            QAction *action = menu.exec(screenPos);
            if (action == openAction)
                openInMapEditor(mapPath, mScene->views().value(0));
        }
        return;
    }

    QMenu menu;
    QIcon lightIcon(QLatin1String(":/images/idea.png"));
    QAction *lightbulbRoomAction = nullptr;
    QString roomName;
    if (LightSwitchOverlay *overlay = topmostSwitchAt(scenePos)) {
        roomName = overlay->mRoomName;
    } else
        roomName = mScene->roomNameAt(scenePos);
    if (roomName.length()) {
        if (LightbulbsMgr::instance().rooms().contains(roomName))
            lightbulbRoomAction = menu.addAction(lightIcon,
                                                 tr("Show lights in rooms called %1").arg(roomName));
        else
            lightbulbRoomAction = menu.addAction(lightIcon,
                                                 tr("Hide lights in rooms called %1").arg(roomName));
    }
    QAction *lightbulbMapAction = nullptr;
    QString mapName = QFileInfo(item->subMap()->mapInfo()->path()).fileName();
    if (LightbulbsMgr::instance().maps().contains(mapName))
        lightbulbMapAction = menu.addAction(lightIcon, tr("Show lights in %1").arg(mapName));
    else
        lightbulbMapAction = menu.addAction(lightIcon, tr("Hide lights in %1").arg(mapName));
    menu.addSeparator();
    const int lotLevel = item->lot()->level();
    const int sourceMinLevel = item->subMap()->minLevel();
    const int sourceMaxLevel = item->subMap()->maxLevel();
    QMenu *verticalMenu = menu.addMenu(tr("Vertical Placement"));
    verticalMenu->setTitle(tr("Vertical Placement (Level %1)")
                           .arg(lotLevel));
    QAction *sourceLevelsAction = verticalMenu->addAction(
                tr("Source levels: %1 to %2")
                .arg(sourceMinLevel).arg(sourceMaxLevel));
    sourceLevelsAction->setEnabled(false);
    QAction *worldLevelsAction = verticalMenu->addAction(
                tr("Current world levels: %1 to %2")
                .arg(lotLevel + sourceMinLevel)
                .arg(lotLevel + sourceMaxLevel));
    worldLevelsAction->setEnabled(false);
    verticalMenu->addSeparator();
    QAction *lowerAction = verticalMenu->addAction(
                tr("Lower Entire Lot One Level..."));
    QAction *raiseAction = verticalMenu->addAction(
                tr("Raise Entire Lot One Level..."));
    QAction *groundAction = verticalMenu->addAction(
                tr("Return Entire Lot to Level 0..."));
    lowerAction->setEnabled(lotLevel - 1 + sourceMinLevel >=
                            MIN_WORLD_LEVEL);
    raiseAction->setEnabled(lotLevel + 1 + sourceMaxLevel <=
                            MAX_WORLD_LEVEL);
    groundAction->setEnabled(lotLevel != 0 &&
                             sourceMinLevel >= MIN_WORLD_LEVEL &&
                             sourceMaxLevel <= MAX_WORLD_LEVEL);
    verticalMenu->addSeparator();
    const int openingCount =
            mScene->basementGroundOpeningCount(item->lot());
    QAction *pierceAction = verticalMenu->addAction(
                tr("Open Ground at Basement Stairs..."));
    pierceAction->setEnabled(openingCount > 0);
    if (!openingCount) {
        pierceAction->setToolTip(
                    tr("No staircase from level -1 to level 0 was detected."));
    }
    QIcon removeIcon(QLatin1String(":images/16x16/edit-delete.png"));
    QAction *removeAction = menu.addAction(removeIcon, tr("Remove Lot"));
    menu.addSeparator();
    QIcon tiledIcon(QLatin1String(":images/tiled-icon-16.png"));
    const QString mapPath = item->subMap()->mapInfo()->path();
    QAction *openAction = menu.addAction(
                tiledIcon,
                tr("Open in %1").arg(editorNameForPath(mapPath)));

    QAction *action = menu.exec(screenPos);
    if (action == nullptr) return;
    if (action == lightbulbRoomAction)
        LightbulbsMgr::instance().toggleRoom(roomName);
    if (action == lightbulbMapAction)
        LightbulbsMgr::instance().toggleMap(mapName);
    const auto confirmVerticalPlacement = [&](int newLevel) {
        const int newWorldMin = newLevel + sourceMinLevel;
        const int newWorldMax = newLevel + sourceMaxLevel;
        return QMessageBox::warning(
                    mScene->views().value(0),
                    tr("Move Entire Lot Vertically"),
                    tr("This changes the world level of the complete lot "
                       "\"%1\".\n\n"
                       "Source levels: %2 to %3\n"
                       "Current world levels: %4 to %5\n"
                       "Resulting world levels: %6 to %7\n\n"
                       "Every floor, wall, window, stair, RoomDef, object, "
                       "and collision layer moves together. If this source "
                       "already stores its basement on negative levels, "
                       "keep the lot at level 0.\n\nContinue?")
                    .arg(mapName)
                    .arg(sourceMinLevel).arg(sourceMaxLevel)
                    .arg(lotLevel + sourceMinLevel)
                    .arg(lotLevel + sourceMaxLevel)
                    .arg(newWorldMin).arg(newWorldMax),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) == QMessageBox::Yes;
    };
    if (action == lowerAction && confirmVerticalPlacement(lotLevel - 1))
        mScene->worldDocument()->setLotLevel(item->lot(), lotLevel - 1);
    if (action == raiseAction && confirmVerticalPlacement(lotLevel + 1))
        mScene->worldDocument()->setLotLevel(item->lot(), lotLevel + 1);
    if (action == groundAction && confirmVerticalPlacement(0))
        mScene->worldDocument()->setLotLevel(item->lot(), 0);
    if (action == pierceAction) {
        const QMessageBox::StandardButton confirmation =
                QMessageBox::warning(
                    mScene->views().value(0),
                    tr("Open Ground at Basement Stairs"),
                    tr("WorldEd detected %n basement staircase opening(s).\n\n"
                       "This will remove the level-zero Floor tile at each "
                       "opening from the affected cell TMX file(s). Backup "
                       "copies will be created beside the project before "
                       "any file is changed.\n\nContinue?",
                       "", openingCount),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
        if (confirmation == QMessageBox::Yes) {
            QStringList backupPaths;
            QString error;
            const int cleared = mScene->pierceGroundAtBasementStairs(
                        item->lot(), &backupPaths, &error);
            if (!cleared) {
                QMessageBox::warning(
                            mScene->views().value(0),
                            tr("Basement Ground Opening"), error);
            } else {
                QMessageBox::information(
                            mScene->views().value(0),
                            tr("Basement Ground Opening"),
                            tr("Removed %n level-zero Floor tile(s).\n\n"
                               "Backup location(s):\n%1",
                               "", cleared)
                            .arg(backupPaths.join(QLatin1Char('\n'))));
            }
        }
    }
    if (action == removeAction) {
        int lotIndex = mScene->cell()->indexOf(item->lot());
        mScene->worldDocument()->removeCellLot(mScene->cell(), lotIndex);
    }
    if (action == openAction) {
        openInMapEditor(mapPath, mScene->views().value(0));
    }
}

SubMapItem *SubMapTool::topmostItemAt(const QPointF &scenePos)
{
    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (SubMapItem *subMapItem = dynamic_cast<SubMapItem*>(item))
            return subMapItem;
    }
    return nullptr;
}

LightSwitchOverlay *SubMapTool::topmostSwitchAt(const QPointF &scenePos)
{
    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (LightSwitchOverlay *switchItem = dynamic_cast<LightSwitchOverlay*>(item))
            return switchItem;
    }
    return 0;
}

/////

class RoomToneCursorItem : public QGraphicsItem
{
public:
    RoomToneCursorItem(CellScene *scene, QGraphicsItem *parent = 0);

    QRectF boundingRect() const;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    void updateBounds();

    CellScene *mScene;
    MapRenderer *mRenderer;
    QRectF mBoundingRect;
    QPointF mPos;
    int mLevel;
    QImage mImage;
};

RoomToneCursorItem::RoomToneCursorItem(CellScene *scene, QGraphicsItem *parent) :
    QGraphicsItem(parent),
    mScene(scene),
    mRenderer(scene->renderer()),
    mImage(QIcon(QStringLiteral(":/images/speaker-tool.svg"))
           .pixmap(32, 32).toImage())
{
}

QRectF RoomToneCursorItem::boundingRect() const
{
    return mBoundingRect;
}

void RoomToneCursorItem::paint(QPainter *painter,
                               const QStyleOptionGraphicsItem *,
                               QWidget *)
{
    QColor color = mScene->document()->currentObjectGroup()->color();
    color = color.lighter();
    mRenderer->drawFancyRectangle(painter, QRectF(mPos, QSizeF(1, 1)), color, mLevel);

    const QFontMetrics fm = painter->fontMetrics();
    int lineHeight = fm.lineSpacing();

    QPointF scenePos = mRenderer->tileToPixelCoords(mPos + QPointF(0.5, 0.5), mLevel);

    auto scene = static_cast<CellScene*>(this->scene());
    auto view = static_cast<CellView*>(scene->views().first());
    qreal zoom = view->zoomable()->scale();
    zoom = qMin(zoom, 1.0);
    QRectF sceneRect(scenePos - QPointF((mImage.width() / 2) / zoom, (lineHeight + mImage.height()) / zoom), mImage.size() / zoom);
    painter->drawImage(sceneRect, mImage);

}

void RoomToneCursorItem::updateBounds()
{
    QPointF pos = mPos;
    QRectF bounds;
    bounds |= mRenderer->boundingRect(QRect(pos.x() - 3, pos.y() - 3, 1, 1), mLevel);
    bounds |= mRenderer->boundingRect(QRect(pos.x(), pos.y(), 1, 1), mLevel);
    bounds.adjust(-2, -3, 2, 2);
    if (bounds != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

/////

SINGLETON_IMPL(RoomToneTool)

RoomToneTool::RoomToneTool()
    : BaseCellSceneTool(QLatin1String("Place Room Tone"),
                        QIcon(QLatin1String(":/images/speaker-tool.svg")),
                        QKeySequence()),
      mContextMenuVisible(false)
{
//    new SpawnToolDialog(MainWindow::instance());
}

void RoomToneTool::activate()
{
    BaseCellSceneTool::activate();

    mCursorItem = new RoomToneCursorItem(mScene);
    mCursorItem->setZValue(mScene->ZVALUE_GRID + 1);
    mScene->addItem(mCursorItem);
}

void RoomToneTool::deactivate()
{
    delete mCursorItem;

    BaseCellSceneTool::activate();
}

void RoomToneTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        showContextMenu(event->scenePos(), event->screenPos());
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    if (RoomToneItem *item = topmostItemAt(event->scenePos())) {
        QList<WorldCellObject*> selectedObjects = mScene->document()->selectedObjects();
        QSet<WorldCellObject*> selection(selectedObjects.begin(), selectedObjects.end());
        if (event->modifiers() & Qt::ShiftModifier) {
            selection += item->object();
        } else if (event->modifiers() & Qt::ControlModifier) {
            if (selection.contains(item->object())) {
                selection -= item->object();
            } else {
                selection += item->object();
            }
        } else {
            selection.clear();
            selection += item->object();
        }
        mScene->document()->setSelectedObjects({selection.begin(), selection.end()});
        event->accept();
        return;
    }

    // Try to ignore left-click that closes the context menu
    if (mContextMenuVisible || (mContextMenuShown.isValid() &&
            mContextMenuShown.msecsTo(QTime::currentTime()) < 500)) {
        return;
    }

    event->accept();

    mScene->document()->undoStack()->beginMacro(tr("Add RoomTone"));

    createWorldTemplateIfNeeded();
    ObjectType *type = mScene->world()->objectType(QLatin1String("RoomTone"));
    PropertyTemplate *pt = mScene->world()->propertyTemplate(QLatin1String("RoomTone"));

    QPoint tilePos = mScene->renderer()->pixelToTileCoordsInt(event->scenePos(),
                                                              mScene->document()->currentLevel());

    WorldObjectGroup *og = mScene->world()->objectGroups().find(QLatin1String("RoomTone"));
    WorldCellObject *obj = new WorldCellObject(mScene->cell(),
                                               QString(), type, og,
                                               tilePos.x(), tilePos.y(),
                                               mScene->document()->currentLevel(),
                                               1, 1);
    obj->addTemplate(obj->templates().size(), pt);
    for (Property *property : pt->properties()) {
        PropertyDef *pd = mScene->world()->propertyDefinition(property->mDefinition->mName);
        obj->addProperty(obj->properties().size(), new Property(pd, pd->mDefaultValue));
    }
    WorldObjectValidation::applyCreationDefaults(obj);
    mScene->worldDocument()->addCellObject(mScene->cell(),
                                           mScene->cell()->objects().size(),
                                           obj);

    mScene->document()->setSelectedObjects(WorldCellObjectList() << obj);

    mScene->document()->undoStack()->endMacro();
}

void RoomToneTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    mCursorItem->setVisible(topmostItemAt(event->scenePos()) == 0);

    QPoint tilePos = mScene->renderer()->pixelToTileCoordsInt(event->scenePos(),
                                                              mScene->document()->currentLevel());
    mCursorItem->mPos = tilePos;
    mCursorItem->mLevel = mScene->document()->currentLevel();
    mCursorItem->updateBounds();
}

void RoomToneTool::showContextMenu(const QPointF &scenePos, const QPoint &screenPos)
{
    ObjectItem *item = contextObjectItemAt(mScene, scenePos);
    if (!item) {
        return;
    }

    mContextMenuVisible = true;
    showObjectContextMenu(mScene, item, screenPos);
    mContextMenuVisible = false;
    mContextMenuShown = QTime::currentTime();
}

RoomToneItem *RoomToneTool::topmostItemAt(const QPointF &scenePos)
{
    for (QGraphicsItem *item : mScene->items(scenePos)) {
        if (RoomToneItem *roomToneItem = dynamic_cast<RoomToneItem*>(item)) {
            if (!roomToneItem->isAdjacent()) {
                return roomToneItem;
            }
        }
    }
    return nullptr;
}

void RoomToneTool::createWorldTemplateIfNeeded()
{
    World *world = mScene->world();

    // Create the RoomTone object type if needed
    ObjectType *type = world->objectType(QLatin1String("RoomTone"));
    if (!type) {
        type = new ObjectType(QLatin1String("RoomTone"));
        mScene->worldDocument()->addObjectType(type);
    }

    // Create the RoomTone object group if needed
    WorldObjectGroup *og = world->objectGroups().find(QLatin1String("RoomTone"));
    if (og == nullptr) {
        og = new WorldObjectGroup(type, QLatin1String("RoomTone"), Qt::blue);
        world->insertObjectGroup(world->objectGroups().size(), og);
    }

    // Create the RoomTone property enum if needed
    PropertyEnum *pe = world->propertyEnums().find(QLatin1String("RoomTone"));
    if (!pe) {
        QStringList rooms;
        rooms << QLatin1String("Generic");
        mScene->worldDocument()->addPropertyEnum(QLatin1String("RoomTone"), rooms, true);
        pe = world->propertyEnums().find(QLatin1String("RoomTone"));
    }

    // Create the RoomTone property definition if needed
    PropertyDef *pd = world->propertyDefinition(QLatin1String("RoomTone"));
    if (!pd) {
        pd = new PropertyDef(QLatin1String("RoomTone"), QLatin1String("Generic"),
                             tr("Used to set the game's RoomType FMOD parameter."),
                             pe);
        mScene->worldDocument()->addPropertyDefinition(pd);
    }

    // Create the EntireBuilding property definition if needed
    PropertyDef *pd2 = world->propertyDefinition(QLatin1String("EntireBuilding"));
    if (!pd2) {
        pd2 = new PropertyDef(QLatin1String("EntireBuilding"), QLatin1String("false"),
                             tr("If true, a RoomTone is active in the entire building."),
                             nullptr);
        mScene->worldDocument()->addPropertyDefinition(pd2);
    }

    // Create the RoomTone template if needed
    PropertyTemplate *pt = world->propertyTemplate(QLatin1String("RoomTone"));
    if (!pt) {
        pt = new PropertyTemplate;
        pt->mName = QLatin1String("RoomTone");
        pt->mDescription = tr("This template holds the default set of properties for all RoomTone objects.");
        mScene->worldDocument()->addTemplate(pt);
    }

    if (pt->canAddProperty(pd)) {
        pt->addProperty(pt->properties().size(), new Property(pd, pd->mDefaultValue));
    }
    if (pt->canAddProperty(pd2)) {
        pt->addProperty(pt->properties().size(), new Property(pd2, pd2->mDefaultValue));
    }
}

/////

class SpawnPointCursorItem : public QGraphicsItem
{
public:
    SpawnPointCursorItem(CellScene *scene, QGraphicsItem *parent = 0);

    QRectF boundingRect() const;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    void updateBounds();

    CellScene *mScene;
    MapRenderer *mRenderer;
    QRectF mBoundingRect;
    QPointF mPos;
    int mLevel;
};

SpawnPointCursorItem::SpawnPointCursorItem(CellScene *scene, QGraphicsItem *parent) :
    QGraphicsItem(parent),
    mScene(scene),
    mRenderer(scene->renderer())
{
}

QRectF SpawnPointCursorItem::boundingRect() const
{
    return mBoundingRect;
}

void SpawnPointCursorItem::paint(QPainter *painter,
                           const QStyleOptionGraphicsItem *,
                           QWidget *)
{
    QColor color = mScene->document()->currentObjectGroup()->color();
    color = color.lighter();
    mRenderer->drawFancyRectangle(painter, QRectF(mPos, QSizeF(1, 1)), color, mLevel);

    painter->setPen(QPen(Qt::black));
    color = mScene->document()->currentObjectGroup()->color();
    color.setAlpha(200);

    int level = mLevel;

    qreal inset = 0.15;
    QPointF pos = mPos;

    // Bottom-right
    QPolygonF poly;
    poly << mRenderer->tileToPixelCoords(pos + QPointF(0.5, 0.5), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(1-inset, 0), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(1-inset, 1-inset), level);
    poly << poly.first();
    painter->setBrush(color.darker(125));
    painter->drawPolygon(poly);
    poly.clear();

    // Bottom-left
    poly << mRenderer->tileToPixelCoords(pos + QPointF(0.5, 0.5), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(1-inset, 1-inset), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(0, 1-inset), level);
    poly << poly.first();
    painter->setBrush(color.darker(115));
    painter->drawPolygon(poly);
    poly.clear();

    // Top-right
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(1-inset, 1-inset), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(1-inset, 0), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(3, 3) + QPointF(0.5, 0.5), level);
    poly << poly.first();
    painter->setBrush(color.lighter(100));
    painter->drawPolygon(poly);
    poly.clear();

    // Top-left
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(1-inset, 1-inset), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(3, 3) + QPointF(0.5, 0.5), level);
    poly << mRenderer->tileToPixelCoords(pos - QPointF(1.5, 1.5) + QPointF(0, 1-inset), level);
    poly << poly.first();
    painter->setBrush(color.lighter(115));
    painter->drawPolygon(poly);
    poly.clear();
}

void SpawnPointCursorItem::updateBounds()
{
    QPointF pos = mPos;
    QRectF bounds;
    bounds |= mRenderer->boundingRect(QRect(pos.x() - 3, pos.y() - 3, 1, 1), mLevel);
    bounds |= mRenderer->boundingRect(QRect(pos.x(), pos.y(), 1, 1), mLevel);
    bounds.adjust(-2, -3, 2, 2);
    if (bounds != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = bounds;
    }
}

/////

SINGLETON_IMPL(SpawnPointTool)

SpawnPointTool::SpawnPointTool()
    : BaseCellSceneTool(QLatin1String("Place Spawn Point"),
                        QIcon(QLatin1String(":/images/22x22/tool-spawn-point.png")),
                        QKeySequence()),
      mContextMenuVisible(false)
{
    new SpawnToolDialog(MainWindow::instance());
}

void SpawnPointTool::activate()
{
    BaseCellSceneTool::activate();

    mCursorItem = new SpawnPointCursorItem(mScene);
    mCursorItem->setZValue(mScene->ZVALUE_GRID + 1);
    mScene->addItem(mCursorItem);

    SpawnToolDialog::instance().setDocument(mScene->document());
    SpawnToolDialog::instance().show();
}

void SpawnPointTool::deactivate()
{
    SpawnToolDialog::instance().hide();
    SpawnToolDialog::instance().setDocument(0);

    delete mCursorItem;

    BaseCellSceneTool::activate();
}

void SpawnPointTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{

    if (event->button() == Qt::RightButton) {
        showContextMenu(event->scenePos(), event->screenPos());
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    if (SpawnPointItem *item = topmostItemAt(event->scenePos())) {
        QList<WorldCellObject*> selectedObjects = mScene->document()->selectedObjects();
        QSet<WorldCellObject*> selection(selectedObjects.begin(), selectedObjects.end());
        if (event->modifiers() & Qt::ShiftModifier)
            selection += item->object();
        else if (event->modifiers() & Qt::ControlModifier) {
            if (selection.contains(item->object()))
                selection -= item->object();
            else
                selection += item->object();
        } else {
            selection.clear();
            selection += item->object();
        }
        mScene->document()->setSelectedObjects({selection.begin(), selection.end()});
        event->accept();
        return;
    }

    // Try to ignore left-click that closes the context menu
    if (mContextMenuVisible || (mContextMenuShown.isValid() &&
            mContextMenuShown.msecsTo(QTime::currentTime()) < 500))
        return;

    event->accept();

    mScene->document()->undoStack()->beginMacro(tr("Add Spawn Point"));

    // Create the SpawnPoint object type if needed
    ObjectType *type = mScene->world()->objectType(QLatin1String("SpawnPoint"));
    if (!type) {
        type = new ObjectType(QLatin1String("SpawnPoint"));
        mScene->worldDocument()->addObjectType(type);
    }

    // Create the Professions property enum if needed
    PropertyEnum *pe = mScene->world()->propertyEnums().find(QLatin1String("Professions"));
    if (!pe) {
        QStringList professions;
        professions << QLatin1String("burglar")
                    << QLatin1String("burgerflipper")
                    << QLatin1String("carpenter")
                    << QLatin1String("chef")
                    << QLatin1String("constructionworker")
                    << QLatin1String("doctor")
                    << QLatin1String("electrician")
                    << QLatin1String("engineer")
                    << QLatin1String("farmer")
                    << QLatin1String("fireofficer")
                    << QLatin1String("fisherman")
                    << QLatin1String("fitnessInstructor")
                    << QLatin1String("lumberjack")
                    << QLatin1String("mechanics")
                    << QLatin1String("metalworker")
                    << QLatin1String("nurse")
                    << QLatin1String("parkranger")
                    << QLatin1String("policeofficer")
                    << QLatin1String("rancher")
                    << QLatin1String("repairman")
                    << QLatin1String("securityguard")
                    << QLatin1String("smither")
                    << QLatin1String("tailor")
                    << QLatin1String("unemployed")
                    << QLatin1String("veteran");
        mScene->worldDocument()->addPropertyEnum(QLatin1String("Professions"), professions, true);
        pe = mScene->world()->propertyEnums().find(QLatin1String("Professions"));
    }

    // Create the Professions property definition if needed
    PropertyDef *pd = mScene->world()->propertyDefinition(QLatin1String("Professions"));
    if (!pd) {
        pd = new PropertyDef(QLatin1String("Professions"), QLatin1String("unemployed"),
                             tr("Comma-separated list of professions that may spawn here."),
                             pe);
        mScene->worldDocument()->addPropertyDefinition(pd);
    }

    // Create the SpawnPoint template if needed
    PropertyTemplate *pt = mScene->world()->propertyTemplate(QLatin1String("SpawnPoint"));
    if (!pt) {
        pt = new PropertyTemplate;
        pt->mName = QLatin1String("SpawnPoint");
        pt->mDescription = tr("This template holds the default set of properties for all spawn points in the world.");
        pt->addProperty(0, new Property(pd, pd->mDefaultValue));
        mScene->worldDocument()->addTemplate(pt);
    }

    QPoint tilePos = mScene->renderer()->pixelToTileCoordsInt(event->scenePos(),
                                                              mScene->document()->currentLevel());

    WorldObjectGroup *og = mScene->document()->currentObjectGroup();
    WorldCellObject *obj = new WorldCellObject(mScene->cell(),
                                               QString(), type, og,
                                               tilePos.x(), tilePos.y(),
                                               mScene->document()->currentLevel(),
                                               1, 1);
    obj->addTemplate(obj->templates().size(), pt);
    WorldObjectValidation::applyCreationDefaults(obj);
    mScene->worldDocument()->addCellObject(mScene->cell(),
                                           mScene->cell()->objects().size(),
                                           obj);

    mScene->document()->setSelectedObjects(WorldCellObjectList() << obj);

    mScene->document()->undoStack()->endMacro();
}

void SpawnPointTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    mCursorItem->setVisible(topmostItemAt(event->scenePos()) == 0);

    QPoint tilePos = mScene->renderer()->pixelToTileCoordsInt(event->scenePos(),
                                                              mScene->document()->currentLevel());
    mCursorItem->mPos = tilePos;
    mCursorItem->mLevel = mScene->document()->currentLevel();
    mCursorItem->updateBounds();
}

void SpawnPointTool::showContextMenu(const QPointF &scenePos, const QPoint &screenPos)
{
    ObjectItem *item = contextObjectItemAt(mScene, scenePos);
    if (!item) {
        return;
    }

    mContextMenuVisible = true;
    showObjectContextMenu(mScene, item, screenPos);
    mContextMenuVisible = false;
    mContextMenuShown = QTime::currentTime();
}

SpawnPointItem *SpawnPointTool::topmostItemAt(const QPointF &scenePos)
{
    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (SpawnPointItem *spawnPtItem = dynamic_cast<SpawnPointItem*>(item)) {
            if (!spawnPtItem->isAdjacent())
                return spawnPtItem;
        }
    }
    return 0;
}

/////

CellCreateRoadTool *CellCreateRoadTool::mInstance = 0;

CellCreateRoadTool *CellCreateRoadTool::instance()
{
    if (!mInstance)
        mInstance = new CellCreateRoadTool();
    return mInstance;
}

void CellCreateRoadTool::deleteInstance()
{
    delete mInstance;
}

CellCreateRoadTool::CellCreateRoadTool()
    : BaseCellSceneTool(tr("Create Roads"),
                         QIcon(QLatin1String(":/images/22x22/road-tool-create.png")),
                         QKeySequence())
    , mCreating(false)
    , mCursorItem(new QGraphicsPolygonItem)
{
    mCursorItem->setBrush(Qt::cyan);
    mCursorItem->setOpacity(0.66);
    mCursorItem->setZValue(CellScene::ZVALUE_ROADITEM_CREATING + 1);
}

CellCreateRoadTool::~CellCreateRoadTool()
{
    delete mRoad;
    delete mRoadItem;
    delete mCursorItem;
}

void CellCreateRoadTool::activate()
{
    BaseCellSceneTool::activate();
    mScene->addItem(mCursorItem);
}

void CellCreateRoadTool::deactivate()
{
    mScene->removeItem(mCursorItem);
    BaseCellSceneTool::deactivate();
}

void CellCreateRoadTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && mCreating) {
        cancelNewRoadItem();
        event->accept();
    }
}

void CellCreateRoadTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (mCreating)
            return;
        startNewRoadItem(event->scenePos());
        mCreating = true;
    }
    if (event->button() == Qt::RightButton) {
        if (!mCreating)
            return;
        cancelNewRoadItem();
        mCreating = false;
    }
}

void CellCreateRoadTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QPoint roadPos = mScene->pixelToRoadCoords(event->scenePos());

    if (mCreating) {
        QPoint delta = roadPos - mStartRoadPos;
        if (qAbs(delta.x()) >= qAbs(delta.y())) {
            delta.setY(0); // horizontal road
        } else {
            delta.setX(0); // vertical road
        }
        mRoad->setCoords(mStartRoadPos, mStartRoadPos + delta);
        mRoadItem->synchWithRoad();

        roadPos = mRoad->end();
    }

    int roadWidth = currentRoadWidth();
    QPoint topLeft = roadPos - QPoint(roadWidth / 2, roadWidth / 2);
    QSize size(roadWidth, roadWidth);

    mCursorItem->setPolygon(mScene->roadRectToScenePolygon(QRect(topLeft, size)));
}

void CellCreateRoadTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!mCreating)
            return;
        if (mRoad->start() == mRoad->end())
            return;
        finishNewRoadItem();
        mCreating = false;
    }
}

int CellCreateRoadTool::currentRoadWidth() const
{
    return WorldCreateRoadTool::instance()->currentRoadWidth();
}

TrafficLines *CellCreateRoadTool::currentTrafficLines() const
{
    return WorldCreateRoadTool::instance()->currentTrafficLines();
}

QString CellCreateRoadTool::currentTileName() const
{
    return WorldCreateRoadTool::instance()->currentTileName();
}

void CellCreateRoadTool::startNewRoadItem(const QPointF &scenePos)
{
    mStartRoadPos = mScene->pixelToRoadCoords(scenePos);
    mRoad = new Road(mScene->world(),
                     mStartRoadPos.x(), mStartRoadPos.y(),
                     mStartRoadPos.x(), mStartRoadPos.y(),
                     currentRoadWidth(), -1);
    mRoad->setTileName(currentTileName());
    mRoad->setTrafficLines(currentTrafficLines());
    mRoadItem = new CellRoadItem(mScene, mRoad);
    mRoadItem->setZValue(CellScene::ZVALUE_ROADITEM_CREATING);
    mScene->addItem(mRoadItem);
}

void CellCreateRoadTool::clearNewRoadItem()
{
    mScene->removeItem(mRoadItem);
    delete mRoadItem;
    mRoadItem = 0;
}

void CellCreateRoadTool::cancelNewRoadItem()
{
    clearNewRoadItem();
    delete mRoad;
    mRoad = 0;
}

void CellCreateRoadTool::finishNewRoadItem()
{
    clearNewRoadItem();

    QUndoStack *undoStack = mScene->worldDocument()->undoStack();
    undoStack->push(new AddRoad(mScene->worldDocument(),
                                mScene->world()->roads().count(),
                                mRoad));
    mRoad = 0;
}

/////

CellEditRoadTool *CellEditRoadTool::mInstance = 0;

CellEditRoadTool *CellEditRoadTool::instance()
{
    if (!mInstance)
        mInstance = new CellEditRoadTool();
    return mInstance;
}

void CellEditRoadTool::deleteInstance()
{
    delete mInstance;
}

CellEditRoadTool::CellEditRoadTool()
    : BaseCellSceneTool(tr("Edit Roads"),
                         QIcon(QLatin1String(":/images/22x22/road-tool-edit.png")),
                         QKeySequence())
    , mSelectedRoadItem(0)
    , mRoad(0)
    , mRoadItem(0)
    , mMoving(false)
    , mStartHandle(new QGraphicsPolygonItem)
    , mEndHandle(new QGraphicsPolygonItem)
    , mHandlesVisible(false)
{
    mStartHandle->setBrush(Qt::cyan);
    mStartHandle->setOpacity(0.66);
    mStartHandle->setZValue(CellScene::ZVALUE_ROADITEM_CREATING + 1);

    mEndHandle->setBrush(Qt::cyan);
    mEndHandle->setOpacity(0.66);
    mEndHandle->setZValue(CellScene::ZVALUE_ROADITEM_CREATING + 1);
}

CellEditRoadTool::~CellEditRoadTool()
{
    delete mRoadItem;
    delete mRoad;
    delete mStartHandle;
    delete mEndHandle;
}

void CellEditRoadTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene)
        mScene->worldDocument()->disconnect(this);

    mScene = scene ? scene->asCellScene() : 0;

    if (mScene) {
        connect(mScene->worldDocument(), SIGNAL(roadAboutToBeRemoved(int)),
                SLOT(roadAboutToBeRemoved(int)));
        connect(mScene->worldDocument(), SIGNAL(roadCoordsChanged(int)),
                SLOT(roadCoordsChanged(int)));
    }
}

void CellEditRoadTool::activate()
{
    BaseCellSceneTool::activate();
}

void CellEditRoadTool::deactivate()
{
    if (mHandlesVisible) {
        mScene->removeItem(mStartHandle);
        mScene->removeItem(mEndHandle);
        mHandlesVisible = false;
    }
    mSelectedRoadItem = 0;

    BaseCellSceneTool::deactivate();
}

void CellEditRoadTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && mMoving) {
        cancelMoving();
        mMoving = false;
        event->accept();
    }
}

void CellEditRoadTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (mMoving)
            return;
        startMoving(event->scenePos());
    }
    if (event->button() == Qt::RightButton) {
        if (!mMoving)
            return;
        cancelMoving();
        mMoving = false;
    }
}

void CellEditRoadTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!mMoving)
        return;
    QPoint curPos = mScene->pixelToRoadCoords(event->scenePos());
    QPoint delta = curPos - (mMovingStart
            ? mSelectedRoadItem->road()->start()
            : mSelectedRoadItem->road()->end());
    if (mSelectedRoadItem->road()->isVertical())
        delta.setX(0);
    else
        delta.setY(0);
    if (mMovingStart)
        mRoad->setCoords(mSelectedRoadItem->road()->start() + delta, mRoad->end());
    else
        mRoad->setCoords(mRoad->start(), mSelectedRoadItem->road()->end() + delta);
    mRoadItem->synchWithRoad();
    updateHandles(mRoad);
}

void CellEditRoadTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!mMoving)
            return;
        if (mRoad->start() == mSelectedRoadItem->road()->start() &&
                mRoad->end() == mSelectedRoadItem->road()->end()) {
            cancelMoving();
            mMoving = false;
            return;
        }
        finishMoving();
        mMoving = false;
    }
}

// The read being edited could be deleted via undo/redo.
void CellEditRoadTool::roadAboutToBeRemoved(int index)
{
    Road *road = mScene->world()->roads().at(index);
    if (mSelectedRoadItem && road == mSelectedRoadItem->road()) {
        if (mMoving) {
            cancelMoving();
            mMoving = false;
        }
        updateHandles(0);
        mSelectedRoadItem = 0;
    }
}

void CellEditRoadTool::roadCoordsChanged(int index)
{
    Road *road = mScene->world()->roads().at(index);
    if (mSelectedRoadItem && road == mSelectedRoadItem->road())
        updateHandles(road);
}

void CellEditRoadTool::startMoving(const QPointF &scenePos)
{
    if (mSelectedRoadItem) {
        QPoint roadPos = mScene->pixelToRoadCoords(scenePos);
        if (mSelectedRoadItem->road()->startBounds().adjusted(0, 0, 1, 1).contains(roadPos)) {
            mMovingStart = true;
        } else if (mSelectedRoadItem->road()->endBounds().adjusted(0, 0, 1, 1).contains(roadPos)) {
            mMovingStart = false;
        } else {
            mSelectedRoadItem->setEditable(false);
            mSelectedRoadItem->setZValue(mSelectedRoadItem->isSelected() ?
                                             CellScene::ZVALUE_ROADITEM_SELECTED :
                                             CellScene::ZVALUE_ROADITEM_UNSELECTED);
            mSelectedRoadItem = 0;
        }
        if (mSelectedRoadItem) {
            mMoving = true;

            mRoad = new Road(mScene->world(),
                             mSelectedRoadItem->road()->x1(), mSelectedRoadItem->road()->y1(),
                             mSelectedRoadItem->road()->x2(), mSelectedRoadItem->road()->y2(),
                             mSelectedRoadItem->road()->width(), -1);
            mRoadItem = new CellRoadItem(mScene, mRoad);
            mRoadItem->setEditable(true);
            mRoadItem->setZValue(CellScene::ZVALUE_ROADITEM_CREATING);
            mScene->addItem(mRoadItem);
            mSelectedRoadItem->setVisible(false);
            return;
        }
    }

    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (CellRoadItem *roadItem = dynamic_cast<CellRoadItem*>(item)) {
            mSelectedRoadItem = roadItem;
            mSelectedRoadItem->setEditable(true);
            mSelectedRoadItem->setZValue(CellScene::ZVALUE_ROADITEM_CREATING);
            updateHandles(mSelectedRoadItem->road());
            break;
        }
    }
    updateHandles(mSelectedRoadItem ? mSelectedRoadItem->road() : 0);
}

void CellEditRoadTool::finishMoving()
{
    QUndoStack *undoStack = mScene->worldDocument()->undoStack();
    undoStack->push(new ChangeRoadCoords(mScene->worldDocument(),
                                         mSelectedRoadItem->road(),
                                         mRoad->start(), mRoad->end()));
    cancelMoving();
}

void CellEditRoadTool::cancelMoving()
{
    mSelectedRoadItem->setVisible(true);
    updateHandles(mSelectedRoadItem->road());

    mScene->removeItem(mRoadItem);
    delete mRoadItem;
    mRoadItem = 0;

    delete mRoad;
    mRoad = 0;
}

void CellEditRoadTool::updateHandles(Road *road)
{
    if (road) {
        mStartHandle->setPolygon(mScene->roadRectToScenePolygon(road->startBounds()));
        mEndHandle->setPolygon(mScene->roadRectToScenePolygon(road->endBounds()));
        if (mHandlesVisible == false) {
            mScene->addItem(mStartHandle);
            mScene->addItem(mEndHandle);
            mHandlesVisible = true;
        }
    } else if (mHandlesVisible) {
        mScene->removeItem(mStartHandle);
        mScene->removeItem(mEndHandle);
        mHandlesVisible = false;
    }
}

/////

CellSelectMoveRoadTool *CellSelectMoveRoadTool::mInstance = 0;

CellSelectMoveRoadTool *CellSelectMoveRoadTool::instance()
{
    if (!mInstance)
        mInstance = new CellSelectMoveRoadTool();
    return mInstance;
}

void CellSelectMoveRoadTool::deleteInstance()
{
    delete mInstance;
}

CellSelectMoveRoadTool::CellSelectMoveRoadTool()
    : BaseCellSceneTool(tr("Select and Move Roads"),
                         QIcon(QLatin1String(":/images/22x22/road-tool-select.png")),
                         QKeySequence())
    , mMode(NoMode)
    , mMousePressed(false)
    , mSelectionRectItem(new QGraphicsRectItem)
{
    mSelectionRectItem->setZValue(1000);
    mSelectionRectItem->setPen(QColor(0x33,0x99,0xff));
    mSelectionRectItem->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
}

CellSelectMoveRoadTool::~CellSelectMoveRoadTool()
{
    delete mSelectionRectItem;
}

void CellSelectMoveRoadTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene)
        mScene->worldDocument()->disconnect(this);

    mScene = scene ? scene->asCellScene() : 0;

    if (mScene) {
        connect(mScene->worldDocument(), SIGNAL(roadAboutToBeRemoved(int)),
                SLOT(roadAboutToBeRemoved(int)));
    }
}

void CellSelectMoveRoadTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && (mMode == Moving)) {
        mMode = CancelMoving;
        foreach (Road *road, mMovingRoads)
            mScene->itemForRoad(road)->setDragging(false);
        mMovingRoads.clear();
        event->accept();
    }
}

void CellSelectMoveRoadTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        if (mMode != NoMode) // Ignore additional presses during select/move
            break;
        mMousePressed = true;
        mStartScenePos = event->scenePos();
        mDropRoadPos = mScene->pixelToRoadCoords(mStartScenePos);
        mClickedItem = topmostItemAt(mStartScenePos);
        break;
    case Qt::RightButton:
        // Right-clicks exits moving, same as the Escape key.
        if (mMode == Moving) {
            mMode = CancelMoving;
            foreach (Road *road, mMovingRoads)
                mScene->itemForRoad(road)->setDragging(false);
            mMovingRoads.clear();
        }
        break;
    default:
        break;
    }
}

void CellSelectMoveRoadTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (mMode == NoMode && mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (mClickedItem &&
                    mScene->worldDocument()->selectedRoads().contains(mClickedItem->road()) &&
                    !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
                startMoving();
            else
                startSelecting();
        }
    }

    switch (mMode) {
    case Selecting:
    {
        QPointF start = mStartScenePos;
        QPointF end = event->scenePos();
        QRectF bounds = QRectF(start, end).normalized();
        mSelectionRectItem->setRect(bounds);
        break;
    }
    case Moving:
        updateMovingItems(event->scenePos(), event->modifiers());
        break;
    case NoMode:
        break;
    case CancelMoving:
        break;
    }
}

void CellSelectMoveRoadTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
    {
        bool toggle = event->modifiers() & Qt::ControlModifier;
        bool extend = event->modifiers() & Qt::ShiftModifier;
        QList<Road*> newSelection;
        if (extend || toggle)
            newSelection = mScene->worldDocument()->selectedRoads();
        if (mClickedItem) {
            if (toggle && newSelection.contains(mClickedItem->road()))
                newSelection.removeOne(mClickedItem->road());
            else if (!newSelection.contains(mClickedItem->road()))
                newSelection += mClickedItem->road();
        }
        mScene->worldDocument()->setSelectedRoads(newSelection);
        break;
    }
    case Selecting:
        updateSelection(event);
        mScene->removeItem(mSelectionRectItem);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case CancelMoving:
        mMode = NoMode;
        break;
    }

    mMousePressed = false;
    mClickedItem = 0;
}

void CellSelectMoveRoadTool::roadAboutToBeRemoved(int index)
{
    if (mMode == Moving) {
        Road *road = mScene->world()->roads().at(index);
        if (mMovingRoads.contains(road)) {
            mScene->itemForRoad(road)->setDragging(false);
            mMovingRoads.removeAll(road);
            if (mMovingRoads.isEmpty())
                mMode = CancelMoving;
        }
    }
}

void CellSelectMoveRoadTool::startSelecting()
{
    mMode = Selecting;
    mScene->addItem(mSelectionRectItem);
}

void CellSelectMoveRoadTool::updateSelection(QGraphicsSceneMouseEvent *event)
{
    QPointF start = mStartScenePos;
    QPointF end = event->scenePos();
    QRectF bounds = QRectF(start, end).normalized();

    bool toggle = event->modifiers() & Qt::ControlModifier;
    bool extend = event->modifiers() & Qt::ShiftModifier;

    QList<Road*> selection;
    if (extend || toggle)
        selection = mScene->worldDocument()->selectedRoads();

    foreach (Road *road, mScene->roadsInRect(bounds)) {
        if (toggle && selection.contains(road))
            selection.removeOne(road);
        else if (!selection.contains(road))
            selection += road;
    }

    mScene->worldDocument()->setSelectedRoads(selection);
}

void CellSelectMoveRoadTool::startMoving()
{
    mMovingRoads = mScene->worldDocument()->selectedRoads();

    // Move only the clicked item, if it was not part of the selection
    if (!mMovingRoads.contains(mClickedItem->road())) {
        mMovingRoads.clear();
        mMovingRoads += mClickedItem->road();
        mScene->worldDocument()->setSelectedRoads(mMovingRoads);
    }

    mMode = Moving;
}

void CellSelectMoveRoadTool::updateMovingItems(const QPointF &pos,
                                      Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)

    QPoint startPos = mScene->pixelToRoadCoords(mStartScenePos);
    mDropRoadPos = mScene->pixelToRoadCoords(pos);

    foreach (Road *road, mMovingRoads) {
        CellRoadItem *item = mScene->itemForRoad(road);
        item->setDragging(true);
        item->setDragOffset(mDropRoadPos - startPos);
    }
}

void CellSelectMoveRoadTool::finishMoving(const QPointF &pos)
{
    Q_UNUSED(pos)
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    foreach (Road *road, mMovingRoads)
        mScene->itemForRoad(road)->setDragging(false);

    QPoint startPos = mScene->pixelToRoadCoords(mStartScenePos);
    QPoint dropPos = mDropRoadPos;
    QPoint diff = dropPos - startPos;
    if (startPos != dropPos) {
        QUndoStack *undoStack = mScene->worldDocument()->undoStack();
        int count = mMovingRoads.size();
        undoStack->beginMacro(tr("Move %1 Road%2").arg(count).arg(QLatin1String((count > 1) ? "s" : "")));
        foreach (Road *road, mMovingRoads) {
            mScene->worldDocument()->changeRoadCoords(road,
                                                      road->start() + diff,
                                                      road->end() + diff);
        }
        undoStack->endMacro();
    }

    mMovingRoads.clear();
}

CellRoadItem *CellSelectMoveRoadTool::topmostItemAt(const QPointF &scenePos)
{
    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (CellRoadItem *roadItem = dynamic_cast<CellRoadItem*>(item))
            return roadItem;
    }
    return 0;
}

/////

SINGLETON_IMPL(CreatePointObjectTool)
SINGLETON_IMPL(CreatePolygonObjectTool)
SINGLETON_IMPL(CreatePolylineObjectTool)

AbstractCreatePolygonObjectTool::AbstractCreatePolygonObjectTool(ObjectGeometryType type)
    : BaseCellSceneTool(QString(),
                        QIcon(QLatin1String(":/images/22x22/road-tool-edit.png")),
                        QKeySequence())
    , mGeometryType(type)
    , mPathItem(nullptr)
{
    switch (mGeometryType) {
    case ObjectGeometryType::INVALID:
        break;
    case ObjectGeometryType::Point:
        setName(tr("Create Object (Point)"));
        break;
    case ObjectGeometryType::Polygon:
        setName(tr("Create Object (Polygon)"));
        break;
    case ObjectGeometryType::Polyline:
        setName(tr("Create Object (Polyline)"));
        break;
    }
}

AbstractCreatePolygonObjectTool::~AbstractCreatePolygonObjectTool()
{
    delete mPathItem;
}

void AbstractCreatePolygonObjectTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene)
        mScene->worldDocument()->disconnect(this);

    mScene = scene ? scene->asCellScene() : nullptr;
}

void AbstractCreatePolygonObjectTool::activate()
{
    BaseCellSceneTool::activate();
}

void AbstractCreatePolygonObjectTool::deactivate()
{
    if (mPathItem != nullptr) {
        mScene->removeItem(mPathItem);
        delete mPathItem;
        mPathItem = nullptr;
        mPointItems.clear();
    }
    mPolygon.clear();
    BaseCellSceneTool::deactivate();
}

void AbstractCreatePolygonObjectTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && mPathItem) {
        mPolygon.clear();
        event->accept();
    }
}

void AbstractCreatePolygonObjectTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        addPoint(event->scenePos());
        updatePathItem();
    }
    if (event->button() == Qt::RightButton) {
        finishItem();
    }
}

void AbstractCreatePolygonObjectTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    mScenePos = event->scenePos();
    updatePathItem();
}

void AbstractCreatePolygonObjectTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
    }
    if (mNewObject && (event->buttons() == Qt::NoButton)) {
        selectNewObject(mNewObject, event->modifiers());
    }
    mNewObject = nullptr;
}

void AbstractCreatePolygonObjectTool::finishItem()
{
    WorldObjectGroup *og = mScene->document()->currentObjectGroup();
    if (og && og->type() && typeRequiresRectangle(og->type()->name())) {
        QMessageBox::warning(
                    MainWindow::instance(), tr("Rectangle required"),
                    tr("%1 must be created with the rectangle object tool. Polygon, polyline and point geometry cannot be exported for this zone type.")
                    .arg(og->type()->name()));
        mPolygon.clear();
        updatePathItem();
        return;
    }

    switch (mGeometryType) {
    case ObjectGeometryType::INVALID:
        break;
    case ObjectGeometryType::Point:
        break;
    case ObjectGeometryType::Polygon:
        if (mPolygon.size() > 2) {
            WorldCellObject* object = new WorldCellObject(mScene->cell(),
                                                          QString(), og->type(), og,
                                                          mPolygon[0].x(), mPolygon[0].y(),
                                                          mScene->document()->currentLevel(),
                                                          MIN_OBJECT_SIZE, MIN_OBJECT_SIZE);
            WorldCellObjectPoints points;
            for (const QPoint& point : std::as_const(mPolygon)) {
                points += WorldCellObjectPoint(point.x(), point.y());
            }
            object->setGeometryType(mGeometryType);
            object->setPoints(points);
            object->calculateBounds();
            mScene->worldDocument()->addCellObject(mScene->cell(), mScene->cell()->objects().size(), object);
            mNewObject = object;
        }
        break;
    case ObjectGeometryType::Polyline:
        if (mPolygon.size() > 1) {
            WorldCellObject* object = new WorldCellObject(mScene->cell(),
                                                          QString(), og->type(), og,
                                                          mPolygon[0].x(), mPolygon[0].y(),
                                                          mScene->document()->currentLevel(),
                                                          MIN_OBJECT_SIZE, MIN_OBJECT_SIZE);
            WorldCellObjectPoints points;
            for (const QPoint& point : std::as_const(mPolygon)) {
                points += WorldCellObjectPoint(point.x(), point.y());
            }
            object->setGeometryType(mGeometryType);
            object->setPoints(points);
            object->calculateBounds();
            mScene->worldDocument()->addCellObject(mScene->cell(), mScene->cell()->objects().size(), object);
            mNewObject = object;
        }
        break;
    }

    mPolygon.clear();
    updatePathItem();
}

void AbstractCreatePolygonObjectTool::selectNewObject(WorldCellObject *obj, Qt::KeyboardModifiers modifiers)
{
    if (!(modifiers & Qt::ControlModifier)) {
        ToolManager::instance()->selectTool(SelectMoveObjectTool::instance());
        mScene->document()->setSelectedObjects(QList<WorldCellObject*>() << obj);
    }
}

void AbstractCreatePolygonObjectTool::updatePathItem()
{
    QPainterPath path;
    int level = mScene->document()->currentLevel();

    if (mGeometryType == ObjectGeometryType::Polygon) {
        if (mPolygon.size() > 2) {
            path.addPolygon(mScene->renderer()->tileToPixelCoords(mPolygon, level));
        }
    }
#if 0
    if (mPolygon.isEmpty()) {
        QPointF tilePos = mScene->renderer()->pixelToTileCoordsNearest(mScenePos);
        QPointF scenePos = mScene->renderer()->tileToPixelCoords(tilePos);
        path.addRect(scenePos.x() - 5, scenePos.y() - 5, 10, 10);
    } else {
        for (const QPoint& point : std::as_const(mPolygon)) {
            QPointF p = mScene->renderer()->tileToPixelCoords(point);
            path.addRect(p.x() - 5, p.y() - 5, 10, 10);
        }
    }
#endif
    if (!mPolygon.isEmpty()) {
        QPointF p1 = mScene->renderer()->tileToPixelCoords(mPolygon[0], level);
        path.moveTo(p1);
        for (int i = 1; i < mPolygon.size(); i++) {
            QPointF p2 = mScene->renderer()->tileToPixelCoords(mPolygon[i], level);
            path.lineTo(p2);
        }

        // Line to mouse pointer
        QPointF p2 = mScene->renderer()->pixelToTileCoordsNearest(mScenePos, level);
        p2 = mScene->renderer()->tileToPixelCoords(p2, level);
        path.lineTo(p2);
    }

    if (mPathItem == nullptr) {
        mPathItem = new QGraphicsPathItem();
        QPen pen(Qt::blue);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        pen.setWidth(3);
        pen.setCosmetic(true);
        mPathItem->setPen(pen);
        mPathItem->setZValue(mScene->ZVALUE_GRID + 1);
        mScene->addItem(mPathItem);
    }

    mPathItem->setPath(path);

    // Add an item for each new point.
    for (int i = 0; i < mPolygon.size(); i++) {
        QPoint point = mPolygon[i];
        if (i >= mPointItems.size()) {
            QGraphicsRectItem *item = new QGraphicsRectItem(-5, -5, 10, 10, mPathItem);
            item->setBrush(Qt::blue);
            item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            item->setPos(mScene->renderer()->tileToPixelCoords(point.x(), point.y(), level));
            mPointItems += item;
        }
    }

    // Remove excess point items.
    for (int i = mPolygon.size() + 1; i < mPointItems.size(); i++) {
        delete mPointItems.takeAt(i--);
    }

    // Create and/or update the point at the cursor position.
    if (mPointItems.size() == mPolygon.size()) {
        QGraphicsRectItem *item = new QGraphicsRectItem(-5, -5, 10, 10, mPathItem);
        item->setBrush(Qt::blue);
        item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        mPointItems += item;
    }
    QPointF tilePos = mScene->renderer()->pixelToTileCoordsNearest(mScenePos, level);
    QPointF scenePos = mScene->renderer()->tileToPixelCoords(tilePos, level);
    mPointItems.last()->setPos(scenePos);
}

void AbstractCreatePolygonObjectTool::addPoint(const QPointF &scenePos)
{
    int level = mScene->document()->currentLevel();
    if (mGeometryType == ObjectGeometryType::Point) {
        WorldObjectGroup *og = mScene->document()->currentObjectGroup();
        if (og && og->type() && typeRequiresRectangle(og->type()->name())) {
            QMessageBox::warning(
                        MainWindow::instance(), tr("Rectangle required"),
                        tr("%1 must be created with the rectangle object tool. Point geometry cannot be exported for this zone type.")
                        .arg(og->type()->name()));
            return;
        }
        QPointF cellPos = mScene->renderer()->pixelToTileCoordsNearest(scenePos, level);
        WorldCellObjectPoints points;
        points += WorldCellObjectPoint(cellPos.x(), cellPos.y());
        WorldCellObject* object = new WorldCellObject(mScene->cell(),
                                                      QString(), og->type(), og,
                                                      points[0].x, points[0].y,
                                                      level,
                                                      MIN_OBJECT_SIZE, MIN_OBJECT_SIZE);
        object->setGeometryType(mGeometryType);
        object->setPoints(points);
        object->calculateBounds();
        mScene->worldDocument()->addCellObject(mScene->cell(), mScene->cell()->objects().size(), object);
        return;
    }

    QPoint tilePos = mScene->renderer()->pixelToTileCoordsNearest(scenePos, level);
    if ((mPolygon.isEmpty() == false) && (mPolygon[0] == tilePos)) {
        // TODO: Allow Polyline to end where it starts?
        finishItem();
        return;
    }
    if (mPolygon.contains(tilePos)) {
        return;
    }

    mPolygon += mScene->renderer()->pixelToTileCoordsNearest(scenePos, level);
}

/////

SINGLETON_IMPL(EditPolygonObjectTool)

EditPolygonObjectTool::EditPolygonObjectTool()
    : BaseCellSceneTool(tr("Edit Object Points"),
                         QIcon(QLatin1String(":/images/24x24/tool-edit-polygons.png")),
                         QKeySequence())
    , mSelectedObjectItem(nullptr)
    , mSelectedObject(nullptr)
    , mRectItem(nullptr)
{
}

EditPolygonObjectTool::~EditPolygonObjectTool()
{
    delete mRectItem;
}

void EditPolygonObjectTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene) {
        mScene->worldDocument()->disconnect(this);
        mScene->document()->disconnect(this);
    }

    mScene = scene ? scene->asCellScene() : nullptr;

    if (mScene) {
        connect(mScene->worldDocument(), &WorldDocument::cellObjectAboutToBeRemoved,
                this, &EditPolygonObjectTool::cellObjectAboutToBeRemoved);
        connect(mScene->worldDocument(), &WorldDocument::cellObjectMoved,
                this, &EditPolygonObjectTool::cellObjectMoved);
        connect(mScene->worldDocument(), &WorldDocument::cellObjectPointMoved,
                this, &EditPolygonObjectTool::cellObjectPointMoved);
        connect(mScene->worldDocument(), &WorldDocument::cellObjectPointsChanged,
                this, &EditPolygonObjectTool::cellObjectPointsChanged);

        connect(mScene->document(), &CellDocument::selectedObjectsChanged,
                this, &EditPolygonObjectTool::selectedObjectsChanged);
        connect(mScene->document(), &CellDocument::selectedObjectPointsChanged,
                this, &EditPolygonObjectTool::selectedObjectPointsChanged);
    }
}

void EditPolygonObjectTool::activate()
{
    BaseCellSceneTool::activate();
}

void EditPolygonObjectTool::deactivate()
{
    if (mSelectedObjectItem) {
        mSelectedObjectItem->setEditable(false);
        for (ObjectPointHandle* handle : std::as_const(mHandles)) {
            mScene->removeItem(handle);
            delete handle;
        }
        mHandles.clear();
        mSelectedObjectItem = nullptr;
        mSelectedObject = nullptr;
        mScene->document()->setSelectedObjectPoints(QList<int>());
    }

    if (mRectItem != nullptr) {
        mScene->removeItem(mRectItem);
        delete mRectItem;
        mRectItem = nullptr;
    }

    BaseCellSceneTool::deactivate();
}

void EditPolygonObjectTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape)) {
        event->accept();
    }
}

void EditPolygonObjectTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        ObjectItem* clickedItem = nullptr;
        const QList<QGraphicsItem*> items = mScene->items(event->scenePos());
        for (QGraphicsItem *item : items) {
            if (ObjectItem *objectItem = dynamic_cast<ObjectItem*>(item)) {
                if (objectItem->isAdjacent())
                    continue;
                if (objectItem->isRectangle())
                    continue;
                clickedItem = objectItem;
                break;
            }
        }
        if ((clickedItem != nullptr) && (clickedItem == mSelectedObjectItem)) {
            if (mSelectedObjectItem->mAddPointIndex != -1) {
                QPointF tilePos = mScene->renderer()->pixelToTileCoordsNearest(mSelectedObjectItem->mAddPointPos, mSelectedObjectItem->mObject->level());
                WorldCellObjectPoint point(tilePos.x(), tilePos.y());
                if (mSelectedObject->points().contains(point)) {
                    return;
                }
                WorldCellObjectPoints coords = mSelectedObject->points();
                coords.insert(mSelectedObjectItem->mAddPointIndex + 1, point);
                mScene->worldDocument()->setCellObjectPoints(mScene->cell(), mSelectedObject->index(), coords);
            }
            return;
        }
        if (clickedItem == nullptr)
            mScene->setSelectedObjectItems(QSet<ObjectItem*>());
        else
            mScene->setSelectedObjectItems(QSet<ObjectItem*>() << clickedItem);
        mScene->document()->setSelectedObjectPoints(QList<int>());
    }
    if (event->button() == Qt::RightButton) {
    }
}

void EditPolygonObjectTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
    if (mSelectedObjectItem && mSelectedObjectItem->mAddPointIndex != -1) {
        if (mRectItem == nullptr) {
            mRectItem = new QGraphicsRectItem();
            mRectItem->setBrush(Qt::red);
            mRectItem->setRect(0 - 5, 0 - 5, 10, 10);
            mRectItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            mRectItem->setZValue(CellScene::ZVALUE_GRID + 1);
            scene()->addItem(mRectItem);
        }
        QPointF tilePos = mScene->renderer()->pixelToTileCoordsNearest(mSelectedObjectItem->mAddPointPos, mSelectedObjectItem->mObject->level());
        QPointF scenePos = mScene->renderer()->tileToPixelCoords(tilePos, mSelectedObjectItem->mObject->level());
        mRectItem->setPos(scenePos);
    } else {
        if (mRectItem) {
            delete mRectItem;
            mRectItem = nullptr;
        }
    }
}

void EditPolygonObjectTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
    }
}

// The object being edited could be deleted via undo/redo.
void EditPolygonObjectTool::cellObjectAboutToBeRemoved(WorldCell* cell, int objectIndex)
{
    WorldCellObject* object = cell->objects().at(objectIndex);
    if (object == mSelectedObject) {
        setSelectedItem(nullptr);
        mScene->document()->setSelectedObjectPoints(QList<int>());
    }
}

void EditPolygonObjectTool::cellObjectMoved(WorldCellObject *object)
{
    if (object == mSelectedObject) {
        setSelectedItem(mSelectedObjectItem);
    }
}

void EditPolygonObjectTool::cellObjectPointMoved(WorldCell* cell, int objectIndex, int pointIndex)
{
    Q_UNUSED(pointIndex)
    WorldCellObject* object = cell->objects().at(objectIndex);
    if (object == mSelectedObject) {
//        mSelectedObjectItem->synchWithObject();
        setSelectedItem(mSelectedObjectItem);
    }
}

void EditPolygonObjectTool::cellObjectPointsChanged(WorldCell *cell, int objectIndex)
{
    WorldCellObject* object = cell->objects().at(objectIndex);
    if (object == mSelectedObject) {
        setSelectedItem(mSelectedObjectItem);
    }
}

void EditPolygonObjectTool::selectedObjectsChanged()
{
    auto& selected = mScene->document()->selectedObjects();
    if (selected.size() == 1 && isCurrent()) {
        WorldCellObject* object = selected.first();
        if (ObjectItem* item = mScene->itemForObject(object)) {
            setSelectedItem(item);
        }
    } else {
        setSelectedItem(nullptr);
    }
}

void EditPolygonObjectTool::selectedObjectPointsChanged()
{
    for (auto* handle : std::as_const(mHandles)) {
        handle->update();
    }
}

void EditPolygonObjectTool::setSelectedItem(ObjectItem *objectItem)
{
    if (mSelectedObjectItem) {
        // The item may have been deleted if inside objectAboutToBeRemoved()
        if (mScene->itemForObject(mSelectedObject)) {
            for (auto* handle : std::as_const(mHandles)) {
                mScene->removeItem(handle);
                delete handle;
            }
            mSelectedObjectItem->setEditable(false);
            mSelectedObjectItem->setZValue(CellScene::ZVALUE_ROADITEM_UNSELECTED);
        }
        mHandles.clear();
        mSelectedObjectItem = nullptr;
        mSelectedObject = nullptr;
    }

    if (objectItem) {
        objectItem->mSyncing = true;

        auto createHandle = [&](int pointIndex) {
            ObjectPointHandle* handle = new ObjectPointHandle(objectItem, pointIndex);
            WorldCellObjectPoint point = handle->geometryPoint();
            handle->setPos(mScene->renderer()->tileToPixelCoords(point.x, point.y, objectItem->mObject->level()));
//            mScene->addItem(handle);
            mHandles += handle;
        };
        switch (objectItem->object()->geometryType()) {
        case ObjectGeometryType::INVALID:
            break;
        case ObjectGeometryType::Point:
            for (int i = 0; i < objectItem->object()->points().size(); i++) {
                createHandle(i);
            }
            break;
        case ObjectGeometryType::Polygon:
            for (int i = 0; i < objectItem->object()->points().size(); i++) {
                createHandle(i);
            }
            break;
        case ObjectGeometryType::Polyline:
            for (int i = 0; i < objectItem->object()->points().size(); i++) {
                createHandle(i);
            }
            break;
        }

        mSelectedObjectItem = objectItem;
        mSelectedObject = objectItem->object();
        mSelectedObjectItem->setEditable(true);
        mSelectedObjectItem->setZValue(CellScene::ZVALUE_ROADITEM_CREATING);

        objectItem->mSyncing = false;
    }
}

/////

#include "worldscene.h"

BaseWorldSceneTool::BaseWorldSceneTool(const QString &name, const QIcon &icon, const QKeySequence &shortcut, QObject *parent)
    : AbstractTool(name, icon, shortcut, WorldToolType, parent)
    , mScene(0)
    , mEventView(0)
{

}

BaseWorldSceneTool::~BaseWorldSceneTool()
{
}

void BaseWorldSceneTool::setScene(BaseGraphicsScene *scene)
{
    mScene = scene ? scene->asWorldScene() : 0;
}

BaseGraphicsScene *BaseWorldSceneTool::scene() const
{
    return mScene;
}

void BaseWorldSceneTool::activate()
{
}

void BaseWorldSceneTool::deactivate()
{
}

void BaseWorldSceneTool::updateEnabledState()
{
    setEnabled(mScene != 0);
}

void BaseWorldSceneTool::setEventView(BaseGraphicsView *view)
{
    mEventView = view;
}

QPointF BaseWorldSceneTool::restrictDragging(const QVector<QPoint> &cellPositions,
                                             const QPointF &startScenePos,
                                             const QPointF &currentScenePos)
{
    // Snap-to-grid
    QPointF pt = mScene->pixelToCellCoords(startScenePos);
    qreal x1 = pt.x() - (int)pt.x();
    qreal y1 = pt.y() - (int)pt.y();

    pt = mScene->pixelToCellCoords(currentScenePos);

    // Pick the correct quadrant of the mouse-over cell depending on
    // the quadrant the hot-spot is in
    qreal x2 = pt.x() - int(pt.x());
    qreal y2 = pt.y() - int(pt.y());
    if (x2 - x1 > 0.5)
        pt.setX(pt.x() + 1);
    else if (x2 - x1 < -0.5)
        pt.setX(pt.x() - 1);
    if (y2 - y1 > 0.5)
        pt.setY(pt.y() + 1);
    else if (y2 - y1 < -0.5)
        pt.setY(pt.y() - 1);

    Q_ASSERT(mEventView);
    qreal frac = 0.25, halfFrac = frac/2;
    qreal scale = qMin(qMax(frac, mEventView->zoomable()->scale()),1.0);
    QPolygonF poly = mScene->cellRectToPolygon(QRectF(int(pt.x())+x1-halfFrac/scale,
                                                      int(pt.y())+y1-halfFrac/scale,
                                                      frac/scale, frac/scale));
    if (poly.containsPoint(currentScenePos, Qt::OddEvenFill))
        pt = mScene->cellToPixelCoords(int(pt.x())+x1, int(pt.y())+y1);
    else
        pt = currentScenePos;

    // Restrict the drop position so the cells stay in bounds
    QPoint startCellPos = mScene->pixelToCellCoordsInt(startScenePos);
    QPoint dropCellPos = mScene->pixelToCellCoordsInt(currentScenePos);
    QPoint deltaCellPos = dropCellPos - startCellPos;

    QRect cellBounds = QRect(cellPositions.first(), QSize(1, 1)).translated(deltaCellPos);
    foreach (QPoint cellPos, cellPositions)
        cellBounds |= QRect(cellPos, QSize(1, 1)).translated(deltaCellPos);

    QRect worldBounds = mScene->world()->bounds();
    if (!worldBounds.contains(cellBounds, true)) {
        if (cellBounds.left() < worldBounds.left())
            dropCellPos += QPoint(worldBounds.left() - cellBounds.left(), 0);
        if (cellBounds.top() < worldBounds.top())
            dropCellPos += QPoint(0, worldBounds.top() - cellBounds.top());
        if (cellBounds.right() > worldBounds.right())
            dropCellPos += QPoint(worldBounds.right() - cellBounds.right(), 0);
        if (cellBounds.bottom() > worldBounds.bottom())
            dropCellPos += QPoint(0, worldBounds.bottom() - cellBounds.bottom());

//        QPointF pt = mScene->pixelToCellCoords(startScenePos);
//        qreal x1 = pt.x() - (int)pt.x();
//        qreal y1 = pt.y() - (int)pt.y();
        return mScene->cellToPixelCoords(dropCellPos + QPointF(x1, y1));
    }
    return pt;
}

/////

WorldCellTool *WorldCellTool::mInstance = 0;

WorldCellTool *WorldCellTool::instance()
{
    if (!mInstance)
        mInstance = new WorldCellTool();
    return mInstance;
}

void WorldCellTool::deleteInstance()
{
    delete mInstance;
}

WorldCellTool::WorldCellTool()
    : BaseWorldSceneTool(tr("Select and Move Cells"),
                         QIcon(QLatin1String(":/images/22x22/stock-tool-rect-select.png")),
                         QKeySequence())
    , mMode(NoMode)
    , mMousePressed(false)
    , mSelectionRectItem(new QGraphicsPolygonItem)
{
    mSelectionRectItem->setZValue(1000);
    mSelectionRectItem->setPen(QColor(0x33,0x99,0xff));
    mSelectionRectItem->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
}

WorldCellTool::~WorldCellTool()
{
    delete mSelectionRectItem;
}

void WorldCellTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && (mMode == Moving)) {
        mMode = CancelMoving;

        qDeleteAll(mDnDItems);
        mDnDItems.clear();

        foreach (WorldCell *cell, mMovingCells)
            mScene->itemForCell(cell)->setVisible(true);
        mMovingCells.clear();
        event->accept();
    }
}

void WorldCellTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        if (mMode != NoMode) // Ignore additional presses during select/move
            break;
        mMousePressed = true;
        mStartScenePos = event->scenePos();
        mDropTilePos = mScene->pixelToCellCoordsInt(mStartScenePos);
        mClickedItem = topmostItemAt(mStartScenePos);
        break;
    case Qt::RightButton:
        if (mMode == NoMode) {
            showContextMenu(event->scenePos(), event->screenPos());
        }
        // Right-clicks exits moving, same as the Escape key.
        if (mMode == Moving) {
            mMode = CancelMoving;
            qDeleteAll(mDnDItems);
            mDnDItems.clear();
            foreach (WorldCell *cell, mMovingCells)
                mScene->itemForCell(cell)->setVisible(true);
            mMovingCells.clear();
        }
        break;
    default:
        break;
    }
}

void WorldCellTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (mMode == NoMode && mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (mClickedItem &&
                    mScene->worldDocument()->selectedCells().contains(mClickedItem->cell()) &&
                    !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
                startMoving();
            else
                startSelecting();
        }
    }

    switch (mMode) {
    case Selecting:
    {
        QPointF start = mScene->pixelToCellCoords(mStartScenePos);
        QPointF end = mScene->pixelToCellCoords(event->scenePos());
        QRectF bounds = QRectF(start, end).normalized();
        mSelectionRectItem->setPolygon(mScene->cellRectToPolygon(bounds));
        break;
    }
    case Moving:
        updateMovingItems(event->scenePos(), event->modifiers());
        break;
    case NoMode:
        break;
    case CancelMoving:
        break;
    }
}

void WorldCellTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
    {
        bool toggle = event->modifiers() & Qt::ControlModifier;
        bool extend = event->modifiers() & Qt::ShiftModifier;
        QList<WorldCell*> newSelection;
        if (extend || toggle)
            newSelection = mScene->worldDocument()->selectedCells();
        if (mClickedItem) {
            if (toggle && newSelection.contains(mClickedItem->cell()))
                newSelection.removeOne(mClickedItem->cell());
            else if (!newSelection.contains(mClickedItem->cell()))
                newSelection += mClickedItem->cell();
        }
        mScene->worldDocument()->setSelectedCells(newSelection);
        break;
    }
    case Selecting:
        updateSelection(event);
        mScene->removeItem(mSelectionRectItem);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case CancelMoving:
        mMode = NoMode;
        break;
    }

    mMousePressed = false;
    mClickedItem = 0;
}

void WorldCellTool::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    mClickedItem = topmostItemAt(event->scenePos());
}

void WorldCellTool::startSelecting()
{
    mMode = Selecting;
    mScene->addItem(mSelectionRectItem);
}

void WorldCellTool::updateSelection(QGraphicsSceneMouseEvent *event)
{
    QPointF start = mScene->pixelToCellCoords(mStartScenePos);
    QPointF end = mScene->pixelToCellCoords(event->scenePos());
    QRectF bounds = QRectF(start, end).normalized();

    bool toggle = event->modifiers() & Qt::ControlModifier;
    bool extend = event->modifiers() & Qt::ShiftModifier;

    QList<WorldCell*> selection;
    if (extend || toggle)
        selection = mScene->worldDocument()->selectedCells();
    for (int y = bounds.top(); y <= bounds.bottom(); y++) {
        for (int x = bounds.left(); x <= bounds.right(); x++) {
            if (WorldCell *cell = mScene->world()->cellAt(x, y)) {
                if (toggle && selection.contains(cell))
                    selection.removeOne(cell);
                else if (!selection.contains(cell))
                    selection += cell;
            }
        }
    }
    mScene->worldDocument()->setSelectedCells(selection);
}

void WorldCellTool::startMoving()
{
    mMovingCells = mScene->worldDocument()->selectedCells();

    // Move only the clicked item, if it was not part of the selection
    if (!mMovingCells.contains(mClickedItem->cell())) {
        mMovingCells.clear();
        mMovingCells += mClickedItem->cell();
        mScene->worldDocument()->setSelectedCells(mMovingCells);
    }

    mMode = Moving;

    foreach (WorldCell *cell, mMovingCells) {
        mScene->itemForCell(cell)->setVisible(false);
        DragCellItem *dndItem = new DragCellItem(cell, mScene);
        mDnDItems.append(dndItem);
        dndItem->setZValue(1000);
        mScene->addItem(dndItem);
    }
}

void WorldCellTool::updateMovingItems(const QPointF &pos,
                                      Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)
#if 0
    // Snap-to-grid
    QPointF pt = mScene->pixelToCellCoords(mStartScenePos);
    qreal x1 = pt.x() - (int)pt.x();
    qreal y1 = pt.y() - (int)pt.y();

    pt = mScene->pixelToCellCoords(pos);

    // Pick the correct quadrant of the mouse-over cell depending on
    // the quadrant the hot-spot is in
    qreal x2 = pt.x() - int(pt.x());
    qreal y2 = pt.y() - int(pt.y());
    if (x2 - x1 > 0.5)
        pt.setX(pt.x() + 1);
    else if (x2 - x1 < -0.5)
        pt.setX(pt.x() - 1);
    if (y2 - y1 > 0.5)
        pt.setY(pt.y() + 1);
    else if (y2 - y1 < -0.5)
        pt.setY(pt.y() - 1);

    Q_ASSERT(mEventView);
    qreal frac = 0.25, halfFrac = frac/2;
    qreal scale = qMin(qMax(frac, mEventView->zoomable()->scale()),1.0);
    QPolygonF poly = mScene->cellRectToPolygon(QRectF(int(pt.x())+x1-halfFrac/scale,
                                                      int(pt.y())+y1-halfFrac/scale,
                                                      frac/scale, frac/scale));
    if (poly.containsPoint(pos, Qt::OddEvenFill))
        pt = mScene->cellToPixelCoords(int(pt.x())+x1, int(pt.y())+y1);
    else
        pt = pos;
#endif
    // Restrict the drop position so the cells stay in bounds
    QVector<QPoint> cellPositions(mDnDItems.size());
    cellPositions.clear();
    foreach (DragCellItem *item, mDnDItems)
        cellPositions.append(item->cellPos());
    QPointF pt = restrictDragging(cellPositions, mStartScenePos, pos);

    foreach (DragCellItem *item, mDnDItems)
        item->setDragOffset(pt - mStartScenePos);

    mDropTilePos = mScene->pixelToCellCoordsInt(pt);
}

void WorldCellTool::finishMoving(const QPointF &pos)
{
    Q_UNUSED(pos)
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    QPoint startCellPos = mScene->pixelToCellCoordsInt(mStartScenePos);
    QPoint dropCellPos = mDropTilePos;
    if (startCellPos != dropCellPos) {
        const QPoint cellOffset = dropCellPos - startCellPos;
        QUndoStack *undoStack = mScene->worldDocument()->undoStack();
        int count = mMovingCells.size();
        undoStack->beginMacro(tr("Move %1 Cell%2").arg(count).arg(QLatin1String((count > 1) ? "s" : "")));
        undoStack->push(new ProgressBegin(tr("Moving Cells"))); // in case of multiple loadMap() calls
        mOrderedMovingCells.clear();
        foreach (WorldCell *cell, mMovingCells)
            pushCellToMove(cell, cellOffset);
        QList<WorldCell*> newSelection;
        foreach (WorldCell *cell, mOrderedMovingCells) {
            QPoint newPos = cell->pos() + cellOffset;
            mScene->worldDocument()->moveCell(cell, newPos);
            newSelection += mScene->world()->cellAt(newPos);
        }
        MainWindow::instance()->moveCellCoordinateData(
                    mScene->worldDocument(), mMovingCells, cellOffset);
        mScene->worldDocument()->setSelectedCells(newSelection, true);
        undoStack->push(new ProgressEnd(tr("Undoing Move Cells"))); // in case of multiple loadMap() calls
        undoStack->endMacro();
    }

    qDeleteAll(mDnDItems);
    mDnDItems.clear();

    foreach (WorldCell *cell, mMovingCells)
        mScene->itemForCell(cell)->setVisible(true);
    mMovingCells.clear();
}

void WorldCellTool::pushCellToMove(WorldCell *cell, const QPoint &offset)
{
    if (mOrderedMovingCells.contains(cell))
        return;
    QPoint newPos = cell->pos() + offset;
    foreach (WorldCell *test, mMovingCells) {
        if (test == cell)
            continue;
        if (test->pos() == newPos)
            pushCellToMove(test, offset);
    }
    mOrderedMovingCells += cell;
}

void WorldCellTool::showContextMenu(const QPointF &scenePos, const QPoint &screenPos)
{
    WorldCellItem *item = topmostItemAt(scenePos);
    if (!item)
        return;

    QMenu menu;
    QIcon tiledIcon(QLatin1String(":images/tiled-icon-16.png"));
    QAction *openAction = menu.addAction(tiledIcon, tr("Open in TileZed"));
    QAction *showThumbnailAction = menu.addAction(tr("Show Thumbnail"));
    showThumbnailAction->setCheckable(true);
    showThumbnailAction->setChecked(item->wantsImages());
    QAction *recreateThumbnailAction = menu.addAction(tr("Recreate Thumbnail"));
    menu.addSeparator();
    QAction *removeEmptyBorderCellsAction =
            menu.addAction(QIcon(QLatin1String(":/images/16x16/edit-delete.png")),
                           tr("Remove All Empty Border Cells..."));
    removeEmptyBorderCellsAction->setEnabled(
                MainWindow::instance()->canRemoveEmptyBorderCells());
    if (item->cell()->mapFilePath().isEmpty()) {
        openAction->setEnabled(false);
        showThumbnailAction->setEnabled(false);
        recreateThumbnailAction->setEnabled(false);
    }

    QAction *action = menu.exec(screenPos);
    if (action == removeEmptyBorderCellsAction) {
        MainWindow::instance()->removeEmptyBorderCells();
        return;
    }
    if (action == openAction) {
        openInMapEditor(item->cell()->mapFilePath(), mScene->views().value(0));
    }
    if (mScene->worldDocument()->selectedCells().contains(item->cell())) {
        const bool wantsImages = !item->wantsImages();
        int recreated = 0;
        int failed = 0;
        for (WorldCell *cell : mScene->worldDocument()->selectedCells()) {
            if (WorldCellItem *item2 = mScene->itemForCell(cell)) {
                if (action == showThumbnailAction) {
                    if (wantsImages) {
                        item2->thumbnailsAreGo();
                    } else {
                        item2->thumbnailsAreFail();
                    }
                }
                if (action == recreateThumbnailAction) {
                    if (MapImageManager::instance()->recreateMapImage(
                                item2->mapFilePath(), QString(),
                                mScene->worldDocument())) {
                        ++recreated;
                        if (!item2->wantsImages())
                            item2->thumbnailsAreGo();
                    } else {
                        ++failed;
                    }
                }
            }
        }
        if (action == recreateThumbnailAction) {
            MainWindow::instance()->statusBar()->showMessage(
                        tr("Thumbnail recreation queued for %1 selected cell(s); %2 failed.")
                        .arg(recreated).arg(failed), 10000);
        }
    } else {
        if (action == showThumbnailAction) {
            if (item->wantsImages()) {
                item->thumbnailsAreFail();
            } else {
                item->thumbnailsAreGo();
            }
        }
        if (action == recreateThumbnailAction) {
            const bool queued = MapImageManager::instance()->recreateMapImage(
                        item->mapFilePath(), QString(),
                        mScene->worldDocument());
            if (queued && !item->wantsImages())
                item->thumbnailsAreGo();
            MainWindow::instance()->statusBar()->showMessage(
                        queued ? tr("Thumbnail recreation queued for this cell.")
                               : tr("Thumbnail recreation failed: %1")
                                 .arg(MapImageManager::instance()->errorString()),
                        10000);
        }
    }
    if ((action == showThumbnailAction || action == recreateThumbnailAction) &&
            !mScene->worldDocument()->fileName().isEmpty()) {
        QSet<ThumbnailCell> visibleCells;
        for (WorldCell *cell : mScene->world()->cells()) {
            if (WorldCellItem *item2 = mScene->itemForCell(cell)) {
                if (item2->wantsImages()) {
                    visibleCells += ThumbnailCell(cell->x(), cell->y());
                }
            }
        }
        ThumbnailSettingsMgr::instance().saveWorld(mScene->worldDocument()->fileName(), visibleCells);
    }
}

WorldCellItem *WorldCellTool::topmostItemAt(const QPointF &scenePos)
{
    WorldCell *cell = mScene->pointToCell(scenePos);
    return cell ? mScene->itemForCell(cell) : 0;
}

/////

namespace {
class ZombieHeatMapStrokeCommand : public QUndoCommand
{
public:
    ZombieHeatMapStrokeCommand(ZombieSpawnImageItem *item,
                               const QImage &before,
                               const QImage &after)
        : QUndoCommand(QObject::tr("Paint Zombie Heatmap"))
        , mItem(item)
        , mBefore(before)
        , mAfter(after)
    {
    }
    void undo() override
    {
        apply(mBefore);
    }
    void redo() override
    {
        apply(mAfter);
    }
private:
    void apply(const QImage &image)
    {
        if (!mItem)
            return;
        QString error;
        if (!mItem->replaceSourceImage(image, true, &error)) {
            qWarning() << "Unable to save Zombie Heatmap edit:" << error;
            QMessageBox::warning(MainWindow::instance(),
                                 QObject::tr("Zombie Heatmap Save Failed"),
                                 error);
        }
    }
    ZombieSpawnImageItem *mItem;
    QImage mBefore;
    QImage mAfter;
};
}
ZombieHeatMapTool *ZombieHeatMapTool::mInstance = nullptr;
ZombieHeatMapTool *ZombieHeatMapTool::instance()
{
    if (!mInstance)
        mInstance = new ZombieHeatMapTool();
    return mInstance;
}
void ZombieHeatMapTool::deleteInstance()
{
    delete mInstance;
    mInstance = nullptr;
}
ZombieHeatMapTool::ZombieHeatMapTool()
    : BaseWorldSceneTool(tr("Paint Zombie Heatmap"),
                         QIcon(QLatin1String(":/images/22x22/tool-zombie-heatmap.svg")),
                         QKeySequence())
    , mPainting(false)
    , mBrushRadius(1)
    , mIntensity(6)
    , mStrokeIntensity(6)
    , mPreviewB42x40(true)
    , mCursorItem(new QGraphicsPolygonItem())
{
    mCursorItem->setZValue(2000);
    mCursorItem->setPen(QPen(QColor(255, 80, 80), 2));
    mCursorItem->setBrush(QBrush(QColor(255, 0, 0, 40)));
    mCursorItem->setVisible(false);
}
ZombieHeatMapTool::~ZombieHeatMapTool()
{
    delete mCursorItem;
}
void ZombieHeatMapTool::activate()
{
    BaseWorldSceneTool::activate();
    if (!mScene || !Preferences::instance()->showZombieSpawnImage())
        return;
    ZombieSpawnImageItem *item = mScene->zombieSpawnImageItem();
    if (item) {
        item->setPreviewB42x40(mPreviewB42x40);
        item->setVisible(Preferences::instance()->showZombieSpawnImage());
    }
    if (!mCursorItem->scene())
        mScene->addItem(mCursorItem);
    mCursorItem->setVisible(false);
    setStatusInfo(tr("Left-drag paints raw intensity; right-drag erases. "
                     "Preview: %1.")
                  .arg(mPreviewB42x40 ? tr("B42 x40") : tr("Raw 0-255")));
}
void ZombieHeatMapTool::deactivate()
{
    finishStroke();
    mCursorItem->setVisible(false);
    if (mCursorItem->scene())
        mCursorItem->scene()->removeItem(mCursorItem);
    if (mScene && mScene->zombieSpawnImageItem()) {
        mScene->zombieSpawnImageItem()->setVisible(
                    Preferences::instance()->showZombieSpawnImage());
    }
    BaseWorldSceneTool::deactivate();
}
void ZombieHeatMapTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!mScene || !Preferences::instance()->showZombieSpawnImage()
            || (event->button() != Qt::LeftButton
                    && event->button() != Qt::RightButton)) {
        return;
    }
    ZombieSpawnImageItem *item = mScene->zombieSpawnImageItem();
    if (!item)
        return;
    QString error;
    if (!item->ensureEditable(&error)) {
        QMessageBox::warning(MainWindow::instance(),
                             tr("Zombie Heatmap Cannot Be Edited"), error);
        return;
    }
    const QPoint imagePoint = item->imagePointAt(event->scenePos());
    if (!item->containsImagePoint(imagePoint))
        return;
    mBeforeStroke = item->sourceImage();
    mPainting = true;
    mStrokeIntensity = event->button() == Qt::RightButton ? 0 : mIntensity;
    mLastImagePoint = imagePoint;
    item->paintStroke(imagePoint, imagePoint, mBrushRadius, mStrokeIntensity);
    updateCursor(event->scenePos());
    event->accept();
}
void ZombieHeatMapTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!Preferences::instance()->showZombieSpawnImage()) {
        finishStroke();
        mCursorItem->setVisible(false);
        return;
    }
    updateCursor(event->scenePos());
    if (mPainting) {
        paintTo(event->scenePos(), mStrokeIntensity);
        event->accept();
    }
}
void ZombieHeatMapTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (!mPainting)
        return;
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
        return;
    paintTo(event->scenePos(), mStrokeIntensity);
    finishStroke();
    event->accept();
}
void ZombieHeatMapTool::languageChanged()
{
    setName(tr("Paint Zombie Heatmap"));
}
void ZombieHeatMapTool::updateEnabledState()
{
    setEnabled(mScene && mScene->zombieSpawnImageItem()
               && mScene->zombieSpawnImageItem()->canEdit()
               && Preferences::instance()->showZombieSpawnImage());
}
void ZombieHeatMapTool::setBrushRadius(int radius)
{
    mBrushRadius = qBound(0, radius, 64);
}
void ZombieHeatMapTool::setIntensity(int intensity)
{
    mIntensity = qBound(0, intensity, 255);
}
void ZombieHeatMapTool::setPreviewB42x40(bool enabled)
{
    mPreviewB42x40 = enabled;
    if (mScene && mScene->zombieSpawnImageItem())
        mScene->zombieSpawnImageItem()->setPreviewB42x40(enabled);
    setStatusInfo(tr("Left-drag paints raw intensity; right-drag erases. "
                     "Preview: %1.")
                  .arg(enabled ? tr("B42 x40") : tr("Raw 0-255")));
}
void ZombieHeatMapTool::expandImageToWorld()
{
    if (!mScene || !mScene->zombieSpawnImageItem())
        return;
    QString error;
    if (!mScene->zombieSpawnImageItem()->expandToWorld(&error)) {
        QMessageBox::warning(MainWindow::instance(),
                             tr("Expand Zombie Heatmap Failed"), error);
        return;
    }
    qInfo() << "Zombie Heatmap expanded to cover the WorldEd project";
}
void ZombieHeatMapTool::updateCursor(const QPointF &scenePos)
{
    if (!mScene || !mScene->zombieSpawnImageItem())
        return;
    ZombieSpawnImageItem *item = mScene->zombieSpawnImageItem();
    const QPoint imagePoint = item->imagePointAt(scenePos);
    if (!item->containsImagePoint(imagePoint)) {
        mCursorItem->setVisible(false);
        return;
    }
    const qreal samples = item->samplesPerCell();
    const QPointF center((imagePoint.x() + 0.5) / samples,
                         (imagePoint.y() + 0.5) / samples);
    const qreal radius = (mBrushRadius + 0.5) / samples;
    QPolygonF polygon;
    const int segments = 32;
    for (int i = 0; i < segments; ++i) {
        const qreal angle = (2.0 * 3.14159265358979323846 * i) / segments;
        polygon += mScene->cellToPixelCoords(
                    center + QPointF(qCos(angle) * radius,
                                     qSin(angle) * radius));
    }
    mCursorItem->setPolygon(polygon);
    mCursorItem->setVisible(true);
}
void ZombieHeatMapTool::paintTo(const QPointF &scenePos, int intensity)
{
    ZombieSpawnImageItem *item = mScene->zombieSpawnImageItem();
    const QPoint imagePoint = item->imagePointAt(scenePos);
    if (!item->containsImagePoint(imagePoint))
        return;
    item->paintStroke(mLastImagePoint, imagePoint, mBrushRadius, intensity);
    mLastImagePoint = imagePoint;
}
void ZombieHeatMapTool::finishStroke()
{
    if (!mPainting || !mScene || !mScene->zombieSpawnImageItem()) {
        mPainting = false;
        return;
    }
    ZombieSpawnImageItem *item = mScene->zombieSpawnImageItem();
    const QImage after = item->sourceImage();
    mPainting = false;
    if (mBeforeStroke == after)
        return;
    QString error;
    item->replaceSourceImage(mBeforeStroke, false, &error);
    mScene->worldDocument()->undoStack()->push(
                new ZombieHeatMapStrokeCommand(item, mBeforeStroke, after));
}
namespace {
class BiomeMapStrokeCommand : public QUndoCommand
{
public:
    BiomeMapStrokeCommand(BiomeMapItem *item,
                          const QImage &before,
                          const QImage &after,
                          const QString &description)
        : QUndoCommand(description)
        , mItem(item)
        , mBefore(before)
        , mAfter(after)
    {
    }
    void undo() override
    {
        apply(mBefore);
    }
    void redo() override
    {
        apply(mAfter);
    }
private:
    void apply(const QImage &image)
    {
        if (!mItem)
            return;
        QString error;
        if (!mItem->replaceSourceImage(image, true, &error)) {
            qWarning() << "Unable to save Biomemap edit:" << error;
            QMessageBox::warning(MainWindow::instance(),
                                 QObject::tr("Biomemap Save Failed"),
                                 error);
        }
    }
    BiomeMapItem *mItem;
    QImage mBefore;
    QImage mAfter;
};
}
BiomeMapTool *BiomeMapTool::mInstance = nullptr;
BiomeMapTool *BiomeMapTool::instance()
{
    if (!mInstance)
        mInstance = new BiomeMapTool();
    return mInstance;
}
void BiomeMapTool::deleteInstance()
{
    delete mInstance;
    mInstance = nullptr;
}
BiomeMapTool::BiomeMapTool()
    : BaseWorldSceneTool(tr("Paint Biomemap Channels"),
                         QIcon(QLatin1String(":/images/22x22/tool-biome-map.svg")),
                         QKeySequence())
    , mPainting(false)
    , mPaintChannel(BiomeChannel)
    , mBiomeBrushRadius(4)
    , mZoneBrushRadius(0)
    , mBiomeValue(255)
    , mZoneValue(64)
    , mCursorItem(new QGraphicsPolygonItem())
{
    mCursorItem->setZValue(2001);
    mCursorItem->setPen(QPen(Qt::white, 2));
    setBiomeValue(mBiomeValue);
    mCursorItem->setVisible(false);
}
BiomeMapTool::~BiomeMapTool()
{
    delete mCursorItem;
}
void BiomeMapTool::activate()
{
    BaseWorldSceneTool::activate();
    if (!mScene || !Preferences::instance()->showBiomeMap())
        return;
    BiomeMapItem *item = mScene->biomeMapItem();
    if (item)
        item->setVisible(true);
    if (item)
        item->setDisplayZoneChannel(mPaintChannel == ZoneChannel);
    if (!mCursorItem->scene())
        mScene->addItem(mCursorItem);
    mCursorItem->setVisible(false);
    updateStatusInfo();
}
void BiomeMapTool::deactivate()
{
    finishStroke();
    mCursorItem->setVisible(false);
    if (mCursorItem->scene())
        mCursorItem->scene()->removeItem(mCursorItem);
    if (mScene && mScene->biomeMapItem()) {
        mScene->biomeMapItem()->setDisplayZoneChannel(false);
        mScene->biomeMapItem()->setVisible(
                    Preferences::instance()->showBiomeMap());
    }
    BaseWorldSceneTool::deactivate();
}
void BiomeMapTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!mScene || !Preferences::instance()->showBiomeMap()
            || event->button() != Qt::LeftButton)
        return;
    BiomeMapItem *item = mScene->biomeMapItem();
    if (!item)
        return;
    QString error;
    if (!item->ensureEditable(&error)) {
        QMessageBox::warning(MainWindow::instance(),
                             tr("Biomemap Cannot Be Edited"), error);
        return;
    }
    const QPoint imagePoint = item->imagePointAt(event->scenePos());
    if (!item->containsImagePoint(imagePoint))
        return;
    mBeforeStroke = item->sourceImage();
    mPainting = true;
    mLastImagePoint = imagePoint;
    if (mPaintChannel == ZoneChannel) {
        item->paintZoneStroke(imagePoint, imagePoint,
                              mZoneBrushRadius, mZoneValue);
    } else {
        item->paintBiomeStroke(imagePoint, imagePoint,
                               mBiomeBrushRadius, mBiomeValue);
    }
    updateCursor(event->scenePos());
    event->accept();
}
void BiomeMapTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!Preferences::instance()->showBiomeMap()) {
        finishStroke();
        mCursorItem->setVisible(false);
        return;
    }
    updateCursor(event->scenePos());
    if (mPainting) {
        paintTo(event->scenePos());
        event->accept();
    }
}
void BiomeMapTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (!mPainting || event->button() != Qt::LeftButton)
        return;
    paintTo(event->scenePos());
    finishStroke();
    event->accept();
}
void BiomeMapTool::languageChanged()
{
    setName(tr("Paint Biomemap Channels"));
    updateStatusInfo();
}
void BiomeMapTool::updateEnabledState()
{
    setEnabled(mScene && mScene->biomeMapItem()
               && mScene->biomeMapItem()->canEdit()
               && Preferences::instance()->showBiomeMap());
}
int BiomeMapTool::brushRadius() const
{
    return mPaintChannel == ZoneChannel
            ? mZoneBrushRadius : mBiomeBrushRadius;
}
int BiomeMapTool::paintValue() const
{
    return mPaintChannel == ZoneChannel ? mZoneValue : mBiomeValue;
}
void BiomeMapTool::setPaintChannel(PaintChannel channel)
{
    if (mPaintChannel == channel)
        return;
    finishStroke();
    mPaintChannel = channel;
    if (mScene && mScene->biomeMapItem())
        mScene->biomeMapItem()->setDisplayZoneChannel(
                    mPaintChannel == ZoneChannel);
    if (mScene && !mScene->views().isEmpty()) {
        updateCursor(mScene->views().isEmpty()
                     ? QPointF()
                     : mScene->views().first()->mapToScene(
                           mScene->views().first()->mapFromGlobal(
                               QCursor::pos())));
    } else {
        mCursorItem->setVisible(false);
    }
    const QColor color = BiomeMapImageProcessor::displayColor(paintValue());
    mCursorItem->setBrush(QBrush(QColor(color.red(), color.green(),
                                        color.blue(), 90)));
    updateStatusInfo();
}
void BiomeMapTool::setBrushRadius(int radius)
{
    if (mPaintChannel == ZoneChannel)
        setZoneBrushRadius(radius);
    else
        setBiomeBrushRadius(radius);
}
void BiomeMapTool::setBiomeBrushRadius(int radius)
{
    mBiomeBrushRadius = qBound(0, radius, 128);
}
void BiomeMapTool::setZoneBrushRadius(int radius)
{
    mZoneBrushRadius = qBound(0, radius, 16);
}
void BiomeMapTool::setBiomeValue(int value)
{
    mBiomeValue = qBound(0, value, 255);
    if (mPaintChannel == BiomeChannel) {
        const QColor color = BiomeMapImageProcessor::displayColor(mBiomeValue);
        mCursorItem->setBrush(QBrush(QColor(color.red(), color.green(),
                                            color.blue(), 90)));
    }
    updateStatusInfo();
}
void BiomeMapTool::setZoneValue(int value)
{
    mZoneValue = qBound(0, value, 255);
    if (mPaintChannel == ZoneChannel) {
        const QColor color = BiomeMapImageProcessor::displayColor(mZoneValue);
        mCursorItem->setBrush(QBrush(QColor(color.red(), color.green(),
                                            color.blue(), 90)));
    }
    updateStatusInfo();
}
void BiomeMapTool::updateCursor(const QPointF &scenePos)
{
    if (!mScene || !mScene->biomeMapItem())
        return;
    BiomeMapItem *item = mScene->biomeMapItem();
    const QPoint imagePoint = item->imagePointAt(scenePos);
    if (!item->containsImagePoint(imagePoint)) {
        mCursorItem->setVisible(false);
        return;
    }
    QPolygonF polygon;
    const qreal pixels = item->pixelsPerCell();
    if (mPaintChannel == ZoneChannel) {
        const QRect chunkPixels = item->zoneChunkRectAt(
                    imagePoint, mZoneBrushRadius);
        const QRectF rect(chunkPixels.x() / pixels,
                          chunkPixels.y() / pixels,
                          chunkPixels.width() / pixels,
                          chunkPixels.height() / pixels);
        polygon = mScene->cellRectToPolygon(rect);
    } else {
        const QPointF center((imagePoint.x() + 0.5) / pixels,
                             (imagePoint.y() + 0.5) / pixels);
        const qreal radius = (mBiomeBrushRadius + 0.5) / pixels;
        const int segments = 32;
        for (int i = 0; i < segments; ++i) {
            const qreal angle = (2.0 * 3.14159265358979323846 * i) / segments;
            polygon += mScene->cellToPixelCoords(
                        center + QPointF(qCos(angle) * radius,
                                         qSin(angle) * radius));
        }
    }
    mCursorItem->setPolygon(polygon);
    mCursorItem->setVisible(true);
}
void BiomeMapTool::paintTo(const QPointF &scenePos)
{
    BiomeMapItem *item = mScene->biomeMapItem();
    const QPoint imagePoint = item->imagePointAt(scenePos);
    if (!item->containsImagePoint(imagePoint))
        return;
    if (mPaintChannel == ZoneChannel) {
        item->paintZoneStroke(mLastImagePoint, imagePoint,
                              mZoneBrushRadius, mZoneValue);
    } else {
        item->paintBiomeStroke(mLastImagePoint, imagePoint,
                               mBiomeBrushRadius, mBiomeValue);
    }
    mLastImagePoint = imagePoint;
}
void BiomeMapTool::finishStroke()
{
    if (!mPainting || !mScene || !mScene->biomeMapItem()) {
        mPainting = false;
        return;
    }
    BiomeMapItem *item = mScene->biomeMapItem();
    const QImage after = item->sourceImage();
    mPainting = false;
    if (mBeforeStroke == after)
        return;
    QString error;
    item->replaceSourceImage(mBeforeStroke, false, &error);
    mScene->worldDocument()->undoStack()->push(
                new BiomeMapStrokeCommand(
                    item, mBeforeStroke, after,
                    mPaintChannel == ZoneChannel
                    ? tr("Paint Biomemap Zone Layer")
                    : tr("Paint Biomemap Biome Layer")));
}
void BiomeMapTool::updateStatusInfo()
{
    const int value = paintValue();
    QString name = tr("value %1").arg(value);
    QString config;
    const BiomeMapImageProcessor::PaletteEntry *entry =
            BiomeMapImageProcessor::entryForValue(value);
    if (entry) {
        name = tr("%1 (ID %2)").arg(entry->name).arg(entry->value);
        if (mPaintChannel == ZoneChannel) {
            config = tr(" Foraging zone: %1.%2")
                    .arg(entry->zone)
                    .arg(entry->enabledByDefault
                         ? QString()
                         : tr(" Map override required."));
        } else {
            config = tr(" Biome: %1. Ore selector: %2.%3")
                    .arg(entry->biome.isEmpty() ? tr("(none)") : entry->biome)
                    .arg(entry->ore.isEmpty() ? tr("(none)") : entry->ore)
                    .arg(entry->enabledByDefault
                         ? QString()
                         : tr(" Map override required."));
        }
    }
    if (mPaintChannel == ZoneChannel) {
        setStatusInfo(tr("Left-drag paints the green Zone channel in complete "
                         "8 x 8 chunks: %1. The red Biome channel is "
                         "preserved.%2").arg(name, config));
    } else {
        setStatusInfo(tr("Left-drag paints only the red Biome channel: %1. "
                         "The green Zone channel is preserved.%2")
                      .arg(name, config));
    }
}
PasteCellsTool *PasteCellsTool::mInstance = 0;

PasteCellsTool *PasteCellsTool::instance()
{
    if (!mInstance)
        mInstance = new PasteCellsTool();
    return mInstance;
}

void PasteCellsTool::deleteInstance()
{
    delete mInstance;
}

PasteCellsTool::PasteCellsTool()
    : BaseWorldSceneTool(QLatin1String("Paste Cells"),
                         QIcon(QLatin1String(":/images/24x24/edit-paste.png")),
                         QKeySequence())
{
}

PasteCellsTool::~PasteCellsTool()
{
}

bool PasteCellsTool::validateCellPastePlacement(QString *summary,
                                                QString *error)
{
    QVector<QPoint> sourceCells;
    sourceCells << QPoint(2, 3) << QPoint(3, 3) << QPoint(2, 4);
    const QPoint sourceAnchor = topLeftCell(sourceCells, QPoint(-1, -1));
    const QPoint selectedAnchor = topLeftCell(
                QVector<QPoint>() << QPoint(7, 5), sourceAnchor);
    const QRect worldBounds(0, 0, 10, 10);
    const QPoint dropAnchor = boundedDropCell(
                sourceCells, sourceAnchor, selectedAnchor, worldBounds);
    if (sourceAnchor != QPoint(2, 3)
            || dropAnchor != QPoint(7, 5)
            || pastedCellPosition(QPoint(2, 3), sourceAnchor, dropAnchor)
               != QPoint(7, 5)
            || pastedCellPosition(QPoint(3, 3), sourceAnchor, dropAnchor)
               != QPoint(8, 5)
            || pastedCellPosition(QPoint(2, 4), sourceAnchor, dropAnchor)
               != QPoint(7, 6)) {
        if (error)
            *error = tr("The selected target cell did not anchor the paste.");
        return false;
    }
    if (boundedDropCell(sourceCells, sourceAnchor, QPoint(9, 9),
                        worldBounds) != QPoint(8, 8)) {
        if (error)
            *error = tr("The paste footprint was not kept inside the world.");
        return false;
    }
    if (summary)
        *summary = tr("selected-cell anchoring, relative multi-cell positions "
                      "and world-bound clamping verified");
    return true;
}

void PasteCellsTool::activate()
{
    BaseWorldSceneTool::activate();
    startMoving();
}

void PasteCellsTool::deactivate()
{
    BaseWorldSceneTool::deactivate();
    cancelMoving();
}

void PasteCellsTool::restart()
{
    startMoving();
}

void PasteCellsTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene) {
        mScene->disconnect(this);
    }

    mScene = scene ? scene->asWorldScene() : 0;

    if (mScene) {
        connect(Clipboard::instance(), &Clipboard::clipboardChanged,
                this, &PasteCellsTool::updateEnabledState);
    }
}

void PasteCellsTool::updateEnabledState()
{
    setEnabled(mScene && Clipboard::instance()->cellsInClipboardCount());
}

void PasteCellsTool::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        qInfo() << "Cell paste cancelled";
        ToolManager::instance()->selectTool(WorldCellTool::instance());
        if (!isCurrent()) {
            cancelMoving();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (confirmOccupiedTargets() && pasteCells())
            ToolManager::instance()->selectTool(WorldCellTool::instance());
        event->accept();
        return;
    }
    event->ignore();
}

void PasteCellsTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!updateDropPosition(event->scenePos())) {
            event->accept();
            return;
        }
        if (confirmOccupiedTargets() && pasteCells())
            ToolManager::instance()->selectTool(WorldCellTool::instance());
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        qInfo() << "Cell paste cancelled";
        ToolManager::instance()->selectTool(WorldCellTool::instance());
        if (!isCurrent()) {
            cancelMoving();
        }
        event->accept();
        return;
    }
    event->ignore();
}

void PasteCellsTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    updateDropPosition(event->scenePos());
    event->accept();
}

void PasteCellsTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        event->accept();
}

void PasteCellsTool::startMoving()
{
    cancelMoving();
    mPreviewRebuildCount = 0;
    if (!mScene)
        return;

    mSourceCellPositions.clear();

    foreach (WorldCellContents *contents, Clipboard::instance()->cellsInClipboard()) {
        PasteCellItem *dndItem = new PasteCellItem(contents, mScene);
        mDnDItems.append(dndItem);
        dndItem->setZValue(1000);
        mScene->addItem(dndItem);
        mSourceCellPositions.append(contents->pos());
    }

    if (mSourceCellPositions.isEmpty())
        return;

    mSourceCellPos = topLeftCell(mSourceCellPositions, QPoint());
    mStartScenePos = mScene->cellToPixelCoords(mSourceCellPos);
    mSourceCellOffsets.clear();
    mSourceCellOffsets.reserve(mSourceCellPositions.size());
    mSourceOffsetBounds = QRect();
    for (const QPoint &sourcePosition : std::as_const(mSourceCellPositions)) {
        const QPoint offset = sourcePosition - mSourceCellPos;
        mSourceCellOffsets.append(offset);
        mSourceOffsetBounds |= QRect(offset, QSize(1, 1));
    }

    QPoint requestedDrop = mSourceCellPos;
    const QList<WorldCell*> &selectedCells =
            mScene->worldDocument()->selectedCells();
    if (!selectedCells.isEmpty()) {
        QVector<QPoint> selectedPositions;
        selectedPositions.reserve(selectedCells.size());
        for (WorldCell *cell : selectedCells)
            selectedPositions.append(cell->pos());
        requestedDrop = topLeftCell(selectedPositions, mSourceCellPos);
    }

    mDropTilePos = boundedDropCell(mSourceOffsetBounds, requestedDrop,
                                   mScene->world()->bounds());
    const QPointF dropScenePos = mScene->cellToPixelCoords(mDropTilePos);
    for (int index = 0; index < mDnDItems.size(); ++index) {
        PasteCellItem *item = mDnDItems.at(index);
        item->setDragOffset(dropScenePos - mStartScenePos);
        const QPoint targetPosition = mDropTilePos
                + mSourceCellOffsets.at(index);
        item->setTargetOccupied(!mScene->world()->cellAt(targetPosition)->isEmpty());
    }
    qInfo() << "Cell paste preview started" << mDnDItems.size()
            << "source anchor" << mSourceCellPos
            << "target anchor" << mDropTilePos;
    setStatusInfo(tr("Cell paste preview at %1,%2. Move the pointer, then "
                     "left-click or press Enter once to place it. "
                     "Right-click or Escape cancels.")
                  .arg(mDropTilePos.x()).arg(mDropTilePos.y()));
}

bool PasteCellsTool::updateDropPosition(const QPointF &pos)
{
    if (!mScene || mDnDItems.isEmpty())
        return false;
    WorldCell *targetCell = mScene->pointToCell(pos);
    if (!targetCell)
        return false;

    const QPoint requestedDrop = targetCell->pos();
    const QPoint nextDrop = boundedDropCell(
                mSourceOffsetBounds, requestedDrop,
                mScene->world()->bounds());
    if (nextDrop == mDropTilePos)
        return true;
    mDropTilePos = nextDrop;
    ++mPreviewRebuildCount;
    const QPointF dropScenePos = mScene->cellToPixelCoords(mDropTilePos);
    for (int index = 0; index < mDnDItems.size(); ++index) {
        PasteCellItem *item = mDnDItems.at(index);
        item->setDragOffset(dropScenePos - mStartScenePos);
        const QPoint targetPosition = mDropTilePos
                + mSourceCellOffsets.at(index);
        item->setTargetOccupied(!mScene->world()->cellAt(targetPosition)->isEmpty());
    }
    setStatusInfo(tr("Cell paste preview at %1,%2. Green targets are empty. "
                     "Orange targets already contain data. Left-click or "
                     "press Enter once to place. Right-click or Escape cancels.")
                  .arg(mDropTilePos.x()).arg(mDropTilePos.y()));
    return true;
}

bool PasteCellsTool::confirmOccupiedTargets() const
{
    QStringList occupied;
    for (PasteCellItem *item : mDnDItems) {
        const QPoint targetPosition = pastedCellPosition(
                    item->cellPos(), mSourceCellPos, mDropTilePos);
        WorldCell *targetCell = mScene->world()->cellAt(targetPosition);
        if (targetCell && !targetCell->isEmpty())
            occupied += QStringLiteral("%1,%2")
                    .arg(targetPosition.x()).arg(targetPosition.y());
    }
    if (occupied.isEmpty())
        return true;

    const QString targets = occupied.mid(0, 12).join(QLatin1String(", "))
            + (occupied.size() > 12
               ? tr(" and %1 more").arg(occupied.size() - 12)
               : QString());
    return QMessageBox::warning(
                MainWindow::instance(), tr("Paste Into Non-Empty Cells"),
                tr("The target contains %1 non-empty cell(s): %2\n\n"
                   "Pasting will merge the copied map, lots, zones, "
                   "properties and features with the existing cell data. "
                   "Overlapping content may result. Continue?")
                .arg(occupied.size()).arg(targets),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::Yes;
}

bool PasteCellsTool::pasteCells()
{
    if (mDnDItems.isEmpty())
        return false;

    QPoint startCellPos = mSourceCellPos;
    QPoint dropCellPos = mDropTilePos;

    QUndoStack *undoStack = mScene->worldDocument()->undoStack();
    int count = mDnDItems.size();
    MainWindow::instance()->beginDocumentTransaction();
    undoStack->beginMacro(tr("Paste %1 Cell%2").arg(count).arg(QLatin1String((count > 1) ? "s" : "")));
    undoStack->push(new ProgressBegin(tr("Pasting Cells"))); // in case of multiple loadMap() calls
#if 1
    Clipboard::instance()->pasteEverythingButCells(mScene->worldDocument());
#endif
    QList<WorldCell*> newSelection;
    foreach (PasteCellItem *item, mDnDItems) {
        QPoint newPos = pastedCellPosition(item->cellPos(), startCellPos,
                                           dropCellPos);
        WorldCell *replace = mScene->world()->cellAt(newPos);
        WorldCellContents *contents = new WorldCellContents(item->contents(), replace);
#if 1
        // PropertyDefs, Templates, ObjectTypes, ObjectGroups -> from clipboard-world to document-world
        contents->swapWorld(mScene->world());
        contents->mergeOnto(replace);
#endif
        undoStack->push(new ReplaceCell(mScene->worldDocument(), replace, contents));
        newSelection += mScene->world()->cellAt(newPos);
    }
    mScene->worldDocument()->setSelectedCells(newSelection, true);
    undoStack->push(new ProgressEnd(tr("Undoing Paste Cells"))); // in case of multiple loadMap() calls
    undoStack->endMacro();
    MainWindow::instance()->endDocumentTransaction();
    qInfo() << "Cell paste completed" << count
            << "source anchor" << startCellPos
            << "target anchor" << dropCellPos;
    return true;
}

QPoint PasteCellsTool::topLeftCell(const QVector<QPoint> &cellPositions,
                                   const QPoint &fallback)
{
    if (cellPositions.isEmpty())
        return fallback;
    QRect bounds(cellPositions.first(), QSize(1, 1));
    for (const QPoint &cellPosition : cellPositions)
        bounds |= QRect(cellPosition, QSize(1, 1));
    return bounds.topLeft();
}

QPoint PasteCellsTool::boundedDropCell(const QVector<QPoint> &cellPositions,
                                       const QPoint &sourceCell,
                                       const QPoint &requestedDrop,
                                       const QRect &worldBounds)
{
    if (cellPositions.isEmpty())
        return requestedDrop;

    QRect sourceOffsetBounds;
    for (const QPoint &cellPos : cellPositions)
        sourceOffsetBounds |= QRect(cellPos - sourceCell, QSize(1, 1));
    return boundedDropCell(sourceOffsetBounds, requestedDrop, worldBounds);
}

QPoint PasteCellsTool::boundedDropCell(const QRect &sourceOffsetBounds,
                                       const QPoint &requestedDrop,
                                       const QRect &worldBounds)
{
    if (sourceOffsetBounds.isEmpty())
        return requestedDrop;

    const QRect cellBounds = sourceOffsetBounds.translated(requestedDrop);
    QPoint drop = requestedDrop;
    if (cellBounds.left() < worldBounds.left())
        drop.rx() += worldBounds.left() - cellBounds.left();
    if (cellBounds.top() < worldBounds.top())
        drop.ry() += worldBounds.top() - cellBounds.top();
    if (cellBounds.right() > worldBounds.right())
        drop.rx() += worldBounds.right() - cellBounds.right();
    if (cellBounds.bottom() > worldBounds.bottom())
        drop.ry() += worldBounds.bottom() - cellBounds.bottom();
    return drop;
}

QPoint PasteCellsTool::pastedCellPosition(const QPoint &cellPosition,
                                          const QPoint &sourceCell,
                                          const QPoint &dropCell)
{
    return cellPosition + dropCell - sourceCell;
}

void PasteCellsTool::cancelMoving()
{
    qDeleteAll(mDnDItems);
    mDnDItems.clear();
    mSourceCellPositions.clear();
    mSourceCellOffsets.clear();
    mSourceOffsetBounds = QRect();
}

/////

WorldCreateRoadTool *WorldCreateRoadTool::mInstance = 0;

WorldCreateRoadTool *WorldCreateRoadTool::instance()
{
    if (!mInstance)
        mInstance = new WorldCreateRoadTool();
    return mInstance;
}

void WorldCreateRoadTool::deleteInstance()
{
    delete mInstance;
}

WorldCreateRoadTool::WorldCreateRoadTool()
    : BaseWorldSceneTool(tr("Create Roads"),
                         QIcon(QLatin1String(":/images/22x22/road-tool-create.png")),
                         QKeySequence())
    , mCreating(false)
    , mCurrentRoadWidth(8)
    , mCurrentTrafficLines(RoadTemplates::instance()->nullTrafficLines())
    , mCursorItem(new QGraphicsPolygonItem)
{
    mCursorItem->setBrush(Qt::cyan);
    mCursorItem->setOpacity(0.66);
    mCursorItem->setZValue(WorldScene::ZVALUE_ROADITEM_CREATING + 0.5);
}

WorldCreateRoadTool::~WorldCreateRoadTool()
{
    delete mRoad;
    delete mRoadItem;
    delete mCursorItem;
}

void WorldCreateRoadTool::activate()
{
    BaseWorldSceneTool::activate();
    mScene->addItem(mCursorItem);
}

void WorldCreateRoadTool::deactivate()
{
    mScene->removeItem(mCursorItem);
    BaseWorldSceneTool::deactivate();
}

void WorldCreateRoadTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && mCreating) {
        cancelNewRoadItem();
        event->accept();
    }
}

void WorldCreateRoadTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (mCreating)
            return;
        startNewRoadItem(event->scenePos());
        mCreating = true;
    }
    if (event->button() == Qt::RightButton) {
        if (!mCreating)
            return;
        cancelNewRoadItem();
        mCreating = false;
    }
}

void WorldCreateRoadTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QPoint roadPos = mScene->pixelToRoadCoords(event->scenePos());

    if (mCreating) {
        QPoint delta = roadPos - mStartRoadPos;
        if (qAbs(delta.x()) >= qAbs(delta.y())) {
            delta.setY(0); // horizontal road
        } else {
            delta.setX(0); // vertical road
        }
        mRoad->setCoords(mStartRoadPos, mStartRoadPos + delta);
        mRoadItem->synchWithRoad();

        roadPos = mRoad->end();
    }

    QPoint topLeft = roadPos - QPoint(mCurrentRoadWidth / 2, mCurrentRoadWidth / 2);
    QSize size(mCurrentRoadWidth, mCurrentRoadWidth);

    mCursorItem->setPolygon(mScene->roadRectToScenePolygon(QRect(topLeft, size)));
}

void WorldCreateRoadTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!mCreating)
            return;
        if (mRoad->start() == mRoad->end())
            return;
        finishNewRoadItem();
        mCreating = false;
    }
}

void WorldCreateRoadTool::startNewRoadItem(const QPointF &scenePos)
{
    mStartRoadPos = mScene->pixelToRoadCoords(scenePos);
    mRoad = new Road(mScene->world(),
                     mStartRoadPos.x(), mStartRoadPos.y(),
                     mStartRoadPos.x(), mStartRoadPos.y(),
                     mCurrentRoadWidth, -1);
    mRoad->setTileName(mCurrentTileName);
    mRoad->setTrafficLines(mCurrentTrafficLines);
    mRoadItem = new WorldRoadItem(mScene, mRoad);
    mRoadItem->setZValue(WorldScene::ZVALUE_ROADITEM_CREATING);
    mScene->addItem(mRoadItem);
}

void WorldCreateRoadTool::clearNewRoadItem()
{
    mScene->removeItem(mRoadItem);
    delete mRoadItem;
    mRoadItem = 0;
}

void WorldCreateRoadTool::cancelNewRoadItem()
{
    clearNewRoadItem();
    delete mRoad;
    mRoad = 0;
}

void WorldCreateRoadTool::finishNewRoadItem()
{
    clearNewRoadItem();

    QUndoStack *undoStack = mScene->worldDocument()->undoStack();
    undoStack->push(new AddRoad(mScene->worldDocument(),
                                mScene->world()->roads().count(),
                                mRoad));
    mRoad = 0;
}

#if 0

/////

/**
 * A handle that allows moving around a point of a Road.
 */
class RoadPointHandle : public QGraphicsItem
{
public:
    RoadPointHandle(RoadItem *roadItem, int pointIndex)
        : QGraphicsItem()
        , mRoadItem(roadItem)
        , mPointIndex(pointIndex)
        , mSelected(false)
        , mDragging(false)
    {
        setFlags(QGraphicsItem::ItemIgnoresTransformations |
                 QGraphicsItem::ItemIgnoresParentOpacity);
        setZValue(10000);
        setCursor(Qt::SizeAllCursor);
    }

    Road *road() const { return mRoadItem->road(); }

    int pointIndex() const { return mPointIndex; }

    QPoint pointPosition() const;
    void setPointPosition(const QPoint &pos);

    // These override the QGraphicsItem members
    void setSelected(bool selected) { mSelected = selected; update(); }
    bool isSelected() const { return mSelected; }

    void setDragging(bool dragging)
    {
        if (dragging != mDragging) {
            mDragging = dragging;
            if (mDragging)
                mStartPos = pointPosition();
        }
    }

    void setDragOffset(const QPoint &offset)
    {
        mDragOffset = offset;
        update();
    }

    QPoint dragOffset() const
    { return mDragOffset; }

    QPoint startPosition() const
    { return mStartPos; }

    QRectF boundingRect() const;

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = 0);

private:
    RoadItem *mRoadItem;
    int mPointIndex;
    bool mSelected;
    bool mDragging;
    QPoint mDragOffset;
    QPoint mStartPos;
};

QPoint RoadPointHandle::pointPosition() const
{
    if (mPointIndex == 0)
        return road()->start();
    else
        return road()->end();
}

void RoadPointHandle::setPointPosition(const QPoint &pos)
{
    if (mPointIndex == 0)
        road()->setCoords(pos, road()->end());
    else
        road()->setCoords(road()->start(), pos);
    mRoadItem->synchWithRoad();
}

QRectF RoadPointHandle::boundingRect() const
{
    return QRectF(-5, -5, 10 + 1, 10 + 1);
}

void RoadPointHandle::paint(QPainter *painter,
                            const QStyleOptionGraphicsItem *,
                            QWidget *)
{
    painter->setPen(Qt::black);
    if (mSelected) {
        painter->setBrush(QApplication::palette().highlight());
        painter->drawRect(QRectF(-4, -4, 8, 8));
    } else {
        painter->setBrush(Qt::lightGray);
        painter->drawRect(QRectF(-3, -3, 6, 6));
    }
}

#endif

/////

WorldEditRoadTool *WorldEditRoadTool::mInstance = 0;

WorldEditRoadTool *WorldEditRoadTool::instance()
{
    if (!mInstance)
        mInstance = new WorldEditRoadTool();
    return mInstance;
}

void WorldEditRoadTool::deleteInstance()
{
    delete mInstance;
}

WorldEditRoadTool::WorldEditRoadTool()
    : BaseWorldSceneTool(tr("Edit Roads"),
                         QIcon(QLatin1String(":/images/22x22/road-tool-edit.png")),
                         QKeySequence())
    , mSelectedRoad(0)
    , mSelectedRoadItem(0)
    , mRoad(0)
    , mRoadItem(0)
    , mMoving(false)
    , mStartHandle(new QGraphicsPolygonItem)
    , mEndHandle(new QGraphicsPolygonItem)
    , mHandlesVisible(false)
{
    mStartHandle->setBrush(Qt::cyan);
    mStartHandle->setOpacity(0.66);
    mStartHandle->setZValue(WorldScene::ZVALUE_ROADITEM_CREATING + 0.5);

    mEndHandle->setBrush(Qt::cyan);
    mEndHandle->setOpacity(0.66);
    mEndHandle->setZValue(WorldScene::ZVALUE_ROADITEM_CREATING + 0.5);
}

WorldEditRoadTool::~WorldEditRoadTool()
{
    delete mRoadItem;
    delete mRoad;
    delete mStartHandle;
    delete mEndHandle;
}

void WorldEditRoadTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene)
        mScene->worldDocument()->disconnect(this);

    mScene = scene ? scene->asWorldScene() : 0;

    if (mScene) {
        connect(mScene->worldDocument(), SIGNAL(roadCoordsChanged(int)),
                SLOT(roadCoordsChanged(int)));
        connect(mScene->worldDocument(), SIGNAL(roadAboutToBeRemoved(int)),
                SLOT(roadAboutToBeRemoved(int)));
    }
}

void WorldEditRoadTool::activate()
{
    BaseWorldSceneTool::activate();
}

void WorldEditRoadTool::deactivate()
{
    if (mHandlesVisible) {
        mScene->removeItem(mStartHandle);
        mScene->removeItem(mEndHandle);
        mHandlesVisible = false;
    }
    BaseWorldSceneTool::deactivate();
}

void WorldEditRoadTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && mMoving) {
        cancelMoving();
        event->accept();
    }
}

void WorldEditRoadTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (mMoving)
            return;
        startMoving(event->scenePos());
    }
    if (event->button() == Qt::RightButton) {
        if (!mMoving)
            return;
        cancelMoving();
        mMoving = false;
    }
}

void WorldEditRoadTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!mMoving)
        return;
    QPoint curPos = mScene->pixelToRoadCoords(event->scenePos());
    QPoint delta = curPos - (mMovingStart
            ? mSelectedRoadItem->road()->start()
            : mSelectedRoadItem->road()->end());
    if (mSelectedRoadItem->road()->isVertical())
        delta.setX(0);
    else
        delta.setY(0);
    if (mMovingStart)
        mRoad->setCoords(mSelectedRoadItem->road()->start() + delta, mRoad->end());
    else
        mRoad->setCoords(mRoad->start(), mSelectedRoadItem->road()->end() + delta);
    mRoadItem->synchWithRoad();
    updateHandles(mRoad);
}

void WorldEditRoadTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!mMoving)
            return;
        if (mRoad->start() == mSelectedRoadItem->road()->start() &&
                mRoad->end() == mSelectedRoadItem->road()->end()) {
            cancelMoving();
            mMoving = false;
            return;
        }
        finishMoving();
        mMoving = false;
    }
}

// The road we are editing could be deleted via undo/redo
void WorldEditRoadTool::roadAboutToBeRemoved(int index)
{
    Road *road = mScene->world()->roads().at(index);
    if (mSelectedRoad && road == mSelectedRoad) {
        if (mMoving) {
            mSelectedRoadItem = 0; // could have been deleted by WorldScene already
            cancelMoving();
            mMoving = false;
        }
        updateHandles(0);
        mSelectedRoadItem = 0;
        mSelectedRoad = 0;
    }
}

void WorldEditRoadTool::roadCoordsChanged(int index)
{
    Road *road = mScene->world()->roads().at(index);
    if (mSelectedRoadItem && road == mSelectedRoadItem->road())
        updateHandles(road);
}

void WorldEditRoadTool::startMoving(const QPointF &scenePos)
{
    if (mSelectedRoadItem) {
        QPoint roadPos = mScene->pixelToRoadCoords(scenePos);
        if (mSelectedRoadItem->road()->startBounds().adjusted(0, 0, 1, 1).contains(roadPos)) {
            mMovingStart = true;
        } else if (mSelectedRoadItem->road()->endBounds().adjusted(0, 0, 1, 1).contains(roadPos)) {
            mMovingStart = false;
        } else {
            mSelectedRoadItem->setEditable(false);
            mSelectedRoadItem->setZValue(mSelectedRoadItem->isSelected() ?
                                             WorldScene::ZVALUE_ROADITEM_SELECTED :
                                             WorldScene::ZVALUE_ROADITEM_UNSELECTED);
            mSelectedRoadItem = 0;
            mSelectedRoad = 0;
        }
        if (mSelectedRoadItem) {
            mMoving = true;

            mRoad = new Road(mScene->world(),
                             mSelectedRoadItem->road()->x1(), mSelectedRoadItem->road()->y1(),
                             mSelectedRoadItem->road()->x2(), mSelectedRoadItem->road()->y2(),
                             mSelectedRoadItem->road()->width(), -1);
            mRoadItem = new WorldRoadItem(mScene, mRoad);
            mRoadItem->setEditable(true);
            mRoadItem->setZValue(WorldScene::ZVALUE_ROADITEM_CREATING);
            mScene->addItem(mRoadItem);
            mSelectedRoadItem->setVisible(false);
            return;
        }
    }

    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (WorldRoadItem *roadItem = dynamic_cast<WorldRoadItem*>(item)) {
            mSelectedRoad = roadItem->road();
            mSelectedRoadItem = roadItem;
            mSelectedRoadItem->setEditable(true);
            mSelectedRoadItem->setZValue(WorldScene::ZVALUE_ROADITEM_CREATING);
            updateHandles(mSelectedRoadItem->road());
            break;
        }
    }
    updateHandles(mSelectedRoadItem ? mSelectedRoadItem->road() : 0);
}

void WorldEditRoadTool::finishMoving()
{
    QUndoStack *undoStack = mScene->worldDocument()->undoStack();
    undoStack->push(new ChangeRoadCoords(mScene->worldDocument(),
                                         mSelectedRoadItem->road(),
                                         mRoad->start(), mRoad->end()));
    cancelMoving();
}

void WorldEditRoadTool::cancelMoving()
{
    if (mSelectedRoadItem) {
        mSelectedRoadItem->setVisible(true);
        updateHandles(mSelectedRoadItem->road());
    } else
        updateHandles(0);

    mScene->removeItem(mRoadItem);
    delete mRoadItem;
    mRoadItem = 0;

    delete mRoad;
    mRoad = 0;
}

void WorldEditRoadTool::updateHandles(Road *road)
{
    if (road) {
        mStartHandle->setPolygon(mScene->roadRectToScenePolygon(road->startBounds()));
        mEndHandle->setPolygon(mScene->roadRectToScenePolygon(road->endBounds()));
        if (mHandlesVisible == false) {
            mScene->addItem(mStartHandle);
            mScene->addItem(mEndHandle);
            mHandlesVisible = true;
        }
    } else if (mHandlesVisible) {
        mScene->removeItem(mStartHandle);
        mScene->removeItem(mEndHandle);
        mHandlesVisible = false;
    }
}

/////

WorldSelectMoveRoadTool *WorldSelectMoveRoadTool::mInstance = 0;

WorldSelectMoveRoadTool *WorldSelectMoveRoadTool::instance()
{
    if (!mInstance)
        mInstance = new WorldSelectMoveRoadTool();
    return mInstance;
}

void WorldSelectMoveRoadTool::deleteInstance()
{
    delete mInstance;
}

WorldSelectMoveRoadTool::WorldSelectMoveRoadTool()
    : BaseWorldSceneTool(tr("Select and Move Roads"),
                         QIcon(QLatin1String(":/images/22x22/road-tool-select.png")),
                         QKeySequence())
    , mMode(NoMode)
    , mMousePressed(false)
    , mSelectionRectItem(new QGraphicsPolygonItem)
{
    mSelectionRectItem->setZValue(1000);
    mSelectionRectItem->setPen(QColor(0x33,0x99,0xff));
    mSelectionRectItem->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
}

WorldSelectMoveRoadTool::~WorldSelectMoveRoadTool()
{
    delete mSelectionRectItem;
}

void WorldSelectMoveRoadTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene)
        mScene->worldDocument()->disconnect(this);

    mScene = scene ? scene->asWorldScene() : 0;

    if (mScene) {
        connect(mScene->worldDocument(), SIGNAL(roadAboutToBeRemoved(int)),
                SLOT(roadAboutToBeRemoved(int)));
    }
}

void WorldSelectMoveRoadTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && (mMode == Moving)) {
        cancelMoving();
        event->accept();
    }
}

void WorldSelectMoveRoadTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        if (mMode != NoMode) // Ignore additional presses during select/move
            break;
        mMousePressed = true;
        mStartScenePos = event->scenePos();
        mDropRoadPos = mScene->pixelToRoadCoords(mStartScenePos);
        mClickedItem = topmostItemAt(mStartScenePos);
        break;
    case Qt::RightButton:
        cancelMoving();
        break;
    default:
        break;
    }
}

void WorldSelectMoveRoadTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (mMode == NoMode && mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (mClickedItem &&
                    mScene->worldDocument()->selectedRoads().contains(mClickedItem->road()) &&
                    !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
                startMoving();
            else
                startSelecting();
        }
    }

    switch (mMode) {
    case Selecting:
    {
        QPointF start = mScene->pixelToCellCoords(mStartScenePos);
        QPointF end = mScene->pixelToCellCoords(event->scenePos());
        QRectF bounds = QRectF(start, end).normalized();
        mSelectionRectItem->setPolygon(mScene->cellRectToPolygon(bounds));
        break;
    }
    case Moving:
        updateMovingItems(event->scenePos(), event->modifiers());
        break;
    case NoMode:
        break;
    case CancelMoving:
        break;
    }
}

void WorldSelectMoveRoadTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
    {
        bool toggle = event->modifiers() & Qt::ControlModifier;
        bool extend = event->modifiers() & Qt::ShiftModifier;
        QList<Road*> newSelection;
        if (extend || toggle)
            newSelection = mScene->worldDocument()->selectedRoads();
        if (mClickedItem) {
            if (toggle && newSelection.contains(mClickedItem->road()))
                newSelection.removeOne(mClickedItem->road());
            else if (!newSelection.contains(mClickedItem->road()))
                newSelection += mClickedItem->road();
        }
        mScene->worldDocument()->setSelectedRoads(newSelection);
        break;
    }
    case Selecting:
        updateSelection(event);
        mScene->removeItem(mSelectionRectItem);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case CancelMoving:
        mMode = NoMode;
        break;
    }

    mMousePressed = false;
    mClickedItem = 0;
}

void WorldSelectMoveRoadTool::roadAboutToBeRemoved(int index)
{
    if (mMode == Moving) {
        Road *road = mScene->world()->roads().at(index);
        if (mMovingRoads.contains(road)) {
//            mScene->itemForRoad(road)->setDragging(false);
            mMovingRoads.removeAll(road);
            if (mMovingRoads.isEmpty())
                mMode = CancelMoving;
        }
    }
}

void WorldSelectMoveRoadTool::startSelecting()
{
    mMode = Selecting;
    mScene->addItem(mSelectionRectItem);
}

void WorldSelectMoveRoadTool::updateSelection(QGraphicsSceneMouseEvent *event)
{
    QPointF start = mScene->pixelToCellCoords(mStartScenePos);
    QPointF end = mScene->pixelToCellCoords(event->scenePos());
    QRectF bounds = QRectF(start, end).normalized();

    bool toggle = event->modifiers() & Qt::ControlModifier;
    bool extend = event->modifiers() & Qt::ShiftModifier;

    QList<Road*> selection;
    if (extend || toggle)
        selection = mScene->worldDocument()->selectedRoads();

    foreach (Road *road, mScene->roadsInRect(bounds)) {
        if (toggle && selection.contains(road))
            selection.removeOne(road);
        else if (!selection.contains(road))
            selection += road;
    }

    mScene->worldDocument()->setSelectedRoads(selection);
}

void WorldSelectMoveRoadTool::startMoving()
{
    mMovingRoads = mScene->worldDocument()->selectedRoads();

    // Move only the clicked item, if it was not part of the selection
    if (!mMovingRoads.contains(mClickedItem->road())) {
        mMovingRoads.clear();
        mMovingRoads += mClickedItem->road();
        mScene->worldDocument()->setSelectedRoads(mMovingRoads);
    }

    mMode = Moving;
}

void WorldSelectMoveRoadTool::updateMovingItems(const QPointF &pos,
                                      Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)

    QPoint startPos = mScene->pixelToRoadCoords(mStartScenePos);
    mDropRoadPos = mScene->pixelToRoadCoords(pos);

    foreach (Road *road, mMovingRoads) {
        WorldRoadItem *item = mScene->itemForRoad(road);
        item->setDragging(true);
        item->setDragOffset(mDropRoadPos - startPos);
    }
}

void WorldSelectMoveRoadTool::finishMoving(const QPointF &pos)
{
    Q_UNUSED(pos)
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    foreach (Road *road, mMovingRoads)
        mScene->itemForRoad(road)->setDragging(false);

    QPoint startPos = mScene->pixelToRoadCoords(mStartScenePos);
    QPoint dropPos = mDropRoadPos;
    QPoint diff = dropPos - startPos;
    if (startPos != dropPos) {
        QUndoStack *undoStack = mScene->worldDocument()->undoStack();
        int count = mMovingRoads.size();
        undoStack->beginMacro(tr("Move %1 Road%2").arg(count).arg(QLatin1String((count > 1) ? "s" : "")));
        foreach (Road *road, mMovingRoads) {
            mScene->worldDocument()->changeRoadCoords(road,
                                                      road->start() + diff,
                                                      road->end() + diff);
        }
        undoStack->endMacro();
    }

    mMovingRoads.clear();
}

void WorldSelectMoveRoadTool::cancelMoving()
{
    if (mMode == Moving) {
        mMode = CancelMoving;
        foreach (Road *road, mMovingRoads)
            mScene->itemForRoad(road)->setDragging(false);
        mMovingRoads.clear();
    }
}

WorldRoadItem *WorldSelectMoveRoadTool::topmostItemAt(const QPointF &scenePos)
{
    foreach (QGraphicsItem *item, mScene->items(scenePos)) {
        if (WorldRoadItem *roadItem = dynamic_cast<WorldRoadItem*>(item))
            return roadItem;
    }
    return 0;
}

/////

WorldBMPTool *WorldBMPTool::mInstance = 0;

WorldBMPTool *WorldBMPTool::instance()
{
    if (!mInstance)
        mInstance = new WorldBMPTool();
    return mInstance;
}

void WorldBMPTool::deleteInstance()
{
    delete mInstance;
}

WorldBMPTool::WorldBMPTool()
    : BaseWorldSceneTool(tr("Select and Move BMP Images"),
                         QIcon(QLatin1String(":/images/22x22/bmp-tool-select.png")),
                         QKeySequence())
    , mMode(NoMode)
    , mMousePressed(false)
    , mSelectionRectItem(new QGraphicsPolygonItem)
{
    mSelectionRectItem->setZValue(1000);
    mSelectionRectItem->setPen(QColor(0x33,0x99,0xff));
    mSelectionRectItem->setBrush(QBrush(QColor(0x33,0x99,0xff,255/8)));
}

WorldBMPTool::~WorldBMPTool()
{
}

void WorldBMPTool::setScene(BaseGraphicsScene *scene)
{
    if (mScene)
        mScene->worldDocument()->disconnect(this);

    mScene = scene ? scene->asWorldScene() : 0;

    if (mScene) {
        connect(mScene->worldDocument(), &WorldDocument::bmpAboutToBeRemoved,
                this, &WorldBMPTool::bmpAboutToBeRemoved);
    }
}

void WorldBMPTool::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && (mMode == Moving)) {
        cancelMoving();
        event->accept();
    }
}

void WorldBMPTool::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        if (mMode != NoMode) // Ignore additional presses during select/move
            break;
        mMousePressed = true;
        mStartScenePos = event->scenePos();
        mDragOffset = QPoint();
        mClickedItem = topmostItemAt(mStartScenePos);
        break;
    case Qt::RightButton:
        if (mMode == Moving)
            cancelMoving();
        break;
    default:
        break;
    }
}

void WorldBMPTool::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (mMode == NoMode && mMousePressed) {
        const int dragDistance = (mStartScenePos - event->scenePos()).manhattanLength();
        if (dragDistance >= QApplication::startDragDistance()) {
            if (mClickedItem)
                startMoving();
            else
                startSelecting();
        }
    }

    switch (mMode) {
    case Selecting:
    {
        QPointF start = mScene->pixelToCellCoords(mStartScenePos);
        QPointF end = mScene->pixelToCellCoords(event->scenePos());
        QRectF bounds = QRectF(start, end).normalized();
        mSelectionRectItem->setPolygon(mScene->cellRectToPolygon(bounds));
        break;
    }
    case Moving:
        updateMovingItems(event->scenePos(), event->modifiers());
        break;
    case NoMode:
        break;
    case CancelMoving:
        break;
    }
}

void WorldBMPTool::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    switch (mMode) {
    case NoMode:
    {
        bool toggle = event->modifiers() & Qt::ControlModifier;
        bool extend = event->modifiers() & Qt::ShiftModifier;
        QList<WorldBMP*> newSelection;
        if (extend || toggle)
            newSelection = mScene->worldDocument()->selectedBMPs();
        if (mClickedItem) {
            WorldBMP *bmp = mClickedItem->bmp();
            if (toggle && newSelection.contains(bmp))
                newSelection.removeOne(bmp);
            else if (!newSelection.contains(bmp))
                newSelection += bmp;
        }
        mScene->worldDocument()->setSelectedBMPs(newSelection);
        break;
    }
    case Selecting:
        updateSelection(event);
        mScene->removeItem(mSelectionRectItem);
        mMode = NoMode;
        break;
    case Moving:
        finishMoving(event->scenePos());
        break;
    case CancelMoving:
        mMode = NoMode;
        break;
    }

    mMousePressed = false;
    mClickedItem = 0;
}

void WorldBMPTool::bmpAboutToBeRemoved(int index)
{
    if (mMode == Moving) {
        WorldBMP *bmp = mScene->world()->bmps().at(index);
        if (mMovingBMPs.contains(bmp)) {
//            mScene->itemForBMP(bmp)->setDragging(false);
            mMovingBMPs.removeAll(bmp);
            if (mMovingBMPs.isEmpty())
                mMode = CancelMoving;
        }
    }
}

void WorldBMPTool::startSelecting()
{
    mMode = Selecting;
    mScene->addItem(mSelectionRectItem);
}

void WorldBMPTool::updateSelection(QGraphicsSceneMouseEvent *event)
{
    QPointF start = mScene->pixelToCellCoords(mStartScenePos);
    QPointF end = mScene->pixelToCellCoords(event->scenePos());
    QRectF bounds = QRectF(start, end).normalized();

    bool toggle = event->modifiers() & Qt::ControlModifier;
    bool extend = event->modifiers() & Qt::ShiftModifier;

    QList<WorldBMP*> selection;
    if (extend || toggle)
        selection = mScene->worldDocument()->selectedBMPs();

    foreach (WorldBMP *bmp, mScene->bmpsInRect(bounds)) {
        if (toggle && selection.contains(bmp))
            selection.removeOne(bmp);
        else if (!selection.contains(bmp))
            selection += bmp;
    }

    mScene->worldDocument()->setSelectedBMPs(selection);
}

void WorldBMPTool::startMoving()
{
    mMovingBMPs = mScene->worldDocument()->selectedBMPs();

    // Move only the clicked item, if it was not part of the selection
    if (!mMovingBMPs.contains(mClickedItem->bmp())) {
        mMovingBMPs.clear();
        mMovingBMPs += mClickedItem->bmp();
        mScene->worldDocument()->setSelectedBMPs(mMovingBMPs);
    }

    mMode = Moving;
}

void WorldBMPTool::updateMovingItems(const QPointF &pos,
                                     Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)
#if 0
    // Restrict the drop position so the images stay in bounds
    QVector<QPoint> cellPositions;
    cellPositions.append(mClickedItem->bmp()->pos());
    cellPositions.append(mClickedItem->bmp()->bounds().bottomRight());
    QPointF pt = restrictDragging(cellPositions, mStartScenePos, pos);
#endif
    QPoint startCellPos = mScene->pixelToCellCoordsInt(mStartScenePos);
    QPoint dropCellPos = mScene->pixelToCellCoordsInt(pos);
    mDragOffset = dropCellPos - startCellPos;

    foreach (WorldBMP *bmp, mMovingBMPs) {
        mScene->itemForBMP(bmp)->setDragging(true);
        mScene->itemForBMP(bmp)->setDragOffset(mDragOffset);
    }
}

void WorldBMPTool::finishMoving(const QPointF &pos)
{
    Q_UNUSED(pos)
    Q_ASSERT(mMode == Moving);
    mMode = NoMode;

    foreach (WorldBMP *bmp, mMovingBMPs)
        mScene->itemForBMP(bmp)->setDragging(false);

    int count = mMovingBMPs.size();
    if (count && !mDragOffset.isNull()) {
        QUndoStack *undoStack = mScene->worldDocument()->undoStack();
        if (count > 1)
            undoStack->beginMacro(tr("Move %1 BMP%2").arg(count).arg(QLatin1String((count > 1) ? "s" : "")));
        foreach (WorldBMP *bmp, mMovingBMPs) {
            mScene->worldDocument()->moveBMP(bmp, bmp->pos() + mDragOffset);
        }
        if (count > 1)
            undoStack->endMacro();
    }

    mMovingBMPs.clear();
}

void WorldBMPTool::cancelMoving()
{
    if (mMode == Moving) {
        mMode = CancelMoving;
        foreach (WorldBMP *bmp, mMovingBMPs)
            mScene->itemForBMP(bmp)->setDragging(false);
        mMovingBMPs.clear();
        mClickedItem = 0;
    }
}

WorldBMPItem *WorldBMPTool::topmostItemAt(const QPointF &scenePos)
{
    WorldBMP *bmp = mScene->pointToBMP(scenePos);
    return bmp ? mScene->itemForBMP(bmp) : 0;
}

/////

CellObjectEdgeResizeHandle::CellObjectEdgeResizeHandle(CellScene *scene, WorldCellObject *object, Edge edge)
    : QGraphicsItem(nullptr)
    , mScene(scene)
    , mObject(object)
    , mEdge(edge)
    , mOffsetItemBG(nullptr)
    , mOffsetItem(nullptr)
{
    setFlags(QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemSendsGeometryChanges |
//             QGraphicsItem::ItemIgnoresTransformations |
             QGraphicsItem::ItemIgnoresParentOpacity);

    setCursor(Qt::SizeFDiagCursor);
}

void CellObjectEdgeResizeHandle::setObject(WorldCellObject *object)
{
    prepareGeometryChange();
    mObject = object;
}

void CellObjectEdgeResizeHandle::setEdge(Edge edge)
{
    prepareGeometryChange();
    mEdge = edge;
}

void CellObjectEdgeResizeHandle::synchWithObject()
{
    prepareGeometryChange();
    setPos(0.0, 0.0);
}

CellObjectEdgeResizeHandle::Edge CellObjectEdgeResizeHandle::pickEdge(ObjectItem *objectItem, const QPointF &scenePos)
{
    if (objectItem == nullptr) {
        return Edge::NONE;
    }
    Tiled::MapRenderer *renderer = objectItem->cellScene()->renderer();
    QPointF worldPos = renderer->pixelToTileCoords(scenePos, objectItem->object()->level());
    WorldCellObject *object = objectItem->object();
    qreal T = edgeThickness(objectItem->cellScene());
    qreal x = object->x(), y = object->y(), w = object->width(), h = object->height();
    if (worldPos.y() >= y - T && worldPos.y() <= y + T) {
        return Edge::North;
    }
    if (worldPos.y() >= y + h - T && worldPos.y() <= y + h + T) {
        return Edge::South;
    }
    if (worldPos.x() >= x - T && worldPos.x() <= x + T) {
        return Edge::West;
    }
    if (worldPos.x() >= x + w - T && worldPos.x() <= x + w + T) {
        return Edge::East;
    }
    return Edge::NONE;
}

QRectF CellObjectEdgeResizeHandle::boundingRect() const
{
    Tiled::MapRenderer *renderer = mScene->renderer();
    return renderer->tileToPixelCoords(edgeRect(), mObject->level()).boundingRect().adjusted(-20, -20, 20, 20);
}

void CellObjectEdgeResizeHandle::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    QPen pen(Qt::blue);
    pen.setWidth(3);
    pen.setCosmetic(true);
    painter->setPen(pen);
    Tiled::MapRenderer *renderer = mScene->renderer();
    QPolygonF scenePolygon = renderer->tileToPixelCoords(edgeRect(), mObject->level());
    painter->drawPolygon(scenePolygon);
}

void CellObjectEdgeResizeHandle::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // Remember the old size since we may resize the object
    if (event->button() == Qt::LeftButton) {
        Tiled::MapRenderer *renderer = mScene->renderer();
        mCancelResize = false;
        mClickObjectPos = renderer->pixelToTileCoords(event->scenePos(), mObject->level()) - mObject->pos();
        mOldSize = mObject->size();
        if (auto *objectItem = mScene->itemForObject(mObject)) {
            objectItem->labelItem()->setShowSize(true);
            objectItem->labelItem()->synch();
        }
    }

    QGraphicsItem::mousePressEvent(event);
}

void CellObjectEdgeResizeHandle::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton) {
        WorldCellObject *obj = mObject;
        auto *objectItem = mScene->itemForObject(mObject);
        if (objectItem == nullptr)
            return;
        QPointF posDelta = objectItem->dragOffset();
        QSizeF resizeDelta = objectItem->resizeDelta();
        objectItem->setDragOffset(QPoint());
        objectItem->setResizeDelta(QSizeF(0, 0));
        updateOffsetLabel();
        if (mCancelResize == false) {
            if (!resizeDelta.isNull() || !posDelta.isNull()) {
                WorldDocument *document = mScene->document()->worldDocument();
                QUndoStack *undoStack = mScene->document()->undoStack();
                undoStack->beginMacro(QStringLiteral("Resize Object"));
                if (posDelta.isNull() == false)
                    document->moveCellObject(obj, obj->pos() + posDelta);
                if (resizeDelta.isNull() == false)
                    document->resizeCellObject(obj, mOldSize + resizeDelta);
                undoStack->endMacro();
            }
        }
        mCancelResize = false;
        objectItem->labelItem()->setShowSize(false);
        objectItem->labelItem()->synch();
    }

    if (event->button() == Qt::RightButton) {
        setPos(QPointF());
        mCancelResize = true;
    }

    // Stop the context-menu messing us up.
    event->accept();
}

QVariant CellObjectEdgeResizeHandle::itemChange(GraphicsItemChange change, const QVariant &value)
{
    Tiled::MapRenderer *renderer = mScene->renderer();
    if (change == ItemPositionChange) {
        if (mCancelResize) {
            return QPointF();
        }
        int level = mObject->level();
        QPointF clickScenePos = renderer->tileToPixelCoords(mClickObjectPos, level);
        QPointF tileCoords = renderer->pixelToTileCoords(clickScenePos + value.toPointF(), level);
        QPointF delta = (tileCoords - mClickObjectPos).toPoint()/* - objectWorldPos*/;
        switch (mEdge) {
        case Edge::North:
            delta.setX(0);
            delta.setY(qMin(delta.y(), mObject->height() - MIN_OBJECT_SIZE));
            break;
        case Edge::South:
            delta.setX(0);
            delta.setY(qMax(delta.y(), -(mObject->height() - MIN_OBJECT_SIZE)));
            break;
        case Edge::West:
            delta.setX(qMin(delta.x(), mObject->width() - MIN_OBJECT_SIZE));
            delta.setY(0);
            break;
        case Edge::East:
            delta.setX(qMax(delta.x(), -(mObject->width() - MIN_OBJECT_SIZE)));
            delta.setY(0);
            break;
        default:
            delta = QPointF(0, 0);
            break;
        }
        return renderer->tileToPixelCoords(mClickObjectPos + delta, level) - clickScenePos;
    } else if (change == ItemPositionHasChanged) {
        if (mCancelResize)
            return QGraphicsItem::itemChange(change, value);
        auto *objectItem = mScene->itemForObject(mObject);
        if (objectItem == nullptr)
            return QGraphicsItem::itemChange(change, value);
        int level = mObject->level();
        QPointF clickScenePos = renderer->tileToPixelCoords(mClickObjectPos, level);
        const QPointF newPos = value.toPointF() + clickScenePos;
        QPointF tileCoords = renderer->pixelToTileCoords(newPos, level);
        QPointF delta = (tileCoords - mClickObjectPos).toPoint();
        switch (mEdge) {
        case Edge::North:
            objectItem->setDragOffset(delta);
            objectItem->setResizeDelta(QSizeF(-delta.x(), -delta.y()));
            break;
        case Edge::South:
            objectItem->setResizeDelta(QSizeF(delta.x(), delta.y()));
            break;
        case Edge::West:
            objectItem->setDragOffset(delta);
            objectItem->setResizeDelta(QSizeF(-delta.x(), -delta.y()));
            break;
        case Edge::East:
            objectItem->setResizeDelta(QSizeF(delta.x(), delta.y()));
            break;
        default:
            break;
        }
        updateOffsetLabel();
    }
    return QGraphicsItem::itemChange(change, value);
}

qreal CellObjectEdgeResizeHandle::edgeThickness(CellScene *scene)
{
    auto view = static_cast<CellView*>(scene->views().first());
    qreal zoom = view->zoomable()->scale();
    qreal T = 0.2 / qMin(zoom, 1.0);
    return T;
}

QRectF CellObjectEdgeResizeHandle::edgeRect() const
{
    qreal T = edgeThickness(mScene);
    int x = mObject->x(), y = mObject->y(), w = mObject->width(), h = mObject->height();
    switch (mEdge) {
    case Edge::NONE:
        return QRectF(x, y, T, T);
    case Edge::North:
        return QRectF(x, y, w, T);
    case Edge::East:
        return QRectF(x + w - T, y, T, h);
    case Edge::South:
        return QRectF(x, y + h - T, w, T);
    case Edge::West:
        return QRectF(x, y, T, h);
    }
    return QRectF();
}

void CellObjectEdgeResizeHandle::updateOffsetLabel()
{
    QSizeF offset;
    if (auto *objectItem = mScene->itemForObject(mObject)) {
        offset = objectItem->resizeDelta();
    }
    if (offset.isNull()) {
        if (mOffsetItem) {
            delete mOffsetItemBG;
            mOffsetItem = nullptr;
            mOffsetItemBG = nullptr;
        }
        return;
    }
    if (mOffsetItem == nullptr) {
        mOffsetItemBG = new QGraphicsRectItem(this);
        mOffsetItemBG->setBrush(Qt::lightGray);
        mOffsetItemBG->setPos(0.0, 0.0);
        mOffsetItem = new QGraphicsSimpleTextItem(mOffsetItemBG);
        mOffsetItem->setPos(4.0, 4.0);
        mOffsetItemBG->setFlags(QGraphicsItem::ItemIgnoresTransformations);
    }
    mOffsetItem->setText(QStringLiteral("offset %1").arg((mEdge == Edge::North || mEdge == Edge::South) ? offset.height() : offset.width()));
    mOffsetItemBG->setRect(QRectF(QPointF(), mOffsetItem->boundingRect().size() + QSizeF(8.0, 8.0)));
    mOffsetItemBG->setPos(mScene->renderer()->tileToPixelCoords(mObject->pos() + mClickObjectPos, mObject->level()));
}
