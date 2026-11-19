#ifndef PZTOOLSABOUT_H
#define PZTOOLSABOUT_H

#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>

inline void showPZToolsAbout(QWidget *parent, const QString &applicationName,
                             bool includeTiledAttribution)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("About %1").arg(applicationName));
    dialog.resize(620, 430);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QTextBrowser *text = new QTextBrowser(&dialog);
    text->setOpenExternalLinks(true);
    QString html = QObject::tr(
                "<h2 align=\"center\">%1 - PZTools Unofficial</h2>"
                "<p>Part of the maintained unofficial Qt 5 continuation of the "
                "Project Zomboid mapping tools.</p>"
                "<p><b>Original Project Zomboid mapping-tools foundation</b><br>"
                "Tim Baker</p>"
                "<p>This application descends from Tim Baker's original Project "
                "Zomboid WorldEd and TileZed codebase. This edition "
                "includes Build 42 workflows, native 256 x 256 support, Biomemap "
                "tooling, enhanced tile workflows, extraction tools, compatibility "
                "work, corrections, and ongoing development.</p>"
                "<p>Later applicable upstream fixes from Tim Baker continue to be "
                "reviewed and selectively integrated with their original "
                "provenance preserved.</p>")
            .arg(applicationName.toHtmlEscaped());
    if (includeTiledAttribution) {
        html += QObject::tr(
                    "<p>TileZed and BuildingEd include Tiled editor code "
                    "originally developed by Thorbj&oslash;rn Lindeijer and "
                    "contributors.</p>");
    }
    html += QObject::tr(
                "<p><b>Official source repository for this continuation</b><br>"
                "<a href=\"https://github.com/Unjammer/PZ_Mapping_Tools\">"
                "https://github.com/Unjammer/PZ_Mapping_Tools</a></p>"
                "<p><b>Detailed provenance</b><br>"
                "<a href=\"https://github.com/Unjammer/PZ_Mapping_Tools/blob/main/FEATURE_PROVENANCE.md\">"
                "FEATURE_PROVENANCE.md</a> | "
                "<a href=\"https://github.com/Unjammer/PZ_Mapping_Tools/blob/main/UPSTREAM-HISTORY.md\">"
                "UPSTREAM-HISTORY.md</a></p>"
                "<p>This is a community-maintained project and is not an official "
                "The Indie Stone release.</p>"
                "<p>Copyright, license, authorship, and third-party attribution "
                "remain documented in AUTHORS.txt, UPSTREAM-HISTORY.md, "
                "THIRD_PARTY_NOTICES.txt, and the bundled license notices.</p>");
    text->setHtml(html);
    layout->addWidget(text);

    QDialogButtonBox *buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok, Qt::Horizontal, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}

#endif
