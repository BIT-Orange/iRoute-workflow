/**
 * @file centralized-search-consumer.cpp
 * @brief Implementation of centralized search consumer.
 */

#include "centralized-search-consumer.hpp"
#include "extensions/iroute-tlv.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"

#include <ndn-cxx/encoding/block-helpers.hpp>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.CentralizedSearchConsumer");
NS_OBJECT_ENSURE_REGISTERED(CentralizedSearchConsumer);

// =============================================================================
// TypeId Registration
// =============================================================================

TypeId
CentralizedSearchConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::CentralizedSearchConsumer")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<CentralizedSearchConsumer>()
        .AddAttribute("OraclePrefix",
                      "Prefix of the search oracle",
                      StringValue("/search/oracle"),
                      MakeNameAccessor(&CentralizedSearchConsumer::m_oraclePrefix),
                      MakeNameChecker())
        .AddAttribute("Frequency",
                      "Frequency of queries (per second)",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&CentralizedSearchConsumer::m_frequency),
                      MakeDoubleChecker<double>(0.0))
        .AddAttribute("LifeTime",
                      "Interest lifetime",
                      TimeValue(Seconds(4.0)),
                      MakeTimeAccessor(&CentralizedSearchConsumer::m_interestLifetime),
                      MakeTimeChecker())
        .AddAttribute("FetchTimeoutMs",
                      "Stage-2 fetch timeout in milliseconds",
                      UintegerValue(4000),
                      MakeUintegerAccessor(&CentralizedSearchConsumer::m_fetchTimeoutMs),
                      MakeUintegerChecker<uint32_t>(100));

    return tid;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

CentralizedSearchConsumer::CentralizedSearchConsumer()
    : m_oraclePrefix("/search/oracle")
    , m_interestLifetime(Seconds(4.0))
    , m_fetchTimeoutMs(4000)
    , m_frequency(1.0)
    , m_queryIndex(0)
    , m_totalQueries(0)
    , m_successfulQueries(0)
{
    NS_LOG_FUNCTION(this);
}

CentralizedSearchConsumer::~CentralizedSearchConsumer()
{
    NS_LOG_FUNCTION(this);
}

// =============================================================================
// Configuration
// =============================================================================

void
CentralizedSearchConsumer::SetQueryTrace(const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries)
{
    m_queryTrace.clear();
    for (const auto& q : queries) {
        m_queryTrace.push_back(q);
    }
    m_queryIndex = 0;
    NS_LOG_INFO("CentralizedConsumer: loaded " << m_queryTrace.size() << " queries");
}

// =============================================================================
// Application Lifecycle
// =============================================================================

void
CentralizedSearchConsumer::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();

    NS_LOG_INFO("CentralizedSearchConsumer started, oraclePrefix=" << m_oraclePrefix);

    // Schedule first query
    if (m_frequency > 0 && !m_queryTrace.empty()) {
        m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_frequency),
                                          &CentralizedSearchConsumer::StartNextQuery,
                                          this);
    }
}

void
CentralizedSearchConsumer::StopApplication()
{
    NS_LOG_FUNCTION(this);
    
    Simulator::Cancel(m_sendEvent);
    
    // Cancel all pending timeouts
    for (auto& [name, event] : m_oracleTimeouts) {
        Simulator::Cancel(event);
    }
    for (auto& [name, event] : m_fetchTimeouts) {
        Simulator::Cancel(event);
    }
    
    PrintStats();
    
    App::StopApplication();
}

// =============================================================================
// Stage-1: Query Oracle
// =============================================================================

