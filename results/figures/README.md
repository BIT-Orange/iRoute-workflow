# Canonical Figure Provenance

This directory stores promoted figure provenance manifests and curated figure outputs.

Canonical policy:

- new generated figure bundles should land here
- root `figures/` is only a legacy compatibility mirror for older workflows
- `paper/figs/` remains the canonical include tree for `paper/main.tex`

Conventions:

- each figure or placeholder gets `<figure-id>.figure.json`
- `figure_index.json` summarizes all figure manifests
- manifests link figures back to aggregate files and source run IDs

Write a placeholder or provenance manifest with:

```bash
python3 scripts/paper_grade_pipeline.py figure-manifest --figure-id <id> --title <title> --status placeholder --aggregate results/aggregates/summary_rows.csv --run-id <run-id>
```
