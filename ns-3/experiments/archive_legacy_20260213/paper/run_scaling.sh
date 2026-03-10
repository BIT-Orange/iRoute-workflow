#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$NS3_DIR"
BIN="$NS3_DIR/build/scratch/iroute-exp-baselines"
export LD_LIBRARY_PATH="$NS3_DIR/build/lib:${LD_LIBRARY_PATH:-}"
# iRoute Experiment 4: State Scaling
# Varies number of domains and measures LSDB/FIB size.

RESULT_DIR="$NS3_DIR/results/paper/scaling"
TRACE="$NS3_DIR/dataset/sdm_smartcity_dataset/consumer_trace.csv"
CENTROIDS="$NS3_DIR/dataset/sdm_smartcity_dataset/domain_centroids.csv"
CONTENT="$NS3_DIR/dataset/sdm_smartcity_dataset/producer_content.csv"

mkdir -p "$RESULT_DIR"
OUTPUT_CSV="$RESULT_DIR/scaling.csv"
echo "domains,lsdb_entries,fib_entries" > "$OUTPUT_CSV"

# NOTE: sdm_smartcity_dataset contains 8 domains. Keep within that range.
DOMAINS_LIST="2 4 8"

for D in $DOMAINS_LIST; do
    echo "Running Scaling Experiment with domains=$D..."
    NAME="iroute_d$D"
    
    # Run simulation with short time (just enough to converge LSDB)
    # 20s should be enough for LSAs to propagate
    $BIN --scheme=iroute --domains=$D --simTime=30 --trace=$TRACE --centroids=$CENTROIDS --content=$CONTENT --resultDir=$RESULT_DIR/$NAME > "$RESULT_DIR/$NAME.log" 2>&1
    
    # Extract metrics from summary.csv
    SUMMARY="$RESULT_DIR/$NAME/summary.csv"
    if [ -f "$SUMMARY" ]; then
        # summary.csv header:
        # scheme,totalQueries,...,avg_FIB_entries,avg_LSDB_entries
        # We need the last two columns (or by name)
        # Using tail -n 1
        LINE=$(tail -n 1 "$SUMMARY")
        # Assuming last column is LSDB, second last is FIB.
        # Check header in step 3413: ...,avg_FIB_entries,avg_LSDB_entries
        
        # Parse CSV line using awk (handle commas)
        # But fields might be quoted?
        # Irrelevant for numbers.
        
        # Get last field (LSDB) and second last (FIB)
        LSDB=$(echo "$LINE" | awk -F, '{print $NF}')
        FIB=$(echo "$LINE" | awk -F, '{print $(NF-1)}')
        
        echo "$D,$LSDB,$FIB" >> "$OUTPUT_CSV"
        echo "  Result: Domains=$D -> LSDB=$LSDB, FIB=$FIB"
    else
        echo "  Error: No summary found for $D"
    fi
done

echo "Done. Results in $OUTPUT_CSV"
