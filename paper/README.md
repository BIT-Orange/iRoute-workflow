# Canonical Paper Tree

`paper/main.tex` is the canonical paper source for the repository.

- `figs/` stores copied figure assets for the canonical paper tree
- `assets/` stores status manifests for non-evaluation paper assets
- `figures/` is a temporary compatibility mirror for unchanged relative references
- `archive/` stores the deprecated ndnSIM paper tree snapshot
- `sections/` and `tables/` are reserved for later logical extraction

Asset policy:

- evaluation figures are generated under `results/figures/` and published into `paper/figs/`
- hand-maintained paper assets are tracked in `paper/assets/asset_status.json`
- `paper-preflight` fails on both missing generated figures and missing manual paper assets

Compile from this directory:

```bash
pdflatex -interaction=nonstopmode -halt-on-error -output-directory /tmp/iroute-paper-check main.tex
```
