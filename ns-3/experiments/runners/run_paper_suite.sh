#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ROOT_DIR="$(cd "$NS3_DIR/.." && pwd)"
EXPERIMENTS_DIR="$NS3_DIR/experiments"
cd "$NS3_DIR"

mkdir -p "$NS3_DIR/.home"
export HOME="$NS3_DIR/.home"
if [ -d "$NS3_DIR/.venv/bin" ]; then
  export PATH="$NS3_DIR/.venv/bin:$PATH"
fi
export MPLBACKEND=Agg
PYTHON_BIN="python3"
if [ -x "$NS3_DIR/.venv/bin/python" ]; then
  PYTHON_BIN="$NS3_DIR/.venv/bin/python"
fi

TS="$(date +%Y%m%d_%H%M%S)"
BASE_DIR="${1:-results/paper_${TS}}"

ACC_DIR="$BASE_DIR/accuracy_comparison"
LOAD_DIR="$BASE_DIR/exp4-load"
SCALING_DIR="$BASE_DIR/exp4-scaling"
FAIL_DIR="$BASE_DIR/exp3-failure"
PLOT_DIR="$BASE_DIR/final_plots"

mkdir -p "$BASE_DIR"

echo "[suite] building ..."
./waf

echo "[suite] running accuracy experiment ..."
TOPO=rocketfuel "$SCRIPT_DIR/run_accuracy_experiment.sh" "$ACC_DIR"

echo "[suite] running load experiment ..."
TOPO=rocketfuel SEEDS=42 "$SCRIPT_DIR/run_load_experiment.sh" "$LOAD_DIR"

echo "[suite] running scaling experiment ..."
TOPO=rocketfuel SEEDS=42 DOMAINS_LIST="8 16 32 64" "$SCRIPT_DIR/run_scaling_experiment.sh" "$SCALING_DIR"

echo "[suite] running failure experiment ..."
TOPO=redundant FAILURE_POLICY=auto-noncut SEEDS=42 "$SCRIPT_DIR/run_failure_experiment.sh" "$FAIL_DIR"

echo "[suite] checking result sanity ..."
"$PYTHON_BIN" "$EXPERIMENTS_DIR/checks/check_results.py" --base-dir "$BASE_DIR"

echo "[suite] plotting paper figures ..."
"$PYTHON_BIN" "$EXPERIMENTS_DIR/plot/plot_paper_figures.py" \
  --acc-dir "$ACC_DIR" \
  --fail-dir "$FAIL_DIR" \
  --load-csv "$LOAD_DIR/load_sweep.csv" \
  --scaling-csv "$SCALING_DIR/scaling.csv" \
  --output "$PLOT_DIR"

PAPER_FIG_DIR="$ROOT_DIR/figures"
mkdir -p "$PAPER_FIG_DIR"
cp "$PLOT_DIR"/fig*.pdf "$PAPER_FIG_DIR"/
if [ -f "$PLOT_DIR/figure_index.md" ]; then
  cp "$PLOT_DIR/figure_index.md" "$PAPER_FIG_DIR/figure_index.md"
fi

echo "[suite] done"
echo "[suite] results: $BASE_DIR"
echo "[suite] figures: $PLOT_DIR"
echo "[suite] copied to: $PAPER_FIG_DIR"
