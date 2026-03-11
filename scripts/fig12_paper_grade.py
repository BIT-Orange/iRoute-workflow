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

ACCURACY_RUNNER = REPO_ROOT / "ns-3" / "experiments" / "runners" / "run_accuracy_experiment.sh"
PLOT_SCRIPT = REPO_ROOT / "ns-3" / "experiments" / "plot" / "plot_paper_figures.py"
ARTIFACT_CHECK = REPO_ROOT / "ns-3" / "experiments" / "checks" / "check_artifact_regression.py"
CLAIM_CHECK = REPO_ROOT / "ns-3" / "experiments" / "checks" / "check_claim_evidence.py"
PIPELINE = REPO_ROOT / "scripts" / "paper_grade_pipeline.py"


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def log(msg: str) -> None:
    print(f"[fig12] {msg}")


def fail(msg: str) -> "NoReturn":
    raise SystemExit(f"[fig12][FAIL] {msg}")


def relpath(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(REPO_ROOT.resolve()))
    except Exception:
        return str(path.resolve())


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


def write_markdown(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run(cmd: list[str], *, cwd: Path | None = None, env: dict | None = None) -> None:
    log("running: " + " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd or REPO_ROOT), env=env, check=True)


def ensure_empty_dir(path: Path, label: str) -> None:
    if path.exists() and any(path.iterdir()):
        fail(f"{label} already exists and is non-empty: {path}")
    path.mkdir(parents=True, exist_ok=True)


def python_bin() -> str:
    venv_python = NS3_DIR / ".venv" / "bin" / "python"
    if venv_python.exists():
        return str(venv_python)
    return sys.executable


def canonical_run_id(batch_id: str, source_name: str) -> str:
    cleaned = source_name.strip().replace("/", "-")
    return f"{batch_id}-{cleaned}"


def stage_environment() -> dict:
    env = dict(os.environ)
    env.setdefault("PAPER_GRADE", "1")
    env["CACHE_MODE"] = "disabled"
    env["CS_SIZE"] = "0"
    env.setdefault("RESUME", "0")
    return env


def run_accuracy_stage(stage_dir: Path) -> Path:
    ensure_empty_dir(stage_dir, "accuracy staging dir")
    env = stage_environment()
    run([str(ACCURACY_RUNNER), str(stage_dir)], cwd=REPO_ROOT, env=env)
    sweep_csv = stage_dir / "accuracy_sweep.csv"
    if not sweep_csv.exists():
        fail(f"accuracy runner did not produce {sweep_csv}")
    return stage_dir


def discover_stage_runs(stage_dir: Path) -> tuple[list[dict], list[dict], list[dict], list[Path]]:
    sweep_csv = stage_dir / "accuracy_sweep.csv"
    ref_csv = stage_dir / "reference_runs.csv"
    cmp_csv = stage_dir / "comparison.csv"
    if not sweep_csv.exists():
        fail(f"missing stage sweep CSV: {sweep_csv}")
    if not ref_csv.exists():
        fail(f"missing stage reference CSV: {ref_csv}")
    if not cmp_csv.exists():
        fail(f"missing stage comparison CSV: {cmp_csv}")

    sweep_rows = load_rows(sweep_csv)
    ref_rows = load_rows(ref_csv)
    cmp_rows = load_rows(cmp_csv)
    if not sweep_rows:
        fail(f"stage sweep CSV is empty: {sweep_csv}")

    run_dirs: list[Path] = []
    seen: set[str] = set()
    for row in sweep_rows:
        run_dir = Path(str(row.get("result_dir", "")).strip())
        if not run_dir.is_absolute():
            run_dir = (NS3_DIR / run_dir).resolve()
        if run_dir.name in seen:
            continue
        if not (run_dir / "summary.csv").exists():
            fail(f"missing summary.csv in staged run dir: {run_dir}")
        seen.add(run_dir.name)
        run_dirs.append(run_dir)
    return sweep_rows, ref_rows, cmp_rows, run_dirs


def filter_stage_rows(
    *,
    sweep_rows: list[dict],
    ref_rows: list[dict],
    cmp_rows: list[dict],
    run_dirs: list[Path],
    seed_filters: list[str],
    scheme_filters: list[str],
) -> tuple[list[dict], list[dict], list[dict], list[Path]]:
    use_seeds = {str(item).strip() for item in seed_filters if str(item).strip()}
    use_schemes = {str(item).strip() for item in scheme_filters if str(item).strip()}

    def keep_row(row: dict) -> bool:
        if use_seeds and str(row.get("seed", "")).strip() not in use_seeds:
            return False
        if use_schemes and str(row.get("scheme", "")).strip() not in use_schemes:
            return False
        return True

    filtered_sweep = [row for row in sweep_rows if keep_row(row)]
    filtered_ref = [row for row in ref_rows if keep_row(row)]
    filtered_cmp = [
        row for row in cmp_rows
        if (not use_schemes or str(row.get("scheme", "")).strip() in use_schemes)
    ]
    selected_names = {Path(str(row.get("result_dir", "")).strip()).name for row in filtered_sweep}
    filtered_run_dirs = [run_dir for run_dir in run_dirs if run_dir.name in selected_names]

    if use_schemes:
        missing_sweep = sorted(use_schemes - {str(row.get("scheme", "")).strip() for row in filtered_sweep})
        missing_ref = sorted(use_schemes - {str(row.get("scheme", "")).strip() for row in filtered_ref})
        if missing_sweep:
            fail(f"filtered sweep is missing required schemes: {missing_sweep}")
        if missing_ref:
            fail(f"filtered reference set is missing required schemes: {missing_ref}")

    if use_seeds and not filtered_sweep:
        fail(f"no sweep rows remain after seed filter: {sorted(use_seeds)}")
    if use_seeds and not filtered_ref:
        fail(f"no reference rows remain after seed filter: {sorted(use_seeds)}")

    return filtered_sweep, filtered_ref, filtered_cmp, filtered_run_dirs


def promote_stage_runs(stage_run_dirs: list[Path], batch_id: str) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for run_dir in stage_run_dirs:
        source_name = run_dir.name
        run_id = canonical_run_id(batch_id, source_name)
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
                "fig12_paper_grade_accuracy",
                "--runner",
                "ns-3/experiments/runners/run_accuracy_experiment.sh",
                "--source-kind",
                "fig12_accuracy_staging",
            ]
        )
        mapping[source_name] = run_id
    return mapping


