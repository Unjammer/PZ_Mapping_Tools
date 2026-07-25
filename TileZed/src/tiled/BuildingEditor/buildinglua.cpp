#include "buildinglua.h"

#include "building.h"
#include "buildingdocument.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingtemplates.h"
#include "buildingtiles.h"
#include "buildingundoredo.h"

#include "luaconsole.h"
#include "zprogress.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QUndoStack>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

using namespace BuildingEditor;

namespace {

static QByteArray utf8(const QString &text)
{
    return text.toUtf8();
}

static QString luaString(lua_State *state, int index)
{
    size_t length = 0;
    const char *value = luaL_checklstring(state, index, &length);
    return QString::fromUtf8(value, int(length));
}

static void pushString(lua_State *state, const QString &value)
{
    const QByteArray bytes = utf8(value);
    lua_pushlstring(state, bytes.constData(), size_t(bytes.size()));
}

static int traceback(lua_State *state)
{
    const char *message = lua_tostring(state, 1);
    if (message)
        luaL_traceback(state, state, message, 1);
    else if (!lua_isnoneornil(state, 1)
             && !luaL_callmeta(state, 1, "__tostring"))
        lua_pushliteral(state, "(no error message)");
    return 1;
}

static void cancellationHook(lua_State *state, lua_Debug *)
{
    qApp->processEvents(QEventLoop::AllEvents);
    if (ZProgressManager::instance()->wasCanceled()) {
        lua_sethook(state, nullptr, 0, 0);
        luaL_error(state, "Script cancelled by user.");
    }
}

static void setFunction(lua_State *state, const char *name, lua_CFunction fn,
                        BuildingLuaScript *script)
{
    lua_pushlightuserdata(state, script);
    lua_pushcclosure(state, fn, 1);
    lua_setfield(state, -2, name);
}

} // namespace

BuildingLuaScript::BuildingLuaScript(BuildingDocument *document)
    : mDocument(document)
    , mProperties(document->building()->properties())
    , mRoomSelection(document->roomSelection())
    , mTileSelection(document->tileSelection())
    , mPropertiesChanged(false)
    , mRoomSelectionChanged(false)
    , mTileSelectionChanged(false)
{
    for (BuildingFloor *floor : document->building()->floors()) {
        FloorState *state = new FloorState;
        state->floor = floor;
        state->rooms = floor->grid();
        state->objects = floor->objects();
        for (auto it = floor->grime().constBegin();
             it != floor->grime().constEnd(); ++it) {
            state->userTiles.insert(it.key(), it.value()->clone());
        }
        mFloors.append(state);
    }
}

BuildingLuaScript::~BuildingLuaScript()
{
    for (FloorState *state : mFloors) {
        qDeleteAll(state->userTiles);
        delete state;
    }
}

BuildingLuaScript::FloorState *BuildingLuaScript::floorState(int level)
{
    for (FloorState *state : mFloors) {
        if (state->floor->level() == level)
            return state;
    }
    return nullptr;
}

const BuildingLuaScript::FloorState *BuildingLuaScript::floorState(int level) const
{
    for (const FloorState *state : mFloors) {
        if (state->floor->level() == level)
            return state;
    }
    return nullptr;
}

int BuildingLuaScript::roomIndex(Room *room) const
{
    return room ? mDocument->building()->rooms().indexOf(room) : -1;
}

BuildingObject *BuildingLuaScript::objectAt(FloorState *state, int index) const
{
    return state && index >= 0 && index < state->objects.size()
            ? state->objects.at(index) : nullptr;
}

QPoint BuildingLuaScript::objectPosition(BuildingObject *object) const
{
    return mMovedObjects.contains(object)
            ? mMovedObjects.value(object) : object->pos();
}

QString BuildingLuaScript::objectType(BuildingObject *object) const
{
    if (object->asDoor()) return QStringLiteral("Door");
    if (object->asWindow()) return QStringLiteral("Window");
    if (object->asStairs()) return QStringLiteral("Stairs");
    if (object->asFurniture()) return QStringLiteral("Furniture");
    if (object->asRoof()) return QStringLiteral("Roof");
    if (object->asWall()) return QStringLiteral("Wall");
    return QStringLiteral("Object");
}

