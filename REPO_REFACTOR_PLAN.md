# Repository Refactor Plan

## Scope

This document records the stage-1 refactor that converts the current iRoute research workspace into a single top-level repository that is easier to review, reproduce, and automate.

The refactor is intentionally conservative:

- `ns-3/scratch/iroute-exp-baselines.cc` remains the only active experiment implementation.
- Existing runner entrypoints remain available through compatibility wrappers.
- The old ndnSIM paper tree is preserved and marked deprecated instead of deleted.
- Working data and evidence stay in place for now unless a move is low risk.

## Current Vs Target Mapping

| Current location | Stage-1 target | Stage-1 decision |
| --- | --- | --- |
| `paper.tex` | `paper/main.tex` | Copied to new canonical paper root; root copy remains as deprecated compatibility source. |
| `ns-3/src/ndnSIM/paper/paper.tex` | `paper/archive/ndnsim-paper-legacy.tex` | Archived copy created; original file stays in place with a deprecation notice. |
| `figures/` | `paper/figs/` | Current figure assets copied into `paper/figs/`; `paper/figures/` is kept as a compatibility mirror for unchanged relative references. |
| `ns-3/experiments/run_*.sh` | `ns-3/experiments/runners/` | Actual runner implementations move into `runners/`; original paths become wrappers. |
| `ns-3/experiments/plot_paper_figures.py` | `ns-3/experiments/plot/plot_paper_figures.py` | Plot implementation moves; original path becomes a wrapper. |
| `ns-3/experiments/check_results.py` | `ns-3/experiments/checks/check_results.py` | Result checks move; original path becomes a wrapper. |
| `ns-3/experiments/paper/run_*.sh` | `ns-3/experiments/runners/paper_*.sh` | Paper runners move; legacy paper paths become wrappers. |
| `dataset/calc_recovery.py` | `dataset/manifests/calc_recovery.py` | Utility moved into canonical manifests area; old path becomes a wrapper. |
| `ns-3/dataset/sdm_smartcity_dataset/` | `dataset/processed/` | Stays in place temporarily as the active working set; top-level dataset folders are created now for later migration. |
| `ns-3/results/sanr_baseline/` and `figures/` | `results/` | Stay in place temporarily as the active evidence set; top-level result folders are created now for later migration. |
| `ns-3/` and `pybindgen/` nested Git repos | top-level monorepo | Nested `.git/` directories are removed so the top-level repo can track both trees. |

## Files Moved In Stage 1

- `paper.tex` copied to `paper/main.tex`
- `ns-3/src/ndnSIM/paper/paper.tex` copied to `paper/archive/ndnsim-paper-legacy.tex`
- `ns-3/experiments/run_accuracy_experiment.sh` moved to `ns-3/experiments/runners/run_accuracy_experiment.sh`
- `ns-3/experiments/run_failure_experiment.sh` moved to `ns-3/experiments/runners/run_failure_experiment.sh`
- `ns-3/experiments/run_load_experiment.sh` moved to `ns-3/experiments/runners/run_load_experiment.sh`
- `ns-3/experiments/run_scaling_experiment.sh` moved to `ns-3/experiments/runners/run_scaling_experiment.sh`
- `ns-3/experiments/run_paper_suite.sh` moved to `ns-3/experiments/runners/run_paper_suite.sh`
- `ns-3/experiments/run_sanr_baseline.sh` moved to `ns-3/experiments/runners/run_sanr_baseline.sh`
- `ns-3/experiments/plot_paper_figures.py` moved to `ns-3/experiments/plot/plot_paper_figures.py`
- `ns-3/experiments/check_results.py` moved to `ns-3/experiments/checks/check_results.py`
- `ns-3/experiments/paper/run_accuracy.sh` moved to `ns-3/experiments/runners/paper_run_accuracy.sh`
- `ns-3/experiments/paper/run_failure.sh` moved to `ns-3/experiments/runners/paper_run_failure.sh`
- `ns-3/experiments/paper/run_load.sh` moved to `ns-3/experiments/runners/paper_run_load.sh`
- `ns-3/experiments/paper/run_scaling.sh` moved to `ns-3/experiments/runners/paper_run_scaling.sh`
- `ns-3/experiments/paper/run_suite.sh` moved to `ns-3/experiments/runners/paper_run_suite.sh`
- `dataset/calc_recovery.py` moved to `dataset/manifests/calc_recovery.py`

