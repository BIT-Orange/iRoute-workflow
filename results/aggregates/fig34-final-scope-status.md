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
