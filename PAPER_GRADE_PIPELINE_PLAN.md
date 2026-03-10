# Paper-Grade Pipeline Plan

## Goals

- make `./results/` the canonical home for promoted runs, aggregates, and figure provenance
- preserve compatibility with existing `./ns-3/results/` outputs during migration
- keep provenance explicit so smoke, exploratory, and paper-grade runs are never conflated

## Canonical Run Layout

Canonical per-run home:

- `results/runs/<run-id>/manifest.json`
- `results/runs/<run-id>/summary.csv`
- `results/runs/<run-id>/query_log.csv`
- `results/runs/<run-id>/latency_sanity.csv`
- `results/runs/<run-id>/failure_sanity.csv` when applicable
- `results/runs/<run-id>/source_manifest.json` when the source run already had a direct driver manifest
- `results/runs/<run-id>/source_run_manifest.json` when the source run already had a lineage manifest from a runner

`manifest.json` in the canonical run directory becomes the pipeline manifest and records:

- `run_id`
- `run_class`: `smoke`, `exploratory`, or `paper_grade`
- `workflow`
- `runner`
- `cache_mode`
- `seed_provenance`
- `git_commit`
- `source_dir`
- `source_kind`
- major CLI / runner options when available
- copied artifact inventory with hashes

## Aggregate Layout

Canonical aggregate home:

- `results/aggregates/run_index.csv`
- `results/aggregates/summary_rows.csv`
- `results/aggregates/paper_grade_summary_rows.csv`
- `results/aggregates/evidence_index.csv`
- `results/aggregates/aggregate_report.json`

Aggregate behavior:

- scan `results/runs/*/manifest.json`
- fail on duplicate or conflicting `run_id` declarations
- build an all-runs summary view
- build a paper-grade-only summary view
- emit warnings when smoke or exploratory runs are excluded from paper-grade views

## Figure Layout

Canonical figure home:

- `results/figures/<figure-id>.figure.json`
- `results/figures/figure_index.json`

At this stage, figure promotion is provenance-first:

- each promoted or placeholder figure gets a manifest linking it to aggregate CSVs and run IDs
- partial figure status is explicit (`placeholder`, `partial`, or `published`)
- no full paper figure regeneration is required in this patch

## Run Classes

### smoke

- tiny plumbing validation
- never treated as paper evidence
- may come from a minimal direct `iroute-exp-baselines` invocation

### exploratory

- ad hoc or legacy imported runs
- useful for debugging and historical comparison
- not paper-grade by default

### paper_grade

- must be intentionally labeled
- requires explicit cache-disabled semantics when relevant
- requires native seed provenance
- must not be assigned automatically to historical legacy outputs with incomplete lineage

## Compatibility With `ns-3/results`

Migration policy:

- existing runners and historical analyses are not broken
- direct runner outputs under `ns-3/results/` remain legacy-compatible locations
- the new pipeline can import legacy per-run directories into `results/runs/`
- documentation must clearly state:
  - `results/` is canonical
  - `ns-3/results/` is legacy / compatibility storage

This task does not rewrite all runner defaults to emit directly into `results/`.
That broader cutover is deferred until the promotion and aggregation layer is established.

## Conservative Implementation Scope

Implement now:

- a canonical pipeline CLI for smoke execution and legacy run promotion
- a canonical aggregate builder
- a figure provenance manifest writer
- updated `results/` documentation

Defer:

- full suite-level paper runner promotion
- automatic regeneration of the full paper figure bundle
- direct runner default rewrites away from `ns-3/results/`

## Validation Target

Minimum validation in this patch:

- run one new smoke validation and promote it into `results/runs/`
- promote one legacy per-run output from `ns-3/results/` as `exploratory`
- build aggregates from the promoted runs
- write one placeholder figure provenance manifest tied to the aggregate and promoted run IDs
