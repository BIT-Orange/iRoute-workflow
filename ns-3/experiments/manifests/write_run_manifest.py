#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def parse_field(text: str):
    if "=" not in text:
        raise SystemExit(f"invalid --field, expected key=value: {text}")
    key, value = text.split("=", 1)
    key = key.strip()
    value = value.strip()
    if not key:
        raise SystemExit(f"empty field name in: {text}")
    for parser in (json.loads,):
        try:
            return key, parser(value)
        except Exception:
            pass
    if value.lower() in {"true", "false"}:
        return key, value.lower() == "true"
    return key, value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def git_commit(repo_root: Path) -> str:
    try:
        return (
            subprocess.check_output(
                ["git", "-C", str(repo_root), "rev-parse", "HEAD"],
                stderr=subprocess.DEVNULL,
                text=True,
            )
            .strip()
        )
    except Exception:
        return ""


def build_input_record(repo_root: Path, raw_path: str):
    path = Path(raw_path)
    if not path.is_absolute():
        path = (repo_root / raw_path).resolve()
    record = {
        "path": os.path.relpath(path, repo_root) if path.exists() else raw_path,
        "exists": path.exists(),
    }
    if path.exists() and path.is_file():
        record["size_bytes"] = path.stat().st_size
        record["sha256"] = sha256_file(path)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description="Write experiment lineage manifest.")
    parser.add_argument("--output", required=True, help="Manifest JSON path")
    parser.add_argument("--workflow", required=True, help="Workflow name")
    parser.add_argument("--runner", required=True, help="Runner script path")
    parser.add_argument("--output-dir", required=True, help="Primary output directory")
    parser.add_argument("--repo-root", default=".", help="Repository root for git and relative paths")
    parser.add_argument("--input", action="append", default=[], help="Input file path to hash")
    parser.add_argument("--field", action="append", default=[], help="Additional manifest field as key=value")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    fields = {}
    for item in args.field:
        key, value = parse_field(item)
        fields[key] = value

    payload = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "workflow": args.workflow,
        "runner": args.runner,
        "output_dir": args.output_dir,
        "git_commit": git_commit(repo_root),
        "inputs": [build_input_record(repo_root, raw_path) for raw_path in args.input],
        "fields": fields,
    }

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[manifest] wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
