# Changes in the August 4, 2026 update

This cumulative update includes the mapping-tool corrections completed on July
29 through August 4, 2026.

## WorldEd Project Doctor for mixed or aging projects

- Added **Tools > Project Doctor: Tiles and Paths...** with a plain-language
  **Check project / Fix safely** workflow for PZW, TMX, and TBX sources.
- Replaced the wall of technical output with a four-column summary showing
  status, file, meaning, and the next action. Clean files are grouped, wording
  is reassuring, and the complete report is hidden behind
  **Show support details**.
- The loaded project folder is selected automatically. WorldEd reports
  assigned maps and sources that are missing, absolute, under
  Downloads/OneDrive/the game installation, or outside the selected project.
- TMX building objects ending in `.tbx` are resolved relative to their map.
  Missing and outside-project dependencies are preserved and reported; only
  existing in-project references are normalized automatically.
- TMX declarations are removed only when they are both unused and unresolved.
  Valid unused sheets remain in the complete ordered header for deterministic
  legacy and adjacent-map compatibility. Used missing sheets are also
  preserved for repair.
- Retained inline image declarations are normalized to the same readable 2x,
  then 1x/custom PNG selected by the shared catalogue.
- TBX `tile_entry`, `user_tiles`, and furniture lists are treated as ordered
  ID tables. Cleanup uses BuildingEd's reader/writer to rebuild the tables and
  remap every building, room, object, grime, furniture, and used-list
  reference. A second canonical round trip must be byte-stable.
- Every modified file is backed up below the project in a dated
  `.pztools-backups/tileset-cleanup-*` tree before atomic replacement. Backup
  trees are excluded from future scans.
- Added read-only `--audit-tileset-cleanup`, the
  `--validate-tileset-cleanup` regression, a UI render command, and
  `WorldEd/docs/PZ-Project-Doctor-Tiles-and-Paths.md`.

## Rules painting, startup, and editor stability

- Special thanks to **Petro** for isolating the TileZed brush delay to the
  exact **BMP Tool > Import Rules > Reload** sequence.
- The report was distinct from Automapper manifest loading. Automapper already
  remembers a failed/absent manifest once per document; imported terrain/BMP
  Rules activate `BmpBlender` work on their target layers.
- A small edit previously intersected the dirty region correctly but then
  regenerated the caller's complete rectangle, commonly all 300 x 300
  squares. TileZed now processes the true dirty bounds plus the required
  two-tile blend border. Recalculations still taking 250 ms or more are logged
  with requested, dirty, and processed bounds.
- Fixed the Tim Baker-reported TileZed grid-color crash path. The preference
  callback is now owned by `MapScene`, so Qt disconnects it when the scene is
  destroyed instead of allowing a later color change to call a deleted scene.
- WorldEd's normal interactive configuration/session work begins after the
  event loop is available. The window can paint while the complete catalogue
  is prepared, and startup shows/logs distinct phases for Tilesets.txt,
  installed-sheet discovery, catalogue preparation, Building catalogs, and
  thumbnail settings.

## Tile identity, SpawnPoints, and catalogue hygiene

- TileDef tooltips now show the logical tile name, tileset ID, tile index,
  base-game TileDef file number where known, and the correct Build 42 sprite
  ID. The file-number offset is handled for normal, erosion, overlay,
  chunk-caching, and both Jumbo definition files; mod/patch files are labelled
  as dependent on their assigned runtime file number.
- A newly created WorldEd SpawnPoint `Professions` enum now includes `all` and
  all 25 base-game Build 42.20 profession IDs, with their runtime casing.
  Existing project-defined enums remain untouched.
- The shipped and source `Tilesets.txt` are synchronized to all 543 unique
  logical names discovered across 546 installed PNG files. Every entry
  resolves to a current 1x, custom, or 2x PNG; 2x wins deterministically.
- Catalogue rewriting preserves valid stored logical geometry for special
  effect sheets whose decoded PNG rectangle cannot represent their tile
  count, including Giblet, Rain, and large-blood sheets.

## Complete configuration-catalogue audit

- Audited every active shipped configuration file and documented its owner,
  purpose, safe customization policy, and current Build 42.20 counts in
  `docs/PZTools-Configuration-Files.md`.
- `WorldDefaults.txt` now carries the complete profession list and current
  object/zone metadata. `RoomNames.txt` contains 588 entries, and
  `RoomTone.txt` contains 267 unique values including both `Office` and
  `Offices`.
- Corrected 72 burnt-roof references in `BuildingTiles.txt`, removed six
  furniture definitions that referenced nonexistent
  `signs_one-off_05_512` through `_559`, and added the six missing fence
  presets.
- Removed two obsolete `Rearrange.txt` groups whose 20 coordinates were
  outside the current PNG dimensions. All remaining `Rearrange`,
  `RearrangeGrid`, `Blends`, terrain-rule, road, fence, curb, edge, template,
  furniture, and building-tile references resolve within the maintained
  catalogue.
- Removed dormant `MapToPNG.txt`, `Textures.txt`, `TileShapes.txt`, and
  `thumbnails.txt` from the runtime `config` directory. Source history remains
  available where appropriate, but the release no longer presents unused
  files as active settings.

## Build and platform documentation

- Added a complete maintainer build guide for Qt 5.14.2/MSVC: prerequisites,
  out-of-source qmake configuration, incremental/clean builds, release
  assembly, SHA-256 deployment verification, and deployed regression tests.
- Added a source-backed Linux/macOS feasibility audit. Linux requires a
  focused settings/RPATH/AppImage and real-host validation pass; a maintained
  Apple Silicon release additionally needs Qt 6, macOS OpenGL/bundle work,
  codesigning, and notarization. Neither platform is presented as supported
  before those gates pass.

