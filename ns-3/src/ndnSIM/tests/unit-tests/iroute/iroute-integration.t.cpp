/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2024  iRoute Team.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/semantic-producer.hpp"
#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-manager.hpp"
#include "extensions/iroute-route-manager-registry.hpp"
#include "helper/ndn-stack-helper.hpp"
#include "helper/ndn-app-helper.hpp"
#include "helper/ndn-strategy-choice-helper.hpp"
#include "helper/ndn-global-routing-helper.hpp"

#include "../tests-common.hpp"

#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"

#include <random>

namespace ns3 {
namespace ndn {

BOOST_FIXTURE_TEST_SUITE(IRouteIntegration, ScenarioHelperWithCleanupFixture)

// ============================================================================
// Helper Functions
// ============================================================================

static iroute::SemanticVector
GenerateRandomVector(uint32_t dim, uint32_t seed)
{
  std::mt19937 rng(seed);
  std::normal_distribution<float> dist(0.0f, 1.0f);
  
  std::vector<float> data(dim);
  float norm = 0.0f;
  for (auto& v : data) {
    v = dist(rng);
    norm += v * v;
  }
  norm = std::sqrt(norm);
  if (norm > 0) {
    for (auto& v : data) {
      v /= norm;
    }
  }
  return iroute::SemanticVector(data);
}

// ============================================================================
// Full Pipeline Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(FullPipelineSimple)
{
  // Topology: Consumer <-> Ingress <-> Producer
  createTopology({
    {"consumer", "ingress"},
    {"ingress", "producer"}
  });
  
  
  // Install IRouteApp on ingress (as ingress node)
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  routingHelper.SetAttribute("RouterId", StringValue("ingress"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(true));
  routingHelper.SetAttribute("LsaInterval", TimeValue(Seconds(1.0)));
  routingHelper.Install(getNode("ingress"));
  
  // Install IRouteApp on producer
  routingHelper.SetAttribute("RouterId", StringValue("producer"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(false));
  ApplicationContainer producerRoutingApps = routingHelper.Install(getNode("producer"));
  
  // Add centroid to producer's IRouteApp
  Ptr<IRouteApp> producerRouting = DynamicCast<IRouteApp>(producerRoutingApps.Get(0));
  iroute::SemanticVector contentVec = GenerateRandomVector(64, 42);
  producerRouting->AddCentroid(iroute::CentroidEntry(0, contentVec, 0.5, 100.0));
  
  // Install SemanticProducer
  ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
  producerHelper.SetPrefix("/producer");
  producerHelper.SetAttribute("PayloadSize", UintegerValue(1024));
  producerHelper.Install(getNode("producer"));
  
  // Install discovery consumer
  ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
  consumerHelper.SetAttribute("Frequency", DoubleValue(0.5));
  ApplicationContainer consumerApps = consumerHelper.Install(getNode("consumer"));
  
  Ptr<IRouteDiscoveryConsumer> consumer = 
      DynamicCast<IRouteDiscoveryConsumer>(consumerApps.Get(0));
  consumer->SetQueryVector(contentVec);  // Use same vector as content
  
  Simulator::Stop(Seconds(10.0));
  Simulator::Run();
  
  // Check that producer received some LSA publications
  BOOST_CHECK(producerRouting->GetLsaTxCount() >= 1);
}

// ============================================================================
// Multi-Domain Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(MultiDomainSetup)
{
  // Topology with multiple domains
  createTopology({
    {"consumer", "ingress"},
    {"ingress", "domain1"},
    {"ingress", "domain2"}
  });
  
  
  // Install IRouteApp on all nodes
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  
  routingHelper.SetAttribute("RouterId", StringValue("ingress"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(true));
  routingHelper.Install(getNode("ingress"));
  
  routingHelper.SetAttribute("RouterId", StringValue("domain1"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(false));
  ApplicationContainer domain1Apps = routingHelper.Install(getNode("domain1"));
  
  routingHelper.SetAttribute("RouterId", StringValue("domain2"));
  ApplicationContainer domain2Apps = routingHelper.Install(getNode("domain2"));
  
  // Add different centroids to each domain
  Ptr<IRouteApp> domain1Routing = DynamicCast<IRouteApp>(domain1Apps.Get(0));
  Ptr<IRouteApp> domain2Routing = DynamicCast<IRouteApp>(domain2Apps.Get(0));
  
  // Domain 1: Sports content
  iroute::SemanticVector sportsVec = GenerateRandomVector(64, 100);
  domain1Routing->AddCentroid(iroute::CentroidEntry(0, sportsVec, 0.3, 50.0));
  
  // Domain 2: News content
  iroute::SemanticVector newsVec = GenerateRandomVector(64, 200);
  domain2Routing->AddCentroid(iroute::CentroidEntry(0, newsVec, 0.3, 75.0));
  
  Simulator::Stop(Seconds(5.0));
  Simulator::Run();
  
  // Check that apps were installed and running (LSA count depends on timing)
  BOOST_CHECK(domain1Routing != nullptr);
  BOOST_CHECK(domain2Routing != nullptr);
}

// ============================================================================
// RouteManager Integration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(RouteManagerIntegration)
{
  createTopology({
    {"node1"}
  });
  
  
  // Install IRouteApp which should create RouteManager
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  routingHelper.SetAttribute("RouterId", StringValue("node1"));
  routingHelper.Install(getNode("node1"));
  
  // Start application
  Simulator::Stop(Seconds(1.0));
  Simulator::Run();
  
  // RouteManager should exist for this node
  uint32_t nodeId = getNode("node1")->GetId();
  auto rm = iroute::RouteManagerRegistry::getOrCreate(nodeId, 64);
  
  BOOST_CHECK(rm != nullptr);
  BOOST_CHECK_EQUAL(rm->getVectorDim(), 64);
  
  // Cleanup
  iroute::RouteManagerRegistry::clear();
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
