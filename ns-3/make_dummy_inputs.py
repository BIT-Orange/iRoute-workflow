#!/usr/bin/env python3
import csv
import os
import random

def make_inputs(out_dir):
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)
        
    domains = 4
    dim = 8 # Ensure matches C++ constant `g_vectorDim` or passed attribute
    users = 1
    queries = 5
    
    # 1. Centroids
    # format: domainId,centroidId,dim(ignored),vector_str(comma separated),radius,weight
    with open(os.path.join(out_dir, "centroids.csv"), "w") as f:
        writer = csv.writer(f)
        f.write("domainId,centroidId,dim,vector,radius,weight\n")
        for d in range(domains):
            vec = [random.random() for _ in range(dim)]
            vec_s = ",".join([f"{v:.4f}" for v in vec])
            # Quoting vector string to be safe if ParseCsvLine handles it, 
            # but ParseVectorString handles quotes too.
            f.write(f'{d},0,{dim},"{vec_s}",1.0,1.0\n')
            
    # 2. Content
    # format: domainId,docId,canonicalName,vector_str
    with open(os.path.join(out_dir, "content.csv"), "w") as f:
        f.write("domainId,docId,canonicalName,vector\n")
        for d in range(domains):
            for i in range(2): # 2 docs per domain
                docId = f"/doc/d{d}/i{i}"
                canon = docId + "/v1"
                vec = [random.random() for _ in range(dim)]
                vec_s = ",".join([f"{v:.4f}" for v in vec])
                f.write(f'{d},{docId},{canon},"{vec_s}"\n')

    # 3. Trace
    # format: queryId,queryText,vector,targetDocIds,targetDomains
    with open(os.path.join(out_dir, "trace.csv"), "w") as f:
        f.write("queryId,queryText,vector,targetDocIds,targetDomains\n")
        for q in range(queries):
            target_d = random.randint(0, domains-1)
            target_doc = f"/doc/d{target_d}/i0"
            vec = [random.random() for _ in range(dim)]
            vec_s = ",".join([f"{v:.4f}" for v in vec])
            # targetDomains and targetDocIds are semicolon separated if multiple
            f.write(f'{q},query{q},"{vec_s}",{target_doc},{target_d}\n')
            
    print(f"Created inputs in {out_dir}")

if __name__ == "__main__":
    make_inputs("dummy_inputs")
