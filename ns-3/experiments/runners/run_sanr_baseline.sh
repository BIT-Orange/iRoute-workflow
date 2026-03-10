#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ROOT_DIR="$(cd "$NS3_DIR/.." && pwd)"
cd "$NS3_DIR"

mkdir -p "$NS3_DIR/.home"
export HOME="$NS3_DIR/.home"
if [ -d "$NS3_DIR/.venv/bin" ]; then
  export PATH="$NS3_DIR/.venv/bin:$PATH"
fi

PYTHON_BIN="python3"
if [ -x "$NS3_DIR/.venv/bin/python" ]; then
  PYTHON_BIN="$NS3_DIR/.venv/bin/python"
fi

BASE_DIR="${1:-results/sanr_baseline}"
ACC_DIR="$BASE_DIR/accuracy_comparison"
FAIL_DIR="$BASE_DIR/failure_stub"
FIG_DIR="$ROOT_DIR/figures/sanr_baseline"
WORKLOAD_DIR="$BASE_DIR/workload"

TOPO="${TOPO:-rocketfuel}"
TOPO_FILE="${TOPO_FILE:-src/ndnSIM/examples/topologies/as1239-r0.txt}"
SEEDS="${SEEDS:-42 43 44}"
TRACE_BASE="${TRACE_BASE:-dataset/sdm_smartcity_dataset/consumer_trace.csv}"
TRACE_REPEAT="${TRACE_REPEAT:-$WORKLOAD_DIR/consumer_trace_repeat.csv}"
QUERY_REPEAT_FACTOR="${QUERY_REPEAT_FACTOR:-3}"

mkdir -p "$WORKLOAD_DIR"

if [ ! -f "$TRACE_REPEAT" ]; then
  echo "[sanr] building repeat trace: $TRACE_REPEAT"
  "$PYTHON_BIN" - "$TRACE_BASE" "$TRACE_REPEAT" "$QUERY_REPEAT_FACTOR" <<'PY'
import csv
import os
import sys

src = sys.argv[1]
dst = sys.argv[2]
repeat = max(1, int(float(sys.argv[3])))

rows = []
with open(src, newline="", encoding="utf-8") as f:
    reader = csv.DictReader(f)
    header = reader.fieldnames
    rows = list(reader)

if not rows:
    raise SystemExit(f"empty trace: {src}")

target_n = len(rows)
out = []
idx = 0
while len(out) < target_n:
    row = rows[idx % len(rows)]
    for _ in range(repeat):
        out.append(dict(row))
        if len(out) >= target_n:
            break
    idx += 1

os.makedirs(os.path.dirname(dst), exist_ok=True)
with open(dst, "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=header)
    w.writeheader()
    w.writerows(out)
print(f"wrote {dst} rows={len(out)} repeat={repeat}")
PY
fi

echo "[sanr] running accuracy baseline (central/iroute/tag/sanr-tag/flood)"
TOPO="$TOPO" \
TOPO_FILE="$TOPO_FILE" \
SEEDS="$SEEDS" \
SCHEMES="central iroute flood tag sanr-tag" \
SANR_THRESH_VALUES="${SANR_THRESH_VALUES:-0.7 0.8 0.9 0.95}" \
SANR_REF_THRESH="${SANR_REF_THRESH:-0.8}" \
CS_SIZE="${CS_SIZE:-512}" \
DATA_FRESHNESS_MS="${DATA_FRESHNESS_MS:-60000}" \
TRACE="$TRACE_REPEAT" \
SHUFFLE_TRACE="${SHUFFLE_TRACE:-0}" \
"$SCRIPT_DIR/run_accuracy_experiment.sh" "$ACC_DIR"

mkdir -p "$FAIL_DIR"
mkdir -p "$FIG_DIR"

echo "[sanr] plotting figures"
MPLCONFIGDIR=.mplcache "$PYTHON_BIN" experiments/plot/plot_paper_figures.py \
  --acc-dir "$ACC_DIR" \
  --fail-dir "$FAIL_DIR" \
  --output "$FIG_DIR"

echo "[sanr] done"
echo "  accuracy: $ACC_DIR"
echo "  figures:  $FIG_DIR"
echo "  index:    $FIG_DIR/figure_index.md"
