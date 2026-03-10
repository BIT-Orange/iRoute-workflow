#!/bin/bash
# =============================================================================
# Experiment: Accuracy vs Vector Dimension (MTU Compliance)
# =============================================================================
# Purpose: Evaluate routing accuracy at different vector dimensions
#          to find optimal dimension that fits within MTU constraints.
#
# MTU Analysis:
#   - Typical MTU: 1500 bytes
#   - NDN Interest overhead: ~100-200 bytes (Name, TLV headers)
#   - SemanticVector: dim × 4 bytes (float32)
#
#   Dimension -> Payload -> Total (with ~200B overhead)
#   64        -> 256B     -> ~456B  ✓ Safe
#   128       -> 512B     -> ~712B  ✓ Safe  
#   192       -> 768B     -> ~968B  ✓ Safe
#   256       -> 1024B    -> ~1224B ✓ Safe (recommended max)
#   384       -> 1536B    -> ~1736B ✗ Exceeds MTU!
#
# =============================================================================

set -e

# Configuration
SPLITS=("trec-dl-2019" "trec-dl-2020")
DIMS=(64 128 192 256)  # 384 exceeds MTU, excluded from main sweep
DOMAINS=8
M=4
EMBED_MODEL="all-MiniLM-L6-v2"
SEED=42

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET_DIR="${SCRIPT_DIR}/dataset"
RESULTS_DIR="${SCRIPT_DIR}/results/exp_dim_sweep"

mkdir -p "${RESULTS_DIR}"

# =============================================================================
# Step 1: Generate datasets for each dimension
# =============================================================================
echo "============================================================"
echo "Step 1: Generating datasets for each dimension"
echo "============================================================"

for SPLIT in "${SPLITS[@]}"; do
    for DIM in "${DIMS[@]}"; do
        OUT_DIR="${DATASET_DIR}/${SPLIT}_dim${DIM}"
        
        if [ -d "${OUT_DIR}" ] && [ -f "${OUT_DIR}/consumer_trace.csv" ]; then
            echo "[SKIP] ${SPLIT} dim=${DIM} already exists"
            continue
        fi
        
        echo ""
        echo "[GENERATE] ${SPLIT} dim=${DIM}..."
        python3 "${DATASET_DIR}/build_msmarco_trec_dl_trace.py" \
            --outDir "${OUT_DIR}" \
            --split "${SPLIT}" \
            --domains ${DOMAINS} \
            --M ${M} \
            --embedModel "${EMBED_MODEL}" \
            --vectorDim ${DIM} \
            --seed ${SEED}
    done
done

echo ""
echo "Dataset generation complete!"

# =============================================================================
# Step 2: Run experiments for each dimension
# =============================================================================
echo ""
echo "============================================================"
echo "Step 2: Running accuracy experiments"
echo "============================================================"

# Combined results file
COMBINED_CSV="${RESULTS_DIR}/dim_sweep_results.csv"
echo "split,vectorDim,queries,domainCorrect,docCorrect,domainAccuracy,docAccuracy,stage1Correct,stage2GivenStage1" > "${COMBINED_CSV}"

