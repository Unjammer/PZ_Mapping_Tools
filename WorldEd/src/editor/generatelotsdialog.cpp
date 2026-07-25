#include "generatelotsdialog.h"
#include "ui_generatelotsdialog.h"

#include "preferences.h"
#include "world.h"
#include "worlddocument.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>

static const QString KEY_EXPORT_DIRECTORIES = QStringLiteral("GenerateLotsDialog/ExportDirectories");
static const QString KEY_SPAWNMAP_DIRECTORIES = QStringLiteral("GenerateLotsDialog/SpawnMapDirectories");
static const QString KEY_TILEDEF_DIRECTORIES = QStringLiteral("GenerateLotsDialog/TileDefDirectories");
static const QString KEY_MOD_ROOT = QStringLiteral("GenerateLotsDialog/ModRoot");
static const QString KEY_MOD_ID = QStringLiteral("GenerateLotsDialog/ModId");
static const QString KEY_MOD_NAME = QStringLiteral("GenerateLotsDialog/ModName");
static const QString KEY_MAP_NAME = QStringLiteral("GenerateLotsDialog/MapName");
static const QString KEY_MOD_POSTER = QStringLiteral("GenerateLotsDialog/ModPoster");
static const QString KEY_MOD_42 = QStringLiteral("GenerateLotsDialog/Mod42");

static bool writeTextFileIfMissing(const QString &fileName, const QString &text,
                                   QString *error)
{
    if (QFileInfo::exists(fileName))
        return true;

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << text;
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

GenerateLotsDialog::GenerateLotsDialog(WorldDocument *worldDoc, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GenerateLotsDialog),
    mWorldDoc(worldDoc)
{
    ui->setupUi(this);

    const GenerateLotsSettings &settings = mWorldDoc->world()->getGenerateLotsSettings();

    QSettings qSettings;
    QStringList directories = qSettings.value(KEY_EXPORT_DIRECTORIES).toStringList();
    ui->exportEdit->addItems(directories);

    directories = qSettings.value(KEY_SPAWNMAP_DIRECTORIES).toStringList();
    ui->spawnEdit->addItems(directories);

    directories = qSettings.value(KEY_TILEDEF_DIRECTORIES).toStringList();
    ui->tiledefEdit->addItems(directories);

    // Export directory
    mExportDir = QDir::toNativeSeparators(settings.exportDir);
    if (mExportDir.isEmpty() == false) {
        addComboItemIfAbsent(ui->exportEdit, mExportDir);
        ui->exportEdit->setCurrentText(mExportDir);
    }
    connect(ui->exportEdit, &QComboBox::currentTextChanged, this, &GenerateLotsDialog::exportChanged);
    connect(ui->exportBrowse, &QAbstractButton::clicked, this, &GenerateLotsDialog::exportBrowse);

    // Zombie Spawn Map
    mZombieSpawnMap = QDir::toNativeSeparators(settings.zombieSpawnMap);
    if (mZombieSpawnMap.isEmpty() == false) {
        addComboItemIfAbsent(ui->spawnEdit, mZombieSpawnMap);
        ui->spawnEdit->setCurrentText(mZombieSpawnMap);
    }
    connect(ui->spawnEdit, &QComboBox::currentTextChanged, this, &GenerateLotsDialog::spawnChanged);
    connect(ui->spawnBrowse, &QAbstractButton::clicked, this, &GenerateLotsDialog::spawnBrowse);

    // TileDef folder
    mTileDefFolder = QDir::toNativeSeparators(settings.tileDefFolder);
    if (mTileDefFolder.isEmpty() == false) {
        addComboItemIfAbsent(ui->tiledefEdit, mTileDefFolder);
        ui->tiledefEdit->setCurrentText(mTileDefFolder);
    }
    connect(ui->tiledefEdit, &QComboBox::currentTextChanged, this, &GenerateLotsDialog::tileDefChanged);
    connect(ui->tiledefBrowse, &QAbstractButton::clicked, this, &GenerateLotsDialog::tileDefBrowse);

    // World origin
    ui->xOrigin->setValue(settings.worldOrigin.x());
    ui->yOrigin->setValue(settings.worldOrigin.y());

    // Number of threads
    ui->numThreadsSlider->setMinimum(1);
    ui->numThreadsSlider->setMaximum(10);
    ui->numThreadsSlider->setValue(settings.numberOfThreads);

    ui->modRootEdit->setText(qSettings.value(KEY_MOD_ROOT).toString());
    ui->modIdEdit->setText(qSettings.value(KEY_MOD_ID).toString());
    ui->modNameEdit->setText(qSettings.value(KEY_MOD_NAME).toString());
    ui->mapNameEdit->setText(qSettings.value(KEY_MAP_NAME).toString());
    ui->posterEdit->setText(qSettings.value(KEY_MOD_POSTER).toString());
    ui->create42Folder->setChecked(qSettings.value(KEY_MOD_42, true).toBool());
    connect(ui->modRootBrowse, &QAbstractButton::clicked,
            this, &GenerateLotsDialog::modRootBrowse);
    connect(ui->posterBrowse, &QAbstractButton::clicked,
            this, &GenerateLotsDialog::posterBrowse);
    connect(ui->exportModGroup, &QGroupBox::toggled,
            this, &GenerateLotsDialog::modExportToggled);
    modExportToggled(ui->exportModGroup->isChecked());

    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QAbstractButton::clicked,
            this, &GenerateLotsDialog::apply);
}

