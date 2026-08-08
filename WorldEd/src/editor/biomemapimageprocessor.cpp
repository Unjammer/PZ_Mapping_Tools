#include "biomemapimageprocessor.h"

#include <QHash>
#include <QStringList>

const QList<BiomeMapImageProcessor::PaletteEntry> &
BiomeMapImageProcessor::palette()
{
    static const QList<PaletteEntry> entries = {
        { 0, QStringLiteral("Water"), QColor(0, 138, 255),
          QString(), QString(), QStringLiteral("Water"), true },
        { 59, QStringLiteral("Clay Shore"), QColor(59, 95, 59),
          QStringLiteral("clay_shore"), QString(),
          QStringLiteral("Forest"), true },
        { 64, QStringLiteral("Foraging Nav"), QColor(100, 100, 100),
          QString(), QString(), QStringLiteral("ForagingNav"), true },
        { 79, QStringLiteral("Clay Lake"), QColor(79, 105, 79),
          QStringLiteral("clay_lake"), QString(),
          QStringLiteral("Forest"), true },
        { 96, QStringLiteral("Random Deep Forest"), QColor(96, 70, 35),
          QStringLiteral("$random"), QString(),
          QStringLiteral("DeepForest"), true },
        { 102, QStringLiteral("Trailer Park"), QColor(210, 200, 160),
          QStringLiteral("townhouse"), QString(),
          QStringLiteral("TrailerPark"), true },
        { 115, QStringLiteral("Town Zone"), QColor(165, 160, 140),
          QStringLiteral("townhouse"), QString(),
          QStringLiteral("TownZone"), true },
        { 128, QStringLiteral("Farm"), QColor(255, 128, 0),
          QStringLiteral("farmmix_forest"), QString(),
          QStringLiteral("Farm"), true },
        { 141, QStringLiteral("Farmland"), QColor(120, 70, 20),
          QStringLiteral("farmmix_forest"), QString(),
          QStringLiteral("FarmLand"), true },
        { 153, QStringLiteral("PH Forest"), QColor(64, 0, 0),
          QStringLiteral("ph_forest"), QString(),
          QStringLiteral("PHForest"), true },
        { 171, QStringLiteral("Redbud Jumbo XXL"), QColor(145, 135, 60),
          QStringLiteral("vegitation"), QString(),
          QStringLiteral("Vegitation"), false },
        { 179, QStringLiteral("PR Forest"), QColor(90, 32, 24),
          QStringLiteral("pr_forest"), QStringLiteral("map_forest"),
          QStringLiteral("PRForest"), true },
        { 192, QStringLiteral("Farm Mix Forest"), QColor(255, 0, 255),
          QStringLiteral("farmmix_forest"), QStringLiteral("map_forest"),
          QStringLiteral("FarmMixForest"), true },
        { 204, QStringLiteral("Farm Forest"), QColor(0, 255, 0),
          QStringLiteral("farm_forest"), QString(),
          QStringLiteral("FarmForest"), true },
        { 217, QStringLiteral("Birch Forest"), QColor(200, 217, 170),
          QStringLiteral("birch_forest"), QStringLiteral("map_forest"),
          QStringLiteral("BirchForest"), true },
        { 230, QStringLiteral("Birch Mix Forest"), QColor(150, 190, 130),
          QStringLiteral("birchmix_forest"), QStringLiteral("map_forest"),
          QStringLiteral("BirchMixForest"), true },
        { 243, QStringLiteral("Organic Forest"), QColor(45, 100, 45),
          QStringLiteral("organic_forest"), QStringLiteral("map_forest"),
          QStringLiteral("OrganicForest"), true },
        { 254, QStringLiteral("Dirt / Foraging Nav"), QColor(110, 80, 55),
          QStringLiteral("dirt"), QStringLiteral("dirt"),
          QStringLiteral("ForagingNav"), true },
        { 255, QStringLiteral("Primary / Deep Forest"), QColor(127, 0, 0),
          QStringLiteral("primary_forest"),
          QStringLiteral("map_deep_forest"),
          QStringLiteral("DeepForest"), true }
    };
    return entries;
}

const BiomeMapImageProcessor::PaletteEntry *
BiomeMapImageProcessor::entryForValue(int value)
{
    for (const PaletteEntry &entry : palette()) {
        if (entry.value == value)
            return &entry;
    }
    return nullptr;
}

QString BiomeMapImageProcessor::oreSelectorDescription(const QString &ore)
{
    if (ore == QStringLiteral("map_forest")) {
        return QStringLiteral(
                    "Surface deposits of boulders, limestone, or flint. "
                    "Density comes from procedural ore noise. Iron and "
                    "copper use separate vein generation.");
    }
    if (ore == QStringLiteral("map_deep_forest")) {
        return QStringLiteral(
                    "Deep-forest surface deposits of boulders, limestone, "
                    "or flint. Density comes from procedural ore noise. "
                    "Iron and copper use separate vein generation.");
    }
    if (ore == QStringLiteral("dirt")) {
        return QStringLiteral(
                    "Secondary dirt map-biome lookup. It does not define "
                    "an iron, copper, limestone, or flint deposit.");
    }
    return QStringLiteral("No secondary WorldGen ore-biome lookup.");
}

