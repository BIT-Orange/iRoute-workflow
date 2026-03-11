#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


INCLUDEGRAPHICS_RE = re.compile(r"\\includegraphics(?:\[[^\]]*\])?\{([^}]+)\}")


def candidate_manifest_path(repo_root: Path, figure_ref: str) -> Path:
    base = Path(figure_ref).stem
    return repo_root / "results" / "figures" / f"{base}.paper_grade.figure.json"


def main() -> int:
    parser = argparse.ArgumentParser(description="Preflight-check that all figures referenced by the canonical paper exist.")
    parser.add_argument("--paper", default="paper/main.tex", help="Paper TeX source to inspect")
    args = parser.parse_args()

    paper_path = Path(args.paper).resolve()
    if not paper_path.exists():
        print(f"[paper-preflight][FAIL] missing paper source: {paper_path}")
        return 1

    refs = INCLUDEGRAPHICS_RE.findall(paper_path.read_text(encoding="utf-8"))
    if not refs:
        print(f"[paper-preflight][FAIL] no figure references found in {paper_path}")
        return 1

    missing = []
    repo_root = paper_path.parents[1]
    for ref in refs:
        figure_path = (paper_path.parent / ref).resolve()
        if figure_path.exists():
            print(f"[paper-preflight][OK] {ref}")
        else:
            print(f"[paper-preflight][FAIL] missing {ref} -> {figure_path}")
            manifest_path = candidate_manifest_path(repo_root, ref)
            if manifest_path.exists():
                try:
                    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                    manifest_status = manifest.get("status", "unknown")
                    print(
                        f"[paper-preflight][INFO] figure provenance {manifest_path} "
                        f"status={manifest_status}"
                    )
                except Exception as ex:
                    print(f"[paper-preflight][WARN] failed to read provenance {manifest_path}: {ex}")
            missing.append(ref)

    if missing:
        print(f"[paper-preflight][FAIL] {len(missing)} missing figure(s)")
        return 1
    print(f"[paper-preflight][OK] all {len(refs)} referenced figures exist")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