void
CentralizedSearchConsumer::StartNextQuery()
{
    NS_LOG_FUNCTION(this);

    // Schedule next query
    if (m_frequency > 0) {
        m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_frequency),
                                          &CentralizedSearchConsumer::StartNextQuery,
                                          this);
    }

    // Check if more queries
    if (m_queryIndex >= m_queryTrace.size()) {
        NS_LOG_INFO("Query trace exhausted");
        Simulator::Cancel(m_sendEvent);
        return;
    }

    const auto& query = m_queryTrace[m_queryIndex];
    uint64_t queryId = m_queryIndex;
    ++m_queryIndex;
    ++m_totalQueries;

    // Initialize transaction record
    CentralizedTxRecord tx;
    tx.queryId = queryId;
    tx.startTime = Simulator::Now().GetSeconds();
    tx.tStage1Send = Simulator::Now();
    tx.expectedDomain = query.expectedDomain;
    tx.targetDocIds = query.targetDocIds;
    tx.targetDomains = query.targetDomains;
    tx.probesUsed = 1;  // Single oracle query
    
    m_activeTxs[queryId] = tx;

    // Get query vector
    iroute::SemanticVector qVec = query.vector;
    if (!qVec.isNormalized()) {
        qVec.normalize();
    }

    SendOracleQuery(queryId, qVec);
}

void
CentralizedSearchConsumer::SendOracleQuery(uint64_t queryId, const iroute::SemanticVector& vec)
{
    NS_LOG_FUNCTION(this << queryId);

    // Build oracle query Interest: /search/oracle/<QueryHash>
    Name queryName(m_oraclePrefix);
    queryName.appendNumber(queryId);  // Use queryId as unique identifier

    auto interest = std::make_shared<Interest>(queryName);
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
    std::string queryNameUri = interest->getName().toUri();

    // Track pending query
    m_oracleQueryToId[queryNameUri] = queryId;

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        txIt->second.stage1InterestBytes = interest->wireEncode().size();
    }

    // Send Interest
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule timeout
    Time timeout = m_interestLifetime + MilliSeconds(10);
    m_oracleTimeouts[queryNameUri] = Simulator::Schedule(timeout,
        &CentralizedSearchConsumer::HandleOracleTimeout, this, queryNameUri);

    NS_LOG_DEBUG("Sent oracle query: " << queryNameUri);
}

void
CentralizedSearchConsumer::HandleOracleReply(const Data& data)
{
    NS_LOG_FUNCTION(this << data.getName());

    std::string dataNameUri = data.getName().toUri();

    // Find matching query
    std::string matchedUri;
    uint64_t queryId = 0;
    for (const auto& [uri, qId] : m_oracleQueryToId) {
        if (dataNameUri.find(uri) != std::string::npos || uri.find(dataNameUri) != std::string::npos) {
            matchedUri = uri;
            queryId = qId;
            break;
        }
    }

    if (matchedUri.empty()) {
        NS_LOG_WARN("Unexpected oracle reply: " << dataNameUri);
        return;
    }

    // Cancel timeout
    auto timeoutIt = m_oracleTimeouts.find(matchedUri);
    if (timeoutIt != m_oracleTimeouts.end()) {
        Simulator::Cancel(timeoutIt->second);
        m_oracleTimeouts.erase(timeoutIt);
    }
    m_oracleQueryToId.erase(matchedUri);

    // Update tx record
    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) {
        return;
    }
    CentralizedTxRecord& tx = txIt->second;
    tx.tStage1Recv = Simulator::Now();
    tx.stage1RttMs = (tx.tStage1Recv - tx.tStage1Send).GetSeconds() * 1000.0;
    tx.stage1DataBytes = data.wireEncode().size();
    tx.totalControlBytes = tx.stage1InterestBytes + tx.stage1DataBytes;

    // Parse oracle reply
    const auto& content = data.getContent();
    if (content.value_size() < 1) {
        NS_LOG_WARN("Empty oracle reply");
        tx.failureType = FailureType::DISCOVERY_TIMEOUT;
        tx.failureReason = "EMPTY_REPLY";
        tx.stage1Success = false;
        FinalizeQuery(queryId);
        return;
    }

    try {
        // data.getContent() returns a Content block (type=21) whose value contains
        // the DiscoveryReply block (type=600). We need to construct a Block from the value.
        const auto& contentBlock = data.getContent();
        auto valueSpan = nonstd::span<const uint8_t>(contentBlock.value(), contentBlock.value_size());
        ::ndn::Block innerBlock(valueSpan);
        
        auto replyOpt = iroute::tlv::decodeDiscoveryReply(innerBlock);
        
        if (replyOpt && replyOpt->found && !replyOpt->canonicalName.empty()) {
            // Extract domainId from canonicalName (format: /domain<X>/data/...)
            std::string nameStr = replyOpt->canonicalName.toUri();
            uint32_t domainId = 0;
            size_t pos = nameStr.find("/domain");
            if (pos != std::string::npos) {
                size_t numStart = pos + 7;  // length of "/domain"
                size_t numEnd = nameStr.find("/", numStart);
                if (numEnd == std::string::npos) numEnd = nameStr.length();
                try {
                    domainId = std::stoul(nameStr.substr(numStart, numEnd - numStart));
                } catch (...) {}
            }

            tx.stage1Success = true;
            tx.confidence = replyOpt->confidence;
            tx.selectedDomain = "/domain" + std::to_string(domainId);
            tx.firstChoiceDomain = tx.selectedDomain;
            tx.requestedName = replyOpt->canonicalName.toUri();

            // Proceed to Stage-2
            SendFetchInterest(replyOpt->canonicalName, domainId, queryId);
        } else {
            tx.failureType = FailureType::DISCOVERY_TIMEOUT;
            tx.failureReason = "NOT_FOUND";
            tx.stage1Success = false;
            FinalizeQuery(queryId);
        }
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to parse oracle reply: " << e.what());
        tx.failureType = FailureType::DISCOVERY_TIMEOUT;
        tx.failureReason = "PARSE_ERROR";
        FinalizeQuery(queryId);
    }
}

