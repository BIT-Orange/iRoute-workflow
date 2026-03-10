/**
 * @file tag-discovery-consumer.hpp
 * @brief Consumer application for Tag-based discovery.
 */

#pragma once

#include "flooding-discovery-consumer.hpp"

namespace ns3 {
namespace ndn {

/**
 * @brief Tag-based Discovery Consumer.
 * 
 * Extends functionality of FloodingDiscoveryConsumer but changes the discovery strategy:
 * Instead of flooding all domains, it looks up a Tag ID for the query and sends
 * a single discovery Interest to /tag/<tid>/disc/<qid>.
 */
class TagDiscoveryConsumer : public FloodingDiscoveryConsumer
{
public:
    static TypeId GetTypeId();

    TagDiscoveryConsumer();

    // Load query->tag mapping
    void LoadQueryTags(const std::string& filePath);
    uint64_t GetMissingTagMappings() const { return m_missingTagMappings; }
    uint64_t GetTotalTagLookups() const { return m_totalTagLookups; }

protected:
    // Override StartApplication to load tags
    virtual void StartApplication() override;
    
    // Override SendDiscoveryQuery
    virtual void SendDiscoveryQuery(uint64_t queryId, const iroute::SemanticVector& vec) override;

    // Override OnData to extract domain from content
    virtual void OnData(std::shared_ptr<const ndn::Data> data) override;
    void SendDiscoveryForTag(uint64_t queryId, uint64_t tagId, const iroute::SemanticVector& vec);

private:
    std::string m_queryTagFile;
    uint32_t m_tagK = 1;
    std::map<uint64_t, std::vector<uint64_t>> m_queryTags; // qid -> tag IDs
    std::vector<uint64_t> m_sortedTagUniverse;
    uint64_t m_missingTagMappings = 0;
    uint64_t m_totalTagLookups = 0;
};

} // namespace ndn
} // namespace ns3