## Native 256 LOT export is now strictly one-to-one

- Special thanks to **шакалоблок**, who reported the Native256 LOT-export bug
  and identified the incomplete-cell symptom that led to this correction.
- A `256 x 256 (Native)` WorldEd project now exports one source cell directly
  to one output `.lotheader` / `.lotpack` cell at the same world coordinate.
  Native projects no longer enter any 300-to-256 bounds conversion.
- The combined TMX map, cell lots/prefabs, RoomDefs, map objects, floor-hole
  checks, and `chunkdata_X_Y.bin` now use the project's source geometry. A
  native cell is placed with a 256-square stride; legacy projects retain their
  historical 300-square conversion path.
- Non-zero and negative world origins remain unchanged during native export.
  The previous conversion could shift origins and introduce extra, partial
  output cells separated by 44-square gaps.
- Native projects now reject an assigned TMX unless it is exactly 256 x 256,
  with the project cell, path, and actual dimensions in the error.
- Added `PZWorldEd.exe --validate-native-256-lot-geometry`, covering one-cell
  worlds, multi-cell worlds, non-zero origins, negative origins, one-to-one
  cell round trips, and preservation of the legacy conversion path.

## Automapper and WorldEd `Rules.txt` isolation

- Automapper now prefers `automapping-rules.txt` beside the target TMX while
  retaining compatible legacy Automapper `rules.txt` manifests.
- WorldEd terrain/BMP `Rules.txt` files are detected from their version,
  alias, rule, and tile-block syntax and are intentionally ignored by
  Automapper. Their contents are no longer expanded into hundreds of false
  missing-file errors.
- An absent or failed manifest is attempted once per open document instead of
  being reread after every brush stroke or object edit. Correct the file and
  use **Reload** to retry. The Automapper dock explains the detected format
  collision without blocking normal painting.
- Added `--validate-automapper-rules` coverage for preferred-manifest
  selection, legacy compatibility, and WorldEd-file isolation.

## Editing responsiveness and documentation

- Audited the work connected to TileZed region/object edit signals after the
  Automapper and minimap side effects were identified. Required renderer,
  Undo, map-state, Lua-tool, and background-minimap updates remain active.
- The Tile Layers panel no longer rebuilds its complete model while hidden.
  While visible, it refreshes only when the edited region and level contain
  the currently inspected square, then catches up immediately when shown.
- Tileset usage/status icons no longer recreate every list icon and tooltip
  for every small region change. Visible updates are grouped by a 150 ms
  single-shot timer; hidden docks defer the refresh until shown.
- The minimap uses its existing named background worker and the already-loaded
  shared tileset catalogue. Ordinary painting does not start a second
  catalogue load.
- Rebuilt the documentation entry points around `DOCUMENTATION.md` and the
  offline `docs/index.html`. The index now routes by application, workflow,
  file type, and troubleshooting task.
- Added a detailed logs/diagnostics reference covering filenames, 20-log
  retention, severity/PID/thread fields, caught Windows/C++ failures, the
  precise libpng suppression policy, deployed regression commands, output
  side effects, and the minimum information for a useful report.
- Expanded the user guide with WorldGen and procedural-loot workflows,
  source/output ownership, confirmed versus tool-enforced limits, scope
  boundaries, and direct troubleshooting entries.
- Expanded the Automapper guide with its input/output mental model, a minimal
  folder and rule-map example, strict layer-suffix behavior, a seven-stage
  sewer walkthrough, performance guidance, and diagnostic locations. The
  source example now includes a preferred `automapping-rules.txt` manifest
  and its own README.
- Feature references now state whether a structure is confirmed against
  Build 42.20, enforced by the editor for safety, representative rather than
  save-identical, or deliberately out of scope.

## Advanced `.pack` comparison and extraction

- Replaced the name-only TileZed comparator with a complete two-pack
  regression view. It reports added, removed, decoded-pixel modified,
  metadata-modified, combined, duplicate, and unchanged textures.
- Added SHA-256 at three useful levels: complete pack files, normalized
  reconstructed RGBA texture canvases, and atlas/trim/canvas metadata. Atlas
  repacking can therefore be distinguished from changed sprite art.
- Added search and status filters, sortable results, pack summaries,
  side-by-side reconstructed previews, a color-coded pixel difference view,
  complete selection details, report copy, and atomic CSV export.
- Expanded the Pack Viewer extractor with checked texture selection,
  contains/prefix/wildcard/regular-expression/exact-tileset filters,
  individual reconstructed PNGs, automatic/1x/2x/custom tileset sheets,
  complete matching atlas pages, three output layouts, safe conflict policies,
  and an optional JSON provenance manifest.
- Added cancellable page/texture progress before the Versatile Pack Extractor
  is shown, plus explicit progress during texture, atlas-page, reconstructed
  sheet, and manifest output. Texture SHA-256 is calculated once per row.
- Hardened legacy version-0 and Build 42 `PZPK` version-1 reading with limits
  and explicit validation for truncated metadata/PNG data, failed PNG
  decoding, atlas bounds, trim offsets, and reconstructed canvas geometry.
  Page alpha flags are preserved on read/write. PNG and reconstructed-sheet
  allocations are bounded, and new pack writes are atomic.
- Added `--validate-pack-tools`, `--render-pack-comparator <path>`, and
  `--render-pack-extractor <path>`, plus
  `TileZed/docs/PZ-Pack-Comparator-and-Extractor.md`.
- The exact libpng `iCCP: known incorrect sRGB profile` warning is omitted
  because it describes non-fatal embedded color metadata and previously
  produced thousands of unactionable lines. Other PNG warnings and errors
  remain visible.
