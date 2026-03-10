/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file iroute-topology-builder.hpp
 * @brief Helper for creating realistic 3-tier datacenter/WAN topologies.
 */

#ifndef IROUTE_TOPOLOGY_BUILDER_HPP
#define IROUTE_TOPOLOGY_BUILDER_HPP

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include <vector>
#include <string>

namespace iroute {

struct TopoResult {
    ns3::NodeContainer allNodes;
    ns3::Ptr<ns3::Node> ingress; // The node where consumer sits
    std::vector<ns3::Ptr<ns3::Node>> domainNodes; // Nodes hosting domain producers
    
    // Access-level method to install iRoute easier
    void InstallStack(ns3::InternetStackHelper& stack) {
        stack.Install(allNodes);
    }
};

/**
 * @brief Builds a 3-tier Core-Edge-Access topology.
 * 
 * Logic:
 * 1. Core: R-node Ring with cross-links.
 * 2. Edge: M nodes, each connected to 2 Core nodes (Dual Homing).
 * 3. Access (Domains): D nodes, distributed RR among Edge nodes.
 * 
 * @param coreN Number of core routers.
 * @param edgeN Number of edge routers.
 * @param domains Total domains (Access nodes).
 * @param dataRate Link bandwidth (e.g. "1Gbps").
 * @param coreDelay Core-Core link delay.
 * @param edgeDelay Core-Edge link delay.
 * @param accessDelay Edge-Domain link delay.
 * @param seed Random seed for cross-links.
 */
TopoResult BuildThreeTier(uint32_t coreN, uint32_t edgeN, uint32_t domains,
                          std::string dataRate = "10Gbps",
                          std::string coreDelay = "5ms",
                          std::string edgeDelay = "2ms",
                          std::string accessDelay = "10ms",
                          uint32_t seed = 42);

} // namespace iroute

#endif // IROUTE_TOPOLOGY_BUILDER_HPP
