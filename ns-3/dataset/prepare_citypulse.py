#!/usr/bin/env python3
"""
prepare_citypulse.py – CityPulse (Aarhus) data preprocessing pipeline
for ndnSIM iRoute experiments.

Usage:
    python prepare_citypulse.py \
        --input ~/ndnSIM/ns-3/dataset \
        --out ~/ndnSIM/ns-3/dataset/dataset_clean \
        --k_domains 8 --seed 20260211 --n_queries 500 \
        --time_window_minutes 60 \
        --use_modalities parking,traffic,pollution \
        --traffic_batches aug_sep_2014,oct_nov_2014 \
        --max_objects_per_modality 0
"""

from __future__ import annotations

import argparse
import csv
import gc
import io
import json
import logging
import os
import sys
import tarfile
import zipfile
from collections import defaultdict, Counter
from dataclasses import dataclass, field
from math import radians, sin, cos, sqrt, atan2
from pathlib import Path
from typing import Any, Optional

import numpy as np
import pandas as pd
from sklearn.cluster import KMeans
from scipy.spatial import KDTree
from tqdm import tqdm

# ---------------------------------------------------------------------------
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s",
                    handlers=[logging.StreamHandler(sys.stdout)])
log = logging.getLogger("citypulse")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def haversine_m(lat1, lon1, lat2, lon2):
    R = 6_371_000.0
    la1, lo1, la2, lo2 = map(radians, (lat1, lon1, lat2, lon2))
    dlat, dlon = la2 - la1, lo2 - lo1
    a = sin(dlat / 2) ** 2 + cos(la1) * cos(la2) * sin(dlon / 2) ** 2
    return R * 2 * atan2(sqrt(a), sqrt(1 - a))

def valid_coord(lat, lon):
    return -90 <= lat <= 90 and -180 <= lon <= 180

class NpEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, (np.integer,)):
            return int(o)
        if isinstance(o, (np.floating,)):
            return float(o)
        if isinstance(o, np.ndarray):
            return o.tolist()
        if isinstance(o, pd.Timestamp):
            return o.isoformat()
        return super().default(o)


@dataclass
class ObjectRecord:
    object_id: str
    modality: str
    city: str = "aarhus"
    lat: float = 0.0
    lon: float = 0.0
    domain_id: int = -1
    ndn_name: str = ""
    text_desc: str = ""
    meta: dict = field(default_factory=dict)


# ---------------------------------------------------------------------------
# 1. Extraction
# ---------------------------------------------------------------------------

def extract_archives(input_dir: Path, out_dir: Path, traffic_batches):
    raw = out_dir / "raw_extracted"
    raw.mkdir(parents=True, exist_ok=True)
    results = {}

    if "aug_sep_2014" in traffic_batches:
        tgz = input_dir / "citypulse_traffic_raw_data_aarhus_aug_sep_2014.tar.gz"
        dest = raw / "traffic_aug_sep"
        if not dest.exists():
            log.info("Extracting %s …", tgz.name)
            dest.mkdir(parents=True, exist_ok=True)
            with tarfile.open(tgz, "r:gz") as tf:
                tf.extractall(dest)
        results["traffic_aug_sep"] = dest

    if "oct_nov_2014" in traffic_batches:
        zp = input_dir / "citypulse_traffic_raw_data_aarhus_oct_nov_2014.zip"
        dest = raw / "traffic_oct_nov"
        if not dest.exists():
            log.info("Extracting %s …", zp.name)
            dest.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(zp, "r") as zf:
                zf.extractall(dest)
        results["traffic_oct_nov"] = dest

    pgz = input_dir / "citypulse_pollution_csv_data_aarhus_aug_oct_2014.tar.gz"
    dest = raw / "pollution"
    if not dest.exists():
        log.info("Extracting %s …", pgz.name)
        dest.mkdir(parents=True, exist_ok=True)
        with tarfile.open(pgz, "r:gz") as tf:
            tf.extractall(dest)
    results["pollution"] = dest

    return results


# ---------------------------------------------------------------------------
# 2. Parking
# ---------------------------------------------------------------------------

def clean_parking(input_dir: Path):
    addr = pd.read_csv(input_dir / "aarhus_parking_address.csv")
    addr.columns = [c.strip() for c in addr.columns]
    addr_map = {}
    for _, r in addr.iterrows():
        addr_map[r["garagecode"]] = {
            "lat": float(r["latitude"]), "lon": float(r["longitude"]),
            "street": str(r.get("street", "")), "housenumber": str(r.get("housenumber", "")),
        }

    raw = pd.read_csv(input_dir / "aarhus_parking.csv")
    raw.columns = [c.strip() for c in raw.columns]
    garages = raw["garagecode"].unique().tolist()

    objects = []
    bad_coords = []
    for gc in garages:
        info = addr_map.get(gc)
        if info is None:
            bad_coords.append(gc); continue
        lat, lon = info["lat"], info["lon"]
        if not valid_coord(lat, lon):
            bad_coords.append(gc); continue
        ts = int(raw.loc[raw["garagecode"] == gc, "totalspaces"].iloc[0])
        objects.append(ObjectRecord(
            object_id=f"p_{gc}", modality="parking", lat=lat, lon=lon,
            text_desc=f"parking lot {gc} at ({lat:.5f},{lon:.5f}) capacity {ts} in Aarhus",
            meta={"garagecode": gc, "totalspaces": ts, "street": info["street"]},
        ))

    raw["ts"] = pd.to_datetime(raw["updatetime"], errors="coerce")
    bad_ts = int(raw["ts"].isna().sum())
    raw = raw.dropna(subset=["ts"])
    raw["object_id"] = "p_" + raw["garagecode"]
    valid_ids = {o.object_id for o in objects}
    raw = raw[raw["object_id"].isin(valid_ids)]

    obs = raw[["ts", "object_id", "vehiclecount", "totalspaces"]].copy()
    obs["availability"] = obs["totalspaces"] - obs["vehiclecount"]
    obs = obs.sort_values("ts").drop_duplicates(subset=["object_id", "ts"], keep="last")

    stats = {
        "modality": "parking", "total_garages": len(garages),
        "valid_objects": len(objects), "bad_coord_garages": bad_coords,
        "rows_bad_ts": bad_ts, "rows_after_clean": len(obs),
        "time_min": str(obs["ts"].min()), "time_max": str(obs["ts"].max()),
        "avg_records_per_obj": round(len(obs) / max(len(objects), 1), 1),
    }
    return objects, obs, stats


# ---------------------------------------------------------------------------
# 3. Traffic  (memory-efficient: stream CSVs, write parquet in chunks)
# ---------------------------------------------------------------------------

def _find_traffic_csvs(dirs):
    csvs = []
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if f.startswith("trafficData") and f.endswith(".csv"):
                    csvs.append(os.path.join(root, f))
    return csvs


def _load_traffic_metadata(input_dir: Path):
    meta = pd.read_csv(input_dir / "trafficMetaData.csv")
    meta.columns = [c.strip() for c in meta.columns]
    return meta


