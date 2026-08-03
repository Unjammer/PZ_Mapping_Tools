# PZTools Unofficial Changelog

This document describes the functional differences between the current PZTools
Unofficial suite and the Tim Baker `basements` branches used as its clean upstream
bases.

Reference date: August 2, 2026.

| Project | Initial Tim Baker baseline | Local branch | Committed local revision |
|---|---|---|---|
| WorldEd | `80e3511cae257f51250df035141243ba6b9cf7cc` | `integration/qt5-basements` | working tree |
| TileZed / BuildingEd | `f9489a9ba605f8dc503c205f19655644798b9ec4` | `integration/qt5-basements` | working tree |

This changelog includes the current working trees under `integration/WorldEd`
and `integration/TileZed`. No public repository has been modified or pushed.

## August 2, 2026 / Distribution licensing

- The portable build now includes a consolidated copying notice, complete
  third-party component index, upstream author credits, the applicable
  GPL/BSD/Qt/LGPL and dependency license texts, and a corresponding-source
  offer.
- Tim Baker's July 30 Qt 6 packaging commits were reviewed as distribution
  compliance changes only. They do not replace the existing GPL/BSD source
  licenses or add an anti-fork condition.

## August 2, 2026 / Crash and legacy-data safeguards

- Opening a TMX no longer invokes the object-default editor unless the user
  explicitly selects the object-creation tool.
- Valid 8 x 1 sheets such as `Giblet_00` no longer fail metadata saving with a
  zero-column geometry error in TileZed, BuildingEd, or WorldEd.
- Official Build 42 `LadderS`, `LadderE`, `LadderN`, and `LadderW` properties
  are accepted and preserved.
- Every map retains its complete ordered tileset header for adjacent-cell
  correctness, and every declared sheet is made ready before rendering.
  PZ BMP rules and blend layers may use sheets absent from normal layer GIDs.
- Startup loads every discovered PNG. The resolver searches all 2x locations
  first and falls back to all 1x locations, without excluding debug,
  placeholder, custom-pack, or nested sheets.
- WorldEd clips 300-cell InGameMap polygons at 256-cell boundaries and guards
  binary exports against the engine renderer's signed 16-bit geometry limits.
  Any needed highway simplification affects only the temporary export copy.

## August 2, 2026 / Tiledefs, shared themes, and Street Names layout

- Tile Properties now supports separate Build 42 mod (512/512) and base-game
  (1024/1024) validation targets. Damaged or oversized files load in recovery
  mode, and a repair command can reassign IDs and emit numbered `.tiles` plus
  `.tiles.txt` series without discarding tile metadata.
- The repair validator covers a 513-sheet input split into 512/1 definitions.
  A single PNG exceeding the 512-tile sheet limit is rejected with a specific
  explanation because the image itself must be split.
- Themes can be synchronized across TileZed, BuildingEd, and WorldEd using the
  shared portable configuration.
- WorldEd tabs Street Names beside Search in the lower-left dock area, with
  Search selected on the default layout.

## August 1, 2026 / Legacy TMX header compatibility

- WorldEd and TileZed once again resolve every declaration in each TMX/TBX
  tileset header before rendering, rather than trusting only the reduced
  used-tileset set.
- This restores correct current/adjacent-cell rendering for older exported
  projects where every cell carries the same catalogue in a different
  `firstgid` order. Valid tiles no longer become red `???` placeholders merely
  because their declaration was omitted from another cell's reduced set.
- The image cache continues to share decoded PNG data, but the complete
  per-map declarations deliberately restore the original tools' higher memory
  and loading cost where required for compatibility.

## August 1, 2026 / TileZed geometry-based depth-map editor

- **Tools > Depth Map Editor...** edits Build 42 `tileGeometry.txt` primitives
  and `DEPTH_<tileset>.png` atlases together.
- It implements the game's eight-column 128 x 256 atlas layout, isometric
  30/315-degree orthographic projection, per-pixel ray intersections, and
  normalized depth calculation.
- Tiles support selectable `XY`, `XZ`, and `YZ` polygons, boxes, and
  cylinders. Wireframes are drawn over the source tile, can be selected and
  moved directly in X/Z, and expose exact transform and shape fields.
- Corrected the retained yellow selection brush that could fill the complete
  preview and hide both the source sprite and the wireframe. Selected
  primitives now show an XYZ gizmo and may also be selected by clicking inside
  their projected bounds.
- Tile IDs now use an explicit badge painted into every thumbnail, keeping
  identifiers 10–63 visible rather than clipping them after tile 9.
- Selected or complete tile geometry can be rasterized with an optional source
  opacity mask. Existing pixel painting remains in a separate retouching tab.
- Version-1 and version-2 geometry are readable. Version-2 writes are atomic
  and replace only the selected tileset block, preserving unrelated tilesets
  and existing tile properties.
- `Ctrl+S` saves both geometry and the full Build 42 depth atlas. The automated
  validator covers rasterization, PNG output, geometry round-tripping, and
  preservation of unrelated geometry-file content. It also verifies the
  selection preview and can load a real Build 42 primitive into the editor UI.

## August 1, 2026 / Night-preview exposure

- The DAY/NIGHT prototype is hidden and disabled in WorldEd, TileZed, and
  BuildingEd. The current compositor is not a faithful replacement for the
  game's Lighting64 renderer and is therefore no longer presented as a
  dependable mapping preview.
