# Fig.3/Fig.4/Fig.5 Paper-Grade Pipeline Plan

## Scope

This task builds the missing canonical pipeline for the remaining paper figures:

- Fig. 3: hop count versus offered load
- Fig. 4: routing-state scaling with bounded advertisements
- Fig. 5: recovery under churn, link failure, and domain failure

The goal is to make the run, aggregate, and figure provenance paths reproducible even if the current repository only contains minimal paper-grade examples rather than full final reruns.

## What Each Figure Is Intended To Show

### Fig. 3

- Fig. 3 (`fig3_hop_load.pdf`) is intended to show how hop count evolves as offered query load increases.
- It is driven by the load experiment family.
- Required inputs:
  - per-run `summary.csv`
  - per-run `query_log.csv`
  - aggregate `load_sweep.csv`
  - aggregate `load.csv`

### Fig. 4

- Fig. 4 (`fig4_state_scaling.pdf`) is intended to show that routing/control state scales with bounded per-domain advertisements.
- It is driven by the scaling experiment family.
- Required inputs:
  - per-run `summary.csv`
  - per-run `run_manifest.json`
  - aggregate `scaling.csv`

### Fig. 5

- Fig. 5 (`fig5_recovery_{churn,link-fail,domain-fail}.pdf`) is intended to show recovery behavior after churn and failure events, including minimum post-event success and `t95`.
- It is driven by the failure experiment family.
- Required inputs:
  - per-run `query_log.csv`
  - per-run `latency_sanity.csv`
  - per-run `failure_sanity.csv`
  - aggregate `recovery_summary.csv`

## What Is Missing Today

- `run_load_experiment.sh` does not yet emit per-run lineage manifests, which blocks canonical promotion into `results/runs/`.
- `run_failure_experiment.sh` likewise lacks per-run lineage manifests.
- There is no dedicated orchestration layer for:
  - running paper-grade load/scaling/failure batches
  - promoting per-run outputs into `results/runs/`
  - copying family aggregates into `results/aggregates/<batch-id>/`
  - generating figure provenance manifests in `results/figures/`
- The claim layer does not yet understand partial or blocked figure provenance manifests for Fig. 3/4/5.

## What Can Be Implemented Now

- Add per-run manifests to `load` and `failure` runners.
- Add a dedicated orchestration script for:
  - focused load batches
  - focused scaling batches
  - focused failure batches
- Promote minimal paper-grade examples into `results/runs/`.
- Generate family aggregate bundles under `results/aggregates/<batch-id>/`.
- Generate canonical figure PDFs into `results/figures/<batch-id>/` when plotting succeeds.
- Write machine-readable figure manifests with `partial` or `blocked` status when the bundle is not yet sufficient to support the paper claim.

## What Still Needs Full Reruns

- Fig. 3 still needs a broader paper-grade load sweep before the claim can be treated as supported.
- Fig. 4 still needs a broader scaling sweep across domain counts and seeds before the bounded-state claim can be treated as supported.
- Fig. 5 still needs the intended paper-grade recovery suite across scenarios and seeds before the robustness claim can be treated as supported.
- These figures should not be copied into `paper/figs/` or treated as publishable paper evidence until their figure manifests reach `published`.

## Conservative Status Policy

- `published`:
  - figure file exists
  - aggregate inputs exist
  - promoted runs exist
  - bundle scope matches the intended paper evidence
- `partial`:
  - figure file exists
  - aggregate inputs exist
  - promoted runs exist
  - bundle is real and paper-grade in semantics, but intentionally reduced or incomplete
- `blocked`:
  - plotting could not produce the figure, or no sufficient aggregate exists

## Validation Strategy

- Run one focused minimal paper-grade example for each family if feasible.
- Run artifact regression on promoted runs.
- Refresh aggregate and figure indexes.
- Run claim validation in summary mode.
- Keep claims blocked unless the new evidence is both real and complete enough to remove the current evidence gap.
