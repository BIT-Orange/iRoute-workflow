/**
 * @file semantic-consumer.hpp
 * @brief Consumer app that sends Interests with SemanticVector in ApplicationParameters.
 *
 * This consumer is designed to work with SemanticStrategy by embedding a semantic
 * query vector in the Interest's ApplicationParameters field.
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include "iroute-vector.hpp"

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

#include <vector>
#include <unordered_map>
#include <map>
#include <tuple>

namespace ns3 {
namespace ndn {

// Forward declaration for query trace support
struct ConsumerQueryEntry;

/**
 * @class SemanticConsumer
 * @brief NDN Consumer that embeds semantic query vectors in Interests.
 *
 * This consumer periodically sends Interests to a specified prefix with
 * a SemanticVector encoded in the ApplicationParameters. This triggers
 * SemanticStrategy's vector-based routing instead of name-based FIB lookup.
 *
 * @par Interest Format
 * @verbatim
 * Name: <Prefix>/<SequenceNumber>
 * ApplicationParameters: SemanticVector TLV (Type 128)
 * @endverbatim
 */
class SemanticConsumer : public App
{
public:
    /**
     * @brief Get the NS-3 TypeId for this class.
     */
    static TypeId GetTypeId();

    /**
     * @brief Default constructor.
     */
    SemanticConsumer();

    /**
     * @brief Destructor.
     */
    virtual ~SemanticConsumer();

    /**
     * @brief Sets the query vector that will be included in all Interests.
     *
     * @param vector The 64-dimensional semantic query vector.
     */
    void SetQueryVector(const iroute::SemanticVector& vector);

    /**
     * @brief Gets the current query vector.
     *
     * @return Const reference to the query vector.
     */
    const iroute::SemanticVector& GetQueryVector() const;

    /**
     * @brief Gets the number of Data packets received.
     *
     * @return Count of received Data packets.
     */
    uint32_t GetDataReceived() const { return m_dataReceived; }

    /**
     * @brief Gets the number of Interests sent.
     *
     * @return Count of sent Interests.
     */
    uint32_t GetInterestsSent() const { return m_interestsSent; }

    /**
     * @brief Sets the query trace for batch query execution.
     *
     * When set, the consumer will iterate through the query trace entries
     * instead of using the single m_queryVector.
     *
     * @param trace Vector of query entries loaded from CSV.
     */
    void SetQueryTrace(const std::vector<ConsumerQueryEntry>& trace);

    /**
     * @brief Gets the current query trace.
     *
     * @return Const reference to the query trace vector.
     */
    const std::vector<ConsumerQueryEntry>& GetQueryTrace() const { return m_queryTrace; }

    /**
     * @brief Gets the current query index in the trace.
     *
     * @return The index of the next query to be sent.
     */
    size_t GetCurrentQueryIndex() const { return m_queryIndex; }

protected:
    // Application lifecycle
    virtual void StartApplication() override;
    virtual void StopApplication() override;

    // NDN callbacks
    virtual void OnData(shared_ptr<const Data> data) override;
    virtual void OnNack(shared_ptr<const lp::Nack> nack) override;

private:
    /**
     * @brief Send a single Interest with the semantic query vector.
     */
    void SendInterest();

    /**
     * @brief Schedule the next Interest transmission.
     */
    void ScheduleNextInterest();

private:
    // Configuration
    Name m_prefix;                          ///< Name prefix to request
    double m_frequency;                     ///< Interest sending frequency (per second)
    Time m_interestLifetime;               ///< Interest lifetime
    uint32_t m_vectorDim;                  ///< Expected vector dimension (default: 384)

    // Semantic query
    iroute::SemanticVector m_queryVector;   ///< The semantic query vector

    // Query trace for batch execution
    std::vector<ConsumerQueryEntry> m_queryTrace;  ///< Pre-loaded query entries
    size_t m_queryIndex;                    ///< Current index in query trace

    // Ground truth tracking (seqNum -> expected producer info)
    struct QueryGroundTruth {
        uint32_t expectedProducerId;
        double similarity;
    };
    std::unordered_map<uint64_t, QueryGroundTruth> m_groundTruth;

    // Runtime state
    uint64_t m_seqNum;                      ///< Current sequence number
    EventId m_sendEvent;                    ///< Event for scheduled Interest

    // Statistics
    uint32_t m_interestsSent;               ///< Total Interests sent
    uint32_t m_dataReceived;                ///< Total Data received
    uint32_t m_nacksReceived;               ///< Total NACKs received

    // Semantic accuracy statistics
    uint32_t m_semanticHits;                ///< Data from correct producer
    uint32_t m_semanticMisses;              ///< Data from wrong producer
    uint32_t m_highSimHits;                 ///< Hits for similarity >= 0.8
    uint32_t m_highSimMisses;
    uint32_t m_medSimHits;                  ///< Hits for 0.5 <= similarity < 0.8
    uint32_t m_medSimMisses;
    uint32_t m_lowSimHits;                  ///< Hits for similarity < 0.5
    uint32_t m_lowSimMisses;

    // RTT tracking for multipath experiments
    std::vector<double> m_rttSamples;       ///< RTT samples in seconds
    std::map<uint64_t, Time> m_pendingInterests;  ///< seqNo -> send time

public:
    // Accuracy statistics getters
    uint32_t GetSemanticHits() const { return m_semanticHits; }
    uint32_t GetSemanticMisses() const { return m_semanticMisses; }
    uint32_t GetHighSimHits() const { return m_highSimHits; }
    uint32_t GetHighSimMisses() const { return m_highSimMisses; }
    uint32_t GetMedSimHits() const { return m_medSimHits; }
    uint32_t GetMedSimMisses() const { return m_medSimMisses; }
    uint32_t GetLowSimHits() const { return m_lowSimHits; }
    uint32_t GetLowSimMisses() const { return m_lowSimMisses; }

    /**
     * @brief Add ground truth for a specific sequence number.
     *
     * Called by ExperimentDataLoader when scheduling queries from consumer_trace.csv.
     * The seqNo corresponds to the Interest sequence number assigned by SendInterest().
     *
     * @param seqNo The sequence number for this query.
     * @param expectedProducerId The expected producer ID from the trace.
     * @param similarity The similarity level for accuracy bucketing (0.0-1.0).
     */
    void AddGroundTruth(uint64_t seqNo, uint32_t expectedProducerId, double similarity);

    /**
     * @brief Print statistics in CSV format to stdout.
     *
     * Output format:
     * similarity_bucket,total_interests,satisfied,semantic_hits
     */
    void PrintStats() const;

    /**
     * @brief Get RTT statistics: mean, p90, p99.
     *
     * @return Tuple of (mean_rtt, p90_rtt, p99_rtt) in seconds.
     */
    std::tuple<double, double, double> GetRttStats() const;

    /**
     * @brief Get raw RTT samples.
     */
    const std::vector<double>& GetRttSamples() const { return m_rttSamples; }
};

} // namespace ndn
} // namespace ns3
