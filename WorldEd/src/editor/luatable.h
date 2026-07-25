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

#ifndef LUATABLE_H
#define LUATABLE_H

extern "C" {
#include "lua.h"
}

#include <QList>
#include <QSharedPointer>

namespace Lua {

class LuaTable;

enum struct Value
{
    Nil,
    Boolean,
    Number,
    String,
    Table
};

class LuaValue
{
public:
    LuaValue() : value(Value::Nil), b(false), n(0.0), s(), t(nullptr) {}
    LuaValue(bool value) : value(Value::Boolean), b(value), n(0.0), s(), t(nullptr) {}
    LuaValue(lua_Number value) : value(Value::Number), b(false), n(value), s(), t(nullptr) {}
    LuaValue(QString value) : value(Value::String), b(false), n(0.0), s(value), t(nullptr) {}
    LuaValue(LuaTable *value) : value(Value::Table), b(false), n(0.0), s(), t(value) {}
    ~LuaValue() {}

    bool is(Value value) const { return this->value == value; }
    bool isBoolean() const { return is(Value::Boolean); }
    bool isNumber() const { return is(Value::Number); }
    bool isString() const { return is(Value::String); }
    bool isTable() const { return is(Value::Table); }

    bool operator==(const LuaValue &other) const {
        if (this->value != other.value) {
            return false;
        }
        switch (value) {
        case Value::Nil: return true;
        case Value::Boolean: return b == other.b;
        case Value::Number: return n == other.n;
        case Value::String: return s == other.s;
        case Value::Table: return t == other.t; // the same object pointer
        }
        return false;
    }

    Value value;
    bool b;
    lua_Number n;
    QString s;
    QSharedPointer<LuaTable> t;
};

class LuaTableKeyValue
{
public:
    LuaTableKeyValue(const LuaValue &key, const LuaValue &value) : key(key), value(value) {}
    ~LuaTableKeyValue() {}

    LuaValue key;
    LuaValue value;
};

class LuaTable
{
public:
    ~LuaTable()
    {
        qDeleteAll(kv);
    }

    LuaTableKeyValue* find(const LuaValue& key);
    LuaTableKeyValue* find(const LuaValue& key, Value value);
    bool getBoolean(const LuaValue& key, bool& out);
    bool getNumber(const LuaValue& key, lua_Number& out);
    bool getString(const LuaValue& key, QString& out);
    LuaTable* getTable(const LuaValue& key);

    QList<LuaTableKeyValue*> kv;
};

LuaValue parseValue(lua_State *L);
LuaTable *parseTable(lua_State *L);

} // namespace Lua

#endif // LUATABLE_H
