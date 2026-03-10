/**
 * @file semantic-consumer.cpp
 * @brief Implementation of the SemanticConsumer application.
 *
 * @author iRoute Team
 * @date 2024
 */

#include "semantic-consumer.hpp"
#include "experiment-data-loader.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.SemanticConsumer");

NS_OBJECT_ENSURE_REGISTERED(SemanticConsumer);

// =============================================================================
// NS-3 TypeId Registration
// =============================================================================

TypeId
SemanticConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::SemanticConsumer")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<SemanticConsumer>()
        .AddAttribute("Prefix",
                      "Name prefix to request",
                      StringValue("/semantic/query"),
                      MakeNameAccessor(&SemanticConsumer::m_prefix),
                      MakeNameChecker())
        .AddAttribute("Frequency",
                      "Rate at which Interests are sent (per second)",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&SemanticConsumer::m_frequency),
                      MakeDoubleChecker<double>())
        .AddAttribute("LifeTime",
                      "Interest packet lifetime",
                      TimeValue(Seconds(4.0)),
                      MakeTimeAccessor(&SemanticConsumer::m_interestLifetime),
                      MakeTimeChecker())
        .AddAttribute("VectorDim",
                      "Expected dimension for semantic vectors (default: 384)",
                      UintegerValue(384),
                      MakeUintegerAccessor(&SemanticConsumer::m_vectorDim),
                      MakeUintegerChecker<uint32_t>(1));

    return tid;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

SemanticConsumer::SemanticConsumer()
    : m_frequency(1.0)
    , m_interestLifetime(Seconds(4.0))
    , m_vectorDim(384)
    , m_queryIndex(0)
    , m_seqNum(0)
    , m_interestsSent(0)
    , m_dataReceived(0)
    , m_nacksReceived(0)
    , m_semanticHits(0)
    , m_semanticMisses(0)
    , m_highSimHits(0)
    , m_highSimMisses(0)
    , m_medSimHits(0)
    , m_medSimMisses(0)
    , m_lowSimHits(0)
    , m_lowSimMisses(0)
{
    NS_LOG_FUNCTION(this);
}

SemanticConsumer::~SemanticConsumer()
{
    NS_LOG_FUNCTION(this);
}

// =============================================================================
// Configuration
// =============================================================================

void
SemanticConsumer::SetQueryVector(const iroute::SemanticVector& vector)
{
    NS_LOG_FUNCTION(this << vector.getDimension());
    m_queryVector = vector;
}

const iroute::SemanticVector&
SemanticConsumer::GetQueryVector() const
{
    return m_queryVector;
}

void
SemanticConsumer::SetQueryTrace(const std::vector<ConsumerQueryEntry>& trace)
{
    NS_LOG_FUNCTION(this << trace.size());
    m_queryTrace = trace;
    m_queryIndex = 0;
    NS_LOG_INFO("SemanticConsumer loaded " << trace.size() << " query entries");
}

// =============================================================================
// Application Lifecycle
// =============================================================================

void
SemanticConsumer::StartApplication()
{
    NS_LOG_FUNCTION(this);

    App::StartApplication();

    NS_LOG_INFO("SemanticConsumer started on node " << GetNode()->GetId()
                << ", prefix=" << m_prefix
                << ", frequency=" << m_frequency
                << ", queryVectorDim=" << m_queryVector.getDimension());

    // Schedule first Interest
    ScheduleNextInterest();
}

void
SemanticConsumer::StopApplication()
{
    NS_LOG_FUNCTION(this);

    Simulator::Cancel(m_sendEvent);

    NS_LOG_INFO("SemanticConsumer stopped on node " << GetNode()->GetId()
                << ", sent=" << m_interestsSent
                << ", received=" << m_dataReceived
                << ", nacks=" << m_nacksReceived);

    // Print structured CSV statistics for paper experiments
    PrintStats();

    App::StopApplication();
}

// =============================================================================
// Ground Truth & Statistics
// =============================================================================

void
SemanticConsumer::AddGroundTruth(uint64_t seqNo, uint32_t expectedProducerId, double similarity)
{
    m_groundTruth[seqNo] = {expectedProducerId, similarity};
    NS_LOG_DEBUG("Added ground truth: seqNo=" << seqNo 
                 << ", expectedProd=" << expectedProducerId
                 << ", similarity=" << similarity);
}

