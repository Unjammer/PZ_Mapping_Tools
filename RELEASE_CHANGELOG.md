# PZ Mapping Tools - Build 20260909f

_Changes since Build 20260827_

The exact public release baseline used for this changelog is
[`PETRO`](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/PETRO),
commit
[`ab312d7bfc90d17f2e2c4feaa2052c9efb18dc0a`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/ab312d7bfc90d17f2e2c4feaa2052c9efb18dc0a),
published on 2026-08-27 as **Build 20260827**.

This changelog contains only user-visible work added after that tagged
release.

## Included applications

- All three applications use the same loading window, with a completed
  tileset counter and progress bar. Their main windows start centered at
  1920 x 1080, fitted to the available desktop including window borders.
  Saved dock layouts and temporary window-size overrides remain available.

- TileZed, BuildingEd and WorldEd prepare tilesheet images in parallel with
  a processor-aware limit of eight workers. Complete catalogue loading,
  ordered map declarations and shared image deduplication are preserved.
  WorldEd also retains atlas coordinates when copying newly added tiles
  from the image cache.

- WorldEd exports legacy `Professions = all` SpawnPoints as `unemployed`,
  preserving other explicitly selected professions and avoiding duplicates.
  Existing rectangular SpawnPoints retain their original position and size.
  The saved project is unchanged by the conversion.
- The LotPack Viewer restores Save Screenshot and F12, with PNG output to
  a chosen destination.
- WorldEd preserves existing rectangular RoomTone objects during Lua export.
  Their original position and dimensions are retained, and EntireBuilding
  may be omitted for room scope, as supported by the game.
- WorldEd keeps room and building groups separate between source cells during
  LOT generation. Connected underground complexes retain their room definitions
  when neighboring cells are converted and exported separately.
- Building Tiles ignores exact duplicate material definitions when reading
  and saving catalogues. The bundled catalogue no longer contains repeated
  entries. Definitions with different orientations or offsets remain separate,
  and the first occurrence retains its position and default selection.
- TileZed includes a native TileSetZ editor in Tools and tile context menus.
  It opens and saves PNG tilesheets, paints individual tiles with Undo,
  precuts textures into individual 1x or 2x wall tiles, and generates sheets from the original
  wall, floor, curb, roof-wall, roof and extended wall masks. Custom PNG masks
  support magenta selection or their alpha channel. Generation has a preview
  before insertion, trim colors are editable, and appending preserves existing
  IDs. Saves refresh sheets in the configured Tiles catalogue and detect
  external file changes before overwriting. Drawing has its own tab with
  illustrated tools, shapes, opacity, panning and pixel zoom up to 32x.
  Tilesheet and mask previews have a separate tab with full-tile inspection.
  Wall cuts use the source dimensions and fixed wall-face size to determine
  the tile count automatically. Each source piece is projected without
  resampling, with transparent padding for partial edges and consecutive IDs
  in source row order. Both wall directions, a source cut grid, an assembly
  check and PNG plus TMX export are available.
- TileZed's Depth Map Editor adds a selectable color depth palette,
  alpha-masked painting, source and grayscale preview modes, and undoable
  inversion. Pixel estimates distinguish ground surfaces from vertical
  objects using the game's projection. Volumes update their preview while
  moved or resized, support Undo and Redo, and can be fitted to the sprite.
  Shift-drag adjusts elevation. Generate All replaces the depth tile with
  the preview, while Merge Selected adds the selected volume to existing depth.
- TileZed selections limited to the current layer and level paste onto the
  destination layer. Broader selection scopes retain layer names and relative
  levels for copying complete structures, even with only one populated layer.
- TileZed and BuildingEd palette context menus offer a saved Show Tile Numbers
  toggle. Erasing a curb restores its blend suppression when no remaining
  neighboring curb needs it. Undo restores the curb and its blend flags.
- Exterior building tiles without a floor retain map tiles on other layers,
  including vegetation. Matching layers are replaced and building floors
  continue to cover underlying map tiles in rendering and export.
- Raster drawing reuses filtered tile images at supported 25% and 50%
  zooms with a bounded cache. Edited images select fresh cache entries.
- TileZed and BuildingEd avoid rewriting unchanged window and dock settings.
- TileZed and WorldEd show their application name and build identifier before
  a document is opened.
- Depth-map volume generation prepares primitive transforms once per render
  and shares the camera ray between primitives at each pixel.
- TileZed generates and removes RoomDefs on negative as well as positive map
  levels. New RoomDefs belong to the correct level, and layer-name lookups
  recognize signed level prefixes.
- Tile-property patches are no longer merged repeatedly during unchanged
  property lookups. This reduces delays when opening and editing buildings,
  preparing their maps, and loading tile catalogues with patch files configured.
  Property-file changes and reordering refresh the merged data, and removing
  a patch or one of its properties restores the underlying values.
- A macOS setup script installs the development dependencies, selects or
  downloads the source, checks the Qt toolchain, and builds all three editors.
  It supports resuming setup, preserves existing sources and build directories,
  and reports failures with a complete log. Downloaded sources are updated only
  when requested, without discarding local modifications.
