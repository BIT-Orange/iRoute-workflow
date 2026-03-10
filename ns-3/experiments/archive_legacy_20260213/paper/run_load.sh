#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$NS3_DIR"
BIN="$NS3_DIR/build/scratch/iroute-exp-baselines"
export LD_LIBRARY_PATH="$NS3_DIR/build/lib:${LD_LIBRARY_PATH:-}"
# iRoute Experiment: Hop Count vs Load
# Varies query frequency and measures hop count.

RESULT_DIR="$NS3_DIR/results/paper/load"
TRACE="$NS3_DIR/dataset/sdm_smartcity_dataset/consumer_trace.csv"
CENTROIDS="$NS3_DIR/dataset/sdm_smartcity_dataset/domain_centroids.csv"
CONTENT="$NS3_DIR/dataset/sdm_smartcity_dataset/producer_content.csv"

mkdir -p "$RESULT_DIR"
OUTPUT_CSV="$RESULT_DIR/load.csv"
echo "scheme,frequency,mean_hops" > "$OUTPUT_CSV"

FREQS="1 5 10 20 50"

for S in iroute flood; do
  for F in $FREQS; do
    echo "Running Load Exp: $S @ $F Hz..."
    NAME="${S}_freq${F}"
    
    # Set args
    ARG=""
    if [ "$S" == "flood" ]; then ARG="--floodResponder=producer --floodThreshold=0.6"; fi
    
    # Duration: 189 queries. At 1Hz=189s. At 50Hz=4s.
    # Set simTime=250 to cover 1Hz.
    $BIN --scheme=$S --frequency=$F --simTime=250 --trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --resultDir=$RESULT_DIR/$NAME $ARG > "$RESULT_DIR/$NAME.log" 2>&1
    
    SUMMARY="$RESULT_DIR/$NAME/summary.csv"
    if [ -f "$SUMMARY" ]; then
        # Extract mean_hops using csv module (no external deps)
        HOPS=$(python3 - <<PY 2>/dev/null
import csv
import sys
path = "$SUMMARY"
with open(path, newline='') as f:
    r = csv.DictReader(f)
    row = next(r, None)
    if row is None:
        print("")
    else:
        print(row.get("mean_hops", ""))
PY
)
        echo "$S,$F,$HOPS" >> "$OUTPUT_CSV"
        echo "  Result: $S @ $F Hz -> $HOPS hops"
    else
        echo "  Error: No summary for $S @ $F"
    fi
  done
done

echo "Done. Results in $OUTPUT_CSV"
