#include "biomemapgeneratordialog.h"
#include "ui_biomemapgeneratordialog.h"

#include "biomemapimageprocessor.h"
#include "generatelotsdialog.h"
#include "world.h"
#include "worldcell.h"
#include "../portablesettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

static QString valuesText(const QSet<int> &values)
{
    QList<int> sorted = values.values();
    std::sort(sorted.begin(), sorted.end());
    QStringList text;
    for (int value : std::as_const(sorted))
        text += QString::number(value);
    return text.join(QLatin1String(", "));
}

static QString colorsText(const QSet<QRgb> &colors)
{
    QStringList result;
    for (QRgb color : colors)
        result += QStringLiteral("#%1%2%3")
                .arg(qRed(color), 2, 16, QLatin1Char('0'))
                .arg(qGreen(color), 2, 16, QLatin1Char('0'))
                .arg(qBlue(color), 2, 16, QLatin1Char('0'));
    result.sort();
    return result.join(QLatin1String(", "));
}

static int zoneId(const QString &type)
{
    static const QHash<QString, int> ids = {
        {QStringLiteral("forest"), 59},
        {QStringLiteral("trailerpark"), 102}, {QStringLiteral("townzone"), 115},
        {QStringLiteral("farm"), 128}, {QStringLiteral("farmland"), 141},
        {QStringLiteral("vegitation"), 171},
        {QStringLiteral("deepforest"), 255}
    };
    return ids.value(type.toLower(), -1);
}

static void appendUnique(QStringList *values, const QString &value)
{
    if (values && !values->contains(value, Qt::CaseInsensitive))
        *values += value;
}

BiomeMapGeneratorDialog::BiomeMapGeneratorDialog(World *world, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::BiomeMapGeneratorDialog)
    , mWorld(world)
{
    ui->setupUi(this);

    if (mWorld) {
        const GenerateLotsSettings &settings = mWorld->getGenerateLotsSettings();
        ui->xOrigin->setValue(settings.worldOrigin.x());
        ui->yOrigin->setValue(settings.worldOrigin.y());

        if (!settings.biomeMap.isEmpty()) {
            ui->outputDirectory->setText(
                        QFileInfo(settings.biomeMap).absolutePath());
        } else {
            const QString exportRoot = settings.exportDir.isEmpty()
                    ? PortableSettings::installRootPath()
                    : settings.exportDir;
            ui->outputDirectory->setText(
                        QDir::cleanPath(QDir(exportRoot).filePath(
                                            QLatin1String("maps"))));
        }
    }

    connect(ui->browseMainImage, &QToolButton::clicked,
            this, &BiomeMapGeneratorDialog::browseMainImage);
    connect(ui->browseVegetationImage, &QToolButton::clicked,
            this, &BiomeMapGeneratorDialog::browseVegetationImage);
    connect(ui->browseZoneImage, &QToolButton::clicked,
            this, &BiomeMapGeneratorDialog::browseZoneImage);
    connect(ui->zonesFromWorld, &QRadioButton::toggled,
            this, &BiomeMapGeneratorDialog::updateZoneSource);
    connect(ui->browseOutputDirectory, &QToolButton::clicked,
            this, &BiomeMapGeneratorDialog::browseOutputDirectory);
    connect(ui->browseFallbackDirectory, &QToolButton::clicked,
            this, &BiomeMapGeneratorDialog::browseFallbackDirectory);
    connect(ui->configurationReferenceButton, &QPushButton::clicked,
            this, &BiomeMapGeneratorDialog::showConfigurationReference);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &BiomeMapGeneratorDialog::generate);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(mWorld != nullptr);
    updateZoneSource();
}

BiomeMapGeneratorDialog::~BiomeMapGeneratorDialog()
{
    delete ui;
}

QString BiomeMapGeneratorDialog::initialInputDirectory() const
{
    if (mWorld) {
        const QString exportDirectory = mWorld->getGenerateLotsSettings().exportDir;
        if (!exportDirectory.isEmpty())
            return QDir(exportDirectory).absoluteFilePath(QLatin1String(".."));
    }
    return PortableSettings::installRootPath();
}

void BiomeMapGeneratorDialog::browseMainImage()
{
    const QString filePath = QFileDialog::getOpenFileName(
                this, tr("Select Map.png"), initialInputDirectory(),
                tr("PNG images (*.png)"));
    if (!filePath.isEmpty())
        ui->mainImagePath->setText(QDir::toNativeSeparators(filePath));
}

