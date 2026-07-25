#include "lotfilesmanager.h"

#include "mapcomposite.h"
#include "mapmanager.h"

DelayedMapLoader::DelayedMapLoader()
{
    connect(MapManager::instance(), &MapManager::mapLoaded,
            this, &DelayedMapLoader::mapLoaded);
    connect(MapManager::instance(), &MapManager::mapFailedToLoad,
            this, &DelayedMapLoader::mapFailedToLoad);
}

void DelayedMapLoader::addMap(MapInfo *info)
{
    mLoading += new SubMapLoading(info);
}

bool DelayedMapLoader::isLoading()
{
    for (int i = 0; i < mLoading.size(); i++) {
        if (mLoading[i]->mapInfo->isLoading())
            return true;
    }
    return false;
}

void DelayedMapLoader::mapLoaded(MapInfo *mapInfo)
{
    for (int i = 0; i < mLoading.size(); i++) {
        SubMapLoading *loading = mLoading[i];
        if (loading->mapInfo == mapInfo) {
            mLoaded += new SubMapLoading(mapInfo);
            delete mLoading.takeAt(i);
            --i;
        }
    }
}

void DelayedMapLoader::mapFailedToLoad(MapInfo *mapInfo)
{
    for (int i = 0; i < mLoading.size(); i++) {
        if (mLoading[i]->mapInfo == mapInfo) {
            mError = MapManager::instance()->errorString();
            delete mLoading.takeAt(i);
            --i;
        }
    }
}

DelayedMapLoader::SubMapLoading::SubMapLoading(MapInfo *info)
    : mapInfo(info)
    , holdsReference(false)
{
    if (mapInfo->map()) {
        MapManager::instance()->addReferenceToMap(mapInfo);
        holdsReference = true;
    }
}

DelayedMapLoader::SubMapLoading::~SubMapLoading()
{
    if (holdsReference)
        MapManager::instance()->removeReferenceToMap(mapInfo);
}
