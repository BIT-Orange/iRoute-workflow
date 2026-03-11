#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


INCLUDEGRAPHICS_RE = re.compile(r"\\includegraphics(?:\[[^\]]*\])?\{([^}]+)\}")
EVALUATION_FIGURE_RE = re.compile(r"(^|/)fig\d+[_-]")


def candidate_manifest_path(repo_root: Path, figure_ref: str) -> Path:
    base = Path(figure_ref).stem
    return repo_root / "results" / "figures" / f"{base}.paper_grade.figure.json"


def asset_manifest_path(repo_root: Path) -> Path:
    return repo_root / "paper" / "assets" / "asset_status.json"


def load_asset_entries(repo_root: Path) -> tuple[dict[str, dict], Path | None]:
    manifest_path = asset_manifest_path(repo_root)
    if not manifest_path.exists():
        return {}, None
    try:
        payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as ex:
        print(f"[paper-preflight][WARN] failed to read paper asset manifest {manifest_path}: {ex}")
        return {}, manifest_path
    entries = {}
    for item in payload.get("assets", []):
        paper_ref = str(item.get("paper_ref", "")).strip()
        if paper_ref:
            entries[paper_ref] = item
    return entries, manifest_path


def is_evaluation_figure(repo_root: Path, figure_ref: str) -> bool:
    if candidate_manifest_path(repo_root, figure_ref).exists():
        return True
    return bool(EVALUATION_FIGURE_RE.search(figure_ref))


def describe_asset_status(manifest_path: Path | None, figure_ref: str, asset_entry: dict) -> None:
    source = asset_entry.get("source", {}) if isinstance(asset_entry.get("source", {}), dict) else {}
    source_kind = str(source.get("kind", "")).strip() or "unknown"
    source_exists = bool(source.get("exists", False))
    source_path = str(source.get("path", "")).strip()
    print(
        f"[paper-preflight][INFO] paper asset status "
        f"{manifest_path or '<missing asset manifest>'} ref={figure_ref} "
        f"status={asset_entry.get('status', 'unknown')} "
        f"management={asset_entry.get('management', 'unknown')} "
        f"source_kind={source_kind} source_exists={source_exists}"
    )
    if source_path:
        print(f"[paper-preflight][INFO] paper asset source {source_path}")
    for legacy in source.get("legacy_mentions", []):
        print(f"[paper-preflight][INFO] legacy mention {legacy}")
    for note in asset_entry.get("notes", []):
        print(f"[paper-preflight][INFO] note {note}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Preflight-check that all figures referenced by the canonical paper exist.")
    parser.add_argument("--paper", default="paper/main.tex", help="Paper TeX source to inspect")
    parser.add_argument(
        "--only",
        action="append",
        default=[],
        help="Optional figure reference(s) to check, e.g. figs/fig3_hop_load.pdf",
    )
    args = parser.parse_args()

    paper_path = Path(args.paper).resolve()
    if not paper_path.exists():
        print(f"[paper-preflight][FAIL] missing paper source: {paper_path}")
        return 1

    refs = INCLUDEGRAPHICS_RE.findall(paper_path.read_text(encoding="utf-8"))
    if not refs:
        print(f"[paper-preflight][FAIL] no figure references found in {paper_path}")
        return 1
    if args.only:
        wanted = set(args.only)
        refs = [ref for ref in refs if ref in wanted]
        if not refs:
            print(f"[paper-preflight][FAIL] none of the requested figure refs are present in {paper_path}: {sorted(wanted)}")
            return 1

    missing = []
    repo_root = paper_path.parents[1]
    asset_entries, asset_manifest = load_asset_entries(repo_root)
    missing_eval = []
    missing_assets = []
    missing_unknown = []
    inconsistent_assets = []
    for ref in refs:
        figure_path = (paper_path.parent / ref).resolve()
        asset_entry = asset_entries.get(ref)
        if figure_path.exists():
            if asset_entry and str(asset_entry.get("status", "")).strip() == "blocked":
                print(f"[paper-preflight][FAIL] paper asset exists but manifest remains blocked {ref} -> {figure_path}")
                describe_asset_status(asset_manifest, ref, asset_entry)
                inconsistent_assets.append(ref)
                missing.append(ref)
            else:
                print(f"[paper-preflight][OK] {ref}")
        else:
            manifest_path = candidate_manifest_path(repo_root, ref)
            if asset_entry:
                print(f"[paper-preflight][FAIL] missing paper asset {ref} -> {figure_path}")
                describe_asset_status(asset_manifest, ref, asset_entry)
                missing_assets.append(ref)
            elif is_evaluation_figure(repo_root, ref):
                print(f"[paper-preflight][FAIL] missing evaluation figure {ref} -> {figure_path}")
                if manifest_path.exists():
                    try:
                        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                        manifest_status = manifest.get("status", "unknown")
                        print(
                            f"[paper-preflight][INFO] figure provenance {manifest_path} "
                            f"status={manifest_status}"
                        )
                    except Exception as ex:
                        print(f"[paper-preflight][WARN] failed to read provenance {manifest_path}: {ex}")
                else:
                    print(f"[paper-preflight][WARN] no figure provenance manifest for {ref}")
                missing_eval.append(ref)
            else:
                print(f"[paper-preflight][FAIL] missing unclassified paper asset {ref} -> {figure_path}")
                if asset_manifest and asset_entries:
                    print(
                        f"[paper-preflight][INFO] paper asset manifest present at {asset_manifest} "
                        f"but no entry exists for {ref}"
                    )
                missing_unknown.append(ref)
            missing.append(ref)

    if missing:
        if missing_assets:
            print(f"[paper-preflight][FAIL] missing paper assets: {len(missing_assets)}")
        if missing_eval:
            print(f"[paper-preflight][FAIL] missing evaluation figures: {len(missing_eval)}")
        if missing_unknown:
            print(f"[paper-preflight][FAIL] missing unclassified refs: {len(missing_unknown)}")
        if inconsistent_assets:
            print(f"[paper-preflight][FAIL] inconsistent paper asset manifest entries: {len(inconsistent_assets)}")
        print(f"[paper-preflight][FAIL] {len(missing)} missing referenced asset(s)")
        return 1
    print(f"[paper-preflight][OK] all {len(refs)} referenced figures exist")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
