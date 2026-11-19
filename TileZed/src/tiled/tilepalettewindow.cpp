#include "tilepalettewindow.h"
#include "tile.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QCache>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHideEvent>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QShowEvent>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionFocusRect>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace Tiled {
namespace Internal {

using TileKey = QPair<QString, int>;
using TileList = QVector<TileKey>;
using Resolver = std::function<const Tile *(const QString &, int)>;

static constexpr int RecentLimit = 32;
static constexpr qint64 MaxImportBytes = 8 * 1024 * 1024;
static const QSize TileCellSize(54, 58);

class TilePaletteDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return TileCellSize;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem panel(option);
        panel.showDecorationSelected = true;
        const QStyle *style = option.widget ? option.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &panel, painter, option.widget);
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QIcon::Mode mode = option.state.testFlag(QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled;
        icon.paint(painter, option.rect.adjusted(3, 3, -3, -3), Qt::AlignCenter, mode);
        if (option.state.testFlag(QStyle::State_HasFocus)) {
            QStyleOptionFocusRect focus;
            focus.QStyleOption::operator=(option);
            focus.rect = option.rect.adjusted(1, 1, -1, -1);
            focus.backgroundColor = option.palette.color(option.state.testFlag(QStyle::State_Selected)
                                                         ? QPalette::Highlight : QPalette::Base);
            style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, option.widget);
        }
    }
};

static bool validKey(const TileKey &key)
{
    return !key.first.isEmpty() && key.second >= 0;
}

static QJsonArray encodeTiles(const TileList &tiles)
{
    QJsonArray array;
    for (const TileKey &tile : tiles) {
        array.append(QJsonObject{{QStringLiteral("tileset"), tile.first},
                                 {QStringLiteral("id"), tile.second}});
    }
    return array;
}

static bool decodeTiles(const QJsonValue &value, TileList *result, int limit = -1)
{
    if (!value.isArray())
        return false;
    TileList tiles;
    QSet<TileKey> seen;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &element : array) {
        const QJsonObject item = element.toObject();
        const TileKey key(item.value(QStringLiteral("tileset")).toString(),
                          item.value(QStringLiteral("id")).toInt(-1));
        if (!validKey(key))
            return false;
        if (!seen.contains(key)) {
            tiles.append(key);
            seen.insert(key);
            if (tiles.size() == limit)
                break;
        }
    }
    *result = std::move(tiles);
    return true;
}

static QJsonObject encodePalette(const QString &name, const TileList &tiles)
{
    return {{QStringLiteral("format"), QStringLiteral("pz-tile-palette")},
            {QStringLiteral("version"), 1}, {QStringLiteral("name"), name},
            {QStringLiteral("tiles"), encodeTiles(tiles)}};
}

static bool decodePalette(const QJsonObject &object, QString *name, TileList *tiles)
{
    *name = object.value(QStringLiteral("name")).toString().trimmed();
    return object.value(QStringLiteral("format")).toString() == QLatin1String("pz-tile-palette")
            && object.value(QStringLiteral("version")).toInt() == 1
            && !name->isEmpty() && decodeTiles(object.value(QStringLiteral("tiles")), tiles);
}

