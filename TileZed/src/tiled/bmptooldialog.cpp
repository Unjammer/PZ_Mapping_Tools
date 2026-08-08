/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "bmptooldialog.h"
#include "ui_bmptooldialog.h"

#include "bmpblender.h"
#include "bmptool.h"
#include "documentmanager.h"
#include "mapcomposite.h"
#include "mapdocument.h"
#include "mainwindow.h"
#include "preferences.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zoomable.h"

#include "BuildingEditor/buildingtiles.h"
#include "../portablesettings.h"

#include "layer.h"
#include "layermodel.h"
#include "map.h"
#include "tile.h"
#include "tileset.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDirIterator>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

using namespace Tiled;
using namespace Tiled::Internal;

namespace Tiled {
namespace Internal {

static bool bmpAliasesEqual(const QList<BmpAlias *> &left,
                            const QList<BmpAlias *> &right)
{
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.size(); ++index) {
        const BmpAlias *leftAlias = left.at(index);
        const BmpAlias *rightAlias = right.at(index);
        if (!leftAlias || !rightAlias) {
            if (leftAlias != rightAlias)
                return false;
            continue;
        }
        if (leftAlias->name != rightAlias->name ||
                leftAlias->tiles != rightAlias->tiles) {
            return false;
        }
    }
    return true;
}

static bool bmpRulesEqual(const QList<BmpRule *> &left,
                          const QList<BmpRule *> &right)
{
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.size(); ++index) {
        const BmpRule *leftRule = left.at(index);
        const BmpRule *rightRule = right.at(index);
        if (!leftRule || !rightRule) {
            if (leftRule != rightRule)
                return false;
            continue;
        }
        if (leftRule->label != rightRule->label ||
                leftRule->bitmapIndex != rightRule->bitmapIndex ||
                leftRule->color != rightRule->color ||
                leftRule->tileChoices != rightRule->tileChoices ||
                leftRule->targetLayer != rightRule->targetLayer ||
                leftRule->condition != rightRule->condition ||
                leftRule->obsolete != rightRule->obsolete) {
            return false;
        }
    }
    return true;
}

static bool bmpBlendsEqual(const QList<BmpBlend *> &left,
                           const QList<BmpBlend *> &right)
{
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.size(); ++index) {
        const BmpBlend *leftBlend = left.at(index);
        const BmpBlend *rightBlend = right.at(index);
        if (!leftBlend || !rightBlend) {
            if (leftBlend != rightBlend)
                return false;
            continue;
        }
        if (leftBlend->targetLayer != rightBlend->targetLayer ||
                leftBlend->mainTile != rightBlend->mainTile ||
                leftBlend->blendTile != rightBlend->blendTile ||
                leftBlend->dir != rightBlend->dir ||
                leftBlend->ExclusionList != rightBlend->ExclusionList ||
                leftBlend->exclude2 != rightBlend->exclude2) {
            return false;
        }
    }
    return true;
}

class ChangeBmpRules : public QUndoCommand
{
public:
    ChangeBmpRules(MapDocument *mapDocument, const QString &fileName,
                   const QList<BmpAlias*> &aliases,
                   const QList<BmpRule*> &rules)
        : QUndoCommand(QCoreApplication::translate("Undo Commands",
                                                   "Change BMP Settings (Rules.txt)"))
        , mMapDocument(mapDocument)
        , mFileName(fileName)
        , mAliases(aliases)
        , mRules(rules)
    {
    }

    ~ChangeBmpRules()
    {
        qDeleteAll(mAliases);
        qDeleteAll(mRules);
    }

    void undo() { swap(); }
    void redo() { swap(); }

    void swap()
    {
        QString oldFile = mMapDocument->map()->bmpSettings()->rulesFile();
        QList<BmpAlias*> oldAliases = mMapDocument->map()->bmpSettings()->aliasesCopy();
        QList<BmpRule*> oldRules = mMapDocument->map()->bmpSettings()->rulesCopy();
        mMapDocument->setBmpRulesAndAliases(mFileName, mAliases, mRules);
        mAliases = oldAliases;
        mFileName = oldFile;
        mRules = oldRules;
    }

    MapDocument *mMapDocument;
    QString mFileName;
    QList<BmpAlias*> mAliases;
    QList<BmpRule*> mRules;
};

class ChangeBmpBlends : public QUndoCommand
{
public:
    ChangeBmpBlends(MapDocument *mapDocument, const QString &fileName,
                    const QList<BmpBlend*> &blends)
        : QUndoCommand(QCoreApplication::translate("Undo Commands",
                                                   "Change BMP Settings (Blends.txt)"))
        , mMapDocument(mapDocument)
        , mFileName(fileName)
        , mBlends(blends)
    {
    }

    ~ChangeBmpBlends()
    {
        qDeleteAll(mBlends);
    }

    void undo() { swap(); }
    void redo() { swap(); }

    void swap()
    {
        QString oldFile = mMapDocument->map()->bmpSettings()->blendsFile();
        QList<BmpBlend*> oldBlends = mMapDocument->map()->bmpSettings()->blendsCopy();
        mMapDocument->setBmpBlends(mFileName, mBlends);
        mFileName = oldFile;
        mBlends = oldBlends;
    }

    MapDocument *mMapDocument;
    QString mFileName;
    QList<BmpBlend*> mBlends;
};

bool BmpToolDialog::validateReloadEquality(QString *errorString)
{
    const QString configDirectory =
            QDir(PortableSettings::installRootPath())
            .filePath(QLatin1String("config"));
    const QString rulesPath =
            QDir(configDirectory).filePath(QLatin1String("Rules.txt"));
    const QString blendsPath =
            QDir(configDirectory).filePath(QLatin1String("Blends.txt"));

    BmpRulesFile rulesFile;
    if (!rulesFile.read(rulesPath)) {
        *errorString = tr("Could not read the deployed Rules.txt: %1")
                .arg(rulesFile.errorString());
        return false;
    }
    QList<BmpAlias *> aliasCopies = rulesFile.aliasesCopy();
    QList<BmpRule *> ruleCopies = rulesFile.rulesCopy();
    if (!bmpAliasesEqual(rulesFile.aliases(), aliasCopies) ||
            !bmpRulesEqual(rulesFile.rules(), ruleCopies)) {
        qDeleteAll(aliasCopies);
        qDeleteAll(ruleCopies);
        *errorString = tr("An unchanged Rules.txt was not recognized");
        return false;
    }
    if (!aliasCopies.isEmpty())
        aliasCopies.first()->name += QLatin1String("_changed");
    if (aliasCopies.isEmpty() ||
            bmpAliasesEqual(rulesFile.aliases(), aliasCopies)) {
        qDeleteAll(aliasCopies);
        qDeleteAll(ruleCopies);
        *errorString = tr("A changed Rules.txt alias was not detected");
        return false;
    }
    qDeleteAll(aliasCopies);
    qDeleteAll(ruleCopies);

    BmpBlendsFile blendsFile;
    if (!blendsFile.read(blendsPath, rulesFile.aliases())) {
        *errorString = tr("Could not read the deployed Blends.txt: %1")
                .arg(blendsFile.errorString());
        return false;
    }
    QList<BmpBlend *> blendCopies = blendsFile.blendsCopy();
    if (!bmpBlendsEqual(blendsFile.blends(), blendCopies)) {
        qDeleteAll(blendCopies);
        *errorString = tr("An unchanged Blends.txt was not recognized");
        return false;
    }
    if (!blendCopies.isEmpty())
        blendCopies.first()->targetLayer += QLatin1String("_changed");
    if (blendCopies.isEmpty() ||
            bmpBlendsEqual(blendsFile.blends(), blendCopies)) {
        qDeleteAll(blendCopies);
        *errorString = tr("A changed Blends.txt entry was not detected");
        return false;
    }
    qDeleteAll(blendCopies);

    qInfo() << "Validated no-op Reload detection with"
            << rulesFile.aliases().size() << "aliases,"
            << rulesFile.rules().size() << "rules and"
            << blendsFile.blends().size() << "blends";
    return true;
}

