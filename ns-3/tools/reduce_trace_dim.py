#!/usr/bin/env python3
"""
Reduce vector dimensions in existing iRoute trace files using PCA.

This script takes existing 384-dim trace files and reduces them to lower
dimensions (e.g., 64, 128, 192, 256) without needing to re-download or
re-embed documents.

Usage:
    python3 reduce_trace_dim.py \
        --inputDir ./trec_dl_2019 \
        --outputDir ./trec_dl_2019_dim128 \
        --targetDim 128

The script:
1. Loads all vectors from consumer_trace.csv, producer_content.csv, domain_centroids.csv
2. Fits PCA on document vectors (largest set)
3. Applies PCA transform to all vectors
4. Re-normalizes after projection
5. Saves new files with reduced dimensions
"""

import argparse
import csv
import os
import numpy as np
from sklearn.decomposition import PCA


def parse_vector(vec_str: str) -> np.ndarray:
    """Parse vector string [v1,v2,...] to numpy array."""
    return np.array([float(x) for x in vec_str.strip('[]').split(',')])


def format_vector(vec: np.ndarray) -> str:
    """Format numpy array to vector string [v1,v2,...]."""
    return '[' + ','.join(f'{v:.6f}' for v in vec) + ']'


def load_vectors(file_path: str, vector_col: str) -> tuple:
    """Load vectors from CSV file. Returns (rows, vectors)."""
    rows = []
    vectors = []
    
    with open(file_path, 'r') as f:
        reader = csv.DictReader(f)
        headers = reader.fieldnames
        for row in reader:
            rows.append(row)
            vectors.append(parse_vector(row[vector_col]))
    
    return rows, np.array(vectors), headers


def main():
    parser = argparse.ArgumentParser(description='Reduce vector dimensions using PCA')
    parser.add_argument('--inputDir', type=str, required=True,
                        help='Input trace directory')
    parser.add_argument('--outputDir', type=str, required=True,
                        help='Output trace directory')
    parser.add_argument('--targetDim', type=int, required=True,
                        help='Target vector dimension')
    parser.add_argument('--seed', type=int, default=42,
                        help='Random seed for PCA')
    args = parser.parse_args()
    
    os.makedirs(args.outputDir, exist_ok=True)
    
    print("=" * 60)
    print(f"Reducing dimensions from input to {args.targetDim}")
    print("=" * 60)
    
    # =========================================================================
    # Load all vectors
    # =========================================================================
    print("\nLoading vectors...")
    
    trace_path = os.path.join(args.inputDir, 'consumer_trace.csv')
    content_path = os.path.join(args.inputDir, 'producer_content.csv')
    centroids_path = os.path.join(args.inputDir, 'domain_centroids.csv')
    
    trace_rows, trace_vectors, trace_headers = load_vectors(trace_path, 'vector')
    content_rows, content_vectors, content_headers = load_vectors(content_path, 'vector')
    centroids_rows, centroids_vectors, centroids_headers = load_vectors(centroids_path, 'vector')
    
    original_dim = content_vectors.shape[1]
    print(f"  consumer_trace: {len(trace_rows)} queries, dim={trace_vectors.shape[1]}")
    print(f"  producer_content: {len(content_rows)} docs, dim={content_vectors.shape[1]}")
    print(f"  domain_centroids: {len(centroids_rows)} centroids, dim={centroids_vectors.shape[1]}")
    
    if args.targetDim >= original_dim:
        print(f"\nERROR: Target dim ({args.targetDim}) >= original dim ({original_dim})")
        return 1
    
    # =========================================================================
    # Fit PCA on document vectors (largest corpus)
    # =========================================================================
    print(f"\nFitting PCA ({original_dim} -> {args.targetDim})...")
    pca = PCA(n_components=args.targetDim, random_state=args.seed)
    pca.fit(content_vectors)
    
    explained_var = sum(pca.explained_variance_ratio_) * 100
    print(f"  Explained variance: {explained_var:.1f}%")
    
    # =========================================================================
    # Transform all vectors
    # =========================================================================
    print("\nTransforming vectors...")
    
    def transform_and_normalize(vectors: np.ndarray) -> np.ndarray:
        transformed = pca.transform(vectors)
        norms = np.linalg.norm(transformed, axis=1, keepdims=True)
        norms[norms == 0] = 1
        return transformed / norms
    
    trace_vectors_reduced = transform_and_normalize(trace_vectors)
    content_vectors_reduced = transform_and_normalize(content_vectors)
    centroids_vectors_reduced = transform_and_normalize(centroids_vectors)
    
    print(f"  trace: {trace_vectors.shape} -> {trace_vectors_reduced.shape}")
    print(f"  content: {content_vectors.shape} -> {content_vectors_reduced.shape}")
    print(f"  centroids: {centroids_vectors.shape} -> {centroids_vectors_reduced.shape}")
    
    # =========================================================================
    # Save reduced files
    # =========================================================================
    print("\nSaving reduced files...")
    
    # Consumer trace
    out_trace = os.path.join(args.outputDir, 'consumer_trace.csv')
    with open(out_trace, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=trace_headers)
        writer.writeheader()
        for i, row in enumerate(trace_rows):
            row['vector'] = format_vector(trace_vectors_reduced[i])
            writer.writerow(row)
    print(f"  Wrote: {out_trace}")
    
    # Producer content
    out_content = os.path.join(args.outputDir, 'producer_content.csv')
    with open(out_content, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=content_headers)
        writer.writeheader()
        for i, row in enumerate(content_rows):
            row['vector'] = format_vector(content_vectors_reduced[i])
            writer.writerow(row)
    print(f"  Wrote: {out_content}")
    
    # Domain centroids
    out_centroids = os.path.join(args.outputDir, 'domain_centroids.csv')
    with open(out_centroids, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=centroids_headers)
        writer.writeheader()
        for i, row in enumerate(centroids_rows):
            row['vector'] = format_vector(centroids_vectors_reduced[i])
            # Update vector_dim column if it exists
            if 'vector_dim' in row:
                row['vector_dim'] = args.targetDim
            writer.writerow(row)
    print(f"  Wrote: {out_centroids}")
    
    # Copy doc_domain_map.csv (unchanged)
    map_src = os.path.join(args.inputDir, 'doc_domain_map.csv')
    map_dst = os.path.join(args.outputDir, 'doc_domain_map.csv')
    if os.path.exists(map_src):
        import shutil
        shutil.copy(map_src, map_dst)
        print(f"  Copied: {map_dst}")
    
    # =========================================================================
    # Summary
    # =========================================================================
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    
    old_wire = 100 + 20 + 10 + original_dim * 4
    new_wire = 100 + 20 + 10 + args.targetDim * 4
    
    print(f"  Original dimension: {original_dim}")
    print(f"  Target dimension: {args.targetDim}")
    print(f"  PCA explained variance: {explained_var:.1f}%")
    print(f"  Wire size: {old_wire}B -> {new_wire}B")
    print(f"  MTU compliant: {'✓ Yes' if new_wire <= 1500 else '✗ No'}")
    print(f"  Output: {args.outputDir}")
    
    return 0


if __name__ == '__main__':
    exit(main())
