#include "worldobjectvalidation.h"

#include "world.h"
#include "worldcell.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QtMath>

namespace {

void resolveProperties(PropertyHolder *holder, PropertyList &result)
{
    for (PropertyTemplate *propertyTemplate : holder->templates())
        resolveProperties(propertyTemplate, result);
    for (Property *property : holder->properties()) {
        result.removeAll(property->mDefinition);
        result += property;
    }
}

bool isBoolean(const QString &value)
{
    return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
            || value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0;
}

bool isNumber(const QString &value)
{
    bool ok = false;
    value.toDouble(&ok);
    return ok;
}

bool isIntegral(qreal value)
{
    return qFuzzyIsNull(value - qRound64(value));
}

bool hasIntegralPosition(WorldCellObject *object)
{
    return isIntegral(object->x()) && isIntegral(object->y());
}

QString typeName(WorldCellObject *object)
{
    return object && object->type() ? object->type()->name() : QString();
}

void attachTemplate(WorldCellObject *object, const QString &name)
{
    World *world = object->cell()->world();
    PropertyTemplate *propertyTemplate = world->propertyTemplate(name);
    if (propertyTemplate && !object->usesTemplate(propertyTemplate)
            && object->canAddTemplate(propertyTemplate)) {
        object->addTemplate(object->templates().size(), propertyTemplate);
    }
}

void addDefaultProperty(WorldCellObject *object, const QString &name,
                        const QString &value)
{
    PropertyDef *definition = object->cell()->world()->propertyDefinition(name);
    if (definition && !object->properties().find(definition)) {
        object->addProperty(object->properties().size(),
                            new Property(definition, value));
    }
}

QString translated(const char *text)
{
    return QCoreApplication::translate("WorldObjectValidation", text);
}

}

PropertyList WorldObjectValidation::resolvedProperties(PropertyHolder *holder)
{
    PropertyList result;
    if (holder)
        resolveProperties(holder, result);
    return result;
}

QString WorldObjectValidation::resolvedValue(WorldCellObject *object,
                                             const QString &propertyName)
{
    if (!object || !object->cell())
        return QString();
    PropertyDef *definition =
            object->cell()->world()->propertyDefinition(propertyName);
    if (!definition)
        return QString();
    Property *property = resolvedProperties(object).find(definition);
    return property ? property->mValue.trimmed() : QString();
}

QStringList WorldObjectValidation::spawnProfessions(WorldCellObject *object)
{
    const QStringList values = resolvedValue(object, QLatin1String("Professions"))
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList professions;
    for (const QString &value : values) {
        QString profession = value.trimmed();
        if (profession.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0)
            profession = QStringLiteral("unemployed");
        if (!profession.isEmpty() && !professions.contains(profession))
            professions += profession;
    }
    return professions;
}

QStringList WorldObjectValidation::expectedObjectTypes()
{
    return QStringList() << QLatin1String("SpawnPoint")
                         << QLatin1String("ParkingStall")
                         << QLatin1String("Vehicle")
                         << QLatin1String("WaterFlow")
                         << QLatin1String("WaterZone")
                         << QLatin1String("RoomTone")
                         << QLatin1String("Mannequin")
                         << QLatin1String("Animal")
                         << QLatin1String("Basement")
                         << QLatin1String("WorldGen");
}

QStringList WorldObjectValidation::expectedPropertyNames(
        const QString &typeName)
{
    if (typeName == QLatin1String("SpawnPoint"))
        return QStringList() << QLatin1String("Professions");
    if (typeName == QLatin1String("ParkingStall")
            || typeName == QLatin1String("Vehicle")) {
        return QStringList() << QLatin1String("Direction")
                             << QLatin1String("FaceDirection");
    }
    if (typeName == QLatin1String("WaterFlow")) {
        return QStringList() << QLatin1String("WaterDirection")
                             << QLatin1String("WaterSpeed");
    }
    if (typeName == QLatin1String("WaterZone")) {
        return QStringList() << QLatin1String("WaterGround")
                             << QLatin1String("WaterShore");
    }
    if (typeName == QLatin1String("RoomTone")) {
        return QStringList() << QLatin1String("RoomTone")
                             << QLatin1String("EntireBuilding");
    }
    if (typeName == QLatin1String("Mannequin")) {
        return QStringList() << QLatin1String("Female")
                             << QLatin1String("Outfit")
                             << QLatin1String("Pose")
                             << QLatin1String("Script")
                             << QLatin1String("Skin")
                             << QLatin1String("Direction");
    }
    if (typeName == QLatin1String("Animal")) {
        return QStringList() << QLatin1String("Action")
                             << QLatin1String("AnimalType")
                             << QLatin1String("SpawnAnimals");
    }
    if (typeName == QLatin1String("Basement")) {
        return QStringList() << QLatin1String("Access")
                             << QLatin1String("StairDirection")
                             << QLatin1String("StairX")
                             << QLatin1String("StairY");
    }
    if (typeName == QLatin1String("WorldGen"))
        return QStringList() << QLatin1String("Rocks");
    return QStringList();
}

