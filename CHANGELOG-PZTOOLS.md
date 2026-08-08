# PZTools Unofficial Changelog

This document describes the functional differences between the current PZTools
Unofficial suite and the Tim Baker `basements` branches used as its clean upstream
bases.

Reference date: August 8, 2026.

| Project | Initial Tim Baker baseline | Local branch | Committed local revision |
|---|---|---|---|
| WorldEd | `80e3511cae257f51250df035141243ba6b9cf7cc` | `integration/qt5-basements` | working tree |
| TileZed / BuildingEd | `f9489a9ba605f8dc503c205f19655644798b9ec4` | `integration/qt5-basements` | working tree |

This changelog includes the current working trees under `integration/WorldEd`
and `integration/TileZed`. No public repository has been modified or pushed.

## August 8, 2026 / Separate InGameMap Forest export

- Reordered WorldEd's **InGameMap** menu so Tree generation and
  **Write Worldmap-Forest** form a distinct workflow above Water, Road, and
  Building generation plus **Write Worldmap**.
- Added scoped XML and binary writing. The Forest writer includes only
  `natural=forest` features and the main Worldmap writer excludes them.
- Made **Write Worldmap-Forest** render `forest.png` and create the actual
  `forest.pyramid.zip`, including tiled PNG levels and project-aware
  `pyramid.txt` bounds.
- Staged and committed the Forest XML, binary, source image, and pyramid as a
  single recoverable four-file export.
- Kept **Write Features XML 8x8...** as the advanced all-feature path. The
  canonical overwrite action preserves Forest and non-Forest separation.
- Added `--validate-ingamemap-forest-export` for deterministic feature, raster,
  ZIP-level, metadata, origin, and atomic-output validation.

## August 8, 2026 / Renderer diagnostics bubble

- Added saved **View > Render Diagnostics** controls to TileZed, BuildingEd,
  and WorldEd. Diagnostics are visible by default and can be hidden from the
  View menu.
- Added a compact lower-left overlay to TileZed, the BuildingEd renderer,
  WorldEd Cell Renderer, and WorldEd World Renderer with FPS, render duration,
  process resident RAM, zoom, backend, and viewport size.
- Counted tile instances at the existing raster draw call and at successful
  WorldEd VBO draw calls. No map scan is performed to produce the metric.
- Reported cells in view for the World Renderer because that renderer uses
  cell thumbnails rather than individual tiles.
- Added portable process-memory sampling for Windows, Linux, and macOS, capped
  at one sample per second while diagnostics are visible.
- Left the maintained renderer order, cell composition, and catalog behavior
  unchanged.

## August 8, 2026 / Deployed non-regression pass

- Rebuilt and deployed all three applications to the maintained portable
  release directory.
- Passed TileZed catalogue, Brush performance, Automapper rules, Depth Map,
  pack tools, and TileDef split validation.
- Passed BuildingEd category, template, palette, Lua placement, and Undo/Redo
  validation.
- Passed WorldEd preview-overlay, Hole Detection repair, Native256 LOT,
  Project Doctor tileset cleanup, Biomemap configuration, and WorldDefaults
  validation, plus the separate InGameMap Forest export validator.
- Confirmed all 14 validators exited successfully, logged PASS, and left no
  editor process running.

## August 8, 2026 / BuildingEd large-building rendering

- Traced the available August 8 report and video to repeated rendering and
  synchronization in a large multi-floor TBX rather than to Rules or Blends.
- Deferred and coalesced tile-region, whole-floor, current-floor, layer
  visibility, and Night Preview updates for hidden Iso, Tile, and Properties
  scenes.
- Batched pending visibility changes to one composite synchronization per
  affected floor.
- Avoided rewalking floor object children when their effective visibility did
  not change.
- Added thresholded timing logs for slow floor rendering, floor preparation,
  deferred application, and BuildingMap update batches.
- Preserved complete tileset preload and the existing BuildingEd, Cell, and
  World renderers.

## August 7, 2026 / Unavailable Rules and Blends filtering

- Traced Petro's old-project Brush Tool delay to unresolved red TMX tilesets
  feeding placeholder tiles into `BmpBlender` after current Rules/Blends were
  imported.