class ReplaceUnknownBmpPixels : public QUndoCommand
{
public:
    ReplaceUnknownBmpPixels(MapDocument *mapDocument,
                            const QImage &mainImage,
                            const QImage &vegetationImage)
        : QUndoCommand(QCoreApplication::translate(
                           "Undo Commands",
                           "Replace Unknown BMP Colors"))
        , mMapDocument(mapDocument)
        , mBeforeMain(mapDocument->map()->bmpMain().image())
        , mBeforeVegetation(mapDocument->map()->bmpVeg().image())
        , mAfterMain(mainImage)
        , mAfterVegetation(vegetationImage)
    {
    }

    void undo() override
    {
        apply(mBeforeMain, mBeforeVegetation);
    }

    void redo() override
    {
        apply(mAfterMain, mAfterVegetation);
    }

private:
    void apply(const QImage &mainImage, const QImage &vegetationImage)
    {
        mMapDocument->swapBmpImage(0, mainImage);
        mMapDocument->swapBmpImage(1, vegetationImage);
        const QRect mainBounds(QPoint(), mainImage.size());
        const QRect vegetationBounds(QPoint(), vegetationImage.size());
        mMapDocument->mapComposite()->bmpBlender()->markDirty(mainBounds);
        mMapDocument->mapComposite()->bmpBlender()->markDirty(
                    vegetationBounds);
        mMapDocument->emitBmpPainted(0, mainBounds);
        mMapDocument->emitBmpPainted(1, vegetationBounds);
    }

    MapDocument *mMapDocument;
    QImage mBeforeMain;
    QImage mBeforeVegetation;
    QImage mAfterMain;
    QImage mAfterVegetation;
};

} // namespace Internal
} // namespace Tiled

BmpToolDialog *BmpToolDialog::mInstance = 0;

BmpToolDialog *BmpToolDialog::instance()
{
    if (!mInstance)
        mInstance = new BmpToolDialog(MainWindow::instance());
    return mInstance;
}

BmpToolDialog::BmpToolDialog(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::BmpToolDialog),
    mDocument(0),
    mExpanded(true)
{
    ui->setupUi(this);
    setupCustomBrushUi();

    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    ui->tabWidget->setCurrentIndex(0);

    ui->tableView->setShowObsolete(ui->showObsolete->isChecked());

    connect(ui->tableView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &BmpToolDialog::currentRuleChanged);

    connect(ui->expandCollapse, &QAbstractButton::clicked,
            this, &BmpToolDialog::expandCollapse);
    connect(ui->showObsolete, &QAbstractButton::clicked,
            this, &BmpToolDialog::showObsoleteChanged);
    ui->tableView->zoomable()->connectToComboBox(ui->scaleCombo);

    connect(ui->blendView, &BmpBlendView::blendHighlighted,
            this, &BmpToolDialog::blendHighlighted);
    ui->blendView->zoomable()->connectToComboBox(ui->blendScaleCombo);

    ui->tilesInBlend->model()->setShowHeaders(false);
    ui->tilesInBlend->setZoomable(ui->blendView->zoomable());
    connect(ui->tilesInBlend->zoomable(), &Zoomable::scaleChanged,
            this, &BmpToolDialog::synchBlendTilesView);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    connect(ui->brushSize, qOverload<int>(&QSpinBox::valueChanged),
            this, &BmpToolDialog::brushSizeChanged);
#else
    connect(ui->brushSize, &QSpinBox::valueChanged,
            this, &BmpToolDialog::brushSizeChanged);
#endif
    connect(ui->brushSquare, &QAbstractButton::clicked,
            this, &BmpToolDialog::brushSquare);
    connect(ui->brushCircle, &QAbstractButton::clicked,
            this, &BmpToolDialog::brushCircle);
    connect(ui->restrictToSelection, &QAbstractButton::toggled,
            this, &BmpToolDialog::restrictToSelection);
    connect(ui->fillAllInSelectedArea, &QCheckBox::toggled,
            this, &BmpToolDialog::fillAllInSelection);
    connect(ui->toggleOverlayLayers, &QAbstractButton::clicked,
            this, &BmpToolDialog::toggleOverlayLayers);
    connect(ui->showBMPTiles, &QAbstractButton::toggled,
            this, &BmpToolDialog::showBMPTiles);
    connect(ui->showMapTiles, &QAbstractButton::toggled,
            this, &BmpToolDialog::showMapTiles);
    connect(ui->blendEdgesEverywhere, &QCheckBox::toggled, this, &BmpToolDialog::blendEdgesEverywhere);

    connect(ui->reloadRules, &QAbstractButton::clicked, this, &BmpToolDialog::reloadRules);
    connect(ui->importRules, &QAbstractButton::clicked, this, &BmpToolDialog::importRules);
    connect(ui->exportRules, &QAbstractButton::clicked, this, &BmpToolDialog::exportRules);
    connect(ui->trashRules, &QAbstractButton::clicked, this, &BmpToolDialog::trashRules);

    connect(ui->reloadBlends, &QAbstractButton::clicked, this, &BmpToolDialog::reloadBlends);
    connect(ui->importBlends, &QAbstractButton::clicked, this, &BmpToolDialog::importBlends);
    connect(ui->exportBlends, &QAbstractButton::clicked, this, &BmpToolDialog::exportBlends);
    connect(ui->trashBlends, &QAbstractButton::clicked, this, &BmpToolDialog::trashBlends);

    connect(ui->help, &QAbstractButton::clicked, this, &BmpToolDialog::help);
    connect(ui->repairUnknownColors, &QCheckBox::toggled,
            this, &BmpToolDialog::repairUnknownColorsToggled);
    ui->groundFallback->setEnabled(false);
    ui->vegetationFallback->setEnabled(false);

    QSettings settings;
    settings.beginGroup(QLatin1String("BmpToolDialog"));
    qreal scale = settings.value(QLatin1String("scale"), 0.5).toReal();
    ui->tableView->zoomable()->setScale(scale);

    scale = settings.value(QLatin1String("Blends/Scale"), 0.5).toReal();
    ui->blendView->zoomable()->setScale(scale);

    int brushSize = settings.value(QLatin1String("brushSize"), 1).toInt();
    BmpBrushTool::instance()->setBrushSize(brushSize);
    ui->brushSize->setValue(brushSize);

    QString shape = settings.value(QLatin1String("brushShape"),
                                   QLatin1String("square")).toString();
    if (shape == QLatin1String("square")) brushSquare();
    else if (shape == QLatin1String("circle")) brushCircle();
    else if (shape == QLatin1String("custom"))
        selectCustomBrush(settings.value(
                    QLatin1String("customBrushPath")).toString());

    bool isRestricted = settings.value(QLatin1String("restrictToSelection"), true).toBool();
    BmpBrushTool::instance()->setRestrictToSelection(isRestricted);

    bool fillAll = settings.value(QLatin1String("fillAllInSelection"), false).toBool();
    BmpBrushTool::instance()->setFillAllInSelection(fillAll);

    bool expanded = settings.value(QLatin1String("expanded"), true).toBool();
    if (!expanded) expandCollapse();
    settings.endGroup();

    mVisibleLaterTimer.setSingleShot(true);
    mVisibleLaterTimer.setInterval(200);
    connect(&mVisibleLaterTimer, &QTimer::timeout, this, &BmpToolDialog::setVisibleNow);

    connect(BmpBrushTool::instance(), &BmpBrushTool::brushChanged,
            this, &BmpToolDialog::brushChanged);

    connect(DocumentManager::instance(), &DocumentManager::documentAboutToClose,
            this, &BmpToolDialog::documentAboutToClose);
}

