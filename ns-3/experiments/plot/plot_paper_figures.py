#!/usr/bin/env python3
import argparse
import datetime as dt
import os
from glob import glob

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

SCHEME_ORDER = ["central", "iroute", "tag", "sanr-tag", "flood", "sanr-oracle", "exact"]
MAIN_SCHEMES = ["central", "iroute", "tag", "sanr-tag", "flood"]
ADJUSTABLE_SCHEMES = ["iroute", "flood", "tag"]

STYLES = {
    "central": {"label": "Central (Oracle Upper Bound)", "color": "#111111", "linestyle": "-", "marker": "D", "linewidth": 2.0},
    "iroute": {"label": "iRoute", "color": "#1f77b4", "linestyle": "-", "marker": "o", "linewidth": 1.8},
    "tag": {"label": "INF-NDN", "color": "#2ca02c", "linestyle": "-.", "marker": "s", "linewidth": 1.8},
    "sanr-tag": {"label": "SANR-CMF (Tag Forwarding)", "color": "#ff7f0e", "linestyle": "-", "marker": "P", "linewidth": 1.8},
    "flood": {"label": "Flood", "color": "#7f7f7f", "linestyle": "--", "marker": "x", "linewidth": 1.8},
    "sanr-oracle": {"label": "SANR-CMF (Oracle Domain)", "color": "#8c564b", "linestyle": "--", "marker": "v", "linewidth": 1.8},
    "exact": {"label": "Exact", "color": "#9467bd", "linestyle": ":", "marker": "^", "linewidth": 1.8},
}

WARNINGS = []
RECOVERY_BIN_QUERIES = 20


def warn(msg: str):
    WARNINGS.append(msg)
    print(f"[plot][WARN] {msg}")


def info(msg: str):
    print(f"[plot] {msg}")


def set_style():
    plt.rcParams["font.family"] = ["Times New Roman", "Helvetica", "serif"]
    plt.rcParams["font.size"] = 9
    plt.rcParams["axes.labelsize"] = 10
    plt.rcParams["axes.titlesize"] = 10
    plt.rcParams["xtick.labelsize"] = 9
    plt.rcParams["ytick.labelsize"] = 9
    plt.rcParams["legend.fontsize"] = 9
    plt.rcParams["lines.linewidth"] = 1.8
    plt.rcParams["axes.grid"] = True
    plt.rcParams["grid.linestyle"] = "--"
    plt.rcParams["grid.alpha"] = 0.35


def _scheme_key(name: str) -> str:
    n = str(name).lower().strip()
    if "sanr-oracle" in n:
        return "sanr-oracle"
    if "sanr-tag" in n:
        return "sanr-tag"
    if n == "sanr":
        return "sanr-tag"
    if "central" in n:
        return "central"
    if "iroute" in n:
        return "iroute"
    if "tag" in n:
        return "tag"
    if "flood" in n:
        return "flood"
    if "exact" in n:
        return "exact"
    return n


def _mean_ci(values: pd.Series):
    vals = pd.to_numeric(values, errors="coerce").dropna().astype(float)
    if vals.empty:
        return np.nan, np.nan, 0
    m = float(vals.mean())
    n = int(vals.size)
    if n <= 1:
        return m, 0.0, n
    ci = 1.96 * float(vals.std(ddof=1)) / np.sqrt(n)
    return m, ci, n


def _load_accuracy_df(acc_dir: str):
    sweep_csv = os.path.join(acc_dir, "accuracy_sweep.csv")
    cmp_csv = os.path.join(acc_dir, "comparison.csv")
    if os.path.exists(sweep_csv):
        df = pd.read_csv(sweep_csv)
        df["scheme"] = df["scheme"].astype(str).map(_scheme_key)
        if "is_reference" not in df.columns:
            df["is_reference"] = 0
        if "seed" not in df.columns:
            df["seed"] = 0
        return df
    if os.path.exists(cmp_csv):
        df = pd.read_csv(cmp_csv)
        df["scheme"] = df["scheme"].astype(str).map(_scheme_key)
        for c in ["seed", "sweep_param", "sweep_value", "is_reference"]:
            if c not in df.columns:
                df[c] = 0
        if "DomainRecall_at_1" in df.columns:
            df["domain_recall_at_1"] = df["DomainRecall_at_1"]
        if "DomainRecall_at_3" in df.columns:
            df["domain_recall_at_3"] = df["DomainRecall_at_3"]
        if "DomainRecall_at_5" in df.columns:
            df["domain_recall_at_5"] = df["DomainRecall_at_5"]
        if "DocHit_at_10" in df.columns:
            df["doc_hit_at_10"] = df["DocHit_at_10"]
        if "nDCG_at_10" in df.columns:
            df["ndcg_at_10"] = df["nDCG_at_10"]
        return df
    return pd.DataFrame()


def _resolve_run_dir(run_dir: str, acc_dir: str):
    rd = str(run_dir).strip()
    if not rd:
        return ""
    if os.path.isabs(rd):
        return rd
    if os.path.exists(rd):
        return os.path.abspath(rd)
    if acc_dir:
        # accuracy dir: <ns3>/results/<exp>; result_dir in CSV is usually relative to <ns3>
        ns3_root = os.path.abspath(os.path.join(acc_dir, "..", ".."))
        cand = os.path.join(ns3_root, rd)
        if os.path.exists(cand):
            return cand
    return os.path.abspath(rd)


def _infer_reference_runs(acc_df: pd.DataFrame, acc_dir: str = ""):
    refs = {}
    if acc_df.empty or "result_dir" not in acc_df.columns:
        return refs
    preferred = acc_df[acc_df.get("is_reference", 0) == 1].copy()
    for scheme in SCHEME_ORDER:
        sub = preferred[preferred["scheme"] == scheme].copy()
        if sub.empty:
            sub = acc_df[acc_df["scheme"] == scheme].copy()
        if sub.empty:
            continue
        if "seed" in sub.columns:
            sub = sub.sort_values(["seed", "ctrl_bytes_per_sec"], ascending=[True, True])
        run_dirs = []
        for rd in sub["result_dir"].astype(str).tolist():
            resolved = _resolve_run_dir(rd, acc_dir)
            if os.path.exists(os.path.join(resolved, "query_log.csv")):
                run_dirs.append(resolved)
        run_dirs = sorted(set(run_dirs))
        if run_dirs:
            refs[scheme] = run_dirs
    return refs


def _load_query_log(run_ref: str):
    path = run_ref if run_ref.endswith(".csv") else os.path.join(run_ref, "query_log.csv")
    if not os.path.exists(path):
        return None, path
    try:
        return pd.read_csv(path), path
    except Exception as ex:
        warn(f"failed to read {path}: {ex}")
        return None, path


def _normalize_run_refs(run_ref):
    if isinstance(run_ref, str):
        return [run_ref]
    if isinstance(run_ref, (list, tuple, set)):
        return [str(x) for x in run_ref if str(x).strip()]
    return [str(run_ref)]


