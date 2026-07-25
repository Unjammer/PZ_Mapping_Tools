#ifndef SEARCHDOCK_H
#define SEARCHDOCK_H

#include <QDockWidget>
#include <QListWidget>

class Document;
class CellDocument;
class WorldCell;
class WorldDocument;

namespace Ui {
class SearchDock;
}

class SearchResults
{
public:
    enum class SearchBy
    {
        ObjectType,
        LotFileName,
        PropertyDef,
    };

    SearchResults()
        : searchBy(SearchBy::ObjectType)
        , selectedCell(-1, -1)
    {

    }

    void reset() {
        cells.clear();
        selectedCell = QPoint(-1, -1);
    }

    SearchBy searchBy;
    QString searchStringObjectType;
    QString searchStringLotFileName;
    QString searchStringPropertyDef;
    QString searchStringPropertyValue;
    QList<QPoint> cells;
    QPoint selectedCell;
};

class SearchDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit SearchDock(QWidget *parent = 0);

    void changeEvent(QEvent *e);
    void retranslateUi();

    void setDocument(Document *doc);
    void clearDocument();
    WorldDocument* worldDocument();

private slots:
    void comboActivated1(int index);
    void comboActivated2(int index);
    void textChanged();
    void searchObjectType();
    void searchLotFileName();
    void searchPropertyDef();
    void setList(SearchResults* results);
    void listSelectionChanged();
    void listActivated(const QModelIndex& index);
    void documentAboutToClose(int index, Document *doc);
    void worldResized();

private:
    void setCombo2(SearchResults::SearchBy searchBy);
    SearchResults *searchResultsFor(WorldDocument *worldDoc, bool create = true);

private:
    Ui::SearchDock* ui;
    CellDocument* mCellDoc;
    WorldDocument* mWorldDoc;
    QMap<Document*,SearchResults*> mResults;
    bool mSynching;
};

#endif // SEARCHDOCK_H