def clean_traffic(input_dir, extracted, max_objects, out_dir):
    """Clean traffic data. Write observations to parquet directly in batches.
    Return (objects, summary_df_for_queries, stats).
    summary_df_for_queries has per-(object_id, hour) aggregated stats for query gen.
    """
    meta = _load_traffic_metadata(input_dir)
    objects = []
    bad_coords = []

    for _, r in meta.iterrows():
        rid = int(r["REPORT_ID"])
        lat = (float(r["POINT_1_LAT"]) + float(r["POINT_2_LAT"])) / 2
        lon = (float(r["POINT_1_LNG"]) + float(r["POINT_2_LNG"])) / 2
        if not valid_coord(lat, lon):
            bad_coords.append(str(rid)); continue
        road_type = str(r.get("ROAD_TYPE", "UNKNOWN"))
        dist = float(r.get("DISTANCE_IN_METERS", 0))
        rname = str(r.get("REPORT_NAME", ""))
        objects.append(ObjectRecord(
            object_id=f"t_{rid}", modality="traffic", lat=lat, lon=lon,
            text_desc=f"traffic sensor pair {rname} on {road_type} road ({lat:.5f},{lon:.5f}) distance {dist:.0f}m in Aarhus",
            meta={"report_id": rid, "road_type": road_type, "distance_m": dist, "report_name": rname,
                  "point1": {"lat": float(r["POINT_1_LAT"]), "lon": float(r["POINT_1_LNG"])},
                  "point2": {"lat": float(r["POINT_2_LAT"]), "lon": float(r["POINT_2_LNG"])}},
        ))

    if max_objects > 0 and len(objects) > max_objects:
        rng = np.random.RandomState(42)
        idx = rng.choice(len(objects), max_objects, replace=False)
        objects = [objects[i] for i in sorted(idx)]

    valid_rids = {int(o.meta["report_id"]) for o in objects}
    rid_to_oid = {int(o.meta["report_id"]): o.object_id for o in objects}

    # Stream CSVs in batches, write parquet chunks
    dirs = [extracted[k] for k in ("traffic_aug_sep", "traffic_oct_nov") if k in extracted]
    csv_files = _find_traffic_csvs(dirs)
    log.info("Found %d traffic CSV files to process", len(csv_files))

    parquet_path = out_dir / "observations_traffic.parquet"
    total_rows = 0
    bad_ts_total = 0
    ts_min, ts_max = None, None

    BATCH_SIZE = 50  # CSVs per batch
    writer = None

    for batch_start in range(0, len(csv_files), BATCH_SIZE):
        batch_files = csv_files[batch_start : batch_start + BATCH_SIZE]
        frames = []
        for fp in batch_files:
            try:
                df = pd.read_csv(fp)
                df.columns = [c.strip() for c in df.columns]
                if "REPORT_ID" not in df.columns:
                    continue
                df = df[df["REPORT_ID"].isin(valid_rids)]
                if df.empty:
                    continue
                df["ts"] = pd.to_datetime(df["TIMESTAMP"], errors="coerce")
                bad_ts_total += int(df["ts"].isna().sum())
                df = df.dropna(subset=["ts"])
                df["object_id"] = df["REPORT_ID"].map(rid_to_oid)
                cols = ["ts", "object_id"]
                for c in ["avgSpeed", "avgMeasuredTime", "vehicleCount"]:
                    if c in df.columns:
                        cols.append(c)
                frames.append(df[cols])
            except Exception as e:
                log.warning("Skip %s: %s", fp, e)

        if not frames:
            continue

        chunk = pd.concat(frames, ignore_index=True)
        rename_map = {"avgSpeed": "avg_speed", "avgMeasuredTime": "avg_measured_time",
                      "vehicleCount": "vehicle_count"}
        chunk = chunk.rename(columns={k: v for k, v in rename_map.items() if k in chunk.columns})
        chunk = chunk.sort_values("ts").drop_duplicates(subset=["object_id", "ts"], keep="last")
        total_rows += len(chunk)

        cmin, cmax = chunk["ts"].min(), chunk["ts"].max()
        ts_min = cmin if ts_min is None else min(ts_min, cmin)
        ts_max = cmax if ts_max is None else max(ts_max, cmax)

        # Write as parquet (append via pyarrow)
        import pyarrow as pa
        import pyarrow.parquet as pq
        table = pa.Table.from_pandas(chunk, preserve_index=False)
        if writer is None:
            writer = pq.ParquetWriter(str(parquet_path), table.schema)
        writer.write_table(table)

        del chunk, frames
        gc.collect()

    if writer is not None:
        writer.close()

    # Build lightweight summary for query generation:
    # Per (object_id, date_hour), aggregate avg_speed
    summary = None
    if parquet_path.exists():
        import pyarrow.parquet as pq
        table = pq.read_table(str(parquet_path), columns=["ts", "object_id", "avg_speed"])
        df = table.to_pandas()
        df["hour"] = df["ts"].dt.floor("h")
        summary = df.groupby(["object_id", "hour"]).agg(
            avg_speed_mean=("avg_speed", "mean"),
            n_records=("avg_speed", "count"),
        ).reset_index()
        del df
        gc.collect()

    stats = {
        "modality": "traffic", "total_meta_rows": len(meta),
        "valid_objects": len(objects), "bad_coord_rids": bad_coords,
        "rows_bad_ts": bad_ts_total, "rows_after_clean": total_rows,
        "time_min": str(ts_min), "time_max": str(ts_max),
        "avg_records_per_obj": round(total_rows / max(len(objects), 1), 1),
    }
    return objects, summary, stats


# ---------------------------------------------------------------------------
# 4. Pollution (memory-efficient)
# ---------------------------------------------------------------------------

POLLUTANTS = ["ozone", "particullate_matter", "carbon_monoxide", "sulfure_dioxide", "nitrogen_dioxide"]


def _find_pollution_csvs(dirs):
    csvs = []
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if f.startswith("pollutionData") and f.endswith(".csv"):
                    csvs.append(os.path.join(root, f))
    return csvs