- Rebuilt active Rule and Blend indexes only from resolved tile outputs.
  Missing-only Rules are inactive, mixed Rules retain valid choices, explicit
  empty choices remain valid, and Blends with an unavailable main/output tile
  are inactive.
- Kept the embedded definitions and missing-sheet warnings intact while
  excluding impossible per-square work.
- Added active/total Rule and Blend counts to slow-redraw diagnostics and
  extended `--validate-brush-performance` with deterministic unavailable-sheet
  fixtures.

## August 7, 2026 / Project Doctor referenced-missing cleanup

- Added an unchecked advanced option to remove referenced tileset
  declarations only when no readable PNG resolves.
- Added exact impact counts for tile cells, tile objects, and embedded
  Rules/Blends references.
- Advanced apply clears affected tile cells, removes affected tile objects,
  and preserves embedded Rules/Blends definitions for diagnosis. Unrelated
  raw GIDs and later original `firstgid` values are retained.
- Added XML, CSV, base64, gzip, and zlib layer rewriting with an analysis/apply
  count gate before the existing backup and atomic replacement.
- Extended `--validate-tileset-cleanup` across the default-preserve and
  advanced-remove paths.

## August 7, 2026 / Maps preview workspace controls

- Added a saved, narrow arrow-only collapse/expand control for the Maps
  browser preview in WorldEd and TileZed. It has no visible label and retains
  a descriptive tooltip.
- TileZed now selects the **Tilesets** dock tab after restoring its lower-right
  dock group.

## August 7, 2026 / Complete Build 42.20 BiomeMapConfig reference

- Replaced the partial Biomemap palette metadata with all verified Build 42.20
  fields for Pixel, Biome, Ore, Zone, and Vanilla availability.
- Added a complete in-app reference table to **Generate Biome Map**.
- Added exact per-entry metadata to the red-channel brush palette and status
  text without changing its channel-preservation behavior.
- Defined `map_forest` directly in the reference and brush tooltips. It selects
  procedural surface deposits of boulders, limestone, or flint across the
  `NONE` through `VERY_HIGH` ore-noise bands. It does not select iron or
  copper, which use separate vein generation.
- Derived accepted IDs and no-effect red warnings from the shared table.
- Added an explicit warning for ID 171 because Vanilla leaves it commented
  out and a map-specific `WorldGenOverride.lua` entry is required.
- Added `--validate-biomemap-config` and comprehensive technical and
  Discord-ready documentation.

## August 7, 2026 / Folder creation in Maps browsers

- Added a compact icon-only **New Folder** button with a tooltip to the
  WorldEd Maps dock, TileZed Maps dock, and BuildingEd welcome-mode TBX
  browser.
- Added **New Folder** to the right-click menu of all three file browsers.
- New folders are created below the directory currently displayed by that
  browser, then selected and scrolled into view immediately.
- Added portable folder-name validation, existing-file collision checks, and
  clear permission or unavailable-directory errors.

## August 7, 2026 / BMP to TMX Validation & Repair

- Added **Validation & Repair** to WorldEd's **BMP To TMX** dialog.
- Kept unknown-color reporting and added optional automatic replacement with
  independent ground and vegetation fallbacks populated from the selected
  `Rules.txt` palette.
- Replacement is intentionally non-destructive to source images. It changes
  only the BMP layers copied into newly generated or updated TMX files.
- Persisted the repair option and fallback colors in PZW while preserving
  default behavior for older projects. TileZed's shared PZW reader recognizes
  the same fields.
- Added a deterministic color-repair probe to BMP-to-TMX input validation.
  The deployed sample-project validator passes.

## August 7, 2026 / TileZed workspace and Depth Map primitives

- Converted **BMP Tools** from a tool dialog to a true `QDockWidget`. The dock
  participates in TileZed's saved main-window state, supports docking,
  tabbing, floating, closing, and reopening from **View**, and returns to its
  saved placement when TileZed starts.
- Renamed the warning page to **Validation & Repair** and labelled automatic
  unknown-color replacement as a validation action. It remains in BMP Tools
  because the active Rules palette and bitmap warning list provide its exact
  context.
