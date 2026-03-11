# Paper Release Dossier

Generated at: `2026-03-11T06:55:38.093688+00:00`

This report is a repository-native release snapshot. It does not replace the strict `paper-release-gate`.

## Summary

- claims: `supported=7` `provisional=2` `blocked=0`
- figure manifests: `published=5` `partial=2` `blocked=0` `placeholder=1`
- manual paper assets: `blocked=2`
- missing paper-facing references: `4`

## Release Readiness

- current dossier view: `not ready`
- blocker: 2 provisional claim(s) remain
- blocker: 4 paper-facing asset(s) referenced by paper/main.tex are still missing
- blocker: 2 manual paper asset(s) remain blocked

## Claims

### Supported

- `CLM-EVAL-001` Paper-grade routing evaluation disables caching by default
- `CLM-EVAL-002` Legacy SANR workflow remains cache-enabled exploratory evidence
- `CLM-EVAL-003` Centralized directory is plotted only as an oracle upper bound
- `CLM-EVAL-004` Exact-NDN is a syntactic reference rather than an apples-to-apples intent-discovery comparator
- `CLM-EVAL-005` Bounded advertisements trade correctness against control overhead
- `CLM-EVAL-006` Selective probing improves retrieval efficiency relative to high-overhead search
- `CLM-EVAL-009` Recovery under churn and failures is supported by the published Fig. 5 bundle

### Provisional

- `CLM-EVAL-007` Hop count remains localized as offered load increases
- `CLM-EVAL-008` Routing state scales with bounded per-domain advertisements

### Blocked

- none

## Figures

### Published

- `fig1_accuracy_overhead.paper_grade` -> `results/figures/fig12-paper-grade-20260310a/fig1_accuracy_overhead.pdf`; paper ref `figs/fig1_accuracy_overhead.pdf`; paper file present=`True`
- `fig2_retrieval_cdf.paper_grade` -> `results/figures/fig12-paper-grade-20260310a/fig2_retrieval_cdf.pdf`; paper ref `figs/fig2_retrieval_cdf.pdf`; paper file present=`True`
- `fig5_recovery_churn.paper_grade` -> `results/figures/fig5-failure-paper-grade-final-20260311d/fig5_recovery_churn.pdf`; paper ref `figs/fig5_recovery_churn.pdf`; paper file present=`True`
- `fig5_recovery_domain-fail.paper_grade` -> `results/figures/fig5-failure-paper-grade-final-20260311d/fig5_recovery_domain-fail.pdf`; paper ref `figs/fig5_recovery_domain-fail.pdf`; paper file present=`True`
- `fig5_recovery_link-fail.paper_grade` -> `results/figures/fig5-failure-paper-grade-final-20260311d/fig5_recovery_link-fail.pdf`; paper ref `figs/fig5_recovery_link-fail.pdf`; paper file present=`True`

### Partial / Blocked / Placeholder

- `fig3_hop_load.paper_grade` [partial] -> `results/figures/fig3-load-paper-grade-20260311a/fig3_hop_load.pdf`; paper ref `figs/fig3_hop_load.pdf`; paper file present=`False`
- `fig4_state_scaling.paper_grade` [partial] -> `results/figures/fig4-scaling-paper-grade-20260311b/fig4_state_scaling.pdf`; paper ref `figs/fig4_state_scaling.pdf`; paper file present=`False`
- `promoted-run-summary-placeholder` [placeholder] -> ``; paper ref `unmatched`; paper file present=`False`

## Manual Paper Assets

- `figs/system-arch.pdf` [blocked] management=`manual` source_kind=`missing_repo_source`
- `figs/mermaid.png` [blocked] management=`manual` source_kind=`missing_repo_source`

## Missing Paper-Facing References

- `figs/system-arch.pdf` [manual_asset] -> `paper/figs/system-arch.pdf` (asset_status=blocked)
- `figs/mermaid.png` [manual_asset] -> `paper/figs/mermaid.png` (asset_status=blocked)
- `figs/fig3_hop_load.pdf` [evaluation_figure] -> `paper/figs/fig3_hop_load.pdf` (figure_status=partial)
- `figs/fig4_state_scaling.pdf` [evaluation_figure] -> `paper/figs/fig4_state_scaling.pdf` (figure_status=partial)

