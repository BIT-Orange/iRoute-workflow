/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file search-consumer.cpp
 * @brief Implementation of two-stage Search Consumer.
 */

#include "search-consumer.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ns3/ptr.h"

#include <ndn-cxx/encoding/block.hpp>
#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/lp/tags.hpp>

#include <numeric>
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.SearchConsumer");

NS_OBJECT_ENSURE_REGISTERED(SearchConsumer);

TypeId
SearchConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::SearchConsumer")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<SearchConsumer>()
        .AddAttribute("SearchPrefix",
                      "Search service prefix",
                      StringValue("/search"),
                      MakeNameAccessor(&SearchConsumer::m_searchPrefix),
                      MakeNameChecker())
        .AddAttribute("Frequency",
                      "Query sending frequency (Hz)",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&SearchConsumer::m_frequency),
                      MakeDoubleChecker<double>())
        .AddAttribute("LifeTime",
                      "Interest packet lifetime",
                      TimeValue(Seconds(4.0)),
                      MakeTimeAccessor(&SearchConsumer::m_interestLifetime),
                      MakeTimeChecker())
        .AddAttribute("VectorDim",
                      "Expected vector dimension",
                      UintegerValue(384),
                      MakeUintegerAccessor(&SearchConsumer::m_vectorDim),
                      MakeUintegerChecker<uint32_t>(1));

    return tid;
}

SearchConsumer::SearchConsumer()
    : m_frequency(1.0)
    , m_interestLifetime(Seconds(4.0))
    , m_vectorDim(384)
{
    NS_LOG_FUNCTION(this);
    m_jitter = CreateObject<UniformRandomVariable>();
    m_jitter->SetAttribute("Min", DoubleValue(0.0));
    m_jitter->SetAttribute("Max", DoubleValue(0.1));
}

SearchConsumer::~SearchConsumer()
{
    NS_LOG_FUNCTION(this);
}

void
SearchConsumer::SetQueryVector(const iroute::SemanticVector& vec)
{
    m_queryTrace.clear();
    m_queryTrace.push_back(vec);
    m_queryIndex = 0;
    m_currentQuery = vec;
}

void
SearchConsumer::SetQueryTrace(const std::vector<iroute::SemanticVector>& trace)
{
    m_queryTrace = trace;
    m_queryIndex = 0;
    if (!trace.empty()) {
        m_currentQuery = trace[0];
    }
}

void
SearchConsumer::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();

    NS_LOG_INFO("SearchConsumer started on node " << GetNode()->GetId()
                << ", searchPrefix=" << m_searchPrefix
                << ", frequency=" << m_frequency << " Hz");

    ScheduleNextQuery();
}

void
SearchConsumer::StopApplication()
{
    NS_LOG_FUNCTION(this);
    Simulator::Cancel(m_sendEvent);
    App::StopApplication();

    // Move any incomplete transactions to completed for stats
    for (auto& kv : m_transactions) {
        m_completedTransactions.push_back(kv.second);
    }
    m_transactions.clear();
}

void
SearchConsumer::ScheduleNextQuery()
{
    if (m_queryTrace.empty()) {
        NS_LOG_WARN("No query trace set, cannot schedule query");
        return;
    }

    double interval = 1.0 / m_frequency;
    double jitter = m_jitter->GetValue();
    Time nextTime = Seconds(interval + jitter);

    m_sendEvent = Simulator::Schedule(nextTime, &SearchConsumer::SendQuery, this);
}

