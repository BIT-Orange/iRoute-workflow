#!/usr/bin/env python3
"""
iRoute Experiment Visualization Script
Generates publication-quality figures for Exp1 (Comprehensive) and Exp2 (Baseline Comparison)

Usage:
    python plot_experiments.py --exp9Dir results/exp9_comprehensive --exp2Dir results/exp2_rocketfuel --outDir figures
"""

import os
import sys
import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.gridspec import GridSpec

# Publication-quality settings
plt.rcParams.update({
    'font.family': 'serif',
    'font.serif': ['Times New Roman', 'DejaVu Serif'],
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'legend.fontsize': 10,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'figure.dpi': 150,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'savefig.pad_inches': 0.05,
    'axes.grid': True,
    'grid.alpha': 0.3,
    'axes.axisbelow': True,
})

# Color palette (colorblind-friendly)
COLORS = {
    'iRoute': '#2E86AB',       # Blue
    'Centralized': '#A23B72',  # Purple
    'Flood-Parallel': '#F18F01',  # Orange
    'Flood-Sequential': '#C73E1D',  # Red
    'tau_line': '#2E86AB',
    'probes_line': '#E94F37',
    'hit1': '#2E86AB',
    'hit2': '#28A745',
    'hit3': '#FFC107',
}

MARKERS = {
    'iRoute': 'o',
    'Centralized': 's',
    'Flood-Parallel': '^',
    'Flood-Sequential': 'D',
}

def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