BmpToolDialog::~BmpToolDialog()
{
    delete ui;
}

QString BmpToolDialog::userBrushDirectory() const
{
    const QString portable = QDir(PortableSettings::rootPath()).filePath(
                QLatin1String("brushes"));
    if (QDir().mkpath(portable))
        return QDir::cleanPath(portable);

    const QString fallback = QDir(
                QStandardPaths::writableLocation(
                    QStandardPaths::AppLocalDataLocation)).filePath(
                QLatin1String("brushes"));
    QDir().mkpath(fallback);
    return QDir::cleanPath(fallback);
}

QStringList BmpToolDialog::brushDirectories() const
{
    QStringList directories;
    directories << QDir::cleanPath(PortableSettings::installPath(
                                       QLatin1String("brushes")))
                << userBrushDirectory();
    directories.removeDuplicates();
    return directories;
}

void BmpToolDialog::ensureExampleBrushes()
{
    const QDir directory(userBrushDirectory());
    if (!directory.exists())
        return;
    QSettings settings;
    if (settings.value(
                QLatin1String("BmpToolDialog/CustomBrushExamplesCreated"),
                false).toBool()) {
        return;
    }

    bool examplesWritten = true;
    const auto writeExample = [&directory, &examplesWritten](
            const QString &fileName, const QImage &image) {
        const QString path = directory.filePath(fileName);
        if (!QFileInfo::exists(path) && !image.save(path, "PNG")) {
            examplesWritten = false;
            qWarning().noquote() << "Could not create example BMP brush"
                                 << QDir::toNativeSeparators(path);
        }
    };

    QImage scatter(32, 32, QImage::Format_ARGB32);
    scatter.fill(Qt::transparent);
    for (int y = 1; y < scatter.height(); ++y) {
        for (int x = 1; x < scatter.width(); ++x) {
            // Stable pseudo-random distribution: examples remain identical
            // across machines and never change an existing user file.
            if (((x * 37 + y * 53 + x * y * 3) % 97) < 12)
                scatter.setPixel(x, y, qRgba(0, 0, 0, 255));
        }
    }
    writeExample(QLatin1String("scatter-32.png"), scatter);

    QImage ring(32, 32, QImage::Format_ARGB32);
    ring.fill(Qt::transparent);
    const QPointF center(15.5, 15.5);
    for (int y = 0; y < ring.height(); ++y) {
        for (int x = 0; x < ring.width(); ++x) {
            const qreal dx = x - center.x();
            const qreal dy = y - center.y();
            const qreal distance2 = dx * dx + dy * dy;
            if (distance2 >= 70.0 && distance2 <= 155.0)
                ring.setPixel(x, y, qRgba(0, 0, 0, 255));
        }
    }
    writeExample(QLatin1String("ring-32.png"), ring);
    if (examplesWritten)
        settings.setValue(
                    QLatin1String(
                        "BmpToolDialog/CustomBrushExamplesCreated"),
                    true);
}

void BmpToolDialog::setupCustomBrushUi()
{
    QGroupBox *group = new QGroupBox(tr("Custom PNG brush masks"), this);
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(5);

    mCustomBrushes = new QComboBox(group);
    mCustomBrushes->setIconSize(QSize(36, 36));
    mCustomBrushes->setSizeAdjustPolicy(
                QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mCustomBrushes->setMinimumContentsLength(22);
    layout->addWidget(mCustomBrushes);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(5);
    mAddCustomBrush = new QPushButton(tr("Add PNG..."), group);
    mOpenBrushFolder = new QPushButton(tr("Open Folder"), group);
    mReloadCustomBrushes = new QPushButton(tr("Reload"), group);
    buttons->addWidget(mAddCustomBrush);
    buttons->addWidget(mOpenBrushFolder);
    buttons->addWidget(mReloadCustomBrushes);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    mCustomBrushInfo = new QLabel(group);
    mCustomBrushInfo->setWordWrap(true);
    mCustomBrushInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mCustomBrushInfo);
    ui->verticalLayout_4->insertWidget(1, group);

    mBrushWatcher = new QFileSystemWatcher(this);
    mBrushReloadTimer.setSingleShot(true);
    mBrushReloadTimer.setInterval(250);
    connect(&mBrushReloadTimer, &QTimer::timeout,
            this, &BmpToolDialog::reloadCustomBrushes);
    connect(mBrushWatcher, &QFileSystemWatcher::directoryChanged,
            &mBrushReloadTimer, qOverload<>(&QTimer::start));
    connect(mBrushWatcher, &QFileSystemWatcher::fileChanged,
            &mBrushReloadTimer, qOverload<>(&QTimer::start));
    connect(mCustomBrushes,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BmpToolDialog::customBrushActivated);
    connect(mAddCustomBrush, &QPushButton::clicked,
            this, &BmpToolDialog::addCustomBrush);
    connect(mOpenBrushFolder, &QPushButton::clicked,
            this, &BmpToolDialog::openBrushFolder);
    connect(mReloadCustomBrushes, &QPushButton::clicked,
            this, &BmpToolDialog::reloadCustomBrushes);

    ensureExampleBrushes();
    reloadCustomBrushes();
}

