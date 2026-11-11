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
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTableWidget>
#include <QTemporaryDir>
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

static void appendUnique(QStringList *values, const QString &value)
{
    if (values && !values->contains(value, Qt::CaseInsensitive))
        *values += value;
}

struct Pixel171Override
{
    QString path;
    bool biome = false;
    bool zone = false;
};

static Pixel171Override readPixel171Override(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QString contents = QString::fromUtf8(file.readAll());
    static const QRegularExpression comments(
                QStringLiteral("--(?:\\[(=*)\\[[\\s\\S]*?\\]\\1\\]|[^\\r\\n]*)"));
    static const QRegularExpression declaration(
                QStringLiteral("\\bbiome_map_config\\s*=\\s*\\{((?:[^{}]|\\{[^{}]*\\})*)\\}"));
    static const QRegularExpression insertion(
                QStringLiteral("\\btable\\s*\\.\\s*insert\\s*\\(\\s*biome_map_config\\s*,\\s*(\\{[^{}]*\\})\\s*\\)"));
    static const QRegularExpression entry(QStringLiteral("\\{([^{}]*)\\}"));
    static const QRegularExpression pixel(QStringLiteral("\\bpixel\\s*=\\s*171(?=\\s*(?:[,;]|$))"));
    static const QRegularExpression biome(
                QStringLiteral("\\bbiome\\s*=\\s*(?:\"vegitation\"|'vegitation')(?=\\s*(?:[,;]|$))"));
    static const QRegularExpression zone(
                QStringLiteral("\\bzone\\s*=\\s*(?:\"Vegitation\"|'Vegitation')(?=\\s*(?:[,;]|$))"));
    contents.replace(comments, QStringLiteral(" "));
    Pixel171Override result;
    for (const auto *statement : {&declaration, &insertion}) {
        auto statements = statement->globalMatch(contents);
        while (statements.hasNext()) {
            auto entries = entry.globalMatch(statements.next().captured(1));
            while (entries.hasNext()) {
                const QString fields = entries.next().captured(1);
                if (fields.contains(pixel)) {
                    result.path = filePath;
                    result.biome = fields.contains(biome);
                    result.zone = fields.contains(zone);
                }
            }
        }
    }
    return result;
}

static Pixel171Override detectedPixel171Override(const QString &projectFilePath,
                                                  const QString &outputDirectory)
{
    if (outputDirectory.isEmpty())
        return {};
    QStringList candidates;
    const QDir outputDir(outputDirectory);
    candidates += outputDir.filePath(QLatin1String("WorldGenOverride.lua"));
    if (QFileInfo(outputDir.absolutePath()).fileName().compare(
                QLatin1String("maps"), Qt::CaseInsensitive) == 0) {
        candidates += QDir(outputDir.absolutePath())
                .filePath(QLatin1String("../WorldGenOverride.lua"));
    }

    if (!projectFilePath.isEmpty()) {
        QDir projectDir(QFileInfo(projectFilePath).absolutePath());
        candidates += projectDir.filePath(
                    QLatin1String("WorldGenOverride.lua"));

        QDir outputMapDir(outputDir.absolutePath());
        if (outputMapDir.dirName().compare(
                    QLatin1String("maps"), Qt::CaseInsensitive) == 0) {
            outputMapDir.cdUp();
        }
        const QString mapName = outputMapDir.dirName();
        for (int depth = 0; depth < 5; ++depth) {
            if (!mapName.isEmpty()) {
                candidates += projectDir.filePath(
                            QStringLiteral("media/maps/%1/WorldGenOverride.lua")
                            .arg(mapName));
            }
            if (!projectDir.cdUp())
                break;
        }
    }

    QSet<QString> checked;
    for (const QString &candidate : std::as_const(candidates)) {
        const QString cleanPath = QDir::cleanPath(
                    QFileInfo(candidate).absoluteFilePath());
        const QString key = cleanPath.toLower();
        if (checked.contains(key))
            continue;
        checked.insert(key);
        const auto detected = readPixel171Override(cleanPath);
        if (!detected.path.isEmpty())
            return detected;
    }
    return {};
}