def _load_query_logs(run_ref):
    used_paths = []
    dfs = []
    for ref in _normalize_run_refs(run_ref):
        qdf, path = _load_query_log(ref)
        if qdf is None or qdf.empty:
            continue
        used_paths.append(path)
        dfs.append(qdf)
    if not dfs:
        return None, used_paths
    return pd.concat(dfs, ignore_index=True), used_paths


def _measurement_subset(df: pd.DataFrame):
    out = df.copy()
    if "is_measurable" in out.columns:
        out = out[pd.to_numeric(out["is_measurable"], errors="coerce").fillna(0) > 0]
    return out


def _success_subset(df: pd.DataFrame):
    out = df.copy()
    if "is_success" in out.columns:
        out = out[pd.to_numeric(out["is_success"], errors="coerce").fillna(0) > 0]
    else:
        out = out[pd.to_numeric(out.get("rtt_total_ms", 0), errors="coerce").fillna(0) > 0]
    return out


def _latency_ms_series(df: pd.DataFrame):
    out = pd.Series(dtype=float)
    if {"t_send_disc_ns", "t_recv_data_ns"}.issubset(df.columns):
        send_ns = pd.to_numeric(df["t_send_disc_ns"], errors="coerce")
        recv_ns = pd.to_numeric(df["t_recv_data_ns"], errors="coerce")
        ns_ok = send_ns.notna() & recv_ns.notna() & (send_ns > 0) & (recv_ns >= send_ns)
        if ns_ok.any():
            out = ((recv_ns[ns_ok] - send_ns[ns_ok]) / 1e6).astype(float)
    if out.empty:
        out = pd.to_numeric(df.get("rtt_total_ms", pd.Series(dtype=float)), errors="coerce")
    out = out[np.isfinite(out) & (out > 0)]
    return out.astype(float)


def _cdf_quantile_curve(vals: pd.Series, points: int = 800):
    arr = pd.to_numeric(vals, errors="coerce").to_numpy(dtype=float)
    arr = arr[np.isfinite(arr) & (arr > 0)]
    if arr.size == 0:
        return np.array([]), np.array([])
    arr = np.sort(arr)
    if arr.size == 1:
        return arr, np.array([1.0], dtype=float)
    q = np.linspace(1.0 / float(arr.size), 1.0, num=max(64, int(points)), dtype=float)
    try:
        x = np.quantile(arr, q, method="linear")
    except TypeError:
        x = np.quantile(arr, q, interpolation="linear")
    return x.astype(float), q.astype(float)


def _fetch_ms_series(df: pd.DataFrame):
    out = pd.Series(dtype=float)
    if "fetch_ms" in df.columns:
        out = pd.to_numeric(df["fetch_ms"], errors="coerce")
    elif {"t_send_fetch_ns", "t_recv_data_ns"}.issubset(df.columns):
        send_ns = pd.to_numeric(df["t_send_fetch_ns"], errors="coerce")
        recv_ns = pd.to_numeric(df["t_recv_data_ns"], errors="coerce")
        ok = send_ns.notna() & recv_ns.notna() & (send_ns >= 0) & (recv_ns >= send_ns)
        out = pd.Series(np.nan, index=df.index, dtype=float)
        out.loc[ok] = ((recv_ns[ok] - send_ns[ok]) / 1e6).astype(float)
    elif {"t_send_fetch", "t_recv_data"}.issubset(df.columns):
        send_ms = pd.to_numeric(df["t_send_fetch"], errors="coerce")
        recv_ms = pd.to_numeric(df["t_recv_data"], errors="coerce")
        out = recv_ms - send_ms
    else:
        out = pd.Series(np.nan, index=df.index, dtype=float)
    return out


def _cache_miss_subset(df: pd.DataFrame, eps: float = 1e-9):
    if df is None or df.empty:
        return df.copy(), {"n_success": 0, "n_cache_miss": 0, "cache_hit_ratio": np.nan}
    fetch = _fetch_ms_series(df)
    fetch = pd.to_numeric(fetch, errors="coerce")
    valid_fetch = fetch.notna()
    hit_by_fetch = valid_fetch & (fetch <= eps)
    if "cache_hit_exact" in df.columns or "cache_hit_semantic" in df.columns:
        exact = pd.to_numeric(df.get("cache_hit_exact", 0), errors="coerce").fillna(0)
        sem = pd.to_numeric(df.get("cache_hit_semantic", 0), errors="coerce").fillna(0)
        hit = (exact > 0) | (sem > 0) | hit_by_fetch
        # Keep strict fetch-based miss filter when available to avoid counting
        # unknown/NaN fetch rows as misses.
        miss = valid_fetch & (fetch > eps) & (~((exact > 0) | (sem > 0)))
        miss_df = df.loc[miss].copy()
        n_success = int(len(df))
        n_miss = int(miss.sum())
        ratio = float(hit.sum() / n_success) if n_success > 0 else np.nan
        return miss_df, {"n_success": n_success, "n_cache_miss": n_miss, "cache_hit_ratio": ratio}
    valid = fetch.notna()
    miss = valid & (fetch > eps)
    hit = valid & (fetch <= eps)
    miss_df = df.loc[miss].copy()
    n_success = int(len(df))
    n_miss = int(miss.sum())
    n_hit = int(hit.sum())
    ratio = float(n_hit / n_success) if n_success > 0 else np.nan
    return miss_df, {"n_success": n_success, "n_cache_miss": n_miss, "cache_hit_ratio": ratio}


def _aggregate_sweep(acc_df: pd.DataFrame, schemes):
    rows = []
    for scheme in schemes:
        sub = acc_df[acc_df["scheme"] == scheme]
        if sub.empty:
            continue
        if "sweep_param" not in sub.columns:
            sub = sub.copy()
            sub["sweep_param"] = "single"
            sub["sweep_value"] = 0
        for _, g in sub.groupby(["sweep_param", "sweep_value"], dropna=False):
            x, _, _ = _mean_ci(g["ctrl_bytes_per_sec"])
            row = {"scheme": scheme, "x": x}
            for c in ["domain_recall_at_1", "domain_recall_at_3", "domain_recall_at_5", "doc_hit_at_10", "ndcg_at_10"]:
                if c in g.columns:
                    m, ci, n = _mean_ci(g[c])
                    row[c] = m
                    row[c + "_ci"] = ci
                    row[c + "_n"] = n
            if "n_success" in g.columns:
                row["n_success"] = int(pd.to_numeric(g["n_success"], errors="coerce").fillna(0).mean())
            if "timeout_rate" in g.columns:
                row["timeout_rate"] = float(pd.to_numeric(g["timeout_rate"], errors="coerce").fillna(0).mean())
            if "unique_rtt_values" in g.columns:
                row["unique_rtt_values"] = int(pd.to_numeric(g["unique_rtt_values"], errors="coerce").fillna(0).mean())
            rows.append(row)
    return pd.DataFrame(rows)


