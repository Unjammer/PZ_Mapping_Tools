#include "worldmapannotationsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

QString decodeLuaString(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        if (value.at(i) != QLatin1Char('\\') || i + 1 >= value.size()) {
            result += value.at(i);
            continue;
        }
        const QChar next = value.at(++i);
        if (next == QLatin1Char('n'))
            result += QLatin1Char('\n');
        else if (next == QLatin1Char('r'))
            result += QLatin1Char('\r');
        else if (next == QLatin1Char('t'))
            result += QLatin1Char('\t');
        else
            result += next;
    }
    return result;
}

QString encodeLuaString(QString value)
{
    value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    value.replace(QLatin1Char('"'), QLatin1String("\\\""));
    value.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    value.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    value.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    return value;
}

double setterNumber(const QString &block, const QString &name,
                    double fallback)
{
    const QRegularExpression expression(
                QStringLiteral("symbol\\s*:\\s*%1\\s*\\(\\s*(-?[0-9]+(?:\\.[0-9]+)?)")
                .arg(QRegularExpression::escape(name)));
    const QRegularExpressionMatch match = expression.match(block);
    return match.hasMatch() ? match.captured(1).toDouble() : fallback;
}

bool setterBool(const QString &block, const QString &name, bool fallback)
{
    const QRegularExpression expression(
                QStringLiteral("symbol\\s*:\\s*%1\\s*\\(\\s*(true|false)")
                .arg(QRegularExpression::escape(name)));
    const QRegularExpressionMatch match = expression.match(block);
    return match.hasMatch() ? match.captured(1) == QLatin1String("true")
                            : fallback;
}

QDoubleSpinBox *numberBox(double minimum, double maximum, int decimals,
                          QWidget *parent)
{
    QDoubleSpinBox *box = new QDoubleSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setDecimals(decimals);
    box->setSingleStep(decimals == 3 ? 0.05 : 0.1);
    return box;
}

}

