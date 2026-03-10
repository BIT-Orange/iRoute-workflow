/**
 * @file iroute-v2-exp9-comprehensive.cc
 * @brief Exp9: Offline analytical sweep (no packet-level simulation)
 *
 * This experiment is an analytical parameter scan that uses real centroids/queries
 * and Rocketfuel-derived shortest-path costs. It does NOT run packet-level LSA
 * dissemination. Outputs only:
 *   - Accuracy (domain/doc hit rates)
 *   - Probes (avg probes)
 *   - Compute (avg compute ops, estimated)
 *   - Estimated bytes (control-plane discovery bytes, LSA bytes)
 *
 * Output files:
 *   - exp9_hitk.csv
 *   - exp9_tau_sweep.csv
 *   - exp9_ab_sweep.csv
 *   - exp9_overhead.csv
 */

#include "ns3/core-module.h"

#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-manager.hpp"

#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <queue>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <iomanip>
#include <numeric>
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("iRouteExp9Comprehensive");

// =============================================================================
// Global parameters (analytical)
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_M = 4;
static uint32_t g_queries = 50;
static uint32_t g_kMax = 5;
static double g_tau = 0.3;
static double g_alpha = 0.7;
static double g_beta = 0.3;
static double g_lambda = 10.0;
static std::string g_resultDir = "results/exp9";

// Topology and data files
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_topoSize = 150;
static uint32_t g_ingressNodeId = 0;
static std::string g_centroidsFile = "dataset/trec_dl_combined_dim128/domain_centroids.csv";
static std::string g_traceFile = "dataset/trec_dl_combined_dim128/consumer_trace.csv";
static std::string g_contentFile = "dataset/trec_dl_combined_dim128/producer_content.csv";

// Experiment mode
static std::string g_mode = "full";  // "full", "hitk", "tau_sweep", "ab_sweep", "overhead"

// Analytical constants
static const uint32_t kDiscExchangeBytes = 200; // estimated bytes per discovery probe

// =============================================================================
// Utility functions
// =============================================================================

void CreateDirectoryIfNotExist(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        mkdir(path.c_str(), 0755);
    }
}

iroute::SemanticVector MakeSemanticVector(const std::vector<float>& v) {
    iroute::SemanticVector sv(v);
    sv.normalize();
    return sv;
}

std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> result;
    std::string current;
    bool inQuotes = false;
    int bracketDepth = 0;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == '[') {
            bracketDepth++;
            current += c;
        } else if (c == ']') {
            bracketDepth--;
            current += c;
        } else if (c == ',' && !inQuotes && bracketDepth == 0) {
            result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty() || !result.empty()) {
        result.push_back(current);
    }
    return result;
}

std::vector<float> ParseVectorString(const std::string& str) {
    std::vector<float> result;
    std::string cleaned = str;
    if (!cleaned.empty() && cleaned.front() == '"') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == '"') cleaned.pop_back();
    if (!cleaned.empty() && cleaned.front() == '[') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == ']') cleaned.pop_back();

    std::stringstream ss(cleaned);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            result.push_back(std::stof(token));
        } catch (...) {}
    }
    return result;
}

// =============================================================================
// Rocketfuel topology helpers (offline)
// =============================================================================

using AdjList = std::map<uint32_t, std::vector<uint32_t>>;

AdjList ParseRocketfuelAdjacency(const std::string& topoFile)
{
    AdjList adj;
    std::ifstream file(topoFile);
    if (!file.is_open()) {
        NS_FATAL_ERROR("Cannot open topology file: " << topoFile);
    }

    std::string line;
    bool inLinkSection = false;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.find("link") != std::string::npos && line.find("Node") == std::string::npos) {
            inLinkSection = true;
            continue;
        }
        if (line.find("router") != std::string::npos) {
            inLinkSection = false;
            continue;
        }

        if (inLinkSection) {
            std::istringstream iss(line);
            std::string node1, node2;
            if (iss >> node1 >> node2) {
                if (node1.rfind("Node", 0) == 0 && node2.rfind("Node", 0) == 0) {
                    uint32_t id1 = std::stoul(node1.substr(4));
                    uint32_t id2 = std::stoul(node2.substr(4));
                    adj[id1].push_back(id2);
                    adj[id2].push_back(id1);
                }
            }
        }
    }

    return adj;
}

