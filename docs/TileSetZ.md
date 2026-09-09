# TileSetZ

Open **Tools > TileSetZ - Create and Edit Tiles** in TileZed to create a
tilesheet. To edit an existing tile, right-click it in the Tilesets palette
and choose **Edit in TileSetZ**. This opens its original PNG and selects the
tile. A readable 2x image takes priority over the 1x image.

## Create or open a sheet

**File > New Tilesheet** selects the tile size, column count and row count.
Standard Project Zomboid tiles measure 64 x 128 pixels at 1x, or 128 x 256
pixels at 2x. Eight columns is the default for new sheets.

**File > Open Tilesheet** opens a PNG and asks which tile size it uses.
Its dimensions must contain complete tiles. For an image of any other size,
use **Open Source Texture** in the generator instead. Sheets with custom tile
dimensions or spacing must first be cut into standard tiles.

The left panel lists tiles in ID order. Numbers appear below the images.
Selecting a tile opens it in the **Draw tiles** tab. Editing pixels does
not change the sheet's column count or tile IDs.

## Edit pixels

- **Pencil**, **Eraser**, **Eyedropper** and **Fill** work on the selected tile.
  Their shortcuts are B, E, I and F while working in the pixel editor.
- The color button opens a color picker with opacity. The swatches select
  common colors. Ctrl-click temporarily picks a pixel's color and opacity.
- Right-drag temporarily erases. Brush size ranges from 1 to 32 pixels.
- **Line**, **Rectangle** and **Ellipse** use L, R and O. Drag to preview
  a shape and release to commit it. **Filled shapes** fills rectangles and
  ellipses. Escape cancels the current stroke or shape.
- **Opacity** sets the pixel alpha from 0 to 255. The color picker retains
  the full RGBA value, including when sampling a translucent pixel.
- **Lock alpha** preserves opacity when painting and skips empty pixels.
  Erasing still removes pixels.
- The wheel zooms from 1x to 32x. **Fit tile** shows the whole tile and
  **100%** restores native pixels. The optional grid appears at 5x and above.
- **Pan** (H), the middle mouse button, or Space with a drag moves the view.
- The Edit menu flips a tile, clears it, or removes pixels whose alpha is
  below 5 out of 255. These operations can be undone.

Ctrl+Z and Ctrl+Y undo and redo changes in this editor. Each brush stroke is
one operation. Its history is separate from the map's history and is limited
to 200 operations and 256 MB. Reaching the memory limit clears older history
before adding the next operation.

## Generate tiles from a texture

Open the **Tilesheets, textures & masks** tab. Choose **Open Source Texture**,
or **Edit > Use Selected Tile as Source
Texture**. Select an output scale and a method, then **Generate Preview**.
The large preview switches between the generated sheet, assembly check,
source texture, original or custom mask, and current sheet. Zoom with the
wheel or percentage control, pan by dragging, or use **Fit image** and **100%**.
Resizing the window preserves a manually chosen zoom.

Select a **Tile ID** and **Fit tile**, or double-click a tile to inspect it
at a useful size. Double-clicking a tile in **Current tilesheet** opens it in
the drawing tab. **Isolate selected tile** displays only that tile at the
center of the preview. Tile IDs remain outside the drawing canvas.

| Method | Result |
|---|---|
| Precut texture into wall tiles | Automatically cuts the source at wall-face dimensions, then projects every piece into an individual isometric tile without resizing the source. |
| Rectangular sprite grid | Splits a sprite without scaling, preserving its pixels and alpha. Pieces run left to right, then top to bottom. Partial pieces are padded with transparency at the right and bottom. This method does not project a wall. |
| Fit inside one tile | Scales the entire image to fit without changing its proportions and aligns it to the bottom center. |
| Wall masks | Original wall, corner and trim masks with left and right projections. |
| Floor masks | Original floor mask with a floor diamond at the bottom of each tile. |
| Street curbs | Original curb blend masks using the floor projection. |
| Roof walls | Original roof-wall masks with left and right projections and trim. |
| Roofs | Original roof projections and the six masks for left and right top, middle and bottom sections. |
| Extended wall masks | Original TileSetZ custom wall, corner and trim layout. |
| Custom PNG mask | A supplied PNG mask filled with the source texture or a chosen wall, corner or floor projection. |