void
SemanticConsumer::PrintStats() const
{
    // Output CSV-format statistics for semantic accuracy by similarity bucket
    // Format: similarity_bucket,total_interests,satisfied,semantic_hits
    
    uint32_t highTotal = m_highSimHits + m_highSimMisses;
    uint32_t medTotal = m_medSimHits + m_medSimMisses;
    uint32_t lowTotal = m_lowSimHits + m_lowSimMisses;
    
    std::cout << "\n--- Semantic Accuracy CSV ---\n";
    std::cout << "similarity_bucket,total_interests,satisfied,semantic_hits\n";
    
    // High similarity (>= 0.8)
    if (highTotal > 0) {
        std::cout << "high_0.8+," << highTotal << "," << highTotal << "," << m_highSimHits << "\n";
    }
    
    // Medium similarity (0.5-0.8)
    if (medTotal > 0) {
        std::cout << "med_0.5-0.8," << medTotal << "," << medTotal << "," << m_medSimHits << "\n";
    }
    
    // Low similarity (< 0.5)
    if (lowTotal > 0) {
        std::cout << "low_<0.5," << lowTotal << "," << lowTotal << "," << m_lowSimHits << "\n";
    }
    
    // Overall totals
    std::cout << "total," << m_interestsSent << "," << m_dataReceived << "," 
              << m_semanticHits << "\n";
    std::cout << "---\n";
}

// =============================================================================
// NDN Callbacks
// =============================================================================