void BiomeMapGeneratorDialog::browseVegetationImage()
{
    const QString filePath = QFileDialog::getOpenFileName(
                this, tr("Select Map_veg.png"), initialInputDirectory(),
                tr("PNG images (*.png)"));
    if (!filePath.isEmpty())
        ui->vegetationImagePath->setText(QDir::toNativeSeparators(filePath));
}

void BiomeMapGeneratorDialog::browseZoneImage()
{
    const QString filePath = QFileDialog::getOpenFileName(
                this, tr("Select Zone Data PNG"), initialInputDirectory(),
                tr("PNG images (*.png)"));
    if (!filePath.isEmpty())
        ui->zoneImagePath->setText(QDir::toNativeSeparators(filePath));
}

void BiomeMapGeneratorDialog::updateZoneSource()
{
    const bool fromPng = ui->zonesFromPng->isChecked();
    ui->zoneImagePath->setEnabled(fromPng);
    ui->browseZoneImage->setEnabled(fromPng);
}

void BiomeMapGeneratorDialog::browseOutputDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Select Output Directory"),
                ui->outputDirectory->text().trimmed());
    if (!directory.isEmpty())
        ui->outputDirectory->setText(QDir::toNativeSeparators(directory));
}

void BiomeMapGeneratorDialog::browseFallbackDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Select Base Biomemap Directory"),
                ui->fallbackDirectory->text().trimmed().isEmpty()
                    ? ui->outputDirectory->text().trimmed()
                    : ui->fallbackDirectory->text().trimmed());
    if (!directory.isEmpty())
        ui->fallbackDirectory->setText(QDir::toNativeSeparators(directory));
}

