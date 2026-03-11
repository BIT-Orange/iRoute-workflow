#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/iroute-paths.sh"

REPO_ROOT="$IROUTE_REPO_ROOT"
NS3_DIR="$IROUTE_NS3_ROOT"
PAPER_DIR="$IROUTE_PAPER_ROOT"
DEFAULT_SMOKE_ROOT="${IROUTE_SMOKE_ROOT:-/tmp/iroute-automation-smoke}"
LATEST_SMOKE_FILE="$DEFAULT_SMOKE_ROOT/latest-path.txt"

PYTHON_BIN="python3"
if [ -x "$NS3_DIR/.venv/bin/python" ]; then
  PYTHON_BIN="$NS3_DIR/.venv/bin/python"
fi

log() {
  printf '[workflow] %s\n' "$*"
}

skip() {
  printf '[workflow][SKIP] %s\n' "$*"
}

fail() {
  printf '[workflow][FAIL] %s\n' "$*" >&2
  exit 1
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

run_shell_syntax() {
  local files=()
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/ns-3/experiments" -type f -name '*.sh' | sort)
  files+=("$REPO_ROOT/scripts/iroute-paths.sh")
  files+=("$REPO_ROOT/scripts/workflow.sh")
  log "checking shell syntax (${#files[@]} files)"
  for file in "${files[@]}"; do
    bash -n "$file"
  done
}

run_python_compile() {
  local files=()
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/ns-3/experiments" -type f -name '*.py' ! -path '*/__pycache__/*' | sort)
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/scripts" -type f -name '*.py' ! -path '*/__pycache__/*' | sort)
  if [ -f "$REPO_ROOT/dataset/calc_recovery.py" ]; then
    files+=("$REPO_ROOT/dataset/calc_recovery.py")
  fi
  if [ "${#files[@]}" -eq 0 ]; then
    skip "no python files selected for py_compile"
    return 0
  fi
  log "checking python bytecode (${#files[@]} files)"
  "$PYTHON_BIN" -m py_compile "${files[@]}"
}

run_json_sanity() {
  if ! have_cmd jq; then
    skip "jq unavailable; skipping JSON sanity"
    return 0
  fi
  local files=()
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/ns-3/experiments/manifests" -type f -name '*.json' | sort)
  if [ -f "$REPO_ROOT/review/claims/claims_map.json" ]; then
    files+=("$REPO_ROOT/review/claims/claims_map.json")
  fi
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/paper/assets" -type f -name '*.json' 2>/dev/null | sort)
  if [ "${#files[@]}" -eq 0 ]; then
    skip "no JSON manifests found"
    return 0
  fi
  log "checking JSON manifests (${#files[@]} files)"
  for file in "${files[@]}"; do
    jq empty "$file" >/dev/null
  done
}

run_yaml_sanity() {
  local files=()
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/.github/workflows" -type f \( -name '*.yml' -o -name '*.yaml' \) 2>/dev/null | sort)
  while IFS= read -r file; do
    files+=("$file")
  done < <(find "$REPO_ROOT/.github/ISSUE_TEMPLATE" -type f \( -name '*.yml' -o -name '*.yaml' \) 2>/dev/null | sort)
  if [ "${#files[@]}" -eq 0 ]; then
    skip "no workflow YAML files found"
    return 0
  fi
  if ! have_cmd ruby; then
    skip "ruby unavailable; skipping YAML sanity"
    return 0
  fi
  log "checking workflow YAML (${#files[@]} files)"
  ruby -e 'require "yaml"; ARGV.each { |path| YAML.load_file(path) }' "${files[@]}"
}

run_tool_help_checks() {
  log "running utility help checks"
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_artifact_regression.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_claim_evidence.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_lineage.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_paper_figures.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_results.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/repeat_workload.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/manifests/write_run_manifest.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/scripts/fig12_paper_grade.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/scripts/fig345_paper_grade.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/scripts/manual_paper_assets.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/scripts/paper_release_dossier.py" --help >/dev/null
  "$PYTHON_BIN" "$REPO_ROOT/scripts/paper_submission_bundle.py" --help >/dev/null
  HOME="$NS3_DIR/.home" XDG_CACHE_HOME="/tmp/iroute-xdg-cache" \
  MPLBACKEND=Agg MPLCONFIGDIR="/tmp/iroute-mplcache-workflow" \
    "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/plot/plot_paper_figures.py" --help >/dev/null
  if [ -f "$REPO_ROOT/dataset/calc_recovery.py" ]; then
    "$PYTHON_BIN" "$REPO_ROOT/dataset/calc_recovery.py" --help >/dev/null
  fi
}

run_static_manifest_checks() {
  if ! have_cmd jq; then
    skip "jq unavailable; skipping static manifest checks"
    return 0
  fi
  log "checking static workflow manifests"
  jq -e '
    .name == "paper_suite" and
    .runner == "experiments/runners/run_paper_suite.sh" and
    .workflow_semantics.paper_grade == true and
    .default_environment.CACHE_MODE == "disabled" and
    .default_environment.CS_SIZE == "0" and
    .default_environment.PAPER_GRADE == "1"
  ' "$REPO_ROOT/ns-3/experiments/manifests/paper_suite.json" >/dev/null
  jq -e '
    .name == "sanr_baseline" and
    .runner == "experiments/runners/run_sanr_baseline.sh" and
    .workflow_semantics.paper_grade == false and
    .default_environment.CACHE_MODE == "enabled" and
    .default_environment.CS_SIZE == "512"
  ' "$REPO_ROOT/ns-3/experiments/manifests/sanr_baseline.json" >/dev/null
}

run_paper_suite_preflight() {
  log "checking paper-suite static semantics"
  grep -F 'CACHE_MODE="${CACHE_MODE:-disabled}"' "$REPO_ROOT/ns-3/experiments/runners/run_paper_suite.sh" >/dev/null
  grep -F 'CS_SIZE="${CS_SIZE:-0}"' "$REPO_ROOT/ns-3/experiments/runners/run_paper_suite.sh" >/dev/null
  grep -F 'PAPER_GRADE="${PAPER_GRADE:-1}"' "$REPO_ROOT/ns-3/experiments/runners/run_paper_suite.sh" >/dev/null
  grep -F 'paper-grade scaling runs forbid CLONE_HIGH_DOMAIN=1' "$REPO_ROOT/ns-3/experiments/runners/run_scaling_experiment.sh" >/dev/null
  grep -F 'CACHE_MODE="${CACHE_MODE:-enabled}"' "$REPO_ROOT/ns-3/experiments/runners/run_sanr_baseline.sh" >/dev/null
  grep -F 'CS_SIZE="${CS_SIZE:-512}"' "$REPO_ROOT/ns-3/experiments/runners/run_sanr_baseline.sh" >/dev/null
  run_static_manifest_checks
}

run_latex_compile_if_available() {
  local out_dir="/tmp/iroute-paper-preflight"
  mkdir -p "$out_dir"
  if have_cmd latexmk; then
    log "running latexmk compile preflight"
    (cd "$PAPER_DIR" && latexmk -pdf -interaction=nonstopmode -halt-on-error -output-directory="$out_dir" main.tex)
    return 0
  fi
  if have_cmd tectonic; then
    log "running tectonic compile preflight"
    (cd "$PAPER_DIR" && tectonic --outdir "$out_dir" main.tex)
    return 0
  fi
  if have_cmd pdflatex; then
    log "running pdflatex compile preflight"
    (cd "$PAPER_DIR" && pdflatex -interaction=nonstopmode -halt-on-error -output-directory "$out_dir" main.tex >/tmp/iroute-paper-preflight.log)
    return 0
  fi
  skip "no LaTeX tool available; figure/reference preflight completed without compile"
}

run_claim_check() {
  log "running claim-evidence validation"
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_claim_evidence.py" "$@"
}

run_paper_preflight() {
  local status=0
  log "running paper figure preflight"
  if ! "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_paper_figures.py" --paper "$REPO_ROOT/paper/main.tex"; then
    status=1
  fi
  if ! run_claim_check --strict; then
    status=1
  fi
  if [ "$status" -eq 0 ]; then
    run_latex_compile_if_available
  else
    skip "paper preflight failed before LaTeX compile"
  fi
  return "$status"
}

run_smoke() {
  local trace
  local centroids
  local content
  trace="$(iroute_resolve_dataset_file "sdm_smartcity_dataset/consumer_trace.csv")"
  centroids="$(iroute_resolve_dataset_file "sdm_smartcity_dataset/domain_centroids_m4.csv")"
  content="$(iroute_resolve_dataset_file "sdm_smartcity_dataset/producer_content.csv")"
  local binary="$NS3_DIR/build/scratch/iroute-exp-baselines"
  local run_dir="$DEFAULT_SMOKE_ROOT/$(date +%Y%m%d_%H%M%S)"

  if [ ! -x "$NS3_DIR/waf" ]; then
    skip "ns-3/waf not available; skipping smoke run"
    return 0
  fi
  if [ ! -x "$binary" ]; then
    skip "compiled smoke binary missing at $binary; skipping smoke run"
    return 0
  fi
  [ -f "$trace" ] || fail "missing smoke trace: $trace"
  [ -f "$centroids" ] || fail "missing smoke centroids: $centroids"
  [ -f "$content" ] || fail "missing smoke content: $content"

  mkdir -p "$DEFAULT_SMOKE_ROOT" "$NS3_DIR/.home"
  log "running ns-3 smoke validation into $run_dir"
  (
    cd "$NS3_DIR"
    HOME="$NS3_DIR/.home" ./waf --run-no-build "iroute-exp-baselines --scheme=iroute --domains=8 --M=4 --K=5 --tau=0.3 --simTime=3 --frequency=1 --seed=42 --topo=star --dataFreshnessMs=60000 --csSize=0 --trace=$trace --centroids=$centroids --content=$content --resultDir=$run_dir --shuffleTrace=0 --warmupSec=0 --measureStartSec=0 --cdfSuccessOnly=1 --failureTargetPolicy=manual"
  )

  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/manifests/write_run_manifest.py" \
    --repo-root "$REPO_ROOT" \
    --output "$run_dir/run_manifest.json" \
    --workflow smoke_validation \
    --runner scripts/workflow.sh \
    --output-dir "$run_dir" \
    --input "$trace" \
    --input "$centroids" \
    --input "$content" \
    --field 'cache_mode="disabled"' \
    --field 'cs_size=0' \
    --field 'run_mode="smoke_validation"' \
    --field 'seed_provenance="native"'

  printf '%s\n' "$run_dir" > "$LATEST_SMOKE_FILE"
  log "smoke output ready: $run_dir"
}

resolve_artifact_dir() {
  local requested="${1:-}"
  if [ -n "$requested" ]; then
    printf '%s\n' "$requested"
    return 0
  fi
  if [ -n "${IROUTE_ARTIFACT_DIR:-}" ]; then
    printf '%s\n' "$IROUTE_ARTIFACT_DIR"
    return 0
  fi
  if [ -f "$LATEST_SMOKE_FILE" ]; then
    head -n 1 "$LATEST_SMOKE_FILE"
    return 0
  fi
  return 1
}

run_artifact_check() {
  local requested="${1:-}"
  local run_dir
  if ! run_dir="$(resolve_artifact_dir "$requested")"; then
    skip "no artifact directory provided and no smoke output recorded; skipping artifact-check"
    return 0
  fi
  if [ ! -d "$run_dir" ]; then
    if [ -n "$requested" ] || [ -n "${IROUTE_ARTIFACT_DIR:-}" ]; then
      fail "artifact directory not found: $run_dir"
    fi
    skip "recorded smoke output missing: $run_dir"
    return 0
  fi
  log "running artifact regression on $run_dir"
  "$PYTHON_BIN" "$REPO_ROOT/ns-3/experiments/checks/check_artifact_regression.py" --run-dir "$run_dir"
}

run_checks() {
  run_tool_help_checks
  run_static_manifest_checks
}

run_dev_fast() {
  run_lint
  run_checks
  run_paper_suite_preflight
  run_claim_check --summary-only
}

run_merge_gate() {
  run_dev_fast
  run_claim_check --enforce-supported --summary-only
  run_smoke
  run_artifact_check
}

run_paper_release_gate() {
  run_merge_gate
  run_paper_preflight
}

run_ci_local() {
  run_paper_release_gate
}

run_fig12_paper_grade() {
  log "running paper-grade Fig.1/Fig.2 pipeline"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/fig12_paper_grade.py" "$@"
}

run_fig345_paper_grade() {
  log "running paper-grade Fig.3/Fig.4/Fig.5 pipeline"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/fig345_paper_grade.py" "$@"
}

run_fig34_final_scope() {
  log "running final-scope paper-grade Fig.3/Fig.4 pipeline"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/fig345_paper_grade.py" --scope final --publish-paper "$@"
}

run_fig5_paper_grade() {
  log "running paper-grade Fig.5 repair/publish pipeline"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/fig345_paper_grade.py" --scope final --publish-paper failure "$@"
}

run_publish_figure() {
  log "publishing canonical figure into paper/figs"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/paper_grade_pipeline.py" publish-figure "$@"
}

run_paper_assets_sync() {
  log "refreshing source-managed manual paper assets"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/manual_paper_assets.py" sync "$@"
}

run_release_dossier() {
  log "generating paper release dossier"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/paper_release_dossier.py" "$@"
}

run_submission_bundle() {
  log "generating paper submission bundle"
  "$PYTHON_BIN" "$REPO_ROOT/scripts/paper_submission_bundle.py" "$@"
}

run_lint() {
  run_shell_syntax
  run_python_compile
  run_json_sanity
  run_yaml_sanity
}

usage() {
  cat <<'EOF'
Usage: scripts/workflow.sh <command> [args]

Commands:
  lint                  Run shell, Python, JSON, and YAML hygiene checks.
  checks                Run static experiment utility/help and manifest checks.
  dev-fast              Run the fast local developer gate.
  merge-gate            Run the merge-blocking local gate.
  paper-release-gate    Run the strict paper-release gate.
  claim-check [args]    Run claim-evidence validation (--enforce-supported for merge tier, --strict for release tier).
  paper-preflight       Run paper figure preflight and optional LaTeX compile if available.
  artifact-check [dir]  Run artifact regression on a smoke or supplied run directory.
  fig12-paper-grade     Run the canonical paper-grade Fig.1/Fig.2 rerun, promotion, aggregation, and figure sync path.
  fig345-paper-grade    Run the canonical Fig.3/Fig.4/Fig.5 paper-grade staging and figure provenance path.
  fig34-final-scope     Run the final-scope Fig.3/Fig.4 rerun, promotion, and paper figure sync path.
                        Supports sharding via --frequencies/--domains-list/--schemes and status recompute via --finalize-only.
  fig5-paper-grade      Run the current paper-grade Fig.5 rerun, promotion, and paper figure sync path.
  publish-figure        Publish a canonical figure manifest into paper/figs/ after provenance checks.
  paper-assets-sync     Refresh or synchronize source-managed manual paper assets into paper/figs/.
  release-dossier       Generate JSON and Markdown paper release dossier snapshots under review/paper_audit/.
  submission-bundle     Freeze the current paper tree and evidence snapshots into a release_ready or audit_only bundle.
  smoke-run             Run a tiny ns-3 smoke validation if a compiled binary is available.
  paper-suite-preflight Check static paper-suite semantics and manifest defaults.
  ci-local              Compatibility alias for the strict local paper-release gate.
  help                  Show this message.
EOF
}

main() {
  local cmd="${1:-help}"
  shift || true
  case "$cmd" in
    lint)
      run_lint
      ;;
    checks)
      run_checks
      ;;
    dev-fast)
      run_dev_fast
      ;;
    merge-gate)
      run_merge_gate
      ;;
    paper-release-gate)
      run_paper_release_gate
      ;;
    claim-check)
      run_claim_check "$@"
      ;;
    paper-preflight)
      run_paper_preflight
      ;;
    artifact-check)
      run_artifact_check "${1:-}"
      ;;
    fig12-paper-grade)
      run_fig12_paper_grade "$@"
      ;;
    fig345-paper-grade)
      run_fig345_paper_grade "$@"
      ;;
    fig34-final-scope)
      run_fig34_final_scope "$@"
      ;;
    fig5-paper-grade)
      run_fig5_paper_grade "$@"
      ;;
    publish-figure)
      run_publish_figure "$@"
      ;;
    paper-assets-sync)
      run_paper_assets_sync "$@"
      ;;
    release-dossier)
      run_release_dossier "$@"
      ;;
    submission-bundle)
      run_submission_bundle "$@"
      ;;
    smoke-run)
      run_smoke
      ;;
    paper-suite-preflight)
      run_paper_suite_preflight
      ;;
    ci-local)
      run_ci_local
      ;;
    help|-h|--help)
      usage
      ;;
    *)
      fail "unknown command: $cmd"
      ;;
  esac
}

main "$@"
