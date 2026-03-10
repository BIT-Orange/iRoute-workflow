#!/usr/bin/env python3
"""
Plot accuracy vs vector dimension curves for iRoute experiments.

This script generates publication-quality figures showing:
1. Domain routing accuracy vs vector dimension
2. MTU compliance annotations
3. Combined 2019+2020 results

Usage:
    python3 plot_dim_sweep.py [--resultsDir ./results/exp_dim_sweep]
"""

import argparse
import csv
import os
from collections import defaultdict

# Try to import matplotlib, provide helpful error if missing
try:
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
except ImportError:
    print("ERROR: matplotlib not installed. Run: pip install matplotlib")
    import sys
    sys.exit(1)

try:
    import numpy as np
except ImportError:
    print("ERROR: numpy not installed. Run: pip install numpy")
    import sys
    sys.exit(1)


def calc_wire_size(dim: int) -> int:
    """Calculate estimated Interest wire size for given dimension."""
    # SemanticVector: TLV header (~10 bytes) + dim * 4 (float32)
    vec_size = 10 + dim * 4
    # Interest: Name (~100 bytes) + ApplicationParameters (~20 bytes) + vec
    interest_size = 100 + 20 + vec_size
    return interest_size


def load_results(results_file: str) -> dict:
    """Load dimension sweep results from CSV."""
    results = defaultdict(list)
    
    with open(results_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            split = row['split']
            dim = int(row['vectorDim'])
            domain_acc = float(row['domainAccuracy'].rstrip('%'))
            doc_acc = float(row['docAccuracy'].rstrip('%'))
            queries = int(row['queries'])
            domain_correct = int(row['domainCorrect'])
            
            results[split].append({
                'dim': dim,
                'domain_acc': domain_acc,
                'doc_acc': doc_acc,
                'queries': queries,
                'domain_correct': domain_correct,
            })
    
    # Sort by dimension
    for split in results:
        results[split].sort(key=lambda x: x['dim'])
    
    return dict(results)


def plot_accuracy_vs_dim(results: dict, output_path: str):
    """Generate accuracy vs dimension plot."""
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
    
    # Color scheme
    colors = {
        'trec-dl-2019': '#2196F3',  # Blue
        'trec-dl-2020': '#4CAF50',  # Green
    }
    markers = {
        'trec-dl-2019': 'o',
        'trec-dl-2020': 's',
    }
    
    # MTU threshold
    MTU_THRESHOLD = 1500
    
    # =========================================================================
    # Left plot: Accuracy vs Dimension
    # =========================================================================
    for split, data in results.items():
        dims = [d['dim'] for d in data]
        accs = [d['domain_acc'] for d in data]
        
        ax1.plot(dims, accs, marker=markers[split], color=colors[split],
                 linewidth=2, markersize=8, label=split.upper())
    
    # Add MTU compliance shading
    ax1.axvspan(0, 256, alpha=0.1, color='green', label='MTU-compliant')
    ax1.axvspan(256, 400, alpha=0.1, color='red', label='Exceeds MTU')
    ax1.axvline(x=256, color='orange', linestyle='--', linewidth=1.5, label='Max safe dim')
    
    ax1.set_xlabel('Vector Dimension', fontsize=12)
    ax1.set_ylabel('Domain Routing Accuracy (%)', fontsize=12)
    ax1.set_title('Routing Accuracy vs Vector Dimension', fontsize=14)
    ax1.set_xlim(50, 270)
    ax1.set_ylim(80, 100)
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc='lower right')
    
    # =========================================================================
    # Right plot: Wire Size Analysis
    # =========================================================================
    dims_all = list(range(32, 400, 16))
    wire_sizes = [calc_wire_size(d) for d in dims_all]
    
    ax2.plot(dims_all, wire_sizes, 'b-', linewidth=2, label='Interest size')
    ax2.axhline(y=MTU_THRESHOLD, color='red', linestyle='--', linewidth=2, label='MTU (1500B)')
    ax2.fill_between(dims_all, 0, wire_sizes, 
                     where=[w <= MTU_THRESHOLD for w in wire_sizes],
                     alpha=0.3, color='green', label='Safe zone')
    ax2.fill_between(dims_all, 0, wire_sizes,
                     where=[w > MTU_THRESHOLD for w in wire_sizes],
                     alpha=0.3, color='red', label='Exceeds MTU')
    
    # Mark key dimensions
    key_dims = [64, 128, 192, 256, 384]
    for d in key_dims:
        ws = calc_wire_size(d)
        color = 'green' if ws <= MTU_THRESHOLD else 'red'
        ax2.scatter([d], [ws], color=color, s=100, zorder=5)
        ax2.annotate(f'{d}D\n{ws}B', (d, ws), textcoords="offset points",
                     xytext=(0, 10), ha='center', fontsize=9)
    
    ax2.set_xlabel('Vector Dimension', fontsize=12)
    ax2.set_ylabel('Estimated Interest Size (bytes)', fontsize=12)
    ax2.set_title('MTU Compliance Analysis', fontsize=14)
    ax2.set_xlim(0, 400)
    ax2.set_ylim(0, 2000)
    ax2.grid(True, alpha=0.3)
    ax2.legend(loc='upper left')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved: {output_path}")
    plt.close()