def annotate_sweep_rows(rows: list[dict], run_map: dict[str, str]) -> list[dict]:
    out = []
    for row in rows:
        source_result_dir = str(row.get("result_dir", "")).strip()
        source_name = Path(source_result_dir).name
        run_id = run_map.get(source_name)
        if not run_id:
            fail(f"no canonical run mapping for staged result dir: {source_result_dir}")
        updated = dict(row)
        updated["source_result_dir"] = source_result_dir
        updated["canonical_run_id"] = run_id
        updated["result_dir"] = f"results/runs/{run_id}"
        updated["run_class"] = "paper_grade"
        updated["cache_mode"] = "disabled"
        updated["seed_provenance"] = "native"
        updated["paper_grade"] = "true"
        out.append(updated)
    return out


def build_comparison_rows(ref_rows: list[dict], batch_id: str) -> list[dict]:
    group_cols = [
        "oracle_flag",
        "ctrl_bytes_per_sec",
        "domain_acc",
        "domain_recall_at_1",
        "domain_recall_at_3",
        "domain_recall_at_5",
        "doc_hit_at_1",
        "doc_hit_at_10",
        "ndcg_at_10",
        "p50_ms",
        "p95_ms",
        "mean_hops",
        "total_queries",
        "measurable_queries",
        "mean_relset_size",
        "singleton_queries",
        "n_success",
        "timeout_rate",
        "unique_rtt_values",
    ]
    by_scheme: dict[str, list[dict]] = {}
    for row in ref_rows:
        by_scheme.setdefault(str(row.get("scheme", "")).strip(), []).append(row)

    out = []
    for scheme in sorted(by_scheme):
        rows = by_scheme[scheme]
        if not rows:
            continue

        def mean_of(key: str) -> str:
            vals = []
            for row in rows:
                try:
                    vals.append(float(row.get(key, 0.0)))
                except Exception:
                    vals.append(0.0)
            return f"{(sum(vals) / float(len(vals))):.6f}"

        item = {
            "scheme": scheme,
            "batch_id": batch_id,
            "run_class": "paper_grade",
            "cache_mode": "disabled",
            "seed_provenance": "native",
            "paper_grade": "true",
        }
        for key in group_cols:
            item[key] = mean_of(key)
        out.append(item)
    return out


