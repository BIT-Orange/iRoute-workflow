# Figure Index

Generated at: 2026-03-11T13:20:43

## fig5_recovery_churn.pdf
- topology: redundant
- failure_policy: auto-noncut
- metric: domain_hit
- central: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/central-churn-s42'], 'targets': ['central'], 'notes': ['churn_rounds=1;churn_interval_sec=4.000000;affected_domains=/domain1|/domain3;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 17, 'global_min': 1.0, 'global_t95': 15, 'global_baseline': 1.0, 'affected_min': 1.0, 'affected_t95': None, 'affected_baseline': 1.0, 'failure_hash': 'fe78d5bd242f95dd'}
- iroute: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/iroute-churn-s42'], 'targets': ['iroute'], 'notes': ['churn_rounds=1;churn_interval_sec=4.000000;affected_domains=/domain2|/domain5;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 13, 'global_min': 0.7000000000000001, 'global_t95': None, 'global_baseline': 0.9, 'affected_min': 0.6, 'affected_t95': None, 'affected_baseline': 0.6, 'failure_hash': '9bf5cf89fbd6a3d7'}
- flood: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/flood-churn-s42'], 'targets': ['flood'], 'notes': ['churn_rounds=1;churn_interval_sec=4.000000;affected_domains=/domain2|/domain3;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 10, 'global_min': 0.4666666666666666, 'global_t95': 15, 'global_baseline': 0.475, 'affected_min': 0.3, 'affected_t95': None, 'affected_baseline': 0.3, 'failure_hash': '861aa851624ca906'}

## fig5_recovery_domain-fail.pdf
- topology: redundant
- failure_policy: auto-noncut
- metric: domain_hit
- central: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/central-domain-fail-s42'], 'targets': ['domain0'], 'notes': ['apps_stopped=1;isolated_links=1;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 7, 'global_min': 1.0, 'global_t95': 15, 'global_baseline': 1.0, 'affected_min': 1.0, 'affected_t95': 14, 'affected_baseline': 1.0, 'failure_hash': '389d2a35fccf5bd6'}
- iroute: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/iroute-domain-fail-s42'], 'targets': ['domain1'], 'notes': ['apps_stopped=2;isolated_links=2;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 8, 'global_min': 0.09999999999999999, 'global_t95': None, 'global_baseline': 0.9, 'affected_min': 0.375, 'affected_t95': None, 'affected_baseline': 0.9375, 'failure_hash': 'c834a85f10662f0b'}
- flood: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/flood-domain-fail-s42'], 'targets': ['domain1'], 'notes': ['apps_stopped=2;isolated_links=2;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 8, 'global_min': 0.43333333333333335, 'global_t95': None, 'global_baseline': 0.475, 'affected_min': 0.125, 'affected_t95': 19, 'affected_baseline': 0.14583333333333331, 'failure_hash': '98973174a85d4612'}

## fig5_recovery_link-fail.pdf
- topology: redundant
- failure_policy: auto-noncut
- metric: domain_hit
- central: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/central-link-fail-s42'], 'targets': ['ingress-domain0'], 'notes': ['link_recovered;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 8, 'global_min': 1.0, 'global_t95': 15, 'global_baseline': 1.0, 'affected_min': 1.0, 'affected_t95': 14, 'affected_baseline': 1.0, 'failure_hash': '2fb12046fbd5bbee'}
- iroute: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/iroute-link-fail-s42'], 'targets': ['coreA-domain4'], 'notes': ['link_recovered;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 6, 'global_min': 0.6, 'global_t95': None, 'global_baseline': 0.9, 'affected_min': 0.6, 'affected_t95': None, 'affected_baseline': 1.0, 'failure_hash': '6cb638dbcdcbf759'}
- flood: {'runs': ['/tmp/fig5-failure-paper-grade-final-20260311d-stage/flood-link-fail-s42'], 'targets': ['coreA-domain4'], 'notes': ['link_recovered;metric=domain_hit'], 'n_runs_global': 1, 'n_runs_affected': 1, 'n_affected_total': 6, 'global_min': 0.43333333333333335, 'global_t95': None, 'global_baseline': 0.475, 'affected_min': 0.0, 'affected_t95': None, 'affected_baseline': 0.0, 'failure_hash': '861aa851624ca906'}

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

