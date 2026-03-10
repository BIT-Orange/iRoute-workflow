/**
 * @file iroute-v2-exp2.2-param-sweep.cc
 * @brief Exp2.2: Parameter sweep for trade-off analysis
 *
 * This experiment performs parameter sweeps to characterize iRoute trade-offs:
 * - Domains: {4, 8, 16} - Impact of semantic partitioning
 * - M: {1, 2, 4, 8} - Centroid granularity
 * - kMax: {1, 3, 5} - Probe budget
 * - τ: {0, 0.05, 0.1} - Early-stopping threshold
 *
 * Output files:
 *   - exp2.2_sweep.csv: Per-config results
 *   - exp2.2_domains_sweep.csv: Domain count impact
 *   - exp2.2_M_sweep.csv: Centroid count impact
 *   - exp2.2_kmax_sweep.csv: Probe budget impact
 *   - exp2.2_tau_sweep.csv: Threshold impact
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
#include <map>
#include <set>
#include <iomanip>
#include <filesystem>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp2ParamSweep");

// =============================================================================
// Global Parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_vectorDim = 128;
static double g_simTime = 200.0;
static uint32_t g_fetchTimeoutMs = 4000;
static std::string g_resultDir = "results/exp2.2_sweep";

// Data files
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";

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
        if (c == '"') inQuotes = !inQuotes;
        else if (c == '[') { bracketDepth++; current += c; }
        else if (c == ']') { bracketDepth--; current += c; }
        else if (c == ',' && !inQuotes && bracketDepth == 0) {
            result.push_back(current); current.clear();
        }
        else current += c;
    }
    if (!current.empty() || !result.empty()) result.push_back(current);
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
// Data Structures
// =============================================================================

struct ContentEntry {
    uint32_t domainId;
    std::string docId;
    std::string canonicalName;
    std::vector<float> vector;
};

struct SweepConfig {
    uint32_t domains;
    uint32_t M;
    uint32_t kMax;
    double tau;
};

struct SweepResult {
    SweepConfig config;
    
    uint32_t totalQueries = 0;
    uint32_t domainCorrect = 0;
    uint32_t docCorrect = 0;
    
    double avgProbes = 0;
    double avgLatencyMs = 0;
    double p50LatencyMs = 0;
    double p95LatencyMs = 0;
    
    uint64_t totalControlBytes = 0;
    uint64_t totalInterests = 0;
    
    double GetDomainAcc() const { return totalQueries > 0 ? 100.0 * domainCorrect / totalQueries : 0; }
    double GetDocAcc() const { return totalQueries > 0 ? 100.0 * docCorrect / totalQueries : 0; }
};

// =============================================================================
// Data Loading
// =============================================================================

std::map<uint32_t, std::vector<ContentEntry>> LoadContentFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<ContentEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) return result;
    
    std::string line;
    std::getline(file, line);
    
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
    if (!file.is_open()) return result;
    
    std::string line;
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 5) continue;
        
        IRouteDiscoveryConsumer::QueryItem item;
        try {
            // Format: query_id,query_text,vector,target_docids,target_domains
            auto vec = ParseVectorString(fields[2]);  // vector is at index 2
            item.vector = MakeSemanticVector(vec);
            
            // Parse target_docids (index 3)
            if (fields.size() > 3 && !fields[3].empty()) {
                std::stringstream ss(fields[3]);
                std::string docId;
                while (std::getline(ss, docId, ';')) {
                    if (!docId.empty()) item.targetDocIds.push_back(docId);
                }
            }
            
            // Parse target_domains (index 4)
            if (fields.size() > 4 && !fields[4].empty()) {
                std::stringstream ss(fields[4]);
                std::string domain;
                while (std::getline(ss, domain, ';')) {
                    if (!domain.empty()) item.targetDomains.push_back(domain);
                }
            }
            
            // expectedDomain would be at index 5 if present
            if (fields.size() > 5 && !fields[5].empty()) {
                item.expectedDomain = fields[5];
            } else if (!item.targetDomains.empty()) {
                item.expectedDomain = item.targetDomains[0];
            }
        } catch (...) { continue; }
        
        result.push_back(item);
    }
    return result;
}

std::map<uint32_t, std::vector<iroute::CentroidEntry>> LoadCentroidsFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) return result;
    
    std::string line;
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 6) continue;
        
        try {
            // Format: domain_id,centroid_id,vector_dim,vector,radius,weight
            uint32_t domainId = std::stoul(fields[0]);
            uint32_t centroidId = std::stoul(fields[1]);
            auto vec = ParseVectorString(fields[3]);  // vector is at index 3
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
    return result;
}

// =============================================================================
// Simulation Runner
// =============================================================================

class ParameterSweep {
public:
    ParameterSweep() {}
    
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
    
    SweepResult RunConfig(const SweepConfig& cfg, uint32_t seed) {
        NS_LOG_UNCOND("Running: D=" << cfg.domains << ", M=" << cfg.M 
                      << ", kMax=" << cfg.kMax << ", tau=" << cfg.tau);
        
        SweepResult result;
        result.config = cfg;
        
        // Create topology
        NodeContainer nodes;
        nodes.Create(1 + cfg.domains);
        
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        
        for (uint32_t d = 0; d < cfg.domains; ++d) {
            p2p.Install(nodes.Get(0), nodes.Get(1 + d));
        }
        
        // Install NDN stack
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(1000);
        ndnHelper.InstallAll();
        
        Ptr<Node> ingressNode = nodes.Get(0);
        
        // Install IRouteApp on ingress
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        irouteHelper.SetAttribute("RouterId", StringValue("ingress"));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
        irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(cfg.tau));
        irouteHelper.Install(ingressNode);
        
        // Prepare centroids: sample M centroids per domain
        std::map<uint32_t, std::vector<iroute::CentroidEntry>> sampledCentroids;
        std::mt19937 rng(seed);
        
        for (uint32_t d = 0; d < cfg.domains; ++d) {
            if (m_centroids.count(d) && !m_centroids[d].empty()) {
                auto& src = m_centroids[d];
                std::vector<iroute::CentroidEntry> sampled;
                
                if (src.size() <= cfg.M) {
                    sampled = src;
                } else {
                    // Random sample M centroids
                    std::vector<size_t> indices(src.size());
                    std::iota(indices.begin(), indices.end(), 0);
                    std::shuffle(indices.begin(), indices.end(), rng);
                    for (size_t i = 0; i < cfg.M; ++i) {
                        sampled.push_back(src[indices[i]]);
                        sampled.back().centroidId = i;
                    }
                }
                sampledCentroids[d] = sampled;
            }
        }
        
        // Install apps on domain nodes
        for (uint32_t d = 0; d < cfg.domains; ++d) {
            Ptr<Node> domainNode = nodes.Get(1 + d);
            std::string domainName = "/domain" + std::to_string(d);
            
            irouteHelper.SetAttribute("RouterId", StringValue(domainName));
            irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
            irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(cfg.tau));
            auto apps = irouteHelper.Install(domainNode);
            
            if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
                if (sampledCentroids.count(d)) {
                    irouteApp->SetLocalCentroids(sampledCentroids[d]);
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
            
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.Install(domainNode);
        }
        
        // Global routing
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        for (uint32_t d = 0; d < cfg.domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, nodes.Get(1 + d));
            grHelper.AddOrigins(domainName + "/data", nodes.Get(1 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();
        
        // Populate DomainIndex
        auto ingressRM = iroute::RouteManagerRegistry::getOrCreate(ingressNode->GetId(), g_vectorDim);
        ingressRM->setActiveSemVerId(1);
        
        for (uint32_t d = 0; d < cfg.domains; ++d) {
            iroute::DomainEntry entry;
            entry.domainId = Name("/domain" + std::to_string(d));
            entry.semVerId = 1;
            entry.seqNo = 1;
            entry.cost = 1.0;
            if (sampledCentroids.count(d)) {
                entry.centroids = sampledCentroids[d];
            }
            ingressRM->updateDomain(entry);
        }
        
        // Remap query targets to available domains
        auto remappedTrace = m_queryTrace;
        for (auto& q : remappedTrace) {
            std::vector<std::string> newDomains;
            for (const auto& td : q.targetDomains) {
                // Extract domain number
                size_t pos = td.find("/domain");
                if (pos != std::string::npos) {
                    try {
                        uint32_t origDom = std::stoul(td.substr(pos + 7));
                        uint32_t mappedDom = origDom % cfg.domains;
                        newDomains.push_back("/domain" + std::to_string(mappedDom));
                    } catch (...) {}
                }
            }
            if (!newDomains.empty()) {
                q.targetDomains = newDomains;
            }
            if (!q.expectedDomain.empty()) {
                size_t pos = q.expectedDomain.find("/domain");
                if (pos != std::string::npos) {
                    try {
                        uint32_t origDom = std::stoul(q.expectedDomain.substr(pos + 7));
                        uint32_t mappedDom = origDom % cfg.domains;
                        q.expectedDomain = "/domain" + std::to_string(mappedDom);
                    } catch (...) {}
                }
            }
        }
        
        // Install consumer
        AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
        consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
        consumerHelper.SetAttribute("KMax", UintegerValue(cfg.kMax));
        consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(cfg.tau));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(ingressNode);
        consumerApps.Start(Seconds(5.0));
        
        Ptr<IRouteDiscoveryConsumer> consumer = DynamicCast<IRouteDiscoveryConsumer>(consumerApps.Get(0));
        if (consumer) {
            Simulator::Schedule(Seconds(4.9), [consumer, &remappedTrace]() {
                consumer->SetQueryTrace(remappedTrace);
            });
        }
        
        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();
        
        // Collect results
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            result.totalQueries = txs.size();
            
            std::vector<double> latencies;
            double totalProbes = 0;
            
            for (const auto& tx : txs) {
                // Domain correctness
                bool domainHit = false;
                for (const auto& td : tx.targetDomains) {
                    if (tx.selectedDomain.find(td) != std::string::npos ||
                        td.find(tx.selectedDomain) != std::string::npos) {
                        domainHit = true;
                        break;
                    }
                }
                if (domainHit) result.domainCorrect++;
                
                // Doc correctness
                bool docHit = false;
                for (const auto& targetDoc : tx.targetDocIds) {
                    if (tx.requestedName.find(targetDoc) != std::string::npos) {
                        docHit = true;
                        break;
                    }
                }
                if (docHit) result.docCorrect++;
                
                if (tx.totalMs > 0) {
                    latencies.push_back(tx.totalMs);
                }
                
                totalProbes += tx.probesUsed;
                result.totalControlBytes += tx.stage1InterestBytes + tx.stage1DataBytes;
                result.totalInterests += tx.probesUsed;
            }
            
            result.avgProbes = txs.empty() ? 0 : totalProbes / txs.size();
            
            if (!latencies.empty()) {
                std::sort(latencies.begin(), latencies.end());
                double sum = 0;
                for (double l : latencies) sum += l;
                result.avgLatencyMs = sum / latencies.size();
                result.p50LatencyMs = latencies[latencies.size() / 2];
                result.p95LatencyMs = latencies[(size_t)(latencies.size() * 0.95)];
            }
        }
        
        Simulator::Destroy();
        return result;
    }
    
private:
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> m_centroids;
    std::map<uint32_t, std::vector<ContentEntry>> m_content;
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
};

// =============================================================================
// Export Functions
// =============================================================================

void ExportAllResults(const std::vector<SweepResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return;
    
    file << "domains,M,kMax,tau,queries,domain_acc,doc_acc,avg_probes,avg_ms,p50_ms,p95_ms,ctrl_bytes\n";
    
    for (const auto& r : results) {
        file << r.config.domains << ","
             << r.config.M << ","
             << r.config.kMax << ","
             << std::fixed << std::setprecision(2) << r.config.tau << ","
             << r.totalQueries << ","
             << std::fixed << std::setprecision(2) << r.GetDomainAcc() << ","
             << std::fixed << std::setprecision(2) << r.GetDocAcc() << ","
             << std::fixed << std::setprecision(2) << r.avgProbes << ","
             << std::fixed << std::setprecision(2) << r.avgLatencyMs << ","
             << std::fixed << std::setprecision(2) << r.p50LatencyMs << ","
             << std::fixed << std::setprecision(2) << r.p95LatencyMs << ","
             << r.totalControlBytes << "\n";
    }
    
    NS_LOG_UNCOND("Exported " << results.size() << " configs to " << filename);
}

void ExportDimensionSweep(const std::vector<SweepResult>& results, 
                          const std::string& dimension,
                          const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return;
    
    file << dimension << ",domain_acc,doc_acc,avg_probes,p50_ms,p95_ms\n";
    
    // Aggregate by dimension
    std::map<std::string, std::vector<const SweepResult*>> grouped;
    
    for (const auto& r : results) {
        std::string key;
        if (dimension == "domains") key = std::to_string(r.config.domains);
        else if (dimension == "M") key = std::to_string(r.config.M);
        else if (dimension == "kMax") key = std::to_string(r.config.kMax);
        else if (dimension == "tau") key = std::to_string(r.config.tau);
        
        grouped[key].push_back(&r);
    }
    
    for (const auto& [key, group] : grouped) {
        double domAcc = 0, docAcc = 0, avgProbes = 0, p50 = 0, p95 = 0;
        for (const auto* r : group) {
            domAcc += r->GetDomainAcc();
            docAcc += r->GetDocAcc();
            avgProbes += r->avgProbes;
            p50 += r->p50LatencyMs;
            p95 += r->p95LatencyMs;
        }
        size_t n = group.size();
        file << key << ","
             << std::fixed << std::setprecision(2) << (domAcc / n) << ","
             << std::fixed << std::setprecision(2) << (docAcc / n) << ","
             << std::fixed << std::setprecision(2) << (avgProbes / n) << ","
             << std::fixed << std::setprecision(2) << (p50 / n) << ","
             << std::fixed << std::setprecision(2) << (p95 / n) << "\n";
    }
    
    NS_LOG_UNCOND("Exported " << dimension << " sweep to " << filename);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("simTime", "Simulation time (seconds)", g_simTime);
    cmd.AddValue("resultDir", "Output directory", g_resultDir);
    cmd.AddValue("centroids", "Centroids CSV file", g_centroidsFile);
    cmd.AddValue("content", "Content CSV file", g_contentFile);
    cmd.AddValue("trace", "Query trace CSV file", g_traceFile);
    cmd.Parse(argc, argv);
    
    CreateDirectories(g_resultDir);
    
    NS_LOG_UNCOND("=== Exp2.2: Parameter Sweep ===");
    
    if (g_centroidsFile.empty() || g_contentFile.empty() || g_traceFile.empty()) {
        NS_LOG_ERROR("Missing data files. Please specify --centroids, --content, and --trace");
        return 1;
    }
    
    ParameterSweep sweep;
    sweep.LoadData(g_centroidsFile, g_contentFile, g_traceFile);
    
    // Parameter grid
    std::vector<uint32_t> domainValues = {4, 8, 16};
    std::vector<uint32_t> mValues = {1, 2, 4, 8};
    std::vector<uint32_t> kMaxValues = {1, 3, 5};
    std::vector<double> tauValues = {0.0, 0.05, 0.1};
    
    std::vector<SweepResult> allResults;
    uint32_t configIdx = 0;
    
    // Full grid sweep
    for (uint32_t domains : domainValues) {
        for (uint32_t M : mValues) {
            for (uint32_t kMax : kMaxValues) {
                for (double tau : tauValues) {
                    SweepConfig cfg;
                    cfg.domains = domains;
                    cfg.M = M;
                    cfg.kMax = kMax;
                    cfg.tau = tau;
                    
                    auto result = sweep.RunConfig(cfg, g_seed + configIdx);
                    allResults.push_back(result);
                    
                    NS_LOG_UNCOND("  -> DomAcc=" << result.GetDomainAcc() 
                                  << "%, DocAcc=" << result.GetDocAcc()
                                  << "%, AvgProbes=" << result.avgProbes);
                    
                    ++configIdx;
                }
            }
        }
    }
    
    NS_LOG_UNCOND("\n=== Completed " << allResults.size() << " configurations ===");
    
    // Export results
    ExportAllResults(allResults, g_resultDir + "/exp2.2_sweep.csv");
    ExportDimensionSweep(allResults, "domains", g_resultDir + "/exp2.2_domains_sweep.csv");
    ExportDimensionSweep(allResults, "M", g_resultDir + "/exp2.2_M_sweep.csv");
    ExportDimensionSweep(allResults, "kMax", g_resultDir + "/exp2.2_kmax_sweep.csv");
    ExportDimensionSweep(allResults, "tau", g_resultDir + "/exp2.2_tau_sweep.csv");
    
    NS_LOG_UNCOND("\nResults saved to " << g_resultDir);
    
    return 0;
}