# =============================================================================
# Figure 1: Baseline Comparison (Bar Charts)
# =============================================================================
def plot_exp2_baselines(exp2_dir, out_dir):
    """Plot Exp2 baseline comparison as grouped bar charts"""
    summary_file = os.path.join(exp2_dir, 'exp2_summary.csv')
    if not os.path.exists(summary_file):
        print(f"Warning: {summary_file} not found, skipping exp2 plots")
        return
    
    df = pd.read_csv(summary_file)
    
    # Reorder methods
    method_order = ['iRoute', 'Flood-Parallel', 'Flood-Sequential', 'Centralized']
    df['method'] = pd.Categorical(df['method'], categories=method_order, ordered=True)
    df = df.sort_values('method')
    
    methods = df['method'].tolist()
    colors = [COLORS.get(m, '#666666') for m in methods]
    
    fig, axes = plt.subplots(2, 2, figsize=(10, 8))
    
    # (a) Accuracy
    ax = axes[0, 0]
    x = np.arange(len(methods))
    width = 0.35
    domain_acc = df['domain_accuracy'].values * 100
    doc_acc = df['doc_accuracy'].values * 100
    
    bars1 = ax.bar(x - width/2, domain_acc, width, label='Domain Accuracy', color='#2E86AB', edgecolor='black', linewidth=0.5)
    bars2 = ax.bar(x + width/2, doc_acc, width, label='Document Accuracy', color='#A23B72', edgecolor='black', linewidth=0.5)
    
    ax.set_ylabel('Accuracy (%)')
    ax.set_xticks(x)
    ax.set_xticklabels(methods, rotation=15, ha='right')
    ax.set_ylim(0, 110)
    ax.legend(loc='lower right')
    ax.set_title('(a) Routing Accuracy')
    
    # Add value labels
    for bar in bars1:
        height = bar.get_height()
        ax.annotate(f'{height:.1f}', xy=(bar.get_x() + bar.get_width()/2, height),
                   xytext=(0, 3), textcoords="offset points", ha='center', va='bottom', fontsize=8)
    for bar in bars2:
        height = bar.get_height()
        ax.annotate(f'{height:.1f}', xy=(bar.get_x() + bar.get_width()/2, height),
                   xytext=(0, 3), textcoords="offset points", ha='center', va='bottom', fontsize=8)
    
    # (b) Probes / Bandwidth
    ax = axes[0, 1]
    probes = df['avg_probes'].values
    bytes_kb = df['avg_total_bytes'].values / 1000
    
    bars1 = ax.bar(x - width/2, probes, width, label='Avg Probes', color='#F18F01', edgecolor='black', linewidth=0.5)
    ax.set_ylabel('Average Probes')
    ax.set_xticks(x)
    ax.set_xticklabels(methods, rotation=15, ha='right')
    ax.set_ylim(0, max(probes) * 1.3)
    
    ax2 = ax.twinx()
    ax2.plot(x, bytes_kb, 'D-', color='#C73E1D', markersize=8, linewidth=2, label='Total Bytes')
    ax2.set_ylabel('Total Bytes (KB)', color='#C73E1D')
    ax2.tick_params(axis='y', labelcolor='#C73E1D')
    
    # Combined legend
    lines1, labels1 = ax.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(lines1 + lines2, labels1 + labels2, loc='upper right')
    ax.set_title('(b) Communication Overhead')
    
    # (c) Latency
    ax = axes[1, 0]
    stage1_lat = df['avg_stage1_latency_ms'].values
    total_lat = df['avg_total_latency_ms'].values
    stage2_lat = total_lat - stage1_lat
    
    bars1 = ax.bar(x, stage1_lat, width*1.5, label='Stage-1 (Discovery)', color='#2E86AB', edgecolor='black', linewidth=0.5)
    bars2 = ax.bar(x, stage2_lat, width*1.5, bottom=stage1_lat, label='Stage-2 (Fetch)', color='#28A745', edgecolor='black', linewidth=0.5)
    
    ax.set_ylabel('Latency (ms)')
    ax.set_xticks(x)
    ax.set_xticklabels(methods, rotation=15, ha='right')
    ax.legend(loc='upper right')
    ax.set_title('(c) End-to-End Latency')
    
    # Add total labels
    for i, (s1, s2) in enumerate(zip(stage1_lat, stage2_lat)):
        ax.annotate(f'{s1+s2:.1f}', xy=(i, s1+s2), xytext=(0, 3),
                   textcoords="offset points", ha='center', va='bottom', fontsize=9, fontweight='bold')
    
    # (d) Control Plane Overhead
    ax = axes[1, 1]
    overhead_file = os.path.join(exp2_dir, 'exp2_overhead.csv')
    if os.path.exists(overhead_file):
        df_oh = pd.read_csv(overhead_file)
        df_oh['method'] = pd.Categorical(df_oh['method'], categories=method_order, ordered=True)
        df_oh = df_oh.sort_values('method')
        
        lsa_kb = df_oh['lsa_bytes'].values / 1000
        index_mb = df_oh['index_bytes'].values / 1e6
        
        # Log scale for comparison
        overhead_values = []
        labels_oh = []
        for i, m in enumerate(df_oh['method']):
            if lsa_kb[i] > 0:
                overhead_values.append(lsa_kb[i])
                labels_oh.append(f'{m}\n(LSA)')
            elif index_mb[i] > 0:
                overhead_values.append(index_mb[i] * 1000)  # Convert to KB for comparison
                labels_oh.append(f'{m}\n(Index)')
            else:
                overhead_values.append(0.001)  # Small value for log scale
                labels_oh.append(f'{m}\n(None)')
        
        colors_oh = [COLORS.get(m, '#666666') for m in df_oh['method']]
        bars = ax.bar(range(len(overhead_values)), overhead_values, color=colors_oh, edgecolor='black', linewidth=0.5)
        ax.set_yscale('log')
        ax.set_ylabel('Control Plane Overhead (KB, log scale)')
        ax.set_xticks(range(len(labels_oh)))
        ax.set_xticklabels(labels_oh, rotation=0, ha='center')
        ax.set_title('(d) Control Plane Overhead')
        
        # Add value labels
        for bar, val in zip(bars, overhead_values):
            if val > 1:
                height = bar.get_height()
                label = f'{val:.1f}KB' if val < 1000 else f'{val/1000:.1f}MB'
                ax.annotate(label, xy=(bar.get_x() + bar.get_width()/2, height),
                           xytext=(0, 3), textcoords="offset points", ha='center', va='bottom', fontsize=8)
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_exp2_baselines.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_exp2_baselines.png'))
    plt.close()
    print(f"Saved: fig_exp2_baselines.pdf/png")


