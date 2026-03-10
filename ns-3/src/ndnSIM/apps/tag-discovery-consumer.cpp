/**
 * @file tag-discovery-consumer.cpp
 * @brief Implementation of TagDiscoveryConsumer.
 */

#include "tag-discovery-consumer.hpp"
#include "extensions/iroute-tlv.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include <fstream>
#include <limits>
#include <sstream>
#include <set>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.TagDiscoveryConsumer");
NS_OBJECT_ENSURE_REGISTERED(TagDiscoveryConsumer);

TypeId
TagDiscoveryConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::TagDiscoveryConsumer")
        .SetGroupName("Ndn")
        .SetParent<FloodingDiscoveryConsumer>()
        .AddConstructor<TagDiscoveryConsumer>()
        .AddAttribute("QueryToTagFile",
                      "Path to query -> tag ID mapping CSV (qid,tid)",
                      StringValue(""),
                      MakeStringAccessor(&TagDiscoveryConsumer::m_queryTagFile),
                      MakeStringChecker())
        .AddAttribute("TagK",
                      "Number of tag IDs to probe per query (1=strict mapping)",
                      UintegerValue(1),
                      MakeUintegerAccessor(&TagDiscoveryConsumer::m_tagK),
                      MakeUintegerChecker<uint32_t>(1));
    return tid;
}

TagDiscoveryConsumer::TagDiscoveryConsumer()
{
}
void
TagDiscoveryConsumer::StartApplication()
{
    if (!m_queryTagFile.empty()) {
        LoadQueryTags(m_queryTagFile);
    }
    FloodingDiscoveryConsumer::StartApplication();
}

void
TagDiscoveryConsumer::LoadQueryTags(const std::string& filePath)
{
    m_queryTags.clear();
    m_sortedTagUniverse.clear();
    std::set<uint64_t> uniqueTags;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open query->tag file: " << filePath);
        return;
    }

    std::string line;
    // Skip header if present? Assumed yes (qid,tid)
    std::getline(file, line); 

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string segment;
        std::vector<std::string> parts;
        while (std::getline(ss, segment, ',')) {
            parts.push_back(segment);
        }

        if (parts.size() >= 2) {
            std::string qidStr = parts[0];
            if (!qidStr.empty() && qidStr[0] == 'q') {
                qidStr = qidStr.substr(1);
            }
            uint64_t qid = std::numeric_limits<uint64_t>::max();
            try {
                qid = std::stoull(qidStr);
            } catch (...) {
                continue;
            }
            std::vector<uint64_t> tags;

            std::stringstream tagSs(parts[1]);
            std::string tok;
            while (std::getline(tagSs, tok, ';')) {
                if (tok.empty()) {
                    continue;
                }
                try {
                    uint64_t tid = std::stoull(tok);
                    tags.push_back(tid);
                    uniqueTags.insert(tid);
                } catch (...) {
                }
            }
            if (!tags.empty()) {
                m_queryTags[qid] = tags;
            }
        }
    }
    m_sortedTagUniverse.assign(uniqueTags.begin(), uniqueTags.end());
    NS_LOG_INFO("Loaded " << m_queryTags.size() << " query->tag mappings. tagUniverse=" << m_sortedTagUniverse.size());
}

void
TagDiscoveryConsumer::SendDiscoveryQuery(uint64_t queryId, const iroute::SemanticVector& vec)
{
    m_totalTagLookups++;

    auto it = m_queryTags.find(queryId);
    std::vector<uint64_t> tags;
    if (it != m_queryTags.end()) {
        tags = it->second;
    }
    if (it == m_queryTags.end()) {
        m_missingTagMappings++;
        NS_LOG_WARN("No tag mapping for query " << queryId << ", using fallback tag universe");
    }

    if (tags.empty() && !m_sortedTagUniverse.empty()) {
        tags.push_back(m_sortedTagUniverse[queryId % m_sortedTagUniverse.size()]);
    }

    std::vector<uint64_t> selected;
    std::set<uint64_t> seen;
    for (uint64_t tid : tags) {
        if (seen.insert(tid).second) {
            selected.push_back(tid);
        }
    }
    if (selected.empty()) {
        selected.push_back(0); // provoke timeout if mapping/universe unavailable
    }
    if (selected.size() > m_tagK) {
        selected.resize(m_tagK);
    }

    if (m_tagK > selected.size() && !m_sortedTagUniverse.empty()) {
        uint64_t anchor = selected.front();
        size_t start = 0;
        for (size_t i = 0; i < m_sortedTagUniverse.size(); ++i) {
            if (m_sortedTagUniverse[i] == anchor) {
                start = i;
                break;
            }
        }
        for (size_t step = 1; selected.size() < m_tagK && step < m_sortedTagUniverse.size(); ++step) {
            uint64_t tid = m_sortedTagUniverse[(start + step) % m_sortedTagUniverse.size()];
            if (seen.insert(tid).second) {
                selected.push_back(tid);
            }
        }
    }

    for (uint64_t tagId : selected) {
        SendDiscoveryForTag(queryId, tagId, vec);
    }
}

