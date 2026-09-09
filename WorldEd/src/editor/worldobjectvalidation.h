#ifndef WORLDOBJECTVALIDATION_H
#define WORLDOBJECTVALIDATION_H

#include "worldproperties.h"

#include <QString>
#include <QStringList>

class WorldCellObject;
class QComboBox;

class WorldObjectValidation
{
public:
    static PropertyList resolvedProperties(PropertyHolder *holder);
    static QString resolvedValue(WorldCellObject *object,
                                 const QString &propertyName);
    static QStringList spawnProfessions(WorldCellObject *object);
    static QStringList expectedObjectTypes();
    static QStringList expectedPropertyNames(const QString &typeName);
    static QStringList expectedPropertyNames(WorldCellObject *object);
    static QString typeDescription(const QString &typeName);
    static void applyTypeTooltips(QComboBox *comboBox);
    static bool supportsBasementAccess(WorldCellObject *object);
    static void applyCreationDefaults(WorldCellObject *object);
    static bool requiresUnitRectangle(WorldCellObject *object);
    static bool requiresRectangle(WorldCellObject *object);
    static bool validateSpawnPoint(WorldCellObject *object, QString *reason);
    static bool validateExportObject(WorldCellObject *object, QString *reason);
    static QString describe(WorldCellObject *object);
};

#endif
