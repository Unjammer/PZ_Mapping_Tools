/*
 * Copyright 2014, Tim Baker <treectrl@users.sf.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "packcompare.h"
#include "ui_packcompare.h"

#include "zprogress.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {

QString csvCell(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString shortHash(const QByteArray &hash)
{
    if (hash.isEmpty())
        return QStringLiteral("-");
    return PackFile::sha256Text(hash).left(16);
}

void fillRect(QImage *image, const QRect &rect, const QColor &color)
{
    QPainter painter(image);
    painter.fillRect(rect, color);
}

void addDemoTexture(PackPage *page, const QString &name,
                    const QRect &packedRect, const QColor &color,
                    const QSize &fullSize = QSize(64, 64),
                    const QPoint &offset = QPoint(16, 16))
{
    fillRect(&page->image, packedRect, color);
    page->mInfo += PackSubTexInfo(
                packedRect.x(), packedRect.y(),
                packedRect.width(), packedRect.height(),
                offset.x(), offset.y(),
                fullSize.width(), fullSize.height(), name);
}

bool createDemoPacks(const QString &firstPath, const QString &secondPath,
                     QString *errorString)
{
    PackPage firstPage;
    firstPage.name = QStringLiteral("demo_page");
    firstPage.image = QImage(256, 256, QImage::Format_ARGB32);
    firstPage.image.fill(Qt::transparent);
    addDemoTexture(&firstPage, QStringLiteral("same_0"),
                   QRect(0, 0, 32, 32), QColor(40, 180, 90));
    addDemoTexture(&firstPage, QStringLiteral("metadata_0"),
                   QRect(40, 0, 32, 32), QColor(70, 130, 230));
    addDemoTexture(&firstPage, QStringLiteral("pixels_0"),
                   QRect(80, 0, 32, 32), QColor(230, 80, 70));
    addDemoTexture(&firstPage, QStringLiteral("removed_0"),
                   QRect(120, 0, 32, 32), QColor(245, 190, 50));
    addDemoTexture(&firstPage, QStringLiteral("duplicate_0"),
                   QRect(160, 0, 32, 32), QColor(150, 85, 210));
    addDemoTexture(&firstPage, QStringLiteral("both_0"),
                   QRect(0, 80, 32, 32), QColor(210, 125, 40));

    PackPage secondPage;
    secondPage.name = QStringLiteral("demo_page");
    secondPage.image = QImage(256, 256, QImage::Format_ARGB32);
    secondPage.image.fill(Qt::transparent);
    addDemoTexture(&secondPage, QStringLiteral("same_0"),
                   QRect(0, 0, 32, 32), QColor(40, 180, 90));
    addDemoTexture(&secondPage, QStringLiteral("metadata_0"),
                   QRect(40, 40, 32, 32), QColor(70, 130, 230));
    addDemoTexture(&secondPage, QStringLiteral("pixels_0"),
                   QRect(80, 0, 32, 32), QColor(235, 60, 210));
    addDemoTexture(&secondPage, QStringLiteral("added_0"),
                   QRect(120, 0, 32, 32), QColor(40, 205, 220));
    addDemoTexture(&secondPage, QStringLiteral("duplicate_0"),
                   QRect(160, 0, 32, 32), QColor(150, 85, 210));
    addDemoTexture(&secondPage, QStringLiteral("duplicate_0"),
                   QRect(200, 0, 32, 32), QColor(150, 85, 210));
    addDemoTexture(&secondPage, QStringLiteral("both_0"),
                   QRect(40, 80, 32, 32), QColor(40, 175, 210));

    PackFile first;
    first.addPage(firstPage);
    if (!first.write(firstPath)) {
        *errorString = first.errorString();
        return false;
    }
    PackFile second;
    second.addPage(secondPage);
    if (!second.write(secondPath)) {
        *errorString = second.errorString();
        return false;
    }
    return true;
}

} // anonymous namespace

PackCompare::PackCompare(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PackCompare)
{
    ui->setupUi(this);

    ui->statusFilter->addItem(tr("All statuses"), -1);
    ui->statusFilter->addItem(tr("All changes"), 0);
    ui->statusFilter->addItem(tr("Added"), int(Added) + 1);
    ui->statusFilter->addItem(tr("Removed"), int(Removed) + 1);
    ui->statusFilter->addItem(tr("Pixel changes"), 100);
    ui->statusFilter->addItem(tr("Metadata changes"), 101);
    ui->statusFilter->addItem(tr("Duplicates"), int(Duplicate) + 1);
    ui->statusFilter->addItem(tr("Unchanged"), int(Unchanged) + 1);

    ui->comparisonTable->setSelectionBehavior(
                QAbstractItemView::SelectRows);
    ui->comparisonTable->setSelectionMode(
                QAbstractItemView::SingleSelection);
    ui->comparisonTable->setEditTriggers(
                QAbstractItemView::NoEditTriggers);
    ui->comparisonTable->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::ResizeToContents);
    ui->comparisonTable->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::Stretch);
    for (int column = 2; column < ui->comparisonTable->columnCount();
         ++column) {
        ui->comparisonTable->horizontalHeader()->setSectionResizeMode(
                    column, QHeaderView::ResizeToContents);
    }

    connect(ui->packBrowse1, &QAbstractButton::clicked,
            this, &PackCompare::browse1);
    connect(ui->packBrowse2, &QAbstractButton::clicked,
            this, &PackCompare::browse2);
    connect(ui->compareButton, &QAbstractButton::clicked,
            this, &PackCompare::compare);
    connect(ui->swapButton, &QAbstractButton::clicked,
            this, &PackCompare::swapPacks);
    connect(ui->exportButton, &QAbstractButton::clicked,
            this, &PackCompare::exportReport);
    connect(ui->copyButton, &QAbstractButton::clicked,
            this, &PackCompare::copyReport);
    connect(ui->searchEdit, &QLineEdit::textChanged,
            this, &PackCompare::updateFilter);
    connect(ui->statusFilter,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PackCompare::updateFilter);
    connect(ui->comparisonTable, &QTableWidget::itemSelectionChanged,
            this, &PackCompare::selectedTextureChanged);
    connect(ui->actionClose, &QAction::triggered,
            this, &QWidget::close);
    connect(ui->actionExportReport, &QAction::triggered,
            this, &PackCompare::exportReport);

    ui->exportButton->setEnabled(false);
    ui->copyButton->setEnabled(false);
    ui->actionExportReport->setEnabled(false);
    selectedTextureChanged();
}

PackCompare::~PackCompare()
{
    delete ui;
}

void PackCompare::browse1()
{
    const QString fileName = QFileDialog::getOpenFileName(
                this, tr("Choose first pack file"),
                ui->packEdit1->text(), tr("Pack files (*.pack)"));
    if (!fileName.isEmpty())
        ui->packEdit1->setText(QDir::toNativeSeparators(fileName));
}

void PackCompare::browse2()
{
    const QString fileName = QFileDialog::getOpenFileName(
                this, tr("Choose second pack file"),
                ui->packEdit2->text(), tr("Pack files (*.pack)"));
    if (!fileName.isEmpty())
        ui->packEdit2->setText(QDir::toNativeSeparators(fileName));
}

void PackCompare::compare()
{
    QString error;
    if (!loadComparison(ui->packEdit1->text().trimmed(),
                        ui->packEdit2->text().trimmed(), &error)) {
        QMessageBox::warning(this, tr("Pack comparison failed"), error);
    }
}

void PackCompare::swapPacks()
{
    const QString first = ui->packEdit1->text();
    ui->packEdit1->setText(ui->packEdit2->text());
    ui->packEdit2->setText(first);
    if (!ui->packEdit1->text().isEmpty() &&
            !ui->packEdit2->text().isEmpty()) {
        compare();
    }
}

bool PackCompare::loadComparison(const QString &file1,
                                 const QString &file2,
                                 QString *errorString)
{
    if (file1.isEmpty() || file2.isEmpty()) {
        *errorString = tr("Choose both .pack files.");
        return false;
    }

    PROGRESS progress(tr("Reading first pack"), this);
    if (!mPackFile1.read(file1)) {
        *errorString = mPackFile1.errorString();
        return false;
    }
    progress.update(tr("Reading second pack"));
    if (!mPackFile2.read(file2)) {
        *errorString = mPackFile2.errorString();
        return false;
    }

    progress.update(tr("Hashing and comparing textures"));
    buildComparison();
    updateSummary();
    rebuildTable();
    ui->exportButton->setEnabled(true);
    ui->copyButton->setEnabled(true);
    ui->actionExportReport->setEnabled(true);
    return true;
}

void PackCompare::buildComparison()
{
    QMap<QString, QVector<Location>> first;
    QMap<QString, QVector<Location>> second;

    const auto collect = [](const PackFile &pack,
                            QMap<QString, QVector<Location>> *locations) {
        for (int pageIndex = 0; pageIndex < pack.pages().size();
             ++pageIndex) {
            const PackPage &page = pack.pages().at(pageIndex);
            for (int textureIndex = 0;
                 textureIndex < page.mInfo.size(); ++textureIndex) {
                const PackSubTexInfo &texture =
                        page.mInfo.at(textureIndex);
                Location location;
                location.pageIndex = pageIndex;
                location.textureIndex = textureIndex;
                location.pixelHash =
                        PackFile::textureSha256(page, texture);
                location.metadataHash =
                        PackFile::metadataSha256(page, texture);
                (*locations)[texture.name].append(location);
            }
        }
    };
    collect(mPackFile1, &first);
    collect(mPackFile2, &second);

    QSet<QString> allNames;
    for (auto iterator = first.constBegin();
         iterator != first.constEnd(); ++iterator) {
        allNames += iterator.key();
    }
    for (auto iterator = second.constBegin();
         iterator != second.constEnd(); ++iterator) {
        allNames += iterator.key();
    }
    QStringList names = allNames.values();
    std::sort(names.begin(), names.end(),
              [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });

    mRows.clear();
    mRows.reserve(names.size());
    for (const QString &name : names) {
        ComparisonRow row;
        row.name = name;
        row.pack1 = first.value(name);
        row.pack2 = second.value(name);
        if (row.pack1.size() > 1 || row.pack2.size() > 1) {
            row.status = Duplicate;
        } else if (row.pack1.isEmpty()) {
            row.status = Added;
        } else if (row.pack2.isEmpty()) {
            row.status = Removed;
        } else {
            const bool samePixels =
                    row.pack1.first().pixelHash ==
                    row.pack2.first().pixelHash;
            const bool sameMetadata =
                    row.pack1.first().metadataHash ==
                    row.pack2.first().metadataHash;
            if (samePixels && sameMetadata)
                row.status = Unchanged;
            else if (!samePixels && !sameMetadata)
                row.status = ModifiedPixelsAndMetadata;
            else if (!samePixels)
                row.status = ModifiedPixels;
            else
                row.status = ModifiedMetadata;
        }
        mRows.append(row);
    }
}

QString PackCompare::statusText(Status status) const
{
    switch (status) {
    case Added: return tr("Added");
    case Removed: return tr("Removed");
    case ModifiedPixels: return tr("Pixels changed");
    case ModifiedMetadata: return tr("Metadata changed");
    case ModifiedPixelsAndMetadata:
        return tr("Pixels + metadata");
    case Duplicate: return tr("Duplicate name");
    case Unchanged: return tr("Unchanged");
    }
    return QString();
}

QColor PackCompare::statusColor(Status status) const
{
    switch (status) {
    case Added: return QColor(50, 150, 80);
    case Removed: return QColor(200, 70, 65);
    case ModifiedPixels: return QColor(155, 80, 190);
    case ModifiedMetadata: return QColor(210, 135, 40);
    case ModifiedPixelsAndMetadata: return QColor(190, 70, 130);
    case Duplicate: return QColor(205, 160, 35);
    case Unchanged: return QColor(115, 125, 135);
    }
    return QColor();
}

bool PackCompare::statusMatchesFilter(Status status) const
{
    const int filter = ui->statusFilter->currentData().toInt();
    if (filter == -1)
        return true;
    if (filter == 0)
        return status != Unchanged;
    if (filter == 100) {
        return status == ModifiedPixels ||
                status == ModifiedPixelsAndMetadata;
    }
    if (filter == 101) {
        return status == ModifiedMetadata ||
                status == ModifiedPixelsAndMetadata;
    }
    return filter == int(status) + 1;
}

void PackCompare::updateFilter()
{
    rebuildTable();
}

void PackCompare::rebuildTable()
{
    const QString search = ui->searchEdit->text().trimmed();
    ui->comparisonTable->setSortingEnabled(false);
    ui->comparisonTable->setRowCount(0);

    int visibleCount = 0;
    for (int index = 0; index < mRows.size(); ++index) {
        const ComparisonRow &row = mRows.at(index);
        if (!statusMatchesFilter(row.status) ||
                (!search.isEmpty() &&
                 !row.name.contains(search, Qt::CaseInsensitive))) {
            continue;
        }

        const int tableRow = ui->comparisonTable->rowCount();
        ui->comparisonTable->insertRow(tableRow);
        QTableWidgetItem *statusItem =
                new QTableWidgetItem(statusText(row.status));
        statusItem->setData(Qt::UserRole, index);
        statusItem->setForeground(statusColor(row.status));
        QFont statusFont = statusItem->font();
        statusFont.setBold(true);
        statusItem->setFont(statusFont);
        ui->comparisonTable->setItem(tableRow, 0, statusItem);

        QTableWidgetItem *nameItem =
                new QTableWidgetItem(row.name);
        nameItem->setData(Qt::UserRole, index);
        ui->comparisonTable->setItem(tableRow, 1, nameItem);

        const auto pageName = [this](const PackFile &pack,
                                     const QVector<Location> &locations) {
            if (locations.isEmpty())
                return QStringLiteral("-");
            const PackPage *page = pageFor(pack, locations.first());
            QString text = page ? page->name : QStringLiteral("?");
            if (locations.size() > 1)
                text += tr(" (%1 copies)").arg(locations.size());
            return text;
        };
        ui->comparisonTable->setItem(
                    tableRow, 2,
                    new QTableWidgetItem(pageName(mPackFile1, row.pack1)));
        ui->comparisonTable->setItem(
                    tableRow, 3,
                    new QTableWidgetItem(pageName(mPackFile2, row.pack2)));

        QString geometry = QStringLiteral("-");
        const PackSubTexInfo *firstTexture =
                row.pack1.isEmpty() ? nullptr :
                textureFor(mPackFile1, row.pack1.first());
        const PackSubTexInfo *secondTexture =
                row.pack2.isEmpty() ? nullptr :
                textureFor(mPackFile2, row.pack2.first());
        if (firstTexture || secondTexture) {
            const auto sizeText = [](const PackSubTexInfo *texture) {
                return texture
                        ? QStringLiteral("%1x%2")
                          .arg(texture->fx).arg(texture->fy)
                        : QStringLiteral("-");
            };
            geometry = sizeText(firstTexture) +
                    QStringLiteral(" / ") + sizeText(secondTexture);
        }
        ui->comparisonTable->setItem(
                    tableRow, 4, new QTableWidgetItem(geometry));

        const QByteArray firstHash = row.pack1.isEmpty()
                ? QByteArray() : row.pack1.first().pixelHash;
        const QByteArray secondHash = row.pack2.isEmpty()
                ? QByteArray() : row.pack2.first().pixelHash;
        QTableWidgetItem *firstHashItem =
                new QTableWidgetItem(shortHash(firstHash));
        firstHashItem->setToolTip(PackFile::sha256Text(firstHash));
        ui->comparisonTable->setItem(tableRow, 5, firstHashItem);
        QTableWidgetItem *secondHashItem =
                new QTableWidgetItem(shortHash(secondHash));
        secondHashItem->setToolTip(PackFile::sha256Text(secondHash));
        ui->comparisonTable->setItem(tableRow, 6, secondHashItem);
        ++visibleCount;
    }

    ui->comparisonTable->setSortingEnabled(true);
    ui->visibleSummary->setText(
                tr("%1 of %2 texture names shown")
                .arg(visibleCount).arg(mRows.size()));
    if (ui->comparisonTable->rowCount() > 0)
        ui->comparisonTable->selectRow(0);
    else
        selectedTextureChanged();
}

const PackPage *PackCompare::pageFor(
        const PackFile &pack, const Location &location) const
{
    if (location.pageIndex < 0 ||
            location.pageIndex >= pack.pages().size()) {
        return nullptr;
    }
    return &pack.pages().at(location.pageIndex);
}

const PackSubTexInfo *PackCompare::textureFor(
        const PackFile &pack, const Location &location) const
{
    const PackPage *page = pageFor(pack, location);
    if (!page || location.textureIndex < 0 ||
            location.textureIndex >= page->mInfo.size()) {
        return nullptr;
    }
    return &page->mInfo.at(location.textureIndex);
}

QImage PackCompare::imageFor(
        const PackFile &pack,
        const QVector<Location> &locations) const
{
    if (locations.isEmpty())
        return QImage();
    const PackPage *page = pageFor(pack, locations.first());
    const PackSubTexInfo *texture =
            textureFor(pack, locations.first());
    if (!page || !texture)
        return QImage();
    return PackFile::extractTexture(*page, *texture);
}

QString PackCompare::locationDescription(
        const PackFile &pack,
        const QVector<Location> &locations) const
{
    if (locations.isEmpty())
        return tr("Not present");

    QStringList descriptions;
    for (const Location &location : locations) {
        const PackPage *page = pageFor(pack, location);
        const PackSubTexInfo *texture =
                textureFor(pack, location);
        if (!page || !texture)
            continue;
        descriptions += tr(
                    "Page %1 | packed %2,%3 %4x%5 | "
                    "offset %6,%7 | canvas %8x%9 | alpha %10\n"
                    "Pixel SHA-256: %11\nMetadata SHA-256: %12")
                .arg(page->name)
                .arg(texture->x).arg(texture->y)
                .arg(texture->w).arg(texture->h)
                .arg(texture->ox).arg(texture->oy)
                .arg(texture->fx).arg(texture->fy)
                .arg(page->mask ? tr("yes") : tr("no"))
                .arg(PackFile::sha256Text(location.pixelHash))
                .arg(PackFile::sha256Text(location.metadataHash));
    }
    return descriptions.join(QStringLiteral("\n\n"));
}

void PackCompare::setPreview(QLabel *label, const QImage &image,
                             const QString &emptyText)
{
    if (image.isNull()) {
        label->setPixmap(QPixmap());
        label->setText(emptyText);
        return;
    }
    label->setText(QString());
    label->setPixmap(QPixmap::fromImage(image).scaled(
                         QSize(360, 280), Qt::KeepAspectRatio,
                         Qt::FastTransformation));
}

QImage PackCompare::differenceImage(
        const QImage &firstImage,
        const QImage &secondImage) const
{
    if (firstImage.isNull() && secondImage.isNull())
        return QImage();

    const QImage first = firstImage.convertToFormat(
                QImage::Format_ARGB32);
    const QImage second = secondImage.convertToFormat(
                QImage::Format_ARGB32);
    const QSize size(qMax(first.width(), second.width()),
                     qMax(first.height(), second.height()));
    QImage difference(size, QImage::Format_ARGB32);
    difference.fill(Qt::transparent);

    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const QRgb firstPixel =
                    x < first.width() && y < first.height()
                    ? first.pixel(x, y) : qRgba(0, 0, 0, 0);
            const QRgb secondPixel =
                    x < second.width() && y < second.height()
                    ? second.pixel(x, y) : qRgba(0, 0, 0, 0);
            if (firstPixel == secondPixel) {
                const int gray = qGray(firstPixel);
                difference.setPixel(
                            x, y, qRgba(gray, gray, gray,
                                      qAlpha(firstPixel) / 3));
            } else if (qAlpha(firstPixel) == 0) {
                difference.setPixel(x, y, qRgba(30, 235, 90, 230));
            } else if (qAlpha(secondPixel) == 0) {
                difference.setPixel(x, y, qRgba(240, 60, 45, 230));
            } else {
                difference.setPixel(x, y, qRgba(240, 50, 220, 240));
            }
        }
    }
    return difference;
}

void PackCompare::selectedTextureChanged()
{
    const QList<QTableWidgetItem *> selected =
            ui->comparisonTable->selectedItems();
    if (selected.isEmpty()) {
        ui->detailLabel->setText(tr("Select a texture to inspect it."));
        setPreview(ui->preview1, QImage(), tr("No texture selected"));
        setPreview(ui->preview2, QImage(), tr("No texture selected"));
        setPreview(ui->previewDiff, QImage(), tr("No difference"));
        return;
    }

    const int index = selected.first()->data(Qt::UserRole).toInt();
    if (index < 0 || index >= mRows.size())
        return;
    const ComparisonRow &row = mRows.at(index);
    const QImage first = imageFor(mPackFile1, row.pack1);
    const QImage second = imageFor(mPackFile2, row.pack2);
    setPreview(ui->preview1, first, tr("Not present in pack 1"));
    setPreview(ui->preview2, second, tr("Not present in pack 2"));
    setPreview(ui->previewDiff, differenceImage(first, second),
               tr("No difference image"));
    ui->detailLabel->setText(
                tr("%1 - %2\n\nPack 1\n%3\n\nPack 2\n%4")
                .arg(row.name, statusText(row.status),
                     locationDescription(mPackFile1, row.pack1),
                     locationDescription(mPackFile2, row.pack2)));
}

void PackCompare::updateSummary()
{
    const auto summary = [](const PackFile &pack) {
        return QObject::tr(
                    "Format v%1 | %2 pages | %3 textures | SHA-256 %4")
                .arg(pack.version())
                .arg(pack.pages().size())
                .arg(pack.textureCount())
                .arg(PackFile::sha256Text(pack.fileSha256()));
    };
    ui->packSummary1->setText(summary(mPackFile1));
    ui->packSummary1->setToolTip(mPackFile1.fileName());
    ui->packSummary2->setText(summary(mPackFile2));
    ui->packSummary2->setToolTip(mPackFile2.fileName());

    QMap<Status, int> counts;
    for (const ComparisonRow &row : mRows)
        ++counts[row.status];
    ui->comparisonSummary->setText(
                tr("Added %1 | Removed %2 | Pixel changes %3 | "
                   "Metadata changes %4 | Both %5 | Duplicates %6 | "
                   "Unchanged %7")
                .arg(counts.value(Added))
                .arg(counts.value(Removed))
                .arg(counts.value(ModifiedPixels))
                .arg(counts.value(ModifiedMetadata))
                .arg(counts.value(ModifiedPixelsAndMetadata))
                .arg(counts.value(Duplicate))
                .arg(counts.value(Unchanged)));
}

QString PackCompare::csvReport() const
{
    QString report;
    QTextStream stream(&report);
    stream << "# PZTools .pack comparison\n";
    stream << "# Pack 1," << csvCell(mPackFile1.fileName()) << ","
           << PackFile::sha256Text(mPackFile1.fileSha256()) << "\n";
    stream << "# Pack 2," << csvCell(mPackFile2.fileName()) << ","
           << PackFile::sha256Text(mPackFile2.fileSha256()) << "\n";
    stream << "status,name,pack1_page,pack2_page,"
              "pack1_pixel_sha256,pack2_pixel_sha256,"
              "pack1_metadata_sha256,pack2_metadata_sha256\n";
    for (const ComparisonRow &row : mRows) {
        const auto pageName = [this](const PackFile &pack,
                                     const QVector<Location> &locations) {
            if (locations.isEmpty())
                return QString();
            const PackPage *page = pageFor(pack, locations.first());
            return page ? page->name : QString();
        };
        const auto hash = [](const QVector<Location> &locations,
                             bool metadata) {
            if (locations.isEmpty())
                return QString();
            return PackFile::sha256Text(
                        metadata ? locations.first().metadataHash
                                 : locations.first().pixelHash);
        };
        stream << csvCell(statusText(row.status)) << ","
               << csvCell(row.name) << ","
               << csvCell(pageName(mPackFile1, row.pack1)) << ","
               << csvCell(pageName(mPackFile2, row.pack2)) << ","
               << hash(row.pack1, false) << ","
               << hash(row.pack2, false) << ","
               << hash(row.pack1, true) << ","
               << hash(row.pack2, true) << "\n";
    }
    return report;
}

void PackCompare::exportReport()
{
    const QString fileName = QFileDialog::getSaveFileName(
                this, tr("Export pack comparison"),
                QStringLiteral("pack-comparison.csv"),
                tr("CSV files (*.csv);;Text files (*.txt)"));
    if (fileName.isEmpty())
        return;
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Could not open %1.")
                             .arg(QDir::toNativeSeparators(fileName)));
        return;
    }
    const QByteArray bytes = csvReport().toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Could not write %1.")
                             .arg(QDir::toNativeSeparators(fileName)));
    }
}

void PackCompare::copyReport()
{
    QApplication::clipboard()->setText(csvReport());
    ui->statusbar->showMessage(tr("Comparison report copied."), 3000);
}

bool PackCompare::runSelfTest(QString *summary,
                              QString *errorString)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the pack-tool test directory");
        return false;
    }
    const QString firstPath =
            temporary.filePath(QStringLiteral("first.pack"));
    const QString secondPath =
            temporary.filePath(QStringLiteral("second.pack"));
    if (!createDemoPacks(firstPath, secondPath, errorString))
        return false;

    PackCompare comparator;
    if (!comparator.loadComparison(firstPath, secondPath, errorString))
        return false;

    QMap<Status, int> counts;
    for (const ComparisonRow &row : comparator.mRows)
        ++counts[row.status];
    if (counts.value(Added) != 1 ||
            counts.value(Removed) != 1 ||
            counts.value(ModifiedPixels) != 1 ||
            counts.value(ModifiedMetadata) != 1 ||
            counts.value(ModifiedPixelsAndMetadata) != 1 ||
            counts.value(Duplicate) != 1 ||
            counts.value(Unchanged) != 1 ||
            comparator.mPackFile1.fileSha256().isEmpty() ||
            comparator.mPackFile2.fileSha256().isEmpty()) {
        *errorString = QStringLiteral(
                    "Unexpected comparison status or SHA-256 result");
        return false;
    }

    QFile validFile(secondPath);
    if (!validFile.open(QIODevice::ReadOnly)) {
        *errorString = QStringLiteral(
                    "Could not reopen the generated pack for truncation");
        return false;
    }
    QByteArray truncatedData = validFile.readAll();
    validFile.close();
    if (truncatedData.size() < 32) {
        *errorString = QStringLiteral(
                    "Generated pack is unexpectedly small");
        return false;
    }
    truncatedData.chop(17);
    const QString truncatedPath =
            temporary.filePath(QStringLiteral("truncated.pack"));
    QFile truncatedFile(truncatedPath);
    if (!truncatedFile.open(QIODevice::WriteOnly) ||
            truncatedFile.write(truncatedData) != truncatedData.size()) {
        *errorString = QStringLiteral(
                    "Could not create the truncated pack test input");
        return false;
    }
    truncatedFile.close();
    PackFile truncatedPack;
    if (truncatedPack.read(truncatedPath) ||
            truncatedPack.errorString().isEmpty()) {
        *errorString = QStringLiteral(
                    "A truncated pack was not rejected explicitly");
        return false;
    }

    *summary = QStringLiteral(
                "2 version-1 packs, all 7 status classes, file/metadata/pixel "
                "SHA-256 and truncated-input rejection verified");
    return true;
}

bool PackCompare::renderValidation(const QString &outputFile,
                                   QString *errorString)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the pack-render directory");
        return false;
    }
    const QString firstPath =
            temporary.filePath(QStringLiteral("baseline.pack"));
    const QString secondPath =
            temporary.filePath(QStringLiteral("candidate.pack"));
    if (!createDemoPacks(firstPath, secondPath, errorString))
        return false;

    PackCompare comparator;
    comparator.resize(1440, 900);
    comparator.ui->packEdit1->setText(firstPath);
    comparator.ui->packEdit2->setText(secondPath);
    if (!comparator.loadComparison(firstPath, secondPath, errorString))
        return false;
    comparator.show();
    QApplication::processEvents();

    for (int row = 0; row < comparator.ui->comparisonTable->rowCount();
         ++row) {
        const int index = comparator.ui->comparisonTable
                ->item(row, 0)->data(Qt::UserRole).toInt();
        if (index >= 0 && index < comparator.mRows.size() &&
                comparator.mRows.at(index).status == ModifiedPixels) {
            comparator.ui->comparisonTable->selectRow(row);
            break;
        }
    }
    QApplication::processEvents();

    const QFileInfo outputInfo(outputFile);
    if (!QDir().mkpath(outputInfo.absolutePath()) ||
            !comparator.grab().save(outputFile, "PNG")) {
        *errorString = QStringLiteral("Could not save %1").arg(outputFile);
        return false;
    }
    comparator.close();
    return true;
}
