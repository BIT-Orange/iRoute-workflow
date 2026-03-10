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

RESULT_DIR="${1:-results/accuracy_comparison}"
TOPO="${TOPO:-rocketfuel}"
TOPO_FILE="${TOPO_FILE:-src/ndnSIM/examples/topologies/as1239-r0.txt}"
INGRESS_NODE="${INGRESS_NODE:-0}"
LINK_DELAY_MS="${LINK_DELAY_MS:-2.0}"
LINK_DELAY_JITTER_US="${LINK_DELAY_JITTER_US:-1500}"
SERVICE_JITTER_US="${SERVICE_JITTER_US:-3000}"
SEEDS="${SEEDS:-42 43 44}"
DOMAINS="${DOMAINS:-8}"
SIM_TIME="${SIM_TIME:-130}"
FREQUENCY="${FREQUENCY:-5}"
WARMUP_SEC="${WARMUP_SEC:-20}"
MEASURE_START_SEC="${MEASURE_START_SEC:-20}"
K_VALUE="${K_VALUE:-5}"
TAU_VALUE="${TAU_VALUE:-0.3}"

IROUTE_M_VALUES="${IROUTE_M_VALUES:-1 2 4 8}"
FLOOD_BUDGET_VALUES="${FLOOD_BUDGET_VALUES:-2 4 6 8}"
FLOOD_REF_BUDGET="${FLOOD_REF_BUDGET:-8}"
TAG_K_VALUES="${TAG_K_VALUES:-8 16 32 64}"
TAG_LSA_PERIOD_MS="${TAG_LSA_PERIOD_MS:-20000}"
SANR_THRESH_VALUES="${SANR_THRESH_VALUES:-0.7 0.8 0.9 0.95}"
SANR_REF_THRESH="${SANR_REF_THRESH:-0.8}"
SANR_MSRR_TOP_PCT="${SANR_MSRR_TOP_PCT:-0.2}"
SANR_CMLT_SEC="${SANR_CMLT_SEC:-4.0}"
SANR_SLOT_SEC="${SANR_SLOT_SEC:-5.0}"
SANR_CCN_K="${SANR_CCN_K:-1}"
SANR_TOP_L="${SANR_TOP_L:-32}"
CENTRAL_PROC_VALUES="${CENTRAL_PROC_VALUES:-2 4 8 12}"
CENTRAL_PROC_JITTER_US="${CENTRAL_PROC_JITTER_US:-0}"
EXACT_USE_INDEX="${EXACT_USE_INDEX:-0}"
SCHEMES="${SCHEMES:-iroute flood tag central exact}"
CS_SIZE="${CS_SIZE:-0}"
RESUME="${RESUME:-0}"
DATA_FRESHNESS_MS="${DATA_FRESHNESS_MS:-60000}"

TRACE="${TRACE:-dataset/sdm_smartcity_dataset/consumer_trace.csv}"
SHUFFLE_TRACE="${SHUFFLE_TRACE:-1}"
CENTROIDS="${CENTROIDS:-dataset/sdm_smartcity_dataset/domain_centroids_m4.csv}"
if [ ! -f "$CENTROIDS" ]; then
  CENTROIDS="dataset/sdm_smartcity_dataset/domain_centroids.csv"
fi
CONTENT="${CONTENT:-dataset/sdm_smartcity_dataset/producer_content.csv}"
INDEX="${INDEX:-dataset/sdm_smartcity_dataset/index_exact.csv}"
QRELS="${QRELS:-dataset/sdm_smartcity_dataset/qrels.tsv}"
TAG_INDEX="${TAG_INDEX:-dataset/sdm_smartcity_dataset/tag_index.csv}"
QUERY_TO_TAG="${QUERY_TO_TAG:-dataset/sdm_smartcity_dataset/query_to_tag.csv}"

mkdir -p "$RESULT_DIR"
SWEEP_CSV="$RESULT_DIR/accuracy_sweep.csv"
REF_CSV="$RESULT_DIR/reference_runs.csv"

cat > "$SWEEP_CSV" <<'CSVHDR'
run_id,scheme,seed,sweep_param,sweep_value,is_reference,oracle_flag,ctrl_bytes_per_sec,domain_acc,domain_recall_at_1,domain_recall_at_3,domain_recall_at_5,doc_hit_at_1,doc_hit_at_10,ndcg_at_10,p50_ms,p95_ms,mean_hops,total_queries,measurable_queries,mean_relset_size,singleton_queries,n_success,timeout_rate,unique_rtt_values,result_dir
CSVHDR

