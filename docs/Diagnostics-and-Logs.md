# PZTools logs, diagnostics, and useful issue reports

This reference applies to PZWorldEd, TileZed, and BuildingEd in the portable
PZTools distribution.

## Where logs are stored

Each application writes a new UTF-8 log at startup:

```text
PZTools-Qt5-Latest/
└── settings/
    └── logs/
        ├── PZWorldEd-YYYYMMDD-HHMMSS-mmm-PID.log
        ├── TileZed-YYYYMMDD-HHMMSS-mmm-PID.log
        └── BuildingEd-YYYYMMDD-HHMMSS-mmm-PID.log
```

The logger retains the newest 20 files per application. A new run never
appends to an unrelated older session.

The first lines identify:

- the exact log path;
- the portable installation root;
- the application configuration directory;
- the active INI file.

This makes it possible to detect a copied executable, a mixed release, or a
different portable installation immediately.

## How to read a log line

```text
2026-08-03 17:52:43.735 [INFO] [pid:16620 thread:2f34 name:MiniMapRenderWorker] message
```

| Field | Meaning |
|---|---|
| Timestamp | Local date and time, including milliseconds. |
| `DEBUG` | Developer detail. Usually useful only around the failing operation. |
| `INFO` | Normal startup, selected paths, counts, phases, and successful validations. |
| `WARNING` | Recoverable problem, missing optional data, rejected entry, or fallback. Review the surrounding lines. |
| `CRITICAL` | The requested operation could not complete, or a serious runtime failure was caught. |
| `FATAL` | Process-ending Qt failure. |
| PID | Identifies the exact process that wrote the file. |
| Thread and optional name | Distinguishes the UI thread from readers, renderers, and minimap workers. |
| Source suffix | Development builds may include the source file and line. |

An unhandled Windows exception is logged with its exception code, address,
module path, and module-relative offset when Windows provides them. An
unhandled C++ exception or `std::terminate()` is also recorded.

## PNG color-profile warning policy

Only the exact libpng warning
`iCCP: known incorrect sRGB profile` is suppressed. It is non-fatal embedded
color-profile noise and does not identify a bad tile layout or failed decode.
All other PNG warnings and errors remain logged.

If Windows reports **Bad Image** before an application window opens, the Qt
runtime or DLL loader may have failed before PZTools could install its logger.
In that case, record the DLL named by Windows and re-extract or verify the
release before changing editor preferences.

## What normal startup can look like

The editors intentionally discover and preload the complete valid Tiles
catalogue. A current 2x installation can take tens of seconds and several
gigabytes of memory. Normal logs include discovery, registration, decode
progress, loaded/missing/unresolved totals, and timing summaries.

These are different states:

- **loaded**: a readable 2x or 1x PNG was resolved and decoded;
- **missing**: neither scale could be read and a placeholder is used;
- **unresolved catalogue entry**: metadata exists but no image has yet been
  confirmed for that logical name;
- **invalid**: dimensions, grid geometry, format metadata, or encoded data
  failed a specific validation.

Do not report only a changing total. Include the nearby paths, summary, and
first warning or critical line that explains why the total changed.

## Reproducing a problem

1. Close unrelated PZTools instances.
2. Start the deployed executable from `PZTools-Qt5-Latest/bin`.
3. Perform the smallest sequence that reproduces the issue.
4. Close the application normally when possible.
5. Sort `settings/logs` by modification time and select the newest file for
   that application and PID.
6. Keep roughly 30 lines before and after the first relevant warning or
   critical message. Attach the complete log for a crash or startup problem.

Do not use a log from a source build directory to describe behavior observed
in a different deployed executable.

## Maintainer regression commands

Run these against the deployed binaries, where the matching Qt DLLs and
plugins are present.

### TileZed and BuildingEd

```powershell
.\TileZed.exe --validate-automapper-rules
.\TileZed.exe --validate-brush-performance
.\TileZed.exe --validate-depthmap-editor
.\TileZed.exe --validate-pack-tools
.\TileZed.exe --validate-tiledef-split
.\TileZed.exe --validate-tileset-catalog
.\BuildingEd.exe --validate-building-categories
.\BuildingEd.exe --validate-tileset-catalog
```

Data-dependent checks:

```powershell
.\TileZed.exe --validate-loot-distributions <game-root> [project-or-mod-root]
.\BuildingEd.exe --validate-loot-distributions <game-root> [project-or-mod-root]
```

