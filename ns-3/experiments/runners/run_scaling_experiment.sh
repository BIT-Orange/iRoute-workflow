#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ROOT_DIR="$(cd "$NS3_DIR/.." && pwd)"
source "$ROOT_DIR/scripts/iroute-paths.sh"

NS3_DIR="$IROUTE_NS3_ROOT"
ROOT_DIR="$IROUTE_REPO_ROOT"
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
    echo "[scaling][ERROR] refusing to overwrite non-empty timestamped output dir: $dir" >&2
    exit 2
  fi
  mkdir -p "$dir"
}

validate_cache_settings() {
  CACHE_MODE="${CACHE_MODE:-disabled}"
  if ! [[ "$CS_SIZE" =~ ^-?[0-9]+$ ]]; then
    echo "[scaling][ERROR] CS_SIZE must be an integer, got: $CS_SIZE" >&2
    exit 2
  fi
  case "$CACHE_MODE" in
    disabled)
      if [ "$CS_SIZE" -ne 0 ]; then
        echo "[scaling][ERROR] CACHE_MODE=disabled requires CS_SIZE=0, got $CS_SIZE" >&2
        exit 2
      fi
      ;;
    enabled)
      if [ "$CS_SIZE" -le 0 ]; then
        echo "[scaling][ERROR] CACHE_MODE=enabled requires CS_SIZE>0, got $CS_SIZE" >&2
        exit 2
      fi
      ;;
    *)
      echo "[scaling][ERROR] unsupported CACHE_MODE=$CACHE_MODE (expected disabled|enabled)" >&2
      exit 2
      ;;
  esac
}

RESULT_DIR="$(iroute_resolve_results_path "${1:-exp4-scaling}")"
TOPO="${TOPO:-rocketfuel}"
TOPO_FILE="$(iroute_resolve_topology_file "${TOPO_FILE:-src/ndnSIM/examples/topologies/as1239-r0.txt}")"
INGRESS_NODE="${INGRESS_NODE:-0}"
LINK_DELAY_MS="${LINK_DELAY_MS:-2.0}"
LINK_DELAY_JITTER_US="${LINK_DELAY_JITTER_US:-0}"
SERVICE_JITTER_US="${SERVICE_JITTER_US:-1000}"
TRACE="${TRACE:-$(iroute_resolve_dataset_file "sdm_smartcity_dataset/consumer_trace.csv")}"
SHUFFLE_TRACE="${SHUFFLE_TRACE:-1}"
CENTROIDS="${CENTROIDS:-$(iroute_resolve_dataset_file "sdm_smartcity_dataset/domain_centroids_m4.csv")}"
if [ ! -f "$CENTROIDS" ]; then
  CENTROIDS="$(iroute_resolve_dataset_file "sdm_smartcity_dataset/domain_centroids.csv")"
fi
CONTENT="${CONTENT:-$(iroute_resolve_dataset_file "sdm_smartcity_dataset/producer_content.csv")}"
QRELS="${QRELS:-$(iroute_resolve_dataset_file "sdm_smartcity_dataset/qrels.tsv")}"
TAG_INDEX="${TAG_INDEX:-$(iroute_resolve_dataset_file "sdm_smartcity_dataset/tag_index.csv")}"
QUERY_TO_TAG="${QUERY_TO_TAG:-$(iroute_resolve_dataset_file "sdm_smartcity_dataset/query_to_tag.csv")}"

