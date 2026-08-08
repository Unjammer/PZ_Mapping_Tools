/*
 * Copyright 2021, Tim Baker <treectrl@users.sf.net>
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

#ifndef INGAMEMAPIMAGEPYRAMIDWINDOW_H
#define INGAMEMAPIMAGEPYRAMIDWINDOW_H

#include <QImage>
#include <QMainWindow>
#include <QRect>

#include <functional>

namespace Ui {
class InGameMapImagePyramidWindow;
}

class QuaZip;

class InGameMapImagePyramidWindow : public QMainWindow
{
    Q_OBJECT

public:
    using LogFunction = std::function<void(const QString &)>;

    explicit InGameMapImagePyramidWindow(QWidget *parent = nullptr);
    ~InGameMapImagePyramidWindow();

    static bool createPyramidZip(
            const QImage &image,
            const QRect &worldBounds,
            const QString &outputFileName,
            QString *error = nullptr,
            const LogFunction &logger = LogFunction());

private slots:
    void chooseInputFile();
    void chooseOutputFile();
    void createZip();

private:
    void log(const QString& str);

    Ui::InGameMapImagePyramidWindow *ui;
};

#endif // INGAMEMAPIMAGEPYRAMIDWINDOW_H
