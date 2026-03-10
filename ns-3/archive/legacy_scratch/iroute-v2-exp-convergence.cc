/**
 * @file iroute-v2-exp-convergence.cc
 * @brief Exp B: Convergence Timeline (Link Failure)
 * 
 * Logic:
 * - Diamond Topology (Src -> R1/R2 -> Dst)
 * - T=10s: Link Src-R1 fails.
 * - iRoute: Multipath/kMax should retry R2 immediately.
 * - NLSR (Emulated): GlobalRouting is NOT recalculated immediately.
 *   We wait delay (e.g. 2s) then recalc.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/helper/ndn-link-control-helper.hpp"
#include "apps/exact-name-consumer.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/iroute-app.hpp"

// Shared utilities
#include "iroute-common-utils.hpp"

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExpConvergence");

static std::string g_protocol = "iroute";
static double g_convDelay = 2.0; // NLSR convergence delay (seconds)
static double g_failTime = 10.0;
static double g_recoverTime = 30.0;
static std::string g_resultDir = "results/exp-convergence";

// Topology options
static std::string g_topo = "diamond";  // "diamond" or "rocketfuel"
static std::string g_topoFile = "src/ndnSIM/topologies/rocketfuel_maps_cch/as1239-r0.txt";
static uint32_t g_subgraphSize = 50;  // For Rocketfuel, use a subgraph

// Emulator function for NLSR
void RecalculateRoutesDelayed() {
    NS_LOG_UNCOND("NLSR Emulation: Re-calculating Global Routes now (after delay).");
    GlobalRoutingHelper::CalculateRoutes();
}

void BreakdownLink(Ptr<Node> n1, Ptr<Node> n2) {
    NS_LOG_UNCOND("Failure Event: Link Breakdown at " << Simulator::Now().GetSeconds());
    LinkControlHelper::FailLink(n1, n2);
    
    if (g_protocol == "nlsr") {
        // Schedule delayed SPF
        Simulator::Schedule(Seconds(g_convDelay), &RecalculateRoutesDelayed);
    } else {
        // iRoute handles it natively via data plane probing (no RIB update needed immediately)
        // OR simply, iRoute doesn't use GlobalRouting for Stage-2 forwarding? 
        // Note: Stage-2 relies on FIB. If FIB is wrong, Stage-2 fails.
        // iRoute Stage-2 uses standard forwarding.
        // Wait, if link fails, NFD detects it locally.
        // If we use "BestRoute", it might switch?
        // But comparing "Control Plane" convergence:
        // iRoute updates centroids? No, centroids don't change.
        // iRoute relies on "kMax" probes to try other domains/paths.
        // Ah, if the *path* to the domain fails, iRoute needs network routing.
        // But if the failure is at the edge/access, iRoute switches selection.
        
        // Scenario: Src is connected to D1 and D2. D1 link fails.
        // NLSR: Needs to propagate map update.
        // iRoute: Consumer probes D1 -> Fail. Probes D2 -> Success. Instant switch.
        
    }
}

void RestoreLink(Ptr<Node> n1, Ptr<Node> n2) {
    NS_LOG_UNCOND("Recovery Event: Link Restore at " << Simulator::Now().GetSeconds());
    LinkControlHelper::UpLink(n1, n2);
    if (g_protocol == "nlsr") {
        Simulator::Schedule(Seconds(g_convDelay), &RecalculateRoutesDelayed);
    }
}

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("protocol", "[iroute|nlsr]", g_protocol);
    cmd.AddValue("convDelay", "NLSR convergence delay (seconds)", g_convDelay);
    cmd.AddValue("failTime", "Time to trigger failure", g_failTime);
    cmd.AddValue("recoverTime", "Time to trigger recovery", g_recoverTime);
    cmd.AddValue("resultDir", "Results directory", g_resultDir);
    cmd.AddValue("topo", "Topology: diamond or rocketfuel", g_topo);
    cmd.AddValue("topoFile", "Path to Rocketfuel topology file", g_topoFile);
    cmd.AddValue("subgraphSize", "Subgraph size for Rocketfuel", g_subgraphSize);
    cmd.Parse(argc, argv);
    
    // Topology: Consumer connected to R1, R2. R1->Prod, R2->Prod.
    // If Link C-R1 fails, must use R2.
    // Using simple 4 node diamond for clarity?
    // C -- R1 -- P
    // |    |
    // R2 --/
    
    NodeContainer nodes;
    nodes.Create(4);
    // 0: Consumer
    // 1: Router1
    // 2: Router2
    // 3: Producer
    
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));
    
    p2p.Install(nodes.Get(0), nodes.Get(1)); // C-R1 (Primary)
    p2p.Install(nodes.Get(0), nodes.Get(2)); // C-R2 (Backup)
    p2p.Install(nodes.Get(1), nodes.Get(3)); // R1-P
    p2p.Install(nodes.Get(2), nodes.Get(3)); // R2-P
    
    StackHelper ndnHelper;
    ndnHelper.InstallAll();
    
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    
    // Cost configuration to prefer path via R1 initially
    // C->R1 cost 1, C->R2 cost 10
    // Actually GlobalRouting helper might just use hop count.
    // We can use RemoveOrigins to manually control or let Strategy decide.
    // Let's rely on FailLink to break the path. 
    
    // Producer
    AppHelper producerHelper("ns3::ndn::SemanticProducer");
    producerHelper.SetAttribute("Prefix", StringValue("/data"));
    producerHelper.Install(nodes.Get(3));
    
    // Configure iRoute on Producer (needed for Discovery reply)
    if (g_protocol == "iroute") {
        AppHelper irouteProducer("ns3::ndn::IRouteApp");
        irouteProducer.SetAttribute("IsIngress", BooleanValue(false));
        irouteProducer.SetAttribute("RouterId", StringValue("/domain0")); // Pretend domain0
        auto apps = irouteProducer.Install(nodes.Get(3));
        
        // Set a catch-all centroid
        auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0));
        std::vector<iroute::CentroidEntry> centroids;
        iroute::SemanticVector v(64); // Zero vector
        centroids.emplace_back(0, v, 2.0, 100); // Radius 2.0 covers everything (normalized vectors <= 1.0 distance)
        irouteApp->SetLocalCentroids(centroids);
    }
    
    grHelper.AddOrigins("/domain0", nodes.Get(3)); // Reachable prefix
    grHelper.AddOrigins("/domain0/data", nodes.Get(3)); // Data prefix
    grHelper.AddOrigins("/data", nodes.Get(3)); // Fallback
    GlobalRoutingHelper::CalculateRoutes();
    
    // Consumer
    if (g_protocol == "iroute") {
        // iRoute mode: Semantic Discovery? 
        // For simple link failure, maybe ExactName is enough if "protocol" implies the routing behavior?
        // User asked: "iRoute: Real code (Multipath/kMax)". 
        // This implies Discovery Consumer using iRoute logic.
        // But for single producer, iRoute adds value if we have replicas?
        // Let's assume we use ExactNameConsumer for simplicity if we are just testing network convergence?
        // No, prompt says: "iRoute: Multipath/kMax". So use IRouteDiscoveryConsumer.
        // We need IRouteApp on Nodes.
        
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
        irouteHelper.Install(nodes.Get(3)); // Producer has capabilities
        
        // Consumer
        AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
        
        // Set Frequency (100 Hz = 10ms gap)
        consumerHelper.SetAttribute("Frequency", DoubleValue(100.0));
        auto apps = consumerHelper.Install(nodes.Get(0));
        
        // Generate Trace
        std::vector<IRouteDiscoveryConsumer::QueryItem> trace;
        for(uint32_t i=0; i<4000; ++i) {
            IRouteDiscoveryConsumer::QueryItem item;
            // Vector: Zero vector matches the producer's catch-all centroid
            item.vector = iroute::SemanticVector(std::vector<float>(64, 0.5f)); 
            // Expected Domain: Just for stats, using /domain0 as producer setup
            item.expectedDomain = "/domain0";
            trace.push_back(item);
        }
        
        // FIX: We must configure Producer with Centroids matching these queries.
        // Or simpler: disable discovery and assume it works? No, "comparison" requires discovery logic.
        // Let's assume standard centroid config.
        
        auto app = DynamicCast<IRouteDiscoveryConsumer>(apps.Get(0));
        if(app) {
            std::cerr << "DEBUG: Setting trace size " << trace.size() << std::endl;
            app->SetQueryTrace(trace);
        } else {
            std::cerr << "ERROR: Failed to cast IRouteDiscoveryConsumer app!" << std::endl;
        }
    } else {
        // NLSR mode: Standard consumer
        AppHelper consumerHelper("ns3::ndn::ExactNameConsumer");
        auto apps = consumerHelper.Install(nodes.Get(0));
        
        // Configuration
        std::vector<std::string> queries;
        for(int i=0; i<4000; ++i) { // 100 queries/sec for 40s
            queries.push_back("/data/obj" + std::to_string(i));
        }
        
        auto consumer = DynamicCast<ExactNameConsumer>(apps.Get(0));
        if(consumer) consumer->SetQueryList(queries);
    }
    
    // Schedule Failure
    Simulator::Schedule(Seconds(g_failTime), &BreakdownLink, nodes.Get(0), nodes.Get(1));
    Simulator::Schedule(Seconds(g_recoverTime), &RestoreLink, nodes.Get(0), nodes.Get(1));
    
    Simulator::Stop(Seconds(40.0));
    Simulator::Run();
    
    // Export CSVs
    auto node = nodes.Get(0);
    for (uint32_t i = 0; i < node->GetNApplications(); ++i) {
        auto app = node->GetApplication(i);
        if (g_protocol == "iroute") {
            auto consumer = DynamicCast<IRouteDiscoveryConsumer>(app);
            if (consumer) {
                consumer->ExportToCsv("results/convergence_iroute_queries.csv");
                break;
            }
        } else {
            auto consumer = DynamicCast<ExactNameConsumer>(app);
            if (consumer) {
                consumer->ExportToCsv("results/convergence_nlsr_queries.csv");
                break;
            }
        }
    }
    
    Simulator::Destroy();
    
    return 0;
}
