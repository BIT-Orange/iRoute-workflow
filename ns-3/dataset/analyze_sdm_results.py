#!/usr/bin/env python3
import csv, sys
from collections import defaultdict
import numpy as np

def analyze(trace_path, results_path):
    # 1. Load Query Families
    q_families = {}
    with open(trace_path) as f:
        r = csv.DictReader(f)
        # We infer family from ID range since it's sequential
        # TZ=125 EC=150 PP=100 CT=50 NEG=75
        for row in r:
            qid = row["query_id"]
            try:
                idx = int(qid[1:])
                if 0 <= idx < 125: fam = "Type+Zone"
                elif 125 <= idx < 275: fam = "Enum Combo"
                elif 275 <= idx < 375: fam = "Paraphrase"
                elif 375 <= idx < 425: fam = "Cross-Type"
                else: fam = "Negative"
                q_families[qid] = fam
            except:
                pass

    # 2. Load Results
    fam_stats = defaultdict(lambda: {"total": 0, "domain_ok": 0, "doc_ok": 0, "failures": []})
    
    print(f"Loaded {len(q_families)} families from trace.")
    
    with open(results_path) as f:
        r = csv.DictReader(f)
        count = 0
        for row in r:
            try:
                raw_qid = row.get("queryId", "").strip()
                # Map integer ID '0' -> 'q0000' to match trace
                qid_idx = int(raw_qid)
                qid = f"q{qid_idx:04d}"
            except ValueError:
                qid = raw_qid
            
            if qid not in q_families: continue
            fam = q_families[qid]
            
            # Parse CSV outcomes
            # domainCorrect: 1=OK, 0=Fail, -1=Unmeasured/Neg
            # docCorrect: 1=DocOK, 0=Fail, -1=Unmeasured
            dc = int(float(row.get("domainCorrect", -1)))
            doc_c = int(float(row.get("docCorrect", -1)))
            
            # Handle Negative queries separately?
            # If Negative, correct behavior is "NOT_FOUND" or empty
            # The C++ script marks them as -1 usually.
            
            fam_stats[fam]["total"] += 1
            if dc == 1: fam_stats[fam]["domain_ok"] += 1
            if doc_c == 1: fam_stats[fam]["doc_ok"] += 1

    # 3. Print Report
    print(f"{'Family':<15} | {'Count':<5} | {'Dom Acc%':<8} | {'Doc Acc%':<8}")
    print("-" * 50)
    
    overall_dom, overall_doc, overall_tot = 0, 0, 0
    
    for fam in ["Type+Zone", "Enum Combo", "Paraphrase", "Cross-Type", "Negative"]:
        s = fam_stats[fam]
        tot = s["total"]
        if tot == 0: continue
        
        # Negative queries: "Domain Acc" isn't well defined by C++ metric (usually -1)
        # unless we check if it acted correctly (no domain selected).
        # For now, report raw C++ metrics.
        
        d_acc = s["domain_ok"] / tot * 100
        doc_acc = s["doc_ok"] / tot * 100
        
        if fam != "Negative":
            overall_dom += s["domain_ok"]
            overall_doc += s["doc_ok"]
            overall_tot += tot
            
        print(f"{fam:<15} | {tot:<5} | {d_acc:<8.1f} | {doc_acc:<8.1f}")
        
    print("-" * 50)
    if overall_tot > 0:
        print(f"{'Positive Avg':<15} | {overall_tot:<5} | {overall_dom/overall_tot*100:<8.1f} | {overall_doc/overall_tot*100:<8.1f}")

if __name__ == "__main__":
    analyze("dataset/sdm_smartcity_dataset/consumer_trace.csv", "results/exp1/exp1.csv")
