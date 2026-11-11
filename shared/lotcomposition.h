#ifndef LOTCOMPOSITION_H
#define LOTCOMPOSITION_H

#include <QString>
#include <QVector>

class MapComposite;

namespace PZTools {

inline bool lotLayerPreservesVegetation(const QString &name)
{
    return name.startsWith(QLatin1String("Walls")) ||
            name.startsWith(QLatin1String("WallTrim")) ||
            name.startsWith(QLatin1String("WallOverlay")) ||
            name.startsWith(QLatin1String("WallGrime")) ||
            name == QLatin1String("Frames") ||
            name == QLatin1String("Doors") ||
            name == QLatin1String("Windows");
}

inline bool isLotUnderlayFloor(const QString &name)
{
    return name.startsWith(QLatin1String("Floor"));
}

class LotVisibilityMask
{
public:
    void add(MapComposite *lot)
    {
        if (mFirstLot)
            mOtherLots.append(lot);
        else
            mFirstLot = lot;
    }

    template<class IsVisible>
    bool hidesTile(IsVisible isVisible) const
    {
        if (!mFirstLot)
            return false;
        if (isVisible(mFirstLot))
            return true;
        for (MapComposite *lot : mOtherLots) {
            if (isVisible(lot))
                return true;
        }
        return false;
    }

private:
    MapComposite *mFirstLot = nullptr;
    QVector<MapComposite *> mOtherLots;
};

class LotUnderlay
{
public:
    template<class Cell>
    explicit LotUnderlay(const QVector<Cell> &lotCells)
    {
        for (const Cell &cell : lotCells) {
            const QString &name = cell.layer->name();
            if (name == QLatin1String("Floor")) {
                mHasFloor = true;
                break;
            }
            mKeepVegetation &= lotLayerPreservesVegetation(name);
        }
    }

    bool keepsLayer(const QString &name)
    {
        mLeadingFloors &= isLotUnderlayFloor(name);
        return keepsLayer(name, mLeadingFloors);
    }

    bool keepsLayer(const QString &name, bool leadingFloor) const
    {
        return !mHasFloor && (leadingFloor
                || (mKeepVegetation && name.startsWith(QLatin1String("Vegetation"))));
    }

private:
    bool mHasFloor = false;
    bool mKeepVegetation = true;
    bool mLeadingFloors = true;
};

template<class Cell>
void suppressLotUnderlay(QVector<Cell> &cells, const QVector<Cell> &lotCells)
{
    LotUnderlay underlay(lotCells);
    int kept = 0;
    for (int i = 0; i < cells.size(); ++i) {
        if (underlay.keepsLayer(cells.at(i).layer->name())) {
            if (kept != i)
                cells[kept] = cells.at(i);
            ++kept;
        }
    }
    cells.resize(kept);
}

}

#endif
