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

RESULT_DIR="${1:-results/exp4-load}"
TOPO="${TOPO:-rocketfuel}"
TOPO_FILE="${TOPO_FILE:-src/ndnSIM/examples/topologies/as1239-r0.txt}"
INGRESS_NODE="${INGRESS_NODE:-0}"
LINK_DELAY_MS="${LINK_DELAY_MS:-2.0}"
LINK_DELAY_JITTER_US="${LINK_DELAY_JITTER_US:-0}"
SERVICE_JITTER_US="${SERVICE_JITTER_US:-1000}"
SEEDS="${SEEDS:-42 43 44}"
DOMAINS="${DOMAINS:-8}"
M_VALUE="${M_VALUE:-4}"
K_VALUE="${K_VALUE:-5}"
TAU_VALUE="${TAU_VALUE:-0.3}"
SCHEMES="${SCHEMES:-central iroute tag sanr-tag flood}"
FREQS="${FREQS:-1 2 5 10 20}"
WARMUP_SEC="${WARMUP_SEC:-20}"
MEASURE_START_SEC="${MEASURE_START_SEC:-20}"
SIM_TIME_MAX="${SIM_TIME_MAX:-180}"
RESUME="${RESUME:-1}"
TAG_K="${TAG_K:-32}"
TAG_LSA_PERIOD_MS="${TAG_LSA_PERIOD_MS:-20000}"
FLOOD_BUDGET="${FLOOD_BUDGET:-4}"
SANR_SIM_THRESH="${SANR_SIM_THRESH:-0.8}"
SANR_MSRR_TOP_PCT="${SANR_MSRR_TOP_PCT:-0.2}"
SANR_CMLT_SEC="${SANR_CMLT_SEC:-4.0}"
SANR_SLOT_SEC="${SANR_SLOT_SEC:-5.0}"
SANR_CCN_K="${SANR_CCN_K:-1}"
SANR_TOP_L="${SANR_TOP_L:-32}"
DATA_FRESHNESS_MS="${DATA_FRESHNESS_MS:-60000}"

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
INDEX="${INDEX:-dataset/sdm_smartcity_dataset/index_exact.csv}"

mkdir -p "$RESULT_DIR"
SWEEP_CSV="$RESULT_DIR/load_sweep.csv"
LOAD_CSV="$RESULT_DIR/load.csv"

cat > "$SWEEP_CSV" <<'CSVHDR'
run_id,scheme,seed,frequency,sim_time,topology,mean_hops,unique_hops_values,p50_ms,p95_ms,domain_acc,ctrl_bytes_per_sec,result_dir
CSVHDR

QUERY_COUNT=$(($(wc -l < "$TRACE") - 1))
if [ "$QUERY_COUNT" -lt 1 ]; then
  echo "[load] empty trace: $TRACE" >&2
  exit 1
fi

calc_sim_time() {
  local freq="$1"
  "$PYTHON_BIN" - "$QUERY_COUNT" "$freq" "$SIM_TIME_MAX" <<'PY'
import math
import sys
nq = float(sys.argv[1])
freq = float(sys.argv[2])
sim_max = float(sys.argv[3])
# start offset ~5s + tail slack
sim_time = math.ceil(nq / max(freq, 1e-9) + 15)
sim_time = int(max(sim_time, 30))
if sim_max > 0:
    sim_time = int(min(sim_time, sim_max))
print(sim_time)
PY
}

