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

#include "configdialog.h"
#include "ui_configdialog.h"
#include "../portablesettings.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>

static QString KEY_CONFIG_DIR = QLatin1String("ConfigDirectory");
static QString KEY_TILES_DIR = QLatin1String("Tilesets/TilesDirectory");

ConfigDialog::ConfigDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConfigDialog)
{
    ui->setupUi(this);

    QSettings settings;
    QString configPath = settings.value(
                KEY_CONFIG_DIR,
                PortableSettings::applicationConfigPath()).toString();
    if (QDir::cleanPath(configPath).compare(
                QDir::cleanPath(PortableSettings::rootPath()),
                Qt::CaseInsensitive) == 0) {
        configPath = PortableSettings::applicationConfigPath();
    }
    ui->configDirectory->setText(configPath);
    QString tilesPath = settings.value(KEY_TILES_DIR).toString();
    if (tilesPath.isEmpty() || !QDir(tilesPath).exists())
        tilesPath = PortableSettings::detectTilesPath();
    ui->tilesDirectory->setText(tilesPath);

    connect(ui->configBrowse, &QAbstractButton::clicked, this, &ConfigDialog::configBrowse);
    connect(ui->tilesBrowse, &QAbstractButton::clicked, this, &ConfigDialog::tilesBrowse);
}

ConfigDialog::~ConfigDialog()
{
    delete ui;
}

void ConfigDialog::configBrowse()
{
    const QString path = QFileDialog::getExistingDirectory(
                this, tr("Choose the configuration directory"),
                ui->configDirectory->text());
    if (!path.isEmpty())
        ui->configDirectory->setText(QDir::cleanPath(path));
}

void ConfigDialog::tilesBrowse()
{
    const QString path = QFileDialog::getExistingDirectory(
                this, tr("Choose the Project Zomboid Tiles directory"),
                ui->tilesDirectory->text());
    if (!path.isEmpty())
        ui->tilesDirectory->setText(QDir::cleanPath(path));
}

void ConfigDialog::accept()
{
    const QString configPath = QDir::cleanPath(
                ui->configDirectory->text().trimmed());
    if (configPath.isEmpty() || !QDir(configPath).exists()) {
        QMessageBox::critical(
                    this, tr("Invalid Configuration Directory"),
                    tr("Choose an existing directory containing the PZTools "
                       "configuration catalogs, such as Tilesets.txt and "
                       "BuildingTiles.txt.\n\n%1")
                    .arg(QDir::toNativeSeparators(configPath)));
        return;
    }

    const QString tilesPath = QDir::cleanPath(
                ui->tilesDirectory->text().trimmed());
    if (!tilesPath.isEmpty() && !QDir(tilesPath).exists()) {
        QMessageBox::critical(
                    this, tr("Invalid Tiles Directory"),
                    tr("The selected Project Zomboid Tiles directory does not "
                       "exist:\n%1")
                    .arg(QDir::toNativeSeparators(tilesPath)));
        return;
    }

    QSettings settings;
    settings.setValue(KEY_CONFIG_DIR, configPath);
    settings.setValue(KEY_TILES_DIR, tilesPath);
    settings.sync();
    QDialog::accept();
}
