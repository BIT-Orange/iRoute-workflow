/**
 * @file iroute-v2-exp1-accuracy.cc
 * @brief Exp1: Accuracy using REAL protocol workflow
 *
 * Measures accuracy, success rate, and latency with full NDN exchanges.
 * Supports both star topology and Rocketfuel topology with domain partitioning.
 *
 * Added CLI options:
 *   --topo=star|rocketfuel
 *   --topoFile=path/to/topology.txt
 *   --centroidsFile=path/to/domain_centroids.csv
 *   --traceFile=path/to/consumer_trace.csv
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
#include <unordered_set>
#include <unordered_map>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp1RealAccuracy");

// Global parameters
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 60.0;
static uint32_t g_vectorDim = 64;
static uint32_t g_domains = 5;
static uint32_t g_M = 4;
static uint32_t g_queries = 50;
static uint32_t g_kMax = 5;
static double g_tau = 0.35;
static uint32_t g_fetchTimeoutMs = 4000;
static uint32_t g_csSize = 0;
static std::string g_resultDir = "results/exp1";

// New topology options
static std::string g_topo = "star";  // "star" or "rocketfuel"
static std::string g_topoFile = "";  // Path to topology file for rocketfuel
static std::string g_centroidsFile = "";  // Path to domain_centroids.csv
static std::string g_traceFile = "";  // Path to consumer_trace.csv
static std::string g_contentFile = "";  // Path to producer_content.csv (for doc-level matching)
static uint32_t g_ingressNodeId = 0;  // ID of ingress node (default: Node0)

std::vector<float> GenerateRandomVector(std::mt19937& rng, uint32_t dim) {
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> v(dim);
    float norm = 0.0f;
    for (uint32_t i = 0; i < dim; ++i) {
        v[i] = dist(rng);
        norm += v[i] * v[i];
    }
    norm = std::sqrt(norm);
    for (uint32_t i = 0; i < dim; ++i) v[i] /= norm;
    return v;
}

iroute::SemanticVector MakeSemanticVector(const std::vector<float>& v) {
    iroute::SemanticVector sv(v);
    sv.normalize();
    return sv;
}

double Percentile(std::vector<double>& data, double p) {
    if (data.empty()) return 0.0;
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    return data[idx];
}

// BFS Hop-Distance Computation (single pair)
uint32_t ComputeHopDistance(Ptr<Node> src, Ptr<Node> dst) {
    if (src == dst) return 0;
    std::map<uint32_t, uint32_t> distance;
    std::queue<Ptr<Node>> queue;
    distance[src->GetId()] = 0;
    queue.push(src);
    while (!queue.empty()) {
        Ptr<Node> current = queue.front();
        queue.pop();
        uint32_t currentDist = distance[current->GetId()];
        for (uint32_t i = 0; i < current->GetNDevices(); ++i) {
            Ptr<NetDevice> dev = current->GetDevice(i);
            Ptr<Channel> channel = dev->GetChannel();
            if (!channel) continue;
            for (std::size_t j = 0; j < channel->GetNDevices(); ++j) {
                Ptr<NetDevice> otherDev = channel->GetDevice(j);
                Ptr<Node> neighbor = otherDev->GetNode();
                if (neighbor == current) continue;
                if (distance.find(neighbor->GetId()) == distance.end()) {
                    distance[neighbor->GetId()] = currentDist + 1;
                    if (neighbor == dst) return currentDist + 1;
                    queue.push(neighbor);
                }
            }
        }
    }
    return UINT32_MAX;
}

// BFS from single source to all nodes (returns distance map)
std::map<uint32_t, uint32_t> BFSAllDistances(Ptr<Node> src) {
    std::map<uint32_t, uint32_t> distance;
    std::queue<Ptr<Node>> queue;
    distance[src->GetId()] = 0;
    queue.push(src);
    while (!queue.empty()) {
        Ptr<Node> current = queue.front();
        queue.pop();
        uint32_t currentDist = distance[current->GetId()];
        for (uint32_t i = 0; i < current->GetNDevices(); ++i) {
            Ptr<NetDevice> dev = current->GetDevice(i);
            Ptr<Channel> channel = dev->GetChannel();
            if (!channel) continue;
            for (std::size_t j = 0; j < channel->GetNDevices(); ++j) {
                Ptr<NetDevice> otherDev = channel->GetDevice(j);
                Ptr<Node> neighbor = otherDev->GetNode();
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
 * @param allNodes All nodes in the topology
 * @param D Number of domains/gateways
 * @param startNodeId Starting node for selection (typically ingress)
 * @return Vector of gateway node IDs
 */
std::vector<uint32_t> SelectGatewayNodes(const NodeContainer& allNodes, uint32_t D, uint32_t startNodeId) {
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
        // Compute min distance from each node to any existing gateway
        std::map<uint32_t, uint32_t> minDistToGateway;
        for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
            uint32_t nid = allNodes.Get(i)->GetId();
            minDistToGateway[nid] = UINT32_MAX;
        }
        
        for (uint32_t gwId : gateways) {
            // Find node by ID
            Ptr<Node> gwNode = nullptr;
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
        
        // Find node with maximum min-distance to any gateway
        uint32_t bestNode = 0;
        uint32_t bestDist = 0;
        for (auto& p : minDistToGateway) {
            if (selected.count(p.first) == 0 && p.second > bestDist && p.second != UINT32_MAX) {
                bestDist = p.second;
                bestNode = p.first;
            }
        }
        
        if (bestDist == 0) break;  // No more nodes to select
        gateways.push_back(bestNode);
        selected.insert(bestNode);
    }
    
    return gateways;
}

