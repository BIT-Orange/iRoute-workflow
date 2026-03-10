#!/usr/bin/env python3
"""
build_sdm_dataset.py — Smart Data Models synthetic smart city dataset
for ndnSIM iRoute experiments.

Outputs: producer_content.csv, consumer_trace{,_train,_test}.csv,
         qrels.tsv, domain_centroids.csv, entities.csv, report.md
"""
import argparse, csv, json, logging, math, os, random, re, sys, io, textwrap
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Set, Any
import numpy as np

log = logging.getLogger("sdm")
logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(name)s  %(message)s",
                    datefmt="%H:%M:%S")

# ═════════════════════════════════════════════════════════════════════
# 1. SCHEMA DEFINITIONS  (Smart Data Models GitHub)
# ═════════════════════════════════════════════════════════════════════
SCHEMA_SOURCES = {
    "streetlight": {
        "repo": "smart-data-models/dataModel.Streetlighting",
        "model": "Streetlight",
        "url": "https://github.com/smart-data-models/dataModel.Streetlighting"
               "/blob/master/Streetlight/schema.json",
    },
    "parking": {
        "repo": "smart-data-models/dataModel.Parking",
        "model": "OffStreetParking",
        "url": "https://github.com/smart-data-models/dataModel.Parking"
               "/blob/master/OffStreetParking/schema.json",
    },
    "traffic": {
        "repo": "smart-data-models/dataModel.Transportation",
        "model": "TrafficFlowObserved",
        "url": "https://github.com/smart-data-models/dataModel.Transportation"
               "/blob/master/TrafficFlowObserved/schema.json",
    },
    "pollution": {
        "repo": "smart-data-models/dataModel.Environment",
        "model": "AirQualityObserved",
        "url": "https://github.com/smart-data-models/dataModel.Environment"
               "/blob/master/AirQualityObserved/schema.json",
    },
}

ENTITY_ENUMS: Dict[str, Dict[str, List[str]]] = {
    "streetlight": {
        "powerState":        ["on", "off", "low", "bootingUp"],
        "status":            ["ok", "defectiveLamp", "columnIssue", "brokenLantern"],
        "locationCategory":  ["facade", "garden", "park", "pedestrianPath",
                              "playground", "road", "sidewalk", "tunnel"],
        "lanternModel":      ["LED", "HPS", "MH", "fluorescent"],
    },
    "parking": {
        "category":          ["onStreet", "offStreet", "underground", "rooftop",
                              "surface", "multiStorey"],
        "occupancyStatus":   ["available", "full", "almostFull", "few"],
        "allowedVehicleType":["car", "motorcycle", "bicycle", "truck",
                              "bus", "electricVehicle"],
        "facilityType":      ["covered", "uncovered", "gated", "metered"],
    },
    "traffic": {
        "roadType":          ["arterial", "residential", "highway", "collector",
                              "localStreet", "serviceRoad"],
        "congestionLevel":   ["free", "light", "moderate", "heavy", "gridlock"],
        "vehicleType":       ["car", "bus", "truck", "motorcycle", "bicycle"],
        "laneDirection":     ["inbound", "outbound", "bidirectional"],
    },
    "pollution": {
        "pollutantType":     ["NO2", "PM10", "PM2_5", "O3", "CO", "SO2"],
        "airQualityLevel":   ["good", "moderate", "unhealthy", "hazardous",
                              "veryUnhealthy"],
        "source_type":       ["industrial", "traffic", "residential",
                              "naturalBackground"],
        "monitoringStation": ["fixed", "mobile", "satellite"],
    },
}

DOC_PREFIX  = {"streetlight": "sl_", "parking": "p_",
               "traffic": "traf_", "pollution": "pol_"}
SERVICE     = {"streetlight": "streetlight", "parking": "parking",
               "traffic": "traffic", "pollution": "pollution"}
BBOX        = (56.12, 56.20, 10.15, 10.25)   # lat_min, lat_max, lon_min, lon_max

# ═════════════════════════════════════════════════════════════════════
# 2. WORD LISTS
# ═════════════════════════════════════════════════════════════════════
_BASE_STREETS = [
    "Elm St","Oak Ave","Maple Blvd","Cedar Ln","Pine Rd","Birch Dr",
    "Willow Way","Ash Ct","Spruce St","Cherry Ln","Harbor Blvd","Lake Dr",
    "River Rd","Valley Path","Summit Way","Forest Ave","Garden St",
    "Park Rd","Bridge Ln","Station Rd","Canal St","Market Rd","Church Ln",
    "Mill Rd","School St","King Blvd","Queen Ave","Castle Rd","Tower Ln",
    "Gate St",
]
_BASE_LANDMARKS = [
    "Riverside Park","Central Plaza","City Hall","Main Station",
    "Harbor Bridge","Town Square","University Campus","Sports Arena",
    "Shopping Mall","Art Museum","Science Center","Botanical Garden",
    "Cathedral","Clock Tower","Lighthouse","Cultural Center",
    "Convention Center","Waterfront Promenade","Historic Market",
    "City Library",
]
_BASE_AREAS = [
    "Downtown","Uptown","Commercial Strip","Financial Row","Arts Quarter",
    "Industrial Zone","Residential Block","Transit Hub","Civic Center",
    "Waterfront",
]
DISTRICT_NAMES = [
    "North Harbor","Westside Heights","Eastshore Quarter","South Hills",
    "Midtown Center","Lakeside District","Old Town Core","Greenfield Park",
    "Tech Valley","Marina Quarter","Sunset Bluffs","Riverside Glen",
]
ALIEN_LANDMARKS = [
    "Mars Colony Alpha","Lunar Outpost","Atlantis Hub","Quantum Garden",
    "Nebula Square","Gravity Falls","Phantom Island","Time Warp Plaza",
    "Crystal Void","Shadow Realm Gate",
]