std::vector<uint32_t> SelectConnectedSubgraph(const AdjList& adj, uint32_t targetSize, uint32_t seed)
{
    std::vector<uint32_t> selected;
    if (adj.empty()) return selected;

    targetSize = std::min<uint32_t>(targetSize, adj.size());

    std::queue<uint32_t> q;
    std::unordered_set<uint32_t> visited;

    if (!adj.count(seed)) {
        seed = adj.begin()->first;
    }

    q.push(seed);
    visited.insert(seed);

    while (!q.empty() && selected.size() < targetSize) {
        uint32_t u = q.front();
        q.pop();
        selected.push_back(u);

        auto it = adj.find(u);
        if (it == adj.end()) continue;
        for (uint32_t v : it->second) {
            if (visited.insert(v).second) {
                q.push(v);
            }
        }
    }

    if (selected.size() < targetSize) {
        for (const auto& kv : adj) {
            if (selected.size() >= targetSize) break;
            if (visited.insert(kv.first).second) {
                selected.push_back(kv.first);
            }
        }
    }

    return selected;
}

std::map<uint32_t, uint32_t> BFS(const AdjList& adj,
                                uint32_t src,
                                const std::unordered_set<uint32_t>& allowed)
{
    std::map<uint32_t, uint32_t> dist;
    if (!allowed.count(src)) return dist;

    std::queue<uint32_t> q;
    dist[src] = 0;
    q.push(src);

    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        uint32_t du = dist[u];

        auto it = adj.find(u);
        if (it == adj.end()) continue;
        for (uint32_t v : it->second) {
            if (!allowed.count(v)) continue;
            if (!dist.count(v)) {
                dist[v] = du + 1;
                q.push(v);
            }
        }
    }
    return dist;
}

uint32_t PickLowestDegreeNode(const AdjList& adj, const std::unordered_set<uint32_t>& allowed)
{
    uint32_t bestId = 0;
    uint32_t bestDeg = UINT32_MAX;
    for (const auto& kv : adj) {
        if (!allowed.count(kv.first)) continue;
        uint32_t deg = 0;
        for (auto v : kv.second) if (allowed.count(v)) deg++;
        if (deg < bestDeg) {
            bestDeg = deg;
            bestId = kv.first;
        }
    }
    return bestId;
}

std::vector<uint32_t> SelectGatewayNodes(const AdjList& adj,
                                         const std::vector<uint32_t>& nodes,
                                         uint32_t numDomains,
                                         uint32_t ingressId)
{
    std::unordered_set<uint32_t> allowed(nodes.begin(), nodes.end());
    std::vector<uint32_t> gateways;
    std::unordered_set<uint32_t> selected;

    auto distFromIngress = BFS(adj, ingressId, allowed);
    uint32_t farthestId = ingressId;
    uint32_t maxDist = 0;
    for (uint32_t n : nodes) {
        if (distFromIngress.count(n) && distFromIngress[n] > maxDist) {
            maxDist = distFromIngress[n];
            farthestId = n;
        }
    }
    gateways.push_back(farthestId);
    selected.insert(farthestId);

    while (gateways.size() < numDomains) {
        std::map<uint32_t, uint32_t> minDistToGateway;
        for (uint32_t n : nodes) {
            minDistToGateway[n] = UINT32_MAX;
        }

        for (uint32_t gw : gateways) {
            auto dist = BFS(adj, gw, allowed);
            for (uint32_t n : nodes) {
                if (dist.count(n) && dist[n] < minDistToGateway[n]) {
                    minDistToGateway[n] = dist[n];
                }
            }
        }

        uint32_t bestNode = 0;
        uint32_t bestDist = 0;
        for (const auto& kv : minDistToGateway) {
            if (selected.count(kv.first)) continue;
            if (kv.second != UINT32_MAX && kv.second > bestDist) {
                bestDist = kv.second;
                bestNode = kv.first;
            }
        }

        if (bestDist == 0) break;
        gateways.push_back(bestNode);
        selected.insert(bestNode);
    }

    return gateways;
}

