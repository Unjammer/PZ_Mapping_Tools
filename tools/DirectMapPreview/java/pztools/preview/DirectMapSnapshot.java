package pztools.preview;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import gnu.trove.list.array.TIntArrayList;

final class DirectMapSnapshot {
    static final int CELL_SIZE = 256;
    static final int CHUNK_SIZE = 8;
    static final int CHUNKS_PER_CELL = CELL_SIZE / CHUNK_SIZE;

    private static final int MAGIC = 0x505A4450;
    private static final int PROTOCOL = 2;
    private static final int MAX_CELL_COORDINATE = Short.MAX_VALUE / CHUNKS_PER_CELL;
    private static final int MAX_WORLD_SQUARE = (MAX_CELL_COORDINATE + 1) * CELL_SIZE - 1;

    record Rect(int x, int y, int width, int height) {}

    record Room(String name, int level, List<Rect> rectangles) {}

    record Cell(int x, int y, int minLevel, int maxLevel, List<String> tiles,
                List<List<Room>> buildings, int squareCount, Chunk[] chunks) {
        Chunk chunkAt(int worldX, int worldY) {
            int localX = worldX - x * CHUNKS_PER_CELL;
            int localY = worldY - y * CHUNKS_PER_CELL;
            return chunks[localX + localY * CHUNKS_PER_CELL];
        }

        int roomCount() {
            return buildings.stream().mapToInt(List::size).sum();
        }
    }

    private final Map<Integer, Cell> cells;

    private DirectMapSnapshot(Map<Integer, Cell> cells) {
        this.cells = Collections.unmodifiableMap(cells);
    }

