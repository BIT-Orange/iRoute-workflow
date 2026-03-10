/**
 * @file iroute-v2-exp2.1-packet-baselines.cc
 * @brief Exp2.1: Packet-level baseline comparison with failure type breakdown
 *
 * This experiment runs ACTUAL packet-level simulations for:
 * 1. iRoute: Semantic routing with Top-K probing
 * 2. Flood-Parallel: Broadcast discovery to ALL domains simultaneously
 * 3. Centralized: Single search oracle with global knowledge
 *
 * Key differences from exp2-rocketfuel:
 * - Uses real ndnSIM apps (FloodingDiscoveryConsumer, SearchOracleApp, CentralizedSearchConsumer)
 * - Records packet-level metrics (Interest/Data counts, actual RTTs)
 * - Tracks failure type breakdown (DOMAIN_WRONG, DOC_WRONG, TIMEOUT, NACK)
 * - Uses TREC DL 2019+2020 combined dataset (~88 queries)
 *
 * Output files:
 *   - exp2.1_iroute.csv: Per-query iRoute results
 *   - exp2.1_flooding.csv: Per-query Flooding results
 *   - exp2.1_centralized.csv: Per-query Centralized results
 *   - exp2.1_comparison.csv: Combined comparison
 *   - exp2.1_failure_breakdown.csv: Failure type distribution
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/flooding-discovery-consumer.hpp"
#include "apps/search-oracle-app.hpp"
#include "apps/centralized-search-consumer.hpp"
#include "apps/random-k-discovery-consumer.hpp"
#include "apps/exact-ndn-consumer.hpp"
#include "apps/exact-ndn-producer.hpp"

// ... (existing includes)

// Global parameter for hand dict


// ... (existing params)

// ... (RunExactNDN implementation)
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <map>
#include <set>
#include <iomanip>
#include <chrono>
#include <filesystem>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp2PacketBaselines");

// =============================================================================
// Global Parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_M = 4;
static uint32_t g_kMax = 5;
static double g_tau = 0.3;
static double g_simTime = 300.0;
static uint32_t g_fetchTimeoutMs = 4000;
static std::string g_resultDir = "results/exp2.1_packet";

// Data files
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";
static std::string g_handDictFile = "";

// =============================================================================
// FailureType enum (use from flooding-discovery-consumer.hpp)
// =============================================================================
// Use FailureType from ns3::ndn namespace
using ns3::ndn::FailureType;
using ns3::ndn::FailureTypeToString;

// =============================================================================
// Utility Functions
// =============================================================================

void CreateDirectories(const std::string& path) {
    std::filesystem::create_directories(path);
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
// Data Loading
// =============================================================================

struct ContentEntry {
    uint32_t domainId;
    std::string docId;
    std::string canonicalName;
    std::vector<float> vector;
};

std::map<uint32_t, std::vector<ContentEntry>> LoadContentFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<ContentEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_LOG_WARN("Cannot open content file: " << filename);
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 4) continue;
        
        ContentEntry e;
        try {
            e.domainId = std::stoul(fields[0]);
            e.docId = fields[1];
            e.canonicalName = fields[2];
            e.vector = ParseVectorString(fields[3]);
        } catch (...) { continue; }
        
        result[e.domainId].push_back(e);
    }
    
    return result;
}

std::vector<IRouteDiscoveryConsumer::QueryItem> LoadTraceFromCsv(const std::string& filename) {
    std::vector<IRouteDiscoveryConsumer::QueryItem> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_LOG_WARN("Cannot open trace file: " << filename);
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    // Header: query_id,query_text,vector,target_docids,target_domains,source
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        // Expected: query_id(0), query_text(1), vector(2), target_docids(3), target_domains(4), source(5)
        if (fields.size() < 5) continue;
        
        IRouteDiscoveryConsumer::QueryItem item;
        try {
            // Vector is in fields[2], NOT fields[1]
            auto vec = ParseVectorString(fields[2]);
            if (vec.empty()) continue;
            item.vector = MakeSemanticVector(vec);
            
            // Parse target doc IDs (semicolon-separated) from fields[3]
            if (!fields[3].empty()) {
                std::stringstream ss(fields[3]);
                std::string docId;
                while (std::getline(ss, docId, ';')) {
                    if (!docId.empty()) item.targetDocIds.push_back(docId);
                }
            }
            
            // Parse target domains (semicolon-separated) from fields[4]
            if (!fields[4].empty()) {
                std::stringstream ss(fields[4]);
                std::string domain;
                while (std::getline(ss, domain, ';')) {
                    if (!domain.empty()) item.targetDomains.push_back(domain);
                }
            }
            
            // If there's a source field, we could use it; for now skip
            if (item.targetDomains.size() > 0) {
                item.expectedDomain = item.targetDomains[0];
            }
        } catch (...) { continue; }
        
        result.push_back(item);
    }
    
    NS_LOG_INFO("LoadTraceFromCsv: loaded " << result.size() << " queries from " << filename);
    return result;
}

std::map<uint32_t, std::vector<iroute::CentroidEntry>> LoadCentroidsFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_LOG_WARN("Cannot open centroids file: " << filename);
        return result;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    // Header: domain_id,centroid_id,vector_dim,vector,radius,weight
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        // Expected: domain_id(0), centroid_id(1), vector_dim(2), vector(3), radius(4), weight(5)
        if (fields.size() < 6) continue;
        
        try {
            uint32_t domainId = std::stoul(fields[0]);
            uint32_t centroidId = std::stoul(fields[1]);
            // fields[2] is vector_dim, skip
            auto vec = ParseVectorString(fields[3]);
            if (vec.empty()) continue;
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
    
    NS_LOG_INFO("LoadCentroidsFromCsv: loaded " << result.size() << " domains from " << filename);
    return result;
}

// =============================================================================
// Aggregated Results
// =============================================================================

struct MethodStats {
    std::string method;
    uint32_t totalQueries = 0;
    uint32_t successCount = 0;
    uint32_t domainCorrect = 0;
    uint32_t docCorrect = 0;
    
    // Failure breakdown (using int instead of enum for map compatibility)
    std::map<int, uint32_t> failureTypeCounts;
    
    // Latency (only for successful)
    std::vector<double> latencies;
    
    // Overhead
    uint64_t totalInterests = 0;
    uint64_t totalControlBytes = 0;
    uint64_t totalDataBytes = 0;
    
    double GetDomainAcc() const { return totalQueries > 0 ? 100.0 * domainCorrect / totalQueries : 0; }
    double GetDocAcc() const { return totalQueries > 0 ? 100.0 * docCorrect / totalQueries : 0; }
    
    double GetP50Latency() const {
        if (latencies.empty()) return 0;
        auto sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    double GetMeanLatency() const {
        if (latencies.empty()) return 0;
        double sum = 0.0;
        for (double v : latencies) sum += v;
        return sum / latencies.size();
    }
    
    double GetP95Latency() const {
        if (latencies.empty()) return 0;
        auto sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        return sorted[(size_t)(sorted.size() * 0.95)];
    }
};

// =============================================================================
// Simulation Runners
// =============================================================================

class Exp2PacketSimulation {
public:
    Exp2PacketSimulation(uint32_t seed, uint32_t domains, uint32_t M, uint32_t kMax, double tau)
        : m_seed(seed), m_domains(domains), m_M(M), m_kMax(kMax), m_tau(tau), m_rng(seed) {}
    
    void LoadData(const std::string& centroidsFile, 
                  const std::string& contentFile,
                  const std::string& traceFile) {
        m_centroids = LoadCentroidsFromCsv(centroidsFile);
        m_content = LoadContentFromCsv(contentFile);
        m_queryTrace = LoadTraceFromCsv(traceFile);
        
        NS_LOG_UNCOND("Loaded " << m_centroids.size() << " domains with centroids");
        NS_LOG_UNCOND("Loaded " << m_content.size() << " domains with content");
        NS_LOG_UNCOND("Loaded " << m_queryTrace.size() << " queries");
    }
    
    /**
     * Run iRoute simulation
     */
    MethodStats RunIRoute() {
        NS_LOG_UNCOND("\n=== Running iRoute ===");
        
        MethodStats stats;
        stats.method = "iRoute";
        
        // Create topology: star with ingress + domains
        NodeContainer nodes;
        nodes.Create(1 + m_domains);  // Node 0 = ingress, 1..N = domains
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.Install(nodes.Get(0), nodes.Get(1 + d));
        }
        
        // Install NDN stack
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(1000);
        ndnHelper.InstallAll();
        
        // Install IRouteApp on ingress
        Ptr<Node> ingressNode = nodes.Get(0);
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        irouteHelper.SetAttribute("RouterId", StringValue("ingress"));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
        irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(m_tau));
        irouteHelper.Install(ingressNode);
        
        // Install apps on domain nodes
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = nodes.Get(1 + d);
            std::string domainName = "/domain" + std::to_string(d);
            
            irouteHelper.SetAttribute("RouterId", StringValue(domainName));
            irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
            irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(m_tau));
            auto apps = irouteHelper.Install(domainNode);
            
            if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
                if (m_centroids.count(d)) {
                    irouteApp->SetLocalCentroids(m_centroids[d]);
                }
                if (m_content.count(d)) {
                    std::vector<IRouteApp::ContentEntry> entries;
                    for (const auto& c : m_content[d]) {
                        IRouteApp::ContentEntry e;
                        e.docId = c.docId;
                        e.vector = MakeSemanticVector(c.vector);
                        entries.push_back(e);
                    }
                    irouteApp->SetLocalContent(entries);
                }
            }
            
            // Producer
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.Install(domainNode);
        }
        
        // Global routing
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, nodes.Get(1 + d));
            grHelper.AddOrigins(domainName + "/data", nodes.Get(1 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();
        
        // Populate DomainIndex
        auto ingressRM = iroute::RouteManagerRegistry::getOrCreate(ingressNode->GetId(), g_vectorDim);
        ingressRM->setActiveSemVerId(1);
        
        for (uint32_t d = 0; d < m_domains; ++d) {
            iroute::DomainEntry entry;
            entry.domainId = Name("/domain" + std::to_string(d));
            entry.semVerId = 1;
            entry.seqNo = 1;
            entry.cost = 1.0;
            if (m_centroids.count(d)) {
                entry.centroids = m_centroids[d];
            }
            ingressRM->updateDomain(entry);
        }
        
        // Install consumer
        AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
        consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
        consumerHelper.SetAttribute("KMax", UintegerValue(m_kMax));
        consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(m_tau));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(ingressNode);
        consumerApps.Start(Seconds(5.0));
        
        Ptr<IRouteDiscoveryConsumer> consumer = DynamicCast<IRouteDiscoveryConsumer>(consumerApps.Get(0));
        if (consumer) {
            Simulator::Schedule(Seconds(4.9), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }
        
        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();
        
        // Collect results
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            stats.totalQueries = txs.size();
            
            for (const auto& tx : txs) {
                // Check domain correctness
                bool domainHit = false;
                for (const auto& td : tx.targetDomains) {
                    if (tx.selectedDomain.find(td) != std::string::npos ||
                        td.find(tx.selectedDomain) != std::string::npos) {
                        domainHit = true;
                        break;
                    }
                }
                if (domainHit) stats.domainCorrect++;
                
                // Check doc correctness
                bool docHit = false;
                for (const auto& targetDoc : tx.targetDocIds) {
                    if (tx.requestedName.find(targetDoc) != std::string::npos) {
                        docHit = true;
                        break;
                    }
                }
                if (docHit) {
                    stats.docCorrect++;
                    stats.successCount++;
                }
                
                // Latency
                if (tx.totalMs > 0) {
                    stats.latencies.push_back(tx.totalMs);
                }
                
                // Classify failure
                FailureType ft = FailureType::SUCCESS;
                if (!docHit) {
                    if (!domainHit) {
                        ft = FailureType::DOMAIN_WRONG;
                    } else {
                        ft = FailureType::DOC_WRONG;
                    }
                    if (!tx.stage1Success) {
                        ft = FailureType::DISCOVERY_TIMEOUT;
                    }
                }
                stats.failureTypeCounts[static_cast<int>(ft)]++;
                
                // Overhead
                stats.totalInterests += tx.probesUsed;
                stats.totalControlBytes += tx.stage1InterestBytes + tx.stage1DataBytes;
                stats.totalDataBytes += tx.stage2InterestBytes + tx.stage2DataBytes;
            }
            ExportQueryLogIRoute(DynamicCast<IRouteDiscoveryConsumer>(consumer));
        }
        
        Simulator::Destroy();
        return stats;
    }
    
    /**
     * Run Exact-NDN simulation (Exact Match Flooding)
     * Matches user requirement: "Exact-NDN as lower bound... consumer only based on query_text... producer only replies if local existence"
     */
    MethodStats RunExactNDN() {
        NS_LOG_UNCOND("\n=== Running Exact-NDN ===");
        
        MethodStats stats;
        stats.method = "Exact-NDN";
        
        // Create topology
        NodeContainer nodes;
        nodes.Create(1 + m_domains);
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.Install(nodes.Get(0), nodes.Get(1 + d));
        }
        
        // Install NDN stack
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(1000);
        ndnHelper.InstallAll();
        
        // Set Multicast Strategy for /exact to reach all producers
        StrategyChoiceHelper::Install(nodes, "/exact", "/localhost/nfd/strategy/multicast");
        
        Ptr<Node> ingressNode = nodes.Get(0);
        
        // Install ExactNdnProducer on domain nodes
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = nodes.Get(1 + d);
            
            AppHelper producerHelper("ns3::ndn::ExactNdnProducer");
            auto apps = producerHelper.Install(domainNode);
            
            if (auto producer = DynamicCast<ExactNdnProducer>(apps.Get(0))) {
                if (!g_handDictFile.empty()) {
                    producer->LoadDictionary(g_handDictFile);
                }
            }
        }
        
        // Global routing to populate FIB for /exact pointing to domains
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        for (uint32_t d = 0; d < m_domains; ++d) {
            grHelper.AddOrigins("/exact", nodes.Get(1 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();
        
        // Install ExactNdnConsumer
        AppHelper consumerHelper("ns3::ndn::ExactNdnConsumer");
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(1000)); // Shorter timeout for exact
        auto consumerApps = consumerHelper.Install(ingressNode);
        consumerApps.Start(Seconds(5.0));
        
        Ptr<ExactNdnConsumer> consumer = DynamicCast<ExactNdnConsumer>(consumerApps.Get(0));
        if (consumer) {
            // ExactNdnConsumer records docId/canonical/domainId from Data.
            // We need to match it against m_queryTrace inside this function after collecting stats.
            consumer->SetQueryTrace(m_queryTrace);
        }
        
        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();
        
        // Collect results
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            stats.totalQueries = txs.size();
            
            // We need to match txs with m_queryTrace by index
            for (size_t i = 0; i < txs.size() && i < m_queryTrace.size(); ++i) {
                const auto& tx = txs[i];
                const auto& query = m_queryTrace[i];
                
                // Domain hit
                bool domainHit = false;
                for (const auto& td : query.targetDomains) {
                    if (tx.selectedDomain.find(td) != std::string::npos ||
                        td.find(tx.selectedDomain) != std::string::npos) {
                        domainHit = true;
                        break;
                    }
                }
                if (domainHit) stats.domainCorrect++;
                
                // Doc hit
                bool docHit = false;
                for (const auto& targetDoc : query.targetDocIds) {
                    if (tx.requestedName.find(targetDoc) != std::string::npos) {
                        docHit = true;
                        break;
                    }
                }
                if (docHit) {
                    stats.docCorrect++;
                    stats.successCount++;
                }
                
                if (tx.totalMs > 0) stats.latencies.push_back(tx.totalMs);
                
                // Failure Type
                FailureType ft = docHit ? FailureType::SUCCESS :
                                 (!domainHit ? FailureType::DOMAIN_WRONG : FailureType::DOC_WRONG);
                if (!tx.stage1Success) ft = FailureType::DISCOVERY_TIMEOUT; // Exact failure is usually timeout (miss)
                stats.failureTypeCounts[static_cast<int>(ft)]++;
                
                // Overhead estimate: 1 broadcast interest reaches M domains (Flooding)
                stats.totalInterests += m_domains; 
                stats.totalControlBytes += tx.stage1InterestBytes * m_domains; // Flooding multiplies Interest
                stats.totalDataBytes += tx.stage1DataBytes; // Data is unicast-ish (path reverse)
            }
            ExportQueryLogExact(consumer);
        }
        
        Simulator::Destroy();
        return stats;
    }

    /**
     * Run Flooding-Parallel simulation
     */
    MethodStats RunFloodingParallel() {
        NS_LOG_UNCOND("\n=== Running Flood-Parallel ===");
        
        MethodStats stats;
        stats.method = "Flood-Parallel";
        
        // Create topology
        NodeContainer nodes;
        nodes.Create(1 + m_domains);
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.Install(nodes.Get(0), nodes.Get(1 + d));
        }
        
        // Install NDN stack
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(1000);
        ndnHelper.InstallAll();
        
        Ptr<Node> ingressNode = nodes.Get(0);
        
        // Install IRouteApp on domain nodes (for discovery reply handling)
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = nodes.Get(1 + d);
            std::string domainName = "/domain" + std::to_string(d);
            
            irouteHelper.SetAttribute("RouterId", StringValue(domainName));
            irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
            irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            auto apps = irouteHelper.Install(domainNode);
            
            if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
                if (m_centroids.count(d)) {
                    irouteApp->SetLocalCentroids(m_centroids[d]);
                }
                if (m_content.count(d)) {
                    std::vector<IRouteApp::ContentEntry> entries;
                    for (const auto& c : m_content[d]) {
                        IRouteApp::ContentEntry e;
                        e.docId = c.docId;
                        e.vector = MakeSemanticVector(c.vector);
                        entries.push_back(e);
                    }
                    irouteApp->SetLocalContent(entries);
                }
            }
            
            // Producer
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.Install(domainNode);
        }
        
        // Global routing
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, nodes.Get(1 + d));
            grHelper.AddOrigins(domainName + "/data", nodes.Get(1 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();
        
        // Build domain list for flooding consumer
        std::vector<Name> allDomains;
        for (uint32_t d = 0; d < m_domains; ++d) {
            allDomains.push_back(Name("/domain" + std::to_string(d)));
        }
        
        // Install FloodingDiscoveryConsumer
        AppHelper consumerHelper("ns3::ndn::FloodingDiscoveryConsumer");
        // Note: FloodingDiscoveryConsumer doesn't use VectorDim - it broadcasts to all domains
        consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
        consumerHelper.SetAttribute("ParallelMode", BooleanValue(true));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(ingressNode);
        consumerApps.Start(Seconds(5.0));
        
        Ptr<FloodingDiscoveryConsumer> consumer = DynamicCast<FloodingDiscoveryConsumer>(consumerApps.Get(0));
        if (consumer) {
            consumer->SetAllDomains(allDomains);
            Simulator::Schedule(Seconds(4.9), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }
        
        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();
        
        // Collect results
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            stats.totalQueries = txs.size();
            
            for (const auto& tx : txs) {
                // Domain hit
                bool domainHit = false;
                for (const auto& td : tx.targetDomains) {
                    if (tx.selectedDomain.find(td) != std::string::npos ||
                        td.find(tx.selectedDomain) != std::string::npos) {
                        domainHit = true;
                        break;
                    }
                }
                if (domainHit) stats.domainCorrect++;
                
                // Doc hit
                bool docHit = false;
                for (const auto& targetDoc : tx.targetDocIds) {
                    if (tx.requestedName.find(targetDoc) != std::string::npos) {
                        docHit = true;
                        break;
                    }
                }
                if (docHit) {
                    stats.docCorrect++;
                    stats.successCount++;
                }
                
                if (tx.totalMs > 0) stats.latencies.push_back(tx.totalMs);
                
                // Failure type
                FailureType ft = docHit ? FailureType::SUCCESS :
                                 (!domainHit ? FailureType::DOMAIN_WRONG : FailureType::DOC_WRONG);
                stats.failureTypeCounts[static_cast<int>(ft)]++;
                
                // Overhead (flooding sends to ALL domains)
                stats.totalInterests += m_domains;
                stats.totalControlBytes += tx.stage1InterestBytes + tx.stage1DataBytes;
                stats.totalDataBytes += tx.stage2InterestBytes + tx.stage2DataBytes;
            }
            ExportQueryLogFlooding(consumer);
        }
        
        Simulator::Destroy();
        return stats;
    }
    
    /**
     * Run Centralized simulation
     */
    MethodStats RunCentralized() {
        NS_LOG_UNCOND("\n=== Running Centralized ===");
        
        MethodStats stats;
        stats.method = "Centralized";
        
        // Create topology: consumer + oracle + domains
        NodeContainer nodes;
        nodes.Create(2 + m_domains);  // 0=consumer, 1=oracle, 2..N=domains
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        
        // Consumer <-> Oracle
        p2p.Install(nodes.Get(0), nodes.Get(1));
        
        // Oracle <-> Domains (for data fetch)
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.Install(nodes.Get(1), nodes.Get(2 + d));
        }
        
        // Consumer <-> Domains (direct for data fetch)
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.Install(nodes.Get(0), nodes.Get(2 + d));
        }
        
        // Install NDN stack
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(1000);
        ndnHelper.InstallAll();
        
        Ptr<Node> consumerNode = nodes.Get(0);
        Ptr<Node> oracleNode = nodes.Get(1);
        
        // Install SearchOracleApp on oracle node
        AppHelper oracleHelper("ns3::ndn::SearchOracleApp");
        oracleHelper.SetAttribute("Prefix", StringValue("/search/oracle"));
        auto oracleApps = oracleHelper.Install(oracleNode);
        
        if (auto oracle = DynamicCast<SearchOracleApp>(oracleApps.Get(0))) {
            // Populate global index
            for (const auto& kv : m_content) {
                uint32_t domId = kv.first;
                const auto& docs = kv.second;
                for (const auto& doc : docs) {
                    GlobalContentEntry entry;
                    entry.domainId = domId;
                    entry.docId = doc.docId;
                    entry.canonicalName = doc.canonicalName;
                    entry.vector = MakeSemanticVector(doc.vector);
                    oracle->AddContent(entry);
                }
            }
        }
        
        // Install SemanticProducer on domain nodes
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = nodes.Get(2 + d);
            std::string domainName = "/domain" + std::to_string(d);
            
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.Install(domainNode);
        }
        
        // Global routing
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        grHelper.AddOrigins("/search/oracle", oracleNode);
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, nodes.Get(2 + d));
            grHelper.AddOrigins(domainName + "/data", nodes.Get(2 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();
        
        // Install CentralizedSearchConsumer
        AppHelper consumerHelper("ns3::ndn::CentralizedSearchConsumer");
        consumerHelper.SetAttribute("OraclePrefix", StringValue("/search/oracle"));
        consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(consumerNode);
        consumerApps.Start(Seconds(5.0));
        
        Ptr<CentralizedSearchConsumer> consumer = DynamicCast<CentralizedSearchConsumer>(consumerApps.Get(0));
        if (consumer) {
            Simulator::Schedule(Seconds(4.9), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }
        
        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();
        
        // Collect results
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            stats.totalQueries = txs.size();
            
            for (const auto& tx : txs) {
                // Domain hit
                bool domainHit = false;
                for (const auto& td : tx.targetDomains) {
                    if (tx.selectedDomain.find(td) != std::string::npos ||
                        td.find(tx.selectedDomain) != std::string::npos) {
                        domainHit = true;
                        break;
                    }
                }
                if (domainHit) stats.domainCorrect++;
                
                // Doc hit
                bool docHit = false;
                for (const auto& targetDoc : tx.targetDocIds) {
                    if (tx.requestedName.find(targetDoc) != std::string::npos) {
                        docHit = true;
                        break;
                    }
                }
                if (docHit) {
                    stats.docCorrect++;
                    stats.successCount++;
                }
                
                if (tx.totalMs > 0) stats.latencies.push_back(tx.totalMs);
                
                // Failure type (centralized always knows best domain)
                FailureType ft = docHit ? FailureType::SUCCESS : FailureType::DOC_WRONG;
                if (!tx.stage1Success) ft = FailureType::DISCOVERY_TIMEOUT;
                stats.failureTypeCounts[static_cast<int>(ft)]++;
                
                // Overhead (1 oracle query)
                stats.totalInterests += 1;
                stats.totalControlBytes += tx.totalControlBytes;
                stats.totalDataBytes += tx.totalDataBytes;
            }
            ExportQueryLogCentralized(DynamicCast<CentralizedSearchConsumer>(consumer));
        }
        
        Simulator::Destroy();
        return stats;
    }
    
    /**
     * Run Random-K simulation - "No Semantics" Lower Bound
     * Randomly probes k domains without using vector similarity
     */
    MethodStats RunRandomK() {
        NS_LOG_UNCOND("\n=== Running Random-K (No Semantics Baseline) ===");
        
        MethodStats stats;
        stats.method = "Random-K";
        
        // Create topology
        NodeContainer nodes;
        nodes.Create(1 + m_domains);
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.Install(nodes.Get(0), nodes.Get(1 + d));
        }
        
        // Install NDN stack
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(1000);
        ndnHelper.InstallAll();
        
        Ptr<Node> ingressNode = nodes.Get(0);
        
        // Install IRouteApp on domain nodes (for handling fetch requests)
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = nodes.Get(1 + d);
            std::string domainName = "/domain" + std::to_string(d);
            
            irouteHelper.SetAttribute("RouterId", StringValue(domainName));
            irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
            irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            auto apps = irouteHelper.Install(domainNode);
            
            if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
                if (m_centroids.count(d)) {
                    irouteApp->SetLocalCentroids(m_centroids[d]);
                }
                if (m_content.count(d)) {
                    std::vector<IRouteApp::ContentEntry> entries;
                    for (const auto& c : m_content[d]) {
                        IRouteApp::ContentEntry e;
                        e.docId = c.docId;
                        e.vector = MakeSemanticVector(c.vector);
                        entries.push_back(e);
                    }
                    irouteApp->SetLocalContent(entries);
                }
            }
            
            // Producer
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.Install(domainNode);
        }
        
        // Configure routes (outside the loop)
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, nodes.Get(1 + d));
            grHelper.AddOrigins(domainName + "/data", nodes.Get(1 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();
        
        // Build domain list
        std::vector<std::string> domainList;
        for (uint32_t d = 0; d < m_domains; ++d) {
            domainList.push_back("/domain" + std::to_string(d));
        }
        
        // Install RandomKDiscoveryConsumer
        AppHelper consumerHelper("ns3::ndn::RandomKDiscoveryConsumer");
        consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
        consumerHelper.SetAttribute("KMax", UintegerValue(m_kMax));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        auto consumerApps = consumerHelper.Install(ingressNode);
        
        Ptr<RandomKDiscoveryConsumer> consumer = DynamicCast<RandomKDiscoveryConsumer>(consumerApps.Get(0));
        if (!consumer) {
            NS_LOG_ERROR("Failed to create RandomKDiscoveryConsumer");
            return stats;
        }
        
        // Convert query trace
        std::vector<RandomKDiscoveryConsumer::QueryItem> randomTrace;
        for (const auto& q : m_queryTrace) {
            RandomKDiscoveryConsumer::QueryItem item;
            item.vector = q.vector;
            item.expectedDomain = q.expectedDomain;
            item.targetDocIds = q.targetDocIds;
            item.targetDomains = q.targetDomains;
            item.targetName = q.targetName;
            randomTrace.push_back(item);
        }
        consumer->SetQueryTrace(randomTrace);
        consumer->SetDomainList(domainList);
        
        consumerApps.Start(Seconds(1.0));
        consumerApps.Stop(Seconds(g_simTime - 1.0));
        
        // Run simulation
        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();
        
        // Collect results
        consumer->ExportToCsv(g_resultDir + "/exp2.1_randomk_summary.csv"); // Rename old summary
        ExportQueryLogRandomK(consumer); // New detailed log
        
        const auto& txs = consumer->GetTransactions();
        stats.totalQueries = txs.size();
        
        for (const auto& tx : txs) {
            if (tx.domainCorrect) stats.domainCorrect++;
            if (tx.docCorrect) {
                stats.docCorrect++;
                stats.successCount++;
            }
            
            if (tx.totalMs > 0) stats.latencies.push_back(tx.totalMs);
            
            FailureType ft = FailureType::SUCCESS;
            if (!tx.stage2Success) {
                ft = FailureType::DISCOVERY_TIMEOUT;
            } else if (!tx.domainCorrect) {
                ft = FailureType::DOMAIN_WRONG;
            } else if (!tx.docCorrect) {
                ft = FailureType::DOC_WRONG;
            }
            stats.failureTypeCounts[static_cast<int>(ft)]++;
            
            // Overhead (same as iRoute - probes k domains)
            stats.totalInterests += tx.discoveryAttempts;
            // Note: Random-K doesn't have Stage-1 discovery overhead
            stats.totalControlBytes += 0;  // No semantic discovery
            stats.totalDataBytes += 0;     // Count actual fetched data separately
        }
        
        NS_LOG_UNCOND("Random-K: " << stats.totalQueries << " queries, "
                      << stats.docCorrect << " doc correct (" 
                      << (100.0 * stats.docCorrect / std::max(1u, stats.totalQueries)) << "%)");
        
        Simulator::Destroy();
        return stats;
    }
    
    // =============================================================================
    // Export Query Logs (Per-Query Detail)
    // =============================================================================

    void ExportQueryLogIRoute(Ptr<IRouteDiscoveryConsumer> consumer) {
        if (!consumer) return;
        std::string filename = g_resultDir + "/run_query_log_iroute.csv";
        consumer->ExportToCsv(filename); 
    }

    void ExportQueryLogFlooding(Ptr<FloodingDiscoveryConsumer> consumer) {
        if (!consumer) return;
        std::string filename = g_resultDir + "/run_query_log_flooding.csv";
        consumer->ExportToCsv(filename); 
    }

    void ExportQueryLogCentralized(Ptr<CentralizedSearchConsumer> consumer) {
        if (!consumer) return;
        std::string filename = g_resultDir + "/run_query_log_centralized.csv";
        consumer->ExportToCsv(filename);
    }

    void ExportQueryLogExact(Ptr<ExactNdnConsumer> consumer) {
        if (!consumer) return;
        std::string filename = g_resultDir + "/run_query_log_exact.csv";
        std::ofstream file(filename);
        
        // Explicitly write header for ExactTxRecord
        file << TxRecord::csvHeader() << ",tokenizedQuery,queryType\n";
        
        for (const auto& tx : consumer->GetTransactions()) {
            file << tx.toCsvLine() << "," 
                 << tx.tokenizedQuery << "," 
                 << tx.queryType << "\n";
        }
    }

    void ExportQueryLogRandomK(Ptr<RandomKDiscoveryConsumer> consumer) {
        if (!consumer) return;
        std::string filename = g_resultDir + "/run_query_log_randomk.csv";
        consumer->ExportToCsv(filename);
    }

private:
    uint32_t m_seed;
    uint32_t m_domains;
    uint32_t m_M;
    uint32_t m_kMax;
    double m_tau;
    std::mt19937 m_rng;
    
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> m_centroids;
    std::map<uint32_t, std::vector<ContentEntry>> m_content;
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
};

