# Fig. 3 / Fig. 4 Final-Scope Status

## Current State

- final-scope orchestration entrypoint: `bash scripts/workflow.sh fig34-final-scope <load|scaling|all> --suffix <suffix>`
- claim state:
  - `CLM-EVAL-007`: `provisional`
  - `CLM-EVAL-008`: `provisional`

## Why Claims Remain Provisional

### Fig. 3

- existing canonical bundle:
  - `results/aggregates/fig3-load-paper-grade-20260311a/`
  - `results/figures/fig3-load-paper-grade-20260311a/`
- still missing:
  - a completed final-scope load rerun with the full `1 2 5 10 20` load sweep
  - a synchronized `paper/figs/fig3_hop_load.pdf`

### Fig. 4

- existing canonical bundle:
  - `results/aggregates/fig4-scaling-paper-grade-20260311b/`
  - `results/figures/fig4-scaling-paper-grade-20260311b/`
- still missing:
  - a completed final-scope scaling rerun covering the wider `8 16 32 64` domain sweep
  - a synchronized `paper/figs/fig4_state_scaling.pdf`

## Existing Provisional Mapping

### Fig. 3

- current aggregate bundle:
  - `results/aggregates/fig3-load-paper-grade-20260311a/`
- current figure provenance:
  - `results/figures/fig3_hop_load.paper_grade.figure.json`
- current paper-facing file:
  - `paper/figs/fig3_hop_load.pdf` does not exist

### Fig. 4

- current aggregate bundle:
  - `results/aggregates/fig4-scaling-paper-grade-20260311b/`
- current figure provenance:
  - `results/figures/fig4_state_scaling.paper_grade.figure.json`
- current paper-facing file:
  - `paper/figs/fig4_state_scaling.pdf` does not exist

## Operational Note

This report is intentionally conservative.
It records that the final-scope pipeline now exists, but the repository does not yet contain completed final-scope promoted evidence for Fig. 3 or Fig. 4.

## Sharded Execution

The final-scope entrypoint now supports resumable shards:

- Fig. 3 example:
  - `bash scripts/workflow.sh fig34-final-scope load --suffix <suffix> --frequencies "20" --resume-existing`
- Fig. 4 example:
  - `bash scripts/workflow.sh fig34-final-scope scaling --suffix <suffix> --domains-list "8" --schemes "iroute" --resume-existing`
- status-only recompute:
  - `bash scripts/workflow.sh fig34-final-scope <load|scaling> --suffix <suffix> --finalize-only`

Shard manifests live under `results/aggregates/<batch-id>/shards/`.
Batch completion is tracked in `results/aggregates/<batch-id>/batch_status.json`.

## Current Validation Batch

Validation suffix used in this phase: `20260311shard`

### Fig. 3 Validation

- batch id:
  - `fig3-load-paper-grade-final-20260311shard`
- completed shard:
  - `results/aggregates/fig3-load-paper-grade-final-20260311shard/shards/frequency-20.json`
- batch status:
  - `incomplete`
- missing frequency coverage:
  - `1 2 5 10`

This shows a shard can be complete and artifact-clean while the overall final-scope load batch remains incomplete.

### Fig. 4 Validation

- batch id:
  - `fig4-scaling-paper-grade-final-20260311shard`
- completed shard:
  - `results/aggregates/fig4-scaling-paper-grade-final-20260311shard/shards/domains-8-schemes-iroute.json`
- batch status:
  - `incomplete`
- missing domain coverage:
  - `8 16 32 64`

This is also intentional.
The completion checker still requires the full per-domain matrix across the canonical scheme set before final aggregation or publication is allowed.