DOMAINS_LIST="${DOMAINS_LIST:-8 16 32 64 128}"
SCHEMES="${SCHEMES:-central iroute tag sanr-tag flood}"
M_VALUE="${M_VALUE:-4}"
TAG_K="${TAG_K:-32}"
TAG_LSA_PERIOD_MS="${TAG_LSA_PERIOD_MS:-20000}"
SANR_SIM_THRESH="${SANR_SIM_THRESH:-0.8}"
SANR_MSRR_TOP_PCT="${SANR_MSRR_TOP_PCT:-0.2}"
SANR_CMLT_SEC="${SANR_CMLT_SEC:-4.0}"
SANR_SLOT_SEC="${SANR_SLOT_SEC:-5.0}"
SANR_CCN_K="${SANR_CCN_K:-1}"
SANR_TOP_L="${SANR_TOP_L:-32}"
K_VALUE="${K_VALUE:-5}"
TAU_VALUE="${TAU_VALUE:-0.3}"
SEEDS="${SEEDS:-42 43 44}"
SIM_TIME="${SIM_TIME:-120}"
FREQUENCY="${FREQUENCY:-5}"
WARMUP_SEC="${WARMUP_SEC:-20}"
MEASURE_START_SEC="${MEASURE_START_SEC:-20}"
DATA_FRESHNESS_MS="${DATA_FRESHNESS_MS:-60000}"
CS_SIZE="${CS_SIZE:-0}"
RESUME="${RESUME:-1}"
PAPER_GRADE="${PAPER_GRADE:-1}"
CLONE_HIGH_DOMAIN="${CLONE_HIGH_DOMAIN:-0}"
CLONE_HIGH_DOMAIN_MIN="${CLONE_HIGH_DOMAIN_MIN:-64}"
CLONE_HIGH_DOMAIN_FROM_SEED="${CLONE_HIGH_DOMAIN_FROM_SEED:-42}"
ALLOW_FAKE_SEED_CLONE="${ALLOW_FAKE_SEED_CLONE:-0}"

validate_cache_settings
if [ "$PAPER_GRADE" = "1" ] && [ "$CLONE_HIGH_DOMAIN" = "1" ]; then
  echo "[scaling][ERROR] paper-grade scaling runs forbid CLONE_HIGH_DOMAIN=1" >&2
  exit 2
fi
if [ "$CLONE_HIGH_DOMAIN" = "1" ] && [ "$ALLOW_FAKE_SEED_CLONE" != "1" ]; then
  echo "[scaling][ERROR] seed cloning is a developer-only shortcut; set ALLOW_FAKE_SEED_CLONE=1 and PAPER_GRADE=0 to enable it" >&2
  exit 2
fi

ensure_safe_output_dir "$RESULT_DIR"
OUTPUT_CSV="$RESULT_DIR/scaling.csv"
echo "run_id,scheme,domains,seed,M,tagK,topology,cache_mode,cs_size,paper_grade,seed_provenance,lsdb_entries,fib_entries,lsdb_theory,result_dir,manifest_path" > "$OUTPUT_CSV"

"$PYTHON_BIN" experiments/manifests/write_run_manifest.py \
  --repo-root "$ROOT_DIR" \
  --output "$RESULT_DIR/run_manifest.json" \
  --workflow scaling_experiment \
  --runner ns-3/experiments/runners/run_scaling_experiment.sh \
  --output-dir "$(resolve_path "$RESULT_DIR")" \
  --input "$(resolve_path "$TRACE")" \
  --input "$(resolve_path "$CENTROIDS")" \
  --input "$(resolve_path "$CONTENT")" \
  --input "$(resolve_path "$QRELS")" \
  --input "$(resolve_path "$TAG_INDEX")" \
  --input "$(resolve_path "$QUERY_TO_TAG")" \
  --input "$(resolve_path "$TOPO_FILE")" \
  --field "cache_mode=\"$CACHE_MODE\"" \
  --field "cs_size=$CS_SIZE" \
  --field "run_mode=\"scaling_aggregate\"" \
  --field "seed_provenance=\"aggregate\"" \
  --field "paper_grade=$PAPER_GRADE" \
  --field "clone_high_domain=$CLONE_HIGH_DOMAIN" \
  --field "clone_high_domain_min=$CLONE_HIGH_DOMAIN_MIN" \
  --field "clone_high_domain_from_seed=$CLONE_HIGH_DOMAIN_FROM_SEED" \
  --field "allow_fake_seed_clone=$ALLOW_FAKE_SEED_CLONE"

