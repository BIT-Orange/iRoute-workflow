import argparse
import pandas as pd


def _normalize_columns(df: pd.DataFrame):
    """Support both legacy and current query_log schemas."""
    if {"Time", "IsHit"}.issubset(df.columns):
        out = pd.DataFrame()
        out["time_s"] = pd.to_numeric(df["Time"], errors="coerce")
        out["hit"] = pd.to_numeric(df["IsHit"], errors="coerce")
        out["qid"] = pd.to_numeric(df.get("QueryId", range(len(df))), errors="coerce")
        return out.dropna(subset=["time_s", "hit"])

    # Current ns-3/experiments query_log format
    if {"t_send_disc", "domain_hit"}.issubset(df.columns):
        out = pd.DataFrame()
        out["time_s"] = pd.to_numeric(df["t_send_disc"], errors="coerce") / 1000.0
        out["hit"] = pd.to_numeric(df["domain_hit"], errors="coerce")
        qid_col = "qid" if "qid" in df.columns else None
        if qid_col:
            out["qid"] = pd.to_numeric(df[qid_col], errors="coerce")
        else:
            out["qid"] = range(len(df))
        return out.dropna(subset=["time_s", "hit"])

    raise KeyError("Unsupported log schema. Need either [Time,IsHit] or [t_send_disc,domain_hit].")


def calculate_recovery(log_file, failure_time, window_size=1.0):
    """Calculate Recovery95.

    Recovery95 = time from failure until success rate returns to >=95% of pre-failure baseline.
    """
    df_raw = pd.read_csv(log_file)
    df = _normalize_columns(df_raw)

    if df.empty:
        print("No valid rows after normalization.")
        return

    df = df.sort_values("time_s")
    df["time_bin"] = (df["time_s"] // window_size) * window_size

    stats = df.groupby("time_bin").agg(
        SuccessRate=("hit", "mean"),
        Count=("qid", "count"),
    ).reset_index()

    pre = stats[(stats["time_bin"] >= failure_time - 10) & (stats["time_bin"] < failure_time)]
    baseline = float(pre["SuccessRate"].mean()) if not pre.empty else 0.0
    print(f"Pre-failure Baseline Success Rate: {baseline:.4f}")

    if baseline <= 0:
        print("Baseline is 0, cannot calculate recovery.")
        return

    target = baseline * 0.95
    print(f"Recovery Target (95%): {target:.4f}")

    post = stats[stats["time_bin"] >= failure_time]
    recovery_timestamp = None
    for _, row in post.iterrows():
        if float(row["SuccessRate"]) >= target:
            recovery_timestamp = float(row["time_bin"])
            break

    if recovery_timestamp is not None:
        recovery_time = recovery_timestamp - failure_time
        print(f"Recovered at: {recovery_timestamp:.1f}s")
        print(f"Recovery Time (Recovery95): {recovery_time:.2f}s")
    else:
        max_post = float(post["SuccessRate"].max()) if not post.empty else 0.0
        print("Did not recover to 95% baseline.")
        print(f"Max post-failure rate: {max_post:.4f}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Calculate Recovery95 metric.")
    parser.add_argument("--log", required=True, help="Path to query_log.csv")
    parser.add_argument("--failTime", required=True, type=float, help="Time of failure event")
    parser.add_argument("--window", default=1.0, type=float, help="Time bin window size (seconds)")
    args = parser.parse_args()

    calculate_recovery(args.log, args.failTime, args.window)
