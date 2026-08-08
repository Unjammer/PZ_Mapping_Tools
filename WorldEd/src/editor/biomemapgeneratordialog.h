#ifndef BIOMEMAPGENERATORDIALOG_H
#define BIOMEMAPGENERATORDIALOG_H

#include <QDialog>
#include <QSize>
#include <QStringList>

class QImage;
class World;

namespace Ui {
class BiomeMapGeneratorDialog;
}

class BiomeMapGeneratorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BiomeMapGeneratorDialog(World *world, QWidget *parent = nullptr);
    ~BiomeMapGeneratorDialog() override;
    QString generatedBiomeMapFile() const { return mGeneratedBiomeMapFile; }

private slots:
    void browseMainImage();
    void browseVegetationImage();
    void browseZoneImage();
    void updateZoneSource();
    void browseOutputDirectory();
    void browseFallbackDirectory();
    void showConfigurationReference();
    void generate();

private:
    QString initialInputDirectory() const;
    bool saveTiles(const QImage &image, const QString &outputDirectory,
                   const QString &fallbackDirectory,
                   const QPoint &projectOrigin, int projectCellSize,
                   int *tileCount, QString *failedFile,
                   QStringList *neutralFallbackTiles) const;
    QImage createZoneLayer(const QSize &size,
                           QStringList *rasterizedTypes,
                           QStringList *objectsLuaTypes) const;

    Ui::BiomeMapGeneratorDialog *ui;
    World *mWorld;
    QString mGeneratedBiomeMapFile;
};

#endif // BIOMEMAPGENERATORDIALOG_H
