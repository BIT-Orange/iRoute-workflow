#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


INCLUDEGRAPHICS_RE = re.compile(r"\\includegraphics(?:\[[^\]]*\])?\{([^}]+)\}")
EVALUATION_FIGURE_RE = re.compile(r"(^|/)fig\d+[_-]")


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def relpath(repo_root: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root.resolve()))
    except Exception:
        return str(path)


def candidate_manifest_path(repo_root: Path, figure_ref: str) -> Path:
    return repo_root / "results" / "figures" / f"{Path(figure_ref).stem}.paper_grade.figure.json"


def infer_classification(repo_root: Path, figure_ref: str, asset_entries: dict[str, dict[str, Any]]) -> str:
    if figure_ref in asset_entries:
        return "manual_asset"
    if candidate_manifest_path(repo_root, figure_ref).exists() or EVALUATION_FIGURE_RE.search(figure_ref):
        return "evaluation_figure"
    return "unclassified"


def load_claims(claim_map_path: Path) -> tuple[list[dict[str, Any]], dict[str, list[dict[str, Any]]], Counter]:
    payload = load_json(claim_map_path)
    claims = payload.get("claims", [])
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    counts: Counter = Counter()
    for claim in claims:
        status = str(claim.get("status", "unknown"))
        counts[status] += 1
        grouped[status].append(
            {
                "id": claim.get("id", ""),
                "title": claim.get("title", ""),
                "paper_anchor": claim.get("paper_anchor", ""),
                "statement": claim.get("statement", ""),
                "notes": claim.get("notes", []),
            }
        )
    return claims, grouped, counts


