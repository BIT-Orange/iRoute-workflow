# Paper Submission Bundle Plan

## Goal

Add a repository-native submission bundle that freezes the current paper tree and evidence state into one auditable directory, without inventing release readiness.

## Bundle Contents

A submission bundle should capture both the paper-facing files and the evidence metadata needed to review the current submission state:

- canonical paper source tree from `paper/`
- synchronized `paper/figs/` as copied inside the paper tree snapshot
- current release dossier:
  - `review/paper_audit/paper_release_dossier.json`
  - `review/paper_audit/paper_release_dossier.md`
- current claim status snapshot:
  - `review/claims/CLAIM_STATUS.md`
  - `review/claims/claims_map.json`
- current figure provenance snapshot:
  - `results/figures/figure_index.json`
  - all `results/figures/*.figure.json` manifests
- current manual asset snapshot:
  - `paper/assets/asset_status.json`

The bundle should also include its own:

- `bundle_manifest.json`
- `BUNDLE_SUMMARY.md`
- strict gate log
- optional LaTeX compile log/output status

## Release Gate Coupling

The bundle generator must not define a second release policy.

- It must run the existing strict `paper-release-gate`.
- If the strict gate passes, the bundle may be marked `release_ready`.
- If the strict gate fails, the bundle must still be generated, but only as `audit_only`.
- The bundle generator must never upgrade an audit bundle to `release_ready` by itself.

## Evidence Snapshot Semantics

The bundle is a point-in-time snapshot.

- It should copy paper-facing artifacts exactly as they exist now.
- It should copy evidence manifests and claim status exactly as they exist now.
- It should preserve the distinction between:
  - published figures
  - partial figures
  - blocked manual assets
  - supported vs provisional claims

## Compile Behavior

If a local LaTeX tool is available, the bundle generator should attempt a best-effort compile of the copied paper tree and record:

- tool used
- success or failure
- log path

If no LaTeX tool is available, the bundle must record an explicit `skipped_unavailable` compile status.

Compile status is informative. Release readiness still comes from the strict gate.

## Output Location

Bundle outputs should live under:

- `review/paper_audit/submission_bundles/<bundle-id>/`

This keeps submission packaging near the release dossier rather than mixing it into canonical experiment results.

## Local Workflow

Add a repo-root command:

```bash
bash scripts/workflow.sh submission-bundle
```

Expected behavior:

- always regenerate the release dossier first
- always evaluate strict `paper-release-gate`
- always emit a bundle
- mark the bundle as:
  - `release_ready` only when the strict gate passes
  - `audit_only` otherwise

## CI Relationship

The manual paper-release workflow can optionally generate and upload a submission bundle artifact after the strict gate step.

- If the gate is red, the uploaded bundle still helps reviewers inspect the exact blocker set.
- If the gate is green, the uploaded bundle is a release-ready submission snapshot.
