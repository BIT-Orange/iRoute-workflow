#!/usr/bin/env python3

import argparse
import csv
import json
from pathlib import Path


SUMMARY_COLUMNS = {
    "scheme",
    "totalQueries",
    "measurableQueries",
    "DomainAcc",
    "Recall_at_1",
    "Recall_at_k",
    "P50_ms",
    "P95_ms",
    "mean_hops",
    "ctrl_bytes_per_sec",
    "avg_FIB_entries",
    "avg_LSDB_entries",
    "unique_hops_values",
    "partition_detected",
    "failure_effective",
    "cache_hit_exact_ratio",
    "cache_hit_semantic_ratio",
}

QUERY_COLUMNS = {
    "qid",
    "scheme",
    "query_text",
    "gt_domain",
    "pred_domain",
    "hit_exact",
    "domain_hit",
    "rtt_total_ms",
    "n_interest_sent",
    "n_data_recv",
    "bytes_ctrl_tx",
    "bytes_ctrl_rx",
    "bytes_data_tx",
    "bytes_data_rx",
    "is_measurable",
    "is_success",
    "is_timeout",
    "topology",
    "cache_hit_exact",
    "cache_hit_semantic",
}

LATENCY_COLUMNS = {
    "scheme",
    "total_queries",
    "measurement_queries",
    "success_queries",
    "timeout_queries",
    "timeout_rate",
    "unique_rtt_values",
    "p50_ms",
    "p95_ms",
    "cache_miss_queries",
    "cache_miss_unique_rtt_values",
}

FAILURE_COLUMNS = {
    "scenario",
    "target",
    "event_time",
    "scheduled",
    "applied",
    "before_connected",
    "after_connected",
    "affected_apps",
    "pre_hit",
    "post_hit",
    "pre_success",
    "post_success",
    "pre_domain_hit",
    "post_domain_hit",
    "pre_exact_hit",
    "post_exact_hit",
    "pre_timeout_rate",
    "post_timeout_rate",
    "pre_retrans_avg",
    "post_retrans_avg",
    "pre_rtt_ms",
    "post_rtt_ms",
    "pre_count",
    "post_count",
    "effective_reasons",
    "notes",
}


def ok(msg: str) -> bool:
    print(f"[artifact][OK] {msg}")
    return True


def fail(msg: str) -> bool:
    print(f"[artifact][FAIL] {msg}")
    return False