for SPLIT in "${SPLITS[@]}"; do
    for DIM in "${DIMS[@]}"; do
        TRACE_DIR="${DATASET_DIR}/${SPLIT}_dim${DIM}"
        RUN_RESULTS="${RESULTS_DIR}/${SPLIT}_dim${DIM}"
        
        echo ""
        echo "[RUN] ${SPLIT} dim=${DIM}..."
        
        mkdir -p "${RUN_RESULTS}"
        
        # Run the experiment
        ./waf --run "iroute-v2-exp1-accuracy \
            --traceDir=${TRACE_DIR} \
            --runId=${SPLIT}_dim${DIM} \
            --vectorDim=${DIM} \
            --kMax=3 \
            --topo=tree \
            --seed=${SEED}" 2>&1 | tee "${RUN_RESULTS}/run.log"
        
        # Move results
        mv results/exp1/*.csv "${RUN_RESULTS}/" 2>/dev/null || true
        
        # Extract summary metrics
        if [ -f "${RUN_RESULTS}/exp1_summary.csv" ]; then
            # Parse the summary file (second line, data row)
            SUMMARY=$(tail -1 "${RUN_RESULTS}/exp1_summary.csv")
            QUERIES=$(echo "${SUMMARY}" | cut -d',' -f7)
            DOMAIN_ACC=$(echo "${SUMMARY}" | cut -d',' -f12)
            DOC_ACC=$(echo "${SUMMARY}" | cut -d',' -f13)
            DOMAIN_CORRECT=$(echo "${SUMMARY}" | cut -d',' -f12 | awk -F'%' '{print $1}')
            DOC_CORRECT=$(echo "${SUMMARY}" | cut -d',' -f13 | awk -F'%' '{print $1}')
            
            # Count from per-query file
            DOMAIN_CT=$(awk -F',' 'NR>1 {sum+=$5} END {print sum}' "${RUN_RESULTS}/exp1.csv")
            DOC_CT=$(awk -F',' 'NR>1 {sum+=$8} END {print sum}' "${RUN_RESULTS}/exp1.csv")
            STAGE2=$(awk -F',' 'NR>1 {sum+=$9} END {print sum}' "${RUN_RESULTS}/exp1.csv")
            
            echo "${SPLIT},${DIM},${QUERIES},${DOMAIN_CT},${DOC_CT},${DOMAIN_ACC},${DOC_ACC},${DOMAIN_CT},${STAGE2}" >> "${COMBINED_CSV}"
        fi
    done
done

echo ""
echo "============================================================"
echo "Step 3: Results Summary"
echo "============================================================"
echo ""
cat "${COMBINED_CSV}"
echo ""

# =============================================================================
# Step 3: Generate summary and recommendation
# =============================================================================
echo ""
echo "============================================================"
echo "MTU Compliance Analysis"
echo "============================================================"

python3 << 'PYTHON_SCRIPT'
import csv
import sys

results_file = "results/exp_dim_sweep/dim_sweep_results.csv"

# MTU calculation
def calc_wire_size(dim):
    # SemanticVector: TLV header (~10 bytes) + dim * 4 (float32)
    vec_size = 10 + dim * 4
    # Interest: Name (~50-100 bytes) + ApplicationParameters (~20 bytes) + vec
    interest_size = 100 + 20 + vec_size
    return interest_size

print("\nVector Dimension -> Wire Size Analysis:")
print("-" * 60)
print(f"{'Dim':>6} {'VecBytes':>10} {'InterestBytes':>15} {'MTU Status':>15}")
print("-" * 60)

for dim in [64, 128, 192, 256, 384]:
    vec_bytes = dim * 4
    interest_bytes = calc_wire_size(dim)
    status = "✓ Safe" if interest_bytes <= 1500 else "✗ EXCEEDS MTU"
    print(f"{dim:>6} {vec_bytes:>10} {interest_bytes:>15} {status:>15}")

print("-" * 60)
print("\nRecommendation: Use dim ≤ 256 for MTU compliance")

# Parse results
print("\n\nAccuracy vs Dimension:")
print("-" * 70)
print(f"{'Split':>15} {'Dim':>6} {'Queries':>8} {'DomainAcc':>12} {'DocAcc':>12}")
print("-" * 70)

try:
    with open(results_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            split = row.get('split', 'N/A')
            dim = row.get('vectorDim', 'N/A')
            queries = row.get('queries', 'N/A')
            domain_acc = row.get('domainAccuracy', 'N/A')
            doc_acc = row.get('docAccuracy', 'N/A')
            print(f"{split:>15} {dim:>6} {queries:>8} {domain_acc:>12} {doc_acc:>12}")
except FileNotFoundError:
    print("(Results file not yet generated - run experiments first)")

print("-" * 70)
PYTHON_SCRIPT

echo ""
echo "Results saved to: ${RESULTS_DIR}"
echo "  - dim_sweep_results.csv: Combined results"
echo "  - {split}_dim{N}/: Per-dimension results"
echo ""