QStringList WorldObjectValidation::expectedPropertyNames(
        WorldCellObject *object)
{
    return expectedPropertyNames(typeName(object));
}

QString WorldObjectValidation::typeDescription(const QString &typeName)
{
    if (typeName == QLatin1String("SpawnPoint"))
        return translated("Defines a player start exported to spawnpoints.lua. Select one or more professions.");
    if (typeName == QLatin1String("SpawnOrigin"))
        return translated("Defines an area used as an origin by the zombie population and respawn systems.");
    if (typeName == QLatin1String("TownZone"))
        return translated("Classifies an urban area for foraging, ambient audio, trapping, loot respawn, and related zone queries.");
    if (typeName == QLatin1String("Forest"))
        return translated("Classifies a forest area for foraging, ambient audio, trapping, zombie grouping, and related events.");
    if (typeName == QLatin1String("DeepForest"))
        return translated("Classifies dense forest for foraging, ambient audio, trapping, zombie grouping, and related events.");
    if (typeName == QLatin1String("Nav"))
        return translated("Defines a vector road or navigation strip used by vehicle stories, crashes, events, audio, and road queries. It is distinct from raster ForagingNav.");
    if (typeName == QLatin1String("Vegitation"))
        return translated("Classifies vegetation terrain for foraging, ambient audio, trapping, and related zone queries. The runtime spelling is intentional.");
    if (typeName == QLatin1String("TrailerPark"))
        return translated("Classifies a trailer-park area for foraging, ambient audio, trapping, loot respawn, and poor-area logic.");
    if (typeName == QLatin1String("Farm"))
        return translated("Classifies a farm area for ambient audio, rural stories, poor-area logic, and related zone queries.");
    if (typeName == QLatin1String("ParkingStall"))
        return translated("Defines a rectangle or polyline where vehicles may spawn. The zone name selects the vehicle distribution.");
    if (typeName == QLatin1String("FarmLand"))
        return translated("Classifies farmland for foraging, rural stories, off-road vehicle behavior, and related zone queries.");
    if (typeName == QLatin1String("LootZone"))
        return translated("Assigns a geographic wealth or environment name such as Rich or Poor. It is not a general loot-distribution selector.");
    if (typeName == QLatin1String("NoPower"))
        return translated("Disables electrical grid power inside the zone. Local generators may still provide electricity.");
    if (typeName == QLatin1String("NoWater"))
        return translated("Disables piped water inside the zone.");
    if (typeName == QLatin1String("NoPowerOrWater"))
        return translated("Disables both electrical grid power and piped water inside the zone. It does not affect fuel pumps.");
    if (typeName == QLatin1String("NoGas"))
        return translated("Makes fuel pumps inside the zone initialize empty. The Infinite Fuel sandbox setting still overrides it.");
    if (typeName == QLatin1String("Ranch"))
        return translated("Defines a candidate area for randomized livestock ranches. The zone name selects the animal or ranch definition.");
    if (typeName == QLatin1String("ZombiesType"))
        return translated("Selects a themed zombie outfit or distribution definition, such as restaurant, police, factory, or rich-area zombies.");
    if (typeName == QLatin1String("ZoneStory"))
        return translated("Defines a candidate rectangle for randomized outdoor scenes. The zone name selects the story category.");
    if (typeName == QLatin1String("WaterFlow"))
        return translated("Defines a point controlling animated water-flow direction and speed.");
    if (typeName == QLatin1String("WaterZone"))
        return translated("Overrides shore and water rendering information in a rectangle. It does not create water tiles.");
    if (typeName == QLatin1String("Mannequin"))
        return translated("Applies configuration to mannequins already placed inside the zone.");
    if (typeName == QLatin1String("RoomTone"))
        return translated("Defines an interior audio point. Build 42.20.3 loads active room tones from roomtones.lua.");
    if (typeName == QLatin1String("Animal"))
        return translated("Defines a virtual-animal movement or activity network using points and polylines with actions such as Follow, Eat, and Sleep.");
    if (typeName == QLatin1String("Basement"))
        return translated("Defines candidate basement bounds, entrances, and access information used by basement generation.");
    if (typeName == QLatin1String("WorldGen"))
        return translated("Controls world generation inside the zone. Build 42.20.3 primarily uses Rocks=false to prevent procedural rocks and ore.");
    if (typeName == QLatin1String("BuildingName"))
        return translated("Assigns a name to a building area, including named keys before room and building fallback names.");
    if (typeName == QLatin1String("Region"))
        return translated("Defines a named geographic region used by regional content and location classification.");
    return QString();
}