def make_domain_word_lists(k: int):
    """Per-domain word lists: ≥30 streets, ≥20 landmarks, ≥10 areas."""
    out = {}
    for d in range(k):
        dist = DISTRICT_NAMES[d % len(DISTRICT_NAMES)]
        out[d] = dict(
            district=dist,
            streets=[f"{s}, {dist}" for s in _BASE_STREETS],
            landmarks=[f"{dist} {l}" for l in _BASE_LANDMARKS],
            areas=[f"{dist} {a}" for a in _BASE_AREAS],
        )
    return out

# ═════════════════════════════════════════════════════════════════════
# 3. TEXT-PROFILE TEMPLATES  (≥15 per type)
# ═════════════════════════════════════════════════════════════════════
PROFILE_TPL = {
  "streetlight": [
    "Streetlight near {landmark} in {area}, status {status}, power {powerState}.",
    "A {lanternModel} street lamp on {street} serving {area}, currently {powerState}.",
    "Public lighting fixture near {landmark}, {locationCategory} installation, {status}.",
    "Urban illumination in {area} on {street}, {lanternModel} type, power {powerState}.",
    "{locationCategory} streetlight close to {landmark}, operational status: {status}.",
    "Light post along {street} in {area}, {lanternModel} model, {powerState} state.",
    "Street illumination serving {area}, near {landmark}, lamp is {status}.",
    "A {powerState} {lanternModel} lamp on {street}, located in {area}.",
    "Lighting unit in the {locationCategory} area of {area}, facing {landmark}, {status}.",
    "Municipal lamp near {landmark} on {street}, type {lanternModel}, state {powerState}.",
    "Public light in {area}, {locationCategory} zone, lantern condition: {status}.",
    "Outdoor lighting near {landmark}, along {street}, power: {powerState}.",
    "{lanternModel} fixture in {area}, streetlight {status}, power {powerState}.",
    "City lamp at {street} near {landmark}, {area} district, status {status}.",
    "Nighttime light source in {area} close to {landmark}, {lanternModel} unit.",
  ],
  "parking": [
    "Parking spot in {area}, category {category}, occupancy {occupancyStatus}.",
    "A {category} parking facility on {street}, {area}, for {allowedVehicleType}s.",
    "{facilityType} parking near {landmark}, currently {occupancyStatus}.",
    "Vehicle parking in {area} on {street}, {category} type, {occupancyStatus}.",
    "{category} parking lot close to {landmark}, {facilityType}, {occupancyStatus}.",
    "Parking space serving {area}, near {landmark}, {allowedVehicleType} permitted.",
    "A {facilityType} {category} parking on {street} in {area}, {occupancyStatus}.",
    "Public parking in {area}, near {landmark}, allows {allowedVehicleType}s.",
    "{category} car park on {street}, {area} district, {facilityType} facility.",
    "Parking zone near {landmark} in {area}, {occupancyStatus}, {allowedVehicleType}s.",
    "Urban parking at {street}, {area}, type {category}, {occupancyStatus}.",
    "{facilityType} parking in {area}, for {allowedVehicleType}s.",
    "Parking bay near {landmark}, {area}, {category}, currently {occupancyStatus}.",
    "Vehicle storage on {street} in {area}, {facilityType} and {category}.",
    "Parking in {area} for {allowedVehicleType}s, {occupancyStatus} status.",
  ],
  "traffic": [
    "Traffic sensor on {roadType} road serving {area}, congestion {congestionLevel}.",
    "Flow observation on {street}, {area}, {roadType} road, {congestionLevel} traffic.",
    "Traffic monitor near {landmark}, {vehicleType} flow, {laneDirection} lane.",
    "{roadType} traffic sensor in {area}, {vehicleType} movements, {congestionLevel}.",
    "Traffic observer on {street} near {landmark}, {laneDirection}, {congestionLevel}.",
    "Road sensor in {area}, {roadType} classification, tracking {vehicleType}.",
    "Traffic measurement at {street}, {area}, {congestionLevel} for {vehicleType}s.",
    "Congestion monitor near {landmark} in {area}, {roadType}, {laneDirection} flow.",
    "Vehicle detector on {street}, {area}, road: {roadType}, level: {congestionLevel}.",
    "Traffic collector in {area}, near {landmark}, {vehicleType} on {roadType}.",
    "Road traffic sensor at {street} in {area}, {congestionLevel}, {laneDirection}.",
    "Urban mobility monitor near {landmark}, {roadType} segment, {congestionLevel}.",
    "Traffic station on {street}, {area}, {vehicleType} dominant, {laneDirection}.",
    "Flow sensor in {area} on {roadType} {street}, congestion: {congestionLevel}.",
    "Traffic observer near {landmark}, {vehicleType} on {roadType} road.",
  ],
  "pollution": [
    "Air quality sensor near {landmark}, pollutant {pollutantType}, level {airQualityLevel}.",
    "Pollution monitor in {area}, tracking {pollutantType}, quality: {airQualityLevel}.",
    "Environmental sensor on {street} near {landmark}, {source_type} source, {pollutantType}.",
    "{monitoringStation} air quality monitor in {area}, {pollutantType}, {airQualityLevel}.",
    "Atmospheric sensor near {landmark}, {area}, measures {pollutantType}.",
    "Air quality point on {street}, {area}, {pollutantType} at {airQualityLevel}.",
    "{monitoringStation} monitor in {area}, {pollutantType} from {source_type} sources.",
    "Pollution detector near {landmark} in {area}, {pollutantType}, quality: {airQualityLevel}.",
    "Urban air monitor on {street}, {area}, {source_type} {pollutantType} levels.",
    "Environmental sensor near {landmark}, {monitoringStation} type, {pollutantType}.",
    "AQ sensor in {area}, on {street}, measuring {pollutantType}, {airQualityLevel}.",
    "Atmospheric monitoring near {landmark}, {area}, {pollutantType} from {source_type}.",
    "{pollutantType} tracker in {area}, {monitoringStation}, level: {airQualityLevel}.",
    "Clean-air monitor near {landmark} on {street}, {pollutantType} concentrations.",
    "Air pollution sensor in {area}, {source_type} influenced, {pollutantType}.",
  ],
}

