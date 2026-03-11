# Automation Workflow

## Gate Tiers

Use the repo-root gate commands directly:

```bash
bash scripts/workflow.sh dev-fast
bash scripts/workflow.sh merge-gate
bash scripts/workflow.sh paper-release-gate
```

Operational meaning:

- `dev-fast`: local quick check; shows current claim status without blocking on existing paper debt
- `merge-gate`: the PR-tier gate; already-`supported` claims must remain valid, but `provisional` and `blocked` claims may remain as known debt
- `paper-release-gate`: strict release gate; every mapped claim must be `supported` and all paper figures must be present
- strict paper release also requires non-evaluation paper assets tracked in `paper/assets/asset_status.json` to exist in `paper/figs/`

## Daily Flow

Use the repo-root entrypoint:

```bash
bash scripts/workflow.sh dev-fast
bash scripts/workflow.sh fig12-paper-grade --batch-id <batch-id>
bash scripts/workflow.sh fig345-paper-grade <load|scaling|failure|all> --suffix <suffix>
bash scripts/workflow.sh fig34-final-scope <load|scaling|all> --suffix <suffix>
bash scripts/workflow.sh fig34-final-scope load --suffix <suffix> --frequencies "20" --resume-existing
bash scripts/workflow.sh fig34-final-scope scaling --suffix <suffix> --domains-list "8" --schemes "iroute" --resume-existing
bash scripts/workflow.sh fig34-final-scope load --suffix <suffix> --finalize-only
bash scripts/workflow.sh publish-figure --figure-id <figure-id>
bash scripts/workflow.sh release-dossier
```

If you already have a compiled ns-3 smoke binary, also run:

```bash
bash scripts/workflow.sh smoke-run
bash scripts/workflow.sh artifact-check
```

The smoke run is a tiny plumbing validation only. It is not paper evidence.
The `fig12-paper-grade` command is the canonical rerun/promotion path for the current paper-grade Fig. 1 and Fig. 2 evidence bundle.
The `fig345-paper-grade` command is the canonical staging/promotion path for the current Fig. 3, Fig. 4, and Fig. 5 family bundles.
The `fig34-final-scope` command is the canonical final-scope rerun and publication path for Fig. 3 and Fig. 4. It promotes runs into `results/runs/`, rebuilds canonical aggregate bundles, generates figure bundles under `results/figures/`, and synchronizes the published PDFs into `paper/figs/` only after byte-for-byte verification.
For long-running final-scope batches, `fig34-final-scope` now supports shard selectors and resumable completion tracking. Use `--frequencies` for Fig. 3, `--domains-list` for Fig. 4, optional `--schemes` / `--seeds` for narrower shards, and `--finalize-only` to recompute completion without launching new runs.
Once a full final-scope batch is complete, Fig. 3 / Fig. 4 still need a release-gated publication pass through `publish-figure`. That gate checks the final-scope aggregate report, the batch completion index, the canonical run set, and the byte-for-byte sync into `paper/figs/`.
The `fig5-paper-grade` command is the canonical rerun, promotion, and publication path for the current Fig. 5 robustness bundle. It synchronizes published PDFs into `paper/figs/` only after every promoted failure run records an effective disruption and the figure files match byte-for-byte.
The `publish-figure` command is the generic paper-facing sync path for any figure manifest that already has sufficient provenance. It refuses to upgrade `blocked` or `placeholder` manifests, and it refuses to upgrade `partial` manifests unless their aggregate bundle is already marked publishable.
For Fig. 3 / Fig. 4 specifically, `publish-figure` is stricter: it refuses publication unless the final-scope batch is complete, and it only updates the machine-readable claim map when `--upgrade-claim-status` is passed explicitly.
The `release-dossier` command writes a JSON + Markdown submission snapshot under `review/paper_audit/`. It summarizes current claim status, figure publication status, manual asset debt, paper-facing missing files, and the key evidence references for currently supported claims.
Hand-maintained paper assets such as architecture diagrams do not use `publish-figure`; they stay tracked as explicit manual debt in `paper/assets/asset_status.json` until a real source or synchronized paper-facing file exists.

