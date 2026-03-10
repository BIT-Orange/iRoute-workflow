/**
 * @file random-k-discovery-consumer.hpp
 * @brief Random-k Discovery Consumer - "No Semantics" Lower Bound Baseline
 *
 * This baseline randomly probes k domains without using any semantic information.
 * It represents the worst-case "blind search" approach:
 * - No centroids, no vector similarity
 * - Just pick k random domains and hope one has the content
 *
 * Purpose in paper:
 * - Lower bound for accuracy comparison (what if discovery is random?)
 * - Demonstrates value of semantic routing over blind probing
 * - Fair comparison: same kMax budget, but no semantic guidance
 *
 * Protocol:
 * 1. Consumer picks k random domains from known domain list
 * 2. Sends discovery Interest to each (in parallel or sequential)
 * 3. First domain that responds with content wins
 * 4. If all fail, query fails
 */

#ifndef RANDOM_K_DISCOVERY_CONSUMER_HPP
#define RANDOM_K_DISCOVERY_CONSUMER_HPP

#include "ns3/ndnSIM-module.h"
#include "ns3/random-variable-stream.h"
#include "extensions/iroute-vector.hpp"

#include <vector>
#include <string>
#include <random>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iostream>

namespace ns3 {
namespace ndn {

/**
 * @brief Random-k Discovery Consumer Application
 *
 * Baseline that randomly picks k domains to probe without semantic information.
 */
class RandomKDiscoveryConsumer : public App {
public:
    static TypeId GetTypeId();
    
    RandomKDiscoveryConsumer();
    virtual ~RandomKDiscoveryConsumer() = default;
    
    // Query item structure (compatible with IRouteDiscoveryConsumer)
    struct QueryItem {
        iroute::SemanticVector vector;
        std::string expectedDomain;
        std::vector<std::string> targetDocIds;
        std::vector<std::string> targetDomains;
        std::string targetName;
    };
    
    // Transaction record for analysis
    struct Transaction {
        uint32_t queryId = 0;
        std::string expectedDomain;
        std::string discoveredDomain;
        std::string fetchedDocId;
        bool stage2Success = false;
        bool domainCorrect = false;
        bool docCorrect = false;
        double totalMs = 0;
        double stage1RttMs = 0;
        uint32_t discoveryAttempts = 0;
        std::string attemptedDomains;
    };
    
    void SetQueryTrace(const std::vector<QueryItem>& trace);
    void SetDomainList(const std::vector<std::string>& domains);
    const std::vector<Transaction>& GetTransactions() const { return m_transactions; }
    void ExportToCsv(const std::string& filename) const;

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    
private:
    void ScheduleNextQuery();
    void SendQuery();
    void SendDiscoveryProbe(const std::string& domain);
    void OnData(shared_ptr<const Data> data);
    void OnNack(shared_ptr<const lp::Nack> nack);
    void OnTimeout(uint32_t queryId, const std::string& domain);
    void ProcessStage2Response(shared_ptr<const Data> data);
    void FinalizeCurrentQuery(bool success, const std::string& domain, const std::string& docId);
    
    // Parameters
    double m_frequency = 10.0;  // queries per second
    uint32_t m_kMax = 5;        // number of random domains to probe
    uint32_t m_fetchTimeoutMs = 4000;
    uint32_t m_vectorDim = 128;
    
    // State
    std::vector<QueryItem> m_queryTrace;
    std::vector<std::string> m_domainList;
    std::vector<Transaction> m_transactions;
    
    uint32_t m_currentQueryIdx = 0;
    uint32_t m_currentQueryId = 0;
    bool m_queryInProgress = false;
    
    // Current query state
    std::vector<std::string> m_probedDomains;
    uint32_t m_probesSent = 0;
    uint32_t m_probesCompleted = 0;
    std::string m_bestDomain;
    std::string m_bestDocId;
    Time m_queryStartTime;
    Time m_stage1EndTime;
    
