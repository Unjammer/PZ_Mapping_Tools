# Upstream History and Source Provenance

This document records where the PZTools Unofficial source trees came from,
which upstream revisions formed the initial integration baseline, and how later
upstream changes are tracked.

It exists because this repository combines two independent source histories
into one source tree. The combined repository is therefore not represented by
GitHub as a conventional fork of either upstream repository.

## Initial integration baselines

The current `WorldEd/` and `TileZed/` trees were reconstructed on local
`integration/qt5-basements` branches before being assembled into this combined
repository.

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

Platform-only Docker, Linux, macOS, or Qt 6 commits are not automatically
imported into the Windows Qt 5 tree. They are reviewed for relevant portable
changes. Likewise, broad upstream branch merges are not assumed to be safe:
applicable changes are ported and tested individually.

Future upstream ports should be added to this table with:

1. the original repository and full commit hash;
2. the original author where it is not evident from the linked commit;
3. whether the change was applied directly or adapted;
4. any material deviation required by this repository.

## Relationship to CE

This repository was not derived from CE. As of the provenance cutoff above, no
CE code, patch, asset, documentation, or Git history has been used as a source
for this PZTools iteration.

Similarities may result from the shared Tim Baker/TIS ancestry, compatibility
with the same Project Zomboid formats, or independent solutions to the same
mapping-tool problems. They do not by themselves establish a CE lineage.

CE is not listed as an upstream repository or dependency. If a CE-originated
change is intentionally incorporated in the future, it must be recorded
separately with its source commit and attribution.

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

This file records source ancestry. Functional differences and release-level
changes remain documented in [`CHANGELOG-PZTOOLS.md`](CHANGELOG-PZTOOLS.md) and
[`RELEASE_CHANGELOG.md`](RELEASE_CHANGELOG.md).
