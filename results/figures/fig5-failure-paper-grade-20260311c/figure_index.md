# Figure Index

Generated at: 2026-03-11T01:41:06

## fig5_recovery_churn.pdf
- topology: redundant
- failure_policy: auto-noncut
- metric: domain_hit
- central: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/central-churn-s42'], 'targets': ['central'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 63, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': 'fe78d5bd242f95dd'}
- iroute: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/iroute-churn-s42'], 'targets': ['iroute'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 63, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '9bf5cf89fbd6a3d7'}
- flood: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/flood-churn-s42'], 'targets': ['flood'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 63, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '861aa851624ca906'}

## fig5_recovery_domain-fail.pdf
- topology: redundant
- failure_policy: auto-noncut
- metric: domain_hit
- central: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/central-domain-fail-s42'], 'targets': ['domain0'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 7, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '389d2a35fccf5bd6'}
- iroute: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/iroute-domain-fail-s42'], 'targets': ['domain1'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 8, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': 'c834a85f10662f0b'}
- flood: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/flood-domain-fail-s42'], 'targets': ['domain1'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 8, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '98973174a85d4612'}

## fig5_recovery_link-fail.pdf
- topology: redundant
- failure_policy: auto-noncut
- metric: domain_hit
- central: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/central-link-fail-s42'], 'targets': ['ingress-domain0'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 8, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '2fb12046fbd5bbee'}
- iroute: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/iroute-link-fail-s42'], 'targets': ['coreA-domain4'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 6, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '6cb638dbcdcbf759'}
- flood: {'runs': ['/tmp/fig5-failure-paper-grade-20260311c-stage/flood-link-fail-s42'], 'targets': ['coreA-domain4'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 6, 'global_min': None, 'global_t95': None, 'global_baseline': None, 'affected_min': None, 'affected_t95': None, 'affected_baseline': None, 'failure_hash': '861aa851624ca906'}

## Warnings
- accuracy input empty
- fig2_retrieval_cdf.pdf: missing reference run for central
- fig2_retrieval_cdf.pdf: missing reference run for iroute
- fig2_retrieval_cdf.pdf: missing reference run for tag
- fig2_retrieval_cdf.pdf: missing reference run for sanr-tag
- fig2_retrieval_cdf.pdf: missing reference run for flood
- skip fig2_retrieval_cdf.pdf: no valid curves
- fig2_full_with_exact.pdf: missing reference run for central
- fig2_full_with_exact.pdf: missing reference run for iroute
- fig2_full_with_exact.pdf: missing reference run for tag
- fig2_full_with_exact.pdf: missing reference run for sanr-tag
- fig2_full_with_exact.pdf: missing reference run for flood
- fig2_full_with_exact.pdf: missing reference run for exact
- skip fig2_full_with_exact.pdf: no valid curves
- skip fig2_cache_hit_ratio: no rows
- appendix exact profile skipped: no exact reference run
- skip fig2b: no rows
- skip fig3: load data missing
- skip fig4: scaling data missing
- fig5_churn missing iRoute or INF-NDN