    std::mt19937 m_rng;
    EventId m_sendEvent;
};

// =============================================================================
// Implementation (header-only, no NS_LOG to avoid multiple definition)
// =============================================================================

// Debug flag for verbose output
#ifndef RANDOM_K_DEBUG
#define RANDOM_K_DEBUG 0
#endif

NS_OBJECT_ENSURE_REGISTERED(RandomKDiscoveryConsumer);

TypeId
RandomKDiscoveryConsumer::GetTypeId() {
    static TypeId tid = TypeId("ns3::ndn::RandomKDiscoveryConsumer")
        .SetParent<App>()
        .SetGroupName("Ndn")
        .AddConstructor<RandomKDiscoveryConsumer>()
        .AddAttribute("Frequency", "Query frequency (queries per second)",
                      DoubleValue(10.0),
                      MakeDoubleAccessor(&RandomKDiscoveryConsumer::m_frequency),
                      MakeDoubleChecker<double>())
        .AddAttribute("KMax", "Number of random domains to probe",
                      UintegerValue(5),
                      MakeUintegerAccessor(&RandomKDiscoveryConsumer::m_kMax),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("FetchTimeoutMs", "Timeout for each probe (ms)",
                      UintegerValue(4000),
                      MakeUintegerAccessor(&RandomKDiscoveryConsumer::m_fetchTimeoutMs),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("VectorDim", "Vector dimension",
                      UintegerValue(128),
                      MakeUintegerAccessor(&RandomKDiscoveryConsumer::m_vectorDim),
                      MakeUintegerChecker<uint32_t>());
    return tid;
}

RandomKDiscoveryConsumer::RandomKDiscoveryConsumer()
    : m_rng(std::random_device{}())
{
}

void
RandomKDiscoveryConsumer::SetQueryTrace(const std::vector<QueryItem>& trace) {
    m_queryTrace = trace;
    if (RANDOM_K_DEBUG) std::cerr << "[RandomK] loaded " << trace.size() << " queries" << std::endl;
}

void
RandomKDiscoveryConsumer::SetDomainList(const std::vector<std::string>& domains) {
    m_domainList = domains;
    if (RANDOM_K_DEBUG) std::cerr << "[RandomK] set " << domains.size() << " domains" << std::endl;
}

void
RandomKDiscoveryConsumer::StartApplication() {
    App::StartApplication();
    
    if (m_domainList.empty()) {
        if (RANDOM_K_DEBUG) std::cerr << "[RandomK] WARN: no domains set, cannot start" << std::endl;
        return;
    }
    
    if (m_queryTrace.empty()) {
        if (RANDOM_K_DEBUG) std::cerr << "[RandomK] WARN: no query trace set" << std::endl;
        return;
    }
    
    ScheduleNextQuery();
}

void
RandomKDiscoveryConsumer::StopApplication() {
    if (m_sendEvent.IsRunning()) {
        Simulator::Cancel(m_sendEvent);
    }
    App::StopApplication();
}

void
RandomKDiscoveryConsumer::ScheduleNextQuery() {
    if (m_currentQueryIdx >= m_queryTrace.size()) {
        if (RANDOM_K_DEBUG) std::cerr << "[RandomK] all queries completed" << std::endl;
        return;
    }
    
    double delay = 1.0 / m_frequency;
    m_sendEvent = Simulator::Schedule(Seconds(delay), &RandomKDiscoveryConsumer::SendQuery, this);
}

void
RandomKDiscoveryConsumer::SendQuery() {
    if (m_queryInProgress) return;
    if (m_currentQueryIdx >= m_queryTrace.size()) return;
    
    m_queryInProgress = true;
    m_currentQueryId = m_currentQueryIdx;
    m_queryStartTime = Simulator::Now();
    
    // Reset state
    m_probedDomains.clear();
    m_probesSent = 0;
    m_probesCompleted = 0;
    m_bestDomain.clear();
    m_bestDocId.clear();
    
    // Randomly select kMax domains
    std::vector<std::string> shuffled = m_domainList;
    std::shuffle(shuffled.begin(), shuffled.end(), m_rng);
    
    uint32_t numProbes = std::min(m_kMax, static_cast<uint32_t>(shuffled.size()));
    for (uint32_t i = 0; i < numProbes; ++i) {
        m_probedDomains.push_back(shuffled[i]);
    }
    
    if (RANDOM_K_DEBUG) std::cerr << "[RandomK] Query " << m_currentQueryId << ": probing " << numProbes << " random domains" << std::endl;
    
    // Send probes in parallel
    for (const auto& domain : m_probedDomains) {
        SendDiscoveryProbe(domain);
    }
}

void
RandomKDiscoveryConsumer::SendDiscoveryProbe(const std::string& domain) {
    // Construct fetch Interest directly (skip Stage-1 discovery, go straight to fetch)
    // In random-k, we don't have semantic discovery - we just try to fetch from random domains
    
    const auto& query = m_queryTrace[m_currentQueryIdx];
    
    // Build Interest: /domain{i}/data/fetch?vector=...
    Name interestName(domain);
    interestName.append("data").append("fetch");
    
    // Encode query vector as parameter
    auto& vec = query.vector;
    const auto& data = vec.getData();
    
    // Simple encoding: just use a hash or sequence number for uniqueness
    interestName.appendNumber(m_currentQueryId);
    interestName.appendNumber(Simulator::Now().GetNanoSeconds());
    
    auto interest = std::make_shared<Interest>(interestName);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(time::milliseconds(m_fetchTimeoutMs));
    
    // Attach vector as application parameter
    std::vector<uint8_t> paramBuf(data.size() * sizeof(float));
    std::memcpy(paramBuf.data(), data.data(), paramBuf.size());
    interest->setApplicationParameters(paramBuf.data(), paramBuf.size());
    
    m_probesSent++;
    
    if (RANDOM_K_DEBUG) std::cerr << "[RandomK]   Probe to " << domain << ": " << interestName << std::endl;
    
    m_appLink->onReceiveInterest(*interest);
    
    // Schedule timeout
    Simulator::Schedule(MilliSeconds(m_fetchTimeoutMs),
                        &RandomKDiscoveryConsumer::OnTimeout, this,
                        m_currentQueryId, domain);
}

void
RandomKDiscoveryConsumer::OnData(shared_ptr<const Data> data) {
    const Name& name = data->getName();
    
    // Extract domain from name
    std::string domain;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i].toUri().find("domain") != std::string::npos) {
            domain = "/" + name[i].toUri();
            break;
        }
    }
    