def plot_accuracy_overhead(acc_df: pd.DataFrame, output_dir: str, fig_index: dict, include_exact: bool, out_name: str):
    schemes = MAIN_SCHEMES + (["exact"] if include_exact else [])
    agg = _aggregate_sweep(acc_df, schemes)
    if agg.empty:
        warn(f"skip {out_name}: empty sweep aggregate")
        return

    for scheme in ADJUSTABLE_SCHEMES:
        if scheme in schemes:
            points = agg[agg["scheme"] == scheme]["x"].dropna().shape[0]
            if points < 4:
                warn(f"{out_name}: {scheme} sweep points <4 ({points})")

    fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.4), sharex=True)
    ax1, ax2 = axes
    for scheme in schemes:
        sub = agg[agg["scheme"] == scheme].sort_values("x")
        if sub.empty:
            warn(f"{out_name}: missing scheme {scheme}")
            continue
        st = STYLES[scheme]
        for metric, ls in [("domain_recall_at_1", "-"), ("domain_recall_at_3", "--"), ("domain_recall_at_5", ":")]:
            if metric in sub.columns:
                ax1.errorbar(sub["x"], sub[metric], yerr=sub.get(metric + "_ci", 0), color=st["color"],
                             linestyle=ls, marker=st["marker"], linewidth=st["linewidth"], markersize=4, capsize=2)
        for metric, ls in [("doc_hit_at_10", "-"), ("ndcg_at_10", "--")]:
            if metric in sub.columns:
                ax2.errorbar(sub["x"], sub[metric], yerr=sub.get(metric + "_ci", 0), color=st["color"],
                             linestyle=ls, marker=st["marker"], linewidth=st["linewidth"], markersize=4, capsize=2)

    ax1.set_xlabel("Control Overhead (Bytes/s)")
    ax1.set_ylabel("Domain Recall")
    ax1.set_ylim(0.0, 1.02)
    ax1.set_title("(a) DomainRecall@1/3/5 vs Overhead")
    ax2.set_xlabel("Control Overhead (Bytes/s)")
    ax2.set_ylabel("Doc Success")
    ax2.set_ylim(0.0, 1.02)
    ax2.set_title("(b) Single-Fetch Doc Success vs Overhead")

    scheme_handles = [Line2D([0], [0], color=STYLES[s]["color"], marker=STYLES[s]["marker"], linestyle="-", label=STYLES[s]["label"])
                      for s in schemes if s in agg["scheme"].values]
    metric1 = [Line2D([0], [0], color="black", linestyle="-", label="R@1"),
               Line2D([0], [0], color="black", linestyle="--", label="R@3"),
               Line2D([0], [0], color="black", linestyle=":", label="R@5")]
    metric2 = [Line2D([0], [0], color="black", linestyle="-", label="DocHit (single fetch)"),
               Line2D([0], [0], color="black", linestyle="--", label="nDCG (single fetch)")]
    l1 = ax1.legend(handles=scheme_handles, loc="lower right", frameon=True, title="Scheme")
    ax1.add_artist(l1)
    ax1.legend(handles=metric1, loc="lower center", frameon=True, title="Metric")
    l2 = ax2.legend(handles=scheme_handles, loc="lower right", frameon=True, title="Scheme")
    ax2.add_artist(l2)
    ax2.legend(handles=metric2, loc="lower center", frameon=True, title="Metric")

    out = os.path.join(output_dir, out_name)
    plt.tight_layout()
    plt.savefig(out)
    plt.close()

    fig_index[out_name] = {
        "schemes": schemes,
        "rows": int(len(agg)),
        "oracle_flag": "central included as oracle upper bound",
        "n_success": {s: int(agg[agg["scheme"] == s]["n_success"].mean()) if "n_success" in agg.columns and not agg[agg["scheme"] == s].empty else None for s in schemes},
        "timeout_rate": {s: float(agg[agg["scheme"] == s]["timeout_rate"].mean()) if "timeout_rate" in agg.columns and not agg[agg["scheme"] == s].empty else None for s in schemes},
        "unique_rtt_values": {s: int(agg[agg["scheme"] == s]["unique_rtt_values"].mean()) if "unique_rtt_values" in agg.columns and not agg[agg["scheme"] == s].empty else None for s in schemes},
    }


def plot_retrieval_cdf(ref_runs: dict, output_dir: str, fig_index: dict, include_exact: bool, out_name: str, xscale: str):
    schemes = MAIN_SCHEMES + (["exact"] if include_exact else [])
    fig, ax = plt.subplots(figsize=(7.2, 4.8))
    used = {}
    xmax = 1.0
    for scheme in schemes:
        if scheme not in ref_runs:
            warn(f"{out_name}: missing reference run for {scheme}")
            continue
        qdf, paths = _load_query_logs(ref_runs[scheme])
        if qdf is None or qdf.empty:
            warn(f"{out_name}: empty query log for {scheme}")
            continue
        meas = _measurement_subset(qdf)
        all_n = len(meas)
        suc = _success_subset(meas)
        suc_miss, miss_stats = _cache_miss_subset(suc)
        vals = _latency_ms_series(suc_miss).sort_values().reset_index(drop=True)
        if vals.empty:
            warn(f"{out_name}: no cache-miss success RTT for {scheme}")
            continue
        x_cdf, y_cdf = _cdf_quantile_curve(vals)
        if x_cdf.size == 0:
            warn(f"{out_name}: failed to build CDF for {scheme}")
            continue
        st = STYLES[scheme]
        label = st["label"]
        ax.plot(x_cdf, y_cdf, label=label, color=st["color"], linestyle=st["linestyle"], linewidth=st["linewidth"])
        p50 = float(np.percentile(vals, 50))
        p95 = float(np.percentile(vals, 95))
        ax.scatter([p50, p95], [0.5, 0.95], color=st["color"], marker=st["marker"], s=20, zorder=3)
        ax.annotate(f"{st['label']} P50={p50:.1f}ms\nP95={p95:.1f}ms", xy=(p95, 0.95),
                    xytext=(4, 2), textcoords="offset points", fontsize=7, color=st["color"])
        xmax = max(xmax, float(np.percentile(vals, 99.5)) * 1.1)

        timeout_rate = 1.0 - (len(suc) / all_n) if all_n > 0 else 0.0
        used[scheme] = {
            "runs": paths,
            "n_runs": int(len(paths)),
            "n_success": int(len(suc)),
            "n_success_cache_miss": int(miss_stats["n_cache_miss"]),
            "cache_hit_ratio": float(miss_stats["cache_hit_ratio"]) if np.isfinite(miss_stats["cache_hit_ratio"]) else None,
            "timeout_rate": float(timeout_rate),
            "unique_rtt_values": int(vals.round(3).nunique()),
            "p50_ms": p50,
            "p95_ms": p95,
            "oracle_flag": bool(scheme == "central"),
        }
        if scheme != "exact" and abs(p95 - p50) < 1e-9:
            warn(f"{out_name}: {scheme} has degenerate p50==p95")

    if not used:
        plt.close()
        warn(f"skip {out_name}: no valid curves")
        return

    ax.set_xlabel("Retrieval Time (ms)")
    ax.set_ylabel("CDF")
    ax.set_title("Retrieval Latency CDF (success-only, cache-miss)")
    if xscale == "log":
        ax.set_xscale("log")
        ax.set_xlim(left=1e-3, right=max(1.0, xmax))
    else:
        ax.set_xlim(0.0, max(1.0, xmax))
    ax.legend(loc="lower right", frameon=True)
    out = os.path.join(output_dir, out_name)
    plt.tight_layout()
    plt.savefig(out)
    plt.close()
    fig_index[out_name] = used


