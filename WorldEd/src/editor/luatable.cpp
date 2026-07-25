/*
 * Copyright 2025, Tim Baker <treectrl@users.sf.net>
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

#include "luatable.h"

using namespace Lua;

LuaTableKeyValue *LuaTable::find(const LuaValue &key)
{
    for (LuaTableKeyValue* keyValue : kv) {
        if (keyValue->key == key) {
            return keyValue;
        }
    }
    return nullptr;
}

LuaTableKeyValue *LuaTable::find(const LuaValue &key, Value value)
{
    if (LuaTableKeyValue *kv = find(key)) {
        if (kv->value.is(value)) {
            return kv;
        }
    }
    return nullptr;
}

bool LuaTable::getBoolean(const LuaValue &key, bool &out)
{
    if (LuaTableKeyValue* kv = find(key, Value::Boolean)) {
        out = kv->value.b;
        return true;
    }
    return false;

}

bool LuaTable::getNumber(const LuaValue &key, lua_Number &out)
{
    if (LuaTableKeyValue* kv = find(key, Value::Number)) {
        out = kv->value.n;
        return true;
    }
    return false;
}

bool LuaTable::getString(const LuaValue &key, QString &out)
{
    if (LuaTableKeyValue* kv = find(key, Value::String)) {
        out = kv->value.s;
        return true;
    }
    return false;
}

LuaTable *LuaTable::getTable(const LuaValue &key)
{
    if (LuaTableKeyValue* kv = find(key, Value::Table)) {
        return kv->value.t.data();
    }
    return nullptr;
}

LuaValue Lua::parseValue(lua_State *L)
{
    if (lua_isboolean(L, -1)) {
        return LuaValue(lua_toboolean(L, -1) == 1);
    }
    if (lua_isnumber(L, -1)) {
        return LuaValue(lua_tonumber(L, -1));
    }
    if (lua_isstring(L, -1)) {
        return LuaValue(QString::fromLatin1(lua_tostring(L, -1)));
    }
    if (lua_istable(L, -1)) {
        return LuaValue(parseTable(L));
    }
    return LuaValue();
}

LuaTable *Lua::parseTable(lua_State *L)
{
    LuaTable *table = new LuaTable();
    lua_pushnil(L); // push key
    while (lua_next(L, -2)) {
        // stack now contains: -1 => value; -2 => key; -3 => table
        // copy the key so that lua_tostring does not modify the original
        lua_pushvalue(L, -2);
        LuaValue key = Lua::parseValue(L);
        lua_pop(L, 1); // pop copy of key
        LuaValue value = Lua::parseValue(L);
        lua_pop(L, 1); // pop value
        table->kv += new LuaTableKeyValue(key, value);
        // stack now contains: -1 => key; -2 => table
    }
    return table;
}
