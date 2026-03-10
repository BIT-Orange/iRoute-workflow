# Claims And Evidence

Use this folder to track scientific claims, the evidence expected to support them, and any validation gaps.

Current path context:

- canonical promoted evidence lives under `results/`
- canonical generated figure bundles live under `results/figures/`
- `ns-3/results/` and root `figures/` remain legacy compatibility locations and historical import sources
- the two reference PDFs remain at the repository root until a later curation pass relocates or inventories them

Current claim-binding files:

- `review/claims/claims_map.json`: machine-readable claim-to-evidence map
- `review/claims/CLAIM_STATUS.md`: human-readable summary of supported, provisional, and blocked claims
- `ns-3/experiments/checks/check_claim_evidence.py`: validator used in summary mode during exploration and strict mode during paper-preflight