- The implementation is retained internally for later renderer work, starts
  off, and cannot be enabled from the current user interface.

## July 31, 2026 / World Street Name Editor

- The disabled legacy Road dock is replaced by a `streets.xml` editor for
  Build 42.20 street-name data.
- WorldEd loads existing version-1 street files and displays named,
  variable-width polylines over the World view. It supports point creation and
  dragging, segment insertion, street/point deletion, reverse, split, and a
  dedicated undo/redo history.
- Coordinates account for project cell size and world origin. The XML reader
  and writer validate geometry and use atomic replacement on save.
- The overlay now works in World and Cell scenes, with per-cell culling,
  show/hide control, adjustable display thickness, logical-width scaling, and
  a high-contrast selected-street style.
- A visible street can be selected and highlighted directly from either
  canvas while navigation mode is active. Empty clicks and ordinary cell
  double-clicks continue to reach WorldEd.
- The list provides live case-insensitive name filtering and sortable Street,
  Width, and Points headers, including numeric sorting for the latter two.
- Labels now have rounded, bordered high-contrast backgrounds. Distant
  non-selected labels are culled and screen-space collision checks prevent
  overlaps, while the selected street remains labelled.
- Street Names is a movable, floatable and dockable tool window. It receives
  a compact first-run floating layout and uses icon-sized geometry controls
  with tooltips instead of forcing the normal left dock to remain wide.
- Navigation, geometry editing, and creation are separate, clearly labelled
  states. Only explicit Edit/Create modes intercept scene input, so normal
  map selection and World-view double-click cell opening remain untouched.
- Objects remains the default active dock tab when Street Names is first
  introduced to a saved or fresh layout.
- Saving the project through the menu or `Ctrl+S` also updates `streets.xml`
  when street data, street edits, or an existing street file are present.
- `streets.xml` is visible in WorldEd's Maps browser without being treated as
  an image-preview input.

## July 31, 2026 / Global day/night lighting preview

- WorldEd, TileZed and BuildingEd expose the same **Night Preview** action and
  bottom DAY/NIGHT control. All three applications start with preview modes
  disabled.
- WorldEd places **DAY/NIGHT**, **POWER**, **SNOW**, and **JUMBO** preview
  toggles together in the bottom view-state strip before the zoom control.
- The visual-only overlay dims the scene and reads the Build 42
  `lightswitch`, `lightR`, `lightG`, `lightB`, and `LightRadius` tiledefs to
  render colored halos through a composition mode shared by the raster and
  OpenGL backends. If RGB tiledefs are unavailable, WorldEd derives the color
  from the powered sprite before using the configured fallback. It
  deliberately excludes Lighting64's vision-cone simulation.
- Switch tiles without RGB illuminate their containing mapper-defined room,
  matching the separate room-controller behavior in the Build 42 loader.
- Registered tiledefs and embedded map properties are combined with
  case-insensitive lookup. If no `.tiles` file is configured, vanilla
  `lighting_outdoor_*` lamps and the known `lighting_indoor_01` switch row
  receive a conservative fallback; explicit RGB/radius definitions win.
- World view provides the global night treatment; cell, composite, tile and
  building views provide the detailed tiledef and room preview without
  intercepting editor input.
- The DAY/NIGHT menu provides live darkness, light-intensity, fallback-radius,
  and fallback-color controls.
- POWER draws matching same-index `*_on` images over their unchanged source
  tiles at the original ordered render position. Transparent light pixels no
  longer replace lamp posts or jump above roofs and upper floors.
- SNOW uses `SnowTile` definitions plus the Build 42 roofs fallback mapping to
  `e_roof_snow_1` across every composite level. JUMBO recognizes
  `jumbo_tree_01_0` and chooses a stable coordinate-derived variant from the
  available catalogue.
- JUMBO selects only Build 42 XL/XXL sheets and reconstructs each tree as the
  main sprite `N` plus its `IsoTreeJumbo` treetop `N+6`. Raster and OpenGL
  renderers insert replacements and transparent overlays at the source tile's
  exact ordered position instead of drawing scene-wide sprites.
- All environment previews are non-destructive: tile IDs, map layers,
  TMX/TBX files, and lot output remain unchanged.

## July 31, 2026 / WorldEd cell and OpenGL regression fixes

- Culled Street Names labels are no longer represented by null graphics
  entries, eliminating thousands of `removeItem(nullptr)` warnings and their
  view-switch delay.
- Embedded TMX declarations remain registered and are all resolved before
  current or adjacent-cell rendering. The short-lived used-GID-only
  optimization was removed after it produced red unknown tiles for PZ
  procedural sheets and legacy headers.
- The complete global catalogue is decoded once at startup and subsequent
  cells reuse the shared image cache.
- OpenGL 3.3 uses a compatibility context again. The forced core context
  compiled the shader but could not draw the legacy VBOs without an explicit
  VAO, which caused the tileless cell display.
- The 256/300 project-grid badge is rendered to a raster image before OpenGL
  compositing, avoiding corrupted glyphs after the native VBO pass.

## July 31, 2026 / Zero-column and Jumbo crash hardening

