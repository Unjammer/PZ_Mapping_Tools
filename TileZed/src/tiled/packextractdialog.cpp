#include "packextractdialog.h"
#include "ui_packextractdialog.h"

#include "texturepackfile.h"
#include "zprogress.h"

#include "BuildingEditor/buildingtiles.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>

namespace {

enum ExtractMode {
    IndividualImages,
    ReconstructedSheets,
    AtlasPages
};

enum OutputLayout {
    FlatLayout,
    PageDirectories,
    TilesetDirectories
};

enum ConflictPolicy {
    RenameExisting,
    SkipExisting,
    OverwriteExisting
};

enum GeometryMode {
    AutomaticGeometry,
    OneXGeometry,
    TwoXGeometry,
    CustomGeometry
};

struct ExtractOptions
{
    ExtractMode mode = IndividualImages;
    OutputLayout layout = FlatLayout;
    ConflictPolicy conflict = RenameExisting;
    GeometryMode geometry = AutomaticGeometry;
    int customWidth = 128;
    int customHeight = 256;
    int columns = 8;
    bool writeManifest = true;
    QString outputDirectory;
    QList<QPair<int, int>> textures;
};

struct ExtractResult
{
    int selectedTextures = 0;
    int filesWritten = 0;
    int skippedFiles = 0;
    int renamedFiles = 0;
    int unparseableTextures = 0;
    QStringList outputFiles;
};

QString safeFileName(QString value)
{
    value = value.trimmed();
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        value.replace(character, QLatin1Char('_'));
    for (int index = 0; index < value.size(); ++index) {
        if (value.at(index).unicode() < 32)
            value[index] = QLatin1Char('_');
    }
    while (value.endsWith(QLatin1Char('.')) ||
           value.endsWith(QLatin1Char(' '))) {
        value.chop(1);
    }
    if (value.isEmpty())
        value = QStringLiteral("unnamed");

    static const QSet<QString> reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"),
        QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"),
        QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"),
        QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    if (reserved.contains(value.toUpper()))
        value.prepend(QLatin1Char('_'));
    return value;
}

QString destinationPath(const QDir &directory, const QString &baseName,
                        const QString &extension,
                        ConflictPolicy policy, ExtractResult *result)
{
    const QString safeBase = safeFileName(baseName);
    QString path = directory.filePath(safeBase + extension);
    if (!QFileInfo::exists(path) || policy == OverwriteExisting)
        return path;
    if (policy == SkipExisting) {
        ++result->skippedFiles;
        return QString();
    }

    int suffix = 2;
    do {
        path = directory.filePath(
                    QStringLiteral("%1 (%2)%3")
                    .arg(safeBase).arg(suffix++).arg(extension));
    } while (QFileInfo::exists(path));
    ++result->renamedFiles;
    return path;
}

bool saveImageAtomically(const QImage &image, const QString &filePath,
                         QString *errorString)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) ||
            !image.save(&buffer, "PNG", -1)) {
        *errorString = QObject::tr("Could not encode %1 as PNG.")
                .arg(QDir::toNativeSeparators(filePath));
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) ||
            file.write(encoded) != encoded.size() ||
            !file.commit()) {
        *errorString = QObject::tr("Could not write %1.")
                .arg(QDir::toNativeSeparators(filePath));
        return false;
    }
    return true;
}

bool ensureDirectory(const QString &path, QDir *directory,
                     QString *errorString)
{
    if (!QDir().mkpath(path)) {
        *errorString = QObject::tr("Could not create %1.")
                .arg(QDir::toNativeSeparators(path));
        return false;
    }
    *directory = QDir(path);
    return true;
}

QString tilesetNameFor(const PackSubTexInfo &texture, int *tileIndex)
{
    QString tilesetName;
    int parsedIndex = -1;
    if (!BuildingEditor::BuildingTilesMgr::parseTileName(
            texture.name, tilesetName, parsedIndex)) {
        if (tileIndex)
            *tileIndex = -1;
        return QString();
    }
    if (tileIndex)
        *tileIndex = parsedIndex;
    return tilesetName;
}

bool validSelection(const PackFile &pack, int pageIndex, int textureIndex)
{
    return pageIndex >= 0 && pageIndex < pack.pages().size() &&
            textureIndex >= 0 &&
            textureIndex < pack.pages().at(pageIndex).mInfo.size();
}