struct TopologyCosts {
    uint32_t numNodes = 0;
    double avgHops = 1.0;
    std::vector<uint32_t> gatewayNodes;
    uint32_t ingressId = 0;
    std::map<uint32_t, double> domainCost; // domainId -> hop cost
};

TopologyCosts BuildRocketfuelCosts(uint32_t domains, uint32_t topoSize, uint32_t ingressNodeId,
                                   const std::string& topoFile)
{
    TopologyCosts result;

    auto adj = ParseRocketfuelAdjacency(topoFile);
    if (adj.empty()) {
        NS_FATAL_ERROR("Empty topology: " << topoFile);
    }

    uint32_t seed = ingressNodeId;
    if (seed == 0 || !adj.count(seed)) {
        seed = adj.begin()->first;
    }

    auto selected = SelectConnectedSubgraph(adj, topoSize, seed);
    std::unordered_set<uint32_t> allowed(selected.begin(), selected.end());

    uint32_t ingressId = ingressNodeId;
    if (ingressId == 0 || !allowed.count(ingressId)) {
        ingressId = PickLowestDegreeNode(adj, allowed);
    }

    auto gateways = SelectGatewayNodes(adj, selected, domains, ingressId);
    if (gateways.size() < domains) {
        domains = gateways.size();
    }

    auto distFromIngress = BFS(adj, ingressId, allowed);

    double hopSum = 0.0;
    uint32_t hopCount = 0;
    for (uint32_t n : selected) {
        if (distFromIngress.count(n)) {
            hopSum += distFromIngress[n];
            hopCount++;
        }
    }
    double avgHops = (hopCount > 0) ? hopSum / hopCount : 1.0;

    result.numNodes = selected.size();
    result.avgHops = std::max(1.0, avgHops);
    result.gatewayNodes = gateways;
    result.ingressId = ingressId;

    for (uint32_t i = 0; i < domains; ++i) {
        uint32_t gw = gateways[i];
        double cost = 1.0;
        if (distFromIngress.count(gw)) {
            cost = std::max(1.0, static_cast<double>(distFromIngress[gw]));
        }
        result.domainCost[i] = cost;
    }

    return result;
}

// =============================================================================
// Data structures
// =============================================================================

struct QueryEntry {
    std::string queryId;
    iroute::SemanticVector vector;
    std::set<std::string> targetDocIds;
    std::set<uint32_t> targetDomains;
};

struct CentroidData {
    uint32_t domainId;
    uint32_t centroidId;
    iroute::SemanticVector vector;
    double radius;
    double weight;
};

struct ContentData {
    uint32_t domainId;
    std::string docId;
    std::string canonicalName;
    iroute::SemanticVector vector;
    bool isDistractor;
};

// =============================================================================
// LSA Overhead Calculator (analytical)
// =============================================================================

struct LsaOverhead {
    uint32_t lsaHeaderBytes = 0;
    uint32_t centroidListBytes = 0;
    uint32_t totalLsaBytes = 0;
    uint32_t totalLsaBroadcastBytes = 0;
    uint32_t lsdbEntries = 0;
    uint32_t ribEntries = 0;
    uint32_t vectorBytes = 0;
    uint32_t metadataBytes = 0;
};

LsaOverhead CalculateLsaOverhead(uint32_t domains, uint32_t M, uint32_t vectorDim,
                                 uint32_t avgHops)
{
    LsaOverhead oh;

    const uint32_t TLV_TYPE_LEN = 1;
    const uint32_t TLV_LENGTH_LEN = 2;
    const uint32_t SIGNATURE_BYTES = 256;
    const uint32_t NAME_BYTES = 50;

    oh.vectorBytes = 4 * vectorDim + TLV_TYPE_LEN + TLV_LENGTH_LEN + 4;
    oh.metadataBytes = 4 + 8 + 8 + 3 * (TLV_TYPE_LEN + TLV_LENGTH_LEN);

    uint32_t perCentroidBytes = oh.vectorBytes + oh.metadataBytes + TLV_TYPE_LEN + TLV_LENGTH_LEN;
    oh.centroidListBytes = M * perCentroidBytes + TLV_TYPE_LEN + TLV_LENGTH_LEN;
    oh.lsaHeaderBytes = NAME_BYTES + SIGNATURE_BYTES + 20;
    oh.totalLsaBytes = oh.lsaHeaderBytes + oh.centroidListBytes;

    oh.totalLsaBroadcastBytes = oh.totalLsaBytes * domains * avgHops;
    oh.lsdbEntries = domains * M;
    oh.ribEntries = domains;

    return oh;
}