- Added **Reset Interface Layout** to TileZed Preferences. It rebuilds the
  original dock groups and dimensions immediately while leaving projects,
  custom brushes, primitive presets, and other user files untouched.
- Added visible gold resize handles to the selected Depth Map primitive.
  Wireframe dragging continues to translate on X/Z, while corner-handle
  dragging performs a uniform resize.
- Added editable pixel dimensions for boxes, cylinders, and polygons.
  Precise per-dimension controls complement the visual uniform resize.
- Added optional grid snapping for translation and resize using the same
  Build 42 editor increments: 1/64 on X/Z and 1/96 on Y.
- Added portable reusable primitive presets. A selected primitive is saved
  with a name, type, geometry, and source tileset and can then be inserted
  into another similar tile, including one from a different tileset.
- Extended `--validate-depthmap-editor` with deterministic pixel-size and
  snap checks. The deployed Depth Map, Brush, and BuildingEd category
  validators pass.

## August 6, 2026 / Selective upstream synchronization

- Reviewed the current Tim Baker `basements` heads instead of copying their
  complete working trees.
- Ported the WorldEd basement-aware building separation, minimum-level scan,
  recursive building-layer level, and thumbnail ordering.
- Preserved the maintained Cell and World renderers. The upstream thumbnail
  order was adapted to the existing image-loading and cache logic.
- Ported TileZed's multi-tileset removal dialog with one Undo macro and its
  Qt 5 pointer correction.
- Kept the Lua console menus available on macOS.
- Added only validated street-decoration and fence presets plus clearer edge
  labels. The upstream catalogue and BuildingEd data replacements were
  rejected because some references do not resolve and the maintained local
  catalogues are supersets.

## August 7, 2026 / Hole Detection uses tile presence

- Hole Detection now reports a hole only when no composite tile exists at the
  coordinate on level zero or an available basement level.
- Water, custom building floors, personal tiles, and tiles with unavailable
  TileDefs no longer become false holes merely because `solidfloor` is absent.
- Automatic repair uses the nearest non-empty tile on the current TMX
  level-zero `Floor` layer and remains backed up and atomic.
- Generate Lots applies the same rule and aggregates empty-square reports per
  generated cell instead of allocating one failure entry for every coordinate.

## August 6, 2026 / Hole Detection automatic repair

- **World > Hole Detection...** now offers to fill detected holes or leave
  them highlighted.
- Repair is intentionally narrow. It copies the nearest non-empty cell from
  the current TMX level-zero `Floor` layer and does not edit lot or building
  files.
- The current TMX is backed up below
  `.pztools-backups/hole-detection-*` before an atomic rewrite. Failed backup
  or output operations restore the in-memory cells.
- Added `--validate-hole-repair` for the nearest-source propagation fixture.

## August 6, 2026 / Native 256 lot-coordinate diagnostics

- Inspected the supplied August 5 log. It shows native-256 mode, the expected
  1024 x 1024 Zombie Heatmap, and TMX coordinates covering 0 through 31 on
  both axes. It does not record the lot-generation coordinate frame and
  therefore cannot prove where the reported displacement begins.
- Native 256 generation now logs source cell size, world origin and size, and
  output bounds.
- Export stops if a direct native-256 source cell would land anywhere other
  than local square 0,0 in its output cell.
- The geometry validator now covers negative origins, multiple cells, local
  squares on both sides of chunk boundaries, and a lot crossing from cell 27
  into cell 28.

## August 6, 2026 / Regression matrix

- Rebuilt and deployed all three applications with Qt 5.14.2 and MSVC.
- PZWorldEd passed Hole Detection repair, Native 256 geometry,
  environment-preview overlay, and WorldDefaults validators.
- A real sample world and cell opened under both the Qt Raster and OpenGL 3.3
  compatibility paths. The OpenGL cell shader and a real tileset texture were
  initialized.
- TileZed passed tileset-catalogue, brush-performance, Automapper-rules, and
  TileDef split validators.
- BuildingEd passed its complete category suite, including the populated
  **Building > Tiles** list, template and furniture references, Lua placement,
  Undo, and Redo.

