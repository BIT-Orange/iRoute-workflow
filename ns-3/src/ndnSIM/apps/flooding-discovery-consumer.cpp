/**
 * @file flooding-discovery-consumer.cpp
 * @brief Implementation of flooding baseline consumer.
 */

#include "flooding-discovery-consumer.hpp"
#include "extensions/iroute-tlv.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"

#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/lp/tags.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.FloodingDiscoveryConsumer");
NS_OBJECT_ENSURE_REGISTERED(FloodingDiscoveryConsumer);

// =============================================================================
// TypeId Registration
// =============================================================================

TypeId
FloodingDiscoveryConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::FloodingDiscoveryConsumer")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<FloodingDiscoveryConsumer>()
        .AddAttribute("Frequency",
                      "Frequency of queries (per second)",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&FloodingDiscoveryConsumer::m_frequency),
                      MakeDoubleChecker<double>(0.0))
        .AddAttribute("LifeTime",
                      "Interest lifetime",
                      TimeValue(Seconds(4.0)),
                      MakeTimeAccessor(&FloodingDiscoveryConsumer::m_interestLifetime),
                      MakeTimeChecker())
        .AddAttribute("SemVerId",
                      "Semantic version ID",
                      UintegerValue(1),
                      MakeUintegerAccessor(&FloodingDiscoveryConsumer::m_semVerId),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("ProbeBudget",
                      "Maximum number of domains to probe per query (0=all)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&FloodingDiscoveryConsumer::m_probeBudget),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("FetchTimeoutMs",
                      "Stage-2 fetch timeout in milliseconds",
                      UintegerValue(4000),
                      MakeUintegerAccessor(&FloodingDiscoveryConsumer::m_fetchTimeoutMs),
                      MakeUintegerChecker<uint32_t>(100))
        .AddAttribute("ParallelMode",
                      "True for parallel flooding, false for sequential",
                      BooleanValue(true),
                      MakeBooleanAccessor(&FloodingDiscoveryConsumer::m_parallelMode),
                      MakeBooleanChecker());

    return tid;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

FloodingDiscoveryConsumer::FloodingDiscoveryConsumer()
    : m_parallelMode(true)
    , m_semVerId(1)
    , m_probeBudget(0)
    , m_interestLifetime(Seconds(4.0))
    , m_fetchTimeoutMs(4000)
    , m_frequency(1.0)
    , m_queryIndex(0)
    , m_totalQueries(0)
    , m_successfulQueries(0)
{
    NS_LOG_FUNCTION(this);
}

FloodingDiscoveryConsumer::~FloodingDiscoveryConsumer()
{
    NS_LOG_FUNCTION(this);
}

// =============================================================================
// Configuration
// =============================================================================

void
FloodingDiscoveryConsumer::SetAllDomains(const std::vector<Name>& domains)
{
    m_allDomains = domains;
    NS_LOG_INFO("FloodingConsumer: configured " << domains.size() << " domains");
}

void
FloodingDiscoveryConsumer::SetQueryTrace(const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries)
{
    m_queryTrace.clear();
    for (const auto& q : queries) {
        m_queryTrace.push_back(q);
    }
    m_queryIndex = 0;
    NS_LOG_INFO("FloodingConsumer: loaded " << m_queryTrace.size() << " queries");
}

// =============================================================================
// Application Lifecycle
// =============================================================================

void
FloodingDiscoveryConsumer::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();

    NS_LOG_INFO("FloodingDiscoveryConsumer started, mode=" 
                << (m_parallelMode ? "parallel" : "sequential")
                << ", domains=" << m_allDomains.size());

    // Schedule first query
    if (m_frequency > 0 && !m_queryTrace.empty()) {
        m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_frequency),
                                          &FloodingDiscoveryConsumer::StartNextQuery,
                                          this);
    }
}

