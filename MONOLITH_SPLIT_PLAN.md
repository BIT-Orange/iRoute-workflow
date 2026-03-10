# Monolith Split Plan

## Scope

Target file: `/Users/jiyuan/Desktop/ndnSIM/ns-3/scratch/iroute-exp-baselines.cc`

This driver still mixes five concerns in one translation unit:

- experiment CLI and global parameter plumbing
- topology construction and failure/churn scheduling
- per-scheme execution (`iroute`, `exact`, `central`, `flood`, `tag`, `sanr-*`)
- workload/log conversion
- metric aggregation and artifact emission

The first safe extraction step is the metrics/export path because it is mostly data-shape logic with low coupling to ndnSIM internals.

## Proposed Module Boundaries

### 1. Result Model And Export Helpers

Candidate shared module:

- `QueryLog`
- `SummaryStats`
- `FailureSanityRecord`
- summary aggregation helpers
- CSV / JSON emission
- latency and failure sanity writers

Why first:

- these helpers operate on plain data records
- they do not need topology objects, application instances, or routing helpers
- they already sit near the end of the driver and form a cohesive output pipeline

### 2. Workload And Trace Preparation

Candidate extraction:

- query trace parsing
- shuffling / measurement-window helpers
- qrels and content preprocessing
- tag lookup preprocessing

Why second:

- still mostly data preparation
- currently reused across schemes
- can move behind a stable `ExperimentInputs` type without touching routing logic

### 3. Scheme Dispatch And Run Adapters

Candidate extraction:

- per-scheme `Run*` entrypoints
- common post-run checks
- scheme option normalization

Why third:

- this is the first place where ndnSIM object graphs and application wiring dominate
- extracting too early would risk semantic drift across baselines

### 4. Failure / Churn Scheduling

Candidate extraction:

- failure target selection
- link/domain/churn event injectors
- recovery timers
- failure sanity state mutation

Why fourth:

- behavior is stateful and timing-sensitive
- should only move after result/export code is stable and independently checked

### 5. Topology Construction And Shared Runtime Wiring

Candidate extraction:

- topology readers/builders
- ingress/gateway lookup helpers
- link delay and service jitter plumbing
- NDN stack installation and common app setup

Why last:

- broadest blast radius
- highest chance of changing runtime behavior

## Low-Risk Extraction Order

1. Move result structs and artifact writers into a shared helper.
2. Move summary/latency/failure aggregation into the same helper.
3. Keep the main driver responsible for filling config values and invoking the helper.
4. Keep workload parsing, scheme dispatch, and failure injection in the monolith for now.
5. After the helper is stable, extract trace preprocessing and per-scheme adapters.

## Symbols That Can Move First

Low-risk first movers with minimal semantic change:

- `QueryLog`
- `SummaryStats`
- `FailureSanityRecord`
- `ComputeSummary`
- `WriteQueryLog`
- `WriteSummary`
- `WriteLatencySanity`
- `WriteManifest`
- `WriteFailureSanity`

Keep in place for now:

- CLI/global option declarations
- topology creation helpers
- trace/content/qrels loaders
- `TxRecord` to `QueryLog` conversion
- `RunIRoute`, `RunExactMatch`, `RunCentralized`, `RunFlood`, `RunTag`, `RunSanrTag`, `RunSanrOracle`
- failure/churn injector functions

## Stage-3 Patch Implemented Here

This task performs only step 1:

- a shared header-only results helper is introduced under `ns-3/scratch/`
- the monolithic driver now calls the shared summary/export functions
- manifest emission gains explicit `cache_mode`, `run_mode`, and `seed_provenance` fields

No routing algorithm behavior or scheme wiring is changed in this patch.

## Deferred Work

- split trace parsing into a reusable workload module
- lift scheme-specific adapters out of the driver
- add dedicated C++ unit tests for result aggregation
- replace remaining globals with structured config objects
- move failure/churn scheduling behind explicit interfaces