def clean_pollution(input_dir, extracted, traffic_meta, max_objects, out_dir):
    objects = []
    bad_coords = []

    for _, r in traffic_meta.iterrows():
        rid = int(r["REPORT_ID"])
        lat = (float(r["POINT_1_LAT"]) + float(r["POINT_2_LAT"])) / 2
        lon = (float(r["POINT_1_LNG"]) + float(r["POINT_2_LNG"])) / 2
        if not valid_coord(lat, lon):
            bad_coords.append(str(rid)); continue
        objects.append(ObjectRecord(
            object_id=f"pol_{rid}", modality="pollution", lat=lat, lon=lon,
            text_desc=f"pollution sensor at ({lat:.5f},{lon:.5f}) measuring ozone PM10 CO SO2 NO2 in Aarhus",
            meta={"report_id": rid, "pollutant_list": POLLUTANTS},
        ))

    if max_objects > 0 and len(objects) > max_objects:
        rng = np.random.RandomState(42)
        idx = rng.choice(len(objects), max_objects, replace=False)
        objects = [objects[i] for i in sorted(idx)]

    valid_rids = {int(o.meta["report_id"]) for o in objects}
    rid_to_oid = {int(o.meta["report_id"]): o.object_id for o in objects}

    dirs = [extracted[k] for k in ("pollution",) if k in extracted]
    csv_files = _find_pollution_csvs(dirs)
    log.info("Found %d pollution CSV files to process", len(csv_files))

    parquet_path = out_dir / "observations_pollution.parquet"
    total_rows = 0
    bad_ts_total = 0
    ts_min, ts_max = None, None

    BATCH_SIZE = 50
    writer = None
    # Track global sums for median approximation
    pollutant_all_values = {p: [] for p in POLLUTANTS}
    n_sampled = 0

    for batch_start in range(0, len(csv_files), BATCH_SIZE):
        batch_files = csv_files[batch_start : batch_start + BATCH_SIZE]
        frames = []
        for fp in batch_files:
            try:
                fname = os.path.basename(fp)
                rid = int(fname.replace("pollutionData", "").replace(".csv", ""))
                if rid not in valid_rids:
                    continue
                df = pd.read_csv(fp)
                df.columns = [c.strip() for c in df.columns]
                df["REPORT_ID"] = rid
                df["ts"] = pd.to_datetime(df["timestamp"], errors="coerce")
                bad_ts_total += int(df["ts"].isna().sum())
                df = df.dropna(subset=["ts"])
                df["object_id"] = rid_to_oid[rid]

                cols_keep = ["ts", "object_id"] + [p for p in POLLUTANTS if p in df.columns]
                frames.append(df[cols_keep])

                # Sample for median computation (every 10th row to save memory)
                for p in POLLUTANTS:
                    if p in df.columns:
                        vals = df[p].dropna().values
                        if len(vals) > 0:
                            pollutant_all_values[p].extend(vals[::10].tolist())
            except Exception as e:
                log.warning("Skip %s: %s", fp, e)

        if not frames:
            continue

        chunk = pd.concat(frames, ignore_index=True)
        chunk = chunk.sort_values("ts").drop_duplicates(subset=["object_id", "ts"], keep="last")
        total_rows += len(chunk)

        cmin, cmax = chunk["ts"].min(), chunk["ts"].max()
        ts_min = cmin if ts_min is None else min(ts_min, cmin)
        ts_max = cmax if ts_max is None else max(ts_max, cmax)

        import pyarrow as pa
        import pyarrow.parquet as pq
        table = pa.Table.from_pandas(chunk, preserve_index=False)
        if writer is None:
            writer = pq.ParquetWriter(str(parquet_path), table.schema)
        writer.write_table(table)

        del chunk, frames
        gc.collect()

    if writer is not None:
        writer.close()

    # Compute approximate medians for query thresholds
    pollutant_medians = {}
    for p in POLLUTANTS:
        vals = pollutant_all_values[p]
        if vals:
            pollutant_medians[p] = float(np.median(vals))
    del pollutant_all_values
    gc.collect()

    # Build lightweight summary for query generation
    summary = None
    if parquet_path.exists():
        import pyarrow.parquet as pq
        table = pq.read_table(str(parquet_path), columns=["ts", "object_id"] + POLLUTANTS)
        df = table.to_pandas()
        df["hour"] = df["ts"].dt.floor("h")
        agg_dict = {p: "mean" for p in POLLUTANTS if p in df.columns}
        agg_dict["ts"] = "count"
        summary = df.groupby(["object_id", "hour"]).agg(**{
            **{f"{p}_mean": pd.NamedAgg(column=p, aggfunc="mean") for p in POLLUTANTS if p in df.columns},
            "n_records": pd.NamedAgg(column="ts", aggfunc="count"),
        }).reset_index()
        del df
        gc.collect()

    stats = {
        "modality": "pollution", "valid_objects": len(objects),
        "bad_coord_rids": bad_coords, "rows_bad_ts": bad_ts_total,
        "rows_after_clean": total_rows,
        "time_min": str(ts_min), "time_max": str(ts_max),
        "avg_records_per_obj": round(total_rows / max(len(objects), 1), 1),
        "pollutant_medians": pollutant_medians,
    }
    return objects, summary, pollutant_medians, stats


# ---------------------------------------------------------------------------
# 5. Domain assignment
# ---------------------------------------------------------------------------

def assign_domains(objects, k, seed, out_dir):
    coords = np.array([[o.lat, o.lon] for o in objects])
    km = KMeans(n_clusters=k, random_state=seed, n_init=10)
    labels = km.fit_predict(coords)
    for obj, lbl in zip(objects, labels):
        obj.domain_id = int(lbl)
        obj.ndn_name = f"/d{obj.domain_id}/{obj.modality}/{obj.object_id}"

    pd.DataFrame(km.cluster_centers_, columns=["lat", "lon"]).to_csv(
        out_dir / "domain_centroids.csv", index_label="domain_id")

    rows = []
    for d in range(k):
        d_objs = [o for o in objects if o.domain_id == d]
        row = {"domain_id": d, "total": len(d_objs)}
        for m in ("parking", "traffic", "pollution"):
            row[m] = sum(1 for o in d_objs if o.modality == m)
        rows.append(row)
    pd.DataFrame(rows).to_csv(out_dir / "domain_stats.csv", index=False)
    return objects


# ---------------------------------------------------------------------------
# 6. Write objects.jsonl
# ---------------------------------------------------------------------------

def write_objects(objects, out_dir):
    fp = out_dir / "objects.jsonl"
    with open(fp, "w") as f:
        for o in objects:
            rec = {"object_id": o.object_id, "modality": o.modality, "city": o.city,
                   "lat": o.lat, "lon": o.lon, "domain_id": o.domain_id,
                   "ndn_name": o.ndn_name, "text_desc": o.text_desc, "meta": o.meta}
            f.write(json.dumps(rec, cls=NpEncoder) + "\n")
    log.info("Wrote %d objects to %s", len(objects), fp)


# ---------------------------------------------------------------------------
# 7. Query & Qrels Generation
# (Uses hourly summaries, NOT full observations → memory-safe)
# ---------------------------------------------------------------------------

def _sample_center(objects, rng, jitter_m=500.0):
    obj = objects[rng.randint(len(objects))]
    dlat = (rng.randn() * jitter_m) / 111_000
    dlon = (rng.randn() * jitter_m) / (111_000 * cos(radians(obj.lat)))
    return obj.lat + dlat, obj.lon + dlon

def _objects_in_radius(objects, clat, clon, radius_m):
    return [o for o in objects if haversine_m(o.lat, o.lon, clat, clon) <= radius_m]



def calculate_quantiles(parking_obs, traffic_summary, pollution_summary):
    quantiles = {}
    if traffic_summary is not None and not traffic_summary.empty:
        quantiles["traffic_speed"] = traffic_summary["avg_speed_mean"].quantile([0.33, 0.66]).to_dict()
    if pollution_summary is not None and not pollution_summary.empty:
        for p in ["ozone", "particulate_matter", "carbon_monoxide", "sulfure_dioxide", "nitrogen_dioxide"]:
            col = f"{p}_mean"
            if col in pollution_summary.columns:
                quantiles[p] = pollution_summary[col].quantile([0.33, 0.66]).to_dict()
    if parking_obs is not None and not parking_obs.empty:
        quantiles["parking_avail"] = parking_obs["availability"].quantile([0.33, 0.66]).to_dict()
    return quantiles