void
FloodingDiscoveryConsumer::StopApplication()
{
    NS_LOG_FUNCTION(this);
    
    Simulator::Cancel(m_sendEvent);
    
    // Cancel all pending timeouts
    for (auto& [name, event] : m_discoveryTimeouts) {
        Simulator::Cancel(event);
    }
    for (auto& [name, event] : m_fetchTimeouts) {
        Simulator::Cancel(event);
    }
    
    PrintStats();
    
    App::StopApplication();
}

// =============================================================================
// Stage-1: Flooding Discovery
// =============================================================================

void
FloodingDiscoveryConsumer::StartNextQuery()
{
    NS_LOG_FUNCTION(this);

    // Schedule next query
    if (m_frequency > 0) {
        m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_frequency),
                                          &FloodingDiscoveryConsumer::StartNextQuery,
                                          this);
    }

    // Check if more queries
    if (m_queryIndex >= m_queryTrace.size()) {
        NS_LOG_INFO("Query trace exhausted");
        Simulator::Cancel(m_sendEvent);
        return;
    }

    const auto& query = m_queryTrace[m_queryIndex];
    uint64_t queryId = query.id;
    if (m_activeTxs.count(queryId) || m_pendingDiscoveries.count(queryId) ||
        m_bestResponses.count(queryId)) {
        queryId = static_cast<uint64_t>(m_queryIndex);
    }
    ++m_queryIndex;
    ++m_totalQueries;

    // Initialize transaction record
    FloodingTxRecord tx;
    tx.queryId = queryId;
    tx.startTime = Simulator::Now().GetSeconds();
    tx.tStage1Send = Simulator::Now();
    tx.expectedDomain = query.expectedDomain;
    tx.targetDocIds = query.targetDocIds;
    tx.targetDomains = query.targetDomains;
    tx.isParallelMode = m_parallelMode;
    if (m_probeBudget > 0 && m_probeBudget < m_allDomains.size()) {
        tx.totalInterestsSent = m_probeBudget;
    } else {
        tx.totalInterestsSent = m_allDomains.size();
    }
    
    m_activeTxs[queryId] = tx;
    
    // Initialize tracking structures
    m_pendingDiscoveries[queryId] = std::set<std::string>();
    m_bestResponses[queryId] = BestResponse();
    m_candidateScores[queryId] = std::map<std::string, double>();

    // Get query vector
    iroute::SemanticVector qVec = query.vector;
    if (!qVec.isNormalized()) {
        qVec.normalize();
    }

    // Send to all domains (or override)
    SendDiscoveryQuery(queryId, qVec);
}

void
FloodingDiscoveryConsumer::SendDiscoveryQuery(uint64_t queryId, const iroute::SemanticVector& vec)
{
    // Default behavior: Flood everything
    SendDiscoveryToAllDomains(queryId, vec);
}

void
FloodingDiscoveryConsumer::SendDiscoveryToAllDomains(uint64_t queryId, const iroute::SemanticVector& vec)
{
    NS_LOG_FUNCTION(this << queryId);
    if (m_allDomains.empty()) {
        return;
    }

    uint32_t budget = m_probeBudget;
    if (budget == 0 || budget >= m_allDomains.size()) {
        for (const auto& domain : m_allDomains) {
            SendDiscoveryToDomain(domain, queryId, vec);
        }
        NS_LOG_DEBUG("Sent " << m_allDomains.size() << " discovery Interests for queryId=" << queryId);
        return;
    }

    // Deterministic rotating window to avoid fixed-prefix bias across queries.
    uint32_t start = static_cast<uint32_t>(queryId % m_allDomains.size());
    for (uint32_t i = 0; i < budget; ++i) {
        const auto& domain = m_allDomains[(start + i) % m_allDomains.size()];
        SendDiscoveryToDomain(domain, queryId, vec);
    }
    NS_LOG_DEBUG("Sent " << budget << " discovery Interests for queryId=" << queryId);
}