# =============================================================================
# Figure 2: Tau Sweep (Accuracy-Overhead Tradeoff)
# =============================================================================
def plot_tau_sweep(exp9_dir, out_dir):
    """Plot tau parameter sweep showing accuracy-overhead tradeoff"""
    tau_file = os.path.join(exp9_dir, 'exp9_tau_sweep.csv')
    if not os.path.exists(tau_file):
        print(f"Warning: {tau_file} not found, skipping tau sweep plot")
        return
    
    df = pd.read_csv(tau_file)
    df = df[df['tau'] <= 0.6]  # Filter out zero-accuracy points
    
    fig, ax1 = plt.subplots(figsize=(8, 5))
    
    # Primary axis: Accuracy
    ax1.plot(df['tau'], df['domainAccuracy'] * 100, 'o-', color='#2E86AB', 
             markersize=8, linewidth=2, label='Domain Accuracy')
    ax1.plot(df['tau'], df['docAccuracy'] * 100, 's--', color='#A23B72',
             markersize=8, linewidth=2, label='Document Accuracy')
    ax1.set_xlabel('Threshold τ')
    ax1.set_ylabel('Accuracy (%)', color='#2E86AB')
    ax1.tick_params(axis='y', labelcolor='#2E86AB')
    ax1.set_ylim(0, 110)
    ax1.set_xlim(-0.05, 0.65)
    
    # Secondary axis: Probes
    ax2 = ax1.twinx()
    ax2.plot(df['tau'], df['avgProbes'], '^-', color='#E94F37',
             markersize=8, linewidth=2, label='Avg Probes')
    ax2.set_ylabel('Average Probes', color='#E94F37')
    ax2.tick_params(axis='y', labelcolor='#E94F37')
    ax2.set_ylim(0, max(df['avgProbes']) * 1.2)
    
    # Highlight sweet spot
    sweet_spot_idx = 2  # tau=0.20
    if len(df) > sweet_spot_idx:
        sweet_tau = df.iloc[sweet_spot_idx]['tau']
        ax1.axvline(x=sweet_tau, color='#28A745', linestyle=':', linewidth=2, alpha=0.7)
        ax1.annotate('Sweet spot\n(τ=0.20)', xy=(sweet_tau, 75), fontsize=10,
                    ha='center', color='#28A745', fontweight='bold')
    
    # Combined legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='center right')
    
    ax1.set_title('Accuracy-Overhead Tradeoff: τ Threshold Sweep')
    ax1.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_tau_sweep.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_tau_sweep.png'))
    plt.close()
    print(f"Saved: fig_tau_sweep.pdf/png")


