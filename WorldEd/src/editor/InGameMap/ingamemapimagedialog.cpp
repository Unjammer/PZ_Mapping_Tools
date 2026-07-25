/*
 * Copyright 2023, Tim Baker <treectrl@users.sf.net>
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

#include "ingamemapimagedialog.h"
#include "ui_ingamemapimagedialog.h"

#include "celldocument.h"
#include "chunkmap.h"
#include "documentmanager.h"
#include "simplefile.h"
#include "world.h"
#include "worlddocument.h"

#include "BuildingEditor/buildingtiles.h"

#include <QBuffer>
#include <QDebug>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QSettings>

static QLatin1String KEY_MAP_PATH("InGameMapImageDialog/MapPath");
static QLatin1String KEY_RULES_PATH("InGameMapImageDialog/RulesPath");
static QLatin1String KEY_OUTPUT_PATH("InGameMapImageDialog/OutputPath");

InGameMapImageDialog::InGameMapImageDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::InGameMapImageDialog)
{
    ui->setupUi(this);

    Document *doc = DocumentManager::instance()->currentDocument();
    World *world = doc->asWorldDocument() ? doc->asWorldDocument()->world() : doc->asCellDocument()->world();
    const GenerateLotsSettings &settings = world->getGenerateLotsSettings();
    ui->inputMapPath->setText(settings.exportDir);

    ui->buttonGroup->setId(ui->radioButton300, 1);
    ui->buttonGroup->setId(ui->radioButton256, 2);
    ui->radioButton256->setChecked(true);

    connect(ui->buttonBrowseMap, &QToolButton::clicked, this, &InGameMapImageDialog::chooseMapDirectory);
    connect(ui->buttonBrowseRules, &QToolButton::clicked, this, &InGameMapImageDialog::chooseRulesFile);
    connect(ui->buttonBrowseImage, &QToolButton::clicked, this, &InGameMapImageDialog::chooseOutputFile);
    connect(ui->buttonCreateImage, &QPushButton::clicked, this, &InGameMapImageDialog::clickedTheButton);
    connect(ui->buttonClose, &QPushButton::clicked, this, &InGameMapImageDialog::close);

    ui->statusLabel->setText(QStringLiteral("Click the button below to begin."));

    QSettings qSettings;
    QString mapPath = qSettings.value(KEY_MAP_PATH).toString();
    QString rulesPath = qSettings.value(KEY_RULES_PATH).toString();
    QString outputPath = qSettings.value(KEY_OUTPUT_PATH).toString();
    ui->inputMapPath->setText(mapPath);
    ui->rulesFilePath->setText(rulesPath);
    ui->outputImagePath->setText(outputPath);
}

InGameMapImageDialog::~InGameMapImageDialog()
{
    delete ui;
}

void InGameMapImageDialog::chooseMapDirectory()
{
    QString recent = ui->inputMapPath->text();
    QString f = QFileDialog::getExistingDirectory(this, tr("Choose map directory"), recent);
    if (f.isEmpty()) {
        return;
    }
    ui->inputMapPath->setText(QDir::toNativeSeparators(f));
    QSettings qSettings;
    qSettings.setValue(KEY_MAP_PATH, f);
}

void InGameMapImageDialog::chooseRulesFile()
{
    QString f = QFileDialog::getOpenFileName(this, tr("Choose rules file"),
                                             ui->rulesFilePath->text(), QLatin1String("Text Files (*.txt)"));
    if (f.isEmpty()) {
        return;
    }
    ui->rulesFilePath->setText(QDir::toNativeSeparators(f));
    QSettings qSettings;
    qSettings.setValue(KEY_RULES_PATH, f);
}

void InGameMapImageDialog::chooseOutputFile()
{
    QString f = QFileDialog::getSaveFileName(this, tr("Save PNG As..."),
                                             ui->outputImagePath->text(), QLatin1String("PNG Files (*.png)"));
    if (f.isEmpty()) {
        return;
    }
    ui->outputImagePath->setText(QDir::toNativeSeparators(f));
    QSettings qSettings;
    qSettings.setValue(KEY_OUTPUT_PATH, f);
}

void InGameMapImageDialog::clickedTheButton()
{
    if (mRunning) {
        mStop = true;
        return;
    }

    QString inputPath = ui->inputMapPath->text().trimmed();
    QString rulesPath = ui->rulesFilePath->text().trimmed();
    QString outputPath = ui->outputImagePath->text().trimmed();
    if (inputPath.isEmpty() || !QDir(inputPath).exists()) {
        return;
    }
    if (rulesPath.isEmpty() || !QFile::exists(rulesPath)) {
        return;
    }
    if (outputPath.isEmpty() || !outputPath.toLower().endsWith(QStringLiteral(".png"))) {
        return;
    }

    mRules.clear();
    MapToPNGFile file;
    MapToPNGFileSettings settings2;
    if (file.read(rulesPath, settings2, mRules) == false) {
        QMessageBox::critical(this, tr("In-Game Map Image Error"),
                              tr("Error while reading %1\n%2")
                              .arg(rulesPath)
                              .arg(file.mError));
        return;
    }

    mRunning = true;
    ui->buttonCreateImage->setText(QStringLiteral("STOP"));
    createImage();
    ui->buttonCreateImage->setText(QStringLiteral("Create Image"));
    ui->statusLabel->setText(QStringLiteral("Click the button below to begin."));
    mRunning = false;
    mStop = false;
}

void InGameMapImageDialog::createImage()
{
    ui->statusLabel->setText(QStringLiteral("Loading .lotheader files"));
    qApp->processEvents();

    QString inputPath = ui->inputMapPath->text();
    QString outputPath = ui->outputImagePath->text();

    qDeleteAll(IsoLot::InfoHeaders);
    IsoLot::InfoHeaders.clear();
    IsoLot::CellCoordToLotHeader.clear();

    bool b256 = ui->radioButton256->isChecked();
    IsoMetaGrid metaGrid((IsoConstants(b256)));
    metaGrid.Create(inputPath);
    QSize worldSize(metaGrid.maxx - metaGrid.minx + 1, metaGrid.maxy - metaGrid.miny + 1);

    int CHUNKS_PER_CELL = b256 ? 32 : 30;
    int SQUARES_PER_CHUNK = b256 ? 8 : 10;
    int SQUARES_PER_CELL = CHUNKS_PER_CELL * SQUARES_PER_CHUNK;

    QImage image(worldSize * SQUARES_PER_CELL, QImage::Format_RGBA8888);
    image.fill(Qt::gray);

    for (int cy = metaGrid.miny; cy <= metaGrid.maxy; cy++) {
        for (int cx = metaGrid.minx; cx <= metaGrid.maxx; cx++) {
            ui->statusLabel->setText(QStringLiteral("Creating Image (cell %1,%2)").arg(cx).arg(cy));
            qApp->processEvents();
            if (mStop) {
                qDeleteAll(IsoLot::InfoHeaders);
                IsoLot::InfoHeaders.clear();
                IsoLot::CellCoordToLotHeader.clear();
                mStop = false;
                return;
            }
            cellToImage(image, inputPath, metaGrid, cx, cy);
        }
    }

    ui->statusLabel->setText(QStringLiteral("Writing PNG"));
    qApp->processEvents();
    image.save(outputPath);
}

void InGameMapImageDialog::cellToImage(QImage &image, const QString &mapDirectory, IsoMetaGrid& metaGrid, int cellX, int cellY)
{
    QString filenameheader = QStringLiteral("%1/%2_%3.lotheader").arg(mapDirectory).arg(cellX).arg(cellY);
    if (!IsoLot::InfoHeaders.contains(filenameheader)) {
        return;
    }
    LotHeader* header = IsoLot::InfoHeaders[filenameheader];
    QString filenamepack = QStringLiteral("%1/world_%2_%3.lotpack").arg(mapDirectory).arg(cellX).arg(cellY);
    QFile file(filenamepack);
    if (!file.open(QFile::ReadOnly)) {
        return;
    }

    QBuffer buffer;
    buffer.open(QBuffer::ReadWrite);
    buffer.write(file.readAll());
    file.close();
    buffer.seek(0);

    QDataStream in(&buffer);
    in.setByteOrder(QDataStream::LittleEndian);

    int version = IsoLot::VERSION0;
    char magic[4] = { 0 };
    in.readRawData(magic, 4);
    if (magic[0] == 'L' && magic[1] == 'O' && magic[2] == 'T' && magic[3] == 'P') {
        version = IsoLot::readInt(in);
        if (version < IsoLot::VERSION0 || version > IsoLot::VERSION_LATEST) {
            return;
        }
    } else {
        buffer.seek(0);
    }
    bool b256 = ui->radioButton256->isChecked();
    int CHUNKS_PER_CELL = b256 ? 32 : 30;
    int SQUARES_PER_CHUNK = b256 ? 8 : 10;
    int SQUARES_PER_CELL = CHUNKS_PER_CELL * SQUARES_PER_CHUNK;

    for (int chunkY = 0; chunkY < CHUNKS_PER_CELL; chunkY++) {
        for (int chunkX = 0; chunkX < CHUNKS_PER_CELL; chunkX++) {
            int index = chunkX * CHUNKS_PER_CELL + chunkY;
            // Skip 'LOTP' + version + #chunks
            buffer.seek((version >= IsoLot::VERSION1 ? 8 : 0) + 4 + index * 8);
            qint64 pos;
            in >> pos;
            buffer.seek(pos);
            int skip = 0;
            for (int z = header->minLevel; z <= 0; ++z) { // z=0 only
                for (int x = 0; x < SQUARES_PER_CHUNK; ++x) {
                    for (int y = 0; y < SQUARES_PER_CHUNK; ++y) {
                        if (skip > 0) {
                            --skip;
                            continue;
                        }
                        int count = IsoLot::readInt(in);
                        if (count == -1) {
                            skip = IsoLot::readInt(in);
                            if (skip > 0) {
                                --skip;
                            }
                            continue;
                        }
                        int roomID = IsoLot::readInt(in);
//                        roomIDs[x][y][z] = roomID;
                        Q_ASSERT(count > 1 && count < 30);
                        for (int n = 1; n < count; ++n) {
                            int tileNameIndex = IsoLot::readInt(in);
                            if (z != 0)
                                continue;
//                            this->data[x][y][z] += d;
                            QString tileName = header->tilesUsed[tileNameIndex];
                            BuildingEditor::BuildingTile buildingTile = header->buildingTiles[tileNameIndex];
                            int pixelX = (cellX - metaGrid.minx) * SQUARES_PER_CELL + chunkX * SQUARES_PER_CHUNK + x;
                            int pixelY = (cellY - metaGrid.miny) * SQUARES_PER_CELL + chunkY * SQUARES_PER_CHUNK + y;
                            tileToImage(image, buildingTile, pixelX, pixelY);
                        }
                    }
                }
            }
        }
    }
}

void InGameMapImageDialog::tileToImage(QImage &image, const BuildingEditor::BuildingTile &buildingTile, int pixelX, int pixelY)
{
    if (buildingTile.mIndex == -1) {
        return; // failed to parse the tile name
    }
    int tileIndex = buildingTile.mIndex;
#if 1
    for (int i = mRules.size() - 1; i >= 0; i--) {
        const MapToPNGFileRule &rule = mRules[i];
        if ((rule.mTilesetCompare == QLatin1String("contains")) && (buildingTile.mTilesetName.contains(rule.mTileset) == false)) {
            continue;
        }
        if ((rule.mTilesetCompare == QLatin1String("equals")) && (buildingTile.mTilesetName != rule.mTileset)) {
            continue;
        }
        if ((rule.mTilesetCompare == QLatin1String("startsWith")) && (buildingTile.mTilesetName.startsWith(rule.mTileset) == false)) {
            continue;
        }
        if ((rule.mTileIndexMin >= 0) && ((tileIndex < rule.mTileIndexMin) || (tileIndex > rule.mTileIndexMax))) {
            continue;
        }
        image.setPixel(pixelX, pixelY, qRgba(rule.mColor.red(), rule.mColor.green(), rule.mColor.blue(), rule.mColor.alpha()));
        break;
    }
#else
    if (buildingTile.mTilesetName.contains(QStringLiteral("_trees"))) {
        image.setPixel(pixelX, pixelY, qRgb(38, 53, 22)); // normaltree
    }
    if (buildingTile.mTilesetName.contains(QStringLiteral("jumbo"))) {
        image.setPixel(pixelX, pixelY, qRgb(38, 53, 22)); // jumbotree
    }
    if (buildingTile.mTilesetName.contains(QStringLiteral("_railroad"))) {
        image.setPixel(pixelX, pixelY, qRgb(73, 58, 43)); // rails
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("vegetation"))) {
        image.setPixel(pixelX, pixelY, qRgb(48, 73, 32)); // vegetation
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("blends_natural_01"))) {
        if (tileIndex >= 0 && tileIndex <= 15) {
            image.setPixel(pixelX, pixelY, qRgb(217, 207, 183)); // sand
        }
        if (tileIndex >= 16 && tileIndex <= 31) {
            image.setPixel(pixelX, pixelY, qRgb(75, 88, 27)); // darkgrass
        }
        if (tileIndex >= 32 && tileIndex <= 47) {
            image.setPixel(pixelX, pixelY, qRgb(97, 103, 36)); // medgrass
        }
        if (tileIndex >= 48 && tileIndex <= 63) {
            image.setPixel(pixelX, pixelY, qRgb(127, 120, 45)); // litegrass
        }
        if (tileIndex >= 64 && tileIndex <= 79) {
            image.setPixel(pixelX, pixelY, qRgb(91, 63, 21)); // dirt
        }
        if (tileIndex >= 80 && tileIndex <= 95) {
            image.setPixel(pixelX, pixelY, qRgb(91, 63, 21)); // dirt + grass
        }
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("blends_natural_02"))) {
        image.setPixel(pixelX, pixelY, qRgb(108, 127, 131)); // water
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("blends_street_01"))) {
        image.setPixel(pixelX, pixelY, qRgb(128, 128, 128)); // street
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("floors_exterior_tilesandstone"))) {
        image.setPixel(pixelX, pixelY, qRgb(132, 81, 76)); // tilesand
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("floors_exterior_tilesandwood"))) {
        image.setPixel(pixelX, pixelY, qRgb(132, 81, 76)); // tilesand
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("location_"))) {
        image.setPixel(pixelX, pixelY, qRgb(132, 81, 76)); // tilesand
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("vegetation_farm"))) {
        image.setPixel(pixelX, pixelY, qRgb(218, 165, 32)); // Corn
    }
    if (buildingTile.mTilesetName.startsWith(QStringLiteral("walls_"))) {
        image.setPixel(pixelX, pixelY, qRgb(93, 44, 39)); // walls
    }
#endif
}

/////

const int VERSION_LATEST = 1;

bool MapToPNGFile::read(const QString &filePath, MapToPNGFileSettings &settings, QList<MapToPNGFileRule> &rules)
{
    SimpleFile simpleFile;
    if (!simpleFile.read(filePath)) {
        mError = simpleFile.errorString();
        return false;
    }
    for (const SimpleFileBlock& block : std::as_const(simpleFile.blocks)) {
        SimpleFileKeyValue kv;
        if (block.name == QLatin1String("rule")) {
            MapToPNGFileRule rule;
            rule.mRuleName = block.value("name").trimmed();
            if (rule.mRuleName.isEmpty()) {
                mError = QString::fromLatin1("Line %1: Empty or missing name").arg(block.lineNumber);
                return false;
            }
            QString tilesetStr = block.value("tileset").trimmed();
            if (tilesetStr.isEmpty()) {
                mError = QString::fromLatin1("Line %1: Empty or missing tileset").arg(block.lineNumber);
                return false;
            }
            QStringList tilesetArgs = tilesetStr.split(QLatin1String(" "), Qt::SkipEmptyParts);
            if (tilesetArgs.size() != 2) {
                mError = QString::fromLatin1("Line %1: Expected two tileset arguments").arg(block.lineNumber);
                return false;
            }
            rule.mTilesetCompare = tilesetArgs[0].trimmed();
            if (rule.mTilesetCompare == QLatin1String("contains") ||
                    rule.mTilesetCompare == QLatin1String("equals") ||
                    rule.mTilesetCompare == QLatin1String("startsWith")) {
                // ok
            } else {
                mError = QString::fromLatin1("Line %1: Unknown tileset compare").arg(block.lineNumber);
                return false;
            }
            rule.mTileset = tilesetArgs[1].trimmed();
            QString tileRangeStr = block.value("tileRange").trimmed();
            if (tileRangeStr.isEmpty()) {
                rule.mTileIndexMin = -1;
                rule.mTileIndexMax = -1;
            } else {
                QStringList tileRangeArgs = tileRangeStr.split(QLatin1String(" "), Qt::SkipEmptyParts);
                if (tileRangeArgs.size() != 2) {
                    mError = QString::fromLatin1("Line %1: Expected two tilerange arguments").arg(block.lineNumber);
                    return false;
                }
                bool ok = false;
                rule.mTileIndexMin = tileRangeArgs[0].toInt(&ok);
                if (ok == false) {
                    mError = QString::fromLatin1("Line %1: Invalid tilerange arguments").arg(block.lineNumber);
                    return false;
                }
                rule.mTileIndexMax = tileRangeArgs[1].toInt(&ok);
                if (ok == false) {
                    mError = QString::fromLatin1("Line %1: Invalid tilerange arguments").arg(block.lineNumber);
                    return false;
                }
                if ((rule.mTileIndexMin < 0) || (rule.mTileIndexMax < 0) || (rule.mTileIndexMin > rule.mTileIndexMax)) {
                    mError = QString::fromLatin1("Line %1: Invalid tilerange arguments").arg(block.lineNumber);
                    return false;
                }
            }
            QString colorStr = block.value("color").trimmed();
            rule.mColor = Qt::white;
            if (parseColor(colorStr, block.lineNumber, rule.mColor) == false) {
                return false;
            }
            rules += rule;
        } else if (block.name == QLatin1String("settings")) {
            // Add settings here
        } else {
            mError = QString::fromLatin1("Line %1: Unknown block name '%2'").arg(block.lineNumber).arg(block.name);
            return false;
        }
    }
    return true;
}

bool MapToPNGFile::write(const QString &filePath, const MapToPNGFileSettings &settings, const QList<MapToPNGFileRule> &rules)
{
    SimpleFile simpleFile;

    SimpleFileBlock settingsBlock;
    settingsBlock.name = QLatin1String("settings");
    // Add settings here
    simpleFile.blocks += settingsBlock;

    for (const MapToPNGFileRule &rule : rules) {
        SimpleFileBlock block;
        block.name = QLatin1String("rule");
        block.addValue("name", rule.mRuleName);
        block.addValue("tileset", QString::fromLatin1("%1 %2").arg(rule.mTilesetCompare).arg(rule.mTileset));
        if (rule.mTileIndexMin >= 0 && rule.mTileIndexMax >= 0) {
            block.addValue("tileRange", QString::fromLatin1("%1 %2").arg(rule.mTileIndexMin).arg(rule.mTileIndexMax));
        }
        block.addValue("color", colorString(rule.mColor));
        simpleFile.blocks += block;
    }

    simpleFile.setVersion(VERSION_LATEST);
    if (!simpleFile.write(filePath)) {
        mError = simpleFile.errorString();
        return false;
    }
    return true;
}

bool MapToPNGFile::parseColor(const QString &colorStr, int lineNumber, QColor &result)
{
    QStringList rgb = colorStr.split(QLatin1String(" "), Qt::SkipEmptyParts);
    if (rgb.size() < 3 || rgb.size() > 4) {
        mError = QString::fromLatin1(
                    "Line %1: Color '%2' has %3 component(s); expected "
                    "R G B or R G B A with values from 0 to 255.")
                .arg(lineNumber)
                .arg(colorStr)
                .arg(rgb.size());
        return false;
    }
    const char *componentNames[] = {"red", "green", "blue", "alpha"};
    int components[] = {0, 0, 0, 255};
    for (int index = 0; index < rgb.size(); ++index) {
        bool ok = false;
        const QString token = rgb.at(index).trimmed();
        components[index] = token.toInt(&ok);
        if (!ok || components[index] < 0 || components[index] > 255) {
            mError = QString::fromLatin1(
                        "Line %1: The %2 color component '%3' is invalid; "
                        "expected an integer from 0 to 255 in '%4'.")
                    .arg(lineNumber)
                    .arg(QString::fromLatin1(componentNames[index]))
                    .arg(token)
                    .arg(colorStr);
            return false;
        }
    }
    result = QColor(components[0], components[1],
                    components[2], components[3]);
    return true;
}

QString MapToPNGFile::colorString(const QColor &color)
{
    return QString::fromLatin1("%1 %2 %3 %4")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha());
}
