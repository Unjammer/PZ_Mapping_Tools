# PZ Mapping Tools - Build 20261114f

_Changes since [Build 20261111d](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B261111)._

## Changes

- WorldEd, TileZed, and BuildingEd provide visible **Close** buttons and
  **Esc** in their auxiliary editors and viewers. Windows with pending
  edits offer **Save**, **Discard**, and **Cancel** before closing. In
  WorldEd, **File > Close** also works in Create Image Pyramid.
- WorldEd's **Play Map Source...** starts a Project Zomboid 42.22.2 solo
  debug session in the open or selected Native256 cell. All authored cells
  in the project are available in the same session. WorldEd streams the TMX
  and TBX data in memory, including at existing game map coordinates.
- With the Pencil in BuildingEd Iso view, right-click picks the room type
  under the pointer. Shift+right-click opens the context menu for pinning
  or releasing the room highlight.
- WorldEd supports level-0 NoFishZone and RiverZone rectangles. Export their
  shared Lua directly into a map mod or include it in the complete mod export.
  The Lua covers whole authored squares and includes the last no-fishing
  rectangle in the Build 42.22.2 fish-school lookup.

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
