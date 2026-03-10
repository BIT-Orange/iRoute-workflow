#!/usr/bin/env python3

import argparse
import json
from collections import Counter
from pathlib import Path


ALLOWED_STATUSES = {"supported", "provisional", "blocked"}
MINIMUM_RUN_EVIDENCE = {"none", "non_smoke", "paper_grade"}


def load_claims(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def load_run_manifests(repo_root: Path) -> dict[str, dict]:
    manifests = {}
    runs_dir = repo_root / "results" / "runs"
    if not runs_dir.exists():
        return manifests
    for manifest_path in sorted(runs_dir.glob("*/manifest.json")):
        with manifest_path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        manifests[data.get("run_id", manifest_path.parent.name)] = data
    return manifests


def check_anchor(paper_text: str, anchor: str) -> bool:
    return f"\\label{{{anchor}}}" in paper_text


def classify_claim(claim: dict, repo_root: Path, paper_text: str, run_manifests: dict[str, dict]) -> tuple[list[str], list[str], dict]:
    hard_blockers: list[str] = []
    evidence_gaps: list[str] = []

    claim_id = claim["id"]
    status = claim["status"]
    minimum = claim.get("minimum_run_evidence", "none")
    if status not in ALLOWED_STATUSES:
        hard_blockers.append(f"invalid status {status!r}")
    if minimum not in MINIMUM_RUN_EVIDENCE:
        hard_blockers.append(f"invalid minimum_run_evidence {minimum!r}")

    anchor = claim.get("paper_anchor", "")
    if anchor and not check_anchor(paper_text, anchor):
        hard_blockers.append(f"missing paper anchor {anchor}")

    for marker in claim.get("paper_markers", []):
        if marker not in paper_text:
            hard_blockers.append(f"missing paper marker {marker}")

    for rel_path in claim.get("related_figures", []):
        if not (repo_root / rel_path).exists():
            hard_blockers.append(f"missing figure {rel_path}")

    for rel_path in claim.get("related_aggregates", []):
        if not (repo_root / rel_path).exists():
            hard_blockers.append(f"missing aggregate {rel_path}")

    for rel_path in claim.get("static_evidence", []):
        if not (repo_root / rel_path).exists():
            hard_blockers.append(f"missing static evidence {rel_path}")

    related_runs = claim.get("related_runs", {})
    run_ids = related_runs.get("run_ids", [])
    requested_classes = related_runs.get("run_classes", [])

    actual_run_classes: list[str] = []
    for run_id in run_ids:
        manifest = run_manifests.get(run_id)
        if manifest is None:
            hard_blockers.append(f"missing canonical run {run_id}")
            continue
        actual_run_classes.append(str(manifest.get("run_class", "unknown")))

    class_matches: list[str] = []
    if requested_classes:
        class_matches = [
            run_id
            for run_id, manifest in run_manifests.items()
            if str(manifest.get("run_class", "")) in requested_classes
        ]

    if minimum == "non_smoke":
        non_smoke_found = any(run_class != "smoke" for run_class in actual_run_classes)
        if not non_smoke_found and requested_classes:
            non_smoke_found = any(run_manifests[run_id].get("run_class") != "smoke" for run_id in class_matches)
        if not non_smoke_found:
            evidence_gaps.append(f"claim requires non-smoke evidence but none is referenced for {claim_id}")

    if minimum == "paper_grade":
        paper_grade_found = any(run_class == "paper_grade" for run_class in actual_run_classes)
        if not paper_grade_found and requested_classes:
            paper_grade_found = any(run_manifests[run_id].get("run_class") == "paper_grade" for run_id in class_matches)
        if not paper_grade_found:
            evidence_gaps.append(f"claim requires paper_grade evidence but no matching canonical paper_grade run exists for {claim_id}")

    smoke_only = False
    if minimum != "none":
        smoke_ids = []
        for run_id in run_ids:
            manifest = run_manifests.get(run_id)
            if manifest is not None and manifest.get("run_class") == "smoke":
                smoke_ids.append(run_id)
        if smoke_ids and len(smoke_ids) == len([run_id for run_id in run_ids if run_id in run_manifests]):
            smoke_only = True
        if smoke_only:
            evidence_gaps.append(f"claim depends only on smoke runs: {', '.join(smoke_ids)}")

    return hard_blockers, evidence_gaps, {
        "actual_run_classes": actual_run_classes,
        "class_matches": class_matches,
    }


def format_status_line(claim: dict, hard_blockers: list[str], evidence_gaps: list[str]) -> str:
    reasons = hard_blockers + evidence_gaps
    if reasons:
        return f"[claims][{claim['status'].upper()}] {claim['id']} {claim['title']} :: " + "; ".join(reasons)
    return f"[claims][{claim['status'].upper()}] {claim['id']} {claim['title']}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate claim-to-evidence bindings for the canonical paper.")
    parser.add_argument("--claims-map", default="review/claims/claims_map.json", help="Machine-readable claim map JSON")
    parser.add_argument("--paper", default="paper/main.tex", help="Canonical paper TeX source")
    parser.add_argument("--strict", action="store_true", help="Fail unless every mapped claim is supported and its evidence is present")
    parser.add_argument("--summary-only", action="store_true", help="Print a compact status summary without per-claim notes")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[3]
    claims_path = (repo_root / args.claims_map).resolve()
    paper_path = (repo_root / args.paper).resolve()

    if not claims_path.exists():
        print(f"[claims][FAIL] missing claims map: {claims_path}")
        return 1
    if not paper_path.exists():
        print(f"[claims][FAIL] missing paper source: {paper_path}")
        return 1

    claim_map = load_claims(claims_path)
    paper_text = paper_path.read_text(encoding="utf-8")
    run_manifests = load_run_manifests(repo_root)

    counts = Counter()
    supported_errors = []
    blocked_without_reason = []
    provisional_without_gap = []
    all_results = []

    for claim in claim_map.get("claims", []):
        hard_blockers, evidence_gaps, derived = classify_claim(claim, repo_root, paper_text, run_manifests)
        status = claim["status"]
        counts[status] += 1
        all_results.append((claim, hard_blockers, evidence_gaps, derived))

        if status == "supported" and (hard_blockers or evidence_gaps):
            supported_errors.append(claim["id"])
        if status == "blocked" and not hard_blockers and not evidence_gaps:
            blocked_without_reason.append(claim["id"])
        if status == "provisional" and not hard_blockers and not evidence_gaps:
            provisional_without_gap.append(claim["id"])

    if args.summary_only:
        for claim, hard_blockers, evidence_gaps, _derived in all_results:
            print(format_status_line(claim, hard_blockers, evidence_gaps))
    else:
        for claim, hard_blockers, evidence_gaps, derived in all_results:
            print(format_status_line(claim, hard_blockers, evidence_gaps))
            if derived["actual_run_classes"]:
                print(f"  run_classes={derived['actual_run_classes']}")
            if derived["class_matches"]:
                print(f"  matching_run_ids={derived['class_matches']}")
            for note in claim.get("notes", []):
                print(f"  note: {note}")

    print(
        "[claims][SUMMARY] "
        f"supported={counts['supported']} provisional={counts['provisional']} blocked={counts['blocked']}"
    )

    if blocked_without_reason:
        print(
            "[claims][WARN] blocked claims currently have no detected hard blocker or evidence gap: "
            + ", ".join(blocked_without_reason)
        )
    if provisional_without_gap:
        print(
            "[claims][WARN] provisional claims currently have no detected file or run gap: "
            + ", ".join(provisional_without_gap)
        )

    if args.strict:
        if supported_errors:
            print(
                "[claims][FAIL] supported claims with broken evidence: "
                + ", ".join(supported_errors)
            )
            return 1
        if counts["provisional"] or counts["blocked"]:
            print(
                "[claims][FAIL] strict mode requires every mapped claim to be supported; "
                f"found provisional={counts['provisional']} blocked={counts['blocked']}"
            )
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
