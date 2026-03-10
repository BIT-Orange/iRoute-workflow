#!/usr/bin/env python3
"""Generate multi-centroid domain summaries from producer_content.csv.

Output format matches ndnSIM domain_centroids.csv:
  domain_id,centroid_id,vector_dim,vector,radius,weight
"""

import argparse
import csv
import math
import random
from collections import defaultdict
from pathlib import Path

import numpy as np


def parse_vec(s: str) -> np.ndarray:
    s = s.strip().strip('"')
    if s.startswith("["):
        s = s[1:]
    if s.endswith("]"):
        s = s[:-1]
    if not s:
        return np.array([], dtype=np.float32)
    vals = [float(x.strip()) for x in s.split(",") if x.strip()]
    v = np.array(vals, dtype=np.float32)
    n = float(np.linalg.norm(v))
    if n > 0:
        v /= n
    return v


def format_vec(v: np.ndarray) -> str:
    return "[" + ",".join(f"{x:.6f}" for x in v.tolist()) + "]"


def kmeans(points: np.ndarray, k: int, seed: int, max_iter: int = 30):
    rng = np.random.default_rng(seed)
    n = points.shape[0]
    if k >= n:
        centers = points.copy()
        labels = np.arange(n)
        return centers, labels

    init_idx = rng.choice(n, size=k, replace=False)
    centers = points[init_idx].copy()

    labels = np.zeros(n, dtype=np.int32)
    for _ in range(max_iter):
        # cosine distance since vectors are normalized
        sims = points @ centers.T
        new_labels = np.argmax(sims, axis=1)
        if np.array_equal(labels, new_labels):
            break
        labels = new_labels

        for cid in range(k):
            members = points[labels == cid]
            if len(members) == 0:
                centers[cid] = points[rng.integers(0, n)]
                continue
            c = members.mean(axis=0)
            cn = float(np.linalg.norm(c))
            if cn > 0:
                c = c / cn
            centers[cid] = c

    return centers, labels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", required=True, help="producer_content.csv")
    ap.add_argument("--out", required=True, help="output domain_centroids CSV")
    ap.add_argument("--M", type=int, default=4, help="centroids per domain")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rows_by_domain = defaultdict(list)
    with open(args.content, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                d = int(row["domain_id"])
            except Exception:
                continue
            is_distractor = str(row.get("is_distractor", "0")).strip().lower() in {"1", "true", "yes"}
            if is_distractor:
                continue
            vec = parse_vec(row.get("vector", ""))
            if vec.size == 0:
                continue
            rows_by_domain[d].append(vec)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["domain_id", "centroid_id", "vector_dim", "vector", "radius", "weight"])

        for d in sorted(rows_by_domain.keys()):
            pts = np.stack(rows_by_domain[d], axis=0)
            k = max(1, min(args.M, pts.shape[0]))
            centers, labels = kmeans(pts, k=k, seed=args.seed + d)
            for cid in range(k):
                members = pts[labels == cid]
                if len(members) == 0:
                    continue
                c = centers[cid]
                dists = np.linalg.norm(members - c, axis=1)
                radius = float(np.percentile(dists, 95)) if len(dists) > 0 else 0.0
                w.writerow([
                    d,
                    cid,
                    c.shape[0],
                    format_vec(c),
                    f"{radius:.6f}",
                    int(len(members)),
                ])

    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
