/*
 * Project Zomboid WorldEd - streets.xml editor
 */

#include "streetnamesdock.h"

#include "basegraphicsscene.h"
#include "basegraphicsview.h"
#include "celldocument.h"
#include "cellscene.h"
#include "document.h"
#include "maprenderer.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"
#include "worldscene.h"
#include "zoomable.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTreeWidget>
#include <QTransform>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace {

const int StreetIndexRole = Qt::UserRole + 42;
const int StreetSortRole = Qt::UserRole + 43;

class StreetTreeWidgetItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator <(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (column == 1 || column == 2)
            return data(column, StreetSortRole).toInt() <
                    other.data(column, StreetSortRole).toInt();
        return QString::localeAwareCompare(text(column),
                                           other.text(column)) < 0;
    }
};

qreal pointSegmentDistanceSquared(const QPointF &point,
                                  const QPointF &a,
                                  const QPointF &b,
                                  QPointF *closest = nullptr)
{
    const QPointF ab = b - a;
    const qreal lengthSquared = QPointF::dotProduct(ab, ab);
    qreal t = 0.0;
    if (lengthSquared > 0.0)
        t = QPointF::dotProduct(point - a, ab) / lengthSquared;
    t = qBound<qreal>(0.0, t, 1.0);
    const QPointF candidate = a + ab * t;
    if (closest)
        *closest = candidate;
    const QPointF delta = point - candidate;
    return QPointF::dotProduct(delta, delta);
}

class StreetNamesSnapshotCommand : public QUndoCommand
{
public:
    StreetNamesSnapshotCommand(StreetNamesDock *dock,
                               const QVector<StreetNameRecord> &before,
                               int beforeSelection,
                               const QVector<StreetNameRecord> &after,
                               int afterSelection,
                               const QString &text)
        : QUndoCommand(text)
        , mDock(dock)
        , mBefore(before)
        , mAfter(after)
        , mBeforeSelection(beforeSelection)
        , mAfterSelection(afterSelection)
    {
    }

    void undo() override
    {
        if (mDock)
            mDock->applySnapshot(mBefore, mBeforeSelection);
    }

    void redo() override
    {
        if (mDock)
            mDock->applySnapshot(mAfter, mAfterSelection);
    }

private:
    QPointer<StreetNamesDock> mDock;
    QVector<StreetNameRecord> mBefore;
    QVector<StreetNameRecord> mAfter;
    int mBeforeSelection;
    int mAfterSelection;
};

} // namespace

