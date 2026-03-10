# Migration Notes

## Canonical Locations

- Canonical paper source: `paper/main.tex`
- Archived legacy paper snapshot: `paper/archive/ndnsim-paper-legacy.tex`
- Active experiment implementations: `ns-3/experiments/runners/`
- Legacy experiment entrypoints kept for compatibility: `ns-3/experiments/*.sh`, `ns-3/experiments/paper/*.sh`
- Canonical result checks: `ns-3/experiments/checks/check_results.py`
- Canonical plotting script: `ns-3/experiments/plot/plot_paper_figures.py`
- Canonical dataset helper manifest: `dataset/manifests/calc_recovery.py`
- Canonical dataset root: `dataset/`
- Canonical results root: `results/`
- Canonical shell path helper: `scripts/iroute-paths.sh`

## Paper Workflow

Use `paper/main.tex` as the only editable paper source in stage 1.

Compile from the canonical paper directory:

```bash
cd paper
pdflatex -interaction=nonstopmode -halt-on-error -output-directory /tmp/iroute-paper-check main.tex
```

`paper/figs/` is the canonical paper figure tree. `paper/figures/` exists only as a temporary compatibility mirror while legacy relative references are cleaned up.

## Known Paper Compile Blockers

The following figure assets are referenced by `paper/main.tex` but are not present in the repository as of March 10, 2026:

- `paper/figs/system-arch.pdf`
- `paper/figs/mermaid.png`
- `paper/figs/fig3_hop_load.pdf`
- `paper/figs/fig4_state_scaling.pdf`
- `paper/figs/fig5_recovery_churn.pdf`
- `paper/figs/fig5_recovery_link-fail.pdf`
- `paper/figs/fig5_recovery_domain-fail.pdf`

Stage 1 records these as explicit debt rather than fabricating replacements.

## Experiment Layout

`ns-3/experiments/` now has these subfolders:

- `manifests/`: machine-readable experiment metadata
- `runners/`: active shell entrypoints
- `plot/`: figure generation scripts
- `checks/`: result validation scripts

The original top-level experiment paths remain as wrappers so existing commands still work.

## Dataset And Results Lineage

- Top-level `dataset/` is now the canonical dataset root, with runners resolving `dataset/processed/` first
- Until the physical move is completed, dataset resolution falls back to `ns-3/dataset/sdm_smartcity_dataset/` with an explicit warning
- Top-level `results/` is now the canonical results root for new runner output defaults
- Historical bundles under `ns-3/results/` remain readable legacy storage and promotion sources
- Root `figures/` remains a legacy mirror; canonical generated figure bundles belong under `results/figures/`

## Moved Files Summary

- Experiment runners moved from `ns-3/experiments/` into `ns-3/experiments/runners/`
- Plotting and check scripts moved into `ns-3/experiments/plot/` and `ns-3/experiments/checks/`
- `dataset/calc_recovery.py` moved into `dataset/manifests/`
- The root paper source was copied into `paper/main.tex`
- The legacy ndnSIM paper source was archived into `paper/archive/ndnsim-paper-legacy.tex`

## Deferred Refactor Debt

- Split `ns-3/scratch/iroute-exp-baselines.cc` into shared libraries and thinner experiment binaries
- Physically move curated dataset artifacts into `dataset/processed/`
- Continue curating historical raw outputs from `ns-3/results/` into canonical `results/`
- Reconstruct or regenerate the missing paper figures
- Add CI once the canonical runner and evidence manifests are stable
