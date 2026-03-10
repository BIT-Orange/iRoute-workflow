/**
 * @file directory-server-app.hpp
 * @brief Central Directory Service for baseline comparison (header-only).
 *
 * Receives discovery Interest at /dir/query/<qid> with ApplicationParameters
 * containing query_text. Looks up an offline oracle index (qrels-based) and
 * returns top-K doc names with scores.
 *
 * Features:
 * - Configurable processing delay (--dirProcMs)
 * - Single-point-of-failure injection (stop at t_fail)
 * - Statistics: queries processed, bytes served, queue length proxy
 * - Pluggable modes: oracle (qrels), bm25, embed (stub)
 */

#pragma once

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace ns3 {
namespace ndn {

/**
 * @struct DirectoryResult
 * @brief A single result from directory lookup.
 */
struct DirectoryResult {
    std::string docId;
    std::string canonicalName;
    uint32_t domainId = 0;
    double score = 1.0;
};

/**
 * @class DirectoryServerApp
 * @brief Central directory server with pluggable retrieval modes.
 */
class DirectoryServerApp : public App
{
public:
    static TypeId GetTypeId();

    DirectoryServerApp() = default;
    virtual ~DirectoryServerApp() = default;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Load oracle index from qrels + content.
     * Builds: queryId → list of (docId, canonicalName, domainId) from qrels+content.
     */
    void LoadOracleIndex(const std::string& qrelsFile,
                         const std::string& contentFile) {
        // Load content: doc_id → (canonical_name, domain_id)
        std::map<std::string, std::pair<std::string, uint32_t>> contentMap;
        {
            std::ifstream f(contentFile);
            if (!f.is_open()) {
                std::cerr << "[DirectoryServer] Cannot open content: " << contentFile << std::endl;
                return;
            }
            std::string line;
            std::getline(f, line); // header
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                std::istringstream ss(line);
                std::string domId, docId, canonical;
                if (std::getline(ss, domId, ',') &&
                    std::getline(ss, docId, ',') &&
                    std::getline(ss, canonical, ',')) {
                    try {
                        contentMap[docId] = {canonical, std::stoul(domId)};
                    } catch (...) {}
                }
            }
        }

        // Load qrels: qid → list of (docId, relevance)
        {
            std::ifstream f(qrelsFile);
            if (!f.is_open()) {
                std::cerr << "[DirectoryServer] Cannot open qrels: " << qrelsFile << std::endl;
                return;
            }
            std::string line;
            std::getline(f, line); // header
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                std::istringstream ss(line);
                std::string qid, docId, relStr;
                if (std::getline(ss, qid, '\t') &&
                    std::getline(ss, docId, '\t') &&
                    std::getline(ss, relStr)) {
                    auto it = contentMap.find(docId);
                    if (it != contentMap.end()) {
                        DirectoryResult r;
                        r.docId = docId;
                        r.canonicalName = it->second.first;
                        r.domainId = it->second.second;
                        try { r.score = std::stod(relStr); } catch (...) { r.score = 1.0; }
                        m_oracleIndex[qid].push_back(r);
                    }
                }
            }
        }

        // Also build a query_text → qid mapping from consumer trace
        std::cout << "[DirectoryServer] Loaded oracle index: "
                  << m_oracleIndex.size() << " queries" << std::endl;
    }

    /**
     * @brief Load query text → qid mapping from consumer trace.
     * This allows the server to resolve query_text in ApplicationParameters to a qid.
     */
    void LoadQueryTextMap(const std::string& traceFile) {
        std::ifstream f(traceFile);
        if (!f.is_open()) return;
        std::string line;
        std::getline(f, line); // header
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            // Cheap parse: first two CSV fields = query_id, query_text
            auto pos1 = line.find(',');
            if (pos1 == std::string::npos) continue;
            auto pos2 = line.find(',', pos1 + 1);
            if (pos2 == std::string::npos) pos2 = line.size();
            std::string qid = line.substr(0, pos1);
            std::string text = line.substr(pos1 + 1, pos2 - pos1 - 1);
            m_textToQid[text] = qid;
        }
        std::cout << "[DirectoryServer] Loaded " << m_textToQid.size()
                  << " text→qid mappings" << std::endl;
    }

    /**
     * @brief Set failure time. After this time, the server stops responding.
     */
    void SetFailureTime(double failTimeSec) {
        if (failTimeSec > 0) {
            m_failureTime = Seconds(failTimeSec);
            m_failureEnabled = true;
        }
    }

    // ========================================================================
    // Statistics
    // ========================================================================

    uint64_t GetQueriesProcessed() const { return m_queriesProcessed; }
    uint64_t GetTotalBytesServed() const { return m_totalBytesServed; }
    uint64_t GetQueriesDropped() const { return m_queriesDropped; }
    uint32_t GetPeakQueueLength() const { return m_peakQueueLength; }

