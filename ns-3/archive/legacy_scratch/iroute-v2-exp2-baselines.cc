/**
 * @file iroute-v2-exp2-baselines.cc
 * @brief Exp2: Baseline comparison (Flooding vs Centralized Search vs iRoute)
 *
 * This experiment implements fair comparisons between:
 * 1. iRoute: Semantic routing with Top-K probing
 * 2. Flood-Parallel: Broadcast discovery to ALL domains simultaneously
 * 3. Flood-Sequential: Probe domains one-by-one until hit
 * 4. Centralized Search: Single search server with global knowledge
 *
 * Key fairness guarantees:
 * - Same dataset (consumer_trace.csv, producer_content.csv)
 * - Same embeddings and similarity metric
 * - Same Stage-2 fetch mechanism
 * - Same network topology and parameters
 *
 * Output files:
 *   - exp2_comparison.csv: Per-query results for all methods
 *   - exp2_summary.csv: Aggregated statistics
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

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

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp2Baselines");

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
static std::string g_resultDir = "results/exp2";

// Data files
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";
static std::string g_oracleFile = "";  // For centralized search baseline

// Network parameters
static double g_linkDelayMs = 10.0;      // Per-hop delay
static uint32_t g_interestSize = 200;    // Discovery Interest size (bytes)
static uint32_t g_dataSize = 300;        // Discovery Data size (bytes)
static uint32_t g_searchRequestSize = 250;  // Search request size
static uint32_t g_searchReplySize = 150;    // Search reply size

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

// Per-query result for comparison
struct QueryResult {
    std::string queryId;
    std::string method;
    
    // Accuracy
    bool domainHit;
    bool docHit;
    std::string returnedDocId;
    std::string returnedDomain;
    
    // Efficiency
    uint32_t probes;
    uint32_t stage1Bytes;
    uint32_t stage2Bytes;
    uint32_t totalBytes;
    
    // Latency (simulated)
    double stage1LatencyMs;
    double stage2LatencyMs;
    double totalLatencyMs;
};

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
        
        // Parse target domains
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
        
        // Parse target doc IDs
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
    std::getline(file, line);  // Skip header
    
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
// Baseline implementations
// =============================================================================

/**
 * Find best document within a single domain (local search)
 */
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
 * iRoute: Top-K semantic routing
 */
QueryResult RunIRoute(
    const QueryEntry& query,
    const std::map<uint32_t, std::vector<ContentEntry>>& content,
    iroute::RouteManager& rm,
    uint32_t kMax,
    double tau)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "iRoute";
    result.domainHit = false;
    result.docHit = false;
    result.probes = 0;
    
    // Stage 1: Get ranked domains from RouteManager
    auto domains = rm.findBestDomainsV2(query.vector, kMax, 1);
    
    // Probe domains above tau threshold
    std::string bestDocId;
    double bestScore = -1;
    uint32_t bestDomain = 0;
    
    for (const auto& d : domains) {
        if (d.confidence < tau && result.probes > 0) break;  // Allow at least one probe
        result.probes++;
        
        std::string domainName = d.domainId.toUri();
        if (domainName.size() <= 7) continue;
        uint32_t domId = std::stoul(domainName.substr(7));
        
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
    
    // Check accuracy
    result.domainHit = (query.targetDomains.count(bestDomain) > 0);
    result.docHit = (query.targetDocIds.count(bestDocId) > 0);
    
    // Calculate bytes
    result.stage1Bytes = result.probes * (g_interestSize + g_dataSize);
    result.stage2Bytes = g_interestSize + 1500;  // Assume 1.5KB doc fetch
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // Calculate latency (star topology: 1 hop to each domain)
    result.stage1LatencyMs = result.probes * 2 * g_linkDelayMs;  // RTT per probe
    result.stage2LatencyMs = 2 * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    
    return result;
}

/**
 * Flood-Parallel: Send discovery to ALL domains simultaneously
 */
QueryResult RunFloodParallel(
    const QueryEntry& query,
    const std::map<uint32_t, std::vector<ContentEntry>>& content)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "Flood-Parallel";
    result.domainHit = false;
    result.docHit = false;
    result.probes = content.size();  // All domains
    
    // Probe ALL domains and collect results
    std::string bestDocId;
    double bestScore = -1;
    uint32_t bestDomain = 0;
    
    for (const auto& [domId, docs] : content) {
        auto [docId, score] = FindBestDocInDomain(query.vector, docs);
        if (score > bestScore) {
            bestScore = score;
            bestDocId = docId;
            bestDomain = domId;
        }
    }
    
    result.returnedDocId = bestDocId;
    result.returnedDomain = "/domain" + std::to_string(bestDomain);
    
    // Check accuracy
    result.domainHit = (query.targetDomains.count(bestDomain) > 0);
    result.docHit = (query.targetDocIds.count(bestDocId) > 0);
    
    // Calculate bytes (all domains probed)
    result.stage1Bytes = result.probes * (g_interestSize + g_dataSize);
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // Latency: parallel means single RTT for all (worst case among parallel)
    result.stage1LatencyMs = 2 * g_linkDelayMs;  // Parallel: only 1 RTT
    result.stage2LatencyMs = 2 * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    
    return result;
}