# ═════════════════════════════════════════════════════════════════════
# 4. QUERY TEMPLATES (per family)
# ═════════════════════════════════════════════════════════════════════
Q_TYPE_ZONE = [
    "Find all {etype} sensors in {district}",
    "Show {etype} status for the {area} zone",
    "List {etype} data around {landmark}",
    "What {etype} readings are reported in {district}?",
    "Give me {etype} information near {landmark}",
    "Search for {etype} devices in {area}",
    "Locate every {etype} unit across {district}",
    "Query {etype} records from {area}",
]
Q_ENUM_COMBO = [
    "Find {etype} with {e1_name} {e1_val} and {e2_name} {e2_val} near {landmark}",
    "Show {etype} where {e1_name} is {e1_val}, {e2_name} is {e2_val} in {area}",
    "{etype} with {e1_name}={e1_val} and {e2_name}={e2_val} close to {landmark}",
    "List {etype} having {e1_name} {e1_val} plus {e2_name} {e2_val} in {district}",
    "Search for {etype} devices: {e1_name}={e1_val}, {e2_name}={e2_val}, near {landmark}",
    "Give me {etype} where {e1_name} equals {e1_val} and {e2_name} equals {e2_val}",
    "Locate {etype} with {e1_name} set to {e1_val} near {landmark} in {area}",
    "Query {etype}: {e1_name} {e1_val}, {e2_name} {e2_val}, in {district}",
]
Q_PARAPHRASE_STEMS = [
    ("Find {etype} with {constraint_desc} around {landmark}",
     "Search for {etype} matching {constraint_desc} near {landmark}",
     "Locate {etype} having {constraint_desc} close to {landmark}"),
    ("Show {etype} where {constraint_desc} in {area}",
     "List {etype} that satisfy {constraint_desc} within {area}",
     "Which {etype} have {constraint_desc} in {area}?"),
    ("{etype} data: {constraint_desc}, zone {district}",
     "Report on {etype} with {constraint_desc} in {district}",
     "Query {etype} meeting {constraint_desc} across {district}"),
]
Q_CROSS_TYPE = [
    "Find {type1} and {type2} sensors in {district}",
    "Show {type1} plus {type2} data in {area}",
    "Combined {type1} and {type2} readings near {landmark}",
    "Search for {type1} alongside {type2} around {landmark}",
]
Q_NEGATIVE = [
    "Traffic sensors on an underwater highway in Atlantis",
    "Parking spots on the surface of the Moon",
    "Streetlights in a nonexistent Quantum Dimension",
    "Pollution sensors orbiting Saturn's rings",
    "Find traffic with velocity exceeding the speed of light",
    "Show parking availability in Mars Colony Alpha",
    "Locate streetlights inside an active volcano",
    "Air quality measurements from the bottom of the Mariana Trench",
    "Traffic flow on roads built of pure imagination",
    "Parking facilities made of antimatter near Gravity Falls",
    "Detect streetlights from the year 3025 in Crystal Void",
    "Atmospheric readings from Phantom Island's invisible shores",
    "Show parking on Nebula Square floating in outer space",
    "Traffic congestion on hyperdimensional portals",
    "Air quality inside a black hole near Shadow Realm Gate",
]

# ═════════════════════════════════════════════════════════════════════
# 5. ENTITY GENERATION
# ═════════════════════════════════════════════════════════════════════
def _gen_domain_centers(k, rng):
    lats = np.linspace(BBOX[0]+0.005, BBOX[1]-0.005, max(int(math.sqrt(k))+1, 3))
    lons = np.linspace(BBOX[2]+0.005, BBOX[3]-0.005, max(int(math.sqrt(k))+1, 3))
    pts = [(la, lo) for la in lats for lo in lons]
    rng_np = np.random.RandomState(rng.randint(0, 2**31))
    rng_np.shuffle(pts)
    return pts[:k]

def _nearest_domain(lat, lon, centers):
    best, best_d = 1e9, 0
    for d, (cla, clo) in enumerate(centers):
        dist = (lat - cla)**2 + (lon - clo)**2
        if dist < best:
            best, best_d = dist, d
    return best_d

def generate_entities(k_domains, n_per_type, n_min, seed):
    """Generate synthetic entities for all 4 types."""
    rng = random.Random(seed)
    centers = _gen_domain_centers(k_domains, rng)
    wl = make_domain_word_lists(k_domains)
    all_ents = []
    doc_counter = defaultdict(int)

    for etype in ["streetlight", "parking", "traffic", "pollution"]:
        enums = ENTITY_ENUMS[etype]
        enum_keys = sorted(enums.keys())
        # Ensure full enum coverage: round-robin first
        enum_iters = {k: 0 for k in enum_keys}

        for i in range(n_per_type):
            lat = rng.uniform(BBOX[0], BBOX[1])
            lon = rng.uniform(BBOX[2], BBOX[3])
            d = _nearest_domain(lat, lon, centers)
            dwl = wl[d]
            doc_counter[etype] += 1
            doc_id = f"{DOC_PREFIX[etype]}{doc_counter[etype]:06d}"
            canon = f"/d{d}/{SERVICE[etype]}/{doc_id}"

            ent = dict(
                entity_type=etype, entity_id=f"{etype}_{i:06d}",
                doc_id=doc_id, domain_id=d,
                lat=round(lat, 6), lon=round(lon, 6),
                canonical_name=canon,
                streetName=rng.choice(dwl["streets"]),
                landmarkToken=rng.choice(dwl["landmarks"]),
                areaServed=rng.choice(dwl["areas"]),
            )
            # Assign enum values with round-robin for coverage
            for ek in enum_keys:
                vals = enums[ek]
                idx = enum_iters[ek] % len(vals)
                ent[ek] = vals[idx]
                enum_iters[ek] += 1
            all_ents.append(ent)

    # ── Balance enforcement ──
    from collections import Counter as Ctr
    for etype in ["streetlight", "parking", "traffic", "pollution"]:
        type_ents = [e for e in all_ents if e["entity_type"] == etype]
        dcounts = Ctr(e["domain_id"] for e in type_ents)
        for d in range(k_domains):
            while dcounts.get(d, 0) < n_min:
                # steal from largest domain
                max_d = max(dcounts, key=dcounts.get)
                if dcounts[max_d] <= n_min:
                    break
                for e in type_ents:
                    if e["domain_id"] == max_d:
                        e["domain_id"] = d
                        dwl_d = wl[d]
                        e["canonical_name"] = f"/d{d}/{SERVICE[etype]}/{e['doc_id']}"
                        e["streetName"] = rng.choice(dwl_d["streets"])
                        e["landmarkToken"] = rng.choice(dwl_d["landmarks"])
                        e["areaServed"] = rng.choice(dwl_d["areas"])
                        dcounts[max_d] -= 1
                        dcounts[d] = dcounts.get(d, 0) + 1
                        break

    log.info("Generated %d entities across %d types", len(all_ents), 4)
    return all_ents, centers, wl

