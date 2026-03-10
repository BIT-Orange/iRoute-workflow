# Results Layout

This top-level tree defines the future canonical home for curated experiment outputs and paper evidence.

- `runs/`: raw or lightly processed per-run outputs
- `aggregates/`: merged CSV or JSON summaries used across experiments
- `figures/`: curated figure assets promoted for paper or review use

Stage 1 keeps the current evidence set at `ns-3/results/sanr_baseline/` and the current figure bundle at `figures/` to avoid changing existing scripts and paper references in one patch.
