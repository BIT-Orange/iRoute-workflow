#!/usr/bin/env python3

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from tempfile import TemporaryDirectory


REPO_ROOT = Path(__file__).resolve().parents[1]
NS3_DIR = REPO_ROOT / "ns-3"
DATASET_DIR = Path(os.environ.get("IROUTE_DATASET_ROOT", REPO_ROOT / "dataset"))
DATASET_PROCESSED_DIR = Path(os.environ.get("IROUTE_DATASET_PROCESSED_ROOT", DATASET_DIR / "processed"))
LEGACY_DATASET_DIR = Path(os.environ.get("IROUTE_LEGACY_DATASET_ROOT", NS3_DIR / "dataset"))
RESULTS_DIR = Path(os.environ.get("IROUTE_RESULTS_ROOT", REPO_ROOT / "results"))
RUNS_DIR = RESULTS_DIR / "runs"
AGGREGATES_DIR = RESULTS_DIR / "aggregates"
FIGURES_DIR = Path(os.environ.get("IROUTE_RESULTS_FIGURES_ROOT", RESULTS_DIR / "figures"))
WRITE_RUN_MANIFEST = REPO_ROOT / "ns-3" / "experiments" / "manifests" / "write_run_manifest.py"

CORE_ARTIFACTS = {
    "summary.csv": True,
    "query_log.csv": True,
    "latency_sanity.csv": True,
    "failure_sanity.csv": False,
}

SUMMARY_ARTIFACT = "summary.csv"
QUERY_ARTIFACT = "query_log.csv"
LATENCY_ARTIFACT = "latency_sanity.csv"

RUN_CLASSES = {"smoke", "exploratory", "paper_grade"}


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def log(msg: str) -> None:
    print(f"[pipeline] {msg}")


def warn(msg: str) -> None:
    print(f"[pipeline][WARN] {msg}")


def fail(msg: str) -> "NoReturn":
    raise SystemExit(f"[pipeline][FAIL] {msg}")


def resolve_dataset_file(rel_path: str) -> Path:
    canonical = DATASET_PROCESSED_DIR / rel_path
    if canonical.exists():
        return canonical
    legacy = LEGACY_DATASET_DIR / rel_path
    if legacy.exists():
        warn(
            f"canonical dataset root {DATASET_PROCESSED_DIR} is not populated for {rel_path}; "
            f"falling back to legacy dataset root {LEGACY_DATASET_DIR}"
        )
        return legacy
    fail(f"missing dataset file: canonical={canonical} legacy={legacy}")


def relpath_or_abs(path: Path, root: Path = REPO_ROOT) -> str:
    try:
        resolved = path.resolve()
        root_resolved = root.resolve()
        if resolved.is_relative_to(root_resolved):
            return os.path.relpath(resolved, root_resolved)
        return str(resolved)
    except Exception:
        return str(path.resolve())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def copy_if_exists(source: Path, dest: Path):
    if not source.exists():
        return None
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)
    return dest


def current_git_commit() -> str:
    try:
        return (
            subprocess.check_output(
                ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
                stderr=subprocess.DEVNULL,
                text=True,
            )
            .strip()
        )
    except Exception:
        return ""


def detect_source_manifests(source_dir: Path):
    direct_manifest_path = source_dir / "manifest.json"
    run_manifest_path = source_dir / "run_manifest.json"
    direct_manifest = load_json(direct_manifest_path) if direct_manifest_path.exists() else None
    run_manifest = load_json(run_manifest_path) if run_manifest_path.exists() else None
    return direct_manifest_path, direct_manifest, run_manifest_path, run_manifest


