#!/usr/bin/env python3
"""
Generate index_exact.csv for Exact-NDN syntax baseline.

Reads consumer_trace.csv + qrels.tsv + producer_content.csv to create a
dictionary mapping tokenized query text → (doc_id, canonical_name, domain_id).

Usage:
    python3 dataset/gen_exact_index.py \
        --trace sdm_smartcity_dataset/consumer_trace.csv \
        --qrels sdm_smartcity_dataset/qrels.tsv \
        --content sdm_smartcity_dataset/producer_content.csv \
        --out sdm_smartcity_dataset/index_exact.csv
"""

import argparse
import csv
import re
import sys


# Keyword → type classification rules
TYPE_RULES = [
    ("traffic",     ["traffic", "road", "speed", "jam", "vehicle", "congestion", "flow"]),
    ("parking",     ["parking", "lot", "vacancy", "spot", "garage"]),
    ("pollution",   ["pollution", "air", "aqi", "pm25", "emission", "quality"]),
    ("streetlight", ["streetlight", "light", "lamp", "lantern", "illuminat"]),
]


def tokenize(text: str) -> str:
    """Lowercase, strip punctuation, spaces → hyphens."""
    t = text.lower().strip()
    t = re.sub(r"[^a-z0-9\s-]", "", t)     # keep only alphanum, space, hyphen
    t = re.sub(r"\s+", "-", t)               # spaces → hyphens
    t = re.sub(r"-+", "-", t)                # collapse multiple hyphens
    t = t.strip("-")
    return t


def classify_type(text: str) -> str:
    """Classify query text into entity type by keyword matching."""
    lower = text.lower()
    for type_name, keywords in TYPE_RULES:
        if any(kw in lower for kw in keywords):
            return type_name
    return "general"


def main():
    parser = argparse.ArgumentParser(description="Generate index_exact.csv")
    parser.add_argument("--trace", required=True, help="consumer_trace.csv")
    parser.add_argument("--qrels", required=True, help="qrels.tsv")
    parser.add_argument("--content", required=True, help="producer_content.csv")
    parser.add_argument("--out", required=True, help="output index_exact.csv")
    args = parser.parse_args()

    # 1. Load qrels: qid → list of (object_id, relevance)
    qrels = {}
    with open(args.qrels) as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            qid = row["qid"]
            doc_id = row["object_id"]
            rel = int(row.get("relevance", 1))
            qrels.setdefault(qid, []).append((doc_id, rel))

    # 2. Load content: doc_id → (canonical_name, domain_id)
    content = {}
    with open(args.content) as f:
        reader = csv.DictReader(f)
        for row in reader:
            content[row["doc_id"]] = (row["canonical_name"], row["domain_id"])

    # 3. Load trace and generate index
    with open(args.trace) as f_in, open(args.out, "w", newline="") as f_out:
        reader = csv.DictReader(f_in)
        writer = csv.writer(f_out)
        writer.writerow(["tokenized_query", "type", "doc_id", "canonical_name", "domain_id"])

        seen = set()
        written = 0
        skipped = 0

        for row in reader:
            qid = row["query_id"]
            query_text = row["query_text"]

            tok = tokenize(query_text)
            if tok in seen:
                continue  # skip duplicate tokenized queries
            seen.add(tok)

            qtype = classify_type(query_text)

            # Find best doc from qrels (highest relevance, first match)
            docs = qrels.get(qid, [])
            if not docs:
                skipped += 1
                continue

            # Sort by relevance descending, pick first
            docs.sort(key=lambda x: -x[1])
            best_doc_id = docs[0][0]

            if best_doc_id not in content:
                skipped += 1
                continue

            canonical, domain_id = content[best_doc_id]
            writer.writerow([tok, qtype, best_doc_id, canonical, domain_id])
            written += 1

    print(f"Generated {args.out}: {written} entries ({skipped} skipped, {len(seen)} unique tokens)")


if __name__ == "__main__":
    main()