def plot_cache_hit_ratio(ref_runs: dict, output_dir: str, fig_index: dict):
    rows = []
    for scheme in MAIN_SCHEMES:
        if scheme not in ref_runs:
            continue
        qdf, paths = _load_query_logs(ref_runs[scheme])
        if qdf is None or qdf.empty:
            continue
        meas = _measurement_subset(qdf)
        suc = _success_subset(meas)
        n_success = int(len(suc))
        if n_success <= 0:
            continue
        exact_hit_ratio = 0.0
        semantic_hit_ratio = 0.0
        if "cache_hit_exact" in suc.columns or "cache_hit_semantic" in suc.columns:
            exact_hits = pd.to_numeric(suc.get("cache_hit_exact", 0), errors="coerce").fillna(0)
            sem_hits = pd.to_numeric(suc.get("cache_hit_semantic", 0), errors="coerce").fillna(0)
            fetch = pd.to_numeric(_fetch_ms_series(suc), errors="coerce")
            hit_fetch = fetch.notna() & (fetch <= 1e-9)
            hit_sem = sem_hits > 0
            hit_exact = (exact_hits > 0) | (hit_fetch & (~hit_sem))
            hit_any = hit_exact | hit_sem
            exact_hit_ratio = float(hit_exact.sum()) / n_success
            semantic_hit_ratio = float(hit_sem.sum()) / n_success
            miss_stats = {
                "n_success": n_success,
                "n_cache_miss": int((~hit_any).sum()),
                "cache_hit_ratio": float(hit_any.sum()) / n_success,
            }
        else:
            _, miss_stats = _cache_miss_subset(suc)
            exact_hit_ratio = float(miss_stats["cache_hit_ratio"]) if np.isfinite(miss_stats["cache_hit_ratio"]) else 0.0
            semantic_hit_ratio = 0.0
        rows.append({
            "scheme": scheme,
            "paths": paths,
            "n_runs": int(len(paths)),
            "n_success": int(n_success),
            "n_success_cache_miss": int(miss_stats["n_cache_miss"]),
            "cache_hit_ratio": float(miss_stats["cache_hit_ratio"]),
            "exact_hit_ratio": float(exact_hit_ratio),
            "semantic_hit_ratio": float(semantic_hit_ratio),
        })
    if not rows:
        warn("skip fig2_cache_hit_ratio: no rows")
        return

    df = pd.DataFrame(rows)
    df["ord"] = df["scheme"].map({s: i for i, s in enumerate(SCHEME_ORDER)})
    df = df.sort_values("ord")
    x = np.arange(len(df))
    colors = [STYLES[s]["color"] for s in df["scheme"].tolist()]

    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.bar(x, df["exact_hit_ratio"], color=colors, width=0.62, label="Exact hit")
    ax.bar(x, df["semantic_hit_ratio"], color="#fdae6b", width=0.62, bottom=df["exact_hit_ratio"], label="Semantic hit")
    ax.set_xticks(x)
    ax.set_xticklabels([STYLES[s]["label"].replace(" (Oracle Upper Bound)", "") for s in df["scheme"]])
    ax.set_ylabel("Cache-hit Ratio")
    ax.set_ylim(0.0, 1.0)
    ax.set_title("Cache-hit Ratio among Successful Queries (Exact + Semantic)")
    ax.legend(loc="upper right", frameon=True)
    for i, r in df.reset_index(drop=True).iterrows():
        ax.text(x[i], min(0.99, r["cache_hit_ratio"] + 0.02), f"{r['cache_hit_ratio']:.1%}",
                ha="center", va="bottom", fontsize=8)

    out_name = "fig2_cache_hit_ratio.pdf"
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, out_name))
    plt.close()

    fig_index[out_name] = {
        r["scheme"]: {
            "runs": r["paths"],
            "n_runs": int(r["n_runs"]),
            "n_success": int(r["n_success"]),
            "n_success_cache_miss": int(r["n_success_cache_miss"]),
            "cache_hit_ratio": float(r["cache_hit_ratio"]),
            "exact_hit_ratio": float(r["exact_hit_ratio"]),
            "semantic_hit_ratio": float(r["semantic_hit_ratio"]),
        } for _, r in df.iterrows()
    }


def plot_exact_timeout_appendix(ref_runs: dict, output_dir: str, fig_index: dict):
    if "exact" not in ref_runs:
        warn("appendix exact profile skipped: no exact reference run")
        return
    qdf, paths = _load_query_logs(ref_runs["exact"])
    if qdf is None or qdf.empty:
        warn("appendix exact profile skipped: empty exact log")
        return
    meas = _measurement_subset(qdf)
    vals = _latency_ms_series(meas).sort_values().reset_index(drop=True)
    if vals.empty:
        warn("appendix exact profile skipped: no positive RTT")
        return
    suc = _success_subset(meas)
    suc_vals = _latency_ms_series(suc).sort_values().reset_index(drop=True)

    fig, ax = plt.subplots(figsize=(6.8, 4.6))
    y_all = np.arange(1, len(vals) + 1, dtype=float) / float(len(vals))
    ax.plot(vals, y_all, color=STYLES["exact"]["color"], linestyle=":", linewidth=2.0, label="Exact (all queries)")
    if len(suc_vals) > 0:
        y_suc = np.arange(1, len(suc_vals) + 1, dtype=float) / float(len(suc_vals))
        ax.plot(suc_vals, y_suc, color="#111111", linestyle="-", linewidth=1.6, label="Exact (success-only)")
    timeout_rate = 1.0 - (len(suc_vals) / len(meas)) if len(meas) > 0 else 0.0
    ax.set_xlabel("Retrieval Time (ms)")
    ax.set_ylabel("CDF")
    ax.set_title(f"Exact Strict Timeout Profile (timeout={timeout_rate:.2%})")
    ax.legend(loc="lower right", frameon=True)
    out_name = "fig2_exact_timeout_profile.pdf"
    out = os.path.join(output_dir, out_name)
    plt.tight_layout()
    plt.savefig(out)
    plt.close()
    fig_index[out_name] = {
        "runs": paths,
        "n_runs": int(len(paths)),
        "n_success": int(len(suc_vals)),
        "timeout_rate": float(timeout_rate),
        "unique_rtt_values": int(vals.round(3).nunique()),
    }


