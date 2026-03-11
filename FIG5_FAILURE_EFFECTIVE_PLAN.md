# Fig.5 Failure-Effective Repair Plan

## Scope

This task repairs the remaining blocked Fig. 5 evidence chain for `CLM-EVAL-009` in a conservative way.
The goal is not to broaden the robustness claim. The goal is to make the current paper-grade failure family internally consistent, measurable, and publishable only if the repaired evidence is real.

## What Fig. 5 Is Supposed To Measure

- Fig. 5 is the robustness panel family for:
  - churn
  - link failure
  - domain failure
- The current plotting path already builds Fig. 5 from `query_log.csv` recovery curves using `domain_hit`, not raw transport completion.
- The legacy `dataset/manifests/calc_recovery.py` helper also normalizes current logs through `domain_hit`.
- For the current repository, the most defensible interpretation is therefore:
  - Fig. 5 measures recovery of discovery correctness at the domain-hit level after disruption.
  - It is not a pure packet-delivery success-rate plot.

## Why `failure_effective=0` Happens For `iroute-churn-s42`

The churn event is being injected.
The promoted `fig5-failure-paper-grade-20260311c-iroute-churn-s42` run already records:

- `scheduled=1`
- `applied=1`
- `affected_apps=2`
- non-empty pre/post measurement windows

Query-level inspection shows the disruption is measurable:

- pre-event `domain_hit ≈ 0.90`
- post-event `domain_hit ≈ 0.72`
- pre-event `hit_exact ≈ 0.30`
- post-event `hit_exact ≈ 0.17`

However, the old `WriteFailureSanity()` logic defines `failure_effective` using only:

- change in `is_success`, or
- mean RTT increase above 5%

For the churn case, `is_success` stays at `1.0` and RTT barely moves, so the run is mislabeled as ineffective even though the Fig. 5 recovery metric (`domain_hit`) clearly degrades.

## Root Cause Classification

- event injection:
  - not the primary problem; the churn event is scheduled and applied
- target selection:
  - not the primary problem for the blocked run; the chosen churned domains are sufficient to change domain-hit behavior
- measurement windows:
  - not the primary problem; pre/post windows already contain enough samples
- summary logic:
  - primary problem
  - the `failure_effective` summary logic is inconsistent with the actual Fig. 5 metric and therefore under-reports effective churn

## Conservative Repair

1. Align failure-effectiveness evaluation with the current Fig. 5 evidence path.
   - compute pre/post `domain_hit`
   - retain explicit pre/post `is_success`, timeout, retransmission, and RTT evidence
   - mark a failure as effective when the disruption measurably changes discovery correctness and/or recovery-side latency behavior

2. Strengthen per-run failure evidence.
   - write richer `failure_sanity.csv` columns:
     - `pre_success`, `post_success`
     - `pre_domain_hit`, `post_domain_hit`
     - `pre_exact_hit`, `post_exact_hit`
     - `pre_timeout_rate`, `post_timeout_rate`
     - `pre_retrans_avg`, `post_retrans_avg`
     - `effective_reasons`
   - keep `pre_hit` / `post_hit` as the primary recovery metric and mark `metric=domain_hit` explicitly in notes

3. Make paper-grade failure runs fail loudly on ineffective disruptions.
   - `run_failure_experiment.sh` must stop if a paper-grade run records `failure_effective=0`
   - artifact regression must also fail for a paper-grade failure run with ineffective injection

4. Improve provenance for churn targeting.
   - record the affected churn domains in failure notes so the figure pipeline can distinguish global versus affected-domain curves instead of silently falling back to the full workload

## Required Reruns After The Fix

The current paper-suite failure family is the 9-run matrix:

- schemes:
  - `central`
  - `iroute`
  - `flood`
- scenarios:
  - `churn`
  - `link-fail`
  - `domain-fail`
- seed:
  - native `42`
- cache mode:
  - disabled

This task treats that 9-run family as the current paper-grade Fig. 5 scope because:

- `run_paper_suite.sh` already defines the paper-grade failure family with `SEEDS=42`
- the blocked status is caused by evidence inconsistency, not by a separately documented larger failure sweep

Future multi-seed robustness work can still be added later, but it is follow-up work rather than a prerequisite for repairing the current blocked claim.

## Publication Conditions

`CLM-EVAL-009` can be upgraded only if all of the following become true:

- the repaired failure family reruns successfully
- every promoted paper-grade Fig. 5 run records `failure_effective=1`
- canonical aggregate files exist under `results/aggregates/<batch-id>/`
- canonical figure PDFs exist under `results/figures/<batch-id>/`
- `paper/figs/fig5_recovery_churn.pdf`
- `paper/figs/fig5_recovery_link-fail.pdf`
- `paper/figs/fig5_recovery_domain-fail.pdf`
  are synchronized from the canonical figure bundle
- the figure provenance manifests can be marked `published`

If any of those conditions fail, the claim should remain `blocked` or, at most, `provisional`.
