# PZ Mapping Tools - Build 20261111d

_Changes since [Build 20261030](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B261030),
published on 2026-11-02._

## Changes

- The editors resolve duplicate vanilla 2x sheets in game pack order.
  `fixtures_overlay_counters_01` now uses the complete sheet from `Tiles2x.pack`
  so the counter overlays retain their original sprite IDs and artwork.
- The editors index the configured Tiles tree once per refresh. Catalogue
  discovery and WorldEd's `Tilesets.txt` update use the same image resolution
  as map loading, including nested 2x and 1x sheets.
- WorldEd's BiomeMap reference matches Build 42.22.2. Generation accepts the
  complete editor palette and rasterizes the supported named foraging zones.
  Unmapped source colors and legacy grass without a biome override use
  neutral red 64. Automatic green 171 requires a matching zone entry.
- WorldEd validates the BiomeMap's green-channel foraging-zone IDs on the
  world-aligned 8 x 8 chunk grid and preserves each ID when a chunk contains
  several. In vanilla `biomemap_42_38.png`, pixel `(0, 0)` is
  `RGB(115, 64, 64)`. Red 115 selects the `townhouse` biome, green 64 selects
  `ForagingNav`, and the game ignores blue. Boundary exports retain valid
  base pixels outside the project. The brush reads and writes the native
  tiles alongside the configured BiomeMap image.
- Vanilla TileDef paths follow the selected game installation. The editors
  share an ordered list of additional TileDefs in TileZed Preferences.
- The editors and exporters apply each TileDef patch to sprites already
  loaded at that point. A later regular definition replaces only the sprite
  records it supplies. Generate Lots and LOT source navigation use the same
  resolution as building properties and texture pack creation.
- Rebuilding navigation from LOT sources keeps collision on the original
  squares across chunk boundaries.
- Recreating a texture pack clears previous floor textures when the current
  TileDefs classify every sprite as a regular texture.
- TileZed preserves sprite alignment in legacy isometric TMX rendering.
  Windows and their supporting wall openings render together after a
  BuildingEd TMX export.
- The optional Build 42.22.2 window support TileDef patch supplies missing
  cutaway and frame properties for selected vanilla wall sprites.
- WorldEd and TileZed restore the original masking of TMX tiles beneath TBX
  buildings in previews and exports. Floorless walls, wall finishes, doors,
  windows and frames retain vegetation. The initial `Floor*` layers, including
  `FloorOverlay`, stay visible until a TBX floor covers them. `AboveLot` tiles
  draw above the building.
- TileZed and BuildingEd can display and search user-defined room translations
  in the Procedural Loot Viewer / Editor. The item column displays game
  translations for the selected interface language and retains the original
  item IDs for editing and export.
- The Procedural Loot Viewer / Editor interface is available in Simplified
  Chinese and French. BuildingEd Preferences includes the shared interface
  language setting.

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
