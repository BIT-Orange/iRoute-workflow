/**
 * @file flood-responder-app.hpp
 * @brief Flooding baseline responder with domain and producer modes.
 *
 * Two modes:
 * - domain:   sim(query, domain_centroid) → respond if above threshold
 * - producer: argmax sim(query, doc_embedding) → respond with best doc
 *
 * Listens on /<DomainID>/iroute/disc/<SemVer> prefix.
 */

#pragma once

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "extensions/iroute-tlv.hpp"
#include "iroute-vector.hpp"
#include "iroute-manager.hpp"

#include <ndn-cxx/security/key-chain.hpp>
#include "ns3/random-variable-stream.h"

#include <vector>
#include <string>

namespace ns3 {
namespace ndn {

/**
 * @brief Content entry for producer-mode local search.
 */
struct FloodContentEntry {
    std::string docId;
    iroute::SemanticVector vector;
};

/**
 * @class FloodResponderApp
 * @brief Domain or producer-level responder for flooding discovery.
 */
class FloodResponderApp : public App
{
public:
    static TypeId GetTypeId();

    FloodResponderApp() = default;
    virtual ~FloodResponderApp() = default;

    // ========================================================================
    // Configuration setters
    // ========================================================================

    void SetCentroids(const std::vector<iroute::CentroidEntry>& centroids);
    void SetLocalContent(const std::vector<FloodContentEntry>& content);

    // ========================================================================
    // Statistics
    // ========================================================================

    uint32_t GetQueriesReceived() const { return m_queriesReceived; }
    uint32_t GetResponsesSent() const { return m_responsesSent; }
    uint64_t GetTotalBytesServed() const { return m_totalBytesServed; }

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnInterest(shared_ptr<const Interest> interest) override;

private:
    void HandleDomainMode(const ::ndn::Name& interestName, const iroute::SemanticVector& queryVec);
    void HandleProducerMode(const ::ndn::Name& interestName, const iroute::SemanticVector& queryVec);

    void SendFound(const ::ndn::Name& interestName,
                   const ::ndn::Name& canonicalName,
                   double confidence);
    void SendNotFound(const ::ndn::Name& interestName);

private:
    std::string m_prefix;
    std::string m_mode = "producer";
    double m_threshold = 0.0;
    uint32_t m_semVerId = 1;
    uint32_t m_replyDelayUs = 0;
    uint32_t m_replyJitterUs = 0;
    Ptr<UniformRandomVariable> m_replyDelayRv;

    std::vector<iroute::CentroidEntry> m_centroids;
    std::vector<FloodContentEntry> m_content;

    ::ndn::KeyChain m_keyChain;

    // Stats
    uint32_t m_queriesReceived = 0;
    uint32_t m_responsesSent = 0;
    uint64_t m_totalBytesServed = 0;
};

} // namespace ndn
} // namespace ns3
