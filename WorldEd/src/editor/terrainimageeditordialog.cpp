#include "terrainimageeditordialog.h"

#include "bmpblender.h"
#include "preferences.h"
#include "world.h"
#include "worlddocument.h"
#include "../portablesettings.h"

#include "map.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QSet>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

#include <cmath>

using namespace Tiled;

namespace {

qint64 workingImageBytes(const QSize &size)
{
    // Ground, vegetation and the cached vegetation preview are ARGB32.
    return qint64(size.width()) * qint64(size.height()) * 12;
}

qint64 imageBytes(const QImage &image)
{
    return qint64(image.bytesPerLine()) * qint64(image.height());
}

qint64 historyBudgetBytes()
{
    const qint64 configuredBytes =
            qint64(Preferences::instance()->terrainImageMemoryLimitMiB())
            * 1024 * 1024;
    return qMax(qint64(64) * 1024 * 1024, configuredBytes / 4);
}

QString memorySizeText(qint64 bytes)
{
    const qreal mib = qreal(bytes) / (1024.0 * 1024.0);
    if (mib < 1024.0)
        return QString::number(mib, 'f', mib < 100.0 ? 1 : 0) +
                QLatin1String(" MiB");
    return QString::number(mib / 1024.0, 'f', 1) + QLatin1String(" GiB");
}

} // namespace

TerrainImageCanvas::TerrainImageCanvas(QWidget *parent)
    : QWidget(parent)
    , mPaintColor(90, 100, 35)
    , mHoverPoint(-1, -1)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    updateCanvasSize();
}

QSize TerrainImageCanvas::sizeHint() const
{
    if (mGroundImage.isNull())
        return QSize(640, 480);
    const qreal scale = mZoomPercent / 100.0;
    return QSize(qMax(1, qRound(mGroundImage.width() * scale)),
                 qMax(1, qRound(mGroundImage.height() * scale)));
}

void TerrainImageCanvas::setImages(const QImage &ground,
                                   const QImage &vegetation,
                                   int cellSize)
{
    mGroundImage = ground.convertToFormat(QImage::Format_ARGB32);
    mVegetationImage = vegetation.convertToFormat(QImage::Format_ARGB32);
    mCellSize = qMax(1, cellSize);
    rebuildVegetationOverlay();
    updateCanvasSize();
    update();
}

void TerrainImageCanvas::setActiveLayer(int bitmapIndex)
{
    mActiveLayer = qBound(0, bitmapIndex, 1);
    update();
}

void TerrainImageCanvas::setPaintColor(const QColor &color)
{
    if (color.isValid())
        mPaintColor = color;
    update();
}

void TerrainImageCanvas::setBrushRadius(int radius)
{
    mBrushRadius = qMax(0, radius);
    update();
}

void TerrainImageCanvas::setZoomPercent(int percent)
{
    mZoomPercent = qBound(10, percent, 800);
    updateCanvasSize();
    update();
}

void TerrainImageCanvas::setTool(int tool)
{
    mTool = tool == FillTool ? FillTool : BrushTool;
    setCursor(mTool == FillTool ? Qt::PointingHandCursor : Qt::CrossCursor);
}

void TerrainImageCanvas::setCompositePreview(bool composite)
{
    mCompositePreview = composite;
    update();
}

void TerrainImageCanvas::setVegetationOpacity(int percent)
{
    mVegetationOpacity = qBound(0, percent, 100);
    rebuildVegetationOverlay();
    update();
}

void TerrainImageCanvas::replaceImages(const QImage &ground,
                                       const QImage &vegetation)
{
    setImages(ground, vegetation, mCellSize);
    emit imageChanged();
}

void TerrainImageCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 35, 39));
    if (mGroundImage.isNull())
        return;

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QRect target(QPoint(0, 0), sizeHint());
    if (mCompositePreview) {
        painter.drawImage(target, mGroundImage);
        painter.drawImage(target, mVegetationOverlay);
    } else if (mActiveLayer == 0) {
        painter.drawImage(target, mGroundImage);
    } else {
        painter.drawImage(target, mVegetationImage);
    }

    const qreal scale = mZoomPercent / 100.0;
    painter.setPen(QPen(QColor(255, 255, 255, 100), 1));
    for (int x = mCellSize; x < mGroundImage.width(); x += mCellSize)
        painter.drawLine(qRound(x * scale), 0,
                         qRound(x * scale), target.height());
    for (int y = mCellSize; y < mGroundImage.height(); y += mCellSize)
        painter.drawLine(0, qRound(y * scale),
                         target.width(), qRound(y * scale));

    if (containsImagePoint(mHoverPoint)) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1));
        const qreal radius = qMax(0.5, (mBrushRadius + 0.5) * scale);
        const QPointF center((mHoverPoint.x() + 0.5) * scale,
                             (mHoverPoint.y() + 0.5) * scale);
        painter.drawEllipse(center, radius, radius);
    }
}

void TerrainImageCanvas::mousePressEvent(QMouseEvent *event)
{
    const QPoint point = imagePoint(event->pos());
    if (!containsImagePoint(point))
        return;
    if (event->button() == Qt::RightButton) {
        const QImage &image = mActiveLayer == 0
                ? mGroundImage : mVegetationImage;
        emit colorPicked(QColor::fromRgb(image.pixel(point)));
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (mTool == FillTool) {
        emit editStarted(tr("Fill"));
        floodFill(point);
        emit editFinished();
        return;
    }
    emit editStarted(mActiveLayer == 0
                     ? tr("Paint ground") : tr("Paint vegetation"));
    mPainting = true;
    mLastPoint = point;
    paintSegment(point, point);
}

void TerrainImageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint point = imagePoint(event->pos());
    mHoverPoint = point;
    emit pointerMoved(point);
    if (mPainting && (event->buttons() & Qt::LeftButton) &&
            containsImagePoint(point)) {
        paintSegment(mLastPoint, point);
        mLastPoint = point;
    }
    update();
}

void TerrainImageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && mPainting) {
        mPainting = false;
        emit editFinished();
    }
}

void TerrainImageCanvas::leaveEvent(QEvent *)
{
    mHoverPoint = QPoint(-1, -1);
    emit pointerMoved(mHoverPoint);
    update();
}

QPoint TerrainImageCanvas::imagePoint(const QPoint &widgetPoint) const
{
    const qreal scale = mZoomPercent / 100.0;
    return QPoint(qFloor(widgetPoint.x() / scale),
                  qFloor(widgetPoint.y() / scale));
}

bool TerrainImageCanvas::containsImagePoint(const QPoint &point) const
{
    return !mGroundImage.isNull()
            && point.x() >= 0 && point.x() < mGroundImage.width()
            && point.y() >= 0 && point.y() < mGroundImage.height();
}