    static DirectMapSnapshot fetch(String options) throws IOException {
        if (options == null || !options.matches("[0-9]{1,5}:[A-Za-z0-9_-]{8,128}")) {
            throw new IOException("Expected a loopback port and session token.");
        }
        int separator = options.indexOf(':');
        int port = Integer.parseInt(options.substring(0, separator));
        if (port < 1 || port > 65535) {
            throw new IOException("Invalid loopback port.");
        }
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), port), 10000);
            socket.setSoTimeout(30000);
            socket.getOutputStream().write(("PZDP/1 " + options.substring(separator + 1) + "\n")
                                           .getBytes(StandardCharsets.US_ASCII));
            socket.getOutputStream().flush();
            return read(new DataInputStream(new BufferedInputStream(socket.getInputStream())));
        }
    }

    private static DirectMapSnapshot read(DataInputStream input) throws IOException {
        if (input.readInt() != MAGIC || input.readInt() != PROTOCOL) {
            throw new IOException("Unsupported WorldEd playtest protocol.");
        }
        int axisSize = MAX_CELL_COORDINATE + 1;
        int cellCount = readInt(input, "cell count", 1, axisSize * axisSize);
        Map<Integer, Cell> cells = new LinkedHashMap<>();
        for (int index = 0; index < cellCount; ++index) {
            Cell cell = readCell(input);
            if (cells.putIfAbsent(cellKey(cell.x(), cell.y()), cell) != null) {
                throw new IOException("Duplicate WorldEd preview cell " + cell.x() + "," + cell.y() + ".");
            }
        }
        return new DirectMapSnapshot(cells);
    }

    private static Cell readCell(DataInputStream input) throws IOException {
        int cellX = readInt(input, "cell x", 0, MAX_CELL_COORDINATE);
        int cellY = readInt(input, "cell y", 0, MAX_CELL_COORDINATE);
        int minLevel = readInt(input, "minimum level", -32, 0);
        int maxLevel = readInt(input, "maximum level", 0, 31);
        int tileCount = readInt(input, "tile count", 0, 100000);
        List<String> tiles = new ArrayList<>(tileCount);
        for (int i = 0; i < tileCount; ++i) {
            tiles.add(readString(input));
        }

        List<List<Room>> buildings = readBuildings(input, cellX, cellY, minLevel, maxLevel);
        int squareCount = readInt(input, "square count", 0, CELL_SIZE * CELL_SIZE * (maxLevel - minLevel + 1));
        Chunk[] chunks = new Chunk[CHUNKS_PER_CELL * CHUNKS_PER_CELL];
        long tileReferences = 0;
        for (int i = 0; i < squareCount; ++i) {
            int localX = readInt(input, "square x", 0, CELL_SIZE - 1);
            int localY = readInt(input, "square y", 0, CELL_SIZE - 1);
            int level = readInt(input, "square level", minLevel, maxLevel);
            int count = readInt(input, "square tile count", 1, 1024);
            tileReferences += count;
            if (tileReferences > 8000000) {
                throw new IOException("Direct Map Preview supports at most 8,000,000 tile placements.");
            }
            int chunkIndex = localX / CHUNK_SIZE + localY / CHUNK_SIZE * CHUNKS_PER_CELL;
            if (chunks[chunkIndex] == null) {
                chunks[chunkIndex] = new Chunk(minLevel, maxLevel);
            }
            Chunk chunk = chunks[chunkIndex];
            chunk.addSquare(localX % CHUNK_SIZE, localY % CHUNK_SIZE, level, count,
                            tileCount, input);
        }
        return new Cell(cellX, cellY, minLevel, maxLevel, List.copyOf(tiles), buildings,
                        squareCount, chunks);
    }

    private static List<List<Room>> readBuildings(DataInputStream input, int cellX, int cellY,
                                                   int minLevel, int maxLevel) throws IOException {
        int west = cellX * CELL_SIZE;
        int north = cellY * CELL_SIZE;
        int east = west + CELL_SIZE;
        int south = north + CELL_SIZE;
        int buildingCount = readInt(input, "building count", 0, 1024);
        int totalRectangles = 0;
        List<List<Room>> buildings = new ArrayList<>(buildingCount);
        for (int i = 0; i < buildingCount; ++i) {
            int roomCount = readInt(input, "room count", 1, 1024);
            List<Room> rooms = new ArrayList<>(roomCount);
            int anchorX = Integer.MAX_VALUE;
            int anchorY = Integer.MAX_VALUE;
            for (int j = 0; j < roomCount; ++j) {
                String name = readString(input);
                int level = readInt(input, "room level", minLevel, maxLevel);
                int rectCount = readInt(input, "room rectangle count", 1, 4096);
                totalRectangles += rectCount;
                if (totalRectangles > 65536) {
                    throw new IOException("Direct Map Preview supports at most 65,536 room rectangles.");
                }
                List<Rect> rectangles = new ArrayList<>(rectCount);
                for (int k = 0; k < rectCount; ++k) {
                    int x = readInt(input, "room rectangle x", west, MAX_WORLD_SQUARE);
                    int y = readInt(input, "room rectangle y", north, MAX_WORLD_SQUARE);
                    int width = readInt(input, "room rectangle width", 1, MAX_WORLD_SQUARE - x + 1);
                    int height = readInt(input, "room rectangle height", 1, MAX_WORLD_SQUARE - y + 1);
                    anchorX = Math.min(anchorX, x);
                    anchorY = Math.min(anchorY, y);
                    rectangles.add(new Rect(x, y, width, height));
                }
                rooms.add(new Room(name, level, List.copyOf(rectangles)));
            }
            if (anchorX >= east || anchorY >= south) {
                throw new IOException("A preview building is outside its owning cell.");
            }
            buildings.add(List.copyOf(rooms));
        }
        return List.copyOf(buildings);
    }

    private static int readInt(DataInputStream input, String field, int min, int max) throws IOException {
        int value = input.readInt();
        if (value < min || value > max) {
            throw new IOException("Out-of-range " + field + " in WorldEd preview: " + value);
        }
        return value;
    }

    private static String readString(DataInputStream input) throws IOException {
        int length = readInt(input, "string length", 1, 1024);
        byte[] bytes = input.readNBytes(length);
        if (bytes.length != length) {
            throw new EOFException("Truncated WorldEd preview string.");
        }
        String value = new String(bytes, StandardCharsets.UTF_8);
        if (value.isBlank()) {
            throw new IOException("Blank string in WorldEd preview.");
        }
        return value;
    }

    Collection<Cell> cells() {
        return cells.values();
    }

    Cell getCell(int x, int y) {
        if (x < 0 || y < 0 || x > MAX_CELL_COORDINATE || y > MAX_CELL_COORDINATE) {
            return null;
        }
        return cells.get(cellKey(x, y));
    }

    Cell cellAtChunk(int worldX, int worldY) {
        return getCell(Math.floorDiv(worldX, CHUNKS_PER_CELL),
                       Math.floorDiv(worldY, CHUNKS_PER_CELL));
    }

    private static int cellKey(int x, int y) {
        return x + y * (MAX_CELL_COORDINATE + 1);
    }

    static final class Chunk {
        private final int[] offsets;
        private final TIntArrayList data = new TIntArrayList();
        private final int baseLevel;
        private int minLevel;
        private int maxLevel;

        Chunk(int minLevel, int maxLevel) {
            this.baseLevel = minLevel;
            offsets = new int[(maxLevel - minLevel + 1) * CHUNK_SIZE * CHUNK_SIZE];
            Arrays.fill(offsets, -1);
        }

        void addSquare(int x, int y, int level, int count,
                       int tileCount, DataInputStream input) throws IOException {
            int index = x + y * CHUNK_SIZE + (level - baseLevel) * CHUNK_SIZE * CHUNK_SIZE;
            if (offsets[index] != -1) {
                throw new IOException("Duplicate square in WorldEd preview.");
            }
            offsets[index] = data.size();
            data.add(count);
            for (int i = 0; i < count; ++i) {
                data.add(readInt(input, "tile index", 0, tileCount - 1));
            }
            minLevel = Math.min(minLevel, level);
            maxLevel = Math.max(maxLevel, level);
        }

        int minLevel() {
            return minLevel;
        }

        int maxLevelExclusive() {
            return maxLevel + 1;
        }

        void copyTo(int[] targetOffsets, TIntArrayList targetData) {
            System.arraycopy(offsets, 0, targetOffsets, 0, offsets.length);
            targetData.addAll(data);
        }
    }
}
