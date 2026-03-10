#!/bin/bash
# run_exp2.sh - Run Exp2 baseline comparison experiments
#
# Usage:
#   ./run_exp2.sh [exp2.1|exp2.2|all]
#
# Output:
#   results/exp2.1_packet/  - Packet-level baseline comparison
#   results/exp2.2_sweep/   - Parameter sweep results

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$SCRIPT_DIR"
DATASET_DIR="$NS3_DIR/dataset/trec_dl_combined"

# Default parameters
DOMAINS=8
M=4
KMAX=5
TAU=0.3
SIM_TIME=300

# Check if dataset exists
if [ ! -f "$DATASET_DIR/consumer_trace.csv" ]; then
    echo "Error: Combined dataset not found. Run merge_trec_datasets.py first."
    exit 1
fi

run_exp2_1() {
    echo "======================================"
    echo "Running Exp2.1: Packet-level Baselines"
    echo "======================================"
    
    mkdir -p results/exp2.1_packet
    
    # Run each method separately to avoid GlobalRoutingHelper state issues
    for method in iroute flooding centralized; do
        echo ""
        echo "--- Running $method ---"
        ./waf --run "iroute-v2-exp2.1-packet-baselines \
            --method=$method \
            --domains=$DOMAINS \
            --M=$M \
            --kMax=$KMAX \
            --tau=$TAU \
            --simTime=$SIM_TIME \
            --centroids=$DATASET_DIR/domain_centroids.csv \
            --content=$DATASET_DIR/producer_content.csv \
            --trace=$DATASET_DIR/consumer_trace.csv \
            --resultDir=results/exp2.1_packet" 2>&1 | tee -a results/exp2.1_packet/exp2.1_${method}.log
    done
    
    # Merge results from all methods
    echo ""
    echo "Merging results..."
    python3 - <<'PYTHON_SCRIPT'
import pandas as pd
import os

result_dir = "results/exp2.1_packet"
dfs = []
for method in ["iroute", "flooding", "centralized"]:
    f = os.path.join(result_dir, f"exp2.1_comparison_{method}.csv")
    if os.path.exists(f):
        dfs.append(pd.read_csv(f))
if dfs:
    merged = pd.concat(dfs, ignore_index=True)
    merged.to_csv(os.path.join(result_dir, "exp2.1_comparison.csv"), index=False)
    print(f"Merged {len(dfs)} result files")
PYTHON_SCRIPT
    
    echo ""
    echo "Exp2.1 complete. Results in results/exp2.1_packet/"
}

run_exp2_2() {
    echo "======================================"
    echo "Running Exp2.2: Parameter Sweep"
    echo "======================================"
    
    mkdir -p results/exp2.2_sweep
    
    ./waf --run "iroute-v2-exp2.2-param-sweep \
        --simTime=$SIM_TIME \
        --centroids=$DATASET_DIR/domain_centroids.csv \
        --content=$DATASET_DIR/producer_content.csv \
        --trace=$DATASET_DIR/consumer_trace.csv \
        --resultDir=results/exp2.2_sweep" 2>&1 | tee results/exp2.2_sweep/exp2.2.log
    
    echo ""
    echo "Exp2.2 complete. Results in results/exp2.2_sweep/"
}

print_usage() {
    echo "Usage: $0 [exp2.1|exp2.2|all]"
    echo ""
    echo "  exp2.1  - Run packet-level baseline comparison"
    echo "  exp2.2  - Run parameter sweep"
    echo "  all     - Run both experiments"
}

# Main
cd "$NS3_DIR"

case "${1:-all}" in
    exp2.1)
        run_exp2_1
        ;;
    exp2.2)
        run_exp2_2
        ;;
    all)
        run_exp2_1
        echo ""
        run_exp2_2
        ;;
    -h|--help)
        print_usage
        ;;
    *)
        echo "Unknown option: $1"
        print_usage
        exit 1
        ;;
esac

echo ""
echo "======================================"
echo "Experiments complete!"
echo "======================================"
echo ""
echo "Results:"
ls -la results/exp2.1_packet/*.csv 2>/dev/null || true
ls -la results/exp2.2_sweep/*.csv 2>/dev/null || true