// =============================================================================
// Hit@K Analysis
// =============================================================================

struct HitAtKResult {
    uint32_t k;
    double domainHitRate;
    double docHitRate;
    double avgProbes;
    double avgComputeOps;
    double avgCtrlBytes;
};

struct SweepResult {
    double param1;
    double param2;
    double domainAccuracy;
    double docAccuracy;
    double avgProbes;
    double avgComputeOps;
    double avgCtrlBytes;
};

std::vector<HitAtKResult> ComputeHitAtK(
    const std::vector<QueryEntry>& queries,
    const std::map<uint32_t, std::vector<ContentData>>& domainContent,
    iroute::RouteManager& rm,
    uint32_t maxK,
    double tau,
    uint64_t totalCentroids)
{
    std::vector<HitAtKResult> results;

    for (uint32_t k = 1; k <= maxK; ++k) {
        HitAtKResult res;
        res.k = k;
        res.domainHitRate = 0;
        res.docHitRate = 0;
        res.avgProbes = 0;
        res.avgComputeOps = 0;
        res.avgCtrlBytes = 0;

        uint32_t domainHits = 0;
        uint32_t docHits = 0;

        for (const auto& query : queries) {
            auto domains = rm.findBestDomainsV2(query.vector, k, 1);

            bool domainHit = false;
            for (const auto& d : domains) {
                std::string domainName = d.domainId.toUri();
                if (domainName.size() > 7) {
                    uint32_t domId = std::stoul(domainName.substr(7));
                    if (query.targetDomains.count(domId) > 0) {
                        domainHit = true;
                        break;
                    }
                }
            }
            if (domainHit) domainHits++;

            uint64_t docScans = 0;
            if (!domains.empty()) {
                std::string bestDomainName = domains[0].domainId.toUri();
                if (bestDomainName.size() > 7) {
                    uint32_t bestDomId = std::stoul(bestDomainName.substr(7));
                    auto it = domainContent.find(bestDomId);
                    if (it != domainContent.end()) {
                        double bestSim = -1;
                        std::string bestDocId;
                        for (const auto& doc : it->second) {
                            docScans++;
                            double sim = query.vector.dot(doc.vector);
                            if (sim > bestSim) {
                                bestSim = sim;
                                bestDocId = doc.docId;
                            }
                        }
                        if (query.targetDocIds.count(bestDocId) > 0) {
                            docHits++;
                        }
                    }
                }
            }

            uint32_t probes = 0;
            for (const auto& d : domains) {
                if (d.confidence >= tau) probes++;
            }

            res.avgProbes += probes;
            res.avgCtrlBytes += probes * kDiscExchangeBytes;
            res.avgComputeOps += static_cast<double>(totalCentroids + docScans);
        }

        size_t n = std::max<size_t>(1, queries.size());
        res.domainHitRate = static_cast<double>(domainHits) / n;
        res.docHitRate = static_cast<double>(docHits) / n;
        res.avgProbes /= n;
        res.avgCtrlBytes /= n;
        res.avgComputeOps /= n;

        results.push_back(res);
    }

    return results;
}

