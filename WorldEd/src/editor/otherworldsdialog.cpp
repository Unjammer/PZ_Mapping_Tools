#include "otherworldsdialog.h"

#include "world.h"
#include "worlddocument.h"
#include "worldgeometry.h"
#include "worldreader.h"
#include "worldwriter.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QStyle>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QVBoxLayout>

namespace {

QString gridLabel(WorldGridFormat format)
{
    return format == WorldGridFormat::Native256
            ? QObject::tr("Native 256 x 256")
            : QObject::tr("Legacy 300 x 300");
}

QString pointLabel(const QPoint &point)
{
    return QObject::tr("%1, %2").arg(point.x()).arg(point.y());
}

QString offsetLabel(const QPoint &point)
{
    return QObject::tr("X %1%2, Y %3%4 cells")
            .arg(point.x() >= 0 ? QLatin1String("+") : QString())
            .arg(point.x())
            .arg(point.y() >= 0 ? QLatin1String("+") : QString())
            .arg(point.y());
}

}

OtherWorldsDialog::OtherWorldsDialog(WorldDocument *worldDoc,
                                     QWidget *parent)
    : QDialog(parent)
    , mWorldDoc(worldDoc)
    , mPaths(worldDoc->world()->otherWorlds())
    , mAppliedPaths(mPaths)
    , mTable(new QTableWidget(this))
    , mSummaryLabel(new QLabel(this))
    , mDetailsLabel(new QLabel(this))
    , mReplaceButton(new QPushButton(tr("Replace..."), this))
    , mRemoveButton(new QPushButton(tr("Remove"), this))
    , mMoveUpButton(new QPushButton(tr("Move Up"), this))
    , mMoveDownButton(new QPushButton(tr("Move Down"), this))
    , mApplyButton(nullptr)
    , mOkButton(nullptr)
{
    setWindowTitle(tr("Linked World Projects"));
    setMinimumSize(820, 520);
    resize(1080, 640);

    QLabel *intro = new QLabel(
                tr("Linked PZW projects are read-only visual references in "
                   "the World view. They are not merged into this project "
                   "and they are not included when lots are generated. "
                   "Placement is calculated from the World origin stored in "
                   "each project's Generate Lots settings."), this);
    intro->setWordWrap(true);

    mSummaryLabel->setWordWrap(true);

    mTable->setColumnCount(6);
    mTable->setHorizontalHeaderLabels(QStringList()
            << tr("Project") << tr("Status") << tr("Grid")
            << tr("World Origin") << tr("Size") << tr("Relative Position"));
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->setAlternatingRowColors(true);
    mTable->verticalHeader()->setVisible(false);
    mTable->horizontalHeader()->setSectionResizeMode(
                0, QHeaderView::Stretch);
    for (int column = 1; column < mTable->columnCount(); ++column) {
        mTable->horizontalHeader()->setSectionResizeMode(
                    column, QHeaderView::ResizeToContents);
    }

    QPushButton *addButton = new QPushButton(tr("Add Project..."), this);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    QGridLayout *actions = new QGridLayout;
    actions->addWidget(addButton, 0, 0);
    actions->addWidget(mReplaceButton, 0, 1);
    actions->addWidget(mRemoveButton, 0, 2);
    actions->addWidget(mMoveUpButton, 0, 3);
    actions->addWidget(mMoveDownButton, 0, 4);
    actions->setColumnStretch(5, 1);
    actions->addWidget(refreshButton, 0, 6);

    QGroupBox *detailsBox = new QGroupBox(tr("Selected Project"), this);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsBox);
    mDetailsLabel->setWordWrap(true);
    mDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailsLayout->addWidget(mDetailsLabel);

    QLabel *placementHelp = new QLabel(
                tr("To move a linked project, open that PZW and change its "
                   "World origin in the Generate Lots dialog. Linked projects "
                   "must use the same cell grid format as the current project."),
                this);
    placementHelp->setWordWrap(true);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Apply
                | QDialogButtonBox::Cancel, this);
    mApplyButton = buttonBox->button(QDialogButtonBox::Apply);
    mOkButton = buttonBox->button(QDialogButtonBox::Ok);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(mSummaryLabel);
    layout->addWidget(mTable, 1);
    layout->addLayout(actions);
    layout->addWidget(detailsBox);
    layout->addWidget(placementHelp);
    layout->addWidget(buttonBox);

    connect(addButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::addProject);
    connect(mReplaceButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::replaceProject);
    connect(mRemoveButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::removeProject);
    connect(mMoveUpButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::moveProjectUp);
    connect(mMoveDownButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::moveProjectDown);
    connect(refreshButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::refreshProjects);
    connect(mTable, &QTableWidget::itemSelectionChanged,
            this, &OtherWorldsDialog::updateSelection);
    connect(mTable, &QTableWidget::cellDoubleClicked,
            this, [this](int, int) { replaceProject(); });
    connect(mApplyButton, &QPushButton::clicked,
            this, &OtherWorldsDialog::applyChanges);
    connect(buttonBox, &QDialogButtonBox::accepted,
            this, &OtherWorldsDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &OtherWorldsDialog::reject);

    updateTable();
}

