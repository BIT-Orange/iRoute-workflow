# Experiments

Stage 1 separates experiment tooling into dedicated subfolders while keeping the old command surface available.

## Canonical Layout

- `manifests/`: machine-readable workflow descriptions
- `runners/`: active shell entrypoints
- `plot/`: plotting utilities
- `checks/`: result validation utilities
- `paper/`: compatibility wrappers for the old paper workflow paths

## Recommended Entrypoints

From `ns-3/`:

```bash
./experiments/run_sanr_baseline.sh
./experiments/run_paper_suite.sh results/paper_custom
```

These top-level scripts are now compatibility wrappers. The active implementations live in `experiments/runners/`.

## Backwards Compatibility

The following legacy paths still exist and delegate to the canonical scripts:

- `experiments/run_accuracy_experiment.sh`
- `experiments/run_load_experiment.sh`
- `experiments/run_scaling_experiment.sh`
- `experiments/run_failure_experiment.sh`
- `experiments/run_paper_suite.sh`
- `experiments/run_sanr_baseline.sh`
- `experiments/plot_paper_figures.py`
- `experiments/check_results.py`
- `experiments/paper/run_*.sh`

## Build And Runtime Notes

- Scripts auto-set `HOME` to `ns-3/.home` to avoid host `~/.ndn` permission issues.
- If present, `ns-3/.venv/bin` is added to `PATH` automatically.
- Current working-set data still lives under `ns-3/dataset/sdm_smartcity_dataset/`.
- Current evidence outputs still live under `ns-3/results/sanr_baseline/` and `../figures/`.
