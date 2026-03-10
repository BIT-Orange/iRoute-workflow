/**
 * @file iroute-v2-exp4-latency.cc
 * @brief Exp4: Latency breakdown on Rocketfuel + real dataset
 *
 * Baselines:
 * 1) iRoute (Top-K probes)
 * 2) Flooding discovery (parallel + sequential)
 * 3) Centralized search (oracle -> fetch)
 *
 * Outputs per-query: disc_rtt, fetch_rtt, e2e_rtt, num_probes, timeouts
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/flooding-discovery-consumer.hpp"
#include "apps/centralized-search-consumer.hpp"
#include "apps/search-oracle-app.hpp"
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
#include <iomanip>
#include <sys/stat.h>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp4Latency");

// =============================================================================
// Global parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 40.0;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_queries = 88;

static uint32_t g_kMax = 5;
static double g_tau = 0.3;
static double g_alpha = 0.7;
static double g_beta = 0.3;
static double g_lambda = 10.0;

static uint32_t g_interestLifetimeMs = 4000;
static uint32_t g_fetchTimeoutMs = 4000;
static double g_frequency = 2.0; // queries per second

static std::string g_resultDir = "results/exp4";
static std::string g_method = "all"; // iroute, flood-parallel, flood-seq, centralized, all

// Rocketfuel subgraph
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_topoSize = 150;
static uint32_t g_ingressNodeId = 0;  // 0=auto edge
static uint32_t g_searchNodeId = 0;   // 0=auto center
static double g_linkDelayMs = 2.0;
static std::string g_linkDataRate = "1Gbps";

// Data files
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

// Parse Rocketfuel topology into adjacency list
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
    Ptr<Node> search;
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
                                       uint32_t searchNodeId,
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

    // Pick search server (centralized baseline): highest-degree node
    uint32_t searchOldId = 0;
    if (searchNodeId != 0 && selectedSet.count(searchNodeId)) {
        searchOldId = searchNodeId;
    } else {
        uint32_t bestId = ingressOldId;
        uint32_t bestDeg = 0;
        for (uint32_t u : selected) {
            if (u == ingressOldId) continue;
            uint32_t deg = degree.count(u) ? degree[u] : 0;
            if (deg > bestDeg) {
                bestDeg = deg;
                bestId = u;
            }
        }
        searchOldId = bestId;
    }
    result.search = idToNode[searchOldId];

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

struct ResultRow {
    uint64_t queryId = 0;
    std::string method;
    double discRttMs = -1.0;
    double fetchRttMs = -1.0;
    double e2eRttMs = -1.0;
    uint32_t numProbes = 0;
    uint32_t timeouts = 0;
    bool success = false;
};

void WriteResults(const std::string& filename, const std::vector<ResultRow>& rows)
{
    std::ofstream file(filename);
    file << "query_id,method,disc_rtt_ms,fetch_rtt_ms,e2e_rtt_ms,num_probes,timeouts,success\n";
    for (const auto& r : rows) {
        file << r.queryId << "," << r.method << ","
             << std::fixed << std::setprecision(2)
             << r.discRttMs << "," << r.fetchRttMs << "," << r.e2eRttMs << ","
             << r.numProbes << "," << r.timeouts << "," << (r.success ? 1 : 0) << "\n";
    }
}

// =============================================================================
// Experiment runner
// =============================================================================

static std::vector<ResultRow> RunMethod(const std::string& method,
                                        const std::map<uint32_t, std::vector<iroute::CentroidEntry>>& centroids,
                                        const std::map<uint32_t, std::vector<iroute::utils::ContentEntry>>& content,
                                        const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries,
                                        bool parallelFlood)
{
    iroute::RouteManagerRegistry::clear();
    IRouteApp::ResetLsaCounter();

    auto topo = BuildRocketfuelSubgraph(g_topoFile, g_topoSize, g_domains,
                                        g_ingressNodeId, g_searchNodeId,
                                        g_linkDelayMs, g_linkDataRate);
    uint32_t domains = std::min<uint32_t>(g_domains, topo.domainNodes.size());

    // Install NDN stack
    StackHelper ndnHelper;
    ndnHelper.InstallAll();

    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    StrategyChoiceHelper::InstallAll("/ndn/broadcast", "/localhost/nfd/strategy/multicast");

    // Compute hop distance for cost
    auto hopMap = iroute::utils::BFSAllDistances(topo.ingress);

    // Install domain apps and producers
    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(g_simTime + 1))); // disable LSA
    irouteHelper.SetAttribute("EnableV2LsaData", BooleanValue(false));

    AppHelper producerHelper("ns3::ndn::SemanticProducer");
    producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    producerHelper.SetAttribute("Freshness", TimeValue(Seconds(10)));

    for (uint32_t d = 0; d < domains; ++d) {
        if (d >= topo.domainNodes.size()) break;
        Ptr<Node> node = topo.domainNodes[d];
        std::string domainPrefix = "/domain" + std::to_string(d);

        irouteHelper.SetAttribute("RouterId", StringValue(domainPrefix));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        uint32_t hops = hopMap.count(node->GetId()) ? hopMap[node->GetId()] : 1;
        irouteHelper.SetAttribute("RouteCost", DoubleValue(static_cast<double>(hops)));
        auto apps = irouteHelper.Install(node);

        if (auto app = DynamicCast<IRouteApp>(apps.Get(0))) {
            if (centroids.count(d)) {
                app->SetLocalCentroids(centroids.at(d));
            }
            if (content.count(d)) {
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
        }

        producerHelper.SetAttribute("Prefix", StringValue(domainPrefix + "/data"));
        producerHelper.Install(node);
    }

    // Centralized oracle (if needed)
    Ptr<Node> oracleNode = topo.search;
    if (method == "centralized") {
        AppHelper oracleHelper("ns3::ndn::SearchOracleApp");
        oracleHelper.SetAttribute("Prefix", StringValue("/search/oracle"));
        auto oracleApps = oracleHelper.Install(oracleNode);
        if (auto oracle = DynamicCast<SearchOracleApp>(oracleApps.Get(0))) {
            for (const auto& kv : content) {
                for (const auto& doc : kv.second) {
                    GlobalContentEntry entry;
                    entry.domainId = doc.domainId;
                    entry.docId = doc.docId;
                    entry.canonicalName = doc.canonicalName;
                    entry.vector = MakeSemanticVector(doc.vector);
                    entry.isDistractor = doc.isDistractor;
                    oracle->AddContent(entry);
                }
            }
        }
    }

    // Global routing
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();

    for (uint32_t d = 0; d < domains; ++d) {
        if (d >= topo.domainNodes.size()) break;
        std::string domainPrefix = "/domain" + std::to_string(d);
        grHelper.AddOrigins(domainPrefix, topo.domainNodes[d]);
        grHelper.AddOrigins(domainPrefix + "/data", topo.domainNodes[d]);
    }

    if (method == "centralized") {
        grHelper.AddOrigins("/search/oracle", oracleNode);
    }

    GlobalRoutingHelper::CalculateRoutes();

    // Pre-populate ingress RouteManager for iRoute
    if (method == "iroute") {
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

    // Install consumers
    std::vector<ResultRow> rows;

    if (method == "iroute") {
        AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
        consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        consumerHelper.SetAttribute("KMax", UintegerValue(g_kMax));
        consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("LifeTime", TimeValue(MilliSeconds(g_interestLifetimeMs)));
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
        consumerHelper.SetAttribute("Alpha", DoubleValue(g_alpha));
        consumerHelper.SetAttribute("Beta", DoubleValue(g_beta));
        consumerHelper.SetAttribute("Lambda", DoubleValue(g_lambda));

        auto apps = consumerHelper.Install(topo.ingress);
        auto cons = DynamicCast<IRouteDiscoveryConsumer>(apps.Get(0));
        if (cons) {
            cons->SetQueryTrace(queries);
        }

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
                rows.push_back(r);
            }
        }

    } else if (method == "flood") {
        AppHelper consumerHelper("ns3::ndn::FloodingDiscoveryConsumer");
        consumerHelper.SetAttribute("ParallelMode", BooleanValue(parallelFlood));
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

        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();

        if (cons) {
            for (const auto& tx : cons->GetTransactions()) {
                ResultRow r;
                r.queryId = tx.queryId;
                r.method = parallelFlood ? "Flood-Parallel" : "Flood-Sequential";
                r.discRttMs = tx.stage1RttMs;
                r.fetchRttMs = tx.stage2RttMs;
                r.e2eRttMs = tx.totalMs;
                r.numProbes = tx.totalInterestsSent;
                r.timeouts = (tx.failureType == FailureType::DISCOVERY_TIMEOUT ||
                              tx.failureType == FailureType::FETCH_TIMEOUT) ? 1 : 0;
                r.success = tx.stage2Success;
                rows.push_back(r);
            }
        }

    } else if (method == "centralized") {
        AppHelper consumerHelper("ns3::ndn::CentralizedSearchConsumer");
        consumerHelper.SetAttribute("OraclePrefix", StringValue("/search/oracle"));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("LifeTime", TimeValue(MilliSeconds(g_interestLifetimeMs)));
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));

        auto apps = consumerHelper.Install(topo.ingress);
        auto cons = DynamicCast<CentralizedSearchConsumer>(apps.Get(0));
        if (cons) {
            cons->SetQueryTrace(queries);
        }

        Simulator::Stop(Seconds(g_simTime));
        Simulator::Run();

        if (cons) {
            for (const auto& tx : cons->GetTransactions()) {
                ResultRow r;
                r.queryId = tx.queryId;
                r.method = "Centralized";
                r.discRttMs = tx.stage1RttMs;
                r.fetchRttMs = tx.stage2RttMs;
                r.e2eRttMs = tx.totalMs;
                r.numProbes = 1;
                r.timeouts = (tx.failureType == FailureType::DISCOVERY_TIMEOUT ||
                              tx.failureType == FailureType::FETCH_TIMEOUT) ? 1 : 0;
                r.success = tx.stage2Success;
                rows.push_back(r);
            }
        }
    }

    Simulator::Destroy();
    return rows;
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("run", "Run number", g_run);
    cmd.AddValue("simTime", "Simulation time", g_simTime);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("queries", "Number of queries", g_queries);
    cmd.AddValue("kMax", "Max probes for iRoute", g_kMax);
    cmd.AddValue("tau", "Score threshold", g_tau);
    cmd.AddValue("alpha", "Semantic similarity weight", g_alpha);
    cmd.AddValue("beta", "Cost penalty weight", g_beta);
    cmd.AddValue("lambda", "Gate steepness", g_lambda);
    cmd.AddValue("interestLifetimeMs", "Interest lifetime (ms)", g_interestLifetimeMs);
    cmd.AddValue("fetchTimeoutMs", "Stage-2 fetch timeout (ms)", g_fetchTimeoutMs);
    cmd.AddValue("frequency", "Query frequency (Hz)", g_frequency);
    cmd.AddValue("method", "Method: iroute|flood-parallel|flood-seq|centralized|all", g_method);
    cmd.AddValue("resultDir", "Output dir", g_resultDir);

    cmd.AddValue("topoFile", "Path to Rocketfuel topology file", g_topoFile);
    cmd.AddValue("topoSize", "Rocketfuel subgraph size", g_topoSize);
    cmd.AddValue("ingressNodeId", "Ingress node ID (0=auto)", g_ingressNodeId);
    cmd.AddValue("searchNodeId", "Search server node ID (0=auto)", g_searchNodeId);
    cmd.AddValue("linkDelayMs", "Per-link delay (ms)", g_linkDelayMs);
    cmd.AddValue("dataRate", "Link data rate", g_linkDataRate);

    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("contentFile", "Path to producer_content.csv", g_contentFile);
    cmd.AddValue("traceFile", "Path to consumer_trace.csv", g_traceFile);
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

    std::vector<ResultRow> allRows;

    if (g_method == "all") {
        auto r1 = RunMethod("iroute", centroids, content, queries, true);
        auto r2 = RunMethod("flood", centroids, content, queries, true);
        auto r3 = RunMethod("flood", centroids, content, queries, false);
        auto r4 = RunMethod("centralized", centroids, content, queries, true);

        allRows.insert(allRows.end(), r1.begin(), r1.end());
        allRows.insert(allRows.end(), r2.begin(), r2.end());
        allRows.insert(allRows.end(), r3.begin(), r3.end());
        allRows.insert(allRows.end(), r4.begin(), r4.end());

        WriteResults(g_resultDir + "/exp4_all.csv", allRows);
        WriteResults(g_resultDir + "/exp4_iroute.csv", r1);
        WriteResults(g_resultDir + "/exp4_flood_parallel.csv", r2);
        WriteResults(g_resultDir + "/exp4_flood_sequential.csv", r3);
        WriteResults(g_resultDir + "/exp4_centralized.csv", r4);

    } else if (g_method == "iroute") {
        auto r1 = RunMethod("iroute", centroids, content, queries, true);
        WriteResults(g_resultDir + "/exp4_iroute.csv", r1);
    } else if (g_method == "flood-parallel") {
        auto r2 = RunMethod("flood", centroids, content, queries, true);
        WriteResults(g_resultDir + "/exp4_flood_parallel.csv", r2);
    } else if (g_method == "flood-seq") {
        auto r3 = RunMethod("flood", centroids, content, queries, false);
        WriteResults(g_resultDir + "/exp4_flood_sequential.csv", r3);
    } else if (g_method == "centralized") {
        auto r4 = RunMethod("centralized", centroids, content, queries, true);
        WriteResults(g_resultDir + "/exp4_centralized.csv", r4);
    } else {
        NS_LOG_ERROR("Unknown method: " << g_method);
        return 1;
    }

    return 0;
}
