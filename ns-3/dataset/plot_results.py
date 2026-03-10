#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import sys

def main():
    if len(sys.argv) > 1:
        res_dir = sys.argv[1]
    else:
        res_dir = "results/accuracy_comparison"
        
    csv_file = os.path.join(res_dir, "comparison.csv")
    if not os.path.exists(csv_file):
        print(f"Error: {csv_file} not found.")
        sys.exit(1)
        
    df = pd.read_csv(csv_file)
    print("Loaded data:")
    print(df)
    
    # Preprocess
    df['Control_KBps'] = df['ctrl_bytes_per_sec'] / 1024.0
    
    # Setup plot
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('Semantic Routing Scheme Comparison', fontsize=16)
    
    schemes = df['scheme']
    x = np.arange(len(schemes))
    width = 0.6
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728'] # blue, orange, green, red
    
    # 1. Domain Accuracy
    ax = axes[0, 0]
    bars = ax.bar(x, df['DomainAcc']*100, width, color=colors)
    ax.set_ylabel('Accuracy (%)')
    ax.set_title('Domain Discovery Accuracy')
    ax.set_xticks(x)
    ax.set_xticklabels(schemes)
    ax.set_ylim(0, 100)
    for bar in bars:
        height = bar.get_height()
        ax.annotate(f'{height:.1f}%', xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3), textcoords="offset points", ha='center', va='bottom')

    # 2. Latency (P50)
    ax = axes[0, 1]
    bars = ax.bar(x, df['P50_ms'], width, color=colors)
    ax.set_ylabel('Latency (ms)')
    ax.set_title('Median Latency (P50)')
    ax.set_xticks(x)
    ax.set_xticklabels(schemes)
    for bar in bars:
        height = bar.get_height()
        ax.annotate(f'{height:.1f}ms', xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3), textcoords="offset points", ha='center', va='bottom')

    # 3. Control Overhead
    ax = axes[1, 0]
    bars = ax.bar(x, df['Control_KBps'], width, color=colors)
    ax.set_ylabel('Overhead (KB/s)')
    ax.set_title('Control Plane Overhead')
    ax.set_xticks(x)
    ax.set_xticklabels(schemes)
    for bar in bars:
        height = bar.get_height()
        ax.annotate(f'{height:.1f}', xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3), textcoords="offset points", ha='center', va='bottom')

    # 4. Recall@1
    ax = axes[1, 1]
    bars = ax.bar(x, df['Recall_at_1']*100, width, color=colors)
    ax.set_ylabel('Recall@1 (%)')
    ax.set_title('Document Retrieval Recall@1')
    ax.set_xticks(x)
    ax.set_xticklabels(schemes)
    ax.set_ylim(0, 100)
    for bar in bars:
        height = bar.get_height()
        ax.annotate(f'{height:.1f}%', xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3), textcoords="offset points", ha='center', va='bottom')

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    out_file = os.path.join(res_dir, "comparison_plots.png")
    plt.savefig(out_file)
    print(f"Saved plots to {out_file}")

if __name__ == "__main__":
    main()