def infer_cache_mode(run_manifest: dict | None, direct_manifest: dict | None) -> tuple[str, int | None]:
    fields = (run_manifest or {}).get("fields", {})
    if "cache_mode" in fields:
        cache_mode = str(fields["cache_mode"]).strip()
    elif direct_manifest and "cache_mode" in direct_manifest:
        cache_mode = str(direct_manifest["cache_mode"]).strip()
    else:
        cache_mode = ""

    cs_size = None
    if "cs_size" in fields:
        try:
            cs_size = int(fields["cs_size"])
        except Exception:
            cs_size = None
    elif direct_manifest and "cs_size" in direct_manifest:
        try:
            cs_size = int(direct_manifest["cs_size"])
        except Exception:
            cs_size = None

    if not cache_mode:
        if cs_size is not None:
            cache_mode = "enabled" if cs_size > 0 else "disabled"
        else:
            cache_mode = "unknown"
    return cache_mode, cs_size


def extract_major_options(run_manifest: dict | None, direct_manifest: dict | None) -> dict:
    fields = (run_manifest or {}).get("fields", {})
    options = {}
    direct_keys = [
        "scheme",
        "seed",
        "topology",
        "domains",
        "sim_time",
        "frequency",
        "warmup_sec",
        "measure_start_sec",
        "cs_size",
        "data_freshness_ms",
    ]
    for key in direct_keys:
        if direct_manifest and key in direct_manifest:
            options[key] = direct_manifest[key]
    field_keys = [
        "run_mode",
        "paper_grade",
        "domains",
        "sim_time",
        "frequency",
        "topology",
        "topology_file",
        "seed",
        "seeds",
        "sweep_param",
        "sweep_value",
        "query_repeat_factor",
        "trace_repeat_mode",
        "cache_mode",
        "cs_size",
    ]
    for key in field_keys:
        if key in fields:
            options[key] = fields[key]
    return options


def build_pipeline_manifest(
    *,
    run_id: str,
    run_class: str,
    workflow: str,
    runner: str,
    source_dir: Path,
    dest_dir: Path,
    source_kind: str,
    direct_manifest_path: Path | None,
    direct_manifest: dict | None,
    run_manifest_path: Path | None,
    run_manifest: dict | None,
    artifact_records: list[dict],
    notes: list[str] | None = None,
) -> dict:
    fields = (run_manifest or {}).get("fields", {})
    cache_mode, cs_size = infer_cache_mode(run_manifest, direct_manifest)
    seed_provenance = fields.get("seed_provenance")
    if seed_provenance is None and direct_manifest:
        seed_provenance = direct_manifest.get("seed_provenance")
    if seed_provenance is None:
        seed_provenance = "unknown"

    git_commit = (run_manifest or {}).get("git_commit", "") or current_git_commit()
    manifest = {
        "schema_version": 1,
        "run_id": run_id,
        "run_class": run_class,
        "workflow": workflow,
        "runner": runner,
        "source_kind": source_kind,
        "source_dir": relpath_or_abs(source_dir),
        "canonical_dir": relpath_or_abs(dest_dir),
        "promoted_at_utc": now_utc(),
        "git_commit": git_commit,
        "cache_mode": cache_mode,
        "cs_size": cs_size,
        "seed_provenance": str(seed_provenance),
        "major_options": extract_major_options(run_manifest, direct_manifest),
        "compatibility": {
            "legacy_results_path": relpath_or_abs(source_dir) if source_dir.resolve().is_relative_to((NS3_DIR / "results").resolve()) else "",
            "legacy_status": "legacy_import" if source_kind == "legacy_ns3_results" else "canonical_staging",
        },
        "source_manifests": {
            "direct_manifest": relpath_or_abs(direct_manifest_path) if direct_manifest_path and direct_manifest_path.exists() else "",
            "runner_manifest": relpath_or_abs(run_manifest_path) if run_manifest_path and run_manifest_path.exists() else "",
        },
        "artifacts": artifact_records,
        "notes": notes or [],
    }
    if run_class == "paper_grade":
        paper_grade_flag = bool(fields.get("paper_grade", False))
        if not paper_grade_flag:
            fail(f"refusing to label {run_id} as paper_grade without source paper_grade=true metadata")
        if cache_mode != "disabled":
            fail(f"paper_grade run {run_id} must have cache_mode=disabled, got {cache_mode}")
        if str(seed_provenance) != "native":
            fail(f"paper_grade run {run_id} must have seed_provenance=native, got {seed_provenance}")
    return manifest


