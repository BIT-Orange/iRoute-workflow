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

validate_cache_settings() {
  CACHE_MODE="${CACHE_MODE:-disabled}"
  if ! [[ "$CS_SIZE" =~ ^-?[0-9]+$ ]]; then
    echo "[failure][ERROR] CS_SIZE must be an integer, got: $CS_SIZE" >&2
    exit 2
  fi
  case "$CACHE_MODE" in
    disabled)
      if [ "$CS_SIZE" -ne 0 ]; then
        echo "[failure][ERROR] CACHE_MODE=disabled requires CS_SIZE=0, got $CS_SIZE" >&2
        exit 2
      fi
      ;;
    enabled)
      if [ "$CS_SIZE" -le 0 ]; then
        echo "[failure][ERROR] CACHE_MODE=enabled requires CS_SIZE>0, got $CS_SIZE" >&2
        exit 2
      fi
      ;;
    *)
      echo "[failure][ERROR] unsupported CACHE_MODE=$CACHE_MODE (expected disabled|enabled)" >&2
      exit 2
      ;;
  esac
}

RESULT_DIR="$(iroute_resolve_results_path "${1:-exp3-failure}")"
TOPO="${TOPO:-redundant}"
TOPO_FILE="$(iroute_resolve_topology_file "${TOPO_FILE:-src/ndnSIM/examples/topologies/as1239-r0.txt}")"
INGRESS_NODE="${INGRESS_NODE:-0}"
LINK_DELAY_MS="${LINK_DELAY_MS:-2.0}"
LINK_DELAY_JITTER_US="${LINK_DELAY_JITTER_US:-0}"
SERVICE_JITTER_US="${SERVICE_JITTER_US:-1000}"
SEEDS="${SEEDS:-42 43 44}"
DOMAINS="${DOMAINS:-8}"
M_VALUE="${M_VALUE:-4}"
K_VALUE="${K_VALUE:-5}"
TAU_VALUE="${TAU_VALUE:-0.3}"
SIM_TIME="${SIM_TIME:-120}"
FREQUENCY="${FREQUENCY:-5}"
FAIL_TIME="${FAIL_TIME:-50}"
WARMUP_SEC="${WARMUP_SEC:-20}"
MEASURE_START_SEC="${MEASURE_START_SEC:-20}"
HOT_DOMAIN_RANK="${HOT_DOMAIN_RANK:-0}"
TAG_K="${TAG_K:-32}"
TAG_LSA_PERIOD_MS="${TAG_LSA_PERIOD_MS:-20000}"
FAILURE_POLICY="${FAILURE_POLICY:-auto-noncut}"
LINK_RECOVERY_SEC="${LINK_RECOVERY_SEC:-20}"
BIN_QUERIES="${BIN_QUERIES:-20}"
CHURN_RATIO="${CHURN_RATIO:-0.3}"
CHURN_RECOVERY_SEC="${CHURN_RECOVERY_SEC:-20}"
CHURN_ROUNDS="${CHURN_ROUNDS:-4}"
CHURN_INTERVAL_SEC="${CHURN_INTERVAL_SEC:-10}"
CS_SIZE="${CS_SIZE:-0}"
SCHEMES="${SCHEMES:-central iroute tag sanr-tag flood}"
SANR_SIM_THRESH="${SANR_SIM_THRESH:-0.8}"
SANR_MSRR_TOP_PCT="${SANR_MSRR_TOP_PCT:-0.2}"
SANR_CMLT_SEC="${SANR_CMLT_SEC:-4.0}"
SANR_SLOT_SEC="${SANR_SLOT_SEC:-5.0}"
SANR_CCN_K="${SANR_CCN_K:-1}"
SANR_TOP_L="${SANR_TOP_L:-32}"
DATA_FRESHNESS_MS="${DATA_FRESHNESS_MS:-60000}"
PAPER_GRADE="${PAPER_GRADE:-0}"

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

validate_cache_settings
mkdir -p "$RESULT_DIR"
RECOVERY_CSV="$RESULT_DIR/recovery_summary.csv"
echo "scenario,scheme,seed,run_id,topology,failure_policy,recovery_metric,failure_effective,effective_reasons,min_success,t95,baseline,n_success,timeout_rate,pre_success,post_success,pre_domain_hit,post_domain_hit,pre_exact_hit,post_exact_hit,pre_timeout_rate,post_timeout_rate,pre_retrans_avg,post_retrans_avg,pre_rtt_ms,post_rtt_ms,hash_success,hash_domain_hit,hash_rtt,result_dir" > "$RECOVERY_CSV"