std::vector<SweepResult> TauSweep(
    const std::vector<QueryEntry>& queries,
    const std::map<uint32_t, std::vector<ContentData>>& domainContent,
    iroute::RouteManager& rm,
    uint32_t kMax,
    const std::vector<double>& tauValues,
    uint64_t totalCentroids)
{
    std::vector<SweepResult> results;

    for (double tau : tauValues) {
        SweepResult res;
        res.param1 = tau;
        res.param2 = kMax;
        res.domainAccuracy = 0;
        res.docAccuracy = 0;
        res.avgProbes = 0;
        res.avgComputeOps = 0;
        res.avgCtrlBytes = 0;

        uint32_t stage1Correct = 0;
        uint32_t stage2Correct = 0;

        for (const auto& query : queries) {
            auto domains = rm.findBestDomainsV2(query.vector, kMax, 1);

            uint32_t probes = 0;
            bool domainHit = false;
            bool docHit = false;

            uint64_t docScans = 0;

            for (const auto& d : domains) {
                if (d.confidence < tau) continue;
                probes++;

                std::string domainName = d.domainId.toUri();
                if (domainName.size() > 7) {
                    uint32_t domId = std::stoul(domainName.substr(7));
                    if (query.targetDomains.count(domId) > 0) {
                        domainHit = true;
                    }

                    if (probes == 1) {
                        auto it = domainContent.find(domId);
                        if (it != domainContent.end()) {
                            double bestSim = -1;
                            std::string bestDocId;
                            for (const auto& doc : it->second) {
                                docScans++;
                                double sim = query.vector.dot(doc.vector);
                                if (sim > bestSim) {
                                    bestSim = sim;
                                    bestDocId = doc.docId;
                                }
                            }
                            if (query.targetDocIds.count(bestDocId) > 0) {
                                docHit = true;
                            }
                        }
                    }
                }
            }

            if (domainHit) stage1Correct++;
            if (docHit) stage2Correct++;

            res.avgProbes += probes;
            res.avgCtrlBytes += probes * kDiscExchangeBytes;
            res.avgComputeOps += static_cast<double>(totalCentroids + docScans);
        }

        size_t n = std::max<size_t>(1, queries.size());
        res.domainAccuracy = static_cast<double>(stage1Correct) / n;
        res.docAccuracy = static_cast<double>(stage2Correct) / n;
        res.avgProbes /= n;
        res.avgCtrlBytes /= n;
        res.avgComputeOps /= n;

        results.push_back(res);
    }

    return results;
}

std::vector<SweepResult> AlphaBetaSweep(
    const std::vector<QueryEntry>& queries,
    const std::map<uint32_t, std::vector<ContentData>>& domainContent,
    iroute::RouteManager& rm,
    uint32_t kMax,
    double tau,
    const std::vector<std::pair<double, double>>& abValues,
    uint64_t totalCentroids)
{
    std::vector<SweepResult> results;

    for (const auto& [alpha, beta] : abValues) {
        rm.setAlpha(alpha);
        rm.setBeta(beta);

        SweepResult res;
        res.param1 = alpha;
        res.param2 = beta;
        res.domainAccuracy = 0;
        res.docAccuracy = 0;
        res.avgProbes = 0;
        res.avgComputeOps = 0;
        res.avgCtrlBytes = 0;

        uint32_t stage1Correct = 0;
        uint32_t stage2Correct = 0;

        for (const auto& query : queries) {
            auto domains = rm.findBestDomainsV2(query.vector, kMax, 1);

            uint32_t probes = 0;
            bool domainHit = false;
            bool docHit = false;
            uint64_t docScans = 0;

            for (const auto& d : domains) {
                if (d.confidence < tau) continue;
                probes++;

                std::string domainName = d.domainId.toUri();
                if (domainName.size() > 7) {
                    uint32_t domId = std::stoul(domainName.substr(7));
                    if (query.targetDomains.count(domId) > 0) {
                        domainHit = true;
                    }

                    if (probes == 1) {
                        auto it = domainContent.find(domId);
                        if (it != domainContent.end()) {
                            double bestSim = -1;
                            std::string bestDocId;
                            for (const auto& doc : it->second) {
                                docScans++;
                                double sim = query.vector.dot(doc.vector);
                                if (sim > bestSim) {
                                    bestSim = sim;
                                    bestDocId = doc.docId;
                                }
                            }
                            if (query.targetDocIds.count(bestDocId) > 0) {
                                docHit = true;
                            }
                        }
                    }
                }
            }

            if (domainHit) stage1Correct++;
            if (docHit) stage2Correct++;

            res.avgProbes += probes;
            res.avgCtrlBytes += probes * kDiscExchangeBytes;
            res.avgComputeOps += static_cast<double>(totalCentroids + docScans);
        }

        size_t n = std::max<size_t>(1, queries.size());
        res.domainAccuracy = static_cast<double>(stage1Correct) / n;
        res.docAccuracy = static_cast<double>(stage2Correct) / n;
        res.avgProbes /= n;
        res.avgCtrlBytes /= n;
        res.avgComputeOps /= n;

        results.push_back(res);
    }

    return results;
}