BiomeMapGeneratorDialog::BiomeMapGeneratorDialog(
        World *world, const QString &projectFilePath, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::BiomeMapGeneratorDialog)
    , mWorld(world)
    , mProjectFilePath(projectFilePath)
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
    connect(ui->unzonedFallback,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateUnzonedFallback(); });
    connect(ui->outputDirectory, &QLineEdit::textChanged,
            this, [this](const QString &) { updateUnzonedFallback(); });
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
    ui->unzonedFallbackLabel->setEnabled(!fromPng);
    ui->unzonedFallback->setEnabled(!fromPng);
    ui->unzonedFallbackStatus->setEnabled(!fromPng);
    updateUnzonedFallback();
}

int BiomeMapGeneratorDialog::effectiveUnzonedFallback(
        QString *overridePath, bool *biomeOverride) const
{
    const auto detected = detectedPixel171Override(
                mProjectFilePath, ui->outputDirectory->text().trimmed());
    if (overridePath)
        *overridePath = detected.zone ? detected.path : QString();
    if (biomeOverride)
        *biomeOverride = detected.biome;
    if (ui->unzonedFallback->currentIndex() == 1)
        return 64;
    if (ui->unzonedFallback->currentIndex() == 2)
        return 171;
    return detected.zone ? 171 : 64;
}

