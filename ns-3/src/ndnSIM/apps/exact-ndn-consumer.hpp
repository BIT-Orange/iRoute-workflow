/**
 * @file exact-ndn-consumer.hpp
 * @brief Exact-NDN syntax baseline consumer (header-only).
 *
 * Tokenizes query text into Interest name: /exact/<type>/<tokenized_query>/<qid>
 * No semantic vectors used. Waits for Data reply containing doc info.
 */

#pragma once

#include "iroute-discovery-consumer.hpp"  // Reuse TxRecord, QueryItem

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include <ndn-cxx/lp/tags.hpp>

#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>

namespace ns3 {
namespace ndn {

/**
 * @struct ExactTxRecord
 */
struct ExactTxRecord : public TxRecord {
    std::string tokenizedQuery;
    std::string queryType;  // traffic/parking/pollution/streetlight/general
    std::string parsedDocId;
    std::string parsedCanonical;
    uint32_t parsedDomainId = 0;
    bool found = false;
};

/**
 * @class ExactNdnConsumer
 * @brief Consumer that sends tokenized query text as Interest name.
 */
class ExactNdnConsumer : public App
{
public:
    static TypeId GetTypeId();

    ExactNdnConsumer() = default;
    virtual ~ExactNdnConsumer() = default;

    void SetQueryTrace(const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries) {
        m_queryTrace = queries;
        m_queryIndex = 0;
    }

    const std::vector<ExactTxRecord>& GetTransactions() const { return m_transactions; }

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnData(shared_ptr<const Data> data) override;
    virtual void OnNack(shared_ptr<const lp::Nack> nack) override;

private:
    void ScheduleNextQuery();
    void SendQuery();
    std::string Tokenize(const std::string& text);
    std::string ClassifyType(const std::string& text);
    void HandleTimeout(uint64_t qid);

    // Configuration
    double m_frequency = 2.0;
    uint32_t m_fetchTimeoutMs = 4000;

    // Query trace
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
    size_t m_queryIndex = 0;
    uint64_t m_nextQid = 0;

    // Active transactions
    std::map<uint64_t, ExactTxRecord> m_activeTxs;
    std::vector<ExactTxRecord> m_transactions;

    // Timeout tracking
    std::map<std::string, uint64_t> m_pendingNames;  // interestName → qid
    std::map<uint64_t, EventId> m_timeoutEvents;

