/**
 * @file centralized-search-consumer.hpp
 * @brief Centralized search baseline consumer.
 *
 * Implements centralized search workflow:
 * - Stage-1: Send query to centralized search oracle
 * - Stage-2: Fetch content using returned canonicalName
 *
 * This serves as a lower-bound (in terms of query-plane overhead) baseline.
 */

#pragma once

#include "iroute-vector.hpp"
#include "iroute-discovery-consumer.hpp"  // Reuse TxRecord and QueryItem
#include "flooding-discovery-consumer.hpp"  // Reuse FailureType

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

#include <vector>
#include <map>

namespace ns3 {
namespace ndn {

/**
 * @struct CentralizedTxRecord
 * @brief Transaction record for centralized search.
 */
struct CentralizedTxRecord : public TxRecord {
    FailureType failureType = FailureType::SUCCESS;
    uint64_t dotProductCount = 0;  ///< Compute proxy (from oracle)
    
    std::string failureTypeStr() const {
        return FailureTypeToString(failureType);
    }
    
    std::string toCsvLineExtended() const {
        std::ostringstream oss;
        oss << TxRecord::toCsvLine() << ","
            << failureTypeStr() << ","
            << dotProductCount;
        return oss.str();
    }
    
    static std::string csvHeaderExtended() {
        return TxRecord::csvHeader() + ",failureType,dotProductCount";
    }
};

/**
 * @class CentralizedSearchConsumer
 * @brief Consumer that queries a centralized search oracle.
 *
 * Two-stage workflow:
 * - Stage-1: Send query to /search/oracle, get (canonicalName, domainId)
 * - Stage-2: Fetch content from returned canonicalName
 */
class CentralizedSearchConsumer : public App
{
public:
    static TypeId GetTypeId();

    CentralizedSearchConsumer();
    virtual ~CentralizedSearchConsumer();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Sets the query trace for batch experiments.
     */
    void SetQueryTrace(const std::vector<IRouteDiscoveryConsumer::QueryItem>& queries);

    /**
     * @brief Sets the oracle prefix (default: /search/oracle)
     */
    void SetOraclePrefix(const Name& prefix) { m_oraclePrefix = prefix; }

    // ========================================================================
    // Statistics
    // ========================================================================

    const std::vector<CentralizedTxRecord>& GetTransactions() const { return m_transactions; }
    
    void ExportToCsv(const std::string& filename) const;
    void PrintStats() const;

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnData(shared_ptr<const Data> data) override;
    virtual void OnNack(shared_ptr<const lp::Nack> nack) override;

private:
    // ========================================================================
    // Stage-1: Query Oracle
    // ========================================================================

    void StartNextQuery();
    void SendOracleQuery(uint64_t queryId, const iroute::SemanticVector& vec);
    void HandleOracleReply(const Data& data);
    void HandleOracleTimeout(const std::string& queryNameUri);

    // ========================================================================
    // Stage-2: Fetch Content
    // ========================================================================

    void SendFetchInterest(const Name& canonicalName, uint32_t domainId, uint64_t queryId);
    void HandleFetchData(const Data& data);
    void HandleFetchTimeout(const Name& canonicalName);

    // ========================================================================
    // Helper Methods
    // ========================================================================

    void FinalizeQuery(uint64_t queryId);

private:
    // Configuration
    Name m_oraclePrefix;
    Time m_interestLifetime;
    uint32_t m_fetchTimeoutMs = 4000;
    double m_frequency = 1.0;

    // Query trace
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
    size_t m_queryIndex = 0;

    // Transaction tracking
    std::vector<CentralizedTxRecord> m_transactions;
    std::map<uint64_t, CentralizedTxRecord> m_activeTxs;

    // Pending oracle query tracking
    std::map<std::string, uint64_t> m_oracleQueryToId;
    std::map<std::string, EventId> m_oracleTimeouts;

    // Pending fetch tracking
    struct PendingFetchInfo {
        uint32_t domainId;
        uint64_t queryId;
    };
    std::map<Name, PendingFetchInfo> m_pendingFetch;
    std::map<Name, EventId> m_fetchTimeouts;

    // Scheduling
    EventId m_sendEvent;

    // Statistics
    uint32_t m_totalQueries = 0;
    uint32_t m_successfulQueries = 0;
};

} // namespace ndn
} // namespace ns3