void
FloodingDiscoveryConsumer::SendDiscoveryToDomain(const Name& domainId, uint64_t queryId, 
                                                  const iroute::SemanticVector& vec)
{
    NS_LOG_FUNCTION(this << domainId << queryId);

    // Build Discovery Interest: /<DomainID>/iroute/disc/<SemVerID>
    Name discName(domainId);
    discName.append("iroute");
    discName.append("disc");
    discName.appendNumber(m_semVerId);

    auto interest = std::make_shared<Interest>(discName);
    interest->setCanBePrefix(true);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));

    // Encode query vector
    try {
        ::ndn::Block vectorBlock = vec.wireEncode();
        interest->setApplicationParameters(vectorBlock);
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to encode query vector: " << e.what());
        return;
    }

    interest->wireEncode();
    std::string discNameUri = interest->getName().toUri();

    // Track pending discovery
    m_pendingDiscoveries[queryId].insert(discNameUri);
    m_discNameToQueryId[discNameUri] = queryId;
    m_discNameToDomain[discNameUri] = domainId;

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        txIt->second.stage1InterestBytes += interest->wireEncode().size();
    }

    // Send Interest
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule timeout
    Time timeout = m_interestLifetime + MilliSeconds(10);
    m_discoveryTimeouts[discNameUri] = Simulator::Schedule(timeout,
        &FloodingDiscoveryConsumer::HandleDiscoveryTimeout, this, discNameUri);
}

void
FloodingDiscoveryConsumer::HandleDiscoveryReply(const Data& data)
{
    NS_LOG_FUNCTION(this << data.getName());

    std::string dataNameUri = data.getName().toUri();

    auto discIt = m_discNameToQueryId.find(dataNameUri);
    if (discIt == m_discNameToQueryId.end()) {
        NS_LOG_WARN("Received unexpected discovery reply: " << dataNameUri);
        return;
    }

    uint64_t queryId = discIt->second;
    Name domainId = m_discNameToDomain[dataNameUri];

    // Cancel timeout
    auto timeoutIt = m_discoveryTimeouts.find(dataNameUri);
    if (timeoutIt != m_discoveryTimeouts.end()) {
        Simulator::Cancel(timeoutIt->second);
        m_discoveryTimeouts.erase(timeoutIt);
    }

    // Remove from pending
    m_pendingDiscoveries[queryId].erase(dataNameUri);
    m_discNameToQueryId.erase(dataNameUri);
    m_discNameToDomain.erase(dataNameUri);

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) {
        return; // Already finalized
    }
    FloodingTxRecord& tx = txIt->second;
    tx.responsesReceived++;
    tx.stage1DataBytes += data.wireEncode().size();

    // Parse discovery reply
    const auto& content = data.getContent();
    if (content.value_size() < 1) {
        NS_LOG_WARN("Empty discovery reply from " << domainId);
        CheckAllProbesComplete(queryId);
        return;
    }

    // Parse TLV using proper decoder (matches IRouteApp encoding)
    try {
        const auto& content = data.getContent();
        ::ndn::Block block(nonstd::span<const uint8_t>(content.value(), content.value_size()));
        auto replyOpt = iroute::tlv::decodeDiscoveryReply(block);
        
        if (replyOpt) {
            const auto& reply = *replyOpt;
            
            if (reply.found && !reply.canonicalName.empty()) {
                // Found!
                tx.stage1Success = true;
                tx.tStage1Recv = Simulator::Now();
                tx.stage1RttMs = (tx.tStage1Recv - tx.tStage1Send).GetSeconds() * 1000.0;
                tx.confidence = reply.confidence;
                tx.selectedDomain = domainId.toUri();
                auto& cand = m_candidateScores[queryId];
                auto candIt = cand.find(tx.selectedDomain);
                if (candIt == cand.end() || reply.confidence > candIt->second) {
                    cand[tx.selectedDomain] = reply.confidence;
                }

                // Update best response (use highest confidence)
                auto& best = m_bestResponses[queryId];
                if (!best.found || reply.confidence > best.confidence) {
                    best.found = true;
                    best.canonicalName = reply.canonicalName;
                    best.domainId = domainId;
                    best.confidence = reply.confidence;
                    NS_LOG_DEBUG("Best candidate updated: domain=" << domainId 
                                 << " canonical=" << reply.canonicalName
                                 << " conf=" << reply.confidence);
                }
            }
        }
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to parse discovery reply: " << e.what());
    }

    CheckAllProbesComplete(queryId);
}