## August 6, 2026 / Machine diagnostics and updater design

- The portable log bootstrap shared by all three applications now records the
  OS/kernel build, CPU model, logical processors, total/available startup RAM,
  reported GPU/display adapters, Qt ABI, and process bitness.
- The diagnostic excludes usernames, hostnames, serial numbers, IP addresses,
  and stable machine identifiers. Adapter reporting is labelled as
  operating-system data because remote desktop and hybrid graphics can hide
  the physical rendering device.
- WorldEd records the actual OpenGL vendor, renderer, and version after its
  native context is current.
- Added a concrete GitHub Releases updater design with immutable releases,
  API asset digests, a signed manifest, staging, managed-file hashes,
  user-modification conflicts, external helper replacement, validation,
  backup, and rollback.
- Tools and Tiles use separate manifests. The Tiles design preserves unknown
  custom sheets and allows authorized local-source synchronization. Publishing
  Project Zomboid PNG packages remains gated on explicit redistribution
  permission.

## August 5, 2026 / BuildingEd tileset-list synchronization

- Fixed **Building > Tiles** remaining empty when its singleton dialog was
  constructed before the complete `Tilesets.txt` catalogue was registered.
- The dialog now rebuilds its names after bulk catalogue load and installed
  sheet discovery. Closing the tileset metadata manager also forces a refresh,
  including when reloading an unchanged `Tilesets.txt` produces no individual
  add or remove signals.
- `--validate-building-categories` now compares every displayed tileset name
  and its order against the loaded catalogue, and checks that a valid sheet is
  selected.

## August 4, 2026 / WorldEd Project Doctor

- Added a guided WorldEd project-health window for PZW paths and recursive
  TMX/TBX checks. The normal interface exposes one read-only check and one
  backed-up safe-fix action, with plain file-role and Build 42 explanations.
- The normal result is now a four-column status/file/meaning/action table.
  Clean files are aggregated, unresolved references are described as items
  needing the mapper's help rather than a catastrophic failure, and the
  complete parser report is hidden behind an explicit support-details button.
- PZW input/cell paths and TMX-to-TBX dependencies are resolved from their
  owning file. Missing, absolute, Downloads, OneDrive, game-installation, and
  outside-project sources are differentiated instead of collapsing into a
  generic missing-file failure.
- TMX cleanup protects placed layers/objects and BMP aliases/rules/blends.
  It removes only unused declarations whose PNG is also unresolved, preserves
  used missing declarations, and retains valid unused declarations so the
  complete ordered legacy header remains deterministic.
- Inline TMX image paths use the catalogue's actual readable 2x-first, then
  1x/custom resolution. Existing in-project TBX paths are normalized without
  silently moving external dependencies.
- TBX cleanup deliberately treats `tile_entry`, `user_tiles`, and furniture
  as ordered ID tables. It performs a BuildingReader/BuildingWriter semantic
  round trip, remaps all references, and refuses output unless a second
  canonical round trip is byte-stable.
- Apply copies every changed source into a project-relative timestamped
  `.pztools-backups` tree before atomic replacement; recursive scans ignore
  those backups.
- Added fixture validation, real-file read-only audit and UI-render commands,
  and the dedicated Project Doctor workflow/recovery reference.

## August 4, 2026 / Rules painting and upstream crash review

- Credit and special thanks to **Petro** for reducing the brush regression to
  BMP Tool **Import Rules**, followed by **Reload**, followed by ordinary tile
  painting.
- Confirmed the persistence model from the readers and writers. PZW contains
  external Rules/Blends paths. Each TMX contains exactly one full
  `<bmp-settings>` snapshot with aliases, rules, and blends. Opening an old TMX
  therefore restores its old snapshot, but the file does not append new rule
  generations when it is saved.
- **Import Rules** and **Import Blends** now compare the selected data with the
  current TMX snapshot. Identical imports stop without recreating
  `BmpBlender`. Different imports display old/new counts and require explicit
  confirmation before the undoable replacement. The dialog explains that
  saving persists the replacement and Reload is unnecessary.
- Reload now skips semantically identical aliases, rules, and blends. A real
  Rules change applies aliases and rules in one operation and prepares the
  blender once.
