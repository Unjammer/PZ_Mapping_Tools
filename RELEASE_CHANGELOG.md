# PZ Mapping Tools - Build 20260825c

_Changes since Build 20260820b_

The exact public release baseline used for this changelog is
[`42.20B260820b`](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/42.20B260820b),
commit
[`d724a62e33f13659a1d9799943a8e3d03fe152c2`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/d724a62e33f13659a1d9799943a8e3d03fe152c2),
published on 2026-08-20 as **Build 20260820b**.

This changelog contains only user-visible work added after that tagged
release.

## Shared / All applications

- PZWorldEd, TileZed, and BuildingEd now collect process-wide warnings and
  errors in lower-right notification counters inspired by the Project Zomboid
  error indicator. Errors use red and warnings use orange. A new message
  slides the counter into view, retains the complete timestamped list, and
  then hides the counter without discarding its contents. Clicking a visible
  counter opens a live, filterable message list with copy controls, source and
  thread details, log-folder access, and a Clear action. Messages emitted by
  background workers and warning or critical message boxes are included
  without counting an immediately repeated log message twice.
- The current build identifier is centralized and displayed in the main
  window title of PZWorldEd, TileZed, and BuildingEd. The current identifier is
  **Build 20260825c**.
- The complete tileset catalogue now shares pixel storage between extracted
  tiles whose image data and tile metadata are exactly identical. Tileset
  names, ordering, IDs, offsets, transparent-tile handling, and complete TMX
  headers remain unchanged.
- The PZWorldEd and TileZed Maps browsers can now sort by **Name**, **Size**,
  or **Last modified** by clicking the column header. Repeated clicks reverse
  the order. Name sorting follows Windows case-insensitive behavior, file size
  is sorted numerically, timestamps use the actual file modification time,
  and directories remain grouped before files.

## PZWorldEd

- Empty Linked World entries are ignored without producing a warning. A
  configured non-empty Linked World path still reports a warning when it
  cannot be read.
- Native256 projects can attach a per-cell 256 x 256 RGBA chunk-data override
  from **Tools > Terrain & Environment**. Transparent pixels inherit generated
  metadata while opaque red-channel values from 0 through 31 replace it with
  combinations of Solid, North wall, West wall, Water, and Room. The editor
  explains the meaning of every flag, validates the selected file before LOT
  generation, and preserves the cell reference through PZW save and load,
  Move, Copy, Paste, Undo, and Redo. Legacy300 projects cannot use it.
- Generate Lots directory and file choosers select a newly chosen path
  immediately. The displayed history entry and the path used for LOT export
  no longer require a second selection to update.
- Cell Paste recalculates its preview only when the target cell changes and
  reuses precomputed source-cell offsets. Pointer movement inside the same
  cell no longer revisits every copied cell.
- Partial Chunks lassos redraw only changed chunks, and the optional
  room-under-pointer highlight redraws only the affected rooms. Render
  diagnostics are disabled by default, and rendered-tile counting runs only
  when diagnostics are explicitly enabled.
- The terrain image editor now opens, edits, and saves both PNG and BMP main
  and `_veg` image pairs. Save As offers both formats, the matching extension
  is retained for the pair, and each saved file is encoded in the format named
  by its extension. Existing BMP projects therefore remain BMP projects
  instead of being silently rewritten with PNG data.
- Zombie Heatmap editing now preserves PNG or BMP according to the configured
  file extension. Saved images retain their dimensions and mapping color
  values and reopen through the same WorldEd workflow.
- Saving an attached terrain image refreshes its loaded World view image so
  the edited result is visible without reopening the project.
- PZWorldEd no longer retains a CPU atlas for every loaded tilesheet. Raster
  rendering uses the extracted tile images directly. OpenGL atlases are built
  only for sheets that are actually uploaded and their temporary CPU copy is
  released after upload.
- Basement access previews are loaded only when their Basement object is
  selected. Opening a cell no longer loads every access source referenced by
  its objects, and WorldEd can close normally while the cell is opening.

