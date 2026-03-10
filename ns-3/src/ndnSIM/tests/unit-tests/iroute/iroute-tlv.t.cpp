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

#include "extensions/iroute-tlv.hpp"
#include "extensions/iroute-manager.hpp"

#include "../tests-common.hpp"

#include <cstring>

namespace ns3 {
namespace ndn {

BOOST_FIXTURE_TEST_SUITE(IRouteTlv, CleanupFixture)

// ============================================================================
// DiscoveryReplyData Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(DiscoveryReplyDataDefault)
{
  iroute::tlv::DiscoveryReplyData reply;
  
  BOOST_CHECK(!reply.found);
  BOOST_CHECK(reply.canonicalName.empty());
  BOOST_CHECK_EQUAL(reply.confidence, 0.0);
  BOOST_CHECK_EQUAL(reply.semVerId, 1);
  BOOST_CHECK(!reply.isValid());
}

BOOST_AUTO_TEST_CASE(DiscoveryReplyDataValid)
{
  iroute::tlv::DiscoveryReplyData reply;
  reply.found = true;
  reply.canonicalName = ::ndn::Name("/test/content");
  reply.confidence = 0.85;
  reply.semVerId = 1;
  
  BOOST_CHECK(reply.isValid());
}

BOOST_AUTO_TEST_CASE(DiscoveryReplyDataInvalidEmpty)
{
  iroute::tlv::DiscoveryReplyData reply;
  reply.found = true;
  // canonicalName is empty
  
  BOOST_CHECK(!reply.isValid());
}

// ============================================================================
// Discovery Reply Encoding/Decoding Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(EncodeDiscoveryReplyFound)
{
  iroute::tlv::DiscoveryReplyData reply;
  reply.found = true;
  reply.canonicalName = ::ndn::Name("/domain/content/item1");
  reply.confidence = 0.75;
  reply.semVerId = 2;
  
  ::ndn::Block wire = iroute::tlv::encodeDiscoveryReply(reply);
  
  BOOST_CHECK_EQUAL(wire.type(), iroute::tlv::DiscoveryReply);
  BOOST_CHECK(wire.hasWire());
}

BOOST_AUTO_TEST_CASE(EncodeDiscoveryReplyNotFound)
{
  iroute::tlv::DiscoveryReplyData reply;
  reply.found = false;
  reply.canonicalName = ::ndn::Name();  // Empty
  reply.confidence = 0.0;
  reply.semVerId = 1;
  
  ::ndn::Block wire = iroute::tlv::encodeDiscoveryReply(reply);
  
  BOOST_CHECK_EQUAL(wire.type(), iroute::tlv::DiscoveryReply);
}

BOOST_AUTO_TEST_CASE(DecodeDiscoveryReplyFound)
{
  // Encode first
  iroute::tlv::DiscoveryReplyData original;
  original.found = true;
  original.canonicalName = ::ndn::Name("/test/data/123");
  original.confidence = 0.92;
  original.semVerId = 3;
  
  ::ndn::Block wire = iroute::tlv::encodeDiscoveryReply(original);
  
  // Decode
  auto decodedOpt = iroute::tlv::decodeDiscoveryReply(wire);
  BOOST_REQUIRE(decodedOpt.has_value());
  auto decoded = *decodedOpt;
  
  BOOST_CHECK_EQUAL(decoded.found, original.found);
  BOOST_CHECK_EQUAL(decoded.canonicalName, original.canonicalName);
  BOOST_CHECK_CLOSE(decoded.confidence, original.confidence, 0.001);
  BOOST_CHECK_EQUAL(decoded.semVerId, original.semVerId);
}

BOOST_AUTO_TEST_CASE(DecodeDiscoveryReplyNotFound)
{
  iroute::tlv::DiscoveryReplyData original;
  original.found = false;
  original.canonicalName = ::ndn::Name();
  original.confidence = 0.0;
  original.semVerId = 1;
  
  ::ndn::Block wire = iroute::tlv::encodeDiscoveryReply(original);
  auto decodedOpt = iroute::tlv::decodeDiscoveryReply(wire);
  BOOST_REQUIRE(decodedOpt.has_value());
  
  BOOST_CHECK_EQUAL(decodedOpt->found, false);
}

BOOST_AUTO_TEST_CASE(RoundTripMultipleConfidences)
{
  std::vector<double> confidences = {0.0, 0.1, 0.5, 0.99, 1.0};
  
  for (double conf : confidences) {
    iroute::tlv::DiscoveryReplyData original;
    original.found = true;
    original.canonicalName = ::ndn::Name("/test");
    original.confidence = conf;
    original.semVerId = 1;
    
    ::ndn::Block wire = iroute::tlv::encodeDiscoveryReply(original);
    auto decodedOpt = iroute::tlv::decodeDiscoveryReply(wire);
    BOOST_REQUIRE(decodedOpt.has_value());
    
    BOOST_CHECK_CLOSE(decodedOpt->confidence, conf, 0.001);
  }
}

// ============================================================================
// TLV Type Constants Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(TlvTypeValues)
{
  // Discovery Reply types
  BOOST_CHECK_EQUAL(iroute::tlv::DiscoveryReply, 600);
  BOOST_CHECK_EQUAL(iroute::tlv::Status, 601);
  BOOST_CHECK_EQUAL(iroute::tlv::CanonicalName, 602);
  BOOST_CHECK_EQUAL(iroute::tlv::Confidence, 603);
  BOOST_CHECK_EQUAL(iroute::tlv::SemVerIdReply, 604);
  BOOST_CHECK_EQUAL(iroute::tlv::ManifestDigest, 605);
  
  // Status values
  BOOST_CHECK_EQUAL(iroute::tlv::StatusNotFound, 0);
  BOOST_CHECK_EQUAL(iroute::tlv::StatusFound, 1);
}

BOOST_AUTO_TEST_CASE(LsaTlvTypeValues)
{
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::OriginId, 140);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::SemVerId, 141);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::SeqNo, 142);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::Lifetime, 143);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::Scope, 144);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::CentroidList, 145);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::CentroidEntry, 146);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::CentroidId, 147);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::Radius, 148);
  BOOST_CHECK_EQUAL(iroute::lsa_tlv::Weight, 149);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ns3
