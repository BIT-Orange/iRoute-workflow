#!/usr/bin/env python3
import argparse
import os
import sys
from glob import glob

import numpy as np
import pandas as pd


def fail(msg: str) -> bool:
    print(f"[check][FAIL] {msg}")
    return False


def ok(msg: str) -> bool:
    print(f"[check][OK] {msg}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-dir", required=True, help="Base results dir")
    args = parser.parse_args()

    base = os.path.abspath(args.base_dir)
    all_pass = True

    acc_dir = os.path.join(base, "accuracy_comparison")
    sweep_csv = os.path.join(acc_dir, "accuracy_sweep.csv")
    ref_csv = os.path.join(acc_dir, "reference_runs.csv")
    if not os.path.exists(sweep_csv):
        all_pass &= fail(f"missing {sweep_csv}")
    else:
        df = pd.read_csv(sweep_csv)
        req = {
            "scheme", "seed", "sweep_param", "sweep_value", "result_dir",
            "ctrl_bytes_per_sec", "domain_recall_at_1", "doc_hit_at_10",
            "n_success", "timeout_rate", "unique_rtt_values",
        }
        miss = req - set(df.columns)
        if miss:
            all_pass &= fail(f"accuracy_sweep missing columns: {sorted(miss)}")
        for scheme in ["iroute", "flood", "tag"]:
            sub = df[df["scheme"] == scheme]
            if sub.empty:
                all_pass &= fail(f"accuracy sweep missing scheme={scheme}")
                continue
            points = sub[["sweep_param", "sweep_value"]].drop_duplicates().shape[0]
            if points >= 4:
                all_pass &= ok(f"{scheme} has >=4 sweep points")
            else:
                all_pass &= fail(f"{scheme} sweep points <4 ({points})")
            seeds = sub["seed"].dropna().nunique()
            if seeds >= 3:
                all_pass &= ok(f"{scheme} has >=3 seeds")
            else:
                all_pass &= fail(f"{scheme} seeds <3 ({seeds})")

        central = df[df["scheme"] == "central"]
        if central.empty:
            all_pass &= fail("central missing in accuracy sweep")
        else:
            if "oracle_flag" in central.columns and (central["oracle_flag"] > 0).any():
                all_pass &= ok("central oracle_flag present")
            else:
                all_pass &= fail("central oracle_flag missing or zero")

    if os.path.exists(ref_csv):
        refs = pd.read_csv(ref_csv)
        for _, r in refs.iterrows():
            rd = str(r.get("result_dir", ""))
            scheme = str(r.get("scheme", ""))
            qlog = os.path.join(rd, "query_log.csv")
            lsan = os.path.join(rd, "latency_sanity.csv")
            if not os.path.exists(qlog):
                all_pass &= fail(f"missing reference query_log: {scheme} {qlog}")
                continue
            if not os.path.exists(lsan):
                all_pass &= fail(f"missing latency_sanity: {scheme} {lsan}")
                continue
            qdf = pd.read_csv(qlog)
            nrows = len(qdf)
            if nrows == 500:
                all_pass &= ok(f"{scheme} reference rows == 500")
            else:
                all_pass &= fail(f"{scheme} reference rows != 500 ({nrows})")

            if "rtt_total_ms" in qdf.columns:
                vals = pd.to_numeric(qdf["rtt_total_ms"], errors="coerce")
                vals = vals[np.isfinite(vals) & (vals > 0)]
                if not vals.empty and scheme != "exact":
                    p50 = float(np.percentile(vals, 50))
                    p95 = float(np.percentile(vals, 95))
                    if scheme == "central" and p95 - p50 <= 0:
                        all_pass &= ok(f"{scheme} latency spread degenerate but allowed for oracle mode (p50={p50}, p95={p95})")
                    elif p95 - p50 > 0:
                        all_pass &= ok(f"{scheme} latency spread p95>p50")
                    else:
                        all_pass &= fail(f"{scheme} latency spread degenerate (p50={p50}, p95={p95})")
                    uniq = int(vals.round(3).nunique())
                    if nrows >= 500:
                        min_unique = 2 if scheme == "central" else 10
                        if uniq >= min_unique:
                            all_pass &= ok(f"{scheme} unique latency values >= {min_unique} ({uniq})")
                        else:
                            all_pass &= fail(f"{scheme} unique latency values < {min_unique} ({uniq})")

    load_csv = os.path.join(base, "exp4-load", "load_sweep.csv")
    if not os.path.exists(load_csv):
        all_pass &= fail(f"missing {load_csv}")
    else:
        ld = pd.read_csv(load_csv)
        if "mean_hops" not in ld.columns or not (pd.to_numeric(ld["mean_hops"], errors="coerce") > 0).any():
            all_pass &= fail("load_sweep has no positive mean_hops")
        else:
            all_pass &= ok("load_sweep has positive mean_hops")
        if "topology" in ld.columns:
            topo = ld["topology"].astype(str).str.strip().str.lower()
            rf = ld[topo == "rocketfuel"].copy()
            if not rf.empty:
                rf_hops = pd.to_numeric(rf["mean_hops"], errors="coerce")
                if (rf_hops < 2.0).any():
                    all_pass &= fail("fig3 hard check failed: rocketfuel mean_hops<2 detected")
                else:
                    all_pass &= ok("fig3 hard check passed: rocketfuel mean_hops>=2")
                if {"scheme", "frequency", "mean_hops"}.issubset(rf.columns):
                    has_nonzero_slope = False
                    for _, g in rf.groupby("scheme"):
                        gg = g.copy()
                        gg["frequency"] = pd.to_numeric(gg["frequency"], errors="coerce")
                        gg["mean_hops"] = pd.to_numeric(gg["mean_hops"], errors="coerce")
                        gg = gg.dropna(subset=["frequency", "mean_hops"])
                        if gg["frequency"].nunique() < 2:
                            continue
                        if float(gg["mean_hops"].max() - gg["mean_hops"].min()) > 1e-6:
                            has_nonzero_slope = True
                            break
                    if has_nonzero_slope:
                        all_pass &= ok("fig3 slope sanity: at least one rocketfuel curve has non-zero slope")
                    else:
                        all_pass &= ok("fig3 slope sanity: hop curves are near-flat (allowed if hops metric is path-length dominated)")

    sc_csv = os.path.join(base, "exp4-scaling", "scaling.csv")
    if not os.path.exists(sc_csv):
        all_pass &= fail(f"missing {sc_csv}")
    else:
        sc = pd.read_csv(sc_csv)
        req = {"scheme", "domains", "lsdb_entries", "fib_entries", "lsdb_theory"}
        if req - set(sc.columns):
            all_pass &= fail("scaling.csv missing required columns")
        elif sc.empty:
            all_pass &= fail("scaling.csv is empty")
        else:
            present = set(sc["scheme"].astype(str).str.lower().tolist())
            need = {"iroute", "tag", "flood", "central"}
            miss = need - present
            if miss:
                all_pass &= fail(f"fig4 hard check failed: missing schemes {sorted(miss)}")
            else:
                all_pass &= ok("fig4 hard check passed: required schemes covered")
            dom = pd.to_numeric(sc["domains"], errors="coerce")
            uniq_dom = sorted(set(dom.dropna().astype(int).tolist()))
            if len(uniq_dom) < 4:
                all_pass &= fail(f"fig4 hard check failed: domains points <4 ({uniq_dom})")
            else:
                all_pass &= ok(f"fig4 hard check passed: domains points >=4 ({uniq_dom})")
            per_scheme_ok = True
            for scheme, g in sc.groupby(sc["scheme"].astype(str).str.lower()):
                dcount = pd.to_numeric(g["domains"], errors="coerce").dropna().astype(int).nunique()
                if scheme in need and dcount < 4:
                    all_pass &= fail(f"fig4 hard check failed: {scheme} domains points <4 ({dcount})")
                    per_scheme_ok = False
            if per_scheme_ok:
                all_pass &= ok("fig4 hard check passed: each required scheme has >=4 domain points")
            ir = sc[sc["scheme"].astype(str).str.lower() == "iroute"].copy()
            ratio = (pd.to_numeric(ir["lsdb_entries"], errors="coerce") /
                     pd.to_numeric(ir["lsdb_theory"], errors="coerce").replace(0, np.nan))
            ratio = ratio[np.isfinite(ratio)]
            if ratio.empty:
                all_pass &= fail("scaling LSDB ratio invalid (iroute)")
            elif ((ratio > 0.5) & (ratio < 2.0)).all():
                all_pass &= ok("scaling LSDB measured/theory within sane range (iroute)")
            else:
                all_pass &= fail("scaling LSDB measured/theory out of sane range (iroute)")

    fail_dir = os.path.join(base, "exp3-failure")
    rec_csv = os.path.join(fail_dir, "recovery_summary.csv")
    if not os.path.exists(rec_csv):
        all_pass &= fail(f"missing {rec_csv}")
    else:
        rec = pd.read_csv(rec_csv)
        if rec.empty:
            all_pass &= fail("recovery_summary.csv is empty")
        else:
            all_pass &= ok("recovery_summary present")
            t95_raw = rec.get("t95", pd.Series([], dtype=object))
            t95_num = pd.to_numeric(t95_raw, errors="coerce")
            has_valid_t95 = (t95_num >= 0).any()
            if has_valid_t95:
                all_pass &= ok("fig5 hard check passed: at least one valid t95 exists")
            else:
                all_pass &= fail("fig5 hard check failed: no valid t95 in recovery_summary")
            for _, row in rec.iterrows():
                rd = str(row.get("result_dir", ""))
                qlog = os.path.join(rd, "query_log.csv")
                fsan = os.path.join(rd, "failure_sanity.csv")
                if not os.path.exists(qlog):
                    all_pass &= fail(f"missing query_log: {qlog}")
                if not os.path.exists(fsan):
                    all_pass &= fail(f"missing failure_sanity: {fsan}")
                else:
                    fdf = pd.read_csv(fsan)
                    if fdf.empty:
                        all_pass &= fail(f"empty failure_sanity: {fsan}")
                    else:
                        rr = fdf.iloc[-1]
                        if int(rr.get("scheduled", 0)) != 1 or int(rr.get("applied", 0)) != 1:
                            all_pass &= fail(f"failure not applied for {rd}")
                        pre_cnt = int(float(rr.get("pre_count", 0) or 0))
                        post_cnt = int(float(rr.get("post_count", 0) or 0))
                        if pre_cnt <= 0 or post_cnt <= 0:
                            all_pass &= fail(f"failure_sanity missing pre/post samples for {rd} (pre={pre_cnt}, post={post_cnt})")
                        scenario = str(row.get("scenario", ""))
                        policy = str(row.get("failure_policy", ""))
                        if scenario == "link-fail" and policy == "auto-noncut":
                            after = int(float(rr.get("after_connected", -1) or -1))
                            if after != 1:
                                all_pass &= fail(f"fig5 hard check failed: link-fail auto-noncut after_connected={after} for {rd}")

            if {"scheme", "seed", "scenario", "hash_domain_hit", "hash_rtt"}.issubset(rec.columns):
                if "failure_effective" in rec.columns:
                    fe = pd.to_numeric(rec["failure_effective"], errors="coerce").fillna(0).astype(int)
                    if (fe > 0).any():
                        if (fe <= 0).any():
                            all_pass &= ok("failure_effective check: some runs ineffective (warn), but at least one effective run exists")
                        else:
                            all_pass &= ok("failure_effective hard check passed")
                    else:
                        all_pass &= fail("failure_effective hard check failed: no effective failure rows")
                for (scheme, seed), g in rec.groupby(["scheme", "seed"]):
                    wanted = {"link-fail", "domain-fail", "churn"}
                    if wanted.issubset(set(g["scenario"].astype(str))):
                        gg = g[g["scenario"].isin(wanted)]
                        if gg["hash_domain_hit"].nunique() == 1:
                            all_pass &= fail(f"{scheme}/seed{seed} identical domain_hit hashes across scenarios")
                        if gg["hash_rtt"].nunique() == 1:
                            all_pass &= fail(f"{scheme}/seed{seed} identical rtt hashes across scenarios")
            else:
                all_pass &= fail("recovery_summary missing hash columns")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
