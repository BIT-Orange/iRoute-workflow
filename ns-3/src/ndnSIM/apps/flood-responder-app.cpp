/**
 * @file flood-responder-app.cpp
 * @brief Implementation of FloodResponderApp.
 */

#include "flood-responder-app.hpp"
#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"
#include <cmath>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.FloodResponderApp");
NS_OBJECT_ENSURE_REGISTERED(FloodResponderApp);

TypeId
FloodResponderApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::FloodResponderApp")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<FloodResponderApp>()
        .AddAttribute("Prefix",
                      "Prefix to register (e.g. /domain0)",
                      StringValue("/"),
                      MakeStringAccessor(&FloodResponderApp::m_prefix),
                      MakeStringChecker())
        .AddAttribute("ResponderMode",
                      "Responder mode: domain or producer",
                      StringValue("producer"),
                      MakeStringAccessor(&FloodResponderApp::m_mode),
                      MakeStringChecker())
        .AddAttribute("Threshold",
                      "Minimum similarity to respond (0.0 = always respond)",
                      DoubleValue(0.0),
                      MakeDoubleAccessor(&FloodResponderApp::m_threshold),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("SemVerId",
                      "Semantic version ID",
                      UintegerValue(1),
                      MakeUintegerAccessor(&FloodResponderApp::m_semVerId),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("ReplyDelayUs",
                      "Base processing delay before discovery reply (microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&FloodResponderApp::m_replyDelayUs),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("ReplyJitterUs",
                      "Uniform reply jitter (+/- microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&FloodResponderApp::m_replyJitterUs),
                      MakeUintegerChecker<uint32_t>());
    return tid;
}

void
FloodResponderApp::SetCentroids(const std::vector<iroute::CentroidEntry>& centroids)
{
    m_centroids = centroids;
}

void
FloodResponderApp::SetLocalContent(const std::vector<FloodContentEntry>& content)
{
    m_content = content;
}

void
FloodResponderApp::StartApplication()
{
    App::StartApplication();
    if (!m_replyDelayRv) {
        m_replyDelayRv = CreateObject<UniformRandomVariable>();
    }

    // Register prefix: /<domainId>/iroute/disc
    ::ndn::Name discPrefix(m_prefix);
    discPrefix.append("iroute").append("disc");

    FibHelper::AddRoute(GetNode(), discPrefix, m_face, 0);

    NS_LOG_UNCOND("[FloodResponder] " << m_prefix
                  << " mode=" << m_mode
                  << " centroids=" << m_centroids.size()
                  << " docs=" << m_content.size()
                  << " threshold=" << m_threshold);
}

void
FloodResponderApp::StopApplication()
{
    App::StopApplication();
}

void
FloodResponderApp::OnInterest(shared_ptr<const Interest> interest)
{
    App::OnInterest(interest);
    ++m_queriesReceived;

    // Parse query vector from ApplicationParameters
    iroute::SemanticVector queryVec;
    if (!interest->hasApplicationParameters()) {
        SendNotFound(interest->getName());
        return;
    }

    try {
        const auto& params = interest->getApplicationParameters();
        ::ndn::Block block(nonstd::span<const uint8_t>(params.value(), params.value_size()));
        queryVec.wireDecode(block);
        queryVec.normalize();
    } catch (const std::exception& e) {
        NS_LOG_WARN("Failed to parse query vector: " << e.what());
        SendNotFound(interest->getName());
        return;
    }

    if (m_mode == "domain") {
        HandleDomainMode(interest->getName(), queryVec);
    } else {
        HandleProducerMode(interest->getName(), queryVec);
    }
}

void
FloodResponderApp::HandleDomainMode(const ::ndn::Name& interestName, const iroute::SemanticVector& queryVec)
{
    double bestSim = -1.0;
    for (const auto& c : m_centroids) {
        if (c.C.getDimension() == queryVec.getDimension()) {
            double sim = queryVec.dot(c.C);
            bestSim = std::max(bestSim, sim);
        }
    }

    if (bestSim < m_threshold) {
        SendNotFound(interestName);
        return;
    }

    // Domain mode: we know this domain is relevant but don't have specific doc
    // Return the domain prefix + /data as canonical name
    ::ndn::Name canonicalName(m_prefix);
    canonicalName.append("data");

    SendFound(interestName, canonicalName, bestSim);
}

void
FloodResponderApp::HandleProducerMode(const ::ndn::Name& interestName, const iroute::SemanticVector& queryVec)
{
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
        SendNotFound(interestName);
        return;
    }

    // Construct canonical name: /domain<N>/data/doc/<docId>
    ::ndn::Name canonicalName(m_prefix);
    canonicalName.append("data").append("doc").append(bestDocId);

    SendFound(interestName, canonicalName, bestSim);
}

void
FloodResponderApp::SendFound(const ::ndn::Name& interestName,
               const ::ndn::Name& canonicalName,
               double confidence)
{
    auto data = std::make_shared<Data>(interestName);
    data->setFreshnessPeriod(::ndn::time::milliseconds(100));

    auto replyBlock = iroute::tlv::encodeDiscoveryReply(
        true, canonicalName, confidence, m_semVerId);
    data->setContent(replyBlock);

    m_keyChain.sign(*data);

    m_totalBytesServed += data->wireEncode().size();
    ++m_responsesSent;

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
}

void
FloodResponderApp::SendNotFound(const ::ndn::Name& interestName)
{
    auto data = std::make_shared<Data>(interestName);
    data->setFreshnessPeriod(::ndn::time::milliseconds(100));

    auto replyBlock = iroute::tlv::encodeDiscoveryReply(
        false, ::ndn::Name(), 0.0, m_semVerId);
    data->setContent(replyBlock);

    m_keyChain.sign(*data);

    m_totalBytesServed += data->wireEncode().size();

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
}

} // namespace ndn
} // namespace ns3
