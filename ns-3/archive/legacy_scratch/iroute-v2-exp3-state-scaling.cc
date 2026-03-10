/**
 * @file iroute-v2-exp3-state-scaling.cc
 * @brief Exp3: Control-plane state scaling with real LSA dissemination
 *
 * Changes vs old version:
 * - Uses Rocketfuel topology subgraph (100–200 nodes) instead of star
 * - Runs real LSA dissemination via IRouteApp polling
 * - Measures per-router convergence time and LSA Rx bytes
 * - NLSR baseline split into provider-prefix (lower bound) and object-prefix (upper bound)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

// Shared utilities for CSV loading + topology helpers
#include "iroute-common-utils.hpp"

#include <random>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <queue>
#include <map>
#include <set>
#include <unordered_set>
#include <iomanip>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp3StateScaling");

// =============================================================================
// Global parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 30.0;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_objectsPerDomain = 1000;
static uint32_t g_M = 4;
static double g_lsaPeriod = 5.0;
static std::string g_resultDir = "results/exp3";

// Real data + topology
static std::string g_centroidsFile = "dataset/trec_dl_combined_dim128/domain_centroids.csv";
static std::string g_contentFile = "dataset/trec_dl_combined_dim128/producer_content.csv";
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_topoSize = 150;  // subgraph size (100-200 recommended)
static uint32_t g_ingressNodeId = 0;  // 0=auto (edge node)
static double g_linkDelayMs = 2.0;
static std::string g_linkDataRate = "1Gbps";

// NLSR baselines
static uint32_t g_providerPrefixesPerDomain = 10;  // lower bound

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

// Generate M centroids from objects (synthetic fallback)
std::vector<iroute::CentroidEntry> GenerateCentroidsFromObjects(
    std::mt19937& rng, uint32_t objectCount, uint32_t M, uint32_t vectorDim)
{
    std::vector<iroute::CentroidEntry> centroids;
    if (objectCount == 0 || M == 0) return centroids;

    for (uint32_t m = 0; m < M; ++m) {
        iroute::CentroidEntry c;
        c.centroidId = m;
        c.C = MakeSemanticVector(GenerateRandomVector(rng, vectorDim));
        c.radius = 0.5;
        c.weight = std::min(100.0, static_cast<double>(objectCount) / M);
        centroids.push_back(c);
    }

    return centroids;
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

    // If not enough nodes (disconnected), fill with remaining nodes
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
    std::map<uint32_t, uint32_t> nodeIdToDomain; // ns-3 nodeId -> domain index
    std::vector<uint32_t> oldIdByIndex;           // index -> original Rocketfuel node id
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
    result.oldIdByIndex.reserve(selected.size());

    for (size_t i = 0; i < selected.size(); ++i) {
        idToNode[selected[i]] = result.nodes.Get(i);
        result.oldIdByIndex.push_back(selected[i]);
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

    // Choose ingress node: if specified and in subgraph, use it; else pick low-degree edge
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

    // Select domain gateway nodes using farthest-first (on ns-3 nodes)
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
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("run", "Run number", g_run);
    cmd.AddValue("simTime", "Simulation time", g_simTime);
    cmd.AddValue("vectorDim", "Vector dimension", g_vectorDim);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("objectsPerDomain", "Objects per domain (synthetic fallback)", g_objectsPerDomain);
    cmd.AddValue("M", "Centroids per domain", g_M);
    cmd.AddValue("lsaPeriod", "LSA broadcast interval (seconds)", g_lsaPeriod);
    cmd.AddValue("resultDir", "Results directory", g_resultDir);
    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("contentFile", "Path to producer_content.csv", g_contentFile);
    cmd.AddValue("topoFile", "Path to Rocketfuel topology file", g_topoFile);
    cmd.AddValue("topoSize", "Rocketfuel subgraph size (nodes)", g_topoSize);
    cmd.AddValue("ingressNodeId", "Ingress node ID in Rocketfuel graph (0=auto)", g_ingressNodeId);
    cmd.AddValue("linkDelayMs", "Per-link delay (ms)", g_linkDelayMs);
    cmd.AddValue("dataRate", "Link data rate", g_linkDataRate);
    cmd.AddValue("providerPrefixesPerDomain", "NLSR provider-prefix per domain", g_providerPrefixesPerDomain);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);
    std::mt19937 rng(g_seed + g_run);

    CreateDirectoryIfNotExist(g_resultDir);

    // Load centroids (real) or generate synthetic
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> domainCentroids;
    if (!g_centroidsFile.empty()) {
        domainCentroids = iroute::utils::LoadCentroidsFromCsv(g_centroidsFile);
        if (!domainCentroids.empty()) {
            if (g_domains == 0 || g_domains > domainCentroids.size()) {
                g_domains = domainCentroids.size();
            }
            // infer vector dim from first centroid
            for (const auto& kv : domainCentroids) {
                if (!kv.second.empty()) {
                    g_vectorDim = kv.second.front().C.getDimension();
                    break;
                }
            }
        }
    }

    if (domainCentroids.empty()) {
        domainCentroids.clear();
        for (uint32_t d = 0; d < g_domains; ++d) {
            domainCentroids[d] = GenerateCentroidsFromObjects(rng, g_objectsPerDomain, g_M, g_vectorDim);
        }
    }

    // Build Rocketfuel subgraph
    auto topo = BuildRocketfuelSubgraph(g_topoFile, g_topoSize, g_domains,
                                        g_ingressNodeId, g_linkDelayMs, g_linkDataRate);

    if (topo.domainNodes.size() < g_domains) {
        NS_LOG_WARN("Requested domains=" << g_domains << " but only selected " << topo.domainNodes.size());
        g_domains = topo.domainNodes.size();
    }

    NS_LOG_UNCOND("=== Exp3: State Scaling (Rocketfuel) ===");
    NS_LOG_UNCOND("nodes=" << topo.nodes.GetN() << ", links=" << topo.numLinks
                  << ", domains=" << g_domains << ", M=" << g_M
                  << ", vectorDim=" << g_vectorDim);

    // Install NDN stack
    StackHelper ndnHelper;
    ndnHelper.InstallAll();
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    StrategyChoiceHelper::InstallAll("/ndn/broadcast", "/localhost/nfd/strategy/multicast");

    // Clear per-node registries
    iroute::RouteManagerRegistry::clear();
    IRouteApp::ResetLsaCounter();

    // Compute hop distances from ingress
    auto hopMap = iroute::utils::BFSAllDistances(topo.ingress);

    // Install IRouteApp on all nodes
    std::vector<Ptr<IRouteApp>> irouteApps(topo.nodes.GetN());

    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(g_lsaPeriod)));
    irouteHelper.SetAttribute("LsaFetchInterval", TimeValue(Seconds(g_lsaPeriod / 2.0)));
    irouteHelper.SetAttribute("EnableLsaPolling", BooleanValue(true));

    for (uint32_t i = 0; i < topo.nodes.GetN(); ++i) {
        Ptr<Node> node = topo.nodes.Get(i);
        std::string routerId = "/router" + std::to_string(node->GetId());
        bool isDomain = topo.nodeIdToDomain.count(node->GetId()) > 0;

        if (isDomain) {
            uint32_t d = topo.nodeIdToDomain[node->GetId()];
            routerId = "/domain" + std::to_string(d);
        }

        irouteHelper.SetAttribute("RouterId", StringValue(routerId));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));

        // Set cost for domain nodes based on hop distance to ingress
        if (isDomain) {
            uint32_t hops = hopMap.count(node->GetId()) ? hopMap[node->GetId()] : 1;
            irouteHelper.SetAttribute("RouteCost", DoubleValue(static_cast<double>(hops)));
        } else {
            irouteHelper.SetAttribute("RouteCost", DoubleValue(1.0));
        }

        auto apps = irouteHelper.Install(node);
        auto app = DynamicCast<IRouteApp>(apps.Get(0));
        irouteApps[i] = app;

        if (isDomain && app) {
            uint32_t d = topo.nodeIdToDomain[node->GetId()];
            if (domainCentroids.count(d)) {
                app->SetLocalCentroids(domainCentroids[d]);
            }
        }
    }

    // Global routing for LSA dissemination
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    for (uint32_t d = 0; d < g_domains; ++d) {
        std::string domainName = "/domain" + std::to_string(d);
        if (d < topo.domainNodes.size()) {
            grHelper.AddOrigins(domainName, topo.domainNodes[d]);
        }
    }
    GlobalRoutingHelper::CalculateRoutes();

    // Configure known domains for polling on all routers
    std::vector<Name> knownDomains;
    for (uint32_t d = 0; d < g_domains; ++d) {
        knownDomains.emplace_back("/domain" + std::to_string(d));
    }

    for (auto& app : irouteApps) {
        if (app) app->SetKnownDomains(knownDomains);
    }

    // Convergence tracking
    std::vector<bool> converged(topo.nodes.GetN(), false);
    std::vector<double> convergedMs(topo.nodes.GetN(), -1.0);

    auto checker = std::make_shared<std::function<void()>>();
    *checker = [&, checker]() {
        bool anyPending = false;
        for (uint32_t i = 0; i < topo.nodes.GetN(); ++i) {
            if (converged[i]) continue;
            Ptr<Node> node = topo.nodes.Get(i);
            auto rm = iroute::RouteManagerRegistry::get(node->GetId());
            if (rm && rm->domainCount() >= g_domains) {
                converged[i] = true;
                convergedMs[i] = Simulator::Now().GetSeconds() * 1000.0;
            } else {
                anyPending = true;
            }
        }
        if (anyPending) {
            Simulator::Schedule(MilliSeconds(100), *checker);
        }
    };

    Simulator::Schedule(MilliSeconds(100), *checker);

    // Run simulation
    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();

    // Collect metrics
    uint64_t lsaTxBytesTotal = 0;
    uint32_t lsaTxCount = 0;
    for (auto& app : irouteApps) {
        if (app) {
            lsaTxBytesTotal += app->GetLsaTxBytesTotal();
            lsaTxCount += app->GetLsaTxCount();
        }
    }

    // NLSR baseline counts
    size_t totalDocs = 0;
    if (!g_contentFile.empty()) {
        auto content = iroute::utils::LoadContentFromCsv(g_contentFile);
        for (const auto& kv : content) {
            if (kv.first < g_domains) totalDocs += kv.second.size();
        }
    }
    if (totalDocs == 0) {
        totalDocs = static_cast<size_t>(g_objectsPerDomain) * g_domains;
    }

    uint64_t objectPrefixCount = totalDocs;  // upper bound
    uint64_t providerPrefixCount = static_cast<uint64_t>(g_providerPrefixesPerDomain) * g_domains;  // lower bound

    iroute::utils::NlsrStateEstimator nlsrEstimator;
    nlsrEstimator.numRouters = topo.nodes.GetN();
    nlsrEstimator.avgLinksPerRouter = std::max(1.0, topo.avgLinksPerRouter);

    iroute::utils::IRouteStateEstimator irouteEstimator;
    irouteEstimator.numDomains = g_domains;
    irouteEstimator.centroidsPerDomain = g_M;
    irouteEstimator.vectorDim = g_vectorDim;

    uint64_t irouteLsdbEntries = irouteEstimator.getLsdbEntries();
    uint64_t irouteLsdbBytes = irouteEstimator.getLsdbBytes();

    // Provider-prefix baseline
    nlsrEstimator.numPrefixes = providerPrefixCount;
    uint64_t nlsrProviderEntries = nlsrEstimator.getLsdbEntries();
    uint64_t nlsrProviderBytes = nlsrEstimator.getLsdbBytes();

    // Object-prefix baseline
    nlsrEstimator.numPrefixes = objectPrefixCount;
    uint64_t nlsrObjectEntries = nlsrEstimator.getLsdbEntries();
    uint64_t nlsrObjectBytes = nlsrEstimator.getLsdbBytes();

    // Export per-router convergence stats
    std::string routerFile = g_resultDir + "/exp3_router_stats.csv";
    std::ofstream rf(routerFile);
    rf << "node_id,orig_node_id,is_domain,domain_index,converged_time_ms,lsa_rx_bytes,lsa_rx_count\n";

    for (uint32_t i = 0; i < topo.nodes.GetN(); ++i) {
        Ptr<Node> node = topo.nodes.Get(i);
        uint32_t nodeId = node->GetId();
        uint32_t origId = (i < topo.oldIdByIndex.size()) ? topo.oldIdByIndex[i] : 0;
        bool isDomain = topo.nodeIdToDomain.count(nodeId) > 0;
        int domainIdx = isDomain ? static_cast<int>(topo.nodeIdToDomain[nodeId]) : -1;

        uint64_t rxBytes = 0;
        uint32_t rxCount = 0;
        if (irouteApps[i]) {
            rxBytes = irouteApps[i]->GetLsaRxBytesTotal();
            rxCount = irouteApps[i]->GetLsaRxCount();
        }

        rf << nodeId << "," << origId << "," << (isDomain ? 1 : 0) << "," << domainIdx << ","
           << std::fixed << std::setprecision(2) << convergedMs[i] << ","
           << rxBytes << "," << rxCount << "\n";
    }
    rf.close();

    // Export summary
    std::string summaryFile = g_resultDir + "/exp3_summary.csv";
    bool writeHeader = false;
    { std::ifstream check(summaryFile); writeHeader = !check.good(); }

    std::ofstream sf(summaryFile, std::ios::app);
    if (writeHeader) {
        sf << "nodes,links,domains,M,vectorDim,simTime,lsaPeriod,"
           << "lsaTxCount,lsaTxBytesTotal,avgLinksPerRouter,"
           << "irouteLsdbEntries,irouteLsdbBytes,"
           << "nlsrProviderEntries,nlsrProviderBytes,"
           << "nlsrObjectEntries,nlsrObjectBytes,"
           << "providerPrefixesPerDomain,objectPrefixes\n";
    }

    sf << topo.nodes.GetN() << "," << topo.numLinks << ","
       << g_domains << "," << g_M << "," << g_vectorDim << ","
       << g_simTime << "," << g_lsaPeriod << ","
       << lsaTxCount << "," << lsaTxBytesTotal << ","
       << std::fixed << std::setprecision(2) << topo.avgLinksPerRouter << ","
       << irouteLsdbEntries << "," << irouteLsdbBytes << ","
       << nlsrProviderEntries << "," << nlsrProviderBytes << ","
       << nlsrObjectEntries << "," << nlsrObjectBytes << ","
       << g_providerPrefixesPerDomain << "," << objectPrefixCount << "\n";
    sf.close();

    NS_LOG_UNCOND("Exported: " << routerFile);
    NS_LOG_UNCOND("Exported: " << summaryFile);

    Simulator::Destroy();
    return 0;
}
