/**
 * @file central-dir-consumer.hpp
 * @brief Central Directory consumer for two-stage query workflow (header-only).
 *
 * Stage-1: Send discovery Interest to /dir/query/<qid> with ApplicationParameters
 *          containing query_text. Receives top-K doc names + domain info.
 * Stage-2: Send fetch Interest to /<domain>/data/doc/<docId> for the top-1 result.
 */

#pragma once

#include "iroute-discovery-consumer.hpp"  // Reuse TxRecord, QueryItem

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"

#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>

namespace ns3 {
namespace ndn {

/**
 * @struct CentralDirTxRecord
 * @brief Transaction record for central directory queries.
 */
struct CentralDirTxRecord : public TxRecord {
    std::string queryText;
    uint32_t topKReturned = 0;     ///< Number of results from directory
    std::string top1DocId;
    std::string top1Canonical;
    uint32_t top1DomainId = 0;
    bool directoryHit = false;     ///< Whether directory returned results
    bool fetchSuccess = false;
    Name fetchName;                ///< Name of the fetch Interest
    std::string failureReason_str;
};

/**
 * @class CentralDirConsumer
 * @brief Two-stage consumer: discovery via central directory, then fetch.
 */
class CentralDirConsumer : public App
{
public:
    static TypeId GetTypeId();

    CentralDirConsumer() = default;
    virtual ~CentralDirConsumer() = default;

    void SetQueryTrace(const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries) {
        m_queryTrace = queries;
        m_queryIndex = 0;
    }

    const std::vector<CentralDirTxRecord>& GetTransactions() const { return m_transactions; }

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnData(shared_ptr<const Data> data) override;
    virtual void OnNack(shared_ptr<const lp::Nack> nack) override;

private:
    void ScheduleNextQuery();
    void SendDiscoveryQuery();
    void HandleDiscoveryTimeout(uint64_t qid);
    void SendFetchInterest(uint64_t qid);
    void HandleFetchTimeout(uint64_t qid);

    std::string FormatQid(uint64_t qid) {
        char buf[16];
        snprintf(buf, sizeof(buf), "q%04lu", (unsigned long)qid);
        return std::string(buf);
    }

    // Configuration
    double m_frequency = 2.0;
    uint32_t m_fetchTimeoutMs = 4000;
    uint32_t m_discTimeoutMs = 4000;

    // Query trace
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
    size_t m_queryIndex = 0;
    uint64_t m_nextQid = 0;

    // Active transactions
    std::map<uint64_t, CentralDirTxRecord> m_activeTxs;
    std::vector<CentralDirTxRecord> m_transactions;

    // Pending discovery: qid → Interest name (with digest)
    std::map<uint64_t, Name> m_pendingDiscName;
    std::map<uint64_t, EventId> m_discTimeouts;

    // Pending fetch: qid → fetch Name (stage-2)
    std::map<uint64_t, Name> m_pendingFetchName;
    std::map<uint64_t, EventId> m_fetchTimeouts;

