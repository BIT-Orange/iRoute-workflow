#!/usr/bin/env python3

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from tempfile import TemporaryDirectory


REPO_ROOT = Path(__file__).resolve().parents[1]
NS3_DIR = REPO_ROOT / "ns-3"
RESULTS_DIR = REPO_ROOT / "results"
RUNS_DIR = RESULTS_DIR / "runs"
AGGREGATES_DIR = RESULTS_DIR / "aggregates"
FIGURES_DIR = RESULTS_DIR / "figures"
PAPER_FIGS_DIR = REPO_ROOT / "paper" / "figs"

LOAD_RUNNER = REPO_ROOT / "ns-3" / "experiments" / "runners" / "run_load_experiment.sh"
SCALING_RUNNER = REPO_ROOT / "ns-3" / "experiments" / "runners" / "run_scaling_experiment.sh"
FAILURE_RUNNER = REPO_ROOT / "ns-3" / "experiments" / "runners" / "run_failure_experiment.sh"
PLOT_SCRIPT = REPO_ROOT / "ns-3" / "experiments" / "plot" / "plot_paper_figures.py"
ARTIFACT_CHECK = REPO_ROOT / "ns-3" / "experiments" / "checks" / "check_artifact_regression.py"
PIPELINE = REPO_ROOT / "scripts" / "paper_grade_pipeline.py"


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def log(msg: str) -> None:
    print(f"[fig345] {msg}")


def fail(msg: str) -> "NoReturn":
    raise SystemExit(f"[fig345][FAIL] {msg}")


def python_bin() -> str:
    venv_python = NS3_DIR / ".venv" / "bin" / "python"
    if venv_python.exists():
        return str(venv_python)
    return sys.executable


def relpath(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(REPO_ROOT.resolve()))
    except Exception:
        return str(path.resolve())