void BmpToolDialog::reloadCustomBrushes()
{
    if (!mCustomBrushes)
        return;

    const QString selected = BmpBrushTool::instance()->brushShape()
            == BmpBrushTool::BrushShape::Custom
            ? BmpBrushTool::instance()->customBrushPath()
            : mCustomBrushes->currentData().toString();

    const QSignalBlocker blocker(mCustomBrushes);
    mCustomBrushes->clear();
    mCustomBrushes->addItem(tr("Procedural (Square / Circle)"));

    QStringList files;
    QStringList watchPaths;
    for (const QString &rootPath : brushDirectories()) {
        const QDir root(rootPath);
        if (!root.exists())
            continue;
        watchPaths << root.absolutePath();
        QDirIterator it(root.absolutePath(),
                        QStringList() << QLatin1String("*.png"),
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fileName = QDir::cleanPath(it.next());
            files << fileName;
            watchPaths << fileName;
            watchPaths << QFileInfo(fileName).absolutePath();
        }
    }
    files.removeDuplicates();
    watchPaths.removeDuplicates();
    std::sort(files.begin(), files.end(),
              [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });

    QMap<QString, int> baseNameCounts;
    for (const QString &fileName : files)
        ++baseNameCounts[QFileInfo(fileName).completeBaseName().toLower()];

    int selectedIndex = 0;
    for (const QString &fileName : files) {
        const QImage image(fileName);
        if (image.isNull() || image.width() > 128 || image.height() > 128)
            continue;
        const QRegion mask = BmpBrushTool::regionFromBrushImage(image);
        if (mask.isEmpty())
            continue;

        QImage preview(image.size(), QImage::Format_ARGB32);
        preview.fill(QColor(55, 58, 62));
        QPainter painter(&preview);
        for (const QRect &rect : mask)
            painter.fillRect(rect, QColor(235, 238, 242));
        painter.end();

        const QFileInfo info(fileName);
        QString label = info.completeBaseName();
        if (baseNameCounts.value(label.toLower()) > 1)
            label += QStringLiteral(" — %1").arg(info.dir().dirName());
        mCustomBrushes->addItem(
                    QIcon(QPixmap::fromImage(preview).scaled(
                              48, 48, Qt::KeepAspectRatio,
                              Qt::FastTransformation)),
                    label, fileName);
        const int index = mCustomBrushes->count() - 1;
        mCustomBrushes->setItemData(
                    index,
                    tr("%1 × %2 pixels\n%3")
                    .arg(image.width()).arg(image.height())
                    .arg(QDir::toNativeSeparators(fileName)),
                    Qt::ToolTipRole);
        if (!selected.isEmpty()
                && (QDir::cleanPath(selected).compare(
                        fileName, Qt::CaseInsensitive) == 0
                    || QFileInfo(selected).fileName().compare(
                        info.fileName(), Qt::CaseInsensitive) == 0)) {
            selectedIndex = index;
        }
    }

    const QStringList oldDirectories = mBrushWatcher->directories();
    const QStringList oldFiles = mBrushWatcher->files();
    if (!oldDirectories.isEmpty())
        mBrushWatcher->removePaths(oldDirectories);
    if (!oldFiles.isEmpty())
        mBrushWatcher->removePaths(oldFiles);
    if (!watchPaths.isEmpty())
        mBrushWatcher->addPaths(watchPaths);

    mCustomBrushes->setCurrentIndex(selectedIndex);
    if (selectedIndex > 0)
        customBrushActivated(selectedIndex);
    else {
        setProceduralBrushUi();
        if (BmpBrushTool::instance()->brushShape()
                == BmpBrushTool::BrushShape::Custom) {
            ui->brushSquare->setChecked(true);
            BmpBrushTool::instance()->setBrushShape(
                        BmpBrushTool::BrushShape::Square);
        }
    }
}

void BmpToolDialog::selectCustomBrush(const QString &fileName)
{
    if (fileName.isEmpty() || !mCustomBrushes)
        return;
    for (int index = 1; index < mCustomBrushes->count(); ++index) {
        const QString candidate = mCustomBrushes->itemData(index).toString();
        if (QDir::cleanPath(candidate).compare(
                    QDir::cleanPath(fileName), Qt::CaseInsensitive) == 0
                || QFileInfo(candidate).fileName().compare(
                    QFileInfo(fileName).fileName(),
                    Qt::CaseInsensitive) == 0) {
            mCustomBrushes->setCurrentIndex(index);
            return;
        }
    }
}

void BmpToolDialog::setProceduralBrushUi()
{
    if (!mCustomBrushes)
        return;
    const QSignalBlocker blocker(mCustomBrushes);
    mCustomBrushes->setCurrentIndex(0);
    ui->brushSize->setEnabled(true);
    ui->brushSquare->setEnabled(true);
    ui->brushCircle->setEnabled(true);
    mCustomBrushInfo->setText(
                tr("PNG masks use dark, opaque pixels. Transparent or light "
                   "pixels are ignored. Recommended size: 32 × 32."));
}

void BmpToolDialog::customBrushActivated(int index)
{
    if (!mCustomBrushes)
        return;
    if (index <= 0) {
        ui->brushSquare->setChecked(true);
        brushSquare();
        return;
    }

    const QString fileName = mCustomBrushes->itemData(index).toString();
    const QImage image(fileName);
    const QRegion mask = BmpBrushTool::regionFromBrushImage(image);
    if (image.isNull() || image.width() > 128 || image.height() > 128
            || mask.isEmpty()) {
        QMessageBox::warning(
                    this, tr("Invalid Brush Mask"),
                    tr("The selected PNG must contain dark opaque pixels and "
                       "must not exceed 128 × 128 pixels."));
        setProceduralBrushUi();
        return;
    }

    ui->brushSquare->setAutoExclusive(false);
    ui->brushCircle->setAutoExclusive(false);
    ui->brushSquare->setChecked(false);
    ui->brushCircle->setChecked(false);
    ui->brushSquare->setAutoExclusive(true);
    ui->brushCircle->setAutoExclusive(true);
    ui->brushSize->setEnabled(false);
    int pixelCount = 0;
    for (const QRect &rect : mask)
        pixelCount += rect.width() * rect.height();
    mCustomBrushInfo->setText(
                tr("%1 × %2 — %3 painted cells — centered on the cursor")
                .arg(image.width()).arg(image.height()).arg(pixelCount));
    BmpBrushTool::instance()->setCustomBrush(
                mask, image.size(), fileName);
}