Predefined masks keep their original layout. **Trim Color** recolors the
wall trim. Custom masks use 2x coordinates, with dimensions divisible by
128 x 256, including when the output is 1x. **Magenta #ff0069** selects
exactly that opaque mask color. **Mask alpha** multiplies the texture's
opacity by the mask's opacity. Source transparency is retained.

After inspecting the preview, choose one of these actions:

- **Append to Current Sheet** adds tiles after all existing slots, preserving
  the sheet's width and existing IDs. Empty slots within a generated layout
  are retained.
- **Replace from Selected Tile** replaces consecutive slots, including their
  transparent pixels, after confirmation. Undo restores their previous pixels.
- **Use as New Sheet** creates an unsaved sheet with the generated layout.

Insertion requires the same tile scale as the current sheet. Changing the
generator settings clears the previous preview so it cannot be inserted by
mistake. Rectangular sprite-grid pieces still need manual arrangement.

## Precut a texture into consecutive wall tiles

1. Open a flat wall texture, mural or sign and select **Precut texture into
   wall tiles**. An already projected isometric sprite should not be projected again.
2. Choose the output scale and **Wall direction**.
   A west face rises to the right in the image. A north face falls to the right.
3. **Automatic cut** reports the number of source columns, rows and wall tiles.
   **Source texture** shows the cut grid, including incomplete edge pieces.
   No wall width or height needs to be entered.
4. Generate and inspect **Generated tilesheet**. Each piece has its own
   consecutive tile ID and can be edited or placed individually. Use
   **Assembly check** to inspect their joins in the original arrangement.
5. Append the tiles, replace consecutive tiles, or use them as a new sheet.
   To also export their arrangement, choose **Export Wall Assembly TMX**, then save
   the PNG sheet in the suggested configured Tiles folder. At 2x this is
   the `Tiles/2x` folder. Open the exported TMX in TileZed to use the assembled
   wall with the existing selection and copy/paste tools.

At 2x, the source is cut into **64 x 192 pixel wall faces**, each projected
into a **128 x 256 tile**. At 1x, faces are 32 x 96 and tiles are 64 x 128.
The number of columns and rows is rounded up to include every source pixel.
For example, a 130 x 200 source at 2x produces 3 columns and 2 rows, giving
**6 individual wall tiles**. With eight sheet columns, two remaining slots
are empty. Changing the sheet column count changes the PNG arrangement only.

Pixels retain their original color and alpha. There is no scaling, stretching,
repetition or directional shading. Partial right and bottom pieces keep their
source pixels at the same local coordinates and pad the remainder transparently.
IDs follow the source from left to right, then top to bottom, beginning at 0.

The optional assembly places pieces 64 x 32 pixels apart between adjacent
squares and 192 pixels apart between floors at 2x, with values halved at 1x.
The TMX assigns the bottom source row to `0_Walls` and successive rows above
it to `1_Walls` and higher floors, retaining the source tile IDs. Export uses
the generated preview, independently of subsequent drawing or insertion
into another sheet.

The PNG and TMX are saved as separate files. If saving the TMX fails after
the PNG was saved, the error message identifies that partial result.
The image must stay in the configured Tiles catalogue for normal tool use.
The export supplies artwork and placement, not collision properties or a
furniture definition. Assign gameplay properties in Tile Definitions.

## Save and use the result

**File > Save** and **Save As** write PNG files atomically and retain their
alpha channel. A file changed by another application triggers a confirmation
before it can be replaced. Closing or opening another sheet asks to save
unsaved edits. **Export Selected Tile** writes only the selected tile.

Save 2x sheets under the configured **Tiles/2x** tree, and 1x sheets under
**Tiles** or one of its 1x/custom subdirectories. Saving there refreshes
TileZed's catalogue. Files saved elsewhere remain normal PNG images and must
be placed in that tree to appear in the palette. If both scales exist, the
2x sheet is the one displayed by the tools.

Set gameplay properties and furniture definitions with the existing Tile
Definitions and BuildingEd tools. TileSetZ edits image pixels and does not
alter map selections, structural copy/paste or depth data.

Images are limited to 16,777,216 pixels and 16,384 pixels on either side.
