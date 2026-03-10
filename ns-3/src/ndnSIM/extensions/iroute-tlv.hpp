/**
 * @file iroute-tlv.hpp
 * @brief TLV encoding/decoding helpers for iRoute protocol.
 *
 * Provides structured encode/decode for:
 * - DiscoveryReply: Status + CanonicalName + Confidence + SemVerID
 * - LSA content: CentroidList with multi-centroid entries
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include <ndn-cxx/name.hpp>
#include <ndn-cxx/encoding/buffer.hpp>
#include <ndn-cxx/encoding/block.hpp>
#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/encoding/encoding-buffer.hpp>

#include <optional>
#include <cstring>

namespace iroute {
namespace tlv {

// =============================================================================
// TLV Type Definitions
// =============================================================================

// Discovery Reply TLV types (600-699 range for iRoute)
constexpr uint32_t DiscoveryReply = 600;
constexpr uint32_t Status = 601;          // uint64: 0=NotFound, 1=Found
constexpr uint32_t CanonicalName = 602;   // Name TLV
constexpr uint32_t Confidence = 603;      // uint64 (double bit pattern)
constexpr uint32_t SemVerIdReply = 604;   // uint64: SemVerID used for query
constexpr uint32_t ManifestDigest = 605;  // optional bytes

// Status values
constexpr uint64_t StatusNotFound = 0;
constexpr uint64_t StatusFound = 1;

// =============================================================================
// DiscoveryReplyData Structure
// =============================================================================

/**
 * @brief Parsed DiscoveryReply data structure.
 */
struct DiscoveryReplyData {
    bool found = false;
    ::ndn::Name canonicalName;
    double confidence = 0.0;
    uint32_t semVerId = 1;
    std::optional<::ndn::Buffer> manifestDigest;

    bool isValid() const { return found && !canonicalName.empty(); }
};

// =============================================================================
// Encoding Functions
// =============================================================================

/**
 * @brief Encodes a DiscoveryReply TLV block.
 *
 * @param reply The reply data to encode.
 * @return Encoded Block suitable for Data.setContent().
 */
inline ::ndn::Block
encodeDiscoveryReply(const DiscoveryReplyData& reply)
{
    ::ndn::EncodingBuffer encoder;
    size_t totalLength = 0;

    // Encode in reverse order (TLV prepend convention)

    // Optional ManifestDigest (skip if not present)
    if (reply.manifestDigest && reply.manifestDigest->size() > 0) {
        // Create a block and prepend as bytes
        size_t digestLen = reply.manifestDigest->size();
        totalLength += encoder.prependBytes({reply.manifestDigest->data(), digestLen});
        totalLength += encoder.prependVarNumber(digestLen);
        totalLength += encoder.prependVarNumber(ManifestDigest);
    }

    // SemVerId
    totalLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
        encoder, SemVerIdReply, reply.semVerId);

    // Confidence (as uint64 bit pattern)
    uint64_t confBits;
    std::memcpy(&confBits, &reply.confidence, sizeof(double));
    totalLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
        encoder, Confidence, confBits);

    // CanonicalName - prepend as range
    ::ndn::Block nameBlock = reply.canonicalName.wireEncode();
    totalLength += encoder.prependRange(nameBlock.begin(), nameBlock.end());

    // Status
    uint64_t statusVal = reply.found ? StatusFound : StatusNotFound;
    totalLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
        encoder, Status, statusVal);

    // DiscoveryReply wrapper
    totalLength += encoder.prependVarNumber(totalLength);
    totalLength += encoder.prependVarNumber(DiscoveryReply);

    return encoder.block();
}

/**
 * @brief Encodes a DiscoveryReply with simple parameters.
 */
inline ::ndn::Block
encodeDiscoveryReply(bool found, const ::ndn::Name& canonicalName,
                     double confidence, uint32_t semVerId)
{
    DiscoveryReplyData reply;
    reply.found = found;
    reply.canonicalName = canonicalName;
    reply.confidence = confidence;
    reply.semVerId = semVerId;
    return encodeDiscoveryReply(reply);
}

// =============================================================================
// Decoding Functions
// =============================================================================

/**
 * @brief Decodes a DiscoveryReply TLV block.
 *
 * @param block The Block to decode (should be Data.getContent()).
 * @return Parsed DiscoveryReplyData, or empty if invalid.
 */
inline std::optional<DiscoveryReplyData>
decodeDiscoveryReply(const ::ndn::Block& block)
{
    try {
        // Parse the outer block
        ::ndn::Block parseBlock = block;
        parseBlock.parse();

        DiscoveryReplyData reply;

        for (const auto& element : parseBlock.elements()) {
            switch (element.type()) {
                case Status: {
                    uint64_t statusVal = ::ndn::readNonNegativeInteger(element);
                    reply.found = (statusVal == StatusFound);
                    break;
                }
                case CanonicalName:
                case ::ndn::tlv::Name: {
                    reply.canonicalName.wireDecode(element);
                    break;
                }
                case Confidence: {
                    uint64_t confBits = ::ndn::readNonNegativeInteger(element);
                    std::memcpy(&reply.confidence, &confBits, sizeof(double));
                    break;
                }
                case SemVerIdReply: {
                    reply.semVerId = static_cast<uint32_t>(
                        ::ndn::readNonNegativeInteger(element));
                    break;
                }
                case ManifestDigest: {
                    reply.manifestDigest = ::ndn::Buffer(element.value(), element.value_size());
                    break;
                }
                default:
                    // Ignore unknown elements for forward compatibility
                    break;
            }
        }

        return reply;
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace tlv
} // namespace iroute
