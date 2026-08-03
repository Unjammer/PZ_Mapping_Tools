/*
 * Project Zomboid WorldEd - streets.xml editor
 */

#ifndef STREETNAMESDOCK_H
#define STREETNAMESDOCK_H

#include <QDockWidget>
#include <QPointer>
#include <QPolygonF>
#include <QVector>

class Document;
class BaseGraphicsScene;
class WorldDocument;

class QCheckBox;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QUndoStack;

struct StreetNameRecord
{
    QString name;
    int width = 5;
    QPolygonF points;

    bool operator ==(const StreetNameRecord &other) const
    {
        return name == other.name && width == other.width &&
                points == other.points;
    }
};

class StreetNamesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit StreetNamesDock(QWidget *parent = nullptr);
    ~StreetNamesDock() override;

    void setDocument(Document *document);
    void clearDocument();

    const QVector<StreetNameRecord> &streets() const { return mStreets; }
    bool saveForProject();
    bool validateStreetFile(const QString &fileName, int *streetCount,
                            QString *error) const;
    void applySnapshot(const QVector<StreetNameRecord> &streets,
                       int selectedStreet);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void browseForFile();
    void loadFile();
    void saveFile();
    void createStreet();
    void editStreet();
    void showStreetsChanged(bool visible);
    void visualWidthChanged(int width);
    void removeStreet();
    void reverseStreet();
    void splitStreetAtSelectedPoint();
    void removeSelectedPoint();
    void streetSelectionChanged();
    void streetFilterChanged(const QString &text);
    void streetNameEditingFinished();
    void streetWidthEditingFinished();
    void updateUi();

private:
    enum class InteractionMode {
        Select,
        Edit,
        Create
    };

    bool readFile(const QString &fileName,
                  QVector<StreetNameRecord> *streets,
                  QString *error) const;
    bool writeFile(const QString &fileName, QString *error) const;
    bool validate(QString *error) const;
    bool saveCurrentFile(bool chooseFileWhenMissing);

    QString defaultFileName() const;
    bool maybeSaveCurrentFile();
    void attachScene(BaseGraphicsScene *scene);
    void detachScene();
    void clearGraphics();
    void rebuildGraphics();
    void rebuildList();
    void applyStreetFilter();
    void selectStreet(int index);
    void beginSnapshot();
    void commitSnapshot(const QString &text);
    void cancelSnapshot();
    void finishCreating(bool accept);

    QPointF sceneToWorld(const QPointF &scenePoint) const;
    QPointF worldToScene(const QPointF &worldPoint) const;
    QPointF snappedWorldPoint(const QPointF &scenePoint) const;
    int pickStreet(const QPointF &scenePoint) const;
    int pickPoint(const QPointF &scenePoint) const;
    bool closestPointOnSelectedStreet(const QPointF &scenePoint,
                                      int *segment,
                                      QPointF *worldPoint) const;

    void retranslateUi();

private:
    QPointer<Document> mDocument;
    QPointer<WorldDocument> mWorldDocument;
    QPointer<BaseGraphicsScene> mScene;
    QVector<StreetNameRecord> mStreets;

    QVector<QGraphicsPathItem *> mPathItems;
    QVector<QGraphicsSimpleTextItem *> mLabelItems;
    QVector<QGraphicsEllipseItem *> mPointItems;

    QLineEdit *mFileNameEdit;
    QPushButton *mBrowseButton;
    QPushButton *mLoadButton;
    QPushButton *mSaveButton;
    QPushButton *mUndoButton;
    QPushButton *mRedoButton;
    QCheckBox *mShowStreetsCheckBox;
    QSpinBox *mVisualWidthSpinBox;
    QLineEdit *mStreetFilterEdit;
    QPushButton *mCreateButton;
    QPushButton *mEditButton;
    QPushButton *mRemoveButton;
    QPushButton *mReverseButton;
    QPushButton *mSplitButton;
    QPushButton *mRemovePointButton;
    QTreeWidget *mStreetList;
    QLineEdit *mStreetNameEdit;
    QSpinBox *mWidthSpinBox;
    QLabel *mStatusLabel;
    QUndoStack *mUndoStack;

    InteractionMode mMode = InteractionMode::Select;
    int mSelectedStreet = -1;
    int mSelectedPoint = -1;
    int mDraggingPoint = -1;
    bool mDragging = false;
    bool mConsumedNavigationPress = false;
    bool mUpdatingUi = false;
    QVector<StreetNameRecord> mSnapshotBefore;
    int mSnapshotSelection = -1;
};

#endif // STREETNAMESDOCK_H