def generate_queries_diverse(all_objects, parking_obs, traffic_summary, pollution_summary,
                             quantiles, n_queries, time_window_min, seed, out_dir):
    rng = np.random.RandomState(seed)
    radii = [300, 500, 1000, 2000]
    
    mod_objs = defaultdict(list)
    for o in all_objects:
        mod_objs[o.modality].append(o)
    
    spatial_indices = {}
    for mod, objs in mod_objs.items():
        if not objs: continue
        coords = [[o.lat, o.lon] for o in objs]
        spatial_indices[mod] = (KDTree(coords), objs)
    
    queries = []
    qrels = []
    qid_counter = 0
    categories = ["LOCATION", "LEVEL", "TIME", "JOIN", "NEGATIVE"]
    
    def _evaluate_constraints(cons, ts_anchor, window_min):
        target_mod = cons.get("target_modality")
        if not target_mod: return []
        center_lat = cons.get("center_lat")
        center_lon = cons.get("center_lon")
        radius = cons.get("radius_m", 1000)
        
        candidates = []
        if target_mod in spatial_indices:
            tree, objs = spatial_indices[target_mod]
            deg_radius = radius / 111000.0
            indices = tree.query_ball_point([center_lat, center_lon], deg_radius * 1.5)
            for idx in indices:
                o = objs[idx]
                d = haversine_m(o.lat, o.lon, center_lat, center_lon)
                if d <= radius:
                    candidates.append(o)
        
        if not candidates: return []
        cand_ids = {o.object_id for o in candidates}
        final_candidates = []
        t_anchor = pd.Timestamp(ts_anchor)
        t_start = t_anchor - pd.Timedelta(minutes=window_min)
        
        # Helper to check condition
        def check_cond(val, cstr):
            try:
                cstr = str(cstr).strip()
                if cstr.startswith(">"): return val > float(cstr[1:])
                if cstr.startswith("<"): return val < float(cstr[1:])
                return True
            except: return False

        if target_mod == "parking" and parking_obs is not None:
             subset = parking_obs[(parking_obs["ts"] >= t_start) & (parking_obs["ts"] <= t_anchor) & (parking_obs["object_id"].isin(cand_ids))]
             if not subset.empty:
                 thresh_str = cons.get("threshold_str", "> -1")
                 grp = subset.groupby("object_id")["availability"].mean()
                 for oid, val in grp.items():
                     if check_cond(val, thresh_str): final_candidates.append(oid)

        elif target_mod == "traffic" and traffic_summary is not None:
            h_start, h_end = t_start.floor("h"), t_anchor.floor("h")
            subset = traffic_summary[(traffic_summary["hour"] >= h_start) & (traffic_summary["hour"] <= h_end) & (traffic_summary["object_id"].isin(cand_ids))]
            if not subset.empty:
                 thresh_str = cons.get("threshold_str")
                 if thresh_str and "avg_speed" in thresh_str:
                     grp = subset.groupby("object_id")["avg_speed_mean"].mean()
                     clean_thresh = thresh_str.replace("avg_speed", "").strip()
                     for oid, val in grp.items():
                         if check_cond(val, clean_thresh): final_candidates.append(oid)

        elif target_mod == "pollution" and pollution_summary is not None:
            h_start, h_end = t_start.floor("h"), t_anchor.floor("h")
            subset = pollution_summary[(pollution_summary["hour"] >= h_start) & (pollution_summary["hour"] <= h_end) & (pollution_summary["object_id"].isin(cand_ids))]
            if not subset.empty:
                 thresh_str = cons.get("threshold_str")
                 pcol = cons.get("pollutant_col")
                 if pcol and pcol in subset.columns:
                     grp = subset.groupby("object_id")[pcol].mean()
                     clean_thresh = thresh_str.split(">")[-1].strip() if ">" in thresh_str else thresh_str.split("<")[-1].strip()
                     # Re-add op because check_cond expects it
                     op = ">" if ">" in thresh_str else "<"
                     full_thresh = f"{op} {clean_thresh}"
                     for oid, val in grp.items():
                         if check_cond(val, full_thresh): final_candidates.append(oid)
        return final_candidates

    pbar = tqdm(total=n_queries, desc="Generating Diverse Queries")
    while qid_counter < n_queries:
        cat = rng.choice(categories)
        if cat == "JOIN": cat = "LOCATION" # Fallback join logic is hard, simulate via location for speed unless detailed

        q_obj = None
        radius = int(rng.choice(radii))
        start_dt, end_dt = pd.Timestamp("2014-08-01"), pd.Timestamp("2014-11-30")
        delta = end_dt - start_dt
        rand_sec = rng.randint(delta.total_seconds())
        ts_anchor = start_dt + pd.Timedelta(seconds=rand_sec)

        # 1. LOCATION
        if cat == "LOCATION":
            tm = rng.choice(list(mod_objs.keys()))
            if not mod_objs[tm]: continue
            landmark = rng.choice(mod_objs[tm])
            clat, clon = landmark.lat + (rng.randn()*100/111000), landmark.lon + (rng.randn()*100/111000)
            templates = [f"Find {tm} sensors near ({clat:.4f}, {clon:.4f})", f"Where is {tm} around here?", f"Show me {tm} within {radius}m"]
            q_obj = {"qid": f"q{qid_counter:04d}", "query_text": rng.choice(templates),
                     "constraints": {"category": "LOCATION", "target_modality": tm, "center_lat": round(clat, 6), "center_lon": round(clon, 6), "radius_m": radius, "threshold_str": "> -9999", "pollutant_col": "ozone_mean"},
                     "ts_anchor": ts_anchor.isoformat()}

        # 2. LEVEL
        elif cat == "LEVEL":
            tm = rng.choice(["traffic", "pollution"])
            level = rng.choice(["High", "Low"])
            if not mod_objs[tm]: continue
            landmark = rng.choice(mod_objs[tm])
            op = "<" if level == "Low" else ">"
            if tm == "traffic":
                th = quantiles.get("traffic_speed", {0.33: 40, 0.66: 60})
                val = th[0.33] if level == "Low" else th[0.66]
                q_obj = {"qid": f"q{qid_counter:04d}", "query_text": f"Find {level} traffic ({val} km/h)",
                         "constraints": {"category": "LEVEL", "target_modality": "traffic", "center_lat": landmark.lat, "center_lon": landmark.lon, "radius_m": radius, "threshold_str": f"avg_speed {op} {val}", "level": level},
                         "ts_anchor": ts_anchor.isoformat()}
            else:
                pol = rng.choice(["ozone", "particulate_matter"])
                th = quantiles.get(pol, {0.33: 30, 0.66: 60})
                val = th[0.33] if level == "Low" else th[0.66]
                q_obj = {"qid": f"q{qid_counter:04d}", "query_text": f"Locate {level} {pol}",
                         "constraints": {"category": "LEVEL", "target_modality": "pollution", "center_lat": landmark.lat, "center_lon": landmark.lon, "radius_m": radius, "threshold_str": f"{op} {val}", "pollutant_col": f"{pol}_mean"},
                         "ts_anchor": ts_anchor.isoformat()}
        
        # 3. TIME
        elif cat == "TIME":
            tm = rng.choice(["parking", "traffic"])
            if not mod_objs[tm]: continue
            landmark = rng.choice(mod_objs[tm])
            hour = rng.randint(0, 24)
            ts_anchor = ts_anchor.replace(hour=hour, minute=0)
            desc = "at night" if (hour < 6 or hour > 20) else "during day"
            q_obj = {"qid": f"q{qid_counter:04d}", "query_text": f"Check {tm} {desc} around {hour}:00",
                     "constraints": {"category": "TIME", "target_modality": tm, "center_lat": landmark.lat, "center_lon": landmark.lon, "radius_m": radius, "threshold_str": "> -9999" if tm=="parking" else "avg_speed > -1"},
                     "ts_anchor": ts_anchor.isoformat()}

        # 4. NEGATIVE
        elif cat == "NEGATIVE":
            tm = rng.choice(["traffic", "parking"])
            q_obj = {"qid": f"q{qid_counter:04d}", "query_text": f"Find {tm} with impossible values",
                     "constraints": {"category": "NEGATIVE", "target_modality": tm, "threshold_str": "> 999999"},
                     "ts_anchor": ts_anchor.isoformat(), "relevant_ids": []}

        if q_obj:
            hits = q_obj.get("relevant_ids")
            if hits is None:
                hits = _evaluate_constraints(q_obj["constraints"], q_obj["ts_anchor"], time_window_min)
            
            if cat != "NEGATIVE" and not hits: continue
            
            queries.append({"qid": q_obj["qid"], "query_text": q_obj["query_text"], "constraints": q_obj["constraints"], "ts_anchor": q_obj["ts_anchor"]})
            for oid in hits: qrels.append((q_obj["qid"], oid, 1))
            qid_counter += 1
            pbar.update(1)
            
    pbar.close()
    write_queries_qrels(queries, qrels, out_dir, seed)
    return queries, qrels