for scheme in $SCHEMES; do
  for domains in $DOMAINS_LIST; do
    for seed in $SEEDS; do
      run_id="${scheme}_d${domains}_s${seed}"
      run_dir="$RESULT_DIR/$run_id"
      manifest_path="$run_dir/run_manifest.json"
      seed_provenance="native"
      have_summary=0
      echo "[scaling] running scheme=$scheme domains=$domains seed=$seed"
      mkdir -p "$run_dir"

      if [ -f "$run_dir/summary.csv" ]; then
        have_summary=1
      fi

      if [ "$CLONE_HIGH_DOMAIN" = "1" ] && [ "$domains" -ge "$CLONE_HIGH_DOMAIN_MIN" ] && [ "$seed" != "$CLONE_HIGH_DOMAIN_FROM_SEED" ] && [ "$have_summary" = "0" ]; then
        src_run_dir="$RESULT_DIR/${scheme}_d${domains}_s${CLONE_HIGH_DOMAIN_FROM_SEED}"
        if [ -f "$src_run_dir/summary.csv" ]; then
          cp -f "$src_run_dir/summary.csv" "$run_dir/summary.csv"
          seed_provenance="cloned-from-seed-${CLONE_HIGH_DOMAIN_FROM_SEED}"
          have_summary=1
          echo "[scaling][WARN] cloned summary from seed=$CLONE_HIGH_DOMAIN_FROM_SEED -> seed=$seed for scheme=$scheme domains=$domains"
        fi
      fi

      extra=""
      lsdb_theory=0
      if [ "$scheme" = "iroute" ]; then
        extra="--M=$M_VALUE"
        lsdb_theory=$((domains * M_VALUE))
      elif [ "$scheme" = "tag" ]; then
        extra="--M=$M_VALUE --tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K"
        lsdb_theory=$((domains * TAG_K))
      elif [ "$scheme" = "sanr-tag" ]; then
        extra="--M=$M_VALUE --tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K --sanrSimThresh=$SANR_SIM_THRESH --msrrTopPct=$SANR_MSRR_TOP_PCT --cmltSec=$SANR_CMLT_SEC --slotSec=$SANR_SLOT_SEC --ccnK=$SANR_CCN_K --sanrTopL=$SANR_TOP_L"
        lsdb_theory=$((domains * TAG_K))
      elif [ "$scheme" = "flood" ]; then
        extra="--M=$M_VALUE --floodResponder=producer --floodThreshold=0.6 --floodProbeBudget=4"
        lsdb_theory=0
      elif [ "$scheme" = "central" ]; then
        extra="--qrels=$QRELS --dirMode=oracle --dirTopK=5 --dirProcMs=2"
        lsdb_theory=0
      else
        echo "[scaling][WARN] skip unsupported scheme=$scheme"
        continue
      fi

      if [ "$RESUME" = "1" ] && [ -f "$run_dir/summary.csv" ]; then
        if [ "$PAPER_GRADE" = "1" ] && [ ! -f "$manifest_path" ]; then
          echo "[scaling][ERROR] refusing to paper-grade resume unmanifested summary: $run_dir/summary.csv" >&2
          exit 2
        fi
        if [ -f "$manifest_path" ]; then
          seed_provenance="$("$PYTHON_BIN" - "$manifest_path" <<'PY'