    EventId m_sendEvent;
};

// =============================================================================
// Implementation
// =============================================================================

NS_OBJECT_ENSURE_REGISTERED(ExactNdnConsumer);

TypeId
ExactNdnConsumer::GetTypeId() {
    static TypeId tid = TypeId("ns3::ndn::ExactNdnConsumer")
        .SetParent<App>()
        .SetGroupName("Ndn")
        .AddConstructor<ExactNdnConsumer>()
        .AddAttribute("Frequency", "Query frequency (queries/sec)",
                       DoubleValue(2.0),
                       MakeDoubleAccessor(&ExactNdnConsumer::m_frequency),
                       MakeDoubleChecker<double>())
        .AddAttribute("FetchTimeoutMs", "Timeout per query (ms)",
                       UintegerValue(4000),
                       MakeUintegerAccessor(&ExactNdnConsumer::m_fetchTimeoutMs),
                       MakeUintegerChecker<uint32_t>());
    return tid;
}

void
ExactNdnConsumer::StartApplication() {
    App::StartApplication();
    ScheduleNextQuery();
}

void
ExactNdnConsumer::StopApplication() {
    if (m_sendEvent.IsRunning()) Simulator::Cancel(m_sendEvent);
    for (auto& kv : m_timeoutEvents) {
        if (kv.second.IsRunning()) Simulator::Cancel(kv.second);
    }
    // Finalize any remaining active transactions as timeouts
    for (auto& kv : m_activeTxs) {
        auto& tx = kv.second;
        tx.failureReason = "app_stopped";
        m_transactions.push_back(tx);
    }
    m_activeTxs.clear();
    App::StopApplication();
}

void
ExactNdnConsumer::ScheduleNextQuery() {
    if (m_queryIndex >= m_queryTrace.size()) return;
    double interval = 1.0 / m_frequency;
    m_sendEvent = Simulator::Schedule(Seconds(interval),
                                       &ExactNdnConsumer::SendQuery, this);
}

std::string
ExactNdnConsumer::Tokenize(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (std::isalnum(c)) {
            result += std::tolower(c);
        } else if (c == ' ' || c == '-') {
            if (!result.empty() && result.back() != '-') {
                result += '-';
            }
        }
        // skip punctuation
    }
    // Trim trailing hyphen
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result;
}

std::string
ExactNdnConsumer::ClassifyType(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Check traffic keywords
    for (const auto& kw : {"traffic", "road", "speed", "jam", "vehicle", "congestion", "flow"}) {
        if (lower.find(kw) != std::string::npos) return "traffic";
    }
    // Parking
    for (const auto& kw : {"parking", "lot", "vacancy", "spot", "garage"}) {
        if (lower.find(kw) != std::string::npos) return "parking";
    }
    // Pollution
    for (const auto& kw : {"pollution", "air", "aqi", "pm25", "emission", "quality"}) {
        if (lower.find(kw) != std::string::npos) return "pollution";
    }
    // Streetlight
    for (const auto& kw : {"streetlight", "light", "lamp", "lantern", "illuminat"}) {
        if (lower.find(kw) != std::string::npos) return "streetlight";
    }
    return "general";
}

void
ExactNdnConsumer::SendQuery() {
    if (m_queryIndex >= m_queryTrace.size()) return;

    const auto& qi = m_queryTrace[m_queryIndex];
    uint64_t qid = qi.id;
    if (m_activeTxs.count(qid) || m_timeoutEvents.count(qid)) {
        qid = m_nextQid;
    }
    m_nextQid = std::max<uint64_t>(m_nextQid + 1, qid + 1);
    m_queryIndex++;

    // Tokenize and classify
    std::string tok = Tokenize(qi.queryText);
    std::string qtype = ClassifyType(qi.queryText);

    // Interest name: /exact/<type>/<tokenized_query>/<qid>
    Name interestName("/exact");
    interestName.append(qtype);
    interestName.append(tok);
    interestName.appendNumber(qid);

    // Create TxRecord
    ExactTxRecord tx;
    tx.queryId = qid;
    tx.tokenizedQuery = tok;
    tx.queryType = qtype;
    tx.tStage1Send = Simulator::Now();
    tx.stage1SendTimeMs = Simulator::Now().GetMilliSeconds();
    tx.expectedDomain = qi.expectedDomain;
    tx.targetDocIds = qi.targetDocIds;
    tx.targetDomains = qi.targetDomains;

    // Send Interest
    auto interest = std::make_shared<::ndn::Interest>(interestName);
    interest->setInterestLifetime(::ndn::time::milliseconds(m_fetchTimeoutMs));
    interest->setMustBeFresh(true);

    // Record wire size
    auto wire = interest->wireEncode();
    tx.stage1InterestBytes = wire.size();

    m_appLink->onReceiveInterest(*interest);

    // Track
    m_activeTxs[qid] = tx;
    m_pendingNames[interestName.toUri()] = qid;

    // Timeout
    m_timeoutEvents[qid] = Simulator::Schedule(
        MilliSeconds(m_fetchTimeoutMs),
        &ExactNdnConsumer::HandleTimeout, this, qid);

    ScheduleNextQuery();
}

void
ExactNdnConsumer::OnData(shared_ptr<const Data> data) {
    std::string nameUri = data->getName().toUri();

    // Find matching pending name (prefix match)
    uint64_t qid = UINT64_MAX;
    std::string matchedKey;
    for (const auto& kv : m_pendingNames) {
        if (nameUri.find(kv.first.substr(0, kv.first.size())) != std::string::npos ||
            kv.first.find(nameUri.substr(0, nameUri.size())) != std::string::npos) {
            // Try matching by checking if the data name starts with the interest name
            qid = kv.second;
            matchedKey = kv.first;
            break;
        }
    }

    // Alternative: extract qid from name (last component is a number)
    if (qid == UINT64_MAX) {
        const auto& name = data->getName();
        for (int i = name.size() - 1; i >= 0; --i) {
            try {
                qid = name.at(i).toNumber();
                // Check if this qid is active
                if (m_activeTxs.count(qid)) break;
                qid = UINT64_MAX;
            } catch (...) { continue; }
        }
    }

    if (qid == UINT64_MAX || m_activeTxs.find(qid) == m_activeTxs.end()) {
        return;  // Unknown data
    }

    auto& tx = m_activeTxs[qid];
    tx.tStage1Recv = Simulator::Now();
    tx.stage1RecvTimeMs = Simulator::Now().GetMilliSeconds();
    tx.stage1Success = true;
    tx.stage1RttMs = (tx.tStage1Recv - tx.tStage1Send).GetMilliSeconds();
    tx.totalMs = tx.stage1RttMs;  // No stage-2 for exact match
    
    // Read HopCount
    auto hopCountTag = data->getTag<::ndn::lp::HopCountTag>();
    if (hopCountTag) {
        tx.stage2HopCount = static_cast<int32_t>(*hopCountTag); // Use stage2HopCount for final content hops
    } else {
        tx.stage2HopCount = -1;
    }

    // Record data wire size
    tx.stage1DataBytes = data->wireEncode().size();
    tx.totalControlBytes = tx.stage1InterestBytes + tx.stage1DataBytes;

    // Parse content: "doc_id\tcanonical_name\tdomain_id"
    auto content = data->getContent();
    std::string payload(reinterpret_cast<const char*>(content.value()),
                        content.value_size());

    std::istringstream ss(payload);
    std::string docId, canonical, domIdStr;
    if (std::getline(ss, docId, '\t') &&
        std::getline(ss, canonical, '\t') &&
        std::getline(ss, domIdStr)) {
        tx.found = true;
        tx.parsedDocId = docId;
        tx.parsedCanonical = canonical;
        try { tx.parsedDomainId = std::stoul(domIdStr); } catch (...) {}
        tx.selectedDomain = "/domain" + domIdStr;
        tx.finalSuccessDomain = tx.selectedDomain;
        tx.requestedName = canonical;
        tx.stage2Success = true;  // Exact match = single stage
    }

    // Cancel timeout
    if (m_timeoutEvents.count(qid)) {
        Simulator::Cancel(m_timeoutEvents[qid]);
        m_timeoutEvents.erase(qid);
    }

    // Finalize
    m_transactions.push_back(tx);
    m_activeTxs.erase(qid);
    if (!matchedKey.empty()) m_pendingNames.erase(matchedKey);
}

void
ExactNdnConsumer::OnNack(shared_ptr<const lp::Nack> nack) {
    // Treat as miss
    auto nameUri = nack->getInterest().getName().toUri();
    auto it = m_pendingNames.find(nameUri);
    if (it != m_pendingNames.end()) {
        uint64_t qid = it->second;
        if (m_activeTxs.count(qid)) {
            auto& tx = m_activeTxs[qid];
            tx.failureReason = "NACK";
            tx.tStage1Recv = Simulator::Now();
            tx.totalMs = (tx.tStage1Recv - tx.tStage1Send).GetMilliSeconds();
            m_transactions.push_back(tx);
            m_activeTxs.erase(qid);
        }
        m_pendingNames.erase(it);
    }
}

void
ExactNdnConsumer::HandleTimeout(uint64_t qid) {
    if (m_activeTxs.find(qid) == m_activeTxs.end()) return;

    auto& tx = m_activeTxs[qid];
    tx.failureReason = "TIMEOUT";
    tx.totalMs = m_fetchTimeoutMs;
    m_transactions.push_back(tx);
    m_activeTxs.erase(qid);
    m_timeoutEvents.erase(qid);
}

} // namespace ndn
} // namespace ns3