def scan_paper_refs(repo_root: Path, paper_path: Path, asset_entries: dict[str, dict[str, Any]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    refs = INCLUDEGRAPHICS_RE.findall(paper_path.read_text(encoding="utf-8"))
    referenced = []
    missing = []
    for ref in refs:
        resolved = (paper_path.parent / ref).resolve()
        classification = infer_classification(repo_root, ref, asset_entries)
        item = {
            "paper_ref": ref,
            "path": relpath(repo_root, resolved),
            "exists": resolved.exists(),
            "classification": classification,
        }
        if ref in asset_entries:
            asset = asset_entries[ref]
            item["asset_status"] = asset.get("status", "")
            item["status_reason"] = asset.get("status_reason", "")
            item["asset_management"] = asset.get("management", "")
            item["asset_notes"] = asset.get("notes", [])
        manifest_path = candidate_manifest_path(repo_root, ref)
        if manifest_path.exists():
            try:
                manifest = load_json(manifest_path)
            except Exception as ex:
                item["figure_manifest_error"] = str(ex)
            else:
                item["figure_manifest"] = relpath(repo_root, manifest_path)
                item["figure_status"] = manifest.get("status", "")
        referenced.append(item)
        if not resolved.exists():
            missing.append(item)
    return referenced, missing


def find_matching_paper_ref(figure_id: str, paper_refs: list[str]) -> str:
    figure_base = figure_id.removesuffix(".paper_grade")
    matches = [ref for ref in paper_refs if Path(ref).stem == figure_base]
    if len(matches) == 1:
        return matches[0]
    return ""


def load_figure_manifests(repo_root: Path, paper_refs: list[str]) -> tuple[list[dict[str, Any]], dict[str, list[dict[str, Any]]], Counter]:
    manifests = []
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    counts: Counter = Counter()
    for path in sorted((repo_root / "results" / "figures").glob("*.figure.json")):
        payload = load_json(path)
        status = str(payload.get("status", "unknown"))
        counts[status] += 1
        inferred_ref = find_matching_paper_ref(str(payload.get("figure_id", "")), paper_refs)
        inferred_paper_path = repo_root / "paper" / inferred_ref if inferred_ref else None
        item = {
            "figure_id": payload.get("figure_id", ""),
            "title": payload.get("title", ""),
            "status": status,
            "manifest_path": relpath(repo_root, path),
            "figure_path": str(payload.get("figure_path", "")),
            "figure_exists": bool(payload.get("figure_exists", False)),
            "aggregate_inputs": payload.get("aggregate_inputs", []),
            "run_ids": payload.get("run_ids", []),
            "recorded_paper_figure_path": str(payload.get("paper_figure_path", "")),
            "recorded_paper_figure_in_sync": bool(payload.get("paper_figure_in_sync", False)),
            "paper_ref": inferred_ref,
            "paper_ref_exists": bool(inferred_paper_path and inferred_paper_path.exists()),
            "notes": payload.get("notes", []),
        }
        publication = payload.get("publication")
        if isinstance(publication, dict) and publication:
            item["publication"] = publication
        grouped[status].append(item)
        manifests.append(item)
    return manifests, grouped, counts


def load_manual_assets(repo_root: Path, asset_manifest_path: Path) -> tuple[list[dict[str, Any]], dict[str, list[dict[str, Any]]], Counter, dict[str, dict[str, Any]]]:
    payload = load_json(asset_manifest_path)
    assets = []
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    counts: Counter = Counter()
    by_ref: dict[str, dict[str, Any]] = {}
    for asset in payload.get("assets", []):
        status = str(asset.get("status", "unknown"))
        counts[status] += 1
        item = {
            "paper_ref": asset.get("paper_ref", ""),
            "title": asset.get("title", ""),
            "status": status,
            "status_reason": asset.get("status_reason", ""),
            "management": asset.get("management", ""),
            "asset_manifest": asset.get("asset_manifest", ""),
            "destination_path": asset.get("destination_path", ""),
            "source": asset.get("source", {}),
            "output": asset.get("output", {}),
            "notes": asset.get("notes", []),
        }
        assets.append(item)
        grouped[status].append(item)
        if item["paper_ref"]:
            by_ref[item["paper_ref"]] = asset
    return assets, grouped, counts, by_ref


def summarize_supported_claims(claims: list[dict[str, Any]]) -> list[dict[str, Any]]:
    evidence = []
    for claim in claims:
        if claim.get("status") != "supported":
            continue
        evidence.append(
            {
                "id": claim.get("id", ""),
                "title": claim.get("title", ""),
                "paper_anchor": claim.get("paper_anchor", ""),
                "related_figures": claim.get("related_figures", []),
                "related_aggregates": claim.get("related_aggregates", []),
                "related_figure_manifests": claim.get("related_figure_manifests", []),
                "run_ids": claim.get("related_runs", {}).get("run_ids", []),
                "run_classes": claim.get("related_runs", {}).get("run_classes", []),
                "static_evidence": claim.get("static_evidence", []),
            }
        )
    return evidence


def build_release_readiness(
    claim_counts: Counter,
    missing_refs: list[dict[str, Any]],
    manual_asset_counts: Counter,
) -> dict[str, Any]:
    blockers = []
    provisional_count = claim_counts.get("provisional", 0)
    blocked_count = claim_counts.get("blocked", 0)
    if provisional_count:
        blockers.append(f"{provisional_count} provisional claim(s) remain")
    if blocked_count:
        blockers.append(f"{blocked_count} blocked claim(s) remain")
    if missing_refs:
        blockers.append(f"{len(missing_refs)} paper-facing asset(s) referenced by paper/main.tex are still missing")
    blocked_assets = manual_asset_counts.get("blocked", 0)
    if blocked_assets:
        blockers.append(f"{blocked_assets} manual paper asset(s) remain blocked")
    return {
        "paper_release_gate_ready": not blockers,
        "blockers": blockers,
        "notes": [
            "This dossier is informational only.",
            "The strict paper-release-gate remains the authoritative submission gate.",
        ],
    }


def short_list(items: list[str], limit: int = 4) -> str:
    if not items:
        return "none"
    if len(items) <= limit:
        return ", ".join(items)
    visible = ", ".join(items[:limit])
    return f"{visible}, ... ({len(items)} total)"


def build_markdown(dossier: dict[str, Any]) -> str:
    claim_counts = dossier["claim_summary"]["counts"]
    figure_counts = dossier["figure_summary"]["counts"]
    manual_counts = dossier["manual_asset_summary"]["counts"]
    lines = [
        "# Paper Release Dossier",
        "",
        f"Generated at: `{dossier['generated_at_utc']}`",
        "",
        "This report is a repository-native release snapshot. It does not replace the strict `paper-release-gate`.",
        "",
        "## Summary",
        "",
        f"- claims: `supported={claim_counts.get('supported', 0)}` `provisional={claim_counts.get('provisional', 0)}` `blocked={claim_counts.get('blocked', 0)}`",
        f"- figure manifests: `published={figure_counts.get('published', 0)}` `partial={figure_counts.get('partial', 0)}` `blocked={figure_counts.get('blocked', 0)}` `placeholder={figure_counts.get('placeholder', 0)}`",
        f"- manual paper assets: `blocked={manual_counts.get('blocked', 0)}`",
        f"- missing paper-facing references: `{len(dossier['paper_facing']['missing'])}`",
        "",
        "## Release Readiness",
        "",
    ]
    if dossier["release_readiness"]["paper_release_gate_ready"]:
        lines.append("- current dossier view: `ready`")
    else:
        lines.append("- current dossier view: `not ready`")
        for blocker in dossier["release_readiness"]["blockers"]:
            lines.append(f"- blocker: {blocker}")
    lines.extend(
        [
            "",
            "## Claims",
            "",
            "### Supported",
            "",
        ]
    )
    for claim in dossier["claim_summary"]["claims_by_status"].get("supported", []):
        lines.append(f"- `{claim['id']}` {claim['title']}")
    lines.extend(["", "### Provisional", ""])
    provisional_claims = dossier["claim_summary"]["claims_by_status"].get("provisional", [])
    if provisional_claims:
        for claim in provisional_claims:
            lines.append(f"- `{claim['id']}` {claim['title']}")
    else:
        lines.append("- none")
    lines.extend(["", "### Blocked", ""])
    blocked_claims = dossier["claim_summary"]["claims_by_status"].get("blocked", [])
    if blocked_claims:
        for claim in blocked_claims:
            lines.append(f"- `{claim['id']}` {claim['title']}")
    else:
        lines.append("- none")

    lines.extend(["", "## Figures", "", "### Published", ""])
    published = dossier["figure_summary"]["figures_by_status"].get("published", [])
    if published:
        for figure in published:
            lines.append(
                f"- `{figure['figure_id']}` -> `{figure['figure_path']}`; paper ref `{figure['paper_ref'] or 'unmatched'}`; "
                f"paper file present=`{figure['paper_ref_exists']}`"
            )
    else:
        lines.append("- none")
    lines.extend(["", "### Partial / Blocked / Placeholder", ""])
    for status in ("partial", "blocked", "placeholder"):
        items = dossier["figure_summary"]["figures_by_status"].get(status, [])
        if not items:
            continue
        for figure in items:
            lines.append(
                f"- `{figure['figure_id']}` [{status}] -> `{figure['figure_path']}`; paper ref `{figure['paper_ref'] or 'unmatched'}`; "
                f"paper file present=`{figure['paper_ref_exists']}`"
            )
    if not any(dossier["figure_summary"]["figures_by_status"].get(status) for status in ("partial", "blocked", "placeholder")):
        lines.append("- none")

    lines.extend(["", "## Manual Paper Assets", ""])
    assets = dossier["manual_asset_summary"]["assets"]
    if assets:
        for asset in assets:
            lines.append(
                f"- `{asset['paper_ref']}` [{asset['status']}] reason=`{asset.get('status_reason', '')}` "
                f"management=`{asset['management']}` source_kind=`{asset['source'].get('kind', '')}` "
                f"output_exists=`{asset.get('output', {}).get('exists', False)}`"
            )
    else:
        lines.append("- none")

    lines.extend(["", "## Missing Paper-Facing References", ""])
    missing = dossier["paper_facing"]["missing"]
    if missing:
        for item in missing:
            extra = []
            if item.get("figure_status"):
                extra.append(f"figure_status={item['figure_status']}")
            if item.get("asset_status"):
                extra.append(f"asset_status={item['asset_status']}")
            if item.get("status_reason"):
                extra.append(f"status_reason={item['status_reason']}")
            suffix = f" ({', '.join(extra)})" if extra else ""
            lines.append(f"- `{item['paper_ref']}` [{item['classification']}] -> `{item['path']}`{suffix}")
    else:
        lines.append("- none")

    lines.extend(["", "## Supported Claim Evidence", ""])
    for claim in dossier["supported_claim_evidence"]:
        lines.append(f"- `{claim['id']}` {claim['title']}")
        lines.append(f"  figures: {short_list(claim['related_figures'])}")
        lines.append(f"  aggregates: {short_list(claim['related_aggregates'])}")
        lines.append(f"  runs: {short_list(claim['run_ids'])}")
        if claim["static_evidence"]:
            lines.append(f"  static: {short_list(claim['static_evidence'])}")

    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a repository-native paper release dossier.")
    parser.add_argument("--paper", default="paper/main.tex", help="Canonical paper source to inspect.")
    parser.add_argument(
        "--output-json",
        default="review/paper_audit/paper_release_dossier.json",
        help="Path for the machine-readable dossier JSON.",
    )
    parser.add_argument(
        "--output-md",
        default="review/paper_audit/paper_release_dossier.md",
        help="Path for the human-readable dossier Markdown.",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    paper_path = (repo_root / args.paper).resolve()
    claim_map_path = repo_root / "review" / "claims" / "claims_map.json"
    asset_manifest_path = repo_root / "paper" / "assets" / "asset_status.json"

    if not paper_path.exists():
        raise SystemExit(f"[release-dossier][FAIL] missing paper source: {paper_path}")
    if not claim_map_path.exists():
        raise SystemExit(f"[release-dossier][FAIL] missing claim map: {claim_map_path}")
    if not asset_manifest_path.exists():
        raise SystemExit(f"[release-dossier][FAIL] missing asset status manifest: {asset_manifest_path}")

    assets, assets_by_status, asset_counts, asset_entries = load_manual_assets(repo_root, asset_manifest_path)
    claims, claims_by_status, claim_counts = load_claims(claim_map_path)
    referenced, missing_refs = scan_paper_refs(repo_root, paper_path, asset_entries)
    paper_refs = [item["paper_ref"] for item in referenced]
    figures, figures_by_status, figure_counts = load_figure_manifests(repo_root, paper_refs)
    supported_claim_evidence = summarize_supported_claims(claims)
    generated_at = datetime.now(timezone.utc).isoformat()

    dossier = {
        "schema_version": 1,
        "generated_at_utc": generated_at,
        "paper": relpath(repo_root, paper_path),
        "claim_summary": {
            "counts": dict(sorted(claim_counts.items())),
            "claims_by_status": {status: items for status, items in sorted(claims_by_status.items())},
        },
        "figure_summary": {
            "counts": dict(sorted(figure_counts.items())),
            "figures_by_status": {status: items for status, items in sorted(figures_by_status.items())},
        },
        "manual_asset_summary": {
            "counts": dict(sorted(asset_counts.items())),
            "assets_by_status": {status: items for status, items in sorted(assets_by_status.items())},
            "assets": assets,
        },
        "paper_facing": {
            "referenced": referenced,
            "missing": missing_refs,
        },
        "supported_claim_evidence": supported_claim_evidence,
        "release_readiness": build_release_readiness(claim_counts, missing_refs, asset_counts),
        "gate_relation": {
            "paper_release_gate": "authoritative",
            "dossier": "informational companion; not a substitute for strict validation",
        },
    }

    output_json = repo_root / args.output_json
    output_md = repo_root / args.output_md
    write_json(output_json, dossier)
    write_text(output_md, build_markdown(dossier))
    print(f"[release-dossier][OK] wrote {relpath(repo_root, output_json)}")
    print(f"[release-dossier][OK] wrote {relpath(repo_root, output_md)}")
    print(
        "[release-dossier][OK] "
        f"claims supported={claim_counts.get('supported', 0)} "
        f"provisional={claim_counts.get('provisional', 0)} "
        f"blocked={claim_counts.get('blocked', 0)} "
        f"missing_refs={len(missing_refs)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