class TilePaletteListModel : public QAbstractListModel
{
public:
    TilePaletteListModel(QListView *view, const Resolver &resolver)
        : QAbstractListModel(view), mView(view), mResolver(resolver), mIcons(128)
    {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : mTiles.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= mTiles.size())
            return QVariant();
        const TileKey &key = mTiles.at(index.row());
        if (role == Qt::UserRole)
            return key.first;
        if (role == Qt::UserRole + 1)
            return key.second;
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole)
            return key.first + QLatin1Char('_') + QString::number(key.second);
        if (role == Qt::ToolTipRole)
            return QStringLiteral("<span>%1_%2</span>").arg(key.first.toHtmlEscaped(), QString::number(key.second));
        if (role != Qt::DecorationRole || !mView->isVisible() || !mResolver)
            return QVariant();
        if (const QIcon *cached = mIcons.object(key))
            return *cached;
        const Tile *tile = mResolver(key.first, key.second);
        QIcon icon;
        if (!tile) {
            icon = mView->style()->standardIcon(QStyle::SP_MessageBoxWarning);
        } else if (!tile->image().isNull()) {
            const qreal ratio = mView->devicePixelRatioF();
            QPixmap pixmap = QPixmap::fromImage(tile->image().scaled(
                         mView->iconSize() * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            pixmap.setDevicePixelRatio(ratio);
            icon = QIcon(pixmap);
        }
        mIcons.insert(key, new QIcon(icon));
        return icon;
    }

    const TileList &tiles() const { return mTiles; }
    TileKey keyAt(const QModelIndex &index) const
    {
        return index.isValid() && index.row() < mTiles.size()
                ? mTiles.at(index.row()) : TileKey(QString(), -1);
    }

    void setTiles(const TileList &tiles)
    {
        beginResetModel();
        mTiles = tiles;
        endResetModel();
    }

    void append(const TileKey &key)
    {
        const int end = mTiles.size();
        beginInsertRows(QModelIndex(), end, end);
        mTiles.append(key);
        endInsertRows();
    }

    void removeAt(int row)
    {
        beginRemoveRows(QModelIndex(), row, row);
        mTiles.removeAt(row);
        endRemoveRows();
    }

    bool used(const TileKey &key)
    {
        const int existing = mTiles.indexOf(key);
        if (existing == 0)
            return false;
        if (existing > 0) {
            beginMoveRows(QModelIndex(), existing, existing, QModelIndex(), 0);
            mTiles.move(existing, 0);
            endMoveRows();
        } else {
            if (mTiles.size() >= RecentLimit)
                removeAt(mTiles.size() - 1);
            beginInsertRows(QModelIndex(), 0, 0);
            mTiles.prepend(key);
            endInsertRows();
        }
        return true;
    }

    void refresh()
    {
        mIcons.clear();
        if (mView->isVisible() && !mTiles.isEmpty())
            emit dataChanged(index(0, 0), index(mTiles.size() - 1, 0), {Qt::DecorationRole});
    }

private:
    QListView *mView;
    const Resolver &mResolver;
    TileList mTiles;
    mutable QCache<TileKey, QIcon> mIcons;
};

