/*
 * Copyright 2026 Unjammer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef VEHICLEMESHPREVIEW_H
#define VEHICLEMESHPREVIEW_H

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QVector3D>
#include <functional>

class VehicleMeshPreview
{
public:
    struct RenderedVehicle
    {
        QImage image;
        QPointF anchor;
        QString vehicleName;
        qreal rasterScale = 1.0;

        bool isValid() const
        {
            return !image.isNull();
        }
    };

    static VehicleMeshPreview *instance();

    RenderedVehicle preview(const QString &gameDirectory,
                            const QString &zoneName,
                            qreal angleRadians,
                            int variant = 0,
                            qreal rasterScale = 1.0);
    RenderedVehicle previewForPlacement(const QString &gameDirectory,
                                        const QString &zoneName,
                                        qreal angleRadians,
                                        int zoneVariant,
                                        int placementVariant,
                                        qreal rasterScale,
                                        bool polyline,
                                        qreal minimumAngleJitter = 0.0,
                                        qreal maximumAngleJitter = 0.0);
    QString atlasStatus(const QString &gameDirectory, qreal rasterScale);
    bool rebuildAtlas(
            const QString &gameDirectory, qreal rasterScale,
            const std::function<bool(int, int, const QString &)> &progress,
            QString *summary, QString *error);
    void clear();
    void clearRendered();


private:
    struct TextBlock
    {
        QString name;
        QString body;
    };

    struct ModelDefinition
    {
        QString mesh;
        QString texture;
        qreal scale = 1.0;
        bool invertX = false;
    };

    struct WheelDefinition
    {
        QString name;
        QVector3D offset;
        qreal radius = 0.15;
        qreal width = 0.2;
    };

    struct VehicleDefinition
    {
        QString name;
        QString model;
        QString texture;
        QString templateName;
        qreal modelScale = 1.0;
        QVector3D modelOffset;
        QVector3D extents;
        QVector<WheelDefinition> wheels;
    };

    struct ZoneVehicle
    {
        QString name;
        qreal weight = 0.0;
    };

    struct ZoneDefinition
    {
        QVector<ZoneVehicle> vehicles;
        int chanceToSpawnNormal = 80;
        int chanceToSpawnBurnt = 0;
        int spawnRate = 16;
        int chanceOfOverCar = 0;
        int chanceToSpawnSpecial = 5;
        bool randomAngle = false;
        bool specialCar = false;
        bool burntCar = false;
        bool forceSpawn = false;
    };

    struct SpawnSelection
    {
        QString vehicleName;
        QString distributionName;
        qreal angleRadians = 0.0;
        bool spawn = false;
    };

    struct MeshVertex
    {
        QVector3D position;
        QPointF uv;
    };

    struct MeshTriangle
    {
        MeshVertex vertices[3];
    };

    struct Mesh
    {
        QVector<MeshTriangle> triangles;
        QVector3D minimum;
        QVector3D maximum;
    };

    struct AtlasEntry
    {
        int page = -1;
        QRect rect;
        QPointF anchor;
        QString vehicleName;
        qreal rasterScale = 1.0;
    };

    struct AtlasPage
    {
        QImage image;
        int cursorX = 2;
        int cursorY = 2;
        int rowHeight = 0;
        bool dirty = false;
    };

    struct AtlasCache
    {
        QVector<AtlasPage> pages;
        QHash<QString, AtlasEntry> entries;
        bool loaded = false;
        bool dirty = false;
        int unsavedEntries = 0;
    };

    VehicleMeshPreview();
    ~VehicleMeshPreview();

    bool ensureCatalog(const QString &gameDirectory);
    bool loadCatalog(const QString &gameDirectory);
    bool loadMesh(const VehicleDefinition &vehicle, Mesh *mesh) const;
    bool loadModelMesh(const QString &modelName, Mesh *mesh) const;
    bool loadLegacyMesh(const QString &path,
                        const ModelDefinition &model,
                        Mesh *mesh) const;
    RenderedVehicle render(const VehicleDefinition &vehicle,
                           qreal angleRadians,
                           qreal rasterScale = 1.0);
    RenderedVehicle previewVehicle(const QString &vehicleName,
                                   qreal angleRadians,
                                   qreal rasterScale);
    QString atlasDirectory() const;
    AtlasCache &atlasForQuality(int quality);
    bool loadAtlas(int quality, AtlasCache *atlas);
    void saveAtlas(int quality, AtlasCache *atlas, bool force);
    void saveAtlases(bool force = true);
    RenderedVehicle atlasEntry(const AtlasCache &atlas,
                               const AtlasEntry &entry) const;
    RenderedVehicle addToAtlas(int quality, const QString &key,
                               const RenderedVehicle &rendered);
    SpawnSelection selectSpawn(const QString &zoneName,
                               qreal angleRadians,
                               int zoneVariant,
                               int placementVariant,
                               bool applySpawnChance,
                               bool polyline,
                               qreal minimumAngleJitter,
                               qreal maximumAngleJitter) const;
    QString chooseVehicle(const QString &distribution,
                          quint32 seed) const;

    static QVector<TextBlock> blocks(const QString &text,
                                     const QString &pattern);
    static QString blockAt(const QString &text, int openingBrace);
    static QString value(const QString &text, const QString &name);
    static QVector<qreal> numericArray(const QString &text,
                                       const QString &name);
    static QVector<int> integerArray(const QString &text,
                                     const QString &name);
    static QString normalizedVehicleName(const QString &name);

    QString mGameDirectory;
    QString mMediaDirectory;
    QHash<QString, ModelDefinition> mModels;
    QHash<QString, VehicleDefinition> mVehicles;
    QHash<QString, ZoneDefinition> mZoneDefinitions;
    QVector<QString> mSpecialZoneNames;
    QHash<QString, Mesh> mMeshes;
    QHash<QString, RenderedVehicle> mRendered;
    QHash<int, AtlasCache> mAtlases;
    QString mCatalogFingerprint;
    QString mCacheRootOverride;
    int mAtlasDiskHits = 0;
    bool mCatalogReady = false;
};

#endif