QJsonObject sourceJson(const PackPage &page,
                       const PackSubTexInfo &texture)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), texture.name);
    object.insert(QStringLiteral("page"), page.name);
    object.insert(QStringLiteral("pageHasAlpha"), page.mask);
    object.insert(QStringLiteral("x"), texture.x);
    object.insert(QStringLiteral("y"), texture.y);
    object.insert(QStringLiteral("width"), texture.w);
    object.insert(QStringLiteral("height"), texture.h);
    object.insert(QStringLiteral("offsetX"), texture.ox);
    object.insert(QStringLiteral("offsetY"), texture.oy);
    object.insert(QStringLiteral("canvasWidth"), texture.fx);
    object.insert(QStringLiteral("canvasHeight"), texture.fy);
    object.insert(QStringLiteral("pixelSha256"),
                  PackFile::sha256Text(
                      PackFile::textureSha256(page, texture)));
    object.insert(QStringLiteral("metadataSha256"),
                  PackFile::sha256Text(
                      PackFile::metadataSha256(page, texture)));
    return object;
}

bool performExtraction(const PackFile &pack,
                       const ExtractOptions &options,
                       ExtractResult *result,
                       QString *errorString,
                       PROGRESS *progress = nullptr)
{
    result->selectedTextures = options.textures.size();
    if (progress) {
        progress->update(
                    QObject::tr("Validating %1 selected textures...")
                    .arg(options.textures.size()));
    }
    QDir output;
    if (!ensureDirectory(options.outputDirectory, &output, errorString))
        return false;

    QJsonArray manifestSources;
    for (const QPair<int, int> &selection : options.textures) {
        if (!validSelection(pack, selection.first, selection.second)) {
            *errorString = QObject::tr("The texture selection is invalid.");
            return false;
        }
        const PackPage &page = pack.pages().at(selection.first);
        const PackSubTexInfo &texture =
                page.mInfo.at(selection.second);
        manifestSources.append(sourceJson(page, texture));
    }

    if (options.mode == IndividualImages) {
        int writtenIndex = 0;
        for (const QPair<int, int> &selection : options.textures) {
            const PackPage &page = pack.pages().at(selection.first);
            const PackSubTexInfo &texture =
                    page.mInfo.at(selection.second);
            if (progress && writtenIndex % 16 == 0) {
                progress->update(
                            QObject::tr(
                                "Extracting texture %1 of %2\n%3")
                            .arg(writtenIndex + 1)
                            .arg(options.textures.size())
                            .arg(texture.name));
            }
            ++writtenIndex;
            int tileIndex = -1;
            const QString tileset =
                    tilesetNameFor(texture, &tileIndex);
            QDir destination = output;
            if (options.layout == PageDirectories) {
                if (!ensureDirectory(
                        output.filePath(safeFileName(page.name)),
                        &destination, errorString)) {
                    return false;
                }
            } else if (options.layout == TilesetDirectories) {
                const QString group = tileset.isEmpty()
                        ? QStringLiteral("_unparsed") : tileset;
                if (!ensureDirectory(
                        output.filePath(safeFileName(group)),
                        &destination, errorString)) {
                    return false;
                }
            }

            const QString path = destinationPath(
                        destination, texture.name,
                        QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty())
                continue;
            if (!saveImageAtomically(
                    PackFile::extractTexture(page, texture),
                    path, errorString)) {
                return false;
            }
            ++result->filesWritten;
            result->outputFiles += path;
        }
    } else if (options.mode == AtlasPages) {
        QSet<int> pageIndexes;
        for (const QPair<int, int> &selection : options.textures)
            pageIndexes += selection.first;
        QList<int> sortedPages = pageIndexes.values();
        std::sort(sortedPages.begin(), sortedPages.end());
        int outputIndex = 0;
        for (int pageIndex : sortedPages) {
            const PackPage &page = pack.pages().at(pageIndex);
            if (progress) {
                progress->update(
                            QObject::tr(
                                "Writing atlas page %1 of %2\n%3")
                            .arg(++outputIndex)
                            .arg(sortedPages.size())
                            .arg(page.name));
            }
            const QString path = destinationPath(
                        output, page.name, QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty())
                continue;
            if (!saveImageAtomically(page.image, path, errorString))
                return false;
            ++result->filesWritten;
            result->outputFiles += path;
        }
    } else {
        QMap<QString, QList<QPair<int, int>>> groups;
        for (const QPair<int, int> &selection : options.textures) {
            const PackSubTexInfo &texture = pack.pages()
                    .at(selection.first).mInfo.at(selection.second);
            int tileIndex = -1;
            const QString tileset =
                    tilesetNameFor(texture, &tileIndex);
            if (tileset.isEmpty() || tileIndex < 0) {
                ++result->unparseableTextures;
                continue;
            }
            groups[tileset].append(selection);
        }

        int groupIndex = 0;
        for (auto group = groups.constBegin();
             group != groups.constEnd(); ++group) {
            if (progress) {
                progress->update(
                            QObject::tr(
                                "Reconstructing tilesheet %1 of %2\n%3")
                            .arg(++groupIndex)
                            .arg(groups.size())
                            .arg(group.key()));
            }
            int cellWidth = options.customWidth;
            int cellHeight = options.customHeight;
            if (options.geometry == OneXGeometry) {
                cellWidth = 64;
                cellHeight = 128;
            } else if (options.geometry == TwoXGeometry) {
                cellWidth = 128;
                cellHeight = 256;
            } else if (options.geometry == AutomaticGeometry) {
                cellWidth = 0;
                cellHeight = 0;
                for (const QPair<int, int> &selection : group.value()) {
                    const PackSubTexInfo &texture = pack.pages()
                            .at(selection.first).mInfo.at(selection.second);
                    cellWidth = qMax(cellWidth, texture.fx);
                    cellHeight = qMax(cellHeight, texture.fy);
                }
            }
            if (cellWidth <= 0 || cellHeight <= 0) {
                *errorString = QObject::tr(
                            "Invalid cell geometry for %1.")
                        .arg(group.key());
                return false;
            }

            int maximumIndex = -1;
            QSet<int> usedIndexes;
            for (const QPair<int, int> &selection : group.value()) {
                const PackSubTexInfo &texture = pack.pages()
                        .at(selection.first).mInfo.at(selection.second);
                int tileIndex = -1;
                tilesetNameFor(texture, &tileIndex);
                if (usedIndexes.contains(tileIndex)) {
                    *errorString = QObject::tr(
                                "Duplicate tile index %1 in %2.")
                            .arg(tileIndex).arg(group.key());
                    return false;
                }
                usedIndexes += tileIndex;
                maximumIndex = qMax(maximumIndex, tileIndex);
                if (texture.fx > cellWidth ||
                        texture.fy > cellHeight) {
                    *errorString = QObject::tr(
                                "%1 needs a %2x%3 cell, larger than "
                                "the selected %4x%5 geometry.")
                            .arg(texture.name)
                            .arg(texture.fx).arg(texture.fy)
                            .arg(cellWidth).arg(cellHeight);
                    return false;
                }
            }

            const int rows = maximumIndex / options.columns + 1;
            const qint64 sheetWidth =
                    qint64(options.columns) * cellWidth;
            const qint64 sheetHeight = qint64(rows) * cellHeight;
            if (sheetWidth <= 0 || sheetHeight <= 0 ||
                    sheetWidth > 32768 || sheetHeight > 32768 ||
                    sheetWidth * sheetHeight > 64LL * 1024 * 1024) {
                *errorString = QObject::tr(
                            "The reconstructed sheet %1 would be "
                            "%2x%3 pixels.")
                        .arg(group.key())
                        .arg(sheetWidth).arg(sheetHeight);
                return false;
            }

            QImage sheet(int(sheetWidth), int(sheetHeight),
                         QImage::Format_ARGB32);
            if (sheet.isNull()) {
                *errorString = QObject::tr(
                            "Could not allocate the %1 tilesheet.")
                        .arg(group.key());
                return false;
            }
            sheet.fill(Qt::transparent);
            QPainter painter(&sheet);
            for (const QPair<int, int> &selection : group.value()) {
                const PackPage &page =
                        pack.pages().at(selection.first);
                const PackSubTexInfo &texture =
                        page.mInfo.at(selection.second);
                int tileIndex = -1;
                tilesetNameFor(texture, &tileIndex);
                painter.drawImage(
                            QPoint((tileIndex % options.columns) *
                                   cellWidth,
                                   (tileIndex / options.columns) *
                                   cellHeight),
                            PackFile::extractTexture(page, texture));
            }
            painter.end();

            const QString path = destinationPath(
                        output, group.key(), QStringLiteral(".png"),
                        options.conflict, result);
            if (path.isEmpty())
                continue;
            if (!saveImageAtomically(sheet, path, errorString))
                return false;
            ++result->filesWritten;
            result->outputFiles += path;
        }
    }

    if (options.writeManifest) {
        if (progress)
            progress->update(QObject::tr("Writing extraction manifest..."));
        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"),
                        QStringLiteral("PZToolsPackExtraction"));
        manifest.insert(QStringLiteral("version"), 1);
        manifest.insert(QStringLiteral("sourcePack"),
                        pack.fileName());
        manifest.insert(QStringLiteral("sourcePackSha256"),
                        PackFile::sha256Text(pack.fileSha256()));
        manifest.insert(QStringLiteral("packVersion"),
                        pack.version());
        manifest.insert(QStringLiteral("selectedTextures"),
                        manifestSources);
        QJsonArray outputs;
        for (const QString &path : result->outputFiles)
            outputs.append(QDir(output.path()).relativeFilePath(path));
        manifest.insert(QStringLiteral("outputs"), outputs);

        const QString manifestPath = destinationPath(
                    output, QStringLiteral("pack-extraction-manifest"),
                    QStringLiteral(".json"),
                    options.conflict, result);
        if (!manifestPath.isEmpty()) {
            QSaveFile file(manifestPath);
            const QByteArray json =
                    QJsonDocument(manifest).toJson(
                        QJsonDocument::Indented);
            if (!file.open(QIODevice::WriteOnly) ||
                    file.write(json) != json.size() ||
                    !file.commit()) {
                *errorString = QObject::tr(
                            "Could not write the extraction manifest.");
                return false;
            }
            ++result->filesWritten;
            result->outputFiles += manifestPath;
        }
    }
    return true;
}