def promote_run(
    *,
    source_dir: Path,
    run_id: str,
    run_class: str,
    workflow: str,
    runner: str,
    source_kind: str,
) -> Path:
    if run_class not in RUN_CLASSES:
        fail(f"invalid run_class={run_class}, expected one of {sorted(RUN_CLASSES)}")
    source_dir = source_dir.resolve()
    if not source_dir.is_dir():
        fail(f"source_dir does not exist: {source_dir}")

    dest_dir = RUNS_DIR / run_id
    if dest_dir.exists() and any(dest_dir.iterdir()):
        fail(f"destination already exists and is non-empty: {dest_dir}")
    dest_dir.mkdir(parents=True, exist_ok=True)

    direct_manifest_path, direct_manifest, run_manifest_path, run_manifest = detect_source_manifests(source_dir)
    notes: list[str] = []
    if not run_manifest:
        notes.append("source runner manifest missing; promotion used direct manifest and inferred metadata")
    if not direct_manifest:
        notes.append("source direct manifest missing; artifact metadata is partial")

    copied_direct_manifest = copy_if_exists(direct_manifest_path, dest_dir / "source_manifest.json") if direct_manifest_path else None
    copied_run_manifest = copy_if_exists(run_manifest_path, dest_dir / "source_run_manifest.json") if run_manifest_path else None

    artifact_records: list[dict] = []
    for filename, required in CORE_ARTIFACTS.items():
        source_path = source_dir / filename
        if not source_path.exists():
            if required:
                fail(f"required artifact missing from source: {source_path}")
            continue
        dest_path = dest_dir / filename
        shutil.copy2(source_path, dest_path)
        artifact_records.append(
            {
                "artifact_type": filename,
                "required": required,
                "source_path": relpath_or_abs(source_path),
                "canonical_path": relpath_or_abs(dest_path),
                "size_bytes": dest_path.stat().st_size,
                "sha256": sha256_file(dest_path),
            }
        )

    if copied_direct_manifest:
        artifact_records.append(
            {
                "artifact_type": "source_manifest.json",
                "required": False,
                "source_path": relpath_or_abs(direct_manifest_path),
                "canonical_path": relpath_or_abs(copied_direct_manifest),
                "size_bytes": copied_direct_manifest.stat().st_size,
                "sha256": sha256_file(copied_direct_manifest),
            }
        )
    if copied_run_manifest:
        artifact_records.append(
            {
                "artifact_type": "source_run_manifest.json",
                "required": False,
                "source_path": relpath_or_abs(run_manifest_path),
                "canonical_path": relpath_or_abs(copied_run_manifest),
                "size_bytes": copied_run_manifest.stat().st_size,
                "sha256": sha256_file(copied_run_manifest),
            }
        )

    pipeline_manifest = build_pipeline_manifest(
        run_id=run_id,
        run_class=run_class,
        workflow=workflow,
        runner=runner,
        source_dir=source_dir,
        dest_dir=dest_dir,
        source_kind=source_kind,
        direct_manifest_path=direct_manifest_path if direct_manifest_path and direct_manifest_path.exists() else None,
        direct_manifest=direct_manifest,
        run_manifest_path=run_manifest_path if run_manifest_path and run_manifest_path.exists() else None,
        run_manifest=run_manifest,
        artifact_records=artifact_records,
        notes=notes,
    )
    write_json(dest_dir / "manifest.json", pipeline_manifest)
    log(f"promoted {run_id} -> {dest_dir}")
    return dest_dir