/**
 * @brief Assign each node to nearest gateway using BFS (Voronoi partition)
 * @param allNodes All nodes in topology
 * @param gateways List of gateway node IDs
 * @return Map from node ID to domain index (0..D-1)
 */
std::map<uint32_t, uint32_t> AssignNodesToDomains(const NodeContainer& allNodes, 
                                                   const std::vector<uint32_t>& gateways) {
    std::map<uint32_t, uint32_t> nodeToDomain;
    std::map<uint32_t, uint32_t> nodeToMinDist;
    
    // Initialize all nodes as unassigned
    for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
        uint32_t nid = allNodes.Get(i)->GetId();
        nodeToDomain[nid] = UINT32_MAX;
        nodeToMinDist[nid] = UINT32_MAX;
    }
    
    // For each gateway, compute distances and assign nodes
    for (uint32_t d = 0; d < gateways.size(); ++d) {
        uint32_t gwId = gateways[d];
        Ptr<Node> gwNode = nullptr;
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
 * @brief Parse CSV line into fields (handles quoted strings and bracketed vectors)
 */
std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    int bracketDepth = 0;  // Handle nested [...] vector fields
    
    for (char c : line) {
        // Skip carriage return (handle CRLF line endings)
        if (c == '\r') continue;
        
        if (c == '"') {
            inQuotes = !inQuotes;
            // Don't add quote to field - strip quotes
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
    fields.push_back(field);
    return fields;
}

/**
 * @brief Parse vector string "[0.1, 0.2, ...]" into float vector
 */
std::vector<float> ParseVectorString(const std::string& s) {
    std::vector<float> result;
    std::string clean = s;
    // Remove brackets and quotes
    clean.erase(std::remove(clean.begin(), clean.end(), '['), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), ']'), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), '"'), clean.end());
    
    std::stringstream ss(clean);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            result.push_back(std::stof(token));
        } catch (...) {
            // Skip invalid tokens
        }
    }
    return result;
}

/**
 * @brief Load domain centroids from CSV file
 * 
 * New schema (from build_trace_from_local.py):
 *   domain_id, centroid_id, vector_dim, vector, radius, weight
 * 
 * Legacy schema (also supported):
 *   domain_id, centroid_id, vector[, radius, weight]
 */
std::map<uint32_t, std::vector<iroute::CentroidEntry>> LoadCentroidsFromCsv(const std::string& filepath) {
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> result;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open centroids file: " << filepath);
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Read header to detect schema
    auto header = ParseCsvLine(line);
    
    // Detect schema by header column names
    int domainIdCol = 0;
    int centroidIdCol = 1;
    int vectorDimCol = -1;  // New: optional vector_dim column
    int vectorCol = 2;      // Default for legacy schema
    int radiusCol = -1;
    int weightCol = -1;
    
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "domain_id") domainIdCol = i;
        else if (header[i] == "centroid_id") centroidIdCol = i;
        else if (header[i] == "vector_dim") vectorDimCol = i;
        else if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "radius") radiusCol = i;
        else if (header[i] == "weight") weightCol = i;
    }
    
    NS_LOG_DEBUG("Centroids CSV schema: vectorCol=" << vectorCol 
                 << ", radiusCol=" << radiusCol << ", weightCol=" << weightCol);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 3) continue;
        
        uint32_t domainId = std::stoul(fields[domainIdCol]);
        uint32_t centroidId = std::stoul(fields[centroidIdCol]);
        auto vec = ParseVectorString(fields[vectorCol]);
        
        // Get radius and weight from detected columns
        double radius = 1.0;
        double weight = 1.0;
        
        if (radiusCol >= 0 && static_cast<size_t>(radiusCol) < fields.size() && !fields[radiusCol].empty()) {
            radius = std::stod(fields[radiusCol]);
        }
        if (weightCol >= 0 && static_cast<size_t>(weightCol) < fields.size() && !fields[weightCol].empty()) {
            weight = std::stod(fields[weightCol]);
        }
        
        iroute::CentroidEntry entry;
        entry.centroidId = centroidId;
        entry.C = MakeSemanticVector(vec);
        entry.radius = radius;
        entry.weight = weight;
        
        result[domainId].push_back(entry);
    }
    
    size_t totalCentroids = 0;
    for (const auto& p : result) totalCentroids += p.second.size();
    
    // Log summary with first centroid's radius/weight
    if (!result.empty() && !result.begin()->second.empty()) {
        auto& first = result.begin()->second[0];
        NS_LOG_INFO("Loaded " << totalCentroids << " centroids for " << result.size() 
                    << " domains (sample: radius=" << first.radius << ", weight=" << first.weight << ")");
    }
    return result;
}

/**
 * @brief Load consumer trace from CSV file
 * Format: query_id,query_text,vector,target_docids,target_domains
 * 
 * New schema from build_msmarco_trec_dl_trace.py:
 *   - target_docids: semicolon-separated relevant doc IDs from qrels
 *   - target_domains: semicolon-separated domain names containing relevant docs
 * 
 * Also supports legacy format: query_id,query_text,vector,expected_domain[,target_docids]
 */