void addDemoTexture(PackPage *page, const QString &name,
                    const QRect &packedRect, const QColor &color,
                    const QSize &fullSize)
{
    QPainter pagePainter(&page->image);
    pagePainter.fillRect(packedRect, color);
    pagePainter.end();
    page->mInfo += PackSubTexInfo(
                packedRect.x(), packedRect.y(),
                packedRect.width(), packedRect.height(),
                (fullSize.width() - packedRect.width()) / 2,
                fullSize.height() - packedRect.height(),
                fullSize.width(), fullSize.height(), name);
}

PackFile createDemoPack()
{
    PackPage page;
    page.name = QStringLiteral("unpacker_demo_page");
    page.image = QImage(512, 512, QImage::Format_ARGB32);
    page.image.fill(Qt::transparent);
    addDemoTexture(&page, QStringLiteral("demo_natural_0"),
                   QRect(0, 0, 48, 72),
                   QColor(55, 175, 90), QSize(64, 128));
    addDemoTexture(&page, QStringLiteral("demo_natural_1"),
                   QRect(64, 0, 48, 92),
                   QColor(65, 125, 220), QSize(64, 128));
    addDemoTexture(&page, QStringLiteral("demo_natural_9"),
                   QRect(128, 0, 52, 110),
                   QColor(210, 110, 65), QSize(64, 128));
    addDemoTexture(&page, QStringLiteral("loose_icon"),
                   QRect(192, 0, 40, 40),
                   QColor(220, 190, 60), QSize(48, 48));
    PackFile pack;
    pack.addPage(page);
    return pack;
}

} // anonymous namespace

