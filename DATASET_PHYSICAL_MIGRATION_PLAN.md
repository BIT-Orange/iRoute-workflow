# Dataset Physical Migration Plan

## Scope

This phase physically promotes only the active experiment-ready smart-city working set into the canonical top-level dataset tree.

## Physical Migration In Scope Now

Promote this subtree from legacy storage into canonical storage:

- `ns-3/dataset/sdm_smartcity_dataset/`
  - target: `dataset/processed/sdm_smartcity_dataset/`

Why this subtree:

- it is the working set used by the active runners and workflow smoke checks
- it is small enough to duplicate safely (`~6 MB`)
- it contains the exact canonical inputs referenced by the current cache-disabled paper-grade workflow

Files covered:

- `consumer_trace.csv`
- `consumer_trace_diverse_unique.csv`
- `consumer_trace_train.csv`
- `consumer_trace_test.csv`
- `domain_centroids.csv`
- `domain_centroids_m4.csv`
- `producer_content.csv`
- `qrels.tsv`
- `tag_index.csv`
- `query_to_tag.csv`
- `index_exact.csv`
- `entities.csv`
- `report.md`

## Remains Legacy-Only For Now

Keep these in `ns-3/dataset/` for now:

- large raw archives such as `citypulse_*.tar.gz` and `.zip`
- auxiliary curation outputs under `dataset_clean/`
- one-off analysis helpers and historical preprocessing artifacts

Why they remain legacy-only:

- they are much larger than the active working set
- they are not required by the current reproducible experiment path
- copying them now would add storage and review noise without improving workflow correctness

## Duplication Policy

Avoid broad duplication.

- duplicate only the active smart-city working set
- do not move or delete the legacy copy in this phase
- use a dataset manifest with hashes so the canonical copy and legacy source remain auditable

## Provenance

Record dataset lineage in a machine-readable index:

- canonical root
- legacy source root
- per-file SHA-256
- per-file size
- which files are active workflow inputs

This keeps the physical promotion explicit and reviewable.

## Runtime Behavior After Migration

- runners keep resolving dataset files through `scripts/iroute-paths.sh`
- active smart-city inputs now resolve directly from `dataset/processed/sdm_smartcity_dataset/`
- fallback to `ns-3/dataset/` remains available only for not-yet-promoted files, with explicit warnings

## Deferred Follow-Up

- decide whether `dataset_clean/` should be curated into `dataset/raw/` or `dataset/processed/`
- decide whether raw CityPulse archives should move into `dataset/raw/`
- eventually retire `ns-3/dataset/sdm_smartcity_dataset/` once legacy consumers are removed and the canonical copy is the only supported source
