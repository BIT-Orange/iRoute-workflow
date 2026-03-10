#!/usr/bin/env python3
"""
build_msmarco_ndnsim.py - Generate ndnSIM CSV datasets from MS MARCO with entropy-controlled domain mapping.

This tool generates three CSV files for iRoute experiments:
1. producer_content.csv: Documents per domain with embeddings
2. consumer_trace.csv: Query trace with expected domain labels  
3. domain_centroids.csv: K-Means centroids per domain

Features:
- Entropy-controlled domain mapping via temperature parameter
- Uses sentence-transformers for semantic embeddings
- Query-based mode: assigns queries to domains based on semantic similarity
- Configurable number of domains, centroids, and vector dimensions

Usage:
    python build_msmarco_ndnsim.py \
        --queries dataset/msmarco-docdev-queries.tsv \
        --domains 10 \
        --temperature 1.0 \
        --output-dir data/msmarco_d10_t1.0

Output files:
    - producer_content.csv: doc_id,domain_id,canonical_name,vector,text_preview
    - consumer_trace.csv: query_id,query_text,vector,expected_domain,expected_doc
    - domain_centroids.csv: domain_id,centroid_id,vector_dim,vector,radius,weight
"""

import argparse
import os
import sys
import json
import random
import numpy as np
from collections import defaultdict
from typing import Dict, List, Tuple, Optional

# Try to import sentence-transformers
try:
    from sentence_transformers import SentenceTransformer
    HAS_SBERT = True
except ImportError:
    HAS_SBERT = False
    print("Warning: sentence-transformers not installed. Using random embeddings.")
    print("Install with: pip install sentence-transformers")

# Try to import sklearn for K-Means
try:
    from sklearn.cluster import KMeans
    from sklearn.preprocessing import normalize
    HAS_SKLEARN = True
except ImportError:
    HAS_SKLEARN = False
    print("Warning: sklearn not installed. Using random centroids.")
    print("Install with: pip install scikit-learn")


def load_queries(filepath: str) -> Dict[str, str]:
    """
    Load queries from MS MARCO format (TSV: query_id \t query_text)
    """
    queries = {}
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 2:
                qid, text = parts[0], parts[1]
                queries[qid] = text
    print(f"Loaded {len(queries)} queries from {filepath}")
    return queries


def get_embeddings(texts: List[str], model_name: str = 'all-MiniLM-L6-v2', 
                   target_dim: int = 64, model_cache: dict = None) -> np.ndarray:
    """
    Generate embeddings for texts using sentence-transformers or random.
    Returns: (N, target_dim) numpy array of normalized embeddings
    """
    if len(texts) == 0:
        return np.zeros((0, target_dim), dtype=np.float32)
    
    if HAS_SBERT:
        if model_cache is None:
            model_cache = {}
        if model_name not in model_cache:
            print(f"Loading model {model_name}...")
            model_cache[model_name] = SentenceTransformer(model_name)
        model = model_cache[model_name]
        embeddings = model.encode(texts, show_progress_bar=len(texts) > 100, 
                                  convert_to_numpy=True, batch_size=64)
        # Reduce dimension if needed (simple truncation)
        if embeddings.shape[1] > target_dim:
            embeddings = embeddings[:, :target_dim]
        elif embeddings.shape[1] < target_dim:
            # Pad with zeros
            pad = np.zeros((embeddings.shape[0], target_dim - embeddings.shape[1]))
            embeddings = np.hstack([embeddings, pad])
    else:
        # Random embeddings
        embeddings = np.random.randn(len(texts), target_dim)
    
    # Normalize
    norms = np.linalg.norm(embeddings, axis=1, keepdims=True)
    norms[norms == 0] = 1
    embeddings = embeddings / norms
    return embeddings.astype(np.float32)


