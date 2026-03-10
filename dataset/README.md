# Dataset Layout

This top-level tree defines the future canonical home for curated iRoute datasets.

- `raw/`: immutable source archives or externally obtained inputs
- `processed/`: curated datasets used by reproducible experiments
- `manifests/`: helper scripts and metadata that describe dataset lineage

Stage 1 keeps the active working set in `ns-3/dataset/sdm_smartcity_dataset/` to avoid breaking existing experiment paths. The first helper moved into this canonical area is `dataset/manifests/calc_recovery.py`.