std::vector<IRouteDiscoveryConsumer::QueryItem> LoadTraceFromCsv(const std::string& filepath) {
    std::vector<IRouteDiscoveryConsumer::QueryItem> trace;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open trace file: " << filepath);
        return trace;
    }
    
    std::string line;
    std::getline(file, line);  // Read header to detect schema
    auto header = ParseCsvLine(line);
    
    // Detect schema by header column names
    bool hasTargetDomains = false;
    int targetDocidsCol = -1;
    int targetDomainsCol = -1;
    int vectorCol = 2;  // Default
    int expectedDomainCol = 3;  // Legacy format
    
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "target_docids") targetDocidsCol = i;
        else if (header[i] == "target_domains") {
            targetDomainsCol = i;
            hasTargetDomains = true;
        }
        else if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "expected_domain") expectedDomainCol = i;
    }
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 3) continue;
        
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        
        IRouteDiscoveryConsumer::QueryItem item;
        item.vector = MakeSemanticVector(vec);
        
        // Parse target_docids (semicolon-separated)
        if (targetDocidsCol >= 0 && static_cast<size_t>(targetDocidsCol) < fields.size() 
            && !fields[targetDocidsCol].empty()) {
            std::stringstream ss(fields[targetDocidsCol]);
            std::string docid;
            while (std::getline(ss, docid, ';')) {
                if (!docid.empty()) {
                    item.targetDocIds.push_back(docid);
                }
            }
            if (!item.targetDocIds.empty()) {
                item.targetName = item.targetDocIds[0];
            }
        }
        
        // Parse target_domains (semicolon-separated) - new schema
        if (hasTargetDomains && targetDomainsCol >= 0 
            && static_cast<size_t>(targetDomainsCol) < fields.size()
            && !fields[targetDomainsCol].empty()) {
            std::stringstream ss(fields[targetDomainsCol]);
            std::string domain;
            while (std::getline(ss, domain, ';')) {
                if (!domain.empty()) {
                    item.targetDomains.push_back(domain);
                }
            }
            // Set expectedDomain to first target domain
            if (!item.targetDomains.empty()) {
                item.expectedDomain = item.targetDomains[0];
            }
        } else if (static_cast<size_t>(expectedDomainCol) < fields.size()) {
            // Legacy format: use expected_domain column
            item.expectedDomain = fields[expectedDomainCol];
        }
        
        trace.push_back(item);
    }
    
    NS_LOG_INFO("Loaded " << trace.size() << " queries from " << filepath);
    return trace;
}

/**
 * @brief Load producer content from CSV file for document-level matching
 * 
 * New schema: domain_id,doc_id,canonical_name,vector
 * Legacy schema: domain_id,doc_id,vector
 * 
 * @return Map from domain_id to vector of ContentEntry
 */