GUI render checks write an explicit PNG:

```powershell
.\TileZed.exe --render-pack-comparator <output.png>
.\TileZed.exe --render-pack-extractor <output.png>
.\TileZed.exe --render-tiledef-comparator <output.png>
.\TileZed.exe --render-loot-distributions <game-root> <output.png> [project-root]
```

### PZWorldEd

```powershell
.\PZWorldEd.exe --validate-preview-overlays
.\PZWorldEd.exe --validate-streets=<streets.xml>
.\PZWorldEd.exe --validate-bmp-generation=<project.pzw>
.\PZWorldEd.exe --validate-ingamemap=<worldmap.xml>
.\PZWorldEd.exe --validate-worldgen-preview=<game-or-WorldGen-path>
.\PZWorldEd.exe --validate-worldgen-prefab-import=<source.tmx-or-tbx>
.\PZWorldEd.exe --validate-worldgen-project-overlay=<game-path>::<project-overlay-path>
```

WorldGen render checks:

```powershell
.\PZWorldEd.exe --render-worldgen-preview=<game-or-WorldGen-path> --worldgen-preview-output=<output.png>
.\PZWorldEd.exe --render-worldgen-prefab=<game-or-WorldGen-path> --worldgen-preview-output=<output.png>
.\PZWorldEd.exe --render-worldgen-prefab-window=<game-or-WorldGen-path> --worldgen-preview-output=<output.png>
```

`--validate-ingamemap` writes
`<worldmap.xml>.validated.bin`. Remove only that exact generated file after
review. Render commands overwrite only the explicitly selected output path.

Because these are Windows GUI-subsystem executables, a PowerShell invocation
may return before the process exits and may not provide a useful
`$LASTEXITCODE`. Wait for the exact process to finish and confirm the explicit
`PASS` or `FAIL` line in the newest matching log.

## What each main validator proves

| Command | Coverage |
|---|---|
| `--validate-automapper-rules` | Preferred manifest selection, legacy manifest compatibility, and isolation of WorldEd `Rules.txt`. |
| `--validate-brush-performance` | Sparse brush Undo storage and representative painting data integrity. |
| `--validate-depthmap-editor` | Geometry parsing, depth-atlas editing, and PNG output. |
| `--validate-pack-tools` | Pack reading, reconstructed images, hashing, comparison, and extraction. |
| `--validate-tiledef-split` | Build 42 TileDef limits and the 512/1 split workflow. |
| `--validate-tileset-catalog` | Recovery and loading of valid one-row sheets instead of zero-column rejection. |
| `--validate-building-categories` | Templates, Tile mode, Furniture groups, Ortho/Iso categories, Lua placement, and Undo/Redo. |
| `--validate-loot-distributions` | Game/project registry loading, merged provenance, and unresolved-reference policy. |
| `--validate-preview-overlays` | Raster/OpenGL environment-preview overlay anchor alignment. |
| `--validate-bmp-generation` | Project BMP-to-TMX inputs, including `Rules.txt` versus `MapBaseXML.txt`. |
| `--validate-worldgen-preview` | WorldGen definitions, inheritance, catalogue counts, and representative preview generation. |
| `--validate-worldgen-prefab-import` | Strict z=0 TMX/TBX-to-prefab conversion and format limits. |

## Minimum useful issue report

Include:

- application name;
- release date or commit;
- exact action and smallest reproduction sequence;
- expected and observed behavior;
- affected PZW, TMX, TBX, `.tiles`, `.pack`, PNG, Rules, or Lua path;
- project grid format, when relevant;
- newest matching log;
- screenshot for visual/layout/rendering issues;
- whether Raster and OpenGL behave differently, when renderer code is
  involved;
- whether the problem also occurs with Automapper Interactive disabled;
- whether the file is game data, project data, or generated output.

For a performance report, also include the approximate map dimensions,
selected tool/brush size, visible docks, tileset scale, and pause duration.

## Before declaring a regression

- Confirm the executable path belongs to the current portable release.
- Confirm the log's installation root matches that release.
- Confirm the project is using the intended 300 or 256 grid.
- Confirm `config` is the catalogue directory and `settings` is not.
- Confirm the Tiles path points to the parent Tiles tree, not to an unrelated
  extracted folder.
- For Automapper, confirm the TMX is saved and use
  `automapping-rules.txt`; press **Reload** after correcting a rule.
- For WorldGen or loot editing, confirm the game path is read-only and the
  project output is outside the game installation.