// =============================================================================
// Export Functions
// =============================================================================

void ExportComparison(const std::vector<MethodStats>& allStats, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open " << filename);
        return;
    }
    
    file << "method,queries,success,domain_acc,doc_acc,mean_ms,p50_ms,p95_ms,interests,ctrl_bytes,data_bytes\n";
    
    for (const auto& s : allStats) {
        file << s.method << ","
             << s.totalQueries << ","
             << s.successCount << ","
             << std::fixed << std::setprecision(2) << s.GetDomainAcc() << ","
             << std::fixed << std::setprecision(2) << s.GetDocAcc() << ","
             << std::fixed << std::setprecision(2) << s.GetMeanLatency() << ","
             << std::fixed << std::setprecision(2) << s.GetP50Latency() << ","
             << std::fixed << std::setprecision(2) << s.GetP95Latency() << ","
             << s.totalInterests << ","
             << s.totalControlBytes << ","
             << s.totalDataBytes << "\n";
    }
    
    NS_LOG_UNCOND("Exported comparison to " << filename);
}

void ExportFailureBreakdown(const std::vector<MethodStats>& allStats, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open " << filename);
        return;
    }
    
    file << "method,failure_type,count,percentage\n";
    
    for (const auto& s : allStats) {
        for (const auto& kv : s.failureTypeCounts) {
            int ft = kv.first;
            uint32_t count = kv.second;
            double pct = s.totalQueries > 0 ? 100.0 * count / s.totalQueries : 0;
            file << s.method << ","
                 << FailureTypeToString(static_cast<FailureType>(ft)) << ","
                 << count << ","
                 << std::fixed << std::setprecision(2) << pct << "\n";
        }
    }
    
    NS_LOG_UNCOND("Exported failure breakdown to " << filename);
}


// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    int g_run = 0; // Unused, but passed by run_experiments.py
    std::string g_method = "all";  // Which method to run: iroute, flooding, centralized, randomk, or all
    std::string g_topo = "star"; // Unused
    uint32_t g_queries = 0; // Unused
    
    CommandLine cmd;
    cmd.AddValue("run", "Run index (unused)", g_run);
    cmd.AddValue("topo", "Topology (unused)", g_topo);
    cmd.AddValue("queries", "Number of queries (unused)", g_queries);
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("M", "Centroids per domain", g_M);
    cmd.AddValue("kMax", "Max probes for iRoute", g_kMax);
    cmd.AddValue("tau", "Score threshold", g_tau);
    cmd.AddValue("simTime", "Simulation time (seconds)", g_simTime);
    cmd.AddValue("resultDir", "Output directory", g_resultDir);
    cmd.AddValue("centroids", "Centroids CSV file", g_centroidsFile);
    cmd.AddValue("content", "Content CSV file", g_contentFile);
    cmd.AddValue("trace", "Query trace CSV file", g_traceFile);
    cmd.AddValue("handDict", "Hand Dictionary for Exact-NDN", g_handDictFile);
    cmd.AddValue("method", "Method to run: iroute, flooding, centralized, exact, randomk, or all", g_method);
    cmd.Parse(argc, argv);
    
    CreateDirectories(g_resultDir);
    
    NS_LOG_UNCOND("=== Exp2.1: Packet-level Baseline Comparison ===");
    NS_LOG_UNCOND("Domains: " << g_domains << ", M: " << g_M << ", kMax: " << g_kMax << ", tau: " << g_tau);
    NS_LOG_UNCOND("Method: " << g_method);
    
    // Check data files
    if (g_centroidsFile.empty() || g_contentFile.empty() || g_traceFile.empty()) {
        NS_LOG_ERROR("Missing data files. Please specify --centroids, --content, and --trace");
        return 1;
    }
    
    std::vector<MethodStats> allStats;
    
    // Run selected method(s)
    if (g_method == "iroute" || g_method == "all") {
        Exp2PacketSimulation sim(g_seed, g_domains, g_M, g_kMax, g_tau);
        sim.LoadData(g_centroidsFile, g_contentFile, g_traceFile);
        allStats.push_back(sim.RunIRoute());
    }
    
    // Note: Running multiple methods in one process can cause GlobalRoutingHelper issues.
    // For production, run each method in a separate process.
    if (g_method == "flooding" || g_method == "all") {
        Exp2PacketSimulation sim(g_seed, g_domains, g_M, g_kMax, g_tau);
        sim.LoadData(g_centroidsFile, g_contentFile, g_traceFile);
        allStats.push_back(sim.RunFloodingParallel());
    }
    
    if (g_method == "centralized" || g_method == "all") {
        Exp2PacketSimulation sim(g_seed, g_domains, g_M, g_kMax, g_tau);
        sim.LoadData(g_centroidsFile, g_contentFile, g_traceFile);
        allStats.push_back(sim.RunCentralized());
    }

    if (g_method == "exact" || g_method == "all") {
        if (g_handDictFile.empty()) {
             NS_LOG_WARN("Skipping Exact-NDN: --handDict not specified");
        } else {
            Exp2PacketSimulation sim(g_seed, g_domains, g_M, g_kMax, g_tau);
            sim.LoadData(g_centroidsFile, g_contentFile, g_traceFile);
            allStats.push_back(sim.RunExactNDN());
        }
    }
    
    if (g_method == "randomk" || g_method == "all") {
        Exp2PacketSimulation sim(g_seed, g_domains, g_M, g_kMax, g_tau);
        sim.LoadData(g_centroidsFile, g_contentFile, g_traceFile);
        allStats.push_back(sim.RunRandomK());
    }
    
    // Print summary
    NS_LOG_UNCOND("\n=== Summary ===");
    for (const auto& s : allStats) {
        NS_LOG_UNCOND(s.method << ": DomAcc=" << s.GetDomainAcc() << "%, DocAcc=" << s.GetDocAcc()
                      << "%, p50=" << s.GetP50Latency() << "ms, p95=" << s.GetP95Latency() << "ms"
                      << ", interests=" << s.totalInterests);
    }
    
    // Export results
    std::string suffix = g_method == "all" ? "" : ("_" + g_method);
    ExportComparison(allStats, g_resultDir + "/exp2.1_comparison" + suffix + ".csv");
    ExportFailureBreakdown(allStats, g_resultDir + "/exp2.1_failure_breakdown" + suffix + ".csv");
    
    NS_LOG_UNCOND("\nResults saved to " << g_resultDir);
    
    return 0;
}
// Force rebuild
