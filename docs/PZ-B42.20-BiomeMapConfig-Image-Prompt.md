# Final ImageGen prompt

Mode: built-in ImageGen

```text
Use case: infographic-diagram
Asset type: comprehensive Discord announcement infographic for a game mapping tool
Primary request: Create a polished, highly readable landscape technical infographic titled exactly "BUILD 42.20 BIOMEMAP CONFIG" explaining how Project Zomboid Biomemap pixel IDs work and showing the complete verified ID reference.
Scene/backdrop: clean dark charcoal technical dashboard background with subtle topographic grid texture, no screenshots and no decorative clutter
Style/medium: crisp vector-like infographic, professional mapping-tool documentation, strong typography, precise tables and simple channel diagrams
Composition/framing: wide 16:9 landscape poster. Top section contains a simple 256 x 256 pixel-map diagram flowing into three channel cards. Middle and lower sections contain a two-column compact reference table. A clearly separated warning callout covers ID 171. Keep generous margins and readable text.
Color palette: charcoal and slate background, white text, red accent for BIOME + ORE, green accent for ZONE, muted blue accent only for the ignored channel, amber warning for ID 171
Text (verbatim):
"BUILD 42.20 BIOMEMAP CONFIG"
"ONE PIXEL = ONE MAP SQUARE"
"Each biomemap_X_Y.png is 256 x 256"
"RED CHANNEL"
"Biome + Ore Selector"
"Read per square by WorldGen"
"GREEN CHANNEL"
"Foraging Zone"
"Keep one ID per 8 x 8 chunk"
"BLUE CHANNEL"
"Not read by the BiomeMap raster"
"PIXEL  BIOME  ORE SELECTOR  ZONE"
"0  none  none  Water"
"59  clay_shore  none  Forest"
"64  none  none  ForagingNav"
"79  clay_lake  none  Forest"
"96  $random  none  DeepForest"
"102  townhouse  none  TrailerPark"
"115  townhouse  none  TownZone"
"128  farmmix_forest  none  Farm"
"141  farmmix_forest  none  FarmLand"
"153  ph_forest  none  PHForest"
"179  pr_forest  map_forest  PRForest"
"192  farmmix_forest  map_forest  FarmMixForest"
"204  farm_forest  none  FarmForest"
"217  birch_forest  map_forest  BirchForest"
"230  birchmix_forest  map_forest  BirchMixForest"
"243  organic_forest  map_forest  OrganicForest"
"254  dirt  dirt  ForagingNav"
"255  primary_forest  map_deep_forest  DeepForest"
"ID 171: MAP OVERRIDE ONLY"
"vegitation | none | Vegitation"
"Commented out in Vanilla. Requires WorldGenOverride.lua."
"WHAT map_forest SELECTS"
"Surface deposits: BOULDERS • LIMESTONE • FLINT"
"Density from procedural ore noise:"
"NONE → VERY LOW → LOW → MEDIUM → HIGH → VERY HIGH"
"NOT IRON OR COPPER"
"Iron and copper use separate vein generation."
Constraints: render all supplied text exactly once with no spelling changes, no missing rows, no invented pixel IDs, no extra prose, no logos, no trademarks, no watermark. Make every row readable at Discord image-preview size. Use consistent monospaced typography inside the table. Do not use an em dash or semicolon anywhere.
Avoid: photorealism, game characters, logos, fake UI controls, tiny illegible captions, duplicated rows, decorative icons that reduce table space
```

## Clarification edit prompt

```text
Use case: precise-object-edit
Asset type: corrected Discord technical infographic
Input image: Image 1 is the edit target. Preserve its dark blueprint style, title, map diagram, RGB channel structure, two full data tables, exact table rows, ID 171 warning, colors, borders, typography, dimensions, and overall visual quality.
Primary request: clarify exactly what the `ore` field and `map_forest` mean in Project Zomboid Build 42.20.
Make only these targeted text edits:
1. In the RED CHANNEL card, change the red subtitle to exactly: "Biome + Ore Selector"
2. In the same card, replace the small explanatory copy with exactly these three lines:
"ore = secondary WorldGen biome filter"
"map_forest = surface rock selector"
"Read per square by WorldGen"
3. In both table headers, replace "ORE MAP" with exactly: "ORE SELECTOR"
4. Preserve every table row and value exactly as currently shown.
5. Keep the left half of the bottom orange panel with the ID 171 warning unchanged.
6. Replace only the right half of the bottom orange panel with a clear definition block using this exact text:
"WHAT map_forest SELECTS"
"Surface deposits: BOULDERS • LIMESTONE • FLINT"
"Density from procedural ore noise:"
"NONE → VERY LOW → LOW → MEDIUM → HIGH → VERY HIGH"
"NOT IRON OR COPPER"
"Iron and copper use separate vein generation."
Constraints: make the new definition block readable at Discord image scale. Use orange title and white body text consistent with the existing style. Do not invent or omit data. Do not change `map_deep_forest`, `dirt`, biome names, zones, pixel IDs, or any table value. No watermark, logo, em dash, or semicolon.
```