protected:
    virtual void StartApplication() override {
        App::StartApplication();
        FibHelper::AddRoute(GetNode(), "/dir", m_face, 0);

        if (m_failureEnabled) {
            Simulator::Schedule(m_failureTime, &DirectoryServerApp::InjectFailure, this);
        }
    }

    virtual void StopApplication() override {
        App::StopApplication();
    }

    virtual void OnInterest(shared_ptr<const Interest> interest) override {
        App::OnInterest(interest);

        // Check if failed
        if (m_isFailed) {
            m_queriesDropped++;
            return;  // Silently drop — Interest will timeout at consumer
        }

        // Track queue length
        m_currentQueueLength++;
        if (m_currentQueueLength > m_peakQueueLength) {
            m_peakQueueLength = m_currentQueueLength;
        }

        // Schedule delayed response. Optional micro-jitter avoids fully
        // degenerate constant-latency CDFs in deterministic toy topologies.
        int64_t totalUs = static_cast<int64_t>(m_procDelayMs) * 1000;
        if (m_procJitterUs > 0) {
            if (!m_procJitterRv) {
                m_procJitterRv = CreateObject<UniformRandomVariable>();
            }
            double jitterUs = m_procJitterRv->GetValue(
                -static_cast<double>(m_procJitterUs),
                static_cast<double>(m_procJitterUs));
            totalUs = std::max<int64_t>(0, totalUs + static_cast<int64_t>(std::llround(jitterUs)));
        }
        Simulator::Schedule(MicroSeconds(totalUs),
            &DirectoryServerApp::ProcessQuery, this, interest);
    }

