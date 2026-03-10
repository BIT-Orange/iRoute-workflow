#!/usr/bin/env python3
import csv
import os
from collections import defaultdict
import math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
RESULTS = os.path.join(ROOT, 'results')
FIGS = os.path.join(ROOT, 'figures')
os.makedirs(FIGS, exist_ok=True)

BLUE = '#1f77b4'
ORANGE = '#ff7f0e'
RED = '#d62728'

plt.rcParams.update({
    'font.size': 9,
    'axes.labelsize': 9,
    'axes.titlesize': 9,
    'legend.fontsize': 8,
    'xtick.labelsize': 8,
    'ytick.labelsize': 8,
    'lines.linewidth': 1.5,
    'lines.markersize': 4,
    'axes.linewidth': 0.8,
})


def read_csv(path):
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


def write_csv(path, fieldnames, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)


def f(x, default=0.0):
    try:
        return float(x)
    except Exception:
        return default


def i(x, default=0):
    try:
        return int(float(x))
    except Exception:
        return default


# -----------------------------------------------------------------------------
# Exp1: M sweep (Hit@1/Hit@3)
# -----------------------------------------------------------------------------
M_VALUES = [1, 2, 4, 8, 16, 32]
exp1_rows = []
for M in M_VALUES:
    row = {'M': M}
    for K, label in [(1, 'hit1'), (3, 'hit3')]:
        path = os.path.join(RESULTS, 'eval', f'exp1_sweep_M{M}_k{K}', 'exp1_summary.csv')
        data = read_csv(path)
        if not data:
            row[label] = 0.0
        else:
            row[label] = f(data[0].get('domainAccuracy', 0.0))
    exp1_rows.append(row)

exp1_csv = os.path.join(RESULTS, 'eval', 'exp1_hitk.csv')
write_csv(exp1_csv, ['M', 'hit1', 'hit3'], exp1_rows)

# Flooding baseline for rate-distortion
exp2_sum_path = os.path.join(RESULTS, 'eval', 'exp2_baselines_oracle', 'exp2_summary.csv')
exp2_sum = read_csv(exp2_sum_path)
flood_acc = None
for r in exp2_sum:
    if r.get('method') == 'Flood-Parallel':
        flood_acc = f(r.get('domain_accuracy'))
        break
if flood_acc is None:
    flood_acc = 1.0

# -----------------------------------------------------------------------------
# Exp2 baseline comparison
# -----------------------------------------------------------------------------
base_rows = []
for method in ['iRoute', 'Flood-Parallel', 'Centralized']:
    for r in exp2_sum:
        if r.get('method') == method:
            base_rows.append({'method': method, 'success_rate': f(r.get('domain_accuracy'))})
            break

base_csv = os.path.join(RESULTS, 'eval', 'exp2_baselines_plot.csv')
write_csv(base_csv, ['method', 'success_rate'], base_rows)

# -----------------------------------------------------------------------------
# Exp2.2 tau sweep (from exp2.2-single runs)
# -----------------------------------------------------------------------------
tau_path = os.path.join(RESULTS, 'eval', 'exp2_2_single', 'exp2.2_sweep.csv')
tau_rows = read_csv(tau_path)
# ensure sorted by tau
for r in tau_rows:
    r['tau_val'] = f(r.get('tau'))

tau_rows = sorted(tau_rows, key=lambda r: r['tau_val'])

tau_csv = os.path.join(RESULTS, 'eval', 'exp2_tau_sweep_plot.csv')
write_csv(tau_csv, ['tau', 'domain_acc', 'avg_probes'],
          [{'tau': r['tau_val'], 'domain_acc': f(r.get('domain_acc')),
            'avg_probes': f(r.get('avg_probes'))} for r in tau_rows])

# -----------------------------------------------------------------------------
# Exp2.1 packet-level latency + overhead
# -----------------------------------------------------------------------------
methods = ['iroute', 'flooding', 'centralized']
label_map = {'iroute': 'iRoute', 'flooding': 'Flooding', 'centralized': 'Centralized'}
lat_rows = []
over_rows = []
for m in methods:
    path = os.path.join(RESULTS, 'eval', 'exp2_1_packet', f'exp2.1_comparison_{m}.csv')
    data = read_csv(path)
    if not data:
        continue
    r = data[0]
    mean_ms = f(r.get('mean_ms'))
    p95_ms = f(r.get('p95_ms'))
    q = i(r.get('queries'), 1)
    ctrl_bytes = f(r.get('ctrl_bytes'))
    lat_rows.append({'method': label_map[m], 'mean_ms': mean_ms, 'p95_ms': p95_ms})
    over_rows.append({'method': label_map[m], 'bytes_per_query': ctrl_bytes / max(q, 1)})

