# Fig. 5 Failure Repair Report

## Root Cause

The blocked `iroute-churn-s42` run was not failing because the churn event was absent.
It was failing because `failure_effective` was computed from `is_success` and RTT only, while the Fig. 5 recovery path and legacy recovery helper both operate on `domain_hit`.

The repaired pipeline now:

- records `recovery_metric=domain_hit`
- writes richer `failure_sanity.csv` fields
- fails paper-grade failure runs if `failure_effective=0`

## Canonical Bundle

- aggregate bundle:
  - `results/aggregates/fig5-failure-paper-grade-final-20260311d/`
- figure bundle:
  - `results/figures/fig5-failure-paper-grade-final-20260311d/`
- published paper-facing figures:
  - `paper/figs/fig5_recovery_churn.pdf`
  - `paper/figs/fig5_recovery_link-fail.pdf`
  - `paper/figs/fig5_recovery_domain-fail.pdf`

## Promoted Runs

- `fig5-failure-paper-grade-final-20260311d-central-churn-s42`
- `fig5-failure-paper-grade-final-20260311d-central-domain-fail-s42`
- `fig5-failure-paper-grade-final-20260311d-central-link-fail-s42`
- `fig5-failure-paper-grade-final-20260311d-flood-churn-s42`
- `fig5-failure-paper-grade-final-20260311d-flood-domain-fail-s42`
- `fig5-failure-paper-grade-final-20260311d-flood-link-fail-s42`
- `fig5-failure-paper-grade-final-20260311d-iroute-churn-s42`
- `fig5-failure-paper-grade-final-20260311d-iroute-domain-fail-s42`
- `fig5-failure-paper-grade-final-20260311d-iroute-link-fail-s42`

## Key Evidence

- aggregate CSV:
  - `results/aggregates/fig5-failure-paper-grade-final-20260311d/recovery_summary.csv`
- figure provenance:
  - `results/figures/fig5_recovery_churn.paper_grade.figure.json`
  - `results/figures/fig5_recovery_link-fail.paper_grade.figure.json`
  - `results/figures/fig5_recovery_domain-fail.paper_grade.figure.json`

For the previously blocked churn row:

- `run_id=fig5-failure-paper-grade-final-20260311d-iroute-churn-s42`
- `failure_effective=1`
- `effective_reasons=domain_hit_drop;exact_hit_drop`
- `baseline=0.900000`
- `min_success=0.500000`
- `recovery_metric=domain_hit`

## Claim Outcome

- `CLM-EVAL-009`: `blocked -> supported`
