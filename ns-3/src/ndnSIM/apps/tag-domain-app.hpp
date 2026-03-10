/**
 * @file tag-domain-app.hpp
 * @brief Domain application for Tag-based discovery.
 *
 * Responds to /tag/<tid>/disc/<qid> Interests with the best matching local document.
 */

#pragma once

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"
#include "iroute-vector.hpp"
#include "extensions/iroute-tlv.hpp"

#include <vector>
#include <string>

namespace ns3 {
namespace ndn {

struct TagContentEntry {
    std::string docId;
    iroute::SemanticVector vector;
};

class TagDomainApp : public App
{
public:
    static TypeId GetTypeId();

    TagDomainApp();

    void SetTags(const std::vector<uint64_t>& tags);
    void SetLocalContent(const std::vector<TagContentEntry>& content);
    void SetDomainPrefix(const std::string& prefix); // e.g. /domain0

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnInterest(shared_ptr<const Interest> interest) override;

private:
    void SendFound(const Interest& interest, const std::string& docId, double confidence);
    void SendNotFound(const Interest& interest);

private:
    std::vector<uint64_t> m_tags;
    std::vector<TagContentEntry> m_content;
    std::string m_domainPrefix;
    double m_threshold; // Similarity threshold
    uint64_t m_semVerId = 1;
    uint32_t m_replyDelayUs = 0;
    uint32_t m_replyJitterUs = 0;
    Ptr<UniformRandomVariable> m_replyDelayRv;
    ::ndn::KeyChain m_keyChain;
};

} // namespace ndn
} // namespace ns3