void TerrainImageCanvas::paintSegment(const QPoint &from, const QPoint &to)
{
    QImage *image = mActiveLayer == 0
            ? &mGroundImage : &mVegetationImage;
    if (image->isNull())
        return;

    QPainter painter(image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(mPaintColor);
    pen.setWidth(qMax(1, mBrushRadius * 2 + 1));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(from, to);
    painter.end();
    if (mActiveLayer == 1) {
        const QRect affected = QRect(from, to).normalized()
                .adjusted(-mBrushRadius - 2, -mBrushRadius - 2,
                          mBrushRadius + 2, mBrushRadius + 2)
                .intersected(mVegetationImage.rect());
        rebuildVegetationOverlay(affected);
    }
    emit imageChanged();
    update();
}

void TerrainImageCanvas::floodFill(const QPoint &point)
{
    QImage *image = mActiveLayer == 0
            ? &mGroundImage : &mVegetationImage;
    if (image->isNull() || !containsImagePoint(point))
        return;

    const QRgb target = image->pixel(point);
    const QRgb replacement = mPaintColor.rgb();
    if ((target & 0x00ffffff) == (replacement & 0x00ffffff))
        return;

    QVector<QPoint> pending;
    pending.reserve(1024);
    pending += point;
    while (!pending.isEmpty()) {
        const QPoint seed = pending.takeLast();
        if (!image->rect().contains(seed) ||
                ((image->pixel(seed) & 0x00ffffff) !=
                 (target & 0x00ffffff)))
            continue;

        int left = seed.x();
        int right = seed.x();
        while (left > 0 &&
               ((image->pixel(left - 1, seed.y()) & 0x00ffffff) ==
                (target & 0x00ffffff)))
            --left;
        while (right + 1 < image->width() &&
               ((image->pixel(right + 1, seed.y()) & 0x00ffffff) ==
                (target & 0x00ffffff)))
            ++right;

        QRgb *line = reinterpret_cast<QRgb *>(image->scanLine(seed.y()));
        for (int x = left; x <= right; ++x)
            line[x] = qRgb(mPaintColor.red(),
                           mPaintColor.green(), mPaintColor.blue());

        for (int yOffset : {-1, 1}) {
            const int y = seed.y() + yOffset;
            if (y < 0 || y >= image->height())
                continue;
            bool inSpan = false;
            for (int x = left; x <= right; ++x) {
                const bool matches =
                        ((image->pixel(x, y) & 0x00ffffff) ==
                         (target & 0x00ffffff));
                if (matches && !inSpan) {
                    pending += QPoint(x, y);
                    inSpan = true;
                } else if (!matches) {
                    inSpan = false;
                }
            }
        }
    }

    if (mActiveLayer == 1)
        rebuildVegetationOverlay();
    emit imageChanged();
    update();
}

void TerrainImageCanvas::rebuildVegetationOverlay(const QRect &area)
{
    if (mVegetationImage.isNull()) {
        mVegetationOverlay = QImage();
        return;
    }
    if (mVegetationOverlay.size() != mVegetationImage.size() ||
            mVegetationOverlay.format() != QImage::Format_ARGB32) {
        mVegetationOverlay = QImage(mVegetationImage.size(),
                                    QImage::Format_ARGB32);
        mVegetationOverlay.fill(Qt::transparent);
    }

    const QRect bounds = area.isEmpty()
            ? mVegetationImage.rect()
            : area.intersected(mVegetationImage.rect());
    const int alpha = qRound(mVegetationOpacity * 255.0 / 100.0);
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        const QRgb *source = reinterpret_cast<const QRgb *>(
                    mVegetationImage.constScanLine(y));
        QRgb *target = reinterpret_cast<QRgb *>(
                    mVegetationOverlay.scanLine(y));
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const QRgb color = source[x];
            const bool empty = qRed(color) == 0 &&
                    qGreen(color) == 0 && qBlue(color) == 0;
            target[x] = empty
                    ? qRgba(0, 0, 0, 0)
                    : qRgba(qRed(color), qGreen(color),
                            qBlue(color), alpha);
        }
    }
}

void TerrainImageCanvas::updateCanvasSize()
{
    resize(sizeHint());
    updateGeometry();
}