void BiomeMapGeneratorDialog::updateUnzonedFallback()
{
    if (ui->zonesFromPng->isChecked()) {
        ui->unzonedFallbackStatus->setText(
                    tr("The selected PNG supplies every green-channel value."));
        return;
    }

    QString overridePath;
    const int value = effectiveUnzonedFallback(&overridePath);
    if (ui->unzonedFallback->currentIndex() == 0) {
        if (value == 171) {
            ui->unzonedFallbackStatus->setText(
                        tr("Automatic selected green 171 (Vegitation). "
                           "A literal pixel 171 zone entry was found in:\n%1")
                        .arg(QDir::toNativeSeparators(overridePath)));
        } else {
            ui->unzonedFallbackStatus->setText(
                        tr("Automatic selected green 64 (ForagingNav). "
                           "No matching pixel 171 WorldGenOverride.lua zone entry "
                           "was detected for this output."));
        }
    } else if (value == 171 && overridePath.isEmpty()) {
        ui->unzonedFallbackStatus->setText(
                    tr("Green 171 was selected explicitly, but no active "
                       "pixel 171 WorldGenOverride.lua zone entry was detected. "
                       "Vanilla Build 42.22.2 leaves this zone undefined."));
    } else if (value == 171) {
        ui->unzonedFallbackStatus->setText(
                    tr("Green 171 (Vegitation) is enabled by:\n%1")
                    .arg(QDir::toNativeSeparators(overridePath)));
    } else {
        ui->unzonedFallbackStatus->setText(
                    tr("Green 64 (ForagingNav) is active in Vanilla Build "
                       "42.22.2 and requires no map override."));
    }
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
    dialog.setWindowTitle(tr("Build 42.22.2 BiomeMapConfig Reference"));
    dialog.resize(980, 610);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *description = new QLabel(
                tr("<b>Each pixel stores two independent identifiers.</b> "
                   "Red selects the biome and optional ore selector. Green selects "
                   "the foraging zone. Both channels resolve through the "
                   "same Build 42.22.2 <tt>biome_map_config</tt> table.<br/>"
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
                        ? tr("Vanilla Build 42.22.2")
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

    QString overridePath;
    bool biomeOverride = false;
    const int unzonedFallback = effectiveUnzonedFallback(&overridePath, &biomeOverride);
    const bool useVegitationZone = !overridePath.isEmpty() || unzonedFallback == 171;
    QSet<QRgb> unknownColors;
    const QImage biomeLayer = BiomeMapImageProcessor::createBiomeLayer(
                mainImage, vegetationImage, &unknownColors, biomeOverride);
    QStringList rasterizedZoneTypes;
    QStringList objectsLuaTypes;
    const QImage zoneLayer = ui->zonesFromWorld->isChecked()
            ? createZoneLayer(mainImage.size(), unzonedFallback, useVegitationZone,
                              &rasterizedZoneTypes, &objectsLuaTypes)
            : QImage(zoneImagePath);
    if (zoneLayer.isNull() || zoneLayer.size() != mainImage.size()) {
        QMessageBox::warning(this, tr("Invalid Zone Layer"),
                             tr("The zone layer must exist and have the same dimensions as Map.png."));
        return;
    }

    const QPoint origin(ui->xOrigin->value(), ui->yOrigin->value());
    const BiomeMapImageProcessor::Analysis analysis =
            BiomeMapImageProcessor::analyze(biomeLayer, zoneLayer, origin * projectCellSize);
    QStringList warnings;
    if (!unknownColors.isEmpty())
        warnings += tr("These source colors have no biome mapping: %1. "
                       "WorldEd used neutral red ID 64 for these pixels.")
                .arg(colorsText(unknownColors));
    if (!analysis.unknownBiomeValues.isEmpty())
        warnings += tr("Unknown biome IDs in the red layer: %1")
                .arg(valuesText(analysis.unknownBiomeValues));
    if (analysis.zoneValues.contains(171) && overridePath.isEmpty()) {
        warnings += tr("Green ID 171 requires a Vegitation zone entry in "
                       "the map's WorldGenOverride.lua. Vanilla Build 42.22.2 "
                       "leaves this ID disabled.");
    }
    if (!analysis.unknownZoneValues.isEmpty()) {
        QMessageBox::critical(this, tr("Invalid Foraging Zone IDs"),
                              tr("The green layer contains IDs with no zone mapping: %1\n\n"
                                 "Generation was stopped because Project Zomboid cannot interpret them.")
                              .arg(valuesText(analysis.unknownZoneValues)));
        return;
    }
    if (!warnings.isEmpty()) {
        warnings += tr("Continue with these biome and zone values?");
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
    QSaveFile mergedFile(biomeMapFile);
    if (!mergedFile.open(QIODevice::WriteOnly) || !biomeMap.save(&mergedFile, "PNG")
            || !mergedFile.commit()) {
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
                   origin,
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
    if (analysis.mixedZoneChunks > 0) {
        resultMessage += tr("\n\n%1 world-aligned 8 x 8 chunk(s) contain more than one green ID. "
                            "The game evaluates each distinct ID in those chunks.")
                .arg(analysis.mixedZoneChunks);
    }
    if (ui->zonesFromWorld->isChecked()) {
        resultMessage += tr("\n\nUnzoned terrain uses green %1 (%2).")
                .arg(unzonedFallback)
                .arg(unzonedFallback == 171
                     ? tr("Vegitation, map override")
                     : tr("ForagingNav, Vanilla-safe"));
        resultMessage += tr("\n\nGreen-channel zone types rasterized from this "
                            "project:\n%1")
                .arg(rasterizedZoneTypes.isEmpty()
                     ? tr("(none found)")
                     : rasterizedZoneTypes.join(QLatin1String(", ")));
        if (!useVegitationZone && rasterizedZoneTypes.contains(
                    QStringLiteral("Vegitation"), Qt::CaseInsensitive)) {
            resultMessage += tr("\n\nVegitation zones were written as green 64 "
                                "because no matching zone override was detected.");
        }
        resultMessage += tr("\n\nVector object types excluded from the image; "
                            "export these through objects.lua:\n%1")
                .arg(objectsLuaTypes.isEmpty()
                     ? tr("(none found)")
                     : objectsLuaTypes.join(QLatin1String(", ")));
    } else {
        resultMessage += tr("\n\nThe selected PNG supplied the green channel. "
                            "The BiomeMapConfig reference lists the supported zone IDs.");
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
        const QSize &size, int unzonedFallbackId, bool useVegitationZone,
        QStringList *rasterizedTypes, QStringList *objectsLuaTypes) const
{
    if (!mWorld || size != mWorld->size() * mWorld->cellSize())
        return QImage();
    if (rasterizedTypes)
        rasterizedTypes->clear();
    if (objectsLuaTypes)
        objectsLuaTypes->clear();

    QImage image(size, QImage::Format_ARGB32);
    const int fallback = qBound(0, unzonedFallbackId, 255);
    image.fill(qRgb(fallback, fallback, fallback));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const int cellSize = mWorld->cellSize();
    for (WorldCell *cell : mWorld->cells()) {
        const QPointF offset(cell->x() * cellSize, cell->y() * cellSize);
        for (WorldCellObject *object : cell->objects()) {
            if (!object->type())
                continue;
            const QString type = object->type()->name();
            int id = BiomeMapImageProcessor::zoneValueForName(type);
            if (id < 0) {
                appendUnique(objectsLuaTypes, type);
                continue;
            }
            appendUnique(rasterizedTypes, type);
            if (id == 171 && !useVegitationZone)
                id = 64;
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

bool BiomeMapGeneratorDialog::validateFallbackBehavior(QString *summary,
                                                       QString *error)
{
    if (BiomeMapImageProcessor::zoneValueForName(QStringLiteral("Water")) != 0) {
        if (error)
            *error = QStringLiteral("Water zones do not map to green-channel value 0.");
        return false;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create the fallback test directory.");
        return false;
    }

    const QString projectPath = QDir(temporary.path())
            .filePath(QLatin1String("FallbackTest.pzw"));
    const QString mapsPath = QDir(temporary.path())
            .filePath(QLatin1String("media/maps/FallbackTest/maps"));
    if (!QDir().mkpath(mapsPath)) {
        if (error)
            *error = QStringLiteral("Could not create the fallback fixture path.");
        return false;
    }

    World world(1, 1, WorldGridFormat::Native256);
    BiomeMapGeneratorDialog dialog(&world, projectPath);
    dialog.ui->zonesFromWorld->setChecked(true);
    dialog.ui->outputDirectory->setText(mapsPath);
    dialog.ui->unzonedFallback->setCurrentIndex(0);
    if (dialog.effectiveUnzonedFallback() != 64) {
        if (error)
            *error = QStringLiteral("Automatic fallback did not select Vanilla-safe green 64 without an override.");
        return false;
    }

    const QString overrideFile = QDir(mapsPath)
            .filePath(QLatin1String("../WorldGenOverride.lua"));
    const auto writeOverride = [&](const QByteArray &contents) {
        QFile file(overrideFile);
        return file.open(QIODevice::WriteOnly | QIODevice::Text) && file.write(contents) == contents.size();
    };
    bool biomeOverride = false;
    if (!writeOverride("--[=[table.insert(biome_map_config, {pixel = 171, zone = 'Vegitation'})]=]\n"
                       "unrelated = { {pixel = 171, zone = 'Vegitation'} }\n")
            || dialog.effectiveUnzonedFallback(nullptr, &biomeOverride) != 64 || biomeOverride
            || !writeOverride("table.insert(biome_map_config, {pixel = 171, biome = 'vegitation'})\n")
            || dialog.effectiveUnzonedFallback(nullptr, &biomeOverride) != 64 || !biomeOverride
            || !writeOverride("table.insert(biome_map_config, {pixel = 171, zone = 'Vegitation'})\n")
            || dialog.effectiveUnzonedFallback(nullptr, &biomeOverride) != 171 || biomeOverride
            || !writeOverride("biome_map_config = {\n { pixel = 171, biome = 'vegitation', zone = 'Vegitation' },\n}\n")) {
        if (error)
            *error = QStringLiteral("BiomeMap did not distinguish literal biome and zone overrides from unrelated or commented entries.");
        return false;
    }

    QString detectedPath;
    if (dialog.effectiveUnzonedFallback(&detectedPath) != 171 ||
            QDir::cleanPath(detectedPath) != QDir::cleanPath(overrideFile)) {
        if (error)
            *error = QStringLiteral("Automatic fallback did not detect the active pixel 171 override.");
        return false;
    }

    const QImage overrideZone = dialog.createZoneLayer(
                QSize(256, 256), 171, true, nullptr, nullptr);
    dialog.ui->unzonedFallback->setCurrentIndex(1);
    const QImage vanillaZone = dialog.createZoneLayer(
                QSize(256, 256), dialog.effectiveUnzonedFallback(), false,
                nullptr, nullptr);
    if (overrideZone.isNull() || vanillaZone.isNull() ||
            qGreen(overrideZone.pixel(127, 127)) != 171 ||
            qGreen(vanillaZone.pixel(127, 127)) != 64) {
        if (error)
            *error = QStringLiteral("Generated zone layers did not preserve the selected green fallback value.");
        return false;
    }

    const QDir fixture(temporary.path());
    const QString output = fixture.filePath(QStringLiteral("output"));
    const QString fallback = fixture.filePath(QStringLiteral("fallback"));
    if (!QDir().mkpath(output) || !QDir().mkpath(fallback))
        return false;
    QImage pixels(300, 300, QImage::Format_ARGB32);
    pixels.fill(qRgb(217, 230, 0));
    QImage existing(256, 256, QImage::Format_ARGB32);
    existing.fill(qRgb(153, 179, 43));
    QImage invalid(4, 4, QImage::Format_ARGB32);
    invalid.fill(Qt::black);
    const QString firstTile = QStringLiteral("biomemap_1_1.png");
    const QString existingPath = QDir(output).filePath(firstTile);
    const QString fallbackPath = QDir(fallback).filePath(firstTile);
    QString failedFile;
    QStringList neutral;
    int tiles = 0;
    if (!existing.save(existingPath) || !invalid.save(fallbackPath)
            || !dialog.saveTiles(pixels, output, fallback, QPoint(1, 1), 300,
                                 &tiles, &failedFile, &neutral)) {
        if (error)
            *error = QStringLiteral("Could not export the legacy boundary fixture %1").arg(failedFile);
        return false;
    }
    const QImage first(existingPath);
    const QImage last(QDir(output).filePath(QStringLiteral("biomemap_2_2.png")));
    if (tiles != 4 || neutral.size() != 3 || first.size() != existing.size()
            || last.size() != existing.size()
            || first.pixel(43, 44) != existing.pixel(0, 0)
            || first.pixel(44, 44) != pixels.pixel(0, 0)
            || last.pixel(87, 87) != pixels.pixel(0, 0)
            || last.pixel(88, 88) != qRgb(64, 64, 0)) {
        if (error)
            *error = QStringLiteral("Legacy export did not preserve valid boundary data and native 256-cell offsets.");
        return false;
    }

    if (summary) {
        *summary = QStringLiteral(
                    "literal overrides, zone fallback and legacy boundary export");
    }
    if (error)
        error->clear();
    return true;
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

            const QRect sourceRect = coveredWorldRect.translated(-sourceWorldOrigin);
            QImage tile;
            if (coveredWorldRect == tileWorldRect) {
                tile = image.copy(sourceRect);
            } else {
                QStringList basePaths{filePath};
                if (!fallbackDirectory.isEmpty()) {
                    const QString fallbackPath = fallbackDir.filePath(fileName);
                    if (fallbackPath != filePath)
                        basePaths.prepend(fallbackPath);
                }
                for (const QString &basePath : basePaths) {
                    const QImage base(basePath);
                    if (base.size() == QSize(tileSize, tileSize)) {
                        tile = base.convertToFormat(QImage::Format_ARGB32);
                        break;
                    }
                }
                if (tile.isNull()) {
                    tile = QImage(tileSize, tileSize, QImage::Format_ARGB32);
                    tile.fill(qRgba(64, 64, 0, 255));
                    if (neutralFallbackTiles)
                        *neutralFallbackTiles += fileName;
                }
                QPainter painter(&tile);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(coveredWorldRect.topLeft() - tileWorldRect.topLeft(),
                                  image, sourceRect);
            }
            QSaveFile file(filePath);
            if (!file.open(QIODevice::WriteOnly) || !tile.save(&file, "PNG") || !file.commit()) {
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