void BmpToolDialog::addCustomBrush()
{
    const QStringList files = QFileDialog::getOpenFileNames(
                this, tr("Add Custom Brush Masks"), QString(),
                tr("PNG images (*.png)"));
    if (files.isEmpty())
        return;

    const QDir destination(userBrushDirectory());
    QString lastImported;
    for (const QString &sourceName : files) {
        const QImage image(sourceName);
        const QRegion mask = BmpBrushTool::regionFromBrushImage(image);
        if (image.isNull() || image.width() > 128 || image.height() > 128
                || mask.isEmpty()) {
            QMessageBox::warning(
                        this, tr("Invalid Brush Mask"),
                        tr("'%1' was not imported. A brush PNG must contain "
                           "dark opaque pixels and must not exceed "
                           "128 × 128 pixels.")
                        .arg(QFileInfo(sourceName).fileName()));
            continue;
        }

        const QString destinationName =
                destination.filePath(QFileInfo(sourceName).fileName());
        if (QFileInfo::exists(destinationName)
                && QDir::cleanPath(sourceName).compare(
                    QDir::cleanPath(destinationName),
                    Qt::CaseInsensitive) != 0) {
            if (QMessageBox::question(
                        this, tr("Replace Brush"),
                        tr("'%1' already exists in the brush folder. "
                           "Replace it?")
                        .arg(QFileInfo(destinationName).fileName()))
                    != QMessageBox::Yes) {
                continue;
            }
        }

        if (QDir::cleanPath(sourceName).compare(
                    QDir::cleanPath(destinationName),
                    Qt::CaseInsensitive) != 0) {
            QFile source(sourceName);
            QSaveFile output(destinationName);
            if (!source.open(QIODevice::ReadOnly)
                    || !output.open(QIODevice::WriteOnly)
                    || output.write(source.readAll()) < 0
                    || !output.commit()) {
                QMessageBox::warning(
                            this, tr("Brush Import Failed"),
                            tr("Could not copy '%1' to:\n%2")
                            .arg(QFileInfo(sourceName).fileName(),
                                 QDir::toNativeSeparators(
                                     destinationName)));
                continue;
            }
        }
        lastImported = destinationName;
    }
    reloadCustomBrushes();
    selectCustomBrush(lastImported);
}

void BmpToolDialog::openBrushFolder()
{
    QDesktopServices::openUrl(
                QUrl::fromLocalFile(userBrushDirectory()));
}

void BmpToolDialog::setVisible(bool visible)
{
    QSettings settings;
    settings.beginGroup(QLatin1String("BmpToolDialog"));

    synchBlendTilesView();

    QDockWidget::setVisible(visible);
    if (visible)
        raise();

    if (!visible) {
        settings.setValue(QLatin1String("scale"), ui->tableView->zoomable()->scale());
        settings.setValue(QLatin1String("Blends/Scale"), ui->blendView->zoomable()->scale());
        settings.setValue(QLatin1String("brushSize"), ui->brushSize->value());
        if (ui->brushSquare->isChecked())
            settings.setValue(QLatin1String("brushShape"), QLatin1String("square"));
        if (ui->brushCircle->isChecked())
            settings.setValue(QLatin1String("brushShape"), QLatin1String("circle"));
        if (BmpBrushTool::instance()->brushShape()
                == BmpBrushTool::BrushShape::Custom) {
            settings.setValue(QLatin1String("brushShape"),
                              QLatin1String("custom"));
            settings.setValue(QLatin1String("customBrushPath"),
                              BmpBrushTool::instance()->customBrushPath());
        }
        settings.setValue(QLatin1String("restrictToSelection"),
                          BmpBrushTool::instance()->restrictToSelection());
        settings.setValue(QLatin1String("fillAllInSelection"),
                          BmpBrushTool::instance()->fillAllInSelection());
        settings.setValue(QLatin1String("expanded"), mExpanded);
    }
    settings.endGroup();
}

void BmpToolDialog::setVisibleLater(bool visible)
{
    mVisibleLater = visible;
    mVisibleLaterTimer.start();
}

void BmpToolDialog::setVisibleNow()
{
    if (mVisibleLater != isVisible())
        setVisible(mVisibleLater);
}

void BmpToolDialog::blendHighlighted(BmpBlend *blend, int dir)
{
    QList<Tile*> tiles;
    QString header;
    if (blend && dir != -1) {
        QStringList tileNames;
        QStringList aliasTiles = ui->blendView->model()->aliasTiles(dir ? blend->blendTile : blend->mainTile);
        if (aliasTiles.isEmpty())
            tileNames << (dir ? blend->blendTile : blend->mainTile);
        else
            tileNames = aliasTiles;
        foreach (QString tileName, tileNames) {
            if (Tile *tile = BuildingEditor::BuildingTilesMgr::instance()->tileFor(tileName))
                tiles += tile;
            else
                tiles += TilesetManager::instance()->missingTile();
        }
        if (dir == 0)
            header = tr("<b>mainTile</b>=%1").arg(blend->mainTile);
        else
            header = tr("<b>blendTile</b>=%1, <b>dir</b>=%2, <b>layer</b>=%3")
                    .arg(blend->blendTile)
                    .arg(blend->dirAsString())
                    .arg(blend->targetLayer);
    }
    ui->blendLabel->setText(header);
    ui->tilesInBlend->model()->setColumnCount(qMax(8, tiles.size()));
    ui->tilesInBlend->setTiles(tiles);
}

void BmpToolDialog::synchBlendTilesView()
{
    int height = 2 + ui->tilesInBlend->fontMetrics().lineSpacing() + 2 + 128 * ui->tilesInBlend->zoomable()->scale() + 2;
    height += ui->tilesInBlend->frameWidth() * 2;
    ui->tilesInBlend->setFixedHeight(height);
}

void BmpToolDialog::expandCollapse()
{
    if (mExpanded) {
        ui->expandCollapse->setText(tr("Expand"));
    } else {
        ui->expandCollapse->setText(tr("Collapse"));
    }
    mExpanded = !mExpanded;

    ui->tableView->setExpanded(mExpanded);
}

void BmpToolDialog::showObsoleteChanged()
{
    ui->tableView->setShowObsolete(ui->showObsolete->isChecked());
}

void BmpToolDialog::reloadRules()
{
    QString f = mDocument->map()->bmpSettings()->rulesFile();
    if (!f.isEmpty()/* && QFileInfo(f).exists()*/) {
        BmpRulesFile file;
        if (!file.read(f)) {
            QMessageBox::warning(this, tr("Reload Rules Failed"), file.errorString());
            return;
        }
        const BmpSettings *bmpSettings = mDocument->map()->bmpSettings();
        if (bmpAliasesEqual(file.aliases(), bmpSettings->aliases()) &&
                bmpRulesEqual(file.rules(), bmpSettings->rules())) {
            qInfo() << "BMP Rules.txt reload skipped because the imported "
                       "rules are unchanged:"
                    << QDir::toNativeSeparators(f);
            return;
        }
        mDocument->undoStack()->push(
                    new ChangeBmpRules(mDocument, f, file.aliasesCopy(),
                                       file.rulesCopy()));
    }
}

