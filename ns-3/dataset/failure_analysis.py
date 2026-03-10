import pandas as pd
import argparse
import sys
import re

def analyze(csv_file):
    try:
        df = pd.read_csv(csv_file)
    except Exception as e:
        print(f"Error reading {csv_file}: {e}")
        return

    total = len(df)
    print(f"Total queries: {total}")
    
    if "domainCorrect" in df.columns:
        df["correct"] = df["domainCorrect"]
    else:
        # Fallback (normalize)
        df["correct"] = (df["expectedDomain"] == df["finalSuccessDomain"]).astype(int)
    
    failed = df[df["correct"] == 0]
    print(f"Failures: {len(failed)} ({len(failed)/total*100:.1f}%)")
    
    type_counts = {"Retrieval_Miss": 0, "Ranking_Error": 0, "Fetch_Error": 0, "Selection_Error": 0, "Other": 0}
    
    print("\n--- Failure Details (First 10) ---")
    for i, row in failed.iterrows():
        expected = str(row["expectedDomain"]).strip()
        selected = str(row["selectedDomain"]).strip()
        final = str(row["finalSuccessDomain"]).strip()
        topk_str = str(row.get("topKList", ""))
        
        # Parse topK
        candidates = []
        if topk_str and topk_str.lower() != "nan":
            parts = topk_str.split(";")
            for p in parts:
                if "=" in p:
                    d = p.split("=")[0].strip()
                    # Normalize /domainX -> X
                    if d.startswith("/domain"):
                        d = d.replace("/domain", "")
                    candidates.append(d)
        
        reason = "Unknown"
        # Normalize expected/selected if needed
        norm_expected = expected
        if expected.endswith(".0"): norm_expected = expected[:-2] # "1.0" -> "1"
        
        norm_selected = selected
        if norm_selected.startswith("/domain"): norm_selected = norm_selected.replace("/domain", "")

        norm_final = final
        if norm_final.startswith("/domain"): norm_final = norm_final.replace("/domain", "")

        if norm_expected in candidates:
            if candidates and candidates[0] == norm_expected:
                if norm_selected == norm_expected:
                     if norm_final != norm_expected and norm_final != expected:
                          type_counts["Fetch_Error"] += 1
                          reason = f"Fetch Error (Selected {selected}, Final {final})"
                     else:
                          # Should be successful?
                          type_counts["Other"] += 1
                          reason = "Marked failed but seems correct?"
                else:
                     type_counts["Selection_Error"] += 1
                     reason = f"Selection Error (Top1 {candidates[0]}, Selected {selected})"
            else:
                type_counts["Ranking_Error"] += 1
                reason = f"Ranking Error (Expected {expected} not #1, Top={candidates[0] if candidates else 'None'})"
        else:
            type_counts["Retrieval_Miss"] += 1
            reason = f"Retrieval Miss (Expected {expected} not in Top-K)"

        if i < 10:
            print(f"Q{row['queryId']}: {reason} | TopK: {topk_str}")

    print("\n--- Failure Taxonomy Summary ---")
    for k, v in type_counts.items():
        print(f"{k}: {v}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 failure_analysis.py <csv_file>")
    else:
        analyze(sys.argv[1])
