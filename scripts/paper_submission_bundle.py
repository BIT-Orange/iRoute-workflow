#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


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


def default_bundle_id() -> str:
    return "paper-submission-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def run_logged(cmd: list[str], cwd: Path, log_path: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        cmd,
        cwd=str(cwd),
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    combined = []
    combined.append(f"$ {' '.join(cmd)}")
    if result.stdout:
        combined.append(result.stdout.rstrip())
    if result.stderr:
        combined.append(result.stderr.rstrip())
    write_text(log_path, "\n".join(part for part in combined if part) + "\n")
    return result


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".DS_Store", "__pycache__", "*.pyc"))


def collect_figure_manifest_paths(repo_root: Path) -> list[Path]:
    return sorted((repo_root / "results" / "figures").glob("*.figure.json"))


def find_available_latex_tool() -> tuple[str, list[str]] | None:
    for binary, command in (
        ("latexmk", ["latexmk", "-pdf", "-interaction=nonstopmode", "-halt-on-error"]),
        ("tectonic", ["tectonic"]),
        ("pdflatex", ["pdflatex", "-interaction=nonstopmode", "-halt-on-error"]),
    ):
        resolved = shutil.which(binary)
        if resolved:
            return binary, command
    return None


def run_bundle_compile(bundle_root: Path) -> dict[str, Any]:
    compile_root = bundle_root / "compiled"
    compile_root.mkdir(parents=True, exist_ok=True)
    log_path = bundle_root / "logs" / "latex_compile.log"
    tool = find_available_latex_tool()
    if tool is None:
        write_text(log_path, "[submission-bundle] No LaTeX tool available; compile skipped.\n")
        return {
            "status": "skipped_unavailable",
            "tool": "",
            "log_path": relpath(bundle_root, log_path),
            "output_dir": relpath(bundle_root, compile_root),
        }

    tool_name, base_cmd = tool
    paper_dir = bundle_root / "paper"
    if tool_name == "latexmk":
        cmd = base_cmd + [f"-output-directory={compile_root}", "main.tex"]
    elif tool_name == "tectonic":
        cmd = base_cmd + ["--outdir", str(compile_root), "main.tex"]
    else:
        cmd = base_cmd + ["-output-directory", str(compile_root), "main.tex"]

    result = run_logged(cmd, paper_dir, log_path)
    return {
        "status": "passed" if result.returncode == 0 else "failed",
        "tool": tool_name,
        "exit_code": result.returncode,
        "log_path": relpath(bundle_root, log_path),
        "output_dir": relpath(bundle_root, compile_root),
    }


