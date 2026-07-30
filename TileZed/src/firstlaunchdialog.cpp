#include "firstlaunchdialog.h"

#include "portablesettings.h"

#include <QDialogButtonBox>
#include <QDebug>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

FirstLaunchDialog::FirstLaunchDialog(QWidget *parent)
    : QDialog(parent)
    , mConfigurationPath(new QLineEdit(this))
    , mTilesPath(new QLineEdit(this))
    , mAcceptButton(nullptr)
{
    setWindowTitle(tr("PZTools Initial Setup"));
    setModal(true);
    resize(680, 240);

    QLabel *introduction = new QLabel(tr(
            "PZTools could not find all required shared directories. "
            "Choose them once here; WorldEd, TileZed and BuildingEd will share "
            "the same paths through settings/PZTools.ini."), this);
    introduction->setWordWrap(true);

    QToolButton *configurationBrowse = new QToolButton(this);
    configurationBrowse->setText(QLatin1String("..."));
    QHBoxLayout *configurationLayout = new QHBoxLayout;
    configurationLayout->addWidget(mConfigurationPath);
    configurationLayout->addWidget(configurationBrowse);

    QToolButton *tilesBrowse = new QToolButton(this);
    tilesBrowse->setText(QLatin1String("..."));
    QHBoxLayout *tilesLayout = new QHBoxLayout;
    tilesLayout->addWidget(mTilesPath);
    tilesLayout->addWidget(tilesBrowse);

    QFormLayout *paths = new QFormLayout;
    paths->addRow(tr("Configuration catalogs:"), configurationLayout);
    paths->addRow(tr("Project Zomboid Tiles:"), tilesLayout);

    QLabel *settingsFile = new QLabel(
                tr("Shared settings: %1")
                .arg(QDir::toNativeSeparators(
                         PortableSettings::sharedSettingsFilePath())), this);
    settingsFile->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QDialogButtonBox *buttons = new QDialogButtonBox(
                QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    mAcceptButton = buttons->button(QDialogButtonBox::Ok);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(introduction);
    layout->addLayout(paths);
    layout->addWidget(settingsFile);
    layout->addStretch();
    layout->addWidget(buttons);

    mConfigurationPath->setText(
                QDir::toNativeSeparators(
                    PortableSettings::sharedConfigurationPath()));
    mTilesPath->setText(
                QDir::toNativeSeparators(
                    PortableSettings::sharedTilesPath()));

    connect(configurationBrowse, &QAbstractButton::clicked,
            this, [this]() { browseConfiguration(); });
    connect(tilesBrowse, &QAbstractButton::clicked,
            this, [this]() { browseTiles(); });
    connect(mConfigurationPath, &QLineEdit::textChanged,
            this, [this]() { updateAcceptButton(); });
    connect(mTilesPath, &QLineEdit::textChanged,
            this, [this]() { updateAcceptButton(); });
    connect(buttons, &QDialogButtonBox::accepted,
            this, [this]() { saveAndAccept(); });
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    updateAcceptButton();
}

bool FirstLaunchDialog::ensureSharedPaths(QWidget *parent)
{
    const QString configuration =
            PortableSettings::sharedConfigurationPath();
    const QString tiles = PortableSettings::sharedTilesPath();
    if (PortableSettings::isConfigurationPath(configuration)
            && PortableSettings::isTilesPath(tiles)) {
        return true;
    }

    qInfo().noquote()
            << "Opening PZTools Initial Setup:"
            << "configuration valid="
            << PortableSettings::isConfigurationPath(configuration)
            << "Tiles valid=" << PortableSettings::isTilesPath(tiles);
    return configureSharedPaths(parent);
}

bool FirstLaunchDialog::configureSharedPaths(QWidget *parent)
{
    FirstLaunchDialog dialog(parent);
    return dialog.exec() == QDialog::Accepted;
}

void FirstLaunchDialog::browseConfiguration()
{
    const QString path = QFileDialog::getExistingDirectory(
                this, tr("Choose the PZTools configuration directory"),
                mConfigurationPath->text());
    if (!path.isEmpty())
        mConfigurationPath->setText(QDir::toNativeSeparators(path));
}

void FirstLaunchDialog::browseTiles()
{
    const QString path = QFileDialog::getExistingDirectory(
                this, tr("Choose the Project Zomboid Tiles directory"),
                mTilesPath->text());
    if (!path.isEmpty())
        mTilesPath->setText(QDir::toNativeSeparators(path));
}

void FirstLaunchDialog::updateAcceptButton()
{
    const bool valid = PortableSettings::isConfigurationPath(
                QDir::cleanPath(mConfigurationPath->text().trimmed()))
            && PortableSettings::isTilesPath(
                QDir::cleanPath(mTilesPath->text().trimmed()));
    mAcceptButton->setEnabled(valid);
}

void FirstLaunchDialog::saveAndAccept()
{
    const QString configuration =
            PortableSettings::normalizedConfigurationPath(
                mConfigurationPath->text().trimmed());
    const QString tiles = PortableSettings::normalizedTilesPath(
                mTilesPath->text().trimmed());
    if (!PortableSettings::isConfigurationPath(configuration)
            || !PortableSettings::isTilesPath(tiles)) {
        QMessageBox::critical(
                    this, tr("Invalid PZTools Paths"),
                    tr("The configuration directory must contain Tilesets.txt, "
                       "TMXConfig.txt and the Building*.txt catalogs.\n\n"
                       "The Tiles directory must contain PNG tilesets directly "
                       "or in a non-empty 1x, 2x or custom directory."));
        return;
    }
    PortableSettings::setSharedConfigurationPath(configuration);
    PortableSettings::setSharedTilesPath(tiles);
    qInfo().noquote() << "Saved shared PZTools paths:"
                      << QDir::toNativeSeparators(configuration)
                      << QDir::toNativeSeparators(tiles);
    accept();
}