TerrainImageEditorDialog::TerrainImageEditorDialog(
        WorldDocument *worldDocument, QWidget *parent)
    : QDialog(parent)
    , mWorldDocument(worldDocument)
    , mCanvas(new TerrainImageCanvas(this))
    , mRulesPath(new QLineEdit(this))
    , mGroundPath(new QLineEdit(this))
    , mPalette(new QComboBox(this))
    , mImageStatus(new QLabel(this))
    , mPointerStatus(new QLabel(this))
    , mCellsWide(new QSpinBox(this))
    , mCellsHigh(new QSpinBox(this))
    , mBrushRadius(new QSpinBox(this))
    , mZoom(new QSpinBox(this))
    , mSeed(new QSpinBox(this))
    , mFeatureWidth(new QSpinBox(this))
    , mFeatureCount(new QSpinBox(this))
    , mDensity(new QSpinBox(this))
    , mOriginX(new QSpinBox(this))
    , mOriginY(new QSpinBox(this))
    , mAttachToProject(new QCheckBox(this))
    , mEraserButton(new QToolButton(this))
    , mUndoButton(new QPushButton(this))
    , mRedoButton(new QPushButton(this))
{
    setWindowTitle(tr("Terrain and Vegetation Image Editor"));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    resize(1280, 820);

    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    const int cellSize = world ? world->cellSize() : 300;
    mCellsWide->setRange(1, 256);
    mCellsHigh->setRange(1, 256);
    mCellsWide->setValue(world && !world->bmps().isEmpty()
                         ? qMax(1, world->bmps().first()->width())
                         : world ? qMax(1, world->width()) : 1);
    mCellsHigh->setValue(world && !world->bmps().isEmpty()
                         ? qMax(1, world->bmps().first()->height())
                         : world ? qMax(1, world->height()) : 1);
    mCellsWide->setSuffix(tr(" cells"));
    mCellsHigh->setSuffix(tr(" cells"));

    mBrushRadius->setRange(0, 128);
    mBrushRadius->setValue(4);
    mBrushRadius->setSuffix(tr(" px"));
    mZoom->setRange(10, 800);
    mZoom->setValue(100);
    mZoom->setSuffix(QLatin1String("%"));
    mSeed->setRange(0, 999999999);
    mSeed->setValue(1337);
    mFeatureWidth->setRange(1, qMax(8, cellSize));
    mFeatureWidth->setValue(qMax(4, cellSize / 30));
    mFeatureWidth->setSuffix(tr(" squares"));
    mFeatureCount->setRange(1, 64);
    mFeatureCount->setValue(3);
    mDensity->setRange(1, 100);
    mDensity->setValue(45);
    mDensity->setSuffix(QLatin1String("%"));
    mOriginX->setRange(-100000, 100000);
    mOriginY->setRange(-100000, 100000);
    mAttachToProject->setText(tr("Attach/update this image in the project"));
    mAttachToProject->setChecked(true);
    if (world && !world->bmps().isEmpty()) {
        mOriginX->setValue(world->bmps().first()->x());
        mOriginY->setValue(world->bmps().first()->y());
    } else if (mWorldDocument &&
               !mWorldDocument->selectedCells().isEmpty()) {
        mOriginX->setValue(mWorldDocument->selectedCells().first()->x());
        mOriginY->setValue(mWorldDocument->selectedCells().first()->y());
    }

    QVBoxLayout *sideLayout = new QVBoxLayout;

    QGroupBox *filesGroup = new QGroupBox(tr("Files"), this);
    QGridLayout *filesLayout = new QGridLayout(filesGroup);
    QToolButton *browseRulesButton = new QToolButton(filesGroup);
    browseRulesButton->setText(QLatin1String("..."));
    QPushButton *reloadRulesButton =
            new QPushButton(tr("Reload palette"), filesGroup);
    QToolButton *browseGroundButton = new QToolButton(filesGroup);
    browseGroundButton->setText(QLatin1String("..."));
    filesLayout->addWidget(new QLabel(tr("Rules.txt:"), filesGroup), 0, 0);
    filesLayout->addWidget(mRulesPath, 0, 1);
    filesLayout->addWidget(browseRulesButton, 0, 2);
    filesLayout->addWidget(reloadRulesButton, 1, 1, 1, 2);
    filesLayout->addWidget(new QLabel(tr("Ground PNG:"), filesGroup), 2, 0);
    filesLayout->addWidget(mGroundPath, 2, 1);
    filesLayout->addWidget(browseGroundButton, 2, 2);
    QLabel *pairHint = new QLabel(
                tr("The vegetation layer is saved beside it as *_veg.png."),
                filesGroup);
    pairHint->setWordWrap(true);
    filesLayout->addWidget(pairHint, 3, 0, 1, 3);
    sideLayout->addWidget(filesGroup);

    QGroupBox *documentGroup = new QGroupBox(tr("Image"), this);
    QFormLayout *documentLayout = new QFormLayout(documentGroup);
    documentLayout->addRow(tr("Width:"), mCellsWide);
    documentLayout->addRow(tr("Height:"), mCellsHigh);
    QLabel *cellSizeLabel = new QLabel(
                tr("%1 x %1 pixels per cell").arg(cellSize), documentGroup);
    documentLayout->addRow(tr("Project scale:"), cellSizeLabel);
    QHBoxLayout *originLayout = new QHBoxLayout;
    originLayout->addWidget(new QLabel(tr("X:"), documentGroup));
    originLayout->addWidget(mOriginX);
    originLayout->addWidget(new QLabel(tr("Y:"), documentGroup));
    originLayout->addWidget(mOriginY);
    documentLayout->addRow(tr("Cell origin:"), originLayout);
    documentLayout->addRow(mAttachToProject);
    QHBoxLayout *documentButtons = new QHBoxLayout;
    QPushButton *newButton = new QPushButton(tr("New"), documentGroup);
    QPushButton *openButton = new QPushButton(tr("Open"), documentGroup);
    QPushButton *saveButton = new QPushButton(tr("Save"), documentGroup);
    QPushButton *saveAsButton = new QPushButton(tr("Save As"), documentGroup);
    documentButtons->addWidget(newButton);
    documentButtons->addWidget(openButton);
    documentButtons->addWidget(saveButton);
    documentButtons->addWidget(saveAsButton);
    documentLayout->addRow(documentButtons);
    sideLayout->addWidget(documentGroup);

    QGroupBox *paintGroup = new QGroupBox(tr("Paint"), this);
    QVBoxLayout *paintLayout = new QVBoxLayout(paintGroup);
    QHBoxLayout *historyLayout = new QHBoxLayout;
    mUndoButton->setText(tr("Undo"));
    mRedoButton->setText(tr("Redo"));
    mUndoButton->setShortcut(QKeySequence::Undo);
    mRedoButton->setShortcut(QKeySequence::Redo);
    historyLayout->addWidget(mUndoButton);
    historyLayout->addWidget(mRedoButton);
    paintLayout->addLayout(historyLayout);

    QHBoxLayout *toolLayout = new QHBoxLayout;
    QToolButton *brushTool = new QToolButton(paintGroup);
    QToolButton *fillTool = new QToolButton(paintGroup);
    brushTool->setText(tr("Brush"));
    fillTool->setText(tr("Fill"));
    brushTool->setCheckable(true);
    fillTool->setCheckable(true);
    brushTool->setChecked(true);
    QButtonGroup *paintTools = new QButtonGroup(paintGroup);
    paintTools->setExclusive(true);
    paintTools->addButton(brushTool, TerrainImageCanvas::BrushTool);
    paintTools->addButton(fillTool, TerrainImageCanvas::FillTool);
    mEraserButton->setText(tr("Erase vegetation"));
    mEraserButton->setCheckable(true);
    mEraserButton->setEnabled(false);
    toolLayout->addWidget(brushTool);
    toolLayout->addWidget(fillTool);
    toolLayout->addWidget(mEraserButton);
    paintLayout->addLayout(toolLayout);

    QRadioButton *groundLayer = new QRadioButton(tr("Ground (bitmap 0)"),
                                                 paintGroup);
    QRadioButton *vegetationLayer =
            new QRadioButton(tr("Vegetation (bitmap 1)"), paintGroup);
    groundLayer->setChecked(true);
    QButtonGroup *layers = new QButtonGroup(paintGroup);
    layers->addButton(groundLayer, 0);
    layers->addButton(vegetationLayer, 1);
    paintLayout->addWidget(groundLayer);
    paintLayout->addWidget(vegetationLayer);
    paintLayout->addWidget(mPalette);
    QFormLayout *paintForm = new QFormLayout;
    paintForm->addRow(tr("Brush radius:"), mBrushRadius);
    paintForm->addRow(tr("Zoom:"), mZoom);
    QCheckBox *compositePreview =
            new QCheckBox(tr("Combined ground + vegetation preview"),
                          paintGroup);
    compositePreview->setChecked(true);
    QSpinBox *vegetationOpacity = new QSpinBox(paintGroup);
    vegetationOpacity->setRange(0, 100);
    vegetationOpacity->setValue(70);
    vegetationOpacity->setSuffix(QLatin1String("%"));
    paintForm->addRow(compositePreview);
    paintForm->addRow(tr("Vegetation opacity:"), vegetationOpacity);
    paintLayout->addLayout(paintForm);
    QLabel *paintHint = new QLabel(
                tr("Left click applies the selected tool. Right click picks "
                   "a Rules.txt color. White lines are cell boundaries."),
                paintGroup);
    paintHint->setWordWrap(true);
    paintLayout->addWidget(paintHint);
    sideLayout->addWidget(paintGroup);

    QGroupBox *proceduralGroup = new QGroupBox(
                tr("Procedural generation"), this);
    QFormLayout *proceduralLayout = new QFormLayout(proceduralGroup);
    proceduralLayout->addRow(tr("Seed:"), mSeed);
    proceduralLayout->addRow(tr("Width / radius:"), mFeatureWidth);
    proceduralLayout->addRow(tr("Count:"), mFeatureCount);
    proceduralLayout->addRow(tr("Density:"), mDensity);
    QGridLayout *generatorButtons = new QGridLayout;
    QPushButton *terrainButton =
            new QPushButton(tr("Terrain patches"), proceduralGroup);
    QPushButton *vegetationButton =
            new QPushButton(tr("Vegetation cover"), proceduralGroup);
    QPushButton *riverButton =
            new QPushButton(tr("River"), proceduralGroup);
    QPushButton *lakeButton =
            new QPushButton(tr("Lake"), proceduralGroup);
    QPushButton *roadsButton =
            new QPushButton(tr("Road network"), proceduralGroup);
    generatorButtons->addWidget(terrainButton, 0, 0);
    generatorButtons->addWidget(vegetationButton, 0, 1);
    generatorButtons->addWidget(riverButton, 1, 0);
    generatorButtons->addWidget(lakeButton, 1, 1);
    generatorButtons->addWidget(roadsButton, 2, 0, 1, 2);
    proceduralLayout->addRow(generatorButtons);
    QLabel *generatorHint = new QLabel(
                tr("Generation runs on the complete image, so rivers and roads "
                   "remain continuous across 256/300-cell boundaries."),
                proceduralGroup);
    generatorHint->setWordWrap(true);
    proceduralLayout->addRow(generatorHint);
    sideLayout->addWidget(proceduralGroup);
    sideLayout->addStretch();

    QWidget *sideWidget = new QWidget(this);
    sideWidget->setLayout(sideLayout);
    QScrollArea *controlsScroll = new QScrollArea(this);
    controlsScroll->setWidget(sideWidget);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setMinimumWidth(350);
    controlsScroll->setMaximumWidth(450);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(mCanvas);
    scrollArea->setWidgetResizable(false);
    scrollArea->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QSplitter *splitter = new QSplitter(this);
    splitter->addWidget(controlsScroll);
    splitter->addWidget(scrollArea);
    splitter->setStretchFactor(1, 1);

    QHBoxLayout *statusLayout = new QHBoxLayout;
    statusLayout->addWidget(mImageStatus, 1);
    statusLayout->addWidget(mPointerStatus);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addLayout(statusLayout);

    connect(browseRulesButton, &QToolButton::clicked,
            this, &TerrainImageEditorDialog::browseRules);
    connect(reloadRulesButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::reloadRules);
    connect(browseGroundButton, &QToolButton::clicked,
            this, &TerrainImageEditorDialog::openImages);
    connect(newButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::newImages);
    connect(openButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::openImages);
    connect(saveButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::saveImages);
    connect(saveAsButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::saveImagesAs);
    connect(mUndoButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::undoImageEdit);
    connect(mRedoButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::redoImageEdit);
    connect(paintTools, QOverload<int>::of(&QButtonGroup::buttonClicked),
            mCanvas, &TerrainImageCanvas::setTool);
    connect(layers, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &TerrainImageEditorDialog::activeLayerChanged);
    connect(mPalette, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TerrainImageEditorDialog::paletteChanged);
    connect(mBrushRadius, QOverload<int>::of(&QSpinBox::valueChanged),
            mCanvas, &TerrainImageCanvas::setBrushRadius);
    connect(mZoom, QOverload<int>::of(&QSpinBox::valueChanged),
            mCanvas, &TerrainImageCanvas::setZoomPercent);
    connect(compositePreview, &QCheckBox::toggled,
            mCanvas, &TerrainImageCanvas::setCompositePreview);
    connect(vegetationOpacity,
            QOverload<int>::of(&QSpinBox::valueChanged),
            mCanvas, &TerrainImageCanvas::setVegetationOpacity);
    connect(mEraserButton, &QToolButton::toggled,
            this, &TerrainImageEditorDialog::eraseVegetationToggled);
    connect(mAttachToProject, &QCheckBox::toggled,
            mOriginX, &QSpinBox::setEnabled);
    connect(mAttachToProject, &QCheckBox::toggled,
            mOriginY, &QSpinBox::setEnabled);
    connect(mCanvas, &TerrainImageCanvas::editStarted,
            this, &TerrainImageEditorDialog::beginImageEdit);
    connect(mCanvas, &TerrainImageCanvas::editFinished,
            this, &TerrainImageEditorDialog::endImageEdit);
    connect(mCanvas, &TerrainImageCanvas::colorPicked,
            this, &TerrainImageEditorDialog::pickedColor);
    connect(mCanvas, &TerrainImageCanvas::pointerMoved,
            this, &TerrainImageEditorDialog::updatePointerStatus);
    connect(terrainButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::generateTerrainPatches);
    connect(vegetationButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::generateVegetation);
    connect(riverButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::generateRiver);
    connect(lakeButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::generateLake);
    connect(roadsButton, &QPushButton::clicked,
            this, &TerrainImageEditorDialog::generateRoads);

    mRulesPath->setText(QDir::toNativeSeparators(defaultRulesPath()));
    mGroundPath->setText(QDir::toNativeSeparators(defaultGroundPath()));
    reloadRules();

    const QString initialGround = normalizedGroundPath(mGroundPath->text());
    if (!initialGround.isEmpty() && QFileInfo::exists(initialGround))
        loadImagePair(initialGround);
    else
        newImages();
}