void
TagDiscoveryConsumer::SendDiscoveryForTag(uint64_t queryId, uint64_t tagId, const iroute::SemanticVector& vec)
{
    // Construct Name: /tag/<tid>/disc/<qid>
    ::ndn::Name interestName("/tag");
    interestName.append(std::to_string(tagId));
    interestName.append("disc");
    interestName.append(std::to_string(queryId));

    // Create Interest
    auto interest = std::make_shared<Interest>(interestName);
    interest->setCanBePrefix(true);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(ndn::time::milliseconds(2000));

    // Encode Query Vector into ApplicationParameters
    try {
         ::ndn::Block vectorBlock = vec.wireEncode();
         interest->setApplicationParameters(vectorBlock);
    } catch (...) {
        NS_LOG_WARN("Failed to encode vector for " << queryId);
    }

    interest->wireEncode();
    std::string discNameUri = interest->getName().toUri();

    // Register pending discovery
    m_pendingDiscoveries[queryId].insert(discNameUri); 
    m_discNameToQueryId[discNameUri] = queryId;
    m_discNameToDomain[discNameUri] = Name("/tag/" + std::to_string(tagId));

    // Update stats
    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        txIt->second.stage1InterestBytes += interest->wireEncode().size();
        txIt->second.totalInterestsSent++;
    }

    // Send
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule timeout
    // Reuse HandleDiscoveryTimeout
    Time timeout = m_interestLifetime + MilliSeconds(10);
    m_discoveryTimeouts[discNameUri] = Simulator::Schedule(timeout,
        &TagDiscoveryConsumer::HandleDiscoveryTimeout, this, discNameUri);

    NS_LOG_DEBUG("Sent discovery for query " << queryId << " via tag " << tagId);
}

void
TagDiscoveryConsumer::OnData(std::shared_ptr<const Data> data)
{
    // Fix: Extract actual domain from canonical name in payload
    // Discovery Data Name: /tag/<tid>/disc/<qid> (same as Interest)
    // Content: Canonical Name (e.g., /domain3/data/doc/xyz)
    
    std::string dataNameUri = data->getName().toUri();
    auto it = m_discNameToQueryId.find(dataNameUri);
    
    // Only intercept if it's a discovery reply (found in disc map)
    if (it != m_discNameToQueryId.end()) {
        try {
            // Fix: Parse TLV to get canonical name (TagDomainApp sends TLV, not string)
            const auto& content = data->getContent();
            ::ndn::Block block(nonstd::span<const uint8_t>(content.value(), content.value_size()));
            auto replyOpt = iroute::tlv::decodeDiscoveryReply(block);
            
            if (replyOpt && replyOpt->found && !replyOpt->canonicalName.empty()) {
                // Initial domain prefix from canonical name (e.g. /domainX/data/doc -> /domainX)
                // Assuming domain is the first component if it starts with /domain
                Name canonicalName = replyOpt->canonicalName;
                if (canonicalName.size() > 0) {
                     Name domainPrefix = canonicalName.getPrefix(1);
                     
                     // Update the mapping so base class logs correct domain
                     m_discNameToDomain[dataNameUri] = domainPrefix;
                     NS_LOG_DEBUG("Fixed Tag domain: " << domainPrefix << " from " << canonicalName);
                }
            }
        } catch (...) {
            NS_LOG_WARN("Failed to parse TLV from Tag reply");
        }
    }

    // Call base implementation
    FloodingDiscoveryConsumer::OnData(data);
}

} // namespace ndn
} // namespace ns3
