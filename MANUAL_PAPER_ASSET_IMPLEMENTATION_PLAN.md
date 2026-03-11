## Goal

Convert the two remaining hand-maintained paper assets from implicit manual debt into explicit source-managed assets without weakening release policy.

The affected assets are:

- `paper/figs/system-arch.pdf`
- `paper/figs/mermaid.png`

## Source-Managed Layout

Hand-maintained paper assets should live under:

- `paper/assets/src/system-arch/`
- `paper/assets/src/mermaid/`

Each asset gets a per-asset metadata manifest:

- `paper/assets/src/system-arch/asset.json`
- `paper/assets/src/mermaid/asset.json`

The source-managed directory is the canonical place for:

- author-maintained source files
- exported asset-ready files in the paper-facing format
- per-asset sync metadata

## Acceptable Source Formats

The repository should accept common authoring formats, but availability must remain conservative.

### `system-arch`

Acceptable source formats:

- `system-arch.pdf`
- `system-arch.svg`
- `system-arch.drawio`
- `system-arch.png`

Direct repository-native sync is only guaranteed when:

- `system-arch.pdf` exists under `paper/assets/src/system-arch/`

### `mermaid`

Acceptable source formats:

- `mermaid.png`
- `mermaid.svg`
- `mermaid.mmd`
- `mermaid.drawio`

Direct repository-native sync is only guaranteed when:

- `mermaid.png` exists under `paper/assets/src/mermaid/`

This keeps the helper lightweight and reproducible without inventing export tooling that is not already tracked in the repository.

## Status Transitions

`paper/assets/asset_status.json` should become a generated status snapshot derived from the source-managed manifests plus the current filesystem state.

Expected transitions:

1. `blocked` + `source_missing`
   - source slot exists in `paper/assets/src/...`
   - no real source file exists yet
   - no paper-facing output exists yet

2. `blocked` + `source_present_output_missing`
   - a real source file exists
   - `paper/figs/...` output is still absent

3. `blocked` + `output_present_out_of_sync`
   - source exists
   - paper-facing output exists
   - byte-for-byte sync does not match the export-ready source

4. `blocked` + `output_present_status_stale`
   - source exists
   - paper-facing output exists and matches
   - but the generated status file has not been refreshed yet

5. `available` + `source_and_output_in_sync`
   - source exists
   - paper-facing output exists
   - helper confirms byte-for-byte sync

## Helper Workflow

Add a lightweight helper:

- `python3 scripts/manual_paper_assets.py sync`

This helper should:

- scan `paper/assets/src/*/asset.json`
- detect whether a source file exists
- copy export-ready source files into `paper/figs/` when possible
- regenerate `paper/assets/asset_status.json`

For now the helper should support only direct sync from an already-exported source in the destination format:

- PDF -> PDF
- PNG -> PNG

If authors provide only `svg`, `drawio`, or `mmd`, the helper should keep the asset blocked and report that a paper-facing export is still required.

## Preflight Reporting

`check_paper_figures.py` should distinguish:

- managed source missing
- managed source present but paper-facing output missing
- paper-facing output present but out of sync with managed source
- paper-facing output present and matching, but `asset_status.json` still stale

This gives more actionable feedback than a generic missing-figure failure while preserving strict failure behavior.

## Current Expected Result

Because the repository still lacks real source files for both assets, this task should end with:

- source-managed slots created
- generated `asset_status.json` enriched with source/output state
- preflight reporting improved
- both assets still `blocked`