void TerrainImageEditorDialog::closeEvent(QCloseEvent *event)
{
    if (maybeDiscardChanges())
        event->accept();
    else
        event->ignore();
}

void TerrainImageEditorDialog::browseRules()
{
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Select Rules.txt"),
                QFileInfo(mRulesPath->text()).absolutePath(),
                tr("Rules files (*.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    mRulesPath->setText(QDir::toNativeSeparators(path));
    reloadRules();
}

void TerrainImageEditorDialog::reloadRules()
{
    const QString path = QDir::fromNativeSeparators(
                mRulesPath->text().trimmed());
    if (!loadRulesFile(path)) {
        QMessageBox::warning(this, tr("Rules.txt"),
                             tr("The Rules.txt palette could not be loaded:\n%1")
                             .arg(QDir::toNativeSeparators(path)));
    }
}

void TerrainImageEditorDialog::newImages()
{
    if (!maybeDiscardChanges())
        return;
    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    const int cellSize = world ? world->cellSize() : 300;
    const QSize size(mCellsWide->value() * cellSize,
                     mCellsHigh->value() * cellSize);
    if (!validateWorkingImageSize(size, tr("Create images")))
        return;

    QImage ground(size, QImage::Format_ARGB32);
    QImage vegetation(size, QImage::Format_ARGB32);
    if (ground.isNull() || vegetation.isNull()) {
        QMessageBox::critical(
                    this, tr("Not Enough Memory"),
                    tr("WorldEd could not allocate the %1 x %2 pixel ground "
                       "and vegetation images. Reduce the number of cells and "
                       "try again.")
                    .arg(size.width()).arg(size.height()));
        return;
    }
    QColor base = mLayerColors[0];
    if (!base.isValid())
        base = QColor(90, 100, 35);
    ground.fill(base.rgb());
    vegetation.fill(Qt::black);
    mCanvas->setImages(ground, vegetation, cellSize);
    resetHistory(true);
    updateImageStatus();
}

void TerrainImageEditorDialog::openImages()
{
    if (!maybeDiscardChanges())
        return;
    const QString current = normalizedGroundPath(mGroundPath->text());
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Open Ground or Vegetation PNG"),
                QFileInfo(current).absolutePath(),
                tr("PNG images (*.png)"));
    if (!path.isEmpty())
        loadImagePair(normalizedGroundPath(path));
}

bool TerrainImageEditorDialog::saveImages()
{
    QString groundPath = normalizedGroundPath(mGroundPath->text());
    if (groundPath.isEmpty())
        return saveImagesAs();
    if (mCanvas->groundImage().isNull())
        return false;

    QString attachmentError;
    if (mAttachToProject->isChecked() &&
            !validateAttachment(&attachmentError)) {
        QMessageBox::warning(this, tr("Attach Images"),
                             attachmentError);
        return false;
    }

    const QFileInfo info(groundPath);
    if (!QDir().mkpath(info.absolutePath())) {
        QMessageBox::warning(this, tr("Save Images"),
                             tr("The map directory could not be created."));
        return false;
    }

    const QString vegetationPath = vegetationPathFor(groundPath);
    const QStringList paths = {groundPath, vegetationPath};
    for (const QString &path : paths) {
        if (!QFileInfo::exists(path))
            continue;
        const QString backup = path + QLatin1String(".before-editor.bak");
        if (!QFileInfo::exists(backup))
            QFile::copy(path, backup);
    }

    QString error;
    if (!savePngAtomically(mCanvas->groundImage(), groundPath, &error) ||
            !savePngAtomically(mCanvas->vegetationImage(),
                               vegetationPath, &error)) {
        QMessageBox::warning(this, tr("Save Images"), error);
        return false;
    }

    if (mAttachToProject->isChecked() &&
            !attachImagesToWorld(groundPath, &error)) {
        QMessageBox::warning(
                    this, tr("Attach Images"),
                    tr("The PNG files were saved, but the project could not "
                       "be updated:\n%1").arg(error));
        mSavedRevision = mRevision;
        setDirty(false);
        return false;
    }

    mGroundPath->setText(QDir::toNativeSeparators(groundPath));
    mSavedRevision = mRevision;
    setDirty(false);
    updateImageStatus();
    return true;
}

bool TerrainImageEditorDialog::saveImagesAs()
{
    const QString path = QFileDialog::getSaveFileName(
                this, tr("Save Ground PNG"),
                normalizedGroundPath(mGroundPath->text()),
                tr("PNG images (*.png)"));
    if (path.isEmpty())
        return false;
    mGroundPath->setText(QDir::toNativeSeparators(
                             normalizedGroundPath(path)));
    return saveImages();
}

void TerrainImageEditorDialog::activeLayerChanged(int bitmapIndex)
{
    mActiveLayer = qBound(0, bitmapIndex, 1);
    mCanvas->setActiveLayer(mActiveLayer);
    mEraserButton->setEnabled(mActiveLayer == 1);
    if (mActiveLayer == 0)
        mEraserButton->setChecked(false);
    populatePalette(mActiveLayer);
}

void TerrainImageEditorDialog::paletteChanged(int index)
{
    if (index < 0)
        return;
    const QColor color = mPalette->itemData(index).value<QColor>();
    if (!color.isValid())
        return;
    mLayerColors[mActiveLayer] = color;
    if (mEraserButton->isChecked())
        mEraserButton->setChecked(false);
    mCanvas->setPaintColor(color);
}

void TerrainImageEditorDialog::pickedColor(const QColor &color)
{
    if (mActiveLayer == 1 && color.red() == 0 &&
            color.green() == 0 && color.blue() == 0) {
        mEraserButton->setChecked(true);
        return;
    }
    for (int index = 0; index < mPalette->count(); ++index) {
        if (mPalette->itemData(index).value<QColor>().rgb() == color.rgb()) {
            mPalette->setCurrentIndex(index);
            return;
        }
    }
}

void TerrainImageEditorDialog::eraseVegetationToggled(bool checked)
{
    if (checked && mActiveLayer == 1) {
        mCanvas->setPaintColor(Qt::black);
        return;
    }
    const int index = mPalette->currentIndex();
    if (index >= 0)
        mCanvas->setPaintColor(
                    mPalette->itemData(index).value<QColor>());
}

void TerrainImageEditorDialog::undoImageEdit()
{
    if (mUndoHistory.isEmpty())
        return;

    HistoryState current;
    current.ground = mCanvas->groundImage();
    current.vegetation = mCanvas->vegetationImage();
    current.label = mUndoHistory.last().label;
    current.revision = mRevision;
    mRedoHistory += current;

    const HistoryState previous = mUndoHistory.takeLast();
    restoreHistoryState(previous.ground, previous.vegetation);
    mRevision = previous.revision;
    trimHistoryToBudget();
    setDirty(mRevision != mSavedRevision);
    updateHistoryActions();
    updateImageStatus();
}

void TerrainImageEditorDialog::redoImageEdit()
{
    if (mRedoHistory.isEmpty())
        return;

    HistoryState current;
    current.ground = mCanvas->groundImage();
    current.vegetation = mCanvas->vegetationImage();
    current.label = mRedoHistory.last().label;
    current.revision = mRevision;
    mUndoHistory += current;
    const HistoryState next = mRedoHistory.takeLast();
    restoreHistoryState(next.ground, next.vegetation);
    mRevision = next.revision;
    trimHistoryToBudget();
    setDirty(mRevision != mSavedRevision);
    updateHistoryActions();
    updateImageStatus();
}

void TerrainImageEditorDialog::updateHistoryActions()
{
    mUndoButton->setEnabled(!mUndoHistory.isEmpty());
    mRedoButton->setEnabled(!mRedoHistory.isEmpty());
    mUndoButton->setToolTip(mUndoHistory.isEmpty()
                            ? tr("Nothing to undo")
                            : tr("Undo %1").arg(mUndoHistory.last().label));
    mRedoButton->setToolTip(mRedoHistory.isEmpty()
                            ? tr("Nothing to redo")
                            : tr("Redo %1").arg(mRedoHistory.last().label));
}

void TerrainImageEditorDialog::generateTerrainPatches()
{
    if (mCanvas->groundImage().isNull())
        return;
    QImage ground = mCanvas->groundImage();
    const QImage vegetation = mCanvas->vegetationImage();
    QRandomGenerator random(uint(mSeed->value()));
    QPainter painter(&ground);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mLayerColors[0]);
    const int radius = qMax(1, mFeatureWidth->value());
    const qint64 area = qint64(ground.width()) * ground.height();
    const qint64 spacing = qMax(
                qint64(1), qint64(radius) * radius * 18);
    const qint64 requestedCount =
            area / spacing * mDensity->value() / 100;
    const int count = int(qBound(qint64(1), requestedCount,
                                 qint64(12000)));
    for (int i = 0; i < count; ++i) {
        const int r = radius + int(random.bounded(uint(radius * 3 + 1)));
        const QPoint center(int(random.bounded(uint(ground.width()))),
                            int(random.bounded(uint(ground.height()))));
        painter.drawEllipse(center, r, qMax(1, r * 2 / 3));
    }
    painter.end();
    applyGeneratedImages(ground, vegetation, tr("Generate terrain patches"));
}

void TerrainImageEditorDialog::generateVegetation()
{
    if (mCanvas->vegetationImage().isNull())
        return;
    const QImage ground = mCanvas->groundImage();
    QImage vegetation = mCanvas->vegetationImage();
    QRandomGenerator random(uint(mSeed->value()));
    QPainter painter(&vegetation);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mLayerColors[1]);
    const int radius = qMax(1, mFeatureWidth->value());
    const qint64 area = qint64(vegetation.width()) * vegetation.height();
    const qint64 spacing = qMax(
                qint64(1), qint64(radius) * radius * 14);
    const qint64 requestedCount =
            area / spacing * mDensity->value() / 100;
    const int count = int(qBound(qint64(1), requestedCount,
                                 qint64(16000)));
    for (int i = 0; i < count; ++i) {
        const int r = radius + int(random.bounded(uint(radius * 2 + 1)));
        const QPoint center(int(random.bounded(uint(vegetation.width()))),
                            int(random.bounded(uint(vegetation.height()))));
        painter.drawEllipse(center, r, r);
    }
    painter.end();
    applyGeneratedImages(ground, vegetation, tr("Generate vegetation cover"));
}

void TerrainImageEditorDialog::generateRiver()
{
    if (mCanvas->groundImage().isNull())
        return;
    QImage ground = mCanvas->groundImage();
    QImage vegetation = mCanvas->vegetationImage();
    QRandomGenerator random(uint(mSeed->value()));
    const bool horizontal = random.bounded(2) == 0;
    const int width = qMax(1, mFeatureWidth->value());
    const int major = horizontal ? ground.width() : ground.height();
    const int minor = horizontal ? ground.height() : ground.width();
    const int segments = qMax(3, major / qMax(1, mCanvas->cellSize()));
    QVector<QPointF> points;
    for (int i = 0; i <= segments; ++i) {
        const qreal along = major * i / qreal(segments);
        const qreal base = minor * (0.2 + random.generateDouble() * 0.6);
        points += horizontal ? QPointF(along, base) : QPointF(base, along);
    }
    QPainterPath path(points.first());
    for (int i = 1; i < points.size(); ++i) {
        const QPointF previous = points[i - 1];
        const QPointF current = points[i];
        if (horizontal) {
            const qreal mid = (previous.x() + current.x()) / 2.0;
            path.cubicTo(QPointF(mid, previous.y()),
                         QPointF(mid, current.y()), current);
        } else {
            const qreal mid = (previous.y() + current.y()) / 2.0;
            path.cubicTo(QPointF(previous.x(), mid),
                         QPointF(current.x(), mid), current);
        }
    }

    const QColor water = featureColor(
                0, {QStringLiteral("water")}, QColor(0, 138, 255));
    QPainter groundPainter(&ground);
    groundPainter.setRenderHint(QPainter::Antialiasing, false);
    groundPainter.setPen(QPen(water, width, Qt::SolidLine,
                              Qt::RoundCap, Qt::RoundJoin));
    groundPainter.drawPath(path);
    groundPainter.end();
    QPainter vegetationPainter(&vegetation);
    vegetationPainter.setRenderHint(QPainter::Antialiasing, false);
    vegetationPainter.setPen(QPen(Qt::black, width + 2, Qt::SolidLine,
                                  Qt::RoundCap, Qt::RoundJoin));
    vegetationPainter.drawPath(path);
    vegetationPainter.end();
    applyGeneratedImages(ground, vegetation, tr("Generate river"));
}

void TerrainImageEditorDialog::generateLake()
{
    if (mCanvas->groundImage().isNull())
        return;
    QImage ground = mCanvas->groundImage();
    QImage vegetation = mCanvas->vegetationImage();
    QRandomGenerator random(uint(mSeed->value()));
    const QColor water = featureColor(
                0, {QStringLiteral("water")}, QColor(0, 138, 255));
    const int radius = qMax(2, mFeatureWidth->value() * 3);
    const QPoint center(
                ground.width() / 5 +
                int(random.bounded(uint(qMax(1, ground.width() * 3 / 5)))),
                ground.height() / 5 +
                int(random.bounded(uint(qMax(1, ground.height() * 3 / 5)))));

    QPainter groundPainter(&ground);
    QPainter vegetationPainter(&vegetation);
    groundPainter.setRenderHint(QPainter::Antialiasing, false);
    vegetationPainter.setRenderHint(QPainter::Antialiasing, false);
    groundPainter.setPen(Qt::NoPen);
    vegetationPainter.setPen(Qt::NoPen);
    groundPainter.setBrush(water);
    vegetationPainter.setBrush(Qt::black);
    const int lobes = qMax(3, mFeatureCount->value() + 2);
    for (int i = 0; i < lobes; ++i) {
        const int rx = radius + int(random.bounded(uint(radius * 2 + 1)));
        const int ry = radius + int(random.bounded(uint(radius * 2 + 1)));
        const QPoint offset(
                    int(random.bounded(uint(radius * 2 + 1))) - radius,
                    int(random.bounded(uint(radius * 2 + 1))) - radius);
        const QRect ellipse(center.x() + offset.x() - rx,
                            center.y() + offset.y() - ry,
                            rx * 2, ry * 2);
        groundPainter.drawEllipse(ellipse);
        vegetationPainter.drawEllipse(ellipse.adjusted(-1, -1, 1, 1));
    }
    groundPainter.end();
    vegetationPainter.end();
    applyGeneratedImages(ground, vegetation, tr("Generate lake"));
}

