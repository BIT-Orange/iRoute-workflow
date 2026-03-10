#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/build/lib

# Result directory
RES_DIR="results/baselines"
mkdir -p $RES_DIR

# Common args
ARGS="--domains=8 --simTime=100 \
      --centroids=dataset/sdm_smartcity_dataset/domain_centroids.csv \
      --content=dataset/sdm_smartcity_dataset/producer_content.csv \
      --trace=dataset/sdm_smartcity_dataset/consumer_trace.csv \
      --resultDir=$RES_DIR"

echo "=== Running Semantic Accuracy Experiment ==="

# 1. Exact Match Baseline
echo "[1/4] Running Exact Match Baseline..."
./build/scratch/iroute-exp-baselines --scheme=exact $ARGS \
  > $RES_DIR/log_exact.txt 2>&1

# 2. Flood Baseline (Producer Responder)
echo "[2/4] Running Flood Baseline (Producer mode)..."
./build/scratch/iroute-exp-baselines --scheme=flood --floodResponder=producer --floodThreshold=0.6 $ARGS \
  > $RES_DIR/log_flood_prod.txt 2>&1

# 3. Tag Baseline
echo "[3/4] Running Tag-based Routing Baseline..."
./build/scratch/iroute-exp-baselines --scheme=tag \
  --tagIndex=dataset/sdm_smartcity_dataset/tag_index.csv \
  --queryToTag=dataset/sdm_smartcity_dataset/query_to_tag.csv \
  $ARGS \
  > $RES_DIR/log_tag.txt 2>&1

# 4. iRoute (Proposed)
echo "[4/4] Running iRoute (Proposed)..."
./build/scratch/iroute-exp-baselines --scheme=iroute --K=3 --tau=0.6 $ARGS \
  > $RES_DIR/log_iroute.txt 2>&1

echo "Experiment Complete. Summary:"
cat $RES_DIR/summary.csv
