/**
 * @file iroute-common-utils.hpp
 * @brief Shared utility functions for iRoute experiments
 *
 * Provides:
 * - CSV parsing utilities (centroids, trace, content)
 * - Topology helpers (BFS, gateway selection, domain assignment)
 * - Vector utilities
 */

#ifndef IROUTE_COMMON_UTILS_HPP
#define IROUTE_COMMON_UTILS_HPP

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "extensions/iroute-vector.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/iroute-app.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <queue>
#include <iostream>
#include <unordered_set>
#include <random>
#include <algorithm>
#include <sys/stat.h>

namespace iroute {
namespace utils {

// =============================================================================
// Vector Utilities
// =============================================================================

inline std::vector<float> GenerateRandomVector(std::mt19937& rng, uint32_t dim) {
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> v(dim);
    float norm = 0.0f;
    for (uint32_t i = 0; i < dim; ++i) {
        v[i] = dist(rng);
        norm += v[i] * v[i];
    }
    norm = std::sqrt(norm);
    if (norm > 0) {
        for (uint32_t i = 0; i < dim; ++i) v[i] /= norm;
    }
    return v;
}

inline iroute::SemanticVector MakeSemanticVector(const std::vector<float>& v) {
    iroute::SemanticVector sv(v);
    sv.normalize();
    return sv;
}

inline double Percentile(std::vector<double>& data, double p) {
    if (data.empty()) return 0.0;
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    return data[idx];
}

// =============================================================================
// CSV Parsing Utilities
// =============================================================================

/**
 * @brief Parse CSV line into fields (handles quoted strings and bracketed vectors)
 */
inline std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    int bracketDepth = 0;
    
    for (char c : line) {
        if (c == '\r') continue;
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == '[') {
            bracketDepth++;
            field += c;
        } else if (c == ']') {
            bracketDepth--;
            field += c;
        } else if (c == ',' && !inQuotes && bracketDepth == 0) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    if (!field.empty() || !fields.empty()) {
        fields.push_back(field);
    }
    return fields;
}

/**
 * @brief Parse vector string like "[0.1, 0.2, 0.3]" into float vector
 */
inline std::vector<float> ParseVectorString(const std::string& str) {
    std::vector<float> result;
    std::string cleaned = str;
    
    // Remove surrounding quotes and brackets
    while (!cleaned.empty() && (cleaned.front() == '"' || cleaned.front() == '[' || cleaned.front() == ' '))
        cleaned = cleaned.substr(1);
    while (!cleaned.empty() && (cleaned.back() == '"' || cleaned.back() == ']' || cleaned.back() == ' '))
        cleaned.pop_back();
    
    std::stringstream ss(cleaned);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            try {
                result.push_back(std::stof(token.substr(start, end - start + 1)));
            } catch (...) {}
        }
    }
    return result;
}

/**
 * @brief Load centroids from CSV file
 * Format: domain_id,centroid_id,vector_dim,vector,radius,weight
 */
inline std::map<uint32_t, std::vector<iroute::CentroidEntry>> LoadCentroidsFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[iroute-utils] WARN: Failed to open centroids file: " << filename << std::endl;
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 6) continue;
        
        try {
            uint32_t domainId = std::stoul(fields[0]);
            uint32_t centroidId = std::stoul(fields[1]);
            auto vec = ParseVectorString(fields[3]);
            double radius = std::stod(fields[4]);
            double weight = std::stod(fields[5]);
            
            iroute::CentroidEntry c;
            c.centroidId = centroidId;
            c.C = MakeSemanticVector(vec);
            c.radius = radius;
            c.weight = weight;
            
            result[domainId].push_back(c);
        } catch (...) { continue; }
    }
    
    std::cerr << "[iroute-utils] INFO: LoadCentroidsFromCsv: loaded " << result.size() << " domains from " << filename << std::endl;
    return result;
}

/**
 * @brief Query item for trace loading
 */
struct QueryItem {
    iroute::SemanticVector vector;
    std::string expectedDomain;
    std::vector<std::string> targetDocIds;
    std::vector<std::string> targetDomains;
    std::string targetName;
};

/**
 * @brief Load consumer trace from CSV file
 * Format: query_id,query_text,vector,target_docids,target_domains
 */