# =============================================================================
# Figure 3: Hit@K Analysis
# =============================================================================
def plot_hitk(exp9_dir, out_dir):
    """Plot Hit@K analysis"""
    hitk_file = os.path.join(exp9_dir, 'exp9_hitk.csv')
    if not os.path.exists(hitk_file):
        print(f"Warning: {hitk_file} not found, skipping Hit@K plot")
        return
    
    df = pd.read_csv(hitk_file)
    
    fig, ax = plt.subplots(figsize=(7, 5))
    
    x = df['K'].values
    domain_hit = df['domainHitRate'].values * 100
    doc_hit = df['docHitRate'].values * 100
    
    ax.bar(x - 0.2, domain_hit, 0.4, label='Domain Hit@K', color='#2E86AB', edgecolor='black', linewidth=0.5)
    ax.bar(x + 0.2, doc_hit, 0.4, label='Document Hit@K', color='#A23B72', edgecolor='black', linewidth=0.5)
    
    ax.set_xlabel('K (Top-K Probes)')
    ax.set_ylabel('Hit Rate (%)')
    ax.set_xticks(x)
    ax.set_ylim(0, 110)
    ax.legend(loc='lower right')
    ax.set_title('Hit@K: Cumulative Success Rate')
    
    # Annotate K=1 vs K=2 improvement
    if len(domain_hit) >= 2:
        improvement = domain_hit[1] - domain_hit[0]
        ax.annotate(f'+{improvement:.1f}%', xy=(1.5, domain_hit[1] + 2),
                   fontsize=10, ha='center', color='#28A745', fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_hitk.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_hitk.png'))
    plt.close()
    print(f"Saved: fig_hitk.pdf/png")


# =============================================================================
# Figure 4: Alpha-Beta Sweep (Semantic vs Cost)
# =============================================================================
def plot_ab_sweep(exp9_dir, out_dir):
    """Plot alpha-beta parameter sweep"""
    ab_file = os.path.join(exp9_dir, 'exp9_ab_sweep.csv')
    if not os.path.exists(ab_file):
        print(f"Warning: {ab_file} not found, skipping alpha-beta sweep plot")
        return
    
    df = pd.read_csv(ab_file)
    
    fig, ax1 = plt.subplots(figsize=(8, 5))
    
    ax1.plot(df['alpha'], df['domainAccuracy'] * 100, 'o-', color='#2E86AB',
             markersize=8, linewidth=2, label='Domain Accuracy')
    ax1.plot(df['alpha'], df['docAccuracy'] * 100, 's--', color='#A23B72',
             markersize=8, linewidth=2, label='Document Accuracy')
    
    ax1.set_xlabel('α (Semantic Weight) | β = 1 - α (Cost Weight)')
    ax1.set_ylabel('Accuracy (%)')
    ax1.set_ylim(0, 110)
    ax1.set_xlim(-0.05, 1.05)
    
    # Add secondary x-axis for beta
    ax_top = ax1.secondary_xaxis('top')
    ax_top.set_xticks(df['alpha'].values)
    ax_top.set_xticklabels([f'{1-a:.1f}' for a in df['alpha'].values])
    ax_top.set_xlabel('β (Cost Weight)')
    
    # Highlight regions
    ax1.axvspan(0.6, 1.0, alpha=0.1, color='#2E86AB', label='Semantic-dominant')
    ax1.axvspan(0.0, 0.2, alpha=0.1, color='#E94F37', label='Cost-dominant')
    
    ax1.legend(loc='lower left')
    ax1.set_title('Semantic (α) vs Cost (β) Weight Tradeoff')
    ax1.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_ab_sweep.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_ab_sweep.png'))
    plt.close()
    print(f"Saved: fig_ab_sweep.pdf/png")


# =============================================================================
# Figure 5: Combined Summary (Radar Chart)
# =============================================================================
def plot_radar_comparison(exp2_dir, out_dir):
    """Plot radar chart comparing all methods across multiple metrics"""
    summary_file = os.path.join(exp2_dir, 'exp2_summary.csv')
    if not os.path.exists(summary_file):
        print(f"Warning: {summary_file} not found, skipping radar plot")
        return
    
    df = pd.read_csv(summary_file)
    
    # Normalize metrics (higher is better for all)
    metrics = ['Domain Acc', 'Doc Acc', 'Bandwidth Eff', 'Latency Eff', 'Probe Eff']
    
    method_order = ['iRoute', 'Flood-Parallel', 'Flood-Sequential', 'Centralized']
    df['method'] = pd.Categorical(df['method'], categories=method_order, ordered=True)
    df = df.sort_values('method')
    
    # Calculate normalized scores (0-1, higher is better)
    values = {}
    for _, row in df.iterrows():
        method = row['method']
        # Accuracy (already 0-1)
        domain_acc = row['domain_accuracy']
        doc_acc = row['doc_accuracy']
        # Efficiency (inverse, normalized)
        max_bytes = df['avg_total_bytes'].max()
        max_latency = df['avg_total_latency_ms'].max()
        max_probes = df['avg_probes'].max()
        
        bandwidth_eff = 1 - (row['avg_total_bytes'] / max_bytes)
        latency_eff = 1 - (row['avg_total_latency_ms'] / max_latency)
        probe_eff = 1 - (row['avg_probes'] / max_probes)
        
        values[method] = [domain_acc, doc_acc, bandwidth_eff, latency_eff, probe_eff]
    
    # Radar chart
    angles = np.linspace(0, 2 * np.pi, len(metrics), endpoint=False).tolist()
    angles += angles[:1]  # Complete the loop
    
    fig, ax = plt.subplots(figsize=(8, 8), subplot_kw=dict(polar=True))
    
    for method in method_order:
        if method in values:
            vals = values[method] + values[method][:1]
            ax.plot(angles, vals, 'o-', linewidth=2, label=method, 
                   color=COLORS.get(method, '#666666'), markersize=6)
            ax.fill(angles, vals, alpha=0.1, color=COLORS.get(method, '#666666'))
    
    ax.set_xticks(angles[:-1])
    ax.set_xticklabels(metrics, fontsize=11)
    ax.set_ylim(0, 1.1)
    ax.set_yticks([0.25, 0.5, 0.75, 1.0])
    ax.set_yticklabels(['25%', '50%', '75%', '100%'], fontsize=9)
    ax.legend(loc='upper right', bbox_to_anchor=(1.3, 1.1))
    ax.set_title('Multi-Metric Comparison (Rocketfuel Topology)', pad=20)
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_radar.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_radar.png'))
    plt.close()
    print(f"Saved: fig_radar.pdf/png")


# =============================================================================
# Figure 6: Topology Visualization
# =============================================================================
def plot_topology_stats(exp2_dir, out_dir):
    """Plot topology statistics"""
    topo_file = os.path.join(exp2_dir, 'exp2_topology.csv')
    if not os.path.exists(topo_file):
        print(f"Warning: {topo_file} not found, skipping topology plot")
        return
    
    df = pd.read_csv(topo_file)
    
    # Create info box figure
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.axis('off')
    
    # Extract values
    stats = {row['metric']: row['value'] for _, row in df.iterrows()}
    
    text = f"""
    ┌─────────────────────────────────────────┐
    │      Rocketfuel Topology (AS1239)       │
    │               Sprint ISP                │
    ├─────────────────────────────────────────┤
    │  Nodes:              {int(stats.get('nodes', 0)):>6}            │
    │  Links:              {int(stats.get('links', 0)):>6}            │
    │  Link Delay:         {stats.get('link_delay_ms', 0):>6.1f} ms        │
    │  Avg Hops to Domain: {stats.get('avg_hops_to_domain', 0):>6.1f}           │
    ├─────────────────────────────────────────┤
    │  Consumer Node:      {int(stats.get('consumer_node', 0)):>6}            │
    │  Search Server Node: {int(stats.get('search_server_node', 0)):>6}            │
    └─────────────────────────────────────────┘
    """
    
    ax.text(0.5, 0.5, text, transform=ax.transAxes, fontsize=12,
           fontfamily='monospace', verticalalignment='center',
           horizontalalignment='center',
           bbox=dict(boxstyle='round', facecolor='#f0f0f0', alpha=0.8))
    
    ax.set_title('Rocketfuel Topology Configuration', fontsize=14, fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_topology_info.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_topology_info.png'))
    plt.close()
    print(f"Saved: fig_topology_info.pdf/png")


# =============================================================================
# Figure 7: LSA Overhead Breakdown
# =============================================================================
def plot_lsa_overhead(exp9_dir, out_dir):
    """Plot LSA overhead breakdown"""
    overhead_file = os.path.join(exp9_dir, 'exp9_overhead.csv')
    if not os.path.exists(overhead_file):
        print(f"Warning: {overhead_file} not found, skipping LSA overhead plot")
        return
    
    df = pd.read_csv(overhead_file)
    
    if len(df) == 0:
        return
    
    row = df.iloc[0]
    
    # Pie chart for LSA breakdown
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    
    # (a) LSA component breakdown
    ax = axes[0]
    components = ['Header', 'Centroid List', 'Vectors', 'Metadata']
    sizes = [
        row['lsaHeaderBytes'],
        row['centroidListBytes'] - row['vectorBytes'] - row['metadataBytes'],
        row['vectorBytes'] * row['M'],  # M centroids
        row['metadataBytes'] * row['M']
    ]
    sizes = [max(0, s) for s in sizes]  # Ensure non-negative
    
    colors = ['#2E86AB', '#A23B72', '#F18F01', '#28A745']
    explode = (0, 0, 0.05, 0)
    
    wedges, texts, autotexts = ax.pie(sizes, explode=explode, labels=components, colors=colors,
                                      autopct='%1.1f%%', shadow=False, startangle=90)
    ax.set_title(f'(a) LSA Packet Breakdown\n(Total: {row["totalLsaBytes"]} bytes/domain)')
    
    # (b) Scalability info
    ax = axes[1]
    ax.axis('off')
    
    info_text = f"""
    LSA Overhead Analysis
    ─────────────────────────────────────
    Configuration:
      • Domains: {int(row['domains'])}
      • Centroids per domain (M): {int(row['M'])}
      • Vector dimension: {int(row['vectorDim'])}
    
    Per-Domain LSA:
      • Header: {int(row['lsaHeaderBytes'])} bytes
      • Centroid list: {int(row['centroidListBytes'])} bytes
      • Total: {int(row['totalLsaBytes'])} bytes
    
    Network-Wide:
      • Total broadcast: {int(row['totalBroadcastBytes'])} bytes
      • LSDB entries: {int(row['lsdbEntries'])}
      • RIB entries: {int(row['ribEntries'])}
    
    Scaling:
      • O(D × M × d) where D=domains, M=centroids, d=dim
      • Linear in each parameter
    """
    
    ax.text(0.1, 0.5, info_text, transform=ax.transAxes, fontsize=11,
           fontfamily='monospace', verticalalignment='center',
           bbox=dict(boxstyle='round', facecolor='#f8f8f8', alpha=0.9))
    ax.set_title('(b) Overhead Analysis', fontsize=12)
    
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, 'fig_lsa_overhead.pdf'))
    plt.savefig(os.path.join(out_dir, 'fig_lsa_overhead.png'))
    plt.close()
    print(f"Saved: fig_lsa_overhead.pdf/png")


