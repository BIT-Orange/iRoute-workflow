# SDM Smart City Dataset — Report
**Generated**: 2026-02-12T08:30:08.456312
**Seed**: 20260212  |  **vector_dim**: 128  |  **embed_model**: all-MiniLM-L6-v2
**k_domains**: 8  |  **n_per_type**: 600  |  **n_queries**: 500  |  **distractor_ratio**: 0.15
**BBOX**: lat [56.12, 56.2], lon [10.15, 10.25]

## Schema Sources
- **streetlight**: [Streetlight](https://github.com/smart-data-models/dataModel.Streetlighting/blob/master/Streetlight/schema.json) — repo `smart-data-models/dataModel.Streetlighting`
- **parking**: [OffStreetParking](https://github.com/smart-data-models/dataModel.Parking/blob/master/OffStreetParking/schema.json) — repo `smart-data-models/dataModel.Parking`
- **traffic**: [TrafficFlowObserved](https://github.com/smart-data-models/dataModel.Transportation/blob/master/TrafficFlowObserved/schema.json) — repo `smart-data-models/dataModel.Transportation`
- **pollution**: [AirQualityObserved](https://github.com/smart-data-models/dataModel.Environment/blob/master/AirQualityObserved/schema.json) — repo `smart-data-models/dataModel.Environment`

## Entity Statistics
| Type | Count | Distractors |
|---|---|---|
| streetlight | 600 | 84 |
| parking | 600 | 88 |
| traffic | 600 | 99 |
| pollution | 600 | 89 |
| **Total** | **2400** | **360** |

## Domain Balance
| Domain | streetlight | parking | traffic | pollution | Total |
|---|---|---|---|---|---|
| 0 | 74 | 74 | 70 | 92 | 310 |
| 1 | 98 | 99 | 106 | 99 | 402 |
| 2 | 50 | 50 | 50 | 50 | 200 |
| 3 | 50 | 50 | 55 | 50 | 205 |
| 4 | 50 | 50 | 50 | 57 | 207 |
| 5 | 97 | 98 | 106 | 100 | 401 |
| 6 | 84 | 80 | 73 | 64 | 301 |
| 7 | 97 | 99 | 90 | 88 | 374 |

## Query Statistics
| Family | Count | % |
|---|---|---|
| type_zone | 125 | 25.0% |
| enum_combo | 150 | 30.0% |
| paraphrase | 100 | 20.0% |
| cross_type | 50 | 10.0% |
| negative | 75 | 15.0% |

**Relevant-set sizes** (non-negative queries): min=1, median=10, max=206, mean=25.3

## Diversity Statistics
- **streetlight**: mean_cos=0.5749, p95_cos=0.7582, n=111
- **parking**: mean_cos=0.5584, p95_cos=0.7371, n=108
- **traffic**: mean_cos=0.5321, p95_cos=0.7324, n=112
- **pollution**: mean_cos=0.5587, p95_cos=0.7519, n=94
- **Lexical unique rate**: 0.9800

## Validation
- Schema validation: **PASS** (0 failures)
- vector_dim = 128: **PASS**
- Enum coverage gaps: 0

## Reproduce
```bash
cd /home/jiyuan/ndnSIM
python3 dataset/build_sdm_dataset.py \
  --out .ns-3/dataset/sdm_smartcity_dataset --k_domains 8 \
  --n_per_type 600 --n_queries 500 \
  --seed 20260212 --distractor_ratio 0.15 \
  --embed_model all-MiniLM-L6-v2
```