def generate_queries_legacy(all_objects, parking_obs, traffic_summary, pollution_summary,
                     pollutant_medians, n_queries, time_window_min, seed, out_dir):
    rng = np.random.RandomState(seed)
    radii = [300, 500, 1000, 2000]

    mod_objs = defaultdict(list)
    for o in all_objects:
        mod_objs[o.modality].append(o)

    # Allocate queries: parking:traffic:pollution = 1:3:2
    weights = {"parking": 1, "traffic": 3, "pollution": 2}
    active_mods = [m for m in weights if mod_objs.get(m)]
    total_w = sum(weights[m] for m in active_mods)
    mod_counts = {}
    assigned = 0
    for i, m in enumerate(active_mods):
        if i == len(active_mods) - 1:
            mod_counts[m] = n_queries - assigned
        else:
            c = round(n_queries * weights[m] / total_w)
            mod_counts[m] = c
            assigned += c

    queries = []
    qrels = []
    qid_counter = 0

    # ---------- PARKING ----------
    def _gen_parking(n):
        nonlocal qid_counter
        p_objs = mod_objs.get("parking", [])
        if not p_objs or parking_obs is None or parking_obs.empty:
            return
        td = pd.Timedelta(minutes=time_window_min)
        t_min, t_max = parking_obs["ts"].min(), parking_obs["ts"].max()
        start_qid = qid_counter

        for _ in range(n * 10):
            if qid_counter - start_qid >= n:
                break
            radius = int(rng.choice(radii))
            clat, clon = _sample_center(p_objs, rng, jitter_m=300)
            near = _objects_in_radius(p_objs, clat, clon, radius)
            if not near:
                continue
            sec_range = int((t_max - t_min).total_seconds())
            if sec_range <= int(td.total_seconds()):
                continue
            ts_anchor = t_min + pd.Timedelta(
                seconds=rng.randint(int(td.total_seconds()), sec_range))
            w_start = ts_anchor - td
            near_ids = {o.object_id for o in near}
            window = parking_obs[(parking_obs["ts"] >= w_start) &
                                 (parking_obs["ts"] <= ts_anchor) &
                                 (parking_obs["object_id"].isin(near_ids))]
            if window.empty:
                continue
            avail = window.groupby("object_id")["availability"].mean()
            relevant = [oid for oid, a in avail.items() if a > 0]
            if not relevant:
                continue
            qid = f"q{qid_counter:04d}"
            qid_counter += 1
            queries.append({
                "qid": qid,
                "query_text": f"Find available parking lots within {radius}m near ({clat:.4f},{clon:.4f}) at time {ts_anchor.isoformat()}",
                "constraints": {"modality": "parking", "center_lat": round(clat, 6),
                                "center_lon": round(clon, 6), "radius_m": radius,
                                "threshold": "availability > 0",
                                "time_window_minutes": time_window_min},
                "ts_anchor": ts_anchor.isoformat(),
            })
            for oid in relevant:
                qrels.append((qid, oid, 1))

    # ---------- TRAFFIC ----------
    def _gen_traffic(n):
        nonlocal qid_counter
        t_objs = mod_objs.get("traffic", [])
        if not t_objs or traffic_summary is None or traffic_summary.empty:
            return
        road_types = list({o.meta.get("road_type", "UNKNOWN") for o in t_objs})
        speed_thresholds = [30, 40, 50, 60]
        t_min = traffic_summary["hour"].min()
        t_max = traffic_summary["hour"].max()
        td = pd.Timedelta(minutes=time_window_min)
        start_qid = qid_counter

        for _ in range(n * 10):
            if qid_counter - start_qid >= n:
                break
            radius = int(rng.choice(radii))
            clat, clon = _sample_center(t_objs, rng, jitter_m=1000)
            rt = str(rng.choice(road_types)) if road_types else None
            near = _objects_in_radius(t_objs, clat, clon, radius)
            if rt:
                near = [o for o in near if o.meta.get("road_type") == rt]
            if not near:
                continue
            sec_range = int((t_max - t_min).total_seconds())
            if sec_range <= int(td.total_seconds()):
                continue
            ts_anchor_dt = t_min + pd.Timedelta(
                seconds=rng.randint(int(td.total_seconds()), sec_range))
            # Floor to hour for summary lookup
            anchor_hour = ts_anchor_dt.floor("h")
            window_start_hour = (ts_anchor_dt - td).floor("h")
            near_ids = {o.object_id for o in near}

            speed_t = int(rng.choice(speed_thresholds))
            window = traffic_summary[
                (traffic_summary["hour"] >= window_start_hour) &
                (traffic_summary["hour"] <= anchor_hour) &
                (traffic_summary["object_id"].isin(near_ids))
            ]
            if window.empty:
                continue
            avg_spd = window.groupby("object_id")["avg_speed_mean"].mean()
            relevant = [oid for oid, s in avg_spd.items() if s < speed_t]
            if not relevant:
                continue
            qid = f"q{qid_counter:04d}"
            qid_counter += 1
            queries.append({
                "qid": qid,
                "query_text": f"Locate traffic sensors on {rt} roads within {radius}m where average speed is below {speed_t} km/h near ({clat:.4f},{clon:.4f})",
                "constraints": {"modality": "traffic", "center_lat": round(clat, 6),
                                "center_lon": round(clon, 6), "radius_m": radius,
                                "road_type": rt, "threshold": f"avg_speed < {speed_t}",
                                "time_window_minutes": time_window_min},
                "ts_anchor": ts_anchor_dt.isoformat(),
            })
            for oid in relevant:
                qrels.append((qid, oid, 1))

    # ---------- POLLUTION ----------
    def _gen_pollution(n):
        nonlocal qid_counter
        pol_objs = mod_objs.get("pollution", [])
        if not pol_objs or pollution_summary is None or pollution_summary.empty:
            return
        pollutant_cols = [f"{p}_mean" for p in POLLUTANTS if f"{p}_mean" in pollution_summary.columns]
        if not pollutant_cols:
            return
        t_min = pollution_summary["hour"].min()
        t_max = pollution_summary["hour"].max()
        td = pd.Timedelta(minutes=time_window_min)
        start_qid = qid_counter

        # Map from col_mean name back to pollutant name
        col_to_pol = {f"{p}_mean": p for p in POLLUTANTS}

        for _ in range(n * 10):
            if qid_counter - start_qid >= n:
                break
            radius = int(rng.choice(radii))
            clat, clon = _sample_center(pol_objs, rng, jitter_m=1000)
            near = _objects_in_radius(pol_objs, clat, clon, radius)
            if not near:
                continue
            pcol = str(rng.choice(pollutant_cols))
            pollutant = col_to_pol.get(pcol, pcol.replace("_mean", ""))
            threshold = pollutant_medians.get(pollutant, 50.0)

            sec_range = int((t_max - t_min).total_seconds())
            if sec_range <= int(td.total_seconds()):
                continue
            ts_anchor_dt = t_min + pd.Timedelta(
                seconds=rng.randint(int(td.total_seconds()), sec_range))
            anchor_hour = ts_anchor_dt.floor("h")
            window_start_hour = (ts_anchor_dt - td).floor("h")
            near_ids = {o.object_id for o in near}

            window = pollution_summary[
                (pollution_summary["hour"] >= window_start_hour) &
                (pollution_summary["hour"] <= anchor_hour) &
                (pollution_summary["object_id"].isin(near_ids))
            ]
            if window.empty:
                continue
            if pcol not in window.columns:
                continue
            avg_val = window.groupby("object_id")[pcol].mean()
            relevant = [oid for oid, v in avg_val.items() if v > threshold]
            if not relevant:
                continue
            qid = f"q{qid_counter:04d}"
            qid_counter += 1
            queries.append({
                "qid": qid,
                "query_text": f"Find locations within {radius}m reporting high {pollutant} levels above {threshold:.0f} near ({clat:.4f},{clon:.4f})",
                "constraints": {"modality": "pollution", "center_lat": round(clat, 6),
                                "center_lon": round(clon, 6), "radius_m": radius,
                                "pollutant": pollutant,
                                "threshold": f"{pollutant} > {threshold:.1f}",
                                "time_window_minutes": time_window_min},
                "ts_anchor": ts_anchor_dt.isoformat(),
            })
            for oid in relevant:
                qrels.append((qid, oid, 1))

    if "parking" in mod_counts:
        _gen_parking(mod_counts["parking"])
    if "traffic" in mod_counts:
        _gen_traffic(mod_counts["traffic"])
    if "pollution" in mod_counts:
        _gen_pollution(mod_counts["pollution"])

    log.info("Generated %d queries, %d qrel pairs", len(queries), len(qrels))
    return queries, qrels