def load_csv(path: Path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def parse_float(row: dict, key: str):
    value = row.get(key, "")
    if value in ("", None):
        raise ValueError(f"missing numeric field {key}")
    return float(value)


def parse_int(row: dict, key: str):
    return int(float(row.get(key, "0") or 0))


def parse_bool(value) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def require_columns(path: Path, rows, required: set[str]) -> bool:
    if not rows:
        return fail(f"{path} is empty")
    missing = required - set(rows[0].keys())
    if missing:
        return fail(f"{path} missing columns: {sorted(missing)}")
    return ok(f"{path} required columns present")


def require_nonnegative(row: dict, keys: list[str], label: str) -> bool:
    all_pass = True
    for key in keys:
        try:
            value = parse_float(row, key)
        except Exception as exc:
            all_pass &= fail(f"{label} invalid {key}: {exc}")
            continue
        if value < 0:
            all_pass &= fail(f"{label} negative {key}={value}")
    return all_pass


def validate_metadata(run_dir: Path) -> tuple[bool, dict]:
    manifest_path = run_dir / "run_manifest.json"
    direct_manifest_path = run_dir / "manifest.json"
    all_pass = True
    metadata = {}

    if manifest_path.exists():
        data = load_json(manifest_path)
        fields = data.get("fields", {})
        metadata = {
            "cache_mode": fields.get("cache_mode", data.get("cache_mode")),
            "run_mode": fields.get("run_mode", data.get("run_mode", data.get("workflow"))),
            "seed_provenance": fields.get("seed_provenance", data.get("seed_provenance")),
            "git_commit": data.get("git_commit", ""),
            "runner": data.get("runner", ""),
            "workflow": data.get("workflow", ""),
            "paper_grade": parse_bool(fields.get("paper_grade", data.get("paper_grade", False))),
        }
        if not str(metadata["git_commit"]).strip():
            all_pass &= fail(f"{manifest_path} missing git_commit")
        if not str(metadata["runner"]).strip():
            all_pass &= fail(f"{manifest_path} missing runner")
        if not str(metadata["workflow"]).strip():
            all_pass &= fail(f"{manifest_path} missing workflow")
        if all_pass:
            ok(f"{manifest_path} lineage fields present")
    elif direct_manifest_path.exists():
        data = load_json(direct_manifest_path)
        metadata = {
            "cache_mode": data.get("cache_mode"),
            "run_mode": data.get("run_mode"),
            "seed_provenance": data.get("seed_provenance"),
            "git_commit": "",
            "runner": "",
            "workflow": "direct_driver",
            "paper_grade": parse_bool(data.get("paper_grade", False)),
        }
        ok(f"{direct_manifest_path} direct manifest present")
    else:
        return fail(f"{run_dir} missing run_manifest.json and manifest.json"), metadata

    cache_mode = str(metadata.get("cache_mode", "")).strip()
    run_mode = str(metadata.get("run_mode", "")).strip()
    seed_provenance = str(metadata.get("seed_provenance", "")).strip()

    if cache_mode not in {"enabled", "disabled"}:
        all_pass &= fail(f"{run_dir} invalid cache_mode={cache_mode!r}")
    if not run_mode:
        all_pass &= fail(f"{run_dir} missing run_mode")
    if not seed_provenance:
        all_pass &= fail(f"{run_dir} missing seed_provenance")
    if all_pass:
        ok(f"{run_dir} metadata fields present (cache_mode/run_mode/seed_provenance)")
    return all_pass, metadata


def validate_summary(run_dir: Path):
    path = run_dir / "summary.csv"
    if not path.exists():
        return fail(f"missing {path}"), {}
    rows = load_csv(path)
    all_pass = require_columns(path, rows, SUMMARY_COLUMNS)
    row = rows[-1]
    scheme = str(row.get("scheme", "")).strip()
    if not scheme:
        all_pass &= fail(f"{path} missing scheme value")
    all_pass &= require_nonnegative(
        row,
        [
            "totalQueries",
            "measurableQueries",
            "P50_ms",
            "P95_ms",
            "mean_hops",
            "ctrl_bytes_per_sec",
            "avg_FIB_entries",
            "avg_LSDB_entries",
            "unique_hops_values",
        ],
        str(path),
    )

    total = parse_int(row, "totalQueries")
    measurable = parse_int(row, "measurableQueries")
    if total <= 0:
        all_pass &= fail(f"{path} totalQueries must be >0")
    if measurable > total:
        all_pass &= fail(f"{path} measurableQueries exceeds totalQueries")
    for key in ["DomainAcc", "Recall_at_1", "Recall_at_k", "cache_hit_exact_ratio", "cache_hit_semantic_ratio"]:
        try:
            value = parse_float(row, key)
        except Exception as exc:
            all_pass &= fail(f"{path} invalid {key}: {exc}")
            continue
        if not (0.0 <= value <= 1.0):
            all_pass &= fail(f"{path} {key} out of range: {value}")
    if parse_float(row, "P95_ms") < parse_float(row, "P50_ms"):
        all_pass &= fail(f"{path} P95_ms < P50_ms")
    if all_pass:
        ok(f"{path} summary invariants passed")
    return all_pass, {"scheme": scheme, "total": total, "measurable": measurable}


def validate_query_log(run_dir: Path, summary: dict):
    path = run_dir / "query_log.csv"
    if not path.exists():
        return fail(f"missing {path}"), {}
    rows = load_csv(path)
    all_pass = require_columns(path, rows, QUERY_COLUMNS)
    if len(rows) != summary["total"]:
        all_pass &= fail(f"{path} row count {len(rows)} != summary totalQueries {summary['total']}")
    measurable = 0
    success = 0
    timeout = 0
    measurable_success = 0
    measurable_timeout = 0
    unique_schemes = set()

    for idx, row in enumerate(rows, start=1):
        unique_schemes.add(str(row.get("scheme", "")).strip())
        for key in ["qid", "n_interest_sent", "n_data_recv", "bytes_ctrl_tx", "bytes_ctrl_rx", "bytes_data_tx", "bytes_data_rx", "timeouts", "retransmissions"]:
            try:
                value = parse_float(row, key)
            except Exception as exc:
                all_pass &= fail(f"{path} row {idx} invalid {key}: {exc}")
                continue
            if value < 0:
                all_pass &= fail(f"{path} row {idx} negative {key}={value}")
        for key in ["rtt_total_ms", "disc_ms", "fetch_ms"]:
            try:
                value = parse_float(row, key)
            except Exception as exc:
                all_pass &= fail(f"{path} row {idx} invalid {key}: {exc}")
                continue
            if value < 0:
                all_pass &= fail(f"{path} row {idx} negative {key}={value}")
        for key in ["is_measurable", "is_success", "is_timeout"]:
            value = parse_int(row, key)
            if value not in {0, 1}:
                all_pass &= fail(f"{path} row {idx} {key} must be binary, got {value}")
        is_measurable = parse_int(row, "is_measurable")
        is_success = parse_int(row, "is_success")
        is_timeout = parse_int(row, "is_timeout")

        measurable += is_measurable
        success += is_success
        timeout += is_timeout
        if is_measurable:
            measurable_success += is_success
            measurable_timeout += is_timeout

    if unique_schemes != {summary["scheme"]}:
        all_pass &= fail(f"{path} scheme set {sorted(unique_schemes)} != summary scheme {summary['scheme']}")
    if measurable != summary["measurable"]:
        all_pass &= fail(f"{path} measurable row count {measurable} != summary measurableQueries {summary['measurable']}")
    if all_pass:
        ok(f"{path} query_log invariants passed")
    return all_pass, {
        "rows": len(rows),
        "measurable": measurable,
        "success": success,
        "timeout": timeout,
        "measurable_success": measurable_success,
        "measurable_timeout": measurable_timeout,
    }


def validate_latency_sanity(run_dir: Path, summary: dict, query_stats: dict):
    path = run_dir / "latency_sanity.csv"
    if not path.exists():
        return fail(f"missing {path}")
    rows = load_csv(path)
    all_pass = require_columns(path, rows, LATENCY_COLUMNS)
    row = rows[-1]
    if str(row.get("scheme", "")).strip() != summary["scheme"]:
        all_pass &= fail(f"{path} scheme does not match summary scheme")
    if parse_int(row, "total_queries") != query_stats["rows"]:
        all_pass &= fail(f"{path} total_queries does not match query_log rows")
    if parse_int(row, "measurement_queries") != query_stats["measurable"]:
        all_pass &= fail(f"{path} measurement_queries does not match query_log measurable rows")
    if parse_int(row, "success_queries") != query_stats["measurable_success"]:
        all_pass &= fail(f"{path} success_queries does not match measurable query_log success count")
    if parse_int(row, "timeout_queries") != query_stats["measurable_timeout"]:
        all_pass &= fail(f"{path} timeout_queries does not match measurable query_log timeout count")
    timeout_rate = parse_float(row, "timeout_rate")
    if not (0.0 <= timeout_rate <= 1.0):
        all_pass &= fail(f"{path} timeout_rate out of range: {timeout_rate}")
    p50 = parse_float(row, "p50_ms")
    p95 = parse_float(row, "p95_ms")
    if p50 < 0 or p95 < 0 or p95 < p50:
        all_pass &= fail(f"{path} invalid latency percentiles p50={p50} p95={p95}")
    if all_pass:
        ok(f"{path} latency_sanity invariants passed")
    return all_pass


def validate_failure_sanity(run_dir: Path):
    path = run_dir / "failure_sanity.csv"
    if not path.exists():
        print(f"[artifact][INFO] {path} not present; skipping failure_sanity checks")
        return True, {}
    rows = load_csv(path)
    all_pass = require_columns(path, rows, FAILURE_COLUMNS)
    row = rows[-1]
    for key in ["scheduled", "applied", "affected_apps", "pre_count", "post_count"]:
        if parse_float(row, key) < 0:
            all_pass &= fail(f"{path} negative {key}")
    for key in [
        "pre_hit",
        "post_hit",
        "pre_success",
        "post_success",
        "pre_domain_hit",
        "post_domain_hit",
        "pre_exact_hit",
        "post_exact_hit",
        "pre_timeout_rate",
        "post_timeout_rate",
    ]:
        value = parse_float(row, key)
        if value != -1.0 and not (0.0 <= value <= 1.0):
            all_pass &= fail(f"{path} {key} out of range: {value}")
    for key in ["pre_retrans_avg", "post_retrans_avg", "pre_rtt_ms", "post_rtt_ms"]:
        value = parse_float(row, key)
        if value < 0 and value != -1.0:
            all_pass &= fail(f"{path} invalid {key}: {value}")
    if not str(row.get("scenario", "")).strip():
        all_pass &= fail(f"{path} missing scenario")
    notes = str(row.get("notes", "")).strip()
    if notes and "metric=domain_hit" not in notes:
        all_pass &= fail(f"{path} missing metric=domain_hit note")
    if all_pass:
        ok(f"{path} failure_sanity invariants passed")
    return all_pass, row


def validate_run_dir(run_dir: Path) -> bool:
    all_pass = True
    metadata_ok, metadata = validate_metadata(run_dir)
    all_pass &= metadata_ok
    summary_ok, summary = validate_summary(run_dir)
    all_pass &= summary_ok
    if summary_ok:
        query_ok, query_stats = validate_query_log(run_dir, summary)
        all_pass &= query_ok
        if query_ok:
            all_pass &= validate_latency_sanity(run_dir, summary, query_stats)
    failure_ok, failure_row = validate_failure_sanity(run_dir)
    all_pass &= failure_ok

    if summary_ok and metadata.get("cache_mode") == "disabled":
        manifest_path = run_dir / "manifest.json"
        if manifest_path.exists():
            direct = load_json(manifest_path)
            if int(direct.get("cs_size", 0)) != 0:
                all_pass &= fail(f"{manifest_path} cache_mode=disabled but cs_size != 0")
    if summary_ok and metadata.get("cache_mode") == "enabled":
        manifest_path = run_dir / "manifest.json"
        if manifest_path.exists():
            direct = load_json(manifest_path)
            if int(direct.get("cs_size", 0)) <= 0:
                all_pass &= fail(f"{manifest_path} cache_mode=enabled but cs_size <= 0")

    if summary_ok and metadata.get("paper_grade") and failure_row:
        failure_effective = parse_int(load_csv(run_dir / "summary.csv")[-1], "failure_effective")
        if failure_effective <= 0:
            all_pass &= fail(f"{run_dir} paper-grade failure run recorded failure_effective=0")
        if not str(failure_row.get("effective_reasons", "")).strip():
            all_pass &= fail(f"{run_dir} paper-grade failure run missing effective_reasons")

    if all_pass:
        ok(f"{run_dir} artifact regression checks passed")
    return all_pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate per-run experiment artifacts.")
    parser.add_argument("--run-dir", action="append", default=[], help="Run directory containing summary/query/latency artifacts")
    args = parser.parse_args()
    if not args.run_dir:
        parser.error("provide at least one --run-dir")

    all_pass = True
    for item in args.run_dir:
        all_pass &= validate_run_dir(Path(item))
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
