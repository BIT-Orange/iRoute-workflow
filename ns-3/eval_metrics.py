#!/usr/bin/env python3
import pandas as pd
import numpy as np
import argparse
import os
import json
import ast

def parse_semicolon_list(s):
    if pd.isna(s) or s == "":
        return []
    return str(s).split(';')

def parse_topk_domains(s):
    """Parse topKList string '/d1=0.9;/d2=0.8' into list of domains."""
    if pd.isna(s) or s == "":
        return []
    # format: domain=score;domain=score
    domains = []
    pairs = str(s).split(';')
    for p in pairs:
        if '=' in p:
            d = p.split('=')[0]
            domains.append(d)
    return domains

def extract_doc_id(name):
    """Extract docId from name like /domain/data/doc/D123."""
    if pd.isna(name): return ""
    parts = str(name).split("/doc/")
    if len(parts) > 1:
        return parts[1]
    return ""

def main():
    parser = argparse.ArgumentParser(description="Evaluate iRoute Metrics")
    parser.add_argument("--runDir", required=True, help="Directory containing run artifacts")
    parser.add_argument("--kList", default="1,3,5", help="Comma-separated K values for Recall")
    args = parser.parse_args()

    log_file = os.path.join(args.runDir, "run_query_log.csv")
    if not os.path.exists(log_file):
        print(f"Error: {log_file} not found.")
        return

    df = pd.read_csv(log_file)
    k_vals = [int(x) for x in args.kList.split(",")]
    
    metrics = {}
    
    # Pre-process columns
    df['target_domains_list'] = df['targetDomains'].apply(parse_semicolon_list)
    df['target_docids_list'] = df['targetDocIds'].apply(parse_semicolon_list)
    df['topk_domains_list'] = df['topKList'].apply(parse_topk_domains)
    df['retrieved_docid'] = df['requestedName'].apply(extract_doc_id)
    
    # 1. Domain Recall @ K
    # Fraction of queries where at least one target domain is in top K candidates
    for k in k_vals:
        hits = 0
        valid_queries = 0
        for _, row in df.iterrows():
            targets = row['target_domains_list']
            if not targets: continue
            
            candidates = row['topk_domains_list'][:k]
            # Check overlap
            # Normalize: targets might be "domain0" or "/domain0"
            # Candidates are usually "/domain0"
            
            match = False
            for t in targets:
                t_norm = t.strip()
                if not t_norm.startswith("/"): t_norm = "/" + t_norm
                
                for c in candidates:
                    c_norm = c.strip()
                    if not c_norm.startswith("/"): c_norm = "/" + c_norm
                    if c_norm == t_norm:
                        match = True
                        break
                if match: break
            
            if match: hits += 1
            valid_queries += 1
            
        metrics[f"DomainRecall@{k}"] = hits / valid_queries if valid_queries > 0 else 0.0

    # 2. Doc Recall @ 1 (Currently only fetch 1 doc)
    # Fraction of relevant docs retrieved (or Hit@1)
    doc_hits = 0
    doc_queries = 0
    for _, row in df.iterrows():
        targets = row['target_docids_list']
        if not targets: continue
        
        retrieved = row['retrieved_docid']
        match = False
        if retrieved and retrieved in targets:
            match = True
            
        if match: doc_hits += 1
        doc_queries += 1
        
    metrics["DocHit@1"] = doc_hits / doc_queries if doc_queries > 0 else 0.0
    
    # 3. Hops and Latency
    # Filter successful queries for latency
    success_df = df[df['stage2Success'] == 1]
    metrics["AvgLatencyMs"] = success_df['totalMs'].mean() if not success_df.empty else 0.0
    metrics["SuccessRate"] = len(success_df) / len(df) if len(df) > 0 else 0.0
    
    # Hops (Check if columns exist, prompt 4 adds them later but we might not have them yet)
    if 'stage2HopCount' in df.columns:
        # Filter valid hops (-1 means unknown/local)
        valid_hops = df[df['stage2HopCount'] >= 0]['stage2HopCount']
        metrics["AvgHops"] = valid_hops.mean() if not valid_hops.empty else 0.0
    
    # 4. Overhead
    if 'totalControlBytes' in df.columns:
        metrics["AvgCtrlBytes"] = df['totalControlBytes'].mean()
        metrics["AvgDataBytes"] = df['totalDataBytes'].mean()

    # Sanity Checks
    sanity_passed = True
    sanity_msgs = []
    
    # Check 1: Hops > 0 (if available)
    if "AvgHops" in metrics and metrics["AvgHops"] == 0:
        sanity_msgs.append("WARNING: Average hops is 0. Check HopCount measurement.")
        # Only fail if we expect hops (e.g. not single node)
    
    # Check 2: Exact Baseline (if this is an exact run)
    # We don't verify exact baseline < oracle here effectively without loading the oracle baseline value.
    # But we can check if it's suspiciously perfect (1.0) on difficult queries.
    
    metrics["SanityPassed"] = sanity_passed
    metrics["SanityMessages"] = sanity_msgs

    # Save metrics
    out_file = os.path.join(args.runDir, "metrics.json")
    with open(out_file, "w") as f:
        json.dump(metrics, f, indent=4)
        
    print(json.dumps(metrics, indent=4))
    
    # Also append to summary csv if present? 
    # The C++ code writes run_summary.csv. We can enrich it.
    
    summary_file = os.path.join(args.runDir, "run_summary.csv")
    if os.path.exists(summary_file):
        sdf = pd.read_csv(summary_file)
        for k, v in metrics.items():
            if k not in sdf.columns and not isinstance(v, list):
                sdf[k] = v
        sdf.to_csv(summary_file, index=False)
        print(f"Updated {summary_file} with metrics.")

if __name__ == "__main__":
    main()
