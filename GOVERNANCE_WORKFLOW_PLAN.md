# Governance Workflow Plan

## Required Issue Types

The repository needs three lightweight issue classes that map to the existing evidence-first workflow:

- `experiment_task`
  - for reruns, promotions, aggregation work, regression fixes, and figure-pipeline tasks
- `paper_claim_gap`
  - for mismatches between paper claims and currently traceable evidence
- `workflow_refactor`
  - for code-structure, automation, CI, path-migration, or manifest-lineage work that changes how the repo is operated

These map cleanly onto the current repo organization:

- experiment execution and promotion lives under `ns-3/experiments/`, `results/`, and `dataset/`
- claim tracking lives under `review/claims/`
- automation and CI live under `scripts/` and `.github/workflows/`

## PR Checklist Requirements

PRs should explicitly capture whether the change is:

- code cleanup only
- evidence-generating
- paper-text or claim-status affecting
- workflow or migration related

For evidence-generating PRs, the template should require:

- promoted run IDs
- canonical aggregate CSV paths
- figure provenance files or an explicit `N/A`
- whether reruns were required
- which claim IDs changed, if any

For code-only PRs, the template should still require:

- validation commands run
- whether evidence semantics changed
- whether claim tracking was intentionally untouched

## Recommended Labels

Suggested repository labels:

- `experiment`
- `paper`
- `evidence`
- `provisional-claim`
- `blocked-claim`
- `rerun-required`
- `workflow`
- `debt`

Operational meaning:

- `experiment`, `paper`, `workflow` classify the primary workstream
- `evidence` marks any issue or PR that changes canonical runs, aggregates, figure provenance, or claim state
- `provisional-claim` and `blocked-claim` mirror the current claim-evidence layer and keep paper debt visible
- `rerun-required` marks issues or PRs whose acceptance depends on fresh canonical runs
- `debt` marks tasks that reduce ambiguity, cleanup risk, or migration burden without directly producing evidence

## Claim Status And Task Tracking

Claim status should influence issue tracking directly:

- `supported` claims:
  - regressions are merge-sensitive
  - issues touching them should clearly state how support is preserved
- `provisional` claims:
  - issues should identify what exact run / aggregate / figure artifact is still missing
  - the label `provisional-claim` should stay until canonical evidence is complete
- `blocked` claims:
  - issues should describe the specific blocking defect or missing artifact
  - use `blocked-claim` plus `rerun-required` if fresh paper-grade runs are needed

## Manual Maintainer Enforcement

Maintainers should enforce a few lightweight rules manually:

- evidence-changing PRs must not merge without concrete canonical paths or an explicit statement that the PR is code-only
- claim status changes should update both `review/claims/claims_map.json` and `review/claims/CLAIM_STATUS.md`
- PRs that claim to fix paper evidence should show the exact run IDs, aggregate files, and figure provenance files used
- if a PR only improves tooling, it must not quietly relabel exploratory or legacy outputs as paper-grade

## Validation Expectations

The governance layer should stay repository-native and low-overhead:

- issue form YAML should parse cleanly
- the PR template should reference current commands such as `bash scripts/workflow.sh merge-gate`
- repo hygiene should validate `.github/ISSUE_TEMPLATE/*.yml` alongside workflow YAML
