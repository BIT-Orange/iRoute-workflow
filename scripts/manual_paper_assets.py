#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
ASSETS_DIR = REPO_ROOT / "paper" / "assets"
SRC_DIR = ASSETS_DIR / "src"
STATUS_PATH = ASSETS_DIR / "asset_status.json"


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def relpath(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(REPO_ROOT.resolve()))
    except Exception:
        return str(path.resolve())


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def iter_asset_manifests() -> list[Path]:
    return sorted(SRC_DIR.glob("*/asset.json"))


def load_asset_manifest(path: Path) -> dict:
    data = load_json(path)
    data["_manifest_path"] = path
    return data


def find_selected_source(manifest: dict) -> tuple[dict | None, Path | None]:
    for candidate in manifest.get("source_candidates", []):
        source_path = REPO_ROOT / str(candidate.get("path", "")).strip()
        if source_path.exists():
            return candidate, source_path
    return None, None


def inspect_asset(manifest: dict) -> dict:
    manifest_path = manifest["_manifest_path"]
    selected_meta, selected_path = find_selected_source(manifest)
    destination_path = REPO_ROOT / str(manifest.get("destination_path", "")).strip()
    output_exists = destination_path.exists()
    source_exists = selected_path is not None
    export_ready = bool(selected_meta and selected_meta.get("export_ready", False))
    source_sha = sha256_file(selected_path) if selected_path else ""
    output_sha = sha256_file(destination_path) if output_exists else ""
    output_in_sync = bool(source_exists and export_ready and output_exists and source_sha == output_sha)

    if not source_exists and not output_exists:
        status = "blocked"
        reason = "source_missing"
    elif not source_exists and output_exists:
        status = "blocked"
        reason = "output_present_source_missing"
    elif source_exists and not output_exists:
        status = "blocked"
        reason = "source_present_output_missing"
    elif source_exists and not export_ready:
        status = "blocked"
        reason = "source_present_output_unverified"
    elif output_in_sync:
        status = "available"
        reason = "source_and_output_in_sync"
    else:
        status = "blocked"
        reason = "output_present_out_of_sync"

    source_dir = Path(manifest_path).parent
    notes = list(manifest.get("notes", []))
    if reason == "source_missing":
        notes.append("No accepted source candidate exists under the source-managed asset directory yet.")
    elif reason == "source_present_output_missing":
        notes.append("A managed source exists, but the paper-facing exported file is still missing.")
    elif reason == "source_present_output_unverified":
        notes.append("A managed source exists, but the helper cannot verify export/sync for the current source format.")
    elif reason == "output_present_source_missing":
        notes.append("A paper-facing output exists without a tracked managed source; this is stale and must be fixed.")
    elif reason == "output_present_out_of_sync":
        notes.append("The paper-facing output exists but does not match the managed export-ready source.")

    return {
        "asset_id": manifest.get("asset_id", ""),
        "paper_ref": manifest.get("paper_ref", ""),
        "title": manifest.get("title", ""),
        "category": manifest.get("category", "paper_static_asset"),
        "status": status,
        "status_reason": reason,
        "management": manifest.get("management", "manual"),
        "asset_manifest": relpath(manifest_path),
        "destination_path": relpath(destination_path),
        "source": {
            "kind": "source_managed_slot" if not source_exists else "source_managed_file",
            "path": relpath(source_dir),
            "exists": source_exists,
            "selected_file": relpath(selected_path) if selected_path else "",
            "selected_kind": str(selected_meta.get("kind", "")) if selected_meta else "",
            "export_ready": export_ready,
            "accepted_candidates": [candidate.get("path", "") for candidate in manifest.get("source_candidates", [])],
            "legacy_mentions": manifest.get("legacy_mentions", []),
            "sha256": source_sha,
        },
        "output": {
            "path": relpath(destination_path),
            "exists": output_exists,
            "in_sync": output_in_sync,
            "sha256": output_sha,
            "sync_mode": str(manifest.get("sync", {}).get("mode", "")),
            "destination_format": str(manifest.get("sync", {}).get("destination_format", "")),
        },
        "notes": notes,
    }


def sync_asset(manifest: dict, *, dry_run: bool) -> tuple[dict, str]:
    state = inspect_asset(manifest)
    destination_path = REPO_ROOT / state["destination_path"]
    selected_file = state["source"]["selected_file"]
    if state["status_reason"] == "source_missing":
        return state, "source missing"
    if state["status_reason"] == "source_present_output_unverified":
        return state, "source exists but helper has no reproducible export path for this format"
    if not selected_file:
        return state, "no selected source file"
    selected_path = REPO_ROOT / selected_file
    if not state["source"]["export_ready"]:
        return state, "selected source is not export-ready"
    if dry_run:
        return state, f"would copy {relpath(selected_path)} -> {state['destination_path']}"
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(selected_path, destination_path)
    refreshed = inspect_asset(manifest)
    if refreshed["status"] != "available":
        return refreshed, f"copied output but asset is still {refreshed['status_reason']}"
    return refreshed, f"synced {relpath(selected_path)} -> {refreshed['destination_path']}"


def write_status_snapshot(states: list[dict]) -> None:
    payload = {
        "schema_version": 2,
        "generated_at_utc": now_utc(),
        "paper": "paper/main.tex",
        "assets": states,
    }
    write_json(STATUS_PATH, payload)


def command_sync(args: argparse.Namespace) -> int:
    manifests = [load_asset_manifest(path) for path in iter_asset_manifests()]
    selected_ids = set(args.asset_id or [])
    states = []
    for manifest in manifests:
        asset_id = str(manifest.get("asset_id", "")).strip()
        if selected_ids and asset_id not in selected_ids:
            states.append(inspect_asset(manifest))
            continue
        state, message = sync_asset(manifest, dry_run=args.dry_run)
        print(f"[paper-assets] {asset_id}: {message}")
        states.append(state)
    write_status_snapshot(states)
    print(f"[paper-assets] wrote {relpath(STATUS_PATH)}")
    return 0


def command_status(args: argparse.Namespace) -> int:
    manifests = [load_asset_manifest(path) for path in iter_asset_manifests()]
    states = [inspect_asset(manifest) for manifest in manifests]
    write_status_snapshot(states)
    for state in states:
        print(
            "[paper-assets] "
            f"{state['asset_id']} status={state['status']} "
            f"reason={state['status_reason']} "
            f"source_exists={state['source']['exists']} "
            f"output_exists={state['output']['exists']} "
            f"in_sync={state['output']['in_sync']}"
        )
    print(f"[paper-assets] wrote {relpath(STATUS_PATH)}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Manage source-tracked manual paper assets.")
    sub = parser.add_subparsers(dest="command", required=True)

    sync = sub.add_parser("sync", help="Attempt to synchronize export-ready managed paper assets into paper/figs/ and refresh asset_status.json.")
    sync.add_argument("--asset-id", action="append", default=[], help="Optional asset_id selector.")
    sync.add_argument("--dry-run", action="store_true", help="Report actions without copying outputs.")

    status = sub.add_parser("status", help="Refresh asset_status.json from the source-managed manifests without copying files.")
    status.add_argument("--asset-id", action="append", default=[], help="Reserved for future selective refresh.")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "sync":
        return command_sync(args)
    if args.command == "status":
        return command_status(args)
    raise SystemExit(f"unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
