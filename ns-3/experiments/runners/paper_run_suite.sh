#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$NS3_DIR"

mkdir -p "$NS3_DIR/.home"
export HOME="$NS3_DIR/.home"
export PATH="$NS3_DIR/.venv/bin:$PATH"
export MPLBACKEND=Agg

OUT_DIR="${1:-results/paper_$(date +%Y%m%d_%H%M%S)}"
"$NS3_DIR/experiments/run_paper_suite.sh" "$OUT_DIR"
