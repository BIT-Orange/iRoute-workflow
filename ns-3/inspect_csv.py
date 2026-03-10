
import pandas as pd
import sys

try:
    df = pd.read_csv('results/iroute_v2_full_run_star_s42_r1_20260213_000831/run_query_log_iroute.csv')
    print(f"Columns: {len(df.columns)}")
    print(df.columns.tolist())
    print("\nFirst row values:")
    print(df.iloc[0].tolist())
except Exception as e:
    print(f"Error: {e}")