GenerateLotsDialog::~GenerateLotsDialog()
{
    delete ui;
}

bool GenerateLotsDialog::exportAsMod() const
{
    return ui->exportModGroup->isChecked();
}

void GenerateLotsDialog::setExportAsMod(bool enabled)
{
    ui->exportModGroup->setChecked(enabled);
    if (enabled)
        ui->exportModGroup->setFocus();
}

void GenerateLotsDialog::setModExportAvailable(bool available)
{
    ui->exportModGroup->setChecked(false);
    ui->exportModGroup->setVisible(available);
}

QString GenerateLotsDialog::modDirectory() const
{
    return QDir(ui->modRootEdit->text().trimmed())
            .filePath(ui->modIdEdit->text().trimmed());
}

QString GenerateLotsDialog::modMapDirectory() const
{
    return QDir(modDirectory()).filePath(
                QLatin1String("common/media/maps/") + ui->mapNameEdit->text().trimmed());
}

bool GenerateLotsDialog::prepareModExport(QString *error)
{
    const QString mapDirectory = modMapDirectory();
    if (!QDir().mkpath(mapDirectory)) {
        if (error)
            *error = tr("Could not create the mod map directory:\n%1")
                    .arg(QDir::toNativeSeparators(mapDirectory));
        return false;
    }
    if (ui->create42Folder->isChecked() &&
            !QDir().mkpath(QDir(modDirectory()).filePath(QLatin1String("42.0")))) {
        if (error)
            *error = tr("Could not create the 42.0 metadata directory.");
        return false;
    }
    mExportDir = QDir::toNativeSeparators(mapDirectory);
    return true;
}

bool GenerateLotsDialog::finalizeModExport(QString *error) const
{
    if (!exportAsMod())
        return true;

    QString modInfo = QStringLiteral("name=%1\nid=%2\ndescription=Map exported by WorldEd\n")
            .arg(ui->modNameEdit->text().trimmed(), ui->modIdEdit->text().trimmed());
    if (!ui->posterEdit->text().trimmed().isEmpty())
        modInfo += QStringLiteral("poster=poster.png\n");

    QStringList metadataDirectories;
    metadataDirectories << QDir(modDirectory()).filePath(QLatin1String("common"));
    if (ui->create42Folder->isChecked())
        metadataDirectories << QDir(modDirectory()).filePath(QLatin1String("42.0"));

    for (const QString &directory : std::as_const(metadataDirectories)) {
        if (!QDir().mkpath(directory)) {
            if (error)
                *error = tr("Could not create directory:\n%1")
                        .arg(QDir::toNativeSeparators(directory));
            return false;
        }
        if (!writeTextFileIfMissing(QDir(directory).filePath(QLatin1String("mod.info")),
                                    modInfo, error))
            return false;

        const QString poster = ui->posterEdit->text().trimmed();
        const QString posterTarget = QDir(directory).filePath(QLatin1String("poster.png"));
        if (!poster.isEmpty() && !QFileInfo::exists(posterTarget) &&
                !QFile::copy(poster, posterTarget)) {
            if (error)
                *error = tr("Could not copy poster.png to:\n%1")
                        .arg(QDir::toNativeSeparators(directory));
            return false;
        }
    }

    const QString mapInfoPath = QDir(modMapDirectory()).filePath(QLatin1String("map.info"));
    return writeTextFileIfMissing(mapInfoPath,
            QStringLiteral("title=%1\n").arg(ui->mapNameEdit->text().trimmed()), error);
}

