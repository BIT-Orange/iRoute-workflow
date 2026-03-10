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

#include "extensions/iroute-vector.hpp"

#include "../tests-common.hpp"

#include <cmath>
#include <random>

namespace ns3 {
namespace ndn {

using iroute::SemanticVector;

BOOST_FIXTURE_TEST_SUITE(IRouteSemanticVector, CleanupFixture)

// ============================================================================
// Construction Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(DefaultConstructor)
{
  SemanticVector vec;
  BOOST_CHECK_EQUAL(vec.getDimension(), 0);
  BOOST_CHECK(vec.getData().empty());
}

BOOST_AUTO_TEST_CASE(ConstructFromData)
{
  std::vector<float> data = {0.1f, 0.2f, 0.3f, 0.4f};
  SemanticVector vec(data);
  
  BOOST_CHECK_EQUAL(vec.getDimension(), 4);
  BOOST_CHECK_EQUAL(vec.getData().size(), 4);
  BOOST_CHECK_CLOSE(vec.getData()[0], 0.1f, 0.001);
  BOOST_CHECK_CLOSE(vec.getData()[3], 0.4f, 0.001);
}

BOOST_AUTO_TEST_CASE(ConstructFromDimension)
{
  SemanticVector vec(64);
  
  BOOST_CHECK_EQUAL(vec.getDimension(), 64);
  // All zeros
  for (size_t i = 0; i < 64; ++i) {
    BOOST_CHECK_EQUAL(vec.getData()[i], 0.0f);
  }
}

BOOST_AUTO_TEST_CASE(MoveConstructor)
{
  std::vector<float> data = {1.0f, 2.0f, 3.0f};
  SemanticVector vec1(std::move(data));
  SemanticVector vec2(std::move(vec1));
  
  BOOST_CHECK_EQUAL(vec2.getDimension(), 3);
  BOOST_CHECK_CLOSE(vec2.getData()[0], 1.0f, 0.001);
}

// ============================================================================
// Wire Encoding/Decoding Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(WireEncodeBasic)
{
  std::vector<float> data = {1.0f, 0.5f, -0.5f, 0.0f};
  SemanticVector vec(data);
  
  ::ndn::Block wire = vec.wireEncode();
  
  BOOST_CHECK_EQUAL(wire.type(), iroute::tlv::SemanticVector);
  BOOST_CHECK(wire.hasWire());
}

BOOST_AUTO_TEST_CASE(WireDecodeBasic)
{
  std::vector<float> original = {0.1f, 0.2f, 0.3f, 0.4f};
  SemanticVector vec1(original);
  
  ::ndn::Block wire = vec1.wireEncode();
  
  SemanticVector vec2;
  vec2.wireDecode(wire);
  
  BOOST_CHECK_EQUAL(vec2.getDimension(), 4);
  for (size_t i = 0; i < 4; ++i) {
    BOOST_CHECK_CLOSE(vec2.getData()[i], original[i], 0.001);
  }
}

BOOST_AUTO_TEST_CASE(WireDecodeWithExpectedDimension)
{
  std::vector<float> data(64, 0.5f);
  SemanticVector vec1(data);
  ::ndn::Block wire = vec1.wireEncode();
  
  SemanticVector vec2;
  // Should succeed with matching dimension
  BOOST_CHECK_NO_THROW(vec2.wireDecode(wire, 64));
  
  // Should fail with mismatched dimension
  SemanticVector vec3;
  BOOST_CHECK_THROW(vec3.wireDecode(wire, 32), ::ndn::tlv::Error);
}

BOOST_AUTO_TEST_CASE(WireEncodeLargeDimension)
{
  // Test with 384 dimensions (default for Wikipedia2Vec)
  std::vector<float> data(384);
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  for (auto& v : data) {
    v = dist(rng);
  }
  
  SemanticVector vec1(data);
  ::ndn::Block wire = vec1.wireEncode();
  
  SemanticVector vec2;
  vec2.wireDecode(wire);
  
  BOOST_CHECK_EQUAL(vec2.getDimension(), 384);
  for (size_t i = 0; i < 384; ++i) {
    BOOST_CHECK_CLOSE(vec2.getData()[i], data[i], 0.001);
  }
}

// ============================================================================
// Cosine Similarity Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(CosineSimilarityIdentical)
{
  std::vector<float> data = {1.0f, 0.0f, 0.0f};
  SemanticVector vec1(data);
  SemanticVector vec2(data);
  
  double sim = vec1.computeCosineSimilarity(vec2);
  BOOST_CHECK_CLOSE(sim, 1.0, 0.001);
}

BOOST_AUTO_TEST_CASE(CosineSimilarityOrthogonal)
{
  SemanticVector vec1(std::vector<float>{1.0f, 0.0f, 0.0f});
  SemanticVector vec2(std::vector<float>{0.0f, 1.0f, 0.0f});
  
  double sim = vec1.computeCosineSimilarity(vec2);
  BOOST_CHECK_CLOSE(sim, 0.0, 0.001);
}

BOOST_AUTO_TEST_CASE(CosineSimilarityOpposite)
{
  SemanticVector vec1(std::vector<float>{1.0f, 0.0f});
  SemanticVector vec2(std::vector<float>{-1.0f, 0.0f});
  
  double sim = vec1.computeCosineSimilarity(vec2);
  BOOST_CHECK_CLOSE(sim, -1.0, 0.001);
}

BOOST_AUTO_TEST_CASE(CosineSimilarityGeneral)
{
  SemanticVector vec1(std::vector<float>{3.0f, 4.0f});
  SemanticVector vec2(std::vector<float>{4.0f, 3.0f});
  
  // cos = (3*4 + 4*3) / (5 * 5) = 24/25 = 0.96
  double sim = vec1.computeCosineSimilarity(vec2);
  BOOST_CHECK_CLOSE(sim, 0.96, 0.1);
}

BOOST_AUTO_TEST_CASE(CosineSimilarityDimensionMismatch)
{
  SemanticVector vec1(std::vector<float>{1.0f, 2.0f, 3.0f});
  SemanticVector vec2(std::vector<float>{1.0f, 2.0f});
  
  BOOST_CHECK_THROW(vec1.computeCosineSimilarity(vec2), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(CosineSimilarityEmptyVector)
{
  SemanticVector vec1;
  SemanticVector vec2(std::vector<float>{1.0f, 2.0f});
  
  BOOST_CHECK_THROW(vec1.computeCosineSimilarity(vec2), std::invalid_argument);
}

// ============================================================================
// Edge Cases
// ============================================================================

BOOST_AUTO_TEST_CASE(EncodeEmptyVectorThrows)
{
  SemanticVector vec;
  BOOST_CHECK_THROW(vec.wireEncode(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(DecodeInvalidTypeThrows)
{
  // Create a block with wrong type
  ::ndn::Block block(100);  // Wrong type
  
  SemanticVector vec;
  BOOST_CHECK_THROW(vec.wireDecode(block), ::ndn::tlv::Error);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
