# Paper Evidence Audit

Canonical paper source: `/Users/jiyuan/Desktop/ndnSIM/paper/main.tex`

## Figure Classification

### Referenced By The Paper

| Figure reference | Status | Notes |
| --- | --- | --- |
| `figs/system-arch.pdf` | missing | Referenced in the paper, not present under `paper/figs/`. |
| `figs/mermaid.png` | missing | Referenced in the paper, not present under `paper/figs/`. |
| `figs/fig1_accuracy_overhead.pdf` | present and traceable | Canonical published provenance exists at `results/figures/fig1_accuracy_overhead.paper_grade.figure.json`. |
| `figs/fig2_retrieval_cdf.pdf` | present and traceable | Canonical published provenance exists at `results/figures/fig2_retrieval_cdf.paper_grade.figure.json`. |
| `figs/fig3_hop_load.pdf` | missing from paper tree | A canonical partial paper-grade figure exists under `results/figures/fig3-load-paper-grade-20260311a/`, but it is not yet synchronized into `paper/figs/`. |
| `figs/fig4_state_scaling.pdf` | missing from paper tree | A canonical partial paper-grade figure exists under `results/figures/fig4-scaling-paper-grade-20260311b/`, but it is not yet synchronized into `paper/figs/`. |
| `figs/fig5_recovery_churn.pdf` | missing from paper tree | A canonical partial paper-grade figure exists under `results/figures/fig5-failure-paper-grade-20260311c/`, but the current churn bundle still has an ineffective iRoute disruption and is not publishable. |
| `figs/fig5_recovery_link-fail.pdf` | missing from paper tree | A canonical partial paper-grade figure exists under `results/figures/fig5-failure-paper-grade-20260311c/`, but it is not yet synchronized into `paper/figs/`. |
| `figs/fig5_recovery_domain-fail.pdf` | missing from paper tree | A canonical partial paper-grade figure exists under `results/figures/fig5-failure-paper-grade-20260311c/`, but it is not yet synchronized into `paper/figs/`. |

### Present In The Repository But Not Referenced By `paper/main.tex`

- `figs/fig1_full_with_exact.pdf`
- `figs/fig2_cache_hit_ratio.pdf`
- `figs/fig2_full_with_exact.pdf`
- `figs/fig2b_latency_breakdown.pdf`

These files are useful review artifacts, but they are not the canonical figure set referenced by the current paper source.

## Provenance Findings

- Canonical published provenance now exists for Fig. 1 and Fig. 2 under `results/figures/*.paper_grade.figure.json`.
- Canonical partial provenance now exists for Fig. 3, Fig. 4, and Fig. 5 under `results/figures/*.paper_grade.figure.json`.
- The old root `figures/figure_index.md` remains a legacy artifact and should no longer be treated as the authoritative evidence index.
- Fig. 3/4/5 remain non-publishable because the paper-facing `paper/figs/` files are still absent and the Fig. 5 minimal churn batch still contains an ineffective iRoute disruption.

## Consistency Risks

### Cache semantics

- The paper methodology says caching is disabled by default to isolate routing effects.
- Fig. 1 and Fig. 2 are now regenerated from the cache-disabled paper-grade workflow.
- The legacy SANR bundle remains cache-enabled exploratory evidence and must not be conflated with the canonical paper-grade figure family.

### Centralized directory baseline

- The repository already treats `central` as an oracle-like reference in figure metadata.
- The paper needs the same language in prose so readers do not mistake it for a deployable decentralized baseline.

### Exact-NDN comparisons

- Exact-NDN is a syntactic reference that assumes stronger naming knowledge than intent-based discovery schemes.
- Latency and state comparisons are therefore not apples-to-apples when topology knowledge or object-name knowledge differs.

## High-Confidence Paper Changes Applied In This Task

- explicitly label centralized directory results as an oracle upper bound
- separate cache-enabled SANR workflow language from cache-disabled routing-only evaluation language
- add a caution that Exact-NDN is a syntactic reference rather than a directly fair latency/state comparator under heterogeneous naming

## Deferred Evidence Work

- synchronize Fig. 3 and Fig. 4 into `paper/figs/` only after the final-scope paper-grade reruns complete
- correct and rerun the Fig. 5 churn batch so the iRoute disruption is effective before treating the robustness claim as publishable
- add missing architecture figures (`system-arch.pdf`, `mermaid.png`)
- keep repository-relative provenance as the only accepted figure lineage path
