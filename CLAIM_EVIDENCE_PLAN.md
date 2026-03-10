# Claim-Evidence Binding Plan

## Scope

This layer treats a "claim" as a stable paper statement that a reviewer could reasonably ask to trace back to repository artifacts.
For this repository, claims fall into three classes:

- `methodology`: statements about evaluation semantics or workflow configuration, such as cache-disabled paper-grade runs
- `positioning`: statements that constrain how a baseline or oracle should be interpreted
- `quantitative`: statements about measured correctness, latency, load, scaling, or failure behavior

## Acceptable Evidence

Acceptable evidence depends on claim type.

- `methodology` claims may be supported by:
  - stable paper markers or section anchors
  - runner scripts
  - static workflow manifests
  - canonical run manifests when a smoke or sample run is sufficient to show the mode is wired correctly
- `positioning` claims may be supported by:
  - stable paper markers or section anchors
  - the referenced figure existing
  - explicit repository workflow semantics that prevent a misleading interpretation
- `quantitative` claims require:
  - a referenced paper figure or explicitly acknowledged missing figure
  - at least one aggregate CSV that a reviewer can inspect
  - canonical run lineage
  - non-smoke evidence for any claim intended as publishable evaluation evidence
  - paper-grade runs before a claim can be upgraded to `supported`

## Status Levels

- `supported`: the claim has traceable artifacts, and the repository contains evidence at the level the paper text currently implies
- `provisional`: the claim is directionally tied to artifacts, but the evidence is incomplete, legacy, cache-mismatched, or not yet paper-grade
- `blocked`: the claim cannot presently be defended from repository artifacts because a figure, aggregate, or required rerun is missing

These statuses are conservative.
A claim is never upgraded to `supported` based on unclear provenance, smoke-only results, or legacy bundles that the paper itself already limits.

## Current Gaps

The current paper has three broad evidence states:

- Supported workflow and positioning claims:
  - paper-grade routing runs are cache-disabled by default
  - the SANR bundle used for archived Figs. 1 and 2 is cache-enabled and should not be read as routing-only evidence
  - centralized directory is treated as an oracle upper bound
  - Exact-NDN is positioned as a syntactic reference rather than a like-for-like intent-discovery comparator
- Provisional quantitative claims:
  - the Fig. 1 correctness/overhead tradeoff exists as an archived bundle, but not yet as a cache-disabled paper-grade aggregate
  - the Fig. 2 retrieval CDF exists as an archived bundle, but it remains cache-enabled
- Blocked quantitative claims:
  - Fig. 3 hop/load
  - Fig. 4 state scaling
  - Fig. 5 churn/link/domain recovery

## Maintenance Rules

- Every mapped claim must point to a stable paper anchor or `claim-id` marker in `paper/main.tex`.
- Every quantitative claim must name its intended aggregate CSVs and run classes, even if the current status is only `provisional` or `blocked`.
- Claims may only be upgraded from `provisional` or `blocked` after:
  - the figure exists
  - the aggregate exists
  - the canonical runs are present under `results/runs/`
  - the run class requirement is satisfied, typically `paper_grade`
- The validator is run in non-strict mode during exploratory work and in strict mode during paper-preflight or merge-gating contexts.
- When paper wording changes, the claim map must be updated in the same patch so the repository cannot silently drift.