def plot_combined_accuracy(results: dict, output_path: str):
    """Generate combined accuracy plot with confidence intervals."""
    
    fig, ax = plt.subplots(figsize=(8, 6))
    
    # Combine results across splits at each dimension
    combined = defaultdict(lambda: {'correct': 0, 'total': 0})
    
    for split, data in results.items():
        for d in data:
            combined[d['dim']]['correct'] += d['domain_correct']
            combined[d['dim']]['total'] += d['queries']
    
    dims = sorted(combined.keys())
    accs = []
    ci_lower = []
    ci_upper = []
    
    for dim in dims:
        c = combined[dim]
        p = c['correct'] / c['total'] if c['total'] > 0 else 0
        accs.append(p * 100)
        
        # Wilson confidence interval (95%)
        n = c['total']
        z = 1.96
        if n > 0:
            denom = 1 + z**2 / n
            center = (p + z**2 / (2*n)) / denom
            spread = z * np.sqrt((p * (1-p) + z**2 / (4*n)) / n) / denom
            ci_lower.append(max(0, (center - spread)) * 100)
            ci_upper.append(min(1, (center + spread)) * 100)
        else:
            ci_lower.append(0)
            ci_upper.append(0)
    
    # Plot with error bars
    ax.errorbar(dims, accs, yerr=[np.array(accs) - np.array(ci_lower), 
                                   np.array(ci_upper) - np.array(accs)],
                marker='o', markersize=10, linewidth=2, capsize=5,
                color='#673AB7', label='Combined TREC DL 2019+2020')
    
    # Annotations
    for i, dim in enumerate(dims):
        ax.annotate(f'{accs[i]:.1f}%', (dim, accs[i]), 
                    textcoords="offset points", xytext=(0, 15), ha='center',
                    fontsize=10, fontweight='bold')
    
    # MTU indicator
    ax.axvline(x=256, color='orange', linestyle='--', linewidth=1.5, 
               label='Max MTU-compliant dim (256)')
    
    ax.set_xlabel('Vector Dimension', fontsize=12)
    ax.set_ylabel('Domain Routing Accuracy (%)', fontsize=12)
    ax.set_title('iRoute Routing Accuracy vs Vector Dimension\n(Combined TREC DL 2019+2020)', 
                 fontsize=14)
    ax.set_xlim(50, 270)
    ax.set_ylim(85, 100)
    ax.grid(True, alpha=0.3)
    ax.legend(loc='lower right')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved: {output_path}")
    plt.close()


def generate_latex_table(results: dict, output_path: str):
    """Generate LaTeX table for paper."""
    
    lines = []
    lines.append(r"\begin{table}[t]")
    lines.append(r"\centering")
    lines.append(r"\caption{Routing Accuracy vs Vector Dimension}")
    lines.append(r"\label{tab:dim-sweep}")
    lines.append(r"\begin{tabular}{ccccc}")
    lines.append(r"\toprule")
    lines.append(r"Dim & Wire Size & TREC-DL-2019 & TREC-DL-2020 & Combined \\")
    lines.append(r"\midrule")
    
    # Collect all dims
    all_dims = set()
    for data in results.values():
        for d in data:
            all_dims.add(d['dim'])
    
    for dim in sorted(all_dims):
        wire_size = calc_wire_size(dim)
        mtu_ok = "\\checkmark" if wire_size <= 1500 else "\\texttimes"
        
        acc_2019 = next((d['domain_acc'] for d in results.get('trec-dl-2019', []) 
                         if d['dim'] == dim), None)
        acc_2020 = next((d['domain_acc'] for d in results.get('trec-dl-2020', []) 
                         if d['dim'] == dim), None)
        
        # Combined
        total_correct = 0
        total_queries = 0
        for split in results:
            for d in results[split]:
                if d['dim'] == dim:
                    total_correct += d['domain_correct']
                    total_queries += d['queries']
        combined_acc = (total_correct / total_queries * 100) if total_queries > 0 else 0
        
        line = f"{dim} & {wire_size}B {mtu_ok} & "
        line += f"{acc_2019:.1f}\\% & " if acc_2019 else "-- & "
        line += f"{acc_2020:.1f}\\% & " if acc_2020 else "-- & "
        line += f"{combined_acc:.1f}\\% \\\\"
        lines.append(line)
    
    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    lines.append(r"\end{table}")
    
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"Saved: {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Plot dimension sweep results')
    parser.add_argument('--resultsDir', type=str, default='results/exp_dim_sweep',
                        help='Results directory')
    args = parser.parse_args()
    
    results_file = os.path.join(args.resultsDir, 'dim_sweep_results.csv')
    
    if not os.path.exists(results_file):
        print(f"ERROR: Results file not found: {results_file}")
        print("Run the dimension sweep experiments first:")
        print("  ./run_exp_dim_sweep.sh")
        return 1
    
    # Load results
    results = load_results(results_file)
    print(f"Loaded results for: {list(results.keys())}")
    
    # Generate plots
    plot_accuracy_vs_dim(results, os.path.join(args.resultsDir, 'accuracy_vs_dim.png'))
    plot_combined_accuracy(results, os.path.join(args.resultsDir, 'accuracy_combined.png'))
    
    # Generate LaTeX table
    generate_latex_table(results, os.path.join(args.resultsDir, 'table_dim_sweep.tex'))
    
    # Summary statistics
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    
    print("\nMTU Compliance:")
    for dim in [64, 128, 192, 256, 384]:
        ws = calc_wire_size(dim)
        status = "✓ OK" if ws <= 1500 else "✗ EXCEEDS"
        print(f"  dim={dim:3d}: {ws:4d} bytes  {status}")
    
    print("\nRecommendation:")
    print("  Use dim=192 or dim=256 for best accuracy/MTU tradeoff")
    print("  dim=384 (original all-MiniLM-L6-v2) exceeds MTU!")
    
    return 0


if __name__ == '__main__':
    exit(main())
