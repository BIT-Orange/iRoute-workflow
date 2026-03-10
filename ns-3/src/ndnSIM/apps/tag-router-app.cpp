/**
 * @file tag-router-app.cpp
 * @brief Implementation of TagRouterApp.
 */

#include "tag-router-app.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/face-table.hpp"
#include <ndn-cxx/lp/tags.hpp>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.TagRouterApp");
NS_OBJECT_ENSURE_REGISTERED(TagRouterApp);

TypeId
TagRouterApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::TagRouterApp")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<TagRouterApp>()
        .AddAttribute("RouterId",
                      "Router ID (string)",
                      StringValue("router"),
                      MakeStringAccessor(&TagRouterApp::SetRouterId),
                      MakeStringChecker())
        .AddAttribute("LsaPeriod",
                      "Period for LSA broadcast",
                      TimeValue(Seconds(1.0)),
                      MakeTimeAccessor(&TagRouterApp::m_lsaPeriod),
                      MakeTimeChecker());
    return tid;
}

TagRouterApp::TagRouterApp()
{
    m_jitter = CreateObject<UniformRandomVariable>();
}

void
TagRouterApp::SetRouterId(const std::string& routerId)
{
    m_routerId = routerId;
}

void
TagRouterApp::SetLocalTags(const std::vector<uint64_t>& tags)
{
    m_localTags = tags;
    m_fibUpdates++; // Count local updates? No, just set.
}

void
TagRouterApp::SetLsaPeriod(Time period)
{
    m_lsaPeriod = period;
}

void
TagRouterApp::ScheduleChurn(Time eventTime, const std::vector<uint64_t>& newTags)
{
    Simulator::Schedule(eventTime, &TagRouterApp::ApplyChurn, this, newTags);
}

void
TagRouterApp::ApplyChurn(std::vector<uint64_t> newTags)
{
    NS_LOG_INFO("[TagRouter] Churn! Replacing tags. Old size=" << m_localTags.size() 
                << " New size=" << newTags.size());
    m_localTags = newTags;
    // Trigger immediate LSA?
    Simulator::Schedule(Seconds(0.1), &TagRouterApp::BroadcastLsa, this);
}

void
TagRouterApp::StartApplication()
{
    App::StartApplication();

    // Register prefix to receive LSAs: /ndn/broadcast/tag-lsa
    // Note: We need to register this prefix so local NFD forwards incoming LSAs to this App.
    // The strategy on /ndn/broadcast/tag-lsa should be Multicast to ensure it ALSO goes to other nodes.
    // In RunTag, we must install MulticastStrategy.
    ::ndn::Name lsaPrefix("/ndn/broadcast/tag-lsa");
    FibHelper::AddRoute(GetNode(), lsaPrefix, m_face, 0);

    NS_LOG_INFO("[TagRouter] Started " << m_routerId 
                << " Period=" << m_lsaPeriod.GetSeconds()
                << " LocalTags=" << m_localTags.size());

    ScheduleNextLsa();
}

void
TagRouterApp::StopApplication()
{
    App::StopApplication();
}

void
TagRouterApp::ScheduleNextLsa()
{
    if (!m_active) return;

    // Add jitter: 0.8 to 1.2 * period
    double jitterFactor = m_jitter->GetValue(0.8, 1.2);
    Time nextTime = Seconds(m_lsaPeriod.GetSeconds() * jitterFactor);
    Simulator::Schedule(nextTime, &TagRouterApp::BroadcastLsa, this);
}

void
TagRouterApp::BroadcastLsa()
{
    ScheduleNextLsa(); // Schedule next one

    m_seqNo++;

    // Name: /ndn/broadcast/tag-lsa/<router-id>/<seq>
    ::ndn::Name name("/ndn/broadcast/tag-lsa");
    name.append(m_routerId);
    name.append(std::to_string(m_seqNo)); // OR use SequenceNumber logic? String is fine for simplicity.
    // To be cleaner, maybe Component::fromNumber(m_seqNo) but string is easier to debug.

    auto interest = std::make_shared<Interest>(name);
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(true); 
    // Interest Lifetime? LSA propagation should be fast.
    interest->setInterestLifetime(::ndn::time::milliseconds(2000));

    // Encode LSA in ApplicationParameters
    tag::tlv::TagLsaData lsa;
    lsa.originRouter = ::ndn::Name(m_routerId); // Just the ID or full name? Using ID for now.
    lsa.seqNo = m_seqNo;
    lsa.tags = m_localTags;

    auto block = tag::tlv::encodeTagLsa(lsa);
    interest->setApplicationParameters(block);

    // Sign (optional for sim, skipping for speed/simplicity or use dummy signature)
    // m_keyChain.sign(*interest); 
    // (Skipping signing to avoid keychain setup overhead in baseline)

    // Send
    // Use m_face->expressInterest?
    // Since we are the origin, we inject it into the forwarder.
    // We expect the forwarder (MulticastStrategy) to send it out to all faces relative to /ndn/broadcast/tag-lsa.
    // BUT expressInterest expects a callback.
    interest->wireEncode(); // Ensure wire buffer is created
    m_lsaBytesSent += interest->wireEncode().size();

    // Send Interest
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    NS_LOG_INFO("[TagRouter] Broadcast LSA seq=" << m_seqNo << " tags=" << m_localTags.size());
}

