#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$NS3_DIR"

mkdir -p "$NS3_DIR/.home"
export HOME="$NS3_DIR/.home"
export PATH="$NS3_DIR/.venv/bin:$PATH"

OUT_DIR="${1:-results/paper/exp4-load}"
"$NS3_DIR/experiments/run_load_experiment.sh" "$OUT_DIR"