PackExtractDialog::PackExtractDialog(PackFile &packFile,
                                     QWidget *parent,
                                     PROGRESS *initializationProgress)
    : QDialog(parent)
    , ui(new Ui::PackExtractDialog)
    , mPackFile(packFile)
{
    ui->setupUi(this);

    ui->matchModeCombo->addItem(tr("Contains"), 0);
    ui->matchModeCombo->addItem(tr("Starts with"), 1);
    ui->matchModeCombo->addItem(tr("Wildcard"), 2);
    ui->matchModeCombo->addItem(tr("Regular expression"), 3);
    ui->matchModeCombo->addItem(tr("Exact tileset name"), 4);
    ui->geometryCombo->addItem(tr("Automatic from texture canvases"),
                               int(AutomaticGeometry));
    ui->geometryCombo->addItem(tr("1x cells (64x128)"),
                               int(OneXGeometry));
    ui->geometryCombo->addItem(tr("2x cells (128x256)"),
                               int(TwoXGeometry));
    ui->geometryCombo->addItem(tr("Custom cells"),
                               int(CustomGeometry));
    ui->outputLayoutCombo->addItem(tr("Flat output directory"),
                                   int(FlatLayout));
    ui->outputLayoutCombo->addItem(tr("Subdirectory per atlas page"),
                                   int(PageDirectories));
    ui->outputLayoutCombo->addItem(tr("Subdirectory per tileset"),
                                   int(TilesetDirectories));
    ui->conflictCombo->addItem(tr("Rename new files safely"),
                               int(RenameExisting));
    ui->conflictCombo->addItem(tr("Skip existing files"),
                               int(SkipExisting));
    ui->conflictCombo->addItem(tr("Overwrite existing files"),
                               int(OverwriteExisting));

    ui->textureTable->setIconSize(QSize(48, 48));
    ui->textureTable->setSelectionBehavior(
                QAbstractItemView::SelectRows);
    ui->textureTable->setEditTriggers(
                QAbstractItemView::NoEditTriggers);
    ui->textureTable->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::Stretch);
    for (int column = 0;
         column < ui->textureTable->columnCount(); ++column) {
        if (column != 1) {
            ui->textureTable->horizontalHeader()
                    ->setSectionResizeMode(
                        column, QHeaderView::ResizeToContents);
        }
    }

    connect(ui->outputBrowse, &QAbstractButton::clicked,
            this, &PackExtractDialog::browse);
    connect(ui->filterEdit, &QLineEdit::textChanged,
            this, &PackExtractDialog::filterChanged);
    connect(ui->matchModeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PackExtractDialog::filterChanged);
    connect(ui->caseSensitiveCheck, &QAbstractButton::toggled,
            this, &PackExtractDialog::filterChanged);
    connect(ui->selectVisibleButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::selectAllVisible);
    connect(ui->clearVisibleButton, &QAbstractButton::clicked,
            this, &PackExtractDialog::clearVisible);
    connect(ui->textureTable, &QTableWidget::itemChanged,
            this, &PackExtractDialog::filterChanged);
    connect(ui->radioIndividual, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->radioSheets, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->radioPages, &QAbstractButton::toggled,
            this, &PackExtractDialog::modeChanged);
    connect(ui->geometryCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PackExtractDialog::modeChanged);

    QSettings settings;
    settings.beginGroup(QStringLiteral("PackExtractDialog"));
    ui->filterEdit->setText(
                settings.value(QStringLiteral("Filter")).toString());
    ui->matchModeCombo->setCurrentIndex(
                settings.value(QStringLiteral("MatchMode"), 0).toInt());
    ui->caseSensitiveCheck->setChecked(
                settings.value(QStringLiteral("CaseSensitive"),
                               false).toBool());
    const int mode = settings.value(
                QStringLiteral("Mode"),
                int(IndividualImages)).toInt();
    ui->radioIndividual->setChecked(mode == IndividualImages);
    ui->radioSheets->setChecked(mode == ReconstructedSheets);
    ui->radioPages->setChecked(mode == AtlasPages);
    ui->geometryCombo->setCurrentIndex(
                settings.value(QStringLiteral("Geometry"), 0).toInt());
    ui->cellWidthSpin->setValue(
                settings.value(QStringLiteral("CellWidth"), 128).toInt());
    ui->cellHeightSpin->setValue(
                settings.value(QStringLiteral("CellHeight"), 256).toInt());
    ui->columnSpin->setValue(
                settings.value(QStringLiteral("Columns"), 8).toInt());
    ui->outputLayoutCombo->setCurrentIndex(
                settings.value(QStringLiteral("OutputLayout"), 0).toInt());
    ui->conflictCombo->setCurrentIndex(
                settings.value(QStringLiteral("Conflict"), 0).toInt());
    ui->manifestCheck->setChecked(
                settings.value(QStringLiteral("Manifest"), true).toBool());
    ui->outputEdit->setText(
                settings.value(
                    QStringLiteral("OutputDirectory")).toString());
    settings.endGroup();

    populateTextures(initializationProgress);
    filterChanged();
    modeChanged();
}