## TileZed

- The tileset context menu opens the selected tile in the Project Zomboid
  TileDefs editor instead of editing arbitrary TMX tile metadata. TileZed uses
  the same ordered `.tiles` and `.tiles.txt` resolution as rendering, loads the
  file that defines the selected tileset, and selects the exact tile index.
  Missing definitions report the configured files and direct the user to the
  relevant Preferences list.
- The experimental OpenGL viewport is disabled. TileZed consistently uses the
  Qt raster viewport with minimal dirty-region updates, avoiding severe
  driver-dependent Brush Tool slowdowns.
- Edge, Fence, and Curb previews rebuild only when their effective tile,
  level, layer, orientation, selection, mode, or modifiers change. Their
  tileset resolution is cached locally, invalidated by real map, layer,
  configuration, or tileset changes, and reused for complete short or long
  previews instead of scanning every tileset for every preview tile.
- Partial Chunks lassos redraw only changed chunks. Normal tile edits use the
  affected layer's sprite margins, while BMP edits use only the generated BMP
  layers, so an unrelated Jumbo tile no longer expands every dirty region.
  Render diagnostics are disabled by default.
- The Layers visibility threshold is restored from the portable TileZed INI,
  clamped to the open map's layer count, and applied before rendering. The
  Layers and Levels visibility sliders stay synchronized while maps are
  opened or changed.
- The Layers list context menu now provides **Check All** and **Uncheck All**
  commands for changing every layer visibility checkbox in one action.

## BuildingEd

- Tile, furniture, mixed-tileset, and **Building > Tiles** browsers distinguish
  a valid transparent tile with the crossed-eye marker from an unresolved
  asset shown with the red missing-source marker.
- Moving a tile preview redraws only the affected tile area. Extended margins
  are reserved for Jumbo, Jumbo XL, and Jumbo XXL tiles instead of being
  applied to every tile under the pointer. Preview tile resolution no longer
  rebuilds the complete tileset lookup on each move.
- BuildingEd consistently uses the Qt raster viewport with minimal
  dirty-region updates. The experimental OpenGL option is disabled, and
  render diagnostics are off by default so normal rendering does not count
  every drawn tile.
- Structural edits now regenerate the automatic building layers only in the
  visible Object, Tile, or Attribute mode. Hidden isometric views synchronize
  once when their mode is opened, avoiding duplicate floor-wide work while
  placing or changing rooms, walls, windows, doors, stairs, roofs, and
  furniture.
- Closing a TBX document now releases its complete building model and removes
  its per-document view entries, preventing closed buildings from accumulating
  in memory during long editing sessions.
- Cut now respects its layer scope. **Current Layer**, **Visible Layers**, and
  **Specific Layers** remove only their selected explicit tile layers and keep
  the room layout intact. **All Layers** also removes the selected room layout,
  while Undo restores the complete operation.
- Opening a building ignores tileset notifications while its isometric map is
  only partially constructed. BuildingEd also processes required startup UI
  events while its main window is disabled, avoiding the upstream opening
  crash and startup hang paths.
- The unlit-room mask now tracks its actual map bounds and rejects coordinates
  outside them, preventing invalid mask access after composite bounds change.
- West-wall trim now recognizes all supported window-frame variants.

## Credits and special thanks

- Tim Baker for the original WorldEd and TileZed work that remains the
  upstream foundation of these tools.
- Alree / Unjammer for the unofficial Qt 5 continuation, current maintenance,
  new features, fixes, integrations, and releases.
- A very special thank you to Fred 'Military Surplus' Cooper.
- Petro, Pabbiqo [pq], Dane, ! Cacador, Kyber, shakaloblok, and the Project
  Zomboid mapping and modding community for reproducible reports, project
  files, screenshots, logs, and practical workflow feedback.

Legal authorship and third-party attribution are documented in `AUTHORS.txt`,
`FEATURE_PROVENANCE.md`, `UPSTREAM-HISTORY.md`, and the bundled license
notices.
