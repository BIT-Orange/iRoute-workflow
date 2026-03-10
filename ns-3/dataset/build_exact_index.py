#!/usr/bin/env python3
import csv
import sys
import os
import re

def tokenize(text):
    # Match C++ ExactNdnConsumer::Tokenize
    # Alphanumeric -> lower
    # Space/Dash -> dash
    # Squeeze dashes? C++ code:
    # if (std::isalnum(c)) result += lower
    # else if (space or dash) 
    #   if (!result.empty() && result.back() != '-') result += '-'
    
    result = []
    for c in text:
        if c.isalnum():
            result.append(c.lower())
        elif c in (' ', '-'):
            if result and result[-1] != '-':
                result.append('-')
            # else skip (merge)
    
    # Trim trailing dash
    if result and result[-1] == '-':
        result.pop()
        
    return "".join(result)

def classify(text):
    lower = text.lower()
    if any(k in lower for k in ["traffic", "road", "speed", "jam", "vehicle", "congestion", "flow"]):
        return "traffic"
    if any(k in lower for k in ["parking", "lot", "vacancy", "spot", "garage"]):
        return "parking"
    if any(k in lower for k in ["pollution", "air", "aqi", "pm25", "emission", "quality"]):
        return "pollution"
    if any(k in lower for k in ["streetlight", "light", "lamp", "lantern", "illuminat"]):
        return "streetlight"
    return "general"

def main():
    consumer_trace = "dataset/sdm_smartcity_dataset/consumer_trace.csv"
    producer_content = "dataset/sdm_smartcity_dataset/producer_content.csv"
    output_file = "dataset/sdm_smartcity_dataset/index_exact.csv"

    if not os.path.exists(consumer_trace) or not os.path.exists(producer_content):
        print("Error: Input files not found.")
        sys.exit(1)

    # 1. Load Producer Content: doc_id -> (canonical, domain)
    doc_info = {}
    print(f"Loading {producer_content}...")
    with open(producer_content, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # columns: domain_id,doc_id,canonical_name,vector,is_distractor
            doc_info[row['doc_id']] = (row['canonical_name'], row['domain_id'])

    # 2. Process Queries
    print(f"Processing {consumer_trace}...")
    entries = []
    
    with open(consumer_trace, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            qtext = row['query_text']
            targets = row['target_docids'].split(';')
            if not targets or not targets[0]:
                continue
                
            # Use specific target doc (first one)
            target_doc = targets[0]
            if target_doc not in doc_info:
                # print(f"Warning: Doc {target_doc} not found in content.")
                continue
                
            canonical, domain = doc_info[target_doc]
            
            tok = tokenize(qtext)
            qtype = classify(qtext)
            
            # Entry: tokenized, type, doc_id, canonical, domain
            entries.append([tok, qtype, target_doc, canonical, domain])

    # 3. Write Index
    print(f"Writing {len(entries)} entries to {output_file}...")
    with open(output_file, 'w') as f:
        # Producer expects: tokenized,type,doc_id,canonical,domain_id
        # Producer logic: 
        # getline(ss, tok, ',') && getline(ss, type, ',') ...
        # No header strictly required by loop, but code does: std::getline(file, line); // Skip header
        
        f.write("tokenized_query,type,doc_id,canonical_name,domain_id\n")
        writer = csv.writer(f)
        writer.writerows(entries)

if __name__ == "__main__":
    main()
