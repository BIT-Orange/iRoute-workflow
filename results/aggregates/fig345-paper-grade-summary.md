# Fig. 3 / Fig. 4 / Fig. 5 Paper-Grade Summary

This note summarizes the current canonical paper-grade pipeline outputs for the remaining evaluation figure families.

## Fig. 3: Hop Count Versus Offered Load

- aggregate bundle: `results/aggregates/fig3-load-paper-grade-20260311a/`
- figure bundle: `results/figures/fig3-load-paper-grade-20260311a/`
- figure manifest: `results/figures/fig3_hop_load.paper_grade.figure.json`
- promoted runs: `12`
- status: `partial`
- limitations:
  - figure is generated under cache-disabled `paper_grade` semantics but is not yet synchronized into `paper/figs/fig3_hop_load.pdf`
  - bundle is a focused minimal rerun for pipeline validation rather than the final publishable load sweep

## Fig. 4: State Scaling

- aggregate bundle: `results/aggregates/fig4-scaling-paper-grade-20260311b/`
- figure bundle: `results/figures/fig4-scaling-paper-grade-20260311b/`
- figure manifest: `results/figures/fig4_state_scaling.paper_grade.figure.json`
- promoted runs: `8`
- status: `partial`
- limitations:
  - bundle currently covers the reduced `8/16`-domain validation sweep
  - figure is not yet synchronized into `paper/figs/fig4_state_scaling.pdf`
  - final-scope paper-grade scaling reruns are still required before publication

## Fig. 5: Recovery Under Churn And Failures

- aggregate bundle: `results/aggregates/fig5-failure-paper-grade-final-20260311d/`
- figure bundle: `results/figures/fig5-failure-paper-grade-final-20260311d/`
- figure manifests:
  - `results/figures/fig5_recovery_churn.paper_grade.figure.json`
  - `results/figures/fig5_recovery_link-fail.paper_grade.figure.json`
  - `results/figures/fig5_recovery_domain-fail.paper_grade.figure.json`
- promoted runs: `9`
- status: `published`
- notes:
  - the repaired bundle is synchronized into `paper/figs/fig5_recovery_{churn,link-fail,domain-fail}.pdf`
  - `recovery_summary.csv` now records `recovery_metric=domain_hit` explicitly and every promoted run has `failure_effective=1`
  - the older exploratory repair batch `fig5-failure-paper-grade-20260311c` is now superseded by this published canonical bundle
