# Paper Submission Bundle

Generated at: `2026-03-11T08:45:01.549533+00:00`

## Bundle Status

- bundle id: `audit-bundle-20260311a`
- bundle status: `audit_only`
- strict paper-release gate passed: `False`
- strict paper-release gate log: `logs/paper_release_gate.log`
- LaTeX compile status: `skipped_unavailable`

## Snapshot Summary

- claims: `supported=7` `provisional=2` `blocked=0`
- figure manifests: `published=5` `partial=2` `blocked=0` `placeholder=1`
- manual paper assets blocked: `2`
- missing paper-facing references: `4`

## Included Snapshots

- `paper/`
- `paper/assets/asset_status.json`
- `review/claims/CLAIM_STATUS.md`
- `review/claims/claims_map.json`
- `review/paper_audit/paper_release_dossier.json`
- `review/paper_audit/paper_release_dossier.md`
- `results/figures/figure_index.json`
- `results/figures/*.figure.json`

## Missing Paper-Facing References

- `figs/system-arch.pdf` [manual_asset] reason=`source_missing`
- `figs/mermaid.png` [manual_asset] reason=`source_missing`
- `figs/fig3_hop_load.pdf` [evaluation_figure] reason=`partial`
- `figs/fig4_state_scaling.pdf` [evaluation_figure] reason=`partial`

## Supported Claim Evidence

- `CLM-EVAL-001` Paper-grade routing evaluation disables caching by default
  figures: none
  aggregates: results/aggregates/run_index.csv, results/aggregates/summary_rows.csv
  runs: smoke-iroute-star-s42-20260310a
- `CLM-EVAL-002` Legacy SANR workflow remains cache-enabled exploratory evidence
  figures: none
  aggregates: none
  runs: none
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
- `CLM-EVAL-006` Selective probing improves retrieval efficiency relative to high-overhead search
  figures: paper/figs/fig2_retrieval_cdf.pdf
  aggregates: results/aggregates/fig12-paper-grade-20260310a/reference_runs.csv, results/aggregates/fig12-paper-grade-20260310a/fig2_latency_summary.csv, results/aggregates/fig12-paper-grade-20260310a/fig12_paper_grade_report.json
  runs: fig12-paper-grade-20260310a-central_proc2_s42, fig12-paper-grade-20260310a-flood_budget8_s42, fig12-paper-grade-20260310a-iroute_M4_s42, fig12-paper-grade-20260310a-tag_tagK32_s42
- `CLM-EVAL-009` Recovery under churn and failures is supported by the published Fig. 5 bundle
  figures: paper/figs/fig5_recovery_churn.pdf, paper/figs/fig5_recovery_link-fail.pdf, paper/figs/fig5_recovery_domain-fail.pdf
  aggregates: results/aggregates/fig5-failure-paper-grade-final-20260311d/recovery_summary.csv, results/aggregates/fig5-failure-paper-grade-final-20260311d/failure_paper_grade_report.json
  runs: fig5-failure-paper-grade-final-20260311d-central-churn-s42, fig5-failure-paper-grade-final-20260311d-central-domain-fail-s42, fig5-failure-paper-grade-final-20260311d-central-link-fail-s42, fig5-failure-paper-grade-final-20260311d-flood-churn-s42, ... (9 total)

## Interpretation

- `release_ready` means the existing strict `paper-release-gate` passed when this bundle was created.
- `audit_only` means the bundle is still useful for submission review, but the repository did not satisfy the strict release gate at snapshot time.