void BiomeMapGeneratorDialog::showConfigurationReference()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Build 42.20 BiomeMapConfig Reference"));
    dialog.resize(980, 610);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *description = new QLabel(
                tr("<b>Each pixel stores two independent identifiers.</b> "
                   "Red selects the biome and optional ore selector. Green selects "
                   "the foraging zone. Both channels resolve through the "
                   "same Build 42.20 <tt>biome_map_config</tt> table.<br/>"
                   "<b>map_forest</b> selects procedural surface deposits "
                   "of boulders, limestone, or flint. Ore noise selects the "
                   "density from none through very high. It does not select "
                   "iron or copper, which use separate vein generation.<br/>"
                   "ID 171 is not active in the Vanilla table. It works only "
                   "when the map adds the documented WorldGenOverride.lua "
                   "entry."), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    const QList<BiomeMapImageProcessor::PaletteEntry> &entries =
            BiomeMapImageProcessor::palette();
    QTableWidget *table = new QTableWidget(entries.size(), 6, &dialog);
    table->setHorizontalHeaderLabels(
                QStringList() << tr("Pixel") << tr("Palette")
                << tr("Biome") << tr("Ore selector") << tr("Zone")
                << tr("Availability"));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);

    for (int row = 0; row < entries.size(); ++row) {
        const BiomeMapImageProcessor::PaletteEntry &entry = entries.at(row);
        QTableWidgetItem *pixel =
                new QTableWidgetItem(QString::number(entry.value));
        pixel->setData(Qt::UserRole, entry.value);
        pixel->setBackground(entry.color);
        pixel->setForeground(
                    entry.color.lightness() < 128 ? Qt::white : Qt::black);
        table->setItem(row, 0, pixel);
        table->setItem(row, 1, new QTableWidgetItem(entry.name));
        table->setItem(row, 2, new QTableWidgetItem(
                           entry.biome.isEmpty() ? tr("(none)") : entry.biome));
        table->setItem(row, 3, new QTableWidgetItem(
                           entry.ore.isEmpty() ? tr("(none)") : entry.ore));
        table->item(row, 3)->setToolTip(
                    BiomeMapImageProcessor::oreSelectorDescription(
                        entry.ore));
        table->setItem(row, 4, new QTableWidgetItem(entry.zone));
        table->setItem(
                    row, 5,
                    new QTableWidgetItem(
                        entry.enabledByDefault
                        ? tr("Vanilla Build 42.20")
                        : tr("Map override required")));
    }
    table->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
                2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(
                3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(
                4, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(
                5, QHeaderView::ResizeToContents);
    layout->addWidget(table);

    QDialogButtonBox *buttons =
            new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected,
            &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void BiomeMapGeneratorDialog::generate()
{
    const QString mainImagePath = ui->mainImagePath->text().trimmed();
    const QString vegetationImagePath = ui->vegetationImagePath->text().trimmed();
    const QString zoneImagePath = ui->zoneImagePath->text().trimmed();
    const QString outputPath = ui->outputDirectory->text().trimmed();

    if (mainImagePath.isEmpty() || vegetationImagePath.isEmpty() || outputPath.isEmpty() ||
            (ui->zonesFromPng->isChecked() && zoneImagePath.isEmpty())) {
        QMessageBox::warning(this, tr("Missing Input"),
                             tr("Specify Map.png, Map_veg.png, the selected zone source, and an output directory."));
        return;
    }

    const QImage mainImage(mainImagePath);
    const QImage vegetationImage(vegetationImagePath);
    if (mainImage.isNull() || vegetationImage.isNull()) {
        QMessageBox::warning(this, tr("Invalid Images"),
                             tr("One or both input images could not be loaded."));
        return;
    }
    if (mainImage.size() != vegetationImage.size()) {
        QMessageBox::warning(this, tr("Size Mismatch"),
                             tr("The input images must have identical dimensions."));
        return;
    }
    const int projectCellSize = mWorld->cellSize();
    if ((mainImage.width() % projectCellSize) != 0 ||
            (mainImage.height() % projectCellSize) != 0) {
        QMessageBox::warning(this, tr("Invalid Biomemap Size"),
                             tr("This project uses a %1 x %1 cell grid. "
                                "The data layers must be exact multiples of %1 pixels "
                                "in both dimensions.")
                             .arg(projectCellSize));
        return;
    }
    if (ui->zonesFromWorld->isChecked() &&
            mainImage.size() != mWorld->size() * projectCellSize) {
        const QSize expectedSize = mWorld->size() * projectCellSize;
        QMessageBox::warning(
                    this, tr("Biomemap Size Does Not Match Project"),
                    tr("Rasterizing zones from WorldEd requires images covering the "
                       "complete project.\n\n"
                       "Project grid: %1 x %1\n"
                       "Expected image size: %2 x %3 pixels\n"
                       "Actual image size: %4 x %5 pixels")
                    .arg(projectCellSize)
                    .arg(expectedSize.width())
                    .arg(expectedSize.height())
                    .arg(mainImage.width())
                    .arg(mainImage.height()));
        return;
    }

    QSet<QRgb> unknownColors;
    const QImage biomeLayer = BiomeMapImageProcessor::createBiomeLayer(
                mainImage, vegetationImage, &unknownColors);
    QStringList rasterizedZoneTypes;
    QStringList objectsLuaTypes;
    const QImage zoneLayer = ui->zonesFromWorld->isChecked()
            ? createZoneLayer(mainImage.size(), &rasterizedZoneTypes,
                              &objectsLuaTypes)
            : QImage(zoneImagePath);
    if (zoneLayer.isNull() || zoneLayer.size() != mainImage.size()) {
        QMessageBox::warning(this, tr("Invalid Zone Layer"),
                             tr("The zone layer must exist and have the same dimensions as Map.png."));
        return;
    }

    const BiomeMapImageProcessor::Analysis analysis =
            BiomeMapImageProcessor::analyze(biomeLayer, zoneLayer);
    QStringList warnings;
    if (!unknownColors.isEmpty())
        warnings += tr("These source colors have no biome mapping: %1. "
                       "WorldEd used each color's red component as a fallback "
                       "biome ID. Repaint them with a supported terrain or "
                       "vegetation color to avoid unintended biome IDs.")
                .arg(colorsText(unknownColors));
    if (!analysis.unknownBiomeValues.isEmpty())
        warnings += tr("Unknown biome IDs in the red layer: %1")
                .arg(valuesText(analysis.unknownBiomeValues));
    if (!analysis.biomeValuesWithoutEffect.isEmpty())
        warnings += tr("Red IDs with no biome or ore effect: %1")
                .arg(valuesText(analysis.biomeValuesWithoutEffect));
    if (!analysis.valuesRequiringOverride.isEmpty()) {
        warnings += tr("BiomeMap ID(s) requiring a map-specific "
                       "WorldGenOverride.lua entry: %1. Vanilla Build 42.20 "
                       "leaves ID 171 disabled, so it has no biome or zone "
                       "definition unless the map enables it.")
                .arg(valuesText(analysis.valuesRequiringOverride));
    }
    if (!analysis.unknownZoneValues.isEmpty()) {
        QMessageBox::critical(this, tr("Invalid Foraging Zone IDs"),
                              tr("The green layer contains IDs with no zone mapping: %1\n\n"
                                 "Generation was stopped because Project Zomboid cannot interpret them.")
                              .arg(valuesText(analysis.unknownZoneValues)));
        return;
    }
    if (analysis.mixedZoneChunks > 0)
        warnings += tr("%1 chunk(s) contain more than one green zone ID. "
                       "Project Zomboid evaluates zones at 8 x 8 chunk granularity, "
                       "so these zones may overlap.").arg(analysis.mixedZoneChunks);
    if (!warnings.isEmpty()) {
        warnings += tr("The exact input bytes will be preserved. Continue anyway?");
        if (QMessageBox::warning(this, tr("Biomemap Validation Warnings"),
                                 warnings.join(QLatin1String("\n\n")),
                                 QMessageBox::Ok | QMessageBox::Cancel,
                                 QMessageBox::Cancel) != QMessageBox::Ok)
            return;
    }

    QDir outputDirectory(outputPath);
    if (!outputDirectory.exists() && !QDir().mkpath(outputPath)) {
        QMessageBox::warning(this, tr("Output Error"),
                             tr("The output directory could not be created."));
        return;
    }
    outputDirectory.setPath(outputPath);

    const QImage biomeMap = BiomeMapImageProcessor::process(biomeLayer, zoneLayer);
    if (biomeMap.isNull()) {
        QMessageBox::warning(this, tr("Generation Failed"),
                             tr("The biome map could not be generated."));
        return;
    }

    const QString biomeMapFile = outputDirectory.filePath(
                QLatin1String("biome.png"));
    if (!biomeMap.save(biomeMapFile, "PNG")) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("Could not save %1.")
                             .arg(QDir::toNativeSeparators(biomeMapFile)));
        return;
    }

    int tileCount = 0;
    QString failedFile;
    QStringList neutralFallbackTiles;
    if (!saveTiles(biomeMap, outputDirectory.absolutePath(),
                   ui->fallbackDirectory->text().trimmed(),
                   QPoint(ui->xOrigin->value(), ui->yOrigin->value()),
                   projectCellSize, &tileCount, &failedFile,
                   &neutralFallbackTiles)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("Could not save tile %1.")
                             .arg(QDir::toNativeSeparators(failedFile)));
        return;
    }

    mGeneratedBiomeMapFile = QDir::cleanPath(biomeMapFile);
    QString resultMessage = tr("Saved biome.png and %1 complete 256 x 256 map tile(s) in:\n%2")
            .arg(tileCount)
            .arg(QDir::toNativeSeparators(outputDirectory.absolutePath()));
    if (ui->zonesFromWorld->isChecked()) {
        resultMessage += tr("\n\nGreen-channel zone types rasterized from this "
                            "project:\n%1")
                .arg(rasterizedZoneTypes.isEmpty()
                     ? tr("(none found)")
                     : rasterizedZoneTypes.join(QLatin1String(", ")));
        resultMessage += tr("\n\nVector object types excluded from the image; "
                            "export these through objects.lua:\n%1")
                .arg(objectsLuaTypes.isEmpty()
                     ? tr("(none found)")
                     : objectsLuaTypes.join(QLatin1String(", ")));
    } else {
        resultMessage += tr("\n\nThe selected PNG supplied the green channel. "
                            "Vegitation, DeepForest, Forest, TownZone, Farm, "
                            "FarmLand and TrailerPark belong in that image. "
                            "Export every other vector zone/object type through "
                            "objects.lua.");
    }
    if (!neutralFallbackTiles.isEmpty()) {
        resultMessage += tr("\n\n%1 boundary tile(s) had no full-size base tile. "
                            "Their uncovered pixels were filled with neutral "
                            "ForagingNav data (red 64, green 64):\n%2\n\n"
                            "For a map over Vanilla, select the Vanilla maps "
                            "directory as the base and generate again.")
                .arg(neutralFallbackTiles.size())
                .arg(neutralFallbackTiles.join(QLatin1String(", ")));
    }
    QMessageBox::information(this, tr("Biome Map Generated"), resultMessage);
    accept();
}

