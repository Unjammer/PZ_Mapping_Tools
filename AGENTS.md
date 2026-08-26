# PZ Mapping Tools source rules

## Repository provenance

Tim Baker created the original Project Zomboid WorldEd and TileZed foundation.
BuildingEd is historically part of the TileZed source tree. Alree / Unjammer
maintains and develops the unofficial Qt 5 continuation, its current features,
fixes, compatibility work, integrations, and combined releases, except where a
specific change has another recorded provenance.

Before changing authorship, credits, feature provenance, or statements about
who introduced a feature, read `FEATURE_PROVENANCE.md` and
`UPSTREAM-HISTORY.md`.

Do not erase earlier provenance merely because a later repository contains an
equivalent feature or fix. Compare dates and source. Preserve independent
parallel implementations and distinguish ideas or testing from implementation
authorship.

When porting a new Tim Baker upstream commit, update the `Tim Baker Upstream
Review` section of `FEATURE_PROVENANCE.md` and explicitly record whether the
commit was ported, adapted, already implemented, already fixed, not applicable,
or remains pending review.

Preserve copyright, license, third-party, generated-file, and public API
documentation notices. Do not add project-authored implementation comments.
Keep shipped documentation limited to implemented behavior and never include
private workflow material, internal paths, test narratives, or planned
features.

## Build identifiers

Build identifiers use the local calendar date in `YYYYMMDD` form. The first
designated build of a day has no suffix. Further designated builds on the same
day use `b`, `c`, `d`, and so on. Check the shared source identifier, tags, and
published releases before selecting the next identifier. The identifier is
defined only in `shared/pztoolsbuild.h` and is not derived at runtime.