- Tile metadata lookup no longer divides by zero when an embedded TMX/TBX
  declaration temporarily exposes a zero-column tileset during lazy image
  resolution. It reuses the matching catalogue geometry when available and
  otherwise defers the optional metadata lookup.
- Legacy Jumbo trunk/leaves overlays now validate the column count and target
  tile before use. An incomplete sheet skips only the optional overlay instead
  of crashing the renderer.
- The same guards are present in WorldEd's OpenGL cell renderer and in the
  shared TileZed/BuildingEd renderer.
- Tile Definitions now detects resized sheets inside separate `.pack`
  directories through the shared 1x/2x resolver. Existing properties are
  retained by tile coordinate, the matrix is updated, and the open
  `def.tiles` document is marked for saving.

## July 30, 2026 / WorldEd renderer selection and cell display

- CellView continues to support both OpenGL 3.3 and Qt raster/software
  rendering. Preferences identifies the fallback clearly and WorldEd records
  the active backend in its log.
- Fresh installations hide invisible helper-tile wireframes by default. The
  overlay remains available from **View > Show Invisible Tiles**.
- The 256/300 project-grid badge no longer contains mojibake caused by source
  encoding of its separator and multiplication glyphs.

## July 30, 2026 / BuildingEd browser crash fix

- Activating a TBX in BuildingEd's welcome-page file browser now opens it
  through `BuildingEditorWindow`, the same proven path used by **Open
  Building**.
- The browser no longer dereferences TileZed's absent `MainWindow` singleton
  when BuildingEd runs as the standalone executable. Browser opens and failures
  are recorded with the full TBX path.
- BuildingEd preloads every discovered sheet before Tile mode, categories,
  furniture previews, or a TBX document is exposed. Availability therefore
  never depends on which palette the user clicked first.
- Metadata-only PNG discovery suppresses per-sheet BuildingEd UI notifications
  and refreshes the catalogue once after the scan. This removes the former
  discovery/full-catalogue double pass.
- Tileset readiness logs now report actual decode attempts separately from
  already-ready requests, eliminating misleading duplicate-preload messages.
- The internal BuildingEd validator exercises every template and category,
  Tile mode, the furniture catalogue, and a Lua-created FurnitureObject through
  placement, Undo and Redo.

## July 30, 2026 / BuildingEd Lua furniture objects

- `building.apiVersion` is now 3. Lua scripts can enumerate the furniture
  catalogue, inspect orientations and component tiles, and reverse-match a
  loose tile name with `findFurniture(tileName)`.
- `placeFurniture(level,x,y,group,index,orientation)` creates a real
  catalog-backed `FurnitureObject`, returns its transactional object index, and
  participates in the script's single undo operation.
- New furniture objects remain detached until the script succeeds; errors and
  cancellation delete them without changing the open building.

## July 29, 2026 / Project Zomboid 42.20 update

- Tile-definition loading follows the B42.20 runtime order, including
  `newtiledefinitions`, erosion, overlays, chunk-caching, NoiseWorks patch,
  and both native Jumbo definition files.
- `.patch.tiles` files now merge properties into their base definitions.
  Version-1 file/set/tile limits are validated for both legacy 512-tile files
  and the 1024-tile `newtiledefinitions` format.
- Native 256-cell lot generation no longer applies WorldEd's legacy fake-Jumbo
  randomizer. Explicit B42 Jumbo trees and Biomemap/WorldGen decisions are
  preserved.
- Biomemap ID 171 remains available as **Forced Redbud Jumbo XXL (map
  override)**. It is intentionally enabled per map by `WorldGenOverride.lua`;
  see `docs/PZ-B42.20-Jumbo-Trees.md`.
- Tim Baker's `f492c5c` LotPackViewer z-coordinate fix is integrated.
- Tim Baker's `a7d5a77` cell renderer update is integrated. The legacy
  fixed-function path is replaced by an OpenGL 3.3 shader; the published
  include typo is corrected, core-profile-incompatible quads are rendered as
  triangle fans, CellView explicitly requests a 3.3 core context, and
  unavailable contexts/shaders now produce diagnostics instead of continuing
  through an invalid rendering path.
- The B42.20 room-name catalog and the `connectX`/`connectY` tile properties
  are synchronized with the game scripts.

### Tileset lifecycle and thumbnail loading

- **Tools > Tilesets... > Add Tilesets** now imports a browsed external PNG
  into the active Tiles tree, preserving `1x`/`2x` and `.pack` layout. Name
  collisions are detected by content and never overwritten silently.
- Invalid or undersized sheets are rejected with their selected and resolved
  paths. Zero-column tilesets can no longer crash while `Tilesets.txt` is
  saved, and duplicate registration caused by the directory watcher is safe.
- Valid single-row sheets are supported explicitly. If an in-memory tileset
  temporarily reports zero columns, the tools recover an unambiguous `N x 1`
  geometry from its 1x or 2x PNG before saving `Tilesets.txt`; genuinely
  inconsistent geometry is still rejected.
- Tileset add, path resolution, import, geometry validation and catalog-save
  boundaries are logged and flushed immediately. Unhandled Windows/C++
  exceptions now leave a final diagnostic in each application's log.
- WorldEd startup reports the current sheet and `n / total` while decoding the
  complete discovered catalogue.