void WorldObjectValidation::applyTypeTooltips(QComboBox *comboBox)
{
    if (!comboBox)
        return;
    for (int i = 0; i < comboBox->count(); ++i) {
        comboBox->setItemData(i, typeDescription(comboBox->itemText(i)),
                              Qt::ToolTipRole);
    }
    comboBox->setToolTip(typeDescription(comboBox->currentText()));
    if (comboBox->property("pzTypeTooltipsConnected").toBool())
        return;
    comboBox->setProperty("pzTypeTooltipsConnected", true);
    QObject::connect(comboBox, &QComboBox::currentTextChanged, comboBox,
                     [comboBox](const QString &text) {
        comboBox->setToolTip(
                    WorldObjectValidation::typeDescription(text));
    });
}

bool WorldObjectValidation::supportsBasementAccess(
        WorldCellObject *object)
{
    return typeName(object) == QLatin1String("Basement");
}

void WorldObjectValidation::applyCreationDefaults(WorldCellObject *object)
{
    if (!object || !object->cell())
        return;

    const QString type = typeName(object);
    if (type == QLatin1String("SpawnPoint")) {
        attachTemplate(object, type);
        addDefaultProperty(object, QLatin1String("Professions"),
                           QLatin1String("unemployed"));
    } else if (type == QLatin1String("WaterFlow")) {
        attachTemplate(object, type);
        addDefaultProperty(object, QLatin1String("WaterDirection"),
                           QLatin1String("0"));
        addDefaultProperty(object, QLatin1String("WaterSpeed"),
                           QLatin1String("0.0"));
    } else if (type == QLatin1String("WaterZone")) {
        attachTemplate(object, type);
        addDefaultProperty(object, QLatin1String("WaterGround"),
                           QLatin1String("false"));
        addDefaultProperty(object, QLatin1String("WaterShore"),
                           QLatin1String("true"));
    } else if (type == QLatin1String("RoomTone")) {
        attachTemplate(object, type);
        addDefaultProperty(object, QLatin1String("RoomTone"),
                           QLatin1String("Generic"));
        addDefaultProperty(object, QLatin1String("EntireBuilding"),
                           QLatin1String("false"));
    }

    if (requiresUnitRectangle(object)) {
        object->setGeometryType(ObjectGeometryType::INVALID);
        object->setWidth(1);
        object->setHeight(1);
    }
}

bool WorldObjectValidation::requiresUnitRectangle(WorldCellObject *object)
{
    const QString type = typeName(object);
    return type == QLatin1String("SpawnPoint")
            || type == QLatin1String("WaterFlow")
            || type == QLatin1String("RoomTone");
}

bool WorldObjectValidation::requiresRectangle(WorldCellObject *object)
{
    const QString type = typeName(object);
    return requiresUnitRectangle(object)
            || type == QLatin1String("WaterZone")
            || type == QLatin1String("SpawnOrigin");
}

