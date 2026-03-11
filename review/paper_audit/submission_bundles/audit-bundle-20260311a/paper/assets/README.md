# Paper Asset Status

This directory tracks non-evaluation assets referenced by `paper/main.tex`.

Use it for assets that are part of the paper source but are not generated through the experiment figure pipeline, for example:

- architecture diagrams
- hand-maintained overview graphics
- author-supplied workflow images

Machine-readable status lives in `asset_status.json`.
Source-managed manual assets live under `src/`.

Current policy:

- generated evaluation figures belong under `results/figures/`
- paper-facing evaluation figures are synchronized into `paper/figs/` through `publish-figure`
- non-evaluation paper assets are tracked here with explicit status
- each manual asset should have a source-managed slot under `paper/assets/src/<asset-id>/asset.json`
- missing manual assets should be marked `blocked`, not left implicit
- once a manual asset source and synchronized output both exist, its status should move to `available`; preflight treats a stale still-`blocked` entry as inconsistent

Refresh or sync manual asset status with:

```bash
bash scripts/workflow.sh paper-assets-sync
```
