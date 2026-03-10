/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file search-consumer.hpp
 * @brief Two-stage Search Consumer for baseline comparison.
 *
 * Implements Query→Resolve→Fetch flow:
 * 1. SendQuery: Send Interest to /search with SemanticVector
 * 2. OnQueryData: Parse SearchServer response (resolved name, similarity)
 * 3. SendFetch: Send Interest to resolved content name
 * 4. OnFetchData: Receive content and record E2E RTT
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"

#include "iroute-vector.hpp"

#include <ndn-cxx/face.hpp>

#include <unordered_map>
#include <vector>

namespace ns3 {
namespace ndn {

/**
 * @brief TLV types for SearchServer response.
 */
namespace search_tlv {
constexpr uint32_t ResolvedName = 200;  ///< Name wire encoding
constexpr uint32_t Similarity = 201;    ///< float32 bytes
constexpr uint32_t ProducerId = 202;    ///< uint32
constexpr uint32_t Timestamp = 203;     ///< uint64 (optional)
} // namespace search_tlv

/**
 * @struct TxRecord
 * @brief Tracks timing for a single query-fetch transaction.
 */
struct TxRecord {
    uint64_t seqNum;           ///< Sequence number
    Time tQuerySend;           ///< Time query Interest was sent
    Time tQueryRecv;           ///< Time query Data was received
    Time tFetchSend;           ///< Time fetch Interest was sent
    Time tFetchRecv;           ///< Time fetch Data was received
    bool gotQuery = false;     ///< Query Data received
    bool gotFetch = false;     ///< Fetch Data received
    ::ndn::Name resolvedName;  ///< Resolved content name from SearchServer
    double similarity = 0.0;   ///< Similarity score from SearchServer
    uint32_t producerId = 0;   ///< Producer ID from SearchServer
};

/**
 * @class SearchConsumer
 * @brief Two-stage consumer for baseline (SearchServer) comparison.
 *
 * Sends query Interests to /search prefix, receives resolved names,
 * then fetches content from the resolved producer.
 */
class SearchConsumer : public App {
public:
    static TypeId GetTypeId();

    SearchConsumer();
    virtual ~SearchConsumer();

    /**
     * @brief Sets the semantic query vector.
     */
    void SetQueryVector(const iroute::SemanticVector& vec);

    /**
     * @brief Sets the query trace (multiple queries).
     */
    void SetQueryTrace(const std::vector<iroute::SemanticVector>& trace);

    // Statistics accessors
    uint32_t GetQueryInterestsSent() const { return m_queryInterestsSent; }
    uint32_t GetQueryDataReceived() const { return m_queryDataReceived; }
    uint32_t GetFetchInterestsSent() const { return m_fetchInterestsSent; }
    uint32_t GetFetchDataReceived() const { return m_fetchDataReceived; }

    double GetQueryISR() const;
    double GetFetchISR() const;
    double GetE2EISR() const;

    double GetQueryRttAvg() const;
    double GetFetchRttAvg() const;
    double GetE2ERttAvg() const;

    /**
     * @brief Prints summary statistics.
     */
    void PrintStats() const;

    /**
     * @brief Exports detailed per-transaction CSV.
     */
    void ExportCsv(const std::string& filename) const;

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;

private:
    /**
     * @brief Schedules the next query.
     */
    void ScheduleNextQuery();

    /**
     * @brief Sends a query Interest to /search.
     */
    void SendQuery();

    /**
     * @brief Callback when query Data is received.
     */
    void OnQueryData(std::shared_ptr<const ::ndn::Data> data);

    /**
     * @brief Callback when query times out.
     */
    void OnQueryTimeout(std::shared_ptr<const ::ndn::Interest> interest);

    /**
     * @brief Sends a fetch Interest to resolved name.
     */
    void SendFetch(uint64_t seqNum, const ::ndn::Name& resolvedName);

    /**
     * @brief Callback when fetch Data is received.
     */
    void OnFetchData(std::shared_ptr<const ::ndn::Data> data);

    /**
     * @brief Callback when fetch times out.
     */
    void OnFetchTimeout(std::shared_ptr<const ::ndn::Interest> interest);

    /**
     * @brief Parses SearchServer TLV response.
     */
    bool ParseSearchResponse(const ::ndn::Block& content,
                              ::ndn::Name& resolvedName,
                              double& similarity,
                              uint32_t& producerId);

private:
    // Configuration
    Name m_searchPrefix;         ///< Search service prefix (e.g., /search)
    double m_frequency;          ///< Query frequency (Hz)
    Time m_interestLifetime;     ///< Interest lifetime
    uint32_t m_vectorDim;        ///< Vector dimension

    // Query trace
    std::vector<iroute::SemanticVector> m_queryTrace;
    size_t m_queryIndex = 0;
    iroute::SemanticVector m_currentQuery;

    // Transaction tracking
    uint64_t m_seqNum = 0;
    std::unordered_map<uint64_t, TxRecord> m_transactions;
    std::vector<TxRecord> m_completedTransactions;

    // Statistics
    uint32_t m_queryInterestsSent = 0;
    uint32_t m_queryDataReceived = 0;
    uint32_t m_fetchInterestsSent = 0;
    uint32_t m_fetchDataReceived = 0;

    // Scheduling
    EventId m_sendEvent;
    Ptr<UniformRandomVariable> m_jitter;
};

} // namespace ndn
} // namespace ns3