QImage BiomeMapGeneratorDialog::createZoneLayer(
        const QSize &size, QStringList *rasterizedTypes,
        QStringList *objectsLuaTypes) const
{
    if (!mWorld || size != mWorld->size() * mWorld->cellSize())
        return QImage();
    if (rasterizedTypes)
        rasterizedTypes->clear();
    if (objectsLuaTypes)
        objectsLuaTypes->clear();

    QImage image(size, QImage::Format_ARGB32);
    image.fill(qRgb(64, 64, 64));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const int cellSize = mWorld->cellSize();
    for (WorldCell *cell : mWorld->cells()) {
        const QPointF offset(cell->x() * cellSize, cell->y() * cellSize);
        for (WorldCellObject *object : cell->objects()) {
            if (!object->type())
                continue;
            const QString type = object->type()->name();
            const int id = zoneId(type);
            if (id < 0) {
                appendUnique(objectsLuaTypes, type);
                continue;
            }
            appendUnique(rasterizedTypes, type);
            const QColor color(id, id, id);
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            if (object->isRectangle()) {
                painter.drawRect(QRectF(offset + object->pos(), object->size()));
            } else {
                QPolygonF polygon;
                for (const WorldCellObjectPoint &point : object->points())
                    polygon << offset + QPointF(point.x, point.y);
                if (object->isPolygon()) {
                    painter.drawPolygon(polygon);
                } else if (object->isPolyline() && polygon.size() > 1) {
                    QPen pen(color, qMax(1, object->polylineWidth()), Qt::SolidLine,
                             Qt::FlatCap, Qt::MiterJoin);
                    painter.setPen(pen);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPolyline(polygon);
                }
            }
        }
    }
    painter.end();
    if (rasterizedTypes)
        rasterizedTypes->sort(Qt::CaseInsensitive);
    if (objectsLuaTypes)
        objectsLuaTypes->sort(Qt::CaseInsensitive);
    return image;
}