FloorTileGrid *BuildingLuaScript::userTiles(FloorState *state,
                                            const QString &layerName,
                                            bool create)
{
    if (!state)
        return nullptr;
    FloorTileGrid *grid = state->userTiles.value(layerName);
    if (!grid && create) {
        grid = new FloorTileGrid(state->floor->width() + 1,
                                 state->floor->height() + 1);
        state->userTiles.insert(layerName, grid);
    }
    return grid;
}

BuildingLuaScript *BuildingLuaScript::fromLua(lua_State *state)
{
    return static_cast<BuildingLuaScript *>(
                lua_touserdata(state, lua_upvalueindex(1)));
}

int BuildingLuaScript::argumentBase(lua_State *state)
{
    return lua_istable(state, 1) ? 2 : 1;
}

void BuildingLuaScript::registerApi(lua_State *state)
{
    lua_newtable(state);
    lua_pushinteger(state, 2);
    lua_setfield(state, -2, "apiVersion");

    setFunction(state, "width", luaWidth, this);
    setFunction(state, "height", luaHeight, this);
    setFunction(state, "floorCount", luaFloorCount, this);
    setFunction(state, "floorLevel", luaFloorLevel, this);
    setFunction(state, "currentLevel", luaCurrentLevel, this);
    setFunction(state, "roomCount", luaRoomCount, this);
    setFunction(state, "roomName", luaRoomName, this);
    setFunction(state, "roomInternalName", luaRoomInternalName, this);
    setFunction(state, "roomAt", luaRoomAt, this);
    setFunction(state, "setRoom", luaSetRoom, this);
    setFunction(state, "fillRoom", luaFillRoom, this);
    setFunction(state, "objectCount", luaObjectCount, this);
    setFunction(state, "objectType", luaObjectType, this);
    setFunction(state, "objectX", luaObjectX, this);
    setFunction(state, "objectY", luaObjectY, this);
    setFunction(state, "objectDirection", luaObjectDirection, this);
    setFunction(state, "moveObject", luaMoveObject, this);
    setFunction(state, "removeObject", luaRemoveObject, this);
    setFunction(state, "userLayerNames", luaUserLayerNames, this);
    setFunction(state, "userTileAt", luaUserTileAt, this);
    setFunction(state, "setUserTile", luaSetUserTile, this);
    setFunction(state, "tileAt", luaTileAt, this);
    setFunction(state, "placeTile", luaPlaceTile, this);
    setFunction(state, "deleteTile", luaDeleteTile, this);
    setFunction(state, "deleteTilesByName", luaDeleteTilesByName, this);
    setFunction(state, "replaceTile", luaReplaceTile, this);
    setFunction(state, "replaceTilesByName", luaReplaceTilesByName, this);
    setFunction(state, "property", luaProperty, this);
    setFunction(state, "propertyNames", luaPropertyNames, this);
    setFunction(state, "setProperty", luaSetProperty, this);
    setFunction(state, "removeProperty", luaRemoveProperty, this);
    setFunction(state, "setRoomSelection", luaSetRoomSelection, this);
    setFunction(state, "clearRoomSelection", luaClearRoomSelection, this);
    setFunction(state, "setTileSelection", luaSetTileSelection, this);
    setFunction(state, "clearTileSelection", luaClearTileSelection, this);

    lua_setglobal(state, "building");

    lua_newtable(state);
    lua_pushinteger(state, 1);
    lua_setfield(state, -2, "apiVersion");
    setFunction(state, "availableActions", luaAvailableActions, this);
    setFunction(state, "invoke", luaInvoke, this);
    lua_setglobal(state, "app");
}