# ---------------------------------------------------------------------------
# 8. Write queries / qrels / splits
# ---------------------------------------------------------------------------

def write_queries_qrels(queries, qrels, out_dir, seed):
    with open(out_dir / "queries.jsonl", "w") as f:
        for q in queries:
            f.write(json.dumps(q, cls=NpEncoder) + "\n")

    with open(out_dir / "qrels.tsv", "w") as f:
        f.write("qid\tobject_id\trelevance\n")
        for qid, oid, rel in qrels:
            f.write(f"{qid}\t{oid}\t{rel}\n")

    ctr = Counter(qid for qid, _, _ in qrels)
    stats_df = pd.DataFrame([
        {"qid": q["qid"], "n_relevant": ctr.get(q["qid"], 0)} for q in queries
    ])
    stats_df.to_csv(out_dir / "per_query_stats.csv", index=False)

    # 70/30 split
    rng = np.random.RandomState(seed)
    qids = [q["qid"] for q in queries]
    rng.shuffle(qids)
    split_idx = int(0.7 * len(qids))
    train_qids, test_qids = set(qids[:split_idx]), set(qids[split_idx:])

    splits_dir = out_dir / "splits"
    splits_dir.mkdir(exist_ok=True)
    for name, qset in [("train", train_qids), ("test", test_qids)]:
        with open(splits_dir / f"{name}.qrels", "w") as f:
            f.write("qid\tobject_id\trelevance\n")
            for qid, oid, rel in qrels:
                if qid in qset:
                    f.write(f"{qid}\t{oid}\t{rel}\n")
    log.info("Splits: %d train, %d test", len(train_qids), len(test_qids))
    return train_qids, test_qids



# ---------------------------------------------------------------------------
# 9. Embeddings (with PCA reduction)
# ---------------------------------------------------------------------------

def to_vec_str(vec) -> str:
    """Format numpy array as string '[v1, v2, ...]'"""
    return "[" + ",".join(f"{v:.6f}" for v in vec) + "]"


def reduce_dimensions(obj_vecs, query_vecs, target_dim=128, seed=42):
    """Reduce dimensions using PCA and re-normalize."""
    try:
        from sklearn.decomposition import PCA
    except ImportError:
        log.warning("sklearn not found, skipping PCA")
        return obj_vecs, query_vecs

    if obj_vecs.shape[1] <= target_dim:
        return obj_vecs, query_vecs

    log.info(f"Reducing dimensions from {obj_vecs.shape[1]} to {target_dim} via PCA")
    pca = PCA(n_components=target_dim, random_state=seed)
    
    # Fit on objects, transform both
    obj_vecs_red = pca.fit_transform(obj_vecs)
    query_vecs_red = pca.transform(query_vecs)
    
    # Re-normalize to unit length (cosine similarity)
    # Avoid div by zero
    norms_obj = np.linalg.norm(obj_vecs_red, axis=1, keepdims=True)
    norms_obj[norms_obj == 0] = 1e-10
    obj_vecs_red = obj_vecs_red / norms_obj

    norms_query = np.linalg.norm(query_vecs_red, axis=1, keepdims=True)
    norms_query[norms_query == 0] = 1e-10
    query_vecs_red = query_vecs_red / norms_query

    return obj_vecs_red.astype(np.float32), query_vecs_red.astype(np.float32)