def run_smoke(args) -> int:
    binary = NS3_DIR / "build" / "scratch" / "iroute-exp-baselines"
    waf = NS3_DIR / "waf"
    if not waf.exists():
        fail(f"missing waf at {waf}")
    if not binary.exists():
        fail(f"missing compiled smoke binary at {binary}; build ns-3 first")

    run_id = args.run_id or f"smoke-iroute-star-s42-{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    trace = resolve_dataset_file("sdm_smartcity_dataset/consumer_trace.csv")
    centroids = resolve_dataset_file("sdm_smartcity_dataset/domain_centroids_m4.csv")
    content = resolve_dataset_file("sdm_smartcity_dataset/producer_content.csv")
    with TemporaryDirectory(prefix="iroute-smoke-stage-") as tmp_dir:
        source_dir = Path(tmp_dir) / run_id
        source_dir.mkdir(parents=True, exist_ok=True)
        home_dir = NS3_DIR / ".home"
        home_dir.mkdir(parents=True, exist_ok=True)

        cmd = [
            str(waf),
            "--run-no-build",
            " ".join(
                [
                    "iroute-exp-baselines",
                    "--scheme=iroute",
                    "--domains=8",
                    "--M=4",
                    "--K=5",
                    "--tau=0.3",
                    "--simTime=3",
                    "--frequency=1",
                    "--seed=42",
                    "--topo=star",
                    "--dataFreshnessMs=60000",
                    "--csSize=0",
                    f"--trace={trace}",
                    f"--centroids={centroids}",
                    f"--content={content}",
                    f"--resultDir={source_dir}",
                    "--shuffleTrace=0",
                    "--warmupSec=0",
                    "--measureStartSec=0",
                    "--cdfSuccessOnly=1",
                    "--failureTargetPolicy=manual",
                ]
            ),
        ]
        log(f"running smoke staging at {source_dir}")
        subprocess.run(cmd, cwd=NS3_DIR, check=True, env={**os.environ, "HOME": str(home_dir)})
        subprocess.run(
            [
                sys.executable,
                str(WRITE_RUN_MANIFEST),
                "--repo-root",
                str(REPO_ROOT),
                "--output",
                str(source_dir / "run_manifest.json"),
                "--workflow",
                "smoke_validation",
                "--runner",
                "ns-3/scratch/iroute-exp-baselines.cc",
                "--output-dir",
                str(source_dir),
                "--input",
                str(trace),
                "--input",
                str(centroids),
                "--input",
                str(content),
                "--field",
                'cache_mode="disabled"',
                "--field",
                "cs_size=0",
                "--field",
                'run_mode="smoke_validation"',
                "--field",
                'seed_provenance="native"',
            ],
            cwd=REPO_ROOT,
            check=True,
        )
        promote_run(
            source_dir=source_dir,
            run_id=run_id,
            run_class="smoke",
            workflow="smoke_validation",
            runner="ns-3/scratch/iroute-exp-baselines.cc",
            source_kind="smoke_staging",
        )
    return 0


