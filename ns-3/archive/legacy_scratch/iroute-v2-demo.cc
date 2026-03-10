/**
 * @file iroute-v2-demo.cc
 * @brief Minimal demo of iRoute v2 two-stage discovery + fetch workflow.
 *
 * Topology: Simple 4-node chain
 *   Consumer <-> Ingress <-> Core <-> Producer
 *
 * Components:
 * - Consumer: IRouteDiscoveryConsumer (sends /iroute/query with semantic vector)
 * - Ingress: SemanticStrategy (ingress-only ranking) + IRouteApp (LSA)
 * - Core: best-route strategy + IRouteApp (LSA)
 * - Producer: SemanticProducer (handles Discovery + Fetch)
 *
 * @author iRoute Team
 * @date 2024
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

// iRoute includes
#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/semantic-producer.hpp"
#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ndn.IRouteV2Demo");

int
main(int argc, char* argv[])
{
    // Default parameters
    uint32_t vectorDim = 64;
    double simTime = 20.0;
    bool verbose = false;

    CommandLine cmd;
    cmd.AddValue("vectorDim", "Dimension of semantic vectors", vectorDim);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.Parse(argc, argv);

    if (verbose) {
        LogComponentEnable("ndn.IRouteV2Demo", LOG_LEVEL_INFO);
        LogComponentEnable("ndn.IRouteApp", LOG_LEVEL_INFO);
        LogComponentEnable("ndn.IRouteDiscoveryConsumer", LOG_LEVEL_INFO);
        LogComponentEnable("nfd.SemanticStrategy", LOG_LEVEL_INFO);
    }

    // Create nodes: Consumer, Ingress, Core, Producer
    NodeContainer nodes;
    nodes.Create(4);
    
    Ptr<Node> consumer = nodes.Get(0);
    Ptr<Node> ingress = nodes.Get(1);
    Ptr<Node> core = nodes.Get(2);
    Ptr<Node> producer = nodes.Get(3);

    // Connect nodes with point-to-point links
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    p2p.Install(consumer, ingress);
    p2p.Install(ingress, core);
    p2p.Install(core, producer);

    // Install NDN stack
    ndn::StackHelper ndnHelper;
    ndnHelper.SetDefaultRoutes(true);
    ndnHelper.InstallAll();

    // =========================================================================
    // Strategy Configuration
    // =========================================================================
    
    // Ingress node: SemanticStrategy for /iroute/query prefix
    ndn::StrategyChoiceHelper::Install(ingress, "/iroute/query",
        "/localhost/nfd/strategy/semantic/%FD%01");
    
    // All other nodes: best-route for all prefixes
    ndn::StrategyChoiceHelper::Install(consumer, "/",
        "/localhost/nfd/strategy/best-route/%FD%01");
    ndn::StrategyChoiceHelper::Install(core, "/",
        "/localhost/nfd/strategy/best-route/%FD%01");
    ndn::StrategyChoiceHelper::Install(producer, "/",
        "/localhost/nfd/strategy/best-route/%FD%01");

    // =========================================================================
    // Create Semantic Vectors
    // =========================================================================
    
    // Query vector (consumer's request)
    iroute::SemanticVector queryVector(vectorDim);
    std::vector<float> queryData(vectorDim);
    for (uint32_t i = 0; i < vectorDim; ++i) {
        queryData[i] = (i % 2 == 0) ? 0.1f : 0.15f;
    }
    queryVector.setData(queryData);
    queryVector.normalize();

    // Producer's centroid (content representation)
    iroute::SemanticVector producerCentroid(vectorDim);
    std::vector<float> prodData(vectorDim);
    for (uint32_t i = 0; i < vectorDim; ++i) {
        prodData[i] = (i % 2 == 0) ? 0.12f : 0.14f;  // Similar to query
    }
    producerCentroid.setData(prodData);
    producerCentroid.normalize();

    NS_LOG_INFO("Query-Producer similarity: " 
                << queryVector.computeCosineSimilarity(producerCentroid));

    // =========================================================================
    // Install Applications
    // =========================================================================

    // Producer: SemanticProducer
    ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
    producerHelper.SetPrefix("/producer");
    producerHelper.SetAttribute("VectorDim", UintegerValue(vectorDim));
    auto producerApp = producerHelper.Install(producer).Get(0);
    
    // Set producer's semantic centroid
    auto semanticProd = DynamicCast<ndn::SemanticProducer>(producerApp);
    if (semanticProd) {
        semanticProd->SetNodeVector(producerCentroid);
    }

    // IRouteApp on ingress, core, and producer for LSA distribution
    ndn::AppHelper irouteHelper("ns3::ndn::IRouteApp");
    irouteHelper.SetAttribute("VectorDim", UintegerValue(vectorDim));
    irouteHelper.SetAttribute("LsaInterval", TimeValue(Seconds(5.0)));

    // Ingress IRouteApp - P1-3: set IsIngress=true for query service
    irouteHelper.SetAttribute("RouterId", StringValue("ingress"));
    irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
    irouteHelper.Install(ingress);

    // Core IRouteApp  
    irouteHelper.SetAttribute("RouterId", StringValue("core"));
    irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
    irouteHelper.Install(core);

    // Producer IRouteApp with centroid
    irouteHelper.SetAttribute("RouterId", StringValue("producer"));
    irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
    auto prodIRouteApp = irouteHelper.Install(producer).Get(0);
    auto irouteApp = DynamicCast<ndn::IRouteApp>(prodIRouteApp);
    if (irouteApp) {
        irouteApp->SetLocalSemantics(producerCentroid);
        
        // Also set multi-centroid format
        iroute::CentroidEntry centroid(0, producerCentroid, 0.5, 100.0);
        irouteApp->AddCentroid(centroid);
    }

    // Consumer: IRouteDiscoveryConsumer
    ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
    consumerHelper.SetAttribute("VectorDim", UintegerValue(vectorDim));
    consumerHelper.SetAttribute("Frequency", DoubleValue(0.5));  // 1 query per 2 seconds
    auto consumerApp = consumerHelper.Install(consumer).Get(0);

    auto discoveryConsumer = DynamicCast<ndn::IRouteDiscoveryConsumer>(consumerApp);
    if (discoveryConsumer) {
        discoveryConsumer->SetQueryVector(queryVector);
    }

    // =========================================================================
    // Routing Configuration
    // =========================================================================
    
    // Add routes for LSA prefix (v2 pull-based)
    ndn::FibHelper::AddRoute(consumer, "/iroute/lsa", ingress, 1);
    ndn::FibHelper::AddRoute(ingress, "/iroute/lsa", core, 1);
    ndn::FibHelper::AddRoute(core, "/iroute/lsa", producer, 1);

    // Add routes for legacy LSA broadcast prefix (bidirectional flooding)
    ndn::FibHelper::AddRoute(producer, "/ndn/broadcast/iroute/lsa", core, 1);
    ndn::FibHelper::AddRoute(core, "/ndn/broadcast/iroute/lsa", ingress, 1);
    ndn::FibHelper::AddRoute(ingress, "/ndn/broadcast/iroute/lsa", consumer, 1);
    // Reverse direction for full flooding
    ndn::FibHelper::AddRoute(consumer, "/ndn/broadcast/iroute/lsa", ingress, 1);
    ndn::FibHelper::AddRoute(ingress, "/ndn/broadcast/iroute/lsa", core, 1);
    ndn::FibHelper::AddRoute(core, "/ndn/broadcast/iroute/lsa", producer, 1);

    // Add routes for producer prefix
    ndn::FibHelper::AddRoute(consumer, "/producer", ingress, 1);
    ndn::FibHelper::AddRoute(ingress, "/producer", core, 1);
    ndn::FibHelper::AddRoute(core, "/producer", producer, 1);

    // Add routes for discovery prefix (consumer -> ingress for semantic lookup)
    ndn::FibHelper::AddRoute(consumer, "/iroute/query", ingress, 1);

    // =========================================================================
    // Run Simulation
    // =========================================================================

    Simulator::Stop(Seconds(simTime));

    NS_LOG_INFO("Starting iRoute V2 Demo simulation...");
    std::cout << "\n=== iRoute V2 Two-Stage Demo ===" << std::endl;
    std::cout << "Topology: Consumer <-> Ingress <-> Core <-> Producer" << std::endl;
    std::cout << "Vector dim: " << vectorDim << std::endl;
    std::cout << "Sim time: " << simTime << "s" << std::endl;
    std::cout << "================================\n" << std::endl;

    Simulator::Run();

    // Print final stats
    std::cout << "\n=== Simulation Complete ===" << std::endl;
    
    if (discoveryConsumer) {
        discoveryConsumer->PrintStats();
    }

    Simulator::Destroy();

    return 0;
}

} // namespace ns3

int
main(int argc, char* argv[])
{
    return ns3::main(argc, argv);
}