void
SemanticConsumer::OnData(shared_ptr<const Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());

    App::OnData(data);

    ++m_dataReceived;

    // Extract sequence number from Data name (last component)
    const Name& name = data->getName();
    uint64_t seqNum = 0;
    if (name.size() > 0) {
        try {
            seqNum = name.get(-1).toNumber();
        } catch (...) {
            NS_LOG_WARN("Could not extract seqNum from Data name");
        }
    }

    // Calculate RTT if we have a pending Interest record
    auto pendingIt = m_pendingInterests.find(seqNum);
    if (pendingIt != m_pendingInterests.end()) {
        double rtt = (Simulator::Now() - pendingIt->second).GetSeconds();
        m_rttSamples.push_back(rtt);
        m_pendingInterests.erase(pendingIt);
        NS_LOG_DEBUG("RTT for seqNo=" << seqNum << " is " << rtt << "s");
    }

    // Extract producer ID and semantic vector from Data content
    // Format: [producer_id (4 bytes)] [vector_size (4 bytes)] [semantic_vector (N floats)]
    uint32_t actualProducerId = 0;
    std::vector<float> producerVecData;
    double semanticSimilarity = 0.0;
    
    const auto& content = data->getContent();
    const uint8_t* bytes = content.value();
    size_t bytesLen = content.value_size();
    size_t offset = 0;
    
    // 1. Extract producer ID
    if (bytesLen >= sizeof(uint32_t)) {
        std::memcpy(&actualProducerId, bytes + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
    }
    
    // 2. Extract vector size
    uint32_t vecSize = 0;
    if (bytesLen >= offset + sizeof(uint32_t)) {
        std::memcpy(&vecSize, bytes + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
    }
    
    // 3. Extract semantic vector floats
    if (vecSize > 0 && bytesLen >= offset + vecSize * sizeof(float)) {
        producerVecData.resize(vecSize);
        std::memcpy(producerVecData.data(), bytes + offset, vecSize * sizeof(float));
    }
    
    // 4. Compute semantic similarity with query vector
    if (!producerVecData.empty() && !m_queryVector.empty()) {
        const auto& queryData = m_queryVector.getData();
        if (queryData.size() == producerVecData.size()) {
            // Compute cosine similarity
            double dotProduct = 0.0, normA = 0.0, normB = 0.0;
            for (size_t i = 0; i < queryData.size(); ++i) {
                dotProduct += queryData[i] * producerVecData[i];
                normA += queryData[i] * queryData[i];
                normB += producerVecData[i] * producerVecData[i];
            }
            if (normA > 0 && normB > 0) {
                semanticSimilarity = dotProduct / (std::sqrt(normA) * std::sqrt(normB));
            }
        }
    }

    // Semantic hit criteria: similarity > 0.5 (adjusted for wiki dataset)
    constexpr double kSemanticHitThreshold = 0.5;
    bool isSemanticHit = (semanticSimilarity >= kSemanticHitThreshold);
    bool isExactHit = false;

    // Lookup ground truth for this sequence number (legacy exact match)
    auto gtIt = m_groundTruth.find(seqNum);
    if (gtIt != m_groundTruth.end()) {
        const auto& gt = gtIt->second;
        isExactHit = (actualProducerId == gt.expectedProducerId);
        m_groundTruth.erase(gtIt);
    }

    // Update counters - use SEMANTIC similarity for main hits
    if (isSemanticHit) {
        ++m_semanticHits;
    } else {
        ++m_semanticMisses;
    }

    // Update per-similarity-level counters
    if (semanticSimilarity >= 0.9) {
        ++m_highSimHits;
    } else if (semanticSimilarity >= 0.7) {
        ++m_medSimHits;
    } else if (semanticSimilarity >= 0.5) {
        ++m_medSimMisses;  // Medium similarity but below threshold
    } else {
        ++m_lowSimMisses;  // Low similarity
    }

    NS_LOG_DEBUG("SemanticAccuracy: seq=" << seqNum 
                 << ", producerId=" << actualProducerId
                 << ", similarity=" << semanticSimilarity
                 << ", semanticHit=" << isSemanticHit
                 << ", exactHit=" << isExactHit);

    std::cout << "[Consumer] OnData: " << data->getName() 
              << " (total=" << m_dataReceived 
              << ", sim=" << std::fixed << std::setprecision(3) << semanticSimilarity
              << ", hits=" << m_semanticHits 
              << ", misses=" << m_semanticMisses << ")" << std::endl;

    NS_LOG_INFO("Received Data: " << data->getName()
                << " (total=" << m_dataReceived << ")");
}

void
SemanticConsumer::OnNack(shared_ptr<const lp::Nack> nack)
{
    NS_LOG_FUNCTION(this);

    App::OnNack(nack);

    ++m_nacksReceived;

    NS_LOG_INFO("Received NACK for " << nack->getInterest().getName()
                << " reason=" << nack->getReason()
                << " (total=" << m_nacksReceived << ")");
}

// =============================================================================
// Internal Methods
// =============================================================================

void
SemanticConsumer::SendInterest()
{
    NS_LOG_FUNCTION(this);

    if (!m_active) {
        return;
    }

    // Determine which query vector to use
    iroute::SemanticVector queryVec = m_queryVector;
    uint32_t expectedProducerId = 0;
    double similarity = 0.0;

    // If using query trace, get the next entry
    if (!m_queryTrace.empty() && m_queryIndex < m_queryTrace.size()) {
        const auto& entry = m_queryTrace[m_queryIndex];
        queryVec = entry.queryVector;
        expectedProducerId = entry.expectedProducerId;
        similarity = entry.similarity;
        ++m_queryIndex;
        
        // [FIX] Update m_queryVector so OnData can compute semantic similarity
        // Without this, m_queryVector stays empty when using QueryTrace mode
        m_queryVector = queryVec;
    }

    // Build Interest name with current sequence number
    uint64_t seqNum = m_seqNum++;
    Name interestName(m_prefix);
    interestName.appendNumber(seqNum);

    // Record ground truth for this query
    m_groundTruth[seqNum] = {expectedProducerId, similarity};

    // Create Interest
    auto interest = std::make_shared<Interest>(interestName);
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(false);
    interest->setInterestLifetime(::ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));

    // Encode the semantic query vector into ApplicationParameters
    if (!queryVec.empty()) {
        try {
            ::ndn::Block vectorBlock = queryVec.wireEncode();
            interest->setApplicationParameters(vectorBlock);
            
            NS_LOG_DEBUG("Attached SemanticVector to Interest, dim=" 
                         << queryVec.getDimension()
                         << ", expectedProd=" << expectedProducerId);
        }
        catch (const std::exception& e) {
            NS_LOG_ERROR("Failed to encode query vector: " << e.what());
        }
    }
    else {
        NS_LOG_WARN("Query vector is empty, Interest will not contain semantic data");
    }

    // Send the Interest
    NS_LOG_INFO("Sending semantic Interest: " << interestName);

    ++m_interestsSent;

    // Record send time for RTT measurement
    m_pendingInterests[seqNum] = Simulator::Now();

    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule next Interest
    ScheduleNextInterest();
}

void
SemanticConsumer::ScheduleNextInterest()
{
    if (!m_active || m_frequency <= 0) {
        return;
    }

    m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_frequency),
                                       &SemanticConsumer::SendInterest,
                                       this);
}

std::tuple<double, double, double>
SemanticConsumer::GetRttStats() const
{
    if (m_rttSamples.empty()) {
        return {0.0, 0.0, 0.0};
    }

    // Calculate mean
    double sum = 0.0;
    for (double rtt : m_rttSamples) {
        sum += rtt;
    }
    double mean = sum / m_rttSamples.size();

    // Sort samples for percentile calculation
    std::vector<double> sorted = m_rttSamples;
    std::sort(sorted.begin(), sorted.end());

    // Calculate percentiles
    size_t n = sorted.size();
    size_t p90Index = static_cast<size_t>(std::ceil(0.90 * n)) - 1;
    size_t p99Index = static_cast<size_t>(std::ceil(0.99 * n)) - 1;

    double p90 = sorted[std::min(p90Index, n - 1)];
    double p99 = sorted[std::min(p99Index, n - 1)];

    return {mean, p90, p99};
}

} // namespace ndn
} // namespace ns3