- The user-supplied older pair from August 6 has 40 aliases, 33 rules, and 72
  blends. The current package has 80 aliases, 91 rules, and 128 blends. The
  larger current set is a genuine per-edit workload increase rather than
  several active snapshots layered in the TMX.
- Confirmed from `AutomappingManager` that absent/invalid Automapper manifests
  are already attempted once per document and retried only by explicit reload
  or document change. The external diagnosis correctly recognized the symptom
  but conflated Automapper manifests with WorldEd terrain/BMP Rules.
- Traced the remaining delay to `BmpBlender::flush()`: it intersected its dirty
  region with the requested area, then regenerated the full requested area.
  Rules importing `0_Floor`/`0_Vegetation` targets therefore turned a small
  brush edit into a common 300 x 300 regeneration.
- Both flush paths now coalesce to the dirty bounding rectangle, expanded by
  the two-tile blending neighbourhood and clipped to the request. A 250 ms
  diagnostic records request, dirty, work geometry, and deferred tile
  initialization for any remaining slow case.
- Renamed TileZed's preference to **Experimental OpenGL viewport**, added a
  visible recommendation to keep it disabled, and reset previously enabled
  installations once. TileZed still uses QPainter through a QOpenGLWidget
  rather than WorldEd's native shader/VBO renderer, so the raster viewport
  remains the supported responsive path.
- Ported Tim Baker's `MapScene` grid-color lifetime correction. Supplying the
  scene as the Qt connection context prevents a preferences signal from
  invoking a lambda after the scene has been deleted.

## August 4, 2026 / WorldEd startup and authoring metadata

- Normal WorldEd configuration and session restoration are queued after the
  interactive event loop starts. Headless validators remain synchronous.
- Complete catalogue preload remains intentional and deterministic, but now
  reports Tilesets.txt, PNG discovery, catalogue, Building catalog, and
  thumbnail phases through a visible progress dialog and startup log markers.
- The default missing SpawnPoint `Professions` enum was synchronized with
  Build 42.20 `CharacterProfession`: `all` plus all 25 registered base-game
  profession names. Existing project enums are not rewritten.
- TileDef tooltips now distinguish TileDef file number from tileset ID and
  tile index, and compute the Build 42 sprite ID using the game-confirmed file
  1/2/4/5/6/8 offsets. Unknown patch/mod files are not assigned a fabricated
  absolute ID.
- Source and deployed `Tilesets.txt` now contain the same 543 unique logical
  names discovered from 546 installed PNG files. Each resolves to an
  installed 1x/custom or preferred 2x PNG.
- `TilesetMetaInfo` retains catalogue columns/rows so a rewrite can preserve
  valid logical geometry for nonstandard effect canvases when the decoded PNG
  rectangle alone is insufficient.

## August 4, 2026 / Configuration-catalogue audit

- Audited the shipped `WorldDefaults`, `RoomNames`, `RoomTone`, `Tilesets`,
  TileDef, BuildingEd, terrain-generation, road, fence/curb/edge, Lua-tool,
  rearrangement, blend, and base-map catalogues against their parser code,
  Build 42.20 reference data, and the configured Tiles tree.
- Synchronized WorldEd's professions to `all` plus the 25 registered Build
  42.20 values; added current object/zone values; retained 588 unique room
  names and 267 unique room tones/building types.
- Corrected 72 obsolete burnt-roof IDs, removed six furniture definitions
  above the actual `signs_one-off_05` range, added six missing fence presets,
  and removed two rearrangement groups whose 20 coordinates no longer exist.
- Verified all remaining static references against catalogue dimensions,
  including 100 `RearrangeGrid` references, 128 blend definitions, and all
  shipped Lua tool scripts/icons.
- Removed dormant legacy files from the deployed `config` directory and added
  `docs/PZTools-Configuration-Files.md` to explain ownership, safe
  customization, audited counts, and validator commands.

## August 4, 2026 / Build and platform documentation

- Added `BUILDING.md` with the required source layout and separate build trees
  for PZWorldEd and TileZed/BuildingEd.
