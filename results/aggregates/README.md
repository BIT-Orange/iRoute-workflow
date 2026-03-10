# Canonical Aggregates

Aggregates are rebuilt from `results/runs/` and should not be edited by hand.

Primary outputs:

- `run_index.csv`: one row per promoted canonical run
- `summary_rows.csv`: run metadata joined with `summary.csv`
- `paper_grade_summary_rows.csv`: paper-grade-only summary view
- `evidence_index.csv`: artifact inventory with hashes
- `aggregate_report.json`: counts and warnings

Rebuild with:

```bash
python3 scripts/paper_grade_pipeline.py aggregate
```