PackExtractDialog::~PackExtractDialog()
{
    delete ui;
}

void PackExtractDialog::populateTextures(PROGRESS *progress)
{
    const QSignalBlocker blocker(ui->textureTable);
    ui->textureTable->setSortingEnabled(false);
    ui->textureTable->setRowCount(0);
    int textureCount = 0;
    for (const PackPage &page : mPackFile.pages())
        textureCount += page.mInfo.size();
    if (progress) {
        progress->update(
                    tr("Preparing the Versatile .pack Extractor...\n"
                       "Indexing %1 textures from %2 atlas pages.")
                    .arg(textureCount)
                    .arg(mPackFile.pages().size()));
    }

    int processed = 0;
    for (int pageIndex = 0;
         pageIndex < mPackFile.pages().size(); ++pageIndex) {
        const PackPage &page = mPackFile.pages().at(pageIndex);
        for (int textureIndex = 0;
             textureIndex < page.mInfo.size(); ++textureIndex) {
            if (progress && (processed % 32 == 0 ||
                             processed + 1 == textureCount)) {
                progress->update(
                            tr("Preparing texture %1 of %2\n"
                               "Atlas page: %3")
                            .arg(processed + 1)
                            .arg(textureCount)
                            .arg(page.name));
                if (progress->wasCanceled()) {
                    mInitializationCanceled = true;
                    ui->textureTable->setSortingEnabled(true);
                    return;
                }
            }
            const PackSubTexInfo &texture =
                    page.mInfo.at(textureIndex);
            const int row = ui->textureTable->rowCount();
            ui->textureTable->insertRow(row);

            QTableWidgetItem *check = new QTableWidgetItem;
            check->setFlags(Qt::ItemIsEnabled |
                            Qt::ItemIsSelectable |
                            Qt::ItemIsUserCheckable);
            check->setCheckState(Qt::Checked);
            check->setData(Qt::UserRole, pageIndex);
            check->setData(Qt::UserRole + 1, textureIndex);
            ui->textureTable->setItem(row, 0, check);

            const QImage preview =
                    PackFile::extractTexture(page, texture);
            QTableWidgetItem *name =
                    new QTableWidgetItem(
                        QIcon(QPixmap::fromImage(preview)
                              .scaled(QSize(48, 48),
                                      Qt::KeepAspectRatio,
                                      Qt::FastTransformation)),
                        texture.name);
            ui->textureTable->setItem(row, 1, name);

            int tileIndex = -1;
            const QString tileset =
                    tilesetNameFor(texture, &tileIndex);
            ui->textureTable->setItem(
                        row, 2,
                        new QTableWidgetItem(
                            tileset.isEmpty()
                            ? QStringLiteral("-") : tileset));
            ui->textureTable->setItem(
                        row, 3,
                        new QTableWidgetItem(
                            tileIndex < 0
                            ? QStringLiteral("-")
                            : QString::number(tileIndex)));
            ui->textureTable->setItem(
                        row, 4, new QTableWidgetItem(page.name));
            ui->textureTable->setItem(
                        row, 5,
                        new QTableWidgetItem(
                            QStringLiteral("%1x%2")
                            .arg(texture.fx).arg(texture.fy)));
            ui->textureTable->setItem(
                        row, 6,
                        new QTableWidgetItem(
                            QStringLiteral("%1,%2 %3x%4")
                            .arg(texture.x).arg(texture.y)
                            .arg(texture.w).arg(texture.h)));
            const QString textureHash = PackFile::sha256Text(
                        PackFile::textureSha256(page, texture));
            QTableWidgetItem *hashItem =
                    new QTableWidgetItem(textureHash.left(16));
            hashItem->setToolTip(textureHash);
            ui->textureTable->setItem(row, 7, hashItem);
            ++processed;
        }
    }
    ui->textureTable->setSortingEnabled(true);
    if (progress) {
        progress->update(
                    tr("Finalizing filters and extraction options..."));
    }
}