void TerrainImageEditorDialog::generateRoads()
{
    if (mCanvas->groundImage().isNull())
        return;
    QImage ground = mCanvas->groundImage();
    QImage vegetation = mCanvas->vegetationImage();
    QRandomGenerator random(uint(mSeed->value()));
    const QColor asphalt = featureColor(
                0, {QStringLiteral("dark asphalt"),
                    QStringLiteral("main roads"),
                    QStringLiteral("asphalt")},
                QColor(100, 100, 100));
    const int roadWidth = qMax(1, mFeatureWidth->value());

    QPainter groundPainter(&ground);
    QPainter vegetationPainter(&vegetation);
    groundPainter.setRenderHint(QPainter::Antialiasing, false);
    vegetationPainter.setRenderHint(QPainter::Antialiasing, false);
    groundPainter.setPen(QPen(asphalt, roadWidth, Qt::SolidLine,
                              Qt::RoundCap, Qt::RoundJoin));
    vegetationPainter.setPen(QPen(Qt::black, roadWidth + 2,
                                  Qt::SolidLine, Qt::RoundCap,
                                  Qt::RoundJoin));
    for (int i = 0; i < mFeatureCount->value(); ++i) {
        const bool horizontal = (i % 2) == 0;
        QPointF start;
        QPointF end;
        if (horizontal) {
            start = QPointF(0, random.bounded(uint(ground.height())));
            end = QPointF(ground.width() - 1,
                          random.bounded(uint(ground.height())));
        } else {
            start = QPointF(random.bounded(uint(ground.width())), 0);
            end = QPointF(random.bounded(uint(ground.width())),
                          ground.height() - 1);
        }
        const QPointF middle((start.x() + end.x()) / 2.0 +
                             (random.generateDouble() - 0.5) *
                             mCanvas->cellSize(),
                             (start.y() + end.y()) / 2.0 +
                             (random.generateDouble() - 0.5) *
                             mCanvas->cellSize());
        QPainterPath path(start);
        path.quadTo(middle, end);
        groundPainter.drawPath(path);
        vegetationPainter.drawPath(path);
    }
    groundPainter.end();
    vegetationPainter.end();
    applyGeneratedImages(ground, vegetation, tr("Generate road network"));
}

void TerrainImageEditorDialog::updatePointerStatus(const QPoint &point)
{
    if (point.x() < 0 || point.y() < 0) {
        mPointerStatus->clear();
        return;
    }
    const int cellSize = mCanvas->cellSize();
    mPointerStatus->setText(
                tr("Pixel %1,%2   Cell %3,%4")
                .arg(point.x()).arg(point.y())
                .arg(point.x() / cellSize).arg(point.y() / cellSize));
}

QString TerrainImageEditorDialog::defaultRulesPath() const
{
    if (mWorldDocument && mWorldDocument->world()) {
        const QString configured =
                mWorldDocument->world()->getBMPToTMXSettings().rulesFile;
        if (!configured.isEmpty() && QFileInfo::exists(configured))
            return QDir::cleanPath(configured);
    }
    const QString settingsPath = PortableSettings::path(
                QLatin1String("Rules.txt"));
    if (QFileInfo::exists(settingsPath))
        return settingsPath;
    const QString configPath =
            QDir(PortableSettings::applicationConfigPath())
            .filePath(QLatin1String("Rules.txt"));
    if (QFileInfo::exists(configPath))
        return configPath;
    const QString installPath = QDir(PortableSettings::installRootPath())
            .filePath(QLatin1String("Rules.txt"));
    if (QFileInfo::exists(installPath))
        return installPath;
    return QDir(QCoreApplication::applicationDirPath())
            .filePath(QLatin1String("Rules.txt"));
}

QString TerrainImageEditorDialog::defaultGroundPath() const
{
    if (mWorldDocument && mWorldDocument->world() &&
            !mWorldDocument->world()->bmps().isEmpty()) {
        return normalizedGroundPath(
                    mWorldDocument->world()->bmps().first()->filePath());
    }
    QString root = PortableSettings::installRootPath();
    if (mWorldDocument && !mWorldDocument->fileName().isEmpty())
        root = QFileInfo(mWorldDocument->fileName()).absolutePath();
    return QDir(root).filePath(
                QLatin1String("map/Map.png"));
}

QString TerrainImageEditorDialog::normalizedGroundPath(
        const QString &path) const
{
    QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty())
        return QString();
    QFileInfo info(normalized);
    QString baseName = info.completeBaseName();
    if (baseName.endsWith(QLatin1String("_veg"), Qt::CaseInsensitive))
        baseName.chop(4);
    return QDir::cleanPath(info.dir().filePath(
                               baseName + QLatin1String(".png")));
}

QString TerrainImageEditorDialog::vegetationPathFor(
        const QString &groundPath) const
{
    const QFileInfo info(groundPath);
    return info.dir().filePath(
                info.completeBaseName() + QLatin1String("_veg.png"));
}

bool TerrainImageEditorDialog::loadRulesFile(const QString &path)
{
    Tiled::Internal::BmpRulesFile file;
    if (path.isEmpty() || !file.read(path)) {
        if (!file.errorString().isEmpty())
            QMessageBox::warning(this, tr("Rules.txt"),
                                 file.errorString());
        return false;
    }

    mEntries.clear();
    QSet<QString> seen;
    for (const BmpRule *rule : file.rules()) {
        if (!rule || rule->obsolete ||
                (rule->bitmapIndex != 0 && rule->bitmapIndex != 1))
            continue;
        const QColor color = QColor::fromRgb(rule->color);
        const QString key = QStringLiteral("%1:%2")
                .arg(rule->bitmapIndex).arg(color.rgb());
        if (seen.contains(key))
            continue;
        seen.insert(key);
        TerrainPaletteEntry entry;
        entry.bitmapIndex = rule->bitmapIndex;
        entry.color = color;
        entry.label = rule->label.trimmed();
        if (entry.label.isEmpty() && !rule->tileChoices.isEmpty())
            entry.label = rule->tileChoices.first();
        if (entry.label.isEmpty())
            entry.label = tr("Unnamed rule");
        mEntries += entry;
    }

    if (mEntries.isEmpty())
        return false;
    mRulesPath->setText(QDir::toNativeSeparators(path));
    if (!mLayerColors[0].isValid())
        mLayerColors[0] = featureColor(
                    0, {QStringLiteral("darkgrass"),
                        QStringLiteral("dark grass")},
                    QColor(90, 100, 35));
    if (!mLayerColors[1].isValid())
        mLayerColors[1] = featureColor(
                    1, {QStringLiteral("grass")},
                    QColor(0, 255, 0));
    populatePalette(mActiveLayer);
    return true;
}

