/**
 * @file iroute-v2-exp10-convergence.cc
 * @brief Exp10: Packet-level convergence with LSA dissemination (Rocketfuel)
 *
 * Convergence criterion:
 *   t_conv = max_router time(router LSDB complete for all domains @ latest seq)
 *
 * We run packet-level LSA dissemination for:
 *   1) iRoute Semantic-LSA (real centroids)
 *   2) NLSR-style LSA flooding with matched payload size
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "extensions/iroute-route-manager-registry.hpp"
#include "iroute-common-utils.hpp"

#include <random>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <queue>
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <numeric>
#include <cmath>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExp10Convergence");

// =============================================================================
// Global Parameters
// =============================================================================
static uint32_t g_seed = 42;
static uint32_t g_run = 1;
static double g_simTime = 40.0;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_M = 4;
static double g_lsaPeriod = 1000.0;          // Large to avoid periodic seq increments
static double g_lsaFetchInterval = 1.0;
static double g_initialPublishTime = 1.0;
static double g_updateTime = 10.0;
static double g_checkInterval = 0.2;
static std::string g_resultDir = "results/exp10";

// Rocketfuel subgraph
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_topoSize = 150;
static uint32_t g_ingressNodeId = 0;
static double g_linkDelayMs = 2.0;
static std::string g_linkDataRate = "1Gbps";

// Real data files
static std::string g_centroidsFile = "dataset/trec_dl_combined_dim128/domain_centroids.csv";
static std::string g_contentFile = "dataset/trec_dl_combined_dim128/producer_content.csv";

// =============================================================================
// Rocketfuel subgraph builder (from Exp6)
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
// Minimal NLSR-style LSA App (pull-based flooding)
// =============================================================================

namespace ns3 {
namespace ndn {

class NlsrLsaApp : public App
{
public:
    static TypeId GetTypeId();
    NlsrLsaApp();

    void SetKnownDomains(const std::vector<Name>& domains);
    void PublishLsa();
    uint64_t GetLastSeq(const std::string& domainUri) const;
    uint64_t GetTxBytesTotal() const { return m_txBytesTotal; }
    uint64_t GetRxBytesTotal() const { return m_rxBytesTotal; }

protected:
    void StartApplication() override;
    void StopApplication() override;
    void OnInterest(shared_ptr<const Interest> interest) override;
    void OnData(shared_ptr<const Data> data) override;

private:
    void FetchLsas();

private:
    std::string m_routerId;
    Name m_domainPrefix;
    bool m_isDomain = false;
    bool m_enablePolling = true;
    uint32_t m_payloadSize = 1000;
    uint64_t m_seqNo = 0;

    std::vector<Name> m_knownDomains;
    std::map<std::string, uint64_t> m_lastSeqByDomain;

    Time m_fetchInterval;
    EventId m_fetchEvent;
    Ptr<UniformRandomVariable> m_jitter;
    ::ndn::KeyChain m_keyChain;

    uint64_t m_txBytesTotal = 0;
    uint64_t m_rxBytesTotal = 0;
};

TypeId
NlsrLsaApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::NlsrLsaApp")
        .SetParent<App>()
        .AddConstructor<NlsrLsaApp>()
        .AddAttribute("RouterId",
                      "Router ID string",
                      StringValue("/router"),
                      MakeStringAccessor(&NlsrLsaApp::m_routerId),
                      MakeStringChecker())
        .AddAttribute("IsDomain",
                      "Whether this node is a domain producer",
                      BooleanValue(false),
                      MakeBooleanAccessor(&NlsrLsaApp::m_isDomain),
                      MakeBooleanChecker())
        .AddAttribute("EnablePolling",
                      "Enable pull-based polling",
                      BooleanValue(true),
                      MakeBooleanAccessor(&NlsrLsaApp::m_enablePolling),
                      MakeBooleanChecker())
        .AddAttribute("PayloadSize",
                      "LSA payload size (bytes)",
                      UintegerValue(1000),
                      MakeUintegerAccessor(&NlsrLsaApp::m_payloadSize),
                      MakeUintegerChecker<uint32_t>(1))
        .AddAttribute("FetchInterval",
                      "Polling interval",
                      TimeValue(Seconds(1.0)),
                      MakeTimeAccessor(&NlsrLsaApp::m_fetchInterval),
                      MakeTimeChecker());

    return tid;
}

NlsrLsaApp::NlsrLsaApp()
    : m_fetchInterval(Seconds(1.0))
{
    m_jitter = CreateObject<UniformRandomVariable>();
}

void
NlsrLsaApp::StartApplication()
{
    App::StartApplication();

    m_domainPrefix = Name(m_routerId);

    if (m_isDomain) {
        FibHelper::AddRoute(GetNode(), m_domainPrefix, m_face, 0);
    }

    if (m_active && m_enablePolling && !m_knownDomains.empty()) {
        Time delay = MilliSeconds(100 + m_jitter->GetValue() * 50);
        m_fetchEvent = Simulator::Schedule(delay, &NlsrLsaApp::FetchLsas, this);
    }
}

void
NlsrLsaApp::StopApplication()
{
    Simulator::Cancel(m_fetchEvent);
    App::StopApplication();
}

void
NlsrLsaApp::SetKnownDomains(const std::vector<Name>& domains)
{
    m_knownDomains = domains;
    for (const auto& d : domains) {
        m_lastSeqByDomain[d.toUri()] = 0;
    }

    if (m_active && m_enablePolling && !m_knownDomains.empty()) {
        Time delay = MilliSeconds(100 + m_jitter->GetValue() * 50);
        m_fetchEvent = Simulator::Schedule(delay, &NlsrLsaApp::FetchLsas, this);
    }
}

void
NlsrLsaApp::PublishLsa()
{
    if (!m_isDomain || !m_active) return;
    ++m_seqNo;
    m_lastSeqByDomain[m_domainPrefix.toUri()] = m_seqNo;
}

uint64_t
NlsrLsaApp::GetLastSeq(const std::string& domainUri) const
{
    auto it = m_lastSeqByDomain.find(domainUri);
    if (it != m_lastSeqByDomain.end()) return it->second;
    return 0;
}

void
NlsrLsaApp::FetchLsas()
{
    if (!m_active || !m_enablePolling) return;

    for (size_t idx = 0; idx < m_knownDomains.size(); ++idx) {
        const auto& domain = m_knownDomains[idx];
        std::string domainUri = domain.toUri();
        uint64_t nextSeq = m_lastSeqByDomain[domainUri] + 1;

        Name lsaName(domain);
        lsaName.append("nlsr").append("lsa").appendNumber(nextSeq);

        Time delay = MilliSeconds(idx * 10 + m_jitter->GetValue() * 50);
        Simulator::Schedule(delay, [this, lsaName]() {
            if (!m_active) return;
            auto interest = std::make_shared<Interest>(lsaName);
            interest->setCanBePrefix(false);
            interest->setMustBeFresh(true);
            interest->setInterestLifetime(::ndn::time::milliseconds(2000));
            interest->wireEncode();

            m_txBytesTotal += interest->wireEncode().size();

            m_transmittedInterests(interest, this, m_face);
            m_appLink->onReceiveInterest(*interest);
        });
    }

    m_fetchEvent = Simulator::Schedule(m_fetchInterval, &NlsrLsaApp::FetchLsas, this);
}

void
NlsrLsaApp::OnInterest(shared_ptr<const Interest> interest)
{
    App::OnInterest(interest);
    if (!m_isDomain || !m_active) return;

    const Name& name = interest->getName();
    if (!m_domainPrefix.isPrefixOf(name)) return;

    // Expect: /<domain>/nlsr/lsa/<seq>
    if (name.size() < m_domainPrefix.size() + 3) return;
    if (name.get(m_domainPrefix.size()).toUri() != "nlsr") return;
    if (name.get(m_domainPrefix.size() + 1).toUri() != "lsa") return;

    uint64_t seq = 0;
    try {
        seq = name.get(-1).toNumber();
    } catch (...) {
        return;
    }

    if (seq != m_seqNo) return;

    interest->wireEncode();
    m_rxBytesTotal += interest->wireEncode().size();

    auto data = std::make_shared<Data>(name);
    data->setFreshnessPeriod(::ndn::time::seconds(2));

    auto buffer = std::make_shared<::ndn::Buffer>(m_payloadSize);
    data->setContent(buffer);
    m_keyChain.sign(*data);
    data->wireEncode();

    m_txBytesTotal += data->wireEncode().size();

    m_transmittedDatas(data, this, m_face);
    m_appLink->onReceiveData(*data);
}

void
NlsrLsaApp::OnData(shared_ptr<const Data> data)
{
    App::OnData(data);

    data->wireEncode();
    m_rxBytesTotal += data->wireEncode().size();

    const Name& name = data->getName();
    if (name.size() < 4) return;

    Name domainPrefix = name.getPrefix(1); // /domainX
    uint64_t seq = 0;
    try {
        seq = name.get(-1).toNumber();
    } catch (...) {
        return;
    }

    std::string domainUri = domainPrefix.toUri();
    if (seq > m_lastSeqByDomain[domainUri]) {
        m_lastSeqByDomain[domainUri] = seq;
    }
}

} // namespace ndn
} // namespace ns3

namespace ns3 {
namespace ndn {
NS_OBJECT_ENSURE_REGISTERED(NlsrLsaApp);
} // namespace ndn
} // namespace ns3

// =============================================================================
// Convergence tracking
// =============================================================================

static std::vector<Name> g_knownDomains;
static std::vector<uint32_t> g_nodeIds;
static std::vector<Ptr<IRouteApp>> g_irouteApps;
static std::vector<Ptr<NlsrLsaApp>> g_nlsrApps;
static uint64_t g_targetSeq = 2;
static std::unordered_set<uint32_t> g_activeDomainIds;

static std::map<uint32_t, double> g_irouteConvTime;
static std::map<uint32_t, double> g_nlsrConvTime;
static double g_irouteConvMs = -1.0;
static double g_nlsrConvMs = -1.0;

bool IsRouterConvergedIroute(uint32_t nodeId)
{
    auto rm = iroute::RouteManagerRegistry::get(nodeId);
    if (!rm) return false;

    for (const auto& d : g_knownDomains) {
        auto opt = rm->getDomain(d);
        if (!opt.has_value()) return false;
        if (opt->seqNo < g_targetSeq) return false;
    }
    return true;
}

void CheckIrouteConvergence()
{
    bool all = true;
    double now = Simulator::Now().GetSeconds();

    for (size_t i = 0; i < g_nodeIds.size(); ++i) {
        uint32_t nodeId = g_nodeIds[i];
        if (g_irouteConvTime.count(nodeId)) continue;
        if (IsRouterConvergedIroute(nodeId)) {
            g_irouteConvTime[nodeId] = now;
        } else {
            all = false;
        }
    }

    if (!all) {
        Simulator::Schedule(Seconds(g_checkInterval), &CheckIrouteConvergence);
        return;
    }

    double maxT = 0.0;
    for (const auto& kv : g_irouteConvTime) {
        if (kv.second > maxT) maxT = kv.second;
    }
    g_irouteConvMs = (maxT - g_updateTime) * 1000.0;
}

bool IsRouterConvergedNlsr(size_t idx)
{
    if (idx >= g_nlsrApps.size() || !g_nlsrApps[idx]) return false;
    auto app = g_nlsrApps[idx];
    for (const auto& d : g_knownDomains) {
        if (app->GetLastSeq(d.toUri()) < g_targetSeq) return false;
    }
    return true;
}

void CheckNlsrConvergence()
{
    bool all = true;
    double now = Simulator::Now().GetSeconds();

    for (size_t i = 0; i < g_nodeIds.size(); ++i) {
        uint32_t nodeId = g_nodeIds[i];
        if (g_nlsrConvTime.count(nodeId)) continue;
        if (IsRouterConvergedNlsr(i)) {
            g_nlsrConvTime[nodeId] = now;
        } else {
            all = false;
        }
    }

    if (!all) {
        Simulator::Schedule(Seconds(g_checkInterval), &CheckNlsrConvergence);
        return;
    }

    double maxT = 0.0;
    for (const auto& kv : g_nlsrConvTime) {
        if (kv.second > maxT) maxT = kv.second;
    }
    g_nlsrConvMs = (maxT - g_updateTime) * 1000.0;
}

// Perturb centroids to force publish
void PerturbCentroids(Ptr<IRouteApp> app)
{
    if (!app) return;
    auto centroids = app->GetLocalCentroids();
    if (centroids.empty()) return;

    std::normal_distribution<float> noise(0.0f, 0.01f);
    std::mt19937 rng(g_seed + g_run);

    for (auto& c : centroids) {
        auto data = c.C.getData();
        if (data.empty()) continue;
        for (auto& v : data) v += noise(rng);
        iroute::SemanticVector sv(data);
        sv.normalize();
        c.C = sv;
    }

    app->SetLocalCentroids(centroids);
}

void DoInitialPublish()
{
    for (size_t i = 0; i < g_irouteApps.size(); ++i) {
        auto app = g_irouteApps[i];
        if (!app) continue;
        std::string rid = app->GetRouterId();
        if (rid.find("/domain") == 0) {
            try {
                uint32_t d = std::stoul(rid.substr(7));
                if (g_activeDomainIds.count(d)) {
                    app->TriggerLsaPublish();
                }
            } catch (...) {}
        }
    }
    for (size_t i = 0; i < g_nlsrApps.size(); ++i) {
        auto app = g_nlsrApps[i];
        if (!app) continue;
        app->PublishLsa();
    }
}

void DoUpdatePublish()
{
    for (size_t i = 0; i < g_irouteApps.size(); ++i) {
        auto app = g_irouteApps[i];
        if (!app) continue;
        std::string rid = app->GetRouterId();
        if (rid.find("/domain") == 0) {
            try {
                uint32_t d = std::stoul(rid.substr(7));
                if (g_activeDomainIds.count(d)) {
                    PerturbCentroids(app);
                    app->TriggerLsaPublish();
                }
            } catch (...) {}
        }
    }
    for (size_t i = 0; i < g_nlsrApps.size(); ++i) {
        auto app = g_nlsrApps[i];
        if (!app) continue;
        app->PublishLsa();
    }

    Simulator::Schedule(Seconds(g_checkInterval), &CheckIrouteConvergence);
    Simulator::Schedule(Seconds(g_checkInterval), &CheckNlsrConvergence);
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
    cmd.AddValue("lsaPeriod", "LSA period (large to avoid periodic)", g_lsaPeriod);
    cmd.AddValue("lsaFetchInterval", "LSA fetch interval", g_lsaFetchInterval);
    cmd.AddValue("initialPublishTime", "Initial publish time", g_initialPublishTime);
    cmd.AddValue("updateTime", "Update time", g_updateTime);
    cmd.AddValue("checkInterval", "Convergence check interval", g_checkInterval);
    cmd.AddValue("resultDir", "Results directory", g_resultDir);
    cmd.AddValue("topoFile", "Rocketfuel topology file", g_topoFile);
    cmd.AddValue("topoSize", "Rocketfuel subgraph size", g_topoSize);
    cmd.AddValue("ingressNodeId", "Ingress node id (0=auto)", g_ingressNodeId);
    cmd.AddValue("linkDelayMs", "Link delay (ms)", g_linkDelayMs);
    cmd.AddValue("linkDataRate", "Link data rate", g_linkDataRate);
    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv", g_centroidsFile);
    cmd.AddValue("contentFile", "Path to producer_content.csv", g_contentFile);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(g_seed);
    RngSeedManager::SetRun(g_run);

    auto topo = BuildRocketfuelSubgraph(g_topoFile, g_topoSize, g_domains,
                                        g_ingressNodeId, g_linkDelayMs, g_linkDataRate);

    if (topo.domainNodes.size() < g_domains) {
        g_domains = topo.domainNodes.size();
    }

    NS_LOG_UNCOND("=== Exp10: Convergence (Rocketfuel) ===");
    NS_LOG_UNCOND("nodes=" << topo.nodes.GetN() << ", links=" << topo.numLinks
                  << ", domains=" << g_domains << ", M=" << g_M
                  << ", vectorDim=" << g_vectorDim);

    // Load centroids
    auto domainCentroids = iroute::utils::LoadCentroidsFromCsv(g_centroidsFile);
    if (domainCentroids.empty()) {
        NS_FATAL_ERROR("Failed to load centroids: " << g_centroidsFile);
    }
    uint32_t maxDomainId = 0;
    for (const auto& kv : domainCentroids) {
        if (kv.first > maxDomainId) maxDomainId = kv.first;
    }
    if (maxDomainId + 1 < g_domains) {
        g_domains = maxDomainId + 1;
    }
    if (g_domains > topo.domainNodes.size()) {
        g_domains = topo.domainNodes.size();
    }

    // Load content for NLSR payload sizing
    auto contentMap = iroute::utils::LoadContentFromCsv(g_contentFile);

    // Prepare known domains list (only domains with centroids)
    g_knownDomains.clear();
    g_activeDomainIds.clear();
    for (uint32_t d = 0; d < g_domains; ++d) {
        if (domainCentroids.count(d)) {
            g_knownDomains.emplace_back("/domain" + std::to_string(d));
            g_activeDomainIds.insert(d);
        }
    }

    // Install NDN stack
    StackHelper ndnHelper;
    ndnHelper.InstallAll();
    StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");
    StrategyChoiceHelper::InstallAll("/ndn/broadcast", "/localhost/nfd/strategy/multicast");

    iroute::RouteManagerRegistry::clear();
    IRouteApp::ResetLsaCounter();

    // Hop distances for cost
    auto hopMap = iroute::utils::BFSAllDistances(topo.ingress);

    // Install IRouteApp on all nodes
    AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
    irouteHelper.SetAttribute("SemVerId", UintegerValue(1));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(g_lsaPeriod)));
    irouteHelper.SetAttribute("LsaFetchInterval", TimeValue(Seconds(g_lsaFetchInterval)));
    irouteHelper.SetAttribute("EnableLsaPolling", BooleanValue(true));

    g_irouteApps.resize(topo.nodes.GetN());
    g_nodeIds.clear();

    for (uint32_t i = 0; i < topo.nodes.GetN(); ++i) {
        Ptr<Node> node = topo.nodes.Get(i);
        g_nodeIds.push_back(node->GetId());

        bool isDomain = topo.nodeIdToDomain.count(node->GetId()) > 0;
        std::string routerId = "/router" + std::to_string(node->GetId());
        if (isDomain) {
            uint32_t d = topo.nodeIdToDomain[node->GetId()];
            routerId = "/domain" + std::to_string(d);
        }

        irouteHelper.SetAttribute("RouterId", StringValue(routerId));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(node == topo.ingress));

        double cost = 1.0;
        if (hopMap.count(node->GetId())) cost = static_cast<double>(hopMap[node->GetId()]);
        irouteHelper.SetAttribute("RouteCost", DoubleValue(cost));

        auto apps = irouteHelper.Install(node);
        auto app = DynamicCast<IRouteApp>(apps.Get(0));
        g_irouteApps[i] = app;

        if (isDomain && app) {
            uint32_t d = topo.nodeIdToDomain[node->GetId()];
            if (domainCentroids.count(d)) {
                app->SetLocalCentroids(domainCentroids[d]);
            }
        }
    }

    for (auto& app : g_irouteApps) {
        if (app) app->SetKnownDomains(g_knownDomains);
    }

    // Global routing for domain prefixes
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    for (uint32_t d = 0; d < g_domains; ++d) {
        if (d < topo.domainNodes.size()) {
            grHelper.AddOrigins("/domain" + std::to_string(d), topo.domainNodes[d]);
        }
    }
    GlobalRoutingHelper::CalculateRoutes();

    // NLSR payload sizing (approx)
    uint32_t objectsPerDomain = 1000;
    if (!contentMap.empty()) {
        uint64_t totalDocs = 0;
        uint32_t domCount = 0;
        for (uint32_t d = 0; d < g_domains; ++d) {
            if (contentMap.count(d)) {
                totalDocs += contentMap[d].size();
                domCount++;
            }
        }
        if (domCount > 0) {
            objectsPerDomain = static_cast<uint32_t>(totalDocs / domCount);
        }
    }

    uint32_t nlsrPayloadBytes = objectsPerDomain * iroute::utils::NlsrStateEstimator::AVG_PREFIX_LSA_SIZE;
    uint32_t adjBytes = static_cast<uint32_t>(std::round(topo.avgLinksPerRouter)) * iroute::utils::NlsrStateEstimator::AVG_ADJ_LSA_SIZE;
    nlsrPayloadBytes += adjBytes;

    // Install NLSR LSA app on all nodes
    AppHelper nlsrHelper("ns3::ndn::NlsrLsaApp");
    nlsrHelper.SetAttribute("PayloadSize", UintegerValue(nlsrPayloadBytes));
    nlsrHelper.SetAttribute("FetchInterval", TimeValue(Seconds(g_lsaFetchInterval)));
    nlsrHelper.SetAttribute("EnablePolling", BooleanValue(true));

    g_nlsrApps.resize(topo.nodes.GetN());

    for (uint32_t i = 0; i < topo.nodes.GetN(); ++i) {
        Ptr<Node> node = topo.nodes.Get(i);
        bool isDomain = topo.nodeIdToDomain.count(node->GetId()) > 0;
        std::string routerId = "/router" + std::to_string(node->GetId());
        if (isDomain) {
            uint32_t d = topo.nodeIdToDomain[node->GetId()];
            routerId = "/domain" + std::to_string(d);
        }

        nlsrHelper.SetAttribute("RouterId", StringValue(routerId));
        nlsrHelper.SetAttribute("IsDomain", BooleanValue(isDomain));

        auto apps = nlsrHelper.Install(node);
        auto app = DynamicCast<NlsrLsaApp>(apps.Get(0));
        g_nlsrApps[i] = app;
    }

    for (auto& app : g_nlsrApps) {
        if (app) app->SetKnownDomains(g_knownDomains);
    }

    // Initial publish (seq=1)
    Simulator::Schedule(Seconds(g_initialPublishTime), &DoInitialPublish);

    // Update publish (seq=2) and start convergence checks
    Simulator::Schedule(Seconds(g_updateTime), &DoUpdatePublish);

    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();

    // Control bytes
    uint64_t irouteTxBytes = 0;
    uint64_t irouteRxBytes = 0;
    for (const auto& app : g_irouteApps) {
        if (!app) continue;
        irouteTxBytes += app->GetLsaTxBytesTotal();
        irouteRxBytes += app->GetLsaRxBytesTotal();
    }

    uint64_t nlsrTxBytes = 0;
    uint64_t nlsrRxBytes = 0;
    for (const auto& app : g_nlsrApps) {
        if (!app) continue;
        nlsrTxBytes += app->GetTxBytesTotal();
        nlsrRxBytes += app->GetRxBytesTotal();
    }

    mkdir(g_resultDir.c_str(), 0755);

    std::string summaryFile = g_resultDir + "/exp10_summary.csv";
    bool writeHeader = false;
    { std::ifstream check(summaryFile); writeHeader = !check.good(); }

    std::ofstream sf(summaryFile, std::ios::app);
    if (writeHeader) {
        sf << "topoFile,topoSize,nodes,domains,M,vectorDim,lsaPeriod,lsaFetchInterval,"
           << "initialPublishTime,updateTime,"
           << "irouteConvMs,nlsrConvMs,"
           << "irouteCtrlBytes,nlsrCtrlBytes,"
           << "irouteTxBytes,irouteRxBytes,nlsrTxBytes,nlsrRxBytes,"
           << "nlsrPayloadBytes,avgLinksPerRouter,objectsPerDomain\n";
    }

    sf << g_topoFile << "," << g_topoSize << "," << topo.nodes.GetN() << ","
       << g_domains << "," << g_M << "," << g_vectorDim << ","
       << g_lsaPeriod << "," << g_lsaFetchInterval << ","
       << g_initialPublishTime << "," << g_updateTime << ","
       << g_irouteConvMs << "," << g_nlsrConvMs << ","
       << (irouteTxBytes + irouteRxBytes) << "," << (nlsrTxBytes + nlsrRxBytes) << ","
       << irouteTxBytes << "," << irouteRxBytes << "," << nlsrTxBytes << "," << nlsrRxBytes << ","
       << nlsrPayloadBytes << "," << topo.avgLinksPerRouter << "," << objectsPerDomain << "\n";
    sf.close();

    std::ofstream rf(g_resultDir + "/exp10_router_times.csv");
    rf << "nodeId,irouteConvTimeSec,nlsrConvTimeSec\n";
    for (size_t i = 0; i < g_nodeIds.size(); ++i) {
        uint32_t nodeId = g_nodeIds[i];
        double it = g_irouteConvTime.count(nodeId) ? g_irouteConvTime[nodeId] : -1.0;
        double nt = g_nlsrConvTime.count(nodeId) ? g_nlsrConvTime[nodeId] : -1.0;
        rf << nodeId << "," << it << "," << nt << "\n";
    }
    rf.close();

    NS_LOG_UNCOND("Exp10 Done. iRoute conv(ms)=" << g_irouteConvMs
                  << ", NLSR conv(ms)=" << g_nlsrConvMs);

    Simulator::Destroy();
    return 0;
}
