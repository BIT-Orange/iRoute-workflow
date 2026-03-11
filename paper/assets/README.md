# Paper Asset Status

This directory tracks non-evaluation assets referenced by `paper/main.tex`.

Use it for assets that are part of the paper source but are not generated through the experiment figure pipeline, for example:

- architecture diagrams
- hand-maintained overview graphics
- author-supplied workflow images

Machine-readable status lives in `asset_status.json`.

Current policy:

- generated evaluation figures belong under `results/figures/`
- paper-facing evaluation figures are synchronized into `paper/figs/` through `publish-figure`
- non-evaluation paper assets are tracked here with explicit status
- missing manual assets should be marked `blocked`, not left implicit
- once a manual asset is actually added to `paper/figs/`, its manifest entry should move to `available`; preflight treats a still-`blocked` entry as inconsistent
