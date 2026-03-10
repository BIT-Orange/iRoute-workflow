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

#include "apps/iroute-discovery-consumer.hpp"
#include "extensions/iroute-vector.hpp"
#include "helper/ndn-stack-helper.hpp"
#include "helper/ndn-app-helper.hpp"

#include "../tests-common.hpp"

#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"

namespace ns3 {
namespace ndn {

BOOST_FIXTURE_TEST_SUITE(IRouteDiscoveryConsumerTests, ScenarioHelperWithCleanupFixture)

// ============================================================================
// Basic Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(InstallDiscoveryConsumer)
{
  createTopology({
    {"consumer", "router"}
  });
  
  
  ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
  consumerHelper.SetAttribute("Frequency", DoubleValue(1.0));
  
  ApplicationContainer apps = consumerHelper.Install(getNode("consumer"));
  
  BOOST_CHECK_EQUAL(apps.GetN(), 1);
}

BOOST_AUTO_TEST_CASE(DiscoveryConsumerAttributes)
{
  createTopology({
    {"consumer"}
  });
  
  
  ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
  consumerHelper.SetAttribute("Frequency", DoubleValue(2.0));
  consumerHelper.SetAttribute("SemVerId", UintegerValue(1));
  
  ApplicationContainer apps = consumerHelper.Install(getNode("consumer"));
  
  Ptr<Application> app = apps.Get(0);
  BOOST_CHECK(app != nullptr);
}

BOOST_AUTO_TEST_CASE(SetQueryVector)
{
  createTopology({
    {"consumer"}
  });
  
  
  ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
  ApplicationContainer apps = consumerHelper.Install(getNode("consumer"));
  
  Ptr<IRouteDiscoveryConsumer> consumer = 
      DynamicCast<IRouteDiscoveryConsumer>(apps.Get(0));
  
  iroute::SemanticVector queryVec(std::vector<float>(64, 0.5f));
  consumer->SetQueryVector(queryVec);
  
  BOOST_CHECK_EQUAL(consumer->GetQueryVector().getDimension(), 64);
}

// ============================================================================
// Statistics Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(InitialStatistics)
{
  createTopology({
    {"consumer"}
  });
  
  
  ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
  ApplicationContainer apps = consumerHelper.Install(getNode("consumer"));
  
  Ptr<IRouteDiscoveryConsumer> consumer = 
      DynamicCast<IRouteDiscoveryConsumer>(apps.Get(0));
  
  BOOST_CHECK_EQUAL(consumer->GetDiscoveryAttempts(), 0);
  BOOST_CHECK_EQUAL(consumer->GetDiscoveryFound(), 0);
  BOOST_CHECK_EQUAL(consumer->GetDiscoveryNotFound(), 0);
  BOOST_CHECK_EQUAL(consumer->GetStage2Success(), 0);
  BOOST_CHECK_EQUAL(consumer->GetStage2Failure(), 0);
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(ConsumerStartStop)
{
  createTopology({
    {"consumer", "router"}
  });
  
  
  ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
  consumerHelper.SetAttribute("Frequency", DoubleValue(0.5));
  
  ApplicationContainer apps = consumerHelper.Install(getNode("consumer"));
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(5.0));
  
  Simulator::Stop(Seconds(10.0));
  Simulator::Run();
  
  BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