- WorldEd, TileZed and BuildingEd watch the configured Tiles directories. A
  newly copied PNG is discovered and loaded without restarting the tool.
- PNG removal is also hot-reloaded safely. Directory-only notifications are
  converted into sheet invalidations, missing placeholders use the correct
  width/height test, and thumbnail workers are prevented from reading a tile
  image while the watcher replaces it.
- BuildingEd now performs the same startup discovery as the other editors,
  including 1x, 2x and immediate `.pack` subdirectories. TBX tileset names are
  matched case-insensitively and resolved through the shared path resolver
  before being reported as missing.
- The **Missing Tilesets** dialog now lists only sheets that remain unloaded
  after resolution. A PNG that is available outside the catalogue is loaded
  on demand and registered in memory instead of being falsely reported absent.
- **Edit > Update Tilesets.txt from Tiles PNGs...** adds new sheets, refreshes
  changed dimensions, preserves meta-enums and keeps `Tilesets.txt.bak`.
- Automatic PNG discovery is memory-only. Startup and directory-watcher scans
  no longer rewrite or inflate `Tilesets.txt`; persistence remains an explicit
  menu command.
- TileZed loads the complete discovered global catalogue and makes every
  embedded TMX declaration ready before opening a map. Clicking a sheet in the
  Tilesets dock is no longer required to repair an already-visible cell.
- Startup discovery registers the dimensions of uncatalogued PNGs without
  decoding all of them. A PNG added during the running session is still loaded
  immediately by the directory watcher.
- The first-run path chooser accepts an installation root containing `config`
  and/or `Tiles`, as well as the directories themselves.
- World thumbnail progress is modeless and event-driven. The GUI no longer
  spins in a modal `processEvents()` loop.
- WorldEd preloads the complete discovered catalogue before opening a project.
  Current and adjacent cells retain their ordered headers and reuse the shared
  cache. A real command-line cell-opening validation path exercises the same
  CellDocument and renderer pipeline as the UI.
- WorldEd and TileZed again register every embedded TMX tileset declaration
  with the shared image cache. Artwork loaded for one map is reused by current
  and adjacent cells regardless of embedded catalog-size differences.
- The WorldEd thumbnail-cache version was advanced so thumbnails containing
  stale red unknown-tile placeholders are regenerated automatically.
- TileZed skips automatic document restoration after an unclean shutdown,
  preventing a malformed previous session from creating a startup crash loop.
- Async reader/renderer threads are named in logs, rendered TMX paths are
  recorded at start and finish, and Windows access violations include the
  faulting DLL and module offset. Missing-image replacement now takes the
  shared tileset-image write lock.
- BMP Tools can optionally replace unknown Main and Vegetation pixels during
  validation. Each fallback is chosen from its Rules palette (black by
  default), and the repair is one undoable operation.
- The suite does not require elevation when extracted to a user-writable
  directory. Its portable `settings`, logs, `config`, Tiles, and edited project
  locations must remain writable; `Program Files` and read-only shares are not
  supported portable locations.

## July 28, 2026 update

### WorldEd responsiveness and thumbnails

- Opening a WorldView cell context menu is now constant-time. The menu no
  longer opens and parses every TMX merely to decide whether **Remove All Empty
  Border Cells** should be enabled. The exhaustive safety scan runs only after
  that command is selected.
- Thumbnail preparation and rendering now use up to four independent workers
  instead of one serialized renderer. Pending WorldView and adjacent-world
  thumbnails are admitted according to the available worker slots.
- An explicit **Recreate Thumbnail** request now starts the forced render
  directly. It no longer loads the old cached PNG first or renders the same TMX
  twice after a cache miss.
- PNG encoding and file writing are performed by the render workers. The GUI
  thread only installs the completed image and writes its small metadata file.
- The redundant GUI-thread layer/tileset rescan immediately before rendering
  was removed. TMX loading already prepares the tilesets used by the map.
- Embedded TMX declarations resolve their artwork through the shared image
  cache by stable tileset name. Declarations are retained to preserve the
  historical cross-cell fallback for generated border maps.
- A tileset finishing its load invalidates the CellScene composite/VBO, not
  merely the widget paint. Stale `??` placeholders, including water tiles in
  adjacent-cell mode, are therefore rebuilt with the real tile image.
- World thumbnail visibility is honored for both the current project and
  adjacent worlds. Saved per-cell visibility is restored independently for
  each PZW.

### WorldEd object and browser workflow

- **Create Object** asks for reusable Name and Type defaults. Values are
  initialized from the selected object or current object group and persisted
  for the next activation.
- Newly drawn WorldEd objects receive the chosen name/type immediately.
- **Create Object** remains active after a rectangle is completed, allowing
  several zones to be drawn in succession. The new object remains selected for
  visual feedback.
- The **Maps** browser now displays headers, file size and last-modified time and
  has a wider default layout.
- The 300 x 300 / 256 x 256 project-grid badge is explicitly invalidated during
  scrolling and resizing, eliminating duplicated or smeared badge remnants.

### Biomemap and foraging zones

- The green Biomemap channel now matches the game's `metazoneHandler` split
  exactly: `Vegitation`, `DeepForest`, `Forest`, `TownZone`, `Farm`,
  `FarmLand`, and `TrailerPark` are rasterized into the image.
