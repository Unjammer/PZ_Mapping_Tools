#include "regionsdock.h"
#include "basegraphicsscene.h"
#include "basegraphicsview.h"
#include "celldocument.h"
#include "cellscene.h"
#include "document.h"
#include "maprenderer.h"
#include "world.h"
#include "worldcell.h"
#include "worlddocument.h"
#include "worldobjectvalidation.h"
#include "worldscene.h"
#include "zoomable.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTreeWidget>
#include <QTransform>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
namespace {
const int RegionIndexRole = Qt::UserRole + 52;
const int RegionSortRole = Qt::UserRole + 53;
class RegionTreeWidgetItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator <(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (column >= 2)
            return data(column, RegionSortRole).toInt() <
                    other.data(column, RegionSortRole).toInt();
        return QString::localeAwareCompare(text(column), other.text(column)) < 0;
    }
};
class RegionsSnapshotCommand : public QUndoCommand
{
public:
    RegionsSnapshotCommand(RegionsDock *dock,
                           const QVector<RegionRecord> &before,
                           int beforeSelection,
                           const QVector<RegionRecord> &after,
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
    QPointer<RegionsDock> mDock;
    QVector<RegionRecord> mBefore;
    QVector<RegionRecord> mAfter;
    int mBeforeSelection;
    int mAfterSelection;
};
QString luaString(const QString &value)
{
    QString escaped;
    escaped.reserve(value.size() + 2);
    escaped += QLatin1Char('"');
    for (QChar ch : value) {
        switch (ch.unicode()) {
        case '\\': escaped += QStringLiteral("\\\\"); break;
        case '"': escaped += QStringLiteral("\\\""); break;
        case '\n': escaped += QStringLiteral("\\n"); break;
        case '\r': escaped += QStringLiteral("\\r"); break;
        case '\t': escaped += QStringLiteral("\\t"); break;
        default:
            if (ch.unicode() < 32 || ch.unicode() == 127) {
                escaped += QStringLiteral("\\%1")
                        .arg(ch.unicode(), 3, 10, QLatin1Char('0'));
            } else {
                escaped += ch;
            }
            break;
        }
    }
    escaped += QLatin1Char('"');
    return escaped;
}
QString luaUtf8String(lua_State *state, int index)
{
    size_t length = 0;
    const char *bytes = lua_tolstring(state, index, &length);
    return bytes ? QString::fromUtf8(bytes, int(length)) : QString();
}
bool readStringField(lua_State *state, int tableIndex, const char *key,
                     QString *value, bool required, QString *error)
{
    tableIndex = tableIndex < 0 ? lua_gettop(state) + tableIndex + 1
                                : tableIndex;
    lua_getfield(state, tableIndex, key);
    const int type = lua_type(state, -1);
    if (type == LUA_TNIL && !required) {
        if (value)
            value->clear();
        lua_pop(state, 1);
        return true;
    }
    if (type != LUA_TSTRING) {
        if (error)
            *error = QStringLiteral("Field '%1' must be a string.")
                    .arg(QString::fromLatin1(key));
        lua_pop(state, 1);
        return false;
    }
    if (value)
        *value = luaUtf8String(state, -1);
    lua_pop(state, 1);
    return true;
}
bool readIntegerField(lua_State *state, int tableIndex, const char *key,
                      int *value, QString *error)
{
    tableIndex = tableIndex < 0 ? lua_gettop(state) + tableIndex + 1
                                : tableIndex;
    lua_getfield(state, tableIndex, key);
    if (lua_type(state, -1) != LUA_TNUMBER) {
        if (error)
            *error = QStringLiteral("Field '%1' must be a number.")
                    .arg(QString::fromLatin1(key));
        lua_pop(state, 1);
        return false;
    }
    const lua_Number number = lua_tonumber(state, -1);
    lua_pop(state, 1);
    if (!std::isfinite(double(number)) || std::floor(double(number)) != number ||
            number < std::numeric_limits<int>::min() ||
            number > std::numeric_limits<int>::max()) {
        if (error)
            *error = QStringLiteral("Field '%1' must be a whole number in range.")
                    .arg(QString::fromLatin1(key));
        return false;
    }
    if (value)
        *value = int(number);
    return true;
}
QColor regionColor(const QString &type)
{
    const int hue = int(qHash(type) % 360u);
    return QColor::fromHsv(hue, 165, 225, 72);
}
}
RegionsDock::RegionsDock(QWidget *parent)
    : QDockWidget(parent)
    , mFileNameEdit(new QLineEdit(this))
    , mBrowseButton(new QPushButton(this))
    , mLoadButton(new QPushButton(this))
    , mSaveButton(new QPushButton(this))
    , mUndoButton(new QPushButton(this))
    , mRedoButton(new QPushButton(this))
    , mShowRegionsCheckBox(new QCheckBox(this))
    , mRegionFilterEdit(new QLineEdit(this))
    , mRegionList(new QTreeWidget(this))
    , mNameEdit(new QLineEdit(this))
    , mTypeCombo(new QComboBox(this))
    , mXSpinBox(new QSpinBox(this))
    , mYSpinBox(new QSpinBox(this))
    , mZSpinBox(new QSpinBox(this))
    , mWidthSpinBox(new QSpinBox(this))
    , mHeightSpinBox(new QSpinBox(this))
    , mAddButton(new QPushButton(this))
    , mDuplicateButton(new QPushButton(this))
    , mRemoveButton(new QPushButton(this))
    , mUseSelectionButton(new QPushButton(this))
    , mPropertyTable(new QTableWidget(this))
    , mAddPropertyButton(new QPushButton(this))
    , mRemovePropertyButton(new QPushButton(this))
    , mStatusLabel(new QLabel(this))
    , mUndoStack(new QUndoStack(this))
{
    setObjectName(QStringLiteral("RegionsDock"));
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
    mShowRegionsCheckBox->setChecked(
                QSettings().value(QStringLiteral("Regions/Visible"), true)
                .toBool());
    mainLayout->addWidget(mShowRegionsCheckBox);
    mRegionFilterEdit->setClearButtonEnabled(true);
    mainLayout->addWidget(mRegionFilterEdit);
    mRegionList->setColumnCount(7);
    mRegionList->setRootIsDecorated(false);
    mRegionList->setSelectionMode(QAbstractItemView::SingleSelection);
    mRegionList->header()->setStretchLastSection(false);
    mRegionList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 7; ++column)
        mRegionList->header()->setSectionResizeMode(
                    column, QHeaderView::ResizeToContents);
    mRegionList->setSortingEnabled(true);
    mRegionList->sortByColumn(0, Qt::AscendingOrder);
    mainLayout->addWidget(mRegionList, 1);
    QFormLayout *identityLayout = new QFormLayout;
    mTypeCombo->setEditable(true);
    mTypeCombo->addItems(QStringList()
                         << QStringLiteral("Region")
                         << QStringLiteral("BuildingName")
                          << QStringLiteral("Mannequin")
                          << QStringLiteral("NoGas")
                          << QStringLiteral("NoPower")
                          << QStringLiteral("NoWater")
                          << QStringLiteral("NoPowerOrWater")
                          << QStringLiteral("ParkingStall")
                          << QStringLiteral("ZombiesType"));
    WorldObjectValidation::applyTypeTooltips(mTypeCombo);
    identityLayout->addRow(tr("Name:"), mNameEdit);
    identityLayout->addRow(tr("Type:"), mTypeCombo);
    mainLayout->addLayout(identityLayout);
    const int coordinateLimit = 1000000000;
    mXSpinBox->setRange(-coordinateLimit, coordinateLimit);
    mYSpinBox->setRange(-coordinateLimit, coordinateLimit);
    mZSpinBox->setRange(-1000, 1000);
    mWidthSpinBox->setRange(1, coordinateLimit);
    mHeightSpinBox->setRange(1, coordinateLimit);
    QGridLayout *geometryLayout = new QGridLayout;
    geometryLayout->addWidget(new QLabel(tr("X:"), this), 0, 0);
    geometryLayout->addWidget(mXSpinBox, 0, 1);
    geometryLayout->addWidget(new QLabel(tr("Y:"), this), 0, 2);
    geometryLayout->addWidget(mYSpinBox, 0, 3);
    geometryLayout->addWidget(new QLabel(tr("Z:"), this), 1, 0);
    geometryLayout->addWidget(mZSpinBox, 1, 1);
    geometryLayout->addWidget(new QLabel(tr("Width:"), this), 1, 2);
    geometryLayout->addWidget(mWidthSpinBox, 1, 3);
    geometryLayout->addWidget(new QLabel(tr("Height:"), this), 2, 2);
    geometryLayout->addWidget(mHeightSpinBox, 2, 3);
    mainLayout->addLayout(geometryLayout);
    QHBoxLayout *regionButtons = new QHBoxLayout;
    regionButtons->addWidget(mAddButton);
    regionButtons->addWidget(mDuplicateButton);
    regionButtons->addWidget(mRemoveButton);
    regionButtons->addWidget(mUseSelectionButton);
    regionButtons->addStretch(1);
    mainLayout->addLayout(regionButtons);
    QGroupBox *propertyGroup = new QGroupBox(this);
    QVBoxLayout *propertyLayout = new QVBoxLayout(propertyGroup);
    mPropertyTable->setColumnCount(3);
    mPropertyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mPropertyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mPropertyTable->setToolTip(
                tr("Property types are String, Number, or Boolean."));
    mPropertyTable->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::ResizeToContents);
    mPropertyTable->horizontalHeader()->setSectionResizeMode(
                1, QHeaderView::ResizeToContents);
    mPropertyTable->horizontalHeader()->setSectionResizeMode(
                2, QHeaderView::Stretch);
    propertyLayout->addWidget(mPropertyTable);
    QHBoxLayout *propertyButtons = new QHBoxLayout;
    propertyButtons->addWidget(mAddPropertyButton);
    propertyButtons->addWidget(mRemovePropertyButton);
    propertyButtons->addStretch(1);
    propertyLayout->addLayout(propertyButtons);
    mainLayout->addWidget(propertyGroup);
    mStatusLabel->setWordWrap(true);
    mainLayout->addWidget(mStatusLabel);
    setWidget(contents);
    const QList<QPushButton *> compactButtons = {
        mAddButton, mDuplicateButton, mRemoveButton, mUseSelectionButton,
        mAddPropertyButton, mRemovePropertyButton
    };
    for (QPushButton *button : compactButtons) {
        button->setText(QString());
        button->setFixedSize(30, 28);
        button->setFlat(true);
    }
    mAddButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    mDuplicateButton->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    mRemoveButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    mUseSelectionButton->setIcon(
                style()->standardIcon(QStyle::SP_DialogApplyButton));
    mAddPropertyButton->setIcon(
                style()->standardIcon(QStyle::SP_DialogYesButton));
    mRemovePropertyButton->setIcon(
                style()->standardIcon(QStyle::SP_DialogNoButton));
    connect(mBrowseButton, &QPushButton::clicked,
            this, &RegionsDock::browseForFile);
    connect(mLoadButton, &QPushButton::clicked,
            this, &RegionsDock::loadFile);
    connect(mSaveButton, &QPushButton::clicked,
            this, &RegionsDock::saveFile);
    connect(mUndoButton, &QPushButton::clicked, mUndoStack, &QUndoStack::undo);
    connect(mRedoButton, &QPushButton::clicked, mUndoStack, &QUndoStack::redo);
    connect(mUndoStack, &QUndoStack::canUndoChanged,
            this, &RegionsDock::updateUi);
    connect(mUndoStack, &QUndoStack::canRedoChanged,
            this, &RegionsDock::updateUi);
    connect(mUndoStack, &QUndoStack::cleanChanged,
            this, &RegionsDock::updateUi);
    connect(mShowRegionsCheckBox, &QCheckBox::toggled,
            this, &RegionsDock::showRegionsChanged);
    connect(mRegionFilterEdit, &QLineEdit::textChanged,
            this, &RegionsDock::regionFilterChanged);
    connect(mRegionList, &QTreeWidget::itemSelectionChanged,
            this, &RegionsDock::regionSelectionChanged);
    connect(mAddButton, &QPushButton::clicked, this, &RegionsDock::addRegion);
    connect(mDuplicateButton, &QPushButton::clicked,
            this, &RegionsDock::duplicateRegion);
    connect(mRemoveButton, &QPushButton::clicked,
            this, &RegionsDock::removeRegion);
    connect(mUseSelectionButton, &QPushButton::clicked,
            this, &RegionsDock::applyCellSelection);
    connect(mAddPropertyButton, &QPushButton::clicked,
            this, &RegionsDock::addProperty);
    connect(mRemovePropertyButton, &QPushButton::clicked,
            this, &RegionsDock::removeProperty);
    connect(mPropertyTable, &QTableWidget::itemChanged,
            this, &RegionsDock::propertyItemChanged);
    connect(mPropertyTable, &QTableWidget::itemSelectionChanged,
            this, &RegionsDock::updateUi);
    connect(mNameEdit, &QLineEdit::editingFinished,
            this, &RegionsDock::regionFieldsEditingFinished);
    connect(mTypeCombo->lineEdit(), &QLineEdit::editingFinished,
            this, &RegionsDock::regionFieldsEditingFinished);
    connect(mTypeCombo, QOverload<const QString &>::of(&QComboBox::activated),
            this, [this](const QString &) { regionFieldsEditingFinished(); });
    const QList<QSpinBox *> geometryBoxes = {
        mXSpinBox, mYSpinBox, mZSpinBox, mWidthSpinBox, mHeightSpinBox
    };
    for (QSpinBox *box : geometryBoxes) {
        connect(box, &QAbstractSpinBox::editingFinished,
                this, &RegionsDock::regionFieldsEditingFinished);
    }
    retranslateUi();
    updateUi();
}
RegionsDock::~RegionsDock()
{
    detachScene();
}
void RegionsDock::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}
void RegionsDock::retranslateUi()
{
    setWindowTitle(tr("Regions"));
    mBrowseButton->setText(tr("..."));
    mBrowseButton->setToolTip(tr("Choose a regions.lua file"));
    mLoadButton->setText(tr("Load"));
    mSaveButton->setText(tr("Save"));
    mUndoButton->setText(tr("Undo"));
    mRedoButton->setText(tr("Redo"));
    mShowRegionsCheckBox->setText(tr("Show region overlay"));
    mRegionFilterEdit->setPlaceholderText(tr("Find region by name or type..."));
    mRegionList->setHeaderLabels(QStringList()
            << tr("Name") << tr("Type") << tr("X") << tr("Y")
            << tr("Z") << tr("W") << tr("H"));
    mPropertyTable->setHorizontalHeaderLabels(
                QStringList() << tr("Property") << tr("Type") << tr("Value"));
    if (QGroupBox *group = qobject_cast<QGroupBox *>(mPropertyTable->parentWidget()))
        group->setTitle(tr("Properties"));
    mAddButton->setToolTip(tr("Add a region from the selected cells"));
    mDuplicateButton->setToolTip(tr("Duplicate the selected region"));
    mRemoveButton->setToolTip(tr("Remove the selected region"));
    mUseSelectionButton->setToolTip(
                tr("Set the selected region bounds from the selected cells"));
    mAddPropertyButton->setToolTip(tr("Add a property"));
    mRemovePropertyButton->setToolTip(tr("Remove the selected property"));
}
void RegionsDock::setDocument(Document *document)
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
        mRegions.clear();
        mSavedRegions.clear();
        mSelectedRegion = -1;
        mUndoStack->clear();
        if (mWorldDocument) {
            mFileNameEdit->setText(defaultFileName());
            if (QFileInfo::exists(mFileNameEdit->text()))
                loadFile();
            else {
                rebuildList();
                rebuildPropertyTable();
                mUndoStack->setClean();
            }
            connect(mWorldDocument, &WorldDocument::generateLotSettingsChanged,
                    this, [this]() { rebuildGraphics(); });
            connect(mWorldDocument, &WorldDocument::selectedCellsChanged,
                    this, &RegionsDock::updateUi);
        } else {
            mFileNameEdit->clear();
            rebuildList();
            rebuildPropertyTable();
        }
    }
    mDocument = document;
    if (scene)
        attachScene(scene);
    updateUi();
}
void RegionsDock::clearDocument()
{
    setDocument(nullptr);
}
QString RegionsDock::defaultFileName() const
{
    if (!mWorldDocument)
        return QString();
    const QString exportDirectory =
            mWorldDocument->world()->getGenerateLotsSettings().exportDir;
    if (!exportDirectory.trimmed().isEmpty())
        return QDir(exportDirectory).absoluteFilePath(
                    QStringLiteral("regions.lua"));
    return QDir(QFileInfo(mWorldDocument->fileName()).absolutePath())
            .absoluteFilePath(QStringLiteral("regions.lua"));
}
bool RegionsDock::maybeSaveCurrentFile()
{
    if (!mWorldDocument || !hasUnsavedChanges())
        return true;
    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Unsaved Regions"),
                tr("The current regions.lua has unsaved changes. Save them now?"),
                QMessageBox::Save | QMessageBox::Discard,
                QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        saveFile();
        return !hasUnsavedChanges();
    }
    return true;
}
void RegionsDock::browseForFile()
{
    QString initial = mFileNameEdit->text().trimmed();
    if (initial.isEmpty())
        initial = defaultFileName();
    const QString fileName = QFileDialog::getSaveFileName(
                this, tr("Select regions.lua"), initial,
                tr("Project Zomboid regions (regions.lua);;Lua files (*.lua)"));
    if (!fileName.isEmpty())
        mFileNameEdit->setText(QDir::toNativeSeparators(fileName));
}
void RegionsDock::loadFile()
{
    if (!mWorldDocument)
        return;
    if (mFileNameEdit->text().trimmed().isEmpty()) {
        browseForFile();
        if (mFileNameEdit->text().trimmed().isEmpty())
            return;
    }
    if (hasUnsavedChanges()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
                    this, tr("Reload Regions"),
                    tr("Discard unsaved region changes and reload the file?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    QVector<RegionRecord> loaded;
    QString error;
    if (!readFile(mFileNameEdit->text().trimmed(), &loaded, &error)) {
        QMessageBox::critical(this, tr("Unable to Load regions.lua"), error);
        return;
    }
    mRegions = loaded;
    mSavedRegions = loaded;
    mSelectedRegion = mRegions.isEmpty() ? -1 : 0;
    mUndoStack->clear();
    mUndoStack->setClean();
    qInfo().noquote() << "Regions editor loaded"
                      << mRegions.size() << "record(s) from"
                      << QDir::toNativeSeparators(
                             mFileNameEdit->text().trimmed());
    rebuildList();
    rebuildPropertyTable();
    rebuildGraphics();
    updateUi();
}
void RegionsDock::saveFile()
{
    saveCurrentFile(true);
}
bool RegionsDock::saveForProject()
{
    if (!mWorldDocument)
        return true;
    QString fileName = mFileNameEdit->text().trimmed();
    if (fileName.isEmpty()) {
        fileName = defaultFileName();
        mFileNameEdit->setText(QDir::toNativeSeparators(fileName));
    }
    if (mRegions.isEmpty() && !hasUnsavedChanges() &&
            !QFileInfo::exists(fileName)) {
        mFileNameEdit->setText(QDir::toNativeSeparators(defaultFileName()));
        return true;
    }
    return saveCurrentFile(false);
}
bool RegionsDock::saveCurrentFile(bool chooseFileWhenMissing)
{
    if (!mWorldDocument)
        return true;
    if (mFileNameEdit->text().trimmed().isEmpty() && chooseFileWhenMissing)
        browseForFile();
    const QString fileName = mFileNameEdit->text().trimmed();
    if (fileName.isEmpty())
        return false;
    QString error;
    if (!validateRecords(mRegions, &error)) {
        QMessageBox::warning(this, tr("Invalid Regions"), error);
        return false;
    }
    const QFileInfo info(fileName);
    if (!QDir().mkpath(info.absolutePath())) {
        QMessageBox::critical(
                    this, tr("Unable to Save regions.lua"),
                    tr("Could not create directory:\n%1")
                    .arg(QDir::toNativeSeparators(info.absolutePath())));
        return false;
    }
    if (!writeFile(fileName, &error)) {
        QMessageBox::critical(this, tr("Unable to Save regions.lua"), error);
        return false;
    }
    mFileNameEdit->setText(QDir::toNativeSeparators(info.absoluteFilePath()));
    mSavedRegions = mRegions;
    mUndoStack->setClean();
    qInfo().noquote() << "Regions editor saved"
                      << mRegions.size() << "record(s) to"
                      << QDir::toNativeSeparators(info.absoluteFilePath());
    updateUi();
    return true;
}
bool RegionsDock::hasUnsavedChanges() const
{
    return !mUndoStack->isClean() || mRegions != mSavedRegions;
}
bool RegionsDock::readFile(const QString &fileName,
                           QVector<RegionRecord> *regions,
                           QString *error) const
{
    lua_State *state = luaL_newstate();
    if (!state) {
        if (error)
            *error = tr("Could not create the Lua reader.");
        return false;
    }
    luaL_openlibs(state);
    const QByteArray nativeName = QFile::encodeName(fileName);
    int status = luaL_loadfile(state, nativeName.constData());
    if (status == LUA_OK)
        status = lua_pcall(state, 0, 0, 0);
    if (status != LUA_OK) {
        if (error) {
            *error = tr("Could not read:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName),
                         luaUtf8String(state, -1));
        }
        lua_close(state);
        return false;
    }
    lua_getglobal(state, "regions");
    if (!lua_istable(state, -1)) {
        if (error)
            *error = tr("The file does not define a regions table.");
        lua_close(state);
        return false;
    }
    QVector<QPair<int, RegionRecord> > indexed;
    QString parseError;
    const int rootIndex = lua_gettop(state);
    lua_pushnil(state);
    while (lua_next(state, rootIndex) != 0) {
        if (lua_type(state, -2) != LUA_TNUMBER ||
                lua_type(state, -1) != LUA_TTABLE) {
            parseError = tr("The regions table must contain numbered entries.");
            break;
        }
        const lua_Number keyNumber = lua_tonumber(state, -2);
        if (!std::isfinite(double(keyNumber)) || keyNumber < 1 ||
                std::floor(double(keyNumber)) != keyNumber ||
                keyNumber > std::numeric_limits<int>::max()) {
            parseError = tr("A regions table index is invalid.");
            break;
        }
        RegionRecord region;
        const int tableIndex = lua_gettop(state);
        if (!readStringField(state, tableIndex, "name", &region.name,
                             false, &parseError) ||
                !readStringField(state, tableIndex, "type", &region.type,
                                 true, &parseError) ||
                !readIntegerField(state, tableIndex, "x", &region.x,
                                  &parseError) ||
                !readIntegerField(state, tableIndex, "y", &region.y,
                                  &parseError) ||
                !readIntegerField(state, tableIndex, "z", &region.z,
                                  &parseError) ||
                !readIntegerField(state, tableIndex, "width", &region.width,
                                  &parseError) ||
                !readIntegerField(state, tableIndex, "height", &region.height,
                                  &parseError)) {
            parseError = tr("Region %1: %2")
                    .arg(int(keyNumber)).arg(parseError);
            break;
        }
        lua_getfield(state, tableIndex, "properties");
        if (!lua_isnil(state, -1)) {
            if (!lua_istable(state, -1)) {
                parseError = tr("Region %1: properties must be a table.")
                        .arg(int(keyNumber));
                break;
            }
            const int propertiesIndex = lua_gettop(state);
            lua_pushnil(state);
            while (lua_next(state, propertiesIndex) != 0) {
                if (lua_type(state, -2) != LUA_TSTRING) {
                    parseError = tr("Region %1: property names must be strings.")
                            .arg(int(keyNumber));
                    break;
                }
                RegionPropertyRecord property;
                property.key = luaUtf8String(state, -2);
                switch (lua_type(state, -1)) {
                case LUA_TSTRING:
                    property.type = QStringLiteral("String");
                    property.value = luaUtf8String(state, -1);
                    break;
                case LUA_TNUMBER:
                    property.type = QStringLiteral("Number");
                    property.value = QLocale::c().toString(
                                double(lua_tonumber(state, -1)), 'g', 16);
                    break;
                case LUA_TBOOLEAN:
                    property.type = QStringLiteral("Boolean");
                    property.value = lua_toboolean(state, -1)
                            ? QStringLiteral("true") : QStringLiteral("false");
                    break;
                default:
                    parseError = tr("Region %1: property '%2' uses an "
                                    "unsupported value type.")
                            .arg(int(keyNumber)).arg(property.key);
                    break;
                }
                if (!parseError.isEmpty())
                    break;
                region.properties.append(property);
                lua_pop(state, 1);
            }
            if (!parseError.isEmpty())
                break;
            std::sort(region.properties.begin(), region.properties.end(),
                      [](const RegionPropertyRecord &a,
                         const RegionPropertyRecord &b) {
                return a.key < b.key;
            });
        }
        lua_pop(state, 1);
        indexed.append(qMakePair(int(keyNumber), region));
        lua_pop(state, 1);
    }
    lua_close(state);
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return false;
    }
    std::sort(indexed.begin(), indexed.end(),
              [](const QPair<int, RegionRecord> &a,
                 const QPair<int, RegionRecord> &b) {
        return a.first < b.first;
    });
    QVector<RegionRecord> parsed;
    parsed.reserve(indexed.size());
    for (const auto &entry : indexed)
        parsed.append(entry.second);
    if (!validateRecords(parsed, error))
        return false;
    if (regions)
        *regions = parsed;
    return true;
}
bool RegionsDock::writeFile(const QString &fileName, QString *error) const
{
    return writeRecords(fileName, mRegions, error);
}
bool RegionsDock::writeRecords(const QString &fileName,
                               const QVector<RegionRecord> &regions,
                               QString *error) const
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = tr("Could not open:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "regions = {\n";
    for (const RegionRecord &region : regions) {
        stream << "  { name = " << luaString(region.name)
               << ", type = " << luaString(region.type)
               << ", x = " << region.x
               << ", y = " << region.y
               << ", z = " << region.z
               << ", width = " << region.width
               << ", height = " << region.height;
        if (!region.properties.isEmpty()) {
            stream << ", properties = { ";
            for (int i = 0; i < region.properties.size(); ++i) {
                const RegionPropertyRecord &property = region.properties.at(i);
                if (i)
                    stream << ", ";
                stream << "[" << luaString(property.key) << "] = ";
                if (property.type.compare(QStringLiteral("Boolean"),
                                          Qt::CaseInsensitive) == 0) {
                    stream << property.value.toLower();
                } else if (property.type.compare(QStringLiteral("Number"),
                                                 Qt::CaseInsensitive) == 0) {
                    bool ok = false;
                    const double number = QLocale::c().toDouble(
                                property.value, &ok);
                    Q_UNUSED(ok)
                    stream << QLocale::c().toString(number, 'g', 16);
                } else {
                    stream << luaString(property.value);
                }
            }
            stream << " }";
        }
        stream << ", },\n";
    }
    stream << "}\n";
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        if (error)
            *error = tr("Could not replace:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }
    return true;
}
bool RegionsDock::validateRecords(const QVector<RegionRecord> &regions,
                                  QString *error) const
{
    for (int i = 0; i < regions.size(); ++i) {
        const RegionRecord &region = regions.at(i);
        if (region.type.trimmed().isEmpty()) {
            if (error)
                *error = tr("Region %1 has an empty type.").arg(i + 1);
            return false;
        }
        if (region.width < 1 || region.height < 1) {
            if (error)
                *error = tr("Region %1 has an invalid width or height.")
                        .arg(i + 1);
            return false;
        }
        QStringList propertyKeys;
        for (const RegionPropertyRecord &property : region.properties) {
            if (property.key.trimmed().isEmpty()) {
                if (error)
                    *error = tr("Region %1 has an empty property name.")
                            .arg(i + 1);
                return false;
            }
            if (propertyKeys.contains(property.key)) {
                if (error)
                    *error = tr("Region %1 defines property '%2' more than once.")
                            .arg(i + 1).arg(property.key);
                return false;
            }
            propertyKeys.append(property.key);
            if (property.type.compare(QStringLiteral("String"),
                                      Qt::CaseInsensitive) == 0) {
                continue;
            }
            if (property.type.compare(QStringLiteral("Number"),
                                      Qt::CaseInsensitive) == 0) {
                bool ok = false;
                const double number = QLocale::c().toDouble(property.value, &ok);
                if (ok && std::isfinite(number))
                    continue;
            } else if (property.type.compare(QStringLiteral("Boolean"),
                                             Qt::CaseInsensitive) == 0) {
                const QString value = property.value.trimmed().toLower();
                if (value == QLatin1String("true") ||
                        value == QLatin1String("false")) {
                    continue;
                }
            }
            if (error)
                *error = tr("Region %1 property '%2' has an invalid %3 value.")
                        .arg(i + 1).arg(property.key, property.type);
            return false;
        }
    }
    return true;
}
bool RegionsDock::validateRegionFile(const QString &fileName,
                                     int *regionCount,
                                     QString *error) const
{
    QVector<RegionRecord> parsed;
    if (!readFile(fileName, &parsed, error))
        return false;
    if (regionCount)
        *regionCount = parsed.size();
    return true;
}
bool RegionsDock::validateEditor(QString *summary, QString *error)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create a temporary directory.");
        return false;
    }
    const QString sourceName = temporary.filePath(QStringLiteral("regions.lua"));
    QFile source(sourceName);
    if (!source.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = source.errorString();
        return false;
    }
    source.write(
        "regions = {\n"
        "  { name = \"Central\", type = \"Region\", x = -10, y = 20, z = 0, width = 600, height = 300 },\n"
        "  { name = \"Central\", type = \"Region\", x = 590, y = 20, z = 0, width = 300, height = 300 },\n"
        "  { name = \"\", type = \"Mannequin\", x = 4, y = 5, z = 1, width = 1, height = 1, properties = { Direction = \"SE\", Script = \"Wizard\", Enabled = true, Weight = 1.25 } },\n"
        "  { name = \"Custom \\\"Zone\\\"\", type = \"CustomType\", x = 7, y = 8, z = 0, width = 9, height = 10 },\n"
        "}\n");
    source.close();
    RegionsDock dock;
    for (int i = 0; i < dock.mTypeCombo->count(); ++i) {
        if (!dock.mTypeCombo->itemData(
                    i, Qt::ToolTipRole).toString().isEmpty()) {
            continue;
        }
        if (error)
            *error = QStringLiteral("The Regions tooltip is missing for %1.")
                    .arg(dock.mTypeCombo->itemText(i));
        return false;
    }
    const QStringList utilityTypes = QStringList()
            << QStringLiteral("NoGas")
            << QStringLiteral("NoPower")
            << QStringLiteral("NoWater")
            << QStringLiteral("NoPowerOrWater");
    for (const QString &type : utilityTypes) {
        const int index = dock.mTypeCombo->findText(
                    type, Qt::MatchFixedString);
        if (index >= 0 && !dock.mTypeCombo->itemData(
                    index, Qt::ToolTipRole).toString().isEmpty())
            continue;
        if (error)
            *error = QStringLiteral("The Regions type list or tooltip is missing %1.")
                    .arg(type);
        return false;
    }
    QVector<RegionRecord> parsed;
    if (!dock.readFile(sourceName, &parsed, error))
        return false;
    if (parsed.size() != 4 || parsed.at(0).x != -10 ||
            parsed.at(1).name != QStringLiteral("Central") ||
            parsed.at(2).properties.size() != 4 ||
            parsed.at(3).name != QStringLiteral("Custom \"Zone\"")) {
        if (error)
            *error = QStringLiteral("The fixture was not parsed faithfully.");
        return false;
    }
    QVector<RegionRecord> edited = parsed;
    edited.insert(1, edited.at(0));
    edited.removeAt(1);
    if (edited != parsed) {
        if (error)
            *error = QStringLiteral("Add/remove record snapshots changed data.");
        return false;
    }
    const QString outputName = temporary.filePath(
                QStringLiteral("regions-roundtrip.lua"));
    if (!dock.writeRecords(outputName, parsed, error))
        return false;
    QVector<RegionRecord> roundTrip;
    if (!dock.readFile(outputName, &roundTrip, error))
        return false;
    if (roundTrip != parsed) {
        if (error)
            *error = QStringLiteral("regions.lua changed during round trip.");
        return false;
    }
    if (summary) {
        *summary = QStringLiteral(
                    "4 ordered rectangles, duplicate names, custom types, "
                    "escaped strings, typed properties, add/remove, and "
                    "UTF-8 Lua round trip, with all utility-zone choices");
    }
    return true;
}
void RegionsDock::attachScene(BaseGraphicsScene *scene)
{
    if (mScene == scene)
        return;
    detachScene();
    mScene = scene;
    if (!mScene)
        return;
    if (mDocument && mDocument->view() && mDocument->view()->zoomable()) {
        connect(mDocument->view()->zoomable(), &Zoomable::scaleChanged,
                this, [this]() { rebuildGraphics(); });
    }
    connect(mScene, &QObject::destroyed, this, [this]() {
        mPathItems.clear();
        mLabelItems.clear();
        mScene = nullptr;
    });
    rebuildGraphics();
}
void RegionsDock::detachScene()
{
    if (!mScene)
        return;
    if (mDocument && mDocument->view() && mDocument->view()->zoomable())
        mDocument->view()->zoomable()->disconnect(this);
    clearGraphics();
    mScene->disconnect(this);
    mScene = nullptr;
}
void RegionsDock::clearGraphics()
{
    if (mScene) {
        for (QGraphicsSimpleTextItem *item : mLabelItems) {
            mScene->removeItem(item);
            delete item;
        }
        for (QGraphicsPathItem *item : mPathItems) {
            mScene->removeItem(item);
            delete item;
        }
    }
    mLabelItems.clear();
    mPathItems.clear();
}
void RegionsDock::rebuildGraphics()
{
    clearGraphics();
    if (!mScene || !mWorldDocument || !mShowRegionsCheckBox->isChecked())
        return;
    QRectF visibleWorldBounds;
    if (CellScene *cellScene = mScene->asCellScene()) {
        const int cellSize = mWorldDocument->world()->cellSize();
        const QPoint origin =
                mWorldDocument->world()->getGenerateLotsSettings().worldOrigin;
        visibleWorldBounds = QRectF(
                    (cellScene->cell()->x() + origin.x()) * cellSize,
                    (cellScene->cell()->y() + origin.y()) * cellSize,
                    cellSize, cellSize).adjusted(-2, -2, 2, 2);
    } else if (mDocument && mDocument->view()) {
        const QPolygonF visibleScene = mDocument->view()->mapToScene(
                    mDocument->view()->viewport()->rect());
        for (const QPointF &point : visibleScene) {
            const QPointF world = sceneToWorld(point);
            if (visibleWorldBounds.isNull())
                visibleWorldBounds = QRectF(world, QSizeF(1, 1));
            else
                visibleWorldBounds |= QRectF(world, QSizeF(1, 1));
        }
        visibleWorldBounds = visibleWorldBounds.normalized();
    }
    const qreal overlayZ = mScene->isCellScene()
            ? CellScene::ZVALUE_ROADITEM_SELECTED + 20
            : WorldScene::ZVALUE_SELECTIONITEM + 20;
    const qreal viewScale = mDocument && mDocument->view()
            ? qAbs(mDocument->view()->transform().m11()) : 1.0;
    for (int i = 0; i < mRegions.size(); ++i) {
        const RegionRecord &region = mRegions.at(i);
        const QRectF worldBounds(region.x, region.y,
                                 region.width, region.height);
        if (!visibleWorldBounds.isNull() &&
                !worldBounds.intersects(visibleWorldBounds)) {
            continue;
        }
        const bool selected = i == mSelectedRegion;
        QPolygonF polygon;
        polygon << worldToScene(worldBounds.topLeft())
                << worldToScene(worldBounds.topRight())
                << worldToScene(worldBounds.bottomRight())
                << worldToScene(worldBounds.bottomLeft());
        QPainterPath path;
        path.addPolygon(polygon);
        path.closeSubpath();
        QColor fill = regionColor(region.type);
        if (selected)
            fill.setAlpha(125);
        QGraphicsPathItem *item = new QGraphicsPathItem(path);
        item->setBrush(fill);
        QPen pen(selected ? QColor(40, 155, 255) : fill.darker(180));
        pen.setCosmetic(true);
        pen.setWidthF(selected ? 3.0 : 1.25);
        item->setPen(pen);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setZValue(overlayZ);
        mScene->addItem(item);
        mPathItems.append(item);
        const bool showLabel = selected || mScene->isCellScene() ||
                viewScale >= 0.40;
        if (!showLabel)
            continue;
        const QString labelText = region.name.isEmpty()
                ? region.type : QStringLiteral("%1 [%2]")
                  .arg(region.name, region.type);
        QGraphicsSimpleTextItem *label =
                new QGraphicsSimpleTextItem(labelText);
        label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        QFont font = label->font();
        font.setBold(selected);
        font.setPointSize(selected ? 10 : 8);
        label->setFont(font);
        label->setBrush(selected ? Qt::white : QColor(25, 25, 25));
        const QRectF textBounds = label->boundingRect();
        const QPointF center = worldToScene(worldBounds.center());
        label->setPos(center);
        label->setTransform(QTransform::fromTranslate(
                                -textBounds.width() / 2.0,
                                -textBounds.height() / 2.0));
        label->setAcceptedMouseButtons(Qt::NoButton);
        label->setZValue(overlayZ + 2);
        QPainterPath bubblePath;
        bubblePath.addRoundedRect(
                    textBounds.translated(-textBounds.width() / 2.0,
                                          -textBounds.height() / 2.0)
                    .adjusted(-5, -3, 5, 3), 4, 4);
        QGraphicsPathItem *bubble = new QGraphicsPathItem(bubblePath);
        bubble->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        bubble->setPos(center);
        bubble->setBrush(selected ? QColor(25, 105, 210, 235)
                                  : QColor(250, 248, 240, 220));
        QPen bubblePen(selected ? QColor(235, 247, 255)
                                : QColor(55, 52, 48));
        bubblePen.setCosmetic(true);
        bubble->setPen(bubblePen);
        bubble->setAcceptedMouseButtons(Qt::NoButton);
        bubble->setZValue(overlayZ + 1);
        mScene->addItem(bubble);
        mPathItems.append(bubble);
        mScene->addItem(label);
        mLabelItems.append(label);
    }
}
void RegionsDock::rebuildList()
{
    mUpdatingUi = true;
    const bool sortingEnabled = mRegionList->isSortingEnabled();
    const int sortColumn = mRegionList->sortColumn();
    const Qt::SortOrder sortOrder =
            mRegionList->header()->sortIndicatorOrder();
    mRegionList->setSortingEnabled(false);
    mRegionList->clear();
    for (int i = 0; i < mRegions.size(); ++i) {
        const RegionRecord &region = mRegions.at(i);
        QTreeWidgetItem *item = new RegionTreeWidgetItem(mRegionList);
        item->setText(0, region.name);
        item->setText(1, region.type);
        item->setToolTip(1, WorldObjectValidation::typeDescription(
                             region.type));
        const int values[] = {
            region.x, region.y, region.z, region.width, region.height
        };
        for (int column = 2; column < 7; ++column) {
            item->setText(column, QString::number(values[column - 2]));
            item->setData(column, RegionSortRole, values[column - 2]);
        }
        item->setData(0, RegionIndexRole, i);
        if (i == mSelectedRegion)
            mRegionList->setCurrentItem(item);
    }
    mRegionList->setSortingEnabled(sortingEnabled);
    if (sortingEnabled && sortColumn >= 0)
        mRegionList->sortItems(sortColumn, sortOrder);
    applyRegionFilter();
    mUpdatingUi = false;
}
void RegionsDock::rebuildPropertyTable()
{
    mUpdatingUi = true;
    mPropertyTable->setRowCount(0);
    if (mSelectedRegion >= 0 && mSelectedRegion < mRegions.size()) {
        const QVector<RegionPropertyRecord> &properties =
                mRegions.at(mSelectedRegion).properties;
        mPropertyTable->setRowCount(properties.size());
        for (int row = 0; row < properties.size(); ++row) {
            const RegionPropertyRecord &property = properties.at(row);
            mPropertyTable->setItem(row, 0,
                                    new QTableWidgetItem(property.key));
            mPropertyTable->setItem(row, 1,
                                    new QTableWidgetItem(property.type));
            mPropertyTable->setItem(row, 2,
                                    new QTableWidgetItem(property.value));
        }
    }
    mUpdatingUi = false;
}
void RegionsDock::applyRegionFilter()
{
    const QString filter = mRegionFilterEdit->text().trimmed();
    for (int i = 0; i < mRegionList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = mRegionList->topLevelItem(i);
        item->setHidden(!filter.isEmpty() &&
                        !item->text(0).contains(filter, Qt::CaseInsensitive) &&
                        !item->text(1).contains(filter, Qt::CaseInsensitive));
    }
}
void RegionsDock::regionFilterChanged(const QString &)
{
    applyRegionFilter();
}
void RegionsDock::selectRegion(int index)
{
    if (index < 0 || index >= mRegions.size())
        index = -1;
    mSelectedRegion = index;
    rebuildList();
    rebuildPropertyTable();
    rebuildGraphics();
    updateUi();
}
void RegionsDock::regionSelectionChanged()
{
    if (mUpdatingUi)
        return;
    QTreeWidgetItem *item = mRegionList->currentItem();
    selectRegion(item ? item->data(0, RegionIndexRole).toInt() : -1);
}
void RegionsDock::beginSnapshot()
{
    mSnapshotBefore = mRegions;
    mSnapshotSelection = mSelectedRegion;
}
void RegionsDock::commitSnapshot(const QString &text)
{
    if (mSnapshotBefore == mRegions) {
        mSnapshotBefore.clear();
        return;
    }
    mUndoStack->push(new RegionsSnapshotCommand(
                         this, mSnapshotBefore, mSnapshotSelection,
                         mRegions, mSelectedRegion, text));
    mSnapshotBefore.clear();
}
void RegionsDock::applySnapshot(const QVector<RegionRecord> &regions,
                                int selectedRegion)
{
    mRegions = regions;
    mSelectedRegion = selectedRegion;
    if (mSelectedRegion >= mRegions.size())
        mSelectedRegion = mRegions.size() - 1;
    rebuildList();
    rebuildPropertyTable();
    rebuildGraphics();
    updateUi();
}
void RegionsDock::selectedCellBounds(int *x, int *y,
                                     int *width, int *height) const
{
    if (!mWorldDocument)
        return;
    QList<WorldCell *> selected = mWorldDocument->selectedCells();
    if (selected.isEmpty()) {
        if (CellScene *cellScene = mScene ? mScene->asCellScene() : nullptr)
            selected.append(cellScene->cell());
    }
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    if (!selected.isEmpty()) {
        minX = maxX = selected.first()->x();
        minY = maxY = selected.first()->y();
        for (WorldCell *cell : selected) {
            minX = qMin(minX, cell->x());
            minY = qMin(minY, cell->y());
            maxX = qMax(maxX, cell->x());
            maxY = qMax(maxY, cell->y());
        }
    }
    const int cellSize = mWorldDocument->world()->cellSize();
    const QPoint origin =
            mWorldDocument->world()->getGenerateLotsSettings().worldOrigin;
    if (x)
        *x = (minX + origin.x()) * cellSize;
    if (y)
        *y = (minY + origin.y()) * cellSize;
    if (width)
        *width = (maxX - minX + 1) * cellSize;
    if (height)
        *height = (maxY - minY + 1) * cellSize;
}
void RegionsDock::addRegion()
{
    if (!mWorldDocument)
        return;
    beginSnapshot();
    RegionRecord region;
    region.name = tr("Region");
    selectedCellBounds(&region.x, &region.y,
                       &region.width, &region.height);
    mRegions.append(region);
    mSelectedRegion = mRegions.size() - 1;
    commitSnapshot(tr("Add region"));
}
void RegionsDock::duplicateRegion()
{
    if (mSelectedRegion < 0 || mSelectedRegion >= mRegions.size())
        return;
    beginSnapshot();
    RegionRecord copy = mRegions.at(mSelectedRegion);
    copy.x += 1;
    copy.y += 1;
    mRegions.insert(mSelectedRegion + 1, copy);
    ++mSelectedRegion;
    commitSnapshot(tr("Duplicate region"));
}
void RegionsDock::removeRegion()
{
    if (mSelectedRegion < 0 || mSelectedRegion >= mRegions.size())
        return;
    beginSnapshot();
    mRegions.removeAt(mSelectedRegion);
    if (mSelectedRegion >= mRegions.size())
        mSelectedRegion = mRegions.size() - 1;
    commitSnapshot(tr("Remove region"));
}
void RegionsDock::applyCellSelection()
{
    if (mSelectedRegion < 0 || mSelectedRegion >= mRegions.size())
        return;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    selectedCellBounds(&x, &y, &width, &height);
    beginSnapshot();
    RegionRecord &region = mRegions[mSelectedRegion];
    region.x = x;
    region.y = y;
    region.width = width;
    region.height = height;
    commitSnapshot(tr("Set region from cell selection"));
}
void RegionsDock::addProperty()
{
    if (mSelectedRegion < 0 || mSelectedRegion >= mRegions.size())
        return;
    QString key = QStringLiteral("Property");
    int suffix = 2;
    const QVector<RegionPropertyRecord> &properties =
            mRegions.at(mSelectedRegion).properties;
    auto containsKey = [&properties](const QString &candidate) {
        for (const RegionPropertyRecord &property : properties) {
            if (property.key == candidate)
                return true;
        }
        return false;
    };
    while (containsKey(key))
        key = QStringLiteral("Property%1").arg(suffix++);
    beginSnapshot();
    RegionPropertyRecord property;
    property.key = key;
    mRegions[mSelectedRegion].properties.append(property);
    commitSnapshot(tr("Add region property"));
    mPropertyTable->selectRow(mPropertyTable->rowCount() - 1);
}
void RegionsDock::removeProperty()
{
    if (mSelectedRegion < 0 || mSelectedRegion >= mRegions.size())
        return;
    const int row = mPropertyTable->currentRow();
    if (row < 0 || row >= mRegions.at(mSelectedRegion).properties.size())
        return;
    beginSnapshot();
    mRegions[mSelectedRegion].properties.removeAt(row);
    commitSnapshot(tr("Remove region property"));
}
void RegionsDock::regionFieldsEditingFinished()
{
    if (mUpdatingUi || mSelectedRegion < 0 ||
            mSelectedRegion >= mRegions.size()) {
        return;
    }
    RegionRecord edited = mRegions.at(mSelectedRegion);
    edited.name = mNameEdit->text();
    edited.type = mTypeCombo->currentText().trimmed();
    edited.x = mXSpinBox->value();
    edited.y = mYSpinBox->value();
    edited.z = mZSpinBox->value();
    edited.width = mWidthSpinBox->value();
    edited.height = mHeightSpinBox->value();
    if (edited == mRegions.at(mSelectedRegion))
        return;
    beginSnapshot();
    mRegions[mSelectedRegion] = edited;
    commitSnapshot(tr("Edit region"));
}
void RegionsDock::propertyItemChanged()
{
    if (mUpdatingUi || mSelectedRegion < 0 ||
            mSelectedRegion >= mRegions.size()) {
        return;
    }
    const int row = mPropertyTable->currentRow();
    if (row < 0 || row >= mRegions.at(mSelectedRegion).properties.size())
        return;
    RegionPropertyRecord edited =
            mRegions.at(mSelectedRegion).properties.at(row);
    if (mPropertyTable->item(row, 0))
        edited.key = mPropertyTable->item(row, 0)->text().trimmed();
    if (mPropertyTable->item(row, 1))
        edited.type = mPropertyTable->item(row, 1)->text().trimmed();
    if (mPropertyTable->item(row, 2))
        edited.value = mPropertyTable->item(row, 2)->text();
    if (edited == mRegions.at(mSelectedRegion).properties.at(row))
        return;
    beginSnapshot();
    mRegions[mSelectedRegion].properties[row] = edited;
    commitSnapshot(tr("Edit region property"));
}
void RegionsDock::showRegionsChanged(bool visible)
{
    QSettings().setValue(QStringLiteral("Regions/Visible"), visible);
    rebuildGraphics();
}
void RegionsDock::updateUi()
{
    const bool hasDocument = mWorldDocument != nullptr;
    const bool hasSelection = mSelectedRegion >= 0 &&
            mSelectedRegion < mRegions.size();
    mFileNameEdit->setEnabled(hasDocument);
    mBrowseButton->setEnabled(hasDocument);
    mLoadButton->setEnabled(hasDocument);
    mSaveButton->setEnabled(hasDocument);
    mUndoButton->setEnabled(hasDocument && mUndoStack->canUndo());
    mRedoButton->setEnabled(hasDocument && mUndoStack->canRedo());
    mShowRegionsCheckBox->setEnabled(hasDocument);
    mRegionFilterEdit->setEnabled(hasDocument);
    mRegionList->setEnabled(hasDocument);
    mAddButton->setEnabled(hasDocument);
    mDuplicateButton->setEnabled(hasSelection);
    mRemoveButton->setEnabled(hasSelection);
    mUseSelectionButton->setEnabled(hasSelection && hasDocument);
    mNameEdit->setEnabled(hasSelection);
    mTypeCombo->setEnabled(hasSelection);
    mXSpinBox->setEnabled(hasSelection);
    mYSpinBox->setEnabled(hasSelection);
    mZSpinBox->setEnabled(hasSelection);
    mWidthSpinBox->setEnabled(hasSelection);
    mHeightSpinBox->setEnabled(hasSelection);
    mPropertyTable->setEnabled(hasSelection);
    mAddPropertyButton->setEnabled(hasSelection);
    mRemovePropertyButton->setEnabled(
                hasSelection && mPropertyTable->currentRow() >= 0);
    mUpdatingUi = true;
    if (hasSelection) {
        const RegionRecord &region = mRegions.at(mSelectedRegion);
        mNameEdit->setText(region.name);
        mTypeCombo->setCurrentText(region.type);
        mXSpinBox->setValue(region.x);
        mYSpinBox->setValue(region.y);
        mZSpinBox->setValue(region.z);
        mWidthSpinBox->setValue(region.width);
        mHeightSpinBox->setValue(region.height);
    } else {
        mNameEdit->clear();
        mTypeCombo->setCurrentText(QStringLiteral("Region"));
        mXSpinBox->setValue(0);
        mYSpinBox->setValue(0);
        mZSpinBox->setValue(0);
        mWidthSpinBox->setValue(1);
        mHeightSpinBox->setValue(1);
    }
    mUpdatingUi = false;
    const QString state = !hasUnsavedChanges()
            ? tr("saved") : tr("modified");
    mStatusLabel->setText(tr("%1 region(s), %2. Duplicate names and "
                             "overlapping rectangles are allowed.")
                          .arg(mRegions.size()).arg(state));
}
QPointF RegionsDock::sceneToWorld(const QPointF &scenePoint) const
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
        return localTiles +
                QPointF((cellScene->cell()->x() + origin.x()) * cellSize,
                        (cellScene->cell()->y() + origin.y()) * cellSize);
    }
    return QPointF();
}
QPointF RegionsDock::worldToScene(const QPointF &worldPoint) const
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
        const QPointF localTiles = worldPoint -
                QPointF((cellScene->cell()->x() + origin.x()) * cellSize,
                        (cellScene->cell()->y() + origin.y()) * cellSize);
        return cellScene->renderer()->tileToPixelCoords(localTiles);
    }
    return QPointF();
}
