# Canonical Results Layout

`results/` is now the canonical home for iRoute outputs and evidence metadata.

- `runs/`: canonical promoted per-run outputs
- `aggregates/`: repository-level merged indexes and summary views built from `runs/`
- `figures/`: canonical generated figure bundles and figure provenance

Runner policy in this phase:

- active runner defaults now resolve into top-level `results/`
- explicit `ns-3/results/...` paths still work as legacy compatibility targets, with warnings
- root `figures/` remains a legacy mirror for older workflows, but new figure bundles should be treated as canonical under `results/figures/`

Legacy note:

- direct historical outputs under `ns-3/results/` remain readable and are not deleted
- use `scripts/paper_grade_pipeline.py promote ...` to import legacy per-run outputs into `results/runs/`

Smoke, exploratory, and paper-grade runs are distinguished by `results/runs/<run-id>/manifest.json`.