def cluster_to_domains(embeddings: np.ndarray, num_domains: int, 
                       temperature: float = 1.0, seed: int = 42) -> Tuple[np.ndarray, np.ndarray]:
    """
    Cluster embeddings into domains using K-Means with soft assignment.
    
    Temperature controls entropy:
    - T -> 0: hard assignment (deterministic)
    - T = 1: standard softmax
    - T -> inf: uniform random
    
    Returns: (assignments array, centroids array)
    """
    np.random.seed(seed)
    n = embeddings.shape[0]
    
    if n == 0:
        return np.array([], dtype=int), np.zeros((num_domains, embeddings.shape[1] if embeddings.ndim > 1 else 64))
    
    if not HAS_SKLEARN or num_domains >= n:
        # Random assignment
        assignments = np.random.randint(0, num_domains, size=n)
        centroids = np.random.randn(num_domains, embeddings.shape[1])
        centroids = centroids / np.linalg.norm(centroids, axis=1, keepdims=True)
        return assignments, centroids
    
    # K-Means clustering
    kmeans = KMeans(n_clusters=num_domains, random_state=seed, n_init=10)
    kmeans.fit(embeddings)
    centroids = kmeans.cluster_centers_
    
    # Normalize centroids
    centroids = centroids / np.linalg.norm(centroids, axis=1, keepdims=True)
    
    # Compute distances to all centroids
    # Cosine similarity = dot product for normalized vectors
    similarities = embeddings @ centroids.T  # (N, D)
    
    if temperature <= 0.01:
        # Hard assignment
        assignments = similarities.argmax(axis=1)
    else:
        # Soft assignment with temperature
        logits = similarities / temperature
        # Softmax
        exp_logits = np.exp(logits - logits.max(axis=1, keepdims=True))
        probs = exp_logits / exp_logits.sum(axis=1, keepdims=True)
        # Sample from distribution
        assignments = np.array([np.random.choice(num_domains, p=p) for p in probs])
    
    return assignments, centroids


def compute_domain_centroids(embeddings: np.ndarray, assignments: np.ndarray,
                             num_domains: int, centroids_per_domain: int = 4) -> Dict[int, List[dict]]:
    """
    Compute K-Means centroids for each domain.
    Returns: dict mapping domain_id -> list of centroid dicts
    """
    result = {}
    dim = embeddings.shape[1] if len(embeddings) > 0 else 64
    
    for d in range(num_domains):
        mask = assignments == d
        domain_embs = embeddings[mask] if len(embeddings) > 0 else np.zeros((0, dim))
        
        if len(domain_embs) == 0:
            # Empty domain - use random centroids
            centroids = np.random.randn(centroids_per_domain, dim)
            centroids = centroids / np.linalg.norm(centroids, axis=1, keepdims=True)
        elif len(domain_embs) < centroids_per_domain:
            # Fewer samples than centroids
            centroids = domain_embs.copy()
            # Pad with mean + noise
            mean = domain_embs.mean(axis=0)
            while len(centroids) < centroids_per_domain:
                noise = np.random.randn(dim) * 0.1
                c = mean + noise
                c = c / np.linalg.norm(c)
                centroids = np.vstack([centroids, c])
        else:
            # K-Means within domain
            if HAS_SKLEARN:
                n_clusters = min(centroids_per_domain, len(domain_embs))
                kmeans = KMeans(n_clusters=n_clusters, random_state=42, n_init=10)
                kmeans.fit(domain_embs)
                centroids = kmeans.cluster_centers_
                # Pad if needed
                while len(centroids) < centroids_per_domain:
                    noise = np.random.randn(dim) * 0.1
                    c = centroids.mean(axis=0) + noise
                    c = c / np.linalg.norm(c)
                    centroids = np.vstack([centroids, c])
            else:
                # Random selection
                idx = np.random.choice(len(domain_embs), centroids_per_domain, replace=True)
                centroids = domain_embs[idx]
        
        # Normalize centroids
        centroids = centroids / np.linalg.norm(centroids, axis=1, keepdims=True)
        
        # Compute radius and weight for each centroid
        centroid_list = []
        for i, c in enumerate(centroids):
            # Radius: average distance to assigned points
            if len(domain_embs) > 0:
                sims = domain_embs @ c
                radius = float(1 - np.mean(sims))  # 1 - cosine_sim = cosine_dist
            else:
                radius = 0.5
            # Weight: number of points in domain
            weight = float(mask.sum())
            
            centroid_list.append({
                'centroid_id': i,
                'vector': c.tolist(),
                'radius': min(max(radius, 0.1), 1.0),  # Clamp to [0.1, 1.0]
                'weight': max(weight, 1.0)
            })
        
        result[d] = centroid_list
    
    return result


