# PZ Mapping Tools - Feature Provenance

## Purpose and methodology

This document records feature-level authorship separately from repository
ancestry. Tim Baker created the original Project Zomboid WorldEd and TileZed
foundation. BuildingEd is historically part of the TileZed source tree. Alree /
Unjammer created and maintains the long-running unofficial continuation,
including its independently developed features, fixes, Qt 5 maintenance, and
current combined releases.

The audit used source diffs rather than repository age or commit subjects
alone. It covered the public Tim Baker repositories, the historical Unjammer
WorldEd and TileZed forks, the local reconstruction branches used to assemble
the maintained Qt 5 trees, the combined PZ_Mapping_Tools repository, and
preserved pre-fork C# mapping utilities. The C# archives are historical design
and implementation evidence and are not presented as public PZ Mapping Tools
releases. The native TileSetZ feature separately adapts Alree / Unjammer's C#
TileSetZ texture projections and original PNG masks into the C++/Qt editor.

The earliest verified date is used whenever an exact first implementation
cannot be proved. Equivalent later work does not erase an earlier independent
implementation.

## Repository lineage

| Line | Role | Earliest evidence used |
|---|---|---|
| `timbaker/pzworlded` | Original WorldEd foundation and later upstream work | Full upstream history through `8d01befa83df1afc50f08e9faad00dde4e42925c` |
| `timbaker/tiled` | Original TileZed, BuildingEd, and Tiled-derived foundation | Full upstream history through `f4c25c2a119747b588981563987a1817b74dd7d5` |
| `Unjammer/WorldEd` | Historical unofficial WorldEd continuation | [`fd4ed27dab993c4f8b85091f463fe6f8067eeccc`](https://github.com/Unjammer/WorldEd/commit/fd4ed27dab993c4f8b85091f463fe6f8067eeccc), 2023-03-06 |
| `Unjammer/TileZed` | Historical unofficial TileZed and BuildingEd continuation | [`b5fb778e54c5a36c32b29b8df9489cbcd295a20a`](https://github.com/Unjammer/TileZed/commit/b5fb778e54c5a36c32b29b8df9489cbcd295a20a), 2023-03-06 |
| Local Qt 5 reconstruction branches | Selective reconstruction from Tim baselines plus preserved Unjammer behavior | 2026-07-22 |
| `Unjammer/PZ_Mapping_Tools` | Current combined maintained source and public releases | [`2ae112db61f582c086dcb3f892e1271bae1ea174`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/2ae112db61f582c086dcb3f892e1271bae1ea174), 2026-07-24 |
| Preserved C# mapping utilities | Earlier Alree / Unjammer experiments and working tools, not current C++ source | Verified Git commits from 2022-03-10 and preserved source from 2022 onward |

The precise combined-tree baselines and branch relationships are documented in
[`UPSTREAM-HISTORY.md`](UPSTREAM-HISTORY.md).

## Provenance classifications

- `UPSTREAM_FOUNDATION`: functionality inherited from Tim Baker's original
  WorldEd, TileZed, or BuildingEd code.
- `UNJAMMER_ORIGINAL`: functionality first verifiably introduced in the
  Unjammer continuation.
- `TIM_UPSTREAM_PORT`: later Tim Baker functionality intentionally imported.
- `INDEPENDENT_PARALLEL_IMPLEMENTATION`: equivalent work developed separately
  in both histories.
- `NOT_PORTED_ALREADY_IMPLEMENTED`: a later upstream feature was unnecessary
  because the maintained tree already had equivalent behavior.
- `NOT_PORTED_ALREADY_FIXED`: a later upstream fix was unnecessary because the
  maintained tree had independently fixed the defect.
- `TIM_FIX_PORTED`: a later Tim Baker fix solved a defect still present in the
  maintained tree and was imported.
- `THIRD_PARTY_COMPONENT`: external code, library, algorithm, or theme asset.
- `IDEA_OR_FEEDBACK`: a request, test case, or idea without implementation
  authorship.
- `NEEDS_FURTHER_VERIFICATION`: available evidence does not support a more
  exact attribution or date.

## Master feature provenance table

| Feature | Earliest verified Unjammer evidence | Tim Baker equivalent | Current status | Classification |
|---|---|---|---|---|
| Original WorldEd architecture and mapping workflow | Inherited | Original source | Retained and extensively maintained | `UPSTREAM_FOUNDATION` |
| Original TileZed and BuildingEd architecture | Inherited | Original source | Retained and extensively maintained | `UPSTREAM_FOUNDATION` |
| Original basement data model | Inherited during Build 42 reconstruction | Tim `basements` branches | Retained with later Unjammer placement and preview workflows | `UPSTREAM_FOUNDATION` |
| Qt 5 and MSVC maintenance | Historical forks by 2023-03-06, reconstructed at `9bac0fdc` | Later upstream moved toward Qt 6 and cross-platform builds | Current Windows Qt 5 line is Unjammer maintained | `UNJAMMER_ORIGINAL` |
| Portable settings and standalone layout | 2025-01-18 fork, reconstructed at `53e8a8e6`, `8cd6f291`, `2bd2dd53` | No equivalent post-baseline port used | Current | `UNJAMMER_ORIGINAL` |
| Dark theme fixes | WorldEd `fd4ed27d`, TileZed `b5fb778e`, 2023-03-06 | No earlier equivalent found in audited post-baseline history | Evolved into current theme system | `UNJAMMER_ORIGINAL` |
| External QSS theme loading and persistence | 2025-01-18 forks, reconstructed at `2f8f81f8` and `752c8700` | No port used | Current | `UNJAMMER_ORIGINAL` |
| Theme assets | Various bundled QSS sources and contributors | Separate from application integration | Notices retained where applicable | `THIRD_PARTY_COMPONENT` |
| Tile IDs and names in editor palettes | TileZed `b5fb778e`, 2023-03-06 | Tim later added `tileset_index` tooltips in `c8a4e143` | Maintained implementation predates later upstream tooltip | `NOT_PORTED_ALREADY_IMPLEMENTED` |
| Tile and tilesheet export actions | TileZed `b5fb778e`, 2023-03-06 | Original extraction foundations existed upstream | Expanded by Unjammer | `UNJAMMER_ORIGINAL` |
| Native TileSetZ texture and tilesheet editor | Alree / Unjammer C# TileSetZ projections and masks, adapted in Build 20260908 | No upstream implementation used | Native pixel editing, texture cutting and mask generation | `UNJAMMER_ORIGINAL` |
| Advanced Tiles Unpacker | TileZed `b5fb778e`, 2023-03-06, reconstructed at `8701cb95` | Original pack tools inherited | Current extractor is substantially extended | `UNJAMMER_ORIGINAL` |
| Complete pack, tileset, tile, and object extraction | Preserved C# `PZ_Mapper` pack reader from 2023, current C++ implementation in combined tree | No matching audited upstream workflow | Current | `UNJAMMER_ORIGINAL` |
| Lua mapping API corrections | TileZed `b5fb778e`, 2023-03-06 | Tim later fixed macOS Lua menu retention in `5108244c` | Both provenances retained by change | `INDEPENDENT_PARALLEL_IMPLEMENTATION` |
| WorldEd road and InGameMap feature generation | WorldEd `fd4ed27d`, 2023-03-06 | Original InGameMap framework inherited | Unjammer route classes and controls added | `UNJAMMER_ORIGINAL` |
| Multiple-cell thumbnail regeneration | WorldEd `fd4ed27d`, 2023-03-06 | Original thumbnail framework inherited | Current larger thumbnail and preview workflow evolved from it | `UNJAMMER_ORIGINAL` |
| InGameMap polygon validation | WorldEd `fd4ed27d`, 2023-03-06 | Original generator inherited | Independent guard for invalid short polygons | `UNJAMMER_ORIGINAL` |
| Full building outlines for InGameMap | WorldEd Build 42 fork, 2025-01-18 | Later Tim building export corrections reviewed separately | Current generator supports placed lots and embedded RoomDefs | `UNJAMMER_ORIGINAL` |
| Biomemap Generator | WorldEd `e1438f55`, 2025-01-18 | No earlier Tim generator found in audited baseline history | Current editor evolved from this generator | `UNJAMMER_ORIGINAL` |
| Biomemap `_veg` input and automatic splitting | WorldEd B42 source and README, 2025-01-18 | No later upstream port used | Current | `UNJAMMER_ORIGINAL` |
| Biomemap red and green channel editing | Combined-tree development after the original generator | No Tim equivalent used | Current | `UNJAMMER_ORIGINAL` |
| Native 256 x 256 source and output cells | WorldEd `e1438f55`, `lotfilesmanager256.cpp`, 2025-01-18 | Later Tim `basements` code has 256 output support | Current native one-to-one project mode descends from Unjammer code | `INDEPENDENT_PARALLEL_IMPLEMENTATION` |
| Per-project 256 or 300 grid geometry | Reconstruction `5462f480` and `a940fd5f`, 2026-07-22 | No later port used | Current | `UNJAMMER_ORIGINAL` |
| Partial Chunk LOT export | Combined-tree implementation after 2026-08-12 | No Tim equivalent found | Current | `UNJAMMER_ORIGINAL` |
| Build 42 animals, zones, WorldGen, biome, and prefab editors | Historical B42 fork from 2025-01-18, expanded in combined tree | Basement foundations and formats inherited where applicable | Current feature-specific editors are Unjammer implementations | `UNJAMMER_ORIGINAL` |
| OSM native project generation | Map-style workflow documented since 2023, native C++ implementation in combined tree | No Tim equivalent found | Current | `UNJAMMER_ORIGINAL` |
| OSM as the map-data source | SadPeanut's Pz-RealLifeMap demonstrated the OSM approach | External idea, no copied code or assets | Credited separately | `IDEA_OR_FEEDBACK` |
| LotPackViewer invalid-z fix | No earlier correction found | Tim [`f492c5cd`](https://github.com/timbaker/pzworlded/commit/f492c5cde209e1ef907af5d84f93c89cea4b1927), 2026-07-29 | Ported | `TIM_FIX_PORTED` |
| OpenGL 3.3 renderer | Existing maintained renderer required adaptation | Tim [`a7d5a77c`](https://github.com/timbaker/pzworlded/commit/a7d5a77ccacddef3909decda2da3167a2c4f03e1), 2026-07-29 | Ported with Qt 5 and core-profile corrections | `TIM_UPSTREAM_PORT` |
| MixedTilesetView first-row fix | No earlier correction found | Tim [`2e0d35d5`](https://github.com/timbaker/tiled/commit/2e0d35d5bf4286de1ce55820a0ea2c954b7924ff), 2026-07-29 | Ported | `TIM_FIX_PORTED` |
| WorldScene lot-image order | Maintained renderer and cache differ | Tim [`179a0145`](https://github.com/timbaker/pzworlded/commit/179a0145e46fa427a2817aad4d11c2cb9824723c), 2026-08-06 | Ported with adaptation | `TIM_FIX_PORTED` |
| Rosewood prison BuildingDefs export | Maintained global overlap collection already covers the case differently | Tim [`8d01befa`](https://github.com/timbaker/pzworlded/commit/8d01befa83df1afc50f08e9faad00dde4e42925c), 2026-08-11 | Upstream rewrite not imported | `NOT_PORTED_ALREADY_FIXED` |
| BuildingEd category filters | Existing categories retained | Tim [`39ec9c97`](https://github.com/timbaker/tiled/commit/39ec9c977921aab5cea2e13b437e9fbb06d210db), 2026-08-08 | Applicable UI behavior selectively adapted | `TIM_UPSTREAM_PORT` |
| Current About and provenance UI | Combined-tree implementation, 2026-08-15 | None | Current | `UNJAMMER_ORIGINAL` |

## Historical Feature Timeline

### Build 41 era

#### 2022-03-10 - Compiled-map movement and zombie-layer utilities

**Repository:** preserved `PZ_MoveMap` and `PZ_ZombieLayerReplacer` histories  
**Commits:** `b2a962257c53f5fea8c6877f090794a568dd7922`,
`f440590f596ba4ae0e78643e43aac41f76576cdb`,
`fa0bc4c4690d7ecab3873bbdda576e4d45684a36`, and
`9685c0c3f6caa46503e5eb0e82db97f6620e2b33`  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

These C# utilities provide verified early work on Project Zomboid map
coordinates, compiled map data, and zombie-layer manipulation. They are
historical precursors, not source dependencies of the current Qt tools.

#### 2022-03-12 - Chunk removal workflow

**Repository:** preserved `PZ_ChunkWiper` history  
**Commits:** `06637a6200a5aa0df3e88538593b4632813ba5ee` and
`76eb8bd9e6b6050e9053bba2e621b8d8a4ae8940`  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

The utility is verified evidence of chunk-level map manipulation years before
the current Partial Chunks editor. The modern feature is a separate C++
implementation and does not reuse this C# code.

#### 2022 to 2023 - Map generation and compiled-map inspection experiments

**Repository:** preserved C# `PZ_MapGen`, `PZ_MapMover`, and `PZ_Mapper` source  
**Files:** noise and terrain generation, lotpack and lotheader readers, TileDefs
and texture-pack readers, TMX/PZW writers, image generation, building extraction,
and worldmap-building output  
**Implementation:** Alree / Unjammer  
**Provenance:** `NEEDS_FURTHER_VERIFICATION`

The source and file metadata verify working implementations and design
continuity. These particular archives do not all retain Git history, so their
file dates are not used as unsupported exact public-release dates. Their
existence supports historical authorship of the ideas and precursor tools, not
a claim that the current C++ code was copied from them.

#### 2023-03-06 - First verified public Unjammer WorldEd feature set

**Repository:** `Unjammer/WorldEd`  
**Commit:** [`fd4ed27d`](https://github.com/Unjammer/WorldEd/commit/fd4ed27dab993c4f8b85091f463fe6f8067eeccc)  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

The diff and its README document dark-theme tree fixes, an invalid InGameMap
polygon guard, restored Roads controls, selected-cell thumbnail regeneration,
configurable road-feature thresholds, primary through railway road features,
15-floor testing, explicit session loading, and Qt deprecation work.

#### 2023-03-06 - First verified public Unjammer TileZed and BuildingEd feature set

**Repository:** `Unjammer/TileZed`  
**Commit:** [`b5fb778e`](https://github.com/Unjammer/TileZed/commit/b5fb778e54c5a36c32b29b8df9489cbcd295a20a)  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

The diff and README document dark-theme corrections, grid opacity and thickness,
2048-pixel pack textures, Lua map and object-layer corrections, BuildingEd TMX
and binary export, 32 floors, visible tile IDs and names, multi-sheet export,
tile context export, and early advanced unpacker controls.

#### 2023-03-24 - Expanded themes, InGameMap, thumbnails, and diagnostics

**Repository:** `Unjammer/WorldEd`  
**Commit:** [`79c25675`](https://github.com/Unjammer/WorldEd/commit/79c25675e105ba132e57ec0976a2f37c0d0d9075)  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

This large release diff expanded the InGameMap generator and property UI,
thumbnail controls, LotPack viewing, logging, preferences, progress reporting,
rendering, and the bundled dark theme. Generated build files in that historical
commit are not treated as separate features.

### Build 41 to Build 42 transition

#### 2025-01-18 - Native 256 output and original Biomemap Generator

**Repository:** `Unjammer/WorldEd`, branch `B42`  
**Commit:** [`e1438f55`](https://github.com/Unjammer/WorldEd/commit/e1438f5599ec01461c63b40333e9820bcbffeae4)  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

The commit adds `lotfilesmanager256`, `biomemapgeneratorDialog`, and the image
processor, alongside B42 cell, preference, scene, and generation changes. This
is the first exact public commit found for the Unjammer Biomemap Generator and
native 256 generation code.

#### 2025-01-18 - Build 42 InGameMap generation

**Repository:** `Unjammer/WorldEd`, branch `B42`  
**Commit:** [`c5edd1c3`](https://github.com/Unjammer/WorldEd/commit/c5edd1c3272bff78624bcc82dfc0cd2f6a1debeb)  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

The source changes extend InGameMap feature generation and image generation for
the B42 workflow.

#### 2025-01-18 - Standalone configuration, themes, extraction, and B42 editor work

**Repositories:** historical WorldEd and TileZed `B42` branches  
**Commits:** WorldEd `6de436e7`, TileZed `4f51b121`, `7e57847b`, `1bb30a45`, and
`de4cc6de`  
**Implementation:** Alree / Unjammer  
**Provenance:** `UNJAMMER_ORIGINAL`

The verified source and README describe removal of registry and user-profile
dependencies, a portable installation, theme selection and custom QSS support,
tile identification, advanced extraction, B42 data and TileDefs, BuildingEd B42
controls, Lua fixes, and complete configuration files.

### Build 42 development

#### 2026-07-22 - Selective Qt 5 reconstruction

**Repositories:** local `integration/qt5-basements` reconstruction branches  
**Baselines:** WorldEd `80e3511c`, TileZed `f9489a9b`  
**Implementation:** Alree / Unjammer, retaining Tim Baker's baseline ancestry  
**Provenance:** `INDEPENDENT_PARALLEL_IMPLEMENTATION`

The 21 WorldEd and 22 TileZed reconstruction commits restore portable settings,
Qt 5 and MSVC builds, external themes, Biomemap generation, exact channels,
road and InGameMap performance, tile IDs, advanced pack extraction, PNG import,
Lua mapping tools, clean deployment layout, per-project grid geometry,
thumbnails, and saved layouts. Tim's inherited baseline remains credited for
the original editor and basement foundations. The reconstructed Unjammer
features retain their separate history.

#### 2026-07-29 - Selective later Tim fixes

**Upstream commits:** WorldEd [`f492c5cd`](https://github.com/timbaker/pzworlded/commit/f492c5cde209e1ef907af5d84f93c89cea4b1927),
[`a7d5a77c`](https://github.com/timbaker/pzworlded/commit/a7d5a77ccacddef3909decda2da3167a2c4f03e1),
TileZed [`2e0d35d5`](https://github.com/timbaker/tiled/commit/2e0d35d5bf4286de1ce55820a0ea2c954b7924ff)  
**Implementation:** Tim Baker, selectively ported and adapted by Alree / Unjammer  
**Provenance:** `TIM_FIX_PORTED` and `TIM_UPSTREAM_PORT`

The LotPack z fix and MixedTilesetView first-row fix were genuine later Tim
fixes. The OpenGL 3.3 renderer was adapted to the maintained Qt 5 renderer and
core-profile behavior rather than copied as a wholesale renderer replacement.

### Combined PZ_Mapping_Tools era

#### 2026-07-24 onward - Combined public source

**Repository:** [`Unjammer/PZ_Mapping_Tools`](https://github.com/Unjammer/PZ_Mapping_Tools)  
**Initial commit:** [`2ae112db`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/2ae112db61f582c086dcb3f892e1271bae1ea174)  
**Implementation:** Alree / Unjammer with inherited and individually recorded
upstream components  
**Provenance:** `UNJAMMER_ORIGINAL`, except where explicitly classified otherwise

The combined tree adds and evolves the complete-catalogue tileset pipeline,
Native256 project handling, WorldGen and prefab editors, Biomemap editing,
Project Doctor, OSM project generation, Regions and Street editors, partial
chunk export, InGameMap overlays and annotations, enhanced BuildingEd
clipboard placement, basement previews, standalone application launching,
render diagnostics, repair workflows, and theme coverage.

## Feature histories

### Qt 5 and platform maintenance

The first public Unjammer forks already contained Qt deprecation fixes in 2023.
The 2025 standalone tools removed practical registry and user-profile coupling.
The 2026 reconstruction deliberately restored Qt 5.14.2 and MSVC compatibility
on top of later upstream baselines. Tim's later Docker, Linux, macOS, Qt 6, and
release-packaging commits were reviewed but were not imported into the Windows
Qt 5 release line.

### Theme system

The history has three distinct stages. The 2023 forks fixed dark-theme rendering.
The 2025 forks introduced a theme-management system and external QSS selection.
The current combined tree expands palette coverage, popup and combo styling,
application-drawn labels, theme persistence, and high-contrast icons. External
QSS or icon sources remain third-party components when their notices say so.
Their use does not transfer authorship of the integration system.

### Tile identification and tile workflows

Tile IDs, names, source sheets, and context export were already visible in the
2023 TileZed continuation. The 2026 reconstruction restored those behaviors in
both palettes and road controls. The current catalogue adds recursive source
resolution, 2x, 1x, and custom tags, transparent-tile diagnostics, and distinct
missing-source markers. Tim's later `tileset_index` tooltip is useful upstream
work but does not become the origin of Unjammer tile identification.

### Tiles Unpacker and extraction tools

Upstream supplied the original pack-viewing and extraction foundation. Unjammer
added advanced options by 2023 and reconstructed them in `8701cb95` in 2026.
Preserved C# `PZ_Mapper` source separately demonstrates pack-page parsing,
texture extraction, and tilesheet reconstruction from 2023-era work. The current
C++ extractor adds all-items presets, tileset subdirectories, multi-tile object
assembly, exclusion of assembled tiles, orphan-pixel correction, and lazy page
decoding. Current code is not copied from the C# archive.

### InGameMap and Roads

The original InGameMap model and writer are Tim Baker foundation work. Unjammer
added route-class generation, thresholds, road-menu restoration, and polygon
guards by March 2023. B42 work added full building extraction, larger map image
workflows, forest output, embedded RoomDef building outlines, read-only
worldmap overlays, and structured annotation editing. Tim's later Rosewood
BuildingDefs fix was reviewed against the maintained global overlap collection.

### Lua integration

The Lua engine is inherited. The 2023 TileZed continuation added or corrected
`map:noneTile()`, object-layer creation, and object insertion workflows. The
2026 reconstruction hardened the Lua mapping tools and restored scripts. Tim's
later macOS menu-retention fix remains a Tim fix and was selectively retained
where applicable.

### Thumbnail generation

WorldEd's original thumbnail infrastructure is upstream foundation. Unjammer's
2023 fork added regeneration for all selected cells and later corrected loading,
rendering, and large-size controls. The reconstruction commits `34bba293` and
`c5c5cb4c` restore explicit thumbnail controls and world-minimap thumbnails.

### Biomemap

The first exact public Unjammer Biomemap Generator commit is `e1438f55` from
2025-01-18. It processes a main image and `_veg` image and automatically splits
sources. The 2026 reconstruction restores the generator at `dc9307ee` and exact
Project Zomboid channels at `f7cd858d`. The combined tree adds B42.20 reference
values, Water handling, separate red and green painting modes, validation, and
current WorldEd integration. No Tim commit in the audited post-baseline range
was used as the origin of this feature.

### Build 42

Build 42 provenance is feature-specific. Tim Baker is credited for the basement
branch foundations and original negative-level model. Unjammer's January 2025
B42 forks contain the first verified Unjammer native 256 manager, Biomemap
generator, B42 configuration, TileDefs adaptation, themes, and application
behavior. The combined tree later adds animals-related mapping, WorldGen biome
and feature editing, static prefabs, procedural loot inspection, B42 paths,
texture-pack handling, and dedicated generation safeguards.

### Native 256 x 256 cells

The January 2025 `lotfilesmanager256` implementation proves Unjammer native
256 output work before the combined repository. The reconstruction then adds
per-project grid geometry and removes the legacy fixed conversion assumptions.
The current `Native256` mode maps one 256-square source cell to one output cell.
It is distinct from merely reading a 256 output format through a 300-square
editor model. Partial Chunks is a later Unjammer extension that selectively
exports 8 x 8-square chunks while keeping the source TMX at cell scale.

### WorldGen and current workflow extensions

The current WorldGen biome, feature, static-prefab, Regions, Street Names,
OSM, Project Doctor, hole repair, and world-map annotation interfaces are
Unjammer implementations built on inherited WorldEd data structures and public
Project Zomboid formats. Format compatibility does not transfer implementation
authorship.

## Tim Baker Upstream Review

The two post-baseline ranges contain 67 commits. Platform-only packaging commits
were inspected as part of the range audit and grouped where they have the same
decision. Every user-facing or correctness change is listed separately below.

| Tim Baker commit | Date | Subject | Equivalent already in Unjammer? | Action | Provenance |
|---|---|---|---|---|---|
| WorldEd [`d73466bf`](https://github.com/timbaker/pzworlded/commit/d73466bf66c02337fc0520400c24fda211f6aea6) | 2026-08-18 | West-wall trim for extended window shapes | TileZed already contained the corresponding correction | Ported to WorldEd | `TIM_FIX_PORTED` |
| TileZed [`9ef277c1`](https://github.com/timbaker/tiled/commit/9ef277c19e10e9798a2a041302615e35434ace15) | 2026-10-15 | Recent output paths in CreatePackDialog | No output-path history | Adapted with a bounded, case-insensitive recent-path list | `TIM_UPSTREAM_PORT` |
| WorldEd `54b21a88`, `93f4b2fd` | 2026-07-27 to 07-29 | Docker and macOS Qt 6 builds | Windows Qt 5 line differs | Not applicable | Tim upstream only |
| WorldEd [`f492c5cd`](https://github.com/timbaker/pzworlded/commit/f492c5cde209e1ef907af5d84f93c89cea4b1927) | 2026-07-29 | LotPackViewer invalid z | No | Ported | `TIM_FIX_PORTED` |
| WorldEd `d9d2cd69` | 2026-07-29 | `std::as_const` migration | Qt 5 line retains compatible APIs | Not applicable | Tim upstream only |
| WorldEd [`a7d5a77c`](https://github.com/timbaker/pzworlded/commit/a7d5a77ccacddef3909decda2da3167a2c4f03e1) | 2026-07-29 | OpenGL 3.3 renderer | Maintained renderer required a compatible integration | Ported with adaptation | `TIM_UPSTREAM_PORT` |
| WorldEd `99f451ea` | 2026-07-30 | Avoid blocking during load | Maintained tree has its own deterministic complete-catalogue loader | Not ported — already implemented | `NOT_PORTED_ALREADY_IMPLEMENTED` |
| WorldEd `8b69f5c6`, `4cf9208e` | 2026-07-30 | Release files and redistributable | Separate release pipeline | Not applicable | Tim upstream only |
| WorldEd `10cf8c79` | 2026-07-30 | macOS OpenGL | Windows Qt 5 target | Not applicable | Tim upstream only |
| WorldEd `1d04a1b7`, `22bcab48` | 2026-07-30 | Code removal and Windows link fix | Reconstruction has separate Qt 5 build files | Not applicable | Tim upstream only |
| WorldEd `fda80ae0`, `37c9646f`, `4e66a67a`, `33cc0f24`, `3297fe1c`, `ecf006b7`, `50b12634`, `752a5426`, `32b039a3` | 2026-07-31 to 08-02 | Linux, macOS, and license packaging | Separate Windows distribution | Not applicable | Tim upstream only |
| WorldEd `9ca7ae6a`, `72cc7f46`, `4ceb0d33`, `8a753f1d` | 2026-08-04 | LotPack search workflow | No maintained equivalent | Pending review | `NEEDS_FURTHER_VERIFICATION` |
| WorldEd `0a18dad2`, `0c6c3b55` | 2026-08-04 | Room-under-pointer levels | Maintained level-aware renderer differs | Partially ported | `TIM_FIX_PORTED` |
| WorldEd `2ebde832` | 2026-08-04 | Multi-tile choose dialog | Maintained selection tools are broader and independent | Not ported — already implemented | `NOT_PORTED_ALREADY_IMPLEMENTED` |
| WorldEd [`179a0145`](https://github.com/timbaker/pzworlded/commit/179a0145e46fa427a2817aad4d11c2cb9824723c) | 2026-08-06 | WorldScene lot-image order | Defect remained in ordering | Ported with adaptation | `TIM_FIX_PORTED` |
| WorldEd [`8d01befa`](https://github.com/timbaker/pzworlded/commit/8d01befa83df1afc50f08e9faad00dde4e42925c) | 2026-08-11 | Rosewood BuildingDefs export | Global lot-overlap collection already covers the case differently | Not ported — already fixed | `NOT_PORTED_ALREADY_FIXED` |
| TileZed `c2b64f90`, `d14beeb6`, `62dd6baa` | 2026-07-26 to 07-29 | Linux, Docker, and macOS Qt 6 builds | Windows Qt 5 line differs | Not applicable | Tim upstream only |
| TileZed [`2e0d35d5`](https://github.com/timbaker/tiled/commit/2e0d35d5bf4286de1ce55820a0ea2c954b7924ff) | 2026-07-29 | MixedTilesetView first row | No | Ported | `TIM_FIX_PORTED` |
| TileZed `58ff6356` | 2026-07-30 | Avoid blocking startup | Maintained complete-catalogue loader differs | Not ported — already implemented | `NOT_PORTED_ALREADY_IMPLEMENTED` |
| TileZed `7f910e8a`, `9611f8ce` | 2026-07-30 | Release files and redistributable | Separate release pipeline | Not applicable | Tim upstream only |
| TileZed `8c71064f` | 2026-07-30 | Closed-document grid-color exception | Maintained settings guards already avoid the stale document path | Not ported — already fixed | `NOT_PORTED_ALREADY_FIXED` |
| TileZed `31ef4245`, `93254f4d`, `2bdb094f`, `dca73265`, `14abadda`, `268acfac`, `37476bea`, `e83d9245`, `d50c582a`, `bdd5eab9`, `85dfcb55` | 2026-07-30 to 08-06 | macOS, Linux, licenses, and distribution scripts | Separate Windows distribution | Not applicable | Tim upstream only |
| TileZed `a8ba468d` | 2026-08-01 | Sync embedded and standalone BuildingEd | Maintained tree now intentionally launches standalone BuildingEd | Not ported — already implemented | `NOT_PORTED_ALREADY_IMPLEMENTED` |
| TileZed `8978ce1e` | 2026-08-01 | `std::as_const` migration | Qt 5 line retains compatible APIs | Not applicable | Tim upstream only |
| TileZed `5108244c` | 2026-08-02 | Lua console menus on macOS | Lua workflow already maintained, macOS detail was upstream | Partially ported | `TIM_FIX_PORTED` |
| TileZed [`abb9b75f`](https://github.com/timbaker/tiled/commit/abb9b75f3dd39ffa6dcb7bfea8038729af5d45ef) | 2026-08-03 | Remove multiple tilesets | No equivalent undoable dialog at review time | Ported with adaptation | `TIM_UPSTREAM_PORT` |
| TileZed `d9ed76ee`, `b4ad15fc`, `3c577009` | 2026-08-04 to 08-11 | Lua and mapping data updates | Maintained catalogue and B42 data contain additional entries | Partially ported | `TIM_UPSTREAM_PORT` |
| TileZed `c8a4e143` | 2026-08-04 | Tile name as `tileset_index` tooltip | Tile IDs and names existed since 2023 | Not ported — already implemented | `NOT_PORTED_ALREADY_IMPLEMENTED` |
| TileZed `29f85d5f` | 2026-08-05 | Qt 5.15 compilation | Current target is Qt 5.14.2 with separate compatibility | Not applicable | Tim upstream only |
| TileZed [`39ec9c97`](https://github.com/timbaker/tiled/commit/39ec9c977921aab5cea2e13b437e9fbb06d210db) | 2026-08-08 | Building category filters | Existing category UI was retained | Ported with adaptation | `TIM_UPSTREAM_PORT` |
| TileZed [`fc29849e`](https://github.com/timbaker/tiled/commit/fc29849e40878d3059d69bd31ffac21169184003) | 2026-08-18 | West-wall trim for expanded window-frame shapes | No | Ported | `TIM_FIX_PORTED` |
| TileZed [`cd7dc721`](https://github.com/timbaker/tiled/commit/cd7dc721bb4f80b97701bb14ef4f097e5ea9102a) | 2026-08-18 | Correct image-black mask coordinates and bounds | No | Ported | `TIM_FIX_PORTED` |
| TileZed `801e8c27` | 2026-08-18 | MapBuildings cosmetic cleanup | Maintained MapBuildings differs | Not ported | Tim upstream only |
| TileZed `3d2093b1` | 2026-08-19 | Check Buildings internal-name editing, Pause, and Stop | Separate maintained workflow | Not ported | Tim upstream only |
| TileZed `dc8c6422` | 2026-08-24 | Linux distribution script fix | Windows Qt 5 line differs | Not applicable | Tim upstream only |
| TileZed [`e2dd2d3a`](https://github.com/timbaker/tiled/commit/e2dd2d3afe633076f61b468fb2086d9e224d5406) | 2026-08-25 | BuildingEd opening-hang protection | Maintained startup and progress flow differs | Ported with adaptation | `TIM_FIX_PORTED` |
| TileZed `fbf2ee73`, `0548c0db` | 2026-08-25 | Tileset background color preference and dock refresh | Unrelated preference change | Not ported | Tim upstream only |
| TileZed [`f4c25c2a`](https://github.com/timbaker/tiled/commit/f4c25c2a119747b588981563987a1817b74dd7d5) | 2026-08-25 | BuildingEd opening-crash protection during map construction | Maintained renderer has additional deferred state | Ported with adaptation | `TIM_FIX_PORTED` |

**Not ported: equivalent functionality was already implemented in the Unjammer
continuation before the recorded upstream changes** where the table uses
`NOT_PORTED_ALREADY_IMPLEMENTED`.

**Not ported: this defect had already been independently corrected in the
maintained Unjammer tree** where the table uses `NOT_PORTED_ALREADY_FIXED`.

## Third-party components and integrations

- Tiled code by Thorbjørn Lindeijer and contributors remains part of the
  TileZed ancestry with its GPL notices intact.
- Qt, Lua, zlib, QuaZip, OpenSSL, QSingleApplication, and bundled runtime
  components retain their own notices and licenses.
- External QSS themes and visual assets remain separately attributed where
  their source is known. Alree / Unjammer authorship applies to the application
  theme-management and integration code, not automatically to every asset.
- OSM data and the Pz-RealLifeMap idea credit do not imply copied code. The
  native C++/Qt importer is an Unjammer implementation.

## Ideas, testing and community contributions

Community reports, screenshots, projects, and workflow suggestions are credited
as `IDEA_OR_FEEDBACK`, not software authorship, unless a traceable code
contribution proves otherwise. The current special-thanks list remains in
`AUTHORS.txt` and `README.md`. Fred 'Military Surplus' Cooper receives special
thanks. SadPeanut receives the specific OSM-source idea credit. Neither credit
replaces the implementation provenance recorded above.

## Items requiring further provenance verification

- The exact first private date of the non-Git `PZ_MapGen`, `PZ_MapMover`, and
  `PZ_Mapper` revisions remains `NEEDS_FURTHER_VERIFICATION`. Preserved source
  metadata establishes precursors but not an exact public release chronology.
- The pre-2023 private map-style experiments have independent dated discussion
  evidence, but no public source commit was found. They are therefore not used
  to predate the exact public fork commits.
- The upstream LotPack search series remains pending because it has not been
  integrated and no current equivalent was found.

## Audit scope

This pass reviewed 80 Alree / Unjammer commits across the preserved C# Git
histories, historical WorldEd and TileZed forks, reconstruction branches, and
combined repository. It also reviewed all 58 Tim Baker commits after the two
recorded reconstruction baselines through the 2026-08-11 upstream heads.