WorldMapAnnotationsDialog::WorldMapAnnotationsDialog(
        const QString &suggestedFile, QWidget *parent)
    : QDialog(parent)
    , mFileLabel(new QLabel(this))
    , mTable(new QTableWidget(this))
    , mTranslationMode(new QComboBox(this))
    , mText(new QLineEdit(this))
    , mStyle(new QLineEdit(this))
    , mX(new QSpinBox(this))
    , mY(new QSpinBox(this))
    , mRed(numberBox(0.0, 1.0, 3, this))
    , mGreen(numberBox(0.0, 1.0, 3, this))
    , mBlue(numberBox(0.0, 1.0, 3, this))
    , mAlpha(numberBox(0.0, 1.0, 3, this))
    , mScale(numberBox(0.001, 100.0, 3, this))
    , mAnchorX(numberBox(0.0, 1.0, 2, this))
    , mAnchorY(numberBox(0.0, 1.0, 2, this))
    , mRotation(numberBox(-3600.0, 3600.0, 1, this))
    , mMatchPerspective(new QCheckBox(tr("Match map perspective"), this))
    , mApplyZoom(new QCheckBox(tr("Apply map zoom"), this))
    , mMinZoom(numberBox(-100.0, 100.0, 2, this))
    , mMaxZoom(numberBox(-100.0, 100.0, 2, this))
    , mUserDefined(new QCheckBox(tr("User defined"), this))
    , mSaveButton(new QPushButton(tr("Save"), this))
{
    setWindowTitle(tr("World Map Annotations Editor"));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    resize(1180, 720);

    QPushButton *openButton = new QPushButton(tr("Open..."), this);
    QPushButton *saveAsButton = new QPushButton(tr("Save As..."), this);
    QHBoxLayout *fileLayout = new QHBoxLayout;
    fileLayout->addWidget(new QLabel(tr("File:"), this));
    fileLayout->addWidget(mFileLabel, 1);
    fileLayout->addWidget(openButton);
    fileLayout->addWidget(mSaveButton);
    fileLayout->addWidget(saveAsButton);

    mTable->setColumnCount(7);
    mTable->setHorizontalHeaderLabels(QStringList()
            << tr("Text") << tr("Style") << tr("X") << tr("Y")
            << tr("Scale") << tr("Rotation") << tr("Zoom"));
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mTable->verticalHeader()->setVisible(false);

    QPushButton *addButton = new QPushButton(tr("Add"), this);
    QPushButton *duplicateButton = new QPushButton(tr("Duplicate"), this);
    QPushButton *removeButton = new QPushButton(tr("Remove"), this);
    QHBoxLayout *listButtons = new QHBoxLayout;
    listButtons->addWidget(addButton);
    listButtons->addWidget(duplicateButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();

    QWidget *listWidget = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(mTable, 1);
    listLayout->addLayout(listButtons);

    mTranslationMode->addItem(tr("Untranslated text"), false);
    mTranslationMode->addItem(tr("Translated text"), true);
    mX->setRange(-1000000000, 1000000000);
    mY->setRange(-1000000000, 1000000000);

    QWidget *editorWidget = new QWidget(this);
    QFormLayout *form = new QFormLayout(editorWidget);
    form->addRow(tr("API mode:"), mTranslationMode);
    form->addRow(tr("Text or translation key:"), mText);
    form->addRow(tr("Style ID:"), mStyle);
    form->addRow(tr("World X:"), mX);
    form->addRow(tr("World Y:"), mY);
    form->addRow(tr("Red:"), mRed);
    form->addRow(tr("Green:"), mGreen);
    form->addRow(tr("Blue:"), mBlue);
    form->addRow(tr("Alpha:"), mAlpha);
    form->addRow(tr("Scale:"), mScale);
    form->addRow(tr("Anchor X:"), mAnchorX);
    form->addRow(tr("Anchor Y:"), mAnchorY);
    form->addRow(tr("Rotation:"), mRotation);
    form->addRow(QString(), mMatchPerspective);
    form->addRow(QString(), mApplyZoom);
    form->addRow(tr("Minimum zoom:"), mMinZoom);
    form->addRow(tr("Maximum zoom:"), mMaxZoom);
    form->addRow(QString(), mUserDefined);

    QScrollArea *editorScroll = new QScrollArea(this);
    editorScroll->setWidgetResizable(true);
    editorScroll->setFrameShape(QFrame::NoFrame);
    editorScroll->setWidget(editorWidget);

    QSplitter *splitter = new QSplitter(this);
    splitter->addWidget(listWidget);
    splitter->addWidget(editorScroll);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close,
                                                     this);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(fileLayout);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    connect(openButton, &QPushButton::clicked, this,
            [this]() { chooseFile(); });
    connect(mSaveButton, &QPushButton::clicked, this,
            [this]() { save(); });
    connect(saveAsButton, &QPushButton::clicked, this,
            [this]() { saveAs(); });
    connect(addButton, &QPushButton::clicked, this, [this]() {
        Annotation annotation;
        annotation.text = tr("New annotation");
        addAnnotation(annotation);
    });
    connect(duplicateButton, &QPushButton::clicked, this,
            [this]() { duplicateSelected(); });
    connect(removeButton, &QPushButton::clicked, this,
            [this]() { removeSelected(); });
    connect(buttons, &QDialogButtonBox::rejected,
            this, &WorldMapAnnotationsDialog::reject);
    connect(mTable, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { updateEditor(); });

    const auto update = [this]() { updateCurrent(); };
    connect(mTranslationMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [update](int) { update(); });
    connect(mText, &QLineEdit::textChanged, this,
            [update](const QString &) { update(); });
    connect(mStyle, &QLineEdit::textChanged, this,
            [update](const QString &) { update(); });
    connect(mX, qOverload<int>(&QSpinBox::valueChanged), this,
            [update](int) { update(); });
    connect(mY, qOverload<int>(&QSpinBox::valueChanged), this,
            [update](int) { update(); });
    for (QDoubleSpinBox *box : {mRed, mGreen, mBlue, mAlpha, mScale,
                               mAnchorX, mAnchorY, mRotation, mMinZoom,
                               mMaxZoom}) {
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [update](double) { update(); });
    }
    for (QCheckBox *box : {mMatchPerspective, mApplyZoom, mUserDefined}) {
        connect(box, &QCheckBox::toggled, this,
                [update](bool) { update(); });
    }

    if (!suggestedFile.isEmpty() && QFileInfo::exists(suggestedFile))
        loadFile(suggestedFile);
    else {
        mFileName = suggestedFile;
        mFileLabel->setText(QDir::toNativeSeparators(mFileName));
        rebuildTable();
    }
}

QString WorldMapAnnotationsDialog::fileName() const
{
    return mFileName;
}