void
CentralizedSearchConsumer::HandleOracleTimeout(const std::string& queryNameUri)
{
    NS_LOG_FUNCTION(this << queryNameUri);

    auto it = m_oracleQueryToId.find(queryNameUri);
    if (it == m_oracleQueryToId.end()) {
        return;
    }

    uint64_t queryId = it->second;
    m_oracleQueryToId.erase(it);
    m_oracleTimeouts.erase(queryNameUri);

    auto txIt = m_activeTxs.find(queryId);
    if (txIt != m_activeTxs.end()) {
        txIt->second.failureType = FailureType::DISCOVERY_TIMEOUT;
        txIt->second.failureReason = "ORACLE_TIMEOUT";
        txIt->second.stage1Success = false;
    }

    FinalizeQuery(queryId);
}

// =============================================================================
// Stage-2: Fetch Content
// =============================================================================

void
CentralizedSearchConsumer::SendFetchInterest(const Name& canonicalName, 
                                              uint32_t domainId,
                                              uint64_t queryId)
{
    NS_LOG_FUNCTION(this << canonicalName << domainId << queryId);

    auto interest = std::make_shared<Interest>(canonicalName);
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(true);
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
        &CentralizedSearchConsumer::HandleFetchTimeout, this, canonicalName);
}

void
CentralizedSearchConsumer::HandleFetchData(const Data& data)
{
    NS_LOG_FUNCTION(this << data.getName());

    const Name& name = data.getName();
    
    auto it = m_pendingFetch.find(name);
    if (it == m_pendingFetch.end()) {
        NS_LOG_WARN("Unexpected fetch data: " << name);
        return;
    }

    uint64_t queryId = it->second.queryId;
    uint32_t domainId = it->second.domainId;

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

    CentralizedTxRecord& tx = txIt->second;
    tx.tStage2Recv = Simulator::Now();
    tx.stage2RttMs = (tx.tStage2Recv - tx.tStage2Send).GetSeconds() * 1000.0;
    tx.stage2DataBytes = data.wireEncode().size();
    tx.totalDataBytes = tx.stage2InterestBytes + tx.stage2DataBytes;
    tx.stage2Success = true;
    tx.finalSuccessDomain = "/domain" + std::to_string(domainId);

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
    std::string domainStr = "/domain" + std::to_string(domainId);
    for (const auto& targetDomain : tx.targetDomains) {
        if (domainStr.find(targetDomain) != std::string::npos ||
            targetDomain.find(domainStr) != std::string::npos) {
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

    FinalizeQuery(queryId);
}

void
CentralizedSearchConsumer::HandleFetchTimeout(const Name& canonicalName)
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
        txIt->second.failureType = FailureType::FETCH_TIMEOUT;
        txIt->second.failureReason = "FETCH_TIMEOUT";
        txIt->second.stage2Success = false;
        txIt->second.totalMs = (Simulator::Now().GetSeconds() - txIt->second.startTime) * 1000.0;
    }

    FinalizeQuery(queryId);
}

// =============================================================================
// NDN Callbacks
// =============================================================================

void
CentralizedSearchConsumer::OnData(shared_ptr<const Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());
    App::OnData(data);

    const Name& name = data->getName();

    // Check if oracle reply
    if (m_oraclePrefix.isPrefixOf(name)) {
        HandleOracleReply(*data);
    } else {
        // Must be Stage-2 fetch
        HandleFetchData(*data);
    }
}

void
CentralizedSearchConsumer::OnNack(shared_ptr<const lp::Nack> nack)
{
    NS_LOG_FUNCTION(this);
    App::OnNack(nack);

    const Name& name = nack->getInterest().getName();

    // Check if oracle NACK
    if (m_oraclePrefix.isPrefixOf(name)) {
        std::string nameUri = name.toUri();
        auto it = m_oracleQueryToId.find(nameUri);
        if (it != m_oracleQueryToId.end()) {
            uint64_t queryId = it->second;

            auto timeoutIt = m_oracleTimeouts.find(nameUri);
            if (timeoutIt != m_oracleTimeouts.end()) {
                Simulator::Cancel(timeoutIt->second);
                m_oracleTimeouts.erase(timeoutIt);
            }
            m_oracleQueryToId.erase(it);

            auto txIt = m_activeTxs.find(queryId);
            if (txIt != m_activeTxs.end()) {
                txIt->second.failureType = FailureType::DISCOVERY_NACK;
                txIt->second.failureReason = "ORACLE_NACK";
                txIt->second.stage1Success = false;
            }

            FinalizeQuery(queryId);
        }
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

            FinalizeQuery(queryId);
        }
    }
}

// =============================================================================
// Helper Methods
// =============================================================================

void
CentralizedSearchConsumer::FinalizeQuery(uint64_t queryId)
{
    NS_LOG_FUNCTION(this << queryId);

    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) {
        return;
    }

    // Calculate total latency if not set
    CentralizedTxRecord& tx = txIt->second;
    if (tx.totalMs < 0) {
        tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
    }

    // Move to completed transactions
    m_transactions.push_back(tx);
    m_activeTxs.erase(txIt);
}

void
CentralizedSearchConsumer::ExportToCsv(const std::string& filename) const
{
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open " << filename);
        return;
    }

    file << CentralizedTxRecord::csvHeaderExtended() << "\n";
    for (const auto& tx : m_transactions) {
        file << tx.toCsvLineExtended() << "\n";
    }

    NS_LOG_INFO("Exported " << m_transactions.size() << " transactions to " << filename);
}

void
CentralizedSearchConsumer::PrintStats() const
{
    NS_LOG_UNCOND("=== CentralizedSearchConsumer Statistics ===");
    NS_LOG_UNCOND("Total queries: " << m_totalQueries);
    NS_LOG_UNCOND("Successful: " << m_successfulQueries);
    NS_LOG_UNCOND("Success rate: " << (m_totalQueries > 0 ? 
                  100.0 * m_successfulQueries / m_totalQueries : 0.0) << "%");
}

} // namespace ndn
} // namespace ns3