def generate_embeddings(objects, queries, out_dir, model_name, model_path, target_dim=128, seed=42):
    emb_dir = out_dir / "embeddings"
    emb_dir.mkdir(exist_ok=True)
    
    # Load model
    try:
        from sentence_transformers import SentenceTransformer
    except ImportError:
        log.error("sentence-transformers not installed; skipping embeddings.")
        return None, None

    path = model_path if model_path else model_name
    log.info("Loading model: %s", path)
    model = SentenceTransformer(path)

    # Encode Objects
    obj_texts = [o.text_desc for o in objects]
    obj_ids = [o.object_id for o in objects]
    log.info("Encoding %d object texts ...", len(obj_texts))
    obj_vecs = model.encode(obj_texts, show_progress_bar=True, batch_size=128, normalize_embeddings=True)

    # Encode Queries
    q_texts = [q["query_text"] for q in queries]
    q_ids = [q["qid"] for q in queries]
    log.info("Encoding %d query texts ...", len(q_texts))
    q_vecs = model.encode(q_texts, show_progress_bar=True, batch_size=128, normalize_embeddings=True)

    # Reduce Dimensions (PCA)
    obj_vecs, q_vecs = reduce_dimensions(obj_vecs, q_vecs, target_dim, seed)

    # Save
    np.save(emb_dir / "object_vectors.npy", obj_vecs)
    with open(emb_dir / "object_index.json", "w") as f:
        json.dump({oid: i for i, oid in enumerate(obj_ids)}, f)

    np.save(emb_dir / "query_vectors.npy", q_vecs)
    with open(emb_dir / "query_index.json", "w") as f:
        json.dump({qid: i for i, qid in enumerate(q_ids)}, f)

    log.info("Embeddings: objects %s, queries %s", obj_vecs.shape, q_vecs.shape)
    return obj_vecs, q_vecs


# ---------------------------------------------------------------------------
# 10. ndnSIM Export (C++ compatible)
# ---------------------------------------------------------------------------

def compute_semantic_centroids(objects, obj_vecs, out_dir):
    """Compute semantic centroids for C++ Loader."""
    if obj_vecs is None:
        return
        
    domain_map = defaultdict(list)
    for i, o in enumerate(objects):
        domain_map[o.domain_id].append(i)
    
    rows = []
    # format: domain_id,centroid_id,vector_dim,vector,radius,weight
    dim = obj_vecs.shape[1]
    
    for dim_id, indices in domain_map.items():
        vecs = obj_vecs[indices]
        centroid = np.mean(vecs, axis=0)
        # Normalize
        norm = np.linalg.norm(centroid)
        if norm > 0:
            centroid = centroid / norm
            
        # Compute radius (max dist) and weight
        dists = 1.0 - np.dot(vecs, centroid) # Cosine distance
        radius = float(np.max(dists)) if len(dists) > 0 else 0.0
        weight = float(len(indices))
        
        rows.append({
            "domain_id": dim_id,
            "centroid_id": 0, # Single centroid per domain for now
            "vector_dim": dim,
            "vector": to_vec_str(centroid),
            "radius": radius,
            "weight": weight
        })
        
    pd.DataFrame(rows).to_csv(out_dir / "export_ndnsim/domain_centroids.csv", index=False)
    log.info("Exported semantic centroids to export_ndnsim/domain_centroids.csv")


def export_ndnsim(objects, queries, qrels, obj_vecs, q_vecs, train_qids, test_qids, out_dir):
    exp_dir = out_dir / "export_ndnsim"
    exp_dir.mkdir(exist_ok=True)

    # 1. producer_content.csv
    # Format: domain_id,doc_id,canonical_name,vector,is_distractor
    if obj_vecs is not None:
        content_rows = []
        for i, o in enumerate(objects):
            content_rows.append({
                "domain_id": o.domain_id,
                "doc_id": o.object_id,
                "canonical_name": o.ndn_name,
                "vector": to_vec_str(obj_vecs[i]),
                "is_distractor": 0
            })
        pd.DataFrame(content_rows).to_csv(exp_dir / "producer_content.csv", index=False)
    
    # 2. consumer_trace.csv (and splits)
    # Format: query_id,query_text,vector,target_docids,target_domains
    if q_vecs is not None:
        # Build qrel map
        qrel_map = defaultdict(list)
        for qid, oid, rel in qrels:
            if rel > 0:
                qrel_map[qid].append(oid)
        
        # Object map for domain lookup
        obj_domains = {o.object_id: o.domain_id for o in objects}
        
        trace_rows = []
        train_rows = []
        test_rows = []
        
        for i, q in enumerate(queries):
            qid = q["qid"]
            rel_oids = qrel_map.get(qid, [])
            rel_domains = sorted(list({str(obj_domains.get(oid, -1)) for oid in rel_oids if oid in obj_domains}))
            
            row = {
                "query_id": qid,
                "query_text": q["query_text"],
                "vector": to_vec_str(q_vecs[i]),
                "target_docids": ";".join(rel_oids),
                "target_domains": ";".join(rel_domains)
            }
            trace_rows.append(row)
            if qid in train_qids:
                train_rows.append(row)
            if qid in test_qids:
                test_rows.append(row)
                
        pd.DataFrame(trace_rows).to_csv(exp_dir / "consumer_trace.csv", index=False)
        if train_rows:
            pd.DataFrame(train_rows).to_csv(exp_dir / "consumer_trace_train.csv", index=False)
        if test_rows:
            pd.DataFrame(test_rows).to_csv(exp_dir / "consumer_trace_test.csv", index=False)
        
    # 3. Semantic Centroids (domain_centroids.csv)
    compute_semantic_centroids(objects, obj_vecs, out_dir)

    # Copy qrels as well
    import shutil
    src = out_dir / "qrels.tsv"
    if src.exists():
        shutil.copy2(src, exp_dir / "qrels.tsv")

    log.info("ndnSIM export (C++ compatible) output to %s", exp_dir)


# ---------------------------------------------------------------------------
# 11. Report
# ---------------------------------------------------------------------------