- macOS source builds give BuildingEd its own application identity and TBX
  document association. Each application bundle includes the configuration
  catalogues, documentation, and Lua scripts. Build libraries use a shared
  compiler-output directory with relative runtime search paths. Editor
  commands resolve adjacent macOS application bundles. Native macOS builds
  still require testing on the target operating system.
- Initial Setup now applies the selected configuration and Tiles directories
  to the running application before its catalogues load. A restart is no
  longer needed to clear missing-image placeholders caused by a previously
  empty Tiles path. Unchanged paths do not trigger a tiles-directory refresh.
- Startup logs now include the application's exact build identifier.
- The Windows release now includes one complete, coherent, app-local
  Microsoft Visual C++ x64 runtime that is newer than the compiler used for
  the applications. Only the three DLLs imported by the applications, Qt,
  and their plugins are included. A separate Visual C++ Redistributable
  installation is no longer required. The deployment process also prevents
  the older runtime associated with the Qt 5 kit from replacing the required
  compiler runtime.
- Preferences now includes a Notifications page with a per-application
  option to show only errors in the application message indicator. Enabling
  it immediately removes retained warning notifications and suppresses new
  ones. Warnings remain in the log files, and warning dialogs that require a
  response remain visible.

## PZWorldEd

- Thumbnails of buildings placed with basements retain their above-ground
  floors. Underground floors are excluded from the displayed variant.
- Camera zoom retains existing object geometry and basement previews. Labels
  follow geometry edits, including points and polylines. Tile-ordering buffers
  are reused between drawing operations.
- Search groups rapid typing on large worlds and runs immediately on Enter.
  It reuses unchanged result rows and retains each world's search when views
  change or a cell is closed. Empty property-definition lists are safe.
- Generate Lots now checks every source cell required by the requested
  export before loading its TMX files. When a missing TMX reference is the
  only content of an otherwise empty cell, WorldEd offers to create a dated
  PZW snapshot and remove that stale reference in one undoable action.
  References attached to cells containing lots, zones, properties, or other
  project data are reported but never removed automatically.
- The WorldEd zone catalogue now includes the Build 42.20.3 `SpawnOrigin`
  type with a distinct display color. Animal and Basement zones also have
  explicit colors. SpawnOrigin export accepts only non-empty rectangles at
  non-negative whole map-square coordinates and adds no unused properties.
- The Regions editor and WorldEd object catalogue now expose all four
  Build 42.20.3 utility-zone types: `NoGas`, `NoPower`, `NoWater`, and
  `NoPowerOrWater`. These zones require no custom properties and use distinct
  editor display colors.
- Every built-in WorldEd zone type now displays a concise Build 42.20.3
  runtime-purpose tooltip in zone-selection and inspection workflows.
- The guided Mannequin property editor now exposes all six properties read by
  Build 42.20.3. The obsolete `medium` AnimalType choice was removed while
  existing custom values remain loadable.
- Project thumbnails now have explicit document ownership. Closing a project
  releases its thumbnail images when no other open document uses them.
- Closing the final project immediately purges every loaded TMX map with zero
  references. The complete global tileset catalogue stays resident for
  deterministic compatibility with legacy maps and complete tileset headers.
- Thumbnail render failures now complete their asynchronous lifecycle, so an
  unused failed image can be released instead of remaining pending.

## BuildingEd

- Materials refresh on the first Iso/Tiles switch, including after changing
  floors. Custom furniture groups appear immediately when selected in Building
  Tiles, including groups loaded after the dialog was created.
- Object tools use distinct keyboard shortcut identifiers. Shortcut edits save
  immediately and save errors are reported. Reopening the shortcut editor
  retains unsaved edits if a previous save failed.
- Rotations and mirrors retain painted tile positions, square attributes,
  selections and the supported basement-access footprint with Undo and Redo.
  Painted sprite IDs stay unchanged. Border tiles that would leave the
  supported area are identified before the operation changes anything.
- Undo restores furniture positions correctly for asymmetric or aliased tile
  variants after rotations and mirrors.
- Changes between editing modes retain selections. Isometric views share
  their camera position and zoom without cumulative center drift.
- Selecting the active floor again avoids redundant updates. Room highlight
  regions are reused until building geometry changes.
- Opening a TBX no longer frees replaced local tile definitions while the
  building and its automatic preview layers may still reference them. The
  definitions are retained until the building is destroyed.
- Older TBX tile entries are expanded to the current category layout during
  loading. Stair objects whose tile entry is empty no longer crash automatic
  layer or welcome-thumbnail rendering.
- Room tile Clear, Random, and Choose actions now require a valid selected tile
  row. Selecting only a room can no longer pass an invalid category value to
  the tile chooser.

## Credits and special thanks

- Tim Baker for the original WorldEd and TileZed work that remains the
  upstream foundation of these tools.
- Alree / Unjammer for the unofficial Qt 5 continuation, current maintenance,
  new features, fixes, integrations, and releases.
- A very special thank you to Fred 'Military Surplus' Cooper.
- Petro, Pabbiqo [pq], Dane, ! Cacador, Kyber, shakaloblok, shisan-233, and
  the Project Zomboid mapping and modding community for reproducible reports,
  project files, screenshots, logs, and practical workflow feedback.

Legal authorship and third-party attribution are documented in `AUTHORS.txt`,
`FEATURE_PROVENANCE.md`, `UPSTREAM-HISTORY.md`, and the bundled license
notices.
