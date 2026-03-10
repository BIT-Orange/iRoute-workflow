/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "iroute-topology-builder.hpp"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("iRouteTopologyBuilder");

namespace iroute {

TopoResult BuildThreeTier(uint32_t coreN, uint32_t edgeN, uint32_t domains,
                          std::string dataRate,
                          std::string coreDelay,
                          std::string edgeDelay,
                          std::string accessDelay,
                          uint32_t seed)
{
    TopoResult res;
    ns3::NodeContainer coreNodes;
    ns3::NodeContainer edgeNodes;
    ns3::NodeContainer accessNodes; // These hold the producers

    coreNodes.Create(coreN);
    edgeNodes.Create(edgeN);
    accessNodes.Create(domains);

    res.allNodes.Add(coreNodes);
    res.allNodes.Add(edgeNodes);
    res.allNodes.Add(accessNodes);

    // Helpers
    ns3::PointToPointHelper p2pCore, p2pEdge, p2pAccess;
    p2pCore.SetDeviceAttribute("DataRate", ns3::StringValue("100Gbps"));
    p2pCore.SetChannelAttribute("Delay", ns3::StringValue(coreDelay));

    p2pEdge.SetDeviceAttribute("DataRate", ns3::StringValue("40Gbps"));
    p2pEdge.SetChannelAttribute("Delay", ns3::StringValue(edgeDelay));

    p2pAccess.SetDeviceAttribute("DataRate", ns3::StringValue(dataRate));
    p2pAccess.SetChannelAttribute("Delay", ns3::StringValue(accessDelay));

    // 1. Build Core Ring
    for (uint32_t i = 0; i < coreN; ++i) {
        uint32_t next = (i + 1) % coreN;
        p2pCore.Install(coreNodes.Get(i), coreNodes.Get(next));
    }
    // Optional chords can be added here if coreN is large

    // 2. Build Edge (Dual Homing)
    for (uint32_t i = 0; i < edgeN; ++i) {
        // Connect to Core i%N and (i+1)%N to ensure redundancy
        uint32_t c1 = i % coreN;
        uint32_t c2 = (i + 1) % coreN;
        
        p2pEdge.Install(edgeNodes.Get(i), coreNodes.Get(c1));
        p2pEdge.Install(edgeNodes.Get(i), coreNodes.Get(c2));
    }

    // 3. Build Access (Domains)
    for (uint32_t d = 0; d < domains; ++d) {
        uint32_t e = d % edgeN;
        p2pAccess.Install(accessNodes.Get(d), edgeNodes.Get(e));
        res.domainNodes.push_back(accessNodes.Get(d));
    }

    // 4. Ingress
    // We treat Edge0 as the Ingress point.
    // Consumer app will be installed here.
    res.ingress = edgeNodes.Get(0);

    NS_LOG_INFO("Created 3-Tier Topology: " 
                << coreN << " Core, " 
                << edgeN << " Edge, " 
                << domains << " Access Nodes.");

    return res;
}

} // namespace iroute