def plot_latency_breakdown(ref_runs: dict, output_dir: str, fig_index: dict):
    rows = []
    for scheme in SCHEME_ORDER:
        if scheme not in ref_runs:
            continue
        qdf, paths = _load_query_logs(ref_runs[scheme])
        if qdf is None or qdf.empty:
            continue
        meas = _measurement_subset(qdf)
        suc = _success_subset(meas)
        if suc.empty:
            continue
        disc = pd.to_numeric(suc.get("disc_ms", suc.get("t_recv_disc_reply", 0) - suc.get("t_send_disc", 0)), errors="coerce")
        fetch = pd.to_numeric(suc.get("fetch_ms", suc.get("t_recv_data", 0) - suc.get("t_send_fetch", 0)), errors="coerce")
        disc = disc[np.isfinite(disc) & (disc >= 0)]
        fetch = fetch[np.isfinite(fetch) & (fetch >= 0)]
        d50 = float(np.percentile(disc, 50)) if len(disc) else 0.0
        d95 = float(np.percentile(disc, 95)) if len(disc) else 0.0
        f50 = float(np.percentile(fetch, 50)) if len(fetch) else 0.0
        f95 = float(np.percentile(fetch, 95)) if len(fetch) else 0.0
        rows.append({"scheme": scheme, "paths": paths, "disc_p50": d50, "disc_p95": d95, "fetch_p50": f50, "fetch_p95": f95})
    if not rows:
        warn("skip fig2b: no rows")
        return
    df = pd.DataFrame(rows)
    df["ord"] = df["scheme"].map({s: i for i, s in enumerate(SCHEME_ORDER)})
    df = df.sort_values("ord")

    x = np.arange(len(df))
    w = 0.34
    fig, ax = plt.subplots(figsize=(8.4, 4.7))
    ax.bar(x - w / 2, df["disc_p50"], width=w, color="#9ecae1", label="Discovery (P50)")
    ax.bar(x - w / 2, df["fetch_p50"], width=w, bottom=df["disc_p50"], color="#3182bd", label="Fetch (P50)")
    ax.bar(x + w / 2, df["disc_p95"], width=w, color="#fdd0a2", label="Discovery (P95)")
    ax.bar(x + w / 2, df["fetch_p95"], width=w, bottom=df["disc_p95"], color="#e6550d", label="Fetch (P95)")
    for i in range(len(df)):
        t50 = df.iloc[i]["disc_p50"] + df.iloc[i]["fetch_p50"]
        t95 = df.iloc[i]["disc_p95"] + df.iloc[i]["fetch_p95"]
        ax.text(x[i] - w / 2, t50 + 1, f"{t50:.1f}", ha="center", va="bottom", fontsize=8)
        ax.text(x[i] + w / 2, t95 + 1, f"{t95:.1f}", ha="center", va="bottom", fontsize=8)
    ax.set_xticks(x)
    ax.set_xticklabels([STYLES[s]["label"].replace(" (Oracle Upper Bound)", "") for s in df["scheme"]])
    ax.set_ylabel("Latency (ms)")
    ax.set_title("Latency Breakdown: P50/P95 (Discovery + Fetch)")
    ax.legend(loc="upper left", ncol=2, frameon=True)
    out_name = "fig2b_latency_breakdown.pdf"
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, out_name))
    plt.close()
    fig_index[out_name] = {
        r["scheme"]: {
            "runs": r["paths"],
            "n_runs": int(len(r["paths"])),
            "disc_p50": r["disc_p50"],
            "fetch_p50": r["fetch_p50"],
            "disc_p95": r["disc_p95"],
            "fetch_p95": r["fetch_p95"],
        } for _, r in df.iterrows()
    }


def plot_hop_load(base_dir: str, explicit_csv: str, output_dir: str, fig_index: dict):
    candidates = [explicit_csv, os.path.join(base_dir, "exp4-load", "load_sweep.csv"), os.path.join(base_dir, "exp4-load", "load.csv")]
    path = ""
    df = pd.DataFrame()
    for p in candidates:
        if p and os.path.exists(p):
            path = p
            df = pd.read_csv(p)
            break
    if df.empty:
        warn("skip fig3: load data missing")
        return
    if "scheme" in df.columns:
        df["scheme"] = df["scheme"].astype(str).map(_scheme_key)
    used_topology = "unknown"
    if "topology" in df.columns:
        topo = df["topology"].astype(str).str.strip().str.lower()
        rf = df[topo == "rocketfuel"].copy()
        if not rf.empty:
            df = rf
            used_topology = "rocketfuel"
        else:
            warn("fig3: topology column present but no rocketfuel rows; using full dataset")
            used_topology = "mixed"
    if "seed" not in df.columns:
        df["seed"] = 0
    if not {"frequency", "mean_hops", "scheme"}.issubset(df.columns):
        warn("skip fig3: missing columns")
        return
    df["mean_hops"] = pd.to_numeric(df["mean_hops"], errors="coerce")
    # Keep zero-hop rows (e.g., central directory runs without hop instrumentation)
    # so every baseline can still appear in Fig3; stretch baseline still uses >0 hops.
    df = df[np.isfinite(df["mean_hops"]) & (df["mean_hops"] >= 0)].copy()
    if df.empty:
        warn("skip fig3: no positive hop samples")
        return

    agg = []
    for (scheme, freq), g in df.groupby(["scheme", "frequency"]):
        m, ci, _ = _mean_ci(g["mean_hops"])
        agg.append({"scheme": scheme, "frequency": float(freq), "mean_hops": m, "ci": ci})
    agg = pd.DataFrame(agg)
    if agg.empty:
        warn("skip fig3: empty aggregate")
        return

    baseline = {}
    ex = agg[(agg["scheme"] == "exact") & (agg["mean_hops"] > 0)]
    mode = "Exact"
    if ex.empty:
        mode = "Per-frequency min"
        for f, g in agg.groupby("frequency"):
            vals = g[g["mean_hops"] > 0]["mean_hops"]
            baseline[f] = float(vals.min()) if not vals.empty else np.nan
    else:
        for _, r in ex.iterrows():
            baseline[r["frequency"]] = r["mean_hops"]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.0, 4.4))
    ymax = 1.0
    used = []
    for s in SCHEME_ORDER:
        sub = agg[agg["scheme"] == s].sort_values("frequency")
        if sub.empty:
            continue
        used.append(s)
        st = STYLES[s]
        ax1.errorbar(sub["frequency"], sub["mean_hops"], yerr=sub["ci"], color=st["color"], linestyle=st["linestyle"],
                     marker=st["marker"], linewidth=st["linewidth"], capsize=2, label=st["label"])
        ymax = max(ymax, float((sub["mean_hops"] + sub["ci"]).max()))
        stretch = []
        stretch_ci = []
        for _, r in sub.iterrows():
            b = baseline.get(r["frequency"], np.nan)
            if not np.isfinite(b) or b <= 0:
                stretch.append(np.nan)
                stretch_ci.append(np.nan)
            else:
                stretch.append(r["mean_hops"] / b)
                stretch_ci.append(r["ci"] / b)
        ax2.errorbar(sub["frequency"], stretch, yerr=stretch_ci, color=st["color"], linestyle=st["linestyle"],
                     marker=st["marker"], linewidth=st["linewidth"], capsize=2, label=st["label"])

    if "tag" not in used:
        warn("fig3 missing tag curve")
    ax1.set_ylim(0.0, ymax * 1.15)
    ax1.set_xlabel("Query Load (queries/s)")
    ax1.set_ylabel("Average Hops")
    ax1.set_title("(a) Hop Count vs Load")
    ax1.legend(loc="upper left", frameon=True)
    ax2.set_xlabel("Query Load (queries/s)")
    ax2.set_ylabel("Path Stretch")
    ax2.set_title(f"(b) Stretch vs {mode}")
    ax2.legend(loc="upper left", frameon=True)
    out_name = "fig3_hop_load.pdf"
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, out_name))
    plt.close()
    fig_index[out_name] = {"source": path, "schemes": used, "topology": used_topology}


