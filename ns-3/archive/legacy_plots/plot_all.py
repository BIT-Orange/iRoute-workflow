#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
import os
import numpy as np

# Set style
sns.set_theme(style="whitegrid", context="paper", font_scale=1.2)
plt.rcParams['pdf.fonttype'] = 42
plt.rcParams['ps.fonttype'] = 42

def load_data(run_dir):
    """Load data from a run directory."""
    data = {}
    
    # Load summary if available (glob matching exp2.1_comparison*.csv)
    summary_dfs = []
    for f in os.listdir(run_dir):
        if f.startswith("exp2.1_comparison") and f.endswith(".csv"):
             summary_dfs.append(pd.read_csv(os.path.join(run_dir, f)))
             
    if summary_dfs:
        data['summary'] = pd.concat(summary_dfs, ignore_index=True)
        
    # Load per-query logs
    # We expect run_query_log_<method>.csv
    methods = ['iroute', 'flooding', 'centralized', 'exact', 'randomk']
    dfs = []
    
    for m in methods:
        f = os.path.join(run_dir, f"run_query_log_{m}.csv")
        if os.path.exists(f):
            print(f"Loading {f}...")
            df = pd.read_csv(f)
            df['method'] = m
            # Normalize column names if needed
            # Ensure stage2HopCount exist
            if 'stage2HopCount' not in df.columns:
                 df['stage2HopCount'] = np.nan
            
            # Ensure numeric types for critical columns
            if 'stage2Success' in df.columns:
                df['stage2Success'] = pd.to_numeric(df['stage2Success'], errors='coerce')
                
            if 'totalMs' in df.columns:
                df['totalMs'] = pd.to_numeric(df['totalMs'], errors='coerce')

            # Detect and fix column shift (missing queryId/startTime)
            # If stage2Success (col 11) is > 1 (e.g. timestamp), we are shifted.
            if 'stage2Success' in df.columns:
                unique_vals = df['stage2Success'].dropna().unique()
                if len(unique_vals) > 0 and (unique_vals.max() > 1.0 or unique_vals.min() < 0.0) and not np.all(np.isin(unique_vals, [0,1])):
                    print(f"Detected column shift in {m}. Fixing...")
                    # Shift is -2 approx.
                    # Mapping based on visual inspection:
                    # stage2Success (Header 11) -> Data 11 (Timestamp) -> Data 9 is real success (Header 9 'discoveryAttempts')
                    # totalMs (Header 14) -> Data 14 (Empty) -> Data 12 is real totalMs (Header 12 'stage1RttMs')
                    
                    if 'discoveryAttempts' in df.columns:
                        df['stage2Success'] = df['discoveryAttempts'] # Map Col 9 to Success
                    if 'stage1RttMs' in df.columns:
                        df['totalMs'] = df['stage1RttMs'] # Map Col 12 to totalMs
                        
                    # Fix numeric conversion again after shift
                    df['stage2Success'] = pd.to_numeric(df['stage2Success'], errors='coerce')
                    df['totalMs'] = pd.to_numeric(df['totalMs'], errors='coerce')
                    
            dfs.append(df)
    
    if dfs:
        data['queries'] = pd.concat(dfs, ignore_index=True)
        
    return data

def plot_accuracy(summary_df, out_dir):
    """Plot Domain and Doc Accuracy per method from summary."""
    if summary_df is None or summary_df.empty:
        print("No summary data for accuracy plot.")
        return
    
    # Summary cols: method,queries,success,domain_acc,doc_acc,...
    # We want to plot domain_acc and doc_acc
    
    # Transform to long format for seaborn
    rows = []
    for _, row in summary_df.iterrows():
        m = row['method']
        d_acc = row['domain_acc'] / 100.0 # Convert % to fraction
        doc_acc = row['doc_acc'] / 100.0
        
        rows.append({'Method': m, 'Type': 'Domain', 'Accuracy': d_acc})
        rows.append({'Method': m, 'Type': 'Document', 'Accuracy': doc_acc})
        
    plot_df = pd.DataFrame(rows)
    
    plt.figure(figsize=(8, 5))
    sns.barplot(data=plot_df, x='Method', y='Accuracy', hue='Type', palette="viridis")
    plt.title("Accuracy by Method")
    plt.ylim(0, 1.05)
    plt.ylabel("Accuracy (Fraction)")
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "accuracy_bar.pdf"))
    plt.close()
    print("Saved accuracy_bar.pdf")