void BmpToolDialog::importRules()
{
    QSettings settings;
    settings.beginGroup(QLatin1String("BmpToolDialog"));
    QString initialDir = settings.value(QLatin1String("RulesFile")).toString();
    if (initialDir.isEmpty() || !QFileInfo(initialDir).exists())
        initialDir = Preferences::instance()->appConfigPath(QLatin1String("Rules.txt"));
    settings.endGroup();

    QString f = QFileDialog::getOpenFileName(this, tr("Import Rules"),
                                             initialDir,
                                             tr("Rules.txt files (*.txt)"));
    if (!f.isEmpty()) {
        BmpRulesFile file;
        if (!file.read(f)) {
            QMessageBox::warning(this, tr("Import Rules Failed"), file.errorString());
            return;
        }
        const BmpSettings *bmpSettings = mDocument->map()->bmpSettings();
        if (bmpAliasesEqual(file.aliases(), bmpSettings->aliases()) &&
                bmpRulesEqual(file.rules(), bmpSettings->rules())) {
            QMessageBox::information(
                        this,
                        tr("Rules Already Current"),
                        tr("The selected Rules.txt is already identical to the "
                           "snapshot embedded in %1.\n\nNothing was changed and "
                           "Reload is not required.")
                        .arg(mDocument->displayName()));
            qInfo() << "BMP Rules.txt import skipped because the embedded "
                       "snapshot is already identical:"
                    << QDir::toNativeSeparators(f);
            return;
        }
        const QString question =
                tr("The current TMX contains %1 aliases and %2 rules.\n"
                   "The selected file contains %3 aliases and %4 rules.\n\n"
                   "Replace the embedded Rules snapshot in %5?\n\n"
                   "The previous snapshot will not be kept in the saved TMX. "
                   "Reload is not required. Save the map to keep this change.")
                .arg(bmpSettings->aliases().size())
                .arg(bmpSettings->rules().size())
                .arg(file.aliases().size())
                .arg(file.rules().size())
                .arg(mDocument->displayName());
        if (QMessageBox::question(
                    this, tr("Replace Embedded Rules?"), question,
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes) != QMessageBox::Yes) {
            return;
        }
        settings.setValue(QLatin1String("BmpToolDialog/RulesFile"), f);
        mDocument->undoStack()->push(new ChangeBmpRules(mDocument, f,
                                                        file.aliasesCopy(),
                                                        file.rulesCopy()));
        qInfo() << "Replaced embedded BMP rules snapshot in"
                << mDocument->displayName()
                << "with" << file.aliases().size() << "aliases and"
                << file.rules().size() << "rules from"
                << QDir::toNativeSeparators(f);
    }
}

void BmpToolDialog::exportRules()
{
    QString f = QFileDialog::getSaveFileName(this, tr("Export Rules"),
                                             mDocument->map()->bmpSettings()->rulesFile(),
                                             tr("Rules.txt files (*.txt)"));
    if (!f.isEmpty()) {
        BmpRulesFile file;
        file.fromMap(mDocument->map());
        if (!file.write(f)) {
            QMessageBox::warning(this, tr("Export Rules Failed"), file.errorString());
            return;
        }
    }
}

void BmpToolDialog::trashRules()
{
    mDocument->undoStack()->push(new ChangeBmpRules(mDocument, QString(),
                                                    QList<BmpAlias*>(),
                                                    QList<BmpRule*>()));
}

void BmpToolDialog::reloadBlends()
{
    QString f = mDocument->map()->bmpSettings()->blendsFile();
    if (!f.isEmpty()/* && QFileInfo(f).exists()*/) {
        BmpBlendsFile file;
        if (!file.read(f, mDocument->map()->bmpSettings()->aliases())) {
            QMessageBox::warning(this, tr("Reload Blends Failed"), file.errorString());
            return;
        }
        if (bmpBlendsEqual(
                    file.blends(),
                    mDocument->map()->bmpSettings()->blends())) {
            qInfo() << "BMP Blends.txt reload skipped because the imported "
                       "blends are unchanged:"
                    << QDir::toNativeSeparators(f);
            return;
        }
        mDocument->undoStack()->push(new ChangeBmpBlends(mDocument, f, file.blendsCopy()));
    }
}

void BmpToolDialog::importBlends()
{
    QSettings settings;
    settings.beginGroup(QLatin1String("BmpToolDialog"));
    QString initialDir = settings.value(QLatin1String("BlendsFile")).toString();
    if (initialDir.isEmpty() || !QFileInfo(initialDir).exists())
        initialDir = Preferences::instance()->appConfigPath(QLatin1String("Blends.txt"));
    settings.endGroup();

    QString f = QFileDialog::getOpenFileName(this, tr("Import Blends"),
                                             initialDir,
                                             tr("Blends.txt files (*.txt)"));
    if (!f.isEmpty()) {
        BmpBlendsFile file;
        if (!file.read(f, mDocument->map()->bmpSettings()->aliases())) {
            QMessageBox::warning(this, tr("Import Blends Failed"), file.errorString());
            return;
        }
        const BmpSettings *bmpSettings = mDocument->map()->bmpSettings();
        if (bmpBlendsEqual(file.blends(), bmpSettings->blends())) {
            QMessageBox::information(
                        this,
                        tr("Blends Already Current"),
                        tr("The selected Blends.txt is already identical to the "
                           "snapshot embedded in %1.\n\nNothing was changed and "
                           "Reload is not required.")
                        .arg(mDocument->displayName()));
            qInfo() << "BMP Blends.txt import skipped because the embedded "
                       "snapshot is already identical:"
                    << QDir::toNativeSeparators(f);
            return;
        }
        const QString question =
                tr("The current TMX contains %1 blends.\n"
                   "The selected file contains %2 blends.\n\n"
                   "Replace the embedded Blends snapshot in %3?\n\n"
                   "The previous snapshot will not be kept in the saved TMX. "
                   "Reload is not required. Save the map to keep this change.")
                .arg(bmpSettings->blends().size())
                .arg(file.blends().size())
                .arg(mDocument->displayName());
        if (QMessageBox::question(
                    this, tr("Replace Embedded Blends?"), question,
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes) != QMessageBox::Yes) {
            return;
        }
        settings.setValue(QLatin1String("BmpToolDialog/BlendsFile"), f);
        mDocument->undoStack()->push(new ChangeBmpBlends(mDocument, f, file.blendsCopy()));
        qInfo() << "Replaced embedded BMP blends snapshot in"
                << mDocument->displayName()
                << "with" << file.blends().size() << "blends from"
                << QDir::toNativeSeparators(f);
    }
}

void BmpToolDialog::exportBlends()
{
    QString f = QFileDialog::getSaveFileName(this, tr("Export Blends"),
                                             mDocument->map()->bmpSettings()->blendsFile(),
                                             tr("Blends.txt files (*.txt)"));
    if (!f.isEmpty()) {
        BmpBlendsFile file;
        file.fromMap(mDocument->map());
        if (!file.write(f)) {
            QMessageBox::warning(this, tr("Export Blends Failed"), file.errorString());
            return;
        }
    }
}

void BmpToolDialog::trashBlends()
{
    mDocument->undoStack()->push(new ChangeBmpBlends(mDocument, QString(),
                                                     QList<BmpBlend*>()));
}

void BmpToolDialog::help()
{
    QUrl url = QUrl::fromLocalFile(
                Preferences::instance()->docsPath(QLatin1String("TileZed/BMPTools.html")));
    QDesktopServices::openUrl(url);
}

void BmpToolDialog::bmpRulesChanged()
{
    setDocument(mDocument);
}

void BmpToolDialog::bmpBlendsChanged()
{
    setDocument(mDocument);
}

void BmpToolDialog::bmpBlendEdgesEverywhereChanged()
{
    if (mDocument == nullptr)
        return;
    ui->blendEdgesEverywhere->setChecked(mDocument->map()->bmpSettings()->isBlendEdgesEverywhere());
}

