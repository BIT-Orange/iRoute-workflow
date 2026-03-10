#!/usr/bin/env python3
"""
plot_exp2.py - Generate publication-quality plots for Exp2 results

Generates:
- Figure 2a: Method comparison (Accuracy, Latency, Probes, Traffic)
- Figure 2b: Failure type breakdown by method
- Figure 2c: Parameter sweep heatmaps
"""

import os
import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.patches import Patch

# Style settings for publication
plt.rcParams.update({
    'font.size': 10,
    'font.family': 'serif',
    'axes.labelsize': 11,
    'axes.titlesize': 12,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'legend.fontsize': 9,
    'figure.figsize': (7, 5),
    'figure.dpi': 150,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
})

# Color scheme
COLORS = {
    'iRoute': '#1f77b4',
    'Flooding': '#ff7f0e', 
    'Centralized': '#2ca02c',
}

FAILURE_COLORS = {
    'SUCCESS': '#2ecc71',
    'DOMAIN_WRONG': '#e74c3c',
    'DOC_WRONG': '#9b59b6',
    'DISCOVERY_TIMEOUT': '#f39c12',
    'DISCOVERY_NACK': '#3498db',
    'FETCH_TIMEOUT': '#1abc9c',
    'FETCH_NACK': '#34495e',
}


def plot_method_comparison(result_dir, output_dir):
    """Plot method comparison bar charts."""
    csv_path = os.path.join(result_dir, 'exp2.1_comparison.csv')
    if not os.path.exists(csv_path):
        print(f"Warning: {csv_path} not found, skipping method comparison plot")
        return
    
    df = pd.read_csv(csv_path)
    
    methods = df['method'].tolist()
    metrics = [
        ('accuracy', 'Accuracy', '%'),
        ('avgLatency_ms', 'Avg Latency', 'ms'),
        ('avgProbes', 'Avg Probes', '#'),
        ('totalTraffic_KB', 'Total Traffic', 'KB'),
    ]
    
    fig, axes = plt.subplots(2, 2, figsize=(10, 8))
    axes = axes.flatten()
    
    for idx, (col, label, unit) in enumerate(metrics):
        ax = axes[idx]
        colors = [COLORS.get(m, '#888888') for m in methods]
        bars = ax.bar(methods, df[col], color=colors, edgecolor='black', linewidth=0.5)
        
        ax.set_ylabel(f'{label} ({unit})')
        ax.set_title(f'{label} Comparison')
        
        # Add value labels on bars
        for bar, val in zip(bars, df[col]):
            height = bar.get_height()
            ax.annotate(f'{val:.1f}' if val < 100 else f'{val:.0f}',
                       xy=(bar.get_x() + bar.get_width() / 2, height),
                       xytext=(0, 3), textcoords='offset points',
                       ha='center', va='bottom', fontsize=8)
    
    plt.tight_layout()
    
    out_path = os.path.join(output_dir, 'fig2a_method_comparison.pdf')
    plt.savefig(out_path)
    plt.savefig(out_path.replace('.pdf', '.png'))
    print(f"Saved: {out_path}")
    plt.close()


def plot_failure_breakdown(result_dir, output_dir):
    """Plot failure type breakdown as stacked bar chart."""
    csv_path = os.path.join(result_dir, 'exp2.1_failure_breakdown.csv')
    if not os.path.exists(csv_path):
        print(f"Warning: {csv_path} not found, skipping failure breakdown plot")
        return
    
    df = pd.read_csv(csv_path)
    
    methods = df['method'].unique()
    failure_types = [col for col in df.columns if col != 'method']
    
    fig, ax = plt.subplots(figsize=(8, 5))
    
    x = np.arange(len(methods))
    width = 0.6
    
    bottom = np.zeros(len(methods))
    for ft in failure_types:
        values = df.set_index('method').loc[methods, ft].values
        color = FAILURE_COLORS.get(ft, '#888888')
        ax.bar(x, values, width, label=ft, bottom=bottom, color=color, edgecolor='white', linewidth=0.5)
        bottom += values
    
    ax.set_xticks(x)
    ax.set_xticklabels(methods)
    ax.set_ylabel('Query Count')
    ax.set_title('Failure Type Breakdown by Method')
    ax.legend(loc='upper right', ncol=2, fontsize=8)
    
    plt.tight_layout()
    
    out_path = os.path.join(output_dir, 'fig2b_failure_breakdown.pdf')
    plt.savefig(out_path)
    plt.savefig(out_path.replace('.pdf', '.png'))
    print(f"Saved: {out_path}")
    plt.close()


