/**
 * @file iroute-strategy.hpp
 * @brief Semantic forwarding strategy for iRoute protocol.
 *
 * This strategy extends NFD's default forwarding behavior with semantic-based
 * routing. It extracts semantic vectors from Interest packets and uses the
 * RouteManager to find the best matching next-hops.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see Design_Guide.md Section 2.4 for design specifications
 */

#pragma once

#include "NFD/daemon/fw/strategy.hpp"
#include "NFD/daemon/fw/forwarder.hpp"
#include "NFD/daemon/table/fib.hpp"

#include <cstdint>
#include <unordered_map>

namespace nfd {
namespace fw {

/**
 * @class SemanticStrategy
 * @brief Forwarding strategy that uses semantic similarity for routing.
 *
 * SemanticStrategy extends NFD's Strategy base class to implement
 * content-based routing using 64-dimensional semantic vectors. When an
 * Interest is received:
 * 1. Check if it has ApplicationParameters containing a SemanticVector
 * 2. If yes: decode the vector and query RouteManager for best matches
 * 3. Forward to the top-K matched faces (excluding ingress face)
 * 4. If no matches or no vector: reject the Interest
 *
 * @par Strategy Name
 * `/localhost/nfd/strategy/semantic/%FD%01`
 *
 * @par Parameters
 * - TopK: Maximum number of faces to forward to (default: 3)
 * - Alpha: Weight for semantic similarity (default: 0.7)
 * - Beta: Weight for physical cost (default: 0.3)
 *
 * @note This strategy does NOT implement retransmission suppression.
 *       In production, consider mixing with RetxSuppressionExponential.
 *
 * @see RouteManager for RIB lookup
 * @see SemanticVector for vector encoding
 */
class SemanticStrategy : public Strategy
{
public:
    // ========================================================================
    // Global Statistics (static)
    // ========================================================================
    
    /** @brief Total Interests forwarded via semantic routing */
    static uint64_t s_totalInterestsSent;
    
    /** @brief Interests sent to non-primary paths (Top-K with K>1) */
    static uint64_t s_redundantInterests;
    
    /** @brief Peak PIT size observed */
    static uint64_t s_pitPeakSize;
    
    /** @brief Total hop count accumulated across all forwarded Interests */
    static uint64_t s_totalHopCount;
    
    /** @brief Number of Interests forwarded (for average hop calculation) */
    static uint64_t s_forwardedInterestCount;
    
    /** @brief Total bytes sent (Interest packets including semantic vectors) */
    static uint64_t s_totalBytesSent;
    
    /** @brief Total Data bytes received */
    static uint64_t s_totalDataBytesReceived;
    
    /** @brief Resets all statistics counters */
    static void ResetStatistics() {
        s_totalInterestsSent = 0;
        s_redundantInterests = 0;
        s_pitPeakSize = 0;
        s_totalHopCount = 0;
        s_forwardedInterestCount = 0;
        s_totalBytesSent = 0;
        s_totalDataBytesReceived = 0;
    }
    
    /** @brief Returns average hop count per Interest */
    static double GetAverageHopCount() {
        if (s_forwardedInterestCount == 0) return 0.0;
        return static_cast<double>(s_totalHopCount) / s_forwardedInterestCount;
    }
    
    /** @brief Returns overhead ratio = TotalBytesSent / TotalDataBytesReceived */
    static double GetOverheadRatio() {
        if (s_totalDataBytesReceived == 0) return 0.0;
        return static_cast<double>(s_totalBytesSent) / s_totalDataBytesReceived;
    }

public:
    /**
     * @brief Constructs a SemanticStrategy instance.
     *
     * @param forwarder Reference to the NFD forwarder.
     * @param name Strategy instance name (may include parameters).
     */
    explicit
    SemanticStrategy(Forwarder& forwarder, const Name& name = getStrategyName());

