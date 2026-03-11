# Fig.1/Fig.2 Paper-Grade Promotion Plan

## Scope

This task promotes the current provisional evidence behind Fig. 1 and Fig. 2 into canonical paper-grade evidence without changing routing semantics.

## What Fig. 1 Measures

- Fig. 1 (`fig1_accuracy_overhead.pdf`) measures the correctness-overhead tradeoff under bounded advertisements.
- Its source data is the accuracy sweep:
  - `domain_recall_at_1/3/5`
  - `doc_hit_at_10`
  - `ndcg_at_10`
  - `ctrl_bytes_per_sec`
- The figure is driven by the multi-configuration accuracy experiment, not by the load or scaling suites.

## What Fig. 2 Measures

- Fig. 2 (`fig2_retrieval_cdf.pdf`) measures end-to-end retrieval latency as a success-only cache-miss CDF.
- It is derived from the reference runs within the same accuracy experiment bundle.
- The plot consumes per-run `query_log.csv` files and therefore requires promoted canonical runs, not only a summary CSV.

## Why They Are Still Provisional Today

- `CLM-EVAL-005` is provisional because the current paper-visible Fig. 1 file has been treated as cache-enabled SANR-bundle evidence rather than canonical paper-grade routing evidence.
- `CLM-EVAL-006` is provisional because the current Fig. 2 file is likewise tied to cache-enabled archive outputs and there is no canonical paper-grade latency bundle plus figure provenance linking it back to promoted runs.

## Minimum Evidence Needed To Support Fig. 1

- A fresh cache-disabled accuracy sweep produced with:
  - `PAPER_GRADE=1`
  - `CACHE_MODE=disabled`
  - `CS_SIZE=0`
  - native seeds only
- Promotion of the underlying per-run outputs into `results/runs/<run-id>/`
- A canonical aggregate bundle under `results/aggregates/<batch-id>/` containing:
  - `accuracy_sweep.csv`
  - `reference_runs.csv`
  - `comparison.csv`
- A figure provenance manifest under `results/figures/` pointing to the generated Fig. 1 PDF, the aggregate bundle, and the promoted run IDs

## Minimum Evidence Needed To Support Fig. 2

- The same cache-disabled paper-grade accuracy bundle as Fig. 1
- Canonical promoted reference runs with:
  - `run_class=paper_grade`
  - `cache_mode=disabled`
  - `seed_provenance=native`
- A latency-oriented aggregate under `results/aggregates/<batch-id>/fig2_latency_summary.csv`
- A figure provenance manifest for Fig. 2 referencing:
  - the generated `fig2_retrieval_cdf.pdf`
  - the paper-grade aggregate bundle
  - the exact promoted run IDs used by the CDF

## Required Runs

- One full paper-grade accuracy experiment bundle is sufficient for both claims.
- The required source runner is:
  - `ns-3/experiments/runners/run_accuracy_experiment.sh`
- The canonical run batch produced by this task must:
  - preserve the sweep configurations used by Fig. 1
  - preserve the reference-run subset used by Fig. 2
  - avoid cache-enabled SANR settings entirely

## Cache-Disabled Enforcement

- `run_accuracy_experiment.sh` must record `paper_grade` metadata in each per-run manifest.
- The Fig. 1/Fig. 2 orchestration layer must refuse promotion unless:
  - `paper_grade=true`
  - `cache_mode=disabled`
  - `cs_size=0`
  - `seed_provenance=native`
- No historical cache-enabled figure files may be relabeled as paper-grade.

## Aggregate And Figure Conventions

- Canonical batch aggregates live under `results/aggregates/<batch-id>/`
- Canonical figure files live under `results/figures/<batch-id>/`
- The paper include tree `paper/figs/` is updated only by copying the newly generated canonical figure PDFs
- Each promoted figure gets a root-level provenance manifest in `results/figures/`

## Validation Required

- Run artifact regression on promoted Fig. 1/Fig. 2 runs
- Refresh `results/aggregates/run_index.csv` and `results/aggregates/summary_rows.csv`
- Run `check_claim_evidence.py --summary-only`
- Upgrade `CLM-EVAL-005` and `CLM-EVAL-006` only if the new promoted evidence is actually present

## Deferred Work

- Fig. 3, Fig. 4, and Fig. 5 remain outside this task
- No load/scaling/failure paper-grade reruns are attempted here
- No broad rewrite of the evaluation section is needed; only wording required to reflect the new Fig. 1/Fig. 2 provenance should change
