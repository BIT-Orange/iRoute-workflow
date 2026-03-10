# Claim Status

Current mapped claims: `9`

- `supported`: `4`
- `provisional`: `2`
- `blocked`: `3`

## Supported

- `CLM-EVAL-001`: paper-grade routing workflow disables caching by default
- `CLM-EVAL-002`: archived Figs. 1 and 2 are cache-enabled SANR workflow evidence
- `CLM-EVAL-003`: centralized directory is positioned only as an oracle upper bound
- `CLM-EVAL-004`: Exact-NDN is a syntactic reference rather than an apples-to-apples intent-discovery comparator

## Provisional

- `CLM-EVAL-005`: bounded advertisements versus correctness/overhead
  - Fig. 1 exists, but the current archive is cache-enabled and not yet paper-grade
- `CLM-EVAL-006`: retrieval efficiency and latency/path interpretation
  - Fig. 2 exists, but the current archive is cache-enabled and no canonical paper-grade latency aggregate has been promoted

## Blocked

- `CLM-EVAL-007`: hop count versus offered load
  - blocked by missing `paper/figs/fig3_hop_load.pdf`
- `CLM-EVAL-008`: bounded routing-state scaling
  - blocked by missing `paper/figs/fig4_state_scaling.pdf` and lack of paper-grade scaling reruns
- `CLM-EVAL-009`: churn/link/domain failure recovery
  - blocked by missing `paper/figs/fig5_recovery_*.pdf` and lack of paper-grade failure reruns

## CI / Preflight Usage

- Exploratory summary:
  - `python3 ns-3/experiments/checks/check_claim_evidence.py --summary-only`
- Merge or paper-preflight gate:
  - `python3 ns-3/experiments/checks/check_claim_evidence.py --strict`

Strict mode should fail until all mapped claims are `supported`.
That is intentional: it prevents the paper from drifting ahead of the promoted figure and run evidence.