- Replaced the ambiguous binary TileDef error
  `Invalid dimensions, ID, or tile count` in TileZed, BuildingEd, and WorldEd.
  Failures now list every offending field separately, show the stored
  columns/rows and calculated grid capacity, ID, tile-record count, format
  version and applicable limits, and explain the relevant re-export or
  repair/split workflow. Truncated strings, numeric metadata, property counts,
  and property name/value fields have their own positional diagnostics.
- Rebuilt the `.tiles` comparator around a complete structured comparison:
  file SHA-256, unique tilesets, ID/image/grid/count differences, modified and
  one-sided tile records, search/status filters, side-by-side previews,
  highlighted property tables, explicit merge choices, and report
  copy/export. File 2 remains the merge base and structural differences are
  never silently forced into it.
- Enhanced the Snow / Replacement Editor with `SnowTile`/`BurntTile` presets,
  custom-property preservation, multi-target assignment, same-ID mapping,
  resolved/unresolved visual states, status counts, correct dirty tracking,
  safe reopen/close behavior, and targeted property clearing.

## Project WorldGen biome preview and static-prefab editor

- Added two independent commands, enabled only when a saved WorldEd map
  project is loaded: **Tools > WorldGen Biome Editor / Preview...** and
  **Tools > WorldGen Prefab Editor...**. Biome preview/rules/features and
  static-prefab inspection/import/painting/staging are no longer mixed in one
  window.
- The selected game WorldGen directory is explicitly labelled read-only.
  Created and edited definitions are written atomically under the map project
  at `media/lua/server/WorldGen`; game files and `WorldGenOverride.lua` are
  never modified.
- The isolated loader merges game/project biome features, true
  `worldgen.prefabs`, subbiomes, and game/project biomes. Selectors and the
  resolved-definition inspector identify **Game** versus **Project**
  provenance.
- Added creation and in-place editing for project biomes, including registry,
  parent, generation state, simple selection parameters, and weighted
  biome features. Editing a game biome creates a project child variant so
  advanced inherited rules are not lost.
- Corrected the earlier UI terminology: probabilistic 1x1 through 8x8
  patterns are **biome features**, not static prefabs.
- Added a separate true-prefab editor for the four Build 42 runtime slots:
  `Floor`, `FloorFurniture`, `FloorOverlay`, and `Furniture`. It supports a
  visual Tiles palette, multi-square painting, zombie chance, dimensions up
  to the explicit 256x256 tool limit, and a depth-sorted isometric preview
  whose visible chunk guides and full sprite footprints also cover XL/XXL
  Tiles.
- Added strict TMX and one-floor TBX conversion. It stops rather than silently
  dropping z levels, multiple Floor tiles, or per-square stacks that exceed
  the prefab format, and discloses object/building metadata that cannot be
  represented. A malformed version-2 TMX with a missing PZ layer level now
  returns a parser error instead of dereferencing a null layer.
- Added safe game/mod staging outside the base-game directory. It writes the
  prefab plus a marked, replaceable static-module block in the selected map's
  `WorldGenOverride.lua`, preserves unrelated override content, and reports
  exact square/chunk/cell bounds.
- Advanced subbiome, placement, protection, and replacement tables remain
  inherited/read-only in this stage. A hand-authored project biome containing
  such tables is copied to a child variant rather than overwritten.
- WorldEd resolves biome parent chains and displays effective selection
  parameters, feature probabilities, pattern sizes, and rule counts.
- The selected biome is rendered with the configured Tiles catalogue on a
  16 x 16-square canvas: the exact 2 x 2-chunk unit used by Build 42.20
  WorldGen. Ground, plants, bushes, trees, and ore can be toggled separately,
  and square inspection identifies the sprite and source feature.
- JUMBO, JUMBOXL, and JUMBOXXL custom geometries are supported, including
  the XL/XXL `N` plus treetop `N+6` pair and fitted preview margins. Custom
  sheets whose pixel dimensions are incompatible with their declared
  geometry are reported rather than mis-sliced into floating or clipped
  trees.
- Definition Lua runs in an isolated, instruction-limited state with no file,
  package, operating-system, or network libraries.
- The current deterministic seed provides a representative forced-biome
  preview. Game-save noise selection, authored-map replacement context,
  subbiome marker expansion, complete mod packaging/export, roads, and erosion
  are explicitly identified as later or separate work.
- Added `--validate-worldgen-preview=<path>` and an internal screenshot
  renderer for definition-count, inheritance, generation, and GUI regression
  checks.
- Added `--validate-worldgen-prefab-import=<source>` and
  `--render-worldgen-prefab=<path>` for conversion and isometric-editor
  regressions.
- Added `--render-worldgen-prefab-window=<path>` to capture the separate
  prefab catalogue/inspector window and guard against biome controls leaking
  back into it.
- Added `--validate-worldgen-project-overlay=<game>::<project>` to verify
  merged loading and Game/Project provenance.
- Added
  `WorldEd/docs/PZ-B42.20-WorldGen-Editor-and-Prefabs.md` with format
  distinctions, runtime limits, conversion rules, and staging instructions.
- BMP-to-TMX generation now detects the specific legacy/misconfigured case
  where the MapBaseXML setting points at `Rules.txt`. Valid `alias` blocks are
  no longer reported as unknown map-template blocks; the portable
  `MapBaseXML.txt` is used when safe. Manually typed BMP generation paths now
  update their backing settings correctly.

## Procedural loot viewer and editor

- Added the same visual Build 42 loot editor to **TileZed > Tools** and
  **BuildingEd > Building**. TileZed can focus the selected tile's `container`
  property; BuildingEd can focus the current room's internal RoomDef name.
