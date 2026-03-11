#!/usr/bin/env python3

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
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


def promote_runs(family: str, batch_id: str, run_dirs: list[Path], scope: str) -> dict[str, str]:
    run_map: dict[str, str] = {}
    for run_dir in run_dirs:
        run_id = f"{batch_id}-{run_dir.name}"
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
) -> tuple[Path, dict[str, Path]]:
    figure_dir = figure_bundle_path(batch_id)
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


def publish_paper_figure(figure_path: Path, paper_name: str) -> Path:
    PAPER_FIGS_DIR.mkdir(parents=True, exist_ok=True)
    dest = PAPER_FIGS_DIR / paper_name
    shutil.copy2(figure_path, dest)
    if sha256_file(dest) != sha256_file(figure_path):
        fail(f"paper figure sync mismatch for {paper_name}")
    return dest


def execute_family(family: str, suffix: str, skip_artifact_check: bool, scope: str, publish_paper: bool) -> dict:
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
    return parser


def main() -> int:
    args = build_parser().parse_args()
    families = ["load", "scaling", "failure"] if args.family == "all" else [args.family]
    if args.publish_paper and args.scope != "final":
        fail("--publish-paper is only valid with --scope final")
    results = []
    for family in families:
        results.append(execute_family(family, args.suffix, args.skip_artifact_check, args.scope, args.publish_paper))
    log("completed families: " + ", ".join(item["family"] for item in results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
