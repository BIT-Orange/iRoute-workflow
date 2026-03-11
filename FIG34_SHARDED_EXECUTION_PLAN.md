# Fig.3 / Fig.4 Sharded Execution Plan

## Why The Current Final-Scope Runs Stall

The repository already has a final-scope entrypoint:

```bash
bash scripts/workflow.sh fig34-final-scope <load|scaling|all> --suffix <suffix>
```

But the full matrices are still expensive enough that one slow point can block the entire batch:

- Fig.3 final scope requires the full load sweep `1 2 5 10 20`
- Fig.4 final scope requires the full domain sweep `8 16 32 64`

The expensive tail points are the main problem:

- low-frequency Fig.3 runs stretch simulation time because the full trace is replayed over a longer interval
- high-domain Fig.4 runs amplify routing-state growth and make all-in-one reruns brittle

In a monolithic batch, an interruption means poor restart behavior and no durable record that a subset already completed cleanly.

## Shard Model

Final-scope Fig.3 / Fig.4 now run as resumable shards inside one canonical batch ID.

Canonical batch IDs remain:

- Fig.3: `fig3-load-paper-grade-final-<suffix>`
- Fig.4: `fig4-scaling-paper-grade-final-<suffix>`

Shard selectors are explicit:

- Fig.3:
  - `--frequencies "20"`
  - optional `--schemes "iroute tag"`
  - optional `--seeds "42"`
- Fig.4:
  - `--domains-list "8"`
  - optional `--schemes "iroute"`
  - optional `--seeds "42"`

Examples:

```bash
bash scripts/workflow.sh fig34-final-scope load --suffix 20260311x --frequencies "20" --resume-existing
bash scripts/workflow.sh fig34-final-scope scaling --suffix 20260311x --domains-list "8" --schemes "iroute" --resume-existing
```

## Resume / Retry

- shard-local stage dirs are deterministic:
  - `/tmp/iroute-fig34-final-scope/<batch-id>/<shard-id>/`
- `--resume-existing` reuses already-promoted canonical runs from `results/runs/`
- the underlying runner also receives `RESUME=1`, so retries can reuse shard-local outputs when the stage dir is still present

This keeps reruns practical without weakening provenance.

## Tracking Partial Completion

Each final-scope batch writes:

- `results/aggregates/<batch-id>/batch_config.json`
- `results/aggregates/<batch-id>/shards/<shard-id>.json`
- `results/aggregates/<batch-id>/shard_index.json`
- `results/aggregates/<batch-id>/batch_status.json`
- `results/aggregates/<batch-id>/BATCH_STATUS.md`

Semantics:

- shard status `complete` means the selected shard is present and passed artifact regression
- batch status `incomplete` means the full paper matrix is still missing required coverage
- claim status must remain `provisional` until the batch is complete and the paper-facing PDF is synchronized

## Final Aggregation Rule

Final aggregation is allowed only when the full required matrix is present.

Fig.3 requires:

- schemes: `central iroute tag flood`
- seeds: `42`
- frequencies: `1 2 5 10 20`

Fig.4 requires:

- schemes: `central iroute tag flood`
- seeds: `42`
- domains: `8 16 32 64`

If any required run is missing:

- batch status stays `incomplete`
- no final aggregate bundle is emitted as publishable evidence
- no paper-facing figure is synchronized
- the current minimal bundles remain the only partial evidence

If the full matrix is present:

- canonical aggregate CSVs are rebuilt under `results/aggregates/<batch-id>/`
- canonical figures are rebuilt under `results/figures/<batch-id>/`
- paper publication still requires the existing provenance checks

## Validation Strategy

This phase validates the shard model conservatively:

1. run one real Fig.3 shard
2. run one real Fig.4 shard
3. verify each shard manifest is `complete`
4. verify the overall batch status remains `incomplete`

That proves the workflow can make forward progress without falsely upgrading `CLM-EVAL-007` or `CLM-EVAL-008`.