def vector_to_csv_string(vec: List[float]) -> str:
    """Format vector for CSV (quoted JSON array)"""
    return '"[' + ','.join(f'{v:.6f}' for v in vec) + ']"'


def write_producer_content(filepath: str, query_ids: List[str], assignments: np.ndarray,
                           embeddings: np.ndarray, texts: List[str]):
    """
    Write producer_content.csv - each query becomes a "document" in its assigned domain.
    Format: doc_id,domain_id,canonical_name,vector,text_preview
    """
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write("doc_id,domain_id,canonical_name,vector,text_preview\n")
        for i, (qid, domain, emb, text) in enumerate(zip(query_ids, assignments, embeddings, texts)):
            doc_id = f"doc_{qid}"
            canonical = f"/domain{domain}/data/{doc_id}"
            vec_str = vector_to_csv_string(emb.tolist())
            # Truncate text for preview
            preview = text[:100].replace('"', "'").replace('\n', ' ').replace(',', ' ')
            f.write(f'{doc_id},{domain},{canonical},{vec_str},"{preview}"\n')
    print(f"Written {len(query_ids)} documents to {filepath}")


def write_consumer_trace(filepath: str, query_ids: List[str], queries: Dict[str, str],
                         assignments: np.ndarray, embeddings: np.ndarray):
    """
    Write consumer_trace.csv - queries with their expected domain (same as producer assignment).
    Format: query_id,query_text,vector,expected_domain,expected_doc
    """
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write("query_id,query_text,vector,expected_domain,expected_doc\n")
        for qid, domain, emb in zip(query_ids, assignments, embeddings):
            text = queries[qid].replace('"', "'").replace('\n', ' ').replace(',', ' ')
            vec_str = vector_to_csv_string(emb.tolist())
            expected_domain = f"/domain{domain}"
            expected_doc = f"doc_{qid}"
            
            f.write(f'{qid},"{text}",{vec_str},{expected_domain},{expected_doc}\n')
    
    print(f"Written {len(query_ids)} queries to {filepath}")


def write_domain_centroids(filepath: str, centroids: Dict[int, List[dict]], dim: int):
    """
    Write domain_centroids.csv
    Format: domain_id,centroid_id,vector_dim,vector,radius,weight
    """
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write("domain_id,centroid_id,vector_dim,vector,radius,weight\n")
        total = 0
        for domain_id in sorted(centroids.keys()):
            for c in centroids[domain_id]:
                vec_str = vector_to_csv_string(c['vector'])
                f.write(f"{domain_id},{c['centroid_id']},{dim},{vec_str},{c['radius']:.4f},{c['weight']:.1f}\n")
                total += 1
    print(f"Written {total} centroids ({len(centroids)} domains) to {filepath}")


