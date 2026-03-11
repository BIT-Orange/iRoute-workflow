## Goal

Complete the remaining final-scope paper-grade execution for Fig. 3 and Fig. 4 using the existing sharded model, then finalize each family only if the full required matrix is present.

This plan does not relax any evidence policy:

- cache mode stays `disabled`
- `cs_size` stays `0`
- seed provenance stays `native`
- minimal bundles stay provisional
- no paper-facing publication occurs unless the final-scope batch is complete

## Current Shard State

### Fig. 3

Current final-scope batch:

- `results/aggregates/fig3-load-paper-grade-final-20260311shard/`

Already completed shard:

- `frequency-20`

Already promoted run IDs:

- `fig3-load-paper-grade-final-20260311shard-central_freq20_s42`
- `fig3-load-paper-grade-final-20260311shard-iroute_freq20_s42`
- `fig3-load-paper-grade-final-20260311shard-tag_freq20_s42`
- `fig3-load-paper-grade-final-20260311shard-flood_freq20_s42`

Still missing Fig. 3 frequency coverage:

- `1`
- `2`
- `5`
- `10`

The canonical full matrix for Fig. 3 is:

- frequencies: `1 2 5 10 20`
- schemes: `central iroute tag flood`
- seeds: `42`

That means Fig. 3 needs `20` canonical run IDs total.

### Fig. 4

Current final-scope batch:

- `results/aggregates/fig4-scaling-paper-grade-final-20260311shard/`

Already completed shard:

- `domains-8-schemes-iroute`

Already promoted run IDs:

- `fig4-scaling-paper-grade-final-20260311shard-iroute_d8_s42`

Still missing Fig. 4 domain coverage:

- `8` for `central tag flood`
- `16` for `central iroute tag flood`
- `32` for `central iroute tag flood`
- `64` for `central iroute tag flood`

The canonical full matrix for Fig. 4 is:

- domains: `8 16 32 64`
- schemes: `central iroute tag flood`
- seeds: `42`

That means Fig. 4 needs `16` canonical run IDs total.

## Exact Commands

### Fig. 3 remaining shards

Run each missing load point as its own resumable shard:

```bash
bash scripts/workflow.sh fig34-final-scope load --suffix 20260311shard --frequencies "1" --resume-existing
bash scripts/workflow.sh fig34-final-scope load --suffix 20260311shard --frequencies "2" --resume-existing
bash scripts/workflow.sh fig34-final-scope load --suffix 20260311shard --frequencies "5" --resume-existing
bash scripts/workflow.sh fig34-final-scope load --suffix 20260311shard --frequencies "10" --resume-existing
```

Recompute completion/finalization state after the matrix is present:

```bash
bash scripts/workflow.sh fig34-final-scope load --suffix 20260311shard --finalize-only
```

### Fig. 4 remaining shards

First fill the partial `domains=8` row across the missing schemes, then execute the higher domain rows:

```bash
bash scripts/workflow.sh fig34-final-scope scaling --suffix 20260311shard --domains-list "8" --resume-existing
bash scripts/workflow.sh fig34-final-scope scaling --suffix 20260311shard --domains-list "16" --resume-existing
bash scripts/workflow.sh fig34-final-scope scaling --suffix 20260311shard --domains-list "32" --resume-existing
bash scripts/workflow.sh fig34-final-scope scaling --suffix 20260311shard --domains-list "64" --resume-existing
```

Recompute completion/finalization state after the matrix is present:

```bash
bash scripts/workflow.sh fig34-final-scope scaling --suffix 20260311shard --finalize-only
```

## Completion Detection

Completion is not inferred from one aggregate file. It is detected from:

- `results/aggregates/<batch-id>/batch_status.json`
- `results/aggregates/<batch-id>/BATCH_STATUS.md`
- `results/aggregates/<batch-id>/shard_index.json`

A family is only ready for publication when:

- `status == complete_unpublished` or `published`
- `missing_run_count == 0`
- `missing_sweep_values == []`
- every required canonical run ID exists under `results/runs/`
- the final aggregate report has been rebuilt from the complete matrix

If any shard is still missing, the family remains `incomplete`, and claim status must remain provisional.

## Validation Steps

After each shard:

- the shard manifest must record `status=complete`
- promoted runs must pass artifact regression
- `batch_status.json` must update `present_run_count`, `missing_run_count`, and sweep coverage correctly

After finalization:

- Fig. 3 should produce a complete aggregate bundle under `results/aggregates/fig3-load-paper-grade-final-20260311shard/`
- Fig. 4 should produce a complete aggregate bundle under `results/aggregates/fig4-scaling-paper-grade-final-20260311shard/`
- canonical figure PDFs should exist under `results/figures/fig3-load-paper-grade-final-20260311shard/` and `results/figures/fig4-scaling-paper-grade-final-20260311shard/`
- those figures are only publication-ready at that point; they are not paper-facing until the separate release-gated `publish-figure` step succeeds
