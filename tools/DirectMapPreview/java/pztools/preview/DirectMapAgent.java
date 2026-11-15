package pztools.preview;

import java.io.IOException;
import java.io.InputStream;
import java.lang.classfile.ClassFile;
import java.lang.classfile.ClassHierarchyResolver;
import java.lang.classfile.ClassTransform;
import java.lang.classfile.CodeBuilder;
import java.lang.classfile.CodeElement;
import java.lang.classfile.CodeTransform;
import java.lang.classfile.Opcode;
import java.lang.classfile.instruction.InvokeInstruction;
import java.lang.classfile.instruction.ReturnInstruction;
import java.lang.constant.ClassDesc;
import java.lang.constant.MethodTypeDesc;
import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.function.Consumer;

public final class DirectMapAgent {
    private static final String ISO = "zombie/iso/";
    private static final String STATES = "zombie/gameStates/";
    private static final String WORLD_GEN = ISO + "worldgen/";
    private static final ClassDesc HOOKS = ClassDesc.of("pztools.preview.DirectMapHooks");
    private static final String MAP_FILES = "L" + ISO + "MapFiles;";
    private static final String CHUNK = "L" + ISO + "IsoChunk;";
    private static final String CELL = "L" + ISO + "IsoCell;";
    private static final String HEADER = "L" + ISO + "LotHeader;";
    private static final String META_GRID = "L" + ISO + "IsoMetaGrid;";
    private static final String META_CELL = "L" + ISO + "IsoMetaCell;";
    private static final String ROOM = "L" + ISO + "RoomDef;";
    private static final String LOT = "L" + ISO + "IsoLot;";
    private static final String LOAD_CHUNK = "(" + CELL + "II" + CHUNK + ")Z";
    private static final String LOAD_LOT = "(" + MAP_FILES
            + "Ljava/lang/Integer;Ljava/lang/Integer;Ljava/lang/Integer;Ljava/lang/Integer;" + CHUNK + ")V";
    private static final String STATE_ACTION = "()L" + STATES + "GameStateMachine$StateAction;";

    private DirectMapAgent() {}

