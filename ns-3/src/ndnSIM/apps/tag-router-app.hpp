/**
 * @file tag-router-app.hpp
 * @brief Tag-based routing control plane application.
 *
 * Implements:
 * - Periodic Tag-LSA broadcasting (if local tags present)
 * - LSA flooding and processing
 * - NFD FIB population for /tag/<tid> prefixes
 * - Churn simulation
 */

#pragma once

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"
#include "extensions/tag-tlv.hpp"

#include <map>
#include <set>
#include <vector>

namespace ns3 {
namespace ndn {

class TagRouterApp : public App
{
public:
    static TypeId GetTypeId();

    TagRouterApp();

    // ========================================================================
    // Configuration
    // ========================================================================
    void SetRouterId(const std::string& routerId);
    void SetLocalTags(const std::vector<uint64_t>& tags);
    const std::vector<uint64_t>& GetLocalTags() const { return m_localTags; }
    void SetLsaPeriod(Time period);

    // ========================================================================
    // Churn Simulation
    // ========================================================================
    void ScheduleChurn(Time eventTime, const std::vector<uint64_t>& newTags);

    // ========================================================================
    // Metrics
    // ========================================================================
    uint64_t GetLsaBytesSent() const { return m_lsaBytesSent; }
    uint64_t GetLsaBytesRecv() const { return m_lsaBytesRecv; }
    uint32_t GetFibUpdates() const { return m_fibUpdates; }

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnInterest(shared_ptr<const Interest> interest) override;
    // We don't use OnData for LSA since we flood Interests (Interest-LSA) or Data-LSA?
    // User request: "contents ... in Data (or Interest+Data also fine)".
    // Simplest for flooding is "Interest Carries Payload" (like Semantic LSA in iRoute)
    // or "Interest Notification + Data Fetch".
    // IRoute uses "Interest Notification + Data Fetch" or "Interest Carries Payload"?
    // IRouteApp.cpp uses "Interest carries vector in params" for LSA broadcast?
    // Let's check `IRouteApp::BroadcastLsa`.
    // It uses Interest with ApplicationParameters. This is easiest ("Push" model).
    // So we use Interest-based LSA.

private:
    void ScheduleNextLsa();
    void BroadcastLsa();
    void ProcessLsa(const Interest& lsaInterest);
    void UpdateRoute(uint64_t tagId, uint32_t faceId);
    void ApplyChurn(std::vector<uint64_t> newTags);

private:
    std::string m_routerId;
    std::vector<uint64_t> m_localTags;
    Time m_lsaPeriod;
    Ptr<UniformRandomVariable> m_jitter;

    // State
    uint64_t m_seqNo = 0;
    std::map<std::string, uint64_t> m_topologySeq; // Origin -> MaxSeq
    std::map<std::string, std::pair<uint32_t, std::vector<uint64_t>>> m_originRoutes; // Origin -> {FaceId, Tags}
    std::set<uint32_t> m_seenNonces; // For loop prevention

    // Churn
    EventId m_churnEvent;
    
    // Metrics
    uint64_t m_lsaBytesSent = 0;
    uint64_t m_lsaBytesRecv = 0;
    uint32_t m_fibUpdates = 0;
    
    ::ndn::KeyChain m_keyChain;
};

} // namespace ndn
} // namespace ns3
