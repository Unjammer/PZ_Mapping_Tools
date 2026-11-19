#ifndef REGIONSDOCK_H
#define REGIONSDOCK_H
#include <QDockWidget>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVector>
class BaseGraphicsScene;
class Document;
class WorldDocument;
class QCheckBox;
class QComboBox;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QUndoStack;
struct RegionPropertyRecord
{
    QString key;
    QString type = QStringLiteral("String");
    QString value;
    bool operator ==(const RegionPropertyRecord &other) const
    {
        return key == other.key && type == other.type && value == other.value;
    }
};
struct RegionRecord
{
    QString name;
    QString type = QStringLiteral("Region");
    int x = 0;
    int y = 0;
    int z = 0;
    int width = 1;
    int height = 1;
    QVector<RegionPropertyRecord> properties;
    bool operator ==(const RegionRecord &other) const
    {
        return name == other.name && type == other.type &&
                x == other.x && y == other.y && z == other.z &&
                width == other.width && height == other.height &&
                properties == other.properties;
    }
};
class RegionsDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit RegionsDock(QWidget *parent = nullptr);
    ~RegionsDock() override;
    void setDocument(Document *document);
    void clearDocument();
    bool saveForProject();
    const QVector<RegionRecord> &regions() const { return mRegions; }
    int selectedRegionIndex() const { return mSelectedRegion; }
    void applySnapshot(const QVector<RegionRecord> &regions,
                       int selectedRegion);
protected:
    void changeEvent(QEvent *event) override;
private slots:
    void browseForFile();
    void loadFile();
    void saveFile();
    void addRegion();
    void duplicateRegion();
    void removeRegion();
    void applyCellSelection();
    void addProperty();
    void removeProperty();
    void regionSelectionChanged();
    void regionFilterChanged(const QString &text);
    void regionFieldsEditingFinished();
    void propertyItemChanged();
    void showRegionsChanged(bool visible);
    void updateUi();
private:
    bool readFile(const QString &fileName,
                  QVector<RegionRecord> *regions,
                  QString *error) const;
    bool writeFile(const QString &fileName, QString *error) const;
    bool writeRecords(const QString &fileName,
                      const QVector<RegionRecord> &regions,
                      QString *error) const;
    bool validateRecords(const QVector<RegionRecord> &regions,
                         QString *error) const;
    bool saveCurrentFile(bool chooseFileWhenMissing);
    bool maybeSaveCurrentFile();
    QString defaultFileName() const;
    void attachScene(BaseGraphicsScene *scene);
    void detachScene();
    void clearGraphics();
    void rebuildGraphics();
    void rebuildList();
    void rebuildPropertyTable();
    void applyRegionFilter();
    void selectRegion(int index);
    void beginSnapshot();
    void commitSnapshot(const QString &text);
    bool hasUnsavedChanges() const;
    void selectedCellBounds(int *x, int *y, int *width, int *height) const;
    QPointF worldToScene(const QPointF &worldPoint) const;
    QPointF sceneToWorld(const QPointF &scenePoint) const;
    void retranslateUi();
private:
    QPointer<Document> mDocument;
    QPointer<WorldDocument> mWorldDocument;
    QPointer<BaseGraphicsScene> mScene;
    QVector<RegionRecord> mRegions;
    QVector<RegionRecord> mSavedRegions;
    QVector<QGraphicsPathItem *> mPathItems;
    QVector<QGraphicsSimpleTextItem *> mLabelItems;
    QLineEdit *mFileNameEdit;
    QPushButton *mBrowseButton;
    QPushButton *mLoadButton;
    QPushButton *mSaveButton;
    QPushButton *mUndoButton;
    QPushButton *mRedoButton;
    QCheckBox *mShowRegionsCheckBox;
    QLineEdit *mRegionFilterEdit;
    QTreeWidget *mRegionList;
    QLineEdit *mNameEdit;
    QComboBox *mTypeCombo;
    QSpinBox *mXSpinBox;
    QSpinBox *mYSpinBox;
    QSpinBox *mZSpinBox;
    QSpinBox *mWidthSpinBox;
    QSpinBox *mHeightSpinBox;
    QPushButton *mAddButton;
    QPushButton *mDuplicateButton;
    QPushButton *mRemoveButton;
    QPushButton *mUseSelectionButton;
    QTableWidget *mPropertyTable;
    QPushButton *mAddPropertyButton;
    QPushButton *mRemovePropertyButton;
    QLabel *mStatusLabel;
    QUndoStack *mUndoStack;
    int mSelectedRegion = -1;
    bool mUpdatingUi = false;
    QVector<RegionRecord> mSnapshotBefore;
    int mSnapshotSelection = -1;
};
#endif