- All other vector zone/object types remain outside the image and are reported
  as `objects.lua` content, including vehicle zones, geometries, WorldGen,
  `WaterZone`, and custom types.
- The completion dialog lists the types actually rasterized and those that must
  remain in `objects.lua`.
- Green-channel identifiers are validated without resampling their bytes.
  Unknown IDs stop generation and mixed identifiers inside the game's 8 x 8
  chunk sampling area produce a warning.
- Legacy 300-cell projects generate complete 256 x 256 Biomemap tiles. Boundary
  areas without a full source tile are filled explicitly with neutral values
  and reported; inputs are never silently stretched.

### TileZed workflow fixes

- **Insert Object (O)** now asks for reusable Name and Type defaults and applies
  them to every new object. A single selected object can seed those defaults.
- Insert Object remains available when a tile layer is selected. If the current
  level has no Object Layer, TileZed offers to create one above that level's
  existing visual layers.
- Existing Object Layers on the selected level are reused automatically.
- The **Maps** browser now shows file size and last-modified time with a wider
  default layout.
- The Lua `distanceIndicator` used by scripts such as
  `tool-four-directions.lua` now renders above map content and uses the current
  level instead of being hard-coded to level zero.
- The 300 x 300 / 256 x 256 project-grid badge is available in TileZed and its
  old and new screen regions are invalidated during scrolling, preventing
  visual duplication.
- Opening a map makes its complete ordered tileset header ready before
  exposing the document, including sheets used indirectly by BMP rules and
  blend layers.

### Package and validation

- WorldEd, TileZed and BuildingEd were rebuilt from the current working trees
  with Qt 5.14.2 and MSVC 2017.
- The packaged executables were startup-tested and BuildingEd category
  validation completed successfully.
- `build/PZTools-Qt5-Latest` is the sole retained packaged build; obsolete test,
  backup and intermediate release directories were removed.
- The package remains unsigned. Enterprise Code Integrity policies that require
  trusted signing can still reject `PZWorldEd.exe` or bundled DLLs such as
  `zlib1.dll`; no binary was replaced merely to hide that policy event.

## Suite-wide changes

### Supported platform and build

- Windows 10, Qt 5.14.2, qmake and MSVC 2017 x64 remain the supported
  development target.
- Qt 5 compatibility was restored in code paths that had moved toward Qt 6 APIs.
- Unneeded Qt 6 branches were removed from WorldEd's main execution paths.
- Obsolete Windows build scripts containing machine-specific absolute paths were
  removed from the active sources.
- The portable installation layout was standardized. Executables, Qt libraries,
  plugins, data, documentation, Lua scripts, themes, settings and logs can be
  distributed as one self-contained directory.
- The obsolete 10 × 10 export path was removed. Current exports use the 8 × 8
  layout.
- The abandoned WPF/C# prototype is not part of either source tree. All current
  editor work is implemented in the Qt 5 applications.
- Joke-style error captions such as `It's no good, Jim!` were replaced with
  contextual, professional titles. Validation messages now identify the
  operation, file, color, coordinate or missing resource whenever available.

### Portable settings, sessions and diagnostics

- Mandatory use of `%APPDATA%`, `~/.TileZed` and the Windows Registry was removed.
- Settings are stored in INI files under the installation's local `settings`
  directory.
- Packaged and user-selected catalog files live under `config`, while
  `settings` is reserved for INI state and logs. The first preview's accidental
  copy of catalog `.txt` files into `settings` is migrated automatically.
- The standalone `config.exe` was removed. WorldEd, TileZed and BuildingEd
  display a shared initial-setup dialog only when valid paths cannot be found.
- Shared catalog and Tiles paths are stored once in `settings/PZTools.ini`.
  Valid paths are migrated from older application INI files.
- Portable catalogs no longer treat themselves as a separate upstream source,
  merge with themselves, or rewrite their revision counters on every startup.
  Unchanged startup is read-only for the packaged `config` directory.
- Settings carry an explicit schema version. Incompatible application state can
  be reset on upgrade while the shared paths are preserved.
- A `Tiles` directory beside `config` is detected automatically. Game tile
  images are not distributed with PZTools.
- Selecting a `1x`, `2x` or `custom` subdirectory is normalized to its parent
  Tiles directory, preventing a path that looks valid in the setup dialog but
  cannot be resolved by the editors.
- Paths and setting values preserve Unicode names, including accented, Chinese
  and Cyrillic characters.
- Application logs are written inside the portable installation.
- Main-window geometry, dock state, visible dock tabs, internal splitters and panel
  dimensions are saved and restored.
- Layout state is saved periodically so a normal application shutdown is not the
  only opportunity to persist it.
- Restoring the previous editing session is optional. Independent installations
  and instances no longer have to reopen the same maps.
- WorldEd records whether its previous interactive session closed cleanly. If
  not, automatic project restoration is skipped with a recovery message so a
  malformed recent project cannot create a startup crash loop.

### Appearance and application identity

- The red Unofficial application icons were restored for WorldEd and TileZed.
- BuildingEd has its own red `B` icon.
- Built-in styles were extracted into external QSS files under `themes`.
- WorldEd, TileZed and BuildingEd share the same portable theme discovery rules.
- Dark themes now keep normal tileset names readable without overriding the
  green, orange and red availability/status colors.