QColor BiomeMapImageProcessor::displayColor(int value)
{
    const PaletteEntry *entry = entryForValue(value);
    if (entry)
        return entry->color;
    return QColor(value, value, value);
}

const QSet<int> &BiomeMapImageProcessor::knownValues()
{
    static const QSet<int> values = [] {
        QSet<int> result;
        for (const PaletteEntry &entry : palette())
            result.insert(entry.value);
        return result;
    }();
    return values;
}

const QSet<int> &BiomeMapImageProcessor::overrideValues()
{
    static const QSet<int> values = [] {
        QSet<int> result;
        for (const PaletteEntry &entry : palette()) {
            if (!entry.enabledByDefault)
                result.insert(entry.value);
        }
        return result;
    }();
    return values;
}

QImage BiomeMapImageProcessor::createBiomeLayer(const QImage &mainImage,
                                                const QImage &vegetationImage,
                                                QSet<QRgb> *unknownColors)
{
    if (mainImage.isNull() || vegetationImage.isNull() ||
            mainImage.size() != vegetationImage.size())
        return QImage();

    static const QHash<QRgb, int> biomeForColor = {
        {qRgb(0, 138, 255), 0},
        {qRgb(100, 100, 100), 64},
        {qRgb(120, 120, 120), 115},
        {qRgb(165, 160, 140), 115},
        // Biome 171 is intentionally map-specific in B42.20. It is enabled
        // by WorldGenOverride.lua and deterministically places a redbud XXL.
        {qRgb(145, 135, 60), 171},
        {qRgb(145, 135, 61), 171},
        {qRgb(90, 100, 35), 171},
        {qRgb(117, 117, 47), 171},
        {qRgb(120, 70, 20), 141},
        {qRgb(0, 255, 0), 204},
        {qRgb(64, 0, 0), 153},
        {qRgb(255, 0, 0), 153},
        {qRgb(127, 0, 0), 255},
        {qRgb(255, 0, 255), 192},
        {qRgb(210, 200, 160), 102},
        {qRgb(140, 70, 15), 102},
        {qRgb(255, 128, 0), 128},
        {qRgb(220, 100, 0), 128}
    };

    if (unknownColors)
        unknownColors->clear();
    QImage result(mainImage.size(), QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y) {
        QRgb *output = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb vegetation = vegetationImage.pixel(x, y);
            const QRgb source = qRed(vegetation) == 0 && qGreen(vegetation) == 0 &&
                    qBlue(vegetation) == 0 ? mainImage.pixel(x, y) : vegetation;
            const QRgb rgb = qRgb(qRed(source), qGreen(source), qBlue(source));
            const auto it = biomeForColor.constFind(rgb);
            const int value = it == biomeForColor.constEnd() ? qRed(rgb) : it.value();
            if (it == biomeForColor.constEnd() && unknownColors)
                unknownColors->insert(rgb);
            output[x] = qRgb(value, value, value);
        }
    }
    return result;
}

QImage BiomeMapImageProcessor::process(const QImage &biomeLayer,
                                       const QImage &zoneLayer)
{
    if (biomeLayer.isNull() || zoneLayer.isNull() ||
            biomeLayer.size() != zoneLayer.size()) {
        return QImage();
    }

    QImage result(biomeLayer.size(), QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y) {
        QRgb *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int biome = qRed(biomeLayer.pixel(x, y));
            const int zone = qGreen(zoneLayer.pixel(x, y));
            output[x] = qRgba(biome, zone, 0, 255);
        }
    }
    return result;
}

BiomeMapImageProcessor::Analysis BiomeMapImageProcessor::analyze(
        const QImage &biomeLayer, const QImage &zoneLayer, int chunkSize)
{
    Analysis result;
    if (biomeLayer.isNull() || zoneLayer.isNull() ||
            biomeLayer.size() != zoneLayer.size() || chunkSize <= 0)
        return result;

    for (int y = 0; y < biomeLayer.height(); ++y) {
        for (int x = 0; x < biomeLayer.width(); ++x) {
            result.biomeValues.insert(qRed(biomeLayer.pixel(x, y)));
            result.zoneValues.insert(qGreen(zoneLayer.pixel(x, y)));
        }
    }
    result.unknownBiomeValues = result.biomeValues - knownValues();
    result.unknownZoneValues = result.zoneValues - knownValues();
    for (const PaletteEntry &entry : palette()) {
        if (entry.biome.isEmpty() && entry.ore.isEmpty() &&
                result.biomeValues.contains(entry.value)) {
            result.biomeValuesWithoutEffect.insert(entry.value);
        }
    }
    result.valuesRequiringOverride =
            (result.biomeValues | result.zoneValues) & overrideValues();

    for (int y = 0; y < zoneLayer.height(); y += chunkSize) {
        for (int x = 0; x < zoneLayer.width(); x += chunkSize) {
            const int first = qGreen(zoneLayer.pixel(x, y));
            bool mixed = false;
            for (int yy = y; yy < qMin(y + chunkSize, zoneLayer.height()) && !mixed; ++yy) {
                for (int xx = x; xx < qMin(x + chunkSize, zoneLayer.width()); ++xx) {
                    if (qGreen(zoneLayer.pixel(xx, yy)) != first) {
                        mixed = true;
                        break;
                    }
                }
            }
            if (mixed)
                ++result.mixedZoneChunks;
        }
    }
    return result;
}