def build_markdown_summary(bundle: dict[str, Any], dossier: dict[str, Any]) -> str:
    claims = dossier.get("claim_summary", {}).get("counts", {})
    figures = dossier.get("figure_summary", {}).get("counts", {})
    missing = dossier.get("paper_facing", {}).get("missing", [])
    manual_assets = dossier.get("manual_asset_summary", {}).get("counts", {})
    lines = [
        "# Paper Submission Bundle",
        "",
        f"Generated at: `{bundle['generated_at_utc']}`",
        "",
        "## Bundle Status",
        "",
        f"- bundle id: `{bundle['bundle_id']}`",
        f"- bundle status: `{bundle['bundle_status']}`",
        f"- strict paper-release gate passed: `{bundle['paper_release_gate']['passed']}`",
        f"- strict paper-release gate log: `{bundle['paper_release_gate']['log_path']}`",
        f"- LaTeX compile status: `{bundle['latex_compile']['status']}`",
    ]
    if bundle["latex_compile"].get("tool"):
        lines.append(f"- LaTeX tool: `{bundle['latex_compile']['tool']}`")
    lines.extend(
        [
            "",
            "## Snapshot Summary",
            "",
            f"- claims: `supported={claims.get('supported', 0)}` `provisional={claims.get('provisional', 0)}` `blocked={claims.get('blocked', 0)}`",
            f"- figure manifests: `published={figures.get('published', 0)}` `partial={figures.get('partial', 0)}` `blocked={figures.get('blocked', 0)}` `placeholder={figures.get('placeholder', 0)}`",
            f"- manual paper assets blocked: `{manual_assets.get('blocked', 0)}`",
            f"- missing paper-facing references: `{len(missing)}`",
            "",
            "## Included Snapshots",
            "",
        ]
    )
    for item in bundle["included_paths"]:
        lines.append(f"- `{item}`")

    lines.extend(["", "## Missing Paper-Facing References", ""])
    if not missing:
        lines.append("- none")
    else:
        for item in missing:
            reason = item.get("status_reason") or item.get("figure_status") or "missing"
            lines.append(f"- `{item.get('paper_ref', '')}` [{item.get('classification', 'unknown')}] reason=`{reason}`")

    lines.extend(["", "## Supported Claim Evidence", ""])
    supported = dossier.get("supported_claim_evidence", [])
    if not supported:
        lines.append("- none")
    else:
        for claim in supported:
            lines.append(f"- `{claim.get('id', '')}` {claim.get('title', '')}")
            lines.append(f"  figures: {', '.join(claim.get('related_figures', [])) or 'none'}")
            lines.append(f"  aggregates: {', '.join(claim.get('related_aggregates', [])) or 'none'}")
            run_ids = claim.get("run_ids", [])
            lines.append(f"  runs: {', '.join(run_ids[:4]) + (f', ... ({len(run_ids)} total)' if len(run_ids) > 4 else '') if run_ids else 'none'}")

    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- `release_ready` means the existing strict `paper-release-gate` passed when this bundle was created.",
            "- `audit_only` means the bundle is still useful for submission review, but the repository did not satisfy the strict release gate at snapshot time.",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create a repository-native paper submission bundle.")
    parser.add_argument(
        "--bundle-id",
        default=default_bundle_id(),
        help="Bundle directory name under review/paper_audit/submission_bundles/.",
    )
    parser.add_argument(
        "--output-root",
        default="review/paper_audit/submission_bundles",
        help="Repository-relative output root for generated bundles.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite an existing bundle directory with the same id.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    bundle_root = repo_root / args.output_root / args.bundle_id

    if bundle_root.exists():
        if not args.overwrite:
            raise SystemExit(f"bundle already exists: {bundle_root}")
        shutil.rmtree(bundle_root)
    bundle_root.mkdir(parents=True, exist_ok=True)
    (bundle_root / "logs").mkdir(parents=True, exist_ok=True)

    workflow_path = repo_root / "scripts" / "workflow.sh"
    gate_log = bundle_root / "logs" / "paper_release_gate.log"
    gate_result = run_logged(["bash", str(workflow_path), "paper-release-gate"], repo_root, gate_log)

    dossier_log = bundle_root / "logs" / "release_dossier.log"
    dossier_script = repo_root / "scripts" / "paper_release_dossier.py"
    dossier_result = run_logged([sys.executable, str(dossier_script)], repo_root, dossier_log)
    if dossier_result.returncode != 0:
        raise SystemExit(f"release dossier generation failed; see {dossier_log}")

    dossier_json_path = repo_root / "review" / "paper_audit" / "paper_release_dossier.json"
    dossier_md_path = repo_root / "review" / "paper_audit" / "paper_release_dossier.md"
    dossier = load_json(dossier_json_path)

    copy_tree(repo_root / "paper", bundle_root / "paper")
    copy_file(dossier_json_path, bundle_root / "review" / "paper_audit" / "paper_release_dossier.json")
    copy_file(dossier_md_path, bundle_root / "review" / "paper_audit" / "paper_release_dossier.md")
    copy_file(repo_root / "review" / "claims" / "CLAIM_STATUS.md", bundle_root / "review" / "claims" / "CLAIM_STATUS.md")
    copy_file(repo_root / "review" / "claims" / "claims_map.json", bundle_root / "review" / "claims" / "claims_map.json")
    copy_file(repo_root / "results" / "figures" / "figure_index.json", bundle_root / "results" / "figures" / "figure_index.json")
    copy_file(repo_root / "paper" / "assets" / "asset_status.json", bundle_root / "paper" / "assets" / "asset_status.json")
    for manifest_path in collect_figure_manifest_paths(repo_root):
        copy_file(manifest_path, bundle_root / "results" / "figures" / manifest_path.name)

    latex_compile = run_bundle_compile(bundle_root)
    included_paths = [
        "paper/",
        "paper/assets/asset_status.json",
        "review/claims/CLAIM_STATUS.md",
        "review/claims/claims_map.json",
        "review/paper_audit/paper_release_dossier.json",
        "review/paper_audit/paper_release_dossier.md",
        "results/figures/figure_index.json",
        "results/figures/*.figure.json",
    ]
    bundle_manifest = {
        "generated_at_utc": utc_now(),
        "bundle_id": args.bundle_id,
        "bundle_status": "release_ready" if gate_result.returncode == 0 else "audit_only",
        "repo_root": str(repo_root),
        "bundle_root": relpath(repo_root, bundle_root),
        "paper_release_gate": {
            "passed": gate_result.returncode == 0,
            "exit_code": gate_result.returncode,
            "log_path": relpath(bundle_root, gate_log),
        },
        "release_dossier": {
            "json_path": relpath(bundle_root, bundle_root / "review" / "paper_audit" / "paper_release_dossier.json"),
            "markdown_path": relpath(bundle_root, bundle_root / "review" / "paper_audit" / "paper_release_dossier.md"),
        },
        "latex_compile": latex_compile,
        "included_paths": included_paths,
        "claim_counts": dossier.get("claim_summary", {}).get("counts", {}),
        "figure_counts": dossier.get("figure_summary", {}).get("counts", {}),
        "manual_asset_counts": dossier.get("manual_asset_summary", {}).get("counts", {}),
        "missing_paper_refs": dossier.get("paper_facing", {}).get("missing", []),
        "notes": [
            "release_ready is allowed only when the strict paper-release-gate passed.",
            "audit_only bundles are still useful review artifacts and should not be treated as submission-ready packages.",
        ],
    }
    write_json(bundle_root / "bundle_manifest.json", bundle_manifest)
    write_text(bundle_root / "BUNDLE_SUMMARY.md", build_markdown_summary(bundle_manifest, dossier))

    print(f"[submission-bundle] created {bundle_manifest['bundle_status']} bundle at {bundle_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