bool BuildingLuaScript::run(const QString &fileName, QString *error)
{
    lua_State *state = luaL_newstate();
    if (!state) {
        if (error)
            *error = QObject::tr("Lua could not allocate a new state.");
        return false;
    }

    luaL_openlibs(state);
    registerApi(state);
    pushString(state, QFileInfo(fileName).absolutePath());
    lua_setglobal(state, "scriptDirectory");

    PROGRESS progress(QObject::tr("Running BuildingEd Lua script: %1")
                      .arg(QFileInfo(fileName).fileName()),
                      LuaConsole::instance(), true);
    QElapsedTimer elapsed;
    elapsed.start();

    const QByteArray nativeFileName = QFile::encodeName(fileName);
    int status = luaL_loadfile(state, nativeFileName.constData());
    if (status == LUA_OK) {
        const int base = lua_gettop(state);
        lua_pushcfunction(state, traceback);
        lua_insert(state, base);
        lua_sethook(state, cancellationHook, LUA_MASKCOUNT, 10000);
        status = lua_pcall(state, 0, 0, base);
        lua_sethook(state, nullptr, 0, 0);
        lua_remove(state, base);
    }

    if (status != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        QString text = QString::fromUtf8(message ? message : "");
        if (text.contains(QChar::ReplacementCharacter))
            text = QString::fromLocal8Bit(message ? message : "");
        if (error)
            *error = text;
        LuaConsole::instance()->write(text, Qt::red);
    }

    const QString result = progress.wasCanceled()
            ? QObject::tr("---------- BuildingEd script cancelled after %1s ----------")
            : QObject::tr("---------- BuildingEd script completed in %1s ----------");
    LuaConsole::instance()->write(result.arg(elapsed.elapsed() / 1000.0));
    lua_close(state);
    return status == LUA_OK;
}

bool BuildingLuaScript::applyChanges(const QString &undoText)
{
    int changeCount = 0;
    for (const FloorState *state : mFloors) {
        const QVector<QVector<Room *> > &live = state->floor->grid();
        for (int x = 0; x < state->rooms.size(); ++x) {
            for (int y = 0; y < state->rooms[x].size(); ++y) {
                if (state->rooms[x][y] != live[x][y])
                    ++changeCount;
            }
        }
        changeCount += state->changedUserTiles.size();
    }
    changeCount += mMovedObjects.size() + mRemovedObjects.size();
    changeCount += mPropertiesChanged ? 1 : 0;
    changeCount += mRoomSelectionChanged ? 1 : 0;
    changeCount += mTileSelectionChanged ? 1 : 0;
    if (!changeCount)
        return false;

    QUndoStack *stack = mDocument->undoStack();
    stack->beginMacro(undoText.isEmpty()
                      ? QObject::tr("BuildingEd Lua Script") : undoText);

    for (FloorState *state : mFloors) {
        const QVector<QVector<Room *> > &live = state->floor->grid();
        for (int x = 0; x < state->rooms.size(); ++x) {
            for (int y = 0; y < state->rooms[x].size(); ++y) {
                if (state->rooms[x][y] != live[x][y]) {
                    stack->push(new ChangeRoomAtPosition(
                                    mDocument, state->floor, QPoint(x, y),
                                    state->rooms[x][y]));
                }
            }
        }

        for (auto it = state->changedUserTiles.constBegin();
             it != state->changedUserTiles.constEnd(); ++it) {
            const QRegion region = it.value();
            const QRect bounds = region.boundingRect();
            FloorTileGrid *patch =
                    state->userTiles.value(it.key())->clone(bounds, region);
            stack->push(new PaintFloorTiles(
                            mDocument, state->floor, it.key(), region,
                            bounds.topLeft(), patch, "BuildingEd Lua Script"));
        }
    }

    for (auto it = mMovedObjects.constBegin(); it != mMovedObjects.constEnd(); ++it) {
        if (!mRemovedObjects.contains(it.key()) && it.key()->pos() != it.value())
            stack->push(new MoveObject(mDocument, it.key(), it.value()));
    }

    for (BuildingObject *object : mRemovedObjects) {
        BuildingFloor *floor = object->floor();
        const int index = floor->indexOf(object);
        if (index >= 0)
            stack->push(new RemoveObject(mDocument, floor, index));
    }

    if (mPropertiesChanged)
        stack->push(new ChangeBuildingKeyValues(mDocument, mProperties));
    if (mRoomSelectionChanged)
        stack->push(new ChangeRoomSelection(mDocument, mRoomSelection));
    if (mTileSelectionChanged)
        stack->push(new ChangeTileSelection(mDocument, mTileSelection));

    stack->endMacro();
    return true;
}

