/**
 * @file iroute-v2-exp2-rocketfuel.cc
 * @brief Exp2: Baseline comparison with Rocketfuel topology
 *
 * This experiment implements fair comparisons between:
 * 1. iRoute: Semantic routing with Top-K probing
 * 2. Flood-Parallel: Broadcast discovery to ALL domains simultaneously
 * 3. Flood-Sequential: Probe domains one-by-one until hit
 * 4. Centralized Search: Single search server with global knowledge
 *
 * Key features:
 * - Uses real Rocketfuel AS1239 (Sprint) topology
 * - Latency computed from actual hop counts via BFS
 * - Domain placement on backbone routers
 * - Consumer and search server placement
 *
 * Output files:
 *   - exp2_comparison.csv: Per-query results for all methods
 *   - exp2_summary.csv: Aggregated statistics
 *   - exp2_overhead.csv: Control plane overhead
 *   - exp2_topology.csv: Topology statistics
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/topology-read-module.h"

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/semantic-producer.hpp"
#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <iomanip>
#include <chrono>
#include <limits>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp2Rocketfuel");

// =============================================================================
// Global parameters
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
static std::string g_resultDir = "results/exp2_rocketfuel";

// Data files
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";
static std::string g_oracleFile = "";
static std::string g_topoFile = "";  // Rocketfuel topology file

// Network parameters
static double g_linkDelayMs = 2.0;       // Per-hop delay (Rocketfuel default)
static uint32_t g_interestSize = 200;    // Discovery Interest size (bytes)
static uint32_t g_dataSize = 300;        // Discovery Data size (bytes)
static uint32_t g_searchRequestSize = 250;  // Search request size
static uint32_t g_searchReplySize = 150;    // Search reply size

// =============================================================================
// Topology data structures
// =============================================================================

struct TopologyInfo {
    uint32_t numNodes;
    uint32_t numLinks;
    uint32_t consumerNodeId;
    uint32_t searchServerNodeId;
    std::vector<uint32_t> domainNodeIds;  // Domain i is at node domainNodeIds[i]
    std::map<uint32_t, std::map<uint32_t, uint32_t>> hopCounts;  // hop[src][dst] = hops
};

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
    
    for (char c : line) {
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
        try { result.push_back(std::stof(token)); } catch (...) {}
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

struct ContentEntry {
    uint32_t domainId;
    std::string docId;
    std::string canonicalName;
    iroute::SemanticVector vector;
    bool isDistractor;
};

struct CentroidData {
    uint32_t domainId;
    uint32_t centroidId;
    iroute::SemanticVector vector;
    double radius;
    double weight;
};

struct OracleEntry {
    std::string queryId;
    std::string bestDocId;
    std::string bestCanonicalName;
    double bestScore;
    uint32_t bestDomainId;
};

struct QueryResult {
    std::string queryId;
    std::string method;
    
    bool domainHit;
    bool docHit;
    std::string returnedDocId;
    std::string returnedDomain;
    
    uint32_t probes;
    uint32_t stage1Bytes;
    uint32_t stage2Bytes;
    uint32_t totalBytes;
    
    double stage1LatencyMs;
    double stage2LatencyMs;
    double totalLatencyMs;
    
    // Topology-specific
    uint32_t totalHops;
};

// =============================================================================
// Topology loading and hop count calculation
// =============================================================================

/**
 * Build adjacency list from topology file
 */
std::map<uint32_t, std::vector<uint32_t>> BuildAdjacencyList(const std::string& topoFile) {
    std::map<uint32_t, std::vector<uint32_t>> adj;
    std::ifstream file(topoFile);
    if (!file.is_open()) {
        NS_FATAL_ERROR("Cannot open topology file: " << topoFile);
    }
    
    std::string line;
    bool inLinkSection = false;
    
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Detect section markers
        if (line.find("link") != std::string::npos && line.find("Node") == std::string::npos) {
            inLinkSection = true;
            continue;
        }
        if (line.find("router") != std::string::npos) {
            inLinkSection = false;
            continue;
        }
        
        if (inLinkSection) {
            // Parse link line: Node0 Node1 ...
            std::istringstream iss(line);
            std::string node1, node2;
            if (iss >> node1 >> node2) {
                // Extract node IDs from names like "Node0", "Node123"
                uint32_t id1 = std::stoul(node1.substr(4));
                uint32_t id2 = std::stoul(node2.substr(4));
                adj[id1].push_back(id2);
                adj[id2].push_back(id1);
            }
        }
    }
    
    return adj;
}