bool PackExtractDialog::rowMatches(
        int row, QString *regularExpressionError) const
{
    const QString filter = ui->filterEdit->text().trimmed();
    if (filter.isEmpty())
        return true;

    const Qt::CaseSensitivity sensitivity =
            ui->caseSensitiveCheck->isChecked()
            ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QString name =
            ui->textureTable->item(row, 1)->text();
    const QString tileset =
            ui->textureTable->item(row, 2)->text();
    const int mode = ui->matchModeCombo->currentData().toInt();
    if (mode == 3) {
        QRegularExpression expression(
                    filter,
                    sensitivity == Qt::CaseInsensitive
                    ? QRegularExpression::CaseInsensitiveOption
                    : QRegularExpression::NoPatternOption);
        if (!expression.isValid()) {
            *regularExpressionError = expression.errorString();
            return false;
        }
        return expression.match(name).hasMatch();
    }

    const QStringList patterns = filter.split(
                QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &rawPattern : patterns) {
        const QString pattern = rawPattern.trimmed();
        if (mode == 0 &&
                name.contains(pattern, sensitivity)) {
            return true;
        }
        if (mode == 1 &&
                name.startsWith(pattern, sensitivity)) {
            return true;
        }
        if (mode == 2) {
            QRegularExpression expression(
                        QRegularExpression::wildcardToRegularExpression(
                            pattern),
                        sensitivity == Qt::CaseInsensitive
                        ? QRegularExpression::CaseInsensitiveOption
                        : QRegularExpression::NoPatternOption);
            if (expression.match(name).hasMatch())
                return true;
        }
        if (mode == 4 &&
                tileset.compare(pattern, sensitivity) == 0) {
            return true;
        }
    }
    return false;
}

void PackExtractDialog::filterChanged()
{
    int visible = 0;
    int checked = 0;
    QString regularExpressionError;
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        QString rowError;
        const bool matches = rowMatches(row, &rowError);
        if (!rowError.isEmpty())
            regularExpressionError = rowError;
        ui->textureTable->setRowHidden(row, !matches);
        if (matches) {
            ++visible;
            if (ui->textureTable->item(row, 0)->checkState() ==
                    Qt::Checked) {
                ++checked;
            }
        }
    }

    if (!regularExpressionError.isEmpty()) {
        ui->matchSummary->setStyleSheet(
                    QStringLiteral("color: #d9534f;"));
        ui->matchSummary->setText(
                    tr("Invalid regular expression: %1")
                    .arg(regularExpressionError));
    } else {
        ui->matchSummary->setStyleSheet(QString());
        ui->matchSummary->setText(
                    tr("%1 textures visible, %2 selected, %3 pages")
                    .arg(visible).arg(checked)
                    .arg(mPackFile.pages().size()));
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
                regularExpressionError.isEmpty() && checked > 0);
}

