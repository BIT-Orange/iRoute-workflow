#!/usr/bin/env python3
import argparse
import subprocess
import os
import sys
import json
import time
import hashlib
from datetime import datetime

def hash_file(filepath):
    """Compute MD5 hash of a file."""
    if not filepath or not os.path.exists(filepath):
        return None
    hash_md5 = hashlib.md5()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_command(cmd, log_file):
    """Run command and log output to file."""
    print(f"Running: {cmd}")
    with open(log_file, "w") as f:
        process = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        # Stream output to both console and log file
        for line in process.stdout:
            print(line, end="")
            f.write(line)
        process.wait()
    return process.returncode

def main():
    parser = argparse.ArgumentParser(description="Run iRoute v2 Experiment with Isolation")
    
    # Experiment parameters matching C++ CLI
    parser.add_argument("--program", default="iroute-v2-exp1-accuracy", help="ndnSIM program to run")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    parser.add_argument("--run", type=int, default=1, help="Run number")
    parser.add_argument("--topo", default="star", help="Topology type")
    parser.add_argument("--domains", type=int, default=5, help="Number of domains")
    parser.add_argument("--queries", type=int, default=50, help="Number of queries")
    parser.add_argument("--kMax", type=int, default=5, help="kMax")
    
    # Input files
    parser.add_argument("--traceFile", default="", help="Consumer trace file")
    parser.add_argument("--centroidsFile", default="", help="Centroids file")
    parser.add_argument("--contentFile", default="", help="Content file")
    parser.add_argument("--topoFile", default="", help="Topology file")
    
    # Runner options
    parser.add_argument("--dry-run", action="store_true", help="Print command without running")
    parser.add_argument("--tag", default="", help="Optional tag for run_id")
    parser.add_argument("--outputDir", default="", help="Force output directory")
    
    args, unknown = parser.parse_known_args()
    
    # Generate Run ID
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    if args.outputDir:
        results_dir = os.path.abspath(args.outputDir)
        run_id = os.path.basename(results_dir)
        if not args.dry_run:
            os.makedirs(results_dir, exist_ok=True)
        print(f"Using results directory: {results_dir}")
    else:
        run_id_parts = ["iroute_v2", args.topo, f"s{args.seed}", f"r{args.run}", timestamp]
        if args.tag:
            run_id_parts.insert(1, args.tag)
        
        run_id = "_".join(run_id_parts)
        results_dir = os.path.abspath(f"results/{run_id}")
        
        if not args.dry_run:
            os.makedirs(results_dir, exist_ok=True)
            print(f"Created results directory: {results_dir}")
    
    # compute file hashes
    input_hashes = {}
    for farg in [args.traceFile, args.centroidsFile, args.contentFile, args.topoFile]:
        if farg:
            input_hashes[farg] = hash_file(farg)
            
    # Create manifest
    manifest = {
        "run_id": run_id,
        "timestamp": timestamp,
        "program": args.program,
        "args": vars(args),
        "unknown_args": unknown,
        "input_hashes": input_hashes,
        "output_files": ["run_query_log.csv", "run_summary.csv", "console.log"]
    }
    
    if not args.dry_run:
        with open(os.path.join(results_dir, "manifest.json"), "w") as f:
            json.dump(manifest, f, indent=4)
            
    # Construct waf command
    # We need to quote the argument to --run because it contains spaces
    # and we are running via shell=True string
    program_args = f"{args.program} --resultDir={results_dir}"
    waf_cmd = ["./waf", "--run", f"'{program_args}'"]
    
    # Add mapped arguments to the program_args string, NOT top level waf cmd
    # Actually, waf --run takes a single string. 
    # run_experiments.py was appending to waf_cmd list and then joining.
    # We need to construct the full program command line string first.
    
    prog_cmd_parts = [args.program, f"--resultDir={results_dir}"]
    prog_cmd_parts.append(f"--seed={args.seed}")
    prog_cmd_parts.append(f"--run={args.run}")
    prog_cmd_parts.append(f"--topo={args.topo}")
    prog_cmd_parts.append(f"--domains={args.domains}")
    prog_cmd_parts.append(f"--queries={args.queries}")
    prog_cmd_parts.append(f"--kMax={args.kMax}")
    
    if args.traceFile: prog_cmd_parts.append(f"--trace={os.path.abspath(args.traceFile)}")
    if args.centroidsFile: prog_cmd_parts.append(f"--centroids={os.path.abspath(args.centroidsFile)}")
    if args.contentFile: prog_cmd_parts.append(f"--content={os.path.abspath(args.contentFile)}")
    if args.topoFile: prog_cmd_parts.append(f"--topoFile={os.path.abspath(args.topoFile)}")
    
    # Add pass-through arguments
    for arg in unknown:
        prog_cmd_parts.append(arg)
        
    full_prog_arg = " ".join(prog_cmd_parts)
    
    # Check if we need to escape ' for shell
    full_prog_arg = full_prog_arg.replace("'", "'\\''")
    
    full_cmd = f"./waf --run '{full_prog_arg}'"
    
    if args.dry_run:
        print(f"Dry run: {full_cmd}")
        return 0
    
    # Run
    log_file = os.path.join(results_dir, "console.log")
    ret = run_command(full_cmd, log_file)
    
    if ret == 0:
        print(f"\nSUCCESS: Run {run_id} completed.")
        print(f"Results in: {results_dir}")
    else:
        print(f"\nFAILURE: Run {run_id} failed with exit code {ret}.")
        print(f"Check log: {log_file}")
    
    return ret

if __name__ == "__main__":
    sys.exit(main())
