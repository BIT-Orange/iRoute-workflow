#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


def fail(msg: str) -> bool:
    print(f"[lineage][FAIL] {msg}")
    return False


def ok(msg: str) -> bool:
    print(f"[lineage][OK] {msg}")
    return True


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def require_manifest_fields(path: Path, data: dict) -> bool:
    all_pass = True
    for key in ["generated_at_utc", "workflow", "runner", "output_dir", "git_commit", "inputs", "fields"]:
        if key not in data:
            all_pass &= fail(f"{path} missing key: {key}")
    if not str(data.get("git_commit", "")).strip():
        all_pass &= fail(f"{path} has empty git_commit")
    fields = data.get("fields", {})
    for key in ["cache_mode", "cs_size"]:
        if key not in fields:
            all_pass &= fail(f"{path} missing fields.{key}")
    if fields.get("cache_mode") not in {"enabled", "disabled"}:
        all_pass &= fail(f"{path} has invalid fields.cache_mode={fields.get('cache_mode')}")
    if "inputs" in data and isinstance(data["inputs"], list) and data["inputs"]:
        if any("sha256" in item for item in data["inputs"] if isinstance(item, dict)):
            all_pass &= ok(f"{path} includes input lineage")
        else:
            all_pass &= fail(f"{path} input lineage is missing file hashes")
    else:
        all_pass &= fail(f"{path} has empty input lineage")
    return all_pass


def check_manifest(path: Path) -> bool:
    if not path.exists():
        return fail(f"missing manifest: {path}")
    data = load_json(path)
    all_pass = require_manifest_fields(path, data)
    if all_pass:
        ok(f"manifest fields present: {path}")
    return all_pass


def check_scaling_dir(path: Path) -> bool:
    all_pass = True
    top_manifest = path / "run_manifest.json"
    if not top_manifest.exists():
        return fail(f"missing scaling manifest: {top_manifest}")
    top = load_json(top_manifest)
    all_pass &= require_manifest_fields(top_manifest, top)
    paper_grade = bool(top.get("fields", {}).get("paper_grade", False))
    scaling_csv = path / "scaling.csv"
    if not scaling_csv.exists():
        all_pass &= fail(f"missing scaling.csv: {scaling_csv}")
    else:
        ok(f"found scaling.csv: {scaling_csv}")

    run_manifests = sorted(path.glob("*/run_manifest.json"))
    if not run_manifests:
        all_pass &= fail(f"no per-run manifests under {path}")
    for manifest_path in run_manifests:
        data = load_json(manifest_path)
        all_pass &= require_manifest_fields(manifest_path, data)
        fields = data.get("fields", {})
        provenance = str(fields.get("seed_provenance", "")).strip()
        if not provenance:
            all_pass &= fail(f"{manifest_path} missing seed_provenance")
            continue
        if paper_grade and provenance != "native":
            all_pass &= fail(f"{manifest_path} has non-native provenance in paper-grade mode: {provenance}")
    if all_pass:
        ok(f"scaling lineage checks passed for {path}")
    return all_pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate experiment lineage manifests.")
    parser.add_argument("--manifest", action="append", default=[], help="Manifest JSON to validate")
    parser.add_argument("--scaling-dir", action="append", default=[], help="Scaling result directory to validate")
    args = parser.parse_args()

    all_pass = True
    for manifest in args.manifest:
        all_pass &= check_manifest(Path(manifest))
    for scaling_dir in args.scaling_dir:
        all_pass &= check_scaling_dir(Path(scaling_dir))

    if not args.manifest and not args.scaling_dir:
        parser.error("provide --manifest and/or --scaling-dir")
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
