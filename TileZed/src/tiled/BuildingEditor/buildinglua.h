#ifndef BUILDINGLUA_H
#define BUILDINGLUA_H

#include "properties.h"

#include <QList>
#include <QMap>
#include <QPoint>
#include <QRegion>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

extern "C" {
struct lua_State;
}

namespace BuildingEditor {

class BuildingDocument;
class BuildingFloor;
class BuildingObject;
class FloorTileGrid;
class Room;

/**
 * Runs one Lua script against a detached view of a BuildingEd document.
 *
 * Mutations are recorded in shadow data and are applied to the real document
 * only after the script completes successfully.  applyChanges() creates one
 * undo macro for the complete script.
 */
class BuildingLuaScript
{
public:
    explicit BuildingLuaScript(BuildingDocument *document);
    ~BuildingLuaScript();

    bool run(const QString &fileName, QString *error = nullptr);
    bool applyChanges(const QString &undoText = QString());
    QStringList requestedActions() const { return mRequestedActions; }

private:
    struct FloorState {
        BuildingFloor *floor = nullptr;
        QVector<QVector<Room *> > rooms;
        QList<BuildingObject *> objects;
        QMap<QString, FloorTileGrid *> userTiles;
        QMap<QString, QRegion> changedUserTiles;
    };

    FloorState *floorState(int level);
    const FloorState *floorState(int level) const;
    int roomIndex(Room *room) const;
    BuildingObject *objectAt(FloorState *state, int index) const;
    QPoint objectPosition(BuildingObject *object) const;
    QString objectType(BuildingObject *object) const;
    FloorTileGrid *userTiles(FloorState *state, const QString &layerName,
                             bool create);

    void registerApi(lua_State *state);
    static BuildingLuaScript *fromLua(lua_State *state);
    static int argumentBase(lua_State *state);

    static int luaWidth(lua_State *state);
    static int luaHeight(lua_State *state);
    static int luaFloorCount(lua_State *state);
    static int luaFloorLevel(lua_State *state);
    static int luaCurrentLevel(lua_State *state);
    static int luaRoomCount(lua_State *state);
    static int luaRoomName(lua_State *state);
    static int luaRoomInternalName(lua_State *state);
    static int luaRoomAt(lua_State *state);
    static int luaSetRoom(lua_State *state);
    static int luaFillRoom(lua_State *state);
    static int luaObjectCount(lua_State *state);
    static int luaObjectType(lua_State *state);
    static int luaObjectX(lua_State *state);
    static int luaObjectY(lua_State *state);
    static int luaObjectDirection(lua_State *state);
    static int luaMoveObject(lua_State *state);
    static int luaRemoveObject(lua_State *state);
    static int luaUserLayerNames(lua_State *state);
    static int luaUserTileAt(lua_State *state);
    static int luaSetUserTile(lua_State *state);
    static int luaTileAt(lua_State *state);
    static int luaPlaceTile(lua_State *state);
    static int luaDeleteTile(lua_State *state);
    static int luaDeleteTilesByName(lua_State *state);
    static int luaReplaceTile(lua_State *state);
    static int luaReplaceTilesByName(lua_State *state);
    static int luaProperty(lua_State *state);
    static int luaPropertyNames(lua_State *state);
    static int luaSetProperty(lua_State *state);
    static int luaRemoveProperty(lua_State *state);
    static int luaSetRoomSelection(lua_State *state);
    static int luaClearRoomSelection(lua_State *state);
    static int luaSetTileSelection(lua_State *state);
    static int luaClearTileSelection(lua_State *state);
    static int luaAvailableActions(lua_State *state);
    static int luaInvoke(lua_State *state);

    BuildingDocument *mDocument;
    QList<FloorState *> mFloors;
    Tiled::Properties mProperties;
    QRegion mRoomSelection;
    QRegion mTileSelection;
    QMap<BuildingObject *, QPoint> mMovedObjects;
    QSet<BuildingObject *> mRemovedObjects;
    bool mPropertiesChanged;
    bool mRoomSelectionChanged;
    bool mTileSelectionChanged;
    QStringList mRequestedActions;
};

} // namespace BuildingEditor

#endif // BUILDINGLUA_H