- Documented direct qmake build procedures for Windows, Linux, and macOS,
  followed by native packaging, dependency deployment, hash verification, and
  target-system validation.
- Windows x64 with Qt 5.14.2 and MSVC remains the tested release target.
  Linux and macOS builds require successful compilation and validation on
  their target systems before publication.

## August 4, 2026 / Native 256 LOT export

- Credit and special thanks to **шакалоблок** for reporting the Native256
  LOT-export regression and its incomplete-cell symptom.
- Native 256 WorldEd cells now follow a strict one-to-one LOT export path:
  source bounds, output bounds, cell coordinates, and world origins are
  identical. No 300-to-256 conversion is called for a native project.
- Combined TMX placement, cross-cell lots/prefabs, RoomDefs, objects,
  floor-hole checks, and navigation chunk data use the project geometry.
  Native maps therefore use a 256-square stride while Legacy300 maps keep the
  established 300-square converter.
- Native exports validate that every assigned cell TMX is exactly 256 x 256
  and report the project cell, file, and actual dimensions when it is not.
- Added `--validate-native-256-lot-geometry` with zero, non-zero, and negative
  origins, multi-cell bounds, one-to-one round trips, and a legacy-path guard.

## August 3, 2026 / Automapper manifest isolation

- Added the unambiguous `automapping-rules.txt` filename, preferred beside a
  target TMX, while preserving legacy Automapper `rules.txt` manifests.
- A WorldEd terrain/BMP `Rules.txt` is recognized by its structured syntax and
  ignored by Automapper instead of treating `version`, `alias`, braces, and
  tile names as missing paths.
- Failed or unavailable rule loading is remembered until explicit Reload or a
  document change. Interactive Automapping therefore no longer reparses the
  same bad file, freezes painting, and repeats a modal error for every stroke.
- Added `--validate-automapper-rules` and updated the Automapper documentation.

## August 3, 2026 / Editing-path audit and documentation index

- Audited TileZed edit-signal consumers after the Automapper and minimap
  regressions. Document/Undo updates, renderer synchronization, active Lua
  tools, and the named background minimap worker remain intentional.
- Hidden Tile Layers panels no longer rebuild their model for every cursor or
  paint update. Visible panels refresh only for their inspected square and
  current level, then resynchronize when shown.
- Tileset usage/status decoration is visibility-aware and debounced for 150 ms
  rather than rebuilding every catalogue icon and tooltip for each changed
  region.
- Added a global source documentation index and an offline distribution index
  organized by application, workflow, format, and symptom.
- Added a complete logs/diagnostics/reporting reference and expanded the main
  guide with WorldGen, loot, PNG warning policy, validators, and focused
  troubleshooting.
- Automapper documentation now includes a minimal rule-map tutorial, manifest
  layout, layer-name mapping, pipeline/performance guidance, diagnostic
  behavior, and a documented seven-stage sewer example with a preferred
  manifest.
- WorldGen, loot, pack, and TileDef references explicitly distinguish
  game-confirmed structures, tool-enforced limits, representative previews,
  and unsupported scope.

## August 3, 2026 / Advanced `.pack` comparison and extraction

- TileZed's comparator now classifies added, removed, decoded-pixel changed,
  metadata changed, combined, duplicate-name, and unchanged textures instead
  of only listing names unique to either input.
- It computes complete-file SHA-256, normalized reconstructed-canvas pixel
  SHA-256, and atlas/page/trim/canvas metadata SHA-256 separately. Search and
  status filters, sorting, pack summaries, detailed geometry, side-by-side
  previews, a color-coded pixel difference view, clipboard output, and atomic
  CSV export are included.
- The Pack Viewer extractor now offers checked selection, multiple
  semicolon-aware filter modes, thumbnails and hashes, reconstructed
  individual textures, automatic/1x/2x/custom tileset sheets, matching atlas
  pages, flat/page/tileset output layouts, safe rename/skip/overwrite
  policies, and an optional JSON provenance manifest.
- Extractor construction is now visible and cancellable while per-texture
  previews and hashes are indexed. The current atlas page/texture is shown,
  and extraction reports validation, individual/page/sheet, and manifest
  phases instead of blocking without feedback.
