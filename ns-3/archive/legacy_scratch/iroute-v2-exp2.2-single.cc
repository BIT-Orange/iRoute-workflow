/**
 * @file iroute-v2-exp2.2-single.cc
 * @brief Exp2.2: Single configuration run for parameter sweep
 *
 * Run with different parameters via command line, then aggregate results.
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

NS_LOG_COMPONENT_DEFINE("iRouteExp2Single");

// =============================================================================
// Global Parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_vectorDim = 128;
static double g_simTime = 150.0;
static uint32_t g_fetchTimeoutMs = 4000;
static std::string g_resultDir = "results/exp2.2_sweep";

// Sweep parameters
static uint32_t g_domains = 8;
static uint32_t g_M = 4;
static uint32_t g_kMax = 3;
static double g_tau = 0.2;

// Data files
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";

// =============================================================================
// Utility Functions
// =============================================================================

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
            auto vec = ParseVectorString(fields[2]);
            item.vector = MakeSemanticVector(vec);
            
            if (fields.size() > 3 && !fields[3].empty()) {
                std::stringstream ss(fields[3]);
                std::string docId;
                while (std::getline(ss, docId, ';')) {
                    if (!docId.empty()) item.targetDocIds.push_back(docId);
                }
            }
            
            if (fields.size() > 4 && !fields[4].empty()) {
                std::stringstream ss(fields[4]);
                std::string domain;
                while (std::getline(ss, domain, ';')) {
                    if (!domain.empty()) item.targetDomains.push_back(domain);
                }
            }
            
            if (!item.targetDomains.empty()) {
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
    return result;
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
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("M", "Number of centroids per domain", g_M);
    cmd.AddValue("kMax", "Maximum probes", g_kMax);
    cmd.AddValue("tau", "Score threshold", g_tau);
    cmd.Parse(argc, argv);
    
    std::filesystem::create_directories(g_resultDir);
    
    if (g_centroidsFile.empty() || g_contentFile.empty() || g_traceFile.empty()) {
        NS_LOG_ERROR("Missing data files");
        return 1;
    }
    
    // Load data
    auto centroids = LoadCentroidsFromCsv(g_centroidsFile);
    auto content = LoadContentFromCsv(g_contentFile);
    auto queryTrace = LoadTraceFromCsv(g_traceFile);
    
    NS_LOG_UNCOND("Config: D=" << g_domains << ", M=" << g_M << ", kMax=" << g_kMax << ", tau=" << g_tau);
    NS_LOG_UNCOND("Loaded " << centroids.size() << " domain centroids, " << content.size() << " content domains, " << queryTrace.size() << " queries");
    
    // Create topology
    NodeContainer nodes;
    nodes.Create(1 + g_domains);
    
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    
    for (uint32_t d = 0; d < g_domains; ++d) {
        p2p.Install(nodes.Get(0), nodes.Get(1 + d));
    }
    
    // Install NDN stack
    StackHelper ndnHelper;
    ndnHelper.setPolicy("nfd::cs::lru");
    ndnHelper.setCsSize(1000);
    ndnHelper.InstallAll();
    
    Ptr<Node> ingressNode = nodes.Get(0);
    
    // Sample M centroids per domain
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> sampledCentroids;
    std::mt19937 rng(g_seed);
    
    for (uint32_t d = 0; d < g_domains; ++d) {
        if (centroids.count(d) && !centroids[d].empty()) {
            auto& src = centroids[d];
            std::vector<iroute::CentroidEntry> sampled;
            
            if (src.size() <= g_M) {
                sampled = src;
            } else {
                std::vector<size_t> indices(src.size());
                std::iota(indices.begin(), indices.end(), 0);
                std::shuffle(indices.begin(), indices.end(), rng);
                for (size_t i = 0; i < g_M; ++i) {
                    sampled.push_back(src[indices[i]]);
                    sampled.back().centroidId = i;
                }
            }
            sampledCentroids[d] = sampled;
        }
    }
    
    // Install IRouteApp on ingress
    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("RouterId", StringValue("ingress"));
    irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
    irouteHelper.Install(ingressNode);
    
    // Install apps on domain nodes
    for (uint32_t d = 0; d < g_domains; ++d) {
        Ptr<Node> domainNode = nodes.Get(1 + d);
        std::string domainName = "/domain" + std::to_string(d);
        
        irouteHelper.SetAttribute("RouterId", StringValue(domainName));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
        auto apps = irouteHelper.Install(domainNode);
        
        if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
            if (sampledCentroids.count(d)) {
                irouteApp->SetLocalCentroids(sampledCentroids[d]);
            }
            if (content.count(d)) {
                std::vector<IRouteApp::ContentEntry> entries;
                for (const auto& c : content[d]) {
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
        producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        producerHelper.Install(domainNode);
    }
    
    // Global routing
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    for (uint32_t d = 0; d < g_domains; ++d) {
        std::string domainName = "/domain" + std::to_string(d);
        grHelper.AddOrigins(domainName, nodes.Get(1 + d));
        grHelper.AddOrigins(domainName + "/data", nodes.Get(1 + d));
    }
    GlobalRoutingHelper::CalculateRoutes();
    
    // Populate DomainIndex
    auto ingressRM = iroute::RouteManagerRegistry::getOrCreate(ingressNode->GetId(), g_vectorDim);
    ingressRM->setActiveSemVerId(1);
    
    for (uint32_t d = 0; d < g_domains; ++d) {
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
    
    // Remap query targets
    for (auto& q : queryTrace) {
        std::vector<std::string> newDomains;
        for (const auto& td : q.targetDomains) {
            size_t pos = td.find("/domain");
            if (pos != std::string::npos) {
                try {
                    uint32_t origDom = std::stoul(td.substr(pos + 7));
                    uint32_t mappedDom = origDom % g_domains;
                    newDomains.push_back("/domain" + std::to_string(mappedDom));
                } catch (...) {}
            }
        }
        if (!newDomains.empty()) {
            q.targetDomains = newDomains;
            q.expectedDomain = newDomains[0];
        }
    }
    
    // Install consumer
    AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
    consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
    consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
    consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
    consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
    auto consumerApps = consumerHelper.Install(ingressNode);
    consumerApps.Start(Seconds(5.0));
    
    Ptr<IRouteDiscoveryConsumer> consumer = DynamicCast<IRouteDiscoveryConsumer>(consumerApps.Get(0));
    if (consumer) {
        Simulator::Schedule(Seconds(4.9), [consumer, &queryTrace]() {
            consumer->SetQueryTrace(queryTrace);
        });
    }
    
    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();
    
    // Collect results
    uint32_t totalQueries = 0, domainCorrect = 0, docCorrect = 0;
    uint32_t domainWrong = 0, docWrong = 0, timeout = 0;
    double totalProbes = 0;
    std::vector<double> latencies;
    
    if (consumer) {
        const auto& txs = consumer->GetTransactions();
        totalQueries = txs.size();
        
        for (const auto& tx : txs) {
            bool domainHit = false;
            for (const auto& td : tx.targetDomains) {
                if (tx.selectedDomain.find(td) != std::string::npos ||
                    td.find(tx.selectedDomain) != std::string::npos) {
                    domainHit = true;
                    break;
                }
            }
            if (domainHit) domainCorrect++;
            
            bool docHit = false;
            for (const auto& targetDoc : tx.targetDocIds) {
                if (tx.requestedName.find(targetDoc) != std::string::npos) {
                    docHit = true;
                    break;
                }
            }
            if (docHit) docCorrect++;
            
            // Classify failure
            if (!docHit) {
                if (!tx.stage1Success) timeout++;
                else if (!domainHit) domainWrong++;
                else docWrong++;
            }
            
            if (tx.totalMs > 0) {
                latencies.push_back(tx.totalMs);
            }
            totalProbes += tx.probesUsed;
        }
    }
    
    Simulator::Destroy();
    
    // Output results
    double domAcc = totalQueries > 0 ? 100.0 * domainCorrect / totalQueries : 0;
    double docAcc = totalQueries > 0 ? 100.0 * docCorrect / totalQueries : 0;
    double avgProbes = totalQueries > 0 ? totalProbes / totalQueries : 0;
    
    double p50 = 0, p95 = 0;
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        p50 = latencies[latencies.size() / 2];
        p95 = latencies[(size_t)(latencies.size() * 0.95)];
    }
    
    NS_LOG_UNCOND("\n=== Results ===");
    NS_LOG_UNCOND("DomAcc=" << domAcc << "%, DocAcc=" << docAcc << "%");
    NS_LOG_UNCOND("AvgProbes=" << avgProbes << ", p50=" << p50 << "ms, p95=" << p95 << "ms");
    NS_LOG_UNCOND("Failures: DOMAIN_WRONG=" << domainWrong << ", DOC_WRONG=" << docWrong << ", TIMEOUT=" << timeout);
    
    // Append to CSV
    std::string csvFile = g_resultDir + "/exp2.2_sweep.csv";
    bool fileExists = std::filesystem::exists(csvFile);
    std::ofstream ofs(csvFile, std::ios::app);
    if (!fileExists) {
        ofs << "domains,M,kMax,tau,queries,domain_acc,doc_acc,avg_probes,p50_ms,p95_ms,domain_wrong,doc_wrong,timeout\n";
    }
    ofs << g_domains << "," << g_M << "," << g_kMax << "," << std::fixed << std::setprecision(2) << g_tau << ","
        << totalQueries << "," << std::fixed << std::setprecision(2) << domAcc << ","
        << std::fixed << std::setprecision(2) << docAcc << ","
        << std::fixed << std::setprecision(2) << avgProbes << ","
        << std::fixed << std::setprecision(2) << p50 << ","
        << std::fixed << std::setprecision(2) << p95 << ","
        << domainWrong << "," << docWrong << "," << timeout << "\n";
    
    NS_LOG_UNCOND("Appended to " << csvFile);
    
    return 0;
}