int BuildingLuaScript::luaWidth(lua_State *state)
{
    lua_pushinteger(state, fromLua(state)->mDocument->building()->width());
    return 1;
}

int BuildingLuaScript::luaHeight(lua_State *state)
{
    lua_pushinteger(state, fromLua(state)->mDocument->building()->height());
    return 1;
}

int BuildingLuaScript::luaFloorCount(lua_State *state)
{
    lua_pushinteger(state, fromLua(state)->mFloors.size());
    return 1;
}

int BuildingLuaScript::luaFloorLevel(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    const int index = int(luaL_checkinteger(state, base));
    if (index < 0 || index >= self->mFloors.size())
        return luaL_error(state, "floor index out of range");
    lua_pushinteger(state, self->mFloors.at(index)->floor->level());
    return 1;
}

int BuildingLuaScript::luaCurrentLevel(lua_State *state)
{
    lua_pushinteger(state, fromLua(state)->mDocument->currentLevel());
    return 1;
}

int BuildingLuaScript::luaRoomCount(lua_State *state)
{
    lua_pushinteger(state, fromLua(state)->mDocument->building()->roomCount());
    return 1;
}

int BuildingLuaScript::luaRoomName(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int index = int(luaL_checkinteger(state, argumentBase(state)));
    if (index < 0 || index >= self->mDocument->building()->roomCount())
        return luaL_error(state, "room index out of range");
    pushString(state, self->mDocument->building()->room(index)->Name);
    return 1;
}

int BuildingLuaScript::luaRoomInternalName(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int index = int(luaL_checkinteger(state, argumentBase(state)));
    if (index < 0 || index >= self->mDocument->building()->roomCount())
        return luaL_error(state, "room index out of range");
    pushString(state, self->mDocument->building()->room(index)->internalName);
    return 1;
}

int BuildingLuaScript::luaRoomAt(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const int x = int(luaL_checkinteger(state, base + 1));
    const int y = int(luaL_checkinteger(state, base + 2));
    if (!floor || !floor->floor->contains(x, y))
        return luaL_error(state, "invalid floor level or room coordinate");
    lua_pushinteger(state, self->roomIndex(floor->rooms[x][y]));
    return 1;
}

int BuildingLuaScript::luaSetRoom(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const int x = int(luaL_checkinteger(state, base + 1));
    const int y = int(luaL_checkinteger(state, base + 2));
    const int roomIndex = int(luaL_checkinteger(state, base + 3));
    if (!floor || !floor->floor->contains(x, y))
        return luaL_error(state, "invalid floor level or room coordinate");
    if (roomIndex < -1 || roomIndex >= self->mDocument->building()->roomCount())
        return luaL_error(state, "room index out of range");
    floor->rooms[x][y] = roomIndex < 0
            ? nullptr : self->mDocument->building()->room(roomIndex);
    return 0;
}

int BuildingLuaScript::luaFillRoom(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QRect area(int(luaL_checkinteger(state, base + 1)),
                     int(luaL_checkinteger(state, base + 2)),
                     int(luaL_checkinteger(state, base + 3)),
                     int(luaL_checkinteger(state, base + 4)));
    const int roomIndex = int(luaL_checkinteger(state, base + 5));
    if (!floor || area.isEmpty()
            || !floor->floor->bounds().contains(area))
        return luaL_error(state, "invalid floor level or room rectangle");
    if (roomIndex < -1 || roomIndex >= self->mDocument->building()->roomCount())
        return luaL_error(state, "room index out of range");
    Room *room = roomIndex < 0
            ? nullptr : self->mDocument->building()->room(roomIndex);
    for (int x = area.left(); x <= area.right(); ++x)
        for (int y = area.top(); y <= area.bottom(); ++y)
            floor->rooms[x][y] = room;
    return 0;
}

int BuildingLuaScript::luaObjectCount(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    FloorState *floor = self->floorState(
                int(luaL_checkinteger(state, argumentBase(state))));
    if (!floor)
        return luaL_error(state, "invalid floor level");
    lua_pushinteger(state, floor->objects.size());
    return 1;
}