- The common reader accepts legacy version 0 and current `PZPK` version 1,
  retains the page alpha flag, and rejects excessive counts/strings, truncated
  PNG payloads, failed PNG decoding, out-of-page rectangles, and invalid
  reconstructed-canvas geometry. PNG/sheet allocations are bounded, and new
  `.pack` writes are atomic.
- Added deterministic self-tests and GUI render validators:
  `--validate-pack-tools`, `--render-pack-comparator`, and
  `--render-pack-extractor`.
- The exact libpng incorrect-sRGB-profile warning is omitted as non-fatal,
  unactionable color-profile noise. Distinct PNG warnings and errors remain
  logged.
- Added `TileZed/docs/PZ-Pack-Comparator-and-Extractor.md`.
- Binary `.tiles` loading no longer groups invalid dimensions, tileset ID,
  and stored tile count under one ambiguous message. TileZed/BuildingEd and
  WorldEd now enumerate the exact failed constraints, show the stored values
  and computed grid capacity together with format-specific limits, and offer
  targeted repair guidance. Unterminated strings, truncated headers, numeric
  metadata, property counts, and property name/value fields are distinguished
  before those values are used and include their positions.
- The `.tiles` comparator now performs exhaustive property and structural
  analysis, including file hashes, unique tilesets, changed IDs/images/grids/
  counts, and one-sided tile records. Search/status filters, dual previews,
  highlighted property details, merge decisions, and copy/export reporting
  replace the former summary-only interface.
- The Snow Editor is now a general Snow / Replacement Editor with presets,
  custom-key persistence, batch and same-ID mapping, unresolved-reference
  visualization, accurate modified-state handling, and safer open/save/close
  behavior.

## August 3, 2026 / Procedural loot viewer and editor

- TileZed and BuildingEd now share a visual editor for the Build 42
  `SuburbsDistributions` RoomDef/container registry and
  `ProceduralDistributions.list`.
- Game `Distribution_*.lua`, `Distributions.lua`, and
  `ProceduralDistributions.lua` files are loaded as isolated read-only
  references. Every effective entry is labelled **Game** or **Project**.
- TileZed passes the selected tile's `container` property when available;
  BuildingEd passes the current room's internal RoomDef name.
- Direct item and junk lists show chance per roll, neutral cumulative
  probability, and duplicate independent entries. Procedural selectors show
  relative weights, neutral eligible share, min/max, forced selectors, and
  missing-reference diagnostics.
- New definitions and overrides are stored atomically outside the game below
  `<project-or-mod>/media/lua/server/Items` as
  `PZToolsLootEditor.json` and generated `PZToolsLootDefinitions.lua`.
- The generated definitions apply during `Events.OnPostDistributionMerge`,
  before final item-picker parsing. The editor refuses output inside the
  selected game installation.
- Added `--validate-loot-distributions <game-root> [project-root]`,
  `--render-loot-distributions <game-root> <output.png> [project-root]`, and
  the format/workflow reference
  `TileZed/docs/PZ-B42.20-Procedural-Loot-Editor.md`.

## August 3, 2026 / Project WorldGen editor and preview

- WorldEd now provides two independent windows only while a saved map project
  is loaded: **Tools > WorldGen Biome Editor / Preview...** and **Tools >
  WorldGen Prefab Editor...**. Biome preview/rules/features and true
  static-prefab inspection/import/painting/staging are not mixed in one UI.
  The installed Project Zomboid `media/lua/server/WorldGen` tree is labelled
  and enforced as a read-only source.
- New and edited definitions are stored outside the game under
  `<map-project>/media/lua/server/WorldGen`. Writes are atomic and use
  validated Lua-safe definition names.
- The isolated loader executes game/project biome features, true static
  prefabs, subbiomes, and game/project biomes in dependency order. The
  complete game catalogue and parent chains remain available while UI
  selectors expose **Game** or **Project** provenance.
- The biome editor supports new procedural and map biomes, parent selection,
  `generate`, the simple selection parameters, and weighted biome-feature lists.
  Game definitions are never edited in place: editing one creates a project
  child variant with editable effective values and inherited advanced rules.
