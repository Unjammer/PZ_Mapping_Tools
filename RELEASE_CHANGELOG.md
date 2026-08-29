# PZ Mapping Tools - Build 20260828

_Changes since Build 20260827_

The exact public release baseline used for this changelog is
[`PETRO`](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/PETRO),
commit
[`ab312d7bfc90d17f2e2c4feaa2052c9efb18dc0a`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/ab312d7bfc90d17f2e2c4feaa2052c9efb18dc0a),
published on 2026-08-27 as **Build 20260827**.

This changelog contains only user-visible work added after that tagged
release.

## PZWorldEd

- Project thumbnails now have explicit document ownership. Closing a project
  releases its thumbnail images when no other open document uses them.
- Closing the final project immediately purges every loaded TMX map with zero
  references. The complete global tileset catalogue stays resident for
  deterministic compatibility with legacy maps and complete tileset headers.
- Thumbnail render failures now complete their asynchronous lifecycle, so an
  unused failed image can be released instead of remaining pending.

## BuildingEd

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