## Before Opening A PR

Run:

```bash
bash scripts/workflow.sh merge-gate
```

Notes:

- `merge-gate` keeps merge-blocking CI fast enough for normal PR use.
- if `ns-3/build/scratch/iroute-exp-baselines` is missing, `smoke-run` is skipped explicitly rather than pretending to pass.
- `paper-release-gate` is not required before every PR; it is the release-tier check.
- before an actual submission pass, generate `bash scripts/workflow.sh release-dossier` so the current release blockers and evidence set are visible in one place

## Paper-Grade Evidence

Paper-grade evidence means all of the following:

- the run comes from the cache-disabled paper workflow (`CACHE_MODE=disabled`, `CS_SIZE=0`, `PAPER_GRADE=1`)
- manifests record git commit, runner, output directory, input hashes, cache mode, and native seed provenance
- scaling runs do not use cloned seeds
- artifact regression passes on the produced outputs
- the figure set referenced by `paper/main.tex` exists and is traceable to the current workflow
- non-evaluation assets referenced by `paper/main.tex` either exist in `paper/figs/` or are explicitly listed as blocked manual debt in `paper/assets/asset_status.json`

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
- figures synchronized through the generic `publish-figure` step, with `paper_figure_in_sync=true`
- claims that are consistent with the regenerated evidence set

## CI Expectations

Current GitHub workflows are intentionally conservative:

- `repo-hygiene.yml` is a fast merge-blocking hygiene gate
- `experiment-checks.yml` is the merge-blocking experiment gate and runs `merge-gate`
- `paper-preflight.yml` is a manual paper-release gate and is expected to stay red while provisional or blocked claims remain
- the manual paper-release workflow should also upload the current release dossier so maintainers can inspect the exact blocker set from one artifact bundle

Later CI can add heavier manual jobs for full paper-grade execution and artifact upload, but that is not the default PR path.

## Failing CI Interpretation

- red `repo-hygiene.yml`: repository syntax or manifest hygiene is broken
- red `experiment-checks.yml`: merge-tier workflow integrity is broken, or a previously `supported` claim lost its evidence
- red `paper-preflight.yml`: expected when paper-release debt remains; treat it as release-readiness feedback, not as a routine merge blocker
- a red paper-release gate can come from missing evaluation figures, provisional claims, or missing hand-maintained paper assets

## Provisional Versus Blocked Claims

- `provisional`: there is some canonical evidence, but it is still incomplete, reduced-scope, or unsynchronized with `paper/figs/`
- `blocked`: the claim still lacks a publishable evidence path or contains a known hard defect in the current evidence bundle

Operationally:

- `provisional` and `blocked` claims do not stop normal merges by themselves
- `provisional` and `blocked` claims do stop `paper-release-gate`

## Issue And PR Governance

Use the repository-native templates under `.github/ISSUE_TEMPLATE/`:

- `experiment_task.yml`: experiment runs, reruns, promotion, aggregation, or validation work
- `paper_claim_gap.yml`: gaps between paper claims and current traceable evidence
- `workflow_refactor.yml`: automation, path, or repository-structure refactors

Operational expectations:

- experiment or paper issues should name affected run classes (`smoke`, `exploratory`, `paper_grade`) and, when known, the affected claim IDs
- paper-facing issues should record the exact aggregate CSVs, figure provenance files, and run IDs needed to close the gap
- workflow refactors should state whether they change evidence semantics or only code structure

PRs should use `.github/pull_request_template.md` and make evidence impact explicit:

- code-only cleanup PRs may leave run IDs / aggregates / figure provenance as `N/A`
- evidence-generating PRs should include concrete promoted run IDs and canonical aggregate / figure provenance paths
- if claims change status, update `review/claims/claims_map.json` and `review/claims/CLAIM_STATUS.md` in the same PR

Maintainers should treat `blocked-claim`, `provisional-claim`, and `rerun-required` labels as workflow debt markers rather than generic backlog tags. That keeps claim/evidence debt visible without weakening merge-tier automation.