void WorldMapAnnotationsDialog::reject()
{
    if (maybeSave())
        QDialog::reject();
}

void WorldMapAnnotationsDialog::addAnnotation(const Annotation &annotation)
{
    mAnnotations.append(annotation);
    rebuildTable();
    mTable->setCurrentCell(mAnnotations.size() - 1, 0);
    markDirty();
}

void WorldMapAnnotationsDialog::chooseFile()
{
    if (!maybeSave())
        return;
    const QString fileName = QFileDialog::getOpenFileName(
                this, tr("Open World Map Annotations"),
                mFileName.isEmpty() ? QString() : QFileInfo(mFileName).absolutePath(),
                tr("Lua files (*.lua);;All files (*)"));
    if (!fileName.isEmpty())
        loadFile(fileName);
}

void WorldMapAnnotationsDialog::duplicateSelected()
{
    const int row = mTable->currentRow();
    if (row < 0 || row >= mAnnotations.size())
        return;
    addAnnotation(mAnnotations.at(row));
}

bool WorldMapAnnotationsDialog::loadFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open Failed"), file.errorString());
        return false;
    }
    const QString source = QString::fromUtf8(file.readAll());
    const QRegularExpression callExpression(QStringLiteral(
        "symbol\\s*=\\s*symbolsAPI:add(Untranslated|Translated)Text\\s*\\(\\s*\"((?:\\\\.|[^\"\\\\])*)\"\\s*,\\s*\"((?:\\\\.|[^\"\\\\])*)\"\\s*,\\s*(-?[0-9]+)\\s*,\\s*(-?[0-9]+)\\s*\\)"));
    QVector<QRegularExpressionMatch> calls;
    QRegularExpressionMatchIterator iterator = callExpression.globalMatch(source);
    while (iterator.hasNext())
        calls.append(iterator.next());
    if (calls.isEmpty() && source.contains(QLatin1String("symbolsAPI:add"))) {
        QMessageBox::warning(this, tr("Unsupported Annotation File"),
                             tr("No supported text annotations were found. The file was not changed."));
        return false;
    }

    QVector<Annotation> annotations;
    for (int i = 0; i < calls.size(); ++i) {
        const QRegularExpressionMatch &call = calls.at(i);
        const int blockEnd = i + 1 < calls.size()
                ? calls.at(i + 1).capturedStart()
                : source.lastIndexOf(QLatin1String("\nend"));
        const QString block = source.mid(call.capturedEnd(),
                blockEnd < call.capturedEnd()
                ? source.size() - call.capturedEnd()
                : blockEnd - call.capturedEnd());
        Annotation annotation;
        annotation.translated = call.captured(1) == QLatin1String("Translated");
        annotation.text = decodeLuaString(call.captured(2));
        annotation.style = decodeLuaString(call.captured(3));
        annotation.x = call.captured(4).toInt();
        annotation.y = call.captured(5).toInt();

        const QRegularExpression rgbaExpression(QStringLiteral(
            "symbol\\s*:\\s*setRGBA\\s*\\(\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*,\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*,\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*,\\s*(-?[0-9]+(?:\\.[0-9]+)?)"));
        const QRegularExpressionMatch rgba = rgbaExpression.match(block);
        if (rgba.hasMatch()) {
            annotation.red = rgba.captured(1).toDouble();
            annotation.green = rgba.captured(2).toDouble();
            annotation.blue = rgba.captured(3).toDouble();
            annotation.alpha = rgba.captured(4).toDouble();
        }
        const QRegularExpression anchorExpression(QStringLiteral(
            "symbol\\s*:\\s*setAnchor\\s*\\(\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*,\\s*(-?[0-9]+(?:\\.[0-9]+)?)"));
        const QRegularExpressionMatch anchor = anchorExpression.match(block);
        if (anchor.hasMatch()) {
            annotation.anchorX = anchor.captured(1).toDouble();
            annotation.anchorY = anchor.captured(2).toDouble();
        }
        annotation.scale = setterNumber(block, QStringLiteral("setScale"), 1.0);
        annotation.rotation = setterNumber(block, QStringLiteral("setRotation"), 0.0);
        annotation.matchPerspective = setterBool(block, QStringLiteral("setMatchPerspective"), true);
        annotation.applyZoom = setterBool(block, QStringLiteral("setApplyZoom"), true);
        annotation.minZoom = setterNumber(block, QStringLiteral("setMinZoom"), 0.0);
        annotation.maxZoom = setterNumber(block, QStringLiteral("setMaxZoom"), 24.0);
        annotation.userDefined = setterBool(block, QStringLiteral("setUserDefined"), false);
        annotations.append(annotation);
    }

    mAnnotations = annotations;
    mFileName = QFileInfo(fileName).absoluteFilePath();
    mDirty = false;
    mSaveButton->setEnabled(false);
    mFileLabel->setText(QDir::toNativeSeparators(mFileName));
    rebuildTable();
    if (!mAnnotations.isEmpty())
        mTable->setCurrentCell(0, 0);
    return true;
}

