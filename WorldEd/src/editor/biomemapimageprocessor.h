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
    };

    struct Analysis
    {
        QSet<int> biomeValues;
        QSet<int> zoneValues;
        QSet<int> unknownBiomeValues;
        QSet<int> unknownZoneValues;
        QSet<int> biomeValuesWithoutEffect;
        int mixedZoneChunks = 0;
    };

    static QImage process(const QImage &biomeLayer,
                          const QImage &zoneLayer);
    static QImage createBiomeLayer(const QImage &mainImage,
                                   const QImage &vegetationImage,
                                   QSet<QRgb> *unknownColors = nullptr);
    static const QList<PaletteEntry> &palette();
    static QColor displayColor(int value);
    static Analysis analyze(const QImage &biomeLayer,
                            const QImage &zoneLayer,
                            int chunkSize = 8);

private:
    static const QSet<int> &knownValues();
};

#endif // BIOMEMAPIMAGEPROCESSOR_H