run_one() {
  local scheme="$1"
  local seed="$2"
  local sweep_param="$3"
  local sweep_value="$4"
  local is_reference="$5"
  local extra="$6"

  local run_id="${scheme}_${sweep_param}${sweep_value}_s${seed}"
  local run_dir="$RESULT_DIR/$run_id"
  mkdir -p "$run_dir"

  local common_args="--domains=$DOMAINS --K=$K_VALUE --tau=$TAU_VALUE --simTime=$SIM_TIME --frequency=$FREQUENCY \
--seed=$seed --topo=$TOPO --topoFile=$TOPO_FILE --ingressNode=$INGRESS_NODE \
--linkDelayMs=$LINK_DELAY_MS --linkDelayJitterUs=$LINK_DELAY_JITTER_US \
--serviceJitterUs=$SERVICE_JITTER_US \
--dataFreshnessMs=$DATA_FRESHNESS_MS \
--csSize=$CS_SIZE \
--trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --resultDir=$run_dir \
--shuffleTrace=$SHUFFLE_TRACE \
--warmupSec=$WARMUP_SEC --measureStartSec=$MEASURE_START_SEC --cdfSuccessOnly=1 \
--failureTargetPolicy=manual"

  local have_outputs=0
  if [ -f "$run_dir/summary.csv" ] && [ -f "$run_dir/query_log.csv" ] && [ -f "$run_dir/latency_sanity.csv" ]; then
    have_outputs=1
  fi

  if [ "$RESUME" = "1" ] && [ "$have_outputs" = "1" ]; then
    echo "[accuracy] resume $run_id (reusing existing outputs)"
  else
    echo "[accuracy] running $run_id"
    ./waf --run "iroute-exp-baselines --scheme=$scheme $common_args $extra" \
      > "$run_dir/console.log" 2>&1
  fi

  "$PYTHON_BIN" - "$run_dir/summary.csv" "$run_dir/query_log.csv" "$run_dir/latency_sanity.csv" \
    "$run_id" "$scheme" "$seed" "$sweep_param" "$sweep_value" "$is_reference" "$run_dir" <<'PY' \
    >> "$SWEEP_CSV"
import csv
import math
import sys

summary_path = sys.argv[1]
log_path = sys.argv[2]
latency_path = sys.argv[3]
run_id = sys.argv[4]
scheme = sys.argv[5]
seed = sys.argv[6]
sweep_param = sys.argv[7]
sweep_value = sys.argv[8]
is_reference = sys.argv[9]
run_dir = sys.argv[10]

def parse_num(x, default=0.0):
    try:
        return float(x)
    except Exception:
        return default

def split_semicolon(text):
    if text is None:
        return []
    out = []
    for tok in str(text).split(';'):
        tok = tok.strip()
        if tok:
            out.append(tok)
    return out

def check_domain_hit(pred, gt_list):
    pred = str(pred or '').strip()
    if not pred:
        return False
    for gt in gt_list:
        g = str(gt).strip()
        if not g:
            continue
        if pred.find(g) >= 0 or g.find(pred) >= 0:
            return True
        try:
            gid = int(g)
            if pred.find('/domain' + str(gid)) >= 0:
                return True
        except Exception:
            pass
    return False

def parse_topk(text):
    vals = []
    for part in split_semicolon(text):
        if '=' in part:
            part = part.split('=', 1)[0]
        vals.append(part.strip())
    return vals

summary_row = {}
try:
    with open(summary_path, newline='') as f:
        rows = list(csv.DictReader(f))
    if rows:
        summary_row = rows[-1]
except Exception:
    summary_row = {}

latency_row = {}
try:
    with open(latency_path, newline='') as f:
        rows = list(csv.DictReader(f))
    if rows:
        latency_row = rows[-1]
except Exception:
    latency_row = {}

measurable = 0
r1 = 0
r3 = 0
r5 = 0
doc1 = 0
doc10 = 0
ndcg10 = 0.0
relset_total = 0.0
relset_cnt = 0
singleton_cnt = 0

try:
    with open(log_path, newline='') as f:
        for row in csv.DictReader(f):
            gt_domains = split_semicolon(row.get('gt_domain', ''))
            if not gt_domains:
                continue
            is_meas = parse_num(row.get('is_measurable', 1), 1) > 0
            if not is_meas:
                continue
            measurable += 1
            topk = parse_topk(row.get('topk_domains', ''))
            if not topk:
                pred = str(row.get('pred_domain', '')).strip()
                if pred:
                    topk = [pred]
            if any(check_domain_hit(c, gt_domains) for c in topk[:1]):
                r1 += 1
            if any(check_domain_hit(c, gt_domains) for c in topk[:3]):
                r3 += 1
            if any(check_domain_hit(c, gt_domains) for c in topk[:5]):
                r5 += 1

            hit = parse_num(row.get('hit_exact', 0), 0.0)
            if hit > 0:
                doc1 += 1
                doc10 += 1
                ndcg10 += 1.0

            rel_docs = split_semicolon(row.get('gt_doc', ''))
            if rel_docs:
                relset_total += len(rel_docs)
                relset_cnt += 1
                if len(rel_docs) == 1:
                    singleton_cnt += 1
except Exception:
    pass

if measurable <= 0:
    dr1 = dr3 = dr5 = 0.0
    dh1 = dh10 = ndcg = 0.0
else:
    dr1 = r1 / measurable
    dr3 = r3 / measurable
    dr5 = r5 / measurable
    dh1 = doc1 / measurable
    dh10 = doc10 / measurable
    ndcg = ndcg10 / measurable

ctrl = parse_num(summary_row.get('ctrl_bytes_per_sec', 0.0), 0.0)
dacc = parse_num(summary_row.get('DomainAcc', 0.0), 0.0)
p50 = parse_num(summary_row.get('P50_ms', 0.0), 0.0)
p95 = parse_num(summary_row.get('P95_ms', 0.0), 0.0)
hops = parse_num(summary_row.get('mean_hops', 0.0), 0.0)
queries = int(parse_num(summary_row.get('totalQueries', 0), 0))
meas_q = int(parse_num(summary_row.get('measurableQueries', measurable), measurable))
mean_relset = (relset_total / relset_cnt) if relset_cnt > 0 else parse_num(summary_row.get('mean_relset_size', 0.0), 0.0)
singletons = int(parse_num(summary_row.get('singleton_queries', singleton_cnt), singleton_cnt))
n_success = int(parse_num(latency_row.get('success_queries', 0), 0))
timeout_rate = parse_num(latency_row.get('timeout_rate', 0.0), 0.0)
unique_rtt_values = int(parse_num(latency_row.get('unique_rtt_values', 0), 0))
oracle_flag = 1 if scheme == 'central' else 0

print(','.join([
    run_id,
    scheme,
    str(seed),
    sweep_param,
    str(sweep_value),
    str(is_reference),
    str(oracle_flag),
    f'{ctrl:.6f}',
    f'{dacc:.6f}',
    f'{dr1:.6f}',
    f'{dr3:.6f}',
    f'{dr5:.6f}',
    f'{dh1:.6f}',
    f'{dh10:.6f}',
    f'{ndcg:.6f}',
    f'{p50:.6f}',
    f'{p95:.6f}',
    f'{hops:.6f}',
    str(queries),
    str(meas_q),
    f'{mean_relset:.6f}',
    str(singletons),
    str(n_success),
    f'{timeout_rate:.6f}',
    str(unique_rtt_values),
    run_dir,
]))
PY
}