bool TerrainImageEditorDialog::loadImagePair(const QString &groundPath)
{
    QImageReader groundReader(groundPath);
    const QSize encodedGroundSize = groundReader.size();
    if (encodedGroundSize.isValid() &&
            !validateWorkingImageSize(encodedGroundSize,
                                      tr("Open terrain images"))) {
        return false;
    }

    const QImage ground = groundReader.read();
    if (ground.isNull()) {
        QMessageBox::warning(this, tr("Open Images"),
                             tr("The ground image could not be decoded.\n\n"
                                "File: %1\n"
                                "Reason: %2\n\n"
                                "Select a valid PNG image and try again.")
                             .arg(QDir::toNativeSeparators(groundPath),
                                  groundReader.errorString()));
        return false;
    }
    if (!encodedGroundSize.isValid() &&
            !validateWorkingImageSize(ground.size(),
                                      tr("Open terrain images")))
        return false;

    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    const int cellSize = world ? world->cellSize() : 300;
    if ((ground.width() % cellSize) != 0 ||
            (ground.height() % cellSize) != 0) {
        QMessageBox::warning(
                    this, tr("Invalid Image Size"),
                    tr("%1 is %2 x %3 pixels.\n\n"
                       "Both dimensions must be exact multiples of the "
                       "project cell size (%4 x %4 pixels).")
                    .arg(QDir::toNativeSeparators(groundPath))
                    .arg(ground.width()).arg(ground.height()).arg(cellSize));
        return false;
    }

    const QString vegetationPath = vegetationPathFor(groundPath);
    QImage vegetation;
    if (!QFileInfo::exists(vegetationPath)) {
        vegetation = QImage(ground.size(), QImage::Format_ARGB32);
        if (vegetation.isNull()) {
            QMessageBox::critical(
                        this, tr("Not Enough Memory"),
                        tr("WorldEd could not create the missing vegetation "
                           "image for %1. Reduce the image size and try again.")
                        .arg(QDir::toNativeSeparators(groundPath)));
            return false;
        }
        vegetation.fill(Qt::black);
    } else {
        QImageReader vegetationReader(vegetationPath);
        const QSize encodedVegetationSize = vegetationReader.size();
        if (encodedVegetationSize.isValid() &&
                encodedVegetationSize != ground.size()) {
            QMessageBox::warning(
                        this, tr("Invalid Vegetation Image"),
                        tr("%1 is %2 x %3 pixels, but the ground image is "
                           "%4 x %5 pixels.\n\n"
                           "Ground and vegetation images must have identical "
                           "dimensions.")
                        .arg(QDir::toNativeSeparators(vegetationPath))
                        .arg(encodedVegetationSize.width())
                        .arg(encodedVegetationSize.height())
                        .arg(ground.width()).arg(ground.height()));
            return false;
        }
        vegetation = vegetationReader.read();
        if (vegetation.isNull()) {
            QMessageBox::warning(
                        this, tr("Open Images"),
                        tr("The vegetation image could not be decoded.\n\n"
                           "File: %1\n"
                           "Reason: %2\n\n"
                           "Repair or replace this PNG, or remove it to let "
                           "WorldEd create an empty vegetation image.")
                        .arg(QDir::toNativeSeparators(vegetationPath),
                             vegetationReader.errorString()));
            return false;
        }
    }
    if (vegetation.size() != ground.size()) {
        QMessageBox::warning(
                    this, tr("Invalid Vegetation Image"),
                    tr("%1 is %2 x %3 pixels, but the ground image is "
                       "%4 x %5 pixels.\n\n"
                       "Ground and vegetation images must have identical "
                       "dimensions.")
                    .arg(QDir::toNativeSeparators(vegetationPath))
                    .arg(vegetation.width()).arg(vegetation.height())
                    .arg(ground.width()).arg(ground.height()));
        return false;
    }

    mCanvas->setImages(ground, vegetation, cellSize);
    mGroundPath->setText(QDir::toNativeSeparators(groundPath));
    mCellsWide->setValue(ground.width() / cellSize);
    mCellsHigh->setValue(ground.height() / cellSize);
    if (world) {
        const QString absoluteGround =
                QFileInfo(groundPath).absoluteFilePath();
        for (WorldBMP *bmp : world->bmps()) {
            if (QFileInfo(bmp->filePath()).absoluteFilePath().compare(
                        absoluteGround, Qt::CaseInsensitive) == 0) {
                mOriginX->setValue(bmp->x());
                mOriginY->setValue(bmp->y());
                break;
            }
        }
    }
    resetHistory(false);
    updateImageStatus();
    return true;
}

bool TerrainImageEditorDialog::validateWorkingImageSize(
        const QSize &size, const QString &operation)
{
    if (!size.isValid() || size.isEmpty()) {
        QMessageBox::warning(
                    this, tr("Invalid Image Size"),
                    tr("%1 cannot continue because the requested image size "
                       "is invalid: %2 x %3 pixels.")
                    .arg(operation).arg(size.width()).arg(size.height()));
        return false;
    }

    const qint64 bytes = workingImageBytes(size);
    const qint64 maxWorkingImageBytes =
            qint64(Preferences::instance()->terrainImageMemoryLimitMiB())
            * 1024 * 1024;
    const qint64 warnWorkingImageBytes = maxWorkingImageBytes / 2;
    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    const int cellSize = world ? world->cellSize() : 300;
    const int approximateSquareCells = qMax(
                1, int(std::sqrt(double(maxWorkingImageBytes) / 12.0) /
                       double(cellSize)));
    if (bytes > maxWorkingImageBytes) {
        QMessageBox::critical(
                    this, tr("Terrain Images Are Too Large"),
                    tr("%1 would require at least %2 while editing %3 x %4 "
                       "pixels (%5 x %6 cells).\n\n"
                       "The configured editor limit is %7. Increase "
                       "\"Terrain image memory limit\" in WorldEd Preferences "
                       "if this computer has enough RAM, or split the map into "
                       "smaller images. At the current limit, a square image "
                       "should be about %8 x %8 cells or less.")
                    .arg(operation, memorySizeText(bytes))
                    .arg(size.width()).arg(size.height())
                    .arg(size.width() / cellSize)
                    .arg(size.height() / cellSize)
                    .arg(memorySizeText(maxWorkingImageBytes))
                    .arg(approximateSquareCells));
        return false;
    }

    if (bytes > warnWorkingImageBytes) {
        return QMessageBox::warning(
                    this, tr("Large Terrain Images"),
                    tr("%1 will use approximately %2 for the two images and "
                       "their preview. Undo history can use additional "
                       "memory.\n\nContinue?")
                    .arg(operation, memorySizeText(bytes)),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel) == QMessageBox::Yes;
    }
    return true;
}