lat_csv = os.path.join(RESULTS, 'eval', 'exp2_packet_latency.csv')
write_csv(lat_csv, ['method', 'mean_ms', 'p95_ms'], lat_rows)

ov_csv = os.path.join(RESULTS, 'eval', 'exp2_probe_overhead.csv')
write_csv(ov_csv, ['method', 'bytes_per_query'], over_rows)

# -----------------------------------------------------------------------------
# Exp5 compute overhead
# -----------------------------------------------------------------------------
exp5_path = os.path.join(RESULTS, 'eval', 'exp5', 'exp5_throughput.csv')
exp5_rows = read_csv(exp5_path)
compute_rows = []
linear = next((r for r in exp5_rows if r.get('indexType') == 'linear' and r.get('mode') == 'real'), None)
ann = next((r for r in exp5_rows if r.get('indexType') == 'hnsw' and r.get('mode') == 'real'), None)
if linear:
    compute_rows.append({'method': 'Centralized', 'ops_per_query': f(linear.get('avgDotProducts'))})
if ann:
    # Use ANN ops as proxy for similarity work in iRoute
    compute_rows.append({'method': 'iRoute', 'ops_per_query': f(ann.get('avgAnnOps'))})

compute_csv = os.path.join(RESULTS, 'eval', 'exp_compute.csv')
write_csv(compute_csv, ['method', 'ops_per_query'], compute_rows)

# -----------------------------------------------------------------------------
# Exp-scalability: state + traffic
# -----------------------------------------------------------------------------
scal_dir = os.path.join(RESULTS, 'eval', 'exp_scalability')
scal_domains = [10, 20, 40, 80]
M_CONST = 4
PREFIXES = 50

state_rows = []
for d in scal_domains:
    state_rows.append({'domains': d, 'protocol': 'iRoute', 'entries': d * M_CONST})
    state_rows.append({'domains': d, 'protocol': 'NLSR', 'entries': d * PREFIXES})

state_csv = os.path.join(RESULTS, 'eval', 'exp_scaling_state.csv')
write_csv(state_csv, ['domains', 'protocol', 'entries'], state_rows)

# traffic bytes/sec from trace files

def parse_trace_bytes_per_sec(trace_file, sim_time=60.0):
    total_kb = 0.0
    with open(trace_file, newline='') as fh:
        r = csv.DictReader(fh, delimiter='\t')
        for row in r:
            t = row.get('Type')
            if t not in ('OutInterests', 'OutData'):
                continue
            total_kb += f(row.get('KilobytesRaw'))
    total_bytes = total_kb * 1024.0
    return total_bytes / sim_time

traffic_rows = []
for d in scal_domains:
    for proto in ['iroute', 'nlsr']:
        trace = os.path.join(scal_dir, f'trace_{proto}_{d}.txt')
        bps = parse_trace_bytes_per_sec(trace)
        traffic_rows.append({'domains': d, 'protocol': proto.upper(), 'bytes_per_sec': bps})

traffic_csv = os.path.join(RESULTS, 'eval', 'exp_scaling_traffic.csv')
write_csv(traffic_csv, ['domains', 'protocol', 'bytes_per_sec'], traffic_rows)

# -----------------------------------------------------------------------------
# Exp10 churn (reconvergence time)
# -----------------------------------------------------------------------------
exp10_path = os.path.join(RESULTS, 'eval', 'exp10', 'exp10_summary.csv')
exp10_rows = read_csv(exp10_path)
churn_rows = []
if exp10_rows:
    last = exp10_rows[-1]
    churn_rows.append({'protocol': 'iRoute', 'reconv_ms': f(last.get('irouteConvMs'))})
    churn_rows.append({'protocol': 'NLSR', 'reconv_ms': f(last.get('nlsrConvMs'))})

churn_csv = os.path.join(RESULTS, 'eval', 'exp_churn.csv')
write_csv(churn_csv, ['protocol', 'reconv_ms'], churn_rows)

# -----------------------------------------------------------------------------
# Failover: success rate over time
# -----------------------------------------------------------------------------
fail_iroute = os.path.join(RESULTS, 'convergence_iroute_queries.csv')
fail_nlsr = os.path.join(RESULTS, 'convergence_nlsr_queries.csv')