def build_latency_summary(ref_rows: list[dict]) -> list[dict]:
    by_scheme: dict[str, list[dict]] = {}
    for row in ref_rows:
        by_scheme.setdefault(str(row.get("scheme", "")).strip(), []).append(row)

    out: list[dict] = []
    for scheme in sorted(by_scheme):
        rows = by_scheme[scheme]
        if not rows:
            continue

        def mean_of(key: str) -> float:
            vals = []
            for row in rows:
                try:
                    vals.append(float(row.get(key, 0.0)))
                except Exception:
                    vals.append(0.0)
            return sum(vals) / float(len(vals))

        out.append(
            {
                "scheme": scheme,
                "n_runs": str(len(rows)),
                "oracle_flag": rows[0].get("oracle_flag", "0"),
                "p50_ms": f"{mean_of('p50_ms'):.6f}",
                "p95_ms": f"{mean_of('p95_ms'):.6f}",
                "timeout_rate": f"{mean_of('timeout_rate'):.6f}",
                "n_success": f"{mean_of('n_success'):.6f}",
                "unique_rtt_values": f"{mean_of('unique_rtt_values'):.6f}",
                "cache_mode": "disabled",
                "run_class": "paper_grade",
                "seed_provenance": "native",
                "paper_grade": "true",
            }
        )
    return out


def promote_aggregates(
    *,
    batch_id: str,
    sweep_rows: list[dict],
    ref_rows: list[dict],
    cmp_rows: list[dict],
) -> tuple[Path, Path, Path, Path, Path]:
    bundle_dir = AGGREGATES_DIR / batch_id
    ensure_empty_dir(bundle_dir, "aggregate bundle dir")

    sweep_path = bundle_dir / "accuracy_sweep.csv"
    ref_path = bundle_dir / "reference_runs.csv"
    cmp_path = bundle_dir / "comparison.csv"
    latency_path = bundle_dir / "fig2_latency_summary.csv"
    report_path = bundle_dir / "fig12_paper_grade_report.json"

    write_rows(sweep_path, sweep_rows)
    write_rows(ref_path, ref_rows)
    write_rows(cmp_path, cmp_rows)
    write_rows(latency_path, build_latency_summary(ref_rows))

    report = {
        "generated_at_utc": now_utc(),
        "batch_id": batch_id,
        "aggregate_bundle_dir": relpath(bundle_dir),
        "files": {
            "accuracy_sweep": relpath(sweep_path),
            "reference_runs": relpath(ref_path),
            "comparison": relpath(cmp_path),
            "fig2_latency_summary": relpath(latency_path),
        },
        "run_ids": sorted({row["canonical_run_id"] for row in sweep_rows}),
        "reference_run_ids": sorted({row["canonical_run_id"] for row in ref_rows}),
        "policy": {
            "run_class": "paper_grade",
            "cache_mode": "disabled",
            "cs_size": 0,
            "seed_provenance": "native",
        },
    }
    write_json(report_path, report)
    return bundle_dir, sweep_path, ref_path, latency_path, report_path


def generate_figures(batch_id: str, aggregate_bundle_dir: Path) -> Path:
    figure_dir = FIGURES_DIR / batch_id
    ensure_empty_dir(figure_dir, "figure bundle dir")
    fail_dir = aggregate_bundle_dir / "_empty_failure_inputs"
    fail_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.setdefault("HOME", str(NS3_DIR / ".home"))
    env["MPLBACKEND"] = "Agg"
    env.setdefault("MPLCONFIGDIR", f"/tmp/iroute-mplcache-{batch_id}")

    run(
        [
            python_bin(),
            str(PLOT_SCRIPT),
            "--acc-dir",
            str(aggregate_bundle_dir),
            "--fail-dir",
            str(fail_dir),
            "--output",
            str(figure_dir),
        ],
        cwd=REPO_ROOT,
        env=env,
    )

    for required in ("fig1_accuracy_overhead.pdf", "fig2_retrieval_cdf.pdf"):
        if not (figure_dir / required).exists():
            fail(f"plot bundle missing required figure {required} in {figure_dir}")
    return figure_dir