/**
 * BFS to compute hop counts from a source node to all other nodes
 */
std::map<uint32_t, uint32_t> ComputeHopCounts(
    uint32_t source,
    const std::map<uint32_t, std::vector<uint32_t>>& adj)
{
    std::map<uint32_t, uint32_t> hops;
    std::queue<uint32_t> q;
    std::set<uint32_t> visited;
    
    q.push(source);
    visited.insert(source);
    hops[source] = 0;
    
    while (!q.empty()) {
        uint32_t curr = q.front();
        q.pop();
        
        auto it = adj.find(curr);
        if (it != adj.end()) {
            for (uint32_t neighbor : it->second) {
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    hops[neighbor] = hops[curr] + 1;
                    q.push(neighbor);
                }
            }
        }
    }
    
    return hops;
}

/**
 * Select backbone nodes for domain placement
 * Strategy: Select nodes with high degree (backbone routers)
 */
std::vector<uint32_t> SelectDomainNodes(
    const std::map<uint32_t, std::vector<uint32_t>>& adj,
    uint32_t numDomains,
    std::mt19937& rng)
{
    // Sort nodes by degree
    std::vector<std::pair<uint32_t, uint32_t>> nodesByDegree;
    for (const auto& [nodeId, neighbors] : adj) {
        nodesByDegree.push_back({nodeId, neighbors.size()});
    }
    std::sort(nodesByDegree.begin(), nodesByDegree.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Select top nodes by degree, with some spacing
    std::vector<uint32_t> selected;
    std::set<uint32_t> used;
    
    for (const auto& [nodeId, degree] : nodesByDegree) {
        if (selected.size() >= numDomains) break;
        
        // Check if too close to already selected nodes
        bool tooClose = false;
        for (uint32_t sel : selected) {
            // Compute hop distance
            auto hops = ComputeHopCounts(nodeId, adj);
            if (hops.count(sel) > 0 && hops[sel] < 2) {
                tooClose = true;
                break;
            }
        }
        
        if (!tooClose) {
            selected.push_back(nodeId);
            used.insert(nodeId);
        }
    }
    
    // If not enough, just pick remaining high-degree nodes
    for (const auto& [nodeId, degree] : nodesByDegree) {
        if (selected.size() >= numDomains) break;
        if (used.find(nodeId) == used.end()) {
            selected.push_back(nodeId);
            used.insert(nodeId);
        }
    }
    
    return selected;
}

/**
 * Initialize topology: load file, place domains, compute hop counts
 */
TopologyInfo InitializeTopology(
    const std::string& topoFile,
    uint32_t numDomains,
    std::mt19937& rng)
{
    TopologyInfo info;
    
    // Build adjacency list
    auto adj = BuildAdjacencyList(topoFile);
    
    info.numNodes = adj.size();
    info.numLinks = 0;
    for (const auto& [_, neighbors] : adj) {
        info.numLinks += neighbors.size();
    }
    info.numLinks /= 2;  // Each link counted twice
    
    NS_LOG_UNCOND("Topology: " << info.numNodes << " nodes, " << info.numLinks << " links");
    
    // Select domain nodes (backbone routers)
    info.domainNodeIds = SelectDomainNodes(adj, numDomains, rng);
    
    NS_LOG_UNCOND("Domain placement:");
    for (uint32_t i = 0; i < info.domainNodeIds.size(); ++i) {
        NS_LOG_UNCOND("  Domain " << i << " -> Node" << info.domainNodeIds[i]);
    }
    
    // Consumer at a random edge node (low degree)
    std::vector<std::pair<uint32_t, uint32_t>> nodesByDegree;
    for (const auto& [nodeId, neighbors] : adj) {
        nodesByDegree.push_back({nodeId, neighbors.size()});
    }
    std::sort(nodesByDegree.begin(), nodesByDegree.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // Pick a random edge node (from bottom 20%)
    uint32_t edgeIdx = rng() % std::max(1u, (uint32_t)(nodesByDegree.size() / 5));
    info.consumerNodeId = nodesByDegree[edgeIdx].first;
    
    // Search server near network center (highest centrality)
    info.searchServerNodeId = nodesByDegree.back().first;
    
    NS_LOG_UNCOND("Consumer at Node" << info.consumerNodeId);
    NS_LOG_UNCOND("Search server at Node" << info.searchServerNodeId);
    
    // Compute hop counts from consumer to all domain nodes
    auto consumerHops = ComputeHopCounts(info.consumerNodeId, adj);
    
    // Store in topology info
    info.hopCounts[info.consumerNodeId] = consumerHops;
    
    // Also compute hops from search server
    auto serverHops = ComputeHopCounts(info.searchServerNodeId, adj);
    info.hopCounts[info.searchServerNodeId] = serverHops;
    
    // Print hop statistics
    NS_LOG_UNCOND("\nHop counts from consumer:");
    double avgHops = 0;
    for (uint32_t i = 0; i < info.domainNodeIds.size(); ++i) {
        uint32_t domNode = info.domainNodeIds[i];
        uint32_t hops = consumerHops.count(domNode) > 0 ? consumerHops[domNode] : 0;
        avgHops += hops;
        NS_LOG_UNCOND("  Domain " << i << " (Node" << domNode << "): " << hops << " hops");
    }
    avgHops /= info.domainNodeIds.size();
    NS_LOG_UNCOND("  Average: " << std::fixed << std::setprecision(2) << avgHops << " hops");
    
    uint32_t serverHopsFromConsumer = consumerHops.count(info.searchServerNodeId) > 0 
        ? consumerHops[info.searchServerNodeId] : 0;
    NS_LOG_UNCOND("  Search server: " << serverHopsFromConsumer << " hops");
    
    return info;
}

// =============================================================================
// Data loading functions
// =============================================================================

std::vector<QueryEntry> LoadQueries(const std::string& filepath) {
    std::vector<QueryEntry> queries;
    std::ifstream file(filepath);
    if (!file.is_open()) return queries;
    
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
                try { q.targetDomains.insert(std::stoul(token.substr(7))); } catch (...) {}
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
    return queries;
}

std::map<uint32_t, std::vector<ContentEntry>> LoadContent(const std::string& filepath) {
    std::map<uint32_t, std::vector<ContentEntry>> content;
    std::ifstream file(filepath);
    if (!file.is_open()) return content;
    
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
        
        ContentEntry c;
        c.domainId = std::stoul(fields[domainIdCol]);
        c.docId = fields[docIdCol];
        c.canonicalName = fields[canonicalNameCol];
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        c.vector = MakeSemanticVector(vec);
        c.isDistractor = (isDistractorCol >= 0 && fields[isDistractorCol] == "1");
        
        content[c.domainId].push_back(c);
    }
    return content;
}

std::vector<CentroidData> LoadCentroids(const std::string& filepath) {
    std::vector<CentroidData> centroids;
    std::ifstream file(filepath);
    if (!file.is_open()) return centroids;
    
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
    return centroids;
}

std::map<std::string, OracleEntry> LoadOracle(const std::string& filepath) {
    std::map<std::string, OracleEntry> oracle;
    std::ifstream file(filepath);
    if (!file.is_open()) return oracle;
    
    std::string line;
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 5) continue;
        
        OracleEntry e;
        e.queryId = fields[0];
        e.bestDocId = fields[1];
        e.bestCanonicalName = fields[2];
        e.bestScore = std::stod(fields[3]);
        e.bestDomainId = std::stoul(fields[4]);
        
        oracle[e.queryId] = e;
    }
    return oracle;
}