- The isolated, instruction-limited loader reads `Distribution_*.lua`,
  `Distributions.lua`, and `ProceduralDistributions.lua` without exposing
  filesystem, process, package, or network libraries.
- RoomDef/container mappings and reusable procedural distributions identify
  **Game** read-only definitions versus **Project** additions/overrides.
- Direct items and junk display the raw chance per roll and a clearly labelled
  neutral cumulative estimate. Duplicate entries remain independent and are
  disclosed.
- Procedural mappings display relative `weightChance` share, `min`/`max`,
  force-by-item/zone/tile/room selectors, and unresolved references.
- New or cloned definitions are saved atomically outside the game under
  `<project-or-mod>/media/lua/server/Items`. The editor manifest generates a
  Lua override applied during `Events.OnPostDistributionMerge`.
- Added `--validate-loot-distributions <game-root> [project-root]`,
  `--render-loot-distributions <game-root> <output.png> [project-root]`, and
  `TileZed/docs/PZ-B42.20-Procedural-Loot-Editor.md`.

## License-complete portable distribution

- The portable release now carries the GPL, BSD, Qt/LGPL, QuaZip, Lua, zlib,
  Boost, theme, and embedded-helper license texts together with a consolidated
  `COPYING.txt`, `THIRD_PARTY_NOTICES.txt`, `AUTHORS.txt`, and corresponding
  source offer.
- The exact modified source remains identified by the release tag or commit in
  `https://github.com/Unjammer/PZ_Mapping_Tools`; the matching Qt 5.14.2 source
  archive is identified separately.
- Tim Baker's July 30 Qt 6 release-script changes were reviewed. They add Qt
  licensing and notice files to upstream packages; they do not introduce a new
  restriction on creating or distributing this GPL fork.

## August 2 crash and compatibility corrections

- Opening a TMX from TileZed's Maps browser no longer triggers **New Object
  Defaults**. That prompt is now tied to an explicit user selection of the
  object-creation tool.
- Valid one-row sheets such as `Giblet_00` are recovered as 8 x 1 instead of
  failing with zero columns in TileZed, BuildingEd, or WorldEd.
- `LadderS`, `LadderE`, `LadderN`, and `LadderW` are recognized as official
  Build 42 directional tiledef keys.
- The complete discovered PNG catalogue is loaded before an editor exposes a
  map or palette. Resolution searches the complete 2x tree first, then falls
  back to the complete 1x tree; debug, placeholder, custom-pack, and nested
  sheets are not filtered out.
- Every sheet declared by a TMX is also made ready before its first render.
  This covers BMP rules and blend layers whose sheets cannot be inferred from
  normal tile-layer GIDs, while the shared image cache avoids a second decode.

## InGameMap renderer protection

- Analysis of the supplied crash log identified a Project Zomboid WorldMap
  renderer overflow: polygon offsets/counts use signed 16-bit storage and
  highway features generate several zoom-dependent triangulations.
- WorldEd now clips 300-cell polygon geometry into 256-cell export boundaries
  rather than copying whole polygons into every intersected cell.
- A conservative binary-export budget simplifies only temporary highway
  export geometry when necessary. The editable source is untouched, and an
  irreducible cell is rejected with its world coordinates instead of producing
  a game-crashing binary.
- `--validate-ingamemap=<worldmap.xml>` exercises the conversion and guard.

## Tile-definition repair and Build 42 limits

- The Tile Properties editor separates Build 42 mod limits (512 definition IDs
  and 512 tiles per sheet) from the base-game file limits (1024 and 1024).
- Invalid or oversized `.tiles` and `.tiles.txt` files remain loadable for
  repair, while normal Save validates the selected target and refuses an
  unsafe overwrite.
- **Repair / Split for B42 Mods** reassigns IDs in each part and writes
  `<name>_1.tiles`, `<name>_2.tiles`, and matching `.tiles.txt` companions.
  A source PNG with more than 512 tiles is reported as requiring an image
  split, not merely a definition split.

## Shared theme and WorldEd workspace

- Theme Preferences include an opt-in setting that propagates the choice to
  TileZed, BuildingEd, and WorldEd through their shared portable settings.
- Street Names is docked beside Search in WorldEd's lower-left tab group;
  Search remains selected by default.

## Legacy-project and adjacent-cell tilesets

- Restored the historical complete-header loading behavior for TMX and TBX
  maps. Every tileset declaration is resolved before a map is exposed to the
  renderer, including declarations not reported by the newer used-tileset
  accounting.
- This fixes red `???` placeholders in older projects whose exported cells
  contain the same catalogue in different `firstgid` orders. Each current or
  adjacent cell retains its own ordered header instead of borrowing the
  current cell's reduced subset.
- The shared image cache still avoids decoding the same PNG from disk more
  than once. As with the original tools, projects carrying very large headers
  may take longer to open and consume more memory; correctness across cells
  takes priority over the reduced-memory optimization.

## TileZed Build 42 depth-map editor

- Added **Tools > Depth Map Editor...** as a geometry-first editor for Build
  42 `tileGeometry.txt` and `DEPTH_<tileset>.png` data.
- The editor uses the game's eight-column, 128 x 256 depth-tile layout and
  reproduces the orthographic 30/315-degree projection and normalized-depth
  calculation from `TileGeometryUtils`.
- A tile can contain `XY`, `XZ`, or `YZ` polygons, boxes, and cylinders.
  Primitives are drawn as selectable wireframes over the source tile. They can
  be selected on the canvas, dragged in the X/Z plane, and edited precisely
  through translation, rotation, bounds, radius, height, plane, and polygon
  point controls.