def run(cmd: list[str], *, cwd: Path | None = None, env: dict | None = None) -> None:
    log("running: " + " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd or REPO_ROOT), env=env, check=True)


def ensure_empty_dir(path: Path, label: str) -> None:
    if path.exists() and any(path.iterdir()):
        fail(f"{label} already exists and is non-empty: {path}")
    path.mkdir(parents=True, exist_ok=True)


def load_rows(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_rows(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def family_defaults(family: str, scope: str) -> tuple[str, dict[str, str], list[str]]:
    if family == "load" and scope == "minimal":
        return "fig3-load-paper-grade", {
            "PAPER_GRADE": "1",
            "CACHE_MODE": "disabled",
            "CS_SIZE": "0",
            "RESUME": "0",
            "SCHEMES": "central iroute tag flood",
            "SEEDS": "42",
            "FREQS": "5 10 20",
            "SIM_TIME_MAX": "40",
            "WARMUP_SEC": "0",
            "MEASURE_START_SEC": "0",
        }, [
            "Focused minimal paper-grade batch for pipeline validation.",
            "Full paper-grade reruns are still required before the figure can be treated as published paper evidence.",
        ]
    if family == "load" and scope == "final":
        return "fig3-load-paper-grade-final", {
            "PAPER_GRADE": "1",
            "CACHE_MODE": "disabled",
            "CS_SIZE": "0",
            "RESUME": "0",
            "SCHEMES": "central iroute tag flood",
            "SEEDS": "42",
            "FREQS": "1 2 5 10 20",
            "SIM_TIME_MAX": "0",
            "WARMUP_SEC": "0",
            "MEASURE_START_SEC": "0",
        }, [
            "Final-scope paper-grade Fig. 3 batch matching the current paper-grade load sweep.",
            "SIM_TIME_MAX=0 is used to avoid truncating low-frequency full-trace runs.",
        ]
    if family == "scaling" and scope == "minimal":
        return "fig4-scaling-paper-grade", {
            "PAPER_GRADE": "1",
            "CACHE_MODE": "disabled",
            "CS_SIZE": "0",
            "RESUME": "0",
            "SCHEMES": "central iroute tag flood",
            "SEEDS": "42",
            "DOMAINS_LIST": "8 16",
            "SIM_TIME": "16",
            "FREQUENCY": "2",
            "WARMUP_SEC": "0",
            "MEASURE_START_SEC": "0",
            "CLONE_HIGH_DOMAIN": "0",
        }, [
            "Focused minimal paper-grade batch for pipeline validation.",
            "Full paper-grade reruns are still required before the figure can be treated as published paper evidence.",
        ]
    if family == "scaling" and scope == "final":
        return "fig4-scaling-paper-grade-final", {
            "PAPER_GRADE": "1",
            "CACHE_MODE": "disabled",
            "CS_SIZE": "0",
            "RESUME": "0",
            "SCHEMES": "central iroute tag flood",
            "SEEDS": "42",
            "DOMAINS_LIST": "8 16 32 64",
            "SIM_TIME": "120",
            "FREQUENCY": "5",
            "WARMUP_SEC": "0",
            "MEASURE_START_SEC": "0",
            "CLONE_HIGH_DOMAIN": "0",
        }, [
            "Final-scope paper-grade Fig. 4 batch matching the current paper-grade scaling sweep.",
            "This bundle covers four domain points and uses native seeds only.",
        ]
    if family == "failure" and scope == "final":
        return "fig5-failure-paper-grade-final", {
            "PAPER_GRADE": "1",
            "CACHE_MODE": "disabled",
            "CS_SIZE": "0",
            "SCHEMES": "central iroute flood",
            "SEEDS": "42",
            "SIM_TIME": "32",
            "FREQUENCY": "2",
            "FAIL_TIME": "12",
            "WARMUP_SEC": "0",
            "MEASURE_START_SEC": "0",
            "CHURN_ROUNDS": "1",
            "CHURN_INTERVAL_SEC": "4",
            "CHURN_RECOVERY_SEC": "8",
            "LINK_RECOVERY_SEC": "8",
            "BIN_QUERIES": "10",
        }, [
            "Current paper-suite Fig. 5 scope: central, iRoute, and Flood across churn, link-fail, and domain-fail under native seed 42.",
            "This bundle is publishable only if every promoted run records effective disruption and the paper-facing PDFs are synchronized.",
        ]
    if family == "failure":
        return "fig5-failure-paper-grade", {
            "PAPER_GRADE": "1",
            "CACHE_MODE": "disabled",
            "CS_SIZE": "0",
            "SCHEMES": "central iroute flood",
            "SEEDS": "42",
            "SIM_TIME": "32",
            "FREQUENCY": "2",
            "FAIL_TIME": "12",
            "WARMUP_SEC": "0",
            "MEASURE_START_SEC": "0",
            "CHURN_ROUNDS": "1",
            "CHURN_INTERVAL_SEC": "4",
            "CHURN_RECOVERY_SEC": "8",
            "LINK_RECOVERY_SEC": "8",
            "BIN_QUERIES": "10",
        }, [
            "Focused minimal paper-grade batch for pipeline validation.",
            "Failure-family publication is still blocked pending evidence repair.",
        ]
    fail(f"unsupported family: {family}")


def split_tokens(raw: str) -> list[str]:
    return [token for token in str(raw).split() if token]


def slugify(raw: str) -> str:
    out = re.sub(r"[^A-Za-z0-9._-]+", "-", raw.strip())
    out = re.sub(r"-{2,}", "-", out).strip("-")
    return out or "default"


def final_scope_requirements(family: str) -> dict:
    if family not in {"load", "scaling"}:
        fail(f"final-scope sharding only applies to load/scaling, got {family}")
    prefix, env_defaults, scope_notes = family_defaults(family, "final")
    sweep_env = "FREQS" if family == "load" else "DOMAINS_LIST"
    sweep_field = "frequency" if family == "load" else "domains"
    return {
        "family": family,
        "batch_prefix": prefix,
        "sweep_env": sweep_env,
        "sweep_field": sweep_field,
        "sweep_values": split_tokens(env_defaults[sweep_env]),
        "schemes": split_tokens(env_defaults["SCHEMES"]),
        "seeds": split_tokens(env_defaults["SEEDS"]),
        "env_defaults": env_defaults,
        "scope_notes": scope_notes,
    }


def selection_from_args(family: str, args: argparse.Namespace, requirements: dict) -> dict:
    schemes = split_tokens(args.schemes) if args.schemes else list(requirements["schemes"])
    seeds = split_tokens(args.seeds) if args.seeds else list(requirements["seeds"])
    if family == "load":
        sweep_values = split_tokens(args.frequencies) if args.frequencies else list(requirements["sweep_values"])
    else:
        sweep_values = split_tokens(args.domains_list) if args.domains_list else list(requirements["sweep_values"])

    invalid_schemes = sorted(set(schemes) - set(requirements["schemes"]))
    invalid_seeds = sorted(set(seeds) - set(requirements["seeds"]))
    invalid_sweep = sorted(set(sweep_values) - set(requirements["sweep_values"]))
    if invalid_schemes:
        fail(f"invalid schemes for final-scope {family}: {invalid_schemes}")
    if invalid_seeds:
        fail(f"invalid seeds for final-scope {family}: {invalid_seeds}")
    if invalid_sweep:
        fail(f"invalid {requirements['sweep_field']} values for final-scope {family}: {invalid_sweep}")
    if not schemes or not seeds or not sweep_values:
        fail(f"empty shard selection for final-scope {family}")
    return {
        "schemes": schemes,
        "seeds": seeds,
        "sweep_values": sweep_values,
    }


def make_shard_id(requirements: dict, selection: dict) -> str:
    parts = [f"{requirements['sweep_field']}-{'_'.join(selection['sweep_values'])}"]
    if selection["schemes"] != requirements["schemes"]:
        parts.append(f"schemes-{'_'.join(selection['schemes'])}")
    if selection["seeds"] != requirements["seeds"]:
        parts.append(f"seeds-{'_'.join(selection['seeds'])}")
    return slugify("--".join(parts))


def batch_root(batch_id: str) -> Path:
    return AGGREGATES_DIR / batch_id


def batch_shards_dir(batch_id: str) -> Path:
    return batch_root(batch_id) / "shards"


def batch_status_path(batch_id: str) -> Path:
    return batch_root(batch_id) / "batch_status.json"


def batch_index_path(batch_id: str) -> Path:
    return batch_root(batch_id) / "shard_index.json"


def batch_status_md_path(batch_id: str) -> Path:
    return batch_root(batch_id) / "BATCH_STATUS.md"


def shard_manifest_path(batch_id: str, shard_id: str) -> Path:
    return batch_shards_dir(batch_id) / f"{shard_id}.json"


def shard_stage_dir(batch_id: str, shard_id: str) -> Path:
    return Path("/tmp") / "iroute-fig34-final-scope" / batch_id / shard_id


def run_name_for_selector(family: str, scheme: str, sweep_value: str, seed: str) -> str:
    if family == "load":
        return f"{scheme}_freq{sweep_value}_s{seed}"
    if family == "scaling":
        return f"{scheme}_d{sweep_value}_s{seed}"
    fail(f"unsupported sharded family: {family}")


def run_id_for_name(batch_id: str, run_name: str) -> str:
    return f"{batch_id}-{run_name}"


def expected_run_names(family: str, selection: dict) -> list[str]:
    names = []
    for sweep_value in selection["sweep_values"]:
        for scheme in selection["schemes"]:
            for seed in selection["seeds"]:
                names.append(run_name_for_selector(family, scheme, sweep_value, seed))
    return names


def required_run_ids_for_batch(batch_id: str, requirements: dict) -> list[str]:
    names = expected_run_names(
        requirements["family"],
        {
            "schemes": requirements["schemes"],
            "seeds": requirements["seeds"],
            "sweep_values": requirements["sweep_values"],
        },
    )
    return [run_id_for_name(batch_id, name) for name in names]


def run_dir_complete(run_id: str) -> bool:
    run_dir = RUNS_DIR / run_id
    return run_dir.is_dir() and (run_dir / "manifest.json").exists() and (run_dir / "summary.csv").exists()


def read_last_csv_row(path: Path) -> dict:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        fail(f"CSV is empty: {path}")
    return rows[-1]


def load_run_manifest(run_id: str) -> dict:
    manifest_path = RUNS_DIR / run_id / "manifest.json"
    if not manifest_path.exists():
        fail(f"missing canonical manifest: {manifest_path}")
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def coerce_int(value, default: int = 0) -> int:
    try:
        return int(float(value))
    except Exception:
        return default


def coerce_float(value, default: float = 0.0) -> float:
    try:
        return float(value)
    except Exception:
        return default


def update_shard_index(batch_id: str) -> list[dict]:
    shard_dir = batch_shards_dir(batch_id)
    shard_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for path in sorted(shard_dir.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception as ex:
            entries.append(
                {
                    "path": relpath(path),
                    "status": "invalid",
                    "error": str(ex),
                }
            )
            continue
        entries.append(
            {
                "path": relpath(path),
                "shard_id": data.get("shard_id", path.stem),
                "status": data.get("status", "unknown"),
                "selector": data.get("selector", {}),
                "promoted_run_ids": data.get("promoted_run_ids", []),
                "artifact_check": data.get("artifact_check", {}),
            }
        )
    write_json(
        batch_index_path(batch_id),
        {
            "generated_at_utc": now_utc(),
            "batch_id": batch_id,
            "shards": entries,
        },
    )
    return entries


def stage_environment(overrides: dict[str, str]) -> dict[str, str]:
    env = dict(os.environ)
    env.update(overrides)
    return env


def discover_family_runs(family: str, stage_dir: Path) -> tuple[list[dict], list[Path]]:
    if family == "load":
        rows = load_rows(stage_dir / "load_sweep.csv")
    elif family == "scaling":
        rows = load_rows(stage_dir / "scaling.csv")
    elif family == "failure":
        rows = load_rows(stage_dir / "recovery_summary.csv")
    else:
        fail(f"unsupported family: {family}")

    if not rows:
        fail(f"{family} aggregate CSV is empty in {stage_dir}")

    run_dirs = []
    seen = set()
    for row in rows:
        run_dir = Path(str(row.get("result_dir", "")).strip())
        if not run_dir.is_absolute():
            run_dir = (NS3_DIR / run_dir).resolve()
        key = str(run_dir.resolve())
        if key in seen:
            continue
        if not (run_dir / "summary.csv").exists():
            fail(f"missing summary.csv in staged run dir: {run_dir}")
        seen.add(key)
        run_dirs.append(run_dir)
    return rows, run_dirs


def promote_runs(
    family: str,
    batch_id: str,
    run_dirs: list[Path],
    scope: str,
    *,
    resume_existing: bool = False,
) -> dict[str, str]:
    run_map: dict[str, str] = {}
    for run_dir in run_dirs:
        run_id = f"{batch_id}-{run_dir.name}"
        if resume_existing and run_dir_complete(run_id):
            log(f"reusing canonical run {run_id}")
        else:
            run(
                [
                    python_bin(),
                    str(PIPELINE),
                    "promote",
                    "--source-dir",
                    str(run_dir),
                    "--run-id",
                    run_id,
                    "--run-class",
                    "paper_grade",
                    "--workflow",
                    f"{family}_paper_grade_{scope}",
                    "--runner",
                    f"ns-3/experiments/runners/run_{family}_experiment.sh",
                    "--source-kind",
                    f"{family}_paper_grade_{scope}_staging",
                ]
            )
        run_map[run_dir.name] = run_id
    return run_map


def annotate_rows(rows: list[dict], run_map: dict[str, str]) -> list[dict]:
    out = []
    for row in rows:
        updated = dict(row)
        source_result_dir = str(updated.get("result_dir", "")).strip()
        run_name = Path(source_result_dir).name
        run_id = run_map.get(run_name)
        if not run_id:
            fail(f"missing run_id mapping for {source_result_dir}")
        updated["source_result_dir"] = source_result_dir
        updated["canonical_run_id"] = run_id
        updated["result_dir"] = f"results/runs/{run_id}"
        if "manifest_path" in updated:
            updated["manifest_path"] = f"results/runs/{run_id}/manifest.json"
        updated["run_class"] = "paper_grade"
        updated["cache_mode"] = "disabled"
        updated["seed_provenance"] = "native"
        updated["paper_grade"] = "true"
        out.append(updated)
    return out


def aggregate_bundle_path(batch_id: str) -> Path:
    return AGGREGATES_DIR / batch_id


def figure_bundle_path(batch_id: str) -> Path:
    return FIGURES_DIR / batch_id


def write_family_bundle(
    *,
    family: str,
    scope: str,
    batch_id: str,
    rows: list[dict],
    stage_dir: Path,
) -> tuple[Path, list[Path], Path]:
    bundle_dir = aggregate_bundle_path(batch_id)
    ensure_empty_dir(bundle_dir, f"{family} aggregate bundle")

    aggregate_inputs: list[Path] = []
    if family == "load":
        load_sweep = bundle_dir / "load_sweep.csv"
        load_csv = bundle_dir / "load.csv"
        write_rows(load_sweep, rows)
        write_rows(load_csv, load_rows(stage_dir / "load.csv"))
        aggregate_inputs.extend([load_sweep, load_csv])
    elif family == "scaling":
        scaling_csv = bundle_dir / "scaling.csv"
        write_rows(scaling_csv, rows)
        aggregate_inputs.append(scaling_csv)
    elif family == "failure":
        recovery_csv = bundle_dir / "recovery_summary.csv"
        write_rows(recovery_csv, rows)
        aggregate_inputs.append(recovery_csv)
    else:
        fail(f"unsupported family: {family}")

    report_path = bundle_dir / f"{family}_paper_grade_report.json"
    report = {
        "generated_at_utc": now_utc(),
        "family": family,
        "batch_id": batch_id,
        "aggregate_bundle_dir": relpath(bundle_dir),
        "aggregate_inputs": [relpath(path) for path in aggregate_inputs],
        "run_ids": sorted({row["canonical_run_id"] for row in rows}),
        "status": "partial",
        "policy": {
            "run_class": "paper_grade",
            "cache_mode": "disabled",
            "cs_size": 0,
            "seed_provenance": "native",
        },
        "scope": scope,
        "notes": [
            "Focused minimal paper-grade batch for pipeline validation.",
            "Full paper-grade reruns are still required before the figure can be treated as published paper evidence.",
        ],
    }
    write_json(report_path, report)
    aggregate_inputs.append(report_path)
    return bundle_dir, aggregate_inputs, report_path


def generate_family_figures(
    family: str,
    stage_dir: Path,
    aggregate_dir: Path,
    batch_id: str,
    plot_overrides: dict[str, str] | None = None,
    *,
    allow_existing: bool = False,
) -> tuple[Path, dict[str, Path]]:
    figure_dir = figure_bundle_path(batch_id)
    if allow_existing:
        figure_dir.mkdir(parents=True, exist_ok=True)
    else:
        ensure_empty_dir(figure_dir, f"{family} figure bundle")

    env = dict(os.environ)
    env.setdefault("HOME", str(NS3_DIR / ".home"))
    env["MPLBACKEND"] = "Agg"
    env.setdefault("MPLCONFIGDIR", f"/tmp/iroute-mplcache-{batch_id}")
    env.setdefault("XDG_CACHE_HOME", f"/tmp/iroute-xdg-{batch_id}")

    with TemporaryDirectory(prefix=f"iroute-{family}-empty-") as tmp_dir:
        empty_acc = Path(tmp_dir) / "acc"
        empty_fail = Path(tmp_dir) / "fail"
        empty_acc.mkdir(parents=True, exist_ok=True)
        empty_fail.mkdir(parents=True, exist_ok=True)
        cmd = [
            python_bin(),
            str(PLOT_SCRIPT),
            "--acc-dir",
            str(empty_acc),
            "--fail-dir",
            str(empty_fail if family != "failure" else stage_dir),
            "--output",
            str(figure_dir),
        ]
        if family == "load":
            cmd.extend(["--load-csv", str(aggregate_dir / "load_sweep.csv")])
        if family == "scaling":
            cmd.extend(["--scaling-csv", str(aggregate_dir / "scaling.csv")])
        if family == "failure":
            overrides = plot_overrides or {}
            cmd.extend(["--fail-time", str(overrides.get("FAIL_TIME", "50"))])
            cmd.extend(["--recovery-bin-queries", str(overrides.get("BIN_QUERIES", "20"))])
        run(cmd, env=env)

    expected = {}
    if family == "load":
        expected["fig3_hop_load.paper_grade"] = figure_dir / "fig3_hop_load.pdf"
    elif family == "scaling":
        expected["fig4_state_scaling.paper_grade"] = figure_dir / "fig4_state_scaling.pdf"
    elif family == "failure":
        expected["fig5_recovery_churn.paper_grade"] = figure_dir / "fig5_recovery_churn.pdf"
        expected["fig5_recovery_link-fail.paper_grade"] = figure_dir / "fig5_recovery_link-fail.pdf"
        expected["fig5_recovery_domain-fail.paper_grade"] = figure_dir / "fig5_recovery_domain-fail.pdf"
    else:
        fail(f"unsupported family: {family}")
    return figure_dir, expected


def write_figure_manifest(
    *,
    figure_id: str,
    title: str,
    status: str,
    aggregate_inputs: list[Path],
    run_ids: list[str],
    notes: list[str],
    figure_path: Path | None = None,
    paper_figure_path: Path | None = None,
) -> None:
    cmd = [
        python_bin(),
        str(PIPELINE),
        "figure-manifest",
        "--figure-id",
        figure_id,
        "--title",
        title,
        "--status",
        status,
    ]
    if figure_path and figure_path.exists():
        cmd.extend(["--figure-path", str(figure_path)])
    if paper_figure_path and paper_figure_path.exists():
        cmd.extend(["--paper-figure-path", str(paper_figure_path)])
    for path in aggregate_inputs:
        cmd.extend(["--aggregate", relpath(path)])
    for run_id in run_ids:
        cmd.extend(["--run-id", run_id])
    for note in notes:
        cmd.extend(["--note", note])
    run(cmd)


def refresh_global_aggregates() -> None:
    run([python_bin(), str(PIPELINE), "aggregate"])


def validate_promoted_runs(run_ids: list[str]) -> None:
    for run_id in run_ids:
        run([python_bin(), str(ARTIFACT_CHECK), "--run-dir", str(RUNS_DIR / run_id)])


def build_load_row_from_run(run_id: str, batch_id: str) -> dict:
    manifest = load_run_manifest(run_id)
    summary = read_last_csv_row(RUNS_DIR / run_id / "summary.csv")
    options = manifest.get("major_options", {})
    run_name = run_id.removeprefix(f"{batch_id}-")
    return {
        "run_id": run_name,
        "scheme": str(options.get("scheme", "")),
        "seed": str(coerce_int(options.get("seed", 0))),
        "frequency": str(coerce_int(options.get("frequency", 0))),
        "sim_time": str(coerce_int(options.get("sim_time", 0))),
        "topology": str(options.get("topology", "")),
        "mean_hops": f"{coerce_float(summary.get('mean_hops', 0.0)):.6f}",
        "unique_hops_values": str(coerce_int(summary.get("unique_hops_values", 0))),
        "p50_ms": f"{coerce_float(summary.get('P50_ms', 0.0)):.6f}",
        "p95_ms": f"{coerce_float(summary.get('P95_ms', 0.0)):.6f}",
        "domain_acc": f"{coerce_float(summary.get('DomainAcc', 0.0)):.6f}",
        "ctrl_bytes_per_sec": f"{coerce_float(summary.get('ctrl_bytes_per_sec', 0.0)):.6f}",
        "result_dir": f"results/runs/{run_id}",
        "source_result_dir": str(manifest.get("source_dir", "")),
        "canonical_run_id": run_id,
        "run_class": "paper_grade",
        "cache_mode": str(manifest.get("cache_mode", "unknown")),
        "seed_provenance": str(manifest.get("seed_provenance", "unknown")),
        "paper_grade": "true",
    }


def build_scaling_row_from_run(run_id: str, batch_id: str, requirements: dict) -> dict:
    manifest = load_run_manifest(run_id)
    summary = read_last_csv_row(RUNS_DIR / run_id / "summary.csv")
    options = manifest.get("major_options", {})
    run_name = run_id.removeprefix(f"{batch_id}-")
    default_m = coerce_int(requirements["env_defaults"].get("M_VALUE", 4), 4)
    default_tag_k = coerce_int(requirements["env_defaults"].get("TAG_K", 32), 32)
    scheme = str(options.get("scheme", ""))
    lsdb_theory = 0
    if scheme == "iroute":
        lsdb_theory = coerce_int(options.get("domains", 0)) * default_m
    elif scheme == "tag":
        lsdb_theory = coerce_int(options.get("domains", 0)) * default_tag_k
    return {
        "run_id": run_name,
        "scheme": scheme,
        "domains": str(coerce_int(options.get("domains", 0))),
        "seed": str(coerce_int(options.get("seed", 0))),
        "M": str(default_m),
        "tagK": str(default_tag_k),
        "topology": str(options.get("topology", "")),
        "cache_mode": str(manifest.get("cache_mode", "unknown")),
        "cs_size": str(coerce_int(manifest.get("cs_size", 0))),
        "paper_grade": "true",
        "seed_provenance": str(manifest.get("seed_provenance", "unknown")),
        "lsdb_entries": str(coerce_int(summary.get("avg_LSDB_entries", 0))),
        "fib_entries": str(coerce_int(summary.get("avg_FIB_entries", 0))),
        "lsdb_theory": str(lsdb_theory),
        "result_dir": f"results/runs/{run_id}",
        "manifest_path": f"results/runs/{run_id}/manifest.json",
        "source_result_dir": str(manifest.get("source_dir", "")),
        "canonical_run_id": run_id,
        "run_class": "paper_grade",
    }


def collect_final_scope_rows(family: str, batch_id: str, requirements: dict) -> list[dict]:
    rows = []
    for sweep_value in requirements["sweep_values"]:
        for scheme in requirements["schemes"]:
            for seed in requirements["seeds"]:
                run_name = run_name_for_selector(family, scheme, sweep_value, seed)
                run_id = run_id_for_name(batch_id, run_name)
                if not run_dir_complete(run_id):
                    fail(f"cannot finalize {batch_id}; missing canonical run {run_id}")
                if family == "load":
                    rows.append(build_load_row_from_run(run_id, batch_id))
                else:
                    rows.append(build_scaling_row_from_run(run_id, batch_id, requirements))
    return rows


def write_load_summary_csv(path: Path, rows: list[dict]) -> None:
    grouped: dict[tuple[str, str], list[float]] = defaultdict(list)
    for row in rows:
        grouped[(row["scheme"], row["frequency"])].append(coerce_float(row["mean_hops"], 0.0))
    out_rows = []
    for scheme, frequency in sorted(grouped.keys(), key=lambda item: (item[0], coerce_float(item[1], 0.0))):
        values = grouped[(scheme, frequency)]
        mean_value = sum(values) / max(len(values), 1)
        out_rows.append(
            {
                "scheme": scheme,
                "frequency": frequency,
                "mean_hops": f"{mean_value:.6f}",
            }
        )
    write_rows(path, out_rows)


def write_final_scope_bundle(
    family: str,
    batch_id: str,
    requirements: dict,
    rows: list[dict],
    status: str,
    extra_notes: list[str] | None = None,
) -> tuple[Path, list[Path], Path]:
    bundle_dir = batch_root(batch_id)
    bundle_dir.mkdir(parents=True, exist_ok=True)
    aggregate_inputs: list[Path] = []
    if family == "load":
        load_sweep = bundle_dir / "load_sweep.csv"
        load_csv = bundle_dir / "load.csv"
        write_rows(load_sweep, rows)
        write_load_summary_csv(load_csv, rows)
        aggregate_inputs.extend([load_sweep, load_csv])
    elif family == "scaling":
        scaling_csv = bundle_dir / "scaling.csv"
        write_rows(scaling_csv, rows)
        aggregate_inputs.append(scaling_csv)
    else:
        fail(f"unsupported final-scope bundle family: {family}")

    report_path = bundle_dir / f"{family}_paper_grade_report.json"
    report = {
        "generated_at_utc": now_utc(),
        "family": family,
        "batch_id": batch_id,
        "aggregate_bundle_dir": relpath(bundle_dir),
        "aggregate_inputs": [relpath(path) for path in aggregate_inputs],
        "run_ids": sorted({row["canonical_run_id"] for row in rows}),
        "status": status,
        "policy": {
            "run_class": "paper_grade",
            "cache_mode": "disabled",
            "cs_size": 0,
            "seed_provenance": "native",
        },
        "scope": "final",
        "notes": requirements["scope_notes"] + (extra_notes or []),
    }
    write_json(report_path, report)
    aggregate_inputs.append(report_path)
    return bundle_dir, aggregate_inputs, report_path


def write_batch_status_report(status_path: Path, payload: dict) -> None:
    lines = [
        f"# Final-Scope Batch Status: {payload['batch_id']}",
        "",
        f"- family: `{payload['family']}`",
        f"- status: `{payload['status']}`",
        f"- required {payload['sweep_field']} values: `{' '.join(payload['required_sweep_values'])}`",
        f"- completed {payload['sweep_field']} values: `{' '.join(payload['completed_sweep_values']) or '(none)'}`",
        f"- missing {payload['sweep_field']} values: `{' '.join(payload['missing_sweep_values']) or '(none)'}`",
        f"- required run count: `{payload['required_run_count']}`",
        f"- present run count: `{payload['present_run_count']}`",
        f"- missing run count: `{payload['missing_run_count']}`",
        "",
    ]
    if payload.get("publish_blockers"):
        lines.append("## Publish Blockers")
        lines.append("")
        for item in payload["publish_blockers"]:
            lines.append(f"- {item}")
        lines.append("")
    if payload.get("shard_manifests"):
        lines.append("## Shards")
        lines.append("")
        for item in payload["shard_manifests"]:
            lines.append(f"- `{item['path']}` :: `{item.get('status', 'unknown')}`")
        lines.append("")
    status_path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def compute_final_scope_batch_status(batch_id: str, requirements: dict) -> dict:
    batch_dir = batch_root(batch_id)
    batch_dir.mkdir(parents=True, exist_ok=True)
    shard_entries = update_shard_index(batch_id)
    required_run_ids = required_run_ids_for_batch(batch_id, requirements)
    present_run_ids = [run_id for run_id in required_run_ids if run_dir_complete(run_id)]
    missing_run_ids = [run_id for run_id in required_run_ids if run_id not in present_run_ids]

    completed_sweep_values = []
    missing_sweep_values = []
    coverage = []
    for sweep_value in requirements["sweep_values"]:
        run_ids = [
            run_id_for_name(batch_id, run_name_for_selector(requirements["family"], scheme, sweep_value, seed))
            for scheme in requirements["schemes"]
            for seed in requirements["seeds"]
        ]
        present = [run_id for run_id in run_ids if run_id in present_run_ids]
        missing = [run_id for run_id in run_ids if run_id not in present]
        complete = not missing
        if complete:
            completed_sweep_values.append(sweep_value)
        else:
            missing_sweep_values.append(sweep_value)
        coverage.append(
            {
                requirements["sweep_field"]: sweep_value,
                "required_run_ids": run_ids,
                "present_run_ids": present,
                "missing_run_ids": missing,
                "complete": complete,
            }
        )

    publish_blockers = []
    if missing_sweep_values:
        publish_blockers.append(
            f"missing required {requirements['sweep_field']} coverage: {' '.join(missing_sweep_values)}"
        )
    if missing_run_ids:
        publish_blockers.append(f"missing canonical runs: {len(missing_run_ids)}")

    status = "complete_unpublished" if not publish_blockers else "incomplete"
    payload = {
        "generated_at_utc": now_utc(),
        "batch_id": batch_id,
        "family": requirements["family"],
        "status": status,
        "scope": "final",
        "sweep_field": requirements["sweep_field"],
        "required_sweep_values": requirements["sweep_values"],
        "completed_sweep_values": completed_sweep_values,
        "missing_sweep_values": missing_sweep_values,
        "required_run_count": len(required_run_ids),
        "present_run_count": len(present_run_ids),
        "missing_run_count": len(missing_run_ids),
        "required_run_ids": required_run_ids,
        "present_run_ids": present_run_ids,
        "missing_run_ids": missing_run_ids,
        "coverage": coverage,
        "shard_manifests": shard_entries,
        "publish_blockers": publish_blockers,
        "notes": requirements["scope_notes"],
    }
    write_json(batch_status_path(batch_id), payload)
    write_batch_status_report(batch_status_md_path(batch_id), payload)
    return payload


def write_shard_manifest(batch_id: str, shard_id: str, payload: dict) -> None:
    path = shard_manifest_path(batch_id, shard_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    write_json(path, payload)


def finalize_final_scope_family(
    family: str,
    batch_id: str,
    requirements: dict,
    *,
    publish_paper: bool,
    skip_artifact_check: bool,
) -> dict:
    status_payload = compute_final_scope_batch_status(batch_id, requirements)
    result = {
        "family": family,
        "batch_id": batch_id,
        "status_path": batch_status_path(batch_id),
        "status": status_payload["status"],
        "aggregate_dir": batch_root(batch_id),
        "figure_dir": figure_bundle_path(batch_id),
        "run_ids": status_payload["present_run_ids"],
        "published_paths": {},
    }
    if status_payload["status"] != "complete_unpublished":
        log(
            f"final-scope batch {batch_id} incomplete: "
            f"missing {requirements['sweep_field']} values {' '.join(status_payload['missing_sweep_values']) or '(none)'}"
        )
        return result

    rows = collect_final_scope_rows(family, batch_id, requirements)
    aggregate_dir, aggregate_inputs, report_path = write_final_scope_bundle(
        family,
        batch_id,
        requirements,
        rows,
        status="complete_unpublished",
        extra_notes=["All required final-scope shards are present."],
    )
    figure_dir, expected = generate_family_figures(
        family,
        aggregate_dir,
        aggregate_dir,
        batch_id,
        allow_existing=True,
    )
    run_ids = sorted(status_payload["present_run_ids"])
    published_paths: dict[str, str] = {}
    all_published = True
    for figure_id, figure_path in expected.items():
        base_name = figure_path.stem
        notes = [
            f"batch_id={batch_id}",
            "cache_mode=disabled",
            "run_class=paper_grade",
            "scope=final",
        ]
        notes.extend(requirements["scope_notes"])
        notes.append("All required final-scope shards are present.")
        manifest_status = "partial"
        paper_figure_path = None
        if publish_paper and figure_path.exists():
            paper_figure_path = publish_paper_figure(figure_path, f"{base_name}.pdf")
            manifest_status = "published"
            notes.append(f"paper figure synchronized to {relpath(paper_figure_path)}")
            published_paths[figure_id] = relpath(paper_figure_path)
        else:
            all_published = False
            notes.append("Final-scope figure is complete but not yet synchronized into paper/figs.")
        write_figure_manifest(
            figure_id=figure_id,
            title=f"{base_name} canonical figure provenance",
            status=manifest_status,
            figure_path=figure_path if figure_path.exists() else None,
            paper_figure_path=paper_figure_path,
            aggregate_inputs=aggregate_inputs,
            run_ids=run_ids,
            notes=notes,
        )

    if all_published:
        report = json.loads(report_path.read_text(encoding="utf-8"))
        report["status"] = "published"
        write_json(report_path, report)
        status_payload["status"] = "published"
    write_json(batch_status_path(batch_id), status_payload)
    write_batch_status_report(batch_status_md_path(batch_id), status_payload)
    refresh_global_aggregates()
    if not skip_artifact_check:
        validate_promoted_runs(run_ids)
    result.update(
        {
            "status": status_payload["status"],
            "aggregate_dir": aggregate_dir,
            "figure_dir": figure_dir,
            "report_path": report_path,
            "run_ids": run_ids,
            "published_paths": published_paths,
            "expected_figures": {key: relpath(path) for key, path in expected.items()},
        }
    )
    return result


def execute_final_scope_shard(
    family: str,
    suffix: str,
    skip_artifact_check: bool,
    publish_paper: bool,
    args: argparse.Namespace,
) -> dict:
    requirements = final_scope_requirements(family)
    batch_id = f"{requirements['batch_prefix']}-{suffix}"
    batch_dir = batch_root(batch_id)
    batch_dir.mkdir(parents=True, exist_ok=True)
    selection = selection_from_args(family, args, requirements)
    shard_id = make_shard_id(requirements, selection)
    write_json(
        batch_dir / "batch_config.json",
        {
            "generated_at_utc": now_utc(),
            "batch_id": batch_id,
            "family": family,
            "scope": "final",
            "requirements": {
                "schemes": requirements["schemes"],
                "seeds": requirements["seeds"],
                requirements["sweep_field"]: requirements["sweep_values"],
            },
            "notes": requirements["scope_notes"],
        },
    )

    if args.finalize_only:
        return finalize_final_scope_family(
            family,
            batch_id,
            requirements,
            publish_paper=publish_paper,
            skip_artifact_check=skip_artifact_check,
        )

    env_defaults = dict(requirements["env_defaults"])
    env_defaults["SCHEMES"] = " ".join(selection["schemes"])
    env_defaults["SEEDS"] = " ".join(selection["seeds"])
    env_defaults[requirements["sweep_env"]] = " ".join(selection["sweep_values"])
    env_defaults["RESUME"] = "1" if args.resume_existing else env_defaults.get("RESUME", "0")
    stage_dir = shard_stage_dir(batch_id, shard_id)
    stage_dir.mkdir(parents=True, exist_ok=True)

    expected_names = expected_run_names(family, selection)
    expected_run_ids = [run_id_for_name(batch_id, name) for name in expected_names]
    reused_only = args.resume_existing and all(run_dir_complete(run_id) for run_id in expected_run_ids)
    if reused_only:
        log(f"reusing existing canonical shard {shard_id} for batch {batch_id}")
        promoted_run_ids = expected_run_ids
    else:
        runner = {"load": LOAD_RUNNER, "scaling": SCALING_RUNNER}[family]
        run([str(runner), str(stage_dir)], cwd=REPO_ROOT, env=stage_environment(env_defaults))
        raw_rows, stage_run_dirs = discover_family_runs(family, stage_dir)
        promoted_map = promote_runs(
            family,
            batch_id,
            stage_run_dirs,
            "final",
            resume_existing=args.resume_existing,
        )
        promoted_run_ids = [promoted_map[name] for name in expected_names if name in promoted_map]
        if len(promoted_run_ids) != len(expected_run_ids):
            fail(
                f"shard {shard_id} produced {len(promoted_run_ids)} promoted runs; "
                f"expected {len(expected_run_ids)}"
            )
        if not skip_artifact_check:
            validate_promoted_runs(promoted_run_ids)

    shard_payload = {
        "generated_at_utc": now_utc(),
        "batch_id": batch_id,
        "family": family,
        "scope": "final",
        "shard_id": shard_id,
        "status": "complete",
        "selector": {
            "schemes": selection["schemes"],
            "seeds": selection["seeds"],
            requirements["sweep_field"]: selection["sweep_values"],
        },
        "runner": relpath({"load": LOAD_RUNNER, "scaling": SCALING_RUNNER}[family]),
        "stage_dir": str(stage_dir),
        "expected_run_ids": expected_run_ids,
        "promoted_run_ids": promoted_run_ids,
        "reused_canonical_runs": reused_only,
        "artifact_check": {
            "ran": not skip_artifact_check,
            "status": "passed" if not skip_artifact_check else "skipped",
        },
        "notes": [
            "Shard completion only proves the selected sub-matrix is present.",
            "Claim status stays provisional until the full required final-scope matrix is complete and published.",
        ],
    }
    write_shard_manifest(batch_id, shard_id, shard_payload)
    return finalize_final_scope_family(
        family,
        batch_id,
        requirements,
        publish_paper=publish_paper,
        skip_artifact_check=skip_artifact_check,
    )


def publish_paper_figure(figure_path: Path, paper_name: str) -> Path:
    PAPER_FIGS_DIR.mkdir(parents=True, exist_ok=True)
    dest = PAPER_FIGS_DIR / paper_name
    shutil.copy2(figure_path, dest)
    if sha256_file(dest) != sha256_file(figure_path):
        fail(f"paper figure sync mismatch for {paper_name}")
    return dest


def execute_family(
    family: str,
    suffix: str,
    skip_artifact_check: bool,
    scope: str,
    publish_paper: bool,
    args: argparse.Namespace,
) -> dict:
    if scope == "final" and family in {"load", "scaling"}:
        return execute_final_scope_shard(
            family,
            suffix,
            skip_artifact_check,
            publish_paper,
            args,
        )

    prefix, env_defaults, scope_notes = family_defaults(family, scope)
    batch_id = f"{prefix}-{suffix}"
    stage_dir = Path("/tmp") / f"{batch_id}-stage"
    ensure_empty_dir(stage_dir, f"{family} stage dir")

    runner = {
        "load": LOAD_RUNNER,
        "scaling": SCALING_RUNNER,
        "failure": FAILURE_RUNNER,
    }[family]
    run([str(runner), str(stage_dir)], cwd=REPO_ROOT, env=stage_environment(env_defaults))

    raw_rows, stage_run_dirs = discover_family_runs(family, stage_dir)
    run_map = promote_runs(family, batch_id, stage_run_dirs, scope)
    rows = annotate_rows(raw_rows, run_map)
    aggregate_dir, aggregate_inputs, report_path = write_family_bundle(
        family=family,
        scope=scope,
        batch_id=batch_id,
        rows=rows,
        stage_dir=stage_dir,
    )
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report["notes"] = scope_notes
    report["status"] = "partial" if scope != "final" else "final_scope_pending_publish"
    write_json(report_path, report)

    figure_dir, expected = generate_family_figures(
        family,
        stage_dir,
        aggregate_dir,
        batch_id,
        plot_overrides=env_defaults if family == "failure" else None,
    )
    run_ids = sorted(run_map.values())
    published_paths: dict[str, str] = {}
    all_published = True
    for figure_id, figure_path in expected.items():
        base_name = figure_path.stem
        status = "partial" if figure_path.exists() else "blocked"
        notes = [
            f"batch_id={batch_id}",
            "cache_mode=disabled",
            "run_class=paper_grade",
            f"scope={scope}",
        ]
        notes.extend(scope_notes)
        paper_figure_path = None
        if publish_paper and family in {"load", "scaling", "failure"} and figure_path.exists():
            paper_figure_path = publish_paper_figure(figure_path, f"{base_name}.pdf")
            status = "published"
            notes.append(f"paper figure synchronized to {relpath(paper_figure_path)}")
            published_paths[figure_id] = relpath(paper_figure_path)
        if status == "blocked":
            notes.append(f"plotting did not produce {base_name}.pdf")
            all_published = False
        elif status != "published":
            all_published = False
        write_figure_manifest(
            figure_id=figure_id,
            title=f"{base_name} canonical figure provenance",
            status=status,
            figure_path=figure_path if figure_path.exists() else None,
            paper_figure_path=paper_figure_path,
            aggregate_inputs=aggregate_inputs,
            run_ids=run_ids,
            notes=notes,
        )

    if scope == "final" and publish_paper and all_published:
        report["status"] = "published"
        write_json(report_path, report)

    refresh_global_aggregates()
    if not skip_artifact_check:
        validate_promoted_runs(run_ids)

    return {
        "family": family,
        "batch_id": batch_id,
        "aggregate_dir": aggregate_dir,
        "figure_dir": figure_dir,
        "report_path": report_path,
        "run_ids": run_ids,
        "expected_figures": {key: relpath(path) for key, path in expected.items()},
        "published_paths": published_paths,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run minimal or final-scope paper-grade load/scaling/failure figure pipelines.")
    parser.add_argument(
        "family",
        choices=["load", "scaling", "failure", "all"],
        help="Figure family to run.",
    )
    parser.add_argument(
        "--suffix",
        default=datetime.now().strftime("%Y%m%d_%H%M%S"),
        help="Suffix used to build canonical batch IDs.",
    )
    parser.add_argument(
        "--skip-artifact-check",
        action="store_true",
        help="Skip per-run artifact regression after promotion.",
    )
    parser.add_argument(
        "--scope",
        choices=["minimal", "final"],
        default="minimal",
        help="Run the existing minimal validation matrix or the final-scope Fig.3/Fig.4 matrix.",
    )
    parser.add_argument(
        "--publish-paper",
        action="store_true",
        help="Synchronize final-scope Fig.3/Fig.4 PDFs into paper/figs after generation.",
    )
    parser.add_argument(
        "--frequencies",
        default="",
        help="Optional final-scope load shard selector, e.g. '20' or '1 2'. Only valid for load.",
    )
    parser.add_argument(
        "--domains-list",
        default="",
        help="Optional final-scope scaling shard selector, e.g. '8' or '32 64'. Only valid for scaling.",
    )
    parser.add_argument(
        "--schemes",
        default="",
        help="Optional shard scheme subset for final-scope load/scaling, e.g. 'iroute tag'.",
    )
    parser.add_argument(
        "--seeds",
        default="",
        help="Optional shard seed subset for final-scope load/scaling. Canonical final-scope defaults to '42'.",
    )
    parser.add_argument(
        "--resume-existing",
        action="store_true",
        help="Reuse already-promoted canonical runs for matching final-scope shards and rerun only missing pieces.",
    )
    parser.add_argument(
        "--finalize-only",
        action="store_true",
        help="Do not launch new shards; only recompute final-scope batch completion and finalize if the full matrix already exists.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    families = ["load", "scaling", "failure"] if args.family == "all" else [args.family]
    if args.publish_paper and args.scope != "final":
        fail("--publish-paper is only valid with --scope final")
    if args.finalize_only and args.scope != "final":
        fail("--finalize-only is only valid with --scope final")
    if args.frequencies and args.family not in {"load", "all"}:
        fail("--frequencies is only valid for load")
    if args.domains_list and args.family not in {"scaling", "all"}:
        fail("--domains-list is only valid for scaling")
    results = []
    for family in families:
        if family != "load" and args.frequencies:
            fail("--frequencies cannot be used with non-load families")
        if family != "scaling" and args.domains_list:
            fail("--domains-list cannot be used with non-scaling families")
        if family == "failure" and (args.schemes or args.seeds or args.finalize_only or args.resume_existing):
            fail("failure family does not support sharded final-scope controls")
        results.append(execute_family(family, args.suffix, args.skip_artifact_check, args.scope, args.publish_paper, args))
    log("completed families: " + ", ".join(item["family"] for item in results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