inline std::vector<ns3::ndn::IRouteDiscoveryConsumer::QueryItem> LoadTraceFromCsv(const std::string& filename) {
    std::vector<ns3::ndn::IRouteDiscoveryConsumer::QueryItem> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[iroute-utils] WARN: Failed to open trace file: " << filename << std::endl;
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Read header
    auto header = ParseCsvLine(line);
    
    // Detect schema
    int vectorCol = 2;
    int targetDocidsCol = -1;
    int targetDomainsCol = -1;
    int expectedDomainCol = -1;
    
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "target_docids") targetDocidsCol = i;
        else if (header[i] == "target_domains") targetDomainsCol = i;
        else if (header[i] == "expected_domain") expectedDomainCol = i;
    }
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() <= static_cast<size_t>(vectorCol)) continue;
        
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        
        ns3::ndn::IRouteDiscoveryConsumer::QueryItem item;
        item.vector = MakeSemanticVector(vec);
        
        // Parse target_docids
        if (targetDocidsCol >= 0 && static_cast<size_t>(targetDocidsCol) < fields.size() 
            && !fields[targetDocidsCol].empty()) {
            std::stringstream ss(fields[targetDocidsCol]);
            std::string docid;
            while (std::getline(ss, docid, ';')) {
                if (!docid.empty()) item.targetDocIds.push_back(docid);
            }
            if (!item.targetDocIds.empty()) {
                item.targetName = item.targetDocIds[0];
            }
        }
        
        // Parse target_domains
        if (targetDomainsCol >= 0 && static_cast<size_t>(targetDomainsCol) < fields.size()
            && !fields[targetDomainsCol].empty()) {
            std::stringstream ss(fields[targetDomainsCol]);
            std::string domain;
            while (std::getline(ss, domain, ';')) {
                if (!domain.empty()) item.targetDomains.push_back(domain);
            }
            if (!item.targetDomains.empty()) {
                item.expectedDomain = item.targetDomains[0];
            }
        } else if (expectedDomainCol >= 0 && static_cast<size_t>(expectedDomainCol) < fields.size()) {
            item.expectedDomain = fields[expectedDomainCol];
        }
        
        result.push_back(item);
    }
    
    std::cerr << "[iroute-utils] INFO: LoadTraceFromCsv: loaded " << result.size() << " queries from " << filename << std::endl;
    return result;
}

/**
 * @brief Content entry for producer content loading
 */
struct ContentEntry {
    uint32_t domainId;
    std::string docId;
    std::string canonicalName;
    std::vector<float> vector;
    bool isDistractor = false;
};

/**
 * @brief Load producer content from CSV file
 * Format: domain_id,doc_id,canonical_name,vector[,is_distractor]
 */
inline std::map<uint32_t, std::vector<ContentEntry>> LoadContentFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<ContentEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[iroute-utils] WARN: Failed to open content file: " << filename << std::endl;
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Read header
    auto header = ParseCsvLine(line);
    
    // Detect schema
    int domainIdCol = 0, docIdCol = 1, canonicalNameCol = -1, vectorCol = 2, isDistractorCol = -1;
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
        if (fields.size() <= static_cast<size_t>(vectorCol)) continue;
        
        try {
            ContentEntry e;
            e.domainId = std::stoul(fields[domainIdCol]);
            e.docId = fields[docIdCol];
            if (canonicalNameCol >= 0 && static_cast<size_t>(canonicalNameCol) < fields.size()) {
                e.canonicalName = fields[canonicalNameCol];
            } else {
                e.canonicalName = "/domain" + std::to_string(e.domainId) + "/data/doc/" + e.docId;
            }
            e.vector = ParseVectorString(fields[vectorCol]);
            if (isDistractorCol >= 0 && static_cast<size_t>(isDistractorCol) < fields.size()) {
                e.isDistractor = (fields[isDistractorCol] == "1" || fields[isDistractorCol] == "true");
            }
            result[e.domainId].push_back(e);
        } catch (...) { continue; }
    }
    
    size_t totalDocs = 0;
    for (const auto& p : result) totalDocs += p.second.size();
    std::cerr << "[iroute-utils] INFO: LoadContentFromCsv: loaded " << totalDocs << " documents across " << result.size() << " domains" << std::endl;
    return result;
}

// =============================================================================
// Topology Utilities
// =============================================================================

/**
 * @brief BFS from single source to all nodes (returns distance map)
 */
