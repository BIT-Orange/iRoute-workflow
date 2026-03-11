# Governance Labels

Use a small label set that matches the repository's evidence-first workflow.

## Core Labels

- `experiment`
  - experiment execution, aggregation, regression, or result-promotion work
- `paper`
  - paper text, figure references, claim wording, or release-readiness tasks
- `workflow`
  - CI, scripts, path migration, manifests, automation, or repo-structure changes
- `evidence`
  - any issue or PR that changes canonical runs, aggregates, figure provenance, or claim state

## Claim-State Labels

- `provisional-claim`
  - the task touches a claim that has some canonical evidence but is not yet fully publishable
- `blocked-claim`
  - the task addresses a claim that is still blocked by missing figures, missing reruns, or a hard evidence defect
- `rerun-required`
  - acceptance depends on a fresh canonical rerun or promotion, not only code edits

## Maintenance Label

- `debt`
  - cleanup or migration work that reduces ambiguity or risk without directly producing new evidence

## Suggested Maintainer Usage

- experiment pipeline issues:
  - `experiment`, optionally `evidence`, plus `rerun-required` if new runs are needed
- paper claim gap issues:
  - `paper`, `evidence`, and either `provisional-claim` or `blocked-claim`
- workflow refactors:
  - `workflow`, optionally `debt`, and `evidence` only if claim semantics or canonical outputs are affected

## Operational Notes

- do not use `evidence` as a generic importance label
- do not remove `blocked-claim` or `provisional-claim` until claim status is updated in `review/claims/`
- if a PR changes claim status, mirror the same label changes on the linked issue