bool BiomeMapImageProcessor::validateConfiguration(QString *errorString)
{
    const QString expected = QStringLiteral(
        "0|||Water|default\n"
        "59|clay_shore||Forest|default\n"
        "64|||ForagingNav|default\n"
        "79|clay_lake||Forest|default\n"
        "96|$random||DeepForest|default\n"
        "102|townhouse||TrailerPark|default\n"
        "115|townhouse||TownZone|default\n"
        "128|farmmix_forest||Farm|default\n"
        "141|farmmix_forest||FarmLand|default\n"
        "153|ph_forest||PHForest|default\n"
        "171|vegitation||Vegitation|override\n"
        "179|pr_forest|map_forest|PRForest|default\n"
        "192|farmmix_forest|map_forest|FarmMixForest|default\n"
        "204|farm_forest||FarmForest|default\n"
        "217|birch_forest|map_forest|BirchForest|default\n"
        "230|birchmix_forest|map_forest|BirchMixForest|default\n"
        "243|organic_forest|map_forest|OrganicForest|default\n"
        "254|dirt|dirt|ForagingNav|default\n"
        "255|primary_forest|map_deep_forest|DeepForest|default");

    QStringList actual;
    QSet<int> values;
    for (const PaletteEntry &entry : palette()) {
        if (values.contains(entry.value)) {
            if (errorString) {
                *errorString = QStringLiteral(
                            "Duplicate BiomeMap pixel ID %1.")
                        .arg(entry.value);
            }
            return false;
        }
        values.insert(entry.value);
        actual += QStringLiteral("%1|%2|%3|%4|%5")
                .arg(entry.value)
                .arg(entry.biome)
                .arg(entry.ore)
                .arg(entry.zone)
                .arg(entry.enabledByDefault
                     ? QStringLiteral("default")
                     : QStringLiteral("override"));
    }

    if (actual.join(QLatin1Char('\n')) != expected) {
        if (errorString) {
            *errorString = QStringLiteral(
                        "The embedded BiomeMapConfig mapping does not match "
                        "the verified Build 42.20 table.");
        }
        return false;
    }

    const QString mapForestDescription =
            oreSelectorDescription(QStringLiteral("map_forest"));
    const QString mapDeepForestDescription =
            oreSelectorDescription(QStringLiteral("map_deep_forest"));
    if (!mapForestDescription.contains(QStringLiteral("boulders")) ||
            !mapForestDescription.contains(QStringLiteral("limestone")) ||
            !mapForestDescription.contains(QStringLiteral("flint")) ||
            !mapForestDescription.contains(QStringLiteral("Iron")) ||
            !mapForestDescription.contains(QStringLiteral("copper")) ||
            !mapDeepForestDescription.contains(
                QStringLiteral("Deep-forest"))) {
        if (errorString) {
            *errorString = QStringLiteral(
                        "BiomeMap ore-selector descriptions are incomplete.");
        }
        return false;
    }

    QImage biome(2, 1, QImage::Format_ARGB32);
    QImage zone(2, 1, QImage::Format_ARGB32);
    biome.setPixel(0, 0, qRgb(254, 254, 254));
    biome.setPixel(1, 0, qRgb(171, 171, 171));
    zone.setPixel(0, 0, qRgb(64, 64, 64));
    zone.setPixel(1, 0, qRgb(171, 171, 171));
    const Analysis analysis = analyze(biome, zone, 8);
    if (!analysis.unknownBiomeValues.isEmpty() ||
            !analysis.unknownZoneValues.isEmpty() ||
            analysis.biomeValuesWithoutEffect.contains(254) ||
            analysis.valuesRequiringOverride != QSet<int>{171}) {
        if (errorString) {
            *errorString = QStringLiteral(
                        "BiomeMap configuration analysis did not preserve "
                        "active, neutral, and override-only semantics.");
        }
        return false;
    }

    if (errorString)
        errorString->clear();
    return true;
}