QString OtherWorldsDialog::normalizedPath(const QString &path) const
{
    QFileInfo info(path);
    if (info.isRelative() && !mWorldDoc->fileName().isEmpty()) {
        info.setFile(QDir(QFileInfo(mWorldDoc->fileName()).absolutePath()),
                     path);
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

QString OtherWorldsDialog::pathIdentity(const QString &path) const
{
    QFileInfo info(normalizedPath(path));
    QString identity = info.canonicalFilePath();
    if (identity.isEmpty())
        identity = info.absoluteFilePath();
    identity = identity.toLower();
    return QDir::cleanPath(identity);
}

bool OtherWorldsDialog::validateCandidate(const QString &path,
                                          int replacedRow,
                                          QString *error) const
{
    const QString normalized = normalizedPath(path);
    const QFileInfo info(normalized);
    if (info.suffix().compare(QLatin1String("pzw"), Qt::CaseInsensitive) != 0) {
        *error = tr("Choose a WorldEd project file with the .pzw extension.");
        return false;
    }
    if (!info.isFile()) {
        *error = tr("The selected PZW file does not exist.");
        return false;
    }

    const QString identity = pathIdentity(normalized);
    if (!mWorldDoc->fileName().isEmpty()
            && identity == pathIdentity(mWorldDoc->fileName())) {
        *error = tr("A project cannot link to itself.");
        return false;
    }
    for (int row = 0; row < mPaths.size(); ++row) {
        if (row != replacedRow && identity == pathIdentity(mPaths.at(row))) {
            *error = tr("This project is already linked.");
            return false;
        }
    }

    WorldReader reader;
    World *linkedWorld = reader.readWorld(normalized);
    if (!linkedWorld) {
        *error = tr("WorldEd could not read this PZW file.\n\n%1")
                .arg(reader.errorString());
        return false;
    }
    const bool compatible = linkedWorld->gridFormat()
            == mWorldDoc->world()->gridFormat();
    const WorldGridFormat linkedFormat = linkedWorld->gridFormat();
    delete linkedWorld;
    if (!compatible) {
        *error = tr("The linked project uses %1, while the current project "
                    "uses %2. Linked projects must use the same cell grid.")
                .arg(gridLabel(linkedFormat),
                     gridLabel(mWorldDoc->world()->gridFormat()));
        return false;
    }
    return true;
}

void OtherWorldsDialog::addProject()
{
    const QString base = mWorldDoc->fileName().isEmpty()
            ? QString() : QFileInfo(mWorldDoc->fileName()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Add Linked World Project"), base,
                tr("WorldEd Projects (*.pzw)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!validateCandidate(path, -1, &error)) {
        QMessageBox::warning(this, tr("Cannot Link Project"), error);
        return;
    }
    mPaths.append(normalizedPath(path));
    updateTable();
    selectRow(mPaths.size() - 1);
}

void OtherWorldsDialog::replaceProject()
{
    const int row = mTable->currentRow();
    if (row < 0 || row >= mPaths.size())
        return;
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Replace Linked World Project"),
                QFileInfo(mPaths.at(row)).absolutePath(),
                tr("WorldEd Projects (*.pzw)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!validateCandidate(path, row, &error)) {
        QMessageBox::warning(this, tr("Cannot Link Project"), error);
        return;
    }
    mPaths[row] = normalizedPath(path);
    updateTable();
    selectRow(row);
}

void OtherWorldsDialog::removeProject()
{
    const int row = mTable->currentRow();
    if (row < 0 || row >= mPaths.size())
        return;
    mPaths.removeAt(row);
    updateTable();
    selectRow(qMin(row, mPaths.size() - 1));
}

void OtherWorldsDialog::moveProjectUp()
{
    const int row = mTable->currentRow();
    if (row <= 0 || row >= mPaths.size())
        return;
    mPaths.swapItemsAt(row, row - 1);
    updateTable();
    selectRow(row - 1);
}

void OtherWorldsDialog::moveProjectDown()
{
    const int row = mTable->currentRow();
    if (row < 0 || row + 1 >= mPaths.size())
        return;
    mPaths.swapItemsAt(row, row + 1);
    updateTable();
    selectRow(row + 1);
}

void OtherWorldsDialog::refreshProjects()
{
    const int row = mTable->currentRow();
    updateTable();
    selectRow(row);
    mWorldDoc->refreshOtherWorlds();
}

bool OtherWorldsDialog::allProjectsValid() const
{
    for (int row = 0; row < mTable->rowCount(); ++row) {
        QTableWidgetItem *item = mTable->item(row, 1);
        if (!item || !item->data(Qt::UserRole).toBool())
            return false;
    }
    return true;
}

void OtherWorldsDialog::updateTable()
{
    const QPoint currentOrigin =
            mWorldDoc->world()->getGenerateLotsSettings().worldOrigin;
    const QRect currentBounds(currentOrigin, mWorldDoc->world()->size());
    const QString currentIdentity = mWorldDoc->fileName().isEmpty()
            ? QString() : pathIdentity(mWorldDoc->fileName());
    mTable->setRowCount(mPaths.size());

    int validCount = 0;
    for (int row = 0; row < mPaths.size(); ++row) {
        const QString path = normalizedPath(mPaths.at(row));
        const QFileInfo fileInfo(path);
        QString status;
        QString grid;
        QString origin;
        QString size;
        QString position;
        QString details = QDir::toNativeSeparators(path);
        bool valid = false;

        if (!fileInfo.isFile()) {
            status = tr("Missing file");
            details += tr("\n\nThe PZW file does not exist.");
        } else if (!currentIdentity.isEmpty()
                   && pathIdentity(path) == currentIdentity) {
            status = tr("Current project");
            details += tr("\n\nA project cannot link to itself.");
        } else {
            bool duplicate = false;
            for (int previous = 0; previous < row; ++previous) {
                if (pathIdentity(path) == pathIdentity(mPaths.at(previous))) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                status = tr("Duplicate");
                details += tr("\n\nThis project appears more than once.");
            } else {
                WorldReader reader;
                World *linkedWorld = reader.readWorld(path);
                if (!linkedWorld) {
                    status = tr("Invalid PZW");
                    details += tr("\n\n%1").arg(reader.errorString());
                } else {
                    const QPoint linkedOrigin = linkedWorld
                            ->getGenerateLotsSettings().worldOrigin;
                    const QRect linkedBounds(linkedOrigin,
                                             linkedWorld->size());
                    grid = gridLabel(linkedWorld->gridFormat());
                    origin = pointLabel(linkedOrigin);
                    size = tr("%1 x %2 cells")
                            .arg(linkedWorld->width())
                            .arg(linkedWorld->height());
                    position = offsetLabel(linkedOrigin - currentOrigin);
                    details += tr("\n\nWorld origin: %1\n"
                                  "Coverage: %2, %3 to %4, %5\n"
                                  "Relative position: %6")
                            .arg(pointLabel(linkedOrigin))
                            .arg(linkedBounds.left())
                            .arg(linkedBounds.top())
                            .arg(linkedBounds.right())
                            .arg(linkedBounds.bottom())
                            .arg(position);
                    if (linkedWorld->gridFormat()
                            != mWorldDoc->world()->gridFormat()) {
                        status = tr("Grid mismatch");
                        details += tr("\n\nCurrent project grid: %1\n"
                                      "Linked project grid: %2")
                                .arg(gridLabel(mWorldDoc->world()->gridFormat()),
                                     grid);
                    } else {
                        valid = true;
                        ++validCount;
                        status = linkedBounds.intersects(currentBounds)
                                ? tr("Ready, overlaps") : tr("Ready");
                        if (linkedBounds.intersects(currentBounds)) {
                            const QRect overlap = linkedBounds
                                    .intersected(currentBounds);
                            details += tr("\nOverlap with current project: "
                                          "%1 x %2 cells")
                                    .arg(overlap.width())
                                    .arg(overlap.height());
                        }
                    }
                    delete linkedWorld;
                }
            }
        }

        QTableWidgetItem *projectItem = new QTableWidgetItem(
                    fileInfo.completeBaseName().isEmpty()
                    ? fileInfo.fileName() : fileInfo.completeBaseName());
        projectItem->setToolTip(QDir::toNativeSeparators(path));
        projectItem->setData(Qt::UserRole, details);
        mTable->setItem(row, 0, projectItem);

        QTableWidgetItem *statusItem = new QTableWidgetItem(status);
        statusItem->setData(Qt::UserRole, valid);
        statusItem->setIcon(style()->standardIcon(
                    valid ? QStyle::SP_DialogApplyButton
                          : QStyle::SP_MessageBoxWarning));
        mTable->setItem(row, 1, statusItem);
        mTable->setItem(row, 2, new QTableWidgetItem(grid));
        mTable->setItem(row, 3, new QTableWidgetItem(origin));
        mTable->setItem(row, 4, new QTableWidgetItem(size));
        mTable->setItem(row, 5, new QTableWidgetItem(position));
    }

    if (mPaths.isEmpty()) {
        mSummaryLabel->setText(
                    tr("No linked world project is configured."));
    } else {
        mSummaryLabel->setText(
                    tr("%1 linked project(s), %2 ready. Current project uses "
                       "%3 with World origin %4.")
                    .arg(mPaths.size())
                    .arg(validCount)
                    .arg(gridLabel(mWorldDoc->world()->gridFormat()))
                    .arg(pointLabel(currentOrigin)));
    }
    const bool valid = allProjectsValid();
    mOkButton->setEnabled(valid);
    mApplyButton->setEnabled(valid && mPaths != mAppliedPaths);
    updateSelection();
}

void OtherWorldsDialog::selectRow(int row)
{
    if (row >= 0 && row < mTable->rowCount())
        mTable->selectRow(row);
    else
        mTable->clearSelection();
}

void OtherWorldsDialog::updateSelection()
{
    const int row = mTable->currentRow();
    const bool selected = row >= 0 && row < mPaths.size();
    mReplaceButton->setEnabled(selected);
    mRemoveButton->setEnabled(selected);
    mMoveUpButton->setEnabled(selected && row > 0);
    mMoveDownButton->setEnabled(selected && row + 1 < mPaths.size());
    if (!selected) {
        mDetailsLabel->setText(tr("Select a linked project to view its path "
                                  "and placement details."));
        return;
    }
    QTableWidgetItem *item = mTable->item(row, 0);
    mDetailsLabel->setText(item
            ? item->data(Qt::UserRole).toString() : QString());
}

void OtherWorldsDialog::applyChanges()
{
    if (!allProjectsValid())
        return;
    mWorldDoc->changeOtherWorlds(mPaths);
    mAppliedPaths = mPaths;
    mApplyButton->setEnabled(false);
}

void OtherWorldsDialog::accept()
{
    if (!allProjectsValid())
        return;
    applyChanges();
    QDialog::accept();
}

bool OtherWorldsDialog::validateWorkflow(QString *summary, QString *error)
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *error = tr("Could not create a temporary validation directory.");
        return false;
    }

    const QString currentPath = temporary.filePath(
                QLatin1String("Current.pzw"));
    const QString linkedPath = temporary.filePath(
                QLatin1String("Linked.pzw"));
    const QString legacyPath = temporary.filePath(
                QLatin1String("Legacy.pzw"));

    World *current = new World(2, 3, WorldGridFormat::Native256);
    GenerateLotsSettings currentSettings = current->getGenerateLotsSettings();
    currentSettings.worldOrigin = QPoint(10, 20);
    current->setGenerateLotsSettings(currentSettings);

    World *linked = new World(4, 5, WorldGridFormat::Native256);
    GenerateLotsSettings linkedSettings = linked->getGenerateLotsSettings();
    linkedSettings.worldOrigin = QPoint(14, 18);
    linked->setGenerateLotsSettings(linkedSettings);

    World *legacy = new World(1, 1, WorldGridFormat::Legacy300);

    WorldWriter writer;
    const bool currentWritten = writer.writeWorld(current, currentPath);
    const bool linkedWritten = writer.writeWorld(linked, linkedPath);
    const bool legacyWritten = writer.writeWorld(legacy, legacyPath);
    delete current;
    delete linked;
    delete legacy;
    if (!currentWritten || !linkedWritten || !legacyWritten) {
        *error = writer.errorString();
        return false;
    }

    WorldReader reader;
    World *loaded = reader.readWorld(currentPath);
    if (!loaded) {
        *error = reader.errorString();
        return false;
    }

    WorldDocument document(loaded, currentPath);
    OtherWorldsDialog emptyDialog(&document);
    QString validationError;
    if (emptyDialog.validateCandidate(currentPath, -1, &validationError)
            || emptyDialog.validateCandidate(legacyPath, -1,
                                             &validationError)) {
        *error = tr("Self-link or mixed-grid validation failed.");
        return false;
    }
    document.changeOtherWorlds(QStringList() << linkedPath);
    if (document.world()->otherWorlds().size() != 1
            || !document.isModified()) {
        *error = tr("Adding a linked project did not modify the document.");
        return false;
    }
    document.undoStack()->undo();
    if (!document.world()->otherWorlds().isEmpty()) {
        *error = tr("Undo did not restore the linked-project list.");
        return false;
    }
    document.undoStack()->redo();
    if (document.world()->otherWorlds().value(0) != linkedPath) {
        *error = tr("Redo did not restore the linked project.");
        return false;
    }
    OtherWorldsDialog dialog(&document);
    if (!dialog.allProjectsValid()) {
        *error = tr("The manager rejected a valid linked project.");
        return false;
    }
    if (dialog.validateCandidate(linkedPath, -1, &validationError)) {
        *error = tr("Duplicate linked-project validation failed.");
        return false;
    }
    int refreshCount = 0;
    connect(&document, &WorldDocument::otherWorldsChanged,
            &dialog, [&refreshCount]() { ++refreshCount; });
    dialog.refreshProjects();
    if (refreshCount != 1) {
        *error = tr("Refresh did not reload the linked projects.");
        return false;
    }
    dialog.show();
    QApplication::processEvents();
    const QPixmap preview = dialog.grab();
    dialog.hide();
    if (dialog.mTable->rowCount() != 1 || preview.isNull()
            || preview.width() < dialog.minimumWidth()
            || preview.height() < dialog.minimumHeight()) {
        *error = tr("The linked-project manager did not render correctly.");
        return false;
    }

    QString saveError;
    if (!document.save(currentPath, saveError)) {
        *error = saveError;
        return false;
    }
    World *roundTrip = reader.readWorld(currentPath);
    if (!roundTrip) {
        *error = reader.errorString();
        return false;
    }
    const QString savedLink = roundTrip->otherWorlds().value(0);
    const bool roundTripValid = QFileInfo(savedLink).canonicalFilePath()
            == QFileInfo(linkedPath).canonicalFilePath();
    delete roundTrip;
    if (!roundTripValid) {
        *error = tr("The linked PZW path did not survive save and reload.");
        return false;
    }

    *summary = tr("add, Undo, Redo, refresh, path and grid validation, "
                  "relative-path save, reload, and dialog rendering passed "
                  "for a Native256 linked project");
    return true;
}