for scheme in $SCHEMES; do
  for seed in $SEEDS; do
    for freq in $FREQS; do
      sim_time=$(calc_sim_time "$freq")
      run_id="${scheme}_freq${freq}_s${seed}"
      run_dir="$RESULT_DIR/$run_id"
      mkdir -p "$run_dir"

      extra=""
      if [ "$scheme" = "flood" ]; then
        extra="--floodResponder=producer --floodThreshold=0.6 --floodProbeBudget=$FLOOD_BUDGET"
      elif [ "$scheme" = "tag" ]; then
        extra="--tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K"
      elif [ "$scheme" = "central" ]; then
        extra="--qrels=$QRELS --dirMode=oracle --dirTopK=5 --dirProcMs=2"
      elif [ "$scheme" = "exact" ]; then
        extra="--exactUseIndex=1 --index=$INDEX"
      elif [ "$scheme" = "sanr-tag" ]; then
        extra="--tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K --sanrSimThresh=$SANR_SIM_THRESH --msrrTopPct=$SANR_MSRR_TOP_PCT --cmltSec=$SANR_CMLT_SEC --slotSec=$SANR_SLOT_SEC --ccnK=$SANR_CCN_K --sanrTopL=$SANR_TOP_L"
      fi

      if [ "$RESUME" = "1" ] && [ -f "$run_dir/summary.csv" ] && [ -f "$run_dir/query_log.csv" ]; then
        echo "[load] resume $run_id (reusing existing outputs)"
      else
        echo "[load] running $run_id (simTime=$sim_time)"
        ./waf --run "iroute-exp-baselines --scheme=$scheme --domains=$DOMAINS --M=$M_VALUE --K=$K_VALUE --tau=$TAU_VALUE \
--frequency=$freq --simTime=$sim_time --seed=$seed \
--topo=$TOPO --topoFile=$TOPO_FILE --ingressNode=$INGRESS_NODE \
--linkDelayMs=$LINK_DELAY_MS --linkDelayJitterUs=$LINK_DELAY_JITTER_US \
--serviceJitterUs=$SERVICE_JITTER_US \
--dataFreshnessMs=$DATA_FRESHNESS_MS \
--trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --resultDir=$run_dir \
--shuffleTrace=$SHUFFLE_TRACE \
--warmupSec=$WARMUP_SEC --measureStartSec=$MEASURE_START_SEC --cdfSuccessOnly=1 --failureTargetPolicy=manual $extra" \
          > "$run_dir/console.log" 2>&1
      fi

      "$PYTHON_BIN" - "$run_dir/summary.csv" "$run_id" "$scheme" "$seed" "$freq" "$sim_time" "$run_dir" "$TOPO" <<'PY' \
        >> "$SWEEP_CSV"
import csv
import sys

summary_path = sys.argv[1]
run_id = sys.argv[2]
scheme = sys.argv[3]
seed = sys.argv[4]
freq = sys.argv[5]
sim_time = sys.argv[6]
run_dir = sys.argv[7]
topology = sys.argv[8]

vals = {
    'mean_hops': 0.0,
    'unique_hops_values': 0.0,
    'P50_ms': 0.0,
    'P95_ms': 0.0,
    'DomainAcc': 0.0,
    'ctrl_bytes_per_sec': 0.0,
}

try:
    with open(summary_path, newline='') as f:
        rows = list(csv.DictReader(f))
    if rows:
        row = rows[-1]
        for k in vals:
            try:
                vals[k] = float(row.get(k, 0.0))
            except Exception:
                vals[k] = 0.0
except Exception:
    pass

print(','.join([
    run_id,
    scheme,
    str(seed),
    str(freq),
    str(sim_time),
    topology,
    f"{vals['mean_hops']:.6f}",
    str(int(vals['unique_hops_values'])),
    f"{vals['P50_ms']:.6f}",
    f"{vals['P95_ms']:.6f}",
    f"{vals['DomainAcc']:.6f}",
    f"{vals['ctrl_bytes_per_sec']:.6f}",
    run_dir,
]))
PY
    done
  done
done

"$PYTHON_BIN" - "$SWEEP_CSV" "$LOAD_CSV" <<'PY'
import sys
import pandas as pd

sweep_csv, out_csv = sys.argv[1], sys.argv[2]
df = pd.read_csv(sweep_csv)
if df.empty:
    raise SystemExit(0)
agg = df.groupby(['scheme', 'frequency'], as_index=False)['mean_hops'].mean()
agg.to_csv(out_csv, index=False)
PY

echo "[load] done: $SWEEP_CSV"
echo "[load] summary: $LOAD_CSV"