void
FloodingDiscoveryConsumer::HandleDiscoveryTimeout(const std::string& discNameUri)
{
    NS_LOG_FUNCTION(this << discNameUri);

    auto discIt = m_discNameToQueryId.find(discNameUri);
    if (discIt == m_discNameToQueryId.end()) {
        return;
    }

    uint64_t queryId = discIt->second;

    // Remove from tracking
    m_pendingDiscoveries[queryId].erase(discNameUri);
    m_discNameToQueryId.erase(discNameUri);
    m_discNameToDomain.erase(discNameUri);
    m_discoveryTimeouts.erase(discNameUri);

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        txIt->second.timeoutsOccurred++;
    }

    CheckAllProbesComplete(queryId);
}

void
FloodingDiscoveryConsumer::CheckAllProbesComplete(uint64_t queryId)
{
    NS_LOG_FUNCTION(this << queryId);

    // Check if all probes have returned
    if (!m_pendingDiscoveries[queryId].empty()) {
        return;  // Still waiting for more responses
    }

    // All probes complete - proceed with best response
    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) {
        return;
    }

    FloodingTxRecord& tx = txIt->second;
    auto& best = m_bestResponses[queryId];

    if (!best.found) {
        // No successful discovery
        tx.failureType = FailureType::DISCOVERY_TIMEOUT;
        tx.failureReason = "ALL_DOMAINS_FAILED";
        tx.stage1Success = false;
        tx.stage2Success = false;
        tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
        FinalizeQuery(queryId, false);
        return;
    }

    // Found best candidate - proceed to Stage-2
    tx.firstChoiceDomain = best.domainId.toUri();
    tx.requestedName = best.canonicalName.toUri();
    tx.probesUsed = tx.responsesReceived;
    {
        std::vector<std::pair<std::string, double>> ranked;
        auto it = m_candidateScores.find(queryId);
        if (it != m_candidateScores.end()) {
            for (const auto& kv : it->second) {
                ranked.push_back(kv);
            }
        }
        if (ranked.empty()) {
            ranked.push_back({best.domainId.toUri(), best.confidence});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });
        std::ostringstream oss;
        const size_t kMaxList = 10;
        for (size_t i = 0; i < ranked.size() && i < kMaxList; ++i) {
            if (i > 0) {
                oss << ";";
            }
            oss << ranked[i].first << "=" << std::fixed << std::setprecision(4) << ranked[i].second;
        }
        tx.topKList = oss.str();
    }

    // Calculate total control bytes
    tx.totalControlBytes = tx.stage1InterestBytes + tx.stage1DataBytes;

    // Update Stage-1 RTT (time to first successful response)
    tx.tStage1Recv = Simulator::Now();
    if (tx.stage1RttMs < 0) {
        tx.stage1RttMs = (tx.tStage1Recv - tx.tStage1Send).GetSeconds() * 1000.0;
    }

    SendFetchInterest(best.canonicalName, best.domainId, queryId);
}

// =============================================================================
// Stage-2: Fetch
// =============================================================================

void
FloodingDiscoveryConsumer::SendFetchInterest(const Name& canonicalName, 
                                              const Name& domainId, 
                                              uint64_t queryId)
{
    NS_LOG_FUNCTION(this << canonicalName << domainId << queryId);

    auto interest = std::make_shared<Interest>(canonicalName);
    interest->setCanBePrefix(false);
    // interest->setMustBeFresh(true); // Removed to avoid potential NFD drops
    interest->setInterestLifetime(ndn::time::milliseconds(m_fetchTimeoutMs));

    // Track pending fetch
    PendingFetchInfo info;
    info.domainId = domainId;
    info.queryId = queryId;
    m_pendingFetch[canonicalName] = info;

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        txIt->second.tStage2Send = Simulator::Now();
        txIt->second.stage2InterestBytes = interest->wireEncode().size();
    }

    // Send Interest
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule timeout
    m_fetchTimeouts[canonicalName] = Simulator::Schedule(
        MilliSeconds(m_fetchTimeoutMs + 10),
        &FloodingDiscoveryConsumer::HandleFetchTimeout, this, canonicalName);
}