def plot_latency_cdf(df, out_dir):
    """Plot Latency CDF for successful queries."""
    if df is None: return
    
    plt.figure(figsize=(8, 6))
    
    # Filter successful queries.
    # We assume successful queries have totalMs > 0 AND (optionally) totalMs < timeout
    # Or rely on stage2Success if available.
    if 'stage2Success' in df.columns:
        print(f"Debug: stage2Success unique: {df['stage2Success'].unique()}")
        print(f"Debug: totalMs stats: {df['totalMs'].describe()}")
        success_df = df[df['stage2Success'] == 1].copy()
        print(f"Debug: success_df len: {len(success_df)}")
    else:
        # Fallback: assume latency < 3500 (timeout is 4000) is success
        success_df = df[(df['totalMs'] > 0) & (df['totalMs'] < 3900)].copy()

    if success_df.empty:
        print("No successful queries for latency plot.")
        return

    sns.ecdfplot(data=success_df, x="totalMs", hue="method", palette="deep", linewidth=2)
    plt.xlabel("Latency (ms)")
    plt.ylabel("CDF")
    plt.title("End-to-End Latency CDF (Successful Queries)")
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "latency_cdf.pdf"))
    plt.close()
    print("Saved latency_cdf.pdf")

def plot_hops(df, out_dir):
    """Plot Hop Count distribution (bar or box)."""
    if df is None: return
    
    # Filter valid hops
    valid = df[df['stage2HopCount'] >= 0].copy()
    if valid.empty: return
    
    plt.figure(figsize=(6, 5))
    sns.boxplot(data=valid, x='method', y='stage2HopCount', palette="Set2")
    plt.title("Content Fetch Hop Count")
    plt.ylabel("Hops")
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "hops_box.pdf"))
    plt.close()
    print("Saved hops_box.pdf")

def plot_overhead(df, out_dir):
    """Plot Byte Overhead (Control vs Data)."""
    if df is None: return
    
    methods = df['method'].unique()
    rows = []
    
    for m in methods:
        sub = df[df['method'] == m]
        n_queries = len(sub)
        if n_queries == 0: continue
        
        avg_ctrl = sub['totalControlBytes'].mean()
        avg_data = sub['totalDataBytes'].mean()
        
        rows.append({'Method': m, 'Type': 'Control (Interest)', 'Bytes': avg_ctrl})
        rows.append({'Method': m, 'Type': 'Data', 'Bytes': avg_data})
        
    plot_df = pd.DataFrame(rows)
    
    plt.figure(figsize=(8, 5))
    sns.barplot(data=plot_df, x='Method', y='Bytes', hue='Type', palette="muted")
    plt.title("Average Byte Overhead per Query")
    plt.ylabel("Bytes")
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "overhead_bytes.pdf"))
    plt.close()
    print("Saved overhead_bytes.pdf")

def plot_state(out_dir):
    """Placeholder for State over time plot."""
    # Logic to load state_log.csv and plot soft-state count vs time
    pass

def plot_recovery(out_dir):
    """Placeholder for Recovery timeline plot."""
    # Logic to load recovery_log.csv and plot node up/down events
    pass

def main():
    parser = argparse.ArgumentParser(description="Plot iRoute Results")
    parser.add_argument("--runDir", required=True, help="Run directory (e.g., results/run_XYZ)")
    parser.add_argument("--outDir", help="Output directory for plots (defaults to runDir)")
    args = parser.parse_args()
    
    out_dir = args.outDir if args.outDir else args.runDir
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)
        
    print(f"Loading data from {args.runDir}...")
    data = load_data(args.runDir)
    
    if 'summary' in data:
        plot_accuracy(data['summary'], out_dir)
    else:
        print("No summary data found for accuracy plot.")
        
    if 'queries' in data:
        df = data['queries']
        print(f"Loaded {len(df)} query records.")
        plot_latency_cdf(df, out_dir)
        plot_hops(df, out_dir)
        plot_overhead(df, out_dir)
    else:
        print("No query logs found.")
        
    # Placeholders for future experiments
    plot_state(out_dir)
    plot_recovery(out_dir)

if __name__ == "__main__":
    main()
