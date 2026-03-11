# Gate Policy Plan

## Goal

Introduce three explicit gate tiers so normal development stays fast while paper-release evidence remains strict:

- `developer-fast`
- `merge-blocking`
- `paper-release`

## Tier Definitions

### Developer-Fast

Purpose:

- quick local validation before or during normal editing
- no dependence on a built ns-3 binary
- no dependence on LaTeX

Checks:

- `lint`
- `checks`
- `paper-suite-preflight`
- non-strict claim summary

Claim policy:

- `supported`, `provisional`, and `blocked` claims are all allowed
- the command is informational; it surfaces current evidence debt without blocking iteration

### Merge-Blocking

Purpose:

- protect `main` from breaking repository hygiene, experiment plumbing, and already-supported evidence
- remain fast enough for every PR

Checks:

- everything in `developer-fast`
- claim validation in a merge-tier mode that fails only if an already-`supported` claim loses its evidence
- opportunistic `smoke-run`
- opportunistic `artifact-check`

Claim policy:

- `supported` claims must remain valid
- `provisional` claims may remain provisional
- `blocked` claims may remain blocked
- merge should not be blocked merely because the repository still has known paper-release debt

### Paper-Release

Purpose:

- validate whether the repository is ready to serve as a paper-release evidence snapshot

Checks:

- everything in `merge-blocking`
- strict claim validation (`all` mapped claims must be `supported`)
- paper figure preflight
- LaTeX compile if a TeX tool is available

Claim policy:

- `supported` claims must remain valid
- `provisional` claims fail the gate
- `blocked` claims fail the gate

## Local Versus CI

### Local commands

- `dev-fast`: default contributor quick check
- `merge-gate`: local approximation of merge-blocking CI
- `paper-release-gate`: local release check; expected to fail while paper-release debt remains

### GitHub Actions

- `repo-hygiene.yml`
  - PR / push
  - merge-blocking
  - runs fast hygiene only
- `experiment-checks.yml`
  - PR / push
  - merge-blocking
  - runs merge-tier workflow including supported-claim validation and opportunistic smoke/artifact checks
- `paper-preflight.yml`
  - manual `workflow_dispatch`
  - paper-release only
  - runs the strict release gate

## Skip Policy

- missing ns-3 build artifacts:
  - `smoke-run` and `artifact-check` skip explicitly
  - merge CI remains green if the static merge tier passes
- missing LaTeX tools:
  - release gate still runs figure and claim checks
  - compile step prints a skip message instead of silently passing

## Operational Interpretation

- green `developer-fast`: basic local hygiene and static semantics look sane
- green `merge-blocking`: safe to merge from a repository-integrity standpoint
- red `paper-release`: expected until all provisional and blocked claims are promoted to supported and all paper figures are synchronized