// =============================================================================
// Baseline implementations with topology-aware latency
// =============================================================================

std::pair<std::string, double> FindBestDocInDomain(
    const iroute::SemanticVector& query,
    const std::vector<ContentEntry>& domainDocs)
{
    double bestScore = -1;
    std::string bestDocId;
    
    for (const auto& doc : domainDocs) {
        double score = query.dot(doc.vector);
        if (score > bestScore) {
            bestScore = score;
            bestDocId = doc.docId;
        }
    }
    
    return {bestDocId, bestScore};
}

/**
 * Get hop count from consumer to a domain
 */
uint32_t GetHopsToDomain(const TopologyInfo& topo, uint32_t domainId) {
    if (domainId >= topo.domainNodeIds.size()) return 0;
    uint32_t domNode = topo.domainNodeIds[domainId];
    auto it = topo.hopCounts.find(topo.consumerNodeId);
    if (it == topo.hopCounts.end()) return 0;
    auto it2 = it->second.find(domNode);
    return (it2 != it->second.end()) ? it2->second : 0;
}

/**
 * iRoute with topology-aware latency
 */
QueryResult RunIRoute(
    const QueryEntry& query,
    const std::map<uint32_t, std::vector<ContentEntry>>& content,
    iroute::RouteManager& rm,
    const TopologyInfo& topo,
    uint32_t kMax,
    double tau)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "iRoute";
    result.domainHit = false;
    result.docHit = false;
    result.probes = 0;
    result.totalHops = 0;
    
    auto domains = rm.findBestDomainsV2(query.vector, kMax, 1);
    
    std::string bestDocId;
    double bestScore = -1;
    uint32_t bestDomain = 0;
    
    for (const auto& d : domains) {
        if (d.confidence < tau && result.probes > 0) break;
        result.probes++;
        
        std::string domainName = d.domainId.toUri();
        if (domainName.size() <= 7) continue;
        uint32_t domId = std::stoul(domainName.substr(7));
        
        // Add hop count for this probe (RTT = 2 * hops)
        uint32_t hops = GetHopsToDomain(topo, domId);
        result.totalHops += 2 * hops;
        
        auto it = content.find(domId);
        if (it != content.end()) {
            auto [docId, score] = FindBestDocInDomain(query.vector, it->second);
            if (score > bestScore) {
                bestScore = score;
                bestDocId = docId;
                bestDomain = domId;
            }
        }
    }
    
    result.returnedDocId = bestDocId;
    result.returnedDomain = "/domain" + std::to_string(bestDomain);
    
    result.domainHit = (query.targetDomains.count(bestDomain) > 0);
    result.docHit = (query.targetDocIds.count(bestDocId) > 0);
    
    result.stage1Bytes = result.probes * (g_interestSize + g_dataSize);
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // Latency based on actual hop counts
    result.stage1LatencyMs = result.totalHops * g_linkDelayMs;
    
    // Stage 2: fetch from best domain
    uint32_t stage2Hops = GetHopsToDomain(topo, bestDomain);
    result.stage2LatencyMs = 2 * stage2Hops * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    result.totalHops += 2 * stage2Hops;
    
    return result;
}