import json
import sys
path = sys.argv[1]
data = json.load(open(path, encoding="utf-8"))
print(data.get("fields", {}).get("seed_provenance", "native"))
PY
)"
        elif [ "$seed_provenance" = "native" ]; then
          seed_provenance="unverified-existing"
        fi
        echo "[scaling] resume scheme=$scheme domains=$domains seed=$seed provenance=$seed_provenance"
      else
        ./waf --run "iroute-exp-baselines --scheme=$scheme --domains=$domains --K=$K_VALUE --tau=$TAU_VALUE \
--simTime=$SIM_TIME --frequency=$FREQUENCY --seed=$seed \
--topo=$TOPO --topoFile=$TOPO_FILE --ingressNode=$INGRESS_NODE \
--linkDelayMs=$LINK_DELAY_MS --linkDelayJitterUs=$LINK_DELAY_JITTER_US \
--serviceJitterUs=$SERVICE_JITTER_US \
--dataFreshnessMs=$DATA_FRESHNESS_MS \
--csSize=$CS_SIZE \
--trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --resultDir=$run_dir \
--shuffleTrace=$SHUFFLE_TRACE \
--warmupSec=$WARMUP_SEC --measureStartSec=$MEASURE_START_SEC --cdfSuccessOnly=1 --failureTargetPolicy=manual $extra" \
          > "$run_dir.log" 2>&1
      fi

      "$PYTHON_BIN" experiments/manifests/write_run_manifest.py \
        --repo-root "$ROOT_DIR" \
        --output "$manifest_path" \
        --workflow scaling_run \
        --runner ns-3/experiments/runners/run_scaling_experiment.sh \
        --output-dir "$(resolve_path "$run_dir")" \
        --input "$(resolve_path "$TRACE")" \
        --input "$(resolve_path "$CENTROIDS")" \
        --input "$(resolve_path "$CONTENT")" \
        --input "$(resolve_path "$QRELS")" \
        --input "$(resolve_path "$TAG_INDEX")" \
        --input "$(resolve_path "$QUERY_TO_TAG")" \
        --input "$(resolve_path "$TOPO_FILE")" \
        --field "run_id=\"$run_id\"" \
        --field "scheme=\"$scheme\"" \
        --field "seed=$seed" \
        --field "domains=$domains" \
        --field "cache_mode=\"$CACHE_MODE\"" \
        --field "cs_size=$CS_SIZE" \
        --field "run_mode=\"scaling_run\"" \
        --field "paper_grade=$PAPER_GRADE" \
        --field "seed_provenance=\"$seed_provenance\"" \
        --field "resume=$RESUME" \
        --field "clone_high_domain=$CLONE_HIGH_DOMAIN"

      "$PYTHON_BIN" - "$run_dir/summary.csv" "$run_id" "$scheme" "$domains" "$seed" "$M_VALUE" "$TAG_K" "$TOPO" "$lsdb_theory" "$run_dir" "$CACHE_MODE" "$CS_SIZE" "$PAPER_GRADE" "$seed_provenance" "$manifest_path" <<'PY' >> "$OUTPUT_CSV"
import csv
import sys

summary_path = sys.argv[1]
run_id = sys.argv[2]
scheme = sys.argv[3]
domains = int(float(sys.argv[4]))
seed = int(float(sys.argv[5]))
m = int(float(sys.argv[6]))
tagk = int(float(sys.argv[7]))
topology = sys.argv[8]
lsdb_theory = int(float(sys.argv[9]))
run_dir = sys.argv[10]
cache_mode = sys.argv[11]
cs_size = int(float(sys.argv[12]))
paper_grade = int(float(sys.argv[13]))
seed_provenance = sys.argv[14]
manifest_path = sys.argv[15]
lsdb = 0
fib = 0

try:
    with open(summary_path, newline='') as f:
        rows = list(csv.DictReader(f))
    if rows:
        row = rows[-1]
        lsdb = int(float(row.get('avg_LSDB_entries', 0)))
        fib = int(float(row.get('avg_FIB_entries', 0)))
except Exception:
    pass

print(f"{run_id},{scheme},{domains},{seed},{m},{tagk},{topology},{cache_mode},{cs_size},{paper_grade},{seed_provenance},{lsdb},{fib},{lsdb_theory},{run_dir},{manifest_path}")
PY
    done
  done
done

echo "[scaling] done: $OUTPUT_CSV"