def plot_parameter_sweep(result_dir, output_dir):
    """Plot parameter sweep heatmaps."""
    csv_path = os.path.join(result_dir, 'exp2.2_sweep.csv')
    if not os.path.exists(csv_path):
        print(f"Warning: {csv_path} not found, skipping parameter sweep plot")
        return
    
    df = pd.read_csv(csv_path)
    
    # Create separate heatmaps for different parameter combinations
    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    
    # Heatmap 1: Accuracy vs (M, kMax) at fixed domains=8, tau=0.05
    ax = axes[0, 0]
    subset = df[(df['domains'] == 8) & (df['tau'] == 0.05)]
    if len(subset) > 0:
        pivot = subset.pivot_table(index='M', columns='kMax', values='accuracy', aggfunc='mean')
        im = ax.imshow(pivot.values, cmap='RdYlGn', aspect='auto', vmin=0, vmax=100)
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels(pivot.columns)
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels(pivot.index)
        ax.set_xlabel('kMax')
        ax.set_ylabel('M')
        ax.set_title('Accuracy (%) | D=8, τ=0.05')
        plt.colorbar(im, ax=ax, label='Accuracy (%)')
        
        # Add text annotations
        for i in range(len(pivot.index)):
            for j in range(len(pivot.columns)):
                val = pivot.values[i, j]
                ax.text(j, i, f'{val:.1f}', ha='center', va='center', fontsize=8)
    
    # Heatmap 2: Avg Probes vs (M, kMax)
    ax = axes[0, 1]
    subset = df[(df['domains'] == 8) & (df['tau'] == 0.05)]
    if len(subset) > 0:
        pivot = subset.pivot_table(index='M', columns='kMax', values='avgProbes', aggfunc='mean')
        im = ax.imshow(pivot.values, cmap='YlOrRd', aspect='auto')
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels(pivot.columns)
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels(pivot.index)
        ax.set_xlabel('kMax')
        ax.set_ylabel('M')
        ax.set_title('Avg Probes | D=8, τ=0.05')
        plt.colorbar(im, ax=ax, label='Avg Probes')
        
        for i in range(len(pivot.index)):
            for j in range(len(pivot.columns)):
                val = pivot.values[i, j]
                ax.text(j, i, f'{val:.2f}', ha='center', va='center', fontsize=8)
    
    # Heatmap 3: Accuracy vs (domains, tau) at M=4, kMax=3
    ax = axes[1, 0]
    subset = df[(df['M'] == 4) & (df['kMax'] == 3)]
    if len(subset) > 0:
        pivot = subset.pivot_table(index='domains', columns='tau', values='accuracy', aggfunc='mean')
        im = ax.imshow(pivot.values, cmap='RdYlGn', aspect='auto', vmin=0, vmax=100)
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([f'{t:.2f}' for t in pivot.columns])
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels(pivot.index)
        ax.set_xlabel('τ (tau)')
        ax.set_ylabel('Domains')
        ax.set_title('Accuracy (%) | M=4, kMax=3')
        plt.colorbar(im, ax=ax, label='Accuracy (%)')
        
        for i in range(len(pivot.index)):
            for j in range(len(pivot.columns)):
                val = pivot.values[i, j]
                ax.text(j, i, f'{val:.1f}', ha='center', va='center', fontsize=8)
    
    # Heatmap 4: Latency vs (domains, tau)
    ax = axes[1, 1]
    subset = df[(df['M'] == 4) & (df['kMax'] == 3)]
    if len(subset) > 0:
        pivot = subset.pivot_table(index='domains', columns='tau', values='avgLatency_ms', aggfunc='mean')
        im = ax.imshow(pivot.values, cmap='YlOrRd', aspect='auto')
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([f'{t:.2f}' for t in pivot.columns])
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels(pivot.index)
        ax.set_xlabel('τ (tau)')
        ax.set_ylabel('Domains')
        ax.set_title('Avg Latency (ms) | M=4, kMax=3')
        plt.colorbar(im, ax=ax, label='Latency (ms)')
        
        for i in range(len(pivot.index)):
            for j in range(len(pivot.columns)):
                val = pivot.values[i, j]
                ax.text(j, i, f'{val:.1f}', ha='center', va='center', fontsize=8)
    
    plt.tight_layout()
    
    out_path = os.path.join(output_dir, 'fig2c_parameter_sweep.pdf')
    plt.savefig(out_path)
    plt.savefig(out_path.replace('.pdf', '.png'))
    print(f"Saved: {out_path}")
    plt.close()