def sync_paper_figures(figure_dir: Path) -> tuple[Path, Path]:
    PAPER_FIGS_DIR.mkdir(parents=True, exist_ok=True)
    fig1_src = figure_dir / "fig1_accuracy_overhead.pdf"
    fig2_src = figure_dir / "fig2_retrieval_cdf.pdf"
    fig1_dst = PAPER_FIGS_DIR / fig1_src.name
    fig2_dst = PAPER_FIGS_DIR / fig2_src.name
    shutil.copy2(fig1_src, fig1_dst)
    shutil.copy2(fig2_src, fig2_dst)
    log(f"synchronized paper figures: {fig1_dst.name}, {fig2_dst.name}")
    return fig1_dst, fig2_dst


def write_figure_manifest(
    *,
    figure_id: str,
    title: str,
    figure_path: Path,
    aggregate_inputs: list[Path],
    run_ids: list[str],
    notes: list[str],
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
        "published",
        "--figure-path",
        str(figure_path),
    ]
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
        run_dir = RUNS_DIR / run_id
        run([python_bin(), str(ARTIFACT_CHECK), "--run-dir", str(run_dir)])


def write_summary_report(
    *,
    batch_id: str,
    aggregate_bundle_dir: Path,
    figure_dir: Path,
    figure_manifest_ids: list[str],
    run_ids: list[str],
    reference_run_ids: list[str],
    report_json: Path,
) -> Path:
    report_md = aggregate_bundle_dir / "FIG12_PAPER_GRADE_REPORT.md"
    figure_manifest_paths = [FIGURES_DIR / f"{item}.figure.json" for item in figure_manifest_ids]
    lines = [
        "# Fig.1/Fig.2 Paper-Grade Evidence Report",
        "",
        f"- Batch ID: `{batch_id}`",
        f"- Generated at: `{now_utc()}`",
        f"- Aggregate bundle: `{relpath(aggregate_bundle_dir)}`",
        f"- Figure bundle: `{relpath(figure_dir)}`",
        f"- JSON report: `{relpath(report_json)}`",
        "",
        "## Policy",
        "",
        "- `run_class=paper_grade`",
        "- `cache_mode=disabled`",
        "- `cs_size=0`",
        "- `seed_provenance=native`",
        "",
        "## Figures",
        "",
        f"- Fig. 1: `{relpath(figure_dir / 'fig1_accuracy_overhead.pdf')}`",
        f"- Fig. 2: `{relpath(figure_dir / 'fig2_retrieval_cdf.pdf')}`",
        f"- Paper copy Fig. 1: `{relpath(PAPER_FIGS_DIR / 'fig1_accuracy_overhead.pdf')}`",
        f"- Paper copy Fig. 2: `{relpath(PAPER_FIGS_DIR / 'fig2_retrieval_cdf.pdf')}`",
        "",
        "## Aggregates",
        "",
        f"- `{relpath(aggregate_bundle_dir / 'accuracy_sweep.csv')}`",
        f"- `{relpath(aggregate_bundle_dir / 'reference_runs.csv')}`",
        f"- `{relpath(aggregate_bundle_dir / 'comparison.csv')}`",
        f"- `{relpath(aggregate_bundle_dir / 'fig2_latency_summary.csv')}`",
        "",
        "## Figure Provenance Manifests",
        "",
    ]
    for path in figure_manifest_paths:
        lines.append(f"- `{relpath(path)}`")
    lines.extend(["", "## Promoted Runs", ""])
    for run_id in run_ids:
        marker = " (Fig. 2 reference)" if run_id in set(reference_run_ids) else ""
        lines.append(f"- `{run_id}`{marker}")
    lines.append("")
    write_markdown(report_md, "\n".join(lines) + "\n")
    return report_md


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run and promote canonical paper-grade Fig.1/Fig.2 evidence.")
    parser.add_argument(
        "--batch-id",
        default=f"fig12-paper-grade-{datetime.now().strftime('%Y%m%d_%H%M%S')}",
        help="Canonical batch identifier used in results/aggregates, results/figures, and run IDs.",
    )
    parser.add_argument(
        "--source-acc-dir",
        action="append",
        default=[],
        help="Use one or more existing accuracy experiment output directories instead of launching a new rerun.",
    )
    parser.add_argument(
        "--stage-dir",
        help="Optional persistent staging directory for the accuracy runner. Defaults to a temporary directory.",
    )
    parser.add_argument(
        "--skip-artifact-check",
        action="store_true",
        help="Skip artifact regression checks on promoted runs.",
    )
    parser.add_argument(
        "--seed-filter",
        action="append",
        default=[],
        help="Optional seed filter when promoting an existing staging directory.",
    )
    parser.add_argument(
        "--scheme-filter",
        action="append",
        default=[],
        help="Optional scheme filter when promoting an existing staging directory.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    AGGREGATES_DIR.mkdir(parents=True, exist_ok=True)
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)

    source_acc_dirs = [Path(item).resolve() for item in args.source_acc_dir]

    if source_acc_dirs:
        sweep_rows = []
        ref_rows = []
        cmp_rows = []
        stage_run_dirs = []
        seen_run_dirs: set[str] = set()
        for source_acc_dir in source_acc_dirs:
            if not source_acc_dir.exists():
                fail(f"source accuracy dir does not exist: {source_acc_dir}")
            log(f"using existing accuracy bundle: {source_acc_dir}")
            sub_sweep, sub_ref, sub_cmp, sub_run_dirs = discover_stage_runs(source_acc_dir)
            sub_sweep, sub_ref, sub_cmp, sub_run_dirs = filter_stage_rows(
                sweep_rows=sub_sweep,
                ref_rows=sub_ref,
                cmp_rows=sub_cmp,
                run_dirs=sub_run_dirs,
                seed_filters=args.seed_filter,
                scheme_filters=args.scheme_filter,
            )
            sweep_rows.extend(sub_sweep)
            ref_rows.extend(sub_ref)
            cmp_rows.extend(sub_cmp)
            for run_dir in sub_run_dirs:
                resolved = str(run_dir.resolve())
                if resolved in seen_run_dirs:
                    continue
                seen_run_dirs.add(resolved)
                stage_run_dirs.append(run_dir)
    else:
        if args.stage_dir:
            stage_dir = Path(args.stage_dir).resolve()
            run_accuracy_stage(stage_dir)
            sweep_rows, ref_rows, cmp_rows, stage_run_dirs = discover_stage_runs(stage_dir)
            sweep_rows, ref_rows, cmp_rows, stage_run_dirs = filter_stage_rows(
                sweep_rows=sweep_rows,
                ref_rows=ref_rows,
                cmp_rows=cmp_rows,
                run_dirs=stage_run_dirs,
                seed_filters=args.seed_filter,
                scheme_filters=args.scheme_filter,
            )
        else:
            with TemporaryDirectory(prefix="iroute-fig12-stage-") as tmp_dir:
                stage_dir = Path(tmp_dir) / "accuracy_comparison"
                run_accuracy_stage(stage_dir)
                sweep_rows, ref_rows, cmp_rows, stage_run_dirs = discover_stage_runs(stage_dir)
                sweep_rows, ref_rows, cmp_rows, stage_run_dirs = filter_stage_rows(
                    sweep_rows=sweep_rows,
                    ref_rows=ref_rows,
                    cmp_rows=cmp_rows,
                    run_dirs=stage_run_dirs,
                    seed_filters=args.seed_filter,
                    scheme_filters=args.scheme_filter,
                )
                run_map = promote_stage_runs(stage_run_dirs, args.batch_id)
                annotated_sweep = annotate_sweep_rows(sweep_rows, run_map)
                annotated_ref = annotate_sweep_rows(ref_rows, run_map)
                annotated_cmp = build_comparison_rows(annotated_ref, args.batch_id)
                aggregate_bundle_dir, sweep_path, ref_path, latency_path, report_json = promote_aggregates(
                    batch_id=args.batch_id,
                    sweep_rows=annotated_sweep,
                    ref_rows=annotated_ref,
                    cmp_rows=annotated_cmp,
                )
                figure_dir = generate_figures(args.batch_id, aggregate_bundle_dir)
                sync_paper_figures(figure_dir)
                run_ids = sorted(run_map.values())
                reference_run_ids = sorted({row["canonical_run_id"] for row in annotated_ref})
                write_figure_manifest(
                    figure_id="fig1_accuracy_overhead.paper_grade",
                    title="Fig. 1 paper-grade accuracy versus control overhead",
                    figure_path=figure_dir / "fig1_accuracy_overhead.pdf",
                    aggregate_inputs=[sweep_path, ref_path, report_json],
                    run_ids=run_ids,
                    notes=[
                        f"batch_id={args.batch_id}",
                        "cache_mode=disabled",
                        "run_class=paper_grade",
                        "paper figure synchronized to paper/figs/fig1_accuracy_overhead.pdf",
                    ],
                )
                write_figure_manifest(
                    figure_id="fig2_retrieval_cdf.paper_grade",
                    title="Fig. 2 paper-grade retrieval latency CDF",
                    figure_path=figure_dir / "fig2_retrieval_cdf.pdf",
                    aggregate_inputs=[ref_path, latency_path, report_json],
                    run_ids=reference_run_ids,
                    notes=[
                        f"batch_id={args.batch_id}",
                        "cache_mode=disabled",
                        "run_class=paper_grade",
                        "paper figure synchronized to paper/figs/fig2_retrieval_cdf.pdf",
                    ],
                )
                refresh_global_aggregates()
                if not args.skip_artifact_check:
                    validate_promoted_runs(run_ids)
                write_summary_report(
                    batch_id=args.batch_id,
                    aggregate_bundle_dir=aggregate_bundle_dir,
                    figure_dir=figure_dir,
                    figure_manifest_ids=[
                        "fig1_accuracy_overhead.paper_grade",
                        "fig2_retrieval_cdf.paper_grade",
                    ],
                    run_ids=run_ids,
                    reference_run_ids=reference_run_ids,
                    report_json=report_json,
                )
                return 0

    run_map = promote_stage_runs(stage_run_dirs, args.batch_id)
    annotated_sweep = annotate_sweep_rows(sweep_rows, run_map)
    annotated_ref = annotate_sweep_rows(ref_rows, run_map)
    annotated_cmp = build_comparison_rows(annotated_ref, args.batch_id)
    aggregate_bundle_dir, sweep_path, ref_path, latency_path, report_json = promote_aggregates(
        batch_id=args.batch_id,
        sweep_rows=annotated_sweep,
        ref_rows=annotated_ref,
        cmp_rows=annotated_cmp,
    )
    figure_dir = generate_figures(args.batch_id, aggregate_bundle_dir)
    sync_paper_figures(figure_dir)
    run_ids = sorted(run_map.values())
    reference_run_ids = sorted({row["canonical_run_id"] for row in annotated_ref})
    write_figure_manifest(
        figure_id="fig1_accuracy_overhead.paper_grade",
        title="Fig. 1 paper-grade accuracy versus control overhead",
        figure_path=figure_dir / "fig1_accuracy_overhead.pdf",
        aggregate_inputs=[sweep_path, ref_path, report_json],
        run_ids=run_ids,
        notes=[
            f"batch_id={args.batch_id}",
            "cache_mode=disabled",
            "run_class=paper_grade",
            "paper figure synchronized to paper/figs/fig1_accuracy_overhead.pdf",
        ],
    )
    write_figure_manifest(
        figure_id="fig2_retrieval_cdf.paper_grade",
        title="Fig. 2 paper-grade retrieval latency CDF",
        figure_path=figure_dir / "fig2_retrieval_cdf.pdf",
        aggregate_inputs=[ref_path, latency_path, report_json],
        run_ids=reference_run_ids,
        notes=[
            f"batch_id={args.batch_id}",
            "cache_mode=disabled",
            "run_class=paper_grade",
            "paper figure synchronized to paper/figs/fig2_retrieval_cdf.pdf",
        ],
    )
    refresh_global_aggregates()
    if not args.skip_artifact_check:
        validate_promoted_runs(run_ids)
    write_summary_report(
        batch_id=args.batch_id,
        aggregate_bundle_dir=aggregate_bundle_dir,
        figure_dir=figure_dir,
        figure_manifest_ids=[
            "fig1_accuracy_overhead.paper_grade",
            "fig2_retrieval_cdf.paper_grade",
        ],
        run_ids=run_ids,
        reference_run_ids=reference_run_ids,
        report_json=report_json,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
