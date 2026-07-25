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
    void generate();

private:
    QString initialInputDirectory() const;
    bool saveTiles(const QImage &image, const QString &outputDirectory,
                   int startX, int startY, int *tileCount,
                   QString *failedFile) const;
    QImage createZoneLayer(const QSize &size, QStringList *unknownTypes) const;

    Ui::BiomeMapGeneratorDialog *ui;
    World *mWorld;
    QString mGeneratedBiomeMapFile;
};

#endif // BIOMEMAPGENERATORDIALOG_H