## Supported Claim Evidence

- `CLM-EVAL-001` Paper-grade routing evaluation disables caching by default
  figures: none
  aggregates: results/aggregates/run_index.csv, results/aggregates/summary_rows.csv
  runs: smoke-iroute-star-s42-20260310a
  static: ns-3/experiments/manifests/paper_suite.json, ns-3/experiments/runners/run_paper_suite.sh
- `CLM-EVAL-002` Legacy SANR workflow remains cache-enabled exploratory evidence
  figures: none
  aggregates: none
  runs: none
  static: ns-3/experiments/manifests/sanr_baseline.json, ns-3/experiments/runners/run_sanr_baseline.sh
- `CLM-EVAL-003` Centralized directory is plotted only as an oracle upper bound
  figures: paper/figs/fig1_accuracy_overhead.pdf
  aggregates: none
  runs: none
- `CLM-EVAL-004` Exact-NDN is a syntactic reference rather than an apples-to-apples intent-discovery comparator
  figures: paper/figs/fig1_accuracy_overhead.pdf, paper/figs/fig2_retrieval_cdf.pdf
  aggregates: none
  runs: none
- `CLM-EVAL-005` Bounded advertisements trade correctness against control overhead
  figures: paper/figs/fig1_accuracy_overhead.pdf
  aggregates: results/aggregates/fig12-paper-grade-20260310a/accuracy_sweep.csv, results/aggregates/fig12-paper-grade-20260310a/comparison.csv, results/aggregates/fig12-paper-grade-20260310a/fig12_paper_grade_report.json
  runs: fig12-paper-grade-20260310a-central_proc2_s42, fig12-paper-grade-20260310a-flood_budget2_s42, fig12-paper-grade-20260310a-flood_budget4_s42, fig12-paper-grade-20260310a-flood_budget8_s42, ... (10 total)
  static: ns-3/experiments/runners/run_accuracy_experiment.sh, results/figures/fig1_accuracy_overhead.paper_grade.figure.json
- `CLM-EVAL-006` Selective probing improves retrieval efficiency relative to high-overhead search
  figures: paper/figs/fig2_retrieval_cdf.pdf
  aggregates: results/aggregates/fig12-paper-grade-20260310a/reference_runs.csv, results/aggregates/fig12-paper-grade-20260310a/fig2_latency_summary.csv, results/aggregates/fig12-paper-grade-20260310a/fig12_paper_grade_report.json
  runs: fig12-paper-grade-20260310a-central_proc2_s42, fig12-paper-grade-20260310a-flood_budget8_s42, fig12-paper-grade-20260310a-iroute_M4_s42, fig12-paper-grade-20260310a-tag_tagK32_s42
  static: ns-3/experiments/runners/run_accuracy_experiment.sh, results/figures/fig2_retrieval_cdf.paper_grade.figure.json
- `CLM-EVAL-009` Recovery under churn and failures is supported by the published Fig. 5 bundle
  figures: paper/figs/fig5_recovery_churn.pdf, paper/figs/fig5_recovery_link-fail.pdf, paper/figs/fig5_recovery_domain-fail.pdf
  aggregates: results/aggregates/fig5-failure-paper-grade-final-20260311d/recovery_summary.csv, results/aggregates/fig5-failure-paper-grade-final-20260311d/failure_paper_grade_report.json
  runs: fig5-failure-paper-grade-final-20260311d-central-churn-s42, fig5-failure-paper-grade-final-20260311d-central-domain-fail-s42, fig5-failure-paper-grade-final-20260311d-central-link-fail-s42, fig5-failure-paper-grade-final-20260311d-flood-churn-s42, ... (9 total)
  static: ns-3/experiments/manifests/paper_suite.json, ns-3/experiments/runners/run_failure_experiment.sh, scripts/fig345_paper_grade.py
