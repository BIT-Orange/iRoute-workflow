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

#include "extensions/iroute-manager.hpp"
#include "extensions/iroute-route-manager-registry.hpp"
#include "extensions/iroute-vector.hpp"

#include "../tests-common.hpp"

#include <cmath>
#include <random>

namespace ns3 {
namespace ndn {

using iroute::RouteManager;
using iroute::RouteManagerRegistry;
using iroute::SemanticVector;
using iroute::DomainEntry;
using iroute::CentroidEntry;
using iroute::DomainResult;

BOOST_FIXTURE_TEST_SUITE(IRouteRouteManager, CleanupFixture)

// ============================================================================
// Construction and Configuration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(DefaultConstructor)
{
  RouteManager rm;
  BOOST_CHECK_EQUAL(rm.getVectorDim(), 384);  // Default dimension
  BOOST_CHECK_EQUAL(rm.size(), 0);
}

BOOST_AUTO_TEST_CASE(ConstructWithDimension)
{
  RouteManager rm(64);
  BOOST_CHECK_EQUAL(rm.getVectorDim(), 64);
}

BOOST_AUTO_TEST_CASE(SetVectorDim)
{
  RouteManager rm;
  rm.setVectorDim(128);
  BOOST_CHECK_EQUAL(rm.getVectorDim(), 128);
}

// ============================================================================
// Route Management Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(UpdateRouteBasic)
{
  RouteManager rm(4);
  
  std::vector<float> data = {1.0f, 0.0f, 0.0f, 0.0f};
  SemanticVector centroid(data);
  
  rm.updateRoute(::ndn::Name("/router1"), centroid, 1.0, "router1");
  
  BOOST_CHECK_EQUAL(rm.size(), 1);
}

BOOST_AUTO_TEST_CASE(UpdateRouteMultiple)
{
  RouteManager rm(4);
  
  rm.updateRoute(::ndn::Name("/router1"), SemanticVector(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}), 1.0);
  rm.updateRoute(::ndn::Name("/router2"), SemanticVector(std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f}), 2.0);
  rm.updateRoute(::ndn::Name("/router3"), SemanticVector(std::vector<float>{0.0f, 0.0f, 1.0f, 0.0f}), 3.0);
  
  BOOST_CHECK_EQUAL(rm.size(), 3);
}

BOOST_AUTO_TEST_CASE(UpdateRouteOverwrite)
{
  RouteManager rm(4);
  
  SemanticVector centroid1(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  SemanticVector centroid2(std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f});
  
  rm.updateRoute(::ndn::Name("/router1"), centroid1, 1.0);
  rm.updateRoute(::ndn::Name("/router1"), centroid2, 2.0);  // Overwrite
  
  BOOST_CHECK_EQUAL(rm.size(), 1);
}

BOOST_AUTO_TEST_CASE(RemoveRoute)
{
  RouteManager rm(4);
  
  rm.updateRoute(::ndn::Name("/router1"), SemanticVector(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}), 1.0);
  rm.updateRoute(::ndn::Name("/router2"), SemanticVector(std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f}), 2.0);
  
  BOOST_CHECK_EQUAL(rm.size(), 2);
  
  bool removed = rm.removeRoute(::ndn::Name("/router1"));
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(rm.size(), 1);
  
  // Remove non-existent
  removed = rm.removeRoute(::ndn::Name("/router99"));
  BOOST_CHECK(!removed);
}

BOOST_AUTO_TEST_CASE(ClearRoutes)
{
  RouteManager rm(4);
  
  rm.updateRoute(::ndn::Name("/router1"), SemanticVector(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}), 1.0);
  rm.updateRoute(::ndn::Name("/router2"), SemanticVector(std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f}), 2.0);
  
  rm.clearAllRoutes();
  BOOST_CHECK_EQUAL(rm.size(), 0);
}

// ============================================================================
// Domain Entry Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(DomainEntryConstruction)
{
  DomainEntry entry(::ndn::Name("/domain1"), 1, 100);
  
  BOOST_CHECK_EQUAL(entry.domainId.toUri(), "/domain1");
  BOOST_CHECK_EQUAL(entry.semVerId, 1);
  BOOST_CHECK_EQUAL(entry.seqNo, 100);
  BOOST_CHECK(entry.centroids.empty());
}