- Fixed a painter-state regression that could cover the complete preview with
  the yellow selection brush, hiding the source tile and its 3D wireframes.
  The selected primitive now also displays a compact red/green/blue XYZ gizmo,
  and clicking inside its projected bounds selects it for dragging.
- Tile numbers are rendered in dedicated thumbnail badges, so multi-digit
  identifiers remain visible throughout the complete 0–63 sheet instead of
  appearing to stop at tile 9.
- **Primitive to Pixels** adds one projected primitive to the existing depth;
  **Rebuild Tile from Geometry** regenerates the tile from all its primitives.
  Source-image opacity can be used as the same kind of generation mask used by
  the game editor.
- The previous painting controls remain available in a separate **Pixel
  Retouch** tab for inspection, correction, copying, and undo/redo. The
  vertical gradient is explicitly identified as a manual seed, not generated
  game geometry.
- Existing version-1 and version-2 geometry can be read. Writes use the Build
  42 version-2 integer-coordinate format and atomically replace only the
  selected tileset block, preserving unrelated tilesets and tile-property
  blocks.
- `Ctrl+S` writes both the full Build 42 depth atlas and changed geometry.
  Trimmed game depth atlases can be loaded safely; normal saves emit the
  complete 1024-pixel-wide atlas expected by the game writer.
- The command-line regression check now validates 3D box rasterization, PNG
  geometry and alpha, geometry round-tripping, and preservation of unrelated
  `tileGeometry.txt` content. It also checks selection-preview rendering and
  can probe a real Build 42 geometry file all the way through the editor list.

## Source provenance

- Added `UPSTREAM-HISTORY.md` with the exact WorldEd and
  TileZed/BuildingEd source branches, immutable initial baseline commits,
  reconstruction branch points, and selectively ported upstream commits.
- Clarified that the combined repository is not a conventional GitHub fork
  because it contains two independent upstream trees.
- Recorded that no CE code, patch, asset, documentation, or Git history was
  used as a source for this iteration.

## Project Zomboid 42.20 mapping data

- Tile-definition loading now follows the Build 42.20 game order, including
  `newtiledefinitions`, erosion definitions, overlay definitions, chunk-caching
  definitions, the NoiseWorks patch, and both native Jumbo definition files.
- `.patch.tiles` files now merge properties into their base definitions.
  Version-1 limits are validated for legacy 512-tile files and the 1024-tile
  `newtiledefinitions` format.
- Native 256-cell lot generation no longer applies WorldEd's legacy
  fake-Jumbo randomizer. Explicit Build 42 Jumbo trees and Biomemap/WorldGen
  decisions are preserved.
- Biomemap ID 171 remains available as **Forced Redbud Jumbo XXL (map
  override)**. It is enabled per map by `WorldGenOverride.lua`; placement and
  runtime behavior are documented in `docs/PZ-B42.20-Jumbo-Trees.md`.
- Integrated Tim Baker's `f492c5c` LotPackViewer Z-coordinate correction.
- Synchronized the Build 42.20 room-name catalogue and the `connectX` /
  `connectY` tile properties with the game scripts.

## World Street Name Editor

- Replaced the obsolete Road dock with a dedicated editor for Build 42.20
  `streets.xml` files.
- Existing street files can be loaded and edited as named, variable-width
  polylines directly over the World view. Streets and points can be created,
  selected, dragged, inserted, removed, reversed, split, undone, and redone.
- Street coordinates follow the project's cell geometry and world origin.
  Invalid names, widths, coordinates, duplicate consecutive points, and
  incomplete polylines are rejected before output.
- Street overlays can be shown or hidden and their cosmetic line thickness is
  adjustable. Logical street width also influences the preview; the selected
  street receives a high-contrast blue highlight and larger label.
- Street labels now use a bordered, rounded high-contrast background instead
  of bare text. At distant World zoom levels, non-selected labels are culled
  and screen-space collision checks prevent the former unreadable pile-up;
  the selected street label remains visible.
- The Street Names interface is a movable, floatable and dockable tool window.
  Existing installations float it once at a compact default size, while its
  subsequent dock position is remembered. Geometry commands use compact icon
  buttons with descriptive tooltips, so the normal left dock can still be
  collapsed and does not need to remain unusually wide.
- Streets are visible and editable in both World and Cell views. Cell views
  only instantiate the lines crossing that cell.
- Clicking a visible street in World or Cell navigation mode selects and
  highlights it without changing the active WorldEd cell/object selection.
  Empty clicks and normal cell double-clicks remain available to WorldEd.
- The street list now has a live case-insensitive name search. Its Street,
  Width, and Points headers are sortable; width and point counts use numeric
  ordering.
- Navigation, geometry editing, and creation are explicit modes. Navigation
  never consumes map clicks or double-clicks, restoring normal cell opening;
  point dragging and insertion only occur after **Edit Geometry** is enabled.
- The traditional Objects dock remains the initially selected dock when the
  Street Names dock is introduced into an existing or fresh layout.
- Writes are atomic. The normal WorldEd Save action, including `Ctrl+S`, also
  generates or updates `streets.xml` whenever the project contains street
  data, unsaved street edits, or an existing street file.
- `streets.xml` is included in WorldEd's Maps browser alongside the project
  images and TMX/PZW files, without being misinterpreted as an image preview.

## Day/night lighting preview

- The experimental **Night Preview** action and DAY/NIGHT canvas control are
  now hidden and disabled in WorldEd, TileZed, and BuildingEd. A circular
  compositor cannot reproduce the game's Lighting64 renderer faithfully
  enough to be presented as a dependable mapping preview.
- The prototype implementation remains isolated in the source for future work
  against a faithful renderer; it always stays off and is not user-accessible
  in this release.