- The tileset sort action uses a clear vector sort icon instead of the
  unreadable 16-pixel `ABC` bitmap.
- WorldEd's Zombie Heatmap and Biome Map paint tools now have distinct,
  purpose-specific icons instead of sharing the generic bucket-fill icon.
- Invalid embedded PNG color profiles were removed from affected resources to
  avoid repeated image-loading warnings.

## WorldEd

### Project-specific 300 × 300 and 256 × 256 grids

- World grid geometry is stored per project:
  `300 × 300 (Legacy)` or `256 × 256 (Native)`.
- A grid-format selector was added to **New World**.
- The selected format is propagated to views, cells, coordinate conversion,
  thumbnails, LOT generation and density maps.
- WorldView and CellView display a visible project-grid badge.
- Legacy 300 projects remain fully supported. Native 256 is neither a global
  preference nor a replacement for the historical format.
- Native 256 projects avoid the 300-to-256 grid conversion during LOT export.
- TMX 2.0 and negative levels remain supported for projects containing basements.

### Thumbnails and navigation

- Thumbnail creation and reconstruction were corrected for both 300 and 256 cells.
- Thumbnails can be rebuilt only for the current cell selection.
- Thumbnail resolution, grid color and grid thickness controls were restored.
- Cell thumbnails are rendered in the WorldView minimap.
- A thumbnail fills the complete project cell instead of being implicitly fitted
  into a 300 × 300 frame.
- `Ctrl+A` selects all WorldView cells.
- **Open in TileZed** launches the `TileZed.exe` located next to
  `PZWorldEd.exe`; it no longer depends on the Windows TMX file association.
- WorldEd uses the same tileset-resolution strategy as TileZed and waits for a
  map's required tilesets before displaying it.
- Embedded BMP-to-TMX tilesets are matched by name and compatible geometry to
  the shared catalog. This prevents a generated TMX from creating hundreds of
  duplicate missing tilesets when only the catalog's 2x images are installed.

### Terrain and vegetation image editor

- WorldEd now includes a lightweight Qt image editor for `Map.png` and
  `Map_veg.png`.
- The command is disabled until a WorldEd project is loaded, because its palette,
  grid geometry, output paths and dimensions depend on that project.
- Ground and vegetation colors come exclusively from the project's `Rules.txt`;
  invalid or unknown colors are reported with their value and image position.
- Images can span multiple 300 × 300 legacy cells or 256 × 256 native cells.
  Cell origin, width and height are explicit and the images can be attached back
  to the current project.
- Brush, fill, color picker, vegetation eraser, zoom, composite preview and Undo
  are available. Existing PNG files can be opened and edited.
- PNG saving is atomic. Image dimensions and memory use are validated before an
  image is allocated or written.
- The terrain-image working-memory limit is configurable from WorldEd
  Preferences (512 MiB by default, up to 64 GiB). The size warning reports the
  estimated memory for ground, vegetation and composite preview images.
- Procedural tools generate terrain patches, vegetation, lakes, rivers and road
  networks over the complete image so features remain continuous across cell
  boundaries.

### World rectangle and empty cells

- An action removes empty cells from the outer border of a world.
- The rectangular PZW world is reduced safely and its project origin is updated.
- Cells containing a TMX file or identifiable WorldEd content are not silently
  removed.
- Roads and other world coordinates are moved with the origin when the rectangle
  is reduced.

### Biomemap generation

- The fork's Biomemap Generator was restored and optimized.
- `Map.png` and `Map_veg.png` both contribute to the biome layer.
- The green channel is no longer copied directly from the vegetation image.
- The green channel can be generated in either of two ways:
  - rasterize the seven Biomemap-managed Zone types from the current WorldEd
    project;
  - read exact zone identifiers from a grayscale PNG or a PNG green channel.
- `Vegitation`, `DeepForest`, `Forest`, `TownZone`, `Farm`, `FarmLand`, and
  `TrailerPark` are the only WorldEd types rasterized into the green channel,
  matching the types deliberately ignored by the game's `metazoneHandler`.
- Every other vector object or zone type remains excluded from the image and
  must be exported through `objects.lua`, including vehicle zones, geometries,
  WorldGen, and `WaterZone`.
- The result dialog lists the actual project types placed in each output path.
- Input dimensions are validated against the current project's 300 or 256 grid.
- Images are never resized or padded silently.
- Detailed warnings report unmapped biome colors and invalid channel values.

### Zombie Heatmap

- Both historical geometry (30 × 30 samples per 300 cell) and native geometry
  (32 × 32 samples per 256 cell) are supported.
- Stored values remain raw red-channel intensities from 0 to 255.
- The optional B42 `×40` mode affects only the editor preview; it never modifies
  the saved image values.
- WorldView provides direct heatmap editing with brush radius and intensity.
- Left-drag paints and right-drag erases.
- The painting tool is enabled only while **View > ZombieMap** is visible.
- Each stroke is recorded in the Undo history.
- The image can be zero-padded to the complete world bounds.
- Saving is atomic and the first edit creates a `.before-paint.bak` safety copy.

### InGameMap features and roads

- Road Feature generation was restored.
- Primary, secondary and tertiary roads, trails and railways are recognized.
- Separate simplification tolerances and maximum point spacings are available
  under **Preferences > Feature Generation**.