int BuildingLuaScript::luaObjectType(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    BuildingObject *object = self->objectAt(
                floor, int(luaL_checkinteger(state, base + 1)));
    if (!object)
        return luaL_error(state, "object index out of range");
    pushString(state, self->objectType(object));
    return 1;
}

int BuildingLuaScript::luaObjectX(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    BuildingObject *object = self->objectAt(
                floor, int(luaL_checkinteger(state, base + 1)));
    if (!object)
        return luaL_error(state, "object index out of range");
    lua_pushinteger(state, self->objectPosition(object).x());
    return 1;
}

int BuildingLuaScript::luaObjectY(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    BuildingObject *object = self->objectAt(
                floor, int(luaL_checkinteger(state, base + 1)));
    if (!object)
        return luaL_error(state, "object index out of range");
    lua_pushinteger(state, self->objectPosition(object).y());
    return 1;
}

int BuildingLuaScript::luaObjectDirection(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    BuildingObject *object = self->objectAt(
                floor, int(luaL_checkinteger(state, base + 1)));
    if (!object)
        return luaL_error(state, "object index out of range");
    pushString(state, object->dirString());
    return 1;
}

int BuildingLuaScript::luaMoveObject(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    BuildingObject *object = self->objectAt(
                floor, int(luaL_checkinteger(state, base + 1)));
    const QPoint target(int(luaL_checkinteger(state, base + 2)),
                        int(luaL_checkinteger(state, base + 3)));
    if (!object)
        return luaL_error(state, "object index out of range");
    if (!object->isValidPos(target - object->pos(), floor->floor))
        return luaL_error(state, "invalid target position for object");
    self->mMovedObjects.insert(object, target);
    return 0;
}

int BuildingLuaScript::luaRemoveObject(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const int index = int(luaL_checkinteger(state, base + 1));
    BuildingObject *object = self->objectAt(floor, index);
    if (!object)
        return luaL_error(state, "object index out of range");
    floor->objects.removeAt(index);
    self->mMovedObjects.remove(object);
    self->mRemovedObjects.insert(object);
    return 0;
}

int BuildingLuaScript::luaUserLayerNames(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    FloorState *floor = self->floorState(
                int(luaL_checkinteger(state, argumentBase(state))));
    if (!floor)
        return luaL_error(state, "invalid floor level");
    const QStringList names = floor->userTiles.keys();
    lua_createtable(state, names.size(), 0);
    for (int i = 0; i < names.size(); ++i) {
        pushString(state, names.at(i));
        lua_rawseti(state, -2, i + 1);
    }
    return 1;
}

int BuildingLuaScript::luaUserTileAt(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const int x = int(luaL_checkinteger(state, base + 2));
    const int y = int(luaL_checkinteger(state, base + 3));
    FloorTileGrid *grid = self->userTiles(floor, layerName, false);
    if (!floor || !floor->floor->contains(x, y, 1, 1))
        return luaL_error(state, "invalid floor level or user-tile coordinate");
    pushString(state, grid && grid->contains(x, y)
               ? grid->at(x, y) : QString());
    return 1;
}

int BuildingLuaScript::luaSetUserTile(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const int x = int(luaL_checkinteger(state, base + 2));
    const int y = int(luaL_checkinteger(state, base + 3));
    const QString tileName = luaString(state, base + 4);
    if (!floor || layerName.isEmpty()
            || !floor->floor->contains(x, y, 1, 1))
        return luaL_error(state, "invalid floor, layer, or user-tile coordinate");
    FloorTileGrid *grid = self->userTiles(floor, layerName, true);
    grid->replace(x, y, tileName);
    floor->changedUserTiles[layerName] += QRect(x, y, 1, 1);
    return 0;
}

int BuildingLuaScript::luaTileAt(lua_State *state)
{
    return luaUserTileAt(state);
}

