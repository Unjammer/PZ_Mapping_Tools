#ifndef SEARCHDOCK_H
#define SEARCHDOCK_H

#include <QDockWidget>
#include <QMap>
#include <QModelIndex>
#include <QPointer>
#include <QTimer>

class Document;
class CellDocument;
class WorldCell;
class WorldDocument;
class SearchResultsModel;

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
        , pending(false)
    {

    }

    void reset() {
        cells.clear();
        selectedCell = QPoint(-1, -1);
        pending = false;
    }

    SearchBy searchBy;
    QString searchStringObjectType;
    QString searchStringLotFileName;
    QString searchStringPropertyDef;
    QString searchStringPropertyValue;
    QList<QPoint> cells;
    QPoint selectedCell;
    bool pending;
};

class SearchDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit SearchDock(QWidget *parent = 0);
    ~SearchDock() override;

    void changeEvent(QEvent *e);
    void retranslateUi();

    void setDocument(Document *doc);
    void clearDocument();
    WorldDocument* worldDocument();

private slots:
    void comboActivated1(int index);
    void comboActivated2(int index);
    void textChanged();
    void runTextSearch();
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
    QPointer<CellDocument> mCellDoc;
    QPointer<WorldDocument> mWorldDoc;
    QMap<Document*,SearchResults*> mResults;
    SearchResultsModel *mModel;
    QTimer mSearchDelay;
    bool mSynching;
};

#endif // SEARCHDOCK_H