- InGameFeature generation performs less redundant work.
- Clipper output is cleaned by removing duplicate closing points, repeated
  vertices and collinear vertices.
- Degenerate polygons, zero-area polygons and polygons with fewer than three
  distinct vertices are rejected explicitly.
- XML and binary exporters apply the same geometry validation.
- XML and binary InGameMap files are written as one recoverable pair. A failure
  cannot silently leave a newly written XML file beside an older binary file.
- Export validation checks non-finite coordinates, signed 16-bit coordinate
  limits, property counts and per-cell complexity.
- Negative world coordinates use floor division consistently when features are
  assigned to export cells.
- Diagnostics include the output file, cell, feature index, properties and the
  reason for repair or rejection.
- These protections specifically address failures such as
  `IndexOutOfBoundsException` in
  `WorldMapRenderer$Drawer.fillPolygon()`.

### Import, export and editor feedback

- Dragging a `Map.png` / `Map_veg.png` pair reports an explicit success or
  failure result.
- BMP-to-TMX, TMX-to-BMP, PNG building export, Lua object import and image
  pyramid bounds now use the current project's 256 or 300 cell size.
- Failed PNG/BMP writes identify the destination and keep the dialog open
  instead of reporting completion.
- LOT export can create a complete Project Zomboid map mod containing `mod.info`,
  `poster.png`, the version directory, `media/maps`, lots and map metadata.
- Complete-mod export follows the current 8 × 8 layout.
- Tile IDs are shown in the relevant WorldEd palettes.

## TileZed

### Tilesets and tiles

- A Project Zomboid tileset can be imported directly from a PNG without requiring
  all metadata inherited from generic Tiled workflows.
- Custom tile dimensions can be as large as 4096 pixels.
- Large Jumbo tiles are supported by the texture-pack tools.
- Advanced Pack Extractor options were restored, including exact-name matching
  and multiple prefixes.
- Tileset identification, ID reconstruction and direct tile-to-PNG export tools
  were restored.
- Tile names and IDs are displayed in editor palettes.
- Every available catalog tileset image is decoded synchronously during
  application startup. TileZed, BuildingEd, palettes and Lua scripts therefore
  operate on one stable, fully populated catalog.
- Tileset images remain resident for the application session. The shared image
  cache still deduplicates catalog and map references to the same source, but
  no longer evicts images or starts background tile-image jobs.
- Lua tile lookup, named placement, deletion and replacement remain
  synchronous and can use any catalog tileset immediately.
- Missing or undecodable images are recorded explicitly and use the
  missing-tile placeholder without leaving a catalog entry in a pending state.
- Initial catalog registration is treated as one bulk update. BuildingEd no
  longer rebuilds every connected tileset list after each of 500+ entries.
- BuildingEd's Tile mode refreshes when bulk catalog registration finishes and
  self-repairs on mode activation and document changes.
- Opening a BuildingEd document without a valid Tiles directory is stopped with
  an actionable configuration message instead of continuing into rendering.
- Resolution lookup does not assume that a `1x` image exists. A valid `2x` or
  custom tileset can be used as the source when it is the only installed
  variant.
- `Tilesets.txt` is processed deterministically. Displayed totals distinguish
  referenced catalog entries from images actually found on disk.
- Cross-drive temporary-file saves handle a destination left by a failed rename
  and keep the original backup recoverable on copy failure.
- Tileset-list status is visible:
  - green: used by the map and available;
  - orange: used or referenced but its image is missing;
  - normal theme text: available but not used.
- Original-style `1x`, `2x` and `custom` icons were restored at a readable size.

### TileDefs

- B42 properties found in the game's Java and Lua sources were audited and added
  to TileZed's property catalog.
- English descriptions were added for poorly documented properties.
- Contextual guidance is available as tooltips in the relevant UI.
- Upstream property/value filters, recent files, ID reassignment,
  `CustomTileSize` and `FloorOverlay` were preserved.

### Automapper

- A dedicated Automapping panel was restored.
- `rules.txt` supports `.tmx` rule maps and nested `.txt` rule lists.
- Recursive list inclusion and duplicate rule-map loading are detected.
- Unsupported entries and unsaved target maps produce explicit warnings.
- The panel reports loaded files and their pattern counts.
- Object Groups are supported in `input` and `inputnot` rule layers.
- Objects can be compared by name, type, shape, position, size, polygon and
  properties.
- A property value of `*` acts as a wildcard.
- Automapping can react to object addition, modification and removal.

### Lua mapping automation

- The common mapping scripts were restored under the portable `lua` directory.
- Lua errors include a traceback in the console.
- Long batch operations show progress and support cooperative cancellation.
- `print()` output and error messages are decoded as UTF-8 first.
- The API exposes negative levels and WorldEd cell coordinates.
- Object Groups and RoomDefs can be accessed and modified:
  layers can be created, objects can be added or removed, and object names,
  types, bounds and custom properties can be edited.
- Object changes are integrated into TileZed's Undo history.
- Tile layers can now be resolved by `(level, baseName)` with
  `tileLayerAt()`.
- Tile names can be read directly with `tileNameAt()` and known tiles can be
  placed by name with `setTileByName()`.
- Exact-position deletion and replacement are available through
  `clearTileByName()` and `replaceTileByNameAt()`.
