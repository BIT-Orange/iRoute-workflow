#!/usr/bin/env bash

if [ -n "${IROUTE_PATHS_SH_LOADED:-}" ]; then
  return 0 2>/dev/null || exit 0
fi
IROUTE_PATHS_SH_LOADED=1

IROUTE_PATHS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IROUTE_REPO_ROOT="${IROUTE_REPO_ROOT:-$(cd "$IROUTE_PATHS_DIR/.." && pwd)}"
IROUTE_NS3_ROOT="${IROUTE_NS3_ROOT:-$IROUTE_REPO_ROOT/ns-3}"
IROUTE_PAPER_ROOT="${IROUTE_PAPER_ROOT:-$IROUTE_REPO_ROOT/paper}"

IROUTE_DATASET_ROOT="${IROUTE_DATASET_ROOT:-$IROUTE_REPO_ROOT/dataset}"
IROUTE_DATASET_PROCESSED_ROOT="${IROUTE_DATASET_PROCESSED_ROOT:-$IROUTE_DATASET_ROOT/processed}"
IROUTE_RESULTS_ROOT="${IROUTE_RESULTS_ROOT:-$IROUTE_REPO_ROOT/results}"
IROUTE_RESULTS_RUNS_ROOT="${IROUTE_RESULTS_RUNS_ROOT:-$IROUTE_RESULTS_ROOT/runs}"
IROUTE_RESULTS_AGGREGATES_ROOT="${IROUTE_RESULTS_AGGREGATES_ROOT:-$IROUTE_RESULTS_ROOT/aggregates}"
IROUTE_RESULTS_FIGURES_ROOT="${IROUTE_RESULTS_FIGURES_ROOT:-$IROUTE_RESULTS_ROOT/figures}"

IROUTE_LEGACY_DATASET_ROOT="${IROUTE_LEGACY_DATASET_ROOT:-$IROUTE_NS3_ROOT/dataset}"
IROUTE_LEGACY_RESULTS_ROOT="${IROUTE_LEGACY_RESULTS_ROOT:-$IROUTE_NS3_ROOT/results}"
IROUTE_LEGACY_FIGURES_ROOT="${IROUTE_LEGACY_FIGURES_ROOT:-$IROUTE_REPO_ROOT/figures}"

iroute_path_warn() {
  printf '[paths][WARN] %s\n' "$*" >&2
}

iroute_warn_legacy_dataset_once() {
  if [ "${IROUTE_WARNED_LEGACY_DATASET:-0}" != "1" ]; then
    iroute_path_warn "canonical dataset root is $IROUTE_DATASET_PROCESSED_ROOT; falling back to legacy dataset root $IROUTE_LEGACY_DATASET_ROOT"
    export IROUTE_WARNED_LEGACY_DATASET=1
  fi
}

iroute_warn_legacy_results_once() {
  if [ "${IROUTE_WARNED_LEGACY_RESULTS:-0}" != "1" ]; then
    iroute_path_warn "legacy results path requested; canonical output root is $IROUTE_RESULTS_ROOT while compatibility storage remains at $IROUTE_LEGACY_RESULTS_ROOT"
    export IROUTE_WARNED_LEGACY_RESULTS=1
  fi
}

iroute_warn_legacy_figures_once() {
  if [ "${IROUTE_WARNED_LEGACY_FIGURES:-0}" != "1" ]; then
    iroute_path_warn "root figures/ is a legacy compatibility mirror; canonical generated figure root is $IROUTE_RESULTS_FIGURES_ROOT"
    export IROUTE_WARNED_LEGACY_FIGURES=1
  fi
}

iroute_resolve_repo_path() {
  local path="$1"
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
  else
    printf '%s\n' "$IROUTE_REPO_ROOT/$path"
  fi
}

iroute_resolve_results_path() {
  local path="${1:-}"
  if [[ -z "$path" ]]; then
    printf '%s\n' "$IROUTE_RESULTS_ROOT"
    return 0
  fi
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
    return 0
  fi
  case "$path" in
    ns-3/results/*)
      iroute_warn_legacy_results_once
      printf '%s\n' "$IROUTE_REPO_ROOT/$path"
      ;;
    results/*)
      printf '%s\n' "$IROUTE_REPO_ROOT/$path"
      ;;
    *)
      printf '%s\n' "$IROUTE_RESULTS_ROOT/$path"
      ;;
  esac
}

iroute_resolve_figures_path() {
  local path="${1:-}"
  if [[ -z "$path" ]]; then
    printf '%s\n' "$IROUTE_RESULTS_FIGURES_ROOT"
    return 0
  fi
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
    return 0
  fi
  case "$path" in
    figures/*)
      iroute_warn_legacy_figures_once
      printf '%s\n' "$IROUTE_REPO_ROOT/$path"
      ;;
    results/figures/*)
      printf '%s\n' "$IROUTE_REPO_ROOT/$path"
      ;;
    *)
      printf '%s\n' "$IROUTE_RESULTS_FIGURES_ROOT/$path"
      ;;
  esac
}

iroute_resolve_dataset_file() {
  local rel="$1"
  local canonical="$IROUTE_DATASET_PROCESSED_ROOT/$rel"
  if [ -f "$canonical" ]; then
    printf '%s\n' "$canonical"
    return 0
  fi
  local legacy="$IROUTE_LEGACY_DATASET_ROOT/$rel"
  if [ -f "$legacy" ]; then
    iroute_warn_legacy_dataset_once
    printf '%s\n' "$legacy"
    return 0
  fi
  printf '[paths][ERROR] missing dataset file: canonical=%s legacy=%s\n' "$canonical" "$legacy" >&2
  return 1
}

iroute_resolve_topology_file() {
  local path="$1"
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
    return 0
  fi
  local repo_candidate="$IROUTE_REPO_ROOT/$path"
  if [ -f "$repo_candidate" ]; then
    printf '%s\n' "$repo_candidate"
    return 0
  fi
  local ns3_candidate="$IROUTE_NS3_ROOT/$path"
  if [ -f "$ns3_candidate" ]; then
    printf '%s\n' "$ns3_candidate"
    return 0
  fi
  printf '[paths][ERROR] missing topology file: %s\n' "$path" >&2
  return 1
}

iroute_mirror_legacy_figures() {
  local source_dir="$1"
  local subdir="${2:-}"
  local dest="$IROUTE_LEGACY_FIGURES_ROOT"
  if [ -n "$subdir" ]; then
    dest="$dest/$subdir"
  fi
  mkdir -p "$dest"
  if compgen -G "$source_dir/fig*.pdf" >/dev/null; then
    cp "$source_dir"/fig*.pdf "$dest"/
  fi
  if [ -f "$source_dir/figure_index.md" ]; then
    cp "$source_dir/figure_index.md" "$dest/figure_index.md"
  fi
  iroute_warn_legacy_figures_once
}