def plot_accuracy_tradeoff(result_dir, output_dir):
    """Plot accuracy vs probes/latency tradeoff curves."""
    csv_path = os.path.join(result_dir, 'exp2.2_sweep.csv')
    if not os.path.exists(csv_path):
        return
    
    df = pd.read_csv(csv_path)
    
    fig, axes = plt.subplots(1, 2, figsize=(10, 4))
    
    # Plot 1: Accuracy vs Avg Probes (colored by M)
    ax = axes[0]
    for m in sorted(df['M'].unique()):
        subset = df[df['M'] == m]
        ax.scatter(subset['avgProbes'], subset['accuracy'], 
                  label=f'M={m}', alpha=0.7, s=30)
    ax.set_xlabel('Avg Probes')
    ax.set_ylabel('Accuracy (%)')
    ax.set_title('Accuracy vs Exploration Cost')
    ax.legend(title='M')
    ax.grid(True, alpha=0.3)
    
    # Plot 2: Accuracy vs Latency (colored by kMax)
    ax = axes[1]
    for k in sorted(df['kMax'].unique()):
        subset = df[df['kMax'] == k]
        ax.scatter(subset['avgLatency_ms'], subset['accuracy'],
                  label=f'kMax={k}', alpha=0.7, s=30)
    ax.set_xlabel('Avg Latency (ms)')
    ax.set_ylabel('Accuracy (%)')
    ax.set_title('Accuracy vs Response Time')
    ax.legend(title='kMax')
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    out_path = os.path.join(output_dir, 'fig2d_tradeoff_curves.pdf')
    plt.savefig(out_path)
    plt.savefig(out_path.replace('.pdf', '.png'))
    print(f"Saved: {out_path}")
    plt.close()


def main():
    if len(sys.argv) < 2:
        print("Usage: python plot_exp2.py <result_dir> [output_dir]")
        print("Example: python plot_exp2.py results/exp2.1_packet figures/")
        sys.exit(1)
    
    result_dir = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else 'figures'
    
    os.makedirs(output_dir, exist_ok=True)
    
    print(f"Reading results from: {result_dir}")
    print(f"Saving figures to: {output_dir}")
    print()
    
    # Generate all plots
    plot_method_comparison(result_dir, output_dir)
    plot_failure_breakdown(result_dir, output_dir)
    
    # Check for sweep results
    sweep_dir = result_dir.replace('exp2.1_packet', 'exp2.2_sweep')
    if os.path.exists(os.path.join(sweep_dir, 'exp2.2_sweep.csv')):
        plot_parameter_sweep(sweep_dir, output_dir)
        plot_accuracy_tradeoff(sweep_dir, output_dir)
    
    print("\nDone!")


if __name__ == '__main__':
    main()
