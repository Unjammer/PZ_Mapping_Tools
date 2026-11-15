package pztools.preview;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import zombie.characters.IsoPlayer;
import zombie.core.Core;
import zombie.iso.BuildingDef;
import zombie.iso.BuildingID;
import zombie.iso.IsoCell;
import zombie.iso.IsoChunk;
import zombie.iso.IsoLot;
import zombie.iso.IsoMetaCell;
import zombie.iso.IsoMetaGrid;
import zombie.iso.LotHeader;
import zombie.iso.MapFiles;
import zombie.iso.RoomDef;
import zombie.iso.RoomID;
import zombie.iso.enums.ChunkGenerationStatus;
import zombie.iso.sprite.IsoSpriteManager;
import zombie.network.GameClient;
import zombie.network.GameServer;

public final class DirectMapHooks {
    private static final String MAP_NAME = "PZDirectMapPreview";
    private static final String BUILD = "42.22.2";
    private static final AtomicInteger SOURCE_LOADS = new AtomicInteger();
    private static final AtomicInteger STOCK_LOADS = new AtomicInteger();
    private static final AtomicBoolean STARTED = new AtomicBoolean();
    private static volatile DirectMapSnapshot snapshot;
    private static volatile boolean metadataReady;

    private DirectMapHooks() {}

    static void configure(DirectMapSnapshot data) {
        snapshot = data;
    }

    public static boolean isPreviewMap(MapFiles files) {
        return files != null && MAP_NAME.equalsIgnoreCase(files.mapDirectoryName);
    }

    public static boolean isPreviewHeader(LotHeader header) {
        return header != null && isPreviewMap(header.mapFiles);
    }

    public static void registerMap(MapFiles files) {
        DirectMapSnapshot data = snapshot;
        if (!isPreviewMap(files)) {
            for (DirectMapSnapshot.Cell source : data.cells()) {
                suppressStockCell(files, source);
            }
            return;
        }
        if (!BUILD.equals(Core.getInstance().getGameAndBuildVersion())
                || GameClient.client || GameServer.server) {
            throw new IllegalStateException("Direct Map Preview requires Build 42.22.2 in single-player.");
        }

        metadataReady = false;
        for (DirectMapSnapshot.Cell source : data.cells()) {
            LotHeader header = new LotHeader(source.x(), source.y());
            header.fileName = headerName(source);
            header.width = DirectMapSnapshot.CHUNK_SIZE;
            header.height = DirectMapSnapshot.CHUNK_SIZE;
            header.minLevel = source.minLevel();
            header.maxLevel = source.maxLevel();
            header.version = LotHeader.VERSION_LATEST;
            header.fixed2x = true;
            header.mapFiles = files;
            header.tilesUsed.addAll(source.tiles());
            files.minX = Math.min(files.minX, source.x());
            files.minY = Math.min(files.minY, source.y());
            files.maxX = Math.max(files.maxX, source.x());
            files.maxY = Math.max(files.maxY, source.y());
            files.infoHeaders.put(header.fileName, header);
            files.infoHeaderNames.add(header.fileName);
        }
    }

    private static void suppressStockCell(MapFiles files, DirectMapSnapshot.Cell source) {
        String header = headerName(source);
        String coordinates = source.x() + "_" + source.y();
        files.infoHeaders.remove(header);
        files.infoHeaderNames.remove(header);
        removeFileReference(files, header);
        removeFileReference(files, "world_" + coordinates + ".lotpack");
        removeFileReference(files, "chunkdata_" + coordinates + ".bin");
    }

    private static void removeFileReference(MapFiles files, String name) {
        files.infoFileNames.remove(name);
        files.infoFileModded.remove(name);
    }

    public static void preserveNativeCellPriority(MapFiles files) {
        if (isPreviewMap(files)) {
            files.bgHasCell300.clear();
        }
    }

    public static void installMetadata(IsoMetaGrid grid) {
        for (DirectMapSnapshot.Cell source : snapshot.cells()) {
            installCellMetadata(grid, source);
        }
        metadataReady = true;
    }

    private static void installCellMetadata(IsoMetaGrid grid, DirectMapSnapshot.Cell data) {
        String name = headerName(data);
        LotHeader header = IsoLot.InfoHeaders.get(name);
        if (!isPreviewHeader(header)) {
            throw new IllegalStateException("Missing high-priority world " + MAP_NAME + " for Direct Map Preview.");
        }
        for (String tile : data.tiles()) {
            if (!IsoSpriteManager.instance.namedMap.containsKey(tile)) {
                throw new IllegalStateException("Unrecognized preview sprite " + tile);
            }
        }

        IsoMetaCell cell = new IsoMetaCell(data.x(), data.y());
        cell.info = header;
        for (List<DirectMapSnapshot.Room> rooms : data.buildings()) {
            BuildingDef building = new BuildingDef();
            building.id = BuildingID.makeID(data.x(), data.y(), cell.buildings.size());
            for (DirectMapSnapshot.Room source : rooms) {
                installRoom(cell, building, source, data);
            }
            building.CalculateBounds(new ArrayList<>());
            building.metaId = building.calculateMetaID(data.x(), data.y());
            cell.buildings.add(building);
            cell.buildingByMetaId.put(building.metaId, building);
        }
        grid.setCellData(data.x(), data.y(), cell);
        System.out.printf("[PZ Direct Map] Registered cell %d,%d with %d rooms and %d buildings%n",
                data.x(), data.y(), cell.rooms.size(), cell.buildings.size());
    }

