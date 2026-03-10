/**
 * @file iroute-v2-exp-scalability.cc
 * @brief Exp A: Protocol Overhead Scalability (iRoute vs NLSR)
 * 
 * Compares control plane overhead:
 * 1. iRoute: Real semantic LSA exchange via RouteManager.
 * 2. NLSR: Simulated prefix-LSA flooding via NlsrOverheadApp.
 * 
 * Usage:
 *   ./waf --run "iroute-v2-exp-scalability --protocol=iroute --domains=20"
 *   ./waf --run "iroute-v2-exp-scalability --protocol=nlsr --domains=20"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-grid.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "apps/nlsr-overhead-app.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

#include <random>
#include <fstream>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteExpScalability");

// Parameters
static std::string g_protocol = "iroute"; // iroute or nlsr
static uint32_t g_domains = 20;
static double g_simTime = 60.0; 
static uint32_t g_prefixesPerNode = 50; // For NLSR size calc
static uint32_t g_M = 4; // iRoute centroids
static uint32_t g_vectorDim = 64;
static std::string g_resultDir = "results/exp_scalability";

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("protocol", "Protocol to test [iroute|nlsr]", g_protocol);
    cmd.AddValue("domains", "Number of domains (Grid size N)", g_domains);
    cmd.AddValue("simTime", "Simulation time", g_simTime);
    cmd.AddValue("prefixes", "Prefixes per node (for NLSR)", g_prefixesPerNode);
    cmd.AddValue("resultDir", "Result directory", g_resultDir);
    cmd.Parse(argc, argv);
    
    // Create Grid Topology (approx sqrt(N) x sqrt(N))
    int gridSize = std::ceil(std::sqrt(g_domains));
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));
    
    PointToPointGridHelper grid(gridSize, gridSize, p2p);
    grid.BoundingBox(0,0, 100,100);
    
    // Install stack
    StackHelper ndnHelper;
    // For NLSR, we need multicast strategy for /local-hop/nlsr to simulate flooding
    ndnHelper.InstallAll();
    
    // Global Routing Helper (for base connectivity if needed, but here we measure overhead)
    GlobalRoutingHelper grHelper;
    grHelper.InstallAll();
    
    // Install Protocol Apps
    if (g_protocol == "iroute") {
        NS_LOG_UNCOND("Setting up iRoute for " << g_domains << " domains...");
        
        StrategyChoiceHelper::InstallAll("/iroute", "/localhost/nfd/strategy/multicast");
        
        AppHelper appHelper("ns3::ndn::IRouteApp");
        appHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        appHelper.SetAttribute("LsaInterval", TimeValue(Seconds(10.0))); // Frequent updates for test
        
        // Install on first N nodes (grid might have more)
        for (uint32_t i = 0; i < g_domains; ++i) {
            Ptr<Node> node = grid.GetNode(i % gridSize, i / gridSize);
            std::string routerId = "/domain" + std::to_string(i);
            
            appHelper.SetAttribute("RouterId", StringValue(routerId));
            appHelper.SetAttribute("IsIngress", BooleanValue(false)); // Just peers
            auto apps = appHelper.Install(node);
            
            // Set dummy centroids to force LSA generation
            auto iroute = DynamicCast<IRouteApp>(apps.Get(0));
            std::vector<iroute::CentroidEntry> centroids;
            for(uint32_t m=0; m<g_M; ++m) {
                // Just 0 vectors, size matters
                iroute::SemanticVector v(g_vectorDim); 
                centroids.emplace_back(m, v, 0.5, 100);
            }
            iroute->SetLocalCentroids(centroids);
        }
        
    } else if (g_protocol == "nlsr") {
        NS_LOG_UNCOND("Setting up NLSR Baseline for " << g_domains << " domains...");
        
        // Critical: Set Multicast Strategy for /local-hop/nlsr
        StrategyChoiceHelper::InstallAll("/local-hop/nlsr", "/localhost/nfd/strategy/multicast");
        
        AppHelper appHelper("ns3::ndn::NlsrOverheadApp");
        appHelper.SetAttribute("LsaInterval", TimeValue(Seconds(10.0)));
        appHelper.SetAttribute("Prefixes", UintegerValue(g_prefixesPerNode));
        appHelper.SetAttribute("FixedHeader", UintegerValue(64));
        
        for (uint32_t i = 0; i < g_domains; ++i) {
            Ptr<Node> node = grid.GetNode(i % gridSize, i / gridSize);
            appHelper.SetAttribute("NodeId", UintegerValue(i));
            appHelper.Install(node);
        }
    }
    
    // Unique trace file for post-processing
    std::string traceFile = g_resultDir + "/trace_" + g_protocol + "_" + std::to_string(g_domains) + ".txt";
    L3RateTracer::InstallAll(traceFile, Seconds(1.0));
    
    Simulator::Stop(Seconds(g_simTime));
    Simulator::Run();
    
    // Write Summary CSV (Metadata)
    std::ofstream summary(g_resultDir + "/scalability_summary.csv", std::ios::app);
    // bytes will be calculated by python from traceFile
    summary << g_domains << "," << g_protocol << "," << "0" << "," << traceFile << "\n";
    summary.close();
    
    Simulator::Destroy();
    return 0;
}
