#!/bin/bash
set -e

# Configuration
PROGRAM="iroute-v2-exp2.1-packet-baselines"
DOMAINS=8
SIM_TIME=50 # Seconds, adjust as needed
TRACE="dataset/sdm_smartcity_dataset/consumer_trace_test.csv"
CENTROIDS="dataset/sdm_smartcity_dataset/domain_centroids.csv"
CONTENT="dataset/sdm_smartcity_dataset/producer_content.csv"
HAND_DICT="dataset/sdm_smartcity_dataset/index_exact.csv"

echo "=== Starting Full Experiment Suite ==="
echo "Program: $PROGRAM"
echo "Trace: $TRACE"

# 1. Run iRoute (and create directory)
echo "--- Running iRoute ---"

OUTPUT_FILE="last_run_output.txt"
./run_experiments.py \
    --program="$PROGRAM" \
    --domains=$DOMAINS \
    --kMax=5 \
    --tag="full_run" \
    --simTime=$SIM_TIME \
    --trace="$TRACE" \
    --centroids="$CENTROIDS" \
    --content="$CONTENT" \
    --handDict="$HAND_DICT" \
    --method="iroute" 2>&1 | tee $OUTPUT_FILE

# Extract directory from output
# Output line format: "Created results directory: /path/to/dir"
RESULT_DIR=$(grep "Created results directory:" $OUTPUT_FILE | awk '{print $NF}')

if [ -z "$RESULT_DIR" ]; then
    echo "Error: Could not determine results directory from output."
    echo "Check $OUTPUT_FILE for details."
    exit 1
fi

echo "Confirmed Results Directory: $RESULT_DIR"

# 2. Run other methods using the same directory
METHODS=("flooding" "centralized" "exact" "randomk")

for METHOD in "${METHODS[@]}"; do
    echo "--- Running $METHOD ---"
    ./run_experiments.py \
        --program="$PROGRAM" \
        --domains=$DOMAINS \
        --kMax=5 \
        --outputDir="$RESULT_DIR" \
        --simTime=$SIM_TIME \
        --trace="$TRACE" \
        --centroids="$CENTROIDS" \
        --content="$CONTENT" \
        --handDict="$HAND_DICT" \
        --method="$METHOD"
done

echo "=== Experiments Completed ==="

# Generate Plots
echo "=== Generating Plots ==="
python3 plot_all.py --runDir "$RESULT_DIR"

echo "=== Done ==="
echo "Results and plots available in $RESULT_DIR"
# rm $OUTPUT_FILE # Keep for debugging
