# Fig. 3 / Fig. 4 Final-Scope Plan

## Goal

Upgrade the remaining provisional Fig. 3 and Fig. 4 claim families to `supported`, but only after the repository contains final-scope cache-disabled paper-grade runs, canonical aggregate bundles, canonical figure bundles, and synchronized paper-facing PDFs.

## What Final Scope Means

### Fig. 3: Hop Count Versus Offered Load

The current promoted bundle is only a focused minimal rerun:

- frequencies: `5 10 20`
- schemes: `central iroute tag flood`
- seeds: `42`
- status: pipeline validation only

Final-scope Fig. 3 should cover the full current paper-grade load sweep:

- frequencies: `1 2 5 10 20`
- schemes: `central iroute tag flood`
- seeds: `42`
- cache mode: `disabled`
- seed provenance: `native`
- topology: `rocketfuel`
- runtime: long enough to cover the full query trace without truncation

The important correction is runtime coverage:

- the current minimal load bundle used a short `SIM_TIME_MAX`
- the final bundle must let the runner compute enough simulation time for the full trace
- for the final-scope batch, `SIM_TIME_MAX=0` is used so the runner does not clip low-frequency points

### Fig. 4: State Scaling

The current promoted bundle is only a reduced-domain validation sweep:

- domains: `8 16`
- schemes: `central iroute tag flood`
- seeds: `42`

Final-scope Fig. 4 should match the current paper-grade suite scope:

- domains: `8 16 32 64`
- schemes: `central iroute tag flood`
- seeds: `42`
- cache mode: `disabled`
- seed provenance: `native`
- topology: `rocketfuel`
- no cloned seeds

This aligns with the current canonical suite semantics and with the repository's own hard checks requiring at least four domain points for Fig. 4.

## Required Run Matrices

### Fig. 3 Final Matrix

- `4` schemes x `5` load points x `1` seed = `20` promoted `paper_grade` runs
- batch prefix: `fig3-load-paper-grade-final`

### Fig. 4 Final Matrix

- `4` schemes x `4` domain points x `1` seed = `16` promoted `paper_grade` runs
- batch prefix: `fig4-scaling-paper-grade-final`

## Required Output Files Before Claim Upgrade

### Fig. 3

- `results/runs/fig3-load-paper-grade-final-<suffix>-*/`
- `results/aggregates/fig3-load-paper-grade-final-<suffix>/load_sweep.csv`
- `results/aggregates/fig3-load-paper-grade-final-<suffix>/load.csv`
- `results/aggregates/fig3-load-paper-grade-final-<suffix>/load_paper_grade_report.json`
- `results/figures/fig3-load-paper-grade-final-<suffix>/fig3_hop_load.pdf`
- `results/figures/fig3_hop_load.paper_grade.figure.json`
- `paper/figs/fig3_hop_load.pdf`

### Fig. 4

- `results/runs/fig4-scaling-paper-grade-final-<suffix>-*/`
- `results/aggregates/fig4-scaling-paper-grade-final-<suffix>/scaling.csv`
- `results/aggregates/fig4-scaling-paper-grade-final-<suffix>/scaling_paper_grade_report.json`
- `results/figures/fig4-scaling-paper-grade-final-<suffix>/fig4_state_scaling.pdf`
- `results/figures/fig4_state_scaling.paper_grade.figure.json`
- `paper/figs/fig4_state_scaling.pdf`

Claims may move to `supported` only if both the canonical figure bundle and the synchronized paper-facing PDF exist.

## Publication / Synchronization Policy

Publishing Fig. 3 or Fig. 4 into `paper/figs/` must be explicit:

- copy the figure from the canonical figure bundle under `results/figures/`
- verify the copied `paper/figs/*.pdf` matches the canonical bundle file byte-for-byte
- update the figure provenance manifest to `published` only after that verification succeeds

If synchronization fails, keep the figure manifest at `partial` and leave the claim `provisional`.

## Validation Required

- artifact regression for every promoted run in the final-scope batches
- claim-check in summary mode after claim-map updates
- paper figure preflight for `fig3_hop_load.pdf` and `fig4_state_scaling.pdf`
- a concise report that maps each figure to:
  - run IDs
  - aggregate bundle
  - canonical figure bundle
  - synchronized `paper/figs/` file