inline std::map<uint32_t, uint32_t> BFSAllDistances(ns3::Ptr<ns3::Node> src) {
    std::map<uint32_t, uint32_t> distance;
    std::queue<ns3::Ptr<ns3::Node>> queue;
    distance[src->GetId()] = 0;
    queue.push(src);
    
    while (!queue.empty()) {
        ns3::Ptr<ns3::Node> current = queue.front();
        queue.pop();
        uint32_t currentDist = distance[current->GetId()];
        
        for (uint32_t i = 0; i < current->GetNDevices(); ++i) {
            ns3::Ptr<ns3::NetDevice> dev = current->GetDevice(i);
            ns3::Ptr<ns3::Channel> channel = dev->GetChannel();
            if (!channel) continue;
            
            for (std::size_t j = 0; j < channel->GetNDevices(); ++j) {
                ns3::Ptr<ns3::NetDevice> otherDev = channel->GetDevice(j);
                ns3::Ptr<ns3::Node> neighbor = otherDev->GetNode();
                if (neighbor == current) continue;
                if (distance.find(neighbor->GetId()) == distance.end()) {
                    distance[neighbor->GetId()] = currentDist + 1;
                    queue.push(neighbor);
                }
            }
        }
    }
    return distance;
}

/**
 * @brief Select D gateway nodes using farthest-first traversal
 */
inline std::vector<uint32_t> SelectGatewayNodes(const ns3::NodeContainer& allNodes, 
                                                 uint32_t D, uint32_t startNodeId) {
    std::vector<uint32_t> gateways;
    std::unordered_set<uint32_t> selected;
    
    // Start with a node far from ingress
    auto startDistances = BFSAllDistances(allNodes.Get(startNodeId));
    
    // Find the farthest node from start as first gateway
    uint32_t farthestId = startNodeId;
    uint32_t maxDist = 0;
    for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
        uint32_t nid = allNodes.Get(i)->GetId();
        if (startDistances.count(nid) && startDistances[nid] > maxDist) {
            maxDist = startDistances[nid];
            farthestId = nid;
        }
    }
    gateways.push_back(farthestId);
    selected.insert(farthestId);
    
    // Greedily select D-1 more gateways using farthest-first
    while (gateways.size() < D) {
        std::map<uint32_t, uint32_t> minDistToGateway;
        for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
            minDistToGateway[allNodes.Get(i)->GetId()] = UINT32_MAX;
        }
        
        for (uint32_t gwId : gateways) {
            ns3::Ptr<ns3::Node> gwNode = nullptr;
            for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
                if (allNodes.Get(i)->GetId() == gwId) {
                    gwNode = allNodes.Get(i);
                    break;
                }
            }
            if (!gwNode) continue;
            
            auto distances = BFSAllDistances(gwNode);
            for (auto& p : distances) {
                if (p.second < minDistToGateway[p.first]) {
                    minDistToGateway[p.first] = p.second;
                }
            }
        }
        
        uint32_t bestNode = 0;
        uint32_t bestDist = 0;
        for (auto& p : minDistToGateway) {
            if (selected.count(p.first) == 0 && p.second > bestDist && p.second != UINT32_MAX) {
                bestDist = p.second;
                bestNode = p.first;
            }
        }
        
        if (bestDist == 0) break;
        gateways.push_back(bestNode);
        selected.insert(bestNode);
    }
    
    return gateways;
}

/**
 * @brief Assign each node to nearest gateway using BFS (Voronoi partition)
 */
inline std::map<uint32_t, uint32_t> AssignNodesToDomains(const ns3::NodeContainer& allNodes,
                                                          const std::vector<uint32_t>& gateways) {
    std::map<uint32_t, uint32_t> nodeToDomain;
    std::map<uint32_t, uint32_t> nodeToMinDist;
    
    for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
        uint32_t nid = allNodes.Get(i)->GetId();
        nodeToDomain[nid] = UINT32_MAX;
        nodeToMinDist[nid] = UINT32_MAX;
    }
    
    for (uint32_t d = 0; d < gateways.size(); ++d) {
        uint32_t gwId = gateways[d];
        ns3::Ptr<ns3::Node> gwNode = nullptr;
        for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
            if (allNodes.Get(i)->GetId() == gwId) {
                gwNode = allNodes.Get(i);
                break;
            }
        }
        if (!gwNode) continue;
        
        auto distances = BFSAllDistances(gwNode);
        for (auto& p : distances) {
            if (p.second < nodeToMinDist[p.first]) {
                nodeToMinDist[p.first] = p.second;
                nodeToDomain[p.first] = d;
            }
        }
    }
    
    return nodeToDomain;
}

/**
 * @brief Create output directory if not exists
 */
inline void CreateOutputDir(const std::string& dir) {
    mkdir(dir.c_str(), 0755);
}