/**
 * Flood-Parallel with topology-aware latency
 */
QueryResult RunFloodParallel(
    const QueryEntry& query,
    const std::map<uint32_t, std::vector<ContentEntry>>& content,
    const TopologyInfo& topo)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "Flood-Parallel";
    result.domainHit = false;
    result.docHit = false;
    result.probes = content.size();
    
    std::string bestDocId;
    double bestScore = -1;
    uint32_t bestDomain = 0;
    uint32_t maxHops = 0;
    
    for (const auto& [domId, docs] : content) {
        auto [docId, score] = FindBestDocInDomain(query.vector, docs);
        if (score > bestScore) {
            bestScore = score;
            bestDocId = docId;
            bestDomain = domId;
        }
        
        // Track max hops (parallel: limited by slowest)
        uint32_t hops = GetHopsToDomain(topo, domId);
        if (hops > maxHops) maxHops = hops;
    }
    
    result.returnedDocId = bestDocId;
    result.returnedDomain = "/domain" + std::to_string(bestDomain);
    
    result.domainHit = (query.targetDomains.count(bestDomain) > 0);
    result.docHit = (query.targetDocIds.count(bestDocId) > 0);
    
    result.stage1Bytes = result.probes * (g_interestSize + g_dataSize);
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // Parallel: latency is max RTT
    result.stage1LatencyMs = 2 * maxHops * g_linkDelayMs;
    uint32_t stage2Hops = GetHopsToDomain(topo, bestDomain);
    result.stage2LatencyMs = 2 * stage2Hops * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    result.totalHops = 2 * maxHops + 2 * stage2Hops;
    
    return result;
}

