#ifndef WORLDMAPANNOTATIONSDIALOG_H
#define WORLDMAPANNOTATIONSDIALOG_H

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class WorldMapAnnotationsDialog : public QDialog
{
public:
    explicit WorldMapAnnotationsDialog(const QString &suggestedFile,
                                       QWidget *parent = nullptr);

    QString fileName() const;

protected:
    void reject() override;

private:
    struct Annotation
    {
        bool translated = false;
        QString text;
        QString style = QStringLiteral("text-place");
        int x = 0;
        int y = 0;
        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        double alpha = 0.0;
        double scale = 1.0;
        double anchorX = 0.5;
        double anchorY = 0.5;
        double rotation = 0.0;
        bool matchPerspective = true;
        bool applyZoom = true;
        double minZoom = 0.0;
        double maxZoom = 24.0;
        bool userDefined = false;
    };

    void addAnnotation(const Annotation &annotation);
    void chooseFile();
    void duplicateSelected();
    bool loadFile(const QString &fileName);
    void markDirty();
    bool maybeSave();
    void rebuildTable();
    void removeSelected();
    bool save();
    bool saveAs();
    bool saveFile(const QString &fileName);
    void updateCurrent();
    void updateEditor();
    void updateRow(int row);

    QVector<Annotation> mAnnotations;
    QString mFileName;
    bool mDirty = false;
    bool mUpdating = false;
    QLabel *mFileLabel;
    QTableWidget *mTable;
    QComboBox *mTranslationMode;
    QLineEdit *mText;
    QLineEdit *mStyle;
    QSpinBox *mX;
    QSpinBox *mY;
    QDoubleSpinBox *mRed;
    QDoubleSpinBox *mGreen;
    QDoubleSpinBox *mBlue;
    QDoubleSpinBox *mAlpha;
    QDoubleSpinBox *mScale;
    QDoubleSpinBox *mAnchorX;
    QDoubleSpinBox *mAnchorY;
    QDoubleSpinBox *mRotation;
    QCheckBox *mMatchPerspective;
    QCheckBox *mApplyZoom;
    QDoubleSpinBox *mMinZoom;
    QDoubleSpinBox *mMaxZoom;
    QCheckBox *mUserDefined;
    QPushButton *mSaveButton;
};

#endif