has_scheme() {
  local target="$1"
  for s in $SCHEMES; do
    if [ "$s" = "$target" ]; then
      return 0
    fi
  done
  return 1
}

if has_scheme "iroute"; then
  for seed in $SEEDS; do
    for m in $IROUTE_M_VALUES; do
      ref=0
      [ "$m" = "4" ] && ref=1
      run_one "iroute" "$seed" "M" "$m" "$ref" "--M=$m"
    done
  done
fi

if has_scheme "flood"; then
  for seed in $SEEDS; do
    for budget in $FLOOD_BUDGET_VALUES; do
      ref=0
      [ "$budget" = "$FLOOD_REF_BUDGET" ] && ref=1
      run_one "flood" "$seed" "budget" "$budget" "$ref" "--M=4 --floodResponder=producer --floodThreshold=0.6 --floodProbeBudget=$budget"
    done
  done
fi

if has_scheme "tag"; then
  for seed in $SEEDS; do
    for tagk in $TAG_K_VALUES; do
      ref=0
      [ "$tagk" = "32" ] && ref=1
      run_one "tag" "$seed" "tagK" "$tagk" "$ref" "--M=4 --tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$tagk"
    done
  done
fi

if has_scheme "sanr-tag"; then
  for seed in $SEEDS; do
    for sth in $SANR_THRESH_VALUES; do
      ref=0
      [ "$sth" = "$SANR_REF_THRESH" ] && ref=1
      run_one "sanr-tag" "$seed" "sim" "$sth" "$ref" "--M=4 --tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=32 --sanrSimThresh=$sth --msrrTopPct=$SANR_MSRR_TOP_PCT --cmltSec=$SANR_CMLT_SEC --slotSec=$SANR_SLOT_SEC --ccnK=$SANR_CCN_K --sanrTopL=$SANR_TOP_L"
    done
  done