def plot_scaling(base_dir: str, explicit_csv: str, output_dir: str, fig_index: dict):
    candidates = [explicit_csv, os.path.join(base_dir, "exp4-scaling", "scaling.csv"), os.path.join(base_dir, "exp4-scaling-lite", "scaling.csv")]
    path = ""
    df = pd.DataFrame()
    for p in candidates:
        if p and os.path.exists(p):
            path = p
            df = pd.read_csv(p)
            break
    if df.empty:
        warn("skip fig4: scaling data missing")
        return
    if not {"scheme", "domains", "lsdb_entries", "fib_entries"}.issubset(df.columns):
        warn("skip fig4: missing columns")
        return
    df["scheme"] = df["scheme"].astype(str).map(_scheme_key)
    rows = []
    group_cols = ["scheme", "domains"]
    for (scheme, domains), g in df.groupby(group_cols):
        d = float(domains)
        l_m, l_ci, _ = _mean_ci(g["lsdb_entries"])
        f_m, f_ci, _ = _mean_ci(g["fib_entries"])
        th_m, _, _ = _mean_ci(g["lsdb_theory"]) if "lsdb_theory" in g.columns else (np.nan, np.nan, 0)
        rows.append({
            "scheme": scheme,
            "domains": d,
            "lsdb_entries": l_m,
            "lsdb_ci": l_ci,
            "fib_entries": f_m,
            "fib_ci": f_ci,
            "lsdb_theory": th_m,
        })
    agg = pd.DataFrame(rows).sort_values(["scheme", "domains"])
    if agg.empty:
        warn("skip fig4: empty aggregate")
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.8, 4.2))
    used_schemes = []
    for s in MAIN_SCHEMES:
        sub = agg[agg["scheme"] == s].sort_values("domains")
        if sub.empty:
            continue
        st = STYLES[s]
        used_schemes.append(s)
        ax1.errorbar(sub["domains"], sub["lsdb_entries"], yerr=sub["lsdb_ci"], color=st["color"], marker=st["marker"],
                     linestyle=st["linestyle"], linewidth=st["linewidth"], capsize=2, label=st["label"])
        ax2.errorbar(sub["domains"], sub["fib_entries"], yerr=sub["fib_ci"], color=st["color"], marker=st["marker"],
                     linestyle=st["linestyle"], linewidth=st["linewidth"], capsize=2, label=st["label"])

    ir = agg[agg["scheme"] == "iroute"].sort_values("domains")
    if not ir.empty and ir["lsdb_theory"].notna().any():
        ax1.plot(ir["domains"], ir["lsdb_theory"], color="#111111", linestyle="--", marker="x",
                 linewidth=1.5, label="iRoute Theory (MxD)")
    ax1.set_xlabel("Number of Domains")
    ax1.set_ylabel("Entries")
    ax1.set_title("(a) LSDB/Control State Scaling")
    ax1.legend(loc="upper left", frameon=True)

    ax2.set_xlabel("Number of Domains")
    ax2.set_ylabel("Entries")
    ax2.set_title("(b) FIB State Scaling (Scheme Comparison)")
    ax2.legend(loc="upper left", frameon=True)
    out_name = "fig4_state_scaling.pdf"
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, out_name))
    plt.close()
    topo = "unknown"
    if "topology" in df.columns and not df["topology"].empty:
        topo_vals = sorted(set(df["topology"].astype(str).tolist()))
        topo = ",".join(topo_vals[:3])
    fig_index[out_name] = {
        "source": path,
        "schemes": used_schemes,
        "domains": sorted(set(agg["domains"].astype(int).tolist())),
        "topology": topo,
    }


def _parse_domain_id(token):
    if token is None:
        return None
    t = str(token).strip()
    if not t:
        return None
    t = t.replace("/domain", "").replace("domain", "").strip()
    try:
        return int(float(t))
    except Exception:
        return None


def _contains_domain(series: pd.Series, did: int):
    def _hit(v):
        if pd.isna(v):
            return False
        for tok in str(v).split(";"):
            x = _parse_domain_id(tok)
            if x is not None and x == did:
                return True
        return False
    return series.apply(_hit)


def _extract_note_value(notes: str, key: str) -> str:
    for token in str(notes or "").split(";"):
        token = token.strip()
        if token.startswith(f"{key}="):
            return token.split("=", 1)[1].strip()
    return ""


def _affected_mask(qdf: pd.DataFrame, scenario: str, target: str, notes: str = ""):
    if qdf is None or qdf.empty:
        return pd.Series([], dtype=bool)
    sc = str(scenario).strip().lower()
    tgt = str(target).strip().lower()
    if "gt_domain" not in qdf.columns:
        return pd.Series(np.ones(len(qdf), dtype=bool), index=qdf.index)
    if sc == "domain-fail":
        did = _parse_domain_id(tgt)
        if did is None:
            return pd.Series(np.ones(len(qdf), dtype=bool), index=qdf.index)
        return _contains_domain(qdf["gt_domain"], did)
    if sc == "link-fail":
        parts = [p for p in tgt.split("-") if p]
        dom_ids = []
        for p in parts:
            did = _parse_domain_id(p)
            if did is not None:
                dom_ids.append(did)
        if not dom_ids:
            return pd.Series(np.ones(len(qdf), dtype=bool), index=qdf.index)
        mask = pd.Series(np.zeros(len(qdf), dtype=bool), index=qdf.index)
        for did in dom_ids:
            mask = mask | _contains_domain(qdf["gt_domain"], did)
        return mask
    if sc == "churn":
        note_value = _extract_note_value(notes, "affected_domains")
        if note_value:
            dom_ids = []
            for token in note_value.split("|"):
                did = _parse_domain_id(token)
                if did is not None:
                    dom_ids.append(did)
            if dom_ids:
                mask = pd.Series(np.zeros(len(qdf), dtype=bool), index=qdf.index)
                for did in dom_ids:
                    mask = mask | _contains_domain(qdf["gt_domain"], did)
                return mask
    # fallback to global set when the event does not expose explicit affected domains
    return pd.Series(np.ones(len(qdf), dtype=bool), index=qdf.index)