- Probabilistic 1x1 through 8x8 patterns are now labelled correctly as
  **biome features**. Their editor supports new or copied project features,
  and resolved sprite cells display their actual Tiles icon.
- Added a separate editor for actual `worldgen.prefabs`: the four z=0 engine
  slots, dimensions and zombie chance are editable with a visual Tiles palette
  and a composited isometric preview. Sprites are depth-sorted for tall and
  XL/XXL overlap, and orange inspection guides remain visible over complete
  Floor coverage at 8x8 chunk boundaries.
- Added guarded TMX and one-floor TBX import. Conversion rejects unsupported z
  levels and tile stacks instead of reporting a lossy conversion as complete.
  The legacy map reader now rejects a version-2 layer with no PZ `level`
  attribute cleanly instead of passing a null layer to `Map::addLayer`.
- Added **Stage for Game / Mod...**. The destination must remain outside the
  selected game installation; the tool writes the prefab and a marked
  static-module `WorldGenOverride.lua` block while preserving unrelated
  override content. Placement reports inclusive square, chunk, and 256-cell
  bounds.
- Advanced subbiome, placement, protection, and replacement structures remain
  inspected and inherited, not rewritten. Project files containing such
  hand-authored rules are protected from lossy in-place editing.
- The isolated Lua loader discovers all feature registries, procedural
  biomes, map biomes, and subbiomes, then resolves parent inheritance into an
  inspectable effective definition. Lua file/system/package access is not
  opened and an instruction limit guards malformed definitions.
- A deterministic representative preview renders a forced biome on the
  Build 42 16 x 16-square generation block using the shared Tiles catalogue.
  The 2 x 2 chunk boundary, category visibility, feature weights, pattern
  sizes, and per-square sprite provenance are visible.
- JUMBO, JUMBOXL, and JUMBOXXL use their declared custom tile geometry.
  XL/XXL previews draw the runtime main-sprite `N` plus treetop `N+6` pair,
  with a fitted canvas sized for their full visual footprint. Incompatible
  custom-sheet dimensions are reported and rejected instead of producing
  mis-sliced, floating, or clipped trees.
- Map-biome authored-terrain replacement, `$subbiome` marker expansion,
  game-save noise parity, complete mod packaging/export, roads, and erosion
  are deliberately not claimed by this editing stage.
- `--validate-worldgen-preview=<path>` verifies loading, inheritance, and
  concrete preview output. A GUI screenshot path is also available for
  release regression testing after the complete Tiles catalogue is ready.
- `--validate-worldgen-project-overlay=<game>::<project>` verifies merged
  loading and project-definition provenance.
- `--validate-worldgen-prefab-import=<source>` checks strict TMX/TBX
  conversion, while `--render-worldgen-prefab=<path>` captures the real
  isometric prefab-editor path after Tiles loading.
- `--render-worldgen-prefab-window=<path>` captures the independent prefab
  catalogue/inspector window with no biome preview or biome-feature controls.
- Added a full format and workflow reference at
  `WorldEd/docs/PZ-B42.20-WorldGen-Editor-and-Prefabs.md`.
- BMP-to-TMX now recognizes when a legacy/misconfigured `mapbasefile` points
  to `Rules.txt`, where `alias` is valid. It recovers through the portable
  `MapBaseXML.txt` instead of producing the misleading “Unknown block name
  'alias'” error. Directly typed file paths in the generation dialog now
  update the saved values as well as Browse-button selections.

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

## August 2, 2026 / Tiledefs and shared themes

- Tile Properties now supports separate Build 42 mod (512/512) and base-game
  (1024/1024) validation targets. Damaged or oversized files load in recovery
  mode, and a repair command can reassign IDs and emit numbered `.tiles` plus
  `.tiles.txt` series without discarding tile metadata.
- The repair validator covers a 513-sheet input split into 512/1 definitions.
  A single PNG exceeding the 512-tile sheet limit is rejected with a specific
  explanation because the image itself must be split.
- Themes can be synchronized across TileZed, BuildingEd, and WorldEd using the
  shared portable configuration.

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

## July 31, 2026 / WorldEd cell and OpenGL regression fixes

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
