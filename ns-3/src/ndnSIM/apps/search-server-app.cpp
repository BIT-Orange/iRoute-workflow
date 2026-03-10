/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file search-server-app.cpp
 * @brief Implementation of SearchServerApp for NLSR baseline comparison.
 */

#include "search-server-app.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"

#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"

#include <ndn-cxx/lp/tags.hpp>
#include <ndn-cxx/encoding/buffer-stream.hpp>

#include <algorithm>
#include <cmath>

NS_LOG_COMPONENT_DEFINE("ndn.SearchServerApp");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(SearchServerApp);

TypeId
SearchServerApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::SearchServerApp")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<SearchServerApp>()
        .AddAttribute("Prefix", "Search prefix to listen on",
                      StringValue("/search"),
                      MakeNameAccessor(&SearchServerApp::m_prefix),
                      MakeNameChecker())
        .AddAttribute("PayloadSize", "Size of the resolution response payload",
                      UintegerValue(256),
                      MakeUintegerAccessor(&SearchServerApp::m_payloadSize),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("Freshness", "Freshness of resolution Data packets",
                      TimeValue(Seconds(10)),
                      MakeTimeAccessor(&SearchServerApp::m_freshness),
                      MakeTimeChecker());
    return tid;
}

SearchServerApp::SearchServerApp()
{
    NS_LOG_FUNCTION(this);
}

SearchServerApp::~SearchServerApp()
{
    NS_LOG_FUNCTION(this);
}

void
SearchServerApp::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();
    
    // Register prefix with FIB
    FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
    
    NS_LOG_INFO("SearchServerApp started on " << GetNode()->GetId() 
                << ", prefix=" << m_prefix 
                << ", indexed " << m_contentIndex.size() << " entries");
}

void
SearchServerApp::StopApplication()
{
    NS_LOG_FUNCTION(this);
    App::StopApplication();
}

void
SearchServerApp::RegisterContent(const std::string& name, 
                                  uint32_t producerId,
                                  const iroute::SemanticVector& vector)
{
    ContentRegistration entry;
    entry.contentName = name;
    entry.producerId = producerId;
    entry.vector = vector;
    m_contentIndex.push_back(entry);
    
    NS_LOG_DEBUG("Registered content: " << name << " (producer=" << producerId << ")");
}

std::pair<std::string, double>
SearchServerApp::ResolveQuery(const iroute::SemanticVector& query) const
{
    if (m_contentIndex.empty()) {
        return {"", 0.0};
    }
    
    std::string bestName;
    double bestSim = -1.0;
    
    for (const auto& entry : m_contentIndex) {
        double sim = query.computeCosineSimilarity(entry.vector);
        if (sim > bestSim) {
            bestSim = sim;
            bestName = entry.contentName;
        }
    }
    
    return {bestName, bestSim};
}

void
SearchServerApp::OnInterest(shared_ptr<const Interest> interest)
{
    NS_LOG_FUNCTION(this << interest->getName());
    
    App::OnInterest(interest);
    
    m_queriesReceived++;
    
    // Extract query vector from ApplicationParameters
    iroute::SemanticVector queryVector;
    bool hasVector = false;
    
    if (interest->hasApplicationParameters()) {
        try {
            ::ndn::Block params = interest->getApplicationParameters();
            // ApplicationParameters is a TLV containing our SemanticVector
            params.parse();
            
            if (params.elements_size() > 0) {
                // Use the first element
                queryVector.wireDecode(params.elements().front());
            } else {
                // Parse value as complete TLV Block
                ::ndn::Block vectorBlock(nonstd::span<const uint8_t>(params.value(), params.value_size()));
                queryVector.wireDecode(vectorBlock);
            }
            hasVector = true;
            NS_LOG_DEBUG("Decoded query vector, dim=" << queryVector.getDimension());
        } catch (const std::exception& e) {
            NS_LOG_WARN("Failed to decode query vector: " << e.what());
        }
    }
    
    if (!hasVector) {
        NS_LOG_WARN("No query vector in Interest, ignoring");
        return;
    }
    
    // Resolve query to best matching content
    auto [resolvedName, similarity] = ResolveQuery(queryVector);
    
    if (resolvedName.empty()) {
        NS_LOG_WARN("No matching content found");
        return;
    }
    
    NS_LOG_INFO("Resolved query to: " << resolvedName << " (sim=" << similarity << ")");
    
    // Create Data packet with TLV-encoded response
    auto data = std::make_shared<::ndn::Data>(interest->getName());
    data->setFreshnessPeriod(::ndn::time::milliseconds(m_freshness.GetMilliSeconds()));
    
    // Build TLV response:
    // 200: ResolvedName (Name wire encoding)
    // 201: Similarity (float32)
    // 202: ProducerId (uint32)
    
    // Get producer ID for the resolved content
    uint32_t producerId = 0;
    for (const auto& entry : m_contentIndex) {
        if (entry.contentName == resolvedName) {
            producerId = entry.producerId;
            break;
        }
    }
    
    // TLV type constants (matching search-consumer.hpp)
    constexpr uint32_t TLV_ResolvedName = 200;
    constexpr uint32_t TLV_Similarity = 201;
    constexpr uint32_t TLV_ProducerId = 202;
    
    // Encode the resolved name
    ::ndn::Name resolvedNameObj(resolvedName);
    ::ndn::Block nameBlock = resolvedNameObj.wireEncode();
    
    // Build TLV blocks using makeNonNegativeIntegerBlock and makeStringBlock approach
    // For binary data, use Block constructor with span
    
    // Create individual TLV blocks
    ::ndn::Block nameElement(TLV_ResolvedName, 
        std::make_shared<::ndn::Buffer>(nameBlock.wire(), nameBlock.wire() + nameBlock.size()));
    
    float simFloat = static_cast<float>(similarity);
    ::ndn::Block simElement(TLV_Similarity,
        std::make_shared<::ndn::Buffer>(reinterpret_cast<const uint8_t*>(&simFloat), 
                                         reinterpret_cast<const uint8_t*>(&simFloat) + sizeof(float)));
    
    ::ndn::Block prodElement(TLV_ProducerId,
        std::make_shared<::ndn::Buffer>(reinterpret_cast<const uint8_t*>(&producerId), 
                                         reinterpret_cast<const uint8_t*>(&producerId) + sizeof(uint32_t)));
    
    // Build content as a sequence of these elements
    // We'll create the content by concatenating blocks
    size_t totalSize = nameElement.size() + simElement.size() + prodElement.size();
    auto contentBuffer = std::make_shared<::ndn::Buffer>(totalSize);
    uint8_t* ptr = contentBuffer->data();
    
    std::memcpy(ptr, nameElement.wire(), nameElement.size());
    ptr += nameElement.size();
    std::memcpy(ptr, simElement.wire(), simElement.size());
    ptr += simElement.size();
    std::memcpy(ptr, prodElement.wire(), prodElement.size());
    
    data->setContent(contentBuffer);
    
    // Sign and send
    m_keyChain.sign(*data);
    
    NS_LOG_DEBUG("Sending TLV resolution Data: " << data->getName() 
                 << " resolvedName=" << resolvedName
                 << " similarity=" << similarity
                 << " producerId=" << producerId);
    
    m_transmittedDatas(data, this, m_face);
    m_appLink->onReceiveData(*data);
    
    m_resolutionsReturned++;
}

} // namespace ndn
} // namespace ns3