    // Check if this probe already won
    if (!m_bestDomain.empty()) {
        m_probesCompleted++;
        return;
    }
    
    m_stage1EndTime = Simulator::Now();
    
    // Extract doc ID from response
    std::string docId;
    const auto& content = data->getContent();
    if (content.value_size() > 0) {
        docId = std::string(reinterpret_cast<const char*>(content.value()), content.value_size());
    }
    
    if (!docId.empty() && docId != "NO_MATCH") {
        // Success!
        m_bestDomain = domain;
        m_bestDocId = docId;
        FinalizeCurrentQuery(true, domain, docId);
    } else {
        m_probesCompleted++;
        if (m_probesCompleted >= m_probesSent) {
            // All probes failed
            FinalizeCurrentQuery(false, "", "");
        }
    }
}

void
RandomKDiscoveryConsumer::OnNack(shared_ptr<const lp::Nack> nack) {
    m_probesCompleted++;
    if (m_probesCompleted >= m_probesSent && m_bestDomain.empty()) {
        FinalizeCurrentQuery(false, "", "");
    }
}

void
RandomKDiscoveryConsumer::OnTimeout(uint32_t queryId, const std::string& domain) {
    if (queryId != m_currentQueryId) return;
    
    m_probesCompleted++;
    if (RANDOM_K_DEBUG) std::cerr << "[RandomK] Timeout for domain " << domain << std::endl;
    
    if (m_probesCompleted >= m_probesSent && m_bestDomain.empty()) {
        FinalizeCurrentQuery(false, "", "");
    }
}

void
RandomKDiscoveryConsumer::FinalizeCurrentQuery(bool success, const std::string& domain, 
                                                const std::string& docId) {
    if (!m_queryInProgress) return;
    
    const auto& query = m_queryTrace[m_currentQueryIdx];
    
    Transaction tx;
    tx.queryId = m_currentQueryId;
    tx.expectedDomain = query.expectedDomain;
    tx.discoveredDomain = domain;
    tx.fetchedDocId = docId;
    tx.stage2Success = success;
    tx.discoveryAttempts = m_probesSent;
    
    Time now = Simulator::Now();
    tx.totalMs = (now - m_queryStartTime).GetMilliSeconds();
    tx.stage1RttMs = m_stage1EndTime.IsZero() ? tx.totalMs : 
                     (m_stage1EndTime - m_queryStartTime).GetMilliSeconds();
    
    // Build attempted domains string
    for (size_t i = 0; i < m_probedDomains.size(); ++i) {
        if (i > 0) tx.attemptedDomains += ";";
        tx.attemptedDomains += m_probedDomains[i];
    }
    
    // Check correctness
    tx.domainCorrect = false;
    if (!domain.empty()) {
        // Check if discovered domain is in target domains
        for (const auto& td : query.targetDomains) {
            if (domain == td || domain.find(td) != std::string::npos || 
                td.find(domain) != std::string::npos) {
                tx.domainCorrect = true;
                break;
            }
        }
        // Fallback: check expectedDomain
        if (!tx.domainCorrect && !query.expectedDomain.empty()) {
            tx.domainCorrect = (domain == query.expectedDomain || 
                               domain.find(query.expectedDomain) != std::string::npos);
        }
    }
    
    // Check doc correctness
    tx.docCorrect = false;
    if (!docId.empty() && !query.targetDocIds.empty()) {
        for (const auto& targetDoc : query.targetDocIds) {
            if (docId.find(targetDoc) != std::string::npos || 
                targetDoc.find(docId) != std::string::npos) {
                tx.docCorrect = true;
                break;
            }
        }
    }
    
    m_transactions.push_back(tx);
    
    if (RANDOM_K_DEBUG) {
        std::cerr << "[RandomK] Query " << m_currentQueryId << " complete: success=" << success
                  << ", domain=" << domain << ", docId=" << docId
                  << ", domainCorrect=" << tx.domainCorrect 
                  << ", docCorrect=" << tx.docCorrect << std::endl;
    }
    
    m_queryInProgress = false;
    m_currentQueryIdx++;
    ScheduleNextQuery();
}

void
RandomKDiscoveryConsumer::ExportToCsv(const std::string& filename) const {
    std::ofstream f(filename);
    f << "queryId,expectedDomain,discoveredDomain,fetchedDocId,stage2Success,"
      << "domainCorrect,docCorrect,totalMs,stage1RttMs,discoveryAttempts,attemptedDomains\n";
    
    for (const auto& tx : m_transactions) {
        f << tx.queryId << "," << tx.expectedDomain << "," << tx.discoveredDomain << ","
          << tx.fetchedDocId << "," << tx.stage2Success << ","
          << tx.domainCorrect << "," << tx.docCorrect << ","
          << tx.totalMs << "," << tx.stage1RttMs << ","
          << tx.discoveryAttempts << "," << tx.attemptedDomains << "\n";
    }
    f.close();
    
    if (RANDOM_K_DEBUG) std::cerr << "[RandomK] Exported " << m_transactions.size() << " transactions to " << filename << std::endl;
}

}  // namespace ndn
}  // namespace ns3

#endif // RANDOM_K_DISCOVERY_CONSUMER_HPP
