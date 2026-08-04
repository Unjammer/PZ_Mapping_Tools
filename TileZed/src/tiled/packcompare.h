/*
 * Copyright 2014, Tim Baker <treectrl@users.sf.net>
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

#ifndef PACKCOMPARE_H
#define PACKCOMPARE_H

#include "texturepackfile.h"

#include <QMainWindow>
#include <QVector>

namespace Ui {
class PackCompare;
}

class QLabel;

class PackCompare : public QMainWindow
{
    Q_OBJECT

public:
    explicit PackCompare(QWidget *parent = nullptr);
    ~PackCompare();

    static bool runSelfTest(QString *summary, QString *errorString);
    static bool renderValidation(const QString &outputFile,
                                 QString *errorString);

private slots:
    void browse1();
    void browse2();
    void compare();
    void swapPacks();
    void updateFilter();
    void selectedTextureChanged();
    void exportReport();
    void copyReport();

private:
    enum Status {
        Added,
        Removed,
        ModifiedPixels,
        ModifiedMetadata,
        ModifiedPixelsAndMetadata,
        Duplicate,
        Unchanged
    };

    struct Location {
        int pageIndex = -1;
        int textureIndex = -1;
        QByteArray pixelHash;
        QByteArray metadataHash;
    };

    struct ComparisonRow {
        QString name;
        Status status = Unchanged;
        QVector<Location> pack1;
        QVector<Location> pack2;
    };

    bool loadComparison(const QString &file1, const QString &file2,
                        QString *errorString);
    void buildComparison();
    void rebuildTable();
    bool statusMatchesFilter(Status status) const;
    QString statusText(Status status) const;
    QColor statusColor(Status status) const;
    const PackPage *pageFor(const PackFile &pack,
                            const Location &location) const;
    const PackSubTexInfo *textureFor(const PackFile &pack,
                                     const Location &location) const;
    QImage imageFor(const PackFile &pack,
                    const QVector<Location> &locations) const;
    QString locationDescription(const PackFile &pack,
                                const QVector<Location> &locations) const;
    void setPreview(QLabel *label, const QImage &image,
                    const QString &emptyText);
    QImage differenceImage(const QImage &first,
                           const QImage &second) const;
    QString csvReport() const;
    void updateSummary();

    Ui::PackCompare *ui;
    PackFile mPackFile1;
    PackFile mPackFile2;
    QVector<ComparisonRow> mRows;
};

#endif // PACKCOMPARE_H
