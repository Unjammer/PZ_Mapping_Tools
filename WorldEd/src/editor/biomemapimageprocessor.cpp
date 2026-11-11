#include "biomemapimageprocessor.h"
#include <QHash>
#include <QStringList>
#include <array>

static QImage packedPixels(const QImage &image)
{
    if (image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32)
        return image;
    return image.convertToFormat(QImage::Format_ARGB32);
}
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

int BiomeMapImageProcessor::zoneValueForName(const QString &name)
{
    static const QHash<QString, int> values = [] {
        QHash<QString, int> result;
        for (const auto &entry : palette()) {
            const QString zone = entry.zone.toLower();
            if (!result.contains(zone) || entry.value == 255)
                result.insert(zone, entry.value);
        }
        return result;
    }();
    return values.value(name.toLower(), -1);
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
                                               QSet<QRgb> *unknownColors,
                                               bool useVegitationOverride)
{
    if (mainImage.isNull() || vegetationImage.isNull() ||
            mainImage.size() != vegetationImage.size())
        return QImage();
    static const QHash<QRgb, int> biomeForColor = [] {
        QHash<QRgb, int> result = {
            {qRgb(0, 0, 0), 0},
            {qRgb(120, 120, 120), 115},
            {qRgb(145, 135, 61), 171},
            {qRgb(90, 100, 35), 171},
            {qRgb(117, 117, 47), 171},
            {qRgb(80, 55, 20), 64},
            {qRgb(110, 100, 100), 64},
            {qRgb(130, 120, 120), 64},
            {qRgb(255, 0, 0), 153},
            {qRgb(140, 70, 15), 102},
            {qRgb(220, 100, 0), 128}
        };
        for (const auto &entry : palette())
            result.insert(entry.color.rgb(), entry.value);
        return result;
    }();
    if (unknownColors)
        unknownColors->clear();
    const QImage ground = packedPixels(mainImage);
    const QImage plants = packedPixels(vegetationImage);
    QImage result(mainImage.size(), QImage::Format_ARGB32);
    if (ground.isNull() || plants.isNull() || result.isNull())
        return {};
    for (int y = 0; y < result.height(); ++y) {
        const auto *groundLine = reinterpret_cast<const QRgb *>(ground.constScanLine(y));
        const auto *plantsLine = reinterpret_cast<const QRgb *>(plants.constScanLine(y));
        QRgb *output = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb vegetation = plantsLine[x];
            const QRgb source = qAlpha(vegetation) == 0 || (vegetation & 0x00ffffff) == 0
                    ? groundLine[x] : vegetation;
            const QRgb rgb = source | 0xff000000;
            const auto it = biomeForColor.constFind(rgb);
            const int mapped = it == biomeForColor.constEnd() ? 64 : it.value();
            const int value = mapped == 171 && !useVegitationOverride ? 64 : mapped;
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
    const QImage biomes = packedPixels(biomeLayer);
    const QImage zones = packedPixels(zoneLayer);
    if (biomes.isNull() || zones.isNull() || result.isNull())
        return {};
    for (int y = 0; y < result.height(); ++y) {
        const auto *red = reinterpret_cast<const QRgb *>(biomes.constScanLine(y));
        const auto *green = reinterpret_cast<const QRgb *>(zones.constScanLine(y));
        QRgb *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int biome = qRed(red[x]);
            const int zone = qGreen(green[x]);
            output[x] = qRgba(biome, zone, 0, 255);
        }
    }
    return result;
}
BiomeMapImageProcessor::Analysis BiomeMapImageProcessor::analyze(
        const QImage &biomeLayer, const QImage &zoneLayer,
        const QPoint &worldOrigin, int chunkSize)
{
    Analysis result;
    if (biomeLayer.isNull() || zoneLayer.isNull() ||
            biomeLayer.size() != zoneLayer.size() || chunkSize <= 0)
        return result;
    const QImage biomes = packedPixels(biomeLayer);
    const QImage zones = packedPixels(zoneLayer);
    if (biomes.isNull() || zones.isNull())
        return result;
    std::array<bool, 256> usedBiomes{};
    std::array<bool, 256> usedZones{};
    for (int y = 0; y < biomes.height(); ++y) {
        const auto *red = reinterpret_cast<const QRgb *>(biomes.constScanLine(y));
        const auto *green = reinterpret_cast<const QRgb *>(zones.constScanLine(y));
        for (int x = 0; x < biomeLayer.width(); ++x) {
            usedBiomes[qRed(red[x])] = true;
            usedZones[qGreen(green[x])] = true;
        }
    }
    for (int value = 0; value < 256; ++value) {
        if (usedBiomes[value])
            result.biomeValues.insert(value);
        if (usedZones[value])
            result.zoneValues.insert(value);
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
    const auto firstChunk = [chunkSize](int coordinate) {
        return -((coordinate % chunkSize + chunkSize) % chunkSize);
    };
    const int firstColumn = firstChunk(worldOrigin.x());
    for (int top = firstChunk(worldOrigin.y()); top < zones.height(); top += chunkSize) {
        const int y = qMax(0, top);
        const int bottom = qMin(top + chunkSize, zones.height());
        const auto *firstRow = reinterpret_cast<const QRgb *>(zones.constScanLine(y));
        for (int left = firstColumn; left < zones.width(); left += chunkSize) {
            const int x = qMax(0, left);
            const int right = qMin(left + chunkSize, zones.width());
            const int first = qGreen(firstRow[x]);
            bool mixed = false;
            for (int yy = y; yy < bottom && !mixed; ++yy) {
                const auto *row = reinterpret_cast<const QRgb *>(zones.constScanLine(yy));
                for (int xx = x; xx < right; ++xx) {
                    if (qGreen(row[xx]) != first) {
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
                        "the verified Build 42.22.2 table.");
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
    const Analysis analysis = analyze(biome, zone);
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

    QImage colors(palette().size() + 3, 1, QImage::Format_ARGB32);
    QImage vegetation(colors.size(), QImage::Format_ARGB32);
    vegetation.fill(Qt::transparent);
    for (int x = 0; x < palette().size(); ++x)
        colors.setPixel(x, 0, palette().at(x).color.rgb());
    const int extra = palette().size();
    colors.setPixel(extra, 0, qRgb(179, 2, 1));
    colors.setPixel(extra + 1, 0, qRgb(90, 100, 35));
    colors.setPixel(extra + 2, 0, qRgb(0, 138, 255));
    vegetation.setPixel(extra + 2, 0, qRgba(255, 0, 0, 0));
    QSet<QRgb> unknown;
    const QImage vanilla = createBiomeLayer(colors, vegetation, &unknown);
    const QImage modded = createBiomeLayer(colors, vegetation, nullptr, true);
    for (int x = 0; x < palette().size(); ++x) {
        const auto &entry = palette().at(x);
        if (qRed(vanilla.pixel(x, 0)) != (entry.enabledByDefault ? entry.value : 64)
                || qRed(modded.pixel(x, 0)) != entry.value
                || zoneValueForName(entry.zone) < 0) {
            if (errorString)
                *errorString = QStringLiteral("Palette color or zone name did not resolve for ID %1.").arg(entry.value);
            return false;
        }
    }
    if (unknown != QSet<QRgb>{qRgb(179, 2, 1)} || qRed(vanilla.pixel(extra, 0)) != 64
            || qRed(vanilla.pixel(extra + 1, 0)) != 64 || qRed(modded.pixel(extra + 1, 0)) != 171
            || qRed(vanilla.pixel(extra + 2, 0)) != 0) {
        if (errorString)
            *errorString = QStringLiteral("Unknown, legacy grass or transparent source colors resolved incorrectly.");
        return false;
    }
    QImage encodedZones(12, 8, QImage::Format_Indexed8);
    encodedZones.setColorTable({qRgb(153, 230, 243), qRgb(64, 217, 0)});
    encodedZones.fill(0);
    QImage encodedBiomes(encodedZones.size(), QImage::Format_ARGB32);
    encodedBiomes.fill(qRgb(179, 64, 0));
    for (int y = 0; y < encodedZones.height(); ++y) {
        for (int x = 4; x < encodedZones.width(); ++x)
            encodedZones.setPixel(x, y, 1);
    }
    const QImage combined = process(encodedBiomes, encodedZones);
    if (combined.pixel(3, 7) != qRgb(179, 230, 0) || combined.pixel(4, 7) != qRgb(179, 217, 0)
            || analyze(encodedBiomes, encodedZones).mixedZoneChunks != 1
            || analyze(encodedBiomes, encodedZones, QPoint(-4, 0)).mixedZoneChunks != 0
            || zoneValueForName(QStringLiteral("DeepForest")) != 255
            || zoneValueForName(QStringLiteral("FOREST")) != 59
            || zoneValueForName(QStringLiteral("foragingnav")) != 64
            || zoneValueForName(QStringLiteral("ParkingStall")) != -1) {
        if (errorString)
            *errorString = QStringLiteral("Indexed PNG channels or world-aligned green chunks did not round-trip.");
        return false;
    }
    if (errorString)
        errorString->clear();
    return true;
}