has_scheme() {
  local target="$1"
  for s in $SCHEMES; do
    if [ "$s" = "$target" ]; then
      return 0
    fi
  done
  return 1
}

run_case() {
  local scheme="$1"
  local scenario="$2"
  local failure_arg="$3"
  local run_id="$4"
  local seed="$5"
  local extra="${6:-}"
  local recovery_arg="${7:---failRecoverySec=-1}"

  local run_dir="$RESULT_DIR/$run_id"
  mkdir -p "$run_dir"

  echo "[failure] running $run_id"
  ./waf --run "iroute-exp-baselines --scheme=$scheme --domains=$DOMAINS --M=$M_VALUE --K=$K_VALUE --tau=$TAU_VALUE \
--topo=$TOPO --topoFile=$TOPO_FILE --ingressNode=$INGRESS_NODE \
--linkDelayMs=$LINK_DELAY_MS --linkDelayJitterUs=$LINK_DELAY_JITTER_US \
--serviceJitterUs=$SERVICE_JITTER_US \
--dataFreshnessMs=$DATA_FRESHNESS_MS \
--trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT \
--shuffleTrace=$SHUFFLE_TRACE \
--frequency=$FREQUENCY --simTime=$SIM_TIME --seed=$seed --resultDir=$run_dir \
--warmupSec=$WARMUP_SEC --measureStartSec=$MEASURE_START_SEC --cdfSuccessOnly=1 \
--csSize=$CS_SIZE \
--failureTargetPolicy=$FAILURE_POLICY --failHotDomainRank=$HOT_DOMAIN_RANK \
$failure_arg --churnRecoverySec=$CHURN_RECOVERY_SEC \
--churnRounds=$CHURN_ROUNDS --churnIntervalSec=$CHURN_INTERVAL_SEC \
$recovery_arg $extra" \
    > "$run_dir/console.log" 2>&1

  local qlog="$run_dir/query_log.csv"
  local fsanity="$run_dir/failure_sanity.csv"
  local lsanity="$run_dir/latency_sanity.csv"
  if [ ! -f "$qlog" ]; then
    echo "[failure][ERROR] missing $qlog" >&2
    return 2
  fi
  if [ ! -f "$fsanity" ]; then
    echo "[failure][ERROR] missing $fsanity" >&2
    return 2
  fi
  if [ ! -f "$lsanity" ]; then
    echo "[failure][ERROR] missing $lsanity" >&2
    return 2
  fi

  "$PYTHON_BIN" - "$fsanity" "$scenario" "$FAILURE_POLICY" "$PAPER_GRADE" <<'PY'
import csv, sys
path = sys.argv[1]
scenario = sys.argv[2]
policy = sys.argv[3]
paper_grade = str(sys.argv[4]).strip() == "1"
rows = list(csv.DictReader(open(path, newline='')))
if not rows:
    raise SystemExit(f"[failure][ERROR] empty failure_sanity: {path}")
r = rows[-1]
sch = (r.get("scenario") or "").strip()
scheduled = int(float(r.get("scheduled", 0) or 0))
applied = int(float(r.get("applied", 0) or 0))
if scenario in {"link-fail", "domain-fail", "churn"}:
    if scheduled != 1 or applied != 1:
        raise SystemExit(f"[failure][ERROR] invalid failure sanity ({scenario}): scheduled={scheduled}, applied={applied}, row={r}")
    pre_cnt = int(float(r.get("pre_count", 0) or 0))
    post_cnt = int(float(r.get("post_count", 0) or 0))
    if pre_cnt <= 0 or post_cnt <= 0:
        raise SystemExit(f"[failure][ERROR] insufficient pre/post samples ({scenario}): pre_count={pre_cnt}, post_count={post_cnt}")
    target = (r.get("target") or "").strip().lower()
    if scenario == "link-fail" and policy == "auto-noncut" and target.startswith("auto"):
        after_connected = int(float(r.get("after_connected", -1) or -1))
        if after_connected != 1:
            raise SystemExit(f"[failure][ERROR] auto-noncut expected after_connected=1, got {after_connected}, row={r}")
    metric = ""
    notes = (r.get("notes") or "").strip()
    for token in notes.split(";"):
        token = token.strip()
        if token.startswith("metric="):
            metric = token.split("=", 1)[1].strip()
            break
    if paper_grade and metric != "domain_hit":
        raise SystemExit(f"[failure][ERROR] paper-grade failure run must record metric=domain_hit, got {metric!r}, row={r}")
if sch and sch != scenario:
    raise SystemExit(f"[failure][ERROR] scenario mismatch: expected={scenario}, got={sch}")
PY

  "$PYTHON_BIN" - "$qlog" "$lsanity" "$fsanity" "$run_dir/summary.csv" "$scenario" "$scheme" "$seed" "$run_id" "$FAIL_TIME" "$run_dir" "$TOPO" "$FAILURE_POLICY" "$BIN_QUERIES" "$PAPER_GRADE" <<'PY' \
    >> "$RECOVERY_CSV"
import csv
import hashlib
import pandas as pd
import sys

qlog = sys.argv[1]
latency = sys.argv[2]
failure_sanity_csv = sys.argv[3]
summary_csv = sys.argv[4]
scenario = sys.argv[5]
scheme = sys.argv[6]
seed = sys.argv[7]
run_id = sys.argv[8]
fail_time = float(sys.argv[9])
run_dir = sys.argv[10]
topology = sys.argv[11]
policy = sys.argv[12]
bin_queries = int(float(sys.argv[13]))
paper_grade = str(sys.argv[14]).strip() == "1"

try:
    df = pd.read_csv(qlog)
except Exception:
    print(f"{scenario},{scheme},{seed},{run_id},{topology},{policy},domain_hit,0,,0.0,-1,0.0,0,1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,na,na,na,{run_dir}")
    raise SystemExit(0)

if df.empty:
    print(f"{scenario},{scheme},{seed},{run_id},{topology},{policy},domain_hit,0,,0.0,-1,0.0,0,1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,-1.0,na,na,na,{run_dir}")
    raise SystemExit(0)

if "is_measurable" in df.columns:
    meas = df[pd.to_numeric(df["is_measurable"], errors="coerce").fillna(0) > 0].copy()
else:
    meas = df.copy()
if meas.empty:
    meas = df.copy()

if "t_send_disc" in meas.columns:
    meas["_sec"] = pd.to_numeric(meas["t_send_disc"], errors="coerce").fillna(0.0) / 1000.0
else:
    meas["_sec"] = 0.0
if "domain_hit" in meas.columns:
    meas["_hit"] = pd.to_numeric(meas["domain_hit"], errors="coerce").fillna(0.0)
elif "is_success" in meas.columns:
    meas["_hit"] = pd.to_numeric(meas["is_success"], errors="coerce").fillna(0.0)
else:
    meas["_hit"] = 0.0
meas["_success"] = pd.to_numeric(meas.get("is_success", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
meas["_domain_hit"] = pd.to_numeric(meas.get("domain_hit", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
meas["_exact_hit"] = pd.to_numeric(meas.get("hit_exact", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
meas["_timeout"] = pd.to_numeric(meas.get("is_timeout", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
meas["_retrans"] = pd.to_numeric(meas.get("retransmissions", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
meas["_rtt"] = pd.to_numeric(meas.get("rtt_total_ms", pd.Series(dtype=float)), errors="coerce").fillna(0.0)
meas = meas.sort_values("_sec").reset_index(drop=True)
if bin_queries <= 0:
    bin_queries = 25
meas["_bin"] = (meas.index // bin_queries).astype(int)
binned = meas.groupby("_bin", as_index=False).agg(sec=("_sec", "median"), hit=("_hit", "mean"))

pre = binned[binned["sec"] < fail_time]["hit"]
post = binned[binned["sec"] >= fail_time]
baseline = float(pre.mean()) if not pre.empty else 0.0
min_success = float(post["hit"].min()) if not post.empty else 0.0
t95 = -1
if baseline > 0 and not post.empty:
    target = 0.95 * baseline
    rec = post[post["hit"] >= target]
    if not rec.empty:
        t95 = int(rec["sec"].iloc[0])

lat = {}
try:
    rows = list(csv.DictReader(open(latency, newline="")))
    if rows:
        lat = rows[-1]
except Exception:
    lat = {}
n_success = int(float(lat.get("success_queries", 0) or 0))
timeout_rate = float(lat.get("timeout_rate", 0.0) or 0.0)
failure_effective = 0
effective_reasons = ""
try:
    srows = list(csv.DictReader(open(summary_csv, newline="")))
    if srows:
        failure_effective = int(float(srows[-1].get("failure_effective", 0) or 0))
except Exception:
    failure_effective = 0
fsan = {}
try:
    frows = list(csv.DictReader(open(failure_sanity_csv, newline="")))
    if frows:
        fsan = frows[-1]
except Exception:
    fsan = {}
effective_reasons = str(fsan.get("effective_reasons", "") or "").strip()
if failure_effective == 0:
    message = f"[failure][ERROR] ineffective failure for {run_id}: failure_effective=0"
    if effective_reasons:
        message += f" reasons={effective_reasons}"
    if paper_grade:
        raise SystemExit(message)
    print(message, file=sys.stderr)

success_seq = ",".join(str(int(x)) for x in pd.to_numeric(meas.get("_success", pd.Series(dtype=float)), errors="coerce").fillna(0).astype(int).tolist())
domain_seq = ",".join(str(int(x)) for x in pd.to_numeric(meas.get("domain_hit", pd.Series(dtype=float)), errors="coerce").fillna(0).astype(int).tolist())
rtt_seq = ",".join(f"{x:.3f}" for x in pd.to_numeric(meas.get("rtt_total_ms", pd.Series(dtype=float)), errors="coerce").fillna(0.0).tolist())
h_success = hashlib.sha256(success_seq.encode()).hexdigest()[:16]
h_dom = hashlib.sha256(domain_seq.encode()).hexdigest()[:16]
h_rtt = hashlib.sha256(rtt_seq.encode()).hexdigest()[:16]

def pick(name, default="-1.0"):
    value = fsan.get(name, default)
    if value in ("", None):
        return default
    return str(value)

print(
    f"{scenario},{scheme},{seed},{run_id},{topology},{policy},domain_hit,{failure_effective},"
    f"{effective_reasons},{min_success:.6f},{t95},{baseline:.6f},{n_success},{timeout_rate:.6f},"
    f"{pick('pre_success')},{pick('post_success')},{pick('pre_domain_hit')},{pick('post_domain_hit')},"
    f"{pick('pre_exact_hit')},{pick('post_exact_hit')},{pick('pre_timeout_rate')},{pick('post_timeout_rate')},"
    f"{pick('pre_retrans_avg')},{pick('post_retrans_avg')},{pick('pre_rtt_ms')},{pick('post_rtt_ms')},"
    f"{h_success},{h_dom},{h_rtt},{run_dir}"
)
PY

  "$PYTHON_BIN" experiments/manifests/write_run_manifest.py \
    --repo-root "$ROOT_DIR" \
    --output "$run_dir/run_manifest.json" \
    --workflow failure_run \
    --runner ns-3/experiments/runners/run_failure_experiment.sh \
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
    --field "scenario=\"$scenario\"" \
    --field "seed=$seed" \
    --field "cache_mode=\"$CACHE_MODE\"" \
    --field "cs_size=$CS_SIZE" \
    --field "run_mode=\"failure_run\"" \
    --field "paper_grade=$PAPER_GRADE" \
    --field "seed_provenance=\"native\"" \
    --field "domains=$DOMAINS" \
    --field "topology=\"$TOPO\"" \
    --field "topology_file=\"$(resolve_path "$TOPO_FILE")\"" \
    --field "failure_policy=\"$FAILURE_POLICY\"" \
    --field "fail_time=$FAIL_TIME"
}

for seed in $SEEDS; do
  if has_scheme "central"; then
    central_extra="--qrels=$QRELS --dirMode=oracle --dirTopK=5 --dirProcMs=2 --failureTargetPolicy=manual"
    run_case "central" "link-fail" "--failLink=ingress-domain0@${FAIL_TIME}" "central-link-fail-s${seed}" "$seed" "$central_extra" "--failRecoverySec=$LINK_RECOVERY_SEC"
    run_case "central" "domain-fail" "--failDomain=domain0@${FAIL_TIME}" "central-domain-fail-s${seed}" "$seed" "$central_extra"
    run_case "central" "churn" "--churn=central@${FAIL_TIME}@${CHURN_RATIO}" "central-churn-s${seed}" "$seed" "$central_extra"
  fi
  if has_scheme "iroute"; then
    run_case "iroute" "link-fail" "--failLink=auto@${FAIL_TIME}" "iroute-link-fail-s${seed}" "$seed" "" "--failRecoverySec=$LINK_RECOVERY_SEC"
    run_case "iroute" "domain-fail" "--failDomain=auto@${FAIL_TIME}" "iroute-domain-fail-s${seed}" "$seed"
    run_case "iroute" "churn" "--churn=iroute@${FAIL_TIME}@${CHURN_RATIO}" "iroute-churn-s${seed}" "$seed"
  fi
  if has_scheme "flood"; then
    run_case "flood" "link-fail" "--failLink=auto@${FAIL_TIME}" "flood-link-fail-s${seed}" "$seed" "--floodResponder=producer --floodThreshold=0.6 --floodProbeBudget=4" "--failRecoverySec=$LINK_RECOVERY_SEC"
    run_case "flood" "domain-fail" "--failDomain=auto@${FAIL_TIME}" "flood-domain-fail-s${seed}" "$seed" "--floodResponder=producer --floodThreshold=0.6 --floodProbeBudget=4"
    run_case "flood" "churn" "--churn=flood@${FAIL_TIME}@${CHURN_RATIO}" "flood-churn-s${seed}" "$seed" "--floodResponder=producer --floodThreshold=0.6 --floodProbeBudget=4"
  fi
  if has_scheme "tag"; then
    run_case "tag" "link-fail" "--failLink=auto@${FAIL_TIME}" "tag-link-fail-s${seed}" "$seed" "--tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K" "--failRecoverySec=$LINK_RECOVERY_SEC"
    run_case "tag" "domain-fail" "--failDomain=auto@${FAIL_TIME}" "tag-domain-fail-s${seed}" "$seed" "--tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K"
    run_case "tag" "churn" "--churn=tag@${FAIL_TIME}@${CHURN_RATIO}" "tag-churn-s${seed}" "$seed" "--tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K"
  fi
  if has_scheme "sanr-tag"; then
    sanr_extra="--tagIndex=$TAG_INDEX --queryToTag=$QUERY_TO_TAG --lsaPeriod=$TAG_LSA_PERIOD_MS --tagK=$TAG_K --sanrSimThresh=$SANR_SIM_THRESH --msrrTopPct=$SANR_MSRR_TOP_PCT --cmltSec=$SANR_CMLT_SEC --slotSec=$SANR_SLOT_SEC --ccnK=$SANR_CCN_K --sanrTopL=$SANR_TOP_L"
    run_case "sanr-tag" "link-fail" "--failLink=auto@${FAIL_TIME}" "sanr-tag-link-fail-s${seed}" "$seed" "$sanr_extra" "--failRecoverySec=$LINK_RECOVERY_SEC"
    run_case "sanr-tag" "domain-fail" "--failDomain=auto@${FAIL_TIME}" "sanr-tag-domain-fail-s${seed}" "$seed" "$sanr_extra"
    run_case "sanr-tag" "churn" "--churn=tag@${FAIL_TIME}@${CHURN_RATIO}" "sanr-tag-churn-s${seed}" "$seed" "$sanr_extra"
  fi
done

"$PYTHON_BIN" - "$RECOVERY_CSV" <<'PY'
import pandas as pd
import sys

path = sys.argv[1]
df = pd.read_csv(path)
if df.empty:
    raise SystemExit("[failure][ERROR] empty recovery_summary.csv")

bad = []
for (scheme, seed), g in df.groupby(["scheme", "seed"]):
    if {"link-fail", "domain-fail", "churn"}.issubset(set(g["scenario"].tolist())):
        sub = g[g["scenario"].isin(["link-fail", "domain-fail", "churn"])]
        hash_col = "hash_success" if "hash_success" in sub.columns else "hash_domain_hit"
        if sub[hash_col].nunique() == 1:
            bad.append(f"{scheme}/seed{seed}: identical {hash_col} hash")
        if sub["hash_rtt"].nunique() == 1:
            bad.append(f"{scheme}/seed{seed}: identical rtt hash")

if bad:
    for x in bad:
        print(f"[failure][WARN] {x}")
if "recovery_metric" in df.columns:
    metrics = sorted(set(df["recovery_metric"].astype(str).tolist()))
    if metrics != ["domain_hit"]:
        raise SystemExit(f"[failure][ERROR] expected recovery_metric=domain_hit, got {metrics}")
if "failure_effective" in df.columns and (df["failure_effective"].astype(float) <= 0).any():
    print("[failure][WARN] recovery_summary contains ineffective rows", file=sys.stderr)
print("[failure] scenario hash sanity passed")
PY

"$PYTHON_BIN" experiments/manifests/write_run_manifest.py \
  --repo-root "$ROOT_DIR" \
  --output "$RESULT_DIR/run_manifest.json" \
  --workflow failure_experiment \
  --runner ns-3/experiments/runners/run_failure_experiment.sh \
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
  --field "run_mode=\"failure_aggregate\"" \
  --field "paper_grade=$PAPER_GRADE" \
  --field "seed_provenance=\"aggregate\"" \
  --field "scenarios=\"churn link-fail domain-fail\"" \
  --field "seeds=\"$SEEDS\"" \
  --field "topology=\"$TOPO\"" \
  --field "failure_policy=\"$FAILURE_POLICY\"" \
  --field "fail_time=$FAIL_TIME"

echo "[failure] done: $RESULT_DIR"
echo "[failure] recovery: $RECOVERY_CSV"