int BuildingLuaScript::luaPlaceTile(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const int x = int(luaL_checkinteger(state, base + 2));
    const int y = int(luaL_checkinteger(state, base + 3));
    const QString tileName = luaString(state, base + 4);
    if (!floor || layerName.isEmpty()
            || !floor->floor->contains(x, y, 1, 1))
        return luaL_error(state, "invalid floor, layer, or tile coordinate");
    if (tileName.isEmpty()
            || !BuildingTilesMgr::legalTileName(tileName)
            || !BuildingTilesMgr::instance()->tileFor(tileName))
        return luaL_error(state, "unknown or invalid tile name");

    FloorTileGrid *grid = self->userTiles(floor, layerName, true);
    grid->replace(x, y, tileName);
    floor->changedUserTiles[layerName] += QRect(x, y, 1, 1);
    lua_pushboolean(state, 1);
    return 1;
}

int BuildingLuaScript::luaDeleteTile(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const int x = int(luaL_checkinteger(state, base + 2));
    const int y = int(luaL_checkinteger(state, base + 3));
    if (!floor || layerName.isEmpty()
            || !floor->floor->contains(x, y, 1, 1))
        return luaL_error(state, "invalid floor, layer, or tile coordinate");

    FloorTileGrid *grid = self->userTiles(floor, layerName, false);
    if (!grid || !grid->contains(x, y) || grid->at(x, y).isEmpty()) {
        lua_pushboolean(state, 0);
        return 1;
    }
    if (lua_gettop(state) >= base + 4) {
        const QString expected = luaString(state, base + 4);
        if (grid->at(x, y) != expected) {
            lua_pushboolean(state, 0);
            return 1;
        }
    }
    grid->replace(x, y, QString());
    floor->changedUserTiles[layerName] += QRect(x, y, 1, 1);
    lua_pushboolean(state, 1);
    return 1;
}

int BuildingLuaScript::luaDeleteTilesByName(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const QString tileName = luaString(state, base + 2);
    if (!floor || layerName.isEmpty() || tileName.isEmpty())
        return luaL_error(state, "invalid floor, layer, or tile name");

    FloorTileGrid *grid = self->userTiles(floor, layerName, false);
    int changed = 0;
    if (grid) {
        for (int y = 0; y < grid->height(); ++y) {
            for (int x = 0; x < grid->width(); ++x) {
                if (grid->at(x, y) == tileName) {
                    grid->replace(x, y, QString());
                    floor->changedUserTiles[layerName] += QRect(x, y, 1, 1);
                    ++changed;
                }
            }
        }
    }
    lua_pushinteger(state, changed);
    return 1;
}

int BuildingLuaScript::luaReplaceTile(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const int x = int(luaL_checkinteger(state, base + 2));
    const int y = int(luaL_checkinteger(state, base + 3));
    const QString oldTileName = luaString(state, base + 4);
    const QString newTileName = luaString(state, base + 5);
    if (!floor || layerName.isEmpty()
            || !floor->floor->contains(x, y, 1, 1))
        return luaL_error(state, "invalid floor, layer, or tile coordinate");
    if (newTileName.isEmpty()
            || !BuildingTilesMgr::legalTileName(newTileName)
            || !BuildingTilesMgr::instance()->tileFor(newTileName))
        return luaL_error(state, "unknown or invalid replacement tile name");

    FloorTileGrid *grid = self->userTiles(floor, layerName, false);
    if (!grid || !grid->contains(x, y)
            || grid->at(x, y) != oldTileName) {
        lua_pushboolean(state, 0);
        return 1;
    }
    grid->replace(x, y, newTileName);
    floor->changedUserTiles[layerName] += QRect(x, y, 1, 1);
    lua_pushboolean(state, 1);
    return 1;
}