bool BiomeMapGeneratorDialog::saveTiles(const QImage &image,
                                        const QString &outputDirectory,
                                        const QString &fallbackDirectory,
                                        const QPoint &projectOrigin,
                                        int projectCellSize,
                                        int *tileCount,
                                        QString *failedFile,
                                        QStringList *neutralFallbackTiles) const
{
    const int tileSize = 256;
    const QDir outputDir(outputDirectory);
    const QDir fallbackDir(fallbackDirectory);
    const QPoint sourceWorldOrigin(projectOrigin.x() * projectCellSize,
                                   projectOrigin.y() * projectCellSize);
    const QRect sourceWorldRect(sourceWorldOrigin, image.size());
    const int firstTileX = int(std::floor(sourceWorldRect.left()
                                          / double(tileSize)));
    const int firstTileY = int(std::floor(sourceWorldRect.top()
                                          / double(tileSize)));
    const int lastTileX = int(std::floor(sourceWorldRect.right()
                                         / double(tileSize)));
    const int lastTileY = int(std::floor(sourceWorldRect.bottom()
                                         / double(tileSize)));

    if (tileCount)
        *tileCount = 0;
    if (neutralFallbackTiles)
        neutralFallbackTiles->clear();

    for (int tileY = firstTileY; tileY <= lastTileY; ++tileY) {
        for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
            const QString fileName = QStringLiteral("biomemap_%1_%2.png")
                    .arg(tileX)
                    .arg(tileY);
            const QString filePath = outputDir.filePath(fileName);
            const QRect tileWorldRect(tileX * tileSize, tileY * tileSize,
                                      tileSize, tileSize);
            const QRect coveredWorldRect =
                    tileWorldRect.intersected(sourceWorldRect);

            QImage tile(tileSize, tileSize, QImage::Format_ARGB32);
            bool hasBaseTile = false;
            QString baseFilePath;
            if (!fallbackDirectory.isEmpty())
                baseFilePath = fallbackDir.filePath(fileName);
            if ((baseFilePath.isEmpty() || !QFileInfo::exists(baseFilePath))
                    && QFileInfo::exists(filePath)) {
                baseFilePath = filePath;
            }
            if (!baseFilePath.isEmpty()) {
                const QImage baseTile(baseFilePath);
                if (!baseTile.isNull()
                        && baseTile.size() == QSize(tileSize, tileSize)) {
                    tile = baseTile.convertToFormat(QImage::Format_ARGB32);
                    hasBaseTile = true;
                }
            }
            if (!hasBaseTile)
                tile.fill(qRgba(64, 64, 0, 255));

            if (coveredWorldRect != tileWorldRect && !hasBaseTile
                    && neutralFallbackTiles) {
                *neutralFallbackTiles += fileName;
            }

            const QRect sourceRect =
                    coveredWorldRect.translated(-sourceWorldOrigin);
            const QPoint destination =
                    coveredWorldRect.topLeft() - tileWorldRect.topLeft();
            QPainter painter(&tile);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(destination, image, sourceRect);
            painter.end();

            if (!tile.save(filePath, "PNG")) {
                if (failedFile)
                    *failedFile = filePath;
                return false;
            }
            if (tileCount)
                ++(*tileCount);
        }
    }

    return true;
}