## Files Staying In Place Temporarily

- `paper.tex`
- `ns-3/src/ndnSIM/paper/paper.tex`
- `ns-3/dataset/sdm_smartcity_dataset/`
- `ns-3/results/sanr_baseline/`
- `figures/`
- `GEMINI.md`
- `Task.md`
- `INF-NDN_IoT_An_Intelligent_Naming_and_Forwarding_in_Name_Data_Networking_for_Internet_of_Things.pdf`
- `SANR-CMF_Semantic-Aware_Naming_Resolution_and_Cache_Management_Framework_in_Information-Centric_Network_for_Internet_of_Things.pdf`

These stay put in stage 1 because changing them now would either risk breaking existing experiment paths or start a broader evidence migration than this patch is meant to handle.

## Compatibility Shims

The following compatibility shims are required in stage 1:

- Shell wrappers at the original `ns-3/experiments/run_*.sh` paths that delegate to `ns-3/experiments/runners/`
- Shell wrappers at `ns-3/experiments/paper/run_*.sh` that delegate to `ns-3/experiments/runners/paper_*.sh`
- Python wrappers at `ns-3/experiments/plot_paper_figures.py`, `ns-3/experiments/check_results.py`, and `dataset/calc_recovery.py`
- Deprecated notices inside `paper.tex` and `ns-3/src/ndnSIM/paper/paper.tex`
- `paper/figures/` as a compatibility mirror while `paper/main.tex` is transitioned toward the `paper/figs/` layout

## Build And Compilation Risks

### Paper risks

- `paper/main.tex` depends on figure files that are not present in the repository:
  - `figs/system-arch.pdf`
  - `figs/mermaid.png`
  - `figs/fig3_hop_load.pdf`
  - `figs/fig4_state_scaling.pdf`
  - `figs/fig5_recovery_churn.pdf`
  - `figs/fig5_recovery_link-fail.pdf`
  - `figs/fig5_recovery_domain-fail.pdf`
- The root paper source still exists and can drift if edited directly. Stage 1 only marks it deprecated; it does not enforce sync automatically.
- The legacy ndnSIM paper source references `figures/system-arch.png`, which is also absent.

### Build and experiment risks

- Moving runner scripts changes relative path assumptions; stage 1 fixes the known paths but deeper assumptions may still exist in ad hoc scripts.
- `ns-3/scratch/iroute-exp-baselines.cc` is still monolithic, so experiment semantics remain tightly coupled even though new entrypoints are added.
- The flattened monorepo depends on removing `src/ndnSIM` from `ns-3/.gitignore`; if that line reappears, critical source files will silently drop from Git status.
- Existing local ns-3 build products are intentionally ignored and not migrated into the repo.

## Extraction Seams For Future Refactor

The monolithic experiment driver should later be split along these seams:

1. CLI and experiment configuration parsing
2. Topology construction and failure injection setup
3. Per-scheme discovery and forwarding dispatch
4. Metric aggregation and CSV or JSON emitters
5. Runtime sanity checks and evidence validation

Stage 1 only adds wrapper entrypoints for these eventual slices.

## Deferred Work

- Move the active working dataset from `ns-3/dataset/sdm_smartcity_dataset/` into `dataset/processed/`
- Move canonical evidence outputs into `results/runs/`, `results/aggregates/`, and `results/figures/`
- Extract reusable experiment logic from `ns-3/scratch/iroute-exp-baselines.cc`
- Deduplicate or harmonize legacy and canonical paper trees beyond deprecation markers
- Rebuild the missing paper figures from verified experiment outputs
- Add CI workflows after the command surface and evidence manifests stabilize