void
FloodingDiscoveryConsumer::HandleFetchData(const Data& data)
{
    NS_LOG_FUNCTION(this << data.getName());

    const Name& name = data.getName();
    
    auto it = m_pendingFetch.find(name);
    if (it == m_pendingFetch.end()) {
        NS_LOG_WARN("Unexpected fetch data: " << name);
        return;
    }

    uint64_t queryId = it->second.queryId;
    Name domainId = it->second.domainId;

    // Cancel timeout
    auto timeoutIt = m_fetchTimeouts.find(name);
    if (timeoutIt != m_fetchTimeouts.end()) {
        Simulator::Cancel(timeoutIt->second);
        m_fetchTimeouts.erase(timeoutIt);
    }

    m_pendingFetch.erase(it);

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) {
        return;
    }

    FloodingTxRecord& tx = txIt->second;
    tx.tStage2Recv = Simulator::Now();
    tx.stage2RttMs = (tx.tStage2Recv - tx.tStage2Send).GetSeconds() * 1000.0;
    tx.stage2DataBytes = data.wireEncode().size();
    tx.totalDataBytes = tx.stage2InterestBytes + tx.stage2DataBytes;
    tx.stage2Success = true;
    tx.finalSuccessDomain = domainId.toUri();

    // Record hop count for Stage-2 (used by hop/load experiments)
    auto hopCountTag = data.getTag<::ndn::lp::HopCountTag>();
    if (hopCountTag) {
        tx.stage2HopCount = static_cast<int32_t>(*hopCountTag);
    }
    else {
        tx.stage2HopCount = -1;
    }
    tx.stage2FromCache = (tx.stage2HopCount == 0);

    // Calculate total latency
    tx.totalMs = (tx.tStage2Recv - tx.tStage1Send).GetSeconds() * 1000.0;

    // Check if returned doc is correct
    std::string returnedDoc = name.toUri();
    bool docCorrect = false;
    for (const auto& targetDoc : tx.targetDocIds) {
        if (returnedDoc.find(targetDoc) != std::string::npos) {
            docCorrect = true;
            break;
        }
    }

    // Check if domain was correct
    bool domainCorrect = false;
    for (const auto& targetDomain : tx.targetDomains) {
        if (domainId.toUri().find(targetDomain) != std::string::npos ||
            targetDomain.find(domainId.toUri()) != std::string::npos) {
            domainCorrect = true;
            break;
        }
    }

    // Set failure type
    if (docCorrect) {
        tx.failureType = FailureType::SUCCESS;
        ++m_successfulQueries;
    } else if (domainCorrect) {
        tx.failureType = FailureType::DOC_WRONG;
        tx.failureReason = "DOC_WRONG";
    } else {
        tx.failureType = FailureType::DOMAIN_WRONG;
        tx.failureReason = "DOMAIN_WRONG";
    }

    FinalizeQuery(queryId, true);
}

void
FloodingDiscoveryConsumer::HandleFetchTimeout(const Name& canonicalName)
{
    NS_LOG_FUNCTION(this << canonicalName);

    auto it = m_pendingFetch.find(canonicalName);
    if (it == m_pendingFetch.end()) {
        return;
    }

    uint64_t queryId = it->second.queryId;
    m_pendingFetch.erase(it);
    m_fetchTimeouts.erase(canonicalName);

    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        FloodingTxRecord& tx = txIt->second;
        tx.failureType = FailureType::FETCH_TIMEOUT;
        tx.failureReason = "FETCH_TIMEOUT";
        tx.stage2Success = false;
        tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
    }

    FinalizeQuery(queryId, false);
}

// =============================================================================
// NDN Callbacks
// =============================================================================

