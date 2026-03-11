# Figure Publication Plan

## Goal

Add a generic, repository-native publication step that moves a canonical figure from `results/figures/` into `paper/figs/` only after provenance checks pass.

## Publication Rule

A figure manifest may move to `published` only when all of the following are true:

- the canonical figure file exists under `results/figures/`
- the manifest is not `blocked` or `placeholder`
- every referenced aggregate input exists
- every referenced canonical run exists under `results/runs/`
- if the manifest is currently `partial`, at least one referenced aggregate report JSON explicitly reports `status=published`
- the destination file under `paper/figs/` matches a figure actually referenced by `paper/main.tex`
  - unless an explicit override is used for an intentionally unreferenced paper-facing asset

## Path Mapping

- source of truth for generated figures:
  - `results/figures/<bundle>/...`
- source of truth for figure provenance:
  - `results/figures/<figure-id>.figure.json`
- paper-facing include tree:
  - `paper/figs/<basename>`

Default mapping:

- read `<figure-id>.figure.json`
- resolve `figure_path`
- publish to `paper/figs/<basename(figure_path)>`

Optional override:

- caller may provide `--paper-name <name>` to choose a different destination filename under `paper/figs/`

## Required Checks Before Publication

- source manifest exists
- source `figure_path` exists and is a file
- source figure extension is paper-facing safe (`.pdf` or `.png`)
- aggregate inputs exist
- run IDs resolve to canonical promoted runs
- `paper/main.tex` references `figs/<paper-name>` unless explicitly overridden
- copied paper-facing file hash matches the canonical figure hash

## Publication Provenance

The published manifest should record:

- `status=published`
- `paper_figure_path`
- `paper_figure_exists`
- `paper_figure_sha256`
- `paper_figure_size_bytes`
- `paper_figure_in_sync`
- `publication.published_at_utc`
- `publication.source_status_before`
- `publication.source_manifest`
- `publication.aggregate_reports`
- `publication.command`

This keeps publication auditable and reversible without treating `paper/figs/` as an opaque side effect.

## Allowed Status Transitions

- `partial -> published`
  - allowed only if the publication checks pass and the aggregate bundle is already marked publishable
- `published -> published`
  - allowed as an idempotent re-sync or metadata backfill operation
- `blocked -> published`
  - forbidden
- `placeholder -> published`
  - forbidden

## Validation Strategy

- positive case:
  - validate a figure family that already has sufficient provenance, such as Fig. 1, Fig. 2, or Fig. 5
- negative case:
  - verify publication fails for a still-partial figure such as Fig. 3 or Fig. 4
- preflight:
  - once synchronized, `paper-preflight` should benefit from the now-present paper-facing file