# =============================================================================
# Main
# =============================================================================
def main():
    parser = argparse.ArgumentParser(description='Generate publication-quality figures for iRoute experiments')
    parser.add_argument('--exp9Dir', type=str, default='results/exp9_comprehensive',
                       help='Directory containing Exp9 (comprehensive) results')
    parser.add_argument('--exp2Dir', type=str, default='results/exp2_rocketfuel',
                       help='Directory containing Exp2 (baseline comparison) results')
    parser.add_argument('--outDir', type=str, default='figures',
                       help='Output directory for figures')
    args = parser.parse_args()
    
    ensure_dir(args.outDir)
    
    print("=" * 60)
    print("iRoute Experiment Visualization")
    print("=" * 60)
    print(f"Exp9 dir: {args.exp9Dir}")
    print(f"Exp2 dir: {args.exp2Dir}")
    print(f"Output dir: {args.outDir}")
    print()
    
    # Generate all figures
    print("Generating figures...")
    
    # Exp2: Baseline comparison
    plot_exp2_baselines(args.exp2Dir, args.outDir)
    plot_radar_comparison(args.exp2Dir, args.outDir)
    plot_topology_stats(args.exp2Dir, args.outDir)
    
    # Exp9: Comprehensive analysis
    plot_tau_sweep(args.exp9Dir, args.outDir)
    plot_hitk(args.exp9Dir, args.outDir)
    plot_ab_sweep(args.exp9Dir, args.outDir)
    plot_lsa_overhead(args.exp9Dir, args.outDir)
    
    print()
    print("=" * 60)
    print(f"All figures saved to: {args.outDir}")
    print("=" * 60)


if __name__ == '__main__':
    main()
