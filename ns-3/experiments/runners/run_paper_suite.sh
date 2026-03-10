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

ensure_safe_output_dir() {
  local dir="$1"
  local base
  base="$(basename "$dir")"
  if [[ "$base" =~ [0-9]{8}_[0-9]{6}$ ]] && [ -d "$dir" ] && [ -n "$(find "$dir" -mindepth 1 -print -quit 2>/dev/null)" ] && [ "${ALLOW_OVERWRITE_TIMESTAMPED_OUTPUT:-0}" != "1" ]; then
    echo "[suite][ERROR] refusing to overwrite non-empty timestamped output dir: $dir" >&2
    exit 2
  fi
  mkdir -p "$dir"
}

resolve_path() {
  local path="$1"
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
  else
    printf '%s\n' "$NS3_DIR/$path"
  fi
}

TS="$(date +%Y%m%d_%H%M%S)"
BASE_DIR="${1:-results/paper_${TS}}"
CACHE_MODE="${CACHE_MODE:-disabled}"
CS_SIZE="${CS_SIZE:-0}"
PAPER_GRADE="${PAPER_GRADE:-1}"

if [ "$PAPER_GRADE" = "1" ] && { [ "$CACHE_MODE" != "disabled" ] || [ "$CS_SIZE" -ne 0 ]; }; then
  echo "[suite][ERROR] paper-grade runs require CACHE_MODE=disabled and CS_SIZE=0" >&2
  exit 2
fi

ACC_DIR="$BASE_DIR/accuracy_comparison"
LOAD_DIR="$BASE_DIR/exp4-load"
SCALING_DIR="$BASE_DIR/exp4-scaling"
FAIL_DIR="$BASE_DIR/exp3-failure"
PLOT_DIR="$BASE_DIR/final_plots"

ensure_safe_output_dir "$BASE_DIR"

"$PYTHON_BIN" experiments/manifests/write_run_manifest.py \
  --repo-root "$ROOT_DIR" \
  --output "$BASE_DIR/run_manifest.json" \
  --workflow paper_suite \
  --runner ns-3/experiments/runners/run_paper_suite.sh \
  --output-dir "$(resolve_path "$BASE_DIR")" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/consumer_trace.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/domain_centroids_m4.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/producer_content.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/tag_index.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/query_to_tag.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/index_exact.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/qrels.tsv" \
  --input "$NS3_DIR/src/ndnSIM/examples/topologies/as1239-r0.txt" \
  --field "cache_mode=\"$CACHE_MODE\"" \
  --field "cs_size=$CS_SIZE" \
  --field "paper_grade=$PAPER_GRADE"

echo "[suite] building ..."
./waf

echo "[suite] running accuracy experiment ..."
TOPO=rocketfuel CACHE_MODE="$CACHE_MODE" CS_SIZE="$CS_SIZE" PAPER_GRADE="$PAPER_GRADE" "$SCRIPT_DIR/run_accuracy_experiment.sh" "$ACC_DIR"

echo "[suite] running load experiment ..."
TOPO=rocketfuel SEEDS=42 CACHE_MODE="$CACHE_MODE" CS_SIZE="$CS_SIZE" PAPER_GRADE="$PAPER_GRADE" "$SCRIPT_DIR/run_load_experiment.sh" "$LOAD_DIR"

echo "[suite] running scaling experiment ..."
TOPO=rocketfuel SEEDS=42 DOMAINS_LIST="8 16 32 64" CACHE_MODE="$CACHE_MODE" CS_SIZE="$CS_SIZE" PAPER_GRADE="$PAPER_GRADE" "$SCRIPT_DIR/run_scaling_experiment.sh" "$SCALING_DIR"

echo "[suite] running failure experiment ..."
TOPO=redundant FAILURE_POLICY=auto-noncut SEEDS=42 CACHE_MODE="$CACHE_MODE" CS_SIZE="$CS_SIZE" PAPER_GRADE="$PAPER_GRADE" "$SCRIPT_DIR/run_failure_experiment.sh" "$FAIL_DIR"

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
