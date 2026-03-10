/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file semantic-producer.cpp
 * @brief Implementation of SemanticProducer application.
 *
 * @see semantic-producer.hpp for class documentation
 * @author iRoute Team
 * @date 2024
 */

#include "semantic-producer.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"

#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"

#include <cstring>
#include <cmath>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.SemanticProducer");

NS_OBJECT_ENSURE_REGISTERED(SemanticProducer);

TypeId
SemanticProducer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::SemanticProducer")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<SemanticProducer>()
        .AddAttribute("Prefix",
                      "Prefix, for which producer has the data",
                      StringValue("/"),
                      MakeNameAccessor(&SemanticProducer::m_prefix),
                      MakeNameChecker())
        .AddAttribute("PayloadSize",
                      "Virtual payload size for Data packets",
                      UintegerValue(1024),
                      MakeUintegerAccessor(&SemanticProducer::m_payloadSize),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("Freshness",
                      "Freshness of data packets, if 0, then unlimited freshness",
                      TimeValue(Seconds(0)),
                      MakeTimeAccessor(&SemanticProducer::m_freshness),
                      MakeTimeChecker())
        .AddAttribute("VectorDim",
                      "Expected dimension for semantic vectors (default: 384)",
                      UintegerValue(384),
                      MakeUintegerAccessor(&SemanticProducer::m_vectorDim),
                      MakeUintegerChecker<uint32_t>(1))
        .AddAttribute("Active",
                      "If false, the producer drops all interests (simulates failure)",
                      BooleanValue(true),
                      MakeBooleanAccessor(&SemanticProducer::m_active),
                      MakeBooleanChecker())
        .AddAttribute("ReplyDelayUs",
                      "Base processing delay before Data reply (microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&SemanticProducer::m_replyDelayUs),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("ReplyJitterUs",
                      "Uniform per-Interest jitter (+/- microseconds) for Data reply",
                      UintegerValue(0),
                      MakeUintegerAccessor(&SemanticProducer::m_replyJitterUs),
                      MakeUintegerChecker<uint32_t>());
    return tid;
}

SemanticProducer::SemanticProducer()
    : m_payloadSize(1024)
    , m_freshness(Seconds(0))
{
    NS_LOG_FUNCTION(this);
    m_replyDelayRv = CreateObject<UniformRandomVariable>();
}

void
SemanticProducer::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();
    
    // Register prefix with FIB
    FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
    
    NS_LOG_INFO("SemanticProducer started on node " << GetNode()->GetId()
                << ", prefix=" << m_prefix
                << ", payloadSize=" << m_payloadSize);
}

void
SemanticProducer::StopApplication()
{
    NS_LOG_FUNCTION(this);
    App::StopApplication();
}

void
SemanticProducer::OnInterest(shared_ptr<const Interest> interest)
{
    NS_LOG_FUNCTION(this << interest->getName());
    App::OnInterest(interest);
    
    if (!m_active) {
        return;
    }
    
    // =========================================================================
    // KEY FIX: Use the EXACT Interest Name (including params-sha256)
    // =========================================================================
    // The Interest Name already contains the params-sha256 component if
    // ApplicationParameters were present. Using this exact name for the
    // Data packet ensures that PIT lookup will succeed.
    
    Name dataName = interest->getName();
    
    // Extract content ID from Interest name for logging
    std::string contentId = "unknown";
    if (dataName.size() > m_prefix.size()) {
        contentId = dataName.get(m_prefix.size()).toUri();
    }
    
    NS_LOG_INFO("[Producer] OnInterest: name=" << dataName 
                << ", contentId=" << contentId
                << ", producerId=" << m_producerId);
    
    if (!m_active) {
        NS_LOG_INFO("Producer is inactive, dropping Interest " << interest->getName());
        return;
    }

    
    // Create Data packet with the exact Interest Name
    auto data = make_shared<Data>();
    data->setName(dataName);
    
    // Set freshness period (convert ns3::Time to ndn::time)
    if (m_freshness.GetMilliSeconds() > 0) {
        data->setFreshnessPeriod(::ndn::time::milliseconds(m_freshness.GetMilliSeconds()));
    }
    
    // Generate payload with producer ID and semantic vector
    // Format: [producer_id (4 bytes)] [vector_size (4 bytes)] [semantic_vector (N floats)] [padding]
    // This allows Consumer to compute semantic similarity for evaluation
    
    std::vector<uint8_t> payloadData;
    
    // 1. Embed producer ID (4 bytes)
    payloadData.resize(sizeof(uint32_t));
    std::memcpy(payloadData.data(), &m_producerId, sizeof(uint32_t));
    
    // 2. Embed semantic vector if available
    if (!m_nodeVector.empty()) {
        const auto& vecData = m_nodeVector.getData();
        uint32_t vecSize = static_cast<uint32_t>(vecData.size());
        
        // Append vector size (4 bytes)
        size_t offset = payloadData.size();
        payloadData.resize(offset + sizeof(uint32_t));
        std::memcpy(payloadData.data() + offset, &vecSize, sizeof(uint32_t));
        
        // Append vector floats
        offset = payloadData.size();
        size_t vecBytes = vecSize * sizeof(float);
        payloadData.resize(offset + vecBytes);
        std::memcpy(payloadData.data() + offset, vecData.data(), vecBytes);
    } else {
        // No vector: write 0 size
        uint32_t zero = 0;
        size_t offset = payloadData.size();
        payloadData.resize(offset + sizeof(uint32_t));
        std::memcpy(payloadData.data() + offset, &zero, sizeof(uint32_t));
    }
    
    // 3. Pad to m_payloadSize if needed
    if (payloadData.size() < m_payloadSize) {
        payloadData.resize(m_payloadSize, 0);
    }
    
    auto buffer = make_shared<::ndn::Buffer>(payloadData.begin(), payloadData.end());
    data->setContent(buffer);
    
    // Sign the data packet
    m_keyChain.sign(*data);
    
    NS_LOG_INFO("SemanticProducer on node(" << GetNode()->GetId() 
                << ") responding with Data: " << data->getName());
    
    auto sendData = [this, data]() {
        m_transmittedDatas(data, this, m_face);
        m_appLink->onReceiveData(*data);
    };

    int64_t delayUs = static_cast<int64_t>(m_replyDelayUs);
    if (m_replyJitterUs > 0 && m_replyDelayRv) {
        double jitterUs = m_replyDelayRv->GetValue(
            -static_cast<double>(m_replyJitterUs),
            static_cast<double>(m_replyJitterUs));
        delayUs += static_cast<int64_t>(std::llround(jitterUs));
    }
    delayUs = std::max<int64_t>(0, delayUs);
    if (delayUs > 0) {
        Simulator::Schedule(MicroSeconds(delayUs), sendData);
    } else {
        sendData();
    }
}

void
SemanticProducer::SetNodeVector(const iroute::SemanticVector& vector)
{
    m_nodeVector = vector;
    NS_LOG_INFO("SemanticProducer on node " << GetNode()->GetId() 
                << " set node vector (dim=" << vector.getDimension() << ")");
}

void
SemanticProducer::AddContent(const std::string& contentName, 
                              const iroute::SemanticVector& semanticVector)
{
    m_contentRegistry[contentName] = semanticVector;
    NS_LOG_DEBUG("SemanticProducer on node " << GetNode()->GetId() 
                 << " added content: " << contentName
                 << " (total=" << m_contentRegistry.size() << ")");
}

} // namespace ndn
} // namespace ns3