- WorldEd keeps the dependable visual-test modes in a compact strip at the
  bottom of the main window, immediately before zoom: **POWER**, **SNOW**, and
  **JUMBO**. The reserved DAY/NIGHT control is hidden.
- The preview dims the complete scene, reads Build 42 tile-definition
  `IsoType=lightswitch`, `lightR`, `lightG`, `lightB`, and `LightRadius`
  properties, and draws colored halos at their mapped positions through a
  composition path supported by both Qt raster and OpenGL. Explicit radii
  remain authoritative; if an RGB definition is unavailable, WorldEd derives
  the lamp color from its powered `*_on` sprite before using the configurable
  fallback.
- A `lightswitch` without RGB is treated as a room controller, matching the
  Build 42 distinction. Rooms containing such a switch receive a soft powered
  interior fill in WorldEd cells, TileZed composites and BuildingEd.
- Tile properties are merged from both registered `.tiles` definitions and
  properties embedded in TMX/TBX data, with case-insensitive key matching.
  Standard `lighting_outdoor_*` lamps and the known first-row
  `lighting_indoor_01` room switches receive a vanilla fallback when no
  `.tiles` property file is configured; explicit tiledefs remain authoritative.
- World view receives the global night treatment; cell/building views add the
  tile and room-aware lighting. The overlay never accepts mouse input, so
  selection, painting and street editing continue to work normally.
- The retained internal DAY/NIGHT prototype includes Darkness, Light
  intensity, fallback radius, and fallback color parameters, but does not
  expose them in this release.
- **POWER** draws the transparent same-index `*_on` sprite immediately over
  the original tile, preserving the base image (including lamp posts), flips,
  source-layer ordering, and isometric occlusion.
- **SNOW** previews `SnowTile` definitions and includes the Build 42
  `roofs_01` through `roofs_05` fallback mapping to `e_roof_snow_1`, allowing
  roof-coverage gaps to be checked without launching the game. Mappings are
  prepared for every level in the composite, so roof snow no longer depends
  on selecting each Z level once.
- **JUMBO** recognizes `jumbo_tree_01_0` markers and selects a stable
  coordinate-derived XL or XXL variant from the available catalogue. The
  preview reconstructs the Build 42 `IsoTreeJumbo` pair by drawing the main
  sprite `N` together with its treetop `N+6`. SNOW/JUMBO substitutions and
  POWER/Jumbo-foliage overlays are inserted by both the raster renderer and
  OpenGL VBO gatherer at the source tile's exact ordered position; no
  scene-wide overlay is created. These previews are in-memory only and never
  alter TMX/TBX data or lot output.

## WorldEd cell-opening and OpenGL regression fixes

- Cell changes no longer generate thousands of null-item removal warnings
  when distant street labels were culled. This removes the associated UI and
  log-writing delay.
- WorldEd registers and resolves every embedded TMX tileset declaration for
  current and adjacent cells. The earlier used-GID-only optimization was
  removed because PZ BMP rules, blend layers, and legacy per-cell header
  ordering can require a declared sheet that has no normal tile-layer GID.
- The complete global catalogue is loaded once at startup. Per-cell readiness
  then reuses that shared cache instead of decoding the same PNG again.
- The OpenGL 3.3 viewport now requests the compatibility profile required by
  WorldEd's legacy VBO renderer. The previous forced core profile initialized
  its shaders but rejected draw calls without a vertex-array object, leaving
  object outlines visible while all tiles disappeared.
- The 256/300 project-grid badge is rasterized before it is handed to the
  OpenGL viewport, preventing the post-VBO glyph-cache corruption that made
  its text striped or unreadable.

## Tileset import, discovery, and catalogue maintenance

- Opening a `def.tiles` file now resolves tilesheets through the same
  pack-aware 1x/2x path resolver as the rest of the suite. If a PNG changed
  dimensions, the existing tile-definition matrix is resized automatically
  and properties are preserved at their tile coordinates.
- A resize detected while the Tile Definitions editor is already open is
  synchronized immediately. The document is marked modified so closing it
  prompts to save the updated geometry; deleting and re-adding the tileset is
  no longer necessary.
- **Tools > Tilesets... > Add Tilesets** now imports a browsed external PNG
  into the active Tiles tree while preserving the `1x` / `2x` and `.pack`
  layout. Name collisions are checked by content and are never overwritten
  silently.
- Invalid or undersized sheets are rejected with their selected and resolved
  paths. Zero-column tilesets can no longer crash while `Tilesets.txt` is
  saved, and duplicate registration caused by the directory watcher is safe.
- Valid single-row sheets such as `Giblet_00` are accepted. When the in-memory
  column count is lost, an unambiguous `N x 1` layout is recovered from the 1x
  or 2x PNG before saving; genuinely inconsistent geometry remains blocked.
- Tileset addition, path resolution, import, geometry validation, and
  catalogue-save boundaries are logged and flushed immediately. Unhandled
  Windows and C++ exceptions now leave a final diagnostic in each application
  log instead of ending after an incomplete error dialog.
- WorldEd, TileZed, and BuildingEd watch the configured Tiles directories. A
  newly copied PNG can be discovered and loaded without restarting the tool.
- Removing a PNG while an editor is open is now handled symmetrically. A
  directory-only watcher notification invalidates the deleted sheet, open maps
  receive the missing-tile placeholder with the correct geometry, and
  thumbnail workers cannot race the in-place image replacement.
- BuildingEd now performs the same startup discovery across 1x, 2x, and
  immediate `.pack` subdirectories. TBX references are matched
  case-insensitively and resolved on demand before the **Missing Tilesets**
  dialog is shown, preventing available sheets from being reported absent.