bool TerrainImageEditorDialog::maybeDiscardChanges()
{
    if (!mDirty)
        return true;
    const QMessageBox::StandardButton answer =
            QMessageBox::question(
                this, tr("Unsaved Terrain Images"),
                tr("Save changes to the ground and vegetation PNG files?"),
                QMessageBox::Save | QMessageBox::Discard |
                QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveImages();
    return true;
}

bool TerrainImageEditorDialog::savePngAtomically(
        const QImage &image, const QString &path, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, "png");
    writer.setCompression(6);
    if (!writer.write(image)) {
        if (error)
            *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool TerrainImageEditorDialog::validateAttachment(QString *error) const
{
    World *world = mWorldDocument ? mWorldDocument->world() : nullptr;
    if (!world) {
        if (error)
            *error = tr("No WorldEd project is available.");
        return false;
    }
    if (mCanvas->groundImage().isNull()) {
        if (error)
            *error = tr("No terrain image is loaded.");
        return false;
    }

    const int width = mCanvas->groundImage().width() /
            mCanvas->cellSize();
    const int height = mCanvas->groundImage().height() /
            mCanvas->cellSize();
    const int x = mOriginX->value();
    const int y = mOriginY->value();
    if (x < 0 || y < 0 || x + width > world->width() ||
            y + height > world->height()) {
        if (error) {
            *error = tr("The image occupies cells %1,%2 through %3,%4, "
                       "outside the project bounds 0,0 through %5,%6.")
                    .arg(x).arg(y).arg(x + width - 1).arg(y + height - 1)
                    .arg(world->width() - 1).arg(world->height() - 1);
        }
        return false;
    }
    return true;
}

bool TerrainImageEditorDialog::attachImagesToWorld(
        const QString &groundPath, QString *error)
{
    if (!validateAttachment(error))
        return false;

    World *world = mWorldDocument->world();
    const QString absolutePath =
            QDir::cleanPath(QFileInfo(groundPath).absoluteFilePath());
    WorldBMP *existing = nullptr;
    int existingIndex = -1;
    for (int index = 0; index < world->bmps().size(); ++index) {
        WorldBMP *candidate = world->bmps().at(index);
        const QString candidatePath = QDir::cleanPath(
                    QFileInfo(candidate->filePath()).absoluteFilePath());
        if (candidatePath.compare(absolutePath,
                                  Qt::CaseInsensitive) == 0) {
            existing = candidate;
            existingIndex = index;
            break;
        }
    }

    const int width = mCanvas->groundImage().width() /
            mCanvas->cellSize();
    const int height = mCanvas->groundImage().height() /
            mCanvas->cellSize();
    const QPoint origin(mOriginX->value(), mOriginY->value());
    const QString existingAbsolutePath = existing
            ? QDir::cleanPath(
                  QFileInfo(existing->filePath()).absoluteFilePath())
            : QString();
    const bool bmpChanged = !existing || existing->pos() != origin ||
            existing->size() != QSize(width, height) ||
            existingAbsolutePath.compare(
                absolutePath, Qt::CaseInsensitive) != 0;

    BMPToTMXSettings settings = world->getBMPToTMXSettings();
    const QString rawRulesPath = QDir::fromNativeSeparators(
                mRulesPath->text().trimmed());
    const QString rulesPath = rawRulesPath.isEmpty()
            ? QString()
            : QDir::cleanPath(QFileInfo(rawRulesPath).absoluteFilePath());
    const bool settingsChanged =
            !rulesPath.isEmpty() &&
            QDir::cleanPath(settings.rulesFile).compare(
                rulesPath, Qt::CaseInsensitive) != 0;

    if (!bmpChanged && !settingsChanged) {
        mWorldDocument->setSelectedBMPs({existing});
        return true;
    }

    QUndoStack *undoStack = mWorldDocument->undoStack();
    undoStack->beginMacro(tr("Attach Terrain Image"));
    WorldBMP *attached = existing;
    if (bmpChanged) {
        if (existing)
            mWorldDocument->removeBMP(existing);
        attached = new WorldBMP(world, origin.x(), origin.y(),
                                width, height, absolutePath);
        const int insertIndex = existingIndex >= 0
                ? existingIndex : world->bmps().size();
        mWorldDocument->insertBMP(insertIndex, attached);
    }
    if (settingsChanged) {
        settings.rulesFile = rulesPath;
        mWorldDocument->changeBMPToTMXSettings(settings);
    }
    undoStack->endMacro();
    if (attached)
        mWorldDocument->setSelectedBMPs({attached});
    return true;
}

void TerrainImageEditorDialog::beginImageEdit(const QString &label)
{
    if (mHistoryEditActive || mCanvas->groundImage().isNull())
        return;
    mPendingHistory.ground = mCanvas->groundImage();
    mPendingHistory.vegetation = mCanvas->vegetationImage();
    mPendingHistory.label = label;
    mPendingHistory.revision = mRevision;
    mHistoryEditActive = true;
}

void TerrainImageEditorDialog::endImageEdit()
{
    if (!mHistoryEditActive)
        return;
    mHistoryEditActive = false;
    if (mPendingHistory.ground == mCanvas->groundImage() &&
            mPendingHistory.vegetation == mCanvas->vegetationImage()) {
        mPendingHistory = HistoryState();
        return;
    }

    mUndoHistory += mPendingHistory;
    mRedoHistory.clear();
    trimHistoryToBudget();
    mPendingHistory = HistoryState();
    mRevision = mNextRevision++;
    setDirty(mRevision != mSavedRevision);
    updateHistoryActions();
    updateImageStatus();
}

void TerrainImageEditorDialog::trimHistoryToBudget()
{
    qint64 bytes = 0;
    for (const HistoryState &state : mUndoHistory)
        bytes += imageBytes(state.ground) + imageBytes(state.vegetation);
    for (const HistoryState &state : mRedoHistory)
        bytes += imageBytes(state.ground) + imageBytes(state.vegetation);

    const qint64 budget = historyBudgetBytes();
    while (!mUndoHistory.isEmpty()
           && (mUndoHistory.size() > 8 || bytes > budget)) {
        const HistoryState &oldest = mUndoHistory.first();
        bytes -= imageBytes(oldest.ground) +
                imageBytes(oldest.vegetation);
        mUndoHistory.removeFirst();
    }
    while (!mRedoHistory.isEmpty()
           && (mRedoHistory.size() > 8 || bytes > budget)) {
        const HistoryState &oldest = mRedoHistory.first();
        bytes -= imageBytes(oldest.ground) +
                imageBytes(oldest.vegetation);
        mRedoHistory.removeFirst();
    }
}

void TerrainImageEditorDialog::resetHistory(bool dirty)
{
    mUndoHistory.clear();
    mRedoHistory.clear();
    mPendingHistory = HistoryState();
    mHistoryEditActive = false;
    mNextRevision = 1;
    mRevision = dirty ? mNextRevision++ : 0;
    mSavedRevision = 0;
    setDirty(dirty);
    updateHistoryActions();
}

void TerrainImageEditorDialog::restoreHistoryState(
        const QImage &ground, const QImage &vegetation)
{
    mCanvas->setImages(ground, vegetation, mCanvas->cellSize());
}

void TerrainImageEditorDialog::populatePalette(int bitmapIndex)
{
    const QColor selected = mLayerColors[bitmapIndex];
    mPalette->blockSignals(true);
    mPalette->clear();
    int selectedIndex = -1;
    for (const TerrainPaletteEntry &entry : mEntries) {
        if (entry.bitmapIndex != bitmapIndex)
            continue;
        QPixmap swatch(18, 18);
        swatch.fill(entry.color);
        const QString text = QStringLiteral("%1  [%2, %3, %4]")
                .arg(entry.label)
                .arg(entry.color.red()).arg(entry.color.green())
                .arg(entry.color.blue());
        mPalette->addItem(QIcon(swatch), text, entry.color);
        if (entry.color.rgb() == selected.rgb())
            selectedIndex = mPalette->count() - 1;
    }
    if (selectedIndex < 0 && mPalette->count())
        selectedIndex = 0;
    mPalette->setCurrentIndex(selectedIndex);
    mPalette->blockSignals(false);
    paletteChanged(selectedIndex);
}

void TerrainImageEditorDialog::setDirty(bool dirty)
{
    mDirty = dirty;
    QString title = tr("Terrain and Vegetation Image Editor");
    if (mDirty)
        title += QLatin1String(" *");
    setWindowTitle(title);
}

void TerrainImageEditorDialog::updateImageStatus()
{
    if (mCanvas->groundImage().isNull()) {
        mImageStatus->setText(tr("No image"));
        return;
    }
    const QSize size = mCanvas->groundImage().size();
    mImageStatus->setText(
                tr("%1 x %2 px - %3 x %4 cells - %5 px/cell - about %6%7")
                .arg(size.width()).arg(size.height())
                .arg(size.width() / mCanvas->cellSize())
                .arg(size.height() / mCanvas->cellSize())
                .arg(mCanvas->cellSize())
                .arg(memorySizeText(workingImageBytes(size)))
                .arg(mDirty ? tr(" - modified") : QString()));
}

QColor TerrainImageEditorDialog::featureColor(
        int bitmapIndex, const QStringList &keywords,
        const QColor &fallback) const
{
    for (const QString &keyword : keywords) {
        for (const TerrainPaletteEntry &entry : mEntries) {
            if (entry.bitmapIndex == bitmapIndex &&
                    entry.label.contains(keyword, Qt::CaseInsensitive))
                return entry.color;
        }
    }
    for (const TerrainPaletteEntry &entry : mEntries) {
        if (entry.bitmapIndex == bitmapIndex)
            return entry.color;
    }
    return fallback;
}

void TerrainImageEditorDialog::applyGeneratedImages(
        const QImage &ground, const QImage &vegetation,
        const QString &label)
{
    beginImageEdit(label);
    mCanvas->replaceImages(ground, vegetation);
    endImageEdit();
}
