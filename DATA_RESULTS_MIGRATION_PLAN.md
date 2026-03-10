# Data And Results Migration Plan

## Canonical Paths In This Phase

These paths become the canonical homes now:

- `dataset/`
  - canonical logical dataset root for curated inputs
  - experiment-ready working-set files are expected under `dataset/processed/`
- `results/`
  - canonical root for new experiment outputs
  - direct runner outputs should default here instead of `ns-3/results/`
- `results/figures/`
  - canonical home for generated figure bundles and figure provenance outside the paper tree
- `paper/figs/`
  - canonical figure include tree for `paper/main.tex`

In this phase, canonicalization is logical first and physical second.
The active smart-city working set is still stored on disk under `ns-3/dataset/sdm_smartcity_dataset/`, but runners should now resolve that data through a canonical top-level path policy with an explicit fallback.

## Legacy Paths Still Supported

These legacy paths remain supported temporarily:

- `ns-3/dataset/`
  - remains the on-disk fallback source for dataset files until the curated working set is physically promoted
- `ns-3/results/`
  - remains readable as historical raw output storage
  - remains usable if a user explicitly requests a legacy path
- `figures/`
  - remains a compatibility mirror for older figure bundle references and reviewer convenience

Legacy support must be explicit.
Scripts should warn when they fall back to `ns-3/dataset/` or when a caller explicitly targets `ns-3/results/` or `figures/`.

## Path Centralization

Add a small shell helper under `scripts/` that defines:

- repository root
- ns-3 root
- paper root
- canonical dataset root
- canonical results root
- canonical results figure root
- legacy dataset/results/figure roots

The helper should also expose small functions to:

- resolve a dataset file from canonical top-level paths with fallback to `ns-3/dataset/`
- resolve output directories so `results/...` maps to the repo-root canonical results tree
- resolve explicit legacy output paths and warn

This keeps path policy in one place and avoids further copy-paste of `dataset/sdm_smartcity_dataset/...` and `results/...` across runners.

## Runner Changes

Conservative runner updates:

- source the shared path helper
- default output directories into top-level `results/`
- resolve dataset files through the shared helper
- keep accepting explicit absolute paths and explicit legacy paths
- emit warnings when using legacy dataset or results locations

Specific expectations:

- `run_accuracy_experiment.sh`
- `run_load_experiment.sh`
- `run_scaling_experiment.sh`
- `run_failure_experiment.sh`
- `run_sanr_baseline.sh`
- `run_paper_suite.sh`
- `scripts/workflow.sh`
- `scripts/paper_grade_pipeline.py`

## Figure Compatibility

Generated figure bundles should now be treated as canonical under `results/figures/`.
To avoid breaking existing review flows:

- SANR and paper-suite figure generation may still mirror outputs into root `figures/`
- the mirror must be labeled as legacy compatibility, not canonical storage
- paper include assets remain under `paper/figs/`; this phase does not rewrite the paper to use `results/figures/`

## Risks

Main risks in this phase:

- runners executed from `ns-3/` may still assume `results/...` means `ns-3/results/...`
- plotting scripts may have legacy assumptions when resolving relative `result_dir` values
- historical figure indexes contain absolute paths from older machines and should not be treated as canonical lineage
- a premature physical move of the dataset tree could break the existing C++ auto-detection behavior around adjacent `index_exact.csv` and `qrels.tsv`

Mitigations:

- resolve dataset files one-by-one and fall back explicitly
- pass absolute dataset paths to ns-3 binaries where practical
- keep root `figures/` as a mirror for now
- do not delete or move large dataset blobs in this patch

## Deferred Physical Migration

Still deferred after this phase:

- physically moving `ns-3/dataset/sdm_smartcity_dataset/` into `dataset/processed/sdm_smartcity_dataset/`
- re-curating historical `ns-3/results/` bundles into canonical `results/runs/`, `results/aggregates/`, and `results/figures/`
- cleaning absolute-path residue from older figure indexes and legacy summary CSVs
