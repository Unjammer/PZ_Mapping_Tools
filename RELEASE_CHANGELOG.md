# PZ Mapping Tools - Source changes since Build 20261119d

These changes apply to the source tree after the [Build 20261119d Windows release](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B261119d).

- The README opens with the WorldEd map of Muldraugh. Editing and export
  procedures, including the LOT source commands, are in the
  [feature reference](docs/Feature-Reference.md).
- The editor sources no longer include development fixtures, test runners
  or command-line diagnostics. File validation during editing, import and
  export remains part of the normal workflows.
- The [Windows build guide](BUILDING.md) covers compiling the three editors
  from a checkout and assembling the portable data. WorldEd embeds its
  Play Map Source agent. The optional window-support patch and its editable
  text companion are available in the source tree.
- [Forks and redistribution](UPSTREAM-HISTORY.md#forks-and-redistribution)
  identifies the applicable notices, attribution records and corresponding
  source obligations.

Startup and document opening took 42% less time in TileZed, 45% less in
BuildingEd and 40% less in WorldEd in the comparison of Builds 20261118b
and 20261119. The hardware, workloads and timings are in the
[published performance notes](https://github.com/Unjammer/PZ_Mapping_Tools/releases/tag/43.00B261119d).

## Credits and special thanks

- Tim Baker for the original WorldEd and TileZed work that remains the
  upstream foundation of these tools.
- A very special thank you to Fred 'Military Surplus' Cooper.
- Petro, Pabbiqo [pq], Dane, ! Cacador, Kyber, shakaloblok, shisan-233, and
  the Project Zomboid mapping and modding community for reproducible reports,
  project files, screenshots, logs, and practical workflow feedback.

Legal authorship and third-party attribution are documented in `AUTHORS.txt`,
`FEATURE_PROVENANCE.md`, `UPSTREAM-HISTORY.md`, and the bundled license
notices.