bool WorldObjectValidation::validateSpawnPoint(WorldCellObject *object,
                                               QString *reason)
{
    if (!object || !object->cell() || !object->type()
            || !object->isSpawnPoint()) {
        if (reason)
            *reason = translated("the record is not a valid SpawnPoint object");
        return false;
    }
    if (!object->isRectangle() || object->width() <= 0
            || object->height() <= 0) {
        if (reason)
            *reason = translated("SpawnPoint must be a non-empty rectangle");
        return false;
    }
    if (!hasIntegralPosition(object)) {
        if (reason)
            *reason = translated("SpawnPoint position must use whole map-square coordinates");
        return false;
    }

    if (spawnProfessions(object).isEmpty()) {
        if (reason)
            *reason = translated("SpawnPoint must define at least one profession");
        return false;
    }
    return true;
}

bool WorldObjectValidation::validateExportObject(WorldCellObject *object,
                                                 QString *reason)
{
    if (!object || !object->cell() || !object->type()) {
        if (reason)
            *reason = translated("the object has no valid cell or type");
        return false;
    }

    const QString type = typeName(object);
    if (type == QLatin1String("SpawnPoint"))
        return validateSpawnPoint(object, reason);

    if (type == QLatin1String("WaterFlow")) {
        if (!object->isRectangle() || object->width() != 1
                || object->height() != 1) {
            if (reason)
                *reason = translated("WaterFlow must be a 1 x 1 rectangle");
            return false;
        }
        if (!hasIntegralPosition(object)) {
            if (reason)
                *reason = translated("WaterFlow position must use whole map-square coordinates");
            return false;
        }
        if (!isNumber(resolvedValue(object,
                                    QLatin1String("WaterDirection")))
                || !isNumber(resolvedValue(object,
                                           QLatin1String("WaterSpeed")))) {
            if (reason)
                *reason = translated("WaterFlow must define numeric WaterDirection and WaterSpeed properties");
            return false;
        }
    } else if (type == QLatin1String("WaterZone")) {
        if (!object->isRectangle() || object->width() < 1
                || object->height() < 1
                || !isIntegral(object->width())
                || !isIntegral(object->height())
                || !hasIntegralPosition(object)) {
            if (reason)
                *reason = translated("WaterZone must be a non-empty rectangle aligned to whole map squares");
            return false;
        }
        if (!isBoolean(resolvedValue(object,
                                     QLatin1String("WaterGround")))
                || !isBoolean(resolvedValue(object,
                                            QLatin1String("WaterShore")))) {
            if (reason)
                *reason = translated("WaterZone must define boolean WaterGround and WaterShore properties");
            return false;
        }
    } else if (type == QLatin1String("RoomTone")) {
        if (!object->isRectangle() || object->width() <= 0
                || object->height() <= 0) {
            if (reason)
                *reason = translated("RoomTone must be a non-empty rectangle");
            return false;
        }
        if (!hasIntegralPosition(object)) {
            if (reason)
                *reason = translated("RoomTone position must use whole map-square coordinates");
            return false;
        }
        PropertyDef *entireBuildingDefinition =
                object->cell()->world()->propertyDefinition(
                    QLatin1String("EntireBuilding"));
        Property *entireBuilding = entireBuildingDefinition
                ? resolvedProperties(object).find(entireBuildingDefinition)
                : nullptr;
        if (resolvedValue(object, QLatin1String("RoomTone")).isEmpty()
                || (entireBuilding
                    && !isBoolean(entireBuilding->mValue.trimmed()))) {
            if (reason)
                *reason = translated("RoomTone must define a RoomTone value. EntireBuilding must be boolean when present");
            return false;
        }
    } else if (type == QLatin1String("SpawnOrigin")) {
        const QPointF position = object->absoluteWorldPosition();
        if (!object->isRectangle() || object->width() < 1
                || object->height() < 1
                || !isIntegral(object->width())
                || !isIntegral(object->height())
                || !hasIntegralPosition(object)
                || position.x() < 0 || position.y() < 0) {
            if (reason)
                *reason = translated("SpawnOrigin must be a non-empty rectangle aligned to non-negative whole map-square coordinates");
            return false;
        }
    }
    return true;
}

QString WorldObjectValidation::describe(WorldCellObject *object)
{
    if (!object || !object->cell())
        return translated("Unknown object");
    const QPointF position = object->absoluteWorldPosition();
    const QString type = typeName(object).isEmpty()
            ? translated("Object") : typeName(object);
    return QString::fromLatin1("%1 at posX %2, posY %3, posZ %4")
            .arg(type)
            .arg(qRound64(position.x()))
            .arg(qRound64(position.y()))
            .arg(object->level());
}
