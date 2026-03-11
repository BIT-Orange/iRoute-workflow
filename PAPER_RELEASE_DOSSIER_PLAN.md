## Purpose

Add a repository-native paper release dossier that summarizes current release readiness from the existing evidence layer without changing any gate semantics.

The dossier is an informational companion to `paper-release-gate`, not a replacement for it.

## Dossier Contents

The generated dossier should include:

- claim counts by status (`supported`, `provisional`, `blocked`)
- claim lists grouped by status
- key evidence references for every `supported` claim
- figure manifest status summary from `results/figures/*.figure.json`
- published versus partial/blocked/placeholder figure families
- manual paper asset status from `paper/assets/asset_status.json`
- paper-facing files referenced by `paper/main.tex` that are still missing
- a concise release-readiness summary explaining why `paper-release-gate` would currently fail

Two outputs should be written:

- machine-readable JSON for automation and CI artifact upload
- human-readable Markdown for maintainers and paper review

## Status Semantics

- `supported` claims should be listed with the exact figure, aggregate, and run references already tracked in `review/claims/claims_map.json`
- `provisional` claims should be called out explicitly as release blockers for submission-grade evidence
- `blocked` claims should remain visible as hard evidence gaps
- figure manifests should be summarized using their existing status values only; the dossier must not upgrade or reinterpret them
- manual paper assets should be reported separately from evaluation figures because they are tracked in `paper/assets/asset_status.json`

## Relation To `paper-release-gate`

- `paper-release-gate` remains the authoritative strict gate
- the dossier is a readable snapshot explaining the current gate inputs
- a red paper-release gate is expected while provisional claims, blocked assets, or missing paper-facing files remain
- the dossier should therefore report current blockers clearly, but it must not claim the paper is ready unless the strict gate would actually pass

## Implementation Approach

1. Add `scripts/paper_release_dossier.py`
   - read `review/claims/claims_map.json`
   - scan `results/figures/*.figure.json`
   - read `paper/assets/asset_status.json`
   - scan `paper/main.tex` for `\includegraphics{...}` references
   - write `review/paper_audit/paper_release_dossier.json`
   - write `review/paper_audit/paper_release_dossier.md`

2. Add `bash scripts/workflow.sh release-dossier`
   - keep it separate from the strict gate
   - allow maintainers to regenerate the report locally at any time

3. Optionally attach the dossier to the manual GitHub Actions paper-release workflow
   - generate the dossier even if the strict gate fails
   - upload the JSON and Markdown as workflow artifacts

## Current Expected Output

On the current repository state, the dossier should make these points obvious:

- `CLM-EVAL-007` and `CLM-EVAL-008` are still `provisional`
- no claims are currently `blocked`
- `paper/figs/fig3_hop_load.pdf` and `paper/figs/fig4_state_scaling.pdf` are still missing
- manual assets `paper/figs/system-arch.pdf` and `paper/figs/mermaid.png` are still blocked
- the strict paper release path therefore remains not ready