BOOST_AUTO_TEST_CASE(CentroidEntryConstruction)
{
  SemanticVector vec(std::vector<float>{0.5f, 0.5f, 0.5f, 0.5f});
  CentroidEntry centroid(1, vec, 0.3, 100.0);
  
  BOOST_CHECK_EQUAL(centroid.centroidId, 1);
  BOOST_CHECK_CLOSE(centroid.radius, 0.3, 0.001);
  BOOST_CHECK_CLOSE(centroid.weight, 100.0, 0.001);
}

// ============================================================================
// Registry Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(RegistryGetOrCreate)
{
  auto rm1 = RouteManagerRegistry::getOrCreate(0, 64);
  BOOST_CHECK(rm1 != nullptr);
  BOOST_CHECK_EQUAL(rm1->getVectorDim(), 64);
  
  // Getting same node should return same instance
  auto rm2 = RouteManagerRegistry::getOrCreate(0, 64);
  BOOST_CHECK(rm1.get() == rm2.get());
  
  // Different node should return different instance
  auto rm3 = RouteManagerRegistry::getOrCreate(1, 64);
  BOOST_CHECK(rm1.get() != rm3.get());
  
  // Cleanup
  RouteManagerRegistry::clear();
}

BOOST_AUTO_TEST_CASE(RegistryClear)
{
  RouteManagerRegistry::getOrCreate(0, 64);
  RouteManagerRegistry::getOrCreate(1, 64);
  
  RouteManagerRegistry::clear();
  
  // After clear, should create new instances
  auto rm = RouteManagerRegistry::getOrCreate(0, 64);
  BOOST_CHECK(rm != nullptr);
}

// ============================================================================
// Scoring Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(FindBestMatchesSingleDomain)
{
  RouteManager rm(4);
  
  SemanticVector centroid(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  rm.updateRoute(::ndn::Name("/router1"), centroid, 1.0);
  
  SemanticVector query(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});  // Identical
  
  auto results = rm.findBestMatches(query, 3, 1.0, 0.0);
  
  BOOST_CHECK(!results.empty());
  BOOST_CHECK_EQUAL(results[0].targetRouter.toUri(), "/router1");
  BOOST_CHECK_CLOSE(results[0].score, 1.0, 0.1);
}

BOOST_AUTO_TEST_CASE(FindBestMatchesRanking)
{
  RouteManager rm(4);
  
  // Domain 1: Perfect match
  rm.updateRoute(::ndn::Name("/router1"), 
                 SemanticVector(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}), 1.0);
  
  // Domain 2: Orthogonal
  rm.updateRoute(::ndn::Name("/router2"), 
                 SemanticVector(std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f}), 1.0);
  
  // Query
  SemanticVector query(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  
  auto results = rm.findBestMatches(query, 2, 1.0, 0.0);
  
  BOOST_CHECK_EQUAL(results.size(), 2);
  // Router1 should be ranked first (higher similarity)
  BOOST_CHECK_EQUAL(results[0].targetRouter.toUri(), "/router1");
  BOOST_CHECK(results[0].score > results[1].score);
}

BOOST_AUTO_TEST_CASE(FindBestMatchesTopK)
{
  RouteManager rm(4);
  
  // Add 5 domains
  for (int i = 0; i < 5; ++i) {
    std::vector<float> data(4, 0.0f);
    data[i % 4] = 1.0f;
    rm.updateRoute(::ndn::Name("/router" + std::to_string(i)), SemanticVector(data), 1.0);
  }
  
  SemanticVector query(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  
  // Request top 3
  auto results = rm.findBestMatches(query, 3, 1.0, 0.0);
  BOOST_CHECK_EQUAL(results.size(), 3);
}

BOOST_AUTO_TEST_CASE(FindBestMatchesWithCost)
{
  RouteManager rm(4);
  
  // Two domains with same centroid but different costs
  SemanticVector centroid(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  rm.updateRoute(::ndn::Name("/cheap"), centroid, 1.0);
  rm.updateRoute(::ndn::Name("/expensive"), centroid, 10.0);
  
  SemanticVector query(std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  
  // With beta > 0, cost should affect ranking
  auto results = rm.findBestMatches(query, 2, 0.5, 0.5);
  
  BOOST_CHECK_EQUAL(results.size(), 2);
  // Cheap should be ranked first due to lower cost penalty
  BOOST_CHECK_EQUAL(results[0].targetRouter.toUri(), "/cheap");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