void
SearchConsumer::SendQuery()
{
    NS_LOG_FUNCTION(this);

    // Get current query vector
    if (m_queryIndex < m_queryTrace.size()) {
        m_currentQuery = m_queryTrace[m_queryIndex];
        m_queryIndex = (m_queryIndex + 1) % m_queryTrace.size();
    }

    // Create query Interest with SemanticVector in ApplicationParameters
    uint64_t seqNum = m_seqNum++;
    
    // Build Interest name: /search/<seqNum>
    ::ndn::Name queryName(m_searchPrefix);
    queryName.appendNumber(seqNum);

    auto interest = std::make_shared<::ndn::Interest>(queryName);
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(::ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));

    // Attach SemanticVector as ApplicationParameters
    ::ndn::Block vectorBlock = m_currentQuery.wireEncode();
    interest->setApplicationParameters(vectorBlock);

    // Create transaction record
    TxRecord tx;
    tx.seqNum = seqNum;
    tx.tQuerySend = Simulator::Now();
    m_transactions[seqNum] = tx;

    NS_LOG_DEBUG("SendQuery: " << interest->getName() << " (seq=" << seqNum << ")");

    m_appLink->onReceiveInterest(*interest);
    m_queryInterestsSent++;

    ScheduleNextQuery();
}

void
SearchConsumer::OnQueryData(std::shared_ptr<const ::ndn::Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());

    // Extract seqNum from name: /search/<seqNum>/...
    const ::ndn::Name& name = data->getName();
    if (name.size() < 2) {
        NS_LOG_WARN("Invalid query response name: " << name);
        return;
    }

    uint64_t seqNum = name.get(-2).toNumber(); // Get seqNum component

    auto it = m_transactions.find(seqNum);
    if (it == m_transactions.end()) {
        NS_LOG_WARN("Received query response for unknown seqNum: " << seqNum);
        return;
    }

    TxRecord& tx = it->second;
    tx.tQueryRecv = Simulator::Now();
    tx.gotQuery = true;
    m_queryDataReceived++;

    // Parse SearchServer TLV response
    const ::ndn::Block& content = data->getContent();
    ::ndn::Name resolvedName;
    double similarity = 0.0;
    uint32_t producerId = 0;

    if (!ParseSearchResponse(content, resolvedName, similarity, producerId)) {
        NS_LOG_WARN("Failed to parse SearchServer response for seq=" << seqNum);
        // Move to completed even on parse failure
        m_completedTransactions.push_back(tx);
        m_transactions.erase(it);
        return;
    }

    tx.resolvedName = resolvedName;
    tx.similarity = similarity;
    tx.producerId = producerId;

    NS_LOG_DEBUG("OnQueryData: seq=" << seqNum 
                 << " resolvedName=" << resolvedName
                 << " similarity=" << similarity
                 << " producerId=" << producerId);

    // Initiate fetch phase
    SendFetch(seqNum, resolvedName);
}

void
SearchConsumer::OnQueryTimeout(std::shared_ptr<const ::ndn::Interest> interest)
{
    NS_LOG_FUNCTION(this << interest->getName());

    const ::ndn::Name& name = interest->getName();
    if (name.size() < 2) return;

    uint64_t seqNum = name.get(-1).toNumber();
    auto it = m_transactions.find(seqNum);
    if (it != m_transactions.end()) {
        m_completedTransactions.push_back(it->second);
        m_transactions.erase(it);
    }

    NS_LOG_DEBUG("Query timeout: " << name);
}

void
SearchConsumer::SendFetch(uint64_t seqNum, const ::ndn::Name& resolvedName)
{
    NS_LOG_FUNCTION(this << seqNum << resolvedName);

    auto it = m_transactions.find(seqNum);
    if (it == m_transactions.end()) {
        NS_LOG_WARN("Cannot send fetch for unknown seqNum: " << seqNum);
        return;
    }

    TxRecord& tx = it->second;
    tx.tFetchSend = Simulator::Now();

    // Create fetch Interest
    auto interest = std::make_shared<::ndn::Interest>(resolvedName);
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(false);
    interest->setInterestLifetime(::ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));

    // Use a tag to carry seqNum for correlation in OnFetchData
    // Note: We encode seqNum in the Interest name for simplicity
    // Format: resolvedName/<seqNum>
    ::ndn::Name fetchName = resolvedName;
    fetchName.appendNumber(seqNum);
    interest->setName(fetchName);

    NS_LOG_DEBUG("SendFetch: " << interest->getName() << " (seq=" << seqNum << ")");

    m_appLink->onReceiveInterest(*interest);
    m_fetchInterestsSent++;
}

void
SearchConsumer::OnFetchData(std::shared_ptr<const ::ndn::Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());

    const ::ndn::Name& name = data->getName();
    if (name.size() < 1) {
        NS_LOG_WARN("Invalid fetch response name: " << name);
        return;
    }

    // Extract seqNum from last component
    uint64_t seqNum = name.get(-1).toNumber();

    auto it = m_transactions.find(seqNum);
    if (it == m_transactions.end()) {
        NS_LOG_WARN("Received fetch response for unknown seqNum: " << seqNum);
        return;
    }

    TxRecord& tx = it->second;
    tx.tFetchRecv = Simulator::Now();
    tx.gotFetch = true;
    m_fetchDataReceived++;

    NS_LOG_DEBUG("OnFetchData: seq=" << seqNum 
                 << " queryRTT=" << (tx.tQueryRecv - tx.tQuerySend).GetMilliSeconds() << "ms"
                 << " fetchRTT=" << (tx.tFetchRecv - tx.tFetchSend).GetMilliSeconds() << "ms"
                 << " e2eRTT=" << (tx.tFetchRecv - tx.tQuerySend).GetMilliSeconds() << "ms");

    // Move to completed
    m_completedTransactions.push_back(tx);
    m_transactions.erase(it);
}

void
SearchConsumer::OnFetchTimeout(std::shared_ptr<const ::ndn::Interest> interest)
{
    NS_LOG_FUNCTION(this << interest->getName());

    const ::ndn::Name& name = interest->getName();
    if (name.size() < 1) return;

    uint64_t seqNum = name.get(-1).toNumber();
    auto it = m_transactions.find(seqNum);
    if (it != m_transactions.end()) {
        m_completedTransactions.push_back(it->second);
        m_transactions.erase(it);
    }

    NS_LOG_DEBUG("Fetch timeout: " << name);
}

bool
SearchConsumer::ParseSearchResponse(const ::ndn::Block& content,
                                     ::ndn::Name& resolvedName,
                                     double& similarity,
                                     uint32_t& producerId)
{
    try {
        content.parse();

        // Look for TLV elements
        auto resolvedIt = content.find(search_tlv::ResolvedName);
        auto simIt = content.find(search_tlv::Similarity);
        auto prodIt = content.find(search_tlv::ProducerId);

        if (resolvedIt == content.elements_end()) {
            NS_LOG_WARN("Missing ResolvedName TLV in response");
            return false;
        }

        // Parse ResolvedName
        resolvedName.wireDecode(resolvedIt->blockFromValue());

        // Parse Similarity (optional)
        if (simIt != content.elements_end() && simIt->value_size() >= sizeof(float)) {
            float simVal;
            std::memcpy(&simVal, simIt->value(), sizeof(float));
            similarity = static_cast<double>(simVal);
        }

        // Parse ProducerId (optional)
        if (prodIt != content.elements_end() && prodIt->value_size() >= sizeof(uint32_t)) {
            std::memcpy(&producerId, prodIt->value(), sizeof(uint32_t));
        }

        return true;
    }
    catch (const std::exception& e) {
        NS_LOG_WARN("Exception parsing search response: " << e.what());
        return false;
    }
}

double
SearchConsumer::GetQueryISR() const
{
    if (m_queryInterestsSent == 0) return 0.0;
    return 100.0 * m_queryDataReceived / m_queryInterestsSent;
}

double
SearchConsumer::GetFetchISR() const
{
    if (m_queryDataReceived == 0) return 0.0;
    return 100.0 * m_fetchDataReceived / m_queryDataReceived;
}

double
SearchConsumer::GetE2EISR() const
{
    if (m_queryInterestsSent == 0) return 0.0;
    return 100.0 * m_fetchDataReceived / m_queryInterestsSent;
}

double
SearchConsumer::GetQueryRttAvg() const
{
    std::vector<double> rtts;
    for (const auto& tx : m_completedTransactions) {
        if (tx.gotQuery) {
            rtts.push_back((tx.tQueryRecv - tx.tQuerySend).GetMilliSeconds());
        }
    }
    if (rtts.empty()) return 0.0;
    return std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();
}

double
SearchConsumer::GetFetchRttAvg() const
{
    std::vector<double> rtts;
    for (const auto& tx : m_completedTransactions) {
        if (tx.gotFetch) {
            rtts.push_back((tx.tFetchRecv - tx.tFetchSend).GetMilliSeconds());
        }
    }
    if (rtts.empty()) return 0.0;
    return std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();
}

double
SearchConsumer::GetE2ERttAvg() const
{
    std::vector<double> rtts;
    for (const auto& tx : m_completedTransactions) {
        if (tx.gotFetch) {
            rtts.push_back((tx.tFetchRecv - tx.tQuerySend).GetMilliSeconds());
        }
    }
    if (rtts.empty()) return 0.0;
    return std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();
}

void
SearchConsumer::PrintStats() const
{
    std::cout << "\n--- SearchConsumer Statistics ---\n";
    std::cout << "Query Interests Sent:  " << m_queryInterestsSent << "\n";
    std::cout << "Query Data Received:   " << m_queryDataReceived << "\n";
    std::cout << "Query ISR:             " << std::fixed << std::setprecision(1) 
              << GetQueryISR() << "%\n";
    std::cout << "Query RTT Avg:         " << std::setprecision(2) 
              << GetQueryRttAvg() << " ms\n";
    std::cout << "\n";
    std::cout << "Fetch Interests Sent:  " << m_fetchInterestsSent << "\n";
    std::cout << "Fetch Data Received:   " << m_fetchDataReceived << "\n";
    std::cout << "Fetch ISR:             " << std::setprecision(1) 
              << GetFetchISR() << "%\n";
    std::cout << "Fetch RTT Avg:         " << std::setprecision(2) 
              << GetFetchRttAvg() << " ms\n";
    std::cout << "\n";
    std::cout << "E2E ISR:               " << std::setprecision(1) 
              << GetE2EISR() << "%\n";
    std::cout << "E2E RTT Avg:           " << std::setprecision(2) 
              << GetE2ERttAvg() << " ms\n";
    std::cout << "---------------------------------\n";
}

void
SearchConsumer::ExportCsv(const std::string& filename) const
{
    std::ofstream ofs(filename);
    if (!ofs) {
        NS_LOG_WARN("Cannot open file for CSV export: " << filename);
        return;
    }

    ofs << "seq,query_sent_ms,query_recv_ms,query_rtt_ms,fetch_sent_ms,fetch_recv_ms,fetch_rtt_ms,e2e_rtt_ms,similarity,producer_id,resolved_name\n";

    for (const auto& tx : m_completedTransactions) {
        double querySent = tx.tQuerySend.GetMilliSeconds();
        double queryRecv = tx.gotQuery ? tx.tQueryRecv.GetMilliSeconds() : -1;
        double queryRtt = tx.gotQuery ? (tx.tQueryRecv - tx.tQuerySend).GetMilliSeconds() : -1;
        double fetchSent = tx.gotQuery ? tx.tFetchSend.GetMilliSeconds() : -1;
        double fetchRecv = tx.gotFetch ? tx.tFetchRecv.GetMilliSeconds() : -1;
        double fetchRtt = tx.gotFetch ? (tx.tFetchRecv - tx.tFetchSend).GetMilliSeconds() : -1;
        double e2eRtt = tx.gotFetch ? (tx.tFetchRecv - tx.tQuerySend).GetMilliSeconds() : -1;

        ofs << tx.seqNum << ","
            << std::fixed << std::setprecision(3)
            << querySent << ","
            << queryRecv << ","
            << queryRtt << ","
            << fetchSent << ","
            << fetchRecv << ","
            << fetchRtt << ","
            << e2eRtt << ","
            << tx.similarity << ","
            << tx.producerId << ","
            << tx.resolvedName.toUri() << "\n";
    }

    NS_LOG_INFO("Exported " << m_completedTransactions.size() << " transactions to " << filename);
}

} // namespace ndn
} // namespace ns3
