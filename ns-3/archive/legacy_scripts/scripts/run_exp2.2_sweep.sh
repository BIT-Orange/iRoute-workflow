#!/bin/bash
# Exp2.2 Parameter Sweep Runner
# Runs parameter sweep in separate processes to avoid ns-3 state issues

cd /home/jiyuan/ndnSIM/ns-3

RESULT_DIR="results/exp2.2_sweep"
CENTROIDS="dataset/trec_dl_combined_dim128/domain_centroids.csv"
CONTENT="dataset/trec_dl_combined_dim128/producer_content.csv"
TRACE="dataset/trec_dl_combined_dim128/consumer_trace.csv"

# Create output directory and clear old results
mkdir -p "$RESULT_DIR"
rm -f "$RESULT_DIR/exp2.2_sweep.csv"

# Fixed domains=8 (matching dataset)
DOMAINS=8

# Parameter grid
M_VALUES=(1 2 4 8)
KMAX_VALUES=(1 3 5)
TAU_VALUES=(0.0 0.1 0.2 0.3)

total=$((${#M_VALUES[@]} * ${#KMAX_VALUES[@]} * ${#TAU_VALUES[@]}))
count=0

echo "=== Exp2.2: Parameter Sweep ==="
echo "Total configurations: $total"
echo ""

for M in "${M_VALUES[@]}"; do
    for KMAX in "${KMAX_VALUES[@]}"; do
        for TAU in "${TAU_VALUES[@]}"; do
            count=$((count + 1))
            echo "[$count/$total] Running: D=$DOMAINS, M=$M, kMax=$KMAX, tau=$TAU"
            
            ./waf --run "iroute-v2-exp2.2-single \
                --domains=$DOMAINS \
                --M=$M \
                --kMax=$KMAX \
                --tau=$TAU \
                --centroids=$CENTROIDS \
                --content=$CONTENT \
                --trace=$TRACE \
                --resultDir=$RESULT_DIR" 2>&1 | grep -E "(Results|DomAcc|DocAcc|Failures|Appended)"
            
            echo ""
        done
    done
done

echo "=== Sweep Complete ==="
echo "Results saved to: $RESULT_DIR/exp2.2_sweep.csv"
cat "$RESULT_DIR/exp2.2_sweep.csv"