void BmpToolDialog::brushChanged()
{
    int brushSize = BmpBrushTool::instance()->brushSize();
    ui->brushSize->setValue(brushSize);
    switch (BmpBrushTool::instance()->brushShape()) {
    case BmpBrushTool::BrushShape::Square:
        ui->brushSquare->setChecked(true);
        setProceduralBrushUi();
        break;
    case BmpBrushTool::BrushShape::Circle:
        ui->brushCircle->setChecked(true);
        setProceduralBrushUi();
        break;
    case BmpBrushTool::BrushShape::Custom: {
        ui->brushSquare->setAutoExclusive(false);
        ui->brushCircle->setAutoExclusive(false);
        ui->brushSquare->setChecked(false);
        ui->brushCircle->setChecked(false);
        ui->brushSquare->setAutoExclusive(true);
        ui->brushCircle->setAutoExclusive(true);
        ui->brushSize->setEnabled(false);
        const QString selected =
                BmpBrushTool::instance()->customBrushPath();
        for (int index = 1; index < mCustomBrushes->count(); ++index) {
            if (mCustomBrushes->itemData(index).toString().compare(
                        selected, Qt::CaseInsensitive) == 0) {
                const QSignalBlocker blocker(mCustomBrushes);
                mCustomBrushes->setCurrentIndex(index);
                break;
            }
        }
        break;
    }
    }
}

void BmpToolDialog::documentAboutToClose(int index, MapDocument *doc)
{
    Q_UNUSED(index)
    mCurrentRuleForDocument.remove(doc);
}

void BmpToolDialog::warningsChanged()
{
    ui->warnings->clear();
    if (!mDocument)
        return;
    if (ui->repairUnknownColors->isChecked()
            && !mApplyingColorFallback
            && applyUnknownColorFallback()) {
        mDocument->mapComposite()->bmpBlender()->updateWarnings();
        return;
    }
    ui->warnings->addItems(mDocument->mapComposite()->bmpBlender()->warnings());
    ui->tabWidget->setTabIcon(3, ui->warnings->count()
                              ? QIcon(QLatin1String(":/images/24x24/warning.png"))
                              : QIcon());
}

void BmpToolDialog::setDocument(MapDocument *doc)
{
    if (mDocument) {
        mDocument->disconnect(this);
        mDocument->mapComposite()->bmpBlender()->disconnect(this);
        ui->tabWidget->disconnect(mDocument->mapComposite()->bmpBlender());
    }

    mDocument = doc;

    ui->tableView->clear();
    ui->blendView->clear();
    ui->rulesFile->setText(tr("<none>"));
    ui->blendsFile->setText(tr("<none>"));

    ui->reloadRules->setEnabled((mDocument != nullptr) &&
            !mDocument->map()->bmpSettings()->rulesFile().isEmpty());
    ui->importRules->setEnabled(mDocument != nullptr);
    ui->exportRules->setEnabled(mDocument != nullptr);
    ui->trashRules->setEnabled(ui->reloadRules->isEnabled());

    ui->reloadBlends->setEnabled((mDocument != nullptr) &&
            !mDocument->map()->bmpSettings()->blendsFile().isEmpty());
    ui->importBlends->setEnabled(mDocument != nullptr);
    ui->exportBlends->setEnabled(mDocument != nullptr);
    ui->trashBlends->setEnabled(ui->reloadBlends->isEnabled());

    brushChanged();
    ui->restrictToSelection->setChecked(BmpBrushTool::instance()->restrictToSelection());
    ui->fillAllInSelectedArea->setChecked(BmpBrushTool::instance()->fillAllInSelection());
    ui->showBMPTiles->setEnabled(mDocument != nullptr);
    ui->showMapTiles->setEnabled(mDocument != nullptr);
    ui->blendEdgesEverywhere->setEnabled(mDocument != nullptr);

    if (mDocument) {
        ui->tableView->setRules(mDocument->map());
        QList<BmpRule*> rules = mDocument->map()->bmpSettings()->rules();
        populateFallbackColors();

        ui->blendView->setBlends(mDocument->map());

        int ruleIndex = 0;
        if (mCurrentRuleForDocument.contains(mDocument))
            ruleIndex = mCurrentRuleForDocument[mDocument];
        if (ruleIndex >= 0 && ruleIndex < rules.size())
            ui->tableView->setCurrentIndex(
                        ui->tableView->model()->index(rules.at(ruleIndex)));

        const BmpSettings *settings = mDocument->map()->bmpSettings();
        QString fileName = settings->rulesFile();
        if (!fileName.isEmpty())
            ui->rulesFile->setText(QDir::toNativeSeparators(fileName));

        fileName = settings->blendsFile();
        if (!fileName.isEmpty())
            ui->blendsFile->setText(QDir::toNativeSeparators(fileName));

        ui->showBMPTiles->setChecked(mDocument->mapComposite()->showBMPTiles());
        ui->showMapTiles->setChecked(mDocument->mapComposite()->showMapTiles());
        ui->blendEdgesEverywhere->setChecked(mDocument->map()->bmpSettings()->isBlendEdgesEverywhere());

        connect(mDocument, &MapDocument::bmpAliasesChanged, this, &BmpToolDialog::bmpRulesChanged); // XXXXX
        connect(mDocument, &MapDocument::bmpRulesChanged, this, &BmpToolDialog::bmpRulesChanged);
        connect(mDocument, &MapDocument::bmpBlendsChanged, this, &BmpToolDialog::bmpBlendsChanged);
        connect(mDocument, &MapDocument::bmpBlendEdgesEverywhereChanged, this, &BmpToolDialog::bmpBlendEdgesEverywhereChanged);
        connect(mDocument->mapComposite()->bmpBlender(), &BmpBlender::warningsChanged,
                this, &BmpToolDialog::warningsChanged);

        // This is to handle unknown pixels in the BMP images being erased.
        connect(ui->tabWidget, &QTabWidget::currentChanged,
                mDocument->mapComposite()->bmpBlender(), &BmpBlender::updateWarnings);
    }

    warningsChanged();
}

void BmpToolDialog::repairUnknownColorsToggled(bool enabled)
{
    ui->groundFallback->setEnabled(enabled);
    ui->vegetationFallback->setEnabled(enabled);
    if (!enabled || !mDocument)
        return;

    if (applyUnknownColorFallback())
        mDocument->mapComposite()->bmpBlender()->updateWarnings();
}