    private static void installRoom(IsoMetaCell cell, BuildingDef building, DirectMapSnapshot.Room source,
                                    DirectMapSnapshot.Cell data) {
        long id = RoomID.makeID(data.x(), data.y(), cell.roomList.size());
        RoomDef room = new RoomDef(id, source.name());
        room.level = source.level();
        room.building = building;
        for (DirectMapSnapshot.Rect rect : source.rectangles()) {
            room.rects.add(new RoomDef.RoomRect(rect.x(), rect.y(), rect.width(), rect.height()));
        }
        room.CalculateBounds();
        room.metaId = room.calculateMetaID(data.x(), data.y());
        cell.rooms.put(room.id, room);
        cell.roomByMetaId.put(room.metaId, room);
        cell.roomList.add(room);
        if (room.isEmptyOutside()) {
            building.emptyoutside.add(room);
        } else {
            building.rooms.add(room);
        }
    }

    public static boolean excludeRoom(IsoMetaCell cell, RoomDef room) {
        boolean previewCell = snapshot.getCell(cell.getX(), cell.getY()) != null;
        boolean previewRoom = snapshot.getCell(RoomID.getCellX(room.id), RoomID.getCellY(room.id)) != null;
        return previewCell != previewRoom;
    }

    public static boolean containsChunk(int worldX, int worldY) {
        return metadataReady && snapshot.cellAtChunk(worldX, worldY) != null;
    }

    public static void setProceduralChunkLevels(IsoChunk chunk, int minLevel, int maxLevel) {
        if (containsChunk(chunk.wx, chunk.wy)) {
            minLevel = Math.min(minLevel, chunk.getMinLevel());
            maxLevel = Math.max(maxLevel, chunk.getMaxLevel());
        }
        chunk.setMinMaxLevel(minLevel, maxLevel);
    }

    public static void stockChunkLoaded(boolean loaded) {
        if (metadataReady && loaded) {
            STOCK_LOADS.incrementAndGet();
        }
    }

    public static boolean loadChunk(IsoCell cell, int worldX, int worldY, IsoChunk chunk) {
        IsoLot lot = IsoLot.pool.alloc();
        try {
            readChunk(lot, worldX, worldY, chunk);
            int columns = cell.PlaceLot(lot, 0, 0, lot.minLevel, chunk, worldX, worldY,
                                        new boolean[DirectMapSnapshot.CHUNK_SIZE * DirectMapSnapshot.CHUNK_SIZE]);
            if (columns > 0) {
                chunk.addModded(ChunkGenerationStatus.MODDED);
            }
            SOURCE_LOADS.incrementAndGet();
            return true;
        } finally {
            IsoLot.put(lot);
        }
    }

    public static void readChunk(IsoLot lot, Integer worldX, Integer worldY, IsoChunk chunk) {
        DirectMapSnapshot.Cell data = snapshot.cellAtChunk(worldX, worldY);
        if (data == null) {
            throw new IllegalArgumentException("Chunk outside Direct Map Preview coverage.");
        }
        lot.info = IsoLot.InfoHeaders.get(headerName(data));
        lot.wx = worldX;
        lot.wy = worldY;
        lot.minLevel = 0;
        lot.maxLevel = 1;
        lot.data.resetQuick();
        Arrays.fill(lot.offsetInData, -1);
        DirectMapSnapshot.Chunk source = data.chunkAt(worldX, worldY);
        if (source != null) {
            lot.minLevel = source.minLevel();
            lot.maxLevel = source.maxLevelExclusive();
            source.copyTo(lot.offsetInData, lot.data);
        }
        if (chunk != null) {
            chunk.lotheader = lot.info;
        }
    }

    private static String headerName(DirectMapSnapshot.Cell data) {
        return data.x() + "_" + data.y() + ".lotheader";
    }

    public static boolean confirmLoading(boolean pressed) {
        return pressed || (metadataReady && SOURCE_LOADS.get() > 0 && !STARTED.get());
    }

    public static void enteredGame() {
        if (!metadataReady || !STARTED.compareAndSet(false, true)) {
            return;
        }
        IsoPlayer player = IsoPlayer.getInstance();
        System.out.printf("[PZ Direct Map] Playable at %.1f,%.1f,%.1f: %d preview loads, %d successful stock loads%n",
                player.getX(), player.getY(), player.getZ(), SOURCE_LOADS.get(), STOCK_LOADS.get());
    }
}
