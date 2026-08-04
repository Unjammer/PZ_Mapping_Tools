# Project Zomboid `.pack` comparator and extractor

TileZed includes two complementary read-only inspection tools for Project
Zomboid texture packs. They support the legacy version-0 stream and the
current Build 42 `PZPK` version-1 stream.

Neither tool modifies the source `.pack`.

## Evidence and safety boundaries

Version-0 and `PZPK` version-1 headers, page metadata, texture rectangles,
trim offsets, and canvases are parsed by the same maintained reader used by
the tools and covered by deterministic self-tests. Pixel comparisons operate
on decoded, reconstructed RGBA canvases rather than encoded PNG bytes.

Dimension, count, allocation, path, and overwrite checks are
**tool-enforced** safety limits. The comparator and extractor do not claim to
patch, merge, or rewrite a source pack.

## Advanced comparator

Open **Tools > Advanced .pack Comparator...**, choose the baseline and
candidate packs, then select **Compare**.

The result table distinguishes:

- textures added to or removed from the candidate;
- decoded pixel changes;
- metadata changes;
- combined pixel and metadata changes;
- duplicate texture names in either pack;
- unchanged textures.

Search and status filters can be combined, and every column is sortable. The
summary identifies each pack's format, page and texture counts, and complete
file SHA-256. Selecting a result displays both reconstructed texture canvases,
a difference image, atlas-page location, packed rectangle, trim offset, canvas
size, alpha flag, and full hashes.

The difference preview uses:

- green for pixels added by the candidate;
- red for pixels removed from the candidate;
- magenta for changed pixels;
- grey for unchanged pixels.

Use **Copy report** or **Export CSV...** to preserve a stable comparison
report. The export includes full hashes rather than the shortened values used
in the table.

### Hash meanings

The hashes deliberately answer different questions:

- **Pack SHA-256** hashes the complete input file. It changes for any byte-level
  difference, including PNG encoding or atlas layout.
- **Pixel SHA-256** hashes the reconstructed RGBA texture canvas, including its
  dimensions. Moving an unchanged trimmed sprite within an atlas does not
  create a false pixel change.
- **Metadata SHA-256** hashes the page name, page alpha flag, packed rectangle,
  trim offset, and reconstructed canvas dimensions.

This separation makes atlas repacking visible as metadata without reporting
unchanged sprite art as modified pixels.

## Versatile extractor

Open **Tools > .pack Viewer / Extractor...**, load a pack, then choose
**Extract...**.

Opening a large pack no longer leaves the Pack Viewer apparently frozen while
the extractor is being constructed. A cancellable progress window identifies
the current atlas page and texture while thumbnails and hashes are prepared.
The extraction itself also reports validation, texture/page/sheet output, and
manifest-writing phases.

The texture list includes a thumbnail, the parsed tileset and tile number,
source page, reconstructed size, packed rectangle, and pixel hash. Checkboxes
control the exact extraction set. The visible rows can be selected or cleared
in one action.

Available filters are:

- contains;
- starts with;
- wildcard;
- regular expression;
- exact tileset name.

Simple filters can contain multiple semicolon-separated patterns. Matching is
case-insensitive by default and can be made case-sensitive.

### Output modes

- **Individual reconstructed textures** writes each selected texture on its
  complete transparent canvas, applying its trim offset.
- **One reconstructed sheet per tileset** parses names such as
  `floors_exterior_natural_01_42` and places tile 42 in the corresponding
  sheet cell.
- **Complete matching atlas pages** writes every source page referenced by the
  selection.

Reconstructed sheets support automatic geometry derived from the texture
canvases, explicit 1x cells (64 x 128), explicit 2x cells (128 x 256), or
custom cell dimensions. The sheet column count is configurable from 1 to 64.
Entries whose names cannot be parsed as a tileset plus numeric tile index are
reported and skipped only in sheet mode.

Outputs can be flat, grouped by source page, or grouped by tileset. Existing
files can be handled by safe renaming (the default), skipping, or explicit
overwrite. PNG files are committed atomically, and unsafe or Windows-reserved
filenames are normalized.

The optional JSON manifest records the source pack path and SHA-256, pack
version, selected texture metadata, pixel and metadata hashes, and written
outputs. It is useful for reproducible extraction and later regression checks.

## Validation and limitations

The reader validates signatures, versions, counts, string lengths, encoded PNG
lengths, PNG decoding, packed rectangles, trim offsets, and reconstructed
canvas bounds before exposing data to either UI. PNG pages and reconstructed
sheets are subject to explicit dimension/allocation limits. Malformed or
truncated input is rejected with an explicit error. New packs and extracted
PNG/JSON files are committed atomically.

The comparator currently compares two packs at a time and exports CSV. It does
not create patches or merge packs. The extractor reconstructs PNG assets; use
TileZed's existing pack creator when a new `.pack` must be assembled.

Maintainer regression commands:

```text
TileZed.exe --validate-pack-tools
TileZed.exe --render-pack-comparator <output.png>
TileZed.exe --render-pack-extractor <output.png>
```
