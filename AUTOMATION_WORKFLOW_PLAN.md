# Automation Workflow Plan

## Goals

- provide one documented local entrypoint for routine repository checks
- keep pull-request automation fast and reviewable
- separate smoke validation from paper-grade evidence generation
- make missing-tool fallbacks explicit instead of silently passing

## Check Matrix

### Run On Every PR

Fast checks that should execute for every pull request:

- shell syntax on workflow and runner scripts
- Python `py_compile` on check/manifests utilities
- JSON parse checks for static experiment manifests
- YAML parse checks for GitHub workflow files
- static runner/help checks for experiment utilities
- paper figure-reference preflight on `paper/main.tex`
- paper-suite static semantics checks
- smoke artifact regression only when a local ns-3 build is already available

### Smoke-Only Checks

These are lightweight runtime validations, not evidence production:

- `--help` / static invocation checks for experiment utilities
- tiny `iroute-exp-baselines` run on a local sample dataset
- artifact regression on the smoke output directory

Smoke checks are intended to catch broken plumbing, missing manifests, and obvious export regressions.

### Paper-Grade Checks

These are not default PR jobs because they are slower and evidence-sensitive:

- full `run_paper_suite.sh` execution
- regenerated figure bundle for `paper/main.tex`
- post-run artifact regression over the produced paper suite
- provenance review of manifests and figure index

Paper-grade runs remain a local/manual workflow in this stage.

## Local vs GitHub Actions

### Local Developer Commands

Use one repo-root entrypoint:

- `scripts/workflow.sh lint`
- `scripts/workflow.sh checks`
- `scripts/workflow.sh paper-preflight`
- `scripts/workflow.sh artifact-check`
- `scripts/workflow.sh smoke-run`
- `scripts/workflow.sh paper-suite-preflight`
- `scripts/workflow.sh ci-local`

Local commands may opportunistically run ns-3 smoke validation if a compiled binary already exists.

### GitHub Actions Jobs

Conservative CI jobs:

- `repo-hygiene.yml`
  - shell syntax
  - Python bytecode compile
  - JSON and YAML parse sanity
- `experiment-checks.yml`
  - static experiment checks
  - runner/help checks
  - paper-suite static semantics checks
  - smoke run plus artifact regression only if `ns-3/build` already contains the compiled smoke binary
- `paper-preflight.yml`
  - figure-reference preflight on `paper/main.tex`
  - optional LaTeX compile only when a TeX tool is available

## Missing Tool Policy

### ns-3 / waf unavailable

- static checks still run
- smoke execution is skipped with an explicit message
- skip is acceptable in CI for the lightweight workflow

### LaTeX unavailable

- `paper-preflight` still runs the figure-reference check
- TeX compile step is skipped with an explicit message

### YAML tool unavailable

- use a built-in fallback available on common systems (`ruby` YAML parser) rather than adding a dependency

## Merge Blocking Policy

Recommended merge-blocking checks:

- `repo-hygiene`
- `experiment-checks`

Informational for now:

- `paper-preflight`

Reason:

- the canonical paper tree still references missing figures, so the paper preflight should report actionable failures but should not yet be required for merge until the figure set is restored

## What The Local Entrypoint Should Enforce

- clear subcommand-oriented interface
- explicit skips for unavailable runtime environments
- no hidden full experiment execution
- no implicit paper-grade evidence generation

## Deferred CI Work

- dedicated paper-grade manual workflow (`workflow_dispatch`) with artifact upload
- cached ns-3 build job for deterministic smoke execution in CI
- LaTeX toolchain job once the canonical figure set is complete
- stronger fixture-based artifact regression beyond the tiny smoke output