fi

if has_scheme "sanr-oracle"; then
  for seed in $SEEDS; do
    for sth in $SANR_THRESH_VALUES; do
      ref=0
      [ "$sth" = "$SANR_REF_THRESH" ] && ref=1
      run_one "sanr-oracle" "$seed" "sim" "$sth" "$ref" "--qrels=$QRELS --dirMode=oracle --dirTopK=5 --dirProcMs=2 --sanrSimThresh=$sth --msrrTopPct=$SANR_MSRR_TOP_PCT --cmltSec=$SANR_CMLT_SEC --slotSec=$SANR_SLOT_SEC --ccnK=$SANR_CCN_K --sanrTopL=$SANR_TOP_L"
    done
  done
fi

if has_scheme "central"; then
  for seed in $SEEDS; do
    for proc in $CENTRAL_PROC_VALUES; do
      ref=0
      [ "$proc" = "2" ] && ref=1
      run_one "central" "$seed" "proc" "$proc" "$ref" "--qrels=$QRELS --dirMode=oracle --dirTopK=5 --dirProcMs=$proc --dirProcJitterUs=$CENTRAL_PROC_JITTER_US"
    done
  done
fi

if has_scheme "exact"; then
  for seed in $SEEDS; do
    ref=1
    run_one "exact" "$seed" "exact" "$EXACT_USE_INDEX" "$ref" "--exactUseIndex=$EXACT_USE_INDEX --index=$INDEX"
  done
fi

"$PYTHON_BIN" - "$SWEEP_CSV" "$REF_CSV" "$RESULT_DIR/comparison.csv" <<'PY'
import sys
import pandas as pd

sweep_csv, ref_csv, cmp_csv = sys.argv[1], sys.argv[2], sys.argv[3]
df = pd.read_csv(sweep_csv)
if df.empty:
    raise SystemExit(0)

ref = df[df['is_reference'] == 1].copy()
if ref.empty:
    ref = df.copy()

ref.to_csv(ref_csv, index=False)

agg_cols = [
    'oracle_flag',
    'ctrl_bytes_per_sec', 'domain_acc', 'domain_recall_at_1', 'domain_recall_at_3', 'domain_recall_at_5',
    'doc_hit_at_1', 'doc_hit_at_10', 'ndcg_at_10', 'p50_ms', 'p95_ms', 'mean_hops',
    'total_queries', 'measurable_queries', 'mean_relset_size', 'singleton_queries',
    'n_success', 'timeout_rate', 'unique_rtt_values'
]
out = ref.groupby('scheme', as_index=False)[agg_cols].mean(numeric_only=True)
out = out.rename(columns={
    'oracle_flag': 'oracle_flag',
    'domain_acc': 'DomainAcc',
    'domain_recall_at_1': 'DomainRecall_at_1',
    'domain_recall_at_3': 'DomainRecall_at_3',
    'domain_recall_at_5': 'DomainRecall_at_5',
    'doc_hit_at_1': 'DocHit_at_1',
    'doc_hit_at_10': 'DocHit_at_10',
    'ndcg_at_10': 'nDCG_at_10',
    'p50_ms': 'P50_ms',
    'p95_ms': 'P95_ms',
    'mean_hops': 'mean_hops',
    'ctrl_bytes_per_sec': 'ctrl_bytes_per_sec',
    'n_success': 'n_success',
    'timeout_rate': 'timeout_rate',
    'unique_rtt_values': 'unique_rtt_values',
})
out.to_csv(cmp_csv, index=False)
PY

echo "[accuracy] done: $SWEEP_CSV"
echo "[accuracy] reference: $REF_CSV"
echo "[accuracy] comparison: $RESULT_DIR/comparison.csv"