# iRoute
ir_rows = read_csv(fail_iroute)
# bin size 1s up to 40s
bin_size = 1.0
max_t = 40
bins = {i: {'tot':0, 'ok':0} for i in range(max_t)}
for r in ir_rows:
    t = f(r.get('startTime'))
    b = int(t // bin_size)
    if b < 0 or b >= max_t:
        continue
    bins[b]['tot'] += 1
    ok = i(r.get('stage2Success'))
    bins[b]['ok'] += 1 if ok else 0

# nlsr (ExactNameConsumer): infer time from queryId (10ms gap)
ns_rows = read_csv(fail_nlsr)
for r in ns_rows:
    qid = i(r.get('queryId'))
    t = qid * 0.01
    b = int(t // bin_size)
    if b < 0 or b >= max_t:
        continue
    bins.setdefault(b, {'tot':0, 'ok':0})
    bins[b]['tot'] += 1
    ok = i(r.get('success'))
    bins[b]['ok'] += 1 if ok else 0

fail_rows = []
for b in range(max_t):
    t = b + 0.5
    ir_tot = bins[b]['tot'] if b in bins else 0
    # To separate iroute and nlsr, recompute from raw rows

# recompute separately for clarity

def series_from_rows(rows, time_fn):
    out = {i: {'tot':0, 'ok':0} for i in range(max_t)}
    for r in rows:
        t = time_fn(r)
        b = int(t // bin_size)
        if b < 0 or b >= max_t:
            continue
        out[b]['tot'] += 1
        out[b]['ok'] += 1 if i(r.get('stage2Success', r.get('success', 0))) else 0
    return out

ir_bins = series_from_rows(ir_rows, lambda r: f(r.get('startTime')))
ns_bins = series_from_rows(ns_rows, lambda r: i(r.get('queryId')) * 0.01)

for b in range(max_t):
    t = b + 0.5
    ir_tot = ir_bins[b]['tot']
    ir_ok = ir_bins[b]['ok']
    ns_tot = ns_bins[b]['tot']
    ns_ok = ns_bins[b]['ok']
    ir_rate = (ir_ok / ir_tot) if ir_tot > 0 else 0.0
    ns_rate = (ns_ok / ns_tot) if ns_tot > 0 else 0.0
    fail_rows.append({'time': t, 'iroute_rate': ir_rate, 'nlsr_rate': ns_rate})

fail_csv = os.path.join(RESULTS, 'eval', 'exp_failover_timeseries.csv')
write_csv(fail_csv, ['time', 'iroute_rate', 'nlsr_rate'], fail_rows)

# -----------------------------------------------------------------------------
# Adversarial robustness: accuracy degradation
# -----------------------------------------------------------------------------

def load_accuracy(trace_path, t0=None, t1=None):
    rows = read_csv(trace_path)
    ok = 0
    total = 0
    for r in rows:
        t = f(r.get('startTime', 0.0))
        if t0 is not None and t < t0:
            continue
        if t1 is not None and t >= t1:
            continue
        total += 1
        ok += 1 if i(r.get('correct')) == 1 else 0
    return (ok / total) if total > 0 else 0.0

attack_trace = os.path.join(RESULTS, 'eval', 'exp8_attack', 'exp8_trace.csv')
clean_trace = os.path.join(RESULTS, 'eval', 'exp8_clean', 'exp8_trace.csv')

clean_acc = load_accuracy(clean_trace)
attack_acc = load_accuracy(attack_trace, t0=0.0, t1=30.0)
post_acc = load_accuracy(attack_trace, t0=30.0, t1=120.0)

adv_rows = [
    {'scenario': 'Clean', 'accuracy': clean_acc},
    {'scenario': 'Attack', 'accuracy': attack_acc},
    {'scenario': 'Post-attack', 'accuracy': post_acc},
]
adv_csv = os.path.join(RESULTS, 'eval', 'exp_adversary.csv')
write_csv(adv_csv, ['scenario', 'accuracy'], adv_rows)

# -----------------------------------------------------------------------------
# Plotting helpers
# -----------------------------------------------------------------------------

def style_bar(ax):
    ax.grid(axis='y', linestyle='--', linewidth=0.4, alpha=0.6)


def save_fig(fig, name):
    path = os.path.join(FIGS, name)
    fig.tight_layout()
    fig.savefig(path, format='pdf')
    plt.close(fig)

# 1) Rate–distortion (Hit@1 vs M + flooding)
fig, ax = plt.subplots(figsize=(3.6, 2.4))
ms = [r['M'] for r in exp1_rows]
h1 = [r['hit1'] for r in exp1_rows]
ax.bar(ms, h1, color=BLUE, edgecolor='black', linewidth=0.6, label='iRoute', hatch='//')
ax.axhline(flood_acc, color=ORANGE, linestyle='--', linewidth=1.2, label='Flooding')
ax.set_xlabel('Centroids per domain (M)')
ax.set_ylabel('Domain Hit@1')
ax.set_xticks(ms)
ax.set_ylim(0, 1.05)
ax.legend(frameon=False)
style_bar(ax)
save_fig(fig, 'eval-rate-distortion.pdf')

# 2) Accuracy vs budget (Hit@1 & Hit@3)
fig, ax = plt.subplots(figsize=(3.6, 2.4))
width = 0.35
x = range(len(ms))
ax.bar([i - width/2 for i in x], h1, width=width, color=BLUE, edgecolor='black', linewidth=0.6, label='Hit@1', hatch='//')
ax.bar([i + width/2 for i in x], [r['hit3'] for r in exp1_rows], width=width, color=ORANGE, edgecolor='black', linewidth=0.6, label='Hit@3', hatch='\\')
ax.set_xticks(list(x))
ax.set_xticklabels([str(m) for m in ms])
ax.set_xlabel('Centroids per domain (M)')
ax.set_ylabel('Hit Rate')
ax.set_ylim(0, 1.05)
ax.legend(frameon=False)
style_bar(ax)
save_fig(fig, 'eval-accuracy-vs-m.pdf')

# 3) Baseline comparison (success rate)
fig, ax = plt.subplots(figsize=(3.2, 2.4))
methods = [r['method'] for r in base_rows]
vals = [r['success_rate'] for r in base_rows]
ax.bar(methods, vals, color=[BLUE, ORANGE, RED], edgecolor='black', linewidth=0.6, hatch='//')
ax.set_ylabel('Discovery Success Rate')
ax.set_ylim(0, 1.05)
style_bar(ax)
save_fig(fig, 'eval-baselines.pdf')

# 4) Parameter sensitivity (tau sweep) – two subplots
fig, axes = plt.subplots(1, 2, figsize=(6.2, 2.4), sharex=True)
taus = [r['tau'] for r in read_csv(tau_csv)]
accs = [f(r['domain_acc'])/100.0 for r in read_csv(tau_csv)]
probes = [f(r['avg_probes']) for r in read_csv(tau_csv)]

axes[0].bar(taus, accs, color=BLUE, edgecolor='black', linewidth=0.6, hatch='//')
axes[0].set_ylabel('Domain Accuracy')
axes[0].set_xlabel('Confidence threshold τ')
axes[0].set_ylim(0, 1.05)
style_bar(axes[0])

axes[1].bar(taus, probes, color=ORANGE, edgecolor='black', linewidth=0.6, hatch='\\')
axes[1].set_ylabel('Avg Probes')
axes[1].set_xlabel('Confidence threshold τ')
style_bar(axes[1])

save_fig(fig, 'eval-param-sweep.pdf')

# 5) Packet-level latency (mean with p95 error)
lat = read_csv(lat_csv)
fig, ax = plt.subplots(figsize=(3.2, 2.4))
labels = [r['method'] for r in lat]
means = [f(r['mean_ms']) for r in lat]
p95s = [f(r['p95_ms']) for r in lat]
err = [max(p95s[i] - means[i], 0.0) for i in range(len(means))]
ax.bar(labels, means, yerr=err, capsize=3, color=BLUE, edgecolor='black', linewidth=0.6, hatch='//')
ax.set_ylabel('End-to-end latency (ms)')
style_bar(ax)
save_fig(fig, 'eval-latency.pdf')

# 6) Probe traffic overhead
ov = read_csv(ov_csv)
fig, ax = plt.subplots(figsize=(3.2, 2.4))
labels = [r['method'] for r in ov]
vals = [f(r['bytes_per_query']) for r in ov]
ax.bar(labels, vals, color=ORANGE, edgecolor='black', linewidth=0.6, hatch='\\')
ax.set_ylabel('Discovery bytes / query')
style_bar(ax)
save_fig(fig, 'eval-probe-overhead.pdf')

# 7) Compute overhead
comp = read_csv(compute_csv)
fig, ax = plt.subplots(figsize=(3.2, 2.4))
labels = [r['method'] for r in comp]
vals = [f(r['ops_per_query']) for r in comp]
ax.bar(labels, vals, color=[BLUE, ORANGE], edgecolor='black', linewidth=0.6, hatch='//')
ax.set_ylabel('Similarity ops / query')
style_bar(ax)
save_fig(fig, 'eval-compute.pdf')

# 8) Control state scaling
state = read_csv(state_csv)
fig, ax = plt.subplots(figsize=(3.6, 2.4))
xs = sorted(set(i(r['domains']) for r in state))
idx = range(len(xs))
width = 0.35
ir_vals = [next(f(r['entries']) for r in state if r['protocol']=='iRoute' and i(r['domains'])==x) for x in xs]
ns_vals = [next(f(r['entries']) for r in state if r['protocol']=='NLSR' and i(r['domains'])==x) for x in xs]
ax.bar([i - width/2 for i in idx], ir_vals, width=width, color=BLUE, edgecolor='black', linewidth=0.6, label='iRoute', hatch='//')
ax.bar([i + width/2 for i in idx], ns_vals, width=width, color=ORANGE, edgecolor='black', linewidth=0.6, label='NLSR', hatch='\\')
ax.set_xticks(list(idx))
ax.set_xticklabels([str(x) for x in xs])
ax.set_xlabel('Number of domains')
ax.set_ylabel('Control entries')
ax.legend(frameon=False)
style_bar(ax)
save_fig(fig, 'eval-scaling-state.pdf')

# 9) Control-plane traffic
traffic = read_csv(traffic_csv)
fig, ax = plt.subplots(figsize=(3.6, 2.4))
xs = sorted(set(i(r['domains']) for r in traffic))
idx = range(len(xs))
width = 0.35
ir_vals = [next(f(r['bytes_per_sec']) for r in traffic if r['protocol']=='IROUTE' and i(r['domains'])==x) for x in xs]
ns_vals = [next(f(r['bytes_per_sec']) for r in traffic if r['protocol']=='NLSR' and i(r['domains'])==x) for x in xs]
ax.bar([i - width/2 for i in idx], ir_vals, width=width, color=BLUE, edgecolor='black', linewidth=0.6, label='iRoute', hatch='//')
ax.bar([i + width/2 for i in idx], ns_vals, width=width, color=ORANGE, edgecolor='black', linewidth=0.6, label='NLSR', hatch='\\')
ax.set_xticks(list(idx))
ax.set_xticklabels([str(x) for x in xs])
ax.set_xlabel('Number of domains')
ax.set_ylabel('Control bytes / sec')
ax.legend(frameon=False)
style_bar(ax)
save_fig(fig, 'eval-control-overhead.pdf')

# 10) Churn robustness (reconvergence time)
churn = read_csv(churn_csv)
fig, ax = plt.subplots(figsize=(3.2, 2.4))
labels = [r['protocol'] for r in churn]
vals = [f(r['reconv_ms']) for r in churn]
ax.bar(labels, vals, color=[BLUE, ORANGE], edgecolor='black', linewidth=0.6, hatch='//')
ax.set_ylabel('Reconvergence time (ms)')
style_bar(ax)
save_fig(fig, 'eval-churn.pdf')

# 11) Failover (success over time)
fail = read_csv(fail_csv)
fig, ax = plt.subplots(figsize=(4.0, 2.4))
ax.plot([f(r['time']) for r in fail], [f(r['iroute_rate']) for r in fail], color=BLUE, marker='o', linestyle='-', label='iRoute')
ax.plot([f(r['time']) for r in fail], [f(r['nlsr_rate']) for r in fail], color=ORANGE, marker='s', linestyle='--', label='NLSR')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Success rate')
ax.set_ylim(0, 1.05)
ax.legend(frameon=False)
ax.grid(axis='y', linestyle='--', linewidth=0.4, alpha=0.6)
save_fig(fig, 'eval-failover.pdf')

# 12) Adversarial robustness
adv = read_csv(adv_csv)
fig, ax = plt.subplots(figsize=(3.2, 2.4))
labels = [r['scenario'] for r in adv]
vals = [f(r['accuracy']) for r in adv]
ax.bar(labels, vals, color=[BLUE, ORANGE, RED], edgecolor='black', linewidth=0.6, hatch='//')
ax.set_ylabel('Accuracy')
ax.set_ylim(0, 1.05)
style_bar(ax)
save_fig(fig, 'eval-adversary.pdf')

print('Generated figures in', FIGS)
