# PZ Mapping Tools - Build 20260903

_Changes since Build 20260827_

The exact public release baseline used for this changelog is
[`PETRO`](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/PETRO),
commit
[`ab312d7bfc90d17f2e2c4feaa2052c9efb18dc0a`](https://github.com/Unjammer/PZ_Mapping_Tools/commit/ab312d7bfc90d17f2e2c4feaa2052c9efb18dc0a),
published on 2026-08-27 as **Build 20260827**.

This changelog contains only user-visible work added after that tagged
release.

## Included applications

- The Windows release now includes one complete, coherent, app-local
  Microsoft Visual C++ x64 runtime that is newer than the compiler used for
  the applications. Only the three DLLs imported by the applications, Qt,
  and their plugins are included. A separate Visual C++ Redistributable
  installation is no longer required. The deployment process also prevents
  the older runtime associated with the Qt 5 kit from replacing the required
  compiler runtime.
- Preferences now includes a Notifications page with a per-application
  option to show only errors in the application message indicator. Enabling
  it immediately removes retained warning notifications and suppresses new
  ones. Warnings remain in the log files, and warning dialogs that require a
  response remain visible.

## PZWorldEd

- Generate Lots now checks every source cell required by the requested
  export before loading its TMX files. When a missing TMX reference is the
  only content of an otherwise empty cell, WorldEd offers to create a dated
  PZW snapshot and remove that stale reference in one undoable action.
  References attached to cells containing lots, zones, properties, or other
  project data are reported but never removed automatically.
- The WorldEd zone catalogue now includes the Build 42.20.3 `SpawnOrigin`
  type with a distinct display color. Animal and Basement zones also have
  explicit colors. SpawnOrigin export accepts only non-empty rectangles at
  non-negative whole map-square coordinates and adds no unused properties.
- The Regions editor and WorldEd object catalogue now expose all four
  Build 42.20.3 utility-zone types: `NoGas`, `NoPower`, `NoWater`, and
  `NoPowerOrWater`. These zones require no custom properties and use distinct
  editor display colors.
- Every built-in WorldEd zone type now displays a concise Build 42.20.3
  runtime-purpose tooltip in zone-selection and inspection workflows.
- The guided Mannequin property editor now exposes all six properties read by
  Build 42.20.3. The obsolete `medium` AnimalType choice was removed while
  existing custom values remain loadable.
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
