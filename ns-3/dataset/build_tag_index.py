#!/usr/bin/env python3
"""
Generate tag index and query->tag mapping using K-Means clustering.
"""

import argparse
import pandas as pd
import numpy as np
import base64
import struct
import os
from sklearn.cluster import MiniBatchKMeans
from tqdm import tqdm

import json

def parse_vector(val):
    """Parse vector from string (JSON list) or base64."""
    if pd.isna(val):
        return None
    if isinstance(val, str):
        val = val.strip()
        if val.startswith('['):
            try:
                return np.array(json.loads(val), dtype=np.float32)
            except:
                pass
        # Try base64
        try:
            data = base64.b64decode(val)
            count = len(data) // 4
            return np.array(struct.unpack(f'<{count}f', data), dtype=np.float32)
        except:
            pass
    return None

def find_col(df, candidates):
    for c in candidates:
        if c in df.columns:
            return c
    return None

def main():
    parser = argparse.ArgumentParser(description="Build Tag Index")
    parser.add_argument("--content", required=True, help="Path to producer_content.csv")
    parser.add_argument("--queries", required=True, help="Path to consumer_trace.csv")
    parser.add_argument("--output_dir", required=True, help="Output directory")
    parser.add_argument("--k", type=int, default=64, help="Number of tags (clusters)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    
    args = parser.parse_args()
    
    print(f"Loading content from {args.content}...")
    df_content = pd.read_csv(args.content)
    
    vec_col = find_col(df_content, ['vector', 'vector_b64', 'embedding'])
    if not vec_col:
        print("Error: content vector column not found")
        return

    print(f"Decoding content vectors from {vec_col}...")
    vectors = []
    valid_indices = []
    for idx, row in tqdm(df_content.iterrows(), total=len(df_content)):
        vec = parse_vector(row[vec_col])
        if vec is not None:
            vectors.append(vec)
            valid_indices.append(idx)
            
    if not vectors:
        print("Error: No valid content vectors found")
        return

    X_content = np.stack(vectors)
    print(f"Content matrix shape: {X_content.shape}")
    
    print(f"Clustering into {args.k} tags...")
    kmeans = MiniBatchKMeans(n_clusters=args.k, random_state=args.seed, batch_size=1024)
    labels = kmeans.fit_predict(X_content)
    
    df_valid = df_content.iloc[valid_indices].copy()
    df_valid['tag_id'] = labels
    
    # Identify domain column
    domain_col = find_col(df_valid, ['domain', 'domain_id', 'producer_id'])
    if not domain_col:
        print("Warning: domain column missing. Using producer_id % 8 logic.")
        # Fallback if producer_id exists
        if 'producer_id' in df_valid.columns: # unlikely if find_col failed
             pass 
        # Create domains manually if possible
        if 'producer_id' in df_valid.columns: # fallback check
             df_valid['domain'] = df_valid['producer_id'] % 8
             domain_col = 'domain'
        else:
             print("Error: Cannot determine domains.")
             return
    
    # If using producer_id as domain, verify it is mapped correctly
    # Assuming 'domain_id' is preferred if available.
    if domain_col != 'domain_id' and domain_col != 'domain':
         # map producer_id to domain?
         # Check if 'producer_id' is int.
         try:
             df_valid['domain'] = df_valid[domain_col].astype(int) % 8
             domain_col = 'domain'
         except:
             pass

    print(f"Using domain column: {domain_col}")
    print("Generating tag_index.csv...")
    tag_stats = df_valid.groupby(['tag_id', domain_col]).size().reset_index(name='weight')
    tag_stats.rename(columns={domain_col: 'domain'}, inplace=True)
    
    output_tag_index = os.path.join(args.output_dir, "tag_index.csv")
    tag_stats.to_csv(output_tag_index, index=False)
    print(f"Saved {output_tag_index}")
    
    # Process Queries
    print(f"Loading queries from {args.queries}...")
    df_queries = pd.read_csv(args.queries)
    
    q_vec_col = find_col(df_queries, ['vector', 'query_vector', 'vector_b64', 'query_vector_b64'])
    if not q_vec_col:
        print("Error: query vector column not found")
        return

    print(f"Decoding query vectors from {q_vec_col}...")
    q_vectors = []
    q_indices = []
    for idx, row in tqdm(df_queries.iterrows(), total=len(df_queries)):
        vec = parse_vector(row[q_vec_col])
        if vec is not None:
            q_vectors.append(vec)
            q_indices.append(idx)

    if not q_vectors:
        print("Error: No valid query vectors found")
        return

    X_query = np.stack(q_vectors)
    print(f"Query matrix shape: {X_query.shape}")
    
    print("Mapping queries to tags...")
    q_labels = kmeans.predict(X_query)
    
    df_q_valid = df_queries.iloc[q_indices].copy()
    df_q_valid['tag_id'] = q_labels
    
    # Query ID column
    qid_col = find_col(df_q_valid, ['query_id', 'id', 'qid'])
    if not qid_col:
        df_q_valid['query_id'] = df_q_valid.index
        qid_col = 'query_id'

    output_query_tags = os.path.join(args.output_dir, "query_to_tag.csv")
    df_q_valid[[qid_col, 'tag_id']].rename(columns={qid_col: 'query_id'}).to_csv(output_query_tags, index=False)
    print(f"Saved {output_query_tags}")

if __name__ == "__main__":
    main()
