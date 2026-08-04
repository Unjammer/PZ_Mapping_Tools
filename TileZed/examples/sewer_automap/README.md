# Sewer Automapper example

Open `sewers.tmx` in TileZed, then show **View > Automapping**.

`automapping-rules.txt` loads seven rule maps in order. Press **Reload** after
editing the manifest or a rule map, then use **Apply** to run the complete
pipeline. Keep **Interactive** disabled until the full-map result is correct.

`rule_001.tmx` is the smallest starting point. It demonstrates:

- `Regions`, which separates connected examples;
- `Input_set`, which matches the target layer named `set`;
- `Output_Ground`, `Output_Over`, and `Output_Over2`;
- an `Output_Effect` Object Group;
- the `DeleteTiles` and `AutomappingRadius` map properties.

The following rule maps add transitions, walls, and corners. `rule_006.tmx`
introduces `InputNot_set`, and `rule_007.tmx` shows repeated positive and
negative layers.

Comment out later manifest lines and apply one stage at a time to see what
each rule contributes. The complete reference is in
`docs/TileZed/Automapper.html`.