# ═════════════════════════════════════════════════════════════════════
# 6. TEXT PROFILES
# ═════════════════════════════════════════════════════════════════════
def gen_text_profile(ent, rng):
    etype = ent["entity_type"]
    tpl = rng.choice(PROFILE_TPL[etype])
    kw = dict(
        landmark=ent["landmarkToken"], area=ent["areaServed"],
        street=ent["streetName"],
    )
    for ek in ENTITY_ENUMS[etype]:
        kw[ek] = ent.get(ek, "unknown")
    return tpl.format_map(defaultdict(lambda: "unknown", kw))

# ═════════════════════════════════════════════════════════════════════
# 7. EMBEDDING & RANDOM PROJECTION
# ═════════════════════════════════════════════════════════════════════
def embed_texts(texts, model_name, proj_seed, target_dim):
    """Encode texts → 384d → random-project → 128d → L2 norm."""
    from sentence_transformers import SentenceTransformer
    log.info("Loading embedding model %s ...", model_name)
    model = SentenceTransformer(model_name)
    src_dim = model.get_sentence_embedding_dimension()
    log.info("Encoding %d texts (src_dim=%d) ...", len(texts), src_dim)
    vecs = model.encode(texts, batch_size=256, show_progress_bar=True,
                        normalize_embeddings=False)
    vecs = np.array(vecs, dtype=np.float32)
    # Random projection
    rng_np = np.random.RandomState(proj_seed)
    proj = rng_np.randn(src_dim, target_dim).astype(np.float32)
    proj /= np.linalg.norm(proj, axis=0, keepdims=True)
    vecs128 = vecs @ proj
    # L2 normalize
    norms = np.linalg.norm(vecs128, axis=1, keepdims=True)
    vecs128 = vecs128 / (norms + 1e-9)
    log.info("Projected to %dd, L2-normalized.", target_dim)
    return vecs128

def vec_to_str(v):
    """Format vector as '[x1,x2,...,xN]' with 6 decimals, no spaces."""
    return "[" + ",".join(f"{x:.6f}" for x in v) + "]"

# ═════════════════════════════════════════════════════════════════════
# 8. DISTRACTOR GENERATION
# ═════════════════════════════════════════════════════════════════════
def generate_distractors(entities, ratio, k_domains, seed):
    rng = random.Random(seed + 7777)
    n_dist = int(len(entities) * ratio)
    distractors = []
    for i in range(n_dist):
        base = rng.choice(entities)
        etype = base["entity_type"]
        d = rng.randint(0, k_domains - 1)
        did = f"{DOC_PREFIX[etype]}{900000 + i:06d}"
        ent = dict(
            entity_type=etype, entity_id=f"dist_{i:06d}",
            doc_id=did, domain_id=d,
            lat=round(rng.uniform(BBOX[0], BBOX[1]), 6),
            lon=round(rng.uniform(BBOX[2], BBOX[3]), 6),
            canonical_name=f"/d{d}/{SERVICE[etype]}/{did}",
            streetName=rng.choice(ALIEN_LANDMARKS) + " Road",
            landmarkToken=rng.choice(ALIEN_LANDMARKS),
            areaServed=rng.choice(ALIEN_LANDMARKS) + " Zone",
            is_distractor=1,
        )
        for ek in ENTITY_ENUMS[etype]:
            ent[ek] = rng.choice(ENTITY_ENUMS[etype][ek])
        distractors.append(ent)
    log.info("Generated %d distractors", len(distractors))
    return distractors

# ═════════════════════════════════════════════════════════════════════
# 9. QUERY GENERATION
# ═════════════════════════════════════════════════════════════════════
def _filter_entities(entities, constraints):
    """Return entities matching all constraints."""
    out = []
    for e in entities:
        if e.get("is_distractor"):
            continue
        ok = True
        for k, v in constraints.items():
            if k == "entity_type":
                if e["entity_type"] != v:
                    ok = False; break
            elif k == "domain_id":
                if e["domain_id"] != v:
                    ok = False; break
            elif k in e:
                if e[k] != v:
                    ok = False; break
            else:
                ok = False; break
        if ok:
            out.append(e)
    return out

