#ifndef BIOMEMAPIMAGEPROCESSOR_H
#define BIOMEMAPIMAGEPROCESSOR_H

#include <QColor>
#include <QImage>
#include <QList>
#include <QSet>
#include <QString>

class BiomeMapImageProcessor
{
public:
    struct PaletteEntry
    {
        int value;
        QString name;
        QColor color;
        QString biome;
        QString ore;
        QString zone;
        bool enabledByDefault;
    };

    struct Analysis
    {
        QSet<int> biomeValues;
        QSet<int> zoneValues;
        QSet<int> unknownBiomeValues;
        QSet<int> unknownZoneValues;
        QSet<int> biomeValuesWithoutEffect;
        QSet<int> valuesRequiringOverride;
        int mixedZoneChunks = 0;
    };

    static QImage process(const QImage &biomeLayer,
                          const QImage &zoneLayer);
    static QImage createBiomeLayer(const QImage &mainImage,
                                   const QImage &vegetationImage,
                                   QSet<QRgb> *unknownColors = nullptr);
    static const QList<PaletteEntry> &palette();
    static const PaletteEntry *entryForValue(int value);
    static QString oreSelectorDescription(const QString &ore);
    static QColor displayColor(int value);
    static Analysis analyze(const QImage &biomeLayer,
                            const QImage &zoneLayer,
                            int chunkSize = 8);
    static bool validateConfiguration(QString *errorString);

private:
    static const QSet<int> &knownValues();
    static const QSet<int> &overrideValues();
};

#endif // BIOMEMAPIMAGEPROCESSOR_H
