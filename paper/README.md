# Canonical Paper Tree

`paper/main.tex` is the canonical paper source for the repository.

- `figs/` stores copied figure assets for the canonical paper tree
- `figures/` is a temporary compatibility mirror for unchanged relative references
- `archive/` stores the deprecated ndnSIM paper tree snapshot
- `sections/` and `tables/` are reserved for later logical extraction

Compile from this directory:

```bash
pdflatex -interaction=nonstopmode -halt-on-error -output-directory /tmp/iroute-paper-check main.tex
```