void GenerateLotsDialog::exportBrowse()
{
    QString f = QFileDialog::getExistingDirectory(this, tr("Choose the .lot Folder"),
        ui->exportEdit->currentText());
    if (!f.isEmpty()) {
        mExportDir = QDir::toNativeSeparators(f);
        addComboItemIfAbsent(ui->exportEdit, mExportDir);
        ui->exportEdit->setCurrentText(mExportDir);
    }
}

void GenerateLotsDialog::exportChanged(const QString &text)
{
    mExportDir = text;
}

void GenerateLotsDialog::spawnChanged(const QString &text)
{
    mZombieSpawnMap = text;
}

void GenerateLotsDialog::tileDefChanged(const QString &text)
{
    mTileDefFolder = text;
}

void GenerateLotsDialog::spawnBrowse()
{
    QStringList formats;
    foreach (QByteArray format, QImageReader::supportedImageFormats())
        if (format.toLower() == format)
            formats.append(QString::fromUtf8(QByteArray("*." + format)));
    QString formatString = tr("Image files (%1)").arg(formats.join(QLatin1String(" ")));
    formatString += tr(";;All files (*.*)");

    QString initialDir = QFileInfo(mWorldDoc->fileName()).absolutePath();
    if (QFileInfo(mZombieSpawnMap).exists())
        initialDir = QFileInfo(mZombieSpawnMap).absolutePath();

    QString f = QFileDialog::getOpenFileName(this, tr("Choose the Zombie Spawn Map image"),
        initialDir, formatString);
    if (!f.isEmpty()) {
        mZombieSpawnMap = QDir::toNativeSeparators(f);
        addComboItemIfAbsent(ui->spawnEdit, mZombieSpawnMap);
        ui->spawnEdit->setCurrentText(mZombieSpawnMap);
    }
}

void GenerateLotsDialog::tileDefBrowse()
{
    QString f = QFileDialog::getExistingDirectory(this, tr("Choose the .tiles Folder"),
        ui->tiledefEdit->currentText());
    if (!f.isEmpty()) {
        mTileDefFolder = QDir::toNativeSeparators(f);
        addComboItemIfAbsent(ui->tiledefEdit, mTileDefFolder);
        ui->tiledefEdit->setCurrentText(mTileDefFolder);
    }
}

void GenerateLotsDialog::modRootBrowse()
{
    const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Choose the Project Zomboid mods directory"),
                ui->modRootEdit->text());
    if (!directory.isEmpty())
        ui->modRootEdit->setText(QDir::toNativeSeparators(directory));
}

void GenerateLotsDialog::posterBrowse()
{
    const QString fileName = QFileDialog::getOpenFileName(
                this, tr("Choose poster.png"), ui->posterEdit->text(),
                tr("PNG images (*.png)"));
    if (!fileName.isEmpty())
        ui->posterEdit->setText(QDir::toNativeSeparators(fileName));
}

void GenerateLotsDialog::modExportToggled(bool enabled)
{
    ui->groupBox->setEnabled(!enabled);
}

void GenerateLotsDialog::accept()
{
    if (!validate())
        return;

    QString error;
    if (exportAsMod() && !prepareModExport(&error)) {
        QMessageBox::warning(this, tr("Mod Export Error"), error);
        return;
    }

    GenerateLotsSettings settings;
    settings.exportDir = mExportDir;
    settings.zombieSpawnMap = mZombieSpawnMap;
    settings.tileDefFolder = mTileDefFolder;
    settings.worldOrigin = QPoint(ui->xOrigin->value(), ui->yOrigin->value());
    settings.numberOfThreads = ui->numThreadsSlider->value();
    if (settings != mWorldDoc->world()->getGenerateLotsSettings())
        mWorldDoc->changeGenerateLotsSettings(settings);

    QSettings qSettings;
    qSettings.setValue(KEY_EXPORT_DIRECTORIES, comboboxStringList(ui->exportEdit));
    qSettings.setValue(KEY_SPAWNMAP_DIRECTORIES, comboboxStringList(ui->spawnEdit));
    qSettings.setValue(KEY_TILEDEF_DIRECTORIES, comboboxStringList(ui->tiledefEdit));
    qSettings.setValue(KEY_MOD_ROOT, ui->modRootEdit->text().trimmed());
    qSettings.setValue(KEY_MOD_ID, ui->modIdEdit->text().trimmed());
    qSettings.setValue(KEY_MOD_NAME, ui->modNameEdit->text().trimmed());
    qSettings.setValue(KEY_MAP_NAME, ui->mapNameEdit->text().trimmed());
    qSettings.setValue(KEY_MOD_POSTER, ui->posterEdit->text().trimmed());
    qSettings.setValue(KEY_MOD_42, ui->create42Folder->isChecked());

    QDialog::accept();
}

