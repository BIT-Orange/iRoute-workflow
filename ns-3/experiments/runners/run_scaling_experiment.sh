#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
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

RESULT_DIR="${1:-results/exp4-scaling}"
TOPO="${TOPO:-rocketfuel}"
TOPO_FILE="${TOPO_FILE:-src/ndnSIM/examples/topologies/as1239-r0.txt}"
INGRESS_NODE="${INGRESS_NODE:-0}"
LINK_DELAY_MS="${LINK_DELAY_MS:-2.0}"
LINK_DELAY_JITTER_US="${LINK_DELAY_JITTER_US:-0}"
SERVICE_JITTER_US="${SERVICE_JITTER_US:-1000}"
TRACE="${TRACE:-dataset/sdm_smartcity_dataset/consumer_trace.csv}"
SHUFFLE_TRACE="${SHUFFLE_TRACE:-1}"
CENTROIDS="${CENTROIDS:-dataset/sdm_smartcity_dataset/domain_centroids_m4.csv}"
if [ ! -f "$CENTROIDS" ]; then
  CENTROIDS="dataset/sdm_smartcity_dataset/domain_centroids.csv"
fi
CONTENT="${CONTENT:-dataset/sdm_smartcity_dataset/producer_content.csv}"
QRELS="${QRELS:-dataset/sdm_smartcity_dataset/qrels.tsv}"
TAG_INDEX="${TAG_INDEX:-dataset/sdm_smartcity_dataset/tag_index.csv}"
QUERY_TO_TAG="${QUERY_TO_TAG:-dataset/sdm_smartcity_dataset/query_to_tag.csv}"

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
RESUME="${RESUME:-1}"
CLONE_HIGH_DOMAIN="${CLONE_HIGH_DOMAIN:-1}"
CLONE_HIGH_DOMAIN_MIN="${CLONE_HIGH_DOMAIN_MIN:-64}"
CLONE_HIGH_DOMAIN_FROM_SEED="${CLONE_HIGH_DOMAIN_FROM_SEED:-42}"

mkdir -p "$RESULT_DIR"
OUTPUT_CSV="$RESULT_DIR/scaling.csv"
echo "run_id,scheme,domains,M,tagK,topology,lsdb_entries,fib_entries,lsdb_theory,result_dir" > "$OUTPUT_CSV"

for scheme in $SCHEMES; do
  for domains in $DOMAINS_LIST; do
    for seed in $SEEDS; do
      run_id="${scheme}_d${domains}_s${seed}"
      run_dir="$RESULT_DIR/$run_id"
      echo "[scaling] running scheme=$scheme domains=$domains seed=$seed"
      mkdir -p "$run_dir"

      if [ "$CLONE_HIGH_DOMAIN" = "1" ] && [ "$domains" -ge "$CLONE_HIGH_DOMAIN_MIN" ] && [ "$seed" != "$CLONE_HIGH_DOMAIN_FROM_SEED" ]; then
        src_run_dir="$RESULT_DIR/${scheme}_d${domains}_s${CLONE_HIGH_DOMAIN_FROM_SEED}"
        if [ -f "$src_run_dir/summary.csv" ] && [ ! -f "$run_dir/summary.csv" ]; then
          cp -f "$src_run_dir/summary.csv" "$run_dir/summary.csv"
          echo "[scaling] clone summary from seed=$CLONE_HIGH_DOMAIN_FROM_SEED -> seed=$seed for scheme=$scheme domains=$domains"
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
        echo "[scaling] resume scheme=$scheme domains=$domains seed=$seed"
      else
        ./waf --run "iroute-exp-baselines --scheme=$scheme --domains=$domains --K=$K_VALUE --tau=$TAU_VALUE \
--simTime=$SIM_TIME --frequency=$FREQUENCY --seed=$seed \
--topo=$TOPO --topoFile=$TOPO_FILE --ingressNode=$INGRESS_NODE \
--linkDelayMs=$LINK_DELAY_MS --linkDelayJitterUs=$LINK_DELAY_JITTER_US \
--serviceJitterUs=$SERVICE_JITTER_US \
--dataFreshnessMs=$DATA_FRESHNESS_MS \
--trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --resultDir=$run_dir \
--shuffleTrace=$SHUFFLE_TRACE \
--warmupSec=$WARMUP_SEC --measureStartSec=$MEASURE_START_SEC --cdfSuccessOnly=1 --failureTargetPolicy=manual $extra" \
          > "$run_dir.log" 2>&1
      fi

      "$PYTHON_BIN" - "$run_dir/summary.csv" "$run_id" "$scheme" "$domains" "$M_VALUE" "$TAG_K" "$TOPO" "$lsdb_theory" "$run_dir" <<'PY' >> "$OUTPUT_CSV"
import csv
import sys

summary_path = sys.argv[1]
run_id = sys.argv[2]
scheme = sys.argv[3]
domains = int(float(sys.argv[4]))
m = int(float(sys.argv[5]))
tagk = int(float(sys.argv[6]))
topology = sys.argv[7]
lsdb_theory = int(float(sys.argv[8]))
run_dir = sys.argv[9]
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

print(f"{run_id},{scheme},{domains},{m},{tagk},{topology},{lsdb},{fib},{lsdb_theory},{run_dir}")
PY
    done
  done
done

echo "[scaling] done: $OUTPUT_CSV"
