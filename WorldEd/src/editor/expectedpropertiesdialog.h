/*
 * Copyright 2026, Alree / Unjammer
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

#ifndef EXPECTEDPROPERTIESDIALOG_H
#define EXPECTEDPROPERTIESDIALOG_H

#include <QDialog>
#include <QVector>

class PropertyDef;
class QWidget;
class WorldCellObject;
class WorldDocument;

class ExpectedPropertiesDialog : public QDialog
{
public:
    static bool canEdit(WorldCellObject *object);
    static bool edit(WorldDocument *worldDocument,
                     WorldCellObject *object, QWidget *parent);

protected:
    void accept() override;

private:
    enum EditorKind {
        TextEditor,
        BooleanEditor,
        IntegerEditor,
        NumberEditor,
        EnumEditor,
        MultiEnumEditor
    };

    struct Field {
        PropertyDef *definition;
        QWidget *editor;
        EditorKind kind;
    };

    ExpectedPropertiesDialog(WorldDocument *worldDocument,
                             WorldCellObject *object, QWidget *parent);

    QString fieldValue(const Field &field) const;
    void setFieldValue(const Field &field, const QString &value);
    void restoreDefaults();
    void applyChanges();

    WorldDocument *mWorldDocument;
    WorldCellObject *mObject;
    QVector<Field> mFields;
};

#endif
