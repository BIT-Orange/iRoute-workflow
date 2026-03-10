#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$NS3_DIR"
BIN="$NS3_DIR/build/scratch/iroute-exp-baselines"
export LD_LIBRARY_PATH="$NS3_DIR/build/lib:${LD_LIBRARY_PATH:-}"

RESULT_DIR="$NS3_DIR/results/paper/failure"
TRACE="$NS3_DIR/dataset/sdm_smartcity_dataset/consumer_trace.csv"
CENTROIDS="$NS3_DIR/dataset/sdm_smartcity_dataset/domain_centroids.csv"
CONTENT="$NS3_DIR/dataset/sdm_smartcity_dataset/producer_content.csv"

mkdir -p "$RESULT_DIR"

run_exp() {
    SCHEME=$1
    FAIL_ARG=$2
    NAME=$3
    echo "Running $SCHEME - $NAME..."
    # Using 'time' to measure duration
    $BIN --scheme=$SCHEME --trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --simTime=150 --resultDir=$RESULT_DIR/$NAME $FAIL_ARG > "$RESULT_DIR/$NAME.log" 2>&1
    
    # Calculate recovery
    echo "Calculating recovery..."
    python3 "$NS3_DIR/dataset/calc_recovery.py" --log "$RESULT_DIR/$NAME/query_log.csv" --output "$RESULT_DIR/$NAME/recovery_metrics.txt"
    cat "$RESULT_DIR/$NAME/recovery_metrics.txt"
}

# 1. Link Failure (ingress-domain0 @ 50s)
FAIL="--failLink=ingress-domain0@50"
run_exp "iroute" "$FAIL" "iroute-link-fail"
run_exp "flood"  "$FAIL" "flood-link-fail"
run_exp "tag"    "$FAIL" "tag-link-fail"

# 2. Domain Failure (domain0 @ 50s)
FAIL="--failDomain=domain0@50"
run_exp "iroute" "$FAIL" "iroute-domain-fail"
# Flood handles domain failure naturally? Yes.
run_exp "flood"  "$FAIL" "flood-domain-fail"
# Tag handles it?
run_exp "tag"    "$FAIL" "tag-domain-fail"

# 3. Churn (iroute only)
# trigger churn @ 50s, ratio 0.5
FAIL="--churn=iroute@50@0.5"
run_exp "iroute" "$FAIL" "iroute-churn"

echo "Done. Results in $RESULT_DIR"
