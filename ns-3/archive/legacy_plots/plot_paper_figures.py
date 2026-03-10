#!/usr/bin/env python3
import os
import sys
import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import seaborn as sns
from glob import glob

# =============================================================================
# Style Configuration (Paper Quality)
# =============================================================================
def set_style():
    sns.set_style("whitegrid")
    plt.rcParams["font.family"] = "serif"
    # plt.rcParams["font.serif"] = ["Times New Roman"] # Uncomment if installed
    plt.rcParams["font.size"] = 12
    plt.rcParams["axes.labelsize"] = 14
    plt.rcParams["axes.titlesize"] = 14
    plt.rcParams["xtick.labelsize"] = 12
    plt.rcParams["ytick.labelsize"] = 12
    plt.rcParams["legend.fontsize"] = 12
    plt.rcParams["lines.linewidth"] = 2.0
    plt.rcParams["lines.markersize"] = 8
    
    global STYLES
    STYLES = {
        'iroute':  {'color': 'black', 'linestyle': '-',  'marker': 'o', 'label': 'iRoute'},
        'flood':   {'color': 'gray',  'linestyle': '--', 'marker': 'x', 'label': 'Flood'},
        'tag':     {'color': 'blue',  'linestyle': ':',  'marker': 's', 'label': 'Tag'},
        'exact':   {'color': 'green', 'linestyle': '-.', 'marker': '^', 'label': 'Exact'},
        'central': {'color': 'red',   'linestyle': '-',  'marker': 'v', 'label': 'Central'},
    }

# =============================================================================
# 1. Accuracy vs Overhead
# =============================================================================
def plot_accuracy_overhead(csv_path, output_dir):
    print(f"Plotting Accuracy vs Overhead from {csv_path}...")
    if not os.path.exists(csv_path):
        print(f"Warning: {csv_path} not found. Skipping.")
        return

    df = pd.read_csv(csv_path)
    
    # Normalize columns
    # CSV has: scheme, ctrl_bytes_per_sec, Recall_at_1, DomainAcc
    
    x_col = 'ctrl_bytes_per_sec'
    y_col = 'Recall_at_1' # or DomainAcc
    scheme_col = 'scheme'

    if x_col not in df.columns:
        print(f"Warning: {x_col} not found in {df.columns}. Scaling script output?")
        return
        
    fig, ax = plt.subplots(figsize=(6, 4.5))
    
    for scheme in df[scheme_col].unique():
        sub = df[df[scheme_col] == scheme]
        s_key = scheme.lower().split('-')[0]
        style = STYLES.get(s_key, {'label': scheme})
        
        # Plot point
        ax.plot(sub[x_col], sub[y_col], **style)
        
        # Annotate
        for i, row in sub.iterrows():
            ax.text(row[x_col], row[y_col]+0.005, scheme, fontsize=9)
            
    ax.set_xlabel("Control Overhead (Bytes/s)")
    ax.set_ylabel("Recall@1")
    ax.set_title("Accuracy vs. Overhead")
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, "fig1_accuracy_overhead.pdf"))
    print(f"Saved fig1_accuracy_overhead.pdf")
    plt.close()

