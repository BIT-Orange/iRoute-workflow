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

resolve_path() {
  local path="$1"
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
  else
    printf '%s\n' "$NS3_DIR/$path"
  fi
}

ensure_safe_output_dir() {
  local dir="$1"
  local base
  base="$(basename "$dir")"
  if [[ "$base" =~ [0-9]{8}_[0-9]{6}$ ]] && [ -d "$dir" ] && [ -n "$(find "$dir" -mindepth 1 -print -quit 2>/dev/null)" ] && [ "${ALLOW_OVERWRITE_TIMESTAMPED_OUTPUT:-0}" != "1" ]; then
    echo "[sanr][ERROR] refusing to overwrite non-empty timestamped output dir: $dir" >&2
    exit 2
  fi
  mkdir -p "$dir"
}

validate_cache_settings() {
  CACHE_MODE="${CACHE_MODE:-enabled}"
  if ! [[ "$CS_SIZE" =~ ^-?[0-9]+$ ]]; then
    echo "[sanr][ERROR] CS_SIZE must be an integer, got: $CS_SIZE" >&2
    exit 2
  fi
  case "$CACHE_MODE" in
    disabled)
      if [ "$CS_SIZE" -ne 0 ]; then
        echo "[sanr][ERROR] CACHE_MODE=disabled requires CS_SIZE=0, got $CS_SIZE" >&2
        exit 2
      fi
      ;;
    enabled)
      if [ "$CS_SIZE" -le 0 ]; then
        echo "[sanr][ERROR] CACHE_MODE=enabled requires CS_SIZE>0, got $CS_SIZE" >&2
        exit 2
      fi
      ;;
    *)
      echo "[sanr][ERROR] unsupported CACHE_MODE=$CACHE_MODE (expected disabled|enabled)" >&2
      exit 2
      ;;
  esac
}

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
TRACE_REPEAT_MODE="${TRACE_REPEAT_MODE:-row}"
WORKLOAD_STATS_JSON="${WORKLOAD_STATS_JSON:-$WORKLOAD_DIR/workload_repeat_stats.json}"
CS_SIZE="${CS_SIZE:-512}"

validate_cache_settings

ensure_safe_output_dir "$BASE_DIR"
mkdir -p "$WORKLOAD_DIR"

echo "[sanr] building repeated workload: $TRACE_REPEAT"
"$PYTHON_BIN" experiments/checks/repeat_workload.py \
  --source "$TRACE_BASE" \
  --output "$TRACE_REPEAT" \
  --repeat "$QUERY_REPEAT_FACTOR" \
  --mode "$TRACE_REPEAT_MODE" \
  --stats-out "$WORKLOAD_STATS_JSON" \
  --assert-valid

"$PYTHON_BIN" experiments/manifests/write_run_manifest.py \
  --repo-root "$ROOT_DIR" \
  --output "$BASE_DIR/run_manifest.json" \
  --workflow sanr_baseline \
  --runner ns-3/experiments/runners/run_sanr_baseline.sh \
  --output-dir "$(resolve_path "$BASE_DIR")" \
  --input "$(resolve_path "$TRACE_BASE")" \
  --input "$(resolve_path "$TRACE_REPEAT")" \
  --input "$(resolve_path "$TOPO_FILE")" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/domain_centroids_m4.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/producer_content.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/tag_index.csv" \
  --input "$NS3_DIR/dataset/sdm_smartcity_dataset/query_to_tag.csv" \
  --field "cache_mode=\"$CACHE_MODE\"" \
  --field "cs_size=$CS_SIZE" \
  --field "paper_grade=false" \
  --field "query_repeat_factor=$QUERY_REPEAT_FACTOR" \
  --field "trace_repeat_mode=\"$TRACE_REPEAT_MODE\"" \
  --field "workload_stats_json=\"$(resolve_path "$WORKLOAD_STATS_JSON")\"" \
  --field "seeds=\"$SEEDS\""

echo "[sanr] running accuracy baseline (central/iroute/tag/sanr-tag/flood)"
TOPO="$TOPO" \
TOPO_FILE="$TOPO_FILE" \
SEEDS="$SEEDS" \
SCHEMES="central iroute flood tag sanr-tag" \
SANR_THRESH_VALUES="${SANR_THRESH_VALUES:-0.7 0.8 0.9 0.95}" \
SANR_REF_THRESH="${SANR_REF_THRESH:-0.8}" \
CACHE_MODE="$CACHE_MODE" \
CS_SIZE="$CS_SIZE" \
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
echo "  workload stats: $WORKLOAD_STATS_JSON"