private:
    void ProcessQuery(shared_ptr<const Interest> interest) {
        m_currentQueueLength--;

        if (m_isFailed) {
            m_queriesDropped++;
            return;
        }

        const auto& name = interest->getName();
        // Expected: /dir/query/<qid>
        // Extract qid string from Name component
        std::string qidStr;
        if (name.size() >= 3) {
            const auto& comp = name.at(2);
            qidStr = std::string(reinterpret_cast<const char*>(comp.value()),
                                  comp.value_size());
        }

        // Try to resolve query text from ApplicationParameters
        std::string queryText;
        if (interest->hasApplicationParameters()) {
            auto params = interest->getApplicationParameters();
            queryText = std::string(reinterpret_cast<const char*>(params.value()),
                                     params.value_size());
        }

        // Lookup: first try qid, then try text→qid mapping
        std::vector<DirectoryResult> results;
        if (m_mode == "oracle") {
            results = OracleLookup(qidStr, queryText);
        } else if (m_mode == "bm25") {
            results = BM25Lookup(queryText);  // stub
        } else if (m_mode == "embed") {
            results = EmbedLookup(queryText);  // stub
        }

        // Limit to topK
        if (results.size() > m_topK) {
            results.resize(m_topK);
        }

        // Build response payload: TSV format
        // doc_id\tcanonical_name\tdomain_id\tscore\n
        std::ostringstream payload;
        for (const auto& r : results) {
            payload << r.docId << "\t" << r.canonicalName << "\t"
                    << r.domainId << "\t" << r.score << "\n";
        }
        std::string payloadStr = payload.str();

        // Create Data
        auto data = std::make_shared<::ndn::Data>(interest->getName());
        data->setFreshnessPeriod(::ndn::time::seconds(10));
        data->setContent(reinterpret_cast<const uint8_t*>(payloadStr.data()),
                         payloadStr.size());

        ::ndn::security::SigningInfo signingInfo(
            ::ndn::security::SigningInfo::SIGNER_TYPE_SHA256);
        m_keyChain.sign(*data, signingInfo);

        m_appLink->onReceiveData(*data);

        m_queriesProcessed++;
        m_totalBytesServed += data->wireEncode().size();
    }

    std::vector<DirectoryResult> OracleLookup(const std::string& qid,
                                               const std::string& queryText) {
        // Direct qid lookup
        auto it = m_oracleIndex.find(qid);
        if (it != m_oracleIndex.end()) {
            return it->second;
        }
        // Try text→qid
        auto tit = m_textToQid.find(queryText);
        if (tit != m_textToQid.end()) {
            auto it2 = m_oracleIndex.find(tit->second);
            if (it2 != m_oracleIndex.end()) {
                return it2->second;
            }
        }
        // Try prefix match: qid might be "q0042" or just "42"
        // qrels uses format "q0000"
        if (!qid.empty()) {
            std::string paddedQid = qid;
            if (paddedQid[0] != 'q') {
                // Pad to q%04d format
                try {
                    uint32_t num = std::stoul(qid);
                    char buf[16];
                    snprintf(buf, sizeof(buf), "q%04u", num);
                    paddedQid = buf;
                } catch (...) {}
            }
            auto it3 = m_oracleIndex.find(paddedQid);
            if (it3 != m_oracleIndex.end()) {
                return it3->second;
            }
        }
        return {};  // Not found
    }

    std::vector<DirectoryResult> BM25Lookup(const std::string& /*queryText*/) {
        // Stub — returns empty. Implement BM25 scoring later.
        return {};
    }

    std::vector<DirectoryResult> EmbedLookup(const std::string& /*queryText*/) {
        // Stub — returns empty. Implement embedding-based lookup later.
        return {};
    }

    void InjectFailure() {
        m_isFailed = true;
        std::cout << "[DirectoryServer] FAILURE INJECTED at t="
                  << Simulator::Now().GetSeconds() << "s" << std::endl;
    }

    // Configuration
    std::string m_mode = "oracle";
    uint32_t m_topK = 5;
    uint32_t m_procDelayMs = 2;
    uint32_t m_procJitterUs = 0;
    Ptr<UniformRandomVariable> m_procJitterRv;

    // Failure injection
    bool m_failureEnabled = false;
    Time m_failureTime;
    bool m_isFailed = false;

    // Oracle index: qid → results
    std::unordered_map<std::string, std::vector<DirectoryResult>> m_oracleIndex;
    std::unordered_map<std::string, std::string> m_textToQid;

    // Signing
    ::ndn::KeyChain m_keyChain;

    // Statistics
    uint64_t m_queriesProcessed = 0;
    uint64_t m_totalBytesServed = 0;
    uint64_t m_queriesDropped = 0;
    uint32_t m_currentQueueLength = 0;
    uint32_t m_peakQueueLength = 0;
};

// =============================================================================
// TypeId Registration
// =============================================================================

NS_OBJECT_ENSURE_REGISTERED(DirectoryServerApp);

TypeId
DirectoryServerApp::GetTypeId() {
    static TypeId tid = TypeId("ns3::ndn::DirectoryServerApp")
        .SetParent<App>()
        .SetGroupName("Ndn")
        .AddConstructor<DirectoryServerApp>()
        .AddAttribute("Mode", "Retrieval mode: oracle|bm25|embed",
                       StringValue("oracle"),
                       MakeStringAccessor(&DirectoryServerApp::m_mode),
                       MakeStringChecker())
        .AddAttribute("TopK", "Number of results to return",
                       UintegerValue(5),
                       MakeUintegerAccessor(&DirectoryServerApp::m_topK),
                       MakeUintegerChecker<uint32_t>())
        .AddAttribute("ProcDelayMs", "Processing delay per query (ms)",
                       UintegerValue(2),
                       MakeUintegerAccessor(&DirectoryServerApp::m_procDelayMs),
                       MakeUintegerChecker<uint32_t>())
        .AddAttribute("ProcJitterUs", "Uniform per-query processing jitter (+/- us)",
                       UintegerValue(0),
                       MakeUintegerAccessor(&DirectoryServerApp::m_procJitterUs),
                       MakeUintegerChecker<uint32_t>());
    return tid;
}

} // namespace ndn
} // namespace ns3
