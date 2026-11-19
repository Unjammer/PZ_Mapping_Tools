#ifndef TILEPALETTEWINDOW_H
#define TILEPALETTEWINDOW_H

#include <QDialog>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSettings>
#include <QVector>
#include <functional>

class QCheckBox;
class QComboBox;
class QListView;
class QPushButton;
class QTabWidget;
class QTimer;

namespace Tiled {
class Tile;

namespace Internal {
class TilePaletteListModel;

class TilePaletteWindow : public QDialog
{
    Q_OBJECT
public:
    explicit TilePaletteWindow(const QString &editorId, QWidget *parent);
    ~TilePaletteWindow() override;
    static bool historyEnabledForEditor(const QString &editorId);
    bool historyEnabled() const;
    void setCurrentTile(const QString &tilesetName, int tileId);
    void addFavoriteTile(const QString &tilesetName, int tileId);
    void recordUsedTile(const QString &tilesetName, int tileId);
    void setTileResolver(std::function<const Tile *(const QString &, int)> resolver);
    void refreshTiles();
    bool importPalette(const QString &fileName, QString *error = nullptr);
    bool exportPalette(const QString &fileName, QString *error = nullptr) const;

signals:
    void tileActivated(const QString &tilesetName, int tileId);
    void visibilityChanged(bool visible);
    void historyEnabledChanged(bool enabled);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    using TileKey = QPair<QString, int>;
    using TileList = QVector<TileKey>;
    void addFavorite(const TileKey &key);
    void selectPalette(const QString &name);
    void addPalette(const QString &name, const TileList &tiles);
    void savePalettes();
    void flushRecent();
    void updateButtons();
    QListView *makeTileList(QWidget *parent, const QString &name, TilePaletteListModel **model);

    QSettings mSettings;
    QMap<QString, TileList> mPalettes;
    QSet<TileKey> mFavoriteKeys;
    TileKey mCurrent{QString(), -1};
    std::function<const Tile *(const QString &, int)> mResolver;
    QTabWidget *mTabs;
    QComboBox *mNames;
    QListView *mFavorites;
    QListView *mRecent;
    TilePaletteListModel *mFavoriteModel;
    TilePaletteListModel *mRecentModel;
    QPushButton *mAddCurrent;
    QPushButton *mRemoveFavorite;
    QPushButton *mDeletePalette;
    QPushButton *mFavoriteRecent;
    QCheckBox *mTrackRecent;
    QTimer *mSaveTimer;
    bool mActivated;
    bool mRecentDirty = false;
};

}
}

#endif