// =============================================================================
// Data Loading Functions
// =============================================================================

std::vector<QueryEntry> LoadQueries(const std::string& filepath) {
    std::vector<QueryEntry> queries;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open trace file: " << filepath);
        return queries;
    }

    std::string line;
    std::getline(file, line);
    auto header = ParseCsvLine(line);

    int queryIdCol = 0, vectorCol = 2, targetDocidsCol = 3, targetDomainsCol = 4;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "query_id") queryIdCol = i;
        else if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "target_docids") targetDocidsCol = i;
        else if (header[i] == "target_domains") targetDomainsCol = i;
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 4) continue;

        QueryEntry q;
        q.queryId = fields[queryIdCol];
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        q.vector = MakeSemanticVector(vec);

        std::string domainStr = fields[targetDomainsCol];
        if (!domainStr.empty() && domainStr.front() == '"') domainStr = domainStr.substr(1);
        if (!domainStr.empty() && domainStr.back() == '"') domainStr.pop_back();
        std::stringstream ss(domainStr);
        std::string token;
        while (std::getline(ss, token, ';')) {
            if (token.size() > 7 && token.substr(0, 7) == "/domain") {
                try {
                    q.targetDomains.insert(std::stoul(token.substr(7)));
                } catch (...) {}
            }
        }

        std::string docStr = fields[targetDocidsCol];
        if (!docStr.empty() && docStr.front() == '"') docStr = docStr.substr(1);
        if (!docStr.empty() && docStr.back() == '"') docStr.pop_back();
        std::stringstream ss2(docStr);
        while (std::getline(ss2, token, ';')) {
            if (!token.empty()) q.targetDocIds.insert(token);
        }

        queries.push_back(q);
    }

    NS_LOG_INFO("Loaded " << queries.size() << " queries from " << filepath);
    return queries;
}

std::vector<CentroidData> LoadCentroids(const std::string& filepath) {
    std::vector<CentroidData> centroids;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open centroids file: " << filepath);
        return centroids;
    }

    std::string line;
    std::getline(file, line);
    auto header = ParseCsvLine(line);

    int domainIdCol = 0, centroidIdCol = 1, vectorCol = 3, radiusCol = 4, weightCol = 5;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "domain_id") domainIdCol = i;
        else if (header[i] == "centroid_id") centroidIdCol = i;
        else if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "radius") radiusCol = i;
        else if (header[i] == "weight") weightCol = i;
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 5) continue;

        CentroidData c;
        c.domainId = std::stoul(fields[domainIdCol]);
        c.centroidId = std::stoul(fields[centroidIdCol]);
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        c.vector = MakeSemanticVector(vec);
        c.radius = std::stod(fields[radiusCol]);
        c.weight = std::stod(fields[weightCol]);

        centroids.push_back(c);
    }

    NS_LOG_INFO("Loaded " << centroids.size() << " centroids from " << filepath);
    return centroids;
}