void PackExtractDialog::selectAllVisible()
{
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        if (!ui->textureTable->isRowHidden(row))
            ui->textureTable->item(row, 0)->setCheckState(Qt::Checked);
    }
    filterChanged();
}

void PackExtractDialog::clearVisible()
{
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        if (!ui->textureTable->isRowHidden(row))
            ui->textureTable->item(row, 0)->setCheckState(Qt::Unchecked);
    }
    filterChanged();
}

QList<QPair<int, int>>
PackExtractDialog::checkedVisibleTextures() const
{
    QList<QPair<int, int>> selections;
    for (int row = 0; row < ui->textureTable->rowCount(); ++row) {
        QTableWidgetItem *item = ui->textureTable->item(row, 0);
        if (!ui->textureTable->isRowHidden(row) &&
                item->checkState() == Qt::Checked) {
            selections += qMakePair(
                        item->data(Qt::UserRole).toInt(),
                        item->data(Qt::UserRole + 1).toInt());
        }
    }
    return selections;
}

void PackExtractDialog::browse()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Choose output directory"),
                ui->outputEdit->text());
    if (!directory.isEmpty())
        ui->outputEdit->setText(QDir::toNativeSeparators(directory));
}

void PackExtractDialog::modeChanged()
{
    const bool sheets = ui->radioSheets->isChecked();
    ui->geometryCombo->setEnabled(sheets);
    ui->cellWidthSpin->setEnabled(
                sheets &&
                ui->geometryCombo->currentData().toInt() ==
                CustomGeometry);
    ui->cellHeightSpin->setEnabled(
                ui->cellWidthSpin->isEnabled());
    ui->columnSpin->setEnabled(sheets);
    ui->outputLayoutCombo->setEnabled(
                ui->radioIndividual->isChecked());
}

void PackExtractDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("PackExtractDialog"));
    settings.setValue(QStringLiteral("Filter"),
                      ui->filterEdit->text());
    settings.setValue(QStringLiteral("MatchMode"),
                      ui->matchModeCombo->currentIndex());
    settings.setValue(QStringLiteral("CaseSensitive"),
                      ui->caseSensitiveCheck->isChecked());
    settings.setValue(QStringLiteral("Mode"),
                      ui->radioIndividual->isChecked()
                      ? int(IndividualImages)
                      : ui->radioSheets->isChecked()
                        ? int(ReconstructedSheets)
                        : int(AtlasPages));
    settings.setValue(QStringLiteral("Geometry"),
                      ui->geometryCombo->currentIndex());
    settings.setValue(QStringLiteral("CellWidth"),
                      ui->cellWidthSpin->value());
    settings.setValue(QStringLiteral("CellHeight"),
                      ui->cellHeightSpin->value());
    settings.setValue(QStringLiteral("Columns"),
                      ui->columnSpin->value());
    settings.setValue(QStringLiteral("OutputLayout"),
                      ui->outputLayoutCombo->currentIndex());
    settings.setValue(QStringLiteral("Conflict"),
                      ui->conflictCombo->currentIndex());
    settings.setValue(QStringLiteral("Manifest"),
                      ui->manifestCheck->isChecked());
    settings.setValue(QStringLiteral("OutputDirectory"),
                      ui->outputEdit->text().trimmed());
    settings.endGroup();
}

void PackExtractDialog::accept()
{
    const QList<QPair<int, int>> textures =
            checkedVisibleTextures();
    if (textures.isEmpty()) {
        QMessageBox::information(
                    this, tr("Nothing selected"),
                    tr("Select at least one visible texture."));
        return;
    }
    const QString outputPath = ui->outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(
                    this, tr("Output directory required"),
                    tr("Choose an output directory."));
        return;
    }

    ExtractOptions options;
    options.mode = ui->radioIndividual->isChecked()
            ? IndividualImages
            : ui->radioSheets->isChecked()
              ? ReconstructedSheets : AtlasPages;
    options.layout = OutputLayout(
                ui->outputLayoutCombo->currentData().toInt());
    options.conflict = ConflictPolicy(
                ui->conflictCombo->currentData().toInt());
    options.geometry = GeometryMode(
                ui->geometryCombo->currentData().toInt());
    options.customWidth = ui->cellWidthSpin->value();
    options.customHeight = ui->cellHeightSpin->value();
    options.columns = ui->columnSpin->value();
    options.writeManifest = ui->manifestCheck->isChecked();
    options.outputDirectory = outputPath;
    options.textures = textures;

    saveSettings();
    ExtractResult result;
    QString error;
    {
        PROGRESS progress(
                    tr("Starting extraction of %1 textures...")
                    .arg(textures.size()), this);
        if (!performExtraction(
                mPackFile, options, &result, &error, &progress)) {
            QMessageBox::warning(this, tr("Extraction failed"), error);
            return;
        }
    }
    if (result.filesWritten == 0) {
        QMessageBox::information(
                    this, tr("Nothing written"),
                    tr("All matching output files were skipped."));
        return;
    }

    QMessageBox::information(
                this, tr("Extraction complete"),
                tr("%1 selected textures produced %2 files.\n"
                   "Renamed: %3 | Skipped: %4 | "
                   "Unparseable for tilesheets: %5")
                .arg(result.selectedTextures)
                .arg(result.filesWritten)
                .arg(result.renamedFiles)
                .arg(result.skippedFiles)
                .arg(result.unparseableTextures));
    QDialog::accept();
}

