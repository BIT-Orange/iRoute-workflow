# Paper Asset Gap Plan

## Scope

This phase only covers non-evaluation assets referenced by the canonical paper source at `paper/main.tex`.
Evaluation figures already use the `results/figures/` provenance workflow and are tracked separately.

## Referenced Non-Evaluation Assets

The canonical paper currently references these non-evaluation assets:

| Paper ref | Intended role | Current repo source status | Action in this phase |
| --- | --- | --- | --- |
| `figs/system-arch.pdf` | System architecture overview diagram | No source file or generator tracked in the repo | Add explicit blocked asset manifest entry and improve preflight reporting |
| `figs/mermaid.png` | Workflow / logic diagram used in the system overview section | No source file or generator tracked in the repo | Add explicit blocked asset manifest entry and improve preflight reporting |

## Existing Source Search Result

Repository inspection found:

- the canonical references in `paper/main.tex`
- legacy mentions in deprecated paper trees
- no tracked `drawio`, `mmd`, `svg`, `png`, `pdf`, `plantuml`, or other diagram source that can reproduce `system-arch` or `mermaid`

That means these assets cannot be treated as reproducibly generated repository outputs yet.

## Canonical Policy

- `results/figures/` remains the canonical home for generated evaluation figures and their provenance manifests.
- `paper/figs/` remains the canonical include tree for `paper/main.tex`.
- non-evaluation paper assets are tracked explicitly in `paper/assets/asset_status.json`
- missing manual assets must stay visible as blocked paper debt rather than implicit missing-file failures

## Implementation In This Phase

1. Add `paper/assets/asset_status.json` for non-evaluation paper assets.
2. Record, for each asset:
   - referenced paper path
   - status (`blocked` for the current missing assets; later `available` once the paper-facing file and source notes are real)
   - management mode (`manual`)
   - source existence / origin notes
   - legacy mentions if any
3. Update `check_paper_figures.py` so paper preflight distinguishes:
   - missing evaluation figures with figure provenance
   - missing non-evaluation paper assets with asset-status provenance
4. Update documentation so contributors know:
   - generated evaluation figures are published from `results/figures/`
   - architecture / diagram assets are hand-maintained
   - strict paper-release stays blocked until those manual assets exist

## What Is Not Done Here

- no diagram is fabricated
- no paper reference is removed just to make the gate green
- no new external rendering dependency is added without a tracked source file

## Follow-Up Once Sources Exist

If an author later adds a tracked source for `system-arch` or `mermaid`, the next safe step is:

1. store the source path in `paper/assets/asset_status.json`
2. add a reproducible generation or sync command
3. update paper preflight to validate that source-backed asset path
4. only then treat the asset as publishable rather than blocked manual debt