/**
 * Flood-Sequential with topology-aware latency
 */
QueryResult RunFloodSequential(
    const QueryEntry& query,
    const std::map<uint32_t, std::vector<ContentEntry>>& content,
    const TopologyInfo& topo,
    std::mt19937& rng)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "Flood-Sequential";
    result.domainHit = false;
    result.docHit = false;
    result.probes = 0;
    result.totalHops = 0;
    
    std::vector<uint32_t> domainOrder;
    for (const auto& [domId, _] : content) {
        domainOrder.push_back(domId);
    }
    std::shuffle(domainOrder.begin(), domainOrder.end(), rng);
    
    std::string bestDocId;
    double bestScore = -1;
    uint32_t bestDomain = 0;
    
    for (uint32_t domId : domainOrder) {
        result.probes++;
        
        uint32_t hops = GetHopsToDomain(topo, domId);
        result.totalHops += 2 * hops;
        
        auto it = content.find(domId);
        if (it != content.end()) {
            auto [docId, score] = FindBestDocInDomain(query.vector, it->second);
            
            bool hasRelevant = false;
            for (const auto& doc : it->second) {
                if (query.targetDocIds.count(doc.docId) > 0) {
                    hasRelevant = true;
                    break;
                }
            }
            
            if (score > bestScore) {
                bestScore = score;
                bestDocId = docId;
                bestDomain = domId;
            }
            
            if (hasRelevant) break;
        }
    }
    
    result.returnedDocId = bestDocId;
    result.returnedDomain = "/domain" + std::to_string(bestDomain);
    
    result.domainHit = (query.targetDomains.count(bestDomain) > 0);
    result.docHit = (query.targetDocIds.count(bestDocId) > 0);
    
    result.stage1Bytes = result.probes * (g_interestSize + g_dataSize);
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    result.stage1LatencyMs = result.totalHops * g_linkDelayMs;
    uint32_t stage2Hops = GetHopsToDomain(topo, bestDomain);
    result.stage2LatencyMs = 2 * stage2Hops * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    result.totalHops += 2 * stage2Hops;
    
    return result;
}

/**
 * Centralized Search with topology-aware latency
 */
QueryResult RunCentralizedSearch(
    const QueryEntry& query,
    const std::map<std::string, OracleEntry>& oracle,
    const TopologyInfo& topo)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "Centralized";
    result.domainHit = false;
    result.docHit = false;
    result.probes = 1;
    
    // Hops to search server
    auto it = topo.hopCounts.find(topo.consumerNodeId);
    uint32_t serverHops = 0;
    if (it != topo.hopCounts.end()) {
        auto it2 = it->second.find(topo.searchServerNodeId);
        if (it2 != it->second.end()) serverHops = it2->second;
    }
    
    auto oracleIt = oracle.find(query.queryId);
    uint32_t resultDomainId = 0;
    if (oracleIt != oracle.end()) {
        result.returnedDocId = oracleIt->second.bestDocId;
        resultDomainId = oracleIt->second.bestDomainId;
        result.returnedDomain = "/domain" + std::to_string(resultDomainId);
        
        result.domainHit = (query.targetDomains.count(resultDomainId) > 0);
        result.docHit = (query.targetDocIds.count(oracleIt->second.bestDocId) > 0);
    }
    
    result.stage1Bytes = g_searchRequestSize + g_searchReplySize;
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // RTT to search server
    result.stage1LatencyMs = 2 * serverHops * g_linkDelayMs;
    
    // Stage 2: fetch from result domain
    uint32_t stage2Hops = GetHopsToDomain(topo, resultDomainId);
    result.stage2LatencyMs = 2 * stage2Hops * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    result.totalHops = 2 * serverHops + 2 * stage2Hops;
    
    return result;
}

