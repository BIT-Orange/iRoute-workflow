# Fig.3 / Fig.4 Publication Gate Plan

## Scope

This phase does not publish Fig.3 / Fig.4 yet.
It adds the release-quality gate that must pass before either figure can move from `partial` to `published`.

## Publication Criteria

Fig.3 and Fig.4 can move from `partial` to `published` only when all of the following are true:

1. the figure manifest points to a final-scope aggregate bundle, not the older minimal bundle
2. the final-scope aggregate report status is `complete_unpublished` or `published`
3. the matching `batch_status.json` exists and is complete
4. the batch has no missing canonical runs
5. the required sweep coverage is complete:
   - Fig.3: frequencies `1 2 5 10 20`
   - Fig.4: domains `8 16 32 64`
6. the figure manifest `run_ids` match the full completed batch
7. the canonical figure file exists under `results/figures/`
8. the paper-facing file is synchronized into `paper/figs/` byte-for-byte

If any of those conditions is false, publication must fail.

## Publication And Claim Status

Claim upgrades are downstream of publication, not parallel to it.

- `CLM-EVAL-007` may become `supported` only after `fig3_hop_load.paper_grade` is published from a complete final-scope batch
- `CLM-EVAL-008` may become `supported` only after `fig4_state_scaling.paper_grade` is published from a complete final-scope batch

This phase wires that policy into the publication command:

- dry-run must fail early with precise reasons when the final-scope batch is incomplete
- successful publication may optionally update the machine-readable claim map via an explicit flag
- there is no silent claim upgrade

## Provenance Recording

On successful Fig.3 / Fig.4 publication, the figure manifest should record:

- paper-facing path under `paper/figs/`
- byte-for-byte sync state
- the final-scope aggregate report path and status
- the final-scope batch status path and status
- the linked claim ID

## Current Expected Validation Result

At current repository state, release-gated dry-runs for Fig.3 / Fig.4 should fail because:

- the tracked figure manifests still point to the older minimal bundles
- no complete final-scope Fig.3 / Fig.4 aggregate report exists yet
- `paper/figs/fig3_hop_load.pdf` and `paper/figs/fig4_state_scaling.pdf` are still absent