def _curve_min_t95(curve: pd.DataFrame, fail_time: float, hold_bins: int = 3):
    if curve is None or curve.empty or "sec" not in curve.columns or "smooth" not in curve.columns:
        return None, None, None
    use = curve.sort_values("sec").copy()
    pre = use[use["sec"] < fail_time]["smooth"]
    post = use[use["sec"] >= fail_time]["smooth"]
    if post.empty:
        return None, None, None
    baseline = float(pre.mean()) if not pre.empty else float(post.iloc[0])
    min_success = float(post.min())
    if baseline <= 0:
        return min_success, None, baseline
    threshold = 0.95 * baseline
    vals = post.reset_index(drop=True)
    secs = use[use["sec"] >= fail_time]["sec"].reset_index(drop=True)
    t95 = None
    for i in range(len(vals)):
        window = vals.iloc[i:i + hold_bins]
        if len(window) < hold_bins:
            break
        if (window >= threshold).all():
            t95 = int(round(float(secs.iloc[i])))
            break
    return min_success, t95, baseline


def _recovery_curve(
    qdf: pd.DataFrame,
    mask: pd.Series | None = None,
    metric: str = "success",
    recovery_bin_queries: int = RECOVERY_BIN_QUERIES,
):
    if qdf.empty or "t_send_disc" not in qdf.columns:
        return pd.DataFrame()
    use = _measurement_subset(qdf)
    if use.empty:
        use = qdf.copy()
    if mask is not None and len(mask) == len(qdf):
        m = mask.reindex(use.index).fillna(False)
        use = use[m]
    if use.empty:
        return pd.DataFrame()
    t = pd.to_numeric(use["t_send_disc"], errors="coerce").fillna(0.0) / 1000.0
    metric_key = str(metric).strip().lower()
    if metric_key == "domain" and "domain_hit" in use.columns:
        h = pd.to_numeric(use["domain_hit"], errors="coerce").fillna(0.0)
    elif "is_success" in use.columns:
        h = pd.to_numeric(use["is_success"], errors="coerce").fillna(0.0)
    elif "domain_hit" in use.columns:
        h = pd.to_numeric(use["domain_hit"], errors="coerce").fillna(0.0)
    else:
        h = pd.Series(np.zeros(len(use), dtype=float))
    tmp = pd.DataFrame({"sec": t, "hit": h}).sort_values("sec").reset_index(drop=True)
    if recovery_bin_queries > 0 and len(tmp) >= recovery_bin_queries:
        tmp["bin"] = (tmp.index // recovery_bin_queries).astype(int)
        out = tmp.groupby("bin", as_index=False).agg(sec=("sec", "median"), hit=("hit", "mean"), n=("hit", "size"))
        out["smooth"] = out["hit"].rolling(window=3, min_periods=1).mean()
        return out
    sec = tmp.assign(sec=tmp["sec"].astype(int)).groupby("sec", as_index=False).agg(hit=("hit", "mean"), n=("hit", "size")).sort_values("sec")
    sec["smooth"] = sec["hit"].rolling(window=10, min_periods=1).mean()
    return sec


def plot_failure_recovery(
    fail_dir: str,
    output_dir: str,
    fig_index: dict,
    fail_time: int,
    recovery_bin_queries: int = RECOVERY_BIN_QUERIES,
):
    rec_csv = os.path.join(fail_dir, "recovery_summary.csv")
    rec_df = pd.read_csv(rec_csv) if os.path.exists(rec_csv) else pd.DataFrame()
    scenarios = ["churn", "link-fail", "domain-fail"]
    topo_label = "unknown"
    policy_label = "unknown"
    if not rec_df.empty:
        if "topology" in rec_df.columns:
            tops = sorted(set(rec_df["topology"].astype(str).tolist()))
            if tops:
                topo_label = tops[0]
        if "failure_policy" in rec_df.columns:
            pols = sorted(set(rec_df["failure_policy"].astype(str).tolist()))
            if pols:
                policy_label = pols[0]
    for sc in scenarios:
        run_dirs = sorted([d for d in glob(os.path.join(fail_dir, f"*-{sc}*")) if os.path.isdir(d)])
        if not run_dirs:
            warn(f"fig5_{sc}: no runs found")
            continue
        metric_name = "domain"
        curves = {}
        for rd in run_dirs:
            base = os.path.basename(rd)
            scheme = _scheme_key(base.split(f"-{sc}")[0])
            qlog = os.path.join(rd, "query_log.csv")
            if not os.path.exists(qlog):
                continue
            qdf = pd.read_csv(qlog)
            target = ""
            notes = ""
            fsanity = os.path.join(rd, "failure_sanity.csv")
            if os.path.exists(fsanity):
                try:
                    fr = pd.read_csv(fsanity)
                    if not fr.empty:
                        target = str(fr.iloc[-1].get("target", "")).strip()
                        notes = str(fr.iloc[-1].get("notes", "")).strip()
                except Exception:
                    target = ""
                    notes = ""
            global_curve = _recovery_curve(qdf, metric=metric_name, recovery_bin_queries=recovery_bin_queries)
            if global_curve.empty:
                continue
            mask = _affected_mask(qdf, sc, target, notes)
            affected_curve = _recovery_curve(
                qdf,
                mask=mask,
                metric=metric_name,
                recovery_bin_queries=recovery_bin_queries,
            )
            n_affected = int(mask.sum()) if len(mask) == len(qdf) else 0
            curves.setdefault(scheme, []).append({
                "run": rd,
                "global": global_curve,
                "affected": affected_curve,
                "target": target,
                "notes": notes,
                "n_affected": n_affected,
            })
        if not curves:
            warn(f"fig5_{sc}: no valid curves")
            continue

        def merge_curves(items, key):
            merged = None
            used_cnt = 0
            for idx, row in enumerate(items):
                c = row.get(key)
                if c is None or c.empty:
                    continue
                part = c[["sec", "smooth"]].rename(columns={"smooth": f"v{idx}"})
                if merged is None:
                    merged = part
                else:
                    merged = merged.merge(part, on="sec", how="outer")
                used_cnt += 1
            if merged is None:
                return pd.DataFrame(), 0
            merged = merged.sort_values("sec").ffill().bfill()
            vals = [c for c in merged.columns if c.startswith("v")]
            merged["mean"] = merged[vals].mean(axis=1)
            merged["smooth"] = merged["mean"]
            return merged, used_cnt

        fig, ax = plt.subplots(figsize=(7.8, 4.6))
        used = {}
        global_min = 1.0
        for s in MAIN_SCHEMES:
            if s not in curves:
                continue
            runs = curves[s]
            merged_global, used_global = merge_curves(runs, "global")
            if merged_global.empty:
                continue
            st = STYLES[s]
            min_g, t95_g, baseline_g = _curve_min_t95(merged_global, float(fail_time))
            if min_g is not None:
                global_min = min(global_min, float(min_g))
            label_global = st["label"]
            if min_g is not None:
                label_global += f" global(min={min_g:.2f}"
                if t95_g is not None:
                    label_global += f", t95={t95_g}s"
                label_global += ")"
            ax.plot(merged_global["sec"], merged_global["mean"], color=st["color"], linestyle=st["linestyle"],
                    linewidth=st["linewidth"], label=label_global)

            merged_aff, used_aff = merge_curves(runs, "affected")
            min_a = None
            t95_a = None
            baseline_a = None
            if not merged_aff.empty:
                min_a, t95_a, baseline_a = _curve_min_t95(merged_aff, float(fail_time))
                if min_a is not None:
                    global_min = min(global_min, float(min_a))
                label_aff = f"{st['label']} affected"
                if min_a is not None:
                    label_aff += f"(min={min_a:.2f}"
                    if t95_a is not None:
                        label_aff += f", t95={t95_a}s"
                    label_aff += ")"
                ax.plot(merged_aff["sec"], merged_aff["mean"], color=st["color"], linestyle="--",
                        linewidth=max(1.2, st["linewidth"] - 0.2), alpha=0.95, label=label_aff)

            failure_hash = None
            if not rec_df.empty:
                sub = rec_df[(rec_df["scenario"] == sc) & (rec_df["scheme"].astype(str).map(_scheme_key) == s)]
                if not sub.empty:
                    hash_col = "hash_success" if "hash_success" in sub.columns else "hash_domain_hit"
                    hashes = sorted(set(sub[hash_col].astype(str).tolist()))
                    failure_hash = ",".join(hashes[:3])

            used[s] = {
                "runs": [r["run"] for r in runs],
                "targets": sorted(set([r["target"] for r in runs if r.get("target")])),
                "notes": sorted(set([r["notes"] for r in runs if r.get("notes")])),
                "n_runs_global": int(used_global),
                "n_runs_affected": int(used_aff),
                "n_affected_total": int(sum(r.get("n_affected", 0) for r in runs)),
                "global_min": min_g,
                "global_t95": t95_g,
                "global_baseline": baseline_g,
                "affected_min": min_a,
                "affected_t95": t95_a,
                "affected_baseline": baseline_a,
                "failure_hash": failure_hash,
            }

        if sc == "churn" and ("iroute" not in used or "flood" not in used):
            warn("fig5_churn missing iRoute or Flood")

        ax.axvline(fail_time, color="red", linestyle=":", linewidth=1.6, label=f"event t={fail_time}s")
        ax.text(fail_time + 0.5, 0.04, f"t={fail_time}s", color="red", fontsize=8)
        ax.set_xlabel("Simulation Time (s)")
        y_label = "Domain-Hit Rate" if metric_name == "domain" else "Success Rate"
        ax.set_ylabel(y_label)
        y_low = 0.0
        if np.isfinite(global_min) and global_min > 0.65:
            # Zoom when failures are mild to avoid visually flat curves.
            y_low = max(0.6, global_min - 0.12)
        ax.set_ylim(y_low, 1.05)
        ax.set_title(f"Recovery under {sc} ({topo_label} topology, solid=global, dashed=affected)")
        ax.legend(loc="lower right", frameon=True)
        out_name = f"fig5_recovery_{sc}.pdf"
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, out_name))
        plt.close()
        fig_index[out_name] = {
            "topology": topo_label,
            "failure_policy": policy_label,
            "metric": "domain_hit" if metric_name == "domain" else "is_success",
            **used
        }


