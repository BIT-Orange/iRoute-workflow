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
- non-evaluation paper asset status: `paper/assets/`

Paper-facing figures should be synchronized from `results/figures/` into `paper/figs/` through the repository-native publication step:

```bash
bash scripts/workflow.sh publish-figure --figure-id fig5_recovery_churn.paper_grade
```

Hand-maintained paper assets such as architecture diagrams are not generated through the experiment pipeline.
Their current status is tracked explicitly in `paper/assets/asset_status.json`, and `paper-preflight` treats missing manual assets as release-blocking debt.

Path resolution is centralized in `scripts/iroute-paths.sh`.
Shell runners source that helper to resolve canonical roots and explicit legacy fallbacks.

## Legacy Compatibility

These paths still exist temporarily and are not deleted in this phase:

- `ns-3/dataset/`
- `ns-3/results/`
- `figures/`

Runner scripts now prefer the top-level canonical paths.
The active smart-city working set is now physically present under `dataset/processed/sdm_smartcity_dataset/`.
If a requested dataset file has not yet been promoted, runners still fall back to `ns-3/dataset/` with an explicit warning.
If a caller explicitly targets `ns-3/results/` or `figures/`, that path is treated as legacy compatibility storage.

## Main Entrypoints

From the repository root:

```bash
bash scripts/workflow.sh lint
bash scripts/workflow.sh checks
bash scripts/workflow.sh smoke-run
bash scripts/workflow.sh paper-preflight
bash scripts/workflow.sh publish-figure --figure-id <figure-id>
bash scripts/workflow.sh release-dossier
```

From `ns-3/`:

```bash
./experiments/run_sanr_baseline.sh
./experiments/run_paper_suite.sh
```

## Contributor Flow

Use the repository-native GitHub templates when opening work items:

- experiment execution or rerun tasks: `experiment_task`
- paper evidence or claim gaps: `paper_claim_gap`
- workflow and repo-structure refactors: `workflow_refactor`

Before opening a PR, run:

```bash
bash scripts/workflow.sh merge-gate
```

If the PR changes paper-grade evidence, claims, or figure provenance, also update:

- `review/claims/claims_map.json`
- `review/claims/CLAIM_STATUS.md`
- the relevant run IDs, aggregate CSV references, and figure provenance files under `results/`

Label guidance lives in `review/GOVERNANCE_LABELS.md`.

For a pre-submission snapshot of current release readiness, generate:

```bash
bash scripts/workflow.sh release-dossier
```

This writes a JSON + Markdown dossier under `review/paper_audit/` summarizing claim status, figure publication state, manual paper asset debt, and the paper-facing files that are still missing.