    /**
     * @brief Returns the strategy's registered name.
     *
     * @return The strategy name: `/localhost/nfd/strategy/semantic/%FD%01`
     */
    static const Name&
    getStrategyName();

    /**
     * @brief Destructor.
     */
    ~SemanticStrategy() override;

public: // triggers
    /**
     * @brief Triggered when an Interest is received.
     *
     * This is the core forwarding logic. The workflow is:
     * 1. Check if Interest has ApplicationParameters
     * 2. Decode SemanticVector from parameters
     * 3. Query RouteManager::findBestMatches()
     * 4. Apply penalty to score if enabled
     * 5. Forward Interest to matching faces (excluding ingress)
     * 6. If no matches: reject the Interest
     *
     * @param interest The received Interest packet.
     * @param ingress The face/endpoint from which the Interest arrived.
     * @param pitEntry The PIT entry for this Interest.
     *
     * @note Loop prevention: never forwards to ingress face.
     * @note Does NOT use recursion.
     */
    void
    afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                         const shared_ptr<pit::Entry>& pitEntry) override;

    /**
     * @brief Triggered when a Data packet is received.
     *
     * Used for instrumentation to count received Data bytes.
     * Also decreases penalty for the face that satisfied the Interest.
     *
     * @param data The received Data packet.
     * @param ingress The face/endpoint from which the Data arrived.
     * @param pitEntry The PIT entry for this Data.
     */
    void
    afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                     const shared_ptr<pit::Entry>& pitEntry) override;

    /**
     * @brief Triggered when a Nack is received.
     *
     * Increases penalty for the face that returned the Nack.
     *
     * @param nack The received Nack packet.
     * @param ingress The face/endpoint from which the Nack arrived.
     * @param pitEntry The PIT entry for this Nack.
     */
    void
    afterReceiveNack(const lp::Nack& nack, const FaceEndpoint& ingress,
                     const shared_ptr<pit::Entry>& pitEntry) override;

private:
    // ========================================================================
    // Configuration Parameters
    // ========================================================================

    /**
     * @brief Maximum number of faces to forward to.
     */
    size_t m_topK;

    /**
     * @brief Weight for semantic similarity in score calculation.
     */
    double m_alpha;

    /**
     * @brief Weight for physical cost penalty in score calculation.
     */
    double m_beta;

    /**
     * @brief Vector dimension for semantic vectors.
     */
    size_t m_vectorDim;

    // ========================================================================
    // Penalty Mechanism Parameters
    // ========================================================================

    /**
     * @brief Enable/disable penalty mechanism.
     */
    bool m_enablePenalty;

    /**
     * @brief Penalty decay factor (0-1). Lower = faster decay.
     * penalty_new = penalty_old * gamma
     */
    double m_penaltyGamma;

    /**
     * @brief Penalty step for timeout/nack events.
     */
    double m_penaltyStep;

    /**
     * @brief Per-face penalty map.
     * Higher penalty = less likely to forward to this face.
     */
    std::unordered_map<FaceId, double> m_facePenalty;

    /**
     * @brief Reference to the NFD forwarder for FIB access.
     *
     * Used for two-stage routing: semantic stage determines target router,
     * then FIB lookup finds physical next-hop.
     */
    Forwarder& m_forwarderRef;

protected:
    /**
     * @brief Gets the FIB from the forwarder.
     *
     * @return Reference to the FIB table.
     */
    Fib& getFib()
    {
        return m_forwarderRef.getFib();
    }

    /**
     * @brief Get penalty for a face.
     */
    double getFacePenalty(FaceId faceId) const
    {
        auto it = m_facePenalty.find(faceId);
        return it != m_facePenalty.end() ? it->second : 0.0;
    }

    /**
     * @brief Increase penalty for a face (on timeout/nack).
     */
    void increasePenalty(FaceId faceId);

    /**
     * @brief Decrease penalty for a face (on success).
     */
    void decreasePenalty(FaceId faceId);
};

} // namespace fw
} // namespace nfd