int BuildingLuaScript::luaReplaceTilesByName(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    FloorState *floor = self->floorState(int(luaL_checkinteger(state, base)));
    const QString layerName = luaString(state, base + 1);
    const QString oldTileName = luaString(state, base + 2);
    const QString newTileName = luaString(state, base + 3);
    if (!floor || layerName.isEmpty() || oldTileName.isEmpty())
        return luaL_error(state, "invalid floor, layer, or source tile name");
    if (newTileName.isEmpty()
            || !BuildingTilesMgr::legalTileName(newTileName)
            || !BuildingTilesMgr::instance()->tileFor(newTileName))
        return luaL_error(state, "unknown or invalid replacement tile name");

    FloorTileGrid *grid = self->userTiles(floor, layerName, false);
    int changed = 0;
    if (grid) {
        for (int y = 0; y < grid->height(); ++y) {
            for (int x = 0; x < grid->width(); ++x) {
                if (grid->at(x, y) == oldTileName) {
                    grid->replace(x, y, newTileName);
                    floor->changedUserTiles[layerName] += QRect(x, y, 1, 1);
                    ++changed;
                }
            }
        }
    }
    lua_pushinteger(state, changed);
    return 1;
}

int BuildingLuaScript::luaProperty(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const QString key = luaString(state, argumentBase(state));
    if (!self->mProperties.contains(key)) {
        lua_pushnil(state);
        return 1;
    }
    pushString(state, self->mProperties.value(key));
    return 1;
}

int BuildingLuaScript::luaPropertyNames(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    QStringList names = self->mProperties.keys();
    names.sort(Qt::CaseInsensitive);
    lua_createtable(state, names.size(), 0);
    for (int i = 0; i < names.size(); ++i) {
        pushString(state, names.at(i));
        lua_rawseti(state, -2, i + 1);
    }
    return 1;
}

int BuildingLuaScript::luaSetProperty(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    const QString key = luaString(state, base);
    const QString value = luaString(state, base + 1);
    if (key.isEmpty())
        return luaL_error(state, "property name must not be empty");
    self->mProperties.insert(key, value);
    self->mPropertiesChanged = true;
    return 0;
}

int BuildingLuaScript::luaRemoveProperty(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const QString key = luaString(state, argumentBase(state));
    if (self->mProperties.remove(key))
        self->mPropertiesChanged = true;
    return 0;
}

int BuildingLuaScript::luaSetRoomSelection(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    self->mRoomSelection = QRegion(
                int(luaL_checkinteger(state, base)),
                int(luaL_checkinteger(state, base + 1)),
                int(luaL_checkinteger(state, base + 2)),
                int(luaL_checkinteger(state, base + 3)));
    self->mRoomSelectionChanged = true;
    return 0;
}

int BuildingLuaScript::luaClearRoomSelection(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    self->mRoomSelection = QRegion();
    self->mRoomSelectionChanged = true;
    return 0;
}

int BuildingLuaScript::luaSetTileSelection(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const int base = argumentBase(state);
    self->mTileSelection = QRegion(
                int(luaL_checkinteger(state, base)),
                int(luaL_checkinteger(state, base + 1)),
                int(luaL_checkinteger(state, base + 2)),
                int(luaL_checkinteger(state, base + 3)));
    self->mTileSelectionChanged = true;
    return 0;
}

int BuildingLuaScript::luaClearTileSelection(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    self->mTileSelection = QRegion();
    self->mTileSelectionChanged = true;
    return 0;
}

int BuildingLuaScript::luaAvailableActions(lua_State *state)
{
    static const char *const actions[] = {
        "save", "saveAs", "exportTMX", "exportBinary",
        "buildingProperties", "rooms", "floors", "tiles"
    };
    const int count = int(sizeof(actions) / sizeof(actions[0]));
    lua_createtable(state, count, 0);
    for (int i = 0; i < count; ++i) {
        lua_pushstring(state, actions[i]);
        lua_rawseti(state, -2, i + 1);
    }
    return 1;
}

int BuildingLuaScript::luaInvoke(lua_State *state)
{
    BuildingLuaScript *self = fromLua(state);
    const QString action = luaString(state, argumentBase(state));
    const QStringList available = {
        QStringLiteral("save"),
        QStringLiteral("saveAs"),
        QStringLiteral("exportTMX"),
        QStringLiteral("exportBinary"),
        QStringLiteral("buildingProperties"),
        QStringLiteral("rooms"),
        QStringLiteral("floors"),
        QStringLiteral("tiles")
    };
    if (!available.contains(action))
        return luaL_error(state, "unknown or unavailable BuildingEd action");
    self->mRequestedActions.append(action);
    return 0;
}