StreetNamesDock::StreetNamesDock(QWidget *parent)
    : QDockWidget(parent)
    , mFileNameEdit(new QLineEdit(this))
    , mBrowseButton(new QPushButton(this))
    , mLoadButton(new QPushButton(this))
    , mSaveButton(new QPushButton(this))
    , mUndoButton(new QPushButton(this))
    , mRedoButton(new QPushButton(this))
    , mShowStreetsCheckBox(new QCheckBox(this))
    , mVisualWidthSpinBox(new QSpinBox(this))
    , mStreetFilterEdit(new QLineEdit(this))
    , mCreateButton(new QPushButton(this))
    , mEditButton(new QPushButton(this))
    , mRemoveButton(new QPushButton(this))
    , mReverseButton(new QPushButton(this))
    , mSplitButton(new QPushButton(this))
    , mRemovePointButton(new QPushButton(this))
    , mStreetList(new QTreeWidget(this))
    , mStreetNameEdit(new QLineEdit(this))
    , mWidthSpinBox(new QSpinBox(this))
    , mStatusLabel(new QLabel(this))
    , mUndoStack(new QUndoStack(this))
{
    setObjectName(QStringLiteral("StreetNamesDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    QWidget *contents = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(contents);

    QHBoxLayout *fileLayout = new QHBoxLayout;
    mFileNameEdit->setClearButtonEnabled(true);
    fileLayout->addWidget(mFileNameEdit, 1);
    fileLayout->addWidget(mBrowseButton);
    mainLayout->addLayout(fileLayout);

    QHBoxLayout *fileButtons = new QHBoxLayout;
    fileButtons->addWidget(mLoadButton);
    fileButtons->addWidget(mSaveButton);
    fileButtons->addStretch(1);
    fileButtons->addWidget(mUndoButton);
    fileButtons->addWidget(mRedoButton);
    mainLayout->addLayout(fileButtons);

    QHBoxLayout *displayLayout = new QHBoxLayout;
    mShowStreetsCheckBox->setChecked(
                QSettings().value(QStringLiteral("StreetNames/Visible"),
                                  true).toBool());
    mVisualWidthSpinBox->setRange(1, 16);
    mVisualWidthSpinBox->setValue(
                QSettings().value(QStringLiteral("StreetNames/LineWidth"),
                                  4).toInt());
    displayLayout->addWidget(mShowStreetsCheckBox);
    displayLayout->addStretch(1);
    displayLayout->addWidget(new QLabel(tr("Line thickness:"), this));
    displayLayout->addWidget(mVisualWidthSpinBox);
    mainLayout->addLayout(displayLayout);

    mStreetFilterEdit->setClearButtonEnabled(true);
    mainLayout->addWidget(mStreetFilterEdit);

    mStreetList->setColumnCount(3);
    mStreetList->setRootIsDecorated(false);
    mStreetList->setSelectionMode(QAbstractItemView::SingleSelection);
    mStreetList->header()->setStretchLastSection(false);
    mStreetList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mStreetList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mStreetList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mStreetList->setSortingEnabled(true);
    mStreetList->sortByColumn(0, Qt::AscendingOrder);
    mainLayout->addWidget(mStreetList, 1);

    QFormLayout *propertiesLayout = new QFormLayout;
    mWidthSpinBox->setRange(1, 100);
    propertiesLayout->addRow(tr("Name:"), mStreetNameEdit);
    propertiesLayout->addRow(tr("Width:"), mWidthSpinBox);
    mainLayout->addLayout(propertiesLayout);

    QHBoxLayout *streetButtons = new QHBoxLayout;
    streetButtons->addWidget(mCreateButton);
    streetButtons->addWidget(mEditButton);
    streetButtons->addWidget(mRemoveButton);
    streetButtons->addWidget(mReverseButton);
    streetButtons->addWidget(mSplitButton);
    streetButtons->addWidget(mRemovePointButton);
    streetButtons->addStretch(1);
    mainLayout->addLayout(streetButtons);

    mStatusLabel->setWordWrap(true);
    mainLayout->addWidget(mStatusLabel);

    setWidget(contents);
    retranslateUi();

    const QList<QPushButton *> compactButtons = {
        mCreateButton, mEditButton, mRemoveButton, mReverseButton,
        mSplitButton, mRemovePointButton
    };
    for (QPushButton *button : compactButtons) {
        button->setText(QString());
        button->setFixedSize(30, 28);
        button->setFlat(true);
    }
    mEditButton->setCheckable(true);
    mCreateButton->setIcon(style()->standardIcon(
                               QStyle::SP_FileDialogNewFolder));
    mEditButton->setIcon(style()->standardIcon(
                             QStyle::SP_FileDialogDetailedView));
    mRemoveButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    mReverseButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    mSplitButton->setIcon(style()->standardIcon(
                              QStyle::SP_TitleBarUnshadeButton));
    mRemovePointButton->setIcon(style()->standardIcon(
                                    QStyle::SP_DialogDiscardButton));

    connect(mBrowseButton, &QPushButton::clicked,
            this, &StreetNamesDock::browseForFile);
    connect(mLoadButton, &QPushButton::clicked,
            this, &StreetNamesDock::loadFile);
    connect(mSaveButton, &QPushButton::clicked,
            this, &StreetNamesDock::saveFile);
    connect(mCreateButton, &QPushButton::clicked,
            this, &StreetNamesDock::createStreet);
    connect(mEditButton, &QPushButton::clicked,
            this, &StreetNamesDock::editStreet);
    connect(mShowStreetsCheckBox, &QCheckBox::toggled,
            this, &StreetNamesDock::showStreetsChanged);
    connect(mVisualWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &StreetNamesDock::visualWidthChanged);
    connect(mRemoveButton, &QPushButton::clicked,
            this, &StreetNamesDock::removeStreet);
    connect(mReverseButton, &QPushButton::clicked,
            this, &StreetNamesDock::reverseStreet);
    connect(mSplitButton, &QPushButton::clicked,
            this, &StreetNamesDock::splitStreetAtSelectedPoint);
    connect(mRemovePointButton, &QPushButton::clicked,
            this, &StreetNamesDock::removeSelectedPoint);
    connect(mStreetList, &QTreeWidget::itemSelectionChanged,
            this, &StreetNamesDock::streetSelectionChanged);
    connect(mStreetFilterEdit, &QLineEdit::textChanged,
            this, &StreetNamesDock::streetFilterChanged);
    connect(mStreetNameEdit, &QLineEdit::editingFinished,
            this, &StreetNamesDock::streetNameEditingFinished);
    connect(mWidthSpinBox, &QAbstractSpinBox::editingFinished,
            this, &StreetNamesDock::streetWidthEditingFinished);
    connect(mUndoButton, &QPushButton::clicked, mUndoStack, &QUndoStack::undo);
    connect(mRedoButton, &QPushButton::clicked, mUndoStack, &QUndoStack::redo);
    connect(mUndoStack, &QUndoStack::canUndoChanged,
            this, &StreetNamesDock::updateUi);
    connect(mUndoStack, &QUndoStack::canRedoChanged,
            this, &StreetNamesDock::updateUi);
    connect(mUndoStack, &QUndoStack::cleanChanged,
            this, &StreetNamesDock::updateUi);

    updateUi();
}

StreetNamesDock::~StreetNamesDock()
{
    detachScene();
}

bool StreetNamesDock::validateStreetFile(
        const QString &fileName, int *streetCount, QString *error) const
{
    QVector<StreetNameRecord> parsed;
    if (!readFile(fileName, &parsed, error))
        return false;
    if (streetCount)
        *streetCount = parsed.size();
    return true;
}

void StreetNamesDock::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void StreetNamesDock::retranslateUi()
{
    setWindowTitle(tr("Street Names"));
    mBrowseButton->setText(tr("..."));
    mBrowseButton->setToolTip(tr("Choose a streets.xml file"));
    mLoadButton->setText(tr("Load"));
    mSaveButton->setText(tr("Save"));
    mUndoButton->setText(tr("Undo"));
    mRedoButton->setText(tr("Redo"));
    mShowStreetsCheckBox->setText(tr("Show streets"));
    mStreetFilterEdit->setPlaceholderText(tr("Find street by name..."));
    mStreetFilterEdit->setToolTip(
                tr("Filter street names (the column headers can be sorted)"));
    mCreateButton->setText(QString());
    mCreateButton->setToolTip(tr("Create street"));
    mCreateButton->setAccessibleName(tr("Create street"));
    mEditButton->setText(QString());
    mEditButton->setToolTip(tr("Edit street geometry"));
    mEditButton->setAccessibleName(tr("Edit street geometry"));
    mRemoveButton->setText(QString());
    mRemoveButton->setToolTip(tr("Remove street"));
    mRemoveButton->setAccessibleName(tr("Remove street"));
    mReverseButton->setText(QString());
    mReverseButton->setToolTip(tr("Reverse street"));
    mReverseButton->setAccessibleName(tr("Reverse street"));
    mSplitButton->setText(QString());
    mSplitButton->setToolTip(tr("Split street at selected point"));
    mSplitButton->setAccessibleName(tr("Split street"));
    mRemovePointButton->setText(QString());
    mRemovePointButton->setToolTip(tr("Remove selected point"));
    mRemovePointButton->setAccessibleName(tr("Remove selected point"));
    mStreetList->setHeaderLabels(
                QStringList() << tr("Street") << tr("Width") << tr("Points"));
}

void StreetNamesDock::setDocument(Document *document)
{
    WorldDocument *worldDocument = document
            ? document->asWorldDocument()
            : nullptr;
    if (!worldDocument && document && document->asCellDocument())
        worldDocument = document->asCellDocument()->worldDocument();

    BaseGraphicsScene *scene = document && document->view()
            ? document->view()->scene()
            : nullptr;

    if (scene != mScene)
        detachScene();

    if (worldDocument != mWorldDocument) {
        if (!maybeSaveCurrentFile())
            return;

        if (mWorldDocument)
            mWorldDocument->disconnect(this);
        mWorldDocument = worldDocument;
        mStreets.clear();
        mSelectedStreet = -1;
        mSelectedPoint = -1;
        mUndoStack->clear();

        if (mWorldDocument) {
            mFileNameEdit->setText(defaultFileName());
            if (QFileInfo::exists(mFileNameEdit->text()))
                loadFile();
            else {
                rebuildList();
                mUndoStack->setClean();
            }
            connect(mWorldDocument, &WorldDocument::generateLotSettingsChanged,
                    this, [this]() { rebuildGraphics(); });
        } else {
            mFileNameEdit->clear();
            rebuildList();
        }
    }

    mDocument = document;
    if (scene)
        attachScene(scene);
    else if (mScene)
        detachScene();

    updateUi();
}

void StreetNamesDock::clearDocument()
{
    setDocument(nullptr);
}

QString StreetNamesDock::defaultFileName() const
{
    if (!mWorldDocument)
        return QString();

    const QString exportDirectory =
            mWorldDocument->world()->getGenerateLotsSettings().exportDir;
    if (!exportDirectory.trimmed().isEmpty())
        return QDir(exportDirectory).absoluteFilePath(QStringLiteral("streets.xml"));

    return QDir(QFileInfo(mWorldDocument->fileName()).absolutePath())
            .absoluteFilePath(QStringLiteral("streets.xml"));
}

bool StreetNamesDock::maybeSaveCurrentFile()
{
    if (!mWorldDocument || mUndoStack->isClean())
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Unsaved Street Names"),
                tr("The current streets.xml has unsaved changes. Save them now?"),
                QMessageBox::Save | QMessageBox::Discard,
                QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        saveFile();
        return mUndoStack->isClean();
    }
    return true;
}

void StreetNamesDock::browseForFile()
{
    QString initial = mFileNameEdit->text();
    if (initial.isEmpty())
        initial = defaultFileName();

    const QString fileName = QFileDialog::getSaveFileName(
                this, tr("Select streets.xml"), initial,
                tr("Project Zomboid street names (streets.xml);;XML files (*.xml)"));
    if (!fileName.isEmpty())
        mFileNameEdit->setText(QDir::toNativeSeparators(fileName));
}

void StreetNamesDock::loadFile()
{
    if (!mWorldDocument)
        return;

    const QString fileName = mFileNameEdit->text().trimmed();
    if (fileName.isEmpty()) {
        browseForFile();
        if (mFileNameEdit->text().trimmed().isEmpty())
            return;
    }

    if (!mUndoStack->isClean()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
                    this, tr("Reload Street Names"),
                    tr("Discard unsaved street-name changes and reload the file?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QVector<StreetNameRecord> loaded;
    QString error;
    if (!readFile(mFileNameEdit->text().trimmed(), &loaded, &error)) {
        QMessageBox::critical(this, tr("Unable to Load streets.xml"), error);
        return;
    }

    mStreets = loaded;
    mSelectedStreet = mStreets.isEmpty() ? -1 : 0;
    mSelectedPoint = -1;
    mUndoStack->clear();
    mUndoStack->setClean();
    rebuildList();
    rebuildGraphics();
    updateUi();
}

void StreetNamesDock::saveFile()
{
    saveCurrentFile(true);
}

bool StreetNamesDock::saveForProject()
{
    if (!mWorldDocument)
        return true;

    QString fileName = mFileNameEdit->text().trimmed();
    if (fileName.isEmpty()) {
        fileName = defaultFileName();
        mFileNameEdit->setText(QDir::toNativeSeparators(fileName));
    }

    // Do not create an unrelated empty streets.xml for every project. Once
    // street data exists, was edited, or the file already exists, Ctrl+S owns
    // saving it together with the project.
    if (mStreets.isEmpty() && mUndoStack->isClean() &&
            !QFileInfo::exists(fileName)) {
        return true;
    }

    return saveCurrentFile(false);
}

bool StreetNamesDock::saveCurrentFile(bool chooseFileWhenMissing)
{
    if (!mWorldDocument)
        return true;

    if (mFileNameEdit->text().trimmed().isEmpty() && chooseFileWhenMissing)
        browseForFile();
    const QString fileName = mFileNameEdit->text().trimmed();
    if (fileName.isEmpty())
        return false;

    QString error;
    if (!validate(&error)) {
        QMessageBox::warning(this, tr("Invalid Street Names"), error);
        return false;
    }

    const QFileInfo info(fileName);
    if (!QDir().mkpath(info.absolutePath())) {
        QMessageBox::critical(
                    this, tr("Unable to Save streets.xml"),
                    tr("Could not create directory:\n%1")
                    .arg(QDir::toNativeSeparators(info.absolutePath())));
        return false;
    }

    if (!writeFile(fileName, &error)) {
        QMessageBox::critical(this, tr("Unable to Save streets.xml"), error);
        return false;
    }

    mFileNameEdit->setText(QDir::toNativeSeparators(info.absoluteFilePath()));
    mUndoStack->setClean();
    updateUi();
    return true;
}

bool StreetNamesDock::readFile(const QString &fileName,
                               QVector<StreetNameRecord> *streets,
                               QString *error) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = tr("Could not open:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QStringLiteral("streets")) {
        if (error)
            *error = tr("The file does not contain a <streets> root element.");
        return false;
    }
    if (xml.attributes().value(QStringLiteral("version")) != QStringLiteral("1")) {
        if (error)
            *error = tr("Missing or unsupported streets.xml version (expected 1).");
        return false;
    }

    QVector<StreetNameRecord> parsed;
    while (xml.readNextStartElement()) {
        if (xml.name() != QStringLiteral("street")) {
            xml.raiseError(tr("Unrecognised element <%1> inside <streets>.")
                           .arg(xml.name().toString()));
            break;
        }

        StreetNameRecord street;
        street.name = xml.attributes().value(QStringLiteral("name")).toString();
        bool widthOk = false;
        street.width = xml.attributes().value(QStringLiteral("width"))
                .toInt(&widthOk);
        if (!widthOk)
            street.width = 5;

        while (xml.readNextStartElement()) {
            if (xml.name() != QStringLiteral("points")) {
                xml.skipCurrentElement();
                continue;
            }

            while (xml.readNextStartElement()) {
                if (xml.name().compare(QStringLiteral("point"),
                                       Qt::CaseInsensitive) != 0) {
                    xml.skipCurrentElement();
                    continue;
                }
                bool xOk = false;
                bool yOk = false;
                const qreal x = xml.attributes().value(QStringLiteral("x"))
                        .toDouble(&xOk);
                const qreal y = xml.attributes().value(QStringLiteral("y"))
                        .toDouble(&yOk);
                if (!xOk || !yOk || !qIsFinite(x) || !qIsFinite(y)) {
                    xml.raiseError(tr("A street point has invalid coordinates."));
                    break;
                }
                street.points.append(QPointF(x, y));
                xml.skipCurrentElement();
            }
        }

        if (street.name.trimmed().isEmpty() || street.points.isEmpty()) {
            xml.raiseError(tr("A street has an empty name or points list."));
            break;
        }
        parsed.append(street);
    }

    if (xml.hasError()) {
        if (error)
            *error = tr("%1\n\nLine %2, column %3.")
                    .arg(xml.errorString())
                    .arg(xml.lineNumber())
                    .arg(xml.columnNumber());
        return false;
    }

    if (streets)
        *streets = parsed;
    return true;
}

bool StreetNamesDock::writeFile(const QString &fileName, QString *error) const
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = tr("Could not open:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("streets"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
    for (const StreetNameRecord &street : mStreets) {
        xml.writeStartElement(QStringLiteral("street"));
        xml.writeAttribute(QStringLiteral("name"), street.name.trimmed());
        xml.writeAttribute(QStringLiteral("width"), QString::number(street.width));
        xml.writeStartElement(QStringLiteral("points"));
        for (const QPointF &point : street.points) {
            xml.writeEmptyElement(QStringLiteral("point"));
            xml.writeAttribute(QStringLiteral("x"),
                               QLocale::c().toString(point.x(), 'f', 1));
            xml.writeAttribute(QStringLiteral("y"),
                               QLocale::c().toString(point.y(), 'f', 1));
        }
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();

    if (!file.commit()) {
        if (error)
            *error = tr("Could not replace:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }
    return true;
}

bool StreetNamesDock::validate(QString *error) const
{
    for (int i = 0; i < mStreets.size(); ++i) {
        const StreetNameRecord &street = mStreets.at(i);
        if (street.name.trimmed().isEmpty()) {
            if (error)
                *error = tr("Street %1 has an empty name.").arg(i + 1);
            return false;
        }
        if (street.width < 1) {
            if (error)
                *error = tr("Street \"%1\" has an invalid width.")
                        .arg(street.name);
            return false;
        }
        if (street.points.size() < 2) {
            if (error)
                *error = tr("Street \"%1\" must contain at least two points.")
                        .arg(street.name);
            return false;
        }
        for (int p = 0; p < street.points.size(); ++p) {
            const QPointF point = street.points.at(p);
            if (!qIsFinite(point.x()) || !qIsFinite(point.y())) {
                if (error)
                    *error = tr("Street \"%1\" contains invalid coordinates.")
                            .arg(street.name);
                return false;
            }
            if (p > 0 && point == street.points.at(p - 1)) {
                if (error)
                    *error = tr("Street \"%1\" contains two identical "
                               "consecutive points.").arg(street.name);
                return false;
            }
        }
    }
    return true;
}

void StreetNamesDock::attachScene(BaseGraphicsScene *scene)
{
    if (mScene == scene)
        return;
    detachScene();
    mScene = scene;
    if (mScene) {
        mScene->installEventFilter(this);
        if (mDocument && mDocument->view() &&
                mDocument->view()->zoomable()) {
            connect(mDocument->view()->zoomable(), &Zoomable::scaleChanged,
                    this, [this]() { rebuildGraphics(); });
        }
        connect(mScene, &QObject::destroyed, this, [this]() {
            // QGraphicsScene owns and destroys the overlay items. Do not try
            // to delete their stale addresses when another world is opened.
            mPointItems.clear();
            mLabelItems.clear();
            mPathItems.clear();
            mScene = nullptr;
            mDragging = false;
            mMode = InteractionMode::Select;
            updateUi();
        });
        rebuildGraphics();
    }
}

void StreetNamesDock::detachScene()
{
    if (!mScene)
        return;
    if (mDocument && mDocument->view()) {
        mDocument->view()->unsetCursor();
        if (mDocument->view()->zoomable())
            mDocument->view()->zoomable()->disconnect(this);
    }
    mScene->removeEventFilter(this);
    clearGraphics();
    mScene->disconnect(this);
    mScene = nullptr;
    mDragging = false;
    mMode = InteractionMode::Select;
}

void StreetNamesDock::clearGraphics()
{
    if (mScene) {
        for (QGraphicsEllipseItem *item : mPointItems) {
            mScene->removeItem(item);
            delete item;
        }
        for (QGraphicsSimpleTextItem *item : mLabelItems) {
            if (!item)
                continue;
            mScene->removeItem(item);
            delete item;
        }
        for (QGraphicsPathItem *item : mPathItems) {
            mScene->removeItem(item);
            delete item;
        }
    }
    mPointItems.clear();
    mLabelItems.clear();
    mPathItems.clear();
}

void StreetNamesDock::rebuildGraphics()
{
    clearGraphics();
    if (!mScene || !mShowStreetsCheckBox->isChecked())
        return;

    const qreal overlayZ = mScene->isCellScene()
            ? CellScene::ZVALUE_ROADITEM_SELECTED + 10
            : WorldScene::ZVALUE_SELECTIONITEM + 10;
    const int configuredWidth = mVisualWidthSpinBox->value();
    const BaseGraphicsView *activeView =
            mDocument ? mDocument->view() : nullptr;
    const qreal viewScale = activeView
            ? qAbs(activeView->transform().m11())
            : 1.0;
    QList<QRectF> occupiedLabelRects;
    QRectF visibleCellWorldBounds;
    if (CellScene *cellScene = mScene->asCellScene()) {
        const int cellSize = mWorldDocument->world()->cellSize();
        const QPoint origin =
                mWorldDocument->world()->getGenerateLotsSettings().worldOrigin;
        visibleCellWorldBounds = QRectF(
                    (cellScene->cell()->x() + origin.x()) * cellSize,
                    (cellScene->cell()->y() + origin.y()) * cellSize,
                    cellSize, cellSize).adjusted(-32, -32, 32, 32);
    }

    for (int i = 0; i < mStreets.size(); ++i) {
        const StreetNameRecord &street = mStreets.at(i);
        if (!visibleCellWorldBounds.isNull() &&
                !street.points.boundingRect().adjusted(-1, -1, 1, 1).intersects(
                    visibleCellWorldBounds)) {
            continue;
        }
        const bool selected = i == mSelectedStreet;
        QPainterPath path;
        if (!street.points.isEmpty()) {
            path.moveTo(worldToScene(street.points.first()));
            for (int p = 1; p < street.points.size(); ++p)
                path.lineTo(worldToScene(street.points.at(p)));
        }

        const qreal logicalScale =
                qBound<qreal>(0.75, qSqrt(street.width / 5.0), 2.25);
        const qreal lineWidth = configuredWidth * logicalScale +
                (selected ? 2.0 : 0.0);

        QGraphicsPathItem *casingItem = new QGraphicsPathItem(path);
        QPen casingPen(selected
                       ? QColor(210, 235, 255, 250)
                       : QColor(245, 242, 232, 210));
        casingPen.setCosmetic(true);
        casingPen.setWidthF(lineWidth + (selected ? 4.0 : 2.0));
        casingPen.setCapStyle(Qt::RoundCap);
        casingPen.setJoinStyle(Qt::RoundJoin);
        casingItem->setPen(casingPen);
        casingItem->setAcceptedMouseButtons(Qt::NoButton);
        casingItem->setZValue(overlayZ);
        mScene->addItem(casingItem);
        mPathItems.append(casingItem);

        QGraphicsPathItem *pathItem = new QGraphicsPathItem(path);
        QPen pen(selected
                 ? QColor(25, 105, 210, 250)
                 : QColor(83, 79, 76, 225));
        pen.setCosmetic(true);
        pen.setWidthF(lineWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        pathItem->setPen(pen);
        pathItem->setAcceptedMouseButtons(Qt::NoButton);
        pathItem->setZValue(overlayZ + 1);
        pathItem->setData(StreetIndexRole, i);
        mScene->addItem(pathItem);
        mPathItems.append(pathItem);

        const bool showLabel = selected || mScene->isCellScene() ||
                viewScale >= 0.28;
        if (street.points.size() >= 2 && showLabel) {
            const int middle = (street.points.size() - 1) / 2;
            const QPointF labelPoint =
                    (street.points.at(middle) + street.points.at(middle + 1)) / 2.0;
            QGraphicsSimpleTextItem *label =
                    new QGraphicsSimpleTextItem(street.name);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            QFont font = label->font();
            font.setBold(selected);
            font.setPointSize(selected ? 11 : 9);
            label->setFont(font);
            const QRectF textRect = label->boundingRect();
            const QPointF labelOffset(-textRect.width() / 2.0,
                                      -textRect.height() / 2.0);
            const QPointF sceneLabelPoint = worldToScene(labelPoint);

            if (!selected && activeView) {
                const QPointF screenPoint =
                        activeView->mapFromScene(sceneLabelPoint);
                QRectF screenRect(
                            screenPoint.x() + labelOffset.x() - 7,
                            screenPoint.y() + labelOffset.y() - 4,
                            textRect.width() + 14,
                            textRect.height() + 8);
                bool overlaps = false;
                for (const QRectF &occupied : qAsConst(occupiedLabelRects)) {
                    if (screenRect.intersects(occupied)) {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps) {
                    delete label;
                    continue;
                }
                occupiedLabelRects.append(screenRect);
            }

            QPainterPath bubblePath;
            bubblePath.addRoundedRect(
                        textRect.translated(labelOffset)
                        .adjusted(-7, -4, 7, 4),
                        5, 5);
            QGraphicsPathItem *bubble =
                    new QGraphicsPathItem(bubblePath);
            bubble->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            bubble->setBrush(selected
                             ? QColor(25, 105, 210, 238)
                             : QColor(250, 248, 240, 225));
            QPen bubblePen(selected
                           ? QColor(235, 247, 255)
                           : QColor(55, 52, 48));
            bubblePen.setCosmetic(true);
            bubblePen.setWidthF(selected ? 2.0 : 1.25);
            bubble->setPen(bubblePen);
            bubble->setPos(sceneLabelPoint);
            bubble->setAcceptedMouseButtons(Qt::NoButton);
            bubble->setZValue(overlayZ + 2);
            mScene->addItem(bubble);
            mPathItems.append(bubble);

            label->setBrush(selected ? Qt::white : QColor(25, 24, 22));
            label->setTransform(
                        QTransform::fromTranslate(labelOffset.x(),
                                                  labelOffset.y()));
            label->setPos(sceneLabelPoint);
            label->setAcceptedMouseButtons(Qt::NoButton);
            label->setZValue(overlayZ + 3);
            mScene->addItem(label);
            mLabelItems.append(label);
        }
    }

    if ((mMode == InteractionMode::Edit ||
         mMode == InteractionMode::Create) &&
            mSelectedStreet >= 0 && mSelectedStreet < mStreets.size()) {
        const StreetNameRecord &street = mStreets.at(mSelectedStreet);
        for (int p = 0; p < street.points.size(); ++p) {
            QGraphicsEllipseItem *point =
                    new QGraphicsEllipseItem(QRectF(-4, -4, 8, 8));
            point->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            point->setPen(QPen(Qt::black, 1));
            point->setBrush(p == mSelectedPoint
                            ? QColor(255, 210, 0)
                            : (p == 0 ? QColor(255, 80, 80)
                                      : QColor(0, 220, 255)));
            point->setPos(worldToScene(street.points.at(p)));
            point->setAcceptedMouseButtons(Qt::NoButton);
            point->setZValue(overlayZ + 4);
            point->setData(StreetIndexRole, p);
            mScene->addItem(point);
            mPointItems.append(point);
        }
    }
}

void StreetNamesDock::rebuildList()
{
    mUpdatingUi = true;
    const bool sortingEnabled = mStreetList->isSortingEnabled();
    const int sortColumn = mStreetList->sortColumn();
    const Qt::SortOrder sortOrder =
            mStreetList->header()->sortIndicatorOrder();
    mStreetList->setSortingEnabled(false);
    mStreetList->clear();
    for (int i = 0; i < mStreets.size(); ++i) {
        const StreetNameRecord &street = mStreets.at(i);
        QTreeWidgetItem *item = new StreetTreeWidgetItem(mStreetList);
        item->setText(0, street.name);
        item->setText(1, QString::number(street.width));
        item->setText(2, QString::number(street.points.size()));
        item->setData(0, StreetIndexRole, i);
        item->setData(1, StreetSortRole, street.width);
        item->setData(2, StreetSortRole, street.points.size());
        if (i == mSelectedStreet)
            mStreetList->setCurrentItem(item);
    }
    mStreetList->setSortingEnabled(sortingEnabled);
    if (sortingEnabled && sortColumn >= 0)
        mStreetList->sortItems(sortColumn, sortOrder);
    applyStreetFilter();
    mUpdatingUi = false;
    updateUi();
}

void StreetNamesDock::applyStreetFilter()
{
    const QString filter = mStreetFilterEdit->text().trimmed();
    for (int i = 0; i < mStreetList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = mStreetList->topLevelItem(i);
        item->setHidden(!filter.isEmpty() &&
                        !item->text(0).contains(filter,
                                                Qt::CaseInsensitive));
    }
}

void StreetNamesDock::streetFilterChanged(const QString &)
{
    applyStreetFilter();
}

void StreetNamesDock::selectStreet(int index)
{
    if (index < 0 || index >= mStreets.size())
        index = -1;
    mSelectedStreet = index;
    mSelectedPoint = -1;
    rebuildList();
    rebuildGraphics();
}

void StreetNamesDock::streetSelectionChanged()
{
    if (mUpdatingUi)
        return;
    QTreeWidgetItem *item = mStreetList->currentItem();
    selectStreet(item ? item->data(0, StreetIndexRole).toInt() : -1);
}

void StreetNamesDock::beginSnapshot()
{
    mSnapshotBefore = mStreets;
    mSnapshotSelection = mSelectedStreet;
}

void StreetNamesDock::commitSnapshot(const QString &text)
{
    if (mSnapshotBefore == mStreets) {
        mSnapshotBefore.clear();
        return;
    }
    mUndoStack->push(new StreetNamesSnapshotCommand(
                         this, mSnapshotBefore, mSnapshotSelection,
                         mStreets, mSelectedStreet, text));
    mSnapshotBefore.clear();
}

void StreetNamesDock::cancelSnapshot()
{
    if (!mSnapshotBefore.isEmpty() || !mStreets.isEmpty())
        applySnapshot(mSnapshotBefore, mSnapshotSelection);
    mSnapshotBefore.clear();
}

void StreetNamesDock::applySnapshot(
        const QVector<StreetNameRecord> &streets, int selectedStreet)
{
    mStreets = streets;
    mSelectedStreet = selectedStreet;
    if (mSelectedStreet >= mStreets.size())
        mSelectedStreet = mStreets.size() - 1;
    mSelectedPoint = -1;
    rebuildList();
    rebuildGraphics();
    updateUi();
}

void StreetNamesDock::createStreet()
{
    if (!mScene || !mWorldDocument || !mShowStreetsCheckBox->isChecked())
        return;
    if (mMode == InteractionMode::Create) {
        finishCreating(true);
        return;
    }

    beginSnapshot();
    StreetNameRecord street;
    street.name = tr("Street");
    street.width = 5;
    mStreets.append(street);
    mSelectedStreet = mStreets.size() - 1;
    mSelectedPoint = -1;
    mMode = InteractionMode::Create;
    rebuildList();
    rebuildGraphics();
    updateUi();
}

void StreetNamesDock::editStreet()
{
    if (mMode == InteractionMode::Edit) {
        mMode = InteractionMode::Select;
        mSelectedPoint = -1;
    } else if (mMode == InteractionMode::Select &&
               !mStreets.isEmpty() &&
               mScene && mShowStreetsCheckBox->isChecked()) {
        mMode = InteractionMode::Edit;
    }
    rebuildGraphics();
    updateUi();
}

void StreetNamesDock::showStreetsChanged(bool visible)
{
    QSettings().setValue(QStringLiteral("StreetNames/Visible"), visible);
    if (!visible) {
        if (mMode == InteractionMode::Create)
            finishCreating(false);
        mMode = InteractionMode::Select;
        mSelectedPoint = -1;
        mDragging = false;
        mDraggingPoint = -1;
    }
    rebuildGraphics();
    updateUi();
}

void StreetNamesDock::visualWidthChanged(int width)
{
    QSettings().setValue(QStringLiteral("StreetNames/LineWidth"), width);
    rebuildGraphics();
}

void StreetNamesDock::finishCreating(bool accept)
{
    if (mMode != InteractionMode::Create)
        return;

    if (accept && mSelectedStreet >= 0 &&
            mSelectedStreet < mStreets.size() &&
            mStreets.at(mSelectedStreet).points.size() >= 2) {
        commitSnapshot(tr("Create street"));
    } else {
        cancelSnapshot();
    }
    mMode = InteractionMode::Select;
    rebuildList();
    rebuildGraphics();
    updateUi();
}

void StreetNamesDock::removeStreet()
{
    if (mSelectedStreet < 0 || mSelectedStreet >= mStreets.size())
        return;
    beginSnapshot();
    mStreets.removeAt(mSelectedStreet);
    if (mSelectedStreet >= mStreets.size())
        mSelectedStreet = mStreets.size() - 1;
    mSelectedPoint = -1;
    commitSnapshot(tr("Remove street"));
}

void StreetNamesDock::reverseStreet()
{
    if (mSelectedStreet < 0 || mSelectedStreet >= mStreets.size())
        return;
    beginSnapshot();
    StreetNameRecord &street = mStreets[mSelectedStreet];
    std::reverse(street.points.begin(), street.points.end());
    commitSnapshot(tr("Reverse street"));
}

void StreetNamesDock::splitStreetAtSelectedPoint()
{
    if (mSelectedStreet < 0 || mSelectedStreet >= mStreets.size() ||
            mSelectedPoint <= 0)
        return;
    const StreetNameRecord source = mStreets.at(mSelectedStreet);
    if (mSelectedPoint >= source.points.size() - 1)
        return;

    beginSnapshot();
    StreetNameRecord second = source;
    second.points = source.points.mid(mSelectedPoint);
    mStreets[mSelectedStreet].points =
            source.points.mid(0, mSelectedPoint + 1);
    mStreets.insert(mSelectedStreet + 1, second);
    ++mSelectedStreet;
    mSelectedPoint = 0;
    commitSnapshot(tr("Split street"));
}

void StreetNamesDock::removeSelectedPoint()
{
    if (mSelectedStreet < 0 || mSelectedStreet >= mStreets.size() ||
            mSelectedPoint < 0)
        return;
    StreetNameRecord &street = mStreets[mSelectedStreet];
    if (street.points.size() <= 2)
        return;
    beginSnapshot();
    street.points.removeAt(mSelectedPoint);
    mSelectedPoint = -1;
    commitSnapshot(tr("Remove street point"));
}

void StreetNamesDock::streetNameEditingFinished()
{
    if (mUpdatingUi || mSelectedStreet < 0 ||
            mSelectedStreet >= mStreets.size())
        return;
    const QString name = mStreetNameEdit->text().trimmed();
    if (name == mStreets.at(mSelectedStreet).name)
        return;
    beginSnapshot();
    mStreets[mSelectedStreet].name = name;
    commitSnapshot(tr("Rename street"));
}

void StreetNamesDock::streetWidthEditingFinished()
{
    if (mUpdatingUi || mSelectedStreet < 0 ||
            mSelectedStreet >= mStreets.size())
        return;
    const int width = mWidthSpinBox->value();
    if (width == mStreets.at(mSelectedStreet).width)
        return;
    beginSnapshot();
    mStreets[mSelectedStreet].width = width;
    commitSnapshot(tr("Change street width"));
}

QPointF StreetNamesDock::sceneToWorld(const QPointF &scenePoint) const
{
    if (!mScene || !mWorldDocument)
        return QPointF();
    const int cellSize = mWorldDocument->world()->cellSize();
    const QPoint origin =
            mWorldDocument->world()->getGenerateLotsSettings().worldOrigin;

    if (WorldScene *worldScene = mScene->asWorldScene()) {
        const QPointF localCells = worldScene->pixelToCellCoords(scenePoint);
        return QPointF((localCells.x() + origin.x()) * cellSize,
                       (localCells.y() + origin.y()) * cellSize);
    }

    if (CellScene *cellScene = mScene->asCellScene()) {
        const QPointF localTiles =
                cellScene->renderer()->pixelToTileCoords(scenePoint);
        const QPoint cellOffset(cellScene->cell()->x() * cellSize,
                                cellScene->cell()->y() * cellSize);
        return localTiles + cellOffset +
                QPointF(origin.x() * cellSize, origin.y() * cellSize);
    }

    return QPointF();
}

QPointF StreetNamesDock::worldToScene(const QPointF &worldPoint) const
{
    if (!mScene || !mWorldDocument)
        return QPointF();
    const int cellSize = mWorldDocument->world()->cellSize();
    const QPoint origin =
            mWorldDocument->world()->getGenerateLotsSettings().worldOrigin;

    if (WorldScene *worldScene = mScene->asWorldScene()) {
        return worldScene->cellToPixelCoords(
                    worldPoint.x() / cellSize - origin.x(),
                    worldPoint.y() / cellSize - origin.y());
    }

    if (CellScene *cellScene = mScene->asCellScene()) {
        const QPointF internalWorld =
                worldPoint - QPointF(origin.x() * cellSize,
                                     origin.y() * cellSize);
        const QPointF localTiles =
                internalWorld - QPointF(cellScene->cell()->x() * cellSize,
                                        cellScene->cell()->y() * cellSize);
        return cellScene->renderer()->tileToPixelCoords(localTiles);
    }

    return QPointF();
}

QPointF StreetNamesDock::snappedWorldPoint(const QPointF &scenePoint) const
{
    const QPointF world = sceneToWorld(scenePoint);
    return QPointF(qRound(world.x() * 2.0) / 2.0,
                   qRound(world.y() * 2.0) / 2.0);
}

int StreetNamesDock::pickStreet(const QPointF &scenePoint) const
{
    if (!mScene || !mDocument || !mDocument->view())
        return -1;

    const BaseGraphicsView *view = mDocument->view();
    const QPointF mouse = view->mapFromScene(scenePoint);
    const qreal maximumDistance =
            qMax<qreal>(12.0, mVisualWidthSpinBox->value() + 6.0);
    qreal bestDistance = maximumDistance * maximumDistance;
    int bestStreet = -1;
    for (int i = 0; i < mStreets.size(); ++i) {
        const StreetNameRecord &street = mStreets.at(i);
        for (int p = 0; p + 1 < street.points.size(); ++p) {
            const QPointF a =
                    view->mapFromScene(worldToScene(street.points.at(p)));
            const QPointF b =
                    view->mapFromScene(worldToScene(street.points.at(p + 1)));
            const qreal distance =
                    pointSegmentDistanceSquared(mouse, a, b);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestStreet = i;
            }
        }
    }
    return bestStreet;
}

int StreetNamesDock::pickPoint(const QPointF &scenePoint) const
{
    if (!mScene || !mDocument || !mDocument->view() ||
            mSelectedStreet < 0 || mSelectedStreet >= mStreets.size())
        return -1;

    const BaseGraphicsView *view = mDocument->view();
    const QPoint mouse = view->mapFromScene(scenePoint);
    const StreetNameRecord &street = mStreets.at(mSelectedStreet);
    for (int p = street.points.size() - 1; p >= 0; --p) {
        const QPoint screen = view->mapFromScene(
                    worldToScene(street.points.at(p)));
        const QPoint delta = mouse - screen;
        if (delta.x() * delta.x() + delta.y() * delta.y() <= 9 * 9)
            return p;
    }
    return -1;
}

bool StreetNamesDock::closestPointOnSelectedStreet(
        const QPointF &scenePoint, int *segment, QPointF *worldPoint) const
{
    if (!mDocument || !mDocument->view() ||
            mSelectedStreet < 0 || mSelectedStreet >= mStreets.size())
        return false;

    const BaseGraphicsView *view = mDocument->view();
    const QPointF mouse = view->mapFromScene(scenePoint);
    const StreetNameRecord &street = mStreets.at(mSelectedStreet);
    qreal bestDistance = std::numeric_limits<qreal>::max();
    int bestSegment = -1;
    QPointF bestPoint;
    for (int p = 0; p + 1 < street.points.size(); ++p) {
        const QPointF worldA = street.points.at(p);
        const QPointF worldB = street.points.at(p + 1);
        const QPointF screenA = view->mapFromScene(worldToScene(worldA));
        const QPointF screenB = view->mapFromScene(worldToScene(worldB));
        QPointF candidateScreen;
        const qreal distance = pointSegmentDistanceSquared(
                    mouse, screenA, screenB, &candidateScreen);
        if (distance < bestDistance) {
            const QPointF screenDelta = screenB - screenA;
            const qreal lengthSquared =
                    QPointF::dotProduct(screenDelta, screenDelta);
            const qreal t = lengthSquared > 0.0
                    ? qBound<qreal>(
                          0.0,
                          QPointF::dotProduct(candidateScreen - screenA,
                                              screenDelta) / lengthSquared,
                          1.0)
                    : 0.0;
            bestDistance = distance;
            bestSegment = p;
            bestPoint = worldA + (worldB - worldA) * t;
        }
    }
    if (bestSegment < 0 || bestDistance > 12.0 * 12.0)
        return false;
    if (segment)
        *segment = bestSegment;
    if (worldPoint)
        *worldPoint = QPointF(qRound(bestPoint.x() * 2.0) / 2.0,
                              qRound(bestPoint.y() * 2.0) / 2.0);
    return true;
}

bool StreetNamesDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != mScene || !mScene)
        return QDockWidget::eventFilter(watched, event);

    if (!mShowStreetsCheckBox->isChecked()) {
        return QDockWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::GraphicsSceneMousePress) {
        QGraphicsSceneMouseEvent *mouseEvent =
                static_cast<QGraphicsSceneMouseEvent *>(event);
        if (mMode == InteractionMode::Select &&
                mouseEvent->button() == Qt::LeftButton) {
            const int street = pickStreet(mouseEvent->scenePos());
            if (street >= 0) {
                if (!mStreetFilterEdit->text().isEmpty())
                    mStreetFilterEdit->clear();
                selectStreet(street);
                mConsumedNavigationPress = true;
                mouseEvent->accept();
                return true;
            }
            // Empty navigation clicks remain entirely available to WorldEd.
            mConsumedNavigationPress = false;
            return QDockWidget::eventFilter(watched, event);
        } else if (mMode == InteractionMode::Create) {
            if (mouseEvent->button() == Qt::RightButton) {
                finishCreating(true);
                mouseEvent->accept();
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton &&
                    mSelectedStreet >= 0 &&
                    mSelectedStreet < mStreets.size()) {
                const QPointF point = snappedWorldPoint(mouseEvent->scenePos());
                StreetNameRecord &street = mStreets[mSelectedStreet];
                if (!street.points.contains(point))
                    street.points.append(point);
                mSelectedPoint = street.points.size() - 1;
                rebuildList();
                rebuildGraphics();
                mouseEvent->accept();
                return true;
            }
        } else if (mMode == InteractionMode::Edit &&
                   mouseEvent->button() == Qt::LeftButton) {
            const int point = pickPoint(mouseEvent->scenePos());
            if (point >= 0) {
                beginSnapshot();
                mSelectedPoint = point;
                mDraggingPoint = point;
                mDragging = true;
                rebuildGraphics();
                mouseEvent->accept();
                return true;
            }
            const int street = pickStreet(mouseEvent->scenePos());
            if (street >= 0) {
                selectStreet(street);
                mouseEvent->accept();
                return true;
            }
            // Geometry-edit mode is exclusive: an empty click must not alter
            // WorldEd's cell/object selection underneath the overlay.
            mouseEvent->accept();
            return true;
        }
    } else if (event->type() == QEvent::GraphicsSceneMouseMove) {
        if (mDragging && mSelectedStreet >= 0 &&
                mSelectedStreet < mStreets.size() && mDraggingPoint >= 0) {
            QGraphicsSceneMouseEvent *mouseEvent =
                    static_cast<QGraphicsSceneMouseEvent *>(event);
            mStreets[mSelectedStreet].points[mDraggingPoint] =
                    snappedWorldPoint(mouseEvent->scenePos());
            rebuildGraphics();
            mouseEvent->accept();
            return true;
        }
    } else if (event->type() == QEvent::GraphicsSceneMouseRelease) {
        if (mMode == InteractionMode::Select &&
                mConsumedNavigationPress) {
            mConsumedNavigationPress = false;
            QGraphicsSceneMouseEvent *mouseEvent =
                    static_cast<QGraphicsSceneMouseEvent *>(event);
            mouseEvent->accept();
            return true;
        }
        if (mDragging) {
            QGraphicsSceneMouseEvent *mouseEvent =
                    static_cast<QGraphicsSceneMouseEvent *>(event);
            mDragging = false;
            mDraggingPoint = -1;
            commitSnapshot(tr("Move street point"));
            mouseEvent->accept();
            return true;
        }
        if (mMode == InteractionMode::Edit) {
            QGraphicsSceneMouseEvent *mouseEvent =
                    static_cast<QGraphicsSceneMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                mouseEvent->accept();
                return true;
            }
        }
    } else if (event->type() == QEvent::GraphicsSceneMouseDoubleClick) {
        if (mMode == InteractionMode::Edit && mSelectedStreet >= 0) {
            QGraphicsSceneMouseEvent *mouseEvent =
                    static_cast<QGraphicsSceneMouseEvent *>(event);
            int segment = -1;
            QPointF point;
            if (closestPointOnSelectedStreet(mouseEvent->scenePos(),
                                             &segment, &point)) {
                beginSnapshot();
                mStreets[mSelectedStreet].points.insert(segment + 1, point);
                mSelectedPoint = segment + 1;
                commitSnapshot(tr("Insert street point"));
                mouseEvent->accept();
                return true;
            }
            mouseEvent->accept();
            return true;
        }
    } else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape &&
                mMode == InteractionMode::Create) {
            finishCreating(false);
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape &&
                mMode == InteractionMode::Edit) {
            editStreet();
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete &&
                mMode == InteractionMode::Edit) {
            if (mSelectedPoint >= 0)
                removeSelectedPoint();
            else
                removeStreet();
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_R &&
                mMode == InteractionMode::Edit &&
                mSelectedStreet >= 0) {
            reverseStreet();
            keyEvent->accept();
            return true;
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

void StreetNamesDock::updateUi()
{
    const bool hasDocument = !mWorldDocument.isNull();
    const bool hasScene = !mScene.isNull();
    const bool hasStreet = mSelectedStreet >= 0 &&
            mSelectedStreet < mStreets.size();
    const bool hasRemovablePoint = hasStreet && mSelectedPoint >= 0 &&
            mStreets.at(mSelectedStreet).points.size() > 2;
    const bool hasSplittablePoint = hasStreet && mSelectedPoint > 0 &&
            mSelectedPoint < mStreets.at(mSelectedStreet).points.size() - 1;

    mBrowseButton->setEnabled(hasDocument);
    mLoadButton->setEnabled(hasDocument);
    mSaveButton->setEnabled(hasDocument);
    mUndoButton->setEnabled(mUndoStack->canUndo());
    mRedoButton->setEnabled(mUndoStack->canRedo());
    const bool streetsVisible = mShowStreetsCheckBox->isChecked();
    const bool geometryMode = mMode == InteractionMode::Edit;
    const bool navigationMode = mMode == InteractionMode::Select;

    mCreateButton->setEnabled(hasScene && streetsVisible &&
                              mMode != InteractionMode::Edit);
    mEditButton->setEnabled(hasScene && streetsVisible && !mStreets.isEmpty() &&
                            mMode != InteractionMode::Create);
    mRemoveButton->setEnabled(hasStreet && mMode != InteractionMode::Create);
    mReverseButton->setEnabled(hasStreet && mMode != InteractionMode::Create);
    mSplitButton->setEnabled(hasSplittablePoint &&
                             geometryMode);
    mRemovePointButton->setEnabled(hasRemovablePoint &&
                                   geometryMode);
    mStreetList->setEnabled(hasDocument && navigationMode);
    mStreetNameEdit->setEnabled(hasStreet &&
                                mMode != InteractionMode::Create);
    mWidthSpinBox->setEnabled(hasStreet &&
                              mMode != InteractionMode::Create);
    mVisualWidthSpinBox->setEnabled(streetsVisible);

    mUpdatingUi = true;
    if (hasStreet) {
        mStreetNameEdit->setText(mStreets.at(mSelectedStreet).name);
        mWidthSpinBox->setValue(mStreets.at(mSelectedStreet).width);
    } else {
        mStreetNameEdit->clear();
        mWidthSpinBox->setValue(5);
    }
    mUpdatingUi = false;

    if (mMode == InteractionMode::Create) {
        setWindowTitle(tr("Street Names — CREATE MODE"));
        mCreateButton->setToolTip(tr("Finish creating street"));
        mEditButton->setToolTip(tr("Edit street geometry"));
        mEditButton->setChecked(false);
        mStatusLabel->setStyleSheet(
                    QStringLiteral("QLabel { background: #8a4b08; color: white; "
                                   "font-weight: bold; padding: 5px; }"));
        mStatusLabel->setText(
                    tr("CREATE MODE — Left-click to add points. Right-click "
                       "or press Finish to keep the street; Escape cancels."));
        if (mDocument && mDocument->view())
            mDocument->view()->setCursor(Qt::CrossCursor);
    } else if (mMode == InteractionMode::Edit) {
        setWindowTitle(tr("Street Names — EDIT MODE"));
        mCreateButton->setToolTip(tr("Create street"));
        mEditButton->setToolTip(tr("Stop editing street geometry"));
        mEditButton->setChecked(true);
        mStatusLabel->setStyleSheet(
                    QStringLiteral("QLabel { background: #1857a4; color: white; "
                                   "font-weight: bold; padding: 5px; }"));
        mStatusLabel->setText(
                    tr("EDIT MODE — Click a street to select it, drag a point "
                       "to move it, or double-click close to the selected "
                       "street to insert a point. Escape stops editing."));
        if (mDocument && mDocument->view())
            mDocument->view()->setCursor(Qt::CrossCursor);
    } else {
        setWindowTitle(tr("Street Names"));
        mCreateButton->setToolTip(tr("Create street"));
        mEditButton->setToolTip(tr("Edit street geometry"));
        mEditButton->setChecked(false);
        mStatusLabel->setStyleSheet(
                    QStringLiteral("QLabel { padding: 3px; }"));
        QString status = tr("NAVIGATION MODE — %1 street(s). Select a street "
                            "in the list, then press Edit Geometry to change "
                            "its points. Map clicks remain available to "
                            "WorldEd.")
                .arg(mStreets.size());
        if (!mUndoStack->isClean())
            status += tr(" Unsaved changes.");
        if (hasDocument && !hasScene)
            status += tr(" Open a World or Cell tab to edit graphically.");
        mStatusLabel->setText(status);
        if (mDocument && mDocument->view())
            mDocument->view()->unsetCursor();
    }
}
