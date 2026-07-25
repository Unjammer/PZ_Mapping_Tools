#include "unknowncolorsdialog.h"
#include "ui_unknowncolorsdialog.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QListWidgetItem>
#include <QPixmap>

UnknownColorsDialog::UnknownColorsDialog(const QString &path,
                                         const QStringList &colors,
                                         QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UnknownColorsDialog)
{
    ui->setupUi(this);

    ui->prompt->setText(
                tr("%1 contains %2 color value(s) that are not defined in "
                   "Rules.txt. Pixels using them cannot be converted "
                   "reliably.\n\nAdd each color to Rules.txt or repaint the "
                   "listed pixels. Coordinates start at the top-left corner.")
                .arg(QDir::toNativeSeparators(path))
                .arg(colors.size()));

    for (const QString &description : colors) {
        QListWidgetItem *item = new QListWidgetItem(description, ui->list);
        const QString colorName = description.section(QLatin1Char(' '), 0, 0);
        const QColor color(colorName);
        if (color.isValid()) {
            QPixmap swatch(20, 20);
            swatch.fill(color);
            item->setIcon(QIcon(swatch));
        }
        item->setToolTip(description);
    }
    resize(qMax(width(), 760), qMax(height(), 400));
}

UnknownColorsDialog::~UnknownColorsDialog()
{
    delete ui;
}
