/**
 * @file tag-tlv.hpp
 * @brief TLV encoding/decoding helpers for Tag-based routing protocol.
 *
 * Defines TLV types and structures for Tag-LSA messages.
 * Uses TLV range 700-799.
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include <ndn-cxx/encoding/block.hpp>
#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/encoding/encoding-buffer.hpp>
#include <ndn-cxx/name.hpp>
#include <vector>
#include <optional>

namespace ns3 {
namespace ndn {
namespace tag {
namespace tlv {

// =============================================================================
// TLV Type Definitions
// =============================================================================

constexpr uint32_t TagLsa = 700;
constexpr uint32_t OriginRouter = 701;  // Name
constexpr uint32_t SeqNo = 702;         // uint64
constexpr uint32_t TagList = 703;       // Container
constexpr uint32_t TagId = 704;         // uint64 (TID)

// =============================================================================
// TagLsaData Structure
// =============================================================================

struct TagLsaData {
    ::ndn::Name originRouter;
    uint64_t seqNo = 0;
    std::vector<uint64_t> tags;

    bool isValid() const { return !originRouter.empty(); }
};

// =============================================================================
// Encoding Functions
// =============================================================================

inline ::ndn::Block
encodeTagLsa(const TagLsaData& lsa)
{
    ::ndn::EncodingBuffer encoder;
    size_t totalLength = 0;

    // Encode in reverse order

    // TagList
    if (!lsa.tags.empty()) {
        size_t listLength = 0;
        // Encode tags in reverse
        for (auto it = lsa.tags.rbegin(); it != lsa.tags.rend(); ++it) {
            listLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, TagId, *it);
        }
        listLength += encoder.prependVarNumber(listLength);
        listLength += encoder.prependVarNumber(TagList);
        totalLength += listLength;
    }

    // SeqNo
    totalLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
        encoder, SeqNo, lsa.seqNo);

    // OriginRouter
    ::ndn::Block nameBlock = lsa.originRouter.wireEncode();
    totalLength += encoder.prependRange(nameBlock.begin(), nameBlock.end());
    // (Note: Name TLV is self-contained, but usually wrapped if not implicit?
    // OriginRouter is type 701. So we should wrap it?
    // Standard NDN Name is TLV type 7.
    // If we want Type 701, we should wrap it or just use Name type?
    // Let's use specific type OriginRouter to wrap the Name.)
    // Wait, Name::wireEncode gives [Type=Name][Length][Components...].
    // If I want [Type=OriginRouter][Length][NameBlock...], I should wrap it.
    // OR simpler: just embed the Name block directly if the context allows.
    // But to be safe and TLV-compliant:
    // Let's wrap the Name block in OriginRouter type.
    // Actually, typical pattern:
    // [OriginRouter: [Name: /router/id]]
    
    // Let's just prepend the Name block and count it as part of TagLsa.
    // But wait, decoding needs to know it's OriginRouter.
    // If I just put Name TLV, decoder sees Type=Name (7).
    // TagLsa format:
    // [TagLsa
    //   [Name (Origin)]
    //   [SeqNo]
    //   [TagList ...]
    // ]
    // This is fine. I don't need OriginRouter type 701 if strict ordering is used or if types are unique.
    // Let's use specific type for clarity if multiple names exist. Here only 1 name.
    // So let's stick to using standard Name TLV for Origin.
    
    // SeqNo is uint64.
    // TagList is 703.

    // So:
    // [TagList]
    // [SeqNo]
    // [Name]
    
    // TagLsa wrapper
    totalLength += encoder.prependVarNumber(totalLength);
    totalLength += encoder.prependVarNumber(TagLsa);

    return encoder.block();
}

// =============================================================================
// Decoding Functions
// =============================================================================

inline std::optional<TagLsaData>
decodeTagLsa(const ::ndn::Block& block)
{
    try {
        ::ndn::Block parseBlock = block;
        parseBlock.parse();

        // Check type
        if (parseBlock.type() != TagLsa) {
            return std::nullopt;
        }

        TagLsaData lsa;
        
        // Manual parsing or assuming order?
        // Let's loop over elements.
        for (const auto& element : parseBlock.elements()) {
            if (element.type() == ::ndn::tlv::Name) {
                lsa.originRouter.wireDecode(element);
            }
            else if (element.type() == SeqNo) {
                lsa.seqNo = ::ndn::readNonNegativeInteger(element);
            }
            else if (element.type() == TagList) {
                element.parse();
                for (const auto& tagInfo : element.elements()) {
                    if (tagInfo.type() == TagId) {
                        lsa.tags.push_back(::ndn::readNonNegativeInteger(tagInfo));
                    }
                }
            }
        }

        if (lsa.originRouter.empty()) {
            return std::nullopt;
        }

        return lsa;
    }
    catch (...) {
        return std::nullopt;
    }
}

} // namespace tlv
} // namespace tag
} // namespace ndn
} // namespace ns3