TilePaletteWindow::TilePaletteWindow(const QString &editorId, QWidget *parent)
    : QDialog(parent, Qt::Tool),
      mSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("TheIndieStone"), editorId)
{
    setWindowTitle(tr("Tile Palettes"));
    mSettings.beginGroup(QStringLiteral("TilePalettes"));
    mActivated = mSettings.value(QStringLiteral("Activated"), false).toBool();
    mTabs = new QTabWidget(this);
    mTabs->setObjectName(QStringLiteral("tilePaletteTabs"));
    auto *favorites = new QWidget(mTabs);
    auto *recent = new QWidget(mTabs);
    mTabs->addTab(favorites, tr("Favorites"));
    mTabs->addTab(recent, tr("Recently Used"));

    auto *favoriteLayout = new QVBoxLayout(favorites);
    auto *palettes = new QHBoxLayout;
    mNames = new QComboBox(favorites);
    mNames->setAccessibleName(tr("Favorite palette"));
    mNames->setObjectName(QStringLiteral("favoritePalettes"));
    mNames->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto *create = new QPushButton(tr("New Palette…"), favorites);
    mDeletePalette = new QPushButton(tr("Delete Palette…"), favorites);
    mDeletePalette->setObjectName(QStringLiteral("deleteFavoritePalette"));
    palettes->addWidget(mNames, 1);
    palettes->addWidget(create);
    palettes->addWidget(mDeletePalette);
    favoriteLayout->addLayout(palettes);
    mFavorites = makeTileList(favorites, QStringLiteral("favoriteTiles"), &mFavoriteModel);
    favoriteLayout->addWidget(mFavorites, 1);
    auto *favoritesBar = new QHBoxLayout;
    mAddCurrent = new QPushButton(tr("Add current tile"), favorites);
    mAddCurrent->setObjectName(QStringLiteral("addCurrentTile"));
    mRemoveFavorite = new QPushButton(tr("Remove"), favorites);
    mRemoveFavorite->setObjectName(QStringLiteral("removeFavorite"));
    auto *importButton = new QPushButton(tr("Import…"), favorites);
    auto *exportButton = new QPushButton(tr("Export…"), favorites);
    favoritesBar->addWidget(mAddCurrent);
    favoritesBar->addWidget(mRemoveFavorite);
    favoritesBar->addStretch();
    favoritesBar->addWidget(importButton);
    favoritesBar->addWidget(exportButton);
    favoriteLayout->addLayout(favoritesBar);

    auto *recentLayout = new QVBoxLayout(recent);
    mTrackRecent = new QCheckBox(tr("Record recently used tiles"), recent);
    mTrackRecent->setObjectName(QStringLiteral("recordRecentTiles"));
    mTrackRecent->setChecked(mSettings.value(QStringLiteral("RecordRecent"), true).toBool());
    recentLayout->addWidget(mTrackRecent);
    mRecent = makeTileList(recent, QStringLiteral("recentTiles"), &mRecentModel);
    recentLayout->addWidget(mRecent, 1);
    mFavoriteRecent = new QPushButton(tr("Add to favorites"), recent);
    recentLayout->addWidget(mFavoriteRecent, 0, Qt::AlignLeft);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(mTabs, 1);
    layout->addWidget(buttons);
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    const QJsonArray stored = QJsonDocument::fromJson(mSettings.value(QStringLiteral("Favorites")).toByteArray()).array();
    for (const QJsonValue &value : stored) {
        QString name;
        TileList tiles;
        if (decodePalette(value.toObject(), &name, &tiles))
            mPalettes.insert(name, tiles);
    }
    if (mPalettes.isEmpty())
        mPalettes.insert(tr("My tiles"), TileList());
    mNames->addItems(mPalettes.keys());
    mNames->setCurrentText(mSettings.value(QStringLiteral("SelectedPalette")).toString());
    selectPalette(mNames->currentText());
    TileList recentTiles;
    decodeTiles(QJsonDocument::fromJson(mSettings.value(QStringLiteral("Recent")).toByteArray()).array(),
                &recentTiles, RecentLimit);
    mRecentModel->setTiles(recentTiles);
    mTabs->setCurrentIndex(mSettings.value(QStringLiteral("Tab"), 0).toInt() == 1 ? 1 : 0);
    resize(470, 420);
    restoreGeometry(mSettings.value(QStringLiteral("Geometry")).toByteArray());
    mSaveTimer = new QTimer(this);
    mSaveTimer->setSingleShot(true);
    connect(mSaveTimer, &QTimer::timeout, this, &TilePaletteWindow::flushRecent);
    connect(mNames, &QComboBox::currentTextChanged, this, &TilePaletteWindow::selectPalette);
    connect(create, &QPushButton::clicked, this, [this]() {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("New Palette"), tr("Palette name"),
                                                   QLineEdit::Normal, QString(), &accepted).trimmed();
        if (accepted && !name.isEmpty())
            addPalette(name, TileList());
    });
    connect(mDeletePalette, &QPushButton::clicked, this, [this]() {
        const QString name = mNames->currentText();
        if (mPalettes.size() <= 1)
            return;
        QMessageBox confirm(QMessageBox::Question, tr("Delete Palette"),
                            tr("Delete the favorite palette “%1”?").arg(name),
                            QMessageBox::Yes | QMessageBox::Cancel, this);
        confirm.setTextFormat(Qt::PlainText);
        confirm.setDefaultButton(QMessageBox::Cancel);
        if (confirm.exec() != QMessageBox::Yes)
            return;
        mPalettes.remove(name);
        mNames->removeItem(mNames->currentIndex());
        savePalettes();
        updateButtons();
    });
    connect(mAddCurrent, &QPushButton::clicked, this, [this]() { addFavorite(mCurrent); });
    connect(mFavoriteRecent, &QPushButton::clicked, this, [this]() {
        addFavorite(mRecentModel->keyAt(mRecent->currentIndex()));
    });
    connect(mRemoveFavorite, &QPushButton::clicked, this, [this]() {
        const int row = mFavorites->currentIndex().row();
        if (row >= 0 && row < mFavoriteModel->rowCount()) {
            mFavoriteKeys.remove(mFavoriteModel->keyAt(mFavorites->currentIndex()));
            mFavoriteModel->removeAt(row);
            mPalettes[mNames->currentText()] = mFavoriteModel->tiles();
            savePalettes();
            updateButtons();
        }
    });
    connect(importButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import Tile Palette"), QString(), tr("Tile palettes (*.json)"));
        QString error;
        if (!path.isEmpty() && !importPalette(path, &error))
            QMessageBox::warning(this, tr("Import Tile Palette"), error);
    });
    connect(exportButton, &QPushButton::clicked, this, [this]() {
        QString name = mNames->currentText().left(100);
        name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} _.-]")), QStringLiteral("_"));
        QFileDialog dialog(this, tr("Export Tile Palette"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setNameFilter(tr("Tile palettes (*.json)"));
        dialog.setDefaultSuffix(QStringLiteral("json"));
        dialog.selectFile(QStringLiteral("palette-") + name + QStringLiteral(".json"));
        QString error;
        if (dialog.exec() && !dialog.selectedFiles().isEmpty() && !exportPalette(dialog.selectedFiles().first(), &error))
            QMessageBox::warning(this, tr("Export Tile Palette"), error);
    });
    connect(mTrackRecent, &QCheckBox::toggled, this, [this](bool checked) {
        mSettings.setValue(QStringLiteral("RecordRecent"), checked);
        if (mActivated)
            emit historyEnabledChanged(checked);
    });
    connect(mTabs, &QTabWidget::currentChanged, this, [this](int index) { mSettings.setValue(QStringLiteral("Tab"), index); });
    connect(mFavorites->selectionModel(), &QItemSelectionModel::currentChanged, this, &TilePaletteWindow::updateButtons);
    connect(mRecent->selectionModel(), &QItemSelectionModel::currentChanged, this, &TilePaletteWindow::updateButtons);
    updateButtons();
}

