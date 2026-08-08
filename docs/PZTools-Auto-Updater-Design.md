# PZTools GitHub release updater design

This document defines a safe update architecture for the portable PZTools
suite. It covers WorldEd, TileZed, BuildingEd, their managed runtime files, and
a possible separate Tiles synchronization channel.

No updater archive is created by this design. Release archives remain a
maintainer release step.

## Recommended architecture

Use two components:

1. WorldEd, TileZed, and BuildingEd share a small update-check client.
2. A separate `PZToolsUpdater.exe` downloads, verifies, stages, installs, and
   rolls back updates after all three editors are closed.

The editors must not replace their own running executables. On Windows, loaded
executables and DLLs can be locked. Keeping installation work in a separate
helper also makes recovery possible when a new editor build does not start.

The initial user interface should provide:

- **Help > Check for Updates**
- an optional daily automatic check, enabled only with user consent
- stable and preview channels
- release notes, download size, installed version, and available version
- **Download and Install**, **Open Release Page**, and **Skip This Version**

No GitHub token is required for a public repository. Cache the response and use
HTTP `ETag` with `If-None-Match`. One daily request remains far below GitHub's
unauthenticated REST limit.

## Release discovery

Stable checks use:

`GET https://api.github.com/repos/Unjammer/PZ_Mapping_Tools/releases/latest`

Preview checks list published releases and select a release marked as a
prerelease. Draft releases are never eligible.

Requests use:

- `Accept: application/vnd.github+json`
- a fixed supported `X-GitHub-Api-Version`
- an explicit PZTools `User-Agent`
- HTTPS only

The updater accepts assets only from the configured repository and release.
It never accepts an arbitrary update URL supplied by release notes.

## Version and manifest

Do not infer ordering only from tag text such as `42.20B260804`. Every release
should include a small `pztools-update-v1.json` asset with an explicit numeric
build number.

Example:

```json
{
  "schema": 1,
  "channel": "stable",
  "suiteVersion": "42.20B260806",
  "buildNumber": 20260806,
  "minimumUpdaterVersion": 1,
  "sourceCommit": "full-commit-sha",
  "assets": [
    {
      "component": "tools",
      "platform": "windows",
      "architecture": "x86_64",
      "name": "PZTools-Qt5-Windows-x64.zip",
      "size": 0,
      "sha256": "release-asset-sha256"
    }
  ]
}
```

The tool archive should also contain an installation manifest listing every
managed file, its SHA-256, size, and update policy. This allows the updater to
distinguish an unmodified release file from a user-modified file.

## Verification requirements

Before extraction or installation:

1. Require a published release.
2. Prefer a GitHub immutable release.
3. Match the selected asset name, size, and SHA-256 against the GitHub API
   release-asset digest.
4. Verify a detached signature for the update manifest with a public key
   embedded in the updater.
5. Verify every extracted managed file against the installation manifest.
6. Reject absolute paths, parent traversal, device paths, links, and files
   outside the staging directory.
7. Never execute a script downloaded from the manifest.

GitHub immutable releases protect the tag and uploaded assets from later
replacement. A detached signature adds protection against an update manifest
published by an unauthorized repository session.

## Tool installation transaction

Downloads go to:

`<install-root>/updates/staging/<version>`

The updater then:

1. Verifies that WorldEd, TileZed, and BuildingEd are closed.
2. Checks that the installation directory is writable.
3. Creates a timestamped backup under
   `<install-root>/updates/backups/<version>-<timestamp>`.
4. Extracts and verifies the complete new release in staging.
5. Replaces managed files only after every validation succeeds.
6. Starts one lightweight deployed validator from the new release.
7. Records the installed version only after validation passes.
8. Restores the backup automatically if replacement or validation fails.

The updater must preserve:

- `settings/**`, including logs
- `brushes/**`
- project files, which should already live outside the installation
- unknown user files

Packaged `config`, `lua`, `plugins`, `themes`, `translations`, documentation,
licenses, and binaries are managed release content. Before replacing a managed
file, compare its current hash with the previous installation manifest.

- If unchanged, replace it normally.
- If modified by the user, back it up and retain it.
- Put the new version below `updates/incoming/<version>` and report the
  conflict.

For a cleaner long-term layout, custom Lua should move to a dedicated
`lua/user` directory while packaged Lua remains managed.

## Tiles are a separate component

Tools and Tiles must not share one archive or one version number. Use a
separate `pztiles-update-v1.json` with:

- game-data version
- 1x, 2x, and custom component identifiers
- relative PNG path
- logical tileset name
- file size and SHA-256
- package part containing the file
- whether the updater manages the file

GitHub release assets must each remain below 2 GiB. A complete authorized Tiles
release would therefore require numbered package parts. The update process can
download only the parts containing changed files.

The selected Tiles tree is user data. The updater must:

- stage all downloads outside the live Tiles directory
- preserve unknown custom PNG files
- back up locally modified managed PNG files
- delete only files recorded as managed by a previous Tiles manifest
- prefer a verified 2x PNG while preserving supported 1x-only sheets
- run the deployed complete-catalogue validator after installation
- restore the previous tree if validation fails

## Tiles distribution authorization

The PZTools source and binaries can be released under their applicable source
licenses and notices. Project Zomboid PNG assets are a different distribution
scope.

Until explicit permission exists to redistribute the Tiles through GitHub,
the updater should distribute only the tools, catalogues, and manifests. A
safe Tiles feature can instead:

1. Detect that a newer Tiles manifest exists.
2. Ask the user to select an authorized local Tiles archive or installation.
3. Verify its files against the published manifest.
4. Synchronize verified files into the configured Tiles directory.

Hosting actual PNG packages on GitHub should remain disabled until their
redistribution permission and source are documented.

## Rollout plan

### Phase 1

Add a read-only update checker and **Open Release Page** action. Record checks,
versions, and HTTP errors in the normal application log.

### Phase 2

Add the standalone updater, signed tool manifest, staged install, backup, and
rollback. Support Windows x64 first because it is the validated release target.

### Phase 3

Add conflict-aware updates for packaged configuration and Lua files. Introduce
the dedicated user Lua directory.

### Phase 4

Add Tiles manifest comparison and authorized local-source synchronization.
Enable GitHub-hosted Tiles packages only after redistribution approval.

## GitHub references

- Releases REST API:
  https://docs.github.com/en/rest/releases/releases
- Release assets REST API:
  https://docs.github.com/en/rest/releases/assets
- Immutable releases:
  https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases
- Release storage limits:
  https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases
- REST API rate limits:
  https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api
- Artifact attestations:
  https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations
