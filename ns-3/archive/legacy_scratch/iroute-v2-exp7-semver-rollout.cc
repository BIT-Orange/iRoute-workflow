/**
 * @file iroute-v2-exp7-semver-rollout.cc
 * @brief Exp7: SemVer rollout with dynamic upgrade (Rocketfuel + real dataset)
 *
 * Phases:
 *  - t0: all domains on v1 (stable)
 *  - t1: subset upgrades to v2 and withdraws v1 LSA (ingress still v1)
 *  - t2: ingress switches to v2 with fallback to v1
 *
 * Outputs time series: success_rate, fallback_rate, avg_probes, control_bytes
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/semantic-producer.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

#include "iroute-common-utils.hpp"

#include <random>
#include <fstream>
#include <sstream>
#include <queue>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sys/stat.h>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp7SemVerRollout");

// =============================================================================
// Global parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 60.0;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_M = 4;
static uint32_t g_queries = 200;
static uint32_t g_kMax = 5;
static double g_tau = 0.3;
static uint32_t g_fetchTimeoutMs = 4000;
static double g_frequency = 2.0;

static double g_rolloutTime = 15.0;       // t1
static double g_ingressUpdateTime = 30.0; // t2
static double g_windowSec = 2.0;
static double g_upgradeRatio = 0.5;

static double g_lsaPeriod = 2.0;
static double g_lsaFetchInterval = 1.0;

static std::string g_resultDir = "results/exp7";

// Rocketfuel subgraph
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_topoSize = 150;
static uint32_t g_ingressNodeId = 0;
static double g_linkDelayMs = 2.0;
static std::string g_linkDataRate = "1Gbps";

// Data files
static std::string g_centroidsFile = "dataset/trec_dl_combined_dim128/domain_centroids.csv";
static std::string g_contentFile = "dataset/trec_dl_combined_dim128/producer_content.csv";
static std::string g_traceFile = "dataset/trec_dl_combined_dim128/consumer_trace.csv";

// =============================================================================
// Helpers (Rocketfuel subgraph builder, same as Exp4/6)
// =============================================================================

std::map<uint32_t, std::vector<uint32_t>>
ParseRocketfuelAdjacency(const std::string& topoFile)
{
    std::map<uint32_t, std::vector<uint32_t>> adj;
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

uint32_t PickHighestDegreeNode(const std::map<uint32_t, std::vector<uint32_t>>& adj)
{
    uint32_t bestId = 0;
    size_t bestDeg = 0;
    for (const auto& kv : adj) {
        if (kv.second.size() > bestDeg) {
            bestDeg = kv.second.size();
            bestId = kv.first;
        }
    }
    return bestId;
}

std::vector<uint32_t>
SelectConnectedSubgraph(const std::map<uint32_t, std::vector<uint32_t>>& adj,
                        uint32_t targetSize,
                        uint32_t seed)
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

struct TopoBuildResult {
    NodeContainer nodes;
    Ptr<Node> ingress;
    std::vector<Ptr<Node>> domainNodes;
    std::vector<uint32_t> domainNodeIds;
    std::map<uint32_t, uint32_t> nodeIdToDomain;
    uint32_t numLinks = 0;
    double avgLinksPerRouter = 0.0;
};

TopoBuildResult BuildRocketfuelSubgraph(const std::string& topoFile,
                                       uint32_t targetSize,
                                       uint32_t domains,
                                       uint32_t ingressNodeId,
                                       double linkDelayMs,
                                       const std::string& dataRate)
{
    TopoBuildResult result;

    auto adj = ParseRocketfuelAdjacency(topoFile);
    if (adj.empty()) {
        NS_FATAL_ERROR("Empty topology: " << topoFile);
    }

    uint32_t seed = ingressNodeId;
    if (seed == 0 || !adj.count(seed)) {
        seed = PickHighestDegreeNode(adj);
    }

    auto selected = SelectConnectedSubgraph(adj, targetSize, seed);
    std::unordered_set<uint32_t> selectedSet(selected.begin(), selected.end());

    result.nodes.Create(selected.size());

    std::unordered_map<uint32_t, Ptr<Node>> idToNode;
    for (size_t i = 0; i < selected.size(); ++i) {
        idToNode[selected[i]] = result.nodes.Get(i);
    }

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(dataRate));
    p2p.SetChannelAttribute("Delay", StringValue(std::to_string(linkDelayMs) + "ms"));

    result.numLinks = 0;
    std::map<uint32_t, uint32_t> degree;

    for (uint32_t u : selected) {
        for (uint32_t v : adj[u]) {
            if (selectedSet.count(v) && u < v) {
                p2p.Install(idToNode[u], idToNode[v]);
                result.numLinks++;
                degree[u]++;
                degree[v]++;
            }
        }
    }

    if (!selected.empty()) {
        result.avgLinksPerRouter = (2.0 * result.numLinks) / selected.size();
    }

    uint32_t ingressOldId = seed;
    if (ingressNodeId != 0 && selectedSet.count(ingressNodeId)) {
        ingressOldId = ingressNodeId;
    } else {
        uint32_t bestId = selected.front();
        uint32_t bestDeg = UINT32_MAX;
        for (uint32_t u : selected) {
            uint32_t deg = degree.count(u) ? degree[u] : 0;
            if (deg < bestDeg) {
                bestDeg = deg;
                bestId = u;
            }
        }
        ingressOldId = bestId;
    }
    result.ingress = idToNode[ingressOldId];

    auto gatewayNodeIds = iroute::utils::SelectGatewayNodes(result.nodes, domains, result.ingress->GetId());

    for (uint32_t i = 0; i < gatewayNodeIds.size(); ++i) {
        for (uint32_t n = 0; n < result.nodes.GetN(); ++n) {
            if (result.nodes.Get(n)->GetId() == gatewayNodeIds[i]) {
                result.domainNodes.push_back(result.nodes.Get(n));
                result.domainNodeIds.push_back(gatewayNodeIds[i]);
                result.nodeIdToDomain[gatewayNodeIds[i]] = static_cast<uint32_t>(i);
                break;
            }
        }
    }

    return result;
}

// =============================================================================
// Rollout state
// =============================================================================

struct DomainCtx {
    Ptr<IRouteApp> app;
    std::vector<iroute::CentroidEntry> centroids;
};

static std::vector<DomainCtx> g_domainCtx;
static std::vector<uint32_t> g_upgradeDomains;
static std::vector<Ptr<IRouteApp>> g_allApps;
static Ptr<IRouteApp> g_ingressApp;
static Ptr<IRouteDiscoveryConsumer> g_consumer;

struct LsaSample {
    double time = 0.0;
    uint64_t txBytes = 0;
};
static std::vector<LsaSample> g_lsaSamples;

void SampleLsaCounters()
{
    uint64_t bytes = 0;
    for (const auto& app : g_allApps) {
        if (app) bytes += app->GetLsaTxBytesTotal();
    }
    g_lsaSamples.push_back({Simulator::Now().GetSeconds(), bytes});

    if (Simulator::Now().GetSeconds() + g_windowSec <= g_simTime + 1e-9) {
        Simulator::Schedule(Seconds(g_windowSec), &SampleLsaCounters);
    }
}

void DoRollout()
{
    NS_LOG_UNCOND("[Exp7] t1 rollout: upgrading " << g_upgradeDomains.size() << " domains to v2");
    for (uint32_t d : g_upgradeDomains) {
        if (d >= g_domainCtx.size()) continue;
        auto& ctx = g_domainCtx[d];
        if (!ctx.app) continue;

        // Withdraw v1 (empty centroids)
        ctx.app->SetLocalCentroids({});
        ctx.app->TriggerLsaPublish();

        // Switch to v2 and publish
        ctx.app->SetSemVerId(2);
        ctx.app->SetLocalCentroids(ctx.centroids);
        ctx.app->TriggerLsaPublish();
    }
}

void UpdateIngressToV2()
{
    NS_LOG_UNCOND("[Exp7] t2 ingress update: active=v2 prev=v1");
    auto rm = iroute::RouteManagerRegistry::getOrCreate(g_ingressApp->GetNode()->GetId(), g_vectorDim);
    if (rm) {
        rm->setActiveSemVerId(2);
        rm->setPrevSemVerId(1);
    }
    if (g_ingressApp) {
        g_ingressApp->SetSemVerId(2);
    }
    if (g_consumer) {
        g_consumer->SetAttribute("SemVerId", UintegerValue(2));
    }
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[])
{
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("run", "Run number", g_run);
    cmd.AddValue("simTime", "Simulation time", g_simTime);
    cmd.AddValue("vectorDim", "Vector dimension", g_vectorDim);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("M", "Centroids per domain", g_M);
    cmd.AddValue("queries", "Number of queries", g_queries);
    cmd.AddValue("kMax", "Max probes", g_kMax);
    cmd.AddValue("tau", "Score threshold tau", g_tau);
    cmd.AddValue("fetchTimeoutMs", "Fetch timeout ms", g_fetchTimeoutMs);
    cmd.AddValue("frequency", "Query frequency", g_frequency);
    cmd.AddValue("rolloutTime", "t1 rollout time", g_rolloutTime);
    cmd.AddValue("ingressUpdateTime", "t2 ingress update time", g_ingressUpdateTime);
    cmd.AddValue("windowSec", "Time series window (sec)", g_windowSec);
    cmd.AddValue("upgradeRatio", "Fraction of domains upgrading at t1", g_upgradeRatio);
    cmd.AddValue("lsaPeriod", "LSA period (sec)", g_lsaPeriod);
    cmd.AddValue("lsaFetchInterval", "LSA fetch interval (sec)", g_lsaFetchInterval);
    cmd.AddValue("resultDir", "Output directory", g_resultDir);
    cmd.AddValue("topoFile", "Rocketfuel topo file", g_topoFile);
    cmd.AddValue("topoSize", "Rocketfuel subgraph size", g_topoSize);
    cmd.AddValue("ingressNodeId", "Ingress node id (0=auto)", g_ingressNodeId);
    cmd.AddValue("linkDelayMs", "Link delay (ms)", g_linkDelayMs);
    cmd.AddValue("linkDataRate", "Link data rate", g_linkDataRate);
    cmd.AddValue("centroidsFile", "Centroids CSV", g_centroidsFile);
    cmd.AddValue("contentFile", "Content CSV", g_contentFile);
    cmd.AddValue("traceFile", "Trace CSV", g_traceFile);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);
    std::mt19937 rng(g_seed + g_run);

    auto topo = BuildRocketfuelSubgraph(g_topoFile, g_topoSize, g_domains,
                                        g_ingressNodeId, g_linkDelayMs, g_linkDataRate);

    if (topo.domainNodes.size() < g_domains) {
        NS_LOG_WARN("Requested domains=" << g_domains << " but only selected " << topo.domainNodes.size());
        g_domains = topo.domainNodes.size();
    }

    NS_LOG_UNCOND("=== Exp7: SemVer Rollout (Rocketfuel) ===");
    NS_LOG_UNCOND("nodes=" << topo.nodes.GetN() << ", links=" << topo.numLinks
                  << ", domains=" << g_domains << ", M=" << g_M
                  << ", vectorDim=" << g_vectorDim);

    // Install stack
    StackHelper ndnHelper;
    ndnHelper.InstallAll();
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    StrategyChoiceHelper::InstallAll("/ndn/broadcast", "/localhost/nfd/strategy/multicast");

    iroute::RouteManagerRegistry::clear();
    IRouteApp::ResetLsaCounter();

    // Load real data
    auto centroidsMap = iroute::utils::LoadCentroidsFromCsv(g_centroidsFile);
    auto contentMap = iroute::utils::LoadContentFromCsv(g_contentFile);

    // Compute hop distances from ingress
    auto hopMap = iroute::utils::BFSAllDistances(topo.ingress);

    // Ingress app
    AppHelper ingressHelper("ns3::ndn::IRouteApp");
    ingressHelper.SetAttribute("RouterId", StringValue("ingress"));
    ingressHelper.SetAttribute("IsIngress", BooleanValue(true));
    ingressHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    ingressHelper.SetAttribute("SemVerId", UintegerValue(1));
    ingressHelper.SetAttribute("LsaInterval", TimeValue(Seconds(g_lsaPeriod)));
    ingressHelper.SetAttribute("LsaFetchInterval", TimeValue(Seconds(g_lsaFetchInterval)));
    ingressHelper.SetAttribute("EnableLsaPolling", BooleanValue(true));

    auto ingressApps = ingressHelper.Install(topo.ingress);
    g_ingressApp = DynamicCast<IRouteApp>(ingressApps.Get(0));
    g_allApps.push_back(g_ingressApp);

    auto registry = iroute::RouteManagerRegistry::getOrCreate(topo.ingress->GetId(), g_vectorDim);
    if (registry) {
        registry->setActiveSemVerId(1);
        registry->setPrevSemVerId(0);
    }

    // Domain apps
    g_domainCtx.resize(g_domains);

    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("SemVerId", UintegerValue(1));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(g_lsaPeriod)));
    irouteHelper.SetAttribute("LsaFetchInterval", TimeValue(Seconds(g_lsaFetchInterval)));
    irouteHelper.SetAttribute("EnableLsaPolling", BooleanValue(true));

    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();

    for (uint32_t d = 0; d < g_domains; ++d) {
        Ptr<Node> node = topo.domainNodes[d];
        std::string domainPrefix = "/domain" + std::to_string(d);

        irouteHelper.SetAttribute("RouterId", StringValue(domainPrefix));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));

        double cost = 1.0;
        auto itHop = hopMap.find(node->GetId());
        if (itHop != hopMap.end()) cost = static_cast<double>(itHop->second);
        irouteHelper.SetAttribute("RouteCost", DoubleValue(cost));

        auto apps = irouteHelper.Install(node);
        auto app = DynamicCast<IRouteApp>(apps.Get(0));
        g_domainCtx[d].app = app;
        g_allApps.push_back(app);

        // Centroids
        std::vector<iroute::CentroidEntry> centroids;
        if (centroidsMap.count(d)) {
            centroids = centroidsMap[d];
        }
        if (centroids.empty()) {
            NS_LOG_WARN("No centroids for domain " << d << ", using empty list");
        }
        g_domainCtx[d].centroids = centroids;
        if (app) app->SetLocalCentroids(centroids);

        // Local content for Stage-1
        if (contentMap.count(d) && app) {
            std::vector<IRouteApp::ContentEntry> local;
            for (const auto& doc : contentMap[d]) {
                IRouteApp::ContentEntry e;
                e.docId = doc.docId;
                e.canonicalName = doc.canonicalName;
                e.vector = iroute::SemanticVector(doc.vector);
                e.vector.normalize();
                e.isDistractor = doc.isDistractor;
                local.push_back(e);
            }
            app->SetLocalContent(local);
        }

        // Producer
        AppHelper producerHelper("ns3::ndn::SemanticProducer");
        producerHelper.SetAttribute("Prefix", StringValue(domainPrefix + "/data"));
        producerHelper.Install(node);

        grHelper.AddOrigins(domainPrefix, node);
        grHelper.AddOrigins(domainPrefix + "/data", node);
    }

    GlobalRoutingHelper::CalculateRoutes();

    // Configure known domains for LSA polling
    std::vector<Name> knownDomains;
    for (uint32_t d = 0; d < g_domains; ++d) {
        knownDomains.emplace_back("/domain" + std::to_string(d));
    }
    for (auto& app : g_allApps) {
        if (app) app->SetKnownDomains(knownDomains);
    }

    // Consumer
    AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
    consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
    consumerHelper.SetAttribute("SemVerId", UintegerValue(1));
    consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
    consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
    consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
    auto cApps = consumerHelper.Install(topo.ingress);
    g_consumer = DynamicCast<IRouteDiscoveryConsumer>(cApps.Get(0));

    std::vector<IRouteDiscoveryConsumer::QueryItem> queryTrace;
    if (!g_traceFile.empty()) {
        queryTrace = iroute::utils::LoadTraceFromCsv(g_traceFile);
    }

    // Pick upgrade subset
    std::vector<uint32_t> indices(g_domains);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);
    uint32_t upgradeCount = static_cast<uint32_t>(std::round(g_domains * g_upgradeRatio));
    upgradeCount = std::min<uint32_t>(upgradeCount, g_domains);
    upgradeCount = std::max<uint32_t>(upgradeCount, 1);
    g_upgradeDomains.assign(indices.begin(), indices.begin() + upgradeCount);

    if (!queryTrace.empty()) {
        if (g_queries > 0 && g_queries < queryTrace.size()) {
            queryTrace.resize(g_queries);
        }
        if (g_queries > 0 && queryTrace.size() < g_queries && !queryTrace.empty()) {
            size_t i = 0;
            while (queryTrace.size() < g_queries) {
                queryTrace.push_back(queryTrace[i % queryTrace.size()]);
                ++i;
            }
        }
        if (g_consumer) g_consumer->SetQueryTrace(queryTrace);
    }

    // Schedule rollout events and sampling
    Simulator::Schedule(Seconds(g_rolloutTime), &DoRollout);
    Simulator::Schedule(Seconds(g_ingressUpdateTime), &UpdateIngressToV2);
    Simulator::Schedule(Seconds(0.0), &SampleLsaCounters);

    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();

    // Results
    mkdir(g_resultDir.c_str(), 0755);
    if (g_consumer) {
        g_consumer->ExportToCsv(g_resultDir + "/exp7_trace.csv");
    }

    const auto& txs = g_consumer->GetTransactions();
    uint32_t success = 0;
    uint32_t fallback = 0;
    double totalMsSum = 0.0;
    double stage1MsSum = 0.0;
    uint64_t probesSum = 0;

    for (const auto& tx : txs) {
        bool correct = tx.stage2Success && (tx.expectedDomain == tx.finalSuccessDomain);
        if (correct) success++;
        if (tx.fallbackUsed) fallback++;
        if (tx.totalMs > 0) totalMsSum += tx.totalMs;
        if (tx.stage1RttMs > 0) stage1MsSum += tx.stage1RttMs;
        probesSum += tx.probesUsed;
    }

    size_t n = std::max<size_t>(1, txs.size());
    double successRate = static_cast<double>(success) / n;
    double fallbackRate = static_cast<double>(fallback) / n;
    double avgTotalMs = totalMsSum / n;
    double avgStage1Ms = stage1MsSum / n;
    double avgProbes = static_cast<double>(probesSum) / n;

    std::ofstream sf(g_resultDir + "/exp7_summary.csv");
    sf << "successRate,fallbackRate,avgTotalMs,avgStage1Ms,avgProbes\n";
    sf << successRate << "," << fallbackRate << "," << avgTotalMs << ","
       << avgStage1Ms << "," << avgProbes << "\n";
    sf.close();

    // Time-series
    std::ofstream tf(g_resultDir + "/exp7_timeseries.csv");
    tf << "timeStart,timeEnd,queries,success_rate,fallback_rate,avg_probes,control_bytes\n";

    for (size_t i = 1; i < g_lsaSamples.size(); ++i) {
        double t0 = g_lsaSamples[i - 1].time;
        double t1 = g_lsaSamples[i].time;
        uint64_t bytes = g_lsaSamples[i].txBytes - g_lsaSamples[i - 1].txBytes;

        uint32_t count = 0;
        uint32_t succ = 0;
        uint32_t fb = 0;
        uint64_t probes = 0;

        for (const auto& tx : txs) {
            if (tx.startTime >= t0 && tx.startTime < t1) {
                count++;
                bool correct = tx.stage2Success && (tx.expectedDomain == tx.finalSuccessDomain);
                if (correct) succ++;
                if (tx.fallbackUsed) fb++;
                probes += tx.probesUsed;
            }
        }

        double sr = (count > 0) ? static_cast<double>(succ) / count : 0.0;
        double fr = (count > 0) ? static_cast<double>(fb) / count : 0.0;
        double ap = (count > 0) ? static_cast<double>(probes) / count : 0.0;

        tf << t0 << "," << t1 << "," << count << "," << sr << "," << fr
           << "," << ap << "," << bytes << "\n";
    }
    tf.close();

    NS_LOG_UNCOND("Exp7 Done. Success=" << successRate << ", FallbackRate=" << fallbackRate);

    Simulator::Destroy();
    return 0;
}