void
TagRouterApp::OnInterest(shared_ptr<const Interest> interest)
{
    App::OnInterest(interest);

    ::ndn::Name name = interest->getName();
    std::string uri = name.toUri();
    if (uri.find("/ndn/broadcast/tag-lsa") != 0) {
        return; 
    }

    // Don't process our own LSAs (simple check by RouterID in name)
    // Name format: /ndn/broadcast/tag-lsa/<router-id>/<seq>
    // size should be >= 5 (prefix=3 + router + seq)
    if (name.size() < 5) return;

    std::string originId = name.get(3).toUri();
    if (originId == m_routerId) {
        return; 
    }

    ProcessLsa(*interest);
}

void
TagRouterApp::ProcessLsa(const Interest& interest)
{
    m_lsaBytesRecv += interest.wireEncode().size();

    auto params = interest.getApplicationParameters();
    ::ndn::Block block(nonstd::span<const uint8_t>(params.value(), params.value_size()));
    auto lsaOpt = tag::tlv::decodeTagLsa(block);

    if (!lsaOpt) {
        NS_LOG_WARN("Failed to decode LSA from " << interest.getName());
        return;
    }

    const auto& lsa = *lsaOpt;
    std::string originUri = lsa.originRouter.toUri();

    // Check SeqNo
    auto seqIt = m_topologySeq.find(originUri);
    if (seqIt != m_topologySeq.end() && seqIt->second >= lsa.seqNo) {
        return; // Old or duplicate LSA
    }
    m_topologySeq[originUri] = lsa.seqNo;

    // Identify Incoming Face
    auto tag = interest.getTag<::ndn::lp::IncomingFaceIdTag>();
    uint64_t incomingFaceId = 0;
    if (tag) {
        incomingFaceId = *tag;
    } else {
        NS_LOG_WARN("No IncomingFaceIdTag on LSA Interest!");
        // Fallback or return? Return safest.
        return;
    }

    NS_LOG_DEBUG("[TagRouter] Process LSA from " << originUri 
                 << " seq=" << lsa.seqNo 
                 << " tags=" << lsa.tags.size()
                 << " face=" << incomingFaceId);

    // Definitions
    auto& routeEntry = m_originRoutes[originUri];
    uint32_t oldFaceId = routeEntry.first;
    const std::vector<uint64_t>& oldTags = routeEntry.second;
    
    // Convert to sets for easy diff
    std::set<uint64_t> oldSet(oldTags.begin(), oldTags.end());
    std::set<uint64_t> newSet(lsa.tags.begin(), lsa.tags.end());

    // Get Face pointer from ID
    auto face = GetNode()->GetObject<L3Protocol>()->getFaceTable().get(incomingFaceId);
    if (!face) {
        NS_LOG_WARN("Face " << incomingFaceId << " not found");
        return;
    }

    if (oldFaceId != 0 && oldFaceId != incomingFaceId) {
        // Face changed -> Remove everything from old face
        auto oldFacePtr = GetNode()->GetObject<L3Protocol>()->getFaceTable().get(oldFaceId);
        if (oldFacePtr) {
            for (uint64_t tid : oldTags) {
                ::ndn::Name prefix("/tag");
                prefix.append(std::to_string(tid));
                FibHelper::RemoveRoute(GetNode(), prefix, oldFaceId);
            }
        }
        oldSet.clear(); // Treated as if we have no valid routes on new face
    }

    // Now computing diff against oldSet (which is empty if face changed)
    // To Add: newSet - oldSet
    // To Remove: oldSet - newSet
    
    for (uint64_t tid : newSet) {
        if (oldSet.find(tid) == oldSet.end()) {
            ::ndn::Name prefix("/tag");
            prefix.append(std::to_string(tid));
            FibHelper::AddRoute(GetNode(), prefix, incomingFaceId, 0); // Cost 0
            m_fibUpdates++;
        }
    }

    for (uint64_t tid : oldSet) {
        if (newSet.find(tid) == newSet.end()) {
            ::ndn::Name prefix("/tag");
            prefix.append(std::to_string(tid));
            FibHelper::RemoveRoute(GetNode(), prefix, incomingFaceId);
        }
    }

    // Update state
    routeEntry.first = incomingFaceId;
    routeEntry.second = lsa.tags;
}

void
TagRouterApp::UpdateRoute(uint64_t tagId, uint32_t faceId)
{
    // Deprecated/Unused
}

} // namespace ndn
} // namespace ns3