std::map<uint32_t, std::vector<ContentData>> LoadContent(const std::string& filepath) {
    std::map<uint32_t, std::vector<ContentData>> content;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open content file: " << filepath);
        return content;
    }

    std::string line;
    std::getline(file, line);
    auto header = ParseCsvLine(line);

    int domainIdCol = 0, docIdCol = 1, canonicalNameCol = 2, vectorCol = 3, isDistractorCol = -1;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "domain_id") domainIdCol = i;
        else if (header[i] == "doc_id") docIdCol = i;
        else if (header[i] == "canonical_name") canonicalNameCol = i;
        else if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "is_distractor") isDistractorCol = i;
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 4) continue;

        ContentData c;
        c.domainId = std::stoul(fields[domainIdCol]);
        c.docId = fields[docIdCol];
        c.canonicalName = fields[canonicalNameCol];
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        c.vector = MakeSemanticVector(vec);
        c.isDistractor = false;
        if (isDistractorCol >= 0 && static_cast<size_t>(isDistractorCol) < fields.size()) {
            c.isDistractor = (fields[isDistractorCol] == "1");
        }

        content[c.domainId].push_back(c);
    }

    return content;
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("run", "Run number", g_run);
    cmd.AddValue("vectorDim", "Vector dimension", g_vectorDim);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("M", "Centroids per domain", g_M);
    cmd.AddValue("queries", "Number of queries", g_queries);
    cmd.AddValue("kMax", "Max probing limit", g_kMax);
    cmd.AddValue("tau", "Score threshold", g_tau);
    cmd.AddValue("alpha", "Semantic similarity weight", g_alpha);
    cmd.AddValue("beta", "Cost penalty weight", g_beta);
    cmd.AddValue("lambda", "Gate steepness", g_lambda);
    cmd.AddValue("resultDir", "Output directory", g_resultDir);
    cmd.AddValue("topoFile", "Rocketfuel topology file", g_topoFile);
    cmd.AddValue("topoSize", "Rocketfuel subgraph size", g_topoSize);
    cmd.AddValue("ingressNodeId", "Ingress node id (0=auto)", g_ingressNodeId);
    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("traceFile", "Path to consumer_trace.csv", g_traceFile);
    cmd.AddValue("contentFile", "Path to producer_content.csv", g_contentFile);
    cmd.AddValue("mode", "Experiment mode: full|hitk|tau_sweep|ab_sweep|overhead", g_mode);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);

    CreateDirectoryIfNotExist(g_resultDir);

    NS_LOG_UNCOND("============================================================");
    NS_LOG_UNCOND("iRoute Exp9: Offline Analytical Sweep");
    NS_LOG_UNCOND("============================================================");
    NS_LOG_UNCOND("Mode: " << g_mode);
    NS_LOG_UNCOND("Domains: " << g_domains << ", M: " << g_M << ", vectorDim: " << g_vectorDim);
    NS_LOG_UNCOND("kMax: " << g_kMax << ", tau: " << g_tau);
    NS_LOG_UNCOND("alpha: " << g_alpha << ", beta: " << g_beta << ", lambda: " << g_lambda);

    if (g_centroidsFile.empty() || g_traceFile.empty() || g_contentFile.empty()) {
        NS_FATAL_ERROR("Required files: --centroidsFile, --traceFile, --contentFile");
    }

    auto queries = LoadQueries(g_traceFile);
    auto centroids = LoadCentroids(g_centroidsFile);
    auto content = LoadContent(g_contentFile);

    if (queries.empty() || centroids.empty() || content.empty()) {
        NS_FATAL_ERROR("Failed to load required data files");
    }

    if (g_queries > 0 && g_queries < queries.size()) {
        queries.resize(g_queries);
    }

    // Rocketfuel-derived costs
    auto topoCosts = BuildRocketfuelCosts(g_domains, g_topoSize, g_ingressNodeId, g_topoFile);
    if (topoCosts.gatewayNodes.size() < g_domains) {
        g_domains = topoCosts.gatewayNodes.size();
        NS_LOG_WARN("Reducing domains to " << g_domains << " to match gateways");
    }

    // Create RouteManager and populate
    auto rm = std::make_shared<iroute::RouteManager>();
    rm->configureParams(g_alpha, g_beta, g_lambda, 10000.0, 5, 0.3);

    std::map<uint32_t, iroute::DomainEntry> domainEntries;
    for (const auto& c : centroids) {
        if (c.domainId >= g_domains) continue; // only keep selected domains
        auto& entry = domainEntries[c.domainId];
        entry.domainId = ::ndn::Name("/domain" + std::to_string(c.domainId));
        entry.semVerId = 1;
        entry.seqNo = 1;
        entry.lifetime = 3600;
        entry.scope = 0;
        entry.cost = topoCosts.domainCost.count(c.domainId) ? topoCosts.domainCost[c.domainId] : 1.0;

        iroute::CentroidEntry ce;
        ce.centroidId = c.centroidId;
        ce.C = c.vector;
        ce.radius = c.radius;
        ce.weight = c.weight;
        entry.centroids.push_back(ce);
    }

    for (const auto& [domId, entry] : domainEntries) {
        rm->updateDomain(entry);
    }

    uint64_t totalCentroids = 0;
    for (const auto& [domId, entry] : domainEntries) {
        totalCentroids += entry.centroids.size();
    }

    // Overhead (analytical)
    uint32_t avgHops = static_cast<uint32_t>(std::round(topoCosts.avgHops));
    LsaOverhead oh = CalculateLsaOverhead(g_domains, g_M, g_vectorDim, std::max<uint32_t>(1, avgHops));

    std::ofstream ohFile(g_resultDir + "/exp9_overhead.csv");
    ohFile << "domains,M,vectorDim,avgHops,numNodes,"
           << "lsaHeaderBytes,centroidListBytes,vectorBytes,metadataBytes,"
           << "totalLsaBytes,totalBroadcastBytes,lsdbEntries,ribEntries\n";
    ohFile << g_domains << "," << g_M << "," << g_vectorDim << "," << avgHops << "," << topoCosts.numNodes << ","
           << oh.lsaHeaderBytes << "," << oh.centroidListBytes << "," << oh.vectorBytes << ","
           << oh.metadataBytes << "," << oh.totalLsaBytes << "," << oh.totalLsaBroadcastBytes << ","
           << oh.lsdbEntries << "," << oh.ribEntries << "\n";
    ohFile.close();

    // Hit@K
    if (g_mode == "full" || g_mode == "hitk") {
        auto hitKResults = ComputeHitAtK(queries, content, *rm, g_kMax, g_tau, totalCentroids);
        std::ofstream hkFile(g_resultDir + "/exp9_hitk.csv");
        hkFile << "K,domainHitRate,docHitRate,avgProbes,avgComputeOps,avgCtrlBytes\n";
        for (const auto& r : hitKResults) {
            hkFile << r.k << "," << std::fixed << std::setprecision(4)
                   << r.domainHitRate << "," << r.docHitRate << ","
                   << r.avgProbes << "," << r.avgComputeOps << "," << r.avgCtrlBytes << "\n";
        }
        hkFile.close();
    }

    // Tau sweep
    if (g_mode == "full" || g_mode == "tau_sweep") {
        std::vector<double> tauValues = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
        auto tauResults = TauSweep(queries, content, *rm, g_kMax, tauValues, totalCentroids);

        std::ofstream tsFile(g_resultDir + "/exp9_tau_sweep.csv");
        tsFile << "tau,kMax,domainAccuracy,docAccuracy,avgProbes,avgComputeOps,avgCtrlBytes\n";
        for (const auto& r : tauResults) {
            tsFile << std::fixed << std::setprecision(2) << r.param1 << "," << (int)r.param2 << ","
                   << std::setprecision(4) << r.domainAccuracy << "," << r.docAccuracy << ","
                   << r.avgProbes << "," << r.avgComputeOps << "," << r.avgCtrlBytes << "\n";
        }
        tsFile.close();
    }

    // Alpha/Beta sweep
    if (g_mode == "full" || g_mode == "ab_sweep") {
        std::vector<std::pair<double, double>> abValues = {
            {1.0, 0.0}, {0.9, 0.1}, {0.8, 0.2}, {0.7, 0.3}, {0.6, 0.4},
            {0.5, 0.5}, {0.4, 0.6}, {0.3, 0.7}, {0.2, 0.8}, {0.1, 0.9}, {0.0, 1.0}
        };

        auto abResults = AlphaBetaSweep(queries, content, *rm, g_kMax, g_tau, abValues, totalCentroids);

        std::ofstream abFile(g_resultDir + "/exp9_ab_sweep.csv");
        abFile << "alpha,beta,domainAccuracy,docAccuracy,avgProbes,avgComputeOps,avgCtrlBytes\n";
        for (const auto& r : abResults) {
            abFile << std::fixed << std::setprecision(1) << r.param1 << "," << r.param2 << ","
                   << std::setprecision(4) << r.domainAccuracy << "," << r.docAccuracy << ","
                   << r.avgProbes << "," << r.avgComputeOps << "," << r.avgCtrlBytes << "\n";
        }
        abFile.close();
    }

    NS_LOG_UNCOND("Exp9 analytical sweep completed. Results in: " << g_resultDir);
    return 0;
}
