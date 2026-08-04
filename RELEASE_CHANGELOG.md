# Changes in the August 3, 2026 update

This cumulative update includes the mapping-tool corrections completed August 3, 2026.

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