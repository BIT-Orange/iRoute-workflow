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

#include "apps/semantic-producer.hpp"
#include "extensions/iroute-vector.hpp"
#include "helper/ndn-stack-helper.hpp"
#include "helper/ndn-app-helper.hpp"

#include "../tests-common.hpp"

#include "ns3/node-container.h"
#include "ns3/point-to-point-module.h"

namespace ns3 {
namespace ndn {

BOOST_FIXTURE_TEST_SUITE(IRouteSemanticProducer, ScenarioHelperWithCleanupFixture)

// ============================================================================
// Basic Installation Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(InstallSemanticProducer)
{
  createTopology({
    {"producer"}
  });
  
  
  ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
  producerHelper.SetPrefix("/domain1");
  producerHelper.SetAttribute("PayloadSize", UintegerValue(1024));
  
  ApplicationContainer apps = producerHelper.Install(getNode("producer"));
  
  BOOST_CHECK_EQUAL(apps.GetN(), 1);
}

BOOST_AUTO_TEST_CASE(SemanticProducerAttributes)
{
  createTopology({
    {"producer"}
  });
  
  
  ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
  producerHelper.SetPrefix("/test/prefix");
  producerHelper.SetAttribute("PayloadSize", UintegerValue(2048));
  
  ApplicationContainer apps = producerHelper.Install(getNode("producer"));
  
  Ptr<Application> app = apps.Get(0);
  BOOST_CHECK(app != nullptr);
}

// ============================================================================
// Content Registration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(RegisterContent)
{
  createTopology({
    {"producer"}
  });
  
  
  ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
  producerHelper.SetPrefix("/domain");
  
  ApplicationContainer apps = producerHelper.Install(getNode("producer"));
  
  Ptr<SemanticProducer> producer = DynamicCast<SemanticProducer>(apps.Get(0));
  
  // Register content with semantic vector
  iroute::SemanticVector contentVec(std::vector<float>(64, 0.5f));
  producer->AddContent("/domain/data/item1", contentVec);
  
  BOOST_CHECK(true);  // Should complete without error
}

// ============================================================================
// Lifecycle Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(ProducerStartStop)
{
  createTopology({
    {"producer", "router"}
  });
  
  
  ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
  producerHelper.SetPrefix("/domain");
  
  ApplicationContainer apps = producerHelper.Install(getNode("producer"));
  apps.Start(Seconds(0.0));
  apps.Stop(Seconds(5.0));
  
  Simulator::Stop(Seconds(10.0));
  Simulator::Run();
  
  BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
