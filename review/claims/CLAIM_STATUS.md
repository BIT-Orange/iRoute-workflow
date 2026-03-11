# Claim Status

Current mapped claims: `9`

Claim status only covers mapped experimental claims.
The strict paper-release gate can still fail on non-claim paper asset debt tracked in `paper/assets/asset_status.json`.

- `supported`: `7`
- `provisional`: `2`
- `blocked`: `0`

## Supported

- `CLM-EVAL-001`: paper-grade routing workflow disables caching by default
- `CLM-EVAL-002`: legacy SANR workflow remains cache-enabled exploratory evidence
- `CLM-EVAL-003`: centralized directory is positioned only as an oracle upper bound
- `CLM-EVAL-004`: Exact-NDN is a syntactic reference rather than an apples-to-apples intent-discovery comparator
- `CLM-EVAL-005`: bounded advertisements versus correctness/overhead
  - supported by the promoted cache-disabled batch `fig12-paper-grade-20260310a`
- `CLM-EVAL-006`: retrieval efficiency and latency/path interpretation
  - supported by the promoted cache-disabled Fig. 2 reference bundle `fig12-paper-grade-20260310a`
- `CLM-EVAL-009`: churn/link/domain failure recovery
  - supported by the published cache-disabled bundle `fig5-failure-paper-grade-final-20260311d`
  - paper-facing files are synchronized at `paper/figs/fig5_recovery_{churn,link-fail,domain-fail}.pdf`
  - every promoted paper-grade run in the bundle records `failure_effective=1` with explicit `effective_reasons`

## Provisional

- `CLM-EVAL-007`: hop count versus offered load
  - now backed by the promoted minimal paper-grade batch `fig3-load-paper-grade-20260311a`
  - still provisional because `paper/figs/fig3_hop_load.pdf` is not synchronized and the current bundle is a focused minimal rerun
  - the canonical final-scope rerun/publish entrypoint now exists at `bash scripts/workflow.sh fig34-final-scope load --suffix <suffix>`, but the full batch has not yet been completed and promoted
- `CLM-EVAL-008`: bounded routing-state scaling
  - now backed by the promoted minimal paper-grade batch `fig4-scaling-paper-grade-20260311b`
  - still provisional because `paper/figs/fig4_state_scaling.pdf` is not synchronized and the current bundle only covers the reduced 8/16-domain validation sweep
  - the canonical final-scope rerun/publish entrypoint now exists at `bash scripts/workflow.sh fig34-final-scope scaling --suffix <suffix>`, but the wider final batch has not yet been completed and promoted

## CI / Preflight Usage

- Exploratory summary:
  - `python3 ns-3/experiments/checks/check_claim_evidence.py --summary-only`
- Merge-tier gate:
  - `python3 ns-3/experiments/checks/check_claim_evidence.py --enforce-supported --summary-only`
- Paper-release gate:
  - `python3 ns-3/experiments/checks/check_claim_evidence.py --strict`

Merge-tier validation protects already-`supported` claims without blocking normal development on known provisional or blocked paper debt.
Strict mode should fail until all mapped claims are `supported`.
That is intentional: it prevents the paper from drifting ahead of the promoted figure and run evidence.
