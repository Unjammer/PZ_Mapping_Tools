# Upstream History and Source Provenance

This document records where the PZTools Unofficial source trees came from,
which upstream revisions formed the initial integration baseline, and how later
upstream changes are tracked.

Feature-level authorship, independent implementations, later upstream ports,
and review decisions are recorded in
[`FEATURE_PROVENANCE.md`](FEATURE_PROVENANCE.md).

It exists because this repository combines two independent source histories
into one source tree. The combined repository is therefore not represented by
GitHub as a conventional fork of either upstream repository.

## Initial integration baselines

The current `WorldEd/` and `TileZed/` trees were reconstructed on local
`integration/qt5-basements` branches before being assembled into this combined
repository.

## Historical Unjammer continuation before the combined repository

The current combined repository is not the beginning of the unofficial
continuation. Public Unjammer WorldEd and TileZed forks preserve verifiable
feature work from the Build 41 era, followed by dedicated Build 42 branches.

| Historical tree | Earliest verified Unjammer commit | Date | Verified scope |
|---|---|---|---|
| `Unjammer/WorldEd` | [`fd4ed27dab993c4f8b85091f463fe6f8067eeccc`](https://github.com/Unjammer/WorldEd/commit/fd4ed27dab993c4f8b85091f463fe6f8067eeccc) | 2023-03-06 | Dark-theme corrections, InGameMap validation and road features, thumbnail workflow, preferences, and Qt maintenance |
| `Unjammer/TileZed` | [`b5fb778e54c5a36c32b29b8df9489cbcd295a20a`](https://github.com/Unjammer/TileZed/commit/b5fb778e54c5a36c32b29b8df9489cbcd295a20a) | 2023-03-06 | Theme and grid controls, tile IDs and names, export and unpacking, Lua mapping corrections, and BuildingEd extensions |
| `Unjammer/WorldEd`, branch `B42` | [`e1438f5599ec01461c63b40333e9820bcbffeae4`](https://github.com/Unjammer/WorldEd/commit/e1438f5599ec01461c63b40333e9820bcbffeae4) | 2025-01-18 | Native 256 generation, original Biomemap Generator, and Build 42 editor changes |
| `Unjammer/TileZed`, branch `B42` | [`7e57847b5b8e4994b67d7c130ab12eb99e11e3d4`](https://github.com/Unjammer/TileZed/commit/7e57847b5b8e4994b67d7c130ab12eb99e11e3d4) | 2025-01-18 | Portable Build 42 TileZed behavior, TileDefs, Lua, preferences, extraction, and tileset handling |

Preserved C# mapping utilities provide older evidence of Alree / Unjammer work
on chunks, compiled maps, TileDefs, texture packs, TMX/PZW generation, map
images, and building extraction beginning with verified Git commits in March
2022. Those archives are historical precursors, not dependencies or copied
source for the current C++ applications. Exact dates for archives without Git
history are intentionally not inferred from file metadata alone.

The historical forks, reconstruction histories, and current combined history
remain distinct evidence layers. A later equivalent Tim Baker implementation
does not retroactively replace an earlier independent Unjammer implementation.

| Local tree | Upstream repository | Source branch | Exact initial baseline | Upstream date | Subject |
|---|---|---|---|---|---|
| `WorldEd/` | [`timbaker/pzworlded`](https://github.com/timbaker/pzworlded) | `basements` | [`80e3511cae257f51250df035141243ba6b9cf7cc`](https://github.com/timbaker/pzworlded/commit/80e3511cae257f51250df035141243ba6b9cf7cc) | 2026-06-02 | Fixed "Read Objects from Lua" for `objects` tables |
| `TileZed/` | [`timbaker/tiled`](https://github.com/timbaker/tiled) | `basements` | [`f9489a9ba605f8dc503c205f19655644798b9ec4`](https://github.com/timbaker/tiled/commit/f9489a9ba605f8dc503c205f19655644798b9ec4) | 2026-07-15 | Added "Reassign Tileset IDs" |

BuildingEd is not a third imported repository. Its source is contained in the
`TileZed/` tree, principally under `src/tiled/BuildingEditor`, and therefore has
the same `timbaker/tiled` baseline.

## August 6, 2026 upstream review

The Tim Baker `basements` branches were reviewed at WorldEd
`179a0145` and TileZed `b4ad15fc`. Changes were ported selectively into the
maintained integration trees.

The WorldEd review contributed basement and above-ground building separation,
the true minimum-level room scan, recursive building-layer levels, and
basement-aware world-thumbnail ordering. The ordering was adapted to the
existing renderer and thumbnail cache rather than replacing either maintained
renderer.

The TileZed review contributed undoable multi-tileset removal, the Qt 5
follow-up correction, macOS Lua-console menu retention, and validated mapping
tool presets. Complete upstream catalogue and BuildingEd data files were not
adopted when they would remove maintained entries or introduce unresolved
tile references.

These revisions are not estimates. In the reconstruction repositories they are
the Git merge-bases between each local `integration/qt5-basements` branch and
its corresponding `upstream/basements` branch:

```text
WorldEd merge-base:
80e3511cae257f51250df035141243ba6b9cf7cc

TileZed / BuildingEd merge-base:
f9489a9ba605f8dc503c205f19655644798b9ec4
```

The first local reconstruction commit in each original working repository was:

| Tree | First local commit | Parent | Purpose |
|---|---|---|---|
| WorldEd | `11bf3e15f71ef81cff3ec8072b373efce9bda754` | `80e3511cae257f51250df035141243ba6b9cf7cc` | Remove obsolete Windows build scripts |
| TileZed / BuildingEd | `9bac0fdc9eb10f8bb1019b3e1d482577614a5318` | `f9489a9ba605f8dc503c205f19655644798b9ec4` | Restore Qt 5 and MSVC qmake compatibility |

Those reconstruction branches contained 21 WorldEd commits and 22
TileZed/BuildingEd commits before the additional uncommitted integration work
represented by the first public combined tree.

## Why the combined repository is not a GitHub fork

A GitHub repository belongs to one fork network. This project combines:

- `timbaker/pzworlded`, which provides WorldEd; and
- `timbaker/tiled`, which provides TileZed, BuildingEd, and the Project
  Zomboid-adapted Tiled code.

The combined repository could therefore not be a normal GitHub fork of both
projects simultaneously.

Preserving both complete histories was technically possible through subtree
imports, merges of unrelated histories, or a parent repository with
submodules. Each option had different consequences for layout, release
assembly, contribution workflow, and GitHub's presentation of the project.
None would have made the result a conventional fork of both upstreams.

The public source was assembled as a unified tree instead. Consequently:

- copyright and license notices remain in the imported source trees;
- the exact starting revisions are recorded above;
- unchanged pre-baseline code must be traced in its corresponding upstream
  repository;
- the combined repository's own history describes the integration and later
  work, not the full age of every inherited line.

## How to trace a line before the import

For a file under `WorldEd/`, inspect it at the WorldEd baseline:

```bash
git clone https://github.com/timbaker/pzworlded.git
git -C pzworlded log --follow \
  80e3511cae257f51250df035141243ba6b9cf7cc -- path/to/file
git -C pzworlded blame \
  80e3511cae257f51250df035141243ba6b9cf7cc -- path/to/file
```

For a file under `TileZed/`, including BuildingEd, use the TileZed baseline:

```bash
git clone https://github.com/timbaker/tiled.git
git -C tiled log --follow \
  f9489a9ba605f8dc503c205f19655644798b9ec4 -- path/to/file
git -C tiled blame \
  f9489a9ba605f8dc503c205f19655644798b9ec4 -- path/to/file
```

The `timbaker/tiled` history is also the appropriate place to investigate code
that predates the Project Zomboid-specific TileZed changes and originates from
the Tiled editor codebase.

## Later upstream review and selective ports

The baseline hashes above never change. A later upstream commit being reviewed
or ported does not move the initial baseline.

The upstream `basements` heads observed on 2026-07-30 were:

| Repository | Observed `basements` head |
|---|---|
| `timbaker/pzworlded` | [`4cf9208eb0f60286954679a959e5441af2a2a380`](https://github.com/timbaker/pzworlded/commit/4cf9208eb0f60286954679a959e5441af2a2a380) |
| `timbaker/tiled` | [`9611f8ceee9d7e522e87d5face3263184f4973e1`](https://github.com/timbaker/tiled/commit/9611f8ceee9d7e522e87d5face3263184f4973e1) |

Known post-baseline ports in the current PZTools tree are:

| Component | Upstream commit | Status in PZTools |
|---|---|---|
| WorldEd | [`f492c5cde209e1ef907af5d84f93c89cea4b1927`](https://github.com/timbaker/pzworlded/commit/f492c5cde209e1ef907af5d84f93c89cea4b1927) - LotPackViewer z-coordinate fix | Integrated |
| WorldEd | [`a7d5a77ccacddef3909decda2da3167a2c4f03e1`](https://github.com/timbaker/pzworlded/commit/a7d5a77ccacddef3909decda2da3167a2c4f03e1) - OpenGL 3.3 cell renderer | Integrated with Qt 5/core-profile corrections |
| TileZed / BuildingEd | [`2e0d35d5bf4286de1ce55820a0ea2c954b7924ff`](https://github.com/timbaker/tiled/commit/2e0d35d5bf4286de1ce55820a0ea2c954b7924ff) - MixedTilesetView first-row display fix | Integrated |
| BuildingEd | [`fc29849e40878d3059d69bd31ffac21169184003`](https://github.com/timbaker/tiled/commit/fc29849e40878d3059d69bd31ffac21169184003) - west-wall trim for expanded window frames | Integrated |
| TileZed / BuildingEd | [`cd7dc721bb4f80b97701bb14ef4f097e5ea9102a`](https://github.com/timbaker/tiled/commit/cd7dc721bb4f80b97701bb14ef4f097e5ea9102a) - bounded image-black mask coordinates | Integrated |
| TileZed / BuildingEd | [`e2dd2d3afe633076f61b468fb2086d9e224d5406`](https://github.com/timbaker/tiled/commit/e2dd2d3afe633076f61b468fb2086d9e224d5406) - startup event-processing hang protection | Integrated with the maintained progress workflow |
| BuildingEd | [`f4c25c2a119747b588981563987a1817b74dd7d5`](https://github.com/timbaker/tiled/commit/f4c25c2a119747b588981563987a1817b74dd7d5) - opening crash protection during isometric map construction | Integrated with the maintained deferred renderer |

The post-baseline review was refreshed on 2026-08-25 through these heads:

| Repository | Reviewed `basements` head | Post-baseline commits reviewed |
|---|---|---|
| `timbaker/pzworlded` | [`8d01befa83df1afc50f08e9faad00dde4e42925c`](https://github.com/timbaker/pzworlded/commit/8d01befa83df1afc50f08e9faad00dde4e42925c) | 29 |
| `timbaker/tiled` | [`f4c25c2a119747b588981563987a1817b74dd7d5`](https://github.com/timbaker/tiled/commit/f4c25c2a119747b588981563987a1817b74dd7d5) | 38 |

The complete feature and fix decisions, including ports, adaptations, changes
already independently present, and platform-only work, are maintained in the
`Tim Baker Upstream Review` table in
[`FEATURE_PROVENANCE.md`](FEATURE_PROVENANCE.md).

Platform-only Docker, Linux, macOS, or Qt 6 commits are not automatically
imported into the Windows Qt 5 tree. They are reviewed for relevant portable
changes. Likewise, broad upstream branch merges are not assumed to be safe:
applicable changes are ported and tested individually.

### Z3R0-BYT3Z review on 2026-08-24

The common ancestor between the reviewed `Z3R0-BYT3Z/PZ_Mapping_Tools` main
branch and this repository was
[`d724a62e33f13659a1d9799943a8e3d03fe152c2`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/d724a62e33f13659a1d9799943a8e3d03fe152c2).
The following narrowly applicable fixes by Zero were selectively integrated:

| Component | Source commit | Adaptation |
|---|---|---|
| TileZed and WorldEd build configuration | [`91e3d27dd1e3311680ee5cad6a2a1314cb8a674b`](https://github.com/Z3R0-BYT3Z/PZ_Mapping_Tools/commit/91e3d27dd1e3311680ee5cad6a2a1314cb8a674b) | MSVC release flags are limited to MSVC builds |
| BuildingEd template rooms | [`b297d2e679aadcff44bb9fe4e023278b248467d7`](https://github.com/Z3R0-BYT3Z/PZ_Mapping_Tools/commit/b297d2e679aadcff44bb9fe4e023278b248467d7) | Added the missing `QUndoGroup` declaration |
| Lua runtime libraries | [`f0e23f095e1a119a34a5cd35d2511403c0fc2288`](https://github.com/Z3R0-BYT3Z/PZ_Mapping_Tools/commit/f0e23f095e1a119a34a5cd35d2511403c0fc2288) | Removed the compiler entry point from both TileZed and WorldEd copies of the runtime project |

The remaining reviewed changes were interface redesigns, assets,
documentation, platform-specific configuration, or behavior that was already
implemented independently. They were not imported.

## Relationship to CE

This repository was not derived from CE. As of the provenance cutoff above, no
CE code, patch, asset, documentation, or Git history has been used as a source
for this PZTools iteration.

Similarities may result from the shared Tim Baker/TIS ancestry, compatibility
with the same Project Zomboid formats, or independent solutions to the same
mapping-tool problems. They do not by themselves establish a CE lineage.

CE is not listed as an upstream repository or dependency.

## Local and previously unreleased work

Some behavior was reconstructed from older private builds, experiments, and
local reference trees. Those materials were used as behavioral references, not
imported as one continuous public Git history.

The dates of the reconstruction commits indicate when features were rebuilt in
the maintained Qt 5 branches; they do not necessarily indicate when an idea or
earlier private implementation first existed. Where an older implementation
does not have a preserved public commit, this repository does not claim a more
precise historical date than can be verified.

## Provenance policy

When adding code from another project:

- preserve all applicable copyright and license notices;
- record the source repository and full commit hash;
- credit the original author;
- describe substantial adaptations;
- never describe a shared-upstream or independently developed change as having
  been imported from another community fork without evidence.

This file records source ancestry. Current release changes are documented in
[`RELEASE_CHANGELOG.md`](RELEASE_CHANGELOG.md).