std::map<uint32_t, std::vector<IRouteApp::ContentEntry>> LoadContentFromCsv(const std::string& filepath) {
    std::map<uint32_t, std::vector<IRouteApp::ContentEntry>> result;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open content file: " << filepath);
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Read header to detect schema
    auto header = ParseCsvLine(line);
    
    // Detect schema by header
    int domainIdCol = 0;
    int docIdCol = 1;
    int canonicalNameCol = -1;
    int vectorCol = 2;  // Default for legacy
    int isDistractorCol = -1;  // NEW: distractor flag column
    
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "domain_id") domainIdCol = i;
        else if (header[i] == "doc_id") docIdCol = i;
        else if (header[i] == "canonical_name") canonicalNameCol = i;
        else if (header[i] == "vector") vectorCol = i;
        else if (header[i] == "is_distractor") isDistractorCol = i;  // NEW
    }
    
    size_t relevantCount = 0, distractorCount = 0;  // NEW: stats
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 3) continue;
        
        uint32_t domainId = std::stoul(fields[domainIdCol]);
        std::string docId = fields[docIdCol];
        
        // Get vector (position depends on schema)
        auto vec = ParseVectorString(fields[vectorCol]);
        if (vec.empty()) continue;
        
        IRouteApp::ContentEntry entry;
        entry.docId = docId;
        entry.vector = MakeSemanticVector(vec);
        
        // Set canonical name if available, otherwise construct it
        if (canonicalNameCol >= 0 && static_cast<size_t>(canonicalNameCol) < fields.size()) {
            entry.canonicalName = fields[canonicalNameCol];
        } else {
            entry.canonicalName = "/domain" + std::to_string(domainId) + "/data/doc/" + docId;
        }
        
        // NEW: Parse is_distractor flag
        if (isDistractorCol >= 0 && static_cast<size_t>(isDistractorCol) < fields.size()) {
            entry.isDistractor = (fields[isDistractorCol] == "1" || fields[isDistractorCol] == "true");
            if (entry.isDistractor) ++distractorCount;
            else ++relevantCount;
        }
        
        result[domainId].push_back(entry);
    }
    
    size_t totalDocs = 0;
    for (const auto& p : result) totalDocs += p.second.size();
    NS_LOG_INFO("Loaded " << totalDocs << " documents (" << relevantCount << " relevant, " 
                << distractorCount << " distractors) across " << result.size() << " domains from " << filepath);
    return result;
}

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("run", "Run number for RNG", g_run);
    cmd.AddValue("simTime", "Simulation time", g_simTime);
    cmd.AddValue("vectorDim", "Vector dimension", g_vectorDim);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("M", "Centroids per domain", g_M);
    cmd.AddValue("queries", "Number of queries", g_queries);
    cmd.AddValue("kMax", "Bounded probing limit", g_kMax);
    cmd.AddValue("tau", "Ingress score threshold", g_tau);
    cmd.AddValue("fetchTimeoutMs", "Stage-2 fetch timeout (ms)", g_fetchTimeoutMs);
    cmd.AddValue("csSize", "Content Store size (0=default)", g_csSize);
    cmd.AddValue("resultDir", "Results directory", g_resultDir);
    // New topology options
    cmd.AddValue("topo", "Topology type: star or rocketfuel", g_topo);
    cmd.AddValue("topoFile", "Path to topology file (for rocketfuel)", g_topoFile);
    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("traceFile", "Path to consumer_trace.csv", g_traceFile);
    cmd.AddValue("contentFile", "Path to producer_content.csv (for doc-level matching)", g_contentFile);
    cmd.AddValue("ingressNodeId", "ID of ingress node", g_ingressNodeId);
    cmd.Parse(argc, argv);
    
    // =========================================================================
    // Mode detection and validation
    // =========================================================================
    bool hasTraceFile = !g_traceFile.empty();
    bool hasCentroidsFile = !g_centroidsFile.empty();
    bool hasContentFile = !g_contentFile.empty();
    
    // Check if trace file has ground truth (target_docids column)
    bool hasGroundTruth = false;
    if (hasTraceFile) {
        std::ifstream checkFile(g_traceFile);
        if (checkFile.is_open()) {
            std::string header;
            std::getline(checkFile, header);
            hasGroundTruth = (header.find("target_docids") != std::string::npos);
            checkFile.close();
        }
    }
    
    // Paper mode: trace with ground truth requires all files
    if (hasTraceFile && hasGroundTruth) {
        if (!hasCentroidsFile || !hasContentFile) {
            NS_FATAL_ERROR("Paper experiment mode detected (trace has target_docids)!\n"
                           "You must provide ALL three files:\n"
                           "  --traceFile=path/to/consumer_trace.csv\n"
                           "  --centroidsFile=path/to/domain_centroids.csv\n"
                           "  --contentFile=path/to/producer_content.csv\n\n"
                           "Generate these files using:\n"
                           "  python dataset/build_msmarco_trec_dl_trace.py --outDir ./data --split trec-dl-2019");
        }
        NS_LOG_UNCOND("*** PAPER MODE: Using real queries with ground truth ***");
    } else if (hasTraceFile || hasCentroidsFile) {
        // Partial real mode
        if (!hasContentFile) {
            NS_FATAL_ERROR("Real dataset mode requires --contentFile!\n"
                           "You provided --traceFile or --centroidsFile but not --contentFile.\n"
                           "Without contentFile, Stage-2 cannot return real document names.\n"
                           "Either:\n"
                           "  1) Provide --contentFile=path/to/producer_content.csv for real experiments\n"
                           "  2) Remove --traceFile and --centroidsFile for synthetic mode");
        }
    } else {
        // Synthetic mode warning
        NS_LOG_UNCOND("*** SYNTHETIC MODE (debug only) - NOT FOR PAPER ***");
        NS_LOG_UNCOND("    To use real data, provide --traceFile, --centroidsFile, --contentFile");
    }
    
    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);
    std::mt19937 rng(g_seed + g_run);
    
    NS_LOG_UNCOND("=== iRoute v2 Exp1: REAL Protocol Accuracy ===");
    NS_LOG_UNCOND("topo=" << g_topo << ", domains=" << g_domains << ", M=" << g_M 
                  << ", queries=" << g_queries << ", kMax=" << g_kMax << ", csSize=" << g_csSize
                  << ", seed=" << g_seed << ", run=" << g_run);
    
    NodeContainer allNodes;
    Ptr<Node> ingressNode;
    std::vector<Ptr<Node>> gatewayNodes;
    std::vector<uint32_t> gatewayNodeIds;
    std::map<uint32_t, uint32_t> nodeToDomain;
    
    if (g_topo == "rocketfuel") {
        // ====== ROCKETFUEL TOPOLOGY ======
        if (g_topoFile.empty()) {
            g_topoFile = "src/ndnSIM/examples/topologies/as1239-r0.txt";
        }
        NS_LOG_UNCOND("Loading Rocketfuel topology from: " << g_topoFile);
        
        AnnotatedTopologyReader topologyReader("", 25);
        topologyReader.SetFileName(g_topoFile);
        topologyReader.Read();
        
        allNodes = topologyReader.GetNodes();
        NS_LOG_UNCOND("Loaded " << allNodes.GetN() << " nodes from topology");
        
        if (allNodes.GetN() == 0) {
            NS_LOG_ERROR("Failed to load topology!");
            return 1;
        }
        
        // Find ingress node
        if (g_ingressNodeId >= allNodes.GetN()) {
            NS_LOG_WARN("ingressNodeId " << g_ingressNodeId << " out of range, using 0");
            g_ingressNodeId = 0;
        }
        ingressNode = allNodes.Get(g_ingressNodeId);
        NS_LOG_UNCOND("Ingress node: Node" << g_ingressNodeId);
        
        // Select gateway nodes using farthest-first traversal
        NS_LOG_UNCOND("Selecting " << g_domains << " gateway nodes...");
        gatewayNodeIds = SelectGatewayNodes(allNodes, g_domains, g_ingressNodeId);
        
        for (uint32_t gwId : gatewayNodeIds) {
            for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
                if (allNodes.Get(i)->GetId() == gwId) {
                    gatewayNodes.push_back(allNodes.Get(i));
                    break;
                }
            }
        }
        
        NS_LOG_UNCOND("Gateway nodes selected: ");
        for (size_t d = 0; d < gatewayNodeIds.size(); ++d) {
            NS_LOG_UNCOND("  Domain " << d << " -> Gateway NodeId " << gatewayNodeIds[d]);
        }
        
        // Assign nodes to domains (Voronoi partition)
        nodeToDomain = AssignNodesToDomains(allNodes, gatewayNodeIds);
        
        // Count nodes per domain
        std::map<uint32_t, uint32_t> domainSizes;
        for (auto& p : nodeToDomain) {
            if (p.second != UINT32_MAX) domainSizes[p.second]++;
        }
        NS_LOG_UNCOND("Domain partition sizes:");
        for (auto& p : domainSizes) {
            NS_LOG_UNCOND("  Domain " << p.first << ": " << p.second << " nodes");
        }
        
    } else {
        // ====== STAR TOPOLOGY (original) ======
        allNodes.Create(1 + g_domains);
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("10ms"));
        
        for (uint32_t i = 0; i < g_domains; ++i) {
            p2p.Install(allNodes.Get(0), allNodes.Get(1 + i));
        }
        
        ingressNode = allNodes.Get(0);
        for (uint32_t i = 0; i < g_domains; ++i) {
            gatewayNodes.push_back(allNodes.Get(1 + i));
            gatewayNodeIds.push_back(allNodes.Get(1 + i)->GetId());
            nodeToDomain[allNodes.Get(1 + i)->GetId()] = i;
        }
        nodeToDomain[ingressNode->GetId()] = UINT32_MAX;  // Ingress not in any domain
        
        NS_LOG_UNCOND("Star topology: 1 ingress + " << g_domains << " domain nodes");
    }
    
    // Install NDN stack
    StackHelper ndnHelper;
    if (g_csSize > 0) {
        ndnHelper.setCsSize(g_csSize);
    }
    ndnHelper.InstallAll();
    
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    
    // Generate or load domain centroids
    std::vector<std::vector<iroute::CentroidEntry>> domainCentroids(g_domains);
    
    if (!g_centroidsFile.empty()) {
        // Load from CSV
        auto loadedCentroids = LoadCentroidsFromCsv(g_centroidsFile);
        for (uint32_t d = 0; d < g_domains; ++d) {
            if (loadedCentroids.count(d)) {
                domainCentroids[d] = loadedCentroids[d];
                if (domainCentroids[d].size() > g_M) {
                    domainCentroids[d].resize(g_M);
                }
            }
        }
        NS_LOG_UNCOND("Loaded centroids from " << g_centroidsFile);
    } else {
        // Generate random centroids
        for (uint32_t d = 0; d < g_domains; ++d) {
            for (uint32_t m = 0; m < g_M; ++m) {
                auto vec = GenerateRandomVector(rng, g_vectorDim);
                iroute::CentroidEntry c;
                c.centroidId = m;
                c.C = MakeSemanticVector(vec);
                c.radius = 0.5;
                c.weight = 100.0;
                domainCentroids[d].push_back(c);
            }
        }
        NS_LOG_UNCOND("Generated random centroids: " << g_M << " per domain");
    }
    
    // Load producer content for document-level matching (optional)
    std::map<uint32_t, std::vector<IRouteApp::ContentEntry>> domainContent;
    if (!g_contentFile.empty()) {
        domainContent = LoadContentFromCsv(g_contentFile);
        NS_LOG_UNCOND("Loaded content from " << g_contentFile);
    } else if (g_traceFile.empty() && g_centroidsFile.empty()) {
        // Synthetic mode: Generate synthetic content for each centroid
        // This ensures HandleDiscoveryInterest can find matching documents
        for (uint32_t d = 0; d < g_domains; ++d) {
            for (uint32_t c = 0; c < domainCentroids[d].size(); ++c) {
                IRouteApp::ContentEntry entry;
                entry.docId = "doc" + std::to_string(d) + "_" + std::to_string(c);
                entry.vector = domainCentroids[d][c].C; // Use centroid as document vector
                domainContent[d].push_back(entry);
            }
        }
        NS_LOG_UNCOND("Generated synthetic content: " << g_M << " docs per domain");
    }
    // Install IRouteApp on ingress
    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("RouterId", StringValue("ingress"));
    irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(5.0)));
    irouteHelper.Install(ingressNode);
    
    // Install IRouteApp + SemanticProducer on gateway nodes
    for (uint32_t d = 0; d < g_domains && d < gatewayNodes.size(); ++d) {
        std::string domainName = "/domain" + std::to_string(d);
        
        irouteHelper.SetAttribute("RouterId", StringValue(domainName));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        auto apps = irouteHelper.Install(gatewayNodes[d]);
        
        if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
            irouteApp->SetLocalCentroids(domainCentroids[d]);
            // Set content store for document-level matching
            if (domainContent.count(d) && !domainContent[d].empty()) {
                irouteApp->SetLocalContent(domainContent[d]);
            }
        }
        
        AppHelper producerHelper("ns3::ndn::SemanticProducer");
        producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
        producerHelper.Install(gatewayNodes[d]);
    }
    
    // Global routing
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    for (uint32_t d = 0; d < g_domains && d < gatewayNodes.size(); ++d) {
        std::string domainName = "/domain" + std::to_string(d);
        grHelper.AddOrigins(domainName, gatewayNodes[d]);
        grHelper.AddOrigins(domainName + "/data", gatewayNodes[d]);
    }
    GlobalRoutingHelper::CalculateRoutes();
    
    // Compute hop distances for Cost_D (from ingress to each gateway)
    uint32_t costMax = 0;
    std::vector<uint32_t> hopDistances(g_domains);
    for (uint32_t d = 0; d < g_domains && d < gatewayNodes.size(); ++d) {
        hopDistances[d] = ComputeHopDistance(ingressNode, gatewayNodes[d]);
        costMax = std::max(costMax, hopDistances[d]);
        NS_LOG_UNCOND("Domain " << d << " hop distance: " << hopDistances[d]);
    }
    
    // Populate ingress DomainIndex with BFS-based cost
    auto ingressRM = iroute::RouteManagerRegistry::getOrCreate(ingressNode->GetId(), g_vectorDim);
    ingressRM->setActiveSemVerId(1);
    
    for (uint32_t d = 0; d < g_domains && d < gatewayNodes.size(); ++d) {
        iroute::DomainEntry entry;
        entry.domainId = Name("/domain" + std::to_string(d));
        entry.semVerId = 1;
        entry.seqNo = 1;
        entry.cost = static_cast<double>(hopDistances[d]);  // BFS hop-distance
        entry.centroids = domainCentroids[d];
        
        // Debug: Check centroid dimensions
        if (!entry.centroids.empty()) {
            NS_LOG_UNCOND("Domain " << d << " centroid[0] dim=" << entry.centroids[0].C.getDimension());
        }
        
        ingressRM->updateDomain(entry);
    }
    
    uint32_t domainIndexSize = ingressRM->domainCount();
    NS_LOG_UNCOND("DomainIndex populated: " << domainIndexSize << " domains, costMax=" << costMax);
    
    // Generate or load query trace
    std::vector<IRouteDiscoveryConsumer::QueryItem> queryTrace;
    
    if (!g_traceFile.empty()) {
        // Load from CSV
        queryTrace = LoadTraceFromCsv(g_traceFile);
        if (g_queries > 0 && queryTrace.size() > g_queries) {
            queryTrace.resize(g_queries);
        }
        NS_LOG_UNCOND("Loaded " << queryTrace.size() << " queries from " << g_traceFile);
        
        // Debug: Check first query vector dimension
        if (!queryTrace.empty()) {
            NS_LOG_UNCOND("Query[0] dim=" << queryTrace[0].vector.getDimension()
                          << ", targetDomains=" << queryTrace[0].targetDomains.size());
        }
    } else {
        // Generate synthetic queries
        for (uint32_t q = 0; q < g_queries; ++q) {
            uint32_t trueDomain = rng() % g_domains;
            uint32_t trueCentroid = rng() % domainCentroids[trueDomain].size();
            
            auto baseVec = domainCentroids[trueDomain][trueCentroid].C;
            std::vector<float> qvec(g_vectorDim);
            std::normal_distribution<float> noise(0.0f, 0.1f);
            for (uint32_t i = 0; i < g_vectorDim; ++i) {
                qvec[i] = baseVec[i] + noise(rng);
            }
            
            IRouteDiscoveryConsumer::QueryItem item;
            item.vector = MakeSemanticVector(qvec);
            item.expectedDomain = "/domain" + std::to_string(trueDomain);
            queryTrace.push_back(item);
        }
        NS_LOG_UNCOND("Generated " << queryTrace.size() << " synthetic queries");
    }
    
    // Install consumer
    AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
    consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
    consumerHelper.SetAttribute("SemVerId", UintegerValue(1));
    consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
    consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
    consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
    // Note: Beta now uses default value (params::kBeta).
    // τ is now applied to confidence (semantic similarity) instead of score,
    // so the system works correctly even with cost penalty enabled.
    auto consumerApps = consumerHelper.Install(ingressNode);
    consumerApps.Start(Seconds(5.0));
    
    // Debug: Test RouteManager before simulation
    if (!queryTrace.empty()) {
        auto testDomains = ingressRM->findBestDomainsV2(queryTrace[0].vector, g_kMax, 1);
        NS_LOG_UNCOND("Test findBestDomainsV2: returned " << testDomains.size() << " domains");
        for (const auto& d : testDomains) {
            NS_LOG_UNCOND("  " << d.domainId.toUri() << " score=" << d.score);
        }
    }
    
    Ptr<IRouteDiscoveryConsumer> consumer = DynamicCast<IRouteDiscoveryConsumer>(consumerApps.Get(0));
    if (consumer) {
        Simulator::Schedule(Seconds(4.9), [consumer, &queryTrace]() {
            consumer->SetQueryTrace(queryTrace);
        });
    }
    
    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();
    
    // Create results directory
    mkdir(g_resultDir.c_str(), 0755);
    
    // Export per-query CSV with all fields
    const auto& txs = consumer ? consumer->GetTransactions() : std::vector<TxRecord>();
    
    std::string queryFile = g_resultDir + "/run_query_log.csv";
    std::ofstream qf(queryFile, std::ios::trunc);
    // Updated: targetDocIds, targetDomains (semicolon-separated), docCorrect/domainCorrect
    // NEW: stage2CorrectGivenStage1 per-query (1 if both domain and doc correct, 0 if domain wrong or NA)
    qf << "queryId,expectedDomain,selectedDomain,targetDomains,domainCorrect,"
       << "targetDocIds,requestedName,docCorrect,stage2CorrectGivenStage1,"
       << "stage1Success,stage2Success,failureReason,"
       << "stage1RttMs,stage2RttMs,totalMs,cacheHitStage2,"
       << "kMax,tau,fetchTimeoutMs,"
       << "stage1IntBytes,stage1DataBytes,stage2IntBytes,stage2DataBytes,totalCtrlBytes,totalDataBytes,"
       << "topKList,finalSuccessDomain\n";
    
    std::vector<double> stage1List, stage2List, totalList;
    uint32_t correct = 0, docCorrectCount = 0, docMeasuredCount = 0;
    uint32_t domainCorrectCount = 0, domainMeasuredCount = 0;
    uint32_t success = 0, cacheHits = 0;
    double totalProbes = 0;
    size_t totalCtrlBytes = 0, totalDataBytes = 0;
    
    // NEW: Stage-level metrics for paper reporting
    // stage2CorrectGivenStage1: Among queries where stage1 succeeded (domain correct),
    // how many also got the doc correct? This isolates in-domain retrieval quality.
    uint32_t stage1CorrectCount = 0;    // Domain hit (same as domainCorrectCount)
    uint32_t stage2CorrectGivenStage1 = 0;  // Doc hit GIVEN domain was correct
    
    // Helper: extract docId from requestedName (e.g., "/domain0/data/doc/D12345" -> "D12345")
    auto extractDocId = [](const std::string& requestedName) -> std::string {
        // Format: /domain<i>/data/doc/<docid>
        size_t pos = requestedName.rfind("/doc/");
        if (pos != std::string::npos && pos + 5 < requestedName.size()) {
            return requestedName.substr(pos + 5);
        }
        return "";
    };
    
    for (const auto& tx : txs) {
        // Domain-level correctness:
        // If targetDomains is provided, check if selectedDomain ∈ targetDomains
        // Otherwise, fall back to legacy: selectedDomain == expectedDomain
        int domainCorrect = -1;  // Default: not measured
        if (!tx.targetDomains.empty()) {
            bool found = false;
            for (const auto& td : tx.targetDomains) {
                // Normalize selectedDomain: strip "/domain" prefix to match CSV ID "5"
                std::string sel = tx.selectedDomain;
                if (sel.find("/domain") == 0) {
                    sel = sel.substr(7);
                }
                
                if (sel == td) {
                    found = true;
                    break;
                }
            }
            domainCorrect = found ? 1 : 0;
            ++domainMeasuredCount;
            if (found) ++domainCorrectCount;
        } else if (!tx.expectedDomain.empty()) {
            // Legacy check
            std::string sel = tx.selectedDomain;
            if (sel.find("/domain") == 0) {
                 sel = sel.substr(7);
            }
            bool isCorrect = (tx.expectedDomain == sel);
            domainCorrect = isCorrect ? 1 : 0;
            ++domainMeasuredCount;
            if (isCorrect) ++domainCorrectCount;
        }
        
        // Doc-level correctness:
        // -1 = no ground truth (targetDocIds empty)
        //  0 = wrong doc (requestedDocId not in targetDocIds)
        //  1 = correct doc (requestedDocId in targetDocIds)
        int docCorrect = -1;  // Default: not measured
        if (!tx.targetDocIds.empty()) {
            std::string requestedDocId = extractDocId(tx.requestedName);
            bool found = false;
            for (const auto& targetDoc : tx.targetDocIds) {
                if (requestedDocId == targetDoc) {
                    found = true;
                    break;
                }
            }
            docCorrect = found ? 1 : 0;
            ++docMeasuredCount;
            if (found) ++docCorrectCount;
            
            // NEW: Track stage2 correct GIVEN stage1 was correct
            // This isolates in-domain retrieval quality
            if (domainCorrect == 1) {
                ++stage1CorrectCount;
                if (found) {
                    ++stage2CorrectGivenStage1;
                }
            }
        }
        
        // Cache hit: Only count if hopCount == 0 (local CS hit)
        bool cacheHit = tx.stage2Success && tx.stage2FromCache;
        
        if (tx.stage2Success) ++success;
        if (domainCorrect == 1) ++correct;
        if (cacheHit) ++cacheHits;
        totalProbes += tx.probesUsed;
        totalCtrlBytes += tx.totalControlBytes;
        totalDataBytes += tx.totalDataBytes;
        
        // Format targetDocIds and targetDomains as semicolon-separated strings
        std::string targetDocIdsStr;
        for (size_t i = 0; i < tx.targetDocIds.size(); ++i) {
            if (i > 0) targetDocIdsStr += ";";
            targetDocIdsStr += tx.targetDocIds[i];
        }
        std::string targetDomainsStr;
        for (size_t i = 0; i < tx.targetDomains.size(); ++i) {
            if (i > 0) targetDomainsStr += ";";
            targetDomainsStr += tx.targetDomains[i];
        }
        
        // Compute per-query stage2CorrectGivenStage1:
        // -1 if domain wrong or no ground truth
        //  0 if domain correct but doc wrong
        //  1 if domain correct AND doc correct
        int perQueryStage2GivenStage1 = -1;
        if (domainCorrect == 1 && docCorrect >= 0) {
            perQueryStage2GivenStage1 = docCorrect;  // 0 or 1
        }
        
        qf << tx.queryId << "," << tx.expectedDomain << "," << tx.selectedDomain << ","
           << targetDomainsStr << "," << domainCorrect << ","
           << targetDocIdsStr << "," << tx.requestedName << "," << docCorrect << ","
           << perQueryStage2GivenStage1 << ","
           << tx.stage1Success << "," << tx.stage2Success << "," << tx.failureReason << ","
           << tx.stage1RttMs << "," << tx.stage2RttMs << ","
           << tx.totalMs << "," << (cacheHit ? 1 : 0) << ","
           << tx.kMaxEffective << "," << tx.tauEffective << "," << tx.fetchTimeoutMsEffective << ","
           << tx.stage1InterestBytes << "," << tx.stage1DataBytes << ","
           << tx.stage2InterestBytes << "," << tx.stage2DataBytes << ","
           << tx.totalControlBytes << "," << tx.totalDataBytes << ","
           << tx.topKList << "," << tx.finalSuccessDomain << "\n";
        
        stage1List.push_back(tx.stage1RttMs);
        stage2List.push_back(tx.stage2RttMs);
        totalList.push_back(tx.totalMs);
    }
    qf.close();
    
    // Compute summary statistics
    size_t n = txs.size();
    // Domain accuracy: based on domainCorrect (IR-style: hit any relevant domain)
    double accuracy = domainMeasuredCount > 0 ? (double)domainCorrectCount / domainMeasuredCount : 0.0;
    // Doc-level accuracy: only computed on queries with ground truth (docMeasuredCount > 0)
    // If no ground truth provided, docAccuracy = -1 (NA)
    double docAccuracy = docMeasuredCount > 0 ? (double)docCorrectCount / docMeasuredCount : -1.0;
    double successRate = n > 0 ? (double)success / n : 0.0;
    double avgProbes = n > 0 ? totalProbes / n : 0.0;
    double avgStage1 = n > 0 ? std::accumulate(stage1List.begin(), stage1List.end(), 0.0) / n : 0.0;
    double avgStage2 = n > 0 ? std::accumulate(stage2List.begin(), stage2List.end(), 0.0) / n : 0.0;
    double avgTotal = n > 0 ? std::accumulate(totalList.begin(), totalList.end(), 0.0) / n : 0.0;
    double p50Total = Percentile(totalList, 0.5);
    double p95Total = Percentile(totalList, 0.95);
    
    NS_LOG_UNCOND("=== Results ===");
    NS_LOG_UNCOND("Topology: " << g_topo << ", Nodes: " << allNodes.GetN() << ", CostMax: " << costMax);
    NS_LOG_UNCOND("Queries: " << n << ", Domain Accuracy: " << (accuracy * 100) << "% (" 
                  << domainCorrectCount << "/" << domainMeasuredCount << ")");
    // Report doc-level accuracy: NA if no ground truth, otherwise percentage
    if (docAccuracy < 0) {
        NS_LOG_UNCOND("Doc Accuracy: NA (no ground truth in trace)");
    } else {
        NS_LOG_UNCOND("Doc Accuracy: " << (docAccuracy * 100) << "% (" 
                      << docCorrectCount << "/" << docMeasuredCount << " queries with qrels)");
    }
    // NEW: Report stage-level breakdown for paper
    // stage1Correct = domain hit rate
    // stage2CorrectGivenStage1 = P(doc correct | domain correct)
    // This helps verify in-domain retrieval quality is not hidden
    double stage2GivenStage1Rate = stage1CorrectCount > 0 
        ? (double)stage2CorrectGivenStage1 / stage1CorrectCount 
        : 0.0;
    NS_LOG_UNCOND("Stage1 Correct (domain hit): " << stage1CorrectCount << "/" << domainMeasuredCount);
    NS_LOG_UNCOND("Stage2 Correct | Stage1 Correct: " << (stage2GivenStage1Rate * 100) << "% (" 
                  << stage2CorrectGivenStage1 << "/" << stage1CorrectCount << ")");
    NS_LOG_UNCOND("Success: " << (successRate * 100) << "%, Cache hits: " << cacheHits);
    NS_LOG_UNCOND("Avg probes: " << avgProbes << ", Avg total: " << avgTotal << "ms");
    NS_LOG_UNCOND("p50 total: " << p50Total << "ms, p95 total: " << p95Total << "ms");
    NS_LOG_UNCOND("Total ctrl bytes: " << totalCtrlBytes << ", Total data bytes: " << totalDataBytes);
    
    // Export summary CSV (overwrite mode - each run creates fresh file)
    std::string summaryFile = g_resultDir + "/run_summary.csv";
    
    std::ofstream sf(summaryFile, std::ios::trunc);
    // docAccuracy = -1 means NA (no ground truth)
    // NEW: Added stage1Correct, stage2CorrectGivenStage1, stage2GivenStage1Rate
    sf << "runId,seed,topo,nodes,domains,M,queries,vectorDim,kMax,costMax,"
       << "successRate,domainAccuracy,docAccuracy,domainMeasured,docMeasured,"
       << "stage1Correct,stage2CorrectGivenStage1,stage2GivenStage1Rate,"
       << "avgStage1Ms,avgStage2Ms,avgTotalMs,"
       << "avgProbes,p50Total,p95Total,cacheHits,domainIndexSize,"
       << "totalCtrlBytes,totalDataBytes\n";
    
    sf << g_run << "," << g_seed << "," << g_topo << "," << allNodes.GetN() << ","
       << g_domains << "," << g_M << "," << g_queries << "," << g_vectorDim << "," << g_kMax << "," << costMax << ","
       << successRate << "," << accuracy << "," << docAccuracy << "," << domainMeasuredCount << "," << docMeasuredCount << ","
       << stage1CorrectCount << "," << stage2CorrectGivenStage1 << "," << stage2GivenStage1Rate << ","
       << avgStage1 << "," << avgStage2 << "," << avgTotal << ","
       << avgProbes << "," << p50Total << "," << p95Total << ","
       << cacheHits << "," << domainIndexSize << ","
       << totalCtrlBytes << "," << totalDataBytes << "\n";
    sf.close();
    
    NS_LOG_UNCOND("Exported: " << queryFile << " and " << summaryFile);
    
    Simulator::Destroy();
    return 0;
}
