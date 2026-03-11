## Goal

Publish Fig. 3 and Fig. 4 into `paper/figs/` and upgrade `CLM-EVAL-007` / `CLM-EVAL-008` only if the existing release-quality gate truly passes.

This phase does not relax the repository policy:

- older minimal bundles cannot be published
- publication must go through `publish-figure`
- claim upgrades must be explicit and auditable
- missing final-scope coverage must block publication

## Current Release Candidates

### Fig. 3

Current figure manifest:

- `results/figures/fig3_hop_load.paper_grade.figure.json`

Current manifest target:

- `results/figures/fig3-load-paper-grade-20260311a/fig3_hop_load.pdf`

Current final-scope aggregate candidate:

- `results/aggregates/fig3-load-paper-grade-final-20260311shard/`

Current final-scope batch state:

- `status = incomplete`
- completed sweep values: `20`
- missing sweep values: `1 2 5 10`
- missing canonical run count: `16`

Therefore the current repository does **not** yet contain a release-quality Fig. 3 candidate.

### Fig. 4

Current figure manifest:

- `results/figures/fig4_state_scaling.paper_grade.figure.json`

Current manifest target:

- `results/figures/fig4-scaling-paper-grade-20260311b/fig4_state_scaling.pdf`

Current final-scope aggregate candidate:

- `results/aggregates/fig4-scaling-paper-grade-final-20260311shard/`

Current final-scope batch state:

- `status = incomplete`
- completed sweep values: `(none)`
- missing sweep values: `8 16 32 64`
- missing canonical run count: `15`

Therefore the current repository does **not** yet contain a release-quality Fig. 4 candidate.

## Gate Checks

The Fig. 3 / Fig. 4 publication gate must verify all of the following:

1. the figure manifest points to a final-scope evidence bundle rather than the older minimal bundle
2. a matching final-scope aggregate report exists under:
   - `results/aggregates/fig3-load-paper-grade-final-*`
   - `results/aggregates/fig4-scaling-paper-grade-final-*`
3. the aggregate report status is `complete_unpublished` or `published`
4. a matching `batch_status.json` exists
5. `batch_status.json` reports:
   - full required sweep coverage
   - `missing_run_count = 0`
6. the figure file exists under `results/figures/`
7. publication can synchronize the paper-facing file into `paper/figs/`
8. byte-for-byte sync succeeds
9. claim upgrade is requested explicitly with `--upgrade-claim-status`

If any one of these checks fails, publication must abort and claim status must remain unchanged.

## Publication Provenance

If a future complete final-scope batch exists, successful publication should record:

- updated figure manifest status: `published`
- `paper_figure_path`
- `paper_figure_exists`
- `paper_figure_sha256`
- `paper_figure_in_sync`
- publication command metadata
- release-gate metadata tying the manifest back to:
  - the final aggregate report
  - the batch status file
  - the required canonical run set

Only after that should:

- `CLM-EVAL-007` move from `provisional` to `supported`
- `CLM-EVAL-008` move from `provisional` to `supported`
- `review/claims/CLAIM_STATUS.md` be regenerated to reflect the new summary

## Current Outcome

On the current repository state, the correct behavior is refusal:

- Fig. 3 still lacks a complete final-scope load matrix
- Fig. 4 still lacks a complete final-scope scaling matrix
- `paper/figs/fig3_hop_load.pdf` is absent
- `paper/figs/fig4_state_scaling.pdf` is absent

So the correct release action is:

- keep both figure manifests unpublished
- keep both claims provisional
- keep strict paper-release blocked on these two figure families
