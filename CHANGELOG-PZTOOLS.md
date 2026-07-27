# PZTools Unofficial Changelog

This document describes the functional differences between the current PZTools
Unofficial suite and the Tim Baker `basements` branches used as its clean upstream
bases.

Reference date: July 27, 2026.

| Project | Tim Baker baseline | Local branch | Committed local revision |
|---|---|---|---|
| WorldEd | `80e3511cae257f51250df035141243ba6b9cf7cc` | `integration/qt5-basements` | `c5c5cb4` |
| TileZed / BuildingEd | `f9489a9ba605f8dc503c205f19655644798b9ec4` | `integration/qt5-basements` | `bae65aa5` |

This changelog includes the current working trees under `integration/WorldEd`
and `integration/TileZed`. No public repository has been modified or pushed.

## Suite-wide changes

### Supported platform and build

- Windows 10, Qt 5.14.2, qmake and an MSVC-compatible x64 toolchain remain the
  supported development target. The current package was rebuilt with MSVC 2022
  against the Qt `msvc2017_64` binaries.
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
  - rasterize Zone objects from the current WorldEd project;
  - read exact zone identifiers from a grayscale PNG or a PNG green channel.
- A Project Zomboid zone-type-to-biomemap-ID table is included.
- Input dimensions are validated against the current project's 300 or 256 grid.
- Images are never resized or padded silently.
- Detailed warnings report unmapped biome colors and unknown zone types.

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