    EventId m_sendEvent;
};

// =============================================================================
// Implementation
// =============================================================================

NS_OBJECT_ENSURE_REGISTERED(CentralDirConsumer);

TypeId
CentralDirConsumer::GetTypeId() {
    static TypeId tid = TypeId("ns3::ndn::CentralDirConsumer")
        .SetParent<App>()
        .SetGroupName("Ndn")
        .AddConstructor<CentralDirConsumer>()
        .AddAttribute("Frequency", "Query frequency (queries/sec)",
                       DoubleValue(2.0),
                       MakeDoubleAccessor(&CentralDirConsumer::m_frequency),
                       MakeDoubleChecker<double>())
        .AddAttribute("FetchTimeoutMs", "Fetch timeout (ms)",
                       UintegerValue(4000),
                       MakeUintegerAccessor(&CentralDirConsumer::m_fetchTimeoutMs),
                       MakeUintegerChecker<uint32_t>())
        .AddAttribute("DiscTimeoutMs", "Discovery timeout (ms)",
                       UintegerValue(4000),
                       MakeUintegerAccessor(&CentralDirConsumer::m_discTimeoutMs),
                       MakeUintegerChecker<uint32_t>());
    return tid;
}

void
CentralDirConsumer::StartApplication() {
    App::StartApplication();
    ScheduleNextQuery();
}

void
CentralDirConsumer::StopApplication() {
    if (m_sendEvent.IsRunning()) Simulator::Cancel(m_sendEvent);
    for (auto& kv : m_discTimeouts) {
        if (kv.second.IsRunning()) Simulator::Cancel(kv.second);
    }
    for (auto& kv : m_fetchTimeouts) {
        if (kv.second.IsRunning()) Simulator::Cancel(kv.second);
    }
    for (auto& kv : m_activeTxs) {
        auto& tx = kv.second;
        tx.failureReason_str = "app_stopped";
        m_transactions.push_back(tx);
    }
    m_activeTxs.clear();
    App::StopApplication();
}

void
CentralDirConsumer::ScheduleNextQuery() {
    if (m_queryIndex >= m_queryTrace.size()) return;
    double interval = 1.0 / m_frequency;
    m_sendEvent = Simulator::Schedule(Seconds(interval),
                                       &CentralDirConsumer::SendDiscoveryQuery, this);
}

void
CentralDirConsumer::SendDiscoveryQuery() {
    if (m_queryIndex >= m_queryTrace.size()) return;

    const auto& qi = m_queryTrace[m_queryIndex];
    uint64_t qid = qi.id;
    if (m_activeTxs.count(qid) || m_discTimeouts.count(qid) || m_fetchTimeouts.count(qid)) {
        qid = m_nextQid;
    }
    m_nextQid = std::max<uint64_t>(m_nextQid + 1, qid + 1);
    m_queryIndex++;

    CentralDirTxRecord tx;
    tx.queryId = qid;
    tx.queryText = qi.queryText;
    tx.tStage1Send = Simulator::Now();
    tx.stage1SendTimeMs = Simulator::Now().GetMilliSeconds();
    tx.expectedDomain = qi.expectedDomain;
    tx.targetDocIds = qi.targetDocIds;
    tx.targetDomains = qi.targetDomains;

    // Interest: /dir/query/<qid>
    std::string qidStr = FormatQid(qid);
    Name interestName("/dir/query");
    interestName.append(qidStr);

    auto interest = std::make_shared<::ndn::Interest>(interestName);
    interest->setInterestLifetime(::ndn::time::milliseconds(m_discTimeoutMs));
    interest->setMustBeFresh(true);

    // Put query_text in ApplicationParameters
    if (!qi.queryText.empty()) {
        ::ndn::Block paramBlock(::ndn::tlv::ApplicationParameters);
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(qi.queryText.data());
        paramBlock.push_back(::ndn::encoding::makeBinaryBlock(
            128, ptr, ptr + qi.queryText.size()));
        paramBlock.encode();
        interest->setApplicationParameters(paramBlock);
    }

    auto wire = interest->wireEncode();
    tx.stage1InterestBytes = wire.size();

    // Track by qid (reliable matching)
    m_activeTxs[qid] = tx;
    m_pendingDiscName[qid] = interest->getName();  // full name with digest

    m_appLink->onReceiveInterest(*interest);

    m_discTimeouts[qid] = Simulator::Schedule(
        MilliSeconds(m_discTimeoutMs),
        &CentralDirConsumer::HandleDiscoveryTimeout, this, qid);

    ScheduleNextQuery();
}

void
CentralDirConsumer::OnData(shared_ptr<const Data> data) {
    const Name& dataName = data->getName();

    // ---- Discovery reply matching (/dir/query/<qid>[/digest]) ----
    // Look through pending disc names; Data name should match or be prefix-related
    for (auto it = m_pendingDiscName.begin(); it != m_pendingDiscName.end(); ++it) {
        uint64_t qid = it->first;
        // The Data name should equal the Interest name (including digest)
        // or the Interest name should be a prefix of Data name
        if (dataName == it->second ||
            it->second.isPrefixOf(dataName) ||
            dataName.isPrefixOf(it->second)) {

            if (m_activeTxs.find(qid) == m_activeTxs.end()) { 
                m_pendingDiscName.erase(it);
                return;
            }

            auto& tx = m_activeTxs[qid];
            tx.tStage1Recv = Simulator::Now();
            tx.stage1RecvTimeMs = Simulator::Now().GetMilliSeconds();
            tx.stage1Success = true;
            tx.stage1RttMs = (tx.tStage1Recv - tx.tStage1Send).GetMilliSeconds();
            tx.stage1DataBytes = data->wireEncode().size();

            if (m_discTimeouts.count(qid)) {
                Simulator::Cancel(m_discTimeouts[qid]);
                m_discTimeouts.erase(qid);
            }
            m_pendingDiscName.erase(it);

            // Parse response: "doc_id\tcanonical_name\tdomain_id\tscore\n"
            auto content = data->getContent();
            std::string payload(reinterpret_cast<const char*>(content.value()),
                                content.value_size());

            std::istringstream ss(payload);
            std::string line;
            uint32_t resultCount = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                resultCount++;
                if (resultCount == 1) {
                    std::istringstream ls(line);
                    std::string docId, canonical, domIdStr, scoreStr;
                    if (std::getline(ls, docId, '\t') &&
                        std::getline(ls, canonical, '\t') &&
                        std::getline(ls, domIdStr, '\t')) {
                        tx.top1DocId = docId;
                        tx.top1Canonical = canonical;
                        try { tx.top1DomainId = std::stoul(domIdStr); } catch (...) {}
                        if (std::getline(ls, scoreStr)) {
                            try { tx.confidence = std::stod(scoreStr); } catch (...) {}
                        }
                        tx.directoryHit = true;
                    }
                }
            }
            tx.topKReturned = resultCount;

            if (tx.directoryHit) {
                tx.selectedDomain = "/domain" + std::to_string(tx.top1DomainId);
                SendFetchInterest(qid);
            } else {
                tx.failureReason_str = "DIR_EMPTY";
                tx.totalMs = tx.stage1RttMs;
                tx.totalControlBytes = tx.stage1InterestBytes + tx.stage1DataBytes;
                m_transactions.push_back(tx);
                m_activeTxs.erase(qid);
            }
            return;
        }
    }