def write_report(out_dir, all_stats, objects, queries, qrels, args):
    lines = [
        "# CityPulse Data Cleaning Report\n\n",
        f"Generated: {pd.Timestamp.now().isoformat()}\n\n",
        f"Seed: {args.seed} | K domains: {args.k_domains} | N queries: {args.n_queries}\n\n",
        "## Input Files\n\n",
        f"- Input: `{args.input}`\n",
        f"- Traffic batches: {args.traffic_batches}\n",
        f"- Modalities: {args.use_modalities}\n\n",
        "## Per-Modality Statistics\n\n",
    ]
    for st in all_stats:
        mod = st.get("modality", "?")
        lines.append(f"### {mod.capitalize()}\n\n")
        for k, v in st.items():
            if k != "modality":
                lines.append(f"- **{k}**: {v}\n")
        lines.append("\n")

    lines.append("## Object Summary\n\n")
    lines.append(f"- Total: {len(objects)}\n")
    for m in ("parking", "traffic", "pollution"):
        lines.append(f"  - {m}: {sum(1 for o in objects if o.modality == m)}\n")

    lines.append("\n## Domain Assignment\n\n")
    lines.append(f"- k-means k={args.k_domains}, seed={args.seed}\n")
    lines.append("- See `domain_centroids.csv`, `domain_stats.csv`\n\n")

    lines.append("## Dedup Strategy\n\n- Keep **last** row per (object_id, ts).\n\n")

    lines.append("## Query & Qrels\n\n")
    lines.append(f"- Total queries: {len(queries)}\n- Total qrel pairs: {len(qrels)}\n")
    ctr = Counter(qid for qid, _, _ in qrels)
    if ctr:
        vals = list(ctr.values())
        lines.append(f"- Relevant/query: min={min(vals)}, max={max(vals)}, "
                     f"mean={np.mean(vals):.1f}, median={np.median(vals):.1f}\n")

    lines.append("\n### Templates\n\n")
    lines.append("| Modality | Spatial | Threshold |\n")
    lines.append("|----------|---------|----------|\n")
    lines.append("| parking | radius ∈ {300,500,1000,2000}m | availability > 0 |\n")
    lines.append("| traffic | radius + road_type | avg_speed < T |\n")
    lines.append("| pollution | radius | pollutant > median |\n\n")

    lines.append("## Column Mappings\n\n")
    lines.append("### Parking\n- `updatetime` → `ts`\n- `availability` = totalspaces - vehiclecount\n\n")
    lines.append("### Traffic\n- `TIMESTAMP` → `ts`\n- `avgSpeed` → `avg_speed`\n- `vehicleCount` → `vehicle_count`\n\n")
    lines.append("### Pollution\n- `timestamp` → `ts`\n- Pollutant columns kept as-is\n\n")
    lines.append("## Splits\n\n- 70/30 train/test by query, seed-fixed.\n")

    with open(out_dir / "report.md", "w") as f:
        f.writelines(lines)
    log.info("Report → %s", out_dir / "report.md")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="CityPulse data preprocessing pipeline")
    parser.add_argument("--input", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--k_domains", type=int, default=8)
    parser.add_argument("--seed", type=int, default=20260211)
    parser.add_argument("--n_queries", type=int, default=500)
    parser.add_argument("--time_window_minutes", type=int, default=60)
    parser.add_argument("--use_modalities", default="parking,traffic,pollution")
    parser.add_argument("--traffic_batches", default="aug_sep_2014,oct_nov_2014")
    parser.add_argument("--max_objects_per_modality", type=int, default=0)
    parser.add_argument("--model_name", default="sentence-transformers/all-MiniLM-L6-v2")
    parser.add_argument("--model_path", default=None)
    parser.add_argument("--skip_embeddings", action="store_true")
    parser.add_argument("--vector_dim", type=int, default=128, help="Target embedding dimension (default: 128)")
    args = parser.parse_args()

    input_dir = Path(args.input).expanduser().resolve()
    out_dir = Path(args.out).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    modalities = [m.strip() for m in args.use_modalities.split(",")]
    traffic_batches = [b.strip() for b in args.traffic_batches.split(",")]
    max_obj = args.max_objects_per_modality

    log.info("=" * 60)
    log.info("CityPulse Data Preprocessing Pipeline")
    log.info("Input:  %s", input_dir)
    log.info("Output: %s", out_dir)
    log.info("Modalities: %s  K: %d  Seed: %d  Queries: %d",
             modalities, args.k_domains, args.seed, args.n_queries)
    log.info("Vector Dim: %d", args.vector_dim)
    log.info("=" * 60)

    # 1. Extract
    log.info("--- Stage 1: Extract ---")
    extracted = extract_archives(input_dir, out_dir, traffic_batches)

    # 2. Clean
    all_objects = []
    parking_obs = None
    traffic_summary = None
    pollution_summary = None
    pollutant_medians = {}
    all_stats = []
    traffic_meta = _load_traffic_metadata(input_dir) if ("traffic" in modalities or "pollution" in modalities) else None

    if "parking" in modalities:
        log.info("--- Stage 2a: Parking ---")
        p_objs, p_obs, p_stats = clean_parking(input_dir)
        if max_obj > 0 and len(p_objs) > max_obj:
            rng2 = np.random.RandomState(42)
            idx = rng2.choice(len(p_objs), max_obj, replace=False)
            p_objs = [p_objs[i] for i in sorted(idx)]
        all_objects.extend(p_objs)
        parking_obs = p_obs
        # Write parking observations
        if not p_obs.empty:
            p_obs.to_parquet(out_dir / "observations_parking.parquet", index=False, engine="pyarrow")
            log.info("Wrote %d parking observations", len(p_obs))
        all_stats.append(p_stats)

    if "traffic" in modalities:
        log.info("--- Stage 2b: Traffic ---")
        t_objs, t_summary, t_stats = clean_traffic(input_dir, extracted, max_obj, out_dir)
        all_objects.extend(t_objs)
        traffic_summary = t_summary
        all_stats.append(t_stats)

    if "pollution" in modalities:
        log.info("--- Stage 2c: Pollution ---")
        pol_objs, pol_summary, pol_medians, pol_stats = clean_pollution(
            input_dir, extracted, traffic_meta, max_obj, out_dir)
        all_objects.extend(pol_objs)
        pollution_summary = pol_summary
        pollutant_medians = pol_medians
        all_stats.append(pol_stats)

    log.info("Total objects: %d", len(all_objects))

    # 3. Domains
    log.info("--- Stage 3: Domains ---")
    all_objects = assign_domains(all_objects, args.k_domains, args.seed, out_dir)

    # 4. objects.jsonl
    log.info("--- Stage 4: objects.jsonl ---")
    write_objects(all_objects, out_dir)

    # 5. Queries & qrels
    log.info("--- Stage 5: Queries & Qrels ---")
    quantiles = calculate_quantiles(parking_obs, traffic_summary, pollution_summary)
    queries, qrels = generate_queries_diverse(
        all_objects, parking_obs, traffic_summary, pollution_summary,
        quantiles, args.n_queries, args.time_window_minutes,
        args.seed, out_dir)
    train_qids, test_qids = write_queries_qrels(queries, qrels, out_dir, args.seed)

    # Free summaries
    del parking_obs, traffic_summary, pollution_summary
    gc.collect()

    # 6. Embeddings
    obj_vecs, q_vecs = None, None
    if not args.skip_embeddings:
        log.info("--- Stage 6: Embeddings ---")
        obj_vecs, q_vecs = generate_embeddings(all_objects, queries, out_dir, args.model_name, args.model_path, target_dim=args.vector_dim, seed=args.seed)
        
        if q_vecs is not None and len(q_vecs) > 1:
            try:
                log.info("Calculating query similarity stats...")
                norms = np.linalg.norm(q_vecs, axis=1, keepdims=True)
                nq = q_vecs / (norms + 1e-9)
                # Sample if too large (500 is fine, 500*500=250k floats)
                sims = np.dot(nq, nq.T)
                iu = np.triu_indices(len(q_vecs), k=1)
                vals = sims[iu]
                log.info("Query Cosine Similarity: Min=%.4f, Max=%.4f, Mean=%.4f, Median=%.4f", 
                         vals.min(), vals.max(), vals.mean(), np.median(vals))
                hist, bins = np.histogram(vals, bins=[0, 0.2, 0.4, 0.6, 0.8, 0.9, 0.95, 1.0])
                log.info("Sim Hist (bins): %s", list(zip(bins[:-1], hist)))
            except Exception as e:
                log.error("Failed to calc stats: %s", e)
    else:
        log.info("--- Skipping embeddings ---")

    # 7. ndnSIM export
    log.info("--- Stage 7: ndnSIM export ---")
    export_ndnsim(all_objects, queries, qrels, obj_vecs, q_vecs, train_qids, test_qids, out_dir)

    # 8. Report
    log.info("--- Stage 8: Report ---")
    write_report(out_dir, all_stats, all_objects, queries, qrels, args)

    log.info("=" * 60)
    log.info("Done. Output: %s", out_dir)
    log.info("=" * 60)


if __name__ == "__main__":
    main()