TilePaletteWindow::~TilePaletteWindow()
{
    flushRecent();
    if (isVisible())
        mSettings.setValue(QStringLiteral("Geometry"), saveGeometry());
}

bool TilePaletteWindow::historyEnabledForEditor(const QString &editorId)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("TheIndieStone"), editorId);
    settings.beginGroup(QStringLiteral("TilePalettes"));
    return settings.value(QStringLiteral("Activated"), false).toBool()
            && settings.value(QStringLiteral("RecordRecent"), true).toBool();
}

bool TilePaletteWindow::historyEnabled() const
{
    return mActivated && mTrackRecent->isChecked();
}

QListView *TilePaletteWindow::makeTileList(QWidget *parent, const QString &name, TilePaletteListModel **model)
{
    auto *view = new QListView(parent);
    view->setObjectName(name);
    view->setViewMode(QListView::IconMode);
    view->setMovement(QListView::Static);
    view->setFlow(QListView::LeftToRight);
    view->setWrapping(true);
    view->setResizeMode(QListView::Adjust);
    view->setGridSize(TileCellSize);
    view->setIconSize(QSize(48, 52));
    view->setSpacing(0);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setDragDropMode(QAbstractItemView::NoDragDrop);
    view->setUniformItemSizes(true);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setItemDelegate(new TilePaletteDelegate(view));
    auto *tiles = new TilePaletteListModel(view, mResolver);
    view->setModel(tiles);
    *model = tiles;
    const auto activate = [this, tiles](const QModelIndex &index) {
        const TileKey key = tiles->keyAt(index);
        if (validKey(key))
            emit tileActivated(key.first, key.second);
    };
    connect(view, &QListView::clicked, this, activate);
    for (Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *shortcut = new QShortcut(QKeySequence(key), view);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [view, activate]() { activate(view->currentIndex()); });
    }
    return view;
}

void TilePaletteWindow::setCurrentTile(const QString &tilesetName, int tileId)
{
    const TileKey key(tilesetName, tileId);
    if (key != mCurrent) {
        mCurrent = key;
        if (isVisible())
            updateButtons();
    }
}

void TilePaletteWindow::recordUsedTile(const QString &tilesetName, int tileId)
{
    if (!historyEnabled())
        return;
    const TileKey key(tilesetName, tileId);
    if (validKey(key) && mRecentModel->used(key)) {
        mRecentDirty = true;
        if (!mSaveTimer->isActive())
            mSaveTimer->start(750);
    }
}

void TilePaletteWindow::addFavoriteTile(const QString &tilesetName, int tileId)
{
    const TileKey key(tilesetName, tileId);
    if (!validKey(key))
        return;
    addFavorite(key);
    mTabs->setCurrentIndex(0);
    const QModelIndex index = mFavoriteModel->index(mFavoriteModel->tiles().indexOf(key), 0);
    mFavorites->setCurrentIndex(index);
    mFavorites->scrollTo(index);
}

void TilePaletteWindow::setTileResolver(Resolver resolver)
{
    mResolver = std::move(resolver);
    refreshTiles();
}

void TilePaletteWindow::refreshTiles()
{
    mFavoriteModel->refresh();
    mRecentModel->refresh();
    if (isVisible())
        updateButtons();
}

