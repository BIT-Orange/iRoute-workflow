# Canonical Run Outputs

Each promoted run lives under:

```text
results/runs/<run-id>/
```

Expected contents:

- `manifest.json`: canonical pipeline manifest
- `summary.csv`
- `query_log.csv`
- `latency_sanity.csv`
- `failure_sanity.csv` when applicable
- `source_manifest.json` and/or `source_run_manifest.json` when source manifests existed

Run classes:

- `smoke`: tiny plumbing validation only
- `exploratory`: ad hoc or legacy-imported runs
- `paper_grade`: explicitly curated evidence runs only

Promotion commands:

```bash
python3 scripts/paper_grade_pipeline.py run-smoke
python3 scripts/paper_grade_pipeline.py promote --source-dir ns-3/results/... --run-id <id> --run-class exploratory
```