void GenerateLotsDialog::apply()
{
    if (!validate())
        return;

    QString error;
    if (exportAsMod() && !prepareModExport(&error)) {
        QMessageBox::warning(this, tr("Mod Export Error"), error);
        return;
    }

    GenerateLotsSettings settings;
    settings.exportDir = mExportDir;
    settings.zombieSpawnMap = mZombieSpawnMap;
    settings.tileDefFolder = mTileDefFolder;
    settings.worldOrigin = QPoint(ui->xOrigin->value(), ui->yOrigin->value());
    settings.numberOfThreads = ui->numThreadsSlider->value();
    if (settings != mWorldDoc->world()->getGenerateLotsSettings())
        mWorldDoc->changeGenerateLotsSettings(settings);

    QSettings qSettings;
    qSettings.setValue(KEY_EXPORT_DIRECTORIES, comboboxStringList(ui->exportEdit));
    qSettings.setValue(KEY_SPAWNMAP_DIRECTORIES, comboboxStringList(ui->spawnEdit));
    qSettings.setValue(KEY_TILEDEF_DIRECTORIES, comboboxStringList(ui->tiledefEdit));

    QDialog::reject();
}

void GenerateLotsDialog::addComboItemIfAbsent(QComboBox *comboBox, const QString &text)
{
    int index = comboBox->findText(text);
    if (index == -1) {
        comboBox->insertItem(0, text);
    }
}

QStringList GenerateLotsDialog::comboboxStringList(QComboBox *comboBox) const
{
    QStringList items;
    for (int i = 0; i < comboBox->count(); i++) {
        items << comboBox->itemText(i);
    }
    return items;
}

bool GenerateLotsDialog::validate()
{
    QDir dir(mExportDir);
    if (!exportAsMod() && (mExportDir.isEmpty() || !dir.exists())) {
        QMessageBox::warning(this, tr("Lot Generation Error"),
                             tr("Please choose a valid directory to save the .lot files in."));
        return false;
    }
    if (exportAsMod()) {
        const QString root = ui->modRootEdit->text().trimmed();
        const QString modId = ui->modIdEdit->text().trimmed();
        const QString modName = ui->modNameEdit->text().trimmed();
        const QString mapName = ui->mapNameEdit->text().trimmed();
        if (root.isEmpty() || !QDir(root).exists()) {
            QMessageBox::warning(this, tr("Invalid Mod Directory"),
                                 tr("Choose an existing parent directory for the mod."));
            return false;
        }
        const QRegularExpression invalidName(QStringLiteral("[\\\\/:*?\"<>|]"));
        if (modId.isEmpty() || mapName.isEmpty() || modName.isEmpty() ||
                modId.contains(invalidName) || mapName.contains(invalidName) ||
                modId == QLatin1String(".") || modId == QLatin1String("..") ||
                mapName == QLatin1String(".") || mapName == QLatin1String("..")) {
            QMessageBox::warning(this, tr("Invalid Mod Information"),
                                 tr("Enter a mod name, a valid mod ID, and a valid map folder name."));
            return false;
        }
        const QString poster = ui->posterEdit->text().trimmed();
        if (!poster.isEmpty() && (!QFileInfo::exists(poster) ||
                                  QFileInfo(poster).suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) != 0)) {
            QMessageBox::warning(this, tr("Invalid Poster"),
                                 tr("The poster must be an existing PNG image."));
            return false;
        }
    }
    QFileInfo info(mZombieSpawnMap);
    if (mZombieSpawnMap.isEmpty() || !info.exists()) {
        QMessageBox::warning(this, tr("Lot Generation Error"),
                             tr("Please choose a Zombie Spawn Map image file."));
        return false;
    }
    QDir dir2(mTileDefFolder);
    if (mTileDefFolder.isEmpty() || !dir2.exists() ||
            !QFileInfo(mTileDefFolder + QLatin1String("/newtiledefinitions.tiles")).exists()) {
        QMessageBox::warning(this, tr("Lot Generation Error"),
                             tr("Please choose the directory containing newtiledefinitions.tiles."));
        return false;
    }

    return true;
}