// =============================================================================
// Compute Statistics (for Exp5-style benchmarks)
// =============================================================================

struct ComputeStats {
    uint64_t totalDotProducts = 0;
    uint64_t totalBytesTouched = 0;  // bytes read from centroid vectors
    uint64_t totalQueries = 0;
    
    double avgDotProductsPerQuery() const {
        return totalQueries > 0 ? static_cast<double>(totalDotProducts) / totalQueries : 0;
    }
    
    double avgBytesTouchedPerQuery() const {
        return totalQueries > 0 ? static_cast<double>(totalBytesTouched) / totalQueries : 0;
    }
};

// =============================================================================
// Hop Distance Computation
// =============================================================================

/**
 * @brief Computes hop distance between two nodes using BFS.
 * @return Hop count (0 if same node, UINT32_MAX if unreachable)
 */
inline uint32_t ComputeHopDistance(ns3::Ptr<ns3::Node> src, ns3::Ptr<ns3::Node> dst) {
    if (src == dst) return 0;
    
    auto distances = BFSAllDistances(src);
    auto it = distances.find(dst->GetId());
    if (it != distances.end()) {
        return it->second;
    }
    return UINT32_MAX;
}

/**
 * @brief Compute hop distances from a source node to multiple destinations.
 */
inline std::vector<uint32_t> ComputeHopDistances(ns3::Ptr<ns3::Node> src, 
                                                  const std::vector<ns3::Ptr<ns3::Node>>& dsts) {
    auto distances = BFSAllDistances(src);
    std::vector<uint32_t> result;
    result.reserve(dsts.size());
    for (const auto& dst : dsts) {
        auto it = distances.find(dst->GetId());
        result.push_back(it != distances.end() ? it->second : UINT32_MAX);
    }
    return result;
}

// =============================================================================
// NLSR State Estimation (for baseline comparison)
// =============================================================================

/**
 * @brief NLSR-like prefix-LSA state estimator for control-plane comparison.
 * 
 * In NLSR, each producer advertises name prefixes via Name-LSA.
 * State scales as O(#prefixes) vs iRoute's O(D*M).
 */
struct NlsrStateEstimator {
    // Average size of a prefix-LSA announcement (bytes)
    // Typical: prefix name (~50 bytes) + origin router (~20 bytes) + seq/type (~10 bytes)
    static constexpr uint32_t AVG_PREFIX_LSA_SIZE = 80;
    
    // Average size of an adjacency-LSA (per link)
    static constexpr uint32_t AVG_ADJ_LSA_SIZE = 60;
    
    uint32_t numPrefixes = 0;      // Total content prefixes (objects)
    uint32_t numRouters = 0;       // Total routers in network
    uint32_t avgLinksPerRouter = 3; // Average adjacencies
    
    /**
     * @brief Compute LSDB entry count (prefix-LSAs only, for content routing)
     */
    uint64_t getLsdbEntries() const {
        return numPrefixes;
    }
    
    /**
     * @brief Compute LSDB size in bytes (prefix-LSAs)
     */
    uint64_t getLsdbBytes() const {
        return numPrefixes * AVG_PREFIX_LSA_SIZE;
    }
    
    /**
     * @brief Compute total control-plane state including adjacency-LSAs
     */
    uint64_t getTotalStateBytes() const {
        uint64_t prefixBytes = numPrefixes * AVG_PREFIX_LSA_SIZE;
        uint64_t adjBytes = numRouters * avgLinksPerRouter * AVG_ADJ_LSA_SIZE;
        return prefixBytes + adjBytes;
    }
    
    /**
     * @brief Compute FIB entries (one per prefix in worst case)
     */
    uint64_t getFibEntries() const {
        return numPrefixes;
    }
    
    /**
     * @brief Compare with iRoute state and return reduction factor
     */
    double getStateReductionFactor(uint64_t irouteLsdbEntries) const {
        if (irouteLsdbEntries == 0) return 0;
        return static_cast<double>(getLsdbEntries()) / irouteLsdbEntries;
    }
};

/**
 * @brief iRoute state estimator for comparison
 */
struct IRouteStateEstimator {
    uint32_t numDomains = 0;       // D
    uint32_t centroidsPerDomain = 0; // M
    uint32_t vectorDim = 128;      // Embedding dimension
    
    // Average size of a Semantic-LSA entry (per centroid)
    // vector (dim*4 bytes) + centroid_id (4) + radius (8) + weight (8) + metadata (~20)
    uint32_t getAvgCentroidLsaSize() const {
        return vectorDim * 4 + 4 + 8 + 8 + 20;
    }
    
