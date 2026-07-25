/*
 * newtilesetdialog.cpp
 * Copyright 2009-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
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

#include "newtilesetdialog.h"
#include "ui_newtilesetdialog.h"

#include "preferences.h"
#include "tileset.h"
#ifdef ZOMBOID
#include "tilesetmanager.h"
#endif
#include "utils.h"

#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>

#include <memory>

static const char * const COLOR_ENABLED_KEY = "Tileset/UseTransparentColor";
static const char * const COLOR_KEY = "Tileset/TransparentColor";
static const char * const SPACING_KEY = "Tileset/Spacing";
static const char * const MARGIN_KEY = "Tileset/Margin";
static const char * const OFFSET_KEY = "Tileset/Offset";

using namespace Tiled;
using namespace Tiled::Internal;

#ifdef ZOMBOID
namespace {

struct PZTilesetSources
{
    QString source1x;
    QString source2x;
    QSize size1x;
    QSize size2x;
};

PZTilesetSources pzTilesetSources(const QString &selectedPath)
{
    PZTilesetSources sources;
    const QFileInfo selected(selectedPath);
    if (!selected.exists())
        return sources;

    if (selected.dir().dirName().compare(QLatin1String("2x"), Qt::CaseInsensitive) == 0) {
        sources.source2x = selected.absoluteFilePath();
        QDir parent = selected.dir();
        parent.cdUp();
        sources.source1x = parent.filePath(selected.fileName());
    } else {
        sources.source1x = selected.absoluteFilePath();
        sources.source2x = selected.dir().filePath(
                    QLatin1String("2x/") + selected.fileName());
    }

    sources.size1x = QImageReader(sources.source1x).size();
    sources.size2x = QImageReader(sources.source2x).size();
    if (!sources.size1x.isValid())
        sources.source1x.clear();
    if (!sources.size2x.isValid())
        sources.source2x.clear();
    return sources;
}

bool validatePZTileset(const QString &name, const QString &selectedPath,
                       QString *description, QString *error)
{
    const QSize tileSize = Tiled::getZomboidTilesetSize1x(name);
    const PZTilesetSources sources = pzTilesetSources(selectedPath);
    if (sources.source1x.isEmpty() && sources.source2x.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("NewTilesetDialog", "The PNG image could not be read.");
        return false;
    }

    if (!sources.source1x.isEmpty() &&
            ((sources.size1x.width() % tileSize.width()) != 0 ||
             (sources.size1x.height() % tileSize.height()) != 0)) {
        if (error)
            *error = QCoreApplication::translate("NewTilesetDialog",
                    "The 1x image dimensions (%1 x %2) are not multiples of the PZ tile size (%3 x %4).")
                    .arg(sources.size1x.width()).arg(sources.size1x.height())
                    .arg(tileSize.width()).arg(tileSize.height());
        return false;
    }

    if (!sources.source2x.isEmpty() &&
            ((sources.size2x.width() % (tileSize.width() * 2)) != 0 ||
             (sources.size2x.height() % (tileSize.height() * 2)) != 0)) {
        if (error)
            *error = QCoreApplication::translate("NewTilesetDialog",
                    "The 2x image dimensions (%1 x %2) are not multiples of the PZ 2x tile size (%3 x %4).")
                    .arg(sources.size2x.width()).arg(sources.size2x.height())
                    .arg(tileSize.width() * 2).arg(tileSize.height() * 2);
        return false;
    }

    if (!sources.source1x.isEmpty() && !sources.source2x.isEmpty() &&
            sources.size2x != sources.size1x * 2) {
        if (error)
            *error = QCoreApplication::translate("NewTilesetDialog",
                    "The 2x companion must be exactly twice the width and height of the 1x image.");
        return false;
    }

    const QSize logicalImageSize = !sources.source1x.isEmpty()
            ? sources.size1x : sources.size2x / 2;
    const int columns = logicalImageSize.width() / tileSize.width();
    const int rows = logicalImageSize.height() / tileSize.height();
    if (description) {
        *description = QCoreApplication::translate("NewTilesetDialog",
                "PZ grid: %1 x %2 tiles, %3 x %4 px per tile (%5 tiles)%6")
                .arg(columns).arg(rows).arg(tileSize.width()).arg(tileSize.height())
                .arg(columns * rows)
                .arg(sources.source2x.isEmpty()
                     ? QString() : QCoreApplication::translate("NewTilesetDialog", ", 2x companion detected"));
    }
    return true;
}

} // namespace
#endif

NewTilesetDialog::NewTilesetDialog(const QString &path, QWidget *parent) :
    QDialog(parent),
    mPath(path),
    mUi(new Ui::NewTilesetDialog),
    mNameWasEdited(false),
    mNewTileset(0)
{
    mUi->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Restore previously used settings
    QSettings *s = Preferences::instance()->settings();
    bool colorEnabled = s->value(QLatin1String(COLOR_ENABLED_KEY)).toBool();
    QString colorName = s->value(QLatin1String(COLOR_KEY)).toString();
    QColor color = colorName.isEmpty() ? Qt::magenta : QColor(colorName);
    int spacing = s->value(QLatin1String(SPACING_KEY)).toInt();
    int margin = s->value(QLatin1String(MARGIN_KEY)).toInt();
    QPoint offset = s->value(QLatin1String(OFFSET_KEY)).toPoint();

    mUi->useTransparentColor->setChecked(colorEnabled);
    mUi->colorButton->setColor(color);
    mUi->spacing->setValue(spacing);
    mUi->margin->setValue(margin);
    mUi->offsetX->setValue(offset.x());
    mUi->offsetY->setValue(offset.y());

    connect(mUi->browseButton, &QAbstractButton::clicked, this, &NewTilesetDialog::browse);
    connect(mUi->name, &QLineEdit::textEdited, this, &NewTilesetDialog::nameEdited);
    connect(mUi->name, &QLineEdit::textChanged, this, &NewTilesetDialog::updateOkButton);
    connect(mUi->image, &QLineEdit::textChanged, this, &NewTilesetDialog::updateOkButton);
#ifdef ZOMBOID
    connect(mUi->projectZomboidMode, &QCheckBox::toggled,
            this, &NewTilesetDialog::projectZomboidModeChanged);
    connect(mUi->name, &QLineEdit::textChanged,
            this, &NewTilesetDialog::updateProjectZomboidInfo);
    connect(mUi->image, &QLineEdit::textChanged,
            this, &NewTilesetDialog::updateProjectZomboidInfo);
#else
    mUi->projectZomboidMode->hide();
    mUi->projectZomboidInfo->hide();
#endif

    // Set the image and name fields if the given path is a file
    const QFileInfo fileInfo(path);
    if (fileInfo.isFile()) {
        mUi->image->setText(path);
        mUi->name->setText(fileInfo.completeBaseName());
    }

    projectZomboidModeChanged(mUi->projectZomboidMode->isChecked());
    updateOkButton();
}

NewTilesetDialog::~NewTilesetDialog()
{
    delete mUi;
}

void NewTilesetDialog::setTileWidth(int width)
{
    mUi->tileWidth->setValue(width);
}

void NewTilesetDialog::setTileHeight(int height)
{
    mUi->tileHeight->setValue(height);
}

Tileset *NewTilesetDialog::createTileset()
{
    if (exec() != QDialog::Accepted)
        return 0;

    return mNewTileset;
}

void NewTilesetDialog::tryAccept()
{
    const QString name = mUi->name->text();
    const QString image = mUi->image->text();
    const bool projectZomboidMode = mUi->projectZomboidMode->isChecked();
    const bool useTransparentColor = !projectZomboidMode && mUi->useTransparentColor->isChecked();
    const QColor transparentColor = mUi->colorButton->color();
    int tileWidth = mUi->tileWidth->value();
    int tileHeight = mUi->tileHeight->value();
    int spacing = mUi->spacing->value();
    int margin = mUi->margin->value();
    QPoint offset(mUi->offsetX->value(), mUi->offsetY->value());

#ifdef ZOMBOID
    PZTilesetSources pzSources;
    if (projectZomboidMode) {
        QString description;
        QString error;
        if (!validatePZTileset(name, image, &description, &error)) {
            QMessageBox::critical(this, tr("Invalid Project Zomboid Tileset"), error);
            return;
        }
        pzSources = pzTilesetSources(image);
        const QSize pzTileSize = Tiled::getZomboidTilesetSize1x(name);
        tileWidth = pzTileSize.width();
        tileHeight = pzTileSize.height();
        spacing = 0;
        margin = 0;
    }
#endif

    std::unique_ptr<Tileset> tileset(new Tileset(name,
                                               tileWidth, tileHeight,
                                               spacing, margin));

    tileset->setTileOffset(offset);

#ifdef ZOMBOID
    if (projectZomboidMode)
        Tiled::setZomboidTileOffset(tileset.get());
#endif

    if (useTransparentColor)
        tileset->setTransparentColor(transparentColor);

#ifdef ZOMBOID
    if (projectZomboidMode) {
        const QString loadPath = !pzSources.source2x.isEmpty()
                ? pzSources.source2x : pzSources.source1x;
        const QString logicalSource = !pzSources.source1x.isEmpty()
                ? pzSources.source1x
                : QFileInfo(pzSources.source2x).dir().absoluteFilePath(
                      QLatin1String("../") + QFileInfo(pzSources.source2x).fileName());
        if (!pzSources.source2x.isEmpty())
            tileset->setImageSource2x(pzSources.source2x);

        TilesetImageCache *imageCache = TilesetManager::instance()->imageCache();
        Tileset *cached = imageCache
                ? imageCache->findMatch(tileset.get(), logicalSource, pzSources.source2x)
                : nullptr;
        if (!cached || !tileset->loadFromCache(cached)) {
            if (!tileset->loadFromImage(QImage(loadPath), logicalSource)) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to load Project Zomboid tileset image '%1'.")
                                      .arg(loadPath));
                return;
            }
            if (imageCache)
                imageCache->addTileset(tileset.get());
        }
        if (cached)
            TilesetManager::instance()->copyPZProperties(cached, tileset.get());
    }
    else
    if (TilesetImageCache *imageCache = TilesetManager::instance()->imageCache()) {
        Tileset *cached = imageCache->findMatch(tileset.get(), image, QString());
        if (!cached || !tileset->loadFromCache(cached)) {
            if (!tileset->loadFromImage(QImage(image), image)) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to load tileset image '%1'.")
                                      .arg(image));
                return;
            }
            imageCache->addTileset(tileset.get());
        }
        TilesetManager::instance()->copyPZProperties(cached, tileset.get());
    }
    else
#endif

    if (!tileset->loadFromImage(QImage(image), image)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to load tileset image '%1'.")
                              .arg(image));
        return;
    }

    if (tileset->tileCount() == 0) {
        QMessageBox::critical(this, tr("Error"),
                              tr("No tiles found in the tileset image when "
                                 "using the given tile size, margin and "
                                 "spacing!"));
        return;
    }

    // Store settings for next time
    QSettings *s = Preferences::instance()->settings();
    s->setValue(QLatin1String(COLOR_ENABLED_KEY), useTransparentColor);
    s->setValue(QLatin1String(COLOR_KEY), transparentColor.name());
    s->setValue(QLatin1String(SPACING_KEY), spacing);
    s->setValue(QLatin1String(MARGIN_KEY), margin);
    s->setValue(QLatin1String(OFFSET_KEY), offset);

    mNewTileset = tileset.release();
    accept();
}

void NewTilesetDialog::browse()
{
    const QString filter = Utils::readableImageFormatsFilter();
    QString f = QFileDialog::getOpenFileName(this, tr("Tileset Image"), mPath,
                                             filter);
    if (!f.isEmpty()) {
        mUi->image->setText(f);
        mPath = f;

        if (!mNameWasEdited)
            mUi->name->setText(QFileInfo(f).completeBaseName());
    }
}

void NewTilesetDialog::nameEdited(const QString &name)
{
    mNameWasEdited = !name.isEmpty();
}

void NewTilesetDialog::projectZomboidModeChanged(bool enabled)
{
#ifdef ZOMBOID
    mUi->groupBox->setEnabled(!enabled);
    mUi->groupBox_3->setEnabled(!enabled);
    mUi->useTransparentColor->setEnabled(!enabled);
    mUi->colorButton->setEnabled(!enabled && mUi->useTransparentColor->isChecked());
    updateProjectZomboidInfo();
#else
    Q_UNUSED(enabled)
#endif
}

void NewTilesetDialog::updateProjectZomboidInfo()
{
#ifdef ZOMBOID
    if (!mUi->projectZomboidMode->isChecked()) {
        mUi->projectZomboidInfo->setText(
                    tr("Advanced Tiled tileset parameters are enabled."));
        mUi->projectZomboidInfo->setStyleSheet(QString());
        return;
    }

    QString description;
    QString error;
    const bool valid = validatePZTileset(mUi->name->text().trimmed(),
                                         mUi->image->text().trimmed(),
                                         &description, &error);
    mUi->projectZomboidInfo->setText(valid ? description : error);
    mUi->projectZomboidInfo->setStyleSheet(valid
            ? QString() : QLatin1String("QLabel { color: #c00000; }"));
#endif
}

void NewTilesetDialog::updateOkButton()
{
    QPushButton *okButton = mUi->buttonBox->button(QDialogButtonBox::Ok);
    bool valid = !mUi->name->text().isEmpty() && !mUi->image->text().isEmpty();
#ifdef ZOMBOID
    if (valid && mUi->projectZomboidMode->isChecked())
        valid = validatePZTileset(mUi->name->text().trimmed(),
                                  mUi->image->text().trimmed(), nullptr, nullptr);
#endif
    okButton->setEnabled(valid);
}
