/**
 * @file flooding-discovery-consumer.hpp
 * @brief Flooding baseline consumer for fair comparison with iRoute.
 *
 * Implements parallel flooding discovery:
 * - Stage-1: Send Discovery Interests to ALL domains simultaneously
 * - Stage-2: Use first successful response to fetch content
 *
 * This serves as an upper-bound baseline for routing accuracy.
 */

#pragma once

#include "iroute-vector.hpp"
#include "iroute-manager.hpp"
#include "iroute-discovery-consumer.hpp"  // Reuse TxRecord

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

#include <vector>
#include <map>
#include <set>
#include <unordered_map>

namespace ns3 {
namespace ndn {

/**
 * @enum FailureType
 * @brief Fine-grained failure classification for analysis.
 */
enum class FailureType {
    SUCCESS = 0,
    DOMAIN_WRONG,       ///< Stage-1 returned wrong domain
    DOC_WRONG,          ///< Stage-2 returned wrong document
    DISCOVERY_TIMEOUT,  ///< Stage-1 all probes timed out
    DISCOVERY_NACK,     ///< Stage-1 received NACK
    FETCH_TIMEOUT,      ///< Stage-2 fetch timed out
    FETCH_NACK          ///< Stage-2 received NACK
};

inline std::string FailureTypeToString(FailureType ft) {
    switch (ft) {
        case FailureType::SUCCESS: return "SUCCESS";
        case FailureType::DOMAIN_WRONG: return "DOMAIN_WRONG";
        case FailureType::DOC_WRONG: return "DOC_WRONG";
        case FailureType::DISCOVERY_TIMEOUT: return "DISCOVERY_TIMEOUT";
        case FailureType::DISCOVERY_NACK: return "DISCOVERY_NACK";
        case FailureType::FETCH_TIMEOUT: return "FETCH_TIMEOUT";
        case FailureType::FETCH_NACK: return "FETCH_NACK";
        default: return "UNKNOWN";
    }
}

/**
 * @struct FloodingTxRecord
 * @brief Extended transaction record with failure type and flooding-specific fields.
 */
struct FloodingTxRecord : public TxRecord {
    FailureType failureType = FailureType::SUCCESS;
    
    uint32_t totalInterestsSent = 0;    ///< Total discovery Interests sent
    uint32_t responsesReceived = 0;     ///< Number of discovery responses received
    uint32_t nacksReceived = 0;         ///< Number of NACKs received
    uint32_t timeoutsOccurred = 0;      ///< Number of timeouts
    
    bool isParallelMode = true;         ///< True = parallel, False = sequential
    
    // Dot-product count for compute proxy
    uint64_t dotProductCount = 0;       ///< Number of dot-product operations performed
    
    std::string failureTypeStr() const {
        return FailureTypeToString(failureType);
    }
    
    std::string toCsvLineExtended() const {
        std::ostringstream oss;
        oss << TxRecord::toCsvLine() << ","
            << failureTypeStr() << ","
            << totalInterestsSent << "," << responsesReceived << ","
            << nacksReceived << "," << timeoutsOccurred << ","
            << (isParallelMode ? 1 : 0) << ","
            << dotProductCount;
        return oss.str();
    }
    
    static std::string csvHeaderExtended() {
        return TxRecord::csvHeader() + 
               ",failureType,totalInterestsSent,responsesReceived,"
               "nacksReceived,timeoutsOccurred,isParallelMode,dotProductCount";
    }
};

/**
 * @class FloodingDiscoveryConsumer
 * @brief Consumer that floods discovery Interests to all domains.
 *
 * Two modes:
 * - Parallel: Send to all domains simultaneously, use first response
 * - Sequential: Send to domains one-by-one until success
 */
class FloodingDiscoveryConsumer : public App
{
public:
    static TypeId GetTypeId();

    FloodingDiscoveryConsumer();
    virtual ~FloodingDiscoveryConsumer();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Sets the list of all domain prefixes to flood.
     */
    void SetAllDomains(const std::vector<Name>& domains);

    /**
     * @brief Sets the query trace for batch experiments.
     */
    void SetQueryTrace(const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries);

    /**
     * @brief Sets flooding mode.
     * @param parallel True for parallel flooding, false for sequential.
     */
    void SetParallelMode(bool parallel) { m_parallelMode = parallel; }

    // ========================================================================
    // Statistics
    // ========================================================================

    const std::vector<FloodingTxRecord>& GetTransactions() const { return m_transactions; }
    
    void ExportToCsv(const std::string& filename) const;
    void PrintStats() const;

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnData(shared_ptr<const Data> data) override;
    virtual void OnNack(shared_ptr<const lp::Nack> nack) override;

private:
    // ========================================================================
    // Stage-1: Flooding Discovery
    // ========================================================================

    void StartNextQuery();
    void SendDiscoveryToAllDomains(uint64_t queryId, const iroute::SemanticVector& vec);
    void SendDiscoveryToDomain(const Name& domainId, uint64_t queryId, const iroute::SemanticVector& vec);
    void HandleDiscoveryReply(const Data& data);


    // ========================================================================
    // Stage-2: Fetch (same as iRoute)
    // ========================================================================

    void SendFetchInterest(const Name& canonicalName, const Name& domainId, uint64_t queryId);
    void HandleFetchData(const Data& data);
    void HandleFetchTimeout(const Name& canonicalName);

    // ========================================================================
    // Helper Methods
    // ========================================================================

    void FinalizeQuery(uint64_t queryId, bool stage2Success);
    void CheckAllProbesComplete(uint64_t queryId);

    // Virtual hook for subclasses (e.g. TagDiscoveryConsumer)
    virtual void SendDiscoveryQuery(uint64_t queryId, const iroute::SemanticVector& vec);

protected:
    void HandleDiscoveryTimeout(const std::string& discNameUri);

    // Configuration
    std::vector<Name> m_allDomains;
    bool m_parallelMode = true;
    uint32_t m_semVerId = 1;
    uint32_t m_probeBudget = 0;   ///< 0 = probe all domains
    Time m_interestLifetime;
    uint32_t m_fetchTimeoutMs = 4000;
    double m_frequency = 1.0;

    // Query trace
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
    size_t m_queryIndex = 0;

    // Transaction tracking
    std::vector<FloodingTxRecord> m_transactions;
    std::map<uint64_t, FloodingTxRecord> m_activeTxs;

    // Pending discovery tracking
    // queryId -> set of pending domain names
    std::map<uint64_t, std::set<std::string>> m_pendingDiscoveries;
    // queryId -> best response so far
    struct BestResponse {
        bool found = false;
        Name canonicalName;
        Name domainId;
        double confidence = -1.0;
    };
    std::map<uint64_t, BestResponse> m_bestResponses;
    // queryId -> (domain -> best confidence)
    std::map<uint64_t, std::map<std::string, double>> m_candidateScores;

    // Pending fetch tracking
    struct PendingFetchInfo {
        Name domainId;
        uint64_t queryId;
    };
    std::map<Name, PendingFetchInfo> m_pendingFetch;
    std::map<Name, EventId> m_fetchTimeouts;

    // Discovery name tracking
    std::map<std::string, EventId> m_discoveryTimeouts;
    std::map<std::string, uint64_t> m_discNameToQueryId;
    std::map<std::string, Name> m_discNameToDomain;

    // Scheduling
    EventId m_sendEvent;

    // Statistics
    uint32_t m_totalQueries = 0;
    uint32_t m_successfulQueries = 0;
};

} // namespace ndn
} // namespace ns3