def write_figure_index(output_dir: str, fig_index: dict):
    path = os.path.join(output_dir, "figure_index.md")
    lines = ["# Figure Index", "", f"Generated at: {dt.datetime.now().isoformat(timespec='seconds')}", ""]
    for k in sorted(fig_index.keys()):
        lines.append(f"## {k}")
        v = fig_index[k]
        if isinstance(v, dict):
            for kk, vv in v.items():
                lines.append(f"- {kk}: {vv}")
        else:
            lines.append(f"- {v}")
        lines.append("")
    lines.append("## Warnings")
    if WARNINGS:
        for w in WARNINGS:
            lines.append(f"- {w}")
    else:
        lines.append("- none")
    lines.append("")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    info(f"wrote {path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--acc-dir", required=True)
    parser.add_argument("--fail-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--load-csv", default="")
    parser.add_argument("--scaling-csv", default="")
    parser.add_argument("--fail-time", type=int, default=50)
    parser.add_argument("--recovery-bin-queries", type=int, default=RECOVERY_BIN_QUERIES)
    parser.add_argument("--cdf-xscale", choices=["linear", "log"], default="linear")
    args = parser.parse_args()

    set_style()
    os.makedirs(args.output, exist_ok=True)
    fig_index = {}

    acc_df = _load_accuracy_df(os.path.abspath(args.acc_dir))
    if acc_df.empty:
        warn("accuracy input empty")
    else:
        acc_df["scheme"] = acc_df["scheme"].astype(str).map(_scheme_key)
        plot_accuracy_overhead(acc_df, args.output, fig_index, include_exact=False, out_name="fig1_accuracy_overhead.pdf")
        plot_accuracy_overhead(acc_df, args.output, fig_index, include_exact=True, out_name="fig1_full_with_exact.pdf")

    refs = _infer_reference_runs(acc_df, os.path.abspath(args.acc_dir))
    plot_retrieval_cdf(refs, args.output, fig_index, include_exact=False, out_name="fig2_retrieval_cdf.pdf", xscale=args.cdf_xscale)
    plot_retrieval_cdf(refs, args.output, fig_index, include_exact=True, out_name="fig2_full_with_exact.pdf", xscale=args.cdf_xscale)
    plot_cache_hit_ratio(refs, args.output, fig_index)
    plot_exact_timeout_appendix(refs, args.output, fig_index)
    plot_latency_breakdown(refs, args.output, fig_index)

    base_dir = os.path.dirname(os.path.abspath(args.acc_dir))
    plot_hop_load(base_dir, args.load_csv, args.output, fig_index)
    plot_scaling(base_dir, args.scaling_csv, args.output, fig_index)
    plot_failure_recovery(
        os.path.abspath(args.fail_dir),
        args.output,
        fig_index,
        args.fail_time,
        args.recovery_bin_queries,
    )
    write_figure_index(args.output, fig_index)


if __name__ == "__main__":
    main()
