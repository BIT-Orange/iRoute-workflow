# Experiment Fix Plan

## Scope

This patch repairs the highest-risk experiment integrity problems in the current iRoute workflow without redesigning the experiment architecture.

The fixes are intentionally local:

- keep the existing runner structure
- do not change routing algorithms
- do not regenerate the full paper evidence set
- add explicit metadata and checks around the risky workflow edges

## 1. Workload Repetition Semantics

### Intended semantics

The SANR baseline uses a repeated workload to create cache-relevant temporal locality while preserving the full source query set. A valid repeated workload must:

- include every source query at least once
- preserve the source query diversity
- make the repetition rule explicit
- make the final output length predictable from the source length and repeat factor

### Current bug

`ns-3/experiments/runners/run_sanr_baseline.sh` currently repeats each row but stops once the output length reaches the original trace length. With repeat factor `R > 1`, the output therefore keeps only the first `ceil(N / R)` source rows and drops the rest of the source trace entirely.

Example with `N=500` and `R=3`:

- intended repeated length: `1500`
- current repeated length: `500`
- effective unique coverage: only the first ~167 source queries survive

### Why the bug is dangerous

This biases the SANR cache-enabled workload toward an arbitrary prefix of the trace, distorts the query distribution, and invalidates any interpretation of cache-hit behavior as being based on the full workload.

### Planned fix

- add explicit repeat modes:
  - `row`: repeat each source row consecutively `R` times
  - `sequence`: concatenate the full source trace `R` times
- default SANR workflow to `row`, which preserves locality and full coverage
- always write workload stats that report:
  - original query count
  - repeated query count
  - unique query count before and after
  - whether the full source trace is covered

## 2. Multi-Seed Scaling Semantics

### Intended semantics

Scaling experiments that claim multi-seed evidence must execute each seed independently and record provenance for each seed-specific output.

### Current bug

`ns-3/experiments/runners/run_scaling_experiment.sh` silently copies `summary.csv` from seed 42 into other seeds when `CLONE_HIGH_DOMAIN=1`.

### Why cloning is unacceptable

Cloning one seed into other seed slots destroys the meaning of seed-to-seed variance:

- per-seed rows are no longer independent
- downstream averages and confidence intervals are contaminated
- later readers cannot distinguish computed results from copied placeholders

### Planned fix

- disable seed cloning by default
- block cloning in paper-grade mode
- keep cloning only as an explicit developer-only shortcut
- record seed provenance in output manifests and `scaling.csv`
- fail lineage checks if paper-grade scaling outputs contain cloned seeds

## 3. Cache-Enabled Vs Cache-Disabled Workflows

### Current inconsistency

The paper text says evaluation disables caching by default to isolate routing effects, but the recommended SANR workflow sets `CS_SIZE=512` and emits cache-hit figures. That makes it too easy to confuse:

- routing-only paper-grade runs
- cache-enabled SANR cache study runs

### Planned fix

- make cache mode explicit in runners via `CACHE_MODE` plus `CS_SIZE`
- treat paper-grade workflows as cache-disabled by default
- treat SANR baseline workflow as cache-enabled by default
- record cache mode and CS size in manifests
- update README text so the workflows are clearly separated

## 4. Manifest And Lineage Metadata

### Current gap

Static manifest descriptors are too thin for reproducibility. Current outputs do not consistently record:

- git commit
- key input hashes
- cache mode
- runner path
- seed provenance
- workload repetition semantics

### Planned fix

- add runtime `run_manifest.json` files for the repaired workflows
- include:
  - UTC timestamp
  - git commit hash
  - runner script path
  - output directory
  - cache mode and CS size
  - major options
  - key input file hashes
  - seed provenance for scaling runs
  - workload repetition stats path for SANR baseline
- strengthen static JSON manifests with workflow semantics and lineage expectations

## 5. Regression Checks

Minimal checks added in this patch:

- repeated-workload check that catches truncation or incomplete source coverage
- lineage check that rejects cloned seeds in paper-grade scaling mode
- manifest field check for cache mode and core lineage fields

## Deferred Work

- rerun the affected cache-enabled SANR workflow and paper-grade scaling workflow to regenerate evidence under the repaired semantics
- update paper prose and figure references only after the new outputs exist
- extend lineage manifests to load and failure workflows if those become part of paper-grade claims
