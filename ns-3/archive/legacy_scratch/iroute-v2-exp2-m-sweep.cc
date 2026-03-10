/**
 * @file iroute-v2-exp2-m-sweep.cc
 * @brief Exp2: M tradeoff using REAL protocol (one M value per run)
 *
 * Measures accuracy, success rate, latency, overhead as function of M.
 * Uses SynthCorpus for controlled workload and 3-Tier topology.
 *
 * Usage:
 *   for M in 1 2 4 8 16 32; do
 *     ./waf --run "iroute-v2-exp2-m-sweep --M=$M --domains=20 --queries=200"
 *   done
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

// New Utils
#include "utils/iroute-synth-corpus.hpp"
#include "utils/iroute-topology-builder.hpp"

#include <random>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <queue>
#include <map>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp2MSweep");

// Global parameters
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 60.0;
static uint32_t g_vectorDim = 64;
static uint32_t g_domains = 20;
static uint32_t g_M = 4;
static uint32_t g_queries = 200;
static uint32_t g_kMax = 5;
static double g_tau = 0.35;
static uint32_t g_fetchTimeoutMs = 4000;
static uint32_t g_csSize = 5000;
static std::string g_resultDir = "results/exp2";

// SynthCorpus Params
static uint32_t g_topicsPerDomain = 8;
static uint32_t g_globalTopics = 32;
static uint32_t g_objectsPerTopic = 50; // New
static double g_overlapRatio = 0.5;
static double g_objSigma = 0.15;
static double g_querySigma = 0.08;
static double g_zipfSkew = 0.8;

// Topology Params
static uint32_t g_coreN = 4;
static uint32_t g_edgeN = 4;

double Percentile(std::vector<double>& data, double p) {
    if (data.empty()) return 0.0;
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    return data[idx];
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
    cmd.AddValue("csSize", "Content Store size", g_csSize);
    cmd.AddValue("resultDir", "Results directory", g_resultDir);
    
    cmd.AddValue("topicsPerDomain", "Synth T", g_topicsPerDomain);
    cmd.AddValue("globalTopics", "Synth G", g_globalTopics);
    cmd.AddValue("overlapRatio", "Overlap", g_overlapRatio);
    cmd.AddValue("zipfSkew", "Zipf skew", g_zipfSkew);
    
    cmd.AddValue("coreN", "Topo Core N", g_coreN);
    cmd.AddValue("edgeN", "Topo Edge N", g_edgeN);

    // Synth
    cmd.AddValue("objectsPerTopic", "Synth Opt", g_objectsPerTopic); // Note: Need to verify if g_objectsPerTopic exists as static var
    cmd.AddValue("objSigma", "Synth Obj Sigma", g_objSigma);
    cmd.AddValue("querySigma", "Synth Query Sigma", g_querySigma);
    
    cmd.Parse(argc, argv);
    
    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);
    
    NS_LOG_UNCOND("=== iRoute v2 Exp2: M Sweep (PR-A Upgrade) ===");
    NS_LOG_UNCOND("M=" << g_M << ", Domains=" << g_domains << ", Overlap=" << g_overlapRatio);
    
    // 1. Build Topology
    iroute::TopoResult topo = iroute::BuildThreeTier(
        g_coreN, g_edgeN, g_domains,
        "1Gbps", "5ms", "2ms", "10ms", g_seed + g_run
    );
    
    // 2. Install Stack
    StackHelper ndnHelper;
    ndnHelper.setCsSize(g_csSize);
    ndnHelper.InstallAll();
    
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    
    // 3. Prepare Workload (SynthCorpus)
    iroute::SynthCorpus::Config corpCfg;
    corpCfg.seed = g_seed + g_run;
    corpCfg.vectorDim = g_vectorDim;
    corpCfg.domains = g_domains;
    corpCfg.topicsPerDomain = g_topicsPerDomain;
    corpCfg.globalTopics = g_globalTopics;
    corpCfg.objectsPerTopic = g_objectsPerTopic; // Pass to config
    corpCfg.overlapRatio = g_overlapRatio;
    // ... defaults for sigmas
    corpCfg.objSigma = g_objSigma;
    corpCfg.querySigma = g_querySigma;
    
    iroute::SynthCorpus corpus(corpCfg);
    corpus.BuildBaseObjects();
    
    // 4. Install Producer Logic on Domains
    //    We need to enable the domain nodes to answer
    //    a) Discovery Interests: /<DomainID>/iroute/disc/...
    //    b) Data Fetch: /<DomainID>/data/t<Topic>/o<ObjID>
    
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    
    for (uint32_t d = 0; d < g_domains; ++d) {
        Ptr<Node> node = topo.domainNodes[d];
        std::string domainPrefix = "/domain" + std::to_string(d);
        
        // A. iRoute App (Control Plane Answerer)
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        irouteHelper.SetAttribute("RouterId", StringValue(domainPrefix));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        auto apps = irouteHelper.Install(node);
        
        // Export Centroids from Corpus
        auto centroids = corpus.ExportCentroids(d, g_M);
        
        // Convert to IRouteApp format (vector<CentroidEntry>)
        std::vector<iroute::CentroidEntry> appCentroids;
        for (const auto& c : centroids) {
            iroute::CentroidEntry e;
            e.centroidId = c.centroidId;
            e.C = c.C;
            e.radius = c.radius;
            e.weight = c.weight;
            appCentroids.push_back(e);
        }
        
        auto app = DynamicCast<IRouteApp>(apps.Get(0));
        app->SetLocalCentroids(appCentroids);
        
        // B. Producer (Data Plane)
        // Install SemanticProducer to serve /domainX/data
        AppHelper producerHelper("ns3::ndn::SemanticProducer");
        producerHelper.SetAttribute("Prefix", StringValue(domainPrefix + "/data"));
        producerHelper.Install(node);
        
        // C. Global Routing (Advertise prefixes)
        grHelper.AddOrigins(domainPrefix, node);          // For Discovery
        grHelper.AddOrigins(domainPrefix + "/data", node); // For Fetch
    }
    
    GlobalRoutingHelper::CalculateRoutes();
    
    // 5. Install Consumer on Ingress
    if (!topo.ingress) {
        NS_LOG_ERROR("No ingress node!");
        return 1;
    }
    
    // Setup Ingress IRouteApp (RouteManager)
    AppHelper ingressHelper("ns3::ndn::IRouteApp");
    ingressHelper.SetAttribute("RouterId", StringValue("ingress"));
    ingressHelper.SetAttribute("IsIngress", BooleanValue(true));
    ingressHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    ingressHelper.Install(topo.ingress);
    
    // Populate Ingress RIB (Bootstrap from Corpus)
    // In real system this is flooded LSA. Here we pre-fill for Exp 2.
    // We export centroids for ALL domains and put them in Ingress RM.
    auto registry = iroute::RouteManagerRegistry::getOrCreate(topo.ingress->GetId(), g_vectorDim);
    registry->setActiveSemVerId(1);
    
    for (uint32_t d = 0; d < g_domains; ++d) {
        auto centroids = corpus.ExportCentroids(d, g_M); // Re-export just to get data
        std::vector<iroute::CentroidEntry> appCentroids;
        for (const auto& c : centroids) {
            iroute::CentroidEntry e;
            e.centroidId = c.centroidId;
            e.C = c.C;
            e.radius = c.radius;
            e.weight = c.weight;
            appCentroids.push_back(e);
        }
        
        iroute::DomainEntry de;
        de.domainId = Name("/domain" + std::to_string(d));
        de.centroids = appCentroids;
        de.semVerId = 1;
        de.seqNo = 1;
        de.cost = 3.0; // Assume uniform cost or 3-hop typical
        registry->updateDomain(de);
    }
    
    // Install Consumer App
    AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
    consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
    consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
    consumerHelper.SetAttribute("Frequency", DoubleValue(10.0)); // 10 q/s
    auto cApps = consumerHelper.Install(topo.ingress);
    
    // 6. Generate Query Trace
    auto trace = corpus.MakeQueryTrace(g_queries, true, true, g_zipfSkew);
    
    // Set trace after start
    Ptr<IRouteDiscoveryConsumer> consumer = DynamicCast<IRouteDiscoveryConsumer>(cApps.Get(0));
    Simulator::Schedule(Seconds(1.0), [consumer, trace]() {
        consumer->SetQueryTrace(trace);
    });
    
    // 7. Run
    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();
    
    // 8. Collect Stats
    mkdir(g_resultDir.c_str(), 0755);
    const auto& txs = consumer->GetTransactions();
    
    uint32_t correct = 0;
    uint32_t success = 0;
    
    for (const auto& tx : txs) {
        if (tx.stage2Success) success++;
        // Check accuracy: selectedDomain should match expectedDomain
        // Format of expected: /domainX
        // Format of selected: /domainX
        if (tx.selectedDomain == tx.expectedDomain) correct++;
    }
    
    double acc = txs.empty() ? 0 : (double)correct / txs.size();
    double succRate = txs.empty() ? 0 : (double)success / txs.size();
    
    // Append Summary
    std::string summaryFile = g_resultDir + "/exp2_summary.csv";
    std::ofstream os(summaryFile, std::ios::app);
    // Columns: M, domains, accuracy, success, ...
    os << g_M << "," << g_domains << "," << acc << "," << succRate << "," << g_overlapRatio << "\n";
    os.close();
    
    NS_LOG_UNCOND("Exp2 Done. Accuracy=" << acc << ", Success=" << succRate);
    
    Simulator::Destroy();
    return 0;
}
