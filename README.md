# iRoute Repository

The repository now uses the top-level trees as the canonical homes for paper, dataset, and results material.

## Canonical Roots

- `paper/`: canonical paper source
- `dataset/`: canonical dataset root
- `results/`: canonical experiment output and evidence root
- `review/`: audits, workflow notes, and claim tracking

Canonical subpaths used by the active workflow:

- experiment-ready datasets: `dataset/processed/`
- promoted runs: `results/runs/`
- aggregate CSVs and indexes: `results/aggregates/`
- generated figure bundles and figure provenance: `results/figures/`
- paper include figures: `paper/figs/`

Path resolution is centralized in `scripts/iroute-paths.sh`.
Shell runners source that helper to resolve canonical roots and explicit legacy fallbacks.

## Legacy Compatibility

These paths still exist temporarily and are not deleted in this phase:

- `ns-3/dataset/`
- `ns-3/results/`
- `figures/`

Runner scripts now prefer the top-level canonical paths.
If the physical dataset move is not complete, they fall back to `ns-3/dataset/` with an explicit warning.
If a caller explicitly targets `ns-3/results/` or `figures/`, that path is treated as legacy compatibility storage.

## Main Entrypoints

From the repository root:

```bash
bash scripts/workflow.sh lint
bash scripts/workflow.sh checks
bash scripts/workflow.sh smoke-run
bash scripts/workflow.sh paper-preflight
```

From `ns-3/`:

```bash
./experiments/run_sanr_baseline.sh
./experiments/run_paper_suite.sh
```
