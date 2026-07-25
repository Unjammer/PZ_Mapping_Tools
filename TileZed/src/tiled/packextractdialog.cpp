#include "packextractdialog.h"
#include "ui_packextractdialog.h"

#include "texturepackfile.h"

#include "BuildingEditor/buildingtiles.h"

#include <QAbstractButton>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>

namespace {

struct TileInfo
{
    int index;
    QImage image;
};

QImage extractTexture(const PackPage &page, const PackSubTexInfo &texture)
{
    QImage image(texture.fx, texture.fy, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(texture.ox, texture.oy, page.image,
                      texture.x, texture.y, texture.w, texture.h);
    return image;
}

QStringList parsePrefixes(const QString &text, bool multiplePrefixes)
{
    const QString trimmedText = text.trimmed();
    if (!multiplePrefixes)
        return QStringList(trimmedText);

    QStringList result;
    for (const QString &part : trimmedText.split(
             QLatin1Char(';'), QString::SkipEmptyParts)) {
        const QString prefix = part.trimmed();
        if (!prefix.isEmpty() && !result.contains(prefix, Qt::CaseInsensitive))
            result.append(prefix);
    }

    if (trimmedText.isEmpty())
        result.append(QString());
    return result;
}

bool matchesAnyPrefix(const QString &name, const QStringList &prefixes)
{
    for (const QString &prefix : prefixes) {
        if (prefix.isEmpty() || name.startsWith(prefix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

int extractIndividualImages(PackFile &packFile, const QStringList &prefixes,
                            const QDir &outputDirectory, QString *failedFile)
{
    int extractedCount = 0;
    for (const PackPage &page : packFile.pages()) {
        for (const PackSubTexInfo &texture : page.mInfo) {
            if (!matchesAnyPrefix(texture.name, prefixes))
                continue;

            const QString filePath = outputDirectory.filePath(
                        texture.name + QLatin1String(".png"));
            if (!extractTexture(page, texture).save(filePath, "PNG", -1)) {
                if (failedFile)
                    *failedFile = filePath;
                return -1;
            }
            ++extractedCount;
        }
    }
    return extractedCount;
}

bool extractTilesheet(PackFile &packFile, const QString &prefix,
                      bool wholeWordOnly, int tileScale,
                      const QDir &outputDirectory, int *extractedCount,
                      QString *failedFile)
{
    const int tileWidth = 64 * tileScale;
    const int tileHeight = 128 * tileScale;
    QList<TileInfo> tiles;
    int maximumIndex = -1;

    for (const PackPage &page : packFile.pages()) {
        for (const PackSubTexInfo &texture : page.mInfo) {
            QString tileName;
            int tileIndex = -1;
            if (!BuildingEditor::BuildingTilesMgr::parseTileName(
                    texture.name, tileName, tileIndex)) {
                continue;
            }

            const bool matches = wholeWordOnly
                    ? tileName.compare(prefix, Qt::CaseInsensitive) == 0
                    : texture.name.startsWith(prefix, Qt::CaseInsensitive);
            if (!matches || tileIndex < 0)
                continue;

            if (texture.fx != tileWidth || texture.fy != tileHeight) {
                qWarning().noquote()
                        << QStringLiteral("%1 is %2x%3; expected %4x%5")
                           .arg(texture.name)
                           .arg(texture.fx)
                           .arg(texture.fy)
                           .arg(tileWidth)
                           .arg(tileHeight);
            }

            TileInfo tile;
            tile.index = tileIndex;
            tile.image = extractTexture(page, texture);
            tiles.append(tile);
            maximumIndex = qMax(maximumIndex, tileIndex);
        }
    }

    if (extractedCount)
        *extractedCount = tiles.size();
    if (tiles.isEmpty())
        return true;

    const int columnCount = 8;
    const int rowCount = maximumIndex / columnCount + 1;
    QImage sheet(columnCount * tileWidth, rowCount * tileHeight,
                 QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);

    QPainter painter(&sheet);
    for (const TileInfo &tile : tiles) {
        const QPoint position((tile.index % columnCount) * tileWidth,
                              (tile.index / columnCount) * tileHeight);
        painter.drawImage(position, tile.image);
    }
    painter.end();

    QString fileName = prefix;
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        fileName.replace(character, QLatin1Char('_'));
    const QString filePath = outputDirectory.filePath(
                fileName + QLatin1String(".png"));
    if (!sheet.save(filePath, "PNG", -1)) {
        if (failedFile)
            *failedFile = filePath;
        return false;
    }
    return true;
}

} // anonymous namespace

PackExtractDialog::PackExtractDialog(PackFile &packFile, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PackExtractDialog)
    , mPackFile(packFile)
{
    ui->setupUi(this);

    connect(ui->outputBrowse, &QAbstractButton::clicked,
            this, &PackExtractDialog::browse);
    connect(ui->radioSingle, &QRadioButton::toggled,
            this, &PackExtractDialog::radioToggled);

    QSettings settings;
    settings.beginGroup(QStringLiteral("PackExtractDialog"));
    ui->radioSingle->setChecked(
                settings.value(QStringLiteral("IsTilesheet"), false).toBool());
    ui->radioMultiple->setChecked(!ui->radioSingle->isChecked());
    ui->checkBox2x->setChecked(
                settings.value(QStringLiteral("Tilesheet2x"), true).toBool());
    ui->wholeWordCheck->setChecked(
                settings.value(QStringLiteral("WholeWordOnly"), true).toBool());
    ui->multiplePrefixesCheck->setChecked(
                settings.value(QStringLiteral("MultiplePrefixes"), false).toBool());
    ui->prefixEdit->setText(
                settings.value(QStringLiteral("Prefix")).toString());
    ui->outputEdit->setText(
                settings.value(QStringLiteral("OutputDirectory")).toString());
    settings.endGroup();

    radioToggled();
}

PackExtractDialog::~PackExtractDialog()
{
    delete ui;
}

void PackExtractDialog::browse()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Choose output directory"), ui->outputEdit->text());
    if (!directory.isEmpty())
        ui->outputEdit->setText(QDir::toNativeSeparators(directory));
}

void PackExtractDialog::accept()
{
    const QStringList prefixes = parsePrefixes(
                ui->prefixEdit->text(), ui->multiplePrefixesCheck->isChecked());
    if (prefixes.isEmpty() ||
            (ui->radioSingle->isChecked() && prefixes.first().isEmpty())) {
        QMessageBox::warning(this, tr("Missing Prefix"),
                             tr("A tilesheet prefix is required."));
        return;
    }

    const QString outputPath = ui->outputEdit->text().trimmed();
    if (outputPath.isEmpty() ||
            (!QDir(outputPath).exists() && !QDir().mkpath(outputPath))) {
        QMessageBox::warning(this, tr("Output Error"),
                             tr("The output directory could not be created."));
        return;
    }
    const QDir outputDirectory(outputPath);

    QSettings settings;
    settings.beginGroup(QStringLiteral("PackExtractDialog"));
    settings.setValue(QStringLiteral("IsTilesheet"),
                      ui->radioSingle->isChecked());
    settings.setValue(QStringLiteral("Tilesheet2x"),
                      ui->checkBox2x->isChecked());
    settings.setValue(QStringLiteral("WholeWordOnly"),
                      ui->wholeWordCheck->isChecked());
    settings.setValue(QStringLiteral("MultiplePrefixes"),
                      ui->multiplePrefixesCheck->isChecked());
    settings.setValue(QStringLiteral("Prefix"), ui->prefixEdit->text().trimmed());
    settings.setValue(QStringLiteral("OutputDirectory"), outputDirectory.path());
    settings.endGroup();

    QString failedFile;
    int extractedCount = 0;
    if (ui->radioMultiple->isChecked()) {
        extractedCount = extractIndividualImages(
                    mPackFile, prefixes, outputDirectory, &failedFile);
        if (extractedCount < 0) {
            QMessageBox::warning(this, tr("Save Failed"),
                                 tr("Could not save %1.")
                                 .arg(QDir::toNativeSeparators(failedFile)));
            return;
        }
    } else {
        const int tileScale = ui->checkBox2x->isChecked() ? 2 : 1;
        for (const QString &prefix : prefixes) {
            int prefixCount = 0;
            if (!extractTilesheet(mPackFile, prefix,
                                  ui->wholeWordCheck->isChecked(), tileScale,
                                  outputDirectory, &prefixCount, &failedFile)) {
                QMessageBox::warning(this, tr("Save Failed"),
                                     tr("Could not save %1.")
                                     .arg(QDir::toNativeSeparators(failedFile)));
                return;
            }
            extractedCount += prefixCount;
        }
    }

    if (extractedCount == 0) {
        QMessageBox::information(this, tr("Nothing Extracted"),
                                 tr("No textures matched the requested prefix."));
        return;
    }

    QDialog::accept();
}

void PackExtractDialog::radioToggled()
{
    const bool createTilesheet = ui->radioSingle->isChecked();
    ui->checkBox2x->setEnabled(createTilesheet);
    ui->wholeWordCheck->setEnabled(createTilesheet);
}
