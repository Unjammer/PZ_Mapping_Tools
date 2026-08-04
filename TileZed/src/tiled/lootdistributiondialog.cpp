/*
 * Copyright 2026, PZ Mapping Tools contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "lootdistributiondialog.h"

#include "preferences.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QBrush>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>
#include <cmath>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace Tiled {
namespace Internal {

namespace {

const char kManifestRelativePath[] =
        "media/lua/server/Items/PZToolsLootEditor.json";
const char kLuaRelativePath[] =
        "media/lua/server/Items/PZToolsLootDefinitions.lua";

QString luaString(lua_State *state, int index)
{
    size_t length = 0;
    const char *value = lua_tolstring(state, index, &length);
    return value ? QString::fromUtf8(value, int(length)) : QString();
}

void lootInstructionLimit(lua_State *state, lua_Debug *)
{
    luaL_error(state, "loot definition exceeded the editor instruction limit");
}

QString normalizedItemsRoot(const QString &requestedPath)
{
    const QString path =
            QDir::cleanPath(QDir::fromNativeSeparators(requestedPath.trimmed()));
    if (path.isEmpty())
        return QString();

    const QStringList candidates = {
        path,
        QDir(path).filePath(QStringLiteral("media/lua/server/Items")),
        QDir(path).filePath(QStringLiteral("lua/server/Items")),
        QDir(path).filePath(QStringLiteral("server/Items"))
    };
    for (const QString &candidate : candidates) {
        const QDir directory(candidate);
        if (QFileInfo(directory.filePath(
                          QStringLiteral("Distributions.lua"))).isFile()
                && QFileInfo(directory.filePath(
                                 QStringLiteral("ProceduralDistributions.lua")))
                   .isFile()) {
            return QDir::cleanPath(directory.absolutePath());
        }
    }
    return QString();
}

void removeLuaGlobal(lua_State *state, const char *name)
{
    lua_pushnil(state);
    lua_setglobal(state, name);
}

bool executeLuaFile(lua_State *state, const QString &fileName, QString *error)
{
    const QByteArray encoded = QFile::encodeName(fileName);
    int status = luaL_loadfile(state, encoded.constData());
    if (status == LUA_OK)
        status = lua_pcall(state, 0, 0, 0);
    if (status == LUA_OK)
        return true;

    if (error) {
        *error = QObject::tr("%1\n\n%2")
                .arg(QDir::toNativeSeparators(fileName),
                     luaString(state, -1));
    }
    lua_pop(state, 1);
    return false;
}

bool tableIsArray(lua_State *state, int index, const QString &hint)
{
    index = lua_absindex(state, index);
    const int length = int(lua_rawlen(state, index));
    if (length == 0) {
        return hint == QLatin1String("items")
                || hint == QLatin1String("procList")
                || hint == QLatin1String("bags");
    }

    int count = 0;
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        bool valid = false;
        if (lua_type(state, -2) == LUA_TNUMBER) {
            const lua_Number number = lua_tonumber(state, -2);
            const int integer = int(number);
            valid = number == lua_Number(integer)
                    && integer >= 1 && integer <= length;
        }
        lua_pop(state, 1);
        if (!valid) {
            lua_pop(state, 1);
            return false;
        }
        ++count;
    }
    return count == length;
}

QJsonValue luaToJson(lua_State *state, int index, int depth,
                     const QString &hint = QString())
{
    if (depth > 32)
        return QJsonValue();
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        return QJsonValue(bool(lua_toboolean(state, index)));
    case LUA_TNUMBER:
        return QJsonValue(double(lua_tonumber(state, index)));
    case LUA_TSTRING:
        return QJsonValue(luaString(state, index));
    case LUA_TTABLE: {
        if (tableIsArray(state, index, hint)) {
            QJsonArray array;
            const int length = int(lua_rawlen(state, index));
            for (int i = 1; i <= length; ++i) {
                lua_rawgeti(state, index, i);
                array.append(luaToJson(state, -1, depth + 1));
                lua_pop(state, 1);
            }
            return array;
        }

        QJsonObject object;
        lua_pushnil(state);
        while (lua_next(state, index) != 0) {
            QString key;
            if (lua_type(state, -2) == LUA_TSTRING)
                key = luaString(state, -2);
            else if (lua_type(state, -2) == LUA_TNUMBER)
                key = QString::number(lua_tonumber(state, -2), 'g', 15);
            if (!key.isEmpty())
                object.insert(key, luaToJson(state, -1, depth + 1, key));
            lua_pop(state, 1);
        }
        return object;
    }
    default:
        return QJsonValue();
    }
}

QJsonObject readLuaGlobalObject(lua_State *state, const char *name)
{
    QJsonObject result;
    lua_getglobal(state, name);
    if (lua_istable(state, -1))
        result = luaToJson(state, -1, 0).toObject();
    lua_pop(state, 1);
    return result;
}

struct LootData
{
    QString itemsRoot;
    QString projectRoot;
    QJsonObject gameRooms;
    QJsonObject gameProcedures;
    QJsonObject projectRooms;
    QJsonObject projectProcedures;

    QJsonObject rooms() const
    {
        QJsonObject result = gameRooms;
        for (auto roomIt = projectRooms.begin();
             roomIt != projectRooms.end(); ++roomIt) {
            QJsonObject room = result.value(roomIt.key()).toObject();
            const QJsonObject localRoom = roomIt.value().toObject();
            for (auto containerIt = localRoom.begin();
                 containerIt != localRoom.end(); ++containerIt) {
                room.insert(containerIt.key(), containerIt.value());
            }
            result.insert(roomIt.key(), room);
        }
        return result;
    }

    QJsonObject procedures() const
    {
        QJsonObject result = gameProcedures;
        for (auto it = projectProcedures.begin();
             it != projectProcedures.end(); ++it) {
            result.insert(it.key(), it.value());
        }
        return result;
    }

    bool isProjectContainer(const QString &room,
                            const QString &container) const
    {
        return projectRooms.value(room).toObject().contains(container);
    }
};

bool loadGameDefinitions(const QString &requestedPath, LootData *data,
                         QString *error)
{
    const QString root = normalizedItemsRoot(requestedPath);
    if (root.isEmpty()) {
        if (error) {
            *error = QObject::tr(
                        "Select the Project Zomboid game directory or its "
                        "media/lua/server/Items directory.");
        }
        return false;
    }

    lua_State *state = luaL_newstate();
    if (!state) {
        if (error)
            *error = QObject::tr("Could not create the isolated Lua state.");
        return false;
    }
    luaL_openlibs(state);
    lua_sethook(state, lootInstructionLimit, LUA_MASKCOUNT, 1000000);

    // These files are trusted game definitions, but the editor still removes
    // filesystem/process/network entry points before executing them.
    removeLuaGlobal(state, "dofile");
    removeLuaGlobal(state, "loadfile");
    removeLuaGlobal(state, "load");
    removeLuaGlobal(state, "require");
    removeLuaGlobal(state, "io");
    removeLuaGlobal(state, "os");
    removeLuaGlobal(state, "package");
    removeLuaGlobal(state, "debug");

    if (luaL_dostring(state,
                      "Distributions = {}\n"
                      "ClutterTables = {}\n"
                      "ProceduralDistributions = { list = {} }\n")
            != LUA_OK) {
        if (error)
            *error = luaString(state, -1);
        lua_close(state);
        return false;
    }

    QStringList supportFiles;
    QDirIterator iterator(root,
                          QStringList() << QStringLiteral("Distribution_*.lua"),
                          QDir::Files);
    while (iterator.hasNext())
        supportFiles.append(QDir::cleanPath(iterator.next()));
    std::sort(supportFiles.begin(), supportFiles.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const QString &fileName : supportFiles) {
        if (!executeLuaFile(state, fileName, error)) {
            lua_close(state);
            return false;
        }
    }

    if (!executeLuaFile(
                state,
                QDir(root).filePath(QStringLiteral("Distributions.lua")),
                error)
            || !executeLuaFile(
                state,
                QDir(root).filePath(
                    QStringLiteral("ProceduralDistributions.lua")),
                error)) {
        lua_close(state);
        return false;
    }

    QJsonObject rooms = readLuaGlobalObject(state, "SuburbsDistributions");
    if (rooms.isEmpty()) {
        lua_getglobal(state, "Distributions");
        if (lua_istable(state, -1)) {
            lua_rawgeti(state, -1, 1);
            if (lua_istable(state, -1))
                rooms = luaToJson(state, -1, 0).toObject();
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }

    lua_getglobal(state, "ProceduralDistributions");
    lua_getfield(state, -1, "list");
    QJsonObject procedures;
    if (lua_istable(state, -1))
        procedures = luaToJson(state, -1, 0).toObject();
    lua_pop(state, 2);
    lua_close(state);

    if (rooms.isEmpty() || procedures.isEmpty()) {
        if (error) {
            *error = QObject::tr(
                        "The game loot registries were loaded but no room or "
                        "procedural definitions were found.");
        }
        return false;
    }

    data->itemsRoot = root;
    data->gameRooms = rooms;
    data->gameProcedures = procedures;
    return true;
}

QString manifestFileName(const QString &projectRoot)
{
    return QDir(projectRoot).filePath(
                QString::fromLatin1(kManifestRelativePath));
}

QString luaFileName(const QString &projectRoot)
{
    return QDir(projectRoot).filePath(QString::fromLatin1(kLuaRelativePath));
}

QString gameOwnerRoot(const QString &itemsRoot)
{
    QDir directory(itemsRoot);
    for (int i = 0; i < 3; ++i) {
        if (!directory.cdUp())
            return QString();
    }
    if (directory.dirName().compare(
                QStringLiteral("media"), Qt::CaseInsensitive) == 0
            && !directory.cdUp()) {
        return QString();
    }
    return QDir::cleanPath(directory.absolutePath());
}

bool sameOrInside(const QString &candidate, const QString &parent)
{
    QString child = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
    QString root = QDir::cleanPath(QFileInfo(parent).absoluteFilePath());
#ifdef Q_OS_WIN
    child = child.toLower();
    root = root.toLower();
#endif
    return child == root
            || child.startsWith(root + QDir::separator());
}

QString inferredProjectRoot(const QString &suggestedPath)
{
    QDir directory(QDir::cleanPath(
                       QDir::fromNativeSeparators(suggestedPath)));
    if (!directory.exists())
        return suggestedPath;

    const QString original = directory.absolutePath();
    while (true) {
        if (directory.dirName().compare(
                    QStringLiteral("media"), Qt::CaseInsensitive) == 0) {
            if (directory.cdUp())
                return directory.absolutePath();
            break;
        }
        if (QFileInfo(directory.filePath(
                          QStringLiteral("media"))).isDir()) {
            return directory.absolutePath();
        }
        const QString before = directory.absolutePath();
        if (!directory.cdUp()
                || directory.absolutePath() == before) {
            break;
        }
    }
    return original;
}

bool loadProjectDefinitions(const QString &projectRoot, LootData *data,
                            QString *error)
{
    data->projectRoot =
            QDir::cleanPath(QDir::fromNativeSeparators(projectRoot.trimmed()));
    data->projectRooms = QJsonObject();
    data->projectProcedures = QJsonObject();
    if (data->projectRoot.isEmpty())
        return true;

    const QString fileName = manifestFileName(data->projectRoot);
    if (!QFileInfo(fileName).exists())
        return true;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Could not read %1: %2")
                    .arg(QDir::toNativeSeparators(fileName),
                         file.errorString());
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
        if (error) {
            *error = QObject::tr("Invalid loot-editor manifest %1: %2")
                    .arg(QDir::toNativeSeparators(fileName),
                         parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 1) {
        if (error) {
            *error = QObject::tr(
                        "Unsupported loot-editor manifest schema in %1.")
                    .arg(QDir::toNativeSeparators(fileName));
        }
        return false;
    }
    data->projectRooms = root.value(QStringLiteral("rooms")).toObject();
    data->projectProcedures =
            root.value(QStringLiteral("procedural")).toObject();
    return true;
}

int unresolvedReferenceCount(const LootData &data)
{
    int count = 0;
    const QJsonObject procedures = data.procedures();
    const QJsonObject rooms = data.rooms();
    for (auto roomIt = rooms.begin(); roomIt != rooms.end(); ++roomIt) {
        const QJsonObject room = roomIt.value().toObject();
        for (auto containerIt = room.begin();
             containerIt != room.end(); ++containerIt) {
            const QJsonObject definition = containerIt.value().toObject();
            if (!definition.value(
                    QStringLiteral("procedural")).toBool()
                    && !definition.contains(QStringLiteral("procList"))) {
                continue;
            }
            const QJsonArray rules =
                    definition.value(QStringLiteral("procList")).toArray();
            for (const QJsonValue &ruleValue : rules) {
                const QString name = ruleValue.toObject()
                        .value(QStringLiteral("name")).toString();
                if (name.isEmpty() || !procedures.contains(name))
                    ++count;
            }
        }
    }
    return count;
}

QString luaQuoted(const QString &value)
{
    QString result = value;
    result.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    result.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    result.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    result.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    result.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return QStringLiteral("\"%1\"").arg(result);
}

QString jsonToLua(const QJsonValue &value, int indent = 0)
{
    const QString prefix(indent, QLatin1Char(' '));
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    if (value.isString())
        return luaQuoted(value.toString());
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty())
            return QStringLiteral("{}");
        QString result = QStringLiteral("{\n");
        for (const QJsonValue &entry : array) {
            result += prefix + QStringLiteral("    ")
                    + jsonToLua(entry, indent + 4) + QStringLiteral(",\n");
        }
        result += prefix + QLatin1Char('}');
        return result;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty())
            return QStringLiteral("{}");
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(),
                  [](const QString &left, const QString &right) {
            return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        QString result = QStringLiteral("{\n");
        for (const QString &key : keys) {
            result += prefix + QStringLiteral("    [")
                    + luaQuoted(key) + QStringLiteral("] = ")
                    + jsonToLua(object.value(key), indent + 4)
                    + QStringLiteral(",\n");
        }
        result += prefix + QLatin1Char('}');
        return result;
    }
    return QStringLiteral("nil");
}

QString generatedLua(const LootData &data)
{
    QString output = QStringLiteral(
                "-- Generated by TileZed / BuildingEd Procedural Loot Editor.\n"
                "-- Do not edit this file by hand; edit PZToolsLootEditor.json "
                "through the tool.\n\n"
                "local function PZTools_ApplyLootDefinitions()\n"
                "    ProceduralDistributions = ProceduralDistributions or {}\n"
                "    ProceduralDistributions.list = "
                "ProceduralDistributions.list or {}\n"
                "    SuburbsDistributions = SuburbsDistributions or {}\n\n");

    QStringList procedureNames = data.projectProcedures.keys();
    std::sort(procedureNames.begin(), procedureNames.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const QString &name : procedureNames) {
        output += QStringLiteral(
                    "    ProceduralDistributions.list[%1] = %2\n\n")
                .arg(luaQuoted(name),
                     jsonToLua(data.projectProcedures.value(name), 4));
    }

    QStringList roomNames = data.projectRooms.keys();
    std::sort(roomNames.begin(), roomNames.end(),
              [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const QString &roomName : roomNames) {
        output += QStringLiteral(
                    "    SuburbsDistributions[%1] = "
                    "SuburbsDistributions[%1] or {}\n")
                .arg(luaQuoted(roomName));
        const QJsonObject room = data.projectRooms.value(roomName).toObject();
        QStringList containerNames = room.keys();
        std::sort(containerNames.begin(), containerNames.end(),
                  [](const QString &left, const QString &right) {
            return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        for (const QString &containerName : containerNames) {
            output += QStringLiteral(
                        "    SuburbsDistributions[%1][%2] = %3\n")
                    .arg(luaQuoted(roomName), luaQuoted(containerName),
                         jsonToLua(room.value(containerName), 4));
        }
        output += QLatin1Char('\n');
    }

    output += QStringLiteral(
                "end\n\n"
                "-- ItemPickerJava parses the registries after this event.\n"
                "Events.OnPostDistributionMerge.Add("
                "PZTools_ApplyLootDefinitions)\n");
    return output;
}

bool validateGeneratedLua(const QString &lua, QString *error)
{
    lua_State *state = luaL_newstate();
    if (!state) {
        if (error)
            *error = QObject::tr("Could not create the Lua syntax checker.");
        return false;
    }
    const QByteArray bytes = lua.toUtf8();
    const int status = luaL_loadbuffer(
                state, bytes.constData(), size_t(bytes.size()),
                "PZToolsLootDefinitions.lua");
    if (status != LUA_OK && error) {
        *error = QObject::tr("Generated Lua is invalid: %1")
                .arg(luaString(state, -1));
    }
    lua_close(state);
    return status == LUA_OK;
}

bool saveProjectDefinitions(const LootData &data, QString *error)
{
    if (data.projectRoot.isEmpty()) {
        if (error)
            *error = QObject::tr("Select a project/mod root first.");
        return false;
    }
    const QString gameRoot = gameOwnerRoot(data.itemsRoot);
    if (!gameRoot.isEmpty()
            && sameOrInside(data.projectRoot, gameRoot)) {
        if (error) {
            *error = QObject::tr(
                        "The project/mod root must be outside the "
                        "Project Zomboid game directory. Game files remain "
                        "read-only.");
        }
        return false;
    }

    const QString manifest = manifestFileName(data.projectRoot);
    const QString lua = luaFileName(data.projectRoot);
    if (!QDir().mkpath(QFileInfo(manifest).absolutePath())) {
        if (error) {
            *error = QObject::tr("Could not create %1.")
                    .arg(QDir::toNativeSeparators(
                             QFileInfo(manifest).absolutePath()));
        }
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("generator"),
                QStringLiteral("PZTools Procedural Loot Editor"));
    root.insert(QStringLiteral("rooms"), data.projectRooms);
    root.insert(QStringLiteral("procedural"), data.projectProcedures);

    QSaveFile manifestFile(manifest);
    if (!manifestFile.open(QIODevice::WriteOnly)
            || manifestFile.write(
                QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
            || !manifestFile.commit()) {
        if (error) {
            *error = QObject::tr("Could not save %1: %2")
                    .arg(QDir::toNativeSeparators(manifest),
                         manifestFile.errorString());
        }
        return false;
    }

    const QByteArray luaBytes = generatedLua(data).toUtf8();
    QString syntaxError;
    if (!validateGeneratedLua(QString::fromUtf8(luaBytes), &syntaxError)) {
        if (error)
            *error = syntaxError;
        return false;
    }
    QSaveFile luaFile(lua);
    if (!luaFile.open(QIODevice::WriteOnly)
            || luaFile.write(luaBytes) != luaBytes.size()
            || !luaFile.commit()) {
        if (error) {
            *error = QObject::tr("Could not save %1: %2")
                    .arg(QDir::toNativeSeparators(lua),
                         luaFile.errorString());
        }
        return false;
    }
    return true;
}

double rawChance(const QJsonValue &value)
{
    return value.isDouble() ? value.toDouble() : 0.0;
}

QString formattedNumber(double number)
{
    return QString::number(number, 'g', 6);
}

QString cumulativeChance(double chance, double rolls)
{
    const double p = qBound(0.0, chance / 100.0, 1.0);
    const double effectiveRolls = std::floor(qMax(0.0, rolls));
    const double result = (1.0 - std::pow(1.0 - p, effectiveRolls)) * 100.0;
    return QStringLiteral("%1%").arg(QString::number(result, 'f',
                                                     result < 1.0 ? 3 : 1));
}

QJsonArray itemArrayFromTable(QTableWidget *table)
{
    QJsonArray result;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString name = table->item(row, 0)
                ? table->item(row, 0)->text().trimmed() : QString();
        if (name.isEmpty())
            continue;
        bool ok = false;
        const double chance = table->item(row, 1)
                ? table->item(row, 1)->text().toDouble(&ok) : 0.0;
        result.append(name);
        result.append(ok ? chance : 0.0);
    }
    return result;
}

bool validateItemEditor(QTableWidget *table, QString *error)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString name = table->item(row, 0)
                ? table->item(row, 0)->text().trimmed() : QString();
        const QString chanceText = table->item(row, 1)
                ? table->item(row, 1)->text().trimmed() : QString();
        if (name.isEmpty() && chanceText.isEmpty())
            continue;
        if (name.isEmpty()) {
            if (error) {
                *error = QObject::tr(
                            "Item row %1 has a chance but no item name.")
                        .arg(row + 1);
            }
            return false;
        }
        bool ok = false;
        const double chance = chanceText.toDouble(&ok);
        if (!ok || !std::isfinite(chance) || chance < 0.0) {
            if (error) {
                *error = QObject::tr(
                            "Item row %1 needs a non-negative numeric "
                            "chance per roll.")
                        .arg(row + 1);
            }
            return false;
        }
    }
    return true;
}

void populateItemEditor(QTableWidget *table, const QJsonArray &items)
{
    table->setRowCount(0);
    for (int i = 0; i + 1 < items.size(); i += 2) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0,
                       new QTableWidgetItem(items.at(i).toString()));
        table->setItem(row, 1,
                       new QTableWidgetItem(
                           formattedNumber(items.at(i + 1).toDouble())));
    }
}

void appendEditableRow(QTableWidget *table, int columns)
{
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < columns; ++column)
        table->setItem(row, column, new QTableWidgetItem);
    table->setCurrentCell(row, 0);
    table->editItem(table->item(row, 0));
}

void removeSelectedRows(QTableWidget *table)
{
    QSet<int> rows;
    for (const QModelIndex &index :
         table->selectionModel()->selectedRows()) {
        rows.insert(index.row());
    }
    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted)
        table->removeRow(row);
}

QWidget *tableButtons(QTableWidget *table, int columnCount, QWidget *parent)
{
    QWidget *widget = new QWidget(parent);
    QHBoxLayout *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    QToolButton *add = new QToolButton(widget);
    add->setText(QStringLiteral("+"));
    add->setToolTip(QObject::tr("Add row"));
    QToolButton *remove = new QToolButton(widget);
    remove->setText(QStringLiteral("-"));
    remove->setToolTip(QObject::tr("Remove selected rows"));
    layout->addWidget(add);
    layout->addWidget(remove);
    layout->addStretch();
    QObject::connect(add, &QAbstractButton::clicked, table,
                     [table, columnCount]() {
        appendEditableRow(table, columnCount);
    });
    QObject::connect(remove, &QAbstractButton::clicked, table,
                     [table]() {
        removeSelectedRows(table);
    });
    return widget;
}

QTableWidget *newItemEditor(QWidget *parent)
{
    QTableWidget *table = new QTableWidget(parent);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(
                QStringList() << QObject::tr("Item")
                              << QObject::tr("Chance / roll"));
    table->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::ResizeToContents);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setMinimumHeight(145);
    return table;
}

class ProcedureEditorDialog : public QDialog
{
public:
    ProcedureEditorDialog(const QString &name,
                          const QJsonObject &definition,
                          bool allowRename,
                          QWidget *parent)
        : QDialog(parent)
        , mOriginal(definition)
    {
        setWindowTitle(name.isEmpty()
                       ? tr("New procedural distribution")
                       : tr("Edit procedural distribution"));
        resize(680, 650);
        QVBoxLayout *layout = new QVBoxLayout(this);
        QFormLayout *form = new QFormLayout;
        mName = new QLineEdit(name, this);
        mName->setReadOnly(!allowRename);
        mRolls = new QDoubleSpinBox(this);
        mRolls->setRange(0.0, 1000.0);
        mRolls->setDecimals(3);
        mRolls->setValue(definition.value(
                             QStringLiteral("rolls")).toDouble(1.0));
        form->addRow(tr("Name"), mName);
        form->addRow(tr("Rolls"), mRolls);
        layout->addLayout(form);

        QGroupBox *itemsGroup = new QGroupBox(
                    tr("Items - each chance is tested on every roll"), this);
        QVBoxLayout *itemsLayout = new QVBoxLayout(itemsGroup);
        mItems = newItemEditor(itemsGroup);
        populateItemEditor(
                    mItems,
                    definition.value(QStringLiteral("items")).toArray());
        itemsLayout->addWidget(mItems);
        itemsLayout->addWidget(tableButtons(mItems, 2, itemsGroup));
        layout->addWidget(itemsGroup, 1);

        QGroupBox *junkGroup = new QGroupBox(
                    tr("Junk - sandbox loot rarity does not reduce it"), this);
        QVBoxLayout *junkLayout = new QVBoxLayout(junkGroup);
        mJunkRolls = new QDoubleSpinBox(junkGroup);
        mJunkRolls->setRange(0.0, 1000.0);
        mJunkRolls->setDecimals(3);
        const QJsonObject junk =
                definition.value(QStringLiteral("junk")).toObject();
        mJunkRolls->setValue(
                    junk.value(QStringLiteral("rolls")).toDouble(0.0));
        QFormLayout *junkForm = new QFormLayout;
        junkForm->addRow(tr("Junk rolls"), mJunkRolls);
        junkLayout->addLayout(junkForm);
        mJunk = newItemEditor(junkGroup);
        populateItemEditor(
                    mJunk, junk.value(QStringLiteral("items")).toArray());
        junkLayout->addWidget(mJunk);
        junkLayout->addWidget(tableButtons(mJunk, 2, junkGroup));
        layout->addWidget(junkGroup, 1);

        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this,
                [this]() {
            if (mName->text().trimmed().isEmpty()) {
                QMessageBox::warning(
                            this, tr("Missing name"),
                            tr("Enter a procedural-distribution name."));
                return;
            }
            QString error;
            if (!validateItemEditor(mItems, &error)
                    || !validateItemEditor(mJunk, &error)) {
                QMessageBox::warning(
                            this, tr("Invalid item chance"), error);
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    QString name() const
    {
        return mName->text().trimmed();
    }

    QJsonObject definition() const
    {
        QJsonObject result = mOriginal;
        result.insert(QStringLiteral("rolls"), mRolls->value());
        result.insert(QStringLiteral("items"), itemArrayFromTable(mItems));
        if (mJunkRolls->value() > 0.0 || mJunk->rowCount() > 0) {
            QJsonObject junk;
            junk.insert(QStringLiteral("rolls"), mJunkRolls->value());
            junk.insert(QStringLiteral("items"), itemArrayFromTable(mJunk));
            result.insert(QStringLiteral("junk"), junk);
        } else
            result.remove(QStringLiteral("junk"));
        return result;
    }

private:
    QJsonObject mOriginal;
    QLineEdit *mName;
    QDoubleSpinBox *mRolls;
    QTableWidget *mItems;
    QDoubleSpinBox *mJunkRolls;
    QTableWidget *mJunk;
};

class ContainerEditorDialog : public QDialog
{
public:
    ContainerEditorDialog(const QString &room,
                          const QString &container,
                          const QJsonObject &definition,
                          const QStringList &procedures,
                          bool allowRename,
                          QWidget *parent)
        : QDialog(parent)
        , mOriginal(definition)
        , mProcedures(procedures)
    {
        setWindowTitle(container.isEmpty()
                       ? tr("New room/container distribution")
                       : tr("Edit room/container distribution"));
        resize(940, 690);
        QVBoxLayout *layout = new QVBoxLayout(this);
        QFormLayout *form = new QFormLayout;
        mRoom = new QLineEdit(room, this);
        mContainer = new QLineEdit(container, this);
        mRoom->setReadOnly(!allowRename);
        mContainer->setReadOnly(!allowRename);
        mMode = new QComboBox(this);
        mMode->addItem(tr("Procedural"), QStringLiteral("procedural"));
        mMode->addItem(tr("Direct item rolls"), QStringLiteral("direct"));
        form->addRow(tr("RoomDef name"), mRoom);
        form->addRow(tr("Container type"), mContainer);
        form->addRow(tr("Mode"), mMode);
        layout->addLayout(form);

        mStack = new QStackedWidget(this);
        layout->addWidget(mStack, 1);

        QWidget *proceduralPage = new QWidget(mStack);
        QVBoxLayout *procLayout = new QVBoxLayout(proceduralPage);
        QLabel *procHelp = new QLabel(
                    tr("Weight is relative among eligible rows. min/max count "
                       "containers already assigned in the RoomDef. A force* "
                       "selector takes priority when it matches."), proceduralPage);
        procHelp->setWordWrap(true);
        procLayout->addWidget(procHelp);
        mProcList = new QTableWidget(proceduralPage);
        mProcList->setColumnCount(9);
        mProcList->setHorizontalHeaderLabels(
                    QStringList()
                    << tr("Distribution") << tr("Weight")
                    << tr("Min") << tr("Max")
                    << tr("Force items") << tr("Force zones")
                    << tr("Force tiles") << tr("Force rooms")
                    << tr("Editor notes"));
        mProcList->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::ResizeToContents);
        for (int i = 4; i < 9; ++i)
            mProcList->horizontalHeader()->setSectionResizeMode(
                        i, QHeaderView::Stretch);
        mProcList->setSelectionBehavior(QAbstractItemView::SelectRows);
        procLayout->addWidget(mProcList);
        QWidget *procButtons = new QWidget(proceduralPage);
        QHBoxLayout *procButtonsLayout = new QHBoxLayout(procButtons);
        procButtonsLayout->setContentsMargins(0, 0, 0, 0);
        QToolButton *addRule = new QToolButton(procButtons);
        addRule->setText(QStringLiteral("+"));
        QToolButton *removeRule = new QToolButton(procButtons);
        removeRule->setText(QStringLiteral("-"));
        procButtonsLayout->addWidget(addRule);
        procButtonsLayout->addWidget(removeRule);
        procButtonsLayout->addStretch();
        connect(addRule, &QAbstractButton::clicked, this,
                [this]() {
            bool ok = false;
            const QString name = QInputDialog::getItem(
                        this, tr("Add procedural rule"),
                        tr("Distribution"), mProcedures, 0, true, &ok);
            if (!ok || name.trimmed().isEmpty())
                return;
            const int row = mProcList->rowCount();
            mProcList->insertRow(row);
            for (int column = 0; column < mProcList->columnCount(); ++column)
                mProcList->setItem(row, column, new QTableWidgetItem);
            mProcList->item(row, 0)->setText(name.trimmed());
            mProcList->item(row, 1)->setText(QStringLiteral("1"));
            mProcList->item(row, 2)->setText(QStringLiteral("0"));
            mProcList->item(row, 3)->setText(QStringLiteral("99"));
        });
        connect(removeRule, &QAbstractButton::clicked, this,
                [this]() { removeSelectedRows(mProcList); });
        procLayout->addWidget(procButtons);
        mStack->addWidget(proceduralPage);

        QWidget *directPage = new QWidget(mStack);
        QVBoxLayout *directLayout = new QVBoxLayout(directPage);
        QFormLayout *directForm = new QFormLayout;
        mRolls = new QDoubleSpinBox(directPage);
        mRolls->setRange(0.0, 1000.0);
        mRolls->setDecimals(3);
        mRolls->setValue(
                    definition.value(QStringLiteral("rolls")).toDouble(1.0));
        mFillRand = new QDoubleSpinBox(directPage);
        mFillRand->setRange(0.0, 100000.0);
        mFillRand->setDecimals(3);
        mFillRand->setValue(
                    definition.value(QStringLiteral("fillRand")).toDouble());
        mIgnoreDensity = new QCheckBox(
                    tr("Ignore zombie-density bonus"), directPage);
        mIgnoreDensity->setChecked(
                    definition.value(
                        QStringLiteral("ignoreZombieDensity")).toBool());
        mTrash = new QCheckBox(tr("Trash container"), directPage);
        mTrash->setChecked(
                    definition.value(QStringLiteral("isTrash")).toBool());
        directForm->addRow(tr("Rolls"), mRolls);
        directForm->addRow(tr("fillRand (0 = unset)"), mFillRand);
        directForm->addRow(QString(), mIgnoreDensity);
        directForm->addRow(QString(), mTrash);
        directLayout->addLayout(directForm);

        QGroupBox *itemsGroup = new QGroupBox(tr("Items"), directPage);
        QVBoxLayout *itemsLayout = new QVBoxLayout(itemsGroup);
        mItems = newItemEditor(itemsGroup);
        populateItemEditor(
                    mItems,
                    definition.value(QStringLiteral("items")).toArray());
        itemsLayout->addWidget(mItems);
        itemsLayout->addWidget(tableButtons(mItems, 2, itemsGroup));
        directLayout->addWidget(itemsGroup, 1);

        QGroupBox *junkGroup = new QGroupBox(tr("Junk"), directPage);
        QVBoxLayout *junkLayout = new QVBoxLayout(junkGroup);
        mJunkRolls = new QDoubleSpinBox(junkGroup);
        mJunkRolls->setRange(0.0, 1000.0);
        mJunkRolls->setDecimals(3);
        const QJsonObject junk =
                definition.value(QStringLiteral("junk")).toObject();
        mJunkRolls->setValue(
                    junk.value(QStringLiteral("rolls")).toDouble());
        QFormLayout *junkForm = new QFormLayout;
        junkForm->addRow(tr("Junk rolls"), mJunkRolls);
        junkLayout->addLayout(junkForm);
        mJunk = newItemEditor(junkGroup);
        populateItemEditor(
                    mJunk, junk.value(QStringLiteral("items")).toArray());
        junkLayout->addWidget(mJunk);
        junkLayout->addWidget(tableButtons(mJunk, 2, junkGroup));
        directLayout->addWidget(junkGroup, 1);
        mStack->addWidget(directPage);

        const bool procedural =
                definition.value(QStringLiteral("procedural")).toBool()
                || definition.contains(QStringLiteral("procList"));
        mMode->setCurrentIndex(procedural ? 0 : 1);
        mStack->setCurrentIndex(mMode->currentIndex());
        connect(mMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                mStack, &QStackedWidget::setCurrentIndex);
        populateRules(
                    definition.value(QStringLiteral("procList")).toArray());

        QDialogButtonBox *buttons = new QDialogButtonBox(
                    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this,
                [this]() {
            if (mRoom->text().trimmed().isEmpty()
                    || mContainer->text().trimmed().isEmpty()) {
                QMessageBox::warning(
                            this, tr("Missing identity"),
                            tr("Enter both a RoomDef name and a container type."));
                return;
            }
            if (mMode->currentIndex() == 0 && mProcList->rowCount() == 0) {
                QMessageBox::warning(
                            this, tr("Empty procedural mapping"),
                            tr("Add at least one procedural-distribution rule."));
                return;
            }
            if (mMode->currentIndex() == 0) {
                for (int row = 0; row < mProcList->rowCount(); ++row) {
                    if (cellText(row, 0).isEmpty()) {
                        QMessageBox::warning(
                                    this, tr("Invalid procedural rule"),
                                    tr("Procedural rule %1 needs a "
                                       "distribution name.").arg(row + 1));
                        return;
                    }
                    if (!mProcedures.contains(cellText(row, 0))) {
                        QMessageBox::warning(
                                    this, tr("Missing distribution"),
                                    tr("Rule %1 references \"%2\", which is "
                                       "not present in the effective "
                                       "procedural-distribution registry.")
                                    .arg(row + 1)
                                    .arg(cellText(row, 0)));
                        return;
                    }
                    const int numericColumns[] = {1, 2, 3};
                    for (int column : numericColumns) {
                        const QString text = cellText(row, column);
                        if (text.isEmpty())
                            continue;
                        bool ok = false;
                        text.toInt(&ok);
                        if (!ok) {
                            QMessageBox::warning(
                                        this, tr("Invalid procedural rule"),
                                        tr("Rule %1 has a non-integer "
                                           "weight, min, or max value.")
                                        .arg(row + 1));
                            return;
                        }
                    }
                }
            } else {
                QString error;
                if (!validateItemEditor(mItems, &error)
                        || !validateItemEditor(mJunk, &error)) {
                    QMessageBox::warning(
                                this, tr("Invalid item chance"), error);
                    return;
                }
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    QString room() const { return mRoom->text().trimmed(); }
    QString container() const { return mContainer->text().trimmed(); }

    QJsonObject definition() const
    {
        QJsonObject result = mOriginal;
        if (mMode->currentIndex() == 0) {
            result.remove(QStringLiteral("rolls"));
            result.remove(QStringLiteral("items"));
            result.remove(QStringLiteral("junk"));
            result.insert(QStringLiteral("procedural"), true);
            QJsonArray rules;
            for (int row = 0; row < mProcList->rowCount(); ++row) {
                QJsonObject rule;
                if (QTableWidgetItem *identity = mProcList->item(row, 0))
                    rule = identity->data(Qt::UserRole).toJsonObject();
                const QStringList keys = {
                    QStringLiteral("name"),
                    QStringLiteral("weightChance"),
                    QStringLiteral("min"),
                    QStringLiteral("max"),
                    QStringLiteral("forceForItems"),
                    QStringLiteral("forceForZones"),
                    QStringLiteral("forceForTiles"),
                    QStringLiteral("forceForRooms"),
                    QStringLiteral("notes")
                };
                for (const QString &key : keys)
                    rule.remove(key);
                const QString name = cellText(row, 0);
                if (name.isEmpty())
                    continue;
                rule.insert(QStringLiteral("name"), name);
                bool ok = false;
                const int weight = cellText(row, 1).toInt(&ok);
                if (ok)
                    rule.insert(QStringLiteral("weightChance"), weight);
                const int minimum = cellText(row, 2).toInt(&ok);
                if (ok)
                    rule.insert(QStringLiteral("min"), minimum);
                const int maximum = cellText(row, 3).toInt(&ok);
                if (ok)
                    rule.insert(QStringLiteral("max"), maximum);
                const QStringList textKeys = {
                    QStringLiteral("forceForItems"),
                    QStringLiteral("forceForZones"),
                    QStringLiteral("forceForTiles"),
                    QStringLiteral("forceForRooms"),
                    QStringLiteral("notes")
                };
                for (int column = 4; column < 9; ++column) {
                    const QString value = cellText(row, column);
                    if (!value.isEmpty())
                        rule.insert(textKeys.at(column - 4), value);
                }
                rules.append(rule);
            }
            result.insert(QStringLiteral("procList"), rules);
        } else {
            result.remove(QStringLiteral("procedural"));
            result.remove(QStringLiteral("procList"));
            result.insert(QStringLiteral("rolls"), mRolls->value());
            result.insert(QStringLiteral("items"),
                          itemArrayFromTable(mItems));
            if (mFillRand->value() > 0.0)
                result.insert(QStringLiteral("fillRand"),
                              mFillRand->value());
            else
                result.remove(QStringLiteral("fillRand"));
            if (mIgnoreDensity->isChecked())
                result.insert(QStringLiteral("ignoreZombieDensity"), true);
            else
                result.remove(QStringLiteral("ignoreZombieDensity"));
            if (mTrash->isChecked())
                result.insert(QStringLiteral("isTrash"), true);
            else
                result.remove(QStringLiteral("isTrash"));
            if (mJunkRolls->value() > 0.0 || mJunk->rowCount() > 0) {
                QJsonObject junk;
                junk.insert(QStringLiteral("rolls"), mJunkRolls->value());
                junk.insert(QStringLiteral("items"),
                            itemArrayFromTable(mJunk));
                result.insert(QStringLiteral("junk"), junk);
            } else
                result.remove(QStringLiteral("junk"));
        }
        return result;
    }

private:
    QString cellText(int row, int column) const
    {
        QTableWidgetItem *item = mProcList->item(row, column);
        return item ? item->text().trimmed() : QString();
    }

    void populateRules(const QJsonArray &rules)
    {
        for (const QJsonValue &value : rules) {
            const QJsonObject rule = value.toObject();
            const int row = mProcList->rowCount();
            mProcList->insertRow(row);
            const QStringList values = {
                rule.value(QStringLiteral("name")).toString(),
                formattedNumber(
                    rule.value(QStringLiteral("weightChance")).toDouble(1.0)),
                rule.contains(QStringLiteral("min"))
                    ? QString::number(
                        rule.value(QStringLiteral("min")).toInt())
                    : QString(),
                rule.contains(QStringLiteral("max"))
                    ? QString::number(
                        rule.value(QStringLiteral("max")).toInt())
                    : QString(),
                rule.value(QStringLiteral("forceForItems")).toString(),
                rule.value(QStringLiteral("forceForZones")).toString(),
                rule.value(QStringLiteral("forceForTiles")).toString(),
                rule.value(QStringLiteral("forceForRooms")).toString(),
                rule.value(QStringLiteral("notes")).toString()
            };
            for (int column = 0; column < values.size(); ++column) {
                mProcList->setItem(
                            row, column,
                            new QTableWidgetItem(values.at(column)));
            }
            mProcList->item(row, 0)->setData(Qt::UserRole, rule);
        }
    }

    QJsonObject mOriginal;
    QStringList mProcedures;
    QLineEdit *mRoom;
    QLineEdit *mContainer;
    QComboBox *mMode;
    QStackedWidget *mStack;
    QTableWidget *mProcList;
    QDoubleSpinBox *mRolls;
    QDoubleSpinBox *mFillRand;
    QCheckBox *mIgnoreDensity;
    QCheckBox *mTrash;
    QTableWidget *mItems;
    QDoubleSpinBox *mJunkRolls;
    QTableWidget *mJunk;
};

enum TreeRoles {
    RoleRoom = Qt::UserRole,
    RoleContainer,
    RoleProject
};

} // namespace

class LootDistributionDialogPrivate
{
public:
    LootDistributionDialogPrivate(LootDistributionDialog *dialog,
                                  const QString &initialRoom,
                                  const QString &initialContainer,
                                  const QString &suggestedProjectRoot)
        : q(dialog)
        , requestedRoom(initialRoom)
        , requestedContainer(initialContainer)
    {
        buildUi();

        QSettings *settings = Preferences::instance()->settings();
        QString gamePath = settings->value(
                    QStringLiteral("LootDistributionEditor/GamePath"))
                .toString();
        QString projectPath = settings->value(
                    QStringLiteral("LootDistributionEditor/ProjectRoot"))
                .toString();
        if (!suggestedProjectRoot.isEmpty())
            projectPath = inferredProjectRoot(suggestedProjectRoot);
        gameEdit->setText(QDir::toNativeSeparators(gamePath));
        projectEdit->setText(QDir::toNativeSeparators(projectPath));
        if (!gamePath.isEmpty())
            reload(false);
        else
            setStatus(q->tr("Select the read-only game definitions."));
    }

    void buildUi()
    {
        q->setWindowTitle(q->tr("Procedural Loot Viewer / Editor"));
        q->resize(1180, 780);
        QVBoxLayout *layout = new QVBoxLayout(q);

        QGroupBox *paths = new QGroupBox(q->tr("Definition sources"), q);
        QFormLayout *pathLayout = new QFormLayout(paths);
        QWidget *gameRow = new QWidget(paths);
        QHBoxLayout *gameRowLayout = new QHBoxLayout(gameRow);
        gameRowLayout->setContentsMargins(0, 0, 0, 0);
        gameEdit = new QLineEdit(gameRow);
        gameEdit->setPlaceholderText(
                    q->tr("Project Zomboid game directory (read-only)"));
        QPushButton *gameBrowse = new QPushButton(q->tr("Browse..."), gameRow);
        gameRowLayout->addWidget(gameEdit, 1);
        gameRowLayout->addWidget(gameBrowse);
        pathLayout->addRow(q->tr("Game definitions (read-only)"), gameRow);

        QWidget *projectRow = new QWidget(paths);
        QHBoxLayout *projectRowLayout = new QHBoxLayout(projectRow);
        projectRowLayout->setContentsMargins(0, 0, 0, 0);
        projectEdit = new QLineEdit(projectRow);
        projectEdit->setPlaceholderText(
                    q->tr("Map project or mod root (editable)"));
        QPushButton *projectBrowse =
                new QPushButton(q->tr("Browse..."), projectRow);
        projectRowLayout->addWidget(projectEdit, 1);
        projectRowLayout->addWidget(projectBrowse);
        pathLayout->addRow(q->tr("Project / mod root (editable)"), projectRow);

        QWidget *pathButtons = new QWidget(paths);
        QHBoxLayout *pathButtonsLayout = new QHBoxLayout(pathButtons);
        pathButtonsLayout->setContentsMargins(0, 0, 0, 0);
        QPushButton *reloadButton = new QPushButton(q->tr("Reload"), pathButtons);
        openFolderButton = new QPushButton(
                    q->tr("Open generated location"), pathButtons);
        pathButtonsLayout->addWidget(reloadButton);
        pathButtonsLayout->addWidget(openFolderButton);
        pathButtonsLayout->addStretch();
        pathLayout->addRow(QString(), pathButtons);
        layout->addWidget(paths);

        tabs = new QTabWidget(q);
        layout->addWidget(tabs, 1);
        buildRoomTab();
        buildProcedureTab();

        status = new QLabel(q);
        status->setWordWrap(true);
        layout->addWidget(status);
        QDialogButtonBox *buttons =
                new QDialogButtonBox(QDialogButtonBox::Close, q);
        QObject::connect(buttons, &QDialogButtonBox::rejected,
                         q, &QDialog::reject);
        layout->addWidget(buttons);

        QObject::connect(gameBrowse, &QAbstractButton::clicked, q,
                         [this]() { chooseGame(); });
        QObject::connect(projectBrowse, &QAbstractButton::clicked, q,
                         [this]() { chooseProject(); });
        QObject::connect(reloadButton, &QAbstractButton::clicked, q,
                         [this]() { reload(true); });
        QObject::connect(openFolderButton, &QAbstractButton::clicked, q,
                         [this]() {
            const QString path = data.projectRoot.isEmpty()
                    ? projectEdit->text().trimmed()
                    : QFileInfo(luaFileName(data.projectRoot)).absolutePath();
            if (!path.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    }

    void buildRoomTab()
    {
        QWidget *page = new QWidget(tabs);
        QVBoxLayout *layout = new QVBoxLayout(page);
        QLineEdit *filter = new QLineEdit(page);
        filter->setPlaceholderText(
                    q->tr("Filter RoomDef or container type..."));
        layout->addWidget(filter);
        QSplitter *splitter = new QSplitter(page);
        roomTree = new QTreeWidget(splitter);
        roomTree->setHeaderLabels(
                    QStringList() << q->tr("Room / container")
                                  << q->tr("Source"));
        roomTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        roomTree->setMinimumWidth(330);
        splitter->addWidget(roomTree);

        QWidget *details = new QWidget(splitter);
        QVBoxLayout *detailsLayout = new QVBoxLayout(details);
        roomTitle = new QLabel(details);
        QFont titleFont = roomTitle->font();
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleFont.setBold(true);
        roomTitle->setFont(titleFont);
        detailsLayout->addWidget(roomTitle);
        roomHelp = new QLabel(details);
        roomHelp->setWordWrap(true);
        detailsLayout->addWidget(roomHelp);
        roomTable = new QTableWidget(details);
        roomTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        roomTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        detailsLayout->addWidget(roomTable, 1);
        splitter->addWidget(details);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);

        QWidget *buttons = new QWidget(page);
        QHBoxLayout *buttonLayout = new QHBoxLayout(buttons);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        newContainerButton = new QPushButton(
                    q->tr("New room/container..."), buttons);
        editContainerButton = new QPushButton(
                    q->tr("Clone / edit in project..."), buttons);
        deleteContainerButton = new QPushButton(
                    q->tr("Remove project override"), buttons);
        buttonLayout->addWidget(newContainerButton);
        buttonLayout->addWidget(editContainerButton);
        buttonLayout->addWidget(deleteContainerButton);
        buttonLayout->addStretch();
        layout->addWidget(buttons);
        tabs->addTab(page, q->tr("Rooms && containers"));

        QObject::connect(roomTree, &QTreeWidget::currentItemChanged, q,
                         [this]() { showCurrentContainer(); });
        QObject::connect(filter, &QLineEdit::textChanged, q,
                         [this](const QString &text) {
            const QString needle = text.trimmed();
            for (int i = 0; i < roomTree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *room = roomTree->topLevelItem(i);
                bool any = room->text(0).contains(
                            needle, Qt::CaseInsensitive);
                for (int child = 0; child < room->childCount(); ++child) {
                    QTreeWidgetItem *container = room->child(child);
                    const bool match = needle.isEmpty()
                            || room->text(0).contains(
                                needle, Qt::CaseInsensitive)
                            || container->text(0).contains(
                                needle, Qt::CaseInsensitive);
                    container->setHidden(!match);
                    any = any || match;
                }
                room->setHidden(!any);
            }
        });
        QObject::connect(newContainerButton, &QAbstractButton::clicked, q,
                         [this]() { editContainer(true); });
        QObject::connect(editContainerButton, &QAbstractButton::clicked, q,
                         [this]() { editContainer(false); });
        QObject::connect(deleteContainerButton, &QAbstractButton::clicked, q,
                         [this]() { deleteContainer(); });
    }

    void buildProcedureTab()
    {
        QWidget *page = new QWidget(tabs);
        QVBoxLayout *layout = new QVBoxLayout(page);
        QLineEdit *filter = new QLineEdit(page);
        filter->setPlaceholderText(
                    q->tr("Filter procedural distribution or item..."));
        layout->addWidget(filter);
        QSplitter *splitter = new QSplitter(page);
        procedureTree = new QTreeWidget(splitter);
        procedureTree->setHeaderLabels(
                    QStringList() << q->tr("Procedural distribution")
                                  << q->tr("Source"));
        procedureTree->header()->setSectionResizeMode(
                    0, QHeaderView::Stretch);
        procedureTree->setMinimumWidth(330);
        splitter->addWidget(procedureTree);

        QWidget *details = new QWidget(splitter);
        QVBoxLayout *detailsLayout = new QVBoxLayout(details);
        procedureTitle = new QLabel(details);
        QFont titleFont = procedureTitle->font();
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleFont.setBold(true);
        procedureTitle->setFont(titleFont);
        detailsLayout->addWidget(procedureTitle);
        procedureHelp = new QLabel(details);
        procedureHelp->setWordWrap(true);
        detailsLayout->addWidget(procedureHelp);
        procedureTable = new QTableWidget(details);
        procedureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        procedureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        detailsLayout->addWidget(procedureTable, 1);
        splitter->addWidget(details);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);

        QWidget *buttons = new QWidget(page);
        QHBoxLayout *buttonLayout = new QHBoxLayout(buttons);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        newProcedureButton = new QPushButton(
                    q->tr("New distribution..."), buttons);
        editProcedureButton = new QPushButton(
                    q->tr("Clone / edit in project..."), buttons);
        deleteProcedureButton = new QPushButton(
                    q->tr("Remove project override"), buttons);
        buttonLayout->addWidget(newProcedureButton);
        buttonLayout->addWidget(editProcedureButton);
        buttonLayout->addWidget(deleteProcedureButton);
        buttonLayout->addStretch();
        layout->addWidget(buttons);
        tabs->addTab(page, q->tr("Procedural distributions"));

        QObject::connect(procedureTree,
                         &QTreeWidget::currentItemChanged, q,
                         [this]() { showCurrentProcedure(); });
        QObject::connect(filter, &QLineEdit::textChanged, q,
                         [this](const QString &text) {
            const QString needle = text.trimmed();
            const QJsonObject procedures = data.procedures();
            for (int i = 0;
                 i < procedureTree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item =
                        procedureTree->topLevelItem(i);
                bool match = needle.isEmpty()
                        || item->text(0).contains(
                            needle, Qt::CaseInsensitive);
                if (!match) {
                    const QJsonObject definition =
                            procedures.value(item->text(0)).toObject();
                    const QJsonArray groups[] = {
                        definition.value(QStringLiteral("items")).toArray(),
                        definition.value(QStringLiteral("junk")).toObject()
                        .value(QStringLiteral("items")).toArray()
                    };
                    for (const QJsonArray &items : groups) {
                        for (int n = 0; n < items.size(); n += 2) {
                            if (items.at(n).toString().contains(
                                        needle, Qt::CaseInsensitive)) {
                                match = true;
                                break;
                            }
                        }
                        if (match)
                            break;
                    }
                }
                item->setHidden(!match);
            }
        });
        QObject::connect(newProcedureButton, &QAbstractButton::clicked, q,
                         [this]() { editProcedure(true); });
        QObject::connect(editProcedureButton, &QAbstractButton::clicked, q,
                         [this]() { editProcedure(false); });
        QObject::connect(deleteProcedureButton, &QAbstractButton::clicked, q,
                         [this]() { deleteProcedure(); });
    }

    void chooseGame()
    {
        const QString path = QFileDialog::getExistingDirectory(
                    q, q->tr("Choose Project Zomboid game directory"),
                    gameEdit->text());
        if (path.isEmpty())
            return;
        gameEdit->setText(QDir::toNativeSeparators(path));
        reload(true);
    }

    void chooseProject()
    {
        const QString path = QFileDialog::getExistingDirectory(
                    q, q->tr("Choose map project or mod root"),
                    projectEdit->text());
        if (path.isEmpty())
            return;
        projectEdit->setText(QDir::toNativeSeparators(path));
        reload(true);
    }

    bool ensureProjectRoot()
    {
        QString root = projectEdit->text().trimmed();
        if (root.isEmpty()) {
            root = QFileDialog::getExistingDirectory(
                        q, q->tr("Choose map project or mod root"));
            if (root.isEmpty())
                return false;
            projectEdit->setText(QDir::toNativeSeparators(root));
        }
        data.projectRoot =
                QDir::cleanPath(QDir::fromNativeSeparators(root));
        const QString gameRoot = gameOwnerRoot(data.itemsRoot);
        if (!gameRoot.isEmpty()
                && sameOrInside(data.projectRoot, gameRoot)) {
            QMessageBox::warning(
                        q, q->tr("Unsafe project location"),
                        q->tr("Choose a project/mod root outside the "
                              "Project Zomboid installation. The editor never "
                              "writes into the game directory."));
            data.projectRoot.clear();
            return false;
        }
        return true;
    }

    void reload(bool reportErrors)
    {
        LootData loaded;
        QString error;
        if (!loadGameDefinitions(gameEdit->text(), &loaded, &error)) {
            clearViews();
            setStatus(error, true);
            if (reportErrors)
                QMessageBox::warning(q, q->tr("Could not load loot definitions"),
                                     error);
            return;
        }
        if (!loadProjectDefinitions(projectEdit->text(), &loaded, &error)) {
            clearViews();
            setStatus(error, true);
            if (reportErrors)
                QMessageBox::warning(q, q->tr("Could not load project loot"),
                                     error);
            return;
        }
        data = loaded;
        gameEdit->setText(QDir::toNativeSeparators(data.itemsRoot));
        projectEdit->setText(QDir::toNativeSeparators(data.projectRoot));
        QSettings *settings = Preferences::instance()->settings();
        settings->setValue(
                    QStringLiteral("LootDistributionEditor/GamePath"),
                    data.itemsRoot);
        settings->setValue(
                    QStringLiteral("LootDistributionEditor/ProjectRoot"),
                    data.projectRoot);
        populateViews();
        setStatus(q->tr(
                      "Loaded %1 rooms, %2 procedural distributions; "
                      "%3 project room/container overrides and %4 project "
                      "procedural overrides. Game files remain read-only. "
                      "%5 unresolved references are marked in the mapping "
                      "table.")
                  .arg(data.rooms().size())
                  .arg(data.procedures().size())
                  .arg(projectContainerCount())
                  .arg(data.projectProcedures.size())
                  .arg(unresolvedReferenceCount(data)));
    }

    int projectContainerCount() const
    {
        int count = 0;
        for (const QJsonValue &value : data.projectRooms)
            count += value.toObject().size();
        return count;
    }

    void clearViews()
    {
        roomTree->clear();
        procedureTree->clear();
        roomTitle->clear();
        procedureTitle->clear();
        roomTable->clear();
        roomTable->setRowCount(0);
        procedureTable->clear();
        procedureTable->setRowCount(0);
    }

    void populateViews()
    {
        const QString selectedRoom = currentRoom();
        const QString selectedContainer = currentContainer();
        const QString selectedProcedure = currentProcedure();

        roomTree->clear();
        const QJsonObject rooms = data.rooms();
        QStringList roomNames = rooms.keys();
        std::sort(roomNames.begin(), roomNames.end(),
                  [](const QString &left, const QString &right) {
            return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        QTreeWidgetItem *wantedContainer = nullptr;
        for (const QString &roomName : roomNames) {
            QTreeWidgetItem *roomItem =
                    new QTreeWidgetItem(roomTree);
            roomItem->setText(0, roomName);
            roomItem->setData(0, RoleRoom, roomName);
            const QJsonObject room = rooms.value(roomName).toObject();
            QStringList containerNames = room.keys();
            std::sort(containerNames.begin(), containerNames.end(),
                      [](const QString &left, const QString &right) {
                return left.compare(right, Qt::CaseInsensitive) < 0;
            });
            bool localRoom = false;
            for (const QString &containerName : containerNames) {
                QTreeWidgetItem *containerItem =
                        new QTreeWidgetItem(roomItem);
                const bool local = data.isProjectContainer(
                            roomName, containerName);
                containerItem->setText(0, containerName);
                containerItem->setText(
                            1, local ? q->tr("Project") : q->tr("Game"));
                containerItem->setData(0, RoleRoom, roomName);
                containerItem->setData(
                            0, RoleContainer, containerName);
                containerItem->setData(0, RoleProject, local);
                if (local) {
                    containerItem->setForeground(
                                0, QBrush(QColor(35, 145, 65)));
                    localRoom = true;
                }
                const QString wantedRoomName = !requestedRoom.isEmpty()
                        ? requestedRoom : selectedRoom;
                const QString wantedContainerName =
                        !requestedContainer.isEmpty()
                        ? requestedContainer : selectedContainer;
                const bool roomMatches = wantedRoomName.isEmpty()
                        || roomName.compare(
                            wantedRoomName, Qt::CaseInsensitive) == 0;
                const bool containerMatches = wantedContainerName.isEmpty()
                        || containerName.compare(
                            wantedContainerName,
                            Qt::CaseInsensitive) == 0;
                const bool preferredAllRoom =
                        wantedRoomName.isEmpty()
                        && !wantedContainerName.isEmpty()
                        && roomName.compare(
                            QStringLiteral("all"),
                            Qt::CaseInsensitive) == 0;
                if (roomMatches && containerMatches
                        && (!wantedContainer || preferredAllRoom)) {
                    wantedContainer = containerItem;
                }
            }
            const bool gameRoom = data.gameRooms.contains(roomName);
            roomItem->setText(1, localRoom
                              ? (gameRoom ? q->tr("Game + project")
                                          : q->tr("Project"))
                              : q->tr("Game"));
        }
        if (wantedContainer) {
            roomTree->setCurrentItem(wantedContainer);
            roomTree->scrollToItem(wantedContainer);
        } else if (roomTree->topLevelItemCount()
                   && roomTree->topLevelItem(0)->childCount()) {
            roomTree->setCurrentItem(
                        roomTree->topLevelItem(0)->child(0));
        }
        requestedRoom.clear();
        requestedContainer.clear();

        procedureTree->clear();
        const QJsonObject procedures = data.procedures();
        QStringList names = procedures.keys();
        std::sort(names.begin(), names.end(),
                  [](const QString &left, const QString &right) {
            return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        QTreeWidgetItem *wantedProcedure = nullptr;
        for (const QString &name : names) {
            QTreeWidgetItem *item = new QTreeWidgetItem(procedureTree);
            const bool local = data.projectProcedures.contains(name);
            item->setText(0, name);
            item->setText(
                        1, local ? q->tr("Project") : q->tr("Game"));
            item->setData(0, RoleProject, local);
            if (local)
                item->setForeground(0, QBrush(QColor(35, 145, 65)));
            if (name == selectedProcedure)
                wantedProcedure = item;
        }
        if (wantedProcedure)
            procedureTree->setCurrentItem(wantedProcedure);
        else if (procedureTree->topLevelItemCount())
            procedureTree->setCurrentItem(
                        procedureTree->topLevelItem(0));
    }

    QString currentRoom() const
    {
        QTreeWidgetItem *item = roomTree->currentItem();
        return item ? item->data(0, RoleRoom).toString() : QString();
    }

    QString currentContainer() const
    {
        QTreeWidgetItem *item = roomTree->currentItem();
        return item ? item->data(0, RoleContainer).toString() : QString();
    }

    QString currentProcedure() const
    {
        QTreeWidgetItem *item = procedureTree->currentItem();
        return item ? item->text(0) : QString();
    }

    void showCurrentContainer()
    {
        const QString roomName = currentRoom();
        const QString containerName = currentContainer();
        const bool valid = !roomName.isEmpty() && !containerName.isEmpty();
        editContainerButton->setEnabled(valid);
        const bool project = valid
                && data.isProjectContainer(roomName, containerName);
        deleteContainerButton->setEnabled(project);
        if (!valid) {
            roomTitle->setText(roomName);
            roomHelp->setText(q->tr(
                                  "Select a container below this RoomDef."));
            roomTable->setRowCount(0);
            return;
        }

        const QJsonObject definition = data.rooms()
                .value(roomName).toObject()
                .value(containerName).toObject();
        const bool procedural =
                definition.value(QStringLiteral("procedural")).toBool()
                || definition.contains(QStringLiteral("procList"));
        roomTitle->setText(QStringLiteral("%1 -> %2")
                           .arg(roomName, containerName));
        if (procedural) {
            roomHelp->setText(q->tr(
                                  "Procedural selector. Share is normalized "
                                  "only among non-forced rows shown here; "
                                  "force* constraints and min/max can change "
                                  "which rows are eligible at runtime."));
            showRules(definition.value(
                          QStringLiteral("procList")).toArray());
        } else {
            const double rolls =
                    definition.value(QStringLiteral("rolls")).toDouble();
            roomHelp->setText(q->tr(
                                  "Direct distribution: %1 rolls. Chance / "
                                  "roll is the Lua value before sandbox "
                                  "rarity, zombie density, time decay and "
                                  "container-capacity effects.")
                              .arg(formattedNumber(rolls)));
            showContainerItems(definition);
        }
    }

    void showRules(const QJsonArray &rules)
    {
        roomTable->clear();
        roomTable->setColumnCount(7);
        roomTable->setHorizontalHeaderLabels(
                    QStringList()
                    << q->tr("Distribution") << q->tr("Weight")
                    << q->tr("Neutral share") << q->tr("Min")
                    << q->tr("Max") << q->tr("Forced by")
                    << q->tr("Status"));
        roomTable->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);
        roomTable->horizontalHeader()->setSectionResizeMode(
                    5, QHeaderView::Stretch);
        roomTable->setRowCount(rules.size());

        double total = 0.0;
        for (const QJsonValue &value : rules) {
            const QJsonObject rule = value.toObject();
            const bool forced =
                    rule.contains(QStringLiteral("forceForItems"))
                    || rule.contains(QStringLiteral("forceForZones"))
                    || rule.contains(QStringLiteral("forceForTiles"))
                    || rule.contains(QStringLiteral("forceForRooms"));
            double weight =
                    rule.value(QStringLiteral("weightChance")).toDouble(1.0);
            if (weight <= 0.0)
                weight = 1.0;
            if (!forced)
                total += weight;
        }
        const QJsonObject procedures = data.procedures();
        for (int row = 0; row < rules.size(); ++row) {
            const QJsonObject rule = rules.at(row).toObject();
            const QString name =
                    rule.value(QStringLiteral("name")).toString();
            double weight =
                    rule.value(QStringLiteral("weightChance")).toDouble(1.0);
            if (weight <= 0.0)
                weight = 1.0;
            QStringList forcedBy;
            const QStringList forceKeys = {
                QStringLiteral("forceForItems"),
                QStringLiteral("forceForZones"),
                QStringLiteral("forceForTiles"),
                QStringLiteral("forceForRooms")
            };
            const QStringList forceLabels = {
                q->tr("items"), q->tr("zones"),
                q->tr("tiles"), q->tr("rooms")
            };
            for (int i = 0; i < forceKeys.size(); ++i) {
                if (!rule.value(forceKeys.at(i)).toString().isEmpty())
                    forcedBy.append(forceLabels.at(i));
            }
            const bool forced = !forcedBy.isEmpty();
            const QString share = forced || total <= 0.0
                    ? q->tr("N/A")
                    : QStringLiteral("%1%")
                      .arg(QString::number(weight / total * 100.0, 'f', 1));
            const QStringList values = {
                name,
                formattedNumber(weight),
                share,
                rule.contains(QStringLiteral("min"))
                    ? QString::number(
                        rule.value(QStringLiteral("min")).toInt())
                    : q->tr("(missing = 0)"),
                rule.contains(QStringLiteral("max"))
                    ? QString::number(
                        rule.value(QStringLiteral("max")).toInt())
                    : q->tr("(missing = 0)"),
                forcedBy.join(QStringLiteral(", ")),
                procedures.contains(name)
                    ? q->tr("Resolved")
                    : q->tr("Missing distribution")
            };
            for (int column = 0; column < values.size(); ++column)
                roomTable->setItem(
                            row, column,
                            new QTableWidgetItem(values.at(column)));
            if (!procedures.contains(name)) {
                for (int column = 0; column < values.size(); ++column)
                    roomTable->item(row, column)->setForeground(Qt::red);
            }
        }
        roomTable->resizeColumnsToContents();
        roomTable->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);
        roomTable->horizontalHeader()->setSectionResizeMode(
                    5, QHeaderView::Stretch);
    }

    void showContainerItems(const QJsonObject &definition)
    {
        const double rolls =
                definition.value(QStringLiteral("rolls")).toDouble();
        const QJsonArray items =
                definition.value(QStringLiteral("items")).toArray();
        const QJsonObject junk =
                definition.value(QStringLiteral("junk")).toObject();
        const QJsonArray junkItems =
                junk.value(QStringLiteral("items")).toArray();
        const double junkRolls =
                junk.value(QStringLiteral("rolls")).toDouble();
        roomTable->clear();
        roomTable->setColumnCount(5);
        roomTable->setHorizontalHeaderLabels(
                    QStringList()
                    << q->tr("Group") << q->tr("Item")
                    << q->tr("Chance / roll")
                    << q->tr("Neutral cumulative")
                    << q->tr("Notes"));
        roomTable->horizontalHeader()->setSectionResizeMode(
                    1, QHeaderView::Stretch);
        roomTable->horizontalHeader()->setSectionResizeMode(
                    4, QHeaderView::Stretch);
        roomTable->setRowCount(
                    items.size() / 2 + junkItems.size() / 2);
        int row = 0;
        auto append = [this, &row](
                const QString &group, const QJsonArray &array,
                double groupRolls, const QString &note) {
            for (int i = 0; i + 1 < array.size(); i += 2, ++row) {
                const double chance = rawChance(array.at(i + 1));
                const QStringList values = {
                    group,
                    array.at(i).toString(),
                    QStringLiteral("%1%").arg(formattedNumber(chance)),
                    cumulativeChance(chance, groupRolls),
                    note
                };
                for (int column = 0; column < values.size(); ++column)
                    roomTable->setItem(
                                row, column,
                                new QTableWidgetItem(values.at(column)));
            }
        };
        append(q->tr("Normal"), items, rolls, QString());
        append(q->tr("Junk"), junkItems, junkRolls,
               q->tr("Junk modifier rules"));
    }

    void showCurrentProcedure()
    {
        const QString name = currentProcedure();
        const bool valid = !name.isEmpty();
        editProcedureButton->setEnabled(valid);
        const bool project = data.projectProcedures.contains(name);
        deleteProcedureButton->setEnabled(project);
        if (!valid) {
            procedureTitle->clear();
            procedureTable->setRowCount(0);
            return;
        }
        const QJsonObject definition =
                data.procedures().value(name).toObject();
        const double rolls =
                definition.value(QStringLiteral("rolls")).toDouble();
        const QJsonObject junk =
                definition.value(QStringLiteral("junk")).toObject();
        procedureTitle->setText(name);
        procedureHelp->setText(q->tr(
                                   "%1 normal rolls and %2 junk rolls. "
                                   "The cumulative column is a neutral "
                                   "mathematical preview, not an exact "
                                   "in-game result: sandbox category "
                                   "multipliers, zombie density, time decay "
                                   "and capacity still apply.")
                               .arg(formattedNumber(rolls),
                                    formattedNumber(
                                        junk.value(
                                            QStringLiteral("rolls"))
                                        .toDouble())));

        procedureTable->clear();
        procedureTable->setColumnCount(5);
        procedureTable->setHorizontalHeaderLabels(
                    QStringList()
                    << q->tr("Group") << q->tr("Item")
                    << q->tr("Chance / roll")
                    << q->tr("Neutral cumulative")
                    << q->tr("Duplicate entries"));
        procedureTable->horizontalHeader()->setSectionResizeMode(
                    1, QHeaderView::Stretch);
        const QJsonArray items =
                definition.value(QStringLiteral("items")).toArray();
        const QJsonArray junkItems =
                junk.value(QStringLiteral("items")).toArray();
        QMap<QString, int> duplicates;
        for (int i = 0; i + 1 < items.size(); i += 2)
            ++duplicates[items.at(i).toString()];
        for (int i = 0; i + 1 < junkItems.size(); i += 2)
            ++duplicates[junkItems.at(i).toString()];
        procedureTable->setRowCount(
                    items.size() / 2 + junkItems.size() / 2);
        int row = 0;
        auto appendGroup = [this, &row, &duplicates](
                const QString &group, const QJsonArray &array,
                double groupRolls) {
            for (int i = 0; i + 1 < array.size(); i += 2, ++row) {
                const QString itemName = array.at(i).toString();
                const double chance = array.at(i + 1).toDouble();
                const QStringList values = {
                    group,
                    itemName,
                    QStringLiteral("%1%").arg(formattedNumber(chance)),
                    cumulativeChance(chance, groupRolls),
                    duplicates.value(itemName) > 1
                        ? q->tr("%1 independent entries")
                          .arg(duplicates.value(itemName))
                        : QString()
                };
                for (int column = 0; column < values.size(); ++column)
                    procedureTable->setItem(
                                row, column,
                                new QTableWidgetItem(values.at(column)));
            }
        };
        appendGroup(q->tr("Normal"), items, rolls);
        appendGroup(q->tr("Junk"), junkItems,
                    junk.value(QStringLiteral("rolls")).toDouble());
        procedureTable->resizeColumnsToContents();
        procedureTable->horizontalHeader()->setSectionResizeMode(
                    1, QHeaderView::Stretch);
    }

    void editProcedure(bool create)
    {
        if (!ensureProjectRoot())
            return;
        const QString oldName = create ? QString() : currentProcedure();
        if (!create && oldName.isEmpty())
            return;
        const QJsonObject initial = create
                ? QJsonObject()
                : data.procedures().value(oldName).toObject();
        ProcedureEditorDialog dialog(oldName, initial, create, q);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QString name = dialog.name();
        if (create && data.procedures().contains(name)) {
            if (QMessageBox::question(
                        q, q->tr("Replace existing definition"),
                        q->tr("A distribution named \"%1\" already exists. "
                              "Create a project override for it?").arg(name))
                    != QMessageBox::Yes) {
                return;
            }
        }
        data.projectProcedures.insert(name, dialog.definition());
        if (!saveAndRefresh())
            return;
        selectProcedure(name);
    }

    void deleteProcedure()
    {
        const QString name = currentProcedure();
        if (!data.projectProcedures.contains(name))
            return;
        if (QMessageBox::question(
                    q, q->tr("Remove project override"),
                    q->tr("Remove the project definition \"%1\"? "
                          "The read-only game definition, if any, will become "
                          "visible again.").arg(name))
                != QMessageBox::Yes) {
            return;
        }
        data.projectProcedures.remove(name);
        saveAndRefresh();
    }

    void editContainer(bool create)
    {
        if (!ensureProjectRoot())
            return;
        QString roomName = create ? requestedRoom : currentRoom();
        QString containerName = create ? QString() : currentContainer();
        if (create && roomName.isEmpty())
            roomName = currentRoom();
        if (!create && (roomName.isEmpty() || containerName.isEmpty()))
            return;
        const QJsonObject initial = create
                ? QJsonObject()
                : data.rooms().value(roomName).toObject()
                  .value(containerName).toObject();
        ContainerEditorDialog dialog(
                    roomName, containerName, initial,
                    data.procedures().keys(), create, q);
        if (dialog.exec() != QDialog::Accepted)
            return;
        roomName = dialog.room();
        containerName = dialog.container();
        if (create && data.rooms().value(roomName).toObject()
                .contains(containerName)) {
            if (QMessageBox::question(
                        q, q->tr("Replace existing mapping"),
                        q->tr("The mapping %1 -> %2 already exists. Create a "
                              "project override for it?")
                        .arg(roomName, containerName))
                    != QMessageBox::Yes) {
                return;
            }
        }
        QJsonObject room =
                data.projectRooms.value(roomName).toObject();
        room.insert(containerName, dialog.definition());
        data.projectRooms.insert(roomName, room);
        if (!saveAndRefresh())
            return;
        selectContainer(roomName, containerName);
    }

    void deleteContainer()
    {
        const QString roomName = currentRoom();
        const QString containerName = currentContainer();
        if (!data.isProjectContainer(roomName, containerName))
            return;
        if (QMessageBox::question(
                    q, q->tr("Remove project override"),
                    q->tr("Remove the project mapping %1 -> %2? The read-only "
                          "game mapping, if any, will become visible again.")
                    .arg(roomName, containerName))
                != QMessageBox::Yes) {
            return;
        }
        QJsonObject room =
                data.projectRooms.value(roomName).toObject();
        room.remove(containerName);
        if (room.isEmpty())
            data.projectRooms.remove(roomName);
        else
            data.projectRooms.insert(roomName, room);
        saveAndRefresh();
    }

    bool saveAndRefresh()
    {
        QString error;
        if (!saveProjectDefinitions(data, &error)) {
            QMessageBox::critical(
                        q, q->tr("Could not save project loot"), error);
            setStatus(error, true);
            return false;
        }
        populateViews();
        setStatus(q->tr("Saved %1 and generated %2. Game files were not "
                        "modified.")
                  .arg(QDir::toNativeSeparators(
                           manifestFileName(data.projectRoot)),
                       QDir::toNativeSeparators(
                           luaFileName(data.projectRoot))));
        return true;
    }

    void selectProcedure(const QString &name)
    {
        for (int i = 0; i < procedureTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = procedureTree->topLevelItem(i);
            if (item->text(0) == name) {
                procedureTree->setCurrentItem(item);
                procedureTree->scrollToItem(item);
                break;
            }
        }
    }

    void selectContainer(const QString &roomName,
                         const QString &containerName)
    {
        for (int i = 0; i < roomTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *room = roomTree->topLevelItem(i);
            if (room->text(0) != roomName)
                continue;
            for (int child = 0; child < room->childCount(); ++child) {
                QTreeWidgetItem *container = room->child(child);
                if (container->text(0) == containerName) {
                    roomTree->setCurrentItem(container);
                    roomTree->scrollToItem(container);
                    return;
                }
            }
        }
    }

    void setStatus(const QString &text, bool error = false)
    {
        status->setText(text);
        QPalette palette = status->palette();
        palette.setColor(QPalette::WindowText,
                         error ? QColor(190, 35, 35)
                               : q->palette().color(QPalette::WindowText));
        status->setPalette(palette);
    }

    LootDistributionDialog *q;
    LootData data;
    QString requestedRoom;
    QString requestedContainer;
    QLineEdit *gameEdit;
    QLineEdit *projectEdit;
    QPushButton *openFolderButton;
    QTabWidget *tabs;
    QTreeWidget *roomTree;
    QLabel *roomTitle;
    QLabel *roomHelp;
    QTableWidget *roomTable;
    QPushButton *newContainerButton;
    QPushButton *editContainerButton;
    QPushButton *deleteContainerButton;
    QTreeWidget *procedureTree;
    QLabel *procedureTitle;
    QLabel *procedureHelp;
    QTableWidget *procedureTable;
    QPushButton *newProcedureButton;
    QPushButton *editProcedureButton;
    QPushButton *deleteProcedureButton;
    QLabel *status;
};

LootDistributionDialog::LootDistributionDialog(
        QWidget *parent,
        const QString &initialRoom,
        const QString &initialContainer,
        const QString &suggestedProjectRoot)
    : QDialog(parent)
    , d(new LootDistributionDialogPrivate(
            this, initialRoom, initialContainer, suggestedProjectRoot))
{
}

LootDistributionDialog::~LootDistributionDialog() = default;

bool LootDistributionDialog::validateDefinitions(
        const QString &gamePath, const QString &projectRoot,
        QString *summary, QString *error)
{
    LootData data;
    if (!loadGameDefinitions(gamePath, &data, error)
            || !loadProjectDefinitions(projectRoot, &data, error)) {
        return false;
    }
    if (!projectRoot.trimmed().isEmpty()) {
        const QString gameRoot = gameOwnerRoot(data.itemsRoot);
        if (!gameRoot.isEmpty()
                && sameOrInside(data.projectRoot, gameRoot)) {
            if (error) {
                *error = tr("The project/mod root is inside the read-only "
                            "game directory.");
            }
            return false;
        }
    }
    if (!validateGeneratedLua(generatedLua(data), error))
        return false;

    QStringList gameProblems;
    QStringList projectProblems;
    const QJsonObject procedures = data.procedures();
    const QJsonObject rooms = data.rooms();
    int containerCount = 0;
    int proceduralContainerCount = 0;
    int directContainerCount = 0;
    for (auto roomIt = rooms.begin(); roomIt != rooms.end(); ++roomIt) {
        const QJsonObject room = roomIt.value().toObject();
        containerCount += room.size();
        for (auto containerIt = room.begin();
             containerIt != room.end(); ++containerIt) {
            const QJsonObject definition = containerIt.value().toObject();
            const bool procedural =
                    definition.value(
                        QStringLiteral("procedural")).toBool()
                    || definition.contains(QStringLiteral("procList"));
            if (!procedural) {
                ++directContainerCount;
                continue;
            }
            ++proceduralContainerCount;
            const QJsonArray rules =
                    definition.value(
                        QStringLiteral("procList")).toArray();
            for (const QJsonValue &ruleValue : rules) {
                const QString name = ruleValue.toObject()
                        .value(QStringLiteral("name")).toString();
                if (name.isEmpty() || !procedures.contains(name)) {
                    QStringList &problems = data.isProjectContainer(
                                roomIt.key(), containerIt.key())
                            ? projectProblems : gameProblems;
                    problems.append(
                        QStringLiteral("%1/%2 -> %3")
                        .arg(roomIt.key(), containerIt.key(),
                             name.isEmpty()
                             ? QStringLiteral("<empty>")
                             : name));
                }
            }
        }
    }
    if (!projectProblems.isEmpty()) {
        if (error) {
            *error = tr("Unresolved project procedural references:\n%1")
                    .arg(projectProblems.mid(0, 20)
                         .join(QLatin1Char('\n')));
        }
        return false;
    }
    if (summary) {
        *summary = tr("%1 rooms, %2 containers (%3 procedural, %4 direct), "
                     "%5 procedural distributions, %6 project overrides, "
                     "%7 unresolved read-only game references")
                .arg(rooms.size()).arg(containerCount)
                .arg(proceduralContainerCount).arg(directContainerCount)
                .arg(procedures.size())
                .arg(data.projectProcedures.size()
                     + [&data]() {
            int count = 0;
            for (const QJsonValue &room : data.projectRooms)
                count += room.toObject().size();
            return count;
        }())
                .arg(gameProblems.size());
    }
    return true;
}

bool LootDistributionDialog::renderValidation(
        const QString &gamePath, const QString &projectRoot,
        const QString &outputFile, QString *error)
{
    QSettings *settings = Preferences::instance()->settings();
    const QString gameKey =
            QStringLiteral("LootDistributionEditor/GamePath");
    const QString projectKey =
            QStringLiteral("LootDistributionEditor/ProjectRoot");
    const bool hadGameSetting = settings->contains(gameKey);
    const bool hadProjectSetting = settings->contains(projectKey);
    const QVariant oldGameSetting = settings->value(gameKey);
    const QVariant oldProjectSetting = settings->value(projectKey);
    auto restoreSettings = [&]() {
        hadGameSetting ? settings->setValue(gameKey, oldGameSetting)
                       : settings->remove(gameKey);
        hadProjectSetting ? settings->setValue(
                                projectKey, oldProjectSetting)
                          : settings->remove(projectKey);
    };

    LootDistributionDialog dialog;
    dialog.d->gameEdit->setText(QDir::toNativeSeparators(gamePath));
    dialog.d->projectEdit->setText(
                QDir::toNativeSeparators(projectRoot));
    dialog.d->reload(false);
    if (dialog.d->data.itemsRoot.isEmpty()) {
        if (error)
            *error = dialog.d->status->text();
        restoreSettings();
        return false;
    }

    const QFileInfo outputInfo(outputFile);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        if (error) {
            *error = tr("Could not create screenshot directory %1.")
                    .arg(QDir::toNativeSeparators(
                             outputInfo.absolutePath()));
        }
        restoreSettings();
        return false;
    }
    dialog.show();
    QApplication::processEvents();
    const QPixmap capture = dialog.grab();
    dialog.hide();
    if (capture.isNull() || !capture.save(outputFile, "PNG")) {
        if (error) {
            *error = tr("Could not save loot-editor screenshot %1.")
                    .arg(QDir::toNativeSeparators(outputFile));
        }
        restoreSettings();
        return false;
    }
    restoreSettings();
    return true;
}

} // namespace Internal
} // namespace Tiled