    /**
     * @brief LSDB entries = D * M (one per centroid)
     */
    uint64_t getLsdbEntries() const {
        return static_cast<uint64_t>(numDomains) * centroidsPerDomain;
    }
    
    /**
     * @brief LSDB size in bytes
     */
    uint64_t getLsdbBytes() const {
        return getLsdbEntries() * getAvgCentroidLsaSize();
    }
    
    /**
     * @brief FIB entries = D (one per domain prefix)
     */
    uint64_t getFibEntries() const {
        return numDomains;
    }
};

// =============================================================================
// Rocketfuel Topology Utilities
// =============================================================================

/**
 * @brief Result structure for Rocketfuel topology loading
 */
struct RocketfuelTopoResult {
    ns3::NodeContainer allNodes;
    ns3::Ptr<ns3::Node> ingressNode;
    std::vector<ns3::Ptr<ns3::Node>> gatewayNodes;
    std::vector<uint32_t> gatewayNodeIds;
    std::map<uint32_t, uint32_t> nodeToDomain;  // nodeId -> domainIndex
    uint32_t numNodes = 0;
    uint32_t numLinks = 0;
};

/**
 * @brief Load Rocketfuel topology and setup gateway nodes
 * 
 * @param topoFile Path to topology file (e.g., as1239-r0.txt)
 * @param numDomains Number of domains (gateway nodes to select)
 * @param ingressNodeId Node ID for ingress (0 = auto-select center)
 * @return RocketfuelTopoResult with all topology info
 */
inline RocketfuelTopoResult LoadRocketfuelTopology(
    const std::string& topoFile,
    uint32_t numDomains,
    uint32_t ingressNodeId = 0)
{
    RocketfuelTopoResult result;
    
    // Use AnnotatedTopologyReader (must be called after including ndnSIM-module.h)
    // Note: This function assumes the caller has already read the topology
    // We just provide the gateway selection logic here
    
    std::cerr << "[iroute-utils] INFO: LoadRocketfuelTopology: file=" << topoFile 
              << ", domains=" << numDomains 
              << ", ingressId=" << ingressNodeId << std::endl;
    
    return result;  // Caller should use AnnotatedTopologyReader directly
}

/**
 * @brief Select gateway nodes and assign domains for a loaded topology
 */
inline void SetupDomainsForTopology(
    RocketfuelTopoResult& topo,
    const ns3::NodeContainer& allNodes,
    uint32_t numDomains,
    uint32_t ingressNodeId = 0)
{
    topo.allNodes = allNodes;
    topo.numNodes = allNodes.GetN();
    
    if (topo.numNodes == 0) {
        std::cerr << "[iroute-utils] ERROR: SetupDomainsForTopology: Empty node container" << std::endl;
        return;
    }
    
    // Validate ingress node ID
    if (ingressNodeId >= topo.numNodes) {
        std::cerr << "[iroute-utils] WARN: ingressNodeId " << ingressNodeId << " out of range, using 0" << std::endl;
        ingressNodeId = 0;
    }
    topo.ingressNode = allNodes.Get(ingressNodeId);
    
    // Select gateway nodes using farthest-first traversal
    topo.gatewayNodeIds = SelectGatewayNodes(allNodes, numDomains, ingressNodeId);
    
    // Get gateway node pointers
    for (uint32_t gwId : topo.gatewayNodeIds) {
        for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
            if (allNodes.Get(i)->GetId() == gwId) {
                topo.gatewayNodes.push_back(allNodes.Get(i));
                break;
            }
        }
    }
    
    // Assign nodes to domains (Voronoi partition)
    topo.nodeToDomain = AssignNodesToDomains(allNodes, topo.gatewayNodeIds);
    
    std::cerr << "[iroute-utils] INFO: SetupDomainsForTopology: " << topo.gatewayNodes.size() 
              << " gateways selected" << std::endl;
}

/**
 * @brief Get domain node counts for logging
 */
inline std::map<uint32_t, uint32_t> GetDomainSizes(const std::map<uint32_t, uint32_t>& nodeToDomain) {
    std::map<uint32_t, uint32_t> sizes;
    for (const auto& p : nodeToDomain) {
        if (p.second != UINT32_MAX) {
            sizes[p.second]++;
        }
    }
    return sizes;
}

}  // namespace utils
}  // namespace iroute

#endif // IROUTE_COMMON_UTILS_HPP
