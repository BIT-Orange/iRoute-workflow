# Canonical Figure Provenance

This directory stores promoted figure provenance manifests and curated figure outputs.

Canonical policy:

- new generated figure bundles should land here
- root `figures/` is only a legacy compatibility mirror for older workflows
- `paper/figs/` remains the canonical include tree for `paper/main.tex`
- non-evaluation paper assets are tracked separately in `paper/assets/asset_status.json`

Conventions:

- each figure or placeholder gets `<figure-id>.figure.json`
- `figure_index.json` summarizes all figure manifests
- manifests link figures back to aggregate files and source run IDs
- figure manifest `status` values are:
  - `published`: figure is synchronized and ready for paper-facing use
  - `partial`: figure exists under `results/figures/` but is still a focused or incomplete evidence bundle
  - `blocked`: the workflow knows the figure family, but generation or evidence is still blocked
  - `placeholder`: reserved for non-plot placeholder manifests

Publication rule:

- figures are generated under `results/figures/`
- figures become paper-facing only through `publish-figure`
- publication synchronizes the canonical figure into `paper/figs/`
- `published` requires:
  - an existing canonical figure file
  - existing aggregate inputs
  - existing canonical run IDs
  - byte-for-byte sync between `results/figures/` and `paper/figs/`
  - a manifest that is not `blocked` or `placeholder`

Write a placeholder or provenance manifest with:

```bash
python3 scripts/paper_grade_pipeline.py figure-manifest --figure-id <id> --title <title> --status placeholder --aggregate results/aggregates/summary_rows.csv --run-id <run-id>
```

Publish a figure into `paper/figs/` with:

```bash
python3 scripts/paper_grade_pipeline.py publish-figure --figure-id fig5_recovery_churn.paper_grade
```

Dry-run validation without copying files:

```bash
python3 scripts/paper_grade_pipeline.py publish-figure --figure-id fig3_hop_load.paper_grade --dry-run
```