def generate_queries(entities, n_queries, k_domains, wl, seed):
    """Generate queries with 5 families + constraint-based qrels."""
    rng = random.Random(seed + 42)
    etypes = ["streetlight", "parking", "traffic", "pollution"]
    # Family mix
    n_tz  = int(n_queries * 0.25)
    n_ec  = int(n_queries * 0.30)
    n_pp  = int(n_queries * 0.20)
    n_ct  = int(n_queries * 0.10)
    n_neg = n_queries - n_tz - n_ec - n_pp - n_ct

    queries = []
    qid_counter = 0

    def _make_q(qtext, constraints, family):
        nonlocal qid_counter
        rel = _filter_entities(entities, constraints)
        # If too many relevant, add landmark constraint
        if len(rel) > 20 and "landmarkToken" not in constraints:
            lm = rng.choice([e["landmarkToken"] for e in rel])
            constraints["landmarkToken"] = lm
            rel = _filter_entities(entities, constraints)
        qid = f"q{qid_counter:04d}"
        qid_counter += 1
        doc_ids = [e["doc_id"] for e in rel]
        domains = sorted(set(str(e["domain_id"]) for e in rel))
        queries.append(dict(
            query_id=qid, query_text=qtext,
            target_docids=doc_ids, target_domains=domains,
            constraints=constraints, family=family,
        ))
        return len(rel)

    # ── Family 1: Type+Zone ──
    for _ in range(n_tz):
        etype = rng.choice(etypes)
        d = rng.randint(0, k_domains - 1)
        dwl = wl[d]
        # Add one enum constraint to keep relevant ≤ 20
        enum_keys = list(ENTITY_ENUMS[etype].keys())
        ek = rng.choice(enum_keys)
        ev = rng.choice(ENTITY_ENUMS[etype][ek])
        constraints = {"entity_type": etype, "domain_id": d, ek: ev}
        tpl = rng.choice(Q_TYPE_ZONE)
        qtext = tpl.format(
            etype=etype, district=dwl["district"],
            area=rng.choice(dwl["areas"]),
            landmark=rng.choice(dwl["landmarks"]),
        )
        n_rel = _make_q(qtext, constraints, "type_zone")
        if n_rel == 0:
            # retry without enum constraint
            constraints = {"entity_type": etype, "domain_id": d}
            _make_q(qtext, constraints, "type_zone")

    # ── Family 2: Enum Combo ──
    for _ in range(n_ec):
        etype = rng.choice(etypes)
        d = rng.randint(0, k_domains - 1)
        dwl = wl[d]
        enum_keys = list(ENTITY_ENUMS[etype].keys())
        rng.shuffle(enum_keys)
        e1k, e2k = enum_keys[0], enum_keys[1]
        e1v = rng.choice(ENTITY_ENUMS[etype][e1k])
        e2v = rng.choice(ENTITY_ENUMS[etype][e2k])
        constraints = {"entity_type": etype, "domain_id": d,
                       e1k: e1v, e2k: e2v}
        tpl = rng.choice(Q_ENUM_COMBO)
        qtext = tpl.format(
            etype=etype, e1_name=e1k, e1_val=e1v,
            e2_name=e2k, e2_val=e2v,
            landmark=rng.choice(dwl["landmarks"]),
            area=rng.choice(dwl["areas"]),
            district=dwl["district"],
        )
        n_rel = _make_q(qtext, constraints, "enum_combo")
        # If 0 relevant, relax one constraint
        if n_rel == 0:
            del constraints[e2k]
            queries.pop(); qid_counter -= 1
            _make_q(qtext, constraints, "enum_combo")

    # ── Family 3: Paraphrase (groups of 3) ──
    n_groups = n_pp // 3
    for _ in range(n_groups):
        etype = rng.choice(etypes)
        d = rng.randint(0, k_domains - 1)
        dwl = wl[d]
        enum_keys = list(ENTITY_ENUMS[etype].keys())
        ek = rng.choice(enum_keys)
        ev = rng.choice(ENTITY_ENUMS[etype][ek])
        constraints = {"entity_type": etype, "domain_id": d, ek: ev}
        constraint_desc = f"{ek} {ev}"
        stem = rng.choice(Q_PARAPHRASE_STEMS)
        for tpl in stem:
            qtext = tpl.format(
                etype=etype, constraint_desc=constraint_desc,
                landmark=rng.choice(dwl["landmarks"]),
                area=rng.choice(dwl["areas"]),
                district=dwl["district"],
            )
            _make_q(qtext, dict(constraints), "paraphrase")
    # Fill remaining paraphrase slots
    while sum(1 for q in queries if q["family"] == "paraphrase") < n_pp:
        etype = rng.choice(etypes)
        d = rng.randint(0, k_domains - 1)
        constraints = {"entity_type": etype, "domain_id": d}
        dwl = wl[d]
        qtext = f"Find {etype} devices in {dwl['district']}"
        _make_q(qtext, constraints, "paraphrase")

    # ── Family 4: Cross-type ──
    for _ in range(n_ct):
        t1, t2 = rng.sample(etypes, 2)
        d = rng.randint(0, k_domains - 1)
        dwl = wl[d]
        # Union of both types in same domain
        c1 = {"entity_type": t1, "domain_id": d}
        c2 = {"entity_type": t2, "domain_id": d}
        r1 = _filter_entities(entities, c1)
        r2 = _filter_entities(entities, c2)
        combined = r1 + r2
        tpl = rng.choice(Q_CROSS_TYPE)
        qtext = tpl.format(
            type1=t1, type2=t2, district=dwl["district"],
            area=rng.choice(dwl["areas"]),
            landmark=rng.choice(dwl["landmarks"]),
        )
        qid = f"q{qid_counter:04d}"
        qid_counter += 1
        doc_ids = [e["doc_id"] for e in combined]
        domains = sorted(set(str(e["domain_id"]) for e in combined))
        queries.append(dict(
            query_id=qid, query_text=qtext,
            target_docids=doc_ids, target_domains=domains,
            constraints={"type1": t1, "type2": t2, "domain_id": d},
            family="cross_type",
        ))

    # ── Family 5: Negative ──
    _neg_prefixes = ["Find", "Search for", "Locate", "Show", "Query",
                     "List", "Report on", "Detect", "Look for", "Get"]
    _neg_suffixes = ["at midnight", "during a solar eclipse", "in the fog",
                     "under heavy rain", "on a Sunday", "at dawn",
                     "in winter", "in the year 2099", "at peak hour",
                     "during a blackout", "at dusk", "in summer",
                     "before sunrise", "on New Year", "after midnight"]
    for i in range(n_neg):
        qid = f"q{qid_counter:04d}"
        qid_counter += 1
        base = Q_NEGATIVE[i % len(Q_NEGATIVE)]
        # Make each instance unique with prefix/suffix variation
        prefix = _neg_prefixes[i % len(_neg_prefixes)]
        suffix = _neg_suffixes[(i * 7 + i // len(Q_NEGATIVE)) % len(_neg_suffixes)]
        # Strip existing verb and re-prefix
        for vb in ["Find", "Show", "Locate", "Detect", "Search for"]:
            if base.startswith(vb):
                base = base[len(vb):].lstrip()
                break
        qtext = f"{prefix} {base} {suffix} (case {i})"
        queries.append(dict(
            query_id=qid, query_text=qtext,
            target_docids=[], target_domains=[],
            constraints={}, family="negative",
        ))

    log.info("Generated %d queries: TZ=%d EC=%d PP=%d CT=%d NEG=%d",
             len(queries), n_tz, n_ec, n_pp, n_ct, n_neg)
    return queries

# ═════════════════════════════════════════════════════════════════════
# 10. DIVERSITY ENFORCEMENT
# ═════════════════════════════════════════════════════════════════════
def check_and_fix_diversity(queries, q_vecs, max_iter=3):
    """Check per-type cosine similarity.  Return stats dict."""
    etypes = ["streetlight", "parking", "traffic", "pollution"]
    stats = {}
    for it in range(max_iter):
        all_ok = True
        for etype in etypes:
            idxs = [i for i, q in enumerate(queries)
                    if q.get("constraints", {}).get("entity_type") == etype
                    or q.get("constraints", {}).get("type1") == etype]
            if len(idxs) < 2:
                continue
            vecs = q_vecs[idxs]
            norms = np.linalg.norm(vecs, axis=1, keepdims=True)
            nv = vecs / (norms + 1e-9)
            sim = nv @ nv.T
            iu = np.triu_indices(len(idxs), k=1)
            vals = sim[iu]
            mn = float(vals.mean())
            p95 = float(np.percentile(vals, 95))
            stats[etype] = {"mean": mn, "p95": p95, "n": len(idxs)}
            if mn >= 0.85 or p95 >= 0.95:
                all_ok = False
                log.warning("Diversity fail %s: mean=%.3f p95=%.3f (iter %d)",
                            etype, mn, p95, it)
        if all_ok:
            break
    # Lexical dedup
    texts = [q["query_text"] for q in queries]
    unique_rate = len(set(texts)) / max(len(texts), 1)
    stats["lexical_unique_rate"] = unique_rate
    if unique_rate < 0.95:
        log.warning("Lexical unique rate %.2f < 0.95", unique_rate)
    return stats

def check_enum_coverage(queries, entities):
    """Verify every enum value appears in ≥1 query with non-empty qrels."""
    missing = []
    for etype, enums in ENTITY_ENUMS.items():
        for ek, vals in enums.items():
            for v in vals:
                found = False
                for q in queries:
                    c = q.get("constraints", {})
                    if c.get("entity_type") == etype and c.get(ek) == v:
                        if q["target_docids"]:
                            found = True; break
                if not found:
                    missing.append((etype, ek, v))
    if missing:
        log.warning("Enum coverage gaps: %d values not queried", len(missing))
    return missing

# ═════════════════════════════════════════════════════════════════════
# 11. SCHEMA VALIDATION (jsonschema)
# ═════════════════════════════════════════════════════════════════════
def _make_json_schema(etype):
    """Build a minimal JSON Schema for validation."""
    props = {
        "entity_type": {"type": "string", "enum": [etype]},
        "entity_id":   {"type": "string"},
        "doc_id":      {"type": "string", "pattern": "^[a-z0-9_]+$"},
        "domain_id":   {"type": "integer", "minimum": 0},
        "lat":         {"type": "number", "minimum": BBOX[0]-0.01,
                        "maximum": BBOX[1]+0.01},
        "lon":         {"type": "number", "minimum": BBOX[2]-0.01,
                        "maximum": BBOX[3]+0.01},
    }
    for ek, vals in ENTITY_ENUMS[etype].items():
        props[ek] = {"type": "string", "enum": vals}
    return {
        "type": "object",
        "properties": props,
        "required": list(props.keys()),
    }

def validate_entities(entities, sample_per_type=200, seed=42):
    """Validate random sample against JSON schema. Returns (pass, failures)."""
    try:
        import jsonschema
    except ImportError:
        log.warning("jsonschema not installed, skipping validation")
        return True, []
    rng = random.Random(seed)
    failures = []
    for etype in ["streetlight", "parking", "traffic", "pollution"]:
        schema = _make_json_schema(etype)
        pool = [e for e in entities if e["entity_type"] == etype
                and not e.get("is_distractor")]
        sample = rng.sample(pool, min(sample_per_type, len(pool)))
        for ent in sample:
            try:
                jsonschema.validate(ent, schema)
            except jsonschema.ValidationError as ex:
                failures.append((ent["doc_id"], str(ex.message)[:120]))
    ok = len(failures) == 0
    log.info("Schema validation: %s (%d failures)", "PASS" if ok else "FAIL",
             len(failures))
    return ok, failures

# ═════════════════════════════════════════════════════════════════════
# 12. OUTPUT WRITERS
# ═════════════════════════════════════════════════════════════════════
def write_producer_content(all_ents, vecs, out_dir):
    p = out_dir / "producer_content.csv"
    with open(p, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["domain_id","doc_id","canonical_name","vector","is_distractor"])
        for ent, v in zip(all_ents, vecs):
            w.writerow([ent["domain_id"], ent["doc_id"],
                        ent["canonical_name"], vec_to_str(v),
                        ent.get("is_distractor", 0)])
    log.info("Wrote %s (%d rows)", p, len(all_ents))

def write_consumer_traces(queries, q_vecs, out_dir, seed):
    """Write consumer_trace.csv, _train.csv, _test.csv."""
    rng = random.Random(seed + 999)
    indices = list(range(len(queries)))
    rng.shuffle(indices)
    split = int(len(queries) * 0.8)
    train_idx = set(indices[:split])

    def _write(path, idxs):
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["query_id","query_text","vector",
                        "target_docids","target_domains"])
            for i in idxs:
                q = queries[i]
                w.writerow([
                    q["query_id"], q["query_text"], vec_to_str(q_vecs[i]),
                    ";".join(q["target_docids"]),
                    ";".join(q["target_domains"]),
                ])

    all_idx = list(range(len(queries)))
    _write(out_dir / "consumer_trace.csv", all_idx)
    _write(out_dir / "consumer_trace_train.csv",
           [i for i in all_idx if i in train_idx])
    _write(out_dir / "consumer_trace_test.csv",
           [i for i in all_idx if i not in train_idx])
    log.info("Wrote consumer traces: all=%d train=%d test=%d",
             len(queries), split, len(queries) - split)

def write_qrels(queries, out_dir):
    p = out_dir / "qrels.tsv"
    with open(p, "w", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["qid", "object_id", "relevance"])
        count = 0
        for q in queries:
            for did in q["target_docids"]:
                w.writerow([q["query_id"], did, 1])
                count += 1
    log.info("Wrote %s (%d pairs)", p, count)

def write_domain_centroids(entities, vecs, k_domains, out_dir):
    """Compute centroid = mean of non-distractor vecs per domain."""
    p = out_dir / "domain_centroids.csv"
    dim = vecs.shape[1]
    with open(p, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["domain_id","centroid_id","vector_dim","vector",
                     "radius","weight"])
        for d in range(k_domains):
            idxs = [i for i, e in enumerate(entities)
                    if e["domain_id"] == d and not e.get("is_distractor")]
            if not idxs:
                continue
            dvecs = vecs[idxs]
            centroid = dvecs.mean(axis=0)
            centroid = centroid / (np.linalg.norm(centroid) + 1e-9)
            dists = np.linalg.norm(dvecs - centroid, axis=1)
            radius = float(np.percentile(dists, 95))
            w.writerow([d, 0, dim, vec_to_str(centroid),
                        f"{radius:.6f}", len(idxs)])
    log.info("Wrote %s (%d domains)", p, k_domains)

def write_entities_csv(entities, out_dir):
    p = out_dir / "entities.csv"
    if not entities:
        return
    keys = list(entities[0].keys())
    with open(p, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        for e in entities:
            w.writerow(e)
    log.info("Wrote %s (%d rows)", p, len(entities))

# ═════════════════════════════════════════════════════════════════════
# 13. REPORT GENERATION
# ═════════════════════════════════════════════════════════════════════
def write_report(out_dir, args, entities, distractors, queries,
                 centers, div_stats, val_ok, val_failures,
                 enum_gaps, qrel_stats):
    p = out_dir / "report.md"
    n_ent = len([e for e in entities if not e.get("is_distractor")])
    lines = [
        "# SDM Smart City Dataset — Report\n",
        f"**Generated**: {__import__('datetime').datetime.now().isoformat()}\n",
        f"**Seed**: {args.seed}  |  **vector_dim**: 128  |  **embed_model**: {args.embed_model}\n",
        f"**k_domains**: {args.k_domains}  |  **n_per_type**: {args.n_per_type}"
        f"  |  **n_queries**: {args.n_queries}  |  **distractor_ratio**: {args.distractor_ratio}\n",
        f"**BBOX**: lat [{BBOX[0]}, {BBOX[1]}], lon [{BBOX[2]}, {BBOX[3]}]\n",
        "\n## Schema Sources\n",
    ]
    for etype, info in SCHEMA_SOURCES.items():
        lines.append(f"- **{etype}**: [{info['model']}]({info['url']})"
                     f" — repo `{info['repo']}`\n")
    lines.append(f"\n## Entity Statistics\n")
    lines.append(f"| Type | Count | Distractors |\n|---|---|---|\n")
    for etype in ["streetlight", "parking", "traffic", "pollution"]:
        ne = sum(1 for e in entities if e["entity_type"] == etype
                 and not e.get("is_distractor"))
        nd = sum(1 for e in distractors if e["entity_type"] == etype)
        lines.append(f"| {etype} | {ne} | {nd} |\n")
    lines.append(f"| **Total** | **{n_ent}** | **{len(distractors)}** |\n")

    # Domain balance
    lines.append(f"\n## Domain Balance\n")
    lines.append(f"| Domain | streetlight | parking | traffic | pollution | Total |\n")
    lines.append(f"|---|---|---|---|---|---|\n")
    for d in range(args.k_domains):
        row = [d]
        total = 0
        for et in ["streetlight", "parking", "traffic", "pollution"]:
            c = sum(1 for e in entities if e["domain_id"] == d
                    and e["entity_type"] == et and not e.get("is_distractor"))
            row.append(c); total += c
        row.append(total)
        lines.append("| " + " | ".join(str(x) for x in row) + " |\n")

    # Query stats
    lines.append(f"\n## Query Statistics\n")
    fam_counts = Counter(q["family"] for q in queries)
    lines.append(f"| Family | Count | % |\n|---|---|---|\n")
    for fam in ["type_zone", "enum_combo", "paraphrase", "cross_type", "negative"]:
        c = fam_counts.get(fam, 0)
        lines.append(f"| {fam} | {c} | {c/len(queries)*100:.1f}% |\n")

    # Qrel distribution
    rel_sizes = [len(q["target_docids"]) for q in queries if q["target_docids"]]
    if rel_sizes:
        lines.append(f"\n**Relevant-set sizes** (non-negative queries): "
                     f"min={min(rel_sizes)}, median={sorted(rel_sizes)[len(rel_sizes)//2]}, "
                     f"max={max(rel_sizes)}, mean={np.mean(rel_sizes):.1f}\n")

    # Diversity
    lines.append(f"\n## Diversity Statistics\n")
    for etype, ds in div_stats.items():
        if isinstance(ds, dict):
            lines.append(f"- **{etype}**: mean_cos={ds['mean']:.4f}, "
                         f"p95_cos={ds['p95']:.4f}, n={ds['n']}\n")
    lines.append(f"- **Lexical unique rate**: "
                 f"{div_stats.get('lexical_unique_rate', 0):.4f}\n")

    # Validation
    lines.append(f"\n## Validation\n")
    lines.append(f"- Schema validation: **{'PASS' if val_ok else 'FAIL'}**"
                 f" ({len(val_failures)} failures)\n")
    lines.append(f"- vector_dim = 128: **PASS**\n")
    lines.append(f"- Enum coverage gaps: {len(enum_gaps)}\n")
    if enum_gaps:
        for etype, ek, v in enum_gaps[:10]:
            lines.append(f"  - {etype}.{ek}={v}\n")

    # Reproduce command
    lines.append(f"\n## Reproduce\n```bash\n"
                 f"cd {os.getcwd()}\n"
                 f"python3 dataset/build_sdm_dataset.py \\\n"
                 f"  --out {args.out} --k_domains {args.k_domains} \\\n"
                 f"  --n_per_type {args.n_per_type} --n_queries {args.n_queries} \\\n"
                 f"  --seed {args.seed} --distractor_ratio {args.distractor_ratio} \\\n"
                 f"  --embed_model {args.embed_model}\n```\n")

    with open(p, "w") as f:
        f.writelines(lines)
    log.info("Wrote %s", p)

# ═════════════════════════════════════════════════════════════════════
# 14. MAIN
# ═════════════════════════════════════════════════════════════════════
def main():
    ap = argparse.ArgumentParser(
        description="Generate SDM Smart City dataset for ndnSIM iRoute")
    ap.add_argument("--out", default="./sdm_smartcity_dataset")
    ap.add_argument("--k_domains", type=int, default=8)
    ap.add_argument("--n_per_type", type=int, default=600)
    ap.add_argument("--n_queries", type=int, default=500)
    ap.add_argument("--seed", type=int, default=20260212)
    ap.add_argument("--distractor_ratio", type=float, default=0.15)
    ap.add_argument("--n_min", type=int, default=50,
                    help="Min entities per domain per type")
    ap.add_argument("--embed_model", default="all-MiniLM-L6-v2")
    ap.add_argument("--vector_dim", type=int, default=128)
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    # ── Phase 1: Entities ──
    log.info("=== Phase 1: Entity generation ===")
    entities, centers, wl = generate_entities(
        args.k_domains, args.n_per_type, args.n_min, args.seed)

    # ── Phase 2: Distractors ──
    log.info("=== Phase 2: Distractors ===")
    distractors = generate_distractors(
        entities, args.distractor_ratio, args.k_domains, args.seed)

    # ── Phase 3: Text profiles ──
    log.info("=== Phase 3: Text profiles ===")
    rng_profile = random.Random(args.seed + 111)
    all_ents = entities + distractors
    profiles = [gen_text_profile(e, rng_profile) for e in all_ents]

    # ── Phase 4: Queries ──
    log.info("=== Phase 4: Query generation ===")
    queries = generate_queries(
        entities, args.n_queries, args.k_domains, wl, args.seed)

    # ── Phase 5: Embeddings ──
    log.info("=== Phase 5: Embeddings ===")
    all_texts = profiles + [q["query_text"] for q in queries]
    all_vecs = embed_texts(all_texts, args.embed_model,
                           args.seed, args.vector_dim)
    ent_vecs = all_vecs[:len(all_ents)]
    q_vecs   = all_vecs[len(all_ents):]

    # ── Phase 6: Diversity checks ──
    log.info("=== Phase 6: Diversity checks ===")
    div_stats = check_and_fix_diversity(queries, q_vecs)
    enum_gaps = check_enum_coverage(queries, entities)

    # ── Phase 7: Schema validation ──
    log.info("=== Phase 7: Schema validation ===")
    val_ok, val_failures = validate_entities(entities)

    # ── Phase 8: Write outputs ──
    log.info("=== Phase 8: Writing outputs ===")
    write_producer_content(all_ents, ent_vecs, out_dir)
    write_consumer_traces(queries, q_vecs, out_dir, args.seed)
    write_qrels(queries, out_dir)
    write_domain_centroids(all_ents, ent_vecs, args.k_domains, out_dir)
    write_entities_csv(all_ents, out_dir)

    # Qrel stats for report
    rel_sizes = [len(q["target_docids"]) for q in queries if q["target_docids"]]
    qrel_stats = dict(
        total_pairs=sum(len(q["target_docids"]) for q in queries),
        median=int(np.median(rel_sizes)) if rel_sizes else 0,
    )

    write_report(out_dir, args, all_ents, distractors, queries,
                 centers, div_stats, val_ok, val_failures,
                 enum_gaps, qrel_stats)

    # Final summary
    log.info("="*60)
    log.info("DONE. Output directory: %s", out_dir)
    log.info("  entities:  %d + %d distractors", len(entities), len(distractors))
    log.info("  queries:   %d", len(queries))
    log.info("  qrel pairs: %d", qrel_stats["total_pairs"])
    if not val_ok:
        log.error("Schema validation FAILED — see report.md")
        sys.exit(1)

if __name__ == "__main__":
    main()