void TilePaletteWindow::selectPalette(const QString &name)
{
    mFavoriteModel->setTiles(mPalettes.value(name));
    const TileList &tiles = mFavoriteModel->tiles();
    mFavoriteKeys = QSet<TileKey>(tiles.cbegin(), tiles.cend());
    mSettings.setValue(QStringLiteral("SelectedPalette"), name);
    updateButtons();
}

void TilePaletteWindow::addFavorite(const TileKey &key)
{
    if (!validKey(key) || mFavoriteKeys.contains(key))
        return;
    mFavoriteKeys.insert(key);
    mFavoriteModel->append(key);
    mPalettes[mNames->currentText()] = mFavoriteModel->tiles();
    savePalettes();
    updateButtons();
}

void TilePaletteWindow::addPalette(const QString &name, const TileList &tiles)
{
    QString available = name;
    for (int suffix = 2; mPalettes.contains(available); ++suffix)
        available = name + QStringLiteral(" (%1)").arg(suffix);
    mPalettes.insert(available, tiles);
    mNames->addItem(available);
    mNames->setCurrentText(available);
    savePalettes();
}

void TilePaletteWindow::savePalettes()
{
    QJsonArray palettes;
    for (auto it = mPalettes.cbegin(); it != mPalettes.cend(); ++it)
        palettes.append(encodePalette(it.key(), it.value()));
    mSettings.setValue(QStringLiteral("Favorites"), QJsonDocument(palettes).toJson(QJsonDocument::Compact));
}

void TilePaletteWindow::flushRecent()
{
    if (mRecentDirty) {
        mSettings.setValue(QStringLiteral("Recent"), QJsonDocument(encodeTiles(mRecentModel->tiles())).toJson(QJsonDocument::Compact));
        mRecentDirty = false;
    }
}

void TilePaletteWindow::updateButtons()
{
    mAddCurrent->setEnabled(isVisible() && validKey(mCurrent) && !mFavoriteKeys.contains(mCurrent)
                            && mResolver && mResolver(mCurrent.first, mCurrent.second));
    mRemoveFavorite->setEnabled(mFavorites->currentIndex().isValid());
    mDeletePalette->setEnabled(mNames->count() > 1);
    const TileKey selected = mRecentModel->keyAt(mRecent->currentIndex());
    mFavoriteRecent->setEnabled(validKey(selected) && !mFavoriteKeys.contains(selected));
    mFavoriteRecent->setToolTip(QStringLiteral("<span>%1</span>").arg(tr("Add to %1").arg(mNames->currentText()).toHtmlEscaped()));
}

bool TilePaletteWindow::importPalette(const QString &fileName, QString *error)
{
    QFile file(fileName);
    QString message;
    if (!file.open(QIODevice::ReadOnly)) {
        message = file.errorString();
    } else {
        const QByteArray bytes = file.read(MaxImportBytes + 1);
        if (file.error() != QFileDevice::NoError) {
            message = file.errorString();
        } else if (bytes.size() > MaxImportBytes) {
            message = tr("The palette exceeds the 8 MiB import limit.");
        } else {
            QJsonParseError parse;
            const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
            QString name;
            TileList tiles;
            if (parse.error != QJsonParseError::NoError)
                message = parse.errorString();
            else if (!decodePalette(doc.object(), &name, &tiles))
                message = tr("Expected a version 1 tile palette with a name and valid tileset IDs.");
            else {
                addPalette(name, tiles);
                return true;
            }
        }
    }
    if (error)
        *error = message;
    return false;
}

bool TilePaletteWindow::exportPalette(const QString &fileName, QString *error) const
{
    const QString name = mNames->currentText();
    const QByteArray bytes = QJsonDocument(encodePalette(name, mFavoriteModel->tiles())).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxImportBytes) {
        if (error)
            *error = tr("The palette exceeds the 8 MiB exchange limit.");
        return false;
    }
    QSaveFile file(fileName);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}

void TilePaletteWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateButtons();
    if (!mActivated) {
        mActivated = true;
        mSettings.setValue(QStringLiteral("Activated"), true);
        if (mTrackRecent->isChecked())
            emit historyEnabledChanged(true);
    }
    emit visibilityChanged(true);
}

void TilePaletteWindow::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    mSettings.setValue(QStringLiteral("Geometry"), saveGeometry());
    flushRecent();
    emit visibilityChanged(false);
}

}
}
