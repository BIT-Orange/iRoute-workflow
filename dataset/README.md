# Dataset Layout

`dataset/` is now the canonical dataset root for curated iRoute inputs.

- `raw/`: immutable source archives or externally obtained inputs
- `processed/`: curated datasets used by reproducible experiments
- `manifests/`: helper scripts and metadata that describe dataset lineage

Current migration policy:

- runners resolve dataset files from `dataset/processed/` first
- if a requested file is not yet present there, they fall back to `ns-3/dataset/` with an explicit warning
- only the active experiment-ready working set is physically promoted in this phase; large raw archives remain legacy-only for now

The active smart-city working set now physically lives under:

- `dataset/processed/sdm_smartcity_dataset/`

Its canonical file index is:

- `dataset/manifests/sdm_smartcity_dataset.index.json`

The legacy source subtree:

- `ns-3/dataset/sdm_smartcity_dataset/`

is retained temporarily for compatibility and provenance, but new workflow code should treat the top-level processed copy as canonical.

The first helper already promoted into this canonical area is `dataset/manifests/calc_recovery.py`.
