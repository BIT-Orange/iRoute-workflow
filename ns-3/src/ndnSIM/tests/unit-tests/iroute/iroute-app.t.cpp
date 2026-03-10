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
#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-manager.hpp"
#include "extensions/iroute-route-manager-registry.hpp"
#include "helper/ndn-stack-helper.hpp"
#include "helper/ndn-app-helper.hpp"

#include "../tests-common.hpp"

#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"
#include "ns3/application-container.h"

namespace ns3 {
namespace ndn {

BOOST_FIXTURE_TEST_SUITE(IRouteAppTests, ScenarioHelperWithCleanupFixture)

// ============================================================================
// Basic Application Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(InstallIRouteApp)
{
  // Create nodes (NDN stack is installed automatically by createTopology)
  createTopology({
    {"node1", "node2"}
  });
  
  // Install IRouteApp
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("RouterId", StringValue("router1"));
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  routingHelper.SetAttribute("LsaInterval", TimeValue(Seconds(5.0)));
  
  ApplicationContainer apps = routingHelper.Install(getNode("node1"));
  
  BOOST_CHECK_EQUAL(apps.GetN(), 1);
}

BOOST_AUTO_TEST_CASE(IRouteAppAttributes)
{
  createTopology({
    {"node1"}
  });
  
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("RouterId", StringValue("testRouter"));
  routingHelper.SetAttribute("VectorDim", UintegerValue(128));
  routingHelper.SetAttribute("IsIngress", BooleanValue(true));
  routingHelper.SetAttribute("SemVerId", UintegerValue(2));
  routingHelper.SetAttribute("LsaInterval", TimeValue(Seconds(10.0)));
  
  ApplicationContainer apps = routingHelper.Install(getNode("node1"));
  
  Ptr<Application> app = apps.Get(0);
  Ptr<IRouteApp> irouteApp = DynamicCast<IRouteApp>(app);
  
  BOOST_CHECK(irouteApp != nullptr);
  BOOST_CHECK_EQUAL(irouteApp->GetRouterId(), "testRouter");
  BOOST_CHECK_EQUAL(irouteApp->GetSemVerId(), 2);
}

BOOST_AUTO_TEST_CASE(IRouteAppSetCentroids)
{
  createTopology({
    {"node1"}
  });
  
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("RouterId", StringValue("router1"));
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  
  ApplicationContainer apps = routingHelper.Install(getNode("node1"));
  
  Ptr<IRouteApp> irouteApp = DynamicCast<IRouteApp>(apps.Get(0));
  
  // Add centroids
  std::vector<iroute::CentroidEntry> centroids;
  iroute::SemanticVector vec(std::vector<float>(64, 0.5f));
  centroids.emplace_back(0, vec, 0.3, 100.0);
  centroids.emplace_back(1, vec, 0.4, 200.0);
  
  irouteApp->SetLocalCentroids(centroids);
  
  BOOST_CHECK_EQUAL(irouteApp->GetLocalCentroids().size(), 2);
}

// ============================================================================
// Application Lifecycle Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(IRouteAppStartStop)
{
  createTopology({
    {"node1", "node2"}
  });
  
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("RouterId", StringValue("router1"));
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  
  ApplicationContainer apps = routingHelper.Install(getNode("node1"));
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(5.0));
  
  Simulator::Stop(Seconds(10.0));
  Simulator::Run();
  
  // Should complete without errors
  BOOST_CHECK(true);
}

// ============================================================================
// Multiple Node Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(IRouteAppMultipleNodes)
{
  createTopology({
    {"ingress", "core"},
    {"core", "producer"}
  });
  
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  
  // Ingress node
  routingHelper.SetAttribute("RouterId", StringValue("ingress"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(true));
  routingHelper.Install(getNode("ingress"));
  
  // Core node
  routingHelper.SetAttribute("RouterId", StringValue("core"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(false));
  routingHelper.Install(getNode("core"));
  
  // Producer node
  routingHelper.SetAttribute("RouterId", StringValue("producer"));
  routingHelper.SetAttribute("IsIngress", BooleanValue(false));
  routingHelper.Install(getNode("producer"));
  
  Simulator::Stop(Seconds(5.0));
  Simulator::Run();
  
  BOOST_CHECK(true);
}

// ============================================================================
// LSA Broadcasting Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(IRouteAppLsaBroadcast)
{
  createTopology({
    {"node1", "node2"}
  });
  
  ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
  routingHelper.SetAttribute("RouterId", StringValue("producer"));
  routingHelper.SetAttribute("VectorDim", UintegerValue(64));
  routingHelper.SetAttribute("LsaInterval", TimeValue(Seconds(1.0)));
  
  ApplicationContainer apps = routingHelper.Install(getNode("node1"));
  
  Ptr<IRouteApp> irouteApp = DynamicCast<IRouteApp>(apps.Get(0));
  
  // Add a centroid so LSA will be published
  iroute::SemanticVector vec(std::vector<float>(64, 0.5f));
  irouteApp->AddCentroid(iroute::CentroidEntry(0, vec, 0.3, 100.0));
  
  apps.Start(Seconds(0.0));
  
  Simulator::Stop(Seconds(3.0));
  Simulator::Run();
  
  // Check LSA was published
  BOOST_CHECK(irouteApp->GetLsaTxCount() >= 1);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
