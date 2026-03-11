# Figure Index

Generated at: 2026-03-10T20:50:45

## fig1_accuracy_overhead.pdf
- schemes: ['central', 'iroute', 'tag', 'sanr-tag', 'flood']
- rows: 10
- oracle_flag: central included as oracle upper bound
- n_success: {'central': 425, 'iroute': 425, 'tag': 425, 'sanr-tag': None, 'flood': 389}
- timeout_rate: {'central': 0.0, 'iroute': 0.0, 'tag': 0.0, 'sanr-tag': None, 'flood': 0.084706}
- unique_rtt_values: {'central': 390, 'iroute': 265, 'tag': 398, 'sanr-tag': None, 'flood': 307}

## fig1_full_with_exact.pdf
- schemes: ['central', 'iroute', 'tag', 'sanr-tag', 'flood', 'exact']
- rows: 10
- oracle_flag: central included as oracle upper bound
- n_success: {'central': 425, 'iroute': 425, 'tag': 425, 'sanr-tag': None, 'flood': 389, 'exact': None}
- timeout_rate: {'central': 0.0, 'iroute': 0.0, 'tag': 0.0, 'sanr-tag': None, 'flood': 0.084706, 'exact': None}
- unique_rtt_values: {'central': 390, 'iroute': 265, 'tag': 398, 'sanr-tag': None, 'flood': 307, 'exact': None}

## fig2_cache_hit_ratio.pdf
- central: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-central_proc2_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'exact_hit_ratio': 0.0, 'semantic_hit_ratio': 0.0}
- iroute: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-iroute_M4_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'exact_hit_ratio': 0.0, 'semantic_hit_ratio': 0.0}
- tag: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-tag_tagK32_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'exact_hit_ratio': 0.0, 'semantic_hit_ratio': 0.0}
- flood: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-flood_budget8_s42/query_log.csv'], 'n_runs': 1, 'n_success': 419, 'n_success_cache_miss': 419, 'cache_hit_ratio': 0.0, 'exact_hit_ratio': 0.0, 'semantic_hit_ratio': 0.0}

## fig2_full_with_exact.pdf
- central: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-central_proc2_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.0, 'unique_rtt_values': 390, 'p50_ms': 9.608136, 'p95_ms': 13.919956799999998, 'oracle_flag': True}
- iroute: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-iroute_M4_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.0, 'unique_rtt_values': 272, 'p50_ms': 47.969936, 'p95_ms': 56.41306759999999, 'oracle_flag': False}
- tag: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-tag_tagK32_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.0, 'unique_rtt_values': 408, 'p50_ms': 48.635836, 'p95_ms': 53.6072068, 'oracle_flag': False}
- flood: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-flood_budget8_s42/query_log.csv'], 'n_runs': 1, 'n_success': 419, 'n_success_cache_miss': 419, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.014117647058823568, 'unique_rtt_values': 322, 'p50_ms': 50.633152, 'p95_ms': 57.2700282, 'oracle_flag': False}

## fig2_retrieval_cdf.pdf
- central: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-central_proc2_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.0, 'unique_rtt_values': 390, 'p50_ms': 9.608136, 'p95_ms': 13.919956799999998, 'oracle_flag': True}
- iroute: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-iroute_M4_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.0, 'unique_rtt_values': 272, 'p50_ms': 47.969936, 'p95_ms': 56.41306759999999, 'oracle_flag': False}
- tag: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-tag_tagK32_s42/query_log.csv'], 'n_runs': 1, 'n_success': 425, 'n_success_cache_miss': 425, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.0, 'unique_rtt_values': 408, 'p50_ms': 48.635836, 'p95_ms': 53.6072068, 'oracle_flag': False}
- flood: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-flood_budget8_s42/query_log.csv'], 'n_runs': 1, 'n_success': 419, 'n_success_cache_miss': 419, 'cache_hit_ratio': 0.0, 'timeout_rate': 0.014117647058823568, 'unique_rtt_values': 322, 'p50_ms': 50.633152, 'p95_ms': 57.2700282, 'oracle_flag': False}

## fig2b_latency_breakdown.pdf
- central: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-central_proc2_s42/query_log.csv'], 'n_runs': 1, 'disc_p50': 4.031032, 'fetch_p50': 5.37932, 'disc_p95': 6.6568879999999995, 'fetch_p95': 8.636929599999997}
- iroute: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-iroute_M4_s42/query_log.csv'], 'n_runs': 1, 'disc_p50': 22.03654, 'fetch_p50': 21.009756, 'disc_p95': 28.789409599999992, 'fetch_p95': 27.623646}
- tag: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-tag_tagK32_s42/query_log.csv'], 'n_runs': 1, 'disc_p50': 26.152116, 'fetch_p50': 21.902705, 'disc_p95': 28.2308166, 'fetch_p95': 26.73868}
- flood: {'runs': ['/Users/jiyuan/Desktop/ndnSIM/results/runs/fig12-paper-grade-20260310a-flood_budget8_s42/query_log.csv'], 'n_runs': 1, 'disc_p50': 27.752624, 'fetch_p50': 22.267764, 'disc_p95': 30.2726046, 'fetch_p95': 28.095244199999996}

## Warnings
- fig1_accuracy_overhead.pdf: iroute sweep points <4 (3)
- fig1_accuracy_overhead.pdf: flood sweep points <4 (3)
- fig1_accuracy_overhead.pdf: tag sweep points <4 (3)
- fig1_accuracy_overhead.pdf: missing scheme sanr-tag
- fig1_full_with_exact.pdf: iroute sweep points <4 (3)
- fig1_full_with_exact.pdf: flood sweep points <4 (3)
- fig1_full_with_exact.pdf: tag sweep points <4 (3)
- fig1_full_with_exact.pdf: missing scheme sanr-tag
- fig1_full_with_exact.pdf: missing scheme exact
- fig2_retrieval_cdf.pdf: missing reference run for sanr-tag
- fig2_full_with_exact.pdf: missing reference run for sanr-tag
- fig2_full_with_exact.pdf: missing reference run for exact
- appendix exact profile skipped: no exact reference run
- skip fig3: load data missing
- skip fig4: scaling data missing
- fig5_churn: no runs found
- fig5_link-fail: no runs found
- fig5_domain-fail: no runs found