void WorldMapAnnotationsDialog::markDirty()
{
    if (mUpdating)
        return;
    mDirty = true;
    mSaveButton->setEnabled(true);
    setWindowTitle(tr("World Map Annotations Editor [modified]"));
}

bool WorldMapAnnotationsDialog::maybeSave()
{
    if (!mDirty)
        return true;
    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Unsaved Annotations"),
                tr("Save changes to the world map annotations?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return save();
    return true;
}

void WorldMapAnnotationsDialog::rebuildTable()
{
    mUpdating = true;
    mTable->setRowCount(mAnnotations.size());
    for (int row = 0; row < mAnnotations.size(); ++row)
        updateRow(row);
    mUpdating = false;
    updateEditor();
}

void WorldMapAnnotationsDialog::removeSelected()
{
    const int row = mTable->currentRow();
    if (row < 0 || row >= mAnnotations.size())
        return;
    mAnnotations.removeAt(row);
    rebuildTable();
    if (!mAnnotations.isEmpty())
        mTable->setCurrentCell(qMin(row, mAnnotations.size() - 1), 0);
    markDirty();
}

bool WorldMapAnnotationsDialog::save()
{
    return mFileName.isEmpty() ? saveAs() : saveFile(mFileName);
}

bool WorldMapAnnotationsDialog::saveAs()
{
    const QString fileName = QFileDialog::getSaveFileName(
                this, tr("Save World Map Annotations"),
                mFileName.isEmpty() ? QStringLiteral("worldmap-annotations.lua")
                                    : mFileName,
                tr("Lua files (*.lua);;All files (*)"));
    return fileName.isEmpty() ? false : saveFile(fileName);
}

bool WorldMapAnnotationsDialog::saveFile(const QString &fileName)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Failed"), file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "return function(mapUI)\n";
    stream << "\tlocal mapAPI = mapUI.javaObject:getAPIv3()\n";
    stream << "\tlocal symbolsAPI = mapAPI:getSymbolsAPIv2()\n";
    stream << "\tlocal symbol\n";
    for (const Annotation &annotation : mAnnotations) {
        stream << "\tsymbol = symbolsAPI:add"
               << (annotation.translated ? "TranslatedText" : "UntranslatedText")
               << "(\"" << encodeLuaString(annotation.text) << "\", \""
               << encodeLuaString(annotation.style) << "\", "
               << annotation.x << ", " << annotation.y << ")\n";
        stream << QStringLiteral("\tsymbol:setRGBA(%1, %2, %3, %4)\n")
                  .arg(annotation.red, 0, 'f', 3)
                  .arg(annotation.green, 0, 'f', 3)
                  .arg(annotation.blue, 0, 'f', 3)
                  .arg(annotation.alpha, 0, 'f', 3);
        stream << QStringLiteral("\tsymbol:setScale(%1)\n")
                  .arg(annotation.scale, 0, 'f', 3);
        stream << QStringLiteral("\tsymbol:setAnchor(%1, %2)\n")
                  .arg(annotation.anchorX, 0, 'f', 2)
                  .arg(annotation.anchorY, 0, 'f', 2);
        stream << QStringLiteral("\tsymbol:setRotation(%1)\n")
                  .arg(annotation.rotation, 0, 'f', 1);
        stream << "\tsymbol:setMatchPerspective("
               << (annotation.matchPerspective ? "true" : "false") << ")\n";
        stream << "\tsymbol:setApplyZoom("
               << (annotation.applyZoom ? "true" : "false") << ")\n";
        stream << QStringLiteral("\tsymbol:setMinZoom(%1)\n")
                  .arg(annotation.minZoom, 0, 'f', 2);
        stream << QStringLiteral("\tsymbol:setMaxZoom(%1)\n")
                  .arg(annotation.maxZoom, 0, 'f', 2);
        stream << "\tsymbol:setUserDefined("
               << (annotation.userDefined ? "true" : "false") << ")\n\n";
    }
    stream << "end\n";
    stream.flush();
    if (!file.commit()) {
        QMessageBox::warning(this, tr("Save Failed"), file.errorString());
        return false;
    }
    mFileName = QFileInfo(fileName).absoluteFilePath();
    mFileLabel->setText(QDir::toNativeSeparators(mFileName));
    mDirty = false;
    mSaveButton->setEnabled(false);
    setWindowTitle(tr("World Map Annotations Editor"));
    return true;
}