/**
 * Flood-Sequential: Probe domains one-by-one in random order until hit
 */
QueryResult RunFloodSequential(
    const QueryEntry& query,
    const std::map<uint32_t, std::vector<ContentEntry>>& content,
    std::mt19937& rng)
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "Flood-Sequential";
    result.domainHit = false;
    result.docHit = false;
    result.probes = 0;
    
    // Create shuffled domain order
    std::vector<uint32_t> domainOrder;
    for (const auto& [domId, _] : content) {
        domainOrder.push_back(domId);
    }
    std::shuffle(domainOrder.begin(), domainOrder.end(), rng);
    
    // Probe domains until we find one with a relevant doc
    std::string bestDocId;
    double bestScore = -1;
    uint32_t bestDomain = 0;
    
    for (uint32_t domId : domainOrder) {
        result.probes++;
        
        auto it = content.find(domId);
        if (it != content.end()) {
            auto [docId, score] = FindBestDocInDomain(query.vector, it->second);
            
            // Check if this domain has a relevant doc
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
            
            // Stop if we found a domain with relevant docs
            if (hasRelevant) {
                break;
            }
        }
    }
    
    result.returnedDocId = bestDocId;
    result.returnedDomain = "/domain" + std::to_string(bestDomain);
    
    // Check accuracy
    result.domainHit = (query.targetDomains.count(bestDomain) > 0);
    result.docHit = (query.targetDocIds.count(bestDocId) > 0);
    
    // Calculate bytes
    result.stage1Bytes = result.probes * (g_interestSize + g_dataSize);
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // Latency: sequential means probes × RTT
    result.stage1LatencyMs = result.probes * 2 * g_linkDelayMs;
    result.stage2LatencyMs = 2 * g_linkDelayMs;
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    
    return result;
}

/**
 * Centralized Search: Single search server with global knowledge
 */
