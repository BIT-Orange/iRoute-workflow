# Paper Experiment Wrappers

This directory now contains compatibility wrappers for the historical paper workflow paths.

The active implementations live in:

- `../runners/paper_run_accuracy.sh`
- `../runners/paper_run_load.sh`
- `../runners/paper_run_scaling.sh`
- `../runners/paper_run_failure.sh`
- `../runners/paper_run_suite.sh`

From `ns-3/`, the old commands still work:

```bash
./experiments/paper/run_suite.sh
./experiments/paper/run_accuracy.sh
```

Paper wrappers are intended for cache-disabled paper-grade runs. If you need the cache-enabled SANR cache study workflow, use `./experiments/run_sanr_baseline.sh` instead.

Legacy pre-refactor scripts remain archived in `ns-3/experiments/archive_legacy_20260213/paper/`.