def main():
    parser = argparse.ArgumentParser(
        description='Generate ndnSIM CSV datasets from MS MARCO queries',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument('--queries', required=True, help='Path to queries TSV file')
    parser.add_argument('--output-dir', '-o', default='data/msmarco', help='Output directory')
    
    parser.add_argument('--domains', '-D', type=int, default=10, help='Number of domains')
    parser.add_argument('--centroids', '-M', type=int, default=4, help='Centroids per domain')
    parser.add_argument('--dim', type=int, default=64, help='Vector dimension')
    parser.add_argument('--temperature', '-T', type=float, default=1.0, 
                        help='Temperature for domain assignment (0=hard, 1=normal, >1=more random)')
    
    parser.add_argument('--max-queries', type=int, default=1000, help='Max queries to process')
    parser.add_argument('--model', default='all-MiniLM-L6-v2', help='Sentence-transformer model')
    parser.add_argument('--seed', type=int, default=42, help='Random seed')
    
    args = parser.parse_args()
    
    # Set seed
    random.seed(args.seed)
    np.random.seed(args.seed)
    
    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Load queries
    queries = load_queries(args.queries)
    
    # Select subset of queries
    query_ids = list(queries.keys())
    random.shuffle(query_ids)
    if args.max_queries and len(query_ids) > args.max_queries:
        query_ids = query_ids[:args.max_queries]
    print(f"Processing {len(query_ids)} queries")
    
    # Get query texts
    query_texts = [queries[qid] for qid in query_ids]
    
    # Generate embeddings
    print("Generating query embeddings...")
    model_cache = {}
    embeddings = get_embeddings(query_texts, args.model, args.dim, model_cache)
    
    # Cluster queries into domains
    print(f"Clustering queries into {args.domains} domains (T={args.temperature})...")
    assignments, domain_centers = cluster_to_domains(embeddings, args.domains, 
                                                     args.temperature, args.seed)
    
    # Print domain distribution
    domain_counts = defaultdict(int)
    for d in assignments:
        domain_counts[d] += 1
    print("Domain distribution:")
    for d in sorted(domain_counts.keys()):
        print(f"  Domain {d}: {domain_counts[d]} queries")
    
    # Compute domain entropy
    if len(assignments) > 0:
        probs = np.array([domain_counts.get(d, 0) for d in range(args.domains)]) / len(assignments)
        probs = probs[probs > 0]  # Remove zeros
        entropy = -np.sum(probs * np.log2(probs))
        max_entropy = np.log2(args.domains)
        print(f"Domain entropy: {entropy:.3f} bits (max: {max_entropy:.3f})")
    else:
        entropy = 0
        max_entropy = np.log2(args.domains)
    
    # Compute per-domain centroids
    print("Computing domain centroids...")
    centroids = compute_domain_centroids(embeddings, assignments, 
                                         args.domains, args.centroids)
    
    # Write output files
    write_producer_content(
        os.path.join(args.output_dir, 'producer_content.csv'),
        query_ids, assignments, embeddings, query_texts
    )
    
    write_consumer_trace(
        os.path.join(args.output_dir, 'consumer_trace.csv'),
        query_ids, queries, assignments, embeddings
    )
    
    write_domain_centroids(
        os.path.join(args.output_dir, 'domain_centroids.csv'),
        centroids, args.dim
    )
    
    # Write metadata
    metadata = {
        'queries_file': args.queries,
        'num_domains': args.domains,
        'centroids_per_domain': args.centroids,
        'vector_dim': args.dim,
        'temperature': args.temperature,
        'num_queries': len(query_ids),
        'domain_entropy': float(entropy),
        'max_entropy': float(max_entropy),
        'model': args.model if HAS_SBERT else 'random',
        'seed': args.seed
    }
    with open(os.path.join(args.output_dir, 'metadata.json'), 'w') as f:
        json.dump(metadata, f, indent=2)
    
    print(f"\nDone! Output written to {args.output_dir}/")
    print("Files:")
    print(f"  - producer_content.csv ({len(query_ids)} docs)")
    print(f"  - consumer_trace.csv ({len(query_ids)} queries)")
    print(f"  - domain_centroids.csv ({args.domains} domains × {args.centroids} centroids)")
    print(f"  - metadata.json")


if __name__ == '__main__':
    main()
