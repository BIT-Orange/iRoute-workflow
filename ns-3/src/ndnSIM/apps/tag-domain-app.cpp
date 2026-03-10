/**
 * @file tag-domain-app.cpp
 * @brief Implementation of TagDomainApp.
 */

#include "tag-domain-app.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
#include "ns3/simulator.h"
#include <cmath>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.TagDomainApp");
NS_OBJECT_ENSURE_REGISTERED(TagDomainApp);

TypeId
TagDomainApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::TagDomainApp")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<TagDomainApp>()
        .AddAttribute("DomainPrefix",
                      "Domain prefix (e.g. /domain0)",
                      StringValue("/"),
                      MakeStringAccessor(&TagDomainApp::SetDomainPrefix),
                      MakeStringChecker())
        .AddAttribute("Threshold",
                      "Minimum similarity to respond",
                      DoubleValue(0.0),
                      MakeDoubleAccessor(&TagDomainApp::m_threshold),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("ReplyDelayUs",
                      "Base processing delay before discovery reply (microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&TagDomainApp::m_replyDelayUs),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("ReplyJitterUs",
                      "Uniform reply jitter (+/- microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&TagDomainApp::m_replyJitterUs),
                      MakeUintegerChecker<uint32_t>());
    return tid;
}

TagDomainApp::TagDomainApp()
    : m_threshold(0.0)
{
}

void
TagDomainApp::SetDomainPrefix(const std::string& prefix)
{
    m_domainPrefix = prefix;
}

void
TagDomainApp::SetTags(const std::vector<uint64_t>& tags)
{
    m_tags = tags;
}

void
TagDomainApp::SetLocalContent(const std::vector<TagContentEntry>& content)
{
    m_content = content;
}

void
TagDomainApp::StartApplication()
{
    App::StartApplication();
    if (!m_replyDelayRv) {
        m_replyDelayRv = CreateObject<UniformRandomVariable>();
    }

    // Register /tag/<tid>/disc for all tags
    for (uint64_t tid : m_tags) {
        ::ndn::Name prefix("/tag");
        prefix.append(std::to_string(tid));
        prefix.append("disc");

        FibHelper::AddRoute(GetNode(), prefix, m_face, 0);
    }

    NS_LOG_INFO("[TagDomain] Started " << m_domainPrefix 
                << " Tags=" << m_tags.size()
                << " Docs=" << m_content.size());
}

void
TagDomainApp::StopApplication()
{
    App::StopApplication();
}

void
TagDomainApp::OnInterest(shared_ptr<const Interest> interest)
{
    App::OnInterest(interest);

    // Parse query vector
    if (!interest->hasApplicationParameters()) {
        SendNotFound(*interest);
        return;
    }

    iroute::SemanticVector queryVec;
    try {
        const auto& params = interest->getApplicationParameters();
        ::ndn::Block block(nonstd::span<const uint8_t>(params.value(), params.value_size()));
        queryVec.wireDecode(block);
        queryVec.normalize();
    } catch (const std::exception& e) {
        NS_LOG_WARN("Failed to parse vector: " << e.what());
        SendNotFound(*interest);
        return;
    }

    // Find best doc
    double bestSim = -1.0;
    std::string bestDocId;

    for (const auto& doc : m_content) {
        if (doc.vector.getDimension() == queryVec.getDimension()) {
            double sim = queryVec.dot(doc.vector);
            if (sim > bestSim) {
                bestSim = sim;
                bestDocId = doc.docId;
            }
        }
    }

    if (bestSim < m_threshold || bestDocId.empty()) {
        SendNotFound(*interest);
        return;
    }

    // Found!
    SendFound(*interest, bestDocId, bestSim);
}

void
TagDomainApp::SendFound(const Interest& interest, const std::string& docId, double confidence)
{
    auto data = std::make_shared<Data>(interest.getName());
    data->setFreshnessPeriod(::ndn::time::milliseconds(100));

    // Construct Canonical Name: /domain<N>/data/doc/<docId>
    ::ndn::Name canonicalName(m_domainPrefix);
    if (m_domainPrefix.empty() || m_domainPrefix.back() != '/') {
        // Appending 'data'
        // If prefix is /domain0, append /data
        // Use logic from FloodResponderApp?
        // Ensure '/data' exists?
        // Let's assume user passes /domain0. We append /data/doc/<id>.
    }
    // Just force append:
    canonicalName.append("data").append("doc").append(docId);

    auto replyBlock = iroute::tlv::encodeDiscoveryReply(
        true, canonicalName, confidence, m_semVerId);
    data->setContent(replyBlock);

    m_keyChain.sign(*data);

    auto sendData = [this, data]() {
        m_transmittedDatas(data, this, m_face);
        m_appLink->onReceiveData(*data);
    };
    int64_t delayUs = static_cast<int64_t>(m_replyDelayUs);
    if (m_replyJitterUs > 0 && m_replyDelayRv) {
        delayUs += static_cast<int64_t>(std::llround(m_replyDelayRv->GetValue(
            -static_cast<double>(m_replyJitterUs),
            static_cast<double>(m_replyJitterUs))));
    }
    delayUs = std::max<int64_t>(0, delayUs);
    if (delayUs > 0) {
        Simulator::Schedule(MicroSeconds(delayUs), sendData);
    } else {
        sendData();
    }
    
    NS_LOG_DEBUG("[TagDomain] Found " << docId << " sim=" << confidence << " for " << interest.getName());
}

void
TagDomainApp::SendNotFound(const Interest& interest)
{
    auto data = std::make_shared<Data>(interest.getName());
    data->setFreshnessPeriod(::ndn::time::milliseconds(100));

    auto replyBlock = iroute::tlv::encodeDiscoveryReply(
        false, ::ndn::Name(), 0.0, m_semVerId);
    data->setContent(replyBlock);

    m_keyChain.sign(*data);

    auto sendData = [this, data]() {
        m_transmittedDatas(data, this, m_face);
        m_appLink->onReceiveData(*data);
    };
    int64_t delayUs = static_cast<int64_t>(m_replyDelayUs);
    if (m_replyJitterUs > 0 && m_replyDelayRv) {
        delayUs += static_cast<int64_t>(std::llround(m_replyDelayRv->GetValue(
            -static_cast<double>(m_replyJitterUs),
            static_cast<double>(m_replyJitterUs))));
    }
    delayUs = std::max<int64_t>(0, delayUs);
    if (delayUs > 0) {
        Simulator::Schedule(MicroSeconds(delayUs), sendData);
    } else {
        sendData();
    }

    NS_LOG_DEBUG("[TagDomain] Not Found for " << interest.getName());
}

} // namespace ndn
} // namespace ns3