- Map-wide deletion and replacement by exact tile name are available through
  `clearTilesByName()` and `replaceTileByName()`.
- Whole layers can be removed with `removeLayer()` and RoomDef/Object Group
  objects can be removed with `removeObject()`.
- Named placement loads a known tileset when it is not already attached to the
  map.
- The Lua TileLayer wrapper now retains its owning map, so tile-selection
  restrictions and named-tile resolution work as intended.
- Whole-layer replacement now iterates the layer height instead of using its
  width for both axes; non-square maps are handled correctly.
- The new `app` global exposes `availableActions()` and deferred
  `invoke()`. Whitelisted save, export, binary export, Convert To Lot,
  property-dialog and companion-editor actions run only after a successful
  script transaction.
- Lua documentation was rewritten in UTF-8 with execution modes, safety notes,
  a complete API reference and runnable examples.
- Legacy/custom scripts were audited against the current API. Layer comparisons
  that include level prefixes must use `nameWithPrefix()` rather than `name()`.
  The external `custom_lua` reference directory is intentionally not packaged
  or connected to the applications.
- Portable installs compare canonical `LuaTools.txt` paths before loading
  tools. A shared application/config catalog is no longer registered twice.
- A source-audited guide now explains which zombie-density, zombie-type and
  item-loot behavior can be controlled by mapping data and which behavior
  still requires Project Zomboid runtime Lua.

## BuildingEd

- BuildingEd was separated into its own executable instead of remaining merged
  into TileZed's launcher.
- It has its own red `B` application identity.
- Its visual style now follows the same external QSS themes as the other tools.
- Startup progress reports tileset and `Building*.txt` loading.
- Logs report which Building catalogs were loaded or missing.
- Opening a building now logs its parsing, definition resolution, required
  tileset validation and document creation as separate diagnostic phases.
- The complete tileset catalog is loaded before a BuildingEd document or
  category palette can be displayed. This removes the worker-completion,
  queued-selection and recursive-paint paths that caused the `Roof Caps`,
  Furniture and Tile-mode crashes.
- Tile and Furniture palettes are populated from the stable preloaded catalog,
  including when Tile mode is entered before a building is created.
- The Ortho and Iso `Tiles and Furniture` panels now refresh when
  `BuildingFurniture.txt` finishes loading. Previously only Tile mode listened
  for that event, leaving object-mode Furniture groups empty until another
  editor action rebuilt the panel.
- Tile mode selects the first available catalog tileset after the list is
  populated instead of initially showing an empty tile view.
- Selecting a tileset no longer starts a timer or processes image-loader
  events inside the list-click handler.
- Creating a building resolves and waits for every tileset required by its
  template before the document is displayed. Any genuinely unavailable
  tilesets are named in a clear warning instead of appearing only as unexplained
  red question marks.
- The `--validate-building-categories` diagnostic creates a representative
  building, verifies all 26 templates, the Tile-mode catalog, the Furniture
  palette and every preloaded tileset image, then verifies every entry in all
  17 tile categories in both the Ortho and Iso object panels. The release
  package reports catalog, loaded, missing and pending totals explicitly.
- Furniture references whose optional game tiles are not installed are reported
  by name in the log while the rest of the palette remains available.
- Tile names and IDs are visible in BuildingEd palettes.
- Window geometry, docks, splitters and panel sizes are stored in the portable
  INI file.
- Splitter persistence ignores invalid zero-sized values produced by hidden
  editing modes.
- BuildingEd embeds a transactional Lua 5.2 editor engine. Successful scripts
  are committed as one Undo operation; errors and cancellation leave the
  document unchanged.
- BuildingEd Lua API version 2 adds `tileAt()`, `placeTile()`,
  `deleteTile()`, `deleteTilesByName()`, `replaceTile()` and
  `replaceTilesByName()` for user/grime tile layers.
- The BuildingEd `app` global queues whitelisted save, TMX export, binary
  export, properties, rooms, floors and tiles dialogs after a successful
  transaction.
- Arbitrary Qt object construction remains deliberately unexposed in both
  editors; scripts invoke only documented application actions.

## Tim Baker features intentionally preserved

- Editor-side portability fixes from Tim Baker commit
  `54b21a88743b64adeee262ba97d56eb5610e6e28` (`std::ceil`, explicit
  `<limits>`, and Windows-only Qt SVG linkage).
- Basement support and negative levels.
- The newer Map to PNG / InGameMapImageDialog implementation.
- Upstream `LotFilesManager256` as the LOT-export foundation.
- `CustomTileSize`, `FloorOverlay`, ID reassignment and TileDef filters.
- BuildingEd Attribute mode, RoomTone, basement export and unlit-room detection.

## Source publication notes

- `integration/WorldEd` and `integration/TileZed` are the two source trees
  prepared for private publication. Each retains its own upstream history.
- Current working-tree changes still need to be reviewed and grouped into
  thematic commits before the private remotes are pushed.
- Lua examples that modify maps should first be tested on a copy or with batch
  backups enabled.
- The planned Linux migration will require another case-sensitivity and
  Windows-specific-path audit.
- General cleanup of `#if 0`, `ZOMBOID` and other dead branches remains a
  separate, incremental task so each cleanup set can be compiled and tested.
