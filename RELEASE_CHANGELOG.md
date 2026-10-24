# PZ Mapping Tools - Build 20261024

_Changes since [Build 20260930b](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B260930),
published on 2026-10-01._

## Changes

- WorldEd preserves west-wall trim for the 19 extended window shapes when
  importing buildings and exporting lots.
- Overwrite SpawnMap preserves zero zombie intensity in excluded Partial
  Chunks. An unreadable chunk selection leaves the affected lotheader unchanged.
- TileZed's Create .pack dialog remembers up to 20 output paths. Its editable
  history supports reusing an earlier path, and Apply saves the current path
  without creating a pack.
- Additional building materials and furniture contributed by Tanimal extend
  the catalogues while retaining existing definitions, extended window frames
  and ceilings.
- BuildingEd and WorldEd ignore attempts to clear furniture tiles outside the
  current furniture dimensions. Furniture definitions report their empty state
  from their actual orientation tiles.
- WorldEd can switch between InGameMap feature XML files without losing the
  current features when a selected XML cannot be read. A failed import reports
  its XML error and keeps the previous file selected.
  Feature coordinates are converted between 256-square XML cells and
  300-square projects when their grids differ.
- BuildingEd can remove one tilesheet from the open building through Building >
  Remove Tilesheet from Building. The undoable command clears matching room
  materials, object and furniture tiles, painted tiles, and saved used tile
  references. Other tiles in mixed definitions remain available. Shared tile
  and furniture catalogues are unchanged.
- BuildingEd keeps each floor's layer visibility when switching floors. A
  previously saved global visibility cutoff no longer disables `RoofTop` or
  other layers on the newly selected floor.
- LOT export removes uppercase or lowercase `.png` suffixes from tilesheet
  filenames when writing sprite names.
- Generate Lots can export editable LOT, lotpack and chunkdata `.source` text
  files alongside the binary output. The same dialog imports complete source
  triplets and rebuilds matching binary files after overwrite confirmation.
  Command-line conversion is also available for a lots directory.
- WorldEd's vehicle preview applies FBX node scaling. Horsebox and livestock
  trailers now render at their intended size, and existing vehicle atlases
  are rebuilt for the corrected geometry.
- WorldEd accepts indexed PNG tilesheets when creating a tile atlas. WorldEd,
  TileZed and BuildingEd skip invalid roof-slope tile indices. WorldEd also
  uses the corrected default cap directions for 30-degree peaked roofs.

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
