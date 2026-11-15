# PZ Mapping Tools - Build 20261115b

_Changes since [Build 20261114f](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B261114)._

## Changes

- Reopening Keyboard Shortcuts preserves pending edits. **Reload From Disk**
  asks how to handle pending changes.
- **Esc** checks for unsaved changes in WorldEd's World Map Annotations
  and Native256 Chunk Data Overrides editors.
- **Play Map Source** falls back to a walkable square when the authored
  SpawnPoint has no usable floor.
- **Play Map Source** loads the selected cells in World View or the open cell
  in Cell View. Select all cells to playtest the entire project.
- Documentation links resolve from both the source tree and the portable
  installation.

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