- Added **Edit > Update Tilesets.txt from Tiles PNGs...**. It adds new sheets,
  refreshes changed dimensions, preserves metadata enumerations, and keeps a
  `Tilesets.txt.bak` backup.
- Automatic PNG discovery remains memory-only. Startup and directory-watcher
  scans no longer rewrite or progressively inflate `Tilesets.txt`.
- TileZed opens TMX files using the historical complete-catalogue behavior.
  Every declared sheet is ready before the first render because BMP rules and
  blend layers may use sheets that do not appear in normal tile-layer GIDs.
- Startup discovery records new PNG dimensions without decoding every image.
  PNGs copied while an editor is running are still discovered and loaded
  immediately.
- The first-run path chooser accepts an installation root containing `config`
  and/or `Tiles`, as well as either directory itself.

## BuildingEd Lua furniture objects

- BuildingEd's Lua API is now version 3 and can create catalog-backed
  `FurnitureObject` instances transactionally instead of being limited to
  UserTiles.
- Scripts can enumerate furniture groups, definitions, orientations, sizes and
  component tiles; `findFurniture(tileName)` provides reverse catalogue lookup
  for converting loose tiles into structured objects.
- `placeFurniture(...)` additions participate in the script's single undo
  transaction. Failed or cancelled scripts discard staged objects without
  modifying the building.

## WorldEd loading, thumbnails, and adjacent cells

- Fixed the integer division-by-zero identified in the supplied WorldEd logs.
  After tile definitions were loaded, metadata lookup could divide a tile ID
  by the zero column count of a still-unresolved embedded TMX/TBX tileset.
  Lookup now reuses the canonical catalogue geometry or safely defers metadata.
- Hardened the legacy Jumbo tree overlay as well: a zero-column declaration
  or an incomplete four-row Jumbo variant can no longer cause a division by
  zero or invalid tile dereference in a thumbnail worker.
- Applied the same defensive Jumbo rendering rules to TileZed and BuildingEd.
  Invalid or not-yet-resolved geometry is skipped for the optional
  trunk/leaves overlay while the base tile remains renderable.
- WorldEd startup reports the current sheet and `n / total` while loading the
  complete discovered catalogue. Current and adjacent cells keep their own
  ordered declarations and reuse the already-decoded shared images.
- World projects can be supplied on the command line. The internal
  `--cell=x,y` validation path opens a real cell through the same document and
  renderer pipeline used by the UI, with an explicit success/failure log.
- World-thumbnail progress is modeless and event-driven. The GUI no longer
  spins in a modal `processEvents()` loop or appears permanently frozen while
  thumbnails are being generated.
- WorldEd and TileZed again register every embedded TMX tileset declaration
  with the shared image cache. Artwork loaded for one map is reused by current
  and adjacent cells regardless of embedded catalogue-size differences,
  restoring the historical cross-cell fallback instead of displaying red
  unknown tiles.
- WorldEd's thumbnail-cache version was advanced so thumbnails containing
  stale red unknown-tile placeholders are regenerated automatically.
- A tileset finishing its asynchronous load invalidates the affected WorldEd
  CellScene buffers, allowing cells already on screen to refresh with the real
  artwork.

## Startup safety and diagnostics

- TileZed skips automatic document restoration after an unclean shutdown,
  preventing a malformed previous session from creating a startup crash loop.
- Fatal-error and terminate handlers were added across the suite so unexpected
  startup, loading, and import failures leave useful information in the log.
- Thumbnail/map-reader threads now have explicit names in logs, thumbnail
  render start/finish records include the full TMX path, and Windows access
  violations record the owning DLL plus module offset. A formerly unguarded
  missing-image replacement is now protected by the shared tileset image
  write lock, preventing renderer workers from reading an image while it is
  replaced.

## BMP color validation

- TileZed's BMP Tools warning page can optionally repair unknown pixels during
  color checking instead of only listing them.
- Main and vegetation fallbacks are selected independently from their actual
  Rules palettes. Black is the default for both images, and the complete
  replacement is a single undoable operation.

## Portable/non-administrator operation

- Administrator privileges are not required. The portable package stores
  shared settings and logs beneath its own `settings` directory and writes
  catalog updates beneath `config`, so the package directory and edited map
  project must be writable by the current account.
- A normal user can run the published package from an extracted user-owned
  directory. Installing it under `Program Files`, a read-only share, or
  another ACL-protected directory will prevent settings/log/catalog writes;
  elevation is not the intended workaround—move or extract the package to a
  writable location.

## WorldEd CellView OpenGL renderer

- Integrated Tim Baker commit `a7d5a77`, replacing CellView's fixed-function
  rendering path with an OpenGL 3.3 shader.
- Corrected the upstream include typo and replaced removed `GL_QUADS`
  primitives with core-profile-compatible triangle fans. CellView now requests
  the matching OpenGL 3.3 core context explicitly.
- Missing OpenGL 3.3 support, invalid viewports, and shader failures now produce
  actionable log entries and stop the affected draw safely.
- OpenGL remains optional. **Edit > Preferences** explicitly offers OpenGL 3.3
  or Qt's raster/software renderer, and the selected backend is now recorded in
  the log.
- Fresh installations no longer show invisible helper tiles by default. Their
  wireframe placeholder could cover a valid cell and make it appear as if the
  renderer or tilesets had failed; it remains available from **View > Show
  Invisible Tiles**.
- Corrected the project-grid badge's corrupted multiplication and separator
  glyphs.

## BuildingEd welcome-page browser

- Fixed the crash when opening a TBX from the integrated Buildings browser.
  That action now uses the same `BuildingEditorWindow` loading path as **Open
  Building**, instead of dereferencing TileZed's unavailable main window.
