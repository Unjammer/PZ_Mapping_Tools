#include "biomemapimageprocessor.h"

#include <QHash>

const QList<BiomeMapImageProcessor::PaletteEntry> &
BiomeMapImageProcessor::palette()
{
    static const QList<PaletteEntry> entries = {
        { 0,   QStringLiteral("Water"),           QColor(0, 138, 255) },
        { 64,  QStringLiteral("Foraging Nav"),    QColor(100, 100, 100) },
        { 102, QStringLiteral("Trailer Park"),    QColor(210, 200, 160) },
        { 115, QStringLiteral("Town Zone"),       QColor(165, 160, 140) },
        { 128, QStringLiteral("Farm"),             QColor(255, 128, 0) },
        { 141, QStringLiteral("Farmland"),         QColor(120, 70, 20) },
        { 153, QStringLiteral("PH Forest"),        QColor(64, 0, 0) },
        { 171, QStringLiteral("Vegetation"),       QColor(145, 135, 60) },
        { 192, QStringLiteral("Farm Mix Forest"),  QColor(255, 0, 255) },
        { 204, QStringLiteral("Farm Forest"),      QColor(0, 255, 0) },
        { 255, QStringLiteral("Deep Forest"),      QColor(127, 0, 0) }
    };
    return entries;
}

QColor BiomeMapImageProcessor::displayColor(int value)
{
    for (const PaletteEntry &entry : palette()) {
        if (entry.value == value)
            return entry.color;
    }
    return QColor(value, value, value);
}

const QSet<int> &BiomeMapImageProcessor::knownValues()
{
    static const QSet<int> values = {
        0, 59, 64, 79, 96, 102, 115, 128, 141, 153, 179, 192,
        204, 217, 230, 243, 254, 255
    };
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
    static const QSet<int> noBiomeOrOre = { 0, 64 };
    result.biomeValuesWithoutEffect = result.biomeValues & noBiomeOrOre;

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