void WorldMapAnnotationsDialog::updateCurrent()
{
    if (mUpdating)
        return;
    const int row = mTable->currentRow();
    if (row < 0 || row >= mAnnotations.size())
        return;
    Annotation &annotation = mAnnotations[row];
    annotation.translated = mTranslationMode->currentData().toBool();
    annotation.text = mText->text();
    annotation.style = mStyle->text();
    annotation.x = mX->value();
    annotation.y = mY->value();
    annotation.red = mRed->value();
    annotation.green = mGreen->value();
    annotation.blue = mBlue->value();
    annotation.alpha = mAlpha->value();
    annotation.scale = mScale->value();
    annotation.anchorX = mAnchorX->value();
    annotation.anchorY = mAnchorY->value();
    annotation.rotation = mRotation->value();
    annotation.matchPerspective = mMatchPerspective->isChecked();
    annotation.applyZoom = mApplyZoom->isChecked();
    annotation.minZoom = mMinZoom->value();
    annotation.maxZoom = mMaxZoom->value();
    annotation.userDefined = mUserDefined->isChecked();
    updateRow(row);
    markDirty();
}

void WorldMapAnnotationsDialog::updateEditor()
{
    const int row = mTable->currentRow();
    const bool enabled = row >= 0 && row < mAnnotations.size();
    const QList<QWidget *> editorWidgets = QList<QWidget *>()
            << mTranslationMode << mText << mStyle << mX << mY
            << mRed << mGreen << mBlue << mAlpha << mScale
            << mAnchorX << mAnchorY << mRotation << mMatchPerspective
            << mApplyZoom << mMinZoom << mMaxZoom << mUserDefined;
    for (QWidget *widget : editorWidgets) {
        widget->setEnabled(enabled);
    }
    if (!enabled)
        return;
    const Annotation &annotation = mAnnotations.at(row);
    mUpdating = true;
    mTranslationMode->setCurrentIndex(annotation.translated ? 1 : 0);
    mText->setText(annotation.text);
    mStyle->setText(annotation.style);
    mX->setValue(annotation.x);
    mY->setValue(annotation.y);
    mRed->setValue(annotation.red);
    mGreen->setValue(annotation.green);
    mBlue->setValue(annotation.blue);
    mAlpha->setValue(annotation.alpha);
    mScale->setValue(annotation.scale);
    mAnchorX->setValue(annotation.anchorX);
    mAnchorY->setValue(annotation.anchorY);
    mRotation->setValue(annotation.rotation);
    mMatchPerspective->setChecked(annotation.matchPerspective);
    mApplyZoom->setChecked(annotation.applyZoom);
    mMinZoom->setValue(annotation.minZoom);
    mMaxZoom->setValue(annotation.maxZoom);
    mUserDefined->setChecked(annotation.userDefined);
    mUpdating = false;
}

void WorldMapAnnotationsDialog::updateRow(int row)
{
    if (row < 0 || row >= mAnnotations.size())
        return;
    const Annotation &annotation = mAnnotations.at(row);
    const QStringList values = QStringList()
            << annotation.text
            << annotation.style
            << QString::number(annotation.x)
            << QString::number(annotation.y)
            << QString::number(annotation.scale, 'f', 3)
            << QString::number(annotation.rotation, 'f', 1)
            << QStringLiteral("%1 - %2")
               .arg(annotation.minZoom, 0, 'f', 2)
               .arg(annotation.maxZoom, 0, 'f', 2);
    for (int column = 0; column < values.size(); ++column) {
        QTableWidgetItem *item = mTable->item(row, column);
        if (!item) {
            item = new QTableWidgetItem;
            mTable->setItem(row, column, item);
        }
        item->setText(values.at(column));
    }
}