void
FloodingDiscoveryConsumer::OnData(shared_ptr<const Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());
    App::OnData(data);

    const Name& name = data->getName();
    std::string nameUri = name.toUri();

    // Check if discovery reply
    if (m_discNameToQueryId.find(nameUri) != m_discNameToQueryId.end()) {
        HandleDiscoveryReply(*data);
    } else {
        // Must be Stage-2 fetch
        HandleFetchData(*data);
    }
}

void
FloodingDiscoveryConsumer::OnNack(shared_ptr<const lp::Nack> nack)
{
    NS_LOG_FUNCTION(this);
    App::OnNack(nack);

    const Name& name = nack->getInterest().getName();
    std::string nameUri = name.toUri();

    // Check if discovery NACK
    auto discIt = m_discNameToQueryId.find(nameUri);
    if (discIt != m_discNameToQueryId.end()) {
        uint64_t queryId = discIt->second;

        // Cancel timeout
        auto timeoutIt = m_discoveryTimeouts.find(nameUri);
        if (timeoutIt != m_discoveryTimeouts.end()) {
            Simulator::Cancel(timeoutIt->second);
            m_discoveryTimeouts.erase(timeoutIt);
        }

        // Remove from pending
        m_pendingDiscoveries[queryId].erase(nameUri);
        m_discNameToQueryId.erase(nameUri);
        m_discNameToDomain.erase(nameUri);

        // Update tx record
        auto txIt = m_activeTxs.find(queryId);
        if (txIt != m_activeTxs.end()) {
            txIt->second.nacksReceived++;
        }

        CheckAllProbesComplete(queryId);
    } else {
        // Stage-2 NACK
        auto it = m_pendingFetch.find(name);
        if (it != m_pendingFetch.end()) {
            uint64_t queryId = it->second.queryId;

            auto timeoutIt = m_fetchTimeouts.find(name);
            if (timeoutIt != m_fetchTimeouts.end()) {
                Simulator::Cancel(timeoutIt->second);
                m_fetchTimeouts.erase(timeoutIt);
            }

            m_pendingFetch.erase(it);

            auto txIt = m_activeTxs.find(queryId);
            if (txIt != m_activeTxs.end()) {
                txIt->second.failureType = FailureType::FETCH_NACK;
                txIt->second.failureReason = "FETCH_NACK";
                txIt->second.stage2Success = false;
            }

            FinalizeQuery(queryId, false);
        }
    }
}

// =============================================================================
// Helper Methods
// =============================================================================

void
FloodingDiscoveryConsumer::FinalizeQuery(uint64_t queryId, bool stage2Success)
{
    NS_LOG_FUNCTION(this << queryId << stage2Success);

    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) {
        return;
    }

    // Move to completed transactions
    m_transactions.push_back(txIt->second);
    m_activeTxs.erase(txIt);

    // Clean up tracking structures
    m_pendingDiscoveries.erase(queryId);
    m_bestResponses.erase(queryId);
    m_candidateScores.erase(queryId);
}

void
FloodingDiscoveryConsumer::ExportToCsv(const std::string& filename) const
{
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open " << filename);
        return;
    }

    file << FloodingTxRecord::csvHeaderExtended() << "\n";
    for (const auto& tx : m_transactions) {
        file << tx.toCsvLineExtended() << "\n";
    }

    NS_LOG_INFO("Exported " << m_transactions.size() << " transactions to " << filename);
}

void
FloodingDiscoveryConsumer::PrintStats() const
{
    NS_LOG_UNCOND("=== FloodingDiscoveryConsumer Statistics ===");
    NS_LOG_UNCOND("Total queries: " << m_totalQueries);
    NS_LOG_UNCOND("Successful: " << m_successfulQueries);
    NS_LOG_UNCOND("Success rate: " << (m_totalQueries > 0 ? 
                  100.0 * m_successfulQueries / m_totalQueries : 0.0) << "%");
    NS_LOG_UNCOND("Mode: " << (m_parallelMode ? "parallel" : "sequential"));
}

} // namespace ndn
} // namespace ns3
