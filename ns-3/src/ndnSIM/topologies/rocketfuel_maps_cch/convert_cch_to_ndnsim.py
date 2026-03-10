#!/usr/bin/env python3
"""
Convert Rocketfuel .cch files to ndnSIM annotated topology format.

Usage:
    python convert_cch_to_ndnsim.py 1239.r0.cch --output as1239.txt --bandwidth 10Gbps --delay 2ms

Output format is compatible with AnnotatedTopologyReader.
"""

import re
import argparse
import sys
from collections import defaultdict

def parse_cch_line(line):
    """
    Parse a single .cch line.
    Format: uid @location [bb] (num_neigh) [&alias] -> <neigh>... [-extern]... =name[!] rN
    
    Returns dict with uid, location, is_backbone, neighbors, external, name, radius
    or None if parse fails or is external node (negative uid).
    """
    line = line.strip()
    if not line or line.startswith('#'):
        return None
    
    # Match uid at start
    m = re.match(r'^(-?\d+)\s+@(.+?)(?:\s+\+)?(?:\s+bb)?\s*\((\d+)\)', line)
    if not m:
        return None
    
    uid = int(m.group(1))
    if uid < 0:  # Skip external AS nodes
        return None
    
    location = m.group(2).strip()
    # num_neighbors = int(m.group(3))
    
    is_backbone = ' bb' in line or '+' in line.split('(')[0]
    
    # Extract neighbors (positive uids in angle brackets)
    neighbors = [int(x) for x in re.findall(r'<(\d+)>', line)]
    
    # Extract external refs (negative uids in braces) - we ignore these
    external = [int(x) for x in re.findall(r'\{(-?\d+)\}', line)]
    
    # Extract name (after =, before rN)
    name_match = re.search(r'=([^\s]+?)!?\s+r(\d+)', line)
    name = name_match.group(1) if name_match else f"node{uid}"
    radius = int(name_match.group(2)) if name_match else 0
    
    return {
        'uid': uid,
        'location': location,
        'is_backbone': is_backbone,
        'neighbors': neighbors,
        'external': external,
        'name': name,
        'radius': radius
    }


def load_cch_file(filepath):
    """Load and parse a .cch file, return list of node dicts."""
    nodes = []
    uid_set = set()
    
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            parsed = parse_cch_line(line)
            if parsed and parsed['uid'] not in uid_set:
                nodes.append(parsed)
                uid_set.add(parsed['uid'])
    
    return nodes


def build_graph(nodes):
    """
    Build adjacency list and ensure bidirectional edges.
    Returns: 
        - nodes_list: list of node dicts (with contiguous new_id assigned)
        - edges: set of (src_new_id, dst_new_id) tuples (undirected)
    """
    # Create uid -> new_id mapping (contiguous 0..N-1)
    uid_to_new = {n['uid']: i for i, n in enumerate(nodes)}
    
    # Assign new_id to each node
    for i, n in enumerate(nodes):
        n['new_id'] = i
    
    # Build edge set (undirected)
    edges = set()
    for n in nodes:
        src = n['new_id']
        for neigh_uid in n['neighbors']:
            if neigh_uid in uid_to_new:
                dst = uid_to_new[neigh_uid]
                if src < dst:
                    edges.add((src, dst))
                else:
                    edges.add((dst, src))
    
    return nodes, edges


def compute_positions(nodes):
    """
    Assign simple grid positions based on location string hash.
    In practice, we just space them out for visualization.
    """
    # Group by location
    loc_groups = defaultdict(list)
    for n in nodes:
        loc_groups[n['location']].append(n)
    
    # Assign positions: each location gets a column, nodes stacked vertically
    col = 0
    for loc, group in sorted(loc_groups.items()):
        for row, n in enumerate(group):
            n['xPos'] = col * 3
            n['yPos'] = row * 2
        col += 1


def write_ndnsim_topology(nodes, edges, output_path, bandwidth='10Gbps', delay='2ms', queue=100):
    """
    Write nodes and edges in ndnSIM annotated topology format.
    """
    with open(output_path, 'w') as f:
        f.write(f"# Rocketfuel topology converted to ndnSIM format\n")
        f.write(f"# Nodes: {len(nodes)}, Links: {len(edges)}\n")
        f.write(f"# Bandwidth: {bandwidth}, Delay: {delay}\n\n")
        
        f.write("router\n\n")
        f.write("# node\tcomment\tyPos\txPos\n")
        for n in nodes:
            # Use new_id as node name (Node0, Node1, ...)
            node_name = f"Node{n['new_id']}"
            comment = n['location'].replace(' ', '_').replace(',', '')[:20]
            f.write(f"{node_name}\t{comment}\t{n['yPos']}\t{n['xPos']}\n")
        
        f.write("\nlink\n\n")
        f.write(f"# srcNode\tdstNode\tbandwidth\tmetric\tdelay\tqueue\n")
        
        for src, dst in sorted(edges):
            src_name = f"Node{src}"
            dst_name = f"Node{dst}"
            metric = 1  # Could compute based on delay or distance
            f.write(f"{src_name}\t{dst_name}\t{bandwidth}\t{metric}\t{delay}\t{queue}\n")
    
    print(f"Written {output_path}: {len(nodes)} nodes, {len(edges)} links")


def main():
    parser = argparse.ArgumentParser(description='Convert Rocketfuel .cch to ndnSIM topology')
    parser.add_argument('input', help='Input .cch file path')
    parser.add_argument('--output', '-o', default=None, help='Output topology file path')
    parser.add_argument('--bandwidth', '-b', default='10Gbps', help='Link bandwidth (default: 10Gbps)')
    parser.add_argument('--delay', '-d', default='2ms', help='Link delay (default: 2ms)')
    parser.add_argument('--queue', '-q', type=int, default=100, help='Queue size (default: 100)')
    parser.add_argument('--max-nodes', '-n', type=int, default=None, help='Max nodes to include (for testing)')
    
    args = parser.parse_args()
    
    if args.output is None:
        args.output = args.input.replace('.cch', '.txt')
    
    print(f"Loading {args.input}...")
    nodes = load_cch_file(args.input)
    print(f"Parsed {len(nodes)} nodes")
    
    if args.max_nodes and len(nodes) > args.max_nodes:
        # Keep only backbone nodes first, then fill with others
        backbone = [n for n in nodes if n['is_backbone']]
        others = [n for n in nodes if not n['is_backbone']]
        nodes = (backbone + others)[:args.max_nodes]
        print(f"Limited to {len(nodes)} nodes")
    
    nodes, edges = build_graph(nodes)
    print(f"Graph: {len(nodes)} nodes, {len(edges)} edges")
    
    compute_positions(nodes)
    
    write_ndnsim_topology(nodes, edges, args.output, args.bandwidth, args.delay, args.queue)


if __name__ == '__main__':
    main()
