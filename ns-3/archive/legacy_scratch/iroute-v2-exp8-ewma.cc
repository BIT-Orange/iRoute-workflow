/**
 * @file iroute-v2-exp8-ewma.cc
 * @brief Exp8: EWMA suppression under malicious discovery replies (Rocketfuel + real dataset)
 *
 * Attack model:
 * - Malicious domain advertises centroids stolen from a victim domain.
 * - Stage-1 returns high confidence but Stage-2 fetch fails during attack window.
 * - After attack ends, malicious becomes honest and can recover under EWMA.
 *
 * Outputs:
 * - exp8_trace.csv: per-query trace (from consumer)
 * - exp8_timeseries.csv: windowed malicious-first rate, success rate, false suppression rate
 * - exp8_summary.csv: time-to-suppress, false_suppression_rate, recovery_time
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

NS_LOG_COMPONENT_DEFINE("iRouteExp8EWMA");

// =============================================================================
// Global parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 60.0;
static uint32_t g_domains = 8;
static uint32_t g_vectorDim = 128;
static uint32_t g_M = 4;
static uint32_t g_queries = 200;
static uint32_t g_kMax = 1;
static double g_tau = 0.3;
static uint32_t g_fetchTimeoutMs = 4000;
static double g_frequency = 5.0;
static bool g_enableEwma = true;
static uint32_t g_maliciousDomain = 0;
static uint32_t g_victimDomain = 1;
static double g_attackStart = 0.0;
static double g_attackEnd = 30.0;
static double g_windowSec = 2.0;
static double g_suppressThreshold = 0.1;
static double g_recoverThreshold = 0.5;
static double g_maliciousWeightBoost = 1000.0;
static double g_maliciousRadiusBoost = 2.0;
static double g_exploreRate = 0.1;
static std::string g_resultDir = "results/exp8";

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
// Helpers (Rocketfuel subgraph builder)
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
// Global state
// =============================================================================

static Ptr<SemanticProducer> g_maliciousProducer;

void StartAttack()
{
    if (g_maliciousProducer) {
        g_maliciousProducer->SetActive(false);
        NS_LOG_UNCOND("[Exp8] Attack ON at t=" << Simulator::Now().GetSeconds());
    }
}

void StopAttack()
{
    if (g_maliciousProducer) {
        g_maliciousProducer->SetActive(true);
        NS_LOG_UNCOND("[Exp8] Attack OFF at t=" << Simulator::Now().GetSeconds());
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
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("vectorDim", "Vector dimension", g_vectorDim);
    cmd.AddValue("M", "Centroids per domain", g_M);
    cmd.AddValue("queries", "Number of queries", g_queries);
    cmd.AddValue("kMax", "Max probes", g_kMax);
    cmd.AddValue("tau", "Score threshold tau", g_tau);
    cmd.AddValue("fetchTimeoutMs", "Fetch timeout ms", g_fetchTimeoutMs);
    cmd.AddValue("frequency", "Query frequency", g_frequency);
    cmd.AddValue("enableEwma", "Enable EWMA penalty", g_enableEwma);
    cmd.AddValue("maliciousDomain", "Malicious domain id", g_maliciousDomain);
    cmd.AddValue("victimDomain", "Victim domain id", g_victimDomain);
    cmd.AddValue("attackStart", "Attack start time", g_attackStart);
    cmd.AddValue("attackEnd", "Attack end time", g_attackEnd);
    cmd.AddValue("windowSec", "Time series window (sec)", g_windowSec);
    cmd.AddValue("suppressThreshold", "Suppression threshold", g_suppressThreshold);
    cmd.AddValue("recoverThreshold", "Recovery threshold", g_recoverThreshold);
    cmd.AddValue("maliciousWeightBoost", "Weight multiplier for malicious centroids", g_maliciousWeightBoost);
    cmd.AddValue("maliciousRadiusBoost", "Min radius for malicious centroids", g_maliciousRadiusBoost);
    cmd.AddValue("exploreRate", "Exploration probability (adds one extra probe)", g_exploreRate);
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

    auto topo = BuildRocketfuelSubgraph(g_topoFile, g_topoSize, g_domains,
                                        g_ingressNodeId, g_linkDelayMs, g_linkDataRate);

    if (topo.domainNodes.size() < g_domains) {
        NS_LOG_WARN("Requested domains=" << g_domains << " but only selected " << topo.domainNodes.size());
        g_domains = topo.domainNodes.size();
    }

    if (g_domains == 0) {
        NS_LOG_UNCOND("No domains available, exiting.");
        return 0;
    }
    if (g_maliciousDomain >= g_domains) g_maliciousDomain = 0;
    if (g_victimDomain >= g_domains || g_victimDomain == g_maliciousDomain) {
        g_victimDomain = (g_maliciousDomain + 1) % g_domains;
    }

    NS_LOG_UNCOND("=== Exp8: EWMA Suppression (Rocketfuel) ===");
    NS_LOG_UNCOND("nodes=" << topo.nodes.GetN() << ", links=" << topo.numLinks
                  << ", domains=" << g_domains << ", M=" << g_M
                  << ", vectorDim=" << g_vectorDim);

    StackHelper ndnHelper;
    ndnHelper.setCsSize(0);
    ndnHelper.InstallAll();
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");

    iroute::RouteManagerRegistry::clear();

    // Load real data
    auto centroidsMap = iroute::utils::LoadCentroidsFromCsv(g_centroidsFile);
    auto contentMap = iroute::utils::LoadContentFromCsv(g_contentFile);

    // Compute hop distances from ingress
    auto hopMap = iroute::utils::BFSAllDistances(topo.ingress);

    // Ingress app (for consistency)
    AppHelper ingressHelper("ns3::ndn::IRouteApp");
    ingressHelper.SetAttribute("RouterId", StringValue("ingress"));
    ingressHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    ingressHelper.SetAttribute("IsIngress", BooleanValue(true));
    ingressHelper.Install(topo.ingress);

    auto registry = iroute::RouteManagerRegistry::getOrCreate(topo.ingress->GetId(), g_vectorDim);
    if (registry) {
        registry->setActiveSemVerId(1);
        registry->setPrevSemVerId(0);
        registry->SetEnableEwmaPenalty(g_enableEwma);
    }

    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();

    // Victim centroids/content (used to poison malicious)
    std::vector<iroute::CentroidEntry> victimCentroids;
    if (centroidsMap.count(g_victimDomain)) victimCentroids = centroidsMap[g_victimDomain];
    std::vector<iroute::utils::ContentEntry> victimContent;
    if (contentMap.count(g_victimDomain)) victimContent = contentMap[g_victimDomain];

    for (uint32_t d = 0; d < g_domains; ++d) {
        Ptr<Node> node = topo.domainNodes[d];
        std::string domainPrefix = "/domain" + std::to_string(d);

        // IRouteApp for discovery replies
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        irouteHelper.SetAttribute("RouterId", StringValue(domainPrefix));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        auto apps = irouteHelper.Install(node);
        auto app = DynamicCast<IRouteApp>(apps.Get(0));

        // Centroids
        std::vector<iroute::CentroidEntry> centroids;
        if (d == g_maliciousDomain && !victimCentroids.empty()) {
            centroids = victimCentroids; // steal victim centroids
            for (auto& c : centroids) {
                c.weight *= g_maliciousWeightBoost;
                if (c.radius < g_maliciousRadiusBoost) {
                    c.radius = g_maliciousRadiusBoost;
                }
            }
        } else if (centroidsMap.count(d)) {
            centroids = centroidsMap[d];
        }
        if (app) app->SetLocalCentroids(centroids);

        // Local content for Stage-1
        if (app) {
            std::vector<IRouteApp::ContentEntry> local;
            if (d == g_maliciousDomain && !victimContent.empty()) {
                for (const auto& doc : victimContent) {
                    IRouteApp::ContentEntry e;
                    e.docId = doc.docId;
                    e.canonicalName = "/domain" + std::to_string(d) + "/data/doc/" + doc.docId;
                    e.vector = iroute::SemanticVector(doc.vector);
                    e.vector.normalize();
                    e.isDistractor = doc.isDistractor;
                    local.push_back(e);
                }
            } else if (contentMap.count(d)) {
                for (const auto& doc : contentMap[d]) {
                    IRouteApp::ContentEntry e;
                    e.docId = doc.docId;
                    e.canonicalName = doc.canonicalName;
                    e.vector = iroute::SemanticVector(doc.vector);
                    e.vector.normalize();
                    e.isDistractor = doc.isDistractor;
                    local.push_back(e);
                }
            }
            app->SetLocalContent(local);
        }

        // Producer
        AppHelper producerHelper("ns3::ndn::SemanticProducer");
        producerHelper.SetAttribute("Prefix", StringValue(domainPrefix + "/data"));
        auto pApps = producerHelper.Install(node);

        if (d == g_maliciousDomain) {
            g_maliciousProducer = DynamicCast<SemanticProducer>(pApps.Get(0));
            if (g_maliciousProducer) {
                bool active = (g_attackStart > 0.0);
                g_maliciousProducer->SetActive(active);
            }
        }

        grHelper.AddOrigins(domainPrefix, node);
        grHelper.AddOrigins(domainPrefix + "/data", node);

        // Update ingress registry directly (bypass LSA for this exp)
        if (registry) {
            iroute::DomainEntry de;
            de.domainId = Name(domainPrefix);
            de.centroids = centroids;
            de.semVerId = 1;
            de.seqNo = 1;
            double cost = 1.0;
            auto itHop = hopMap.find(node->GetId());
            if (itHop != hopMap.end()) cost = static_cast<double>(itHop->second);
            de.cost = cost;
            registry->updateDomain(de);
        }
    }

    GlobalRoutingHelper::CalculateRoutes();

    // Attack schedule
    if (g_attackStart <= 0.0) {
        StartAttack();
    } else {
        Simulator::Schedule(Seconds(g_attackStart), &StartAttack);
    }
    if (g_attackEnd > g_attackStart) {
        Simulator::Schedule(Seconds(g_attackEnd), &StopAttack);
    }

    // Consumer
    AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
    consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
    consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
    consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
    consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
    consumerHelper.SetAttribute("ExploreRate", DoubleValue(g_exploreRate));
    auto cApps = consumerHelper.Install(topo.ingress);
    auto consumer = DynamicCast<IRouteDiscoveryConsumer>(cApps.Get(0));

    // Query trace: filter to victim domain
    std::vector<IRouteDiscoveryConsumer::QueryItem> trace;
    if (!g_traceFile.empty()) {
        trace = iroute::utils::LoadTraceFromCsv(g_traceFile);
    }

    std::string victimPrefix = "/domain" + std::to_string(g_victimDomain);
    std::vector<IRouteDiscoveryConsumer::QueryItem> filtered;
    for (const auto& item : trace) {
        if (item.expectedDomain == victimPrefix) {
            filtered.push_back(item);
            if (filtered.size() >= g_queries) break;
        }
    }

    if (filtered.empty() && !trace.empty()) {
        NS_LOG_WARN("No victim-domain queries found; using unfiltered trace.");
        filtered = trace;
        if (g_queries > 0 && g_queries < filtered.size()) {
            filtered.resize(g_queries);
        }
    }

    if (filtered.size() < g_queries && !filtered.empty()) {
        while (filtered.size() < g_queries) {
            filtered.push_back(filtered[filtered.size() % filtered.size()]);
        }
    }

    if (consumer) consumer->SetQueryTrace(filtered);

    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();

    // Results
    mkdir(g_resultDir.c_str(), 0755);
    if (consumer) {
        consumer->ExportToCsv(g_resultDir + "/exp8_trace.csv");
    }

    const auto& txs = consumer->GetTransactions();

    // Time series (windowed)
    size_t numWindows = static_cast<size_t>(std::ceil(g_simTime / g_windowSec));
    std::vector<uint32_t> winCount(numWindows, 0);
    std::vector<uint32_t> winMalicious(numWindows, 0);
    std::vector<uint32_t> winSuccess(numWindows, 0);
    std::vector<uint32_t> winFalseSupp(numWindows, 0);
    std::vector<uint64_t> winProbes(numWindows, 0);

    std::string maliciousPrefix = "/domain" + std::to_string(g_maliciousDomain);

    for (const auto& tx : txs) {
        size_t idx = static_cast<size_t>(tx.startTime / g_windowSec);
        if (idx >= numWindows) continue;
        winCount[idx]++;
        if (tx.firstChoiceDomain == maliciousPrefix) winMalicious[idx]++;
        if (tx.stage2Success) winSuccess[idx]++;
        if (tx.firstChoiceDomain != tx.expectedDomain && tx.firstChoiceDomain != maliciousPrefix) {
            winFalseSupp[idx]++;
        }
        winProbes[idx] += tx.probesUsed;
    }

    std::ofstream tf(g_resultDir + "/exp8_timeseries.csv");
    tf << "timeStart,timeEnd,queries,malicious_first_rate,success_rate,false_suppression_rate,avg_probes\n";

    std::vector<double> winMaliciousRate(numWindows, 0.0);
    for (size_t i = 0; i < numWindows; ++i) {
        double t0 = i * g_windowSec;
        double t1 = (i + 1) * g_windowSec;
        double mrate = (winCount[i] > 0) ? static_cast<double>(winMalicious[i]) / winCount[i] : 0.0;
        double srate = (winCount[i] > 0) ? static_cast<double>(winSuccess[i]) / winCount[i] : 0.0;
        double frate = (winCount[i] > 0) ? static_cast<double>(winFalseSupp[i]) / winCount[i] : 0.0;
        double avgp = (winCount[i] > 0) ? static_cast<double>(winProbes[i]) / winCount[i] : 0.0;
        winMaliciousRate[i] = mrate;

        tf << t0 << "," << t1 << "," << winCount[i] << ","
           << mrate << "," << srate << "," << frate << "," << avgp << "\n";
    }
    tf.close();

    // Compute time-to-suppress (during attack)
    double timeToSuppress = -1.0;
    double recoveryTime = -1.0;
    double falseSuppRateOverall = 0.0;

    uint64_t totalFalseSupp = 0;
    uint64_t totalCount = 0;
    for (size_t i = 0; i < numWindows; ++i) {
        double t0 = i * g_windowSec;
        if (t0 >= g_attackStart && t0 < g_attackEnd) {
            if (timeToSuppress < 0 && winMaliciousRate[i] < g_suppressThreshold) {
                timeToSuppress = t0 - g_attackStart;
            }
        }
        if (t0 >= g_attackEnd && recoveryTime < 0) {
            if (winMaliciousRate[i] > g_recoverThreshold) {
                recoveryTime = t0 - g_attackEnd;
            }
        }
        totalFalseSupp += winFalseSupp[i];
        totalCount += winCount[i];
    }

    if (totalCount > 0) {
        falseSuppRateOverall = static_cast<double>(totalFalseSupp) / totalCount;
    }

    std::ofstream sf(g_resultDir + "/exp8_summary.csv");
    sf << "time_to_suppress,false_suppression_rate,recovery_time\n";
    sf << timeToSuppress << "," << falseSuppRateOverall << "," << recoveryTime << "\n";
    sf.close();

    Simulator::Destroy();
    return 0;
}