    public static void premain(String options, Instrumentation instrumentation) throws IOException {
        ClassLoader gameLoader = ClassLoader.getSystemClassLoader();
        ClassFile classFile = ClassFile.of(ClassFile.ClassHierarchyResolverOption.of(
                ClassHierarchyResolver.ofResourceParsing(gameLoader).cached()));
        Map<String, byte[]> stock = new HashMap<>();
        Map<String, byte[]> patches = new HashMap<>();
        Map<String, Hook> hooks = Map.of(
                ISO + "MapFiles", new Hook("postLoad", "()V", DirectMapAgent::mapFiles, DirectMapAgent::priority),
                ISO + "IsoMetaGrid", new Hook("CreateStep2", "()V", null, DirectMapAgent::metadata),
                ISO + "IsoMetaCell", new Hook("addRoom", "(" + ROOM + "II)V", DirectMapAgent::room, null),
                ISO + "CellLoader", new Hook("LoadCellBinaryChunk", LOAD_CHUNK,
                                            DirectMapAgent::loadChunk, DirectMapAgent::trackStockLoads),
                ISO + "IsoLot", new Hook("load", LOAD_LOT, DirectMapAgent::loadLot, null),
                ISO + "LotHeader", new Hook("getZombieIntensityForChunk", "(" + HEADER + "II)I",
                                           DirectMapAgent::zombieIntensity, null),
                WORLD_GEN + "WorldGenChunk", new Hook("genRandomChunk",
                        "(" + CELL + "L" + WORLD_GEN + "ChunksCache;" + CHUNK + "Ljava/util/EnumMap;)V",
                        null, DirectMapAgent::proceduralChunk),
                STATES + "GameLoadingState", new Hook("update", STATE_ACTION, null, DirectMapAgent::loading),
                STATES + "IngameState", new Hook("enter", "()V", null, DirectMapAgent::enteredGame));
        for (Map.Entry<String, Hook> entry : hooks.entrySet()) {
            String owner = entry.getKey();
            try (InputStream bytecode = gameLoader.getResourceAsStream(owner + ".class")) {
                if (bytecode == null) {
                    throw new IOException("Missing Project Zomboid runtime class " + owner);
                }
                byte[] original = bytecode.readAllBytes();
                stock.put(owner, original);
                patches.put(owner, patch(classFile, owner, original, entry.getValue()));
            }
        }

        DirectMapSnapshot snapshot = DirectMapSnapshot.fetch(options);
        instrumentation.addTransformer(new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader, String className, Class<?> type,
                                    ProtectionDomain domain, byte[] bytecode) {
                if (loader != gameLoader || !patches.containsKey(className)) {
                    return null;
                }
                if (!Arrays.equals(bytecode, stock.get(className))) {
                    throw new IllegalStateException("Unexpected bytecode transformer for " + className);
                }
                return patches.get(className);
            }
        });
        DirectMapHooks.configure(snapshot);
        for (DirectMapSnapshot.Cell cell : snapshot.cells()) {
            System.out.printf("[PZ Direct Map] Snapshot cell %d,%d: %d squares, %d rooms, %d buildings, %d sprites%n",
                    cell.x(), cell.y(), cell.squareCount(), cell.roomCount(), cell.buildings().size(),
                    cell.tiles().size());
        }
    }

    private record Hook(String method, String descriptor, Consumer<CodeBuilder> enter,
                        BiConsumer<CodeBuilder, CodeElement> body) {}

    private static byte[] patch(ClassFile classFile, String owner, byte[] bytecode, Hook hook) throws IOException {
        int[] matches = {0};
        boolean[] patched = {false};
        CodeTransform transform = new CodeTransform() {
            @Override
            public void atStart(CodeBuilder builder) {
                if (hook.enter() != null) {
                    hook.enter().accept(builder);
                }
            }

            @Override
            public void accept(CodeBuilder builder, CodeElement element) {
                if (hook.body() != null) {
                    hook.body().accept(builder, element);
                } else {
                    builder.with(element);
                }
                if (builder != null) {
                    patched[0] = true;
                }
            }
        };
        byte[] result = classFile.transformClass(classFile.parse(bytecode),
                ClassTransform.transformingMethodBodies(method -> {
                    if (!method.methodName().equalsString(hook.method())
                            || !method.methodType().equalsString(hook.descriptor())) {
                        return false;
                    }
                    ++matches[0];
                    return true;
                }, transform));
        if (matches[0] != 1 || !patched[0]) {
            throw new IOException("Unsupported Project Zomboid bytecode in " + owner + "." + hook.method());
        }
        return result;
    }

    private static boolean invokes(CodeElement element, String owner, String name, String descriptor) {
        return element instanceof InvokeInstruction invoke
                && invoke.owner().asInternalName().equals(owner)
                && invoke.name().equalsString(name)
                && invoke.type().equalsString(descriptor);
    }

    private static CodeBuilder call(CodeBuilder builder, String name, String descriptor) {
        return builder.invokestatic(HOOKS, name, MethodTypeDesc.ofDescriptor(descriptor));
    }

    private static void mapFiles(CodeBuilder builder) {
        call(builder.aload(0), "registerMap", "(" + MAP_FILES + ")V");
    }

    private static void priority(CodeBuilder builder, CodeElement element) {
        if (element instanceof ReturnInstruction instruction && instruction.opcode() == Opcode.RETURN) {
            call(builder.aload(0), "preserveNativeCellPriority", "(" + MAP_FILES + ")V");
        }
        builder.with(element);
    }

    private static void room(CodeBuilder builder) {
        call(builder.aload(0).aload(1), "excludeRoom", "(" + META_CELL + ROOM + ")Z")
                .ifThen(CodeBuilder::return_);
    }

    private static void metadata(CodeBuilder builder, CodeElement element) {
        if (invokes(element, ISO + "IsoMetaGrid", "consolidateBuildings", "()V")) {
            call(builder.dup(), "installMetadata", "(" + META_GRID + ")V");
        }
        builder.with(element);
    }

    private static void loadChunk(CodeBuilder builder) {
        call(builder.iload(1).iload(2), "containsChunk", "(II)Z").ifThen(body -> {
            call(body.aload(0).iload(1).iload(2).aload(3), "loadChunk", LOAD_CHUNK).ireturn();
        });
    }

    private static void loadLot(CodeBuilder builder) {
        call(builder.aload(1), "isPreviewMap", "(" + MAP_FILES + ")Z").ifThen(body -> {
            call(body.aload(0).aload(4).aload(5).aload(6), "readChunk",
                    "(" + LOT + "Ljava/lang/Integer;Ljava/lang/Integer;" + CHUNK + ")V").return_();
        });
    }

    private static void zombieIntensity(CodeBuilder builder) {
        call(builder.aload(0), "isPreviewHeader", "(" + HEADER + ")Z")
                .ifThen(body -> body.iconst_0().ireturn());
    }

    private static void proceduralChunk(CodeBuilder builder, CodeElement element) {
        if (invokes(element, ISO + "IsoChunk", "setMinMaxLevel", "(II)V")) {
            call(builder, "setProceduralChunkLevels", "(" + CHUNK + "II)V");
        } else {
            builder.with(element);
        }
    }

    private static void trackStockLoads(CodeBuilder builder, CodeElement element) {
        if (element instanceof ReturnInstruction instruction && instruction.opcode() == Opcode.IRETURN) {
            call(builder.dup(), "stockChunkLoaded", "(Z)V");
        }
        builder.with(element);
    }

    private static void loading(CodeBuilder builder, CodeElement element) {
        builder.with(element);
        if (invokes(element, "zombie/input/Mouse", "isButtonDown", "(I)Z")) {
            call(builder, "confirmLoading", "(Z)Z");
        }
    }

    private static void enteredGame(CodeBuilder builder, CodeElement element) {
        if (element instanceof ReturnInstruction instruction && instruction.opcode() == Opcode.RETURN) {
            call(builder, "enteredGame", "()V");
        }
        builder.with(element);
    }
}