QueryResult RunCentralizedSearch(
    const QueryEntry& query,
    const std::map<std::string, OracleEntry>& oracle,
    uint32_t searchServerHops = 2)  // Assume search server is 2 hops away
{
    QueryResult result;
    result.queryId = query.queryId;
    result.method = "Centralized";
    result.domainHit = false;
    result.docHit = false;
    result.probes = 1;  // Single request to search server
    
    auto it = oracle.find(query.queryId);
    if (it != oracle.end()) {
        result.returnedDocId = it->second.bestDocId;
        result.returnedDomain = "/domain" + std::to_string(it->second.bestDomainId);
        
        result.domainHit = (query.targetDomains.count(it->second.bestDomainId) > 0);
        result.docHit = (query.targetDocIds.count(it->second.bestDocId) > 0);
    }
    
    // Calculate bytes (search request/reply + stage2 fetch)
    result.stage1Bytes = g_searchRequestSize + g_searchReplySize;
    result.stage2Bytes = g_interestSize + 1500;
    result.totalBytes = result.stage1Bytes + result.stage2Bytes;
    
    // Latency: RTT to search server + stage2 fetch
    result.stage1LatencyMs = searchServerHops * 2 * g_linkDelayMs;
    result.stage2LatencyMs = 2 * g_linkDelayMs;  // Assume 1 hop to domain
    result.totalLatencyMs = result.stage1LatencyMs + result.stage2LatencyMs;
    
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
    cmd.AddValue("linkDelayMs", "Per-hop link delay (ms)", g_linkDelayMs);
    cmd.Parse(argc, argv);
    
    std::mt19937 rng(g_seed);
    CreateDirectoryIfNotExist(g_resultDir);
    
    NS_LOG_UNCOND("============================================================");
    NS_LOG_UNCOND("iRoute Exp2: Baseline Comparison");
    NS_LOG_UNCOND("============================================================");
    NS_LOG_UNCOND("Domains: " << g_domains << ", M: " << g_M << ", vectorDim: " << g_vectorDim);
    NS_LOG_UNCOND("kMax: " << g_kMax << ", tau: " << g_tau);
    NS_LOG_UNCOND("Link delay: " << g_linkDelayMs << " ms");
    
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
    
    // Setup RouteManager for iRoute
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
        entry.cost = 1.0;
        
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
        // iRoute
        auto r1 = RunIRoute(query, content, *rm, g_kMax, g_tau);
        allResults.push_back(r1);
        
        // Flood-Parallel
        auto r2 = RunFloodParallel(query, content);
        allResults.push_back(r2);
        
        // Flood-Sequential
        auto r3 = RunFloodSequential(query, content, rng);
        allResults.push_back(r3);
        
        // Centralized (if oracle available)
        if (!oracle.empty()) {
            auto r4 = RunCentralizedSearch(query, oracle);
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
       << "returned_docid,returned_domain\n";
    
    for (const auto& r : allResults) {
        qf << r.queryId << "," << r.method << ","
           << (r.domainHit ? 1 : 0) << "," << (r.docHit ? 1 : 0) << ","
           << r.probes << "," << r.stage1Bytes << "," << r.stage2Bytes << ","
           << r.totalBytes << "," << std::fixed << std::setprecision(2)
           << r.stage1LatencyMs << "," << r.stage2LatencyMs << "," << r.totalLatencyMs << ","
           << r.returnedDocId << "," << r.returnedDomain << "\n";
    }
    qf.close();
    
    // ==========================================================================
    // Export summary
    // ==========================================================================
    std::ofstream sf(g_resultDir + "/exp2_summary.csv");
    sf << "method,queries,domain_accuracy,doc_accuracy,avg_probes,"
       << "avg_stage1_bytes,avg_total_bytes,avg_stage1_latency_ms,avg_total_latency_ms\n";
    
    NS_LOG_UNCOND("\n=== Summary ===");
    NS_LOG_UNCOND(std::left << std::setw(18) << "Method" 
                  << std::setw(12) << "DomainAcc" 
                  << std::setw(10) << "DocAcc"
                  << std::setw(10) << "Probes"
                  << std::setw(12) << "Stage1B"
                  << std::setw(12) << "TotalB"
                  << std::setw(12) << "Stage1ms"
                  << "Totalms");
    NS_LOG_UNCOND(std::string(94, '-'));
    
    for (const auto& [method, results] : byMethod) {
        uint32_t domainHits = 0, docHits = 0;
        double totalProbes = 0, totalS1Bytes = 0, totalBytes = 0;
        double totalS1Latency = 0, totalLatency = 0;
        
        for (const auto& r : results) {
            if (r.domainHit) domainHits++;
            if (r.docHit) docHits++;
            totalProbes += r.probes;
            totalS1Bytes += r.stage1Bytes;
            totalBytes += r.totalBytes;
            totalS1Latency += r.stage1LatencyMs;
            totalLatency += r.totalLatencyMs;
        }
        
        size_t n = results.size();
        double domainAcc = 100.0 * domainHits / n;
        double docAcc = 100.0 * docHits / n;
        double avgProbes = totalProbes / n;
        double avgS1Bytes = totalS1Bytes / n;
        double avgTotalBytes = totalBytes / n;
        double avgS1Latency = totalS1Latency / n;
        double avgTotalLatency = totalLatency / n;
        
        sf << method << "," << n << ","
           << std::fixed << std::setprecision(4) << (domainAcc/100) << "," << (docAcc/100) << ","
           << std::setprecision(2) << avgProbes << ","
           << avgS1Bytes << "," << avgTotalBytes << ","
           << avgS1Latency << "," << avgTotalLatency << "\n";
        
        NS_LOG_UNCOND(std::left << std::setw(18) << method 
                      << std::setw(12) << std::fixed << std::setprecision(1) << domainAcc << "%"
                      << std::setw(10) << docAcc << "%"
                      << std::setw(10) << std::setprecision(2) << avgProbes
                      << std::setw(12) << std::setprecision(0) << avgS1Bytes
                      << std::setw(12) << avgTotalBytes
                      << std::setw(12) << std::setprecision(1) << avgS1Latency
                      << avgTotalLatency);
    }
    
    sf.close();
    
    // ==========================================================================
    // LSA overhead comparison
    // ==========================================================================
    NS_LOG_UNCOND("\n=== Control Plane Overhead Comparison ===");
    
    // iRoute: LSA advertisement (one-time, amortized over queries)
    uint32_t lsaSize = 326 + g_M * (519 + 29);  // Header + M centroids
    uint32_t totalLsaBytes = lsaSize * g_domains;
    double lsaPerQuery = (double)totalLsaBytes / queries.size();
    
    NS_LOG_UNCOND("iRoute LSA overhead:");
    NS_LOG_UNCOND("  Per-domain LSA: " << lsaSize << " bytes");
    NS_LOG_UNCOND("  Total LSA (all domains): " << totalLsaBytes << " bytes");
    NS_LOG_UNCOND("  Amortized per query: " << std::fixed << std::setprecision(1) << lsaPerQuery << " bytes");
    
    // Centralized: No LSA needed, but centralized index
    NS_LOG_UNCOND("\nCentralized overhead:");
    size_t totalDocs = 0;
    for (const auto& [_, docs] : content) totalDocs += docs.size();
    uint32_t indexSize = totalDocs * (g_vectorDim * 4 + 50);  // Vector + metadata per doc
    NS_LOG_UNCOND("  Centralized index size: " << indexSize << " bytes (" << totalDocs << " docs)");
    
    // Export overhead comparison
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
    NS_LOG_UNCOND("  - exp2_overhead.csv (control plane)");
    NS_LOG_UNCOND("============================================================");
    
    return 0;
}
