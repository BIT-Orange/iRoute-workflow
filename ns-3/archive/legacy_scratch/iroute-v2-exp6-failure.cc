/**
 * @file iroute-v2-exp6-failure.cc
 * @brief Exp6: Failure recovery with Rocketfuel topology + real dataset
 *
 * Fixes:
 * - Rocketfuel subgraph + TREC DL trace
 * - Control-plane reconvergence via LSA dissemination after failure
 * - Flooding baseline (parallel + sequential)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/flooding-discovery-consumer.hpp"
#include "apps/semantic-producer.hpp"
#include "extensions/iroute-route-manager-registry.hpp"
#include "helper/ndn-link-control-helper.hpp"

#include "iroute-common-utils.hpp"

#include <random>
#include <fstream>
#include <sstream>
#include <queue>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp6Failure");

// =============================================================================
// Global parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 60.0;
static uint32_t g_domains = 8;
static uint32_t g_queries = 88;
static uint32_t g_vectorDim = 128;
static uint32_t g_kMax = 5;
static double g_tau = 0.3;
static uint32_t g_interestLifetimeMs = 4000;
static uint32_t g_fetchTimeoutMs = 4000;
static double g_frequency = 2.0;

static double g_failTime = 10.0;
static double g_recoverTime = 30.0;
static std::string g_failMode = "link";  // link|producer
static uint32_t g_failedDomain = 0;
static std::string g_method = "all"; // iroute|flood-parallel|flood-seq|all
static std::string g_resultDir = "results/exp6";

// LSA control-plane
static double g_lsaPeriod = 2.0;
static double g_lsaFetchInterval = 1.0;

// Rocketfuel subgraph
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_topoSize = 150;
static uint32_t g_ingressNodeId = 0;  // 0=auto edge
static double g_linkDelayMs = 2.0;
static std::string g_linkDataRate = "1Gbps";

// Data files (real)
static std::string g_centroidsFile = "dataset/trec_dl_combined_dim128/domain_centroids.csv";
static std::string g_contentFile = "dataset/trec_dl_combined_dim128/producer_content.csv";
static std::string g_traceFile = "dataset/trec_dl_combined_dim128/consumer_trace.csv";

// =============================================================================
// Helpers
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

    // Build links among selected nodes
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

    // Pick ingress: if specified and in subgraph, use it; else choose low-degree node
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

    // Select domain gateway nodes using farthest-first
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

struct FailureContext {
    Ptr<Node> linkNode1;
    Ptr<Node> linkNode2;
    Ptr<SemanticProducer> failedProducer;
    std::vector<Ptr<IRouteApp>> domainApps;
    Ptr<IRouteApp> ingressApp;
    uint32_t lsaRxAtFail = 0;
    double lsaConvergenceMs = -1.0;
    bool lsaTracking = false;
    std::mt19937 rng;
};

static FailureContext g_ctx;

// Perturb centroids to force LSA update
void PerturbCentroids(Ptr<IRouteApp> app)
{
    if (!app) return;
    auto centroids = app->GetLocalCentroids();
    if (centroids.empty()) return;

    std::normal_distribution<float> noise(0.0f, 0.01f);
    for (auto& c : centroids) {
        auto data = c.C.getData();
        if (data.empty()) continue;
        for (auto& v : data) {
            v += noise(g_ctx.rng);
        }
        iroute::SemanticVector sv(data);
        sv.normalize();
        c.C = sv;
    }

    app->SetLocalCentroids(centroids);
}

void CheckLsaConvergence()
{
    if (!g_ctx.ingressApp || !g_ctx.lsaTracking) return;
    uint32_t rx = g_ctx.ingressApp->GetLsaRxCount();
    if (rx >= g_ctx.lsaRxAtFail + g_domains) {
        g_ctx.lsaConvergenceMs = (Simulator::Now().GetSeconds() - g_failTime) * 1000.0;
        g_ctx.lsaTracking = false;
        return;
    }
    Simulator::Schedule(MilliSeconds(100), &CheckLsaConvergence);
}

void TriggerFailure()
{
    NS_LOG_UNCOND("FAILURE at " << Simulator::Now().GetSeconds());
    if (g_failMode == "link" && g_ctx.linkNode1 && g_ctx.linkNode2) {
        LinkControlHelper::FailLink(g_ctx.linkNode1, g_ctx.linkNode2);
    } else if (g_failMode == "producer" && g_ctx.failedProducer) {
        g_ctx.failedProducer->SetActive(false);
    }

    // Force LSA update (control-plane reconvergence proxy)
    for (auto& app : g_ctx.domainApps) {
        PerturbCentroids(app);
    }

    if (g_ctx.ingressApp) {
        g_ctx.lsaRxAtFail = g_ctx.ingressApp->GetLsaRxCount();
        g_ctx.lsaTracking = true;
        Simulator::Schedule(MilliSeconds(100), &CheckLsaConvergence);
    }
}

void TriggerRecovery()
{
    NS_LOG_UNCOND("RECOVERY at " << Simulator::Now().GetSeconds());
    if (g_failMode == "link" && g_ctx.linkNode1 && g_ctx.linkNode2) {
        LinkControlHelper::UpLink(g_ctx.linkNode1, g_ctx.linkNode2);
    } else if (g_failMode == "producer" && g_ctx.failedProducer) {
        g_ctx.failedProducer->SetActive(true);
    }
}

struct ResultRow {
    uint64_t queryId = 0;
    std::string method;
    double discRttMs = -1.0;
    double fetchRttMs = -1.0;
    double e2eRttMs = -1.0;
    uint32_t numProbes = 0;
    uint32_t timeouts = 0;
    bool success = false;
    bool inFailureWindow = false;
};

struct RunResult {
    std::vector<ResultRow> rows;
    double lsaConvergenceMs = -1.0;
};

RunResult RunScenario(const std::string& method, bool floodParallel,
                      const std::map<uint32_t, std::vector<iroute::CentroidEntry>>& centroids,
                      const std::map<uint32_t, std::vector<iroute::utils::ContentEntry>>& content,
                      const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries)
{
    RunResult out;

    iroute::RouteManagerRegistry::clear();
    IRouteApp::ResetLsaCounter();

    g_ctx = FailureContext();
    g_ctx.rng = std::mt19937(g_seed + g_run);

    auto topo = BuildRocketfuelSubgraph(g_topoFile, g_topoSize, g_domains,
                                        g_ingressNodeId, g_linkDelayMs, g_linkDataRate);

    uint32_t domains = std::min<uint32_t>(g_domains, topo.domainNodes.size());
    g_domains = domains;

    // Install stack
    StackHelper ndnHelper;
    ndnHelper.setCsSize(0);
    ndnHelper.InstallAll();
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    StrategyChoiceHelper::InstallAll("/ndn/broadcast", "/localhost/nfd/strategy/multicast");

    // Ingress app (for LSA polling + cost ranking)
    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(g_lsaPeriod)));
    irouteHelper.SetAttribute("LsaFetchInterval", TimeValue(Seconds(g_lsaFetchInterval)));
    irouteHelper.SetAttribute("EnableLsaPolling", BooleanValue(true));
    irouteHelper.SetAttribute("HysteresisThreshold", DoubleValue(0.0));

    irouteHelper.SetAttribute("RouterId", StringValue("/ingress"));
    irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
    auto ingressApps = irouteHelper.Install(topo.ingress);
    g_ctx.ingressApp = DynamicCast<IRouteApp>(ingressApps.Get(0));

    // Compute hop distance for cost (static, for ranking)
    auto hopMap = iroute::utils::BFSAllDistances(topo.ingress);

    // Domain apps + producers
    g_ctx.domainApps.clear();
    for (uint32_t d = 0; d < domains; ++d) {
        Ptr<Node> node = topo.domainNodes[d];
        std::string domainPrefix = "/domain" + std::to_string(d);

        irouteHelper.SetAttribute("RouterId", StringValue(domainPrefix));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        auto apps = irouteHelper.Install(node);

        auto app = DynamicCast<IRouteApp>(apps.Get(0));
        g_ctx.domainApps.push_back(app);

        if (app && centroids.count(d)) {
            app->SetLocalCentroids(centroids.at(d));
        }

        if (app && content.count(d)) {
            std::vector<IRouteApp::ContentEntry> local;
            local.reserve(content.at(d).size());
            for (const auto& doc : content.at(d)) {
                IRouteApp::ContentEntry e;
                e.docId = doc.docId;
                e.canonicalName = doc.canonicalName;
                e.vector = MakeSemanticVector(doc.vector);
                e.isDistractor = doc.isDistractor;
                local.push_back(e);
            }
            app->SetLocalContent(local);
        }

        AppHelper producerHelper("ns3::ndn::SemanticProducer");
        producerHelper.SetAttribute("Prefix", StringValue(domainPrefix + "/data"));
        producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        producerHelper.SetAttribute("Freshness", TimeValue(Seconds(10)));
        auto prodApps = producerHelper.Install(node);

        if (d == g_failedDomain) {
            g_ctx.failedProducer = DynamicCast<SemanticProducer>(prodApps.Get(0));
        }
    }

    // Global routing
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    for (uint32_t d = 0; d < domains; ++d) {
        std::string domainPrefix = "/domain" + std::to_string(d);
        grHelper.AddOrigins(domainPrefix, topo.domainNodes[d]);
        grHelper.AddOrigins(domainPrefix + "/data", topo.domainNodes[d]);
    }
    GlobalRoutingHelper::CalculateRoutes();

    // Populate ingress RouteManager (static cost)
    if (g_ctx.ingressApp) {
        auto rm = iroute::RouteManagerRegistry::getOrCreate(topo.ingress->GetId(), g_vectorDim);
        if (rm) {
            rm->setActiveSemVerId(1);
            for (uint32_t d = 0; d < domains; ++d) {
                if (!centroids.count(d)) continue;
                iroute::DomainEntry entry;
                entry.domainId = Name("/domain" + std::to_string(d));
                entry.semVerId = 1;
                entry.seqNo = 1;
                uint32_t hops = 1;
                if (d < topo.domainNodes.size()) {
                    auto node = topo.domainNodes[d];
                    hops = hopMap.count(node->GetId()) ? hopMap[node->GetId()] : 1;
                }
                entry.cost = static_cast<double>(hops);
                entry.centroids = centroids.at(d);
                rm->updateDomain(entry);
            }
        }
    }

    // Configure known domains for LSA polling on all routers
    std::vector<Name> knownDomains;
    for (uint32_t d = 0; d < domains; ++d) {
        knownDomains.emplace_back("/domain" + std::to_string(d));
    }
    if (g_ctx.ingressApp) {
        g_ctx.ingressApp->SetKnownDomains(knownDomains);
    }
    for (auto& app : g_ctx.domainApps) {
        if (app) app->SetKnownDomains(knownDomains);
    }

    // Select failure link: ingress -> highest-degree neighbor
    g_ctx.linkNode1 = topo.ingress;
    g_ctx.linkNode2 = nullptr;

    // Find neighbor with max degree
    uint32_t bestDeg = 0;
    Ptr<Node> bestNeighbor;
    std::unordered_set<uint32_t> neighbors;
    for (uint32_t i = 0; i < topo.ingress->GetNDevices(); ++i) {
        Ptr<NetDevice> dev = topo.ingress->GetDevice(i);
        Ptr<Channel> channel = dev->GetChannel();
        if (!channel) continue;
        for (std::size_t j = 0; j < channel->GetNDevices(); ++j) {
            Ptr<NetDevice> otherDev = channel->GetDevice(j);
            Ptr<Node> neighbor = otherDev->GetNode();
            if (neighbor == topo.ingress) continue;
            if (neighbors.insert(neighbor->GetId()).second) {
                uint32_t deg = neighbor->GetNDevices();
                if (deg > bestDeg) {
                    bestDeg = deg;
                    bestNeighbor = neighbor;
                }
            }
        }
    }
    g_ctx.linkNode2 = bestNeighbor;

    if (g_failMode == "link" && (!g_ctx.linkNode1 || !g_ctx.linkNode2)) {
        NS_LOG_WARN("No valid link to fail; failure injection disabled");
    }

    // Install consumers
    if (method == "iroute") {
        AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
        consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
        consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("LifeTime", TimeValue(MilliSeconds(g_interestLifetimeMs)));
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
        auto apps = consumerHelper.Install(topo.ingress);
        auto cons = DynamicCast<IRouteDiscoveryConsumer>(apps.Get(0));
        if (cons) cons->SetQueryTrace(queries);

        Simulator::Schedule(Seconds(g_failTime), &TriggerFailure);
        Simulator::Schedule(Seconds(g_recoverTime), &TriggerRecovery);

        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();

        if (cons) {
            for (const auto& tx : cons->GetTransactions()) {
                ResultRow r;
                r.queryId = tx.queryId;
                r.method = "iRoute";
                r.discRttMs = tx.stage1RttMs;
                r.fetchRttMs = tx.stage2RttMs;
                r.e2eRttMs = tx.totalMs;
                r.numProbes = tx.probesUsed;
                r.timeouts = (tx.failureReason.find("TIMEOUT") != std::string::npos) ? 1 : 0;
                r.success = tx.stage2Success;
                r.inFailureWindow = (tx.startTime >= g_failTime && tx.startTime < g_recoverTime);
                out.rows.push_back(r);
            }
        }
        out.lsaConvergenceMs = g_ctx.lsaConvergenceMs;
    }

    if (method == "flood") {
        AppHelper consumerHelper("ns3::ndn::FloodingDiscoveryConsumer");
        consumerHelper.SetAttribute("ParallelMode", BooleanValue(floodParallel));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("LifeTime", TimeValue(MilliSeconds(g_interestLifetimeMs)));
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));

        auto apps = consumerHelper.Install(topo.ingress);
        auto cons = DynamicCast<FloodingDiscoveryConsumer>(apps.Get(0));
        if (cons) {
            std::vector<Name> allDomains;
            for (uint32_t d = 0; d < domains; ++d) {
                allDomains.emplace_back("/domain" + std::to_string(d));
            }
            cons->SetAllDomains(allDomains);
            cons->SetQueryTrace(queries);
        }

        Simulator::Schedule(Seconds(g_failTime), &TriggerFailure);
        Simulator::Schedule(Seconds(g_recoverTime), &TriggerRecovery);

        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();

        if (cons) {
            for (const auto& tx : cons->GetTransactions()) {
                ResultRow r;
                r.queryId = tx.queryId;
                r.method = floodParallel ? "Flood-Parallel" : "Flood-Sequential";
                r.discRttMs = tx.stage1RttMs;
                r.fetchRttMs = tx.stage2RttMs;
                r.e2eRttMs = tx.totalMs;
                r.numProbes = tx.totalInterestsSent;
                r.timeouts = (tx.failureType == FailureType::DISCOVERY_TIMEOUT ||
                              tx.failureType == FailureType::FETCH_TIMEOUT) ? 1 : 0;
                r.success = tx.stage2Success;
                r.inFailureWindow = (tx.startTime >= g_failTime && tx.startTime < g_recoverTime);
                out.rows.push_back(r);
            }
        }
    }

    Simulator::Destroy();
    return out;
}

void WriteRows(const std::string& filename, const std::vector<ResultRow>& rows)
{
    std::ofstream f(filename);
    f << "query_id,method,disc_rtt_ms,fetch_rtt_ms,e2e_rtt_ms,num_probes,timeouts,success,in_failure_window\n";
    for (const auto& r : rows) {
        f << r.queryId << "," << r.method << ","
          << std::fixed << std::setprecision(2)
          << r.discRttMs << "," << r.fetchRttMs << "," << r.e2eRttMs << ","
          << r.numProbes << "," << r.timeouts << "," << (r.success ? 1 : 0) << ","
          << (r.inFailureWindow ? 1 : 0) << "\n";
    }
}

void WriteSummary(const std::string& filename, const std::string& method,
                  const std::vector<ResultRow>& rows, double lsaConvMs)
{
    uint32_t total = rows.size();
    uint32_t success = 0;
    uint32_t timeouts = 0;
    for (const auto& r : rows) {
        if (r.success) success++;
        timeouts += r.timeouts;
    }

    bool writeHeader = false;
    { std::ifstream check(filename); writeHeader = !check.good(); }

    std::ofstream f(filename, std::ios::app);
    if (writeHeader) {
        f << "method,queries,success,success_rate,timeouts,lsa_convergence_ms\n";
    }

    double successRate = total > 0 ? 100.0 * success / total : 0.0;
    f << method << "," << total << "," << success << ","
      << std::fixed << std::setprecision(2) << successRate << ","
      << timeouts << "," << lsaConvMs << "\n";
}

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("run", "Run number", g_run);
    cmd.AddValue("simTime", "Simulation time", g_simTime);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("queries", "Number of queries", g_queries);
    cmd.AddValue("kMax", "K Max", g_kMax);
    cmd.AddValue("tau", "Score threshold", g_tau);
    cmd.AddValue("interestLifetimeMs", "Interest lifetime (ms)", g_interestLifetimeMs);
    cmd.AddValue("fetchTimeoutMs", "Fetch timeout (ms)", g_fetchTimeoutMs);
    cmd.AddValue("frequency", "Query frequency (Hz)", g_frequency);
    cmd.AddValue("failTime", "Failure injection time", g_failTime);
    cmd.AddValue("recoverTime", "Recovery time", g_recoverTime);
    cmd.AddValue("failMode", "link|producer", g_failMode);
    cmd.AddValue("failedDomain", "Failed domain id (producer mode)", g_failedDomain);
    cmd.AddValue("method", "iroute|flood-parallel|flood-seq|all", g_method);
    cmd.AddValue("resultDir", "Result Directory", g_resultDir);

    cmd.AddValue("lsaPeriod", "LSA publish period (s)", g_lsaPeriod);
    cmd.AddValue("lsaFetchInterval", "LSA fetch interval (s)", g_lsaFetchInterval);

    cmd.AddValue("topoFile", "Rocketfuel topology file", g_topoFile);
    cmd.AddValue("topoSize", "Rocketfuel subgraph size", g_topoSize);
    cmd.AddValue("ingressNodeId", "Ingress node ID (0=auto)", g_ingressNodeId);
    cmd.AddValue("linkDelayMs", "Per-link delay (ms)", g_linkDelayMs);
    cmd.AddValue("dataRate", "Link data rate", g_linkDataRate);

    cmd.AddValue("centroidsFile", "domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("contentFile", "producer_content.csv", g_contentFile);
    cmd.AddValue("traceFile", "consumer_trace.csv", g_traceFile);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);

    if (g_centroidsFile.empty() || g_contentFile.empty() || g_traceFile.empty()) {
        NS_LOG_ERROR("Required: --centroidsFile --contentFile --traceFile");
        return 1;
    }

    CreateDirectoryIfNotExist(g_resultDir);

    auto centroids = iroute::utils::LoadCentroidsFromCsv(g_centroidsFile);
    auto content = iroute::utils::LoadContentFromCsv(g_contentFile);
    auto queries = iroute::utils::LoadTraceFromCsv(g_traceFile);

    if (centroids.empty() || content.empty() || queries.empty()) {
        NS_LOG_ERROR("Failed to load dataset files");
        return 1;
    }

    if (g_domains == 0 || g_domains > centroids.size()) {
        g_domains = centroids.size();
    }

    // infer vector dim
    for (const auto& kv : centroids) {
        if (!kv.second.empty()) {
            g_vectorDim = kv.second.front().C.getDimension();
            break;
        }
    }

    if (g_queries > 0 && g_queries < queries.size()) {
        queries.resize(g_queries);
    }

    if (g_method == "all") {
        auto r1 = RunScenario("iroute", true, centroids, content, queries);
        auto r2 = RunScenario("flood", true, centroids, content, queries);
        auto r3 = RunScenario("flood", false, centroids, content, queries);

        WriteRows(g_resultDir + "/exp6_iroute.csv", r1.rows);
        WriteRows(g_resultDir + "/exp6_flood_parallel.csv", r2.rows);
        WriteRows(g_resultDir + "/exp6_flood_sequential.csv", r3.rows);

        WriteSummary(g_resultDir + "/exp6_summary.csv", "iRoute", r1.rows, r1.lsaConvergenceMs);
        WriteSummary(g_resultDir + "/exp6_summary.csv", "Flood-Parallel", r2.rows, -1.0);
        WriteSummary(g_resultDir + "/exp6_summary.csv", "Flood-Sequential", r3.rows, -1.0);

    } else if (g_method == "iroute") {
        auto r1 = RunScenario("iroute", true, centroids, content, queries);
        WriteRows(g_resultDir + "/exp6_iroute.csv", r1.rows);
        WriteSummary(g_resultDir + "/exp6_summary.csv", "iRoute", r1.rows, r1.lsaConvergenceMs);

    } else if (g_method == "flood-parallel") {
        auto r2 = RunScenario("flood", true, centroids, content, queries);
        WriteRows(g_resultDir + "/exp6_flood_parallel.csv", r2.rows);
        WriteSummary(g_resultDir + "/exp6_summary.csv", "Flood-Parallel", r2.rows, -1.0);

    } else if (g_method == "flood-seq") {
        auto r3 = RunScenario("flood", false, centroids, content, queries);
        WriteRows(g_resultDir + "/exp6_flood_sequential.csv", r3.rows);
        WriteSummary(g_resultDir + "/exp6_summary.csv", "Flood-Sequential", r3.rows, -1.0);

    } else {
        NS_LOG_ERROR("Unknown method: " << g_method);
        return 1;
    }

    return 0;
}
