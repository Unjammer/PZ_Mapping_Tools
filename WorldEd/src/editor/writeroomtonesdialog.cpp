/*
 * Copyright 2024, Tim Baker <treectrl@users.sf.net>
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

#include "writeroomtonesdialog.h"
#include "ui_writeroomtonesdialog.h"

#include "world.h"
#include "worlddocument.h"

#include <QDir>
#include <QFileDialog>

WriteRoomTonesDialog::WriteRoomTonesDialog(WorldDocument *worldDoc, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::WriteRoomTonesDialog),
    mDocument(worldDoc)
{
    ui->setupUi(this);

    mFileName = mDocument->world()->getLuaSettings().roomTonesFile;
    ui->fileName->setText(QDir::toNativeSeparators(mFileName));
    connect(ui->browse, &QAbstractButton::clicked, this, &WriteRoomTonesDialog::browse);
}

WriteRoomTonesDialog::~WriteRoomTonesDialog()
{
    delete ui;
}

void WriteRoomTonesDialog::browse()
{
    QString fileName = mFileName;
    if (fileName.isEmpty() && !mDocument->fileName().isEmpty()) {
        QFileInfo info(mDocument->fileName());
        fileName = info.absolutePath() + QLatin1String("/roomtones.lua");
    }
    QString f = QFileDialog::getSaveFileName(this, tr("Save Room Tones"),
                                             fileName, tr("LUA files (*.lua)"));
    if (f.isEmpty())
        return;

    mFileName = f;
    ui->fileName->setText(QDir::toNativeSeparators(f));
}

void WriteRoomTonesDialog::accept()
{
    LuaSettings settings = mDocument->world()->getLuaSettings();
    settings.roomTonesFile = mFileName;
    if (settings != mDocument->world()->getLuaSettings())
        mDocument->changeLuaSettings(settings);

    QDialog::accept();
}
