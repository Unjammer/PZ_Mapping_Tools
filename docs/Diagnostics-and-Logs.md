# PZTools logs and useful issue reports

This reference applies to PZWorldEd, TileZed, and BuildingEd in the portable
PZTools distribution.

## Log location

Each application writes a new UTF-8 log under:

```text
PZTools-Qt5-Latest/
`-- settings/
    `-- logs/
        |-- PZWorldEd-YYYYMMDD-HHMMSS-mmm-PID.log
        |-- TileZed-YYYYMMDD-HHMMSS-mmm-PID.log
        `-- BuildingEd-YYYYMMDD-HHMMSS-mmm-PID.log
```

The newest 20 files per application are retained. A new session never appends
to an unrelated older log.

The first lines identify the log path, installation root, active
configuration directory, settings file, operating system, CPU, logical
processor count, total and available RAM, display adapters, Qt version, ABI,
and process bitness.

No username, hostname, serial number, IP address, or stable hardware identifier
is collected. Remote desktop sessions can expose only the remote display
adapter. When WorldEd creates an OpenGL context, it also records the actual
OpenGL vendor, renderer, and version selected by the driver.

## Reading a log line

```text
2026-08-03 17:52:43.735 [INFO] [pid:16620 thread:2f34 name:MiniMapRenderWorker] message
```

| Field | Meaning |
|---|---|
| Timestamp | Local date and time with milliseconds |
| `DEBUG` | Detailed support information around an operation |
| `INFO` | Normal startup, selected paths, counts, phases, and completion |
| `WARNING` | Recoverable issue, missing optional data, rejected entry, or fallback |
| `CRITICAL` | The requested operation could not complete |
| `FATAL` | Process-ending Qt failure |
| PID | Exact process that wrote the file |
| Thread and name | UI, reader, renderer, or worker context |

An unhandled Windows exception is recorded with its exception code, address,
module path, and module-relative offset when Windows provides them.

## In-application warning and error counters

PZWorldEd, TileZed, and BuildingEd display notification boxes in the
lower-right corner of their main window when the current process records a
problem. The appearance follows the Project Zomboid error indicator closely:

- red `ERROR` counts critical, fatal, and explicitly recorded errors
- orange `WARNING` counts recoverable warnings

The counters receive Qt warning and critical messages from the UI and worker
threads, including messages that do not open a dialog. Warning and critical
message boxes are captured too. When a message box repeats the same message
that was just logged, it contributes only one entry.

Each new message slides the box above the application status area for three
seconds, then hides it while retaining the list. Click a visible notification
box to open **Application messages**. The window can show all messages or one
severity, follows new messages while it remains open, and exposes the
timestamp, source file or category, thread, and complete message. **Copy
selected**, **Copy all**, and **Open logs folder** help prepare an issue report.
**Clear** resets both counters for the current application session. Clearing
the notification list never deletes a log file.

## Normal startup

The editors intentionally discover and preload the complete valid Tiles
catalogue. A large 2x installation can take several seconds and use multiple
gigabytes of memory. Normal logs include discovery, registration, decoding,
loaded and missing totals, and timing summaries.

- **loaded** means a readable PNG resolved and decoded.
- **missing** means neither supported scale resolved and a placeholder is used.
- **unresolved** means metadata exists but no readable image was confirmed.
- **invalid** means image geometry, metadata, or encoded data was rejected.

Only the exact libpng warning `iCCP: known incorrect sRGB profile` is
suppressed. Other PNG warnings and errors remain visible.

If Windows reports **Bad Image** before a window opens, the runtime can fail
before the logger starts. Record the named DLL, then re-extract the release
before changing preferences.

## Rules and Blends delays

TileZed records slow BMP Rules regeneration. These messages concern the
terrain `Rules.txt` imported through BMP Tools, not an Automapper manifest.

Each TMX contains one Rules snapshot and one Blends snapshot. Import replaces
the selected snapshot in memory. Saving persists the replacement. Reload after
Import is unnecessary. Identical imports are treated as a no-op.

WorldEd BMP To TMX provides **Update Rules/Blends metadata only** for existing
TMX maps assigned to all world cells or only the selected cells. It replaces
the stored Rules and Blends paths, aliases, rules, and blends. Bitmap pixels,
layers, objects, tilesets, no-blend masks, and edge settings remain unchanged.
WorldEd lists the files first, skips identical snapshots, writes atomically,
and creates a dated project backup before changing any TMX file.

Older TMX files can retain unresolved tileset references. Removing only the
tilesets that no longer resolve to a PNG can reduce unnecessary rule work.
Project Doctor can inspect and repair those references with a backup.

TileZed also records a rate-limited warning when ground-brush preparation takes
40 ms or more. The line separates temporary Rules and Blends calculation from
automatic blend-tile cleanup and includes the brush bounds and active metadata
counts.

The deployed validator can measure a specific map while checking that indexed
Blends and sparse dirty regions produce the same layers as their compatibility
paths. It also verifies that the Sand rule resolves only its declared tiles
when similarly named test or custom sheets are present. When the benchmark map
contains `blends_natural_01_TEST`, the validator removes and restores that
sheet, rebuilds the automatic layers, and checks the Sand output after both
operations:

```powershell
TileZed.exe --validate-brush-performance C:\path\to\map.tmx
```

Without a TMX argument, the command runs the built-in brush, undo, Rules, and
Blends checks only.

## Reproducing a problem

1. Close unrelated PZTools instances.
2. Start the executable from the current release `bin` directory.
3. Perform the shortest sequence that reproduces the issue.
4. Close the application normally when possible.
5. Select the newest log for that application and PID.
6. Keep the complete log for crashes and startup failures.

## Useful issue report

Include:

- application name and release
- exact steps
- expected and observed behavior
- newest matching log
- relevant PZW, TMX, TBX, PNG, Rules, Blends, Lua, `.tiles`, or `.pack` file
- project grid format when relevant
- screenshot for visual or layout issues
- Raster and OpenGL behavior when rendering is involved
- approximate map dimensions and brush size for performance issues

Do not include personal information that is unrelated to the problem.