# =============================================================================
# 2. Retrieval Time CDF
# =============================================================================
def plot_retrieval_cdf(acc_dir, output_dir):
    print(f"Plotting Retrieval Time CDF from {acc_dir}...")
    # Look for query_log.csv in subdirectories or root
    # Recursively find
    files = glob(os.path.join(acc_dir, "**", "query_log.csv"), recursive=True)
    if not files:
        files = glob(os.path.join(acc_dir, "query_log.csv")) # single file?
    
    if not files:
        print("Warning: No query_log.csv found.")
        return

    fig, ax = plt.subplots(figsize=(6, 4.5))
    plotted = False
    
    for f in files:
        # Infer scheme from directory name or content
        dirname = os.path.basename(os.path.dirname(f))
        
        df = pd.read_csv(f)
        if 'rtt_total_ms' not in df.columns: continue
        
        # Scheme column in CSV?
        if 'scheme' in df.columns:
            # Maybe file contains multiple schemes
            for scheme in df['scheme'].unique():
                sub = df[df['scheme'] == scheme]
                data = sub['rtt_total_ms'].dropna().sort_values()
                if len(data) == 0: continue
                
                yvals = np.arange(len(data)) / float(len(data) - 1)
                s_key = scheme.lower()
                style = STYLES.get(s_key, {'label': scheme})
                
                ax.plot(data, yvals, label=style['label'], 
                        color=style.get('color'), linestyle=style.get('linestyle'), linewidth=2)
                plotted = True
        else:
            # Infer scheme from dirname
            scheme = dirname.split('_')[0]
            data = df['rtt_total_ms'].dropna().sort_values()
            if len(data) == 0: continue
            
            yvals = np.arange(len(data)) / float(len(data) - 1)
            s_key = scheme.lower()
            style = STYLES.get(s_key, {'label': scheme})
            
            ax.plot(data, yvals, label=style['label'], 
                    color=style.get('color'), linestyle=style.get('linestyle'), linewidth=2)
            plotted = True
        
    if plotted:
        ax.set_xlabel("Retrieval Time (ms)")
        ax.set_ylabel("CDF")
        ax.set_title("Retrieval Time Distribution")
        ax.legend()
        ax.set_xlim(left=0) 
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, "fig2_retrieval_cdf.pdf"))
        print(f"Saved fig2_retrieval_cdf.pdf")
    plt.close()

# =============================================================================
# 5. Failure Recovery
# =============================================================================
def plot_failure_recovery(fail_dir, output_dir):
    print(f"Plotting Failure Recovery from {fail_dir}...")
    
    scenarios = ['link-fail', 'domain-fail', 'churn']
    
    for sc in scenarios:
        subdirs = glob(os.path.join(fail_dir, f"*{sc}"))
        if not subdirs: continue
        
        fig, ax = plt.subplots(figsize=(8, 4))
        plotted = False
        
        for d in subdirs:
            scheme = os.path.basename(d).split(f'-{sc}')[0] # iroute-link-fail -> iroute
            log_file = os.path.join(d, "query_log.csv")
            if not os.path.exists(log_file): continue
            
            df = pd.read_csv(log_file)
            # QueryLog has: qid, t_send_disc (ms), domain_hit (0/1)
            # Use 't_send_disc' or 't_recv_data' as timestamp
            time_col = 't_send_disc'
            if time_col not in df.columns: continue
            
            # Bin by 1s
            df = df.dropna(subset=[time_col])
            df['TimeSec'] = (df[time_col] / 1000).astype(int)
            if df.empty: continue
            
            max_time = df['TimeSec'].max()
            if np.isnan(max_time) or np.isinf(max_time): continue
            
            all_seconds = pd.DataFrame({'TimeSec': range(int(max_time)+1)})
            
            # Metric: domain_hit or (hops_fetch > 0)
            if 'domain_hit' in df.columns:
                df['Success'] = df['domain_hit']
            elif 'Status' in df.columns:
                df['Success'] = df['Status'].apply(lambda x: 1 if x == 'Found' else 0)
            else:
                continue

            grouped = df.groupby('TimeSec')['Success'].mean().reset_index()
            merged = pd.merge(all_seconds, grouped, on='TimeSec', how='left').fillna(0) # Assume 0 success if no queries? Or ffill?
            # Using 0 is safer for outage
            
            # Rolling average
            merged['Smooth'] = merged['Success'].rolling(window=5, min_periods=1).mean()
            
            s_key = scheme.lower()
            style = STYLES.get(s_key, {'label': scheme})
            
            ax.plot(merged['TimeSec'], merged['Smooth'], label=style['label'], 
                    color=style.get('color'), linestyle=style.get('linestyle'))
            plotted = True

        if plotted:
            ax.set_xlabel("Simulation Time (s)")
            ax.set_ylabel("Success Rate (5s Avg)")
            ax.set_title(f"Recovery: {sc}")
            ax.set_ylim(0, 1.1)
            ax.legend()
            
            # Mark Failure Time @ 50s (hardcoded in experiment)
            ax.axvline(x=50, color='red', linestyle=':', alpha=0.5, label='Failure Event')
            
            plt.tight_layout()
            plt.savefig(os.path.join(output_dir, f"fig5_recovery_{sc}.pdf"))
            print(f"Saved fig5_recovery_{sc}.pdf")
        plt.close()