    // ---- Fetch reply matching (/domain<N>/data/doc/<docId>) ----
    for (auto it = m_pendingFetchName.begin(); it != m_pendingFetchName.end(); ++it) {
        uint64_t qid = it->first;
        // Data name should match interest name or be prefix-related
        if (dataName == it->second ||
            it->second.isPrefixOf(dataName) ||
            dataName.isPrefixOf(it->second)) {

            if (m_activeTxs.find(qid) == m_activeTxs.end()) {
                m_pendingFetchName.erase(it);
                return;
            }

            auto& tx = m_activeTxs[qid];
            tx.tStage2Recv = Simulator::Now();
            tx.stage2RecvTimeMs = Simulator::Now().GetMilliSeconds();
            tx.stage2Success = true;
            tx.fetchSuccess = true;
            tx.stage2DataBytes = data->wireEncode().size();
            tx.stage2RttMs = (tx.tStage2Recv - tx.tStage2Send).GetMilliSeconds();
            tx.totalMs = (tx.tStage2Recv - tx.tStage1Send).GetMilliSeconds();
            tx.totalControlBytes = tx.stage1InterestBytes + tx.stage1DataBytes;
            tx.totalDataBytes = tx.stage2InterestBytes + tx.stage2DataBytes;
            tx.requestedName = tx.top1Canonical;
            tx.finalSuccessDomain = tx.selectedDomain;

            if (m_fetchTimeouts.count(qid)) {
                Simulator::Cancel(m_fetchTimeouts[qid]);
                m_fetchTimeouts.erase(qid);
            }
            m_pendingFetchName.erase(it);

            m_transactions.push_back(tx);
            m_activeTxs.erase(qid);
            return;
        }
    }
}

void
CentralDirConsumer::OnNack(shared_ptr<const lp::Nack> nack) {
    const Name& nackName = nack->getInterest().getName();

    // Discovery NACK
    for (auto it = m_pendingDiscName.begin(); it != m_pendingDiscName.end(); ++it) {
        if (nackName == it->second || it->second.isPrefixOf(nackName)) {
            uint64_t qid = it->first;
            if (m_activeTxs.count(qid)) {
                auto& tx = m_activeTxs[qid];
                tx.failureReason_str = "DISC_NACK";
                tx.tStage1Recv = Simulator::Now();
                tx.totalMs = (tx.tStage1Recv - tx.tStage1Send).GetMilliSeconds();
                m_transactions.push_back(tx);
                m_activeTxs.erase(qid);
            }
            m_pendingDiscName.erase(it);
            return;
        }
    }

    // Fetch NACK
    for (auto it = m_pendingFetchName.begin(); it != m_pendingFetchName.end(); ++it) {
        if (nackName == it->second || it->second.isPrefixOf(nackName)) {
            uint64_t qid = it->first;
            if (m_activeTxs.count(qid)) {
                auto& tx = m_activeTxs[qid];
                tx.failureReason_str = "FETCH_NACK";
                tx.totalMs = (Simulator::Now() - tx.tStage1Send).GetMilliSeconds();
                m_transactions.push_back(tx);
                m_activeTxs.erase(qid);
            }
            m_pendingFetchName.erase(it);
            return;
        }
    }
}

void
CentralDirConsumer::SendFetchInterest(uint64_t qid) {
    auto it = m_activeTxs.find(qid);
    if (it == m_activeTxs.end()) return;

    auto& tx = it->second;
    tx.tStage2Send = Simulator::Now();
    tx.stage2SendTimeMs = Simulator::Now().GetMilliSeconds();

    // Construct fetch name from domain + docId 
    // Producer prefix is /domain<N>/data, so we fetch /domain<N>/data/<docId>
    Name fetchName("/domain" + std::to_string(tx.top1DomainId));
    fetchName.append("data").append(tx.top1DocId);
    tx.fetchName = fetchName;

    auto interest = std::make_shared<::ndn::Interest>(fetchName);
    interest->setInterestLifetime(::ndn::time::milliseconds(m_fetchTimeoutMs));
    // Do NOT set MustBeFresh — SemanticProducer doesn't set FreshnessPeriod

    auto wire = interest->wireEncode();
    tx.stage2InterestBytes = wire.size();

    m_pendingFetchName[qid] = fetchName;

    m_appLink->onReceiveInterest(*interest);

    m_fetchTimeouts[qid] = Simulator::Schedule(
        MilliSeconds(m_fetchTimeoutMs),
        &CentralDirConsumer::HandleFetchTimeout, this, qid);
}

void
CentralDirConsumer::HandleDiscoveryTimeout(uint64_t qid) {
    if (m_activeTxs.find(qid) == m_activeTxs.end()) return;

    auto& tx = m_activeTxs[qid];
    tx.failureReason_str = "DISC_TIMEOUT";
    tx.totalMs = m_discTimeoutMs;
    m_transactions.push_back(tx);
    m_activeTxs.erase(qid);
    m_discTimeouts.erase(qid);
    m_pendingDiscName.erase(qid);
}

void
CentralDirConsumer::HandleFetchTimeout(uint64_t qid) {
    if (m_activeTxs.find(qid) == m_activeTxs.end()) return;

    auto& tx = m_activeTxs[qid];
    tx.failureReason_str = "FETCH_TIMEOUT";
    tx.totalMs = (Simulator::Now() - tx.tStage1Send).GetMilliSeconds();
    m_transactions.push_back(tx);
    m_activeTxs.erase(qid);
    m_fetchTimeouts.erase(qid);
    m_pendingFetchName.erase(qid);
}

} // namespace ndn
} // namespace ns3