- Browser activations and failed opens now include the full TBX path in the
  application log.
- BuildingEd loads the complete discovered catalogue before a building,
  palette, template, or furniture view is exposed. This intentionally favors
  deterministic availability over lower startup memory use.
- Startup discovery no longer causes each newly found PNG to be decoded once
  through UI notifications and then revisited by a second catalogue pass.
  Readiness checks now distinguish real decode attempts from already-loaded
  tilesets in the log.
- Added an end-to-end BuildingEd validation mode covering all 26 building
  templates, both object-category docks, Tile mode, 175 furniture groups and
  Lua furniture placement with Undo/Redo.

## Package validation

- PZWorldEd, TileZed, and BuildingEd were rebuilt from the current source tree
  with the Qt 5 / MSVC environment.
- The validated portable package is produced only in
  `C:\pz\build\PZTools-Qt5-Latest`.

# Changes in the July 28, 2026 release

This release consolidates the WorldEd, TileZed and BuildingEd adjustments
completed on July 28, 2026. It describes shipped changes only; investigations
that did not result in a source or package change are not listed as fixes.

## WorldEd performance and thumbnails

- Fixed WorldView context menus taking minutes to appear on large projects.
  Menu creation no longer opens and parses every cell TMX to evaluate the
  empty-border command. That exhaustive scan now runs only when the command is
  selected.
- Replaced the single thumbnail renderer with a pool of up to four workers.
  Current-project and adjacent-world thumbnails can be prepared and rendered
  concurrently.
- **Recreate Thumbnail** now queues the forced render directly instead of first
  loading the old cached PNG. A cache miss no longer causes the same TMX to be
  rendered twice.
- Moved PNG encoding and writing from the GUI thread into the render workers.
- Removed a redundant GUI-thread scan of every rendered layer and tileset.
- Relinked compatible embedded TMX tilesets to the shared catalog so a cell does
  not retain hundreds of duplicate tileset definitions and valid 2x artwork
  does not fall back to `??`.
- Collapsed incompatible embedded-tileset warnings into one summary per TMX.
  The previous large-map test produced 39,609 individual warning lines.
- Fixed stale missing-tile VBO data after an asynchronous tileset becomes
  available. This addresses water and other tiles remaining as `??` in
  adjacent-cell mode.
- Fixed WorldView thumbnail visibility and per-PZW visibility restoration for
  adjacent worlds.

The observed late-run slowdown was also correlated with source complexity:
the first 349 completed TMX files averaged 0.14 MiB, while the final 131
averaged 2.6 MiB and reached 16.7 MiB. The worker pool was not progressively
reduced; later jobs contained substantially more map data.

## WorldEd object and interface workflow

- **Create Object** now asks for persistent Name and Type defaults, initialized
  from the selected object or current object group when possible.
- New objects receive those values immediately instead of being created without
  a name/type.
- The creation tool remains active after drawing an object so multiple zones can
  be created in succession.
- The **Maps** browser now shows file size and modification date.
- Fixed the 300 x 300 / 256 x 256 WorldView and CellView badge leaving duplicate
  images while scrolling.

## Biomemap and foraging zones

- Aligned green-channel generation with the game's `metazoneHandler`.
  `Vegitation`, `DeepForest`, `Forest`, `TownZone`, `Farm`, `FarmLand`, and
  `TrailerPark` are the only WorldEd zone types rasterized into Biomemap.
- Every other vector zone/object type remains an `objects.lua` export. The
  generator reports the project types assigned to each path after generation.
- Added strict validation for unknown green-channel IDs and warnings for mixed
  zone IDs within the game's 8 x 8 chunk sampling area.
- Preserved exact channel bytes; no silent scaling or resampling is performed.
- Added explicit handling and reporting for incomplete boundary images when a
  legacy 300-cell project is emitted as complete 256 x 256 Biomemap tiles.

## TileZed

- **Insert Object (O)** now asks for reusable Name and Type defaults and assigns
  them to newly drawn objects.
- If the selected level has no Object Layer, TileZed offers to create one above
  that level's existing layers. An existing Object Layer is reused.
- The **Maps** browser now displays file size and last-modified time.
- Fixed the Lua `distanceIndicator` used by `tool-four-directions.lua`: it now
  renders above map content and follows the current level instead of level zero.
- Added the 300 x 300 / 256 x 256 project-grid badge and fixed its refresh
  regions during scrolling.
- Map opening registers the full catalog but waits only for tilesets actually
  used by the document.

## BuildingEd and shared tilesets

- Preserved the lazy catalog and category-loading fixes from the current working
  tree.
- Building documents and category palettes wait for their required tilesets.
- Re-entrant category refreshes are coalesced, preventing recursive population
  and stack-overflow crashes.
- `--validate-building-categories` verifies all templates, the Tile and
  Furniture palettes and all 17 Ortho/Iso category groups.

## Packaging and validation

- Rebuilt WorldEd, TileZed and BuildingEd with Qt 5.14.2 and MSVC 2017.
- Completed a clean full WorldEd build followed by an incremental verification
  build.
- Startup-tested the packaged WorldEd executable.
- Retained only `build/PZTools-Qt5-Latest`; obsolete test builds, backups and
  intermediate build directories were removed.
- Current WorldEd SHA-256:
  `C75BFAEE2DBA4785FFAB87DFD2D4B81411250111A04D553A42C30BE0170C3882`.

## Signing note

- The package is not Authenticode-signed. Windows environments enforcing
  Enterprise signing can reject `PZWorldEd.exe` or bundled libraries such as
  `zlib1.dll`.
- This is a policy/signing result, not evidence that loading 500+ PNG files or
  the resulting normal multi-gigabyte working set is itself a defect.
