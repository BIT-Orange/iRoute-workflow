# Automation Workflow

## Daily Flow

Use the repo-root entrypoint:

```bash
bash scripts/workflow.sh lint
bash scripts/workflow.sh checks
```

If you already have a compiled ns-3 smoke binary, also run:

```bash
bash scripts/workflow.sh smoke-run
bash scripts/workflow.sh artifact-check
```

The smoke run is a tiny plumbing validation only. It is not paper evidence.

## Before Opening A PR

Run:

```bash
bash scripts/workflow.sh lint
bash scripts/workflow.sh checks
bash scripts/workflow.sh paper-suite-preflight
bash scripts/workflow.sh smoke-run
bash scripts/workflow.sh artifact-check
bash scripts/workflow.sh paper-preflight
```

Notes:

- `paper-preflight` currently checks figure references first and only attempts LaTeX compilation if a TeX tool is available.
- if `ns-3/build/scratch/iroute-exp-baselines` is missing, `smoke-run` is skipped explicitly rather than pretending to pass
- if the canonical paper still references missing figures, `paper-preflight` fails and should be treated as an evidence gap

## Paper-Grade Evidence

Paper-grade evidence means all of the following:

- the run comes from the cache-disabled paper workflow (`CACHE_MODE=disabled`, `CS_SIZE=0`, `PAPER_GRADE=1`)
- manifests record git commit, runner, output directory, input hashes, cache mode, and native seed provenance
- scaling runs do not use cloned seeds
- artifact regression passes on the produced outputs
- the figure set referenced by `paper/main.tex` exists and is traceable to the current workflow

The cache-enabled SANR baseline workflow is useful for cache studies, but it is not paper-grade routing-only evidence.

Path policy:

- canonical dataset root: `dataset/`
- canonical results root: `results/`
- canonical generated figure root: `results/figures/`
- `ns-3/dataset/`, `ns-3/results/`, and root `figures/` are legacy compatibility locations

## Provisional vs Publishable Outputs

### Provisional

- smoke outputs under `/tmp`
- cache-enabled SANR figure bundles
- historical figures whose provenance still points to old absolute paths
- any run missing `run_manifest.json` lineage
- any figure referenced by the paper but absent from `paper/figs/`

### Publishable

- outputs regenerated from the paper-grade workflow with cache disabled
- manifests with complete lineage and native seed provenance
- figures present in `paper/figs/` and passing `paper-preflight`
- claims that are consistent with the regenerated evidence set

## CI Expectations

Current GitHub workflows are intentionally conservative:

- `repo-hygiene.yml` is a fast merge-blocking hygiene gate
- `experiment-checks.yml` is a fast merge-blocking experiment gate with opportunistic smoke validation
- `paper-preflight.yml` is informational until the canonical paper figure set is restored

Later CI can add heavier manual jobs for full paper-grade execution and artifact upload, but that is not the default PR path.
