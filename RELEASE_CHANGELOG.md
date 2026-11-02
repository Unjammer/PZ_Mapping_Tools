# PZ Mapping Tools - Build 20261030

_Changes since [Build 20261024](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B261024),
published on 2026-10-24._

## Changes

- The TileDef guide documents Build 42.22 ladder directions, upper landings,
  material sounds, door/window forcing and moveable weights with CustomItem.

- BuildingEd adds View > Show Walls, with Ctrl+Shift+W, to hide walls and their
  attached details temporarily while placing furniture in isometric views.
  Existing per-floor layer visibility is retained.
- BuildingEd can pin a room highlight through its right-click menu or
  Ctrl+Shift+H. The highlight remains fixed while the pointer moves and is
  shared between Iso, Tiles and Attributes modes of the same document.
  Right-click object dragging remains available. Delete Object is in the menu.
- Window-frame selection loads pending TileDefs before reading WindowShape,
  preventing a cold lookup from falling back to the standard frame.

- LOT source import can optionally recalculate ground-level navigation using
  neighboring room headers and base-game/mod TileDefs. It preserves manual
  corrections and partial coverage, saves navigation references for subsequent
  source-only imports, and supports a command-line check without output writes.
- Generate Lots and source navigation rebuilding share stair, water, tree and
  hoppable collision rules, including explicit north/west collision properties.
  Import uses a progress window while the application remains responsive.

- Tile definition discovery supports the Build 42.22 `media/tile_definitions`
  directory and the older `media` layout. Missing binary definitions can use
  their `.tiles.txt` companions. Generate Lots, Loot and Create .pack read
  text definitions, and text property patches merge with their base files.
  Automatic discovery loads each binary/text pair only once.
- The existing Ladder direction control writes `ladderN`, `ladderS`,
  `ladderE` and `ladderW`, matching Build 42.22 climbing properties.
- Importing LOT `.source` files preserves leading and trailing spaces in room
  names, including files saved with Windows line endings.
- LOT source directory conversion validates all cells before replacing outputs,
  reports missing companion files and text error locations, and preserves
  identical output files. `--check-lot-sources=<directory>` checks source text
  without replacing binary files.
- TMX saving omits the cached `invisible` tile property while preserving other
  authored tile properties and the loaded tileset declarations.
- Check Buildings safely refreshes selected results after a building changes
  and updates the Fix Selected button for the refreshed selection.
- Check Buildings applies selected Kids Bedroom and Rearrange Grid fixes in
  one read and write per TBX. Parent and child selections are not processed
  twice, and rooms already converted to Kids Bedroom are left unchanged.

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