// =============================================================================
// Main function
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
    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("traceFile", "Path to consumer_trace.csv", g_traceFile);
    cmd.AddValue("contentFile", "Path to producer_content.csv", g_contentFile);
    cmd.AddValue("oracleFile", "Path to search_oracle.csv", g_oracleFile);
    cmd.AddValue("topoFile", "Path to Rocketfuel topology file", g_topoFile);
    cmd.AddValue("linkDelayMs", "Per-hop link delay (ms)", g_linkDelayMs);
    cmd.Parse(argc, argv);
    
    if (g_topoFile.empty()) {
        NS_FATAL_ERROR("Required: --topoFile (Rocketfuel topology file)");
    }
    
    std::mt19937 rng(g_seed);
    CreateDirectoryIfNotExist(g_resultDir);
    
    NS_LOG_UNCOND("============================================================");
    NS_LOG_UNCOND("iRoute Exp2: Baseline Comparison (Rocketfuel Topology)");
    NS_LOG_UNCOND("============================================================");
    NS_LOG_UNCOND("Topology file: " << g_topoFile);
    NS_LOG_UNCOND("Domains: " << g_domains << ", M: " << g_M << ", vectorDim: " << g_vectorDim);
    NS_LOG_UNCOND("kMax: " << g_kMax << ", tau: " << g_tau);
    NS_LOG_UNCOND("Link delay: " << g_linkDelayMs << " ms per hop");
    
    // Initialize topology
    NS_LOG_UNCOND("\n--- Loading Rocketfuel topology ---");
    auto topo = InitializeTopology(g_topoFile, g_domains, rng);
    
    // Load data
    if (g_traceFile.empty() || g_contentFile.empty() || g_centroidsFile.empty()) {
        NS_FATAL_ERROR("Required: --traceFile, --contentFile, --centroidsFile");
    }
    
    auto queries = LoadQueries(g_traceFile);
    auto content = LoadContent(g_contentFile);
    auto centroids = LoadCentroids(g_centroidsFile);
    
    std::map<std::string, OracleEntry> oracle;
    if (!g_oracleFile.empty()) {
        oracle = LoadOracle(g_oracleFile);
        NS_LOG_UNCOND("Loaded " << oracle.size() << " oracle entries");
    }
    
    NS_LOG_UNCOND("Loaded " << queries.size() << " queries");
    NS_LOG_UNCOND("Loaded " << content.size() << " domains with documents");
    NS_LOG_UNCOND("Loaded " << centroids.size() << " centroids");
    
    if (g_queries > 0 && g_queries < queries.size()) {
        queries.resize(g_queries);
    }
    
    // Setup RouteManager with topology-aware costs
    auto rm = std::make_shared<iroute::RouteManager>();
    rm->configureParams(g_alpha, g_beta, g_lambda, 10000.0, 5, 0.3);
    
    std::map<uint32_t, iroute::DomainEntry> domainEntries;
    for (const auto& c : centroids) {
        auto& entry = domainEntries[c.domainId];
        entry.domainId = ::ndn::Name("/domain" + std::to_string(c.domainId));
        entry.semVerId = 1;
        entry.seqNo = 1;
        entry.lifetime = 3600;
        entry.scope = 0;
        
        // Cost based on hop count (normalized)
        uint32_t hops = GetHopsToDomain(topo, c.domainId);
        entry.cost = 1.0 + 0.1 * hops;  // Base cost + hop penalty
        
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
    
    // ==========================================================================
    // Run all methods
    // ==========================================================================
    std::vector<QueryResult> allResults;
    
    NS_LOG_UNCOND("\n--- Running experiments ---");
    
    for (const auto& query : queries) {
        auto r1 = RunIRoute(query, content, *rm, topo, g_kMax, g_tau);
        allResults.push_back(r1);
        
        auto r2 = RunFloodParallel(query, content, topo);
        allResults.push_back(r2);
        
        auto r3 = RunFloodSequential(query, content, topo, rng);
        allResults.push_back(r3);
        
        if (!oracle.empty()) {
            auto r4 = RunCentralizedSearch(query, oracle, topo);
            allResults.push_back(r4);
        }
    }
    
    // ==========================================================================
    // Aggregate results by method
    // ==========================================================================
    std::map<std::string, std::vector<QueryResult>> byMethod;
    for (const auto& r : allResults) {
        byMethod[r.method].push_back(r);
    }
    
    // ==========================================================================
    // Export per-query results
    // ==========================================================================
    std::ofstream qf(g_resultDir + "/exp2_comparison.csv");
    qf << "query_id,method,domain_hit,doc_hit,probes,stage1_bytes,stage2_bytes,"
       << "total_bytes,stage1_latency_ms,stage2_latency_ms,total_latency_ms,"
       << "total_hops,returned_docid,returned_domain\n";
    
    for (const auto& r : allResults) {
        qf << r.queryId << "," << r.method << ","
           << (r.domainHit ? 1 : 0) << "," << (r.docHit ? 1 : 0) << ","
           << r.probes << "," << r.stage1Bytes << "," << r.stage2Bytes << ","
           << r.totalBytes << "," << std::fixed << std::setprecision(2)
           << r.stage1LatencyMs << "," << r.stage2LatencyMs << "," << r.totalLatencyMs << ","
           << r.totalHops << "," << r.returnedDocId << "," << r.returnedDomain << "\n";
    }
    qf.close();
    
    // ==========================================================================
    // Export summary
    // ==========================================================================
    std::ofstream sf(g_resultDir + "/exp2_summary.csv");
    sf << "method,queries,domain_accuracy,doc_accuracy,avg_probes,"
       << "avg_stage1_bytes,avg_total_bytes,avg_stage1_latency_ms,avg_total_latency_ms,avg_total_hops\n";
    
    NS_LOG_UNCOND("\n=== Summary ===");
    NS_LOG_UNCOND(std::left << std::setw(18) << "Method" 
                  << std::setw(12) << "DomainAcc" 
                  << std::setw(10) << "DocAcc"
                  << std::setw(10) << "Probes"
                  << std::setw(12) << "Stage1B"
                  << std::setw(12) << "TotalB"
                  << std::setw(12) << "Stage1ms"
                  << std::setw(10) << "Totalms"
                  << "AvgHops");
    NS_LOG_UNCOND(std::string(104, '-'));
    
    for (const auto& [method, results] : byMethod) {
        uint32_t domainHits = 0, docHits = 0;
        double totalProbes = 0, totalS1Bytes = 0, totalBytes = 0;
        double totalS1Latency = 0, totalLatency = 0, totalHops = 0;
        
        for (const auto& r : results) {
            if (r.domainHit) domainHits++;
            if (r.docHit) docHits++;
            totalProbes += r.probes;
            totalS1Bytes += r.stage1Bytes;
            totalBytes += r.totalBytes;
            totalS1Latency += r.stage1LatencyMs;
            totalLatency += r.totalLatencyMs;
            totalHops += r.totalHops;
        }
        
        size_t n = results.size();
        double domainAcc = 100.0 * domainHits / n;
        double docAcc = 100.0 * docHits / n;
        double avgProbes = totalProbes / n;
        double avgS1Bytes = totalS1Bytes / n;
        double avgTotalBytes = totalBytes / n;
        double avgS1Latency = totalS1Latency / n;
        double avgTotalLatency = totalLatency / n;
        double avgHops = totalHops / n;
        
        sf << method << "," << n << ","
           << std::fixed << std::setprecision(4) << (domainAcc/100) << "," << (docAcc/100) << ","
           << std::setprecision(2) << avgProbes << ","
           << avgS1Bytes << "," << avgTotalBytes << ","
           << avgS1Latency << "," << avgTotalLatency << ","
           << avgHops << "\n";
        
        NS_LOG_UNCOND(std::left << std::setw(18) << method 
                      << std::setw(12) << std::fixed << std::setprecision(1) << domainAcc << "%"
                      << std::setw(10) << docAcc << "%"
                      << std::setw(10) << std::setprecision(2) << avgProbes
                      << std::setw(12) << std::setprecision(0) << avgS1Bytes
                      << std::setw(12) << avgTotalBytes
                      << std::setw(12) << std::setprecision(1) << avgS1Latency
                      << std::setw(10) << avgTotalLatency
                      << std::setprecision(1) << avgHops);
    }
    
    sf.close();
    
    // ==========================================================================
    // Export topology info
    // ==========================================================================
    std::ofstream tf(g_resultDir + "/exp2_topology.csv");
    tf << "metric,value\n";
    tf << "nodes," << topo.numNodes << "\n";
    tf << "links," << topo.numLinks << "\n";
    tf << "consumer_node," << topo.consumerNodeId << "\n";
    tf << "search_server_node," << topo.searchServerNodeId << "\n";
    tf << "link_delay_ms," << g_linkDelayMs << "\n";
    
    // Average hop count to domains
    double avgHopsToDomain = 0;
    for (uint32_t i = 0; i < topo.domainNodeIds.size(); ++i) {
        avgHopsToDomain += GetHopsToDomain(topo, i);
    }
    avgHopsToDomain /= topo.domainNodeIds.size();
    tf << "avg_hops_to_domain," << avgHopsToDomain << "\n";
    
    tf.close();
    
    // ==========================================================================
    // LSA overhead comparison
    // ==========================================================================
    NS_LOG_UNCOND("\n=== Control Plane Overhead Comparison ===");
    
    uint32_t lsaSize = 326 + g_M * (519 + 29);
    uint32_t totalLsaBytes = lsaSize * g_domains;
    double lsaPerQuery = (double)totalLsaBytes / queries.size();
    
    NS_LOG_UNCOND("iRoute LSA overhead:");
    NS_LOG_UNCOND("  Per-domain LSA: " << lsaSize << " bytes");
    NS_LOG_UNCOND("  Total LSA (all domains): " << totalLsaBytes << " bytes");
    NS_LOG_UNCOND("  Amortized per query: " << std::fixed << std::setprecision(1) << lsaPerQuery << " bytes");
    
    NS_LOG_UNCOND("\nCentralized overhead:");
    size_t totalDocs = 0;
    for (const auto& [_, docs] : content) totalDocs += docs.size();
    uint32_t indexSize = totalDocs * (g_vectorDim * 4 + 50);
    NS_LOG_UNCOND("  Centralized index size: " << indexSize << " bytes (" << totalDocs << " docs)");
    
    std::ofstream of(g_resultDir + "/exp2_overhead.csv");
    of << "method,lsa_bytes,index_bytes,amortized_per_query\n";
    of << "iRoute," << totalLsaBytes << ",0," << lsaPerQuery << "\n";
    of << "Flood-Parallel,0,0,0\n";
    of << "Flood-Sequential,0,0,0\n";
    of << "Centralized,0," << indexSize << ",0\n";
    of.close();
    
    NS_LOG_UNCOND("\n============================================================");
    NS_LOG_UNCOND("Experiment completed. Results in: " << g_resultDir);
    NS_LOG_UNCOND("  - exp2_comparison.csv (per-query)");
    NS_LOG_UNCOND("  - exp2_summary.csv (aggregated)");
    NS_LOG_UNCOND("  - exp2_topology.csv (topology info)");
    NS_LOG_UNCOND("  - exp2_overhead.csv (control plane)");
    NS_LOG_UNCOND("============================================================");
    
    return 0;
}
