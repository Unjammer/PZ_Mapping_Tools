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

#include "tiledefcompare.h"
#include "ui_tiledefcompare.h"

#include "preferences.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zprogress.h"

#include "tile.h"
#include "tileset.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <QToolButton>

using namespace Tiled::Internal;

#if defined(Q_OS_WIN) && (_MSC_VER >= 1600)
// Hmmmm.  libtiled.dll defines the Properties class as so:
// class TILEDSHARED_EXPORT Properties : public QMap<QString,QString>
// Suddenly I'm getting a 'multiply-defined symbol' error.
// I found the solution here:
// http://www.archivum.info/qt-interest@trolltech.com/2005-12/00242/RE-Linker-Problem-while-using-QMap.html
template class __declspec(dllimport) QMap<QString, QString>;
#endif

namespace {

enum DifferenceKind {
    ModifiedProperties = 0,
    OnlyInFile1 = 1,
    OnlyInFile2 = 2
};

struct TileDifference
{
    QString tilesetName;
    int tileId = -1;
    TileDefTile *tile1 = nullptr;
    TileDefTile *tile2 = nullptr;
    DifferenceKind kind = ModifiedProperties;
};

struct TileDefAnalysis
{
    QStringList unique1;
    QStringList unique2;
    QStringList structuralDifferences;
    QList<TileDifference> tileDifferences;
};

QString fileSha256(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

QString propertyMapString(const QMap<QString, QString> &properties)
{
    QStringList values;
    for (auto it = properties.constBegin();
         it != properties.constEnd(); ++it) {
        values += QStringLiteral("%1=%2").arg(it.key(), it.value());
    }
    return values.join(QStringLiteral("; "));
}

int changedPropertyCount(TileDefTile *tile1, TileDefTile *tile2)
{
    if (tile1 == nullptr || tile2 == nullptr)
        return tile1 == nullptr
                ? (tile2 == nullptr ? 0 : tile2->mProperties.size())
                : tile1->mProperties.size();
    QSet<QString> keys;
    for (auto it = tile1->mProperties.constBegin();
         it != tile1->mProperties.constEnd(); ++it) {
        keys += it.key();
    }
    for (auto it = tile2->mProperties.constBegin();
         it != tile2->mProperties.constEnd(); ++it) {
        keys += it.key();
    }
    int changed = 0;
    for (const QString &key : keys) {
        if (tile1->mProperties.value(key) !=
                tile2->mProperties.value(key) ||
                tile1->mProperties.contains(key) !=
                tile2->mProperties.contains(key)) {
            ++changed;
        }
    }
    return changed;
}

void appendTilesetDifferences(
        TileDefAnalysis *analysis, const QString &tilesetName,
        TileDefTileset *tileset1, TileDefTileset *tileset2)
{
    const int count1 = tileset1 == nullptr
            ? 0 : tileset1->mTiles.size();
    const int count2 = tileset2 == nullptr
            ? 0 : tileset2->mTiles.size();
    const int maximum = qMax(count1, count2);
    for (int tileId = 0; tileId < maximum; ++tileId) {
        TileDefTile *tile1 = tileset1 == nullptr
                ? nullptr : tileset1->tileAt(tileId);
        TileDefTile *tile2 = tileset2 == nullptr
                ? nullptr : tileset2->tileAt(tileId);
        if (tile1 != nullptr && tile2 != nullptr &&
                tile1->mProperties == tile2->mProperties) {
            continue;
        }
        TileDifference difference;
        difference.tilesetName = tilesetName;
        difference.tileId = tileId;
        difference.tile1 = tile1;
        difference.tile2 = tile2;
        difference.kind = tile1 == nullptr
                ? OnlyInFile2
                : tile2 == nullptr
                  ? OnlyInFile1 : ModifiedProperties;
        analysis->tileDifferences += difference;
    }
}

TileDefAnalysis analyzeTileDefs(
        TileDefFile &file1, TileDefFile &file2, PROGRESS *progress = nullptr)
{
    TileDefAnalysis analysis;
    QSet<QString> names1;
    QSet<QString> names2;
    for (TileDefTileset *tileset : file1.tilesets())
        names1 += tileset->mName;
    for (TileDefTileset *tileset : file2.tilesets())
        names2 += tileset->mName;

    analysis.unique1 = (names1 - names2).values();
    analysis.unique2 = (names2 - names1).values();
    analysis.unique1.sort(Qt::CaseInsensitive);
    analysis.unique2.sort(Qt::CaseInsensitive);

    QStringList allNames = (names1 | names2).values();
    allNames.sort(Qt::CaseInsensitive);
    int processed = 0;
    for (const QString &name : allNames) {
        if (progress && (processed % 16 == 0)) {
            progress->update(
                        QObject::tr("Comparing tileset %1 of %2\n%3")
                        .arg(processed + 1).arg(allNames.size())
                        .arg(name));
        }
        ++processed;
        TileDefTileset *tileset1 = file1.tileset(name);
        TileDefTileset *tileset2 = file2.tileset(name);
        if (tileset1 != nullptr && tileset2 != nullptr) {
            QStringList fields;
            if (tileset1->mID != tileset2->mID) {
                fields += QObject::tr("ID %1 vs %2")
                        .arg(tileset1->mID).arg(tileset2->mID);
            }
            if (tileset1->mImageSource != tileset2->mImageSource) {
                fields += QObject::tr("image \"%1\" vs \"%2\"")
                        .arg(tileset1->mImageSource,
                             tileset2->mImageSource);
            }
            if (tileset1->mColumns != tileset2->mColumns ||
                    tileset1->mRows != tileset2->mRows) {
                fields += QObject::tr("grid %1x%2 vs %3x%4")
                        .arg(tileset1->mColumns)
                        .arg(tileset1->mRows)
                        .arg(tileset2->mColumns)
                        .arg(tileset2->mRows);
            }
            if (tileset1->mTiles.size() !=
                    tileset2->mTiles.size()) {
                fields += QObject::tr("%1 vs %2 tile records")
                        .arg(tileset1->mTiles.size())
                        .arg(tileset2->mTiles.size());
            }
            if (!fields.isEmpty()) {
                analysis.structuralDifferences +=
                        QStringLiteral("%1: %2")
                        .arg(name, fields.join(QStringLiteral(", ")));
            }
        }
        appendTilesetDifferences(
                    &analysis, name, tileset1, tileset2);
    }
    return analysis;
}

QString htmlList(const QStringList &values)
{
    if (values.isEmpty())
        return QStringLiteral("<i>None</i>");
    QString html = QStringLiteral("<ul>");
    for (const QString &value : values)
        html += QStringLiteral("<li>%1</li>").arg(value.toHtmlEscaped());
    return html + QStringLiteral("</ul>");
}

TileDefTileset *createTestTileset(
        const QString &name, int id, int tileCount)
{
    TileDefTileset *tileset = new TileDefTileset;
    tileset->mName = name;
    tileset->mImageSource = name + QStringLiteral(".png");
    tileset->mColumns = 0;
    tileset->mRows = 0;
    tileset->mID = id;
    tileset->resize(qMax(1, tileCount), 1);
    return tileset;
}

} // anonymous namespace

TileDefCompare::TileDefCompare(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::TileDefCompare)
{
    ui->setupUi(this);
    resize(qMax(width(), 1180), qMax(height(), 760));
    ui->packEdit1->setEditable(true);
    ui->packEdit2->setEditable(true);

    ui->gridLayout->removeItem(ui->horizontalLayout);
    ui->gridLayout->addLayout(ui->horizontalLayout, 5, 0);

    QWidget *filterWidget = new QWidget(this);
    QHBoxLayout *filterLayout = new QHBoxLayout(filterWidget);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->addWidget(new QLabel(tr("Filter:"), filterWidget));
    mSearchEdit = new QLineEdit(filterWidget);
    mSearchEdit->setClearButtonEnabled(true);
    mSearchEdit->setPlaceholderText(
                tr("Tileset, tile ID, property, value, or status"));
    filterLayout->addWidget(mSearchEdit, 1);
    mDifferenceFilter = new QComboBox(filterWidget);
    mDifferenceFilter->addItem(tr("All tile differences"), -1);
    mDifferenceFilter->addItem(tr("Modified properties"),
                               int(ModifiedProperties));
    mDifferenceFilter->addItem(tr("Only in File 1"),
                               int(OnlyInFile1));
    mDifferenceFilter->addItem(tr("Only in File 2"),
                               int(OnlyInFile2));
    filterLayout->addWidget(mDifferenceFilter);
    mVisibleSummary = new QLabel(tr("No comparison"), filterWidget);
    filterLayout->addWidget(mVisibleSummary);
    ui->gridLayout->addWidget(filterWidget, 4, 0);

    ui->textBrowser->setMaximumHeight(190);
    ui->listWidget->setMinimumWidth(420);
    ui->listWidget->setSelectionMode(
                QAbstractItemView::ExtendedSelection);
    ui->tileimage->setMinimumSize(180, 240);
    ui->tileimage->setAlignment(Qt::AlignCenter);
    ui->tileimage->setFrameShape(QFrame::StyledPanel);
    ui->tileimage->setText(tr("File 1 preview"));
    ui->tileimage->setToolTip(tr("File 1 tile preview"));
    mTileImage2 = new QLabel(tr("File 2 preview"), this);
    mTileImage2->setMinimumSize(180, 240);
    mTileImage2->setAlignment(Qt::AlignCenter);
    mTileImage2->setFrameShape(QFrame::StyledPanel);
    mTileImage2->setToolTip(tr("File 2 tile preview"));
    ui->horizontalLayout->insertWidget(2, mTileImage2);
    mPropertyDetails = new QTextBrowser(this);
    mPropertyDetails->setMinimumWidth(300);
    mPropertyDetails->setHtml(
                tr("<i>Select a tile difference to inspect its "
                   "properties.</i>"));
    ui->horizontalLayout->insertWidget(3, mPropertyDetails, 1);

    ui->use1->setText(tr("Use File 1"));
    ui->use2->setText(tr("Use File 2"));
    ui->saveMerged->setText(tr("Save merged..."));
    mCopyReportButton = new QToolButton(this);
    mCopyReportButton->setText(tr("Copy report"));
    ui->verticalLayout->addWidget(mCopyReportButton);
    mExportReportButton = new QToolButton(this);
    mExportReportButton->setText(tr("Export report..."));
    ui->verticalLayout->addWidget(mExportReportButton);

    connect(ui->packBrowse1, &QAbstractButton::clicked, this, &TileDefCompare::browse1);
    connect(ui->packBrowse2, &QAbstractButton::clicked, this, &TileDefCompare::browse2);
    connect(ui->switchButton, &QAbstractButton::clicked, this, &TileDefCompare::swapPaths);
    connect(ui->compare, &QAbstractButton::clicked, this, &TileDefCompare::compare);
    connect(ui->use1, &QAbstractButton::clicked, this, &TileDefCompare::use1);
    connect(ui->use2, &QAbstractButton::clicked, this, &TileDefCompare::use2);
    connect(ui->saveMerged, &QAbstractButton::clicked, this, &TileDefCompare::saveMerged);
    connect(ui->listWidget, &QListWidget::currentRowChanged, this, &TileDefCompare::currentRowChanged);
    connect(ui->listWidget, &QListWidget::itemSelectionChanged,
            this, &TileDefCompare::updateActions);
    connect(mSearchEdit, &QLineEdit::textChanged,
            this, &TileDefCompare::filterChanged);
    connect(mDifferenceFilter,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TileDefCompare::filterChanged);
    connect(mCopyReportButton, &QAbstractButton::clicked,
            this, &TileDefCompare::copyReport);
    connect(mExportReportButton, &QAbstractButton::clicked,
            this, &TileDefCompare::exportReport);

    readSettings();
    updateActions();
}

TileDefCompare::~TileDefCompare()
{
    writeSettings();
    delete ui;
}

void TileDefCompare::browse1()
{
    QString f = QFileDialog::getOpenFileName(this, tr("Choose .tiles File"),
                                             ui->packEdit1->currentText(),
                                             tr("Binary property files (*.tiles);;Text property files (*.tiles.txt)"));
    if (!f.isEmpty()) {
        addRecentFile1(QDir::toNativeSeparators(f));
        writeSettings();
    }
}

void TileDefCompare::browse2()
{
    QString f = QFileDialog::getOpenFileName(this, tr("Choose .tiles File"),
                                             ui->packEdit2->currentText(),
                                             tr("Binary property files (*.tiles);;Text property files (*.tiles.txt)"));
    if (!f.isEmpty()) {
        addRecentFile2(QDir::toNativeSeparators(f));
        writeSettings();
    }
}

void TileDefCompare::swapPaths()
{
    QString path1 = ui->packEdit1->currentText();
    QString path2 = ui->packEdit2->currentText();
    addRecentFile1(path2);
    addRecentFile2(path1);
}

void TileDefCompare::compare()
{
    const QString path1 = ui->packEdit1->currentText().trimmed();
    const QString path2 = ui->packEdit2->currentText().trimmed();
    if (path1.isEmpty() || path2.isEmpty()) {
        QMessageBox::information(
                    this, tr("Two files required"),
                    tr("Choose both .tiles files before comparing."));
        return;
    }

    PROGRESS progress(tr("Reading File 1\n%1")
                      .arg(QDir::toNativeSeparators(path1)), this);
    TileDefFileReader reader;
    if (!reader.read(path1, mTileDefFile1)) {
        QMessageBox::warning(this, tr("Error reading .tiles file"), mTileDefFile1.errorString());
        return;
    }

    progress.update(tr("Reading File 2\n%1")
                    .arg(QDir::toNativeSeparators(path2)));
    if (!reader.read(path2, mTileDefFile2)) {
        QMessageBox::warning(this, tr("Error reading .tiles file"), mTileDefFile2.errorString());
        return;
    }
    if (!reader.read(path2, mMergedFile)) {
        QMessageBox::warning(this, tr("Error reading .tiles file"), mMergedFile.errorString());
        return;
    }

    progress.update(tr("Calculating file hashes..."));
    const QString hash1 = fileSha256(path1);
    const QString hash2 = fileSha256(path2);
    TileDefAnalysis analysis =
            analyzeTileDefs(mTileDefFile1, mTileDefFile2, &progress);

    ui->listWidget->clear();
    mTileMap1.clear();
    mTileMap2.clear();
    mUseMap.clear();
    mDifferenceKind.clear();
    mCompared = true;

    int file1Tiles = 0;
    for (TileDefTileset *tileset : mTileDefFile1.tilesets())
        file1Tiles += tileset->mTiles.size();
    int file2Tiles = 0;
    for (TileDefTileset *tileset : mTileDefFile2.tilesets())
        file2Tiles += tileset->mTiles.size();

    QString summaryHtml =
            QStringLiteral(
                "<h3>.tiles comparison</h3>"
                "<table>"
                "<tr><td><b>File 1</b></td><td>%1</td>"
                "<td>%2 tilesets / %3 tiles</td></tr>"
                "<tr><td><b>SHA-256</b></td><td colspan=\"2\"><tt>%4</tt></td></tr>"
                "<tr><td><b>File 2 (merge base)</b></td><td>%5</td>"
                "<td>%6 tilesets / %7 tiles</td></tr>"
                "<tr><td><b>SHA-256</b></td><td colspan=\"2\"><tt>%8</tt></td></tr>"
                "</table>"
                "<p><b>%9 tile differences</b> | "
                "%10 structural differences | "
                "%11 tilesets only in File 1 | "
                "%12 tilesets only in File 2</p>")
            .arg(QDir::toNativeSeparators(path1).toHtmlEscaped())
            .arg(mTileDefFile1.tilesets().size())
            .arg(file1Tiles)
            .arg(hash1.toHtmlEscaped())
            .arg(QDir::toNativeSeparators(path2).toHtmlEscaped())
            .arg(mTileDefFile2.tilesets().size())
            .arg(file2Tiles)
            .arg(hash2.toHtmlEscaped())
            .arg(analysis.tileDifferences.size())
            .arg(analysis.structuralDifferences.size())
            .arg(analysis.unique1.size())
            .arg(analysis.unique2.size());
    summaryHtml += QStringLiteral(
                "<h4>Tilesets only in File 1</h4>%1"
                "<h4>Tilesets only in File 2</h4>%2"
                "<h4>Structural differences</h4>%3"
                "<p><i>The merged output starts from File 2. Property choices "
                "can be changed for tile records present in both files. "
                "File-1-only structures are reported but are not silently "
                "inserted into File 2.</i></p>")
            .arg(htmlList(analysis.unique1),
                 htmlList(analysis.unique2),
                 htmlList(analysis.structuralDifferences));
    ui->textBrowser->setHtml(summaryHtml);

    mReportPreamble =
            QStringLiteral("Category\tName or path\tDetails\n"
                           "File 1\t%1\tSHA-256 %2; %3 tilesets; %4 tiles\n"
                           "File 2 (merge base)\t%5\tSHA-256 %6; "
                           "%7 tilesets; %8 tiles\n")
            .arg(QDir::toNativeSeparators(path1), hash1)
            .arg(mTileDefFile1.tilesets().size())
            .arg(file1Tiles)
            .arg(QDir::toNativeSeparators(path2), hash2)
            .arg(mTileDefFile2.tilesets().size())
            .arg(file2Tiles);
    for (const QString &name : analysis.unique1) {
        mReportPreamble += QStringLiteral(
                    "Tileset only in File 1\t%1\t\n").arg(name);
    }
    for (const QString &name : analysis.unique2) {
        mReportPreamble += QStringLiteral(
                    "Tileset only in File 2\t%1\t\n").arg(name);
    }
    for (const QString &description :
         analysis.structuralDifferences) {
        mReportPreamble += QStringLiteral(
                    "Structural difference\t\t%1\n")
                .arg(description);
    }
    mReportPreamble += QLatin1Char('\n');

    for (const TileDifference &difference :
         analysis.tileDifferences) {
        QListWidgetItem *item = new QListWidgetItem(ui->listWidget);
        mTileMap1[item] = difference.tile1;
        mTileMap2[item] = difference.tile2;
        mDifferenceKind[item] = int(difference.kind);
        const int initialUse =
                difference.tile1 != nullptr &&
                difference.tile2 != nullptr ? 2 : 0;
        mUseMap[item] = initialUse;
        item->setText(listString(
                          initialUse,
                          difference.tile1,
                          difference.tile2));
        const QString properties1 = difference.tile1 == nullptr
                ? QString() : propertyMapString(
                      difference.tile1->mProperties);
        const QString properties2 = difference.tile2 == nullptr
                ? QString() : propertyMapString(
                      difference.tile2->mProperties);
        item->setData(
                    Qt::UserRole,
                    QStringLiteral("%1 %2 %3 %4 %5")
                    .arg(statusName(int(difference.kind)),
                         difference.tilesetName,
                         QString::number(difference.tileId),
                         properties1, properties2));
        item->setData(Qt::UserRole + 1, int(difference.kind));
        item->setToolTip(
                    tr("%1 - %2 tile %3")
                    .arg(statusName(int(difference.kind)),
                         difference.tilesetName)
                    .arg(difference.tileId));
        if (difference.kind == OnlyInFile1)
            item->setForeground(QBrush(QColor(70, 130, 220)));
        else if (difference.kind == OnlyInFile2)
            item->setForeground(QBrush(QColor(160, 90, 200)));
        else
            item->setForeground(QBrush(QColor(205, 135, 25)));

    }
    rebuildReportText();

    addRecentFile1(path1);
    addRecentFile2(path2);
    writeSettings();
    filterChanged();
    if (ui->listWidget->count() > 0)
        ui->listWidget->setCurrentRow(0);
    else
        mPropertyDetails->setHtml(
                    tr("<h3>No tile-property differences</h3>"
                       "<p>Review the summary above for structural or "
                       "tileset-level differences.</p>"));
    updateActions();
    statusBar()->showMessage(
                tr("Compared %1 tile records; found %2 tile difference(s).")
                .arg(file1Tiles + file2Tiles)
                .arg(analysis.tileDifferences.size()));
}

void TileDefCompare::use1()
{
    foreach (QListWidgetItem *item, ui->listWidget->selectedItems()) {
        TileDefTile *tile1 = mTileMap1[item];
        TileDefTile *tile2 = mTileMap2[item];
        if (tile1 == nullptr || tile2 == nullptr)
            continue;
        TileDefTileset *mergedTileset =
                mMergedFile.tileset(tile1->mTileset->mName);
        TileDefTile *mergedTile = mergedTileset == nullptr
                ? nullptr : mergedTileset->tileAt(tile1->id());
        if (mergedTile == nullptr)
            continue;
        mergedTile->mProperties = tile1->mProperties;
        mergedTile->mPropertyUI.FromProperties(
                    mergedTile->mProperties);
        mUseMap[item] = 1;
        item->setText(listString(1, tile1, tile2));
    }
    currentRowChanged(ui->listWidget->currentRow());
    rebuildReportText();
    updateActions();
}

void TileDefCompare::use2()
{
    foreach (QListWidgetItem *item, ui->listWidget->selectedItems()) {
        TileDefTile *tile1 = mTileMap1[item];
        TileDefTile *tile2 = mTileMap2[item];
        if (tile1 == nullptr || tile2 == nullptr)
            continue;
        TileDefTileset *mergedTileset =
                mMergedFile.tileset(tile2->mTileset->mName);
        TileDefTile *mergedTile = mergedTileset == nullptr
                ? nullptr : mergedTileset->tileAt(tile2->id());
        if (mergedTile == nullptr)
            continue;
        mergedTile->mProperties = tile2->mProperties;
        mergedTile->mPropertyUI.FromProperties(
                    mergedTile->mProperties);
        mUseMap[item] = 2;
        item->setText(listString(2, tile1, tile2));
    }
    currentRowChanged(ui->listWidget->currentRow());
    rebuildReportText();
    updateActions();
}

void TileDefCompare::saveMerged()
{
    if (!mCompared) {
        QMessageBox::information(
                    this, tr("No comparison"),
                    tr("Compare two files before saving a merged result."));
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                    mMergedFile.fileName(),
                                                    QLatin1String("Tile properties files (*.tiles)"));
    if (fileName.isEmpty())
        return;

    foreach (TileDefTileset *ts, mMergedFile.tilesets()) {
        foreach (TileDefTile *tile, ts->mTiles) {
            // we copied these in use1()/use2()
            // normally the UI updates mPropertyUI and then TileDefFile.write() does mPropertyUI.ToProperties()
            tile->mPropertyUI.FromProperties(tile->mProperties);
        }
    }

    if (!mMergedFile.write(fileName)) {
        QMessageBox::warning(this, tr("Error writing .tiles file"),
                             mMergedFile.errorString());
        return;
    };
    mMergedFile.setFileName(fileName);

    TileDefTextFile textFile;
    if (!textFile.write(fileName + QLatin1String(".txt"), mMergedFile.tilesets())) {
        QMessageBox::warning(this, tr("Error writing .tiles.txt file"), textFile.errorString());
        return;
    };
    statusBar()->showMessage(
                tr("Saved merged definitions to %1 and %1.txt")
                .arg(QDir::toNativeSeparators(fileName)), 8000);
}

void TileDefCompare::currentRowChanged(int row)
{
    if (row < 0 || row >= ui->listWidget->count()) {
        ui->tileimage->setPixmap(QPixmap());
        ui->tileimage->setText(tr("File 1 preview"));
        mTileImage2->setPixmap(QPixmap());
        mTileImage2->setText(tr("File 2 preview"));
        mPropertyDetails->setHtml(
                    tr("<i>Select a tile difference.</i>"));
        updateActions();
        return;
    }
    QListWidgetItem *item = ui->listWidget->item(row);
    TileDefTile *tile1 = mTileMap1.value(item);
    TileDefTile *tile2 = mTileMap2.value(item);
    const QImage image1 = getTileImage(tile1);
    const QImage image2 = getTileImage(tile2);
    ui->tileimage->setText(QString());
    ui->tileimage->setPixmap(
                QPixmap::fromImage(image1).scaled(
                    QSize(180, 240), Qt::KeepAspectRatio,
                    Qt::SmoothTransformation));
    ui->tileimage->setToolTip(
                tile1 == nullptr
                ? tr("Tile is absent from File 1")
                : tr("File 1: %1 tile %2 - %3x%4 px")
                  .arg(tile1->mTileset->mName)
                  .arg(tile1->id())
                  .arg(image1.width()).arg(image1.height()));
    mTileImage2->setText(QString());
    mTileImage2->setPixmap(
                QPixmap::fromImage(image2).scaled(
                    QSize(180, 240), Qt::KeepAspectRatio,
                    Qt::SmoothTransformation));
    mTileImage2->setToolTip(
                tile2 == nullptr
                ? tr("Tile is absent from File 2")
                : tr("File 2: %1 tile %2 - %3x%4 px")
                  .arg(tile2->mTileset->mName)
                  .arg(tile2->id())
                  .arg(image2.width()).arg(image2.height()));
    const QString choice = mUseMap.value(item) == 1
            ? tr("File 1")
            : mUseMap.value(item) == 2
              ? tr("File 2")
              : tr("Not mergeable");
    mPropertyDetails->setHtml(
                QStringLiteral(
                    "<h3>%1 - tile %2</h3>"
                    "<p><b>Status:</b> %3<br>"
                    "<b>Merged value source:</b> %4</p>%5")
                .arg((tile1 ? tile1->mTileset->mName
                            : tile2 ? tile2->mTileset->mName
                                    : QString()).toHtmlEscaped())
                .arg(tile1 ? tile1->id()
                           : tile2 ? tile2->id() : -1)
                .arg(statusName(mDifferenceKind.value(item))
                     .toHtmlEscaped(),
                     choice.toHtmlEscaped(),
                     propertiesHtml(tile1, tile2)));
    updateActions();
}

QString TileDefCompare::listString(int use, TileDefTile *tdt1, TileDefTile *tdt2)
{
    const QString tilesetName = tdt1 != nullptr
            ? tdt1->mTileset->mName
            : tdt2 != nullptr ? tdt2->mTileset->mName : QString();
    const int tileId = tdt1 != nullptr
            ? tdt1->id() : tdt2 != nullptr ? tdt2->id() : -1;
    const int kind = tdt1 == nullptr
            ? int(OnlyInFile2)
            : tdt2 == nullptr
              ? int(OnlyInFile1) : int(ModifiedProperties);
    const QString mergeChoice = use == 1
            ? tr("merge: File 1")
            : use == 2
              ? tr("merge: File 2")
              : tr("reported only; structure is not mergeable");
    return tr("[%1] %2 - tile %3\n"
              "    %4 changed property key(s) | %5")
            .arg(statusName(kind), tilesetName)
            .arg(tileId)
            .arg(changedPropertyCount(tdt1, tdt2))
            .arg(mergeChoice);
}

QString TileDefCompare::propertiesHtml(
        TileDefTile *tdt1, TileDefTile *tdt2) const
{
    QSet<QString> keys;
    if (tdt1 != nullptr) {
        for (auto it = tdt1->mProperties.constBegin();
             it != tdt1->mProperties.constEnd(); ++it) {
            keys += it.key();
        }
    }
    if (tdt2 != nullptr) {
        for (auto it = tdt2->mProperties.constBegin();
             it != tdt2->mProperties.constEnd(); ++it) {
            keys += it.key();
        }
    }
    QStringList sortedKeys = keys.values();
    sortedKeys.sort(Qt::CaseInsensitive);
    QString html = QStringLiteral(
                "<table border=\"1\" cellspacing=\"0\" "
                "cellpadding=\"4\"><tr><th>Property</th>"
                "<th>File 1</th><th>File 2</th></tr>");
    for (const QString &key : sortedKeys) {
        const bool has1 = tdt1 != nullptr &&
                tdt1->mProperties.contains(key);
        const bool has2 = tdt2 != nullptr &&
                tdt2->mProperties.contains(key);
        const QString value1 = has1
                ? tdt1->mProperties.value(key) : tr("<absent>");
        const QString value2 = has2
                ? tdt2->mProperties.value(key) : tr("<absent>");
        const bool different = has1 != has2 || value1 != value2;
        html += QStringLiteral(
                    "<tr%1><td><b>%2</b></td><td>%3</td><td>%4</td></tr>")
                .arg(different
                     ? QStringLiteral(
                           " style=\"background:#fff0b3;color:#202020\"")
                     : QString())
                .arg(key.toHtmlEscaped(),
                     value1.toHtmlEscaped(),
                     value2.toHtmlEscaped());
    }
    if (sortedKeys.isEmpty()) {
        html += QStringLiteral(
                    "<tr><td colspan=\"3\"><i>No properties</i></td></tr>");
    }
    return html + QStringLiteral("</table>");
}

QString TileDefCompare::statusName(int kind) const
{
    if (kind == OnlyInFile1)
        return tr("Only in File 1");
    if (kind == OnlyInFile2)
        return tr("Only in File 2");
    return tr("Modified properties");
}

void TileDefCompare::updateActions()
{
    bool canChoose = false;
    for (QListWidgetItem *item : ui->listWidget->selectedItems()) {
        if (mTileMap1.value(item) != nullptr &&
                mTileMap2.value(item) != nullptr) {
            canChoose = true;
            break;
        }
    }
    ui->use1->setEnabled(canChoose);
    ui->use2->setEnabled(canChoose);
    ui->saveMerged->setEnabled(mCompared);
    mCopyReportButton->setEnabled(mCompared);
    mExportReportButton->setEnabled(mCompared);
}

void TileDefCompare::rebuildReportText()
{
    mReportText = mReportPreamble +
            QStringLiteral("Status\tTileset\tTile ID\tMerge choice\t"
                           "File 1 properties\tFile 2 properties\n");
    for (int row = 0; row < ui->listWidget->count(); ++row) {
        QListWidgetItem *item = ui->listWidget->item(row);
        TileDefTile *tile1 = mTileMap1.value(item);
        TileDefTile *tile2 = mTileMap2.value(item);
        const QString tileset = tile1 != nullptr
                ? tile1->mTileset->mName
                : tile2 != nullptr ? tile2->mTileset->mName : QString();
        const int tileId = tile1 != nullptr
                ? tile1->id() : tile2 != nullptr ? tile2->id() : -1;
        QString properties1 = tile1 == nullptr
                ? QString() : propertyMapString(tile1->mProperties);
        QString properties2 = tile2 == nullptr
                ? QString() : propertyMapString(tile2->mProperties);
        properties1.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
        properties1.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        properties2.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
        properties2.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        const int use = mUseMap.value(item);
        mReportText += QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\n")
                .arg(statusName(mDifferenceKind.value(item)),
                     tileset,
                     QString::number(tileId),
                     use == 1 ? QStringLiteral("File 1")
                     : use == 2 ? QStringLiteral("File 2")
                                : QStringLiteral("Not mergeable"),
                     properties1, properties2);
    }
}

void TileDefCompare::filterChanged()
{
    const QString search = mSearchEdit->text().trimmed();
    const int kindFilter = mDifferenceFilter->currentData().toInt();
    int visible = 0;
    for (int row = 0; row < ui->listWidget->count(); ++row) {
        QListWidgetItem *item = ui->listWidget->item(row);
        const bool kindMatches = kindFilter < 0 ||
                item->data(Qt::UserRole + 1).toInt() == kindFilter;
        const bool textMatches = search.isEmpty() ||
                item->data(Qt::UserRole).toString().contains(
                    search, Qt::CaseInsensitive);
        const bool show = kindMatches && textMatches;
        item->setHidden(!show);
        if (show)
            ++visible;
    }
    mVisibleSummary->setText(
                tr("%1 of %2 differences visible")
                .arg(visible).arg(ui->listWidget->count()));
}

void TileDefCompare::copyReport()
{
    if (!mReportText.isEmpty()) {
        QApplication::clipboard()->setText(mReportText);
        statusBar()->showMessage(
                    tr("Comparison report copied to the clipboard."), 5000);
    }
}

void TileDefCompare::exportReport()
{
    const QString fileName = QFileDialog::getSaveFileName(
                this, tr("Export .tiles comparison"),
                QStringLiteral("tiledef-comparison.tsv"),
                tr("Tab-separated report (*.tsv);;Text files (*.txt)"));
    if (fileName.isEmpty())
        return;
    QSaveFile file(fileName);
    const QByteArray data = mReportText.toUtf8();
    if (!file.open(QIODevice::WriteOnly) ||
            file.write(data) != data.size() ||
            !file.commit()) {
        QMessageBox::warning(
                    this, tr("Export failed"),
                    tr("Could not write %1.")
                    .arg(QDir::toNativeSeparators(fileName)));
        return;
    }
    statusBar()->showMessage(
                tr("Exported comparison report to %1")
                .arg(QDir::toNativeSeparators(fileName)), 7000);
}

void TileDefCompare::readSettings()
{
    QSettings settings;
    settings.beginGroup(QLatin1String("TileDefCompare"));
    QByteArray geom = settings.value(QLatin1String("geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    QString file1 = settings.value(QLatin1String("file1")).toString();
    addRecentFile1(file1);
    ui->packEdit1->setCurrentIndex(0);
    QString file2 = settings.value(QLatin1String("file2")).toString();
    addRecentFile2(file2);
    ui->packEdit2->setCurrentIndex(0);
    settings.endGroup();
}

void TileDefCompare::writeSettings()
{
    QSettings settings;
    settings.beginGroup(QLatin1String("TileDefCompare"));
    settings.setValue(QLatin1String("geometry"), saveGeometry());
    settings.setValue(QLatin1String("file1"), ui->packEdit1->currentText());
    settings.setValue(QLatin1String("file2"), ui->packEdit2->currentText());
    settings.endGroup();
}

QImage TileDefCompare::getTileImage(TileDefTile *tdt)
{
    if (tdt == nullptr) {
        QImage absent(128, 256, QImage::Format_ARGB32);
        absent.fill(QColor(45, 45, 45));
        QPainter painter(&absent);
        painter.setPen(QColor(210, 210, 210));
        painter.drawText(absent.rect(), Qt::AlignCenter,
                         tr("Tile absent"));
        return absent;
    }
    if (Tiled::Tileset *ts = TileMetaInfoMgr::instance()->tileset(tdt->mTileset->mName)) {
        if (ts->isMissing()) {
            TileMetaInfoMgr::instance()->loadTilesets(QList<Tiled::Tileset*>() << ts, true);
            TilesetManager::instance()->waitForTilesets();
        }
        if (Tiled::Tile *tile = ts->tileAt(tdt->id()))
            return tile->image();
    }
    if (Tiled::Tile *missing =
            TilesetManager::instance()->missingTile())
        return missing->image();
    QImage unavailable(128, 256, QImage::Format_ARGB32);
    unavailable.fill(Qt::transparent);
    return unavailable;
}

void TileDefCompare::addRecentFile1(const QString &fileName)
{
    // Remember the file by its canonical file path
    const QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();
    if (canonicalFilePath.isEmpty()) {
        setRecentFilesCombo1();
        return;
    }
    QStringList files = recentFiles1();
    files.removeAll(canonicalFilePath);
    files.prepend(canonicalFilePath);
    while (files.size() > MaxRecentFiles) {
        files.removeLast();
    }
    QSettings settings;
    settings.beginGroup(QLatin1String("TileDefCompare"));
    settings.setValue(QLatin1String("RecentFiles1"), files);
    settings.endGroup();
    setRecentFilesCombo1();
}

void TileDefCompare::addRecentFile2(const QString &fileName)
{
    // Remember the file by its canonical file path
    const QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();
    if (canonicalFilePath.isEmpty()) {
        setRecentFilesCombo2();
        return;
    }
    QStringList files = recentFiles2();
    files.removeAll(canonicalFilePath);
    files.prepend(canonicalFilePath);
    while (files.size() > MaxRecentFiles) {
        files.removeLast();
    }
    QSettings settings;
    settings.beginGroup(QLatin1String("TileDefCompare"));
    settings.setValue(QLatin1String("RecentFiles2"), files);
    settings.endGroup();
    setRecentFilesCombo2();
}

QStringList TileDefCompare::recentFiles1() const
{
    QSettings settings;
    settings.beginGroup(QLatin1String("TileDefCompare"));
    QStringList paths = settings.value(QLatin1String("RecentFiles1")).toStringList();
    settings.endGroup();
    return paths;
}

QStringList TileDefCompare::recentFiles2() const
{
    QSettings settings;
    settings.beginGroup(QLatin1String("TileDefCompare"));
    QStringList paths = settings.value(QLatin1String("RecentFiles2")).toStringList();
    settings.endGroup();
    return paths;
}

void TileDefCompare::setRecentFilesCombo1()
{
    ui->packEdit1->clear();
    ui->packEdit1->insertItems(0, recentFiles1());
    ui->packEdit1->setEnabled(true);
    ui->packEdit1->setCurrentIndex(0);
}

void TileDefCompare::setRecentFilesCombo2()
{
    ui->packEdit2->clear();
    ui->packEdit2->insertItems(0, recentFiles2());
    ui->packEdit2->setEnabled(true);
    ui->packEdit2->setCurrentIndex(0);
}

bool TileDefCompare::runSelfTest(
        QString *summary, QString *errorString)
{
    TileDefFile file1;
    TileDefFile file2;

    TileDefTileset *shared1 =
            createTestTileset(QStringLiteral("shared"), 1, 2);
    shared1->tileAt(0)->mProperties[
            QStringLiteral("SnowTile")] =
            QStringLiteral("snow_0");
    shared1->tileAt(1)->mProperties[
            QStringLiteral("BurntTile")] =
            QStringLiteral("burnt_1");
    file1.insertTileset(file1.tilesets().size(), shared1);

    TileDefTileset *shared2 =
            createTestTileset(QStringLiteral("shared"), 7, 1);
    shared2->tileAt(0)->mProperties[
            QStringLiteral("SnowTile")] =
            QStringLiteral("snow_9");
    file2.insertTileset(file2.tilesets().size(), shared2);

    file1.insertTileset(
                file1.tilesets().size(),
                createTestTileset(
                    QStringLiteral("only_file_1"), 2, 1));
    file2.insertTileset(
                file2.tilesets().size(),
                createTestTileset(
                    QStringLiteral("only_file_2"), 3, 1));

    const TileDefAnalysis analysis =
            analyzeTileDefs(file1, file2);
    int modified = 0;
    int only1 = 0;
    int only2 = 0;
    for (const TileDifference &difference :
         analysis.tileDifferences) {
        if (difference.kind == ModifiedProperties)
            ++modified;
        else if (difference.kind == OnlyInFile1)
            ++only1;
        else if (difference.kind == OnlyInFile2)
            ++only2;
    }
    if (analysis.unique1 !=
            QStringList{QStringLiteral("only_file_1")} ||
            analysis.unique2 !=
            QStringList{QStringLiteral("only_file_2")} ||
            analysis.structuralDifferences.size() != 1 ||
            modified != 1 || only1 != 2 || only2 != 1) {
        *errorString = QStringLiteral(
                    "Enhanced tiledef analysis returned "
                    "unique=%1/%2 structural=%3 modified=%4 "
                    "only1=%5 only2=%6")
                .arg(analysis.unique1.size())
                .arg(analysis.unique2.size())
                .arg(analysis.structuralDifferences.size())
                .arg(modified).arg(only1).arg(only2);
        return false;
    }

    *summary = QStringLiteral(
                "unique tilesets, structural metadata, modified "
                "properties, and file-only tile records verified");
    return true;
}

bool TileDefCompare::renderValidation(
        const QString &outputFile, QString *errorString)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *errorString = QStringLiteral(
                    "Could not create the comparator render directory.");
        return false;
    }

    TileDefFile file1;
    TileDefFile file2;
    TileDefTileset *shared1 =
            createTestTileset(QStringLiteral("demo_natural"), 1, 3);
    shared1->tileAt(0)->mProperties[QStringLiteral("SnowTile")] =
            QStringLiteral("demo_snow_0");
    shared1->tileAt(1)->mProperties[QStringLiteral("BurntTile")] =
            QStringLiteral("demo_burnt_1");
    shared1->tileAt(2)->mProperties[QStringLiteral("Material")] =
            QStringLiteral("wood");
    file1.insertTileset(file1.tilesets().size(), shared1);
    file1.insertTileset(
                file1.tilesets().size(),
                createTestTileset(
                    QStringLiteral("file1_only"), 2, 2));

    TileDefTileset *shared2 =
            createTestTileset(QStringLiteral("demo_natural"), 7, 2);
    shared2->tileAt(0)->mProperties[QStringLiteral("SnowTile")] =
            QStringLiteral("demo_snow_9");
    shared2->tileAt(1)->mProperties[QStringLiteral("BurntTile")] =
            QStringLiteral("demo_burnt_1");
    file2.insertTileset(file2.tilesets().size(), shared2);
    file2.insertTileset(
                file2.tilesets().size(),
                createTestTileset(
                    QStringLiteral("file2_only"), 3, 1));

    const auto syncProperties = [](TileDefFile &file) {
        for (TileDefTileset *tileset : file.tilesets()) {
            for (TileDefTile *tile : tileset->mTiles) {
                tile->mPropertyUI.FromProperties(
                            tile->mProperties);
            }
        }
    };
    syncProperties(file1);
    syncProperties(file2);

    const QString path1 =
            temporary.filePath(QStringLiteral("baseline.tiles"));
    const QString path2 =
            temporary.filePath(QStringLiteral("candidate.tiles"));
    if (!file1.write(path1) || !file2.write(path2)) {
        *errorString = QStringLiteral(
                    "Could not create the comparator render fixtures: %1 %2")
                .arg(file1.errorString(), file2.errorString());
        return false;
    }

    TileDefCompare window;
    window.ui->packEdit1->setEditText(path1);
    window.ui->packEdit2->setEditText(path2);
    window.compare();
    window.resize(1180, 760);
    window.show();
    qApp->processEvents();
    const QPixmap image = window.grab();
    if (image.isNull() || !image.save(outputFile, "PNG")) {
        *errorString = QStringLiteral(
                    "Could not save the .tiles comparator render to %1.")
                .arg(QDir::toNativeSeparators(outputFile));
        return false;
    }
    return true;
}

