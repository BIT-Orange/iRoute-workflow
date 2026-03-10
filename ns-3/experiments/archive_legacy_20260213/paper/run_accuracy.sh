#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$NS3_DIR"

# Result directory - clean start
RES_DIR="$NS3_DIR/results/paper/accuracy"
rm -rf "$RES_DIR"
mkdir -p "$RES_DIR"

# Common args
COMMON="--domains=8 --simTime=100 \
  --centroids=$NS3_DIR/dataset/sdm_smartcity_dataset/domain_centroids.csv \
  --content=$NS3_DIR/dataset/sdm_smartcity_dataset/producer_content.csv \
  --trace=$NS3_DIR/dataset/sdm_smartcity_dataset/consumer_trace.csv \
  --resultDir=$RES_DIR"

echo "=== Semantic Accuracy Comparison ==="
BIN="$NS3_DIR/build/scratch/iroute-exp-baselines"
export LD_LIBRARY_PATH="$NS3_DIR/build/lib:${LD_LIBRARY_PATH:-}"
echo "Binary: $BIN"
echo ""

# 1. Flood Baseline (Producer Responder)
echo "[1/3] Running Flood Baseline..."
$BIN --scheme=flood --floodResponder=producer --floodThreshold=0.6 $COMMON \
  > "$RES_DIR/log_flood.txt" 2>&1
# Save per-scheme summary
cp "$RES_DIR/summary.csv" "$RES_DIR/summary_flood.csv"
cp "$RES_DIR/query_log.csv" "$RES_DIR/query_log_flood.csv"
echo "  -> Done"

# 2. Tag-based Routing
echo "[2/3] Running Tag-based Routing..."
$BIN --scheme=tag \
  --tagIndex=$NS3_DIR/dataset/sdm_smartcity_dataset/tag_index.csv \
  --queryToTag=$NS3_DIR/dataset/sdm_smartcity_dataset/query_to_tag.csv \
  $COMMON \
  > "$RES_DIR/log_tag.txt" 2>&1
cp "$RES_DIR/summary.csv" "$RES_DIR/summary_tag.csv"
cp "$RES_DIR/query_log.csv" "$RES_DIR/query_log_tag.csv"
echo "  -> Done"

# 3. iRoute (Proposed)
echo "[3/3] Running iRoute (Proposed)..."
$BIN --scheme=iroute --K=3 --tau=0.6 $COMMON \
  > "$RES_DIR/log_iroute.txt" 2>&1
cp "$RES_DIR/summary.csv" "$RES_DIR/summary_iroute.csv"
cp "$RES_DIR/query_log.csv" "$RES_DIR/query_log_iroute.csv"
echo "  -> Done"

# 4. Exact Match (Oracle)
echo "[4/4] Running Exact Match (Oracle)..."
$BIN --scheme=exact \
  --index=$NS3_DIR/dataset/sdm_smartcity_dataset/index_exact.csv \
  $COMMON \
  > "$RES_DIR/log_exact.txt" 2>&1
cp "$RES_DIR/summary.csv" "$RES_DIR/summary_exact.csv"
cp "$RES_DIR/query_log.csv" "$RES_DIR/query_log_exact.csv"
echo "  -> Done"

echo ""
echo "=== All experiments complete ==="
echo ""

# Combine summaries
echo "scheme,totalQueries,measurableQueries,DomainAcc,Recall_at_1,Recall_at_k,meanSSR,P50_ms,P95_ms,mean_hops,ctrl_bytes_per_sec,ctrl_pkts_per_sec,avg_FIB_entries,avg_LSDB_entries" > "$RES_DIR/comparison.csv"
tail -n 1 "$RES_DIR/summary_flood.csv" >> "$RES_DIR/comparison.csv"
tail -n 1 "$RES_DIR/summary_tag.csv" >> "$RES_DIR/comparison.csv"
tail -n 1 "$RES_DIR/summary_iroute.csv" >> "$RES_DIR/comparison.csv"
tail -n 1 "$RES_DIR/summary_exact.csv" >> "$RES_DIR/comparison.csv"

echo "=== Comparison Results ==="
if command -v column &> /dev/null; then
    column -s, -t "$RES_DIR/comparison.csv"
else
    cat "$RES_DIR/comparison.csv"
fi