def aggregate_runs(_: argparse.Namespace) -> int:
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    AGGREGATES_DIR.mkdir(parents=True, exist_ok=True)

    manifests = sorted(RUNS_DIR.glob("*/manifest.json"))
    run_rows: list[dict] = []
    summary_rows: list[dict] = []
    paper_rows: list[dict] = []
    evidence_rows: list[dict] = []
    warnings: list[str] = []
    duplicate_map: dict[str, list[str]] = {}

    for manifest_path in manifests:
        data = load_json(manifest_path)
        run_id = str(data.get("run_id", "")).strip()
        dir_name = manifest_path.parent.name
        if not run_id:
            fail(f"missing run_id in {manifest_path}")
        if dir_name != run_id:
            fail(f"manifest run_id {run_id} does not match directory {dir_name}")
        duplicate_map.setdefault(run_id, []).append(relpath_or_abs(manifest_path.parent))

        run_row = {
            "run_id": run_id,
            "run_class": data.get("run_class", ""),
            "workflow": data.get("workflow", ""),
            "runner": data.get("runner", ""),
            "source_kind": data.get("source_kind", ""),
            "source_dir": data.get("source_dir", ""),
            "cache_mode": data.get("cache_mode", ""),
            "cs_size": data.get("cs_size", ""),
            "seed_provenance": data.get("seed_provenance", ""),
            "git_commit": data.get("git_commit", ""),
            "promoted_at_utc": data.get("promoted_at_utc", ""),
            "has_summary": int((manifest_path.parent / SUMMARY_ARTIFACT).exists()),
            "has_query_log": int((manifest_path.parent / QUERY_ARTIFACT).exists()),
            "has_latency_sanity": int((manifest_path.parent / LATENCY_ARTIFACT).exists()),
            "has_failure_sanity": int((manifest_path.parent / "failure_sanity.csv").exists()),
        }
        run_rows.append(run_row)

        for artifact in data.get("artifacts", []):
            evidence_rows.append(
                {
                    "run_id": run_id,
                    "run_class": data.get("run_class", ""),
                    "workflow": data.get("workflow", ""),
                    "artifact_type": artifact.get("artifact_type", ""),
                    "canonical_path": artifact.get("canonical_path", ""),
                    "source_path": artifact.get("source_path", ""),
                    "size_bytes": artifact.get("size_bytes", ""),
                    "sha256": artifact.get("sha256", ""),
                }
            )

        summary_path = manifest_path.parent / SUMMARY_ARTIFACT
        if summary_path.exists():
            with summary_path.open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            if rows:
                row = rows[-1]
                merged = {
                    "run_id": run_id,
                    "run_class": data.get("run_class", ""),
                    "workflow": data.get("workflow", ""),
                    "cache_mode": data.get("cache_mode", ""),
                    "seed_provenance": data.get("seed_provenance", ""),
                    "git_commit": data.get("git_commit", ""),
                }
                merged.update(row)
                summary_rows.append(merged)
                if data.get("run_class") == "paper_grade":
                    paper_rows.append(dict(merged))

    duplicates = {run_id: paths for run_id, paths in duplicate_map.items() if len(paths) > 1}
    if duplicates:
        fail(f"duplicate run_id declarations detected: {duplicates}")

    counts = Counter(row["run_class"] for row in run_rows)
    excluded = [row["run_id"] for row in summary_rows if row["run_class"] != "paper_grade"]
    if excluded:
        warnings.append(
            "paper_grade aggregates exclude non-paper-grade runs: " + ", ".join(sorted(excluded))
        )
        warn(warnings[-1])

    def write_csv(path: Path, rows: list[dict]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        if not rows:
            path.write_text("", encoding="utf-8")
            return
        fieldnames = list(rows[0].keys())
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    write_csv(AGGREGATES_DIR / "run_index.csv", run_rows)
    write_csv(AGGREGATES_DIR / "summary_rows.csv", summary_rows)
    write_csv(AGGREGATES_DIR / "paper_grade_summary_rows.csv", paper_rows)
    write_csv(AGGREGATES_DIR / "evidence_index.csv", evidence_rows)

    report = {
        "generated_at_utc": now_utc(),
        "total_runs": len(run_rows),
        "run_class_counts": dict(counts),
        "warnings": warnings,
        "files": {
            "run_index": relpath_or_abs(AGGREGATES_DIR / "run_index.csv"),
            "summary_rows": relpath_or_abs(AGGREGATES_DIR / "summary_rows.csv"),
            "paper_grade_summary_rows": relpath_or_abs(AGGREGATES_DIR / "paper_grade_summary_rows.csv"),
            "evidence_index": relpath_or_abs(AGGREGATES_DIR / "evidence_index.csv"),
        },
    }
    write_json(AGGREGATES_DIR / "aggregate_report.json", report)
    log(f"wrote aggregate outputs under {AGGREGATES_DIR}")
    return 0


def promote_legacy(args) -> int:
    promote_run(
        source_dir=Path(args.source_dir),
        run_id=args.run_id,
        run_class=args.run_class,
        workflow=args.workflow,
        runner=args.runner,
        source_kind=args.source_kind,
    )
    return 0


def build_figure_index() -> dict:
    items = []
    for manifest_path in sorted(FIGURES_DIR.glob("*.figure.json")):
        data = load_json(manifest_path)
        items.append(
            {
                "figure_id": data.get("figure_id", manifest_path.stem),
                "status": data.get("status", ""),
                "figure_path": data.get("figure_path", ""),
                "aggregate_inputs": data.get("aggregate_inputs", []),
                "run_ids": data.get("run_ids", []),
            }
        )
    return {
        "generated_at_utc": now_utc(),
        "count": len(items),
        "items": items,
    }


def write_figure_manifest(args) -> int:
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)
    figure_path = Path(args.figure_path).resolve() if args.figure_path else None
    payload = {
        "figure_id": args.figure_id,
        "title": args.title,
        "status": args.status,
        "figure_path": relpath_or_abs(figure_path) if figure_path else "",
        "figure_exists": bool(figure_path and figure_path.exists()),
        "aggregate_inputs": [relpath_or_abs(Path(p).resolve() if Path(p).is_absolute() else (REPO_ROOT / p)) for p in args.aggregate],
        "run_ids": sorted(set(args.run_id)),
        "notes": args.note,
        "generated_at_utc": now_utc(),
    }
    if figure_path and figure_path.exists() and figure_path.is_file():
        payload["figure_sha256"] = sha256_file(figure_path)
        payload["figure_size_bytes"] = figure_path.stat().st_size
    write_json(FIGURES_DIR / f"{args.figure_id}.figure.json", payload)
    write_json(FIGURES_DIR / "figure_index.json", build_figure_index())
    log(f"wrote figure manifest for {args.figure_id}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Canonical paper-grade promotion and aggregation pipeline.")
    sub = parser.add_subparsers(dest="command", required=True)

    smoke = sub.add_parser("run-smoke", help="Run a tiny smoke validation and promote it into results/runs/")
    smoke.add_argument("--run-id", help="Canonical run ID. Default uses a timestamped smoke ID.")
    smoke.set_defaults(func=run_smoke)

    promote = sub.add_parser("promote", help="Promote an existing per-run output directory into results/runs/")
    promote.add_argument("--source-dir", required=True, help="Source directory containing summary/query/latency artifacts")
    promote.add_argument("--run-id", required=True, help="Canonical run ID under results/runs/")
    promote.add_argument("--run-class", required=True, choices=sorted(RUN_CLASSES))
    promote.add_argument("--workflow", default="legacy_import")
    promote.add_argument("--runner", default="legacy_direct_output")
    promote.add_argument("--source-kind", default="legacy_ns3_results")
    promote.set_defaults(func=promote_legacy)

    aggregate = sub.add_parser("aggregate", help="Aggregate promoted canonical runs under results/aggregates/")
    aggregate.set_defaults(func=aggregate_runs)

    figure = sub.add_parser("figure-manifest", help="Write a figure provenance manifest under results/figures/")
    figure.add_argument("--figure-id", required=True)
    figure.add_argument("--title", required=True)
    figure.add_argument("--status", choices=["placeholder", "partial", "published", "blocked"], required=True)
    figure.add_argument("--aggregate", action="append", default=[], help="Aggregate CSV or JSON input")
    figure.add_argument("--run-id", action="append", default=[], help="Source run ID referenced by the figure")
    figure.add_argument("--figure-path", help="Optional actual figure path")
    figure.add_argument("--note", action="append", default=[])
    figure.set_defaults(func=write_figure_manifest)

    return parser


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