bool PackExtractDialog::runSelfTest(
        QString *summary, QString *errorString)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the extraction test directory");
        return false;
    }

    PackFile pack = createDemoPack();
    const QString packPath =
            temporary.filePath(QStringLiteral("demo.pack"));
    if (!pack.write(packPath)) {
        *errorString = pack.errorString();
        return false;
    }
    PackFile loaded;
    if (!loaded.read(packPath)) {
        *errorString = loaded.errorString();
        return false;
    }

    ExtractOptions individual;
    individual.outputDirectory =
            temporary.filePath(QStringLiteral("individual"));
    individual.layout = TilesetDirectories;
    individual.conflict = RenameExisting;
    individual.writeManifest = true;
    individual.textures += qMakePair(0, 0);
    individual.textures += qMakePair(0, 1);
    ExtractResult firstResult;
    if (!performExtraction(
            loaded, individual, &firstResult, errorString)) {
        return false;
    }
    if (firstResult.filesWritten != 3 ||
            !QFileInfo::exists(QDir(individual.outputDirectory)
             .filePath(QStringLiteral(
                "demo_natural/demo_natural_0.png"))) ||
            !QFileInfo::exists(QDir(individual.outputDirectory)
             .filePath(QStringLiteral(
                "pack-extraction-manifest.json")))) {
        *errorString = QStringLiteral(
                    "Individual extraction or manifest output failed");
        return false;
    }

    ExtractResult renameResult;
    if (!performExtraction(
            loaded, individual, &renameResult, errorString) ||
            renameResult.renamedFiles != 3) {
        *errorString = QStringLiteral(
                    "Safe conflict renaming was not applied");
        return false;
    }

    ExtractOptions sheets;
    sheets.mode = ReconstructedSheets;
    sheets.geometry = AutomaticGeometry;
    sheets.columns = 8;
    sheets.outputDirectory =
            temporary.filePath(QStringLiteral("sheets"));
    sheets.writeManifest = false;
    sheets.textures += qMakePair(0, 0);
    sheets.textures += qMakePair(0, 1);
    sheets.textures += qMakePair(0, 2);
    ExtractResult sheetResult;
    if (!performExtraction(
            loaded, sheets, &sheetResult, errorString)) {
        return false;
    }
    const QImage reconstructed(
                QDir(sheets.outputDirectory)
                .filePath(QStringLiteral("demo_natural.png")));
    if (sheetResult.filesWritten != 1 ||
            reconstructed.size() != QSize(512, 256)) {
        *errorString = QStringLiteral(
                    "Automatic tilesheet reconstruction has the wrong size");
        return false;
    }

    ExtractOptions pages;
    pages.mode = AtlasPages;
    pages.outputDirectory =
            temporary.filePath(QStringLiteral("pages"));
    pages.writeManifest = false;
    pages.textures += qMakePair(0, 0);
    ExtractResult pageResult;
    if (!performExtraction(
            loaded, pages, &pageResult, errorString) ||
            pageResult.filesWritten != 1) {
        *errorString = QStringLiteral(
                    "Atlas-page extraction failed");
        return false;
    }

    *summary = QStringLiteral(
                "individual images, safe rename, JSON manifest, "
                "automatic tilesheet and atlas-page extraction verified");
    return true;
}

bool PackExtractDialog::renderValidation(
        const QString &outputFile, QString *errorString)
{
    PackFile pack = createDemoPack();
    PackExtractDialog dialog(pack);
    dialog.resize(1180, 820);
    dialog.ui->filterEdit->setText(QStringLiteral("demo"));
    dialog.ui->radioSheets->setChecked(true);
    dialog.modeChanged();
    dialog.show();
    QApplication::processEvents();

    const QFileInfo outputInfo(outputFile);
    if (!QDir().mkpath(outputInfo.absolutePath()) ||
            !dialog.grab().save(outputFile, "PNG")) {
        *errorString = QStringLiteral("Could not save %1")
                .arg(outputFile);
        return false;
    }
    dialog.close();
    return true;
}
