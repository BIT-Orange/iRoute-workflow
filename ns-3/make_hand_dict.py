#!/usr/bin/env python3
import pandas as pd
import argparse
import random
import re

def tokenize(text):
    # Simple tokenization: lowercase, remove non-alphanumeric, split by space
    text = text.lower()
    text = re.sub(r'[^a-z0-9\s]', '', text)
    tokens = text.split()
    return "-".join(tokens)  # strict tokenization for ExactNdn

def main():
    parser = argparse.ArgumentParser(description="Generate Hand Dictionary for Exact Baseline")
    parser.add_argument("--traceFile", required=True, help="Input queries csv")
    parser.add_argument("--contentFile", required=True, help="Input content csv")
    parser.add_argument("--output", required=True, help="Output hand_dict.csv")
    parser.add_argument("--ratio", type=float, default=0.1, help="Ratio of queries to cover (0.0-1.0)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    args = parser.parse_args()
    
    random.seed(args.seed)
    
    # Load queries
    # Header: query_id,query_text,vector,target_docids,target_domains,source
    try:
        df_q = pd.read_csv(args.traceFile)
    except Exception as e:
        print(f"Error reading trace file: {e}")
        return

    # Load content (to get canonical names/domains for docs)
    # Header: domain_id,doc_id,canonical_name,vector
    try:
        df_c = pd.read_csv(args.contentFile)
    except Exception as e:
        print(f"Error reading content file: {e}")
        return
        
    # Map docId -> (canonical_name, domain_id)
    doc_map = {}
    for _, row in df_c.iterrows():
        doc_map[str(row['doc_id'])] = (row['canonical_name'], row['domain_id'])
        
    # Select subset of queries
    selected_indices = random.sample(range(len(df_q)), int(len(df_q) * args.ratio))
    selected_queries = df_q.iloc[selected_indices]
    
    output_rows = []
    
    for _, row in selected_queries.iterrows():
        q_text = str(row['query_text'])
        tokenized = tokenize(q_text)
        
        # Get target doc (assume first one for now or all?)
        # ExactNdnProducer (simple) maps token -> single response
        # We'll take the first valid target doc
        target_docs = str(row['target_docids']).split(';')
        
        valid_doc = None
        for doc in target_docs:
            if doc in doc_map:
                valid_doc = doc
                break
        
        if valid_doc:
            canonical, domain_id = doc_map[valid_doc]
            # format: tokenized_query,type,doc_id,canonical_name,domain_id
            output_rows.append({
                "tokenized_query": tokenized,
                "type": "kw",
                "doc_id": valid_doc,
                "canonical_name": canonical,
                "domain_id": domain_id
            })
            
    out_df = pd.DataFrame(output_rows)
    out_df.to_csv(args.output, index=False)
    print(f"Generated {len(out_df)} entries in {args.output}")

if __name__ == "__main__":
    main()