# =============================================================================
# 3. Hop Count vs Load
# =============================================================================
def plot_hop_load(load_csv, output_dir):
    print(f"Plotting Hop Count vs Load from {load_csv}...")
    if not os.path.exists(load_csv):
        print("Warning: load.csv not found.")
        return

    df = pd.read_csv(load_csv)
    # csv: scheme,frequency,mean_hops
    if 'mean_hops' not in df.columns: return

    fig, ax = plt.subplots(figsize=(6, 4.5))
    
    for scheme in df['scheme'].unique():
        sub = df[df['scheme'] == scheme].sort_values('frequency')
        s_key = scheme.lower()
        style = STYLES.get(s_key, {'label': scheme})
        
        ax.plot(sub['frequency'], sub['mean_hops'], **style)
        
    ax.set_xlabel("Query Load (Queries/sec)")
    ax.set_ylabel("Avg Hops (Discovery + Fetch)")
    ax.set_title("Hop Count vs. Load")
    ax.set_xscale('log')
    ax.set_xticks([1, 5, 10, 20, 50])
    ax.get_xaxis().set_major_formatter(ticker.ScalarFormatter())
    ax.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, "fig3_hop_load.pdf"))
    print("Saved fig3_hop_load.pdf")
    plt.close()

# =============================================================================
# 4. State Scaling
# =============================================================================
def plot_scaling(scaling_csv, output_dir):
    print(f"Plotting State Scaling from {scaling_csv}...")
    if not os.path.exists(scaling_csv):
        print("Warning: scaling.csv not found.")
        return

    df = pd.read_csv(scaling_csv)
    # csv: domains,lsdb_entries,fib_entries
    
    fig, ax1 = plt.subplots(figsize=(6, 4.5))
    
    # Plot LSDB on left Y
    color = 'tab:blue'
    ax1.set_xlabel('Network Size (Number of Domains)')
    ax1.set_ylabel('LSDB Entries (Ingress)', color=color)
    ax1.plot(df['domains'], df['lsdb_entries'], color=color, marker='o', label='LSDB Size')
    ax1.tick_params(axis='y', labelcolor=color)
    
    # Plot FIB on right Y
    ax2 = ax1.twinx()  
    color = 'tab:red'
    ax2.set_ylabel('FIB Entries (Ingress)', color=color)
    ax2.plot(df['domains'], df['fib_entries'], color=color, marker='s', linestyle='--', label='FIB Size')
    ax2.tick_params(axis='y', labelcolor=color)
    
    plt.title("State Scaling vs Network Size")
    fig.tight_layout() 
    plt.savefig(os.path.join(output_dir, "fig4_state_scaling.pdf"))
    print("Saved fig4_state_scaling.pdf")
    plt.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--acc-dir", default="results/accuracy_comparison", help="Accuracy exp dir")
    parser.add_argument("--fail-dir", default="results/exp3-failure", help="Failure exp dir")
    parser.add_argument("--output", default="results/plots", help="Output dir")
    args = parser.parse_args()
    
    set_style()
    os.makedirs(args.output, exist_ok=True)
    
    try:
        plot_accuracy_overhead(os.path.join(args.acc_dir, "comparison.csv"), args.output)
    except Exception as e:
        print(f"Error plotting Fig 1: {e}")
        import traceback
        traceback.print_exc()
        
    try:
        plot_retrieval_cdf(args.acc_dir, args.output)
    except Exception as e:
        print(f"Error plotting Fig 2: {e}")
        
    try:
        plot_hop_load(os.path.join(args.output, "../exp4-load/load.csv"), args.output)
    except Exception as e:
        print(f"Error plotting Fig 3: {e}")

    try:
        plot_scaling(os.path.join(args.output, "../exp4-scaling/scaling.csv"), args.output)
    except Exception as e:
        print(f"Error plotting Fig 4: {e}")

    try:
        plot_failure_recovery(args.fail_dir, args.output)
    except Exception as e:
        print(f"Error plotting Fig 5: {e}")
