#!/usr/bin/env python3

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Dict, List, Tuple


def _load_rows(path: Path) -> Tuple[List[str], List[Dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        header = list(reader.fieldnames or [])
        rows = [dict(row) for row in reader]
    if not header:
        raise SystemExit(f"missing CSV header: {path}")
    if not rows:
        raise SystemExit(f"empty workload trace: {path}")
    return header, rows


def _row_key(row: Dict[str, str], fallback_index: int) -> str:
    qid = str(row.get("query_id", "")).strip()
    if qid:
        return qid
    return json.dumps({"row_index": fallback_index, "row": row}, sort_keys=True, ensure_ascii=True)


def build_repeated_rows(rows: List[Dict[str, str]], repeat: int, mode: str) -> List[Dict[str, str]]:
    if repeat < 1:
        raise SystemExit(f"repeat must be >=1, got {repeat}")
    out: List[Dict[str, str]] = []
    if mode == "row":
        for row in rows:
            for _ in range(repeat):
                out.append(dict(row))
        return out
    if mode == "sequence":
        for _ in range(repeat):
            for row in rows:
                out.append(dict(row))
        return out
    raise SystemExit(f"unsupported repeat mode: {mode}")


def compute_stats(src_rows: List[Dict[str, str]], out_rows: List[Dict[str, str]], repeat: int, mode: str) -> Dict[str, object]:
    src_keys = [_row_key(row, idx) for idx, row in enumerate(src_rows)]
    out_keys = [_row_key(row, idx) for idx, row in enumerate(out_rows)]
    src_counter = Counter(src_keys)
    out_counter = Counter(out_keys)
    src_unique = len(set(src_keys))
    out_unique = len(set(out_keys))
    full_coverage = all(out_counter.get(key, 0) >= count for key, count in src_counter.items())
    expected_rows = len(src_rows) * repeat
    return {
        "repeat_factor": repeat,
        "repeat_mode": mode,
        "original_query_count": len(src_rows),
        "repeated_query_count": len(out_rows),
        "expected_repeated_query_count": expected_rows,
        "unique_query_count_before": src_unique,
        "unique_query_count_after": out_unique,
        "full_source_trace_covered": full_coverage,
        "length_matches_expectation": len(out_rows) == expected_rows,
        "unique_diversity_preserved": out_unique == src_unique,
    }


def print_stats(stats: Dict[str, object]) -> None:
    print(
        "[repeat] "
        f"source_rows={stats['original_query_count']} "
        f"repeated_rows={stats['repeated_query_count']} "
        f"expected_rows={stats['expected_repeated_query_count']} "
        f"unique_before={stats['unique_query_count_before']} "
        f"unique_after={stats['unique_query_count_after']} "
        f"full_coverage={stats['full_source_trace_covered']}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and validate repeated workload traces.")
    parser.add_argument("--source", required=True, help="Source workload CSV")
    parser.add_argument("--output", help="Output CSV for repeated workload")
    parser.add_argument("--repeat", required=True, type=int, help="Repeat factor >=1")
    parser.add_argument("--mode", choices=["row", "sequence"], default="row", help="Repeat semantics")
    parser.add_argument("--stats-out", help="Optional JSON stats path")
    parser.add_argument("--assert-valid", action="store_true", help="Exit non-zero if coverage/length/diversity checks fail")
    parser.add_argument("--no-write", action="store_true", help="Only validate stats; do not write output CSV")
    args = parser.parse_args()

    src_path = Path(args.source)
    header, src_rows = _load_rows(src_path)
    out_rows = build_repeated_rows(src_rows, args.repeat, args.mode)
    stats = compute_stats(src_rows, out_rows, args.repeat, args.mode)
    stats["source"] = str(src_path)
    if args.output:
        stats["output"] = str(Path(args.output))

    if not args.no_write:
        if not args.output:
            raise SystemExit("--output is required unless --no-write is used")
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with out_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=header)
            writer.writeheader()
            writer.writerows(out_rows)

    if args.stats_out:
        stats_path = Path(args.stats_out)
        stats_path.parent.mkdir(parents=True, exist_ok=True)
        stats_path.write_text(json.dumps(stats, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print_stats(stats)

    if args.assert_valid:
        checks = [
            stats["full_source_trace_covered"],
            stats["length_matches_expectation"],
            stats["unique_diversity_preserved"],
        ]
        if not all(checks):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
