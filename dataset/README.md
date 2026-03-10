# Dataset Layout

`dataset/` is now the canonical dataset root for curated iRoute inputs.

- `raw/`: immutable source archives or externally obtained inputs
- `processed/`: curated datasets used by reproducible experiments
- `manifests/`: helper scripts and metadata that describe dataset lineage

Current migration policy:

- runners resolve dataset files from `dataset/processed/` first
- if a requested file is not yet present there, they fall back to `ns-3/dataset/` with an explicit warning
- large working-set files are not duplicated blindly in this phase

The active smart-city working set therefore still physically lives under `ns-3/dataset/sdm_smartcity_dataset/`, but top-level `dataset/` is the canonical location that new docs and workflow code should reference.

The first helper already promoted into this canonical area is `dataset/manifests/calc_recovery.py`.
