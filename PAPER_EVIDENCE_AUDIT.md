# Paper Evidence Audit

Canonical paper source: `/Users/jiyuan/Desktop/ndnSIM/paper/main.tex`

## Figure Classification

### Referenced By The Paper

| Figure reference | Status | Notes |
| --- | --- | --- |
| `figs/system-arch.pdf` | missing | Referenced in the paper, not present under `paper/figs/`. |
| `figs/mermaid.png` | missing | Referenced in the paper, not present under `paper/figs/`. |
| `figs/fig1_accuracy_overhead.pdf` | present but provenance unclear | File exists locally, but the available `figure_index.md` records stale absolute source paths from another workspace. |
| `figs/fig2_retrieval_cdf.pdf` | inconsistent with current workflow semantics | Present, but the archived figure bundle comes from the cache-enabled SANR baseline workflow rather than the cache-disabled paper-grade routing workflow described in methodology. |
| `figs/fig3_hop_load.pdf` | missing | Figure file absent. Existing figure index explicitly says load data was skipped. |
| `figs/fig4_state_scaling.pdf` | missing | Figure file absent. Existing figure index explicitly says scaling data was skipped. |
| `figs/fig5_recovery_churn.pdf` | missing | Figure file absent. Existing figure index says no recovery runs were found. |
| `figs/fig5_recovery_link-fail.pdf` | missing | Figure file absent. Existing figure index says no recovery runs were found. |
| `figs/fig5_recovery_domain-fail.pdf` | missing | Figure file absent. Existing figure index says no recovery runs were found. |

### Present In The Repository But Not Referenced By `paper/main.tex`

- `figs/fig1_full_with_exact.pdf`
- `figs/fig2_cache_hit_ratio.pdf`
- `figs/fig2_full_with_exact.pdf`
- `figs/fig2b_latency_breakdown.pdf`

These files are useful review artifacts, but they are not the canonical figure set referenced by the current paper source.

## Provenance Findings

- The only available figure index is `/Users/jiyuan/Desktop/ndnSIM/figures/figure_index.md`.
- That index embeds absolute source paths from a different machine root (`/Users/jiyuan/OrbStack/...`), so provenance is not relocatable even when equivalent local result files exist.
- The available bundle is clearly a SANR baseline bundle and includes cache-hit figures, which means it cannot be read as a pure cache-disabled routing-only evidence set.

## Consistency Risks

### Cache semantics

- The paper methodology says caching is disabled by default to isolate routing effects.
- The available figure bundle includes cache-enabled SANR outputs and explicit cache-hit figures.
- Any latency/retrieval claim tied to the archived Fig. 1/2 bundle must therefore be labeled as cache-enabled workflow evidence unless regenerated under `CS_SIZE=0`.

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

- regenerate the full paper figure set from the cache-disabled paper-grade workflow
- repair provenance records so figure indices use current repository-relative paths
- add missing architecture and robustness figures
- re-run scaling and failure suites after the remaining monolith and regression work is complete