void BmpToolDialog::populateFallbackColors()
{
    const QRgb previousMain = fallbackColor(0);
    const QRgb previousVegetation = fallbackColor(1);
    ui->groundFallback->clear();
    ui->vegetationFallback->clear();

    const auto addColor = [](QComboBox *combo, QRgb color,
                             const QString &description) {
        QPixmap swatch(18, 18);
        swatch.fill(QColor::fromRgb(color));
        const QString hex = QStringLiteral("#%1")
                .arg(color & 0x00ffffff, 6, 16, QLatin1Char('0'))
                .toUpper();
        combo->addItem(QIcon(swatch),
                       description.isEmpty()
                       ? hex : QStringLiteral("%1 - %2").arg(hex, description),
                       QVariant::fromValue<quint32>(color));
    };

    addColor(ui->groundFallback, qRgb(0, 0, 0), tr("Black / empty"));
    addColor(ui->vegetationFallback, qRgb(0, 0, 0),
             tr("Black / empty"));

    QSet<QRgb> mainColors;
    QSet<QRgb> vegetationColors;
    const QList<BmpRule *> rules =
            mDocument->map()->bmpSettings()->rules();
    for (const BmpRule *rule : rules) {
        QComboBox *combo = rule->bitmapIndex == 0
                ? ui->groundFallback
                : rule->bitmapIndex == 1
                  ? ui->vegetationFallback : nullptr;
        QSet<QRgb> *colors = rule->bitmapIndex == 0
                ? &mainColors
                : rule->bitmapIndex == 1
                  ? &vegetationColors : nullptr;
        if (!combo || !colors || colors->contains(rule->color))
            continue;
        colors->insert(rule->color);
        addColor(combo, rule->color,
                 rule->tileChoices.mid(0, 2).join(
                     QStringLiteral(", ")));
    }

    const auto restoreColor = [](QComboBox *combo, QRgb color) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toUInt() == quint32(color)) {
                combo->setCurrentIndex(i);
                return;
            }
        }
        combo->setCurrentIndex(0);
    };
    restoreColor(ui->groundFallback, previousMain);
    restoreColor(ui->vegetationFallback, previousVegetation);
}

QRgb BmpToolDialog::fallbackColor(int bitmapIndex) const
{
    const QComboBox *combo = bitmapIndex == 0
            ? ui->groundFallback : ui->vegetationFallback;
    if (!combo || combo->currentIndex() < 0)
        return qRgb(0, 0, 0);
    return QRgb(combo->currentData().toUInt());
}

bool BmpToolDialog::applyUnknownColorFallback()
{
    if (!mDocument || mApplyingColorFallback)
        return false;

    QSet<QRgb> knownMain;
    QSet<QRgb> knownVegetation;
    knownMain.insert(qRgb(0, 0, 0));
    knownVegetation.insert(qRgb(0, 0, 0));
    for (const BmpRule *rule : mDocument->map()->bmpSettings()->rules()) {
        if (rule->bitmapIndex == 0)
            knownMain.insert(rule->color);
        else if (rule->bitmapIndex == 1)
            knownVegetation.insert(rule->color);
    }

    QImage mainImage = mDocument->map()->bmpMain().image();
    QImage vegetationImage = mDocument->map()->bmpVeg().image();
    int replacedMain = 0;
    int replacedVegetation = 0;
    const QRgb mainFallback = fallbackColor(0);
    const QRgb vegetationFallback = fallbackColor(1);

    for (int y = 0; y < mainImage.height(); ++y) {
        for (int x = 0; x < mainImage.width(); ++x) {
            if (!knownMain.contains(mainImage.pixel(x, y))) {
                mainImage.setPixel(x, y, mainFallback);
                ++replacedMain;
            }
        }
    }
    for (int y = 0; y < vegetationImage.height(); ++y) {
        for (int x = 0; x < vegetationImage.width(); ++x) {
            if (!knownVegetation.contains(vegetationImage.pixel(x, y))) {
                vegetationImage.setPixel(x, y, vegetationFallback);
                ++replacedVegetation;
            }
        }
    }

    if (replacedMain == 0 && replacedVegetation == 0)
        return false;

    mApplyingColorFallback = true;
    mDocument->undoStack()->push(new ReplaceUnknownBmpPixels(
        mDocument, mainImage, vegetationImage));
    mApplyingColorFallback = false;
    qInfo() << "BMP color fallback replaced"
            << replacedMain << "Main pixel(s) with"
            << QColor::fromRgb(mainFallback).name(QColor::HexRgb)
            << "and" << replacedVegetation
            << "Vegetation pixel(s) with"
            << QColor::fromRgb(vegetationFallback).name(QColor::HexRgb);
    return true;
}

void BmpToolDialog::changeBmpRules(MapDocument *doc, const QString &fileName,
                                   const QList<BmpAlias *> &aliases,
                                   const QList<BmpRule *> &rules)
{
    doc->undoStack()->push(new ChangeBmpRules(doc, fileName, aliases, rules));
}

void BmpToolDialog::changeBmpBlends(MapDocument *doc, const QString &fileName,
                                    const QList<BmpBlend *> &blends)
{
    doc->undoStack()->push(new ChangeBmpBlends(doc, fileName, blends));
}

void BmpToolDialog::currentRuleChanged(const QModelIndex &current)
{
    if (BmpRule *rule = ui->tableView->model()->ruleAt(current)) {
        BmpBrushTool::instance()->setColor(rule->bitmapIndex, rule->color);
        mCurrentRuleForDocument[mDocument]
                = mDocument->map()->bmpSettings()->rules().indexOf(rule);
    } else {
        BmpBrushTool::instance()->setColor(0, qRgb(0, 0, 0));
    }
}

void BmpToolDialog::brushSizeChanged(int size)
{
    BmpBrushTool::instance()->setBrushSize(size);
}

void BmpToolDialog::brushSquare()
{
    setProceduralBrushUi();
    BmpBrushTool::instance()->setBrushShape(BmpBrushTool::BrushShape::Square);
}

void BmpToolDialog::brushCircle()
{
    setProceduralBrushUi();
    BmpBrushTool::instance()->setBrushShape(BmpBrushTool::BrushShape::Circle);
}

void BmpToolDialog::restrictToSelection(bool isRestricted)
{
    BmpBrushTool::instance()->setRestrictToSelection(isRestricted);
}

void BmpToolDialog::fillAllInSelection(bool fillAll)
{
    BmpBrushTool::instance()->setFillAllInSelection(fillAll);
}

void BmpToolDialog::toggleOverlayLayers()
{
    if (!mDocument)
        return;
    Map *map = mDocument->map();
    int visible = -1;
    foreach (QString layerName, mDocument->mapComposite()->bmpBlender()->blendLayers()) {
        int index = map->indexOfLayer(layerName);
        if (index != -1) {
            Layer *layer = map->layerAt(index);
            if (visible == -1)
                visible = !layer->isVisible();
            mDocument->setLayerVisible(index, visible);
        }
    }
}

void BmpToolDialog::showBMPTiles(bool show)
{
    if (!mDocument)
        return;
    mDocument->mapComposite()->setShowBMPTiles(show);
    if (mDocument->map()->layerCount())
        mDocument->emitRegionChanged(QRect(QPoint(0, 0), mDocument->map()->size()),
                                     mDocument->map()->layerAt(0));
}

void BmpToolDialog::showMapTiles(bool show)
{
    if (!mDocument)
        return;
    mDocument->mapComposite()->setShowMapTiles(show);
    if (mDocument->map()->layerCount())
        mDocument->emitRegionChanged(QRect(QPoint(0, 0), mDocument->map()->size()),
                                     mDocument->map()->layerAt(0));
}

void BmpToolDialog::blendEdgesEverywhere(bool everywhere)
{
    if (mDocument == nullptr)
        return;
    mDocument->setBlendEdgesEverywhere(everywhere);
}
