/**
 * @file iroute-manager.hpp
 * @brief Route Manager for iRoute semantic routing protocol.
 *
 * This file defines the RouteManager class, which serves as the
 * Routing Information Base (RIB) for the iRoute protocol. Each node
 * has its own RouteManager instance, managed by RouteManagerRegistry.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see Design_Guide.md Section 2.2 for design specifications
 */

#pragma once
#ifndef IROUTE_MANAGER_HPP
#define IROUTE_MANAGER_HPP

#include "iroute-vector.hpp"

#include <ndn-cxx/name.hpp>

#include <cstdint>
#include <mutex>
#include <vector>
#include <optional>
#include <map>
#include <string>

namespace iroute {

// =============================================================================
// Protocol Constants (from paper.tex Section 4)
// =============================================================================

/**
 * @brief Default parameters for the gated scoring formula.
 */
namespace params {
    constexpr double kAlpha = 1.0;          ///< Weight for semantic similarity
    constexpr double kBeta = 0.5;           ///< Weight for cost penalty
    constexpr double kLambda = 20.0;        ///< Sigmoid steepness for gate
    constexpr double kTau = 0.35;           ///< Confidence threshold for probing
    constexpr double kWMax = 10000.0;       ///< Weight clamp value
    constexpr size_t kKMax = 5;             ///< Maximum probing candidates
    constexpr size_t kNMin = 10;            ///< Minimum samples before EWMA penalty
    constexpr double kEwmaAlpha = 0.1;      ///< EWMA decay factor
    constexpr double kDelta = 0.01;         ///< Hysteresis centroid drift threshold
    constexpr double kGamma = 0.2;          ///< Hysteresis radius change threshold
} // namespace params

// =============================================================================
// TLV Types for Semantic-LSA Data Content
// =============================================================================

namespace lsa_tlv {
    constexpr uint32_t OriginId = 140;
    constexpr uint32_t SemVerId = 141;
    constexpr uint32_t SeqNo = 142;
    constexpr uint32_t Lifetime = 143;
    constexpr uint32_t Scope = 144;
    constexpr uint32_t CentroidList = 145;
    constexpr uint32_t CentroidEntry = 146;
    constexpr uint32_t CentroidId = 147;
    constexpr uint32_t Radius = 148;
    constexpr uint32_t Weight = 149;
} // namespace lsa_tlv

// =============================================================================
// Data Structures
// =============================================================================

/**
 * @struct CentroidEntry
 * @brief Represents a single semantic centroid within a domain.
 *
 * Per paper.tex Table I: Centroids field contains tuples {(ID_i, C_i, r_i, w_i)}.
 * - CentroidID: Stable ID to track updates across epochs
 * - C: L2-normalized centroid vector
 * - radius: 95th percentile of intra-cluster cosine distances
 * - weight: Object count in cluster (clamped to w_max)
 */
struct CentroidEntry {
    uint32_t centroidId;   ///< Stable ID across updates
    SemanticVector C;      ///< L2-normalized centroid vector
    double radius;         ///< r_i: coverage radius (95th percentile intra-cluster distance)
    double weight;         ///< w_i: object count in cluster (clamped to w_max)

    CentroidEntry() : centroidId(0), radius(0.0), weight(0.0) {}

    CentroidEntry(uint32_t id, const SemanticVector& vec, double r, double w)
        : centroidId(id), C(vec), radius(r), weight(w) {}
};

/**
 * @struct EwmaState
 * @brief Tracks EWMA success rate for a domain (anti-blackholing mechanism).
 *
 * Per paper.tex Section 4.4: S'_D = S_D × EWMA_success(D)
 * Penalty activates after n_min samples.
 */
struct EwmaState {
    double successRate = 1.0;   ///< Current EWMA success rate
    size_t sampleCount = 0;     ///< Number of samples collected

    /**
     * @brief Update EWMA with a new outcome.
     * @param success true if Stage-2 fetch succeeded, false otherwise.
     */
    void update(bool success) {
        double outcome = success ? 1.0 : 0.0;
        successRate = params::kEwmaAlpha * outcome + (1.0 - params::kEwmaAlpha) * successRate;
        ++sampleCount;
    }

    /**
     * @brief Get the penalty multiplier.
     * @return 1.0 if samples < n_min, else successRate.
     */
    double getPenalty() const {
        return (sampleCount >= params::kNMin) ? successRate : 1.0;
    }
};

/**
 * @struct DomainEntry
 * @brief Represents a domain's semantic advertisement in the RIB.
 *
 * Each domain can advertise multiple centroids (bounded by M).
 * This replaces the old single-centroid RibEntry.
 */
struct DomainEntry {
    ndn::Name domainId;                      ///< Topologically routable prefix (OriginID)
    uint32_t semVerId;                       ///< SemVerID for embedding version
    uint64_t seqNo;                          ///< Monotonic sequence number
    double lifetime;                         ///< Soft-state validity (seconds)
    uint8_t scope;                           ///< Propagation scope
    std::vector<CentroidEntry> centroids;    ///< List of centroids (bounded by M)
    double cost;                             ///< Network cost (hop count)
    EwmaState ewma;                          ///< EWMA success tracking

    DomainEntry() : semVerId(0), seqNo(0), lifetime(0.0), scope(0), cost(0.0) {}

    DomainEntry(const ndn::Name& domain, uint32_t semVer, uint64_t seq, double life = 300.0)
        : domainId(domain), semVerId(semVer), seqNo(seq), lifetime(life), scope(0), cost(0.0) {}
};

/**
 * @struct RibEntry
 * @brief Legacy single-centroid entry (deprecated, use DomainEntry).
 *
 * Kept for backward compatibility with existing code during transition.
 *
 * @deprecated Use DomainEntry for new code.
 */
struct RibEntry {
    ndn::Name prefix;
    SemanticVector centroid;
    double cost;
    std::string originRouter;
    ndn::Name originRouterName;

    RibEntry() : cost(0.0) {}

    RibEntry(const ndn::Name& prefix,
             const SemanticVector& centroid,
             double cost,
             const std::string& originRouter = "")
        : prefix(prefix)
        , centroid(centroid)
        , cost(cost)
        , originRouter(originRouter)
        , originRouterName(originRouter.empty() ? ndn::Name() : ndn::Name("/" + originRouter))
    {
    }
};

/**
 * @struct NextHopResult
 * @brief Represents a ranked next-hop result from semantic routing lookup.
 *
 * Contains the target router prefix along with its computed score.
 * The physical next-hop face is determined via FIB lookup using this prefix.
 */
struct NextHopResult {
    /**
     * @brief The target router's prefix.
     *
     * This is used to query the FIB for the actual next-hop face.
     */
    ndn::Name targetRouter;

    /**
     * @brief The computed routing score.
     *
     * Higher scores indicate better matches. Computed as:
     * Score = alpha * similarity - beta * normalizedCost
     */
    double score;

    /**
     * @brief The originating router's prefix (e.g., "/router/42").
     *
     * This is the physical node that hosts the matched content.
     * SemanticStrategy uses this for FIB lookup to find the path.
     */
    ndn::Name originRouter;

    /**
     * @brief The physical routing cost to reach the origin router.
     *
     * Used for loop-freedom guarantee and cost-based logging.
     */
    double cost;

    /**
     * @brief Default constructor.
     *
     * Creates a NextHopResult with empty fields and score=0.0.
     */
    NextHopResult() : score(0.0), cost(0.0) {}

    /**
     * @brief Constructs a NextHopResult with all fields.
     *
     * @param targetRouter The target router's name prefix.
     * @param score The computed score.
     * @param originRouter The physical node's router prefix.
     * @param cost The physical routing cost.
     */
    NextHopResult(const ndn::Name& targetRouter, double score,
                  const ndn::Name& originRouter = ndn::Name(),
                  double cost = 0.0)
        : targetRouter(targetRouter)
        , score(score)
        , originRouter(originRouter)
        , cost(cost)
    {
    }

    /**
     * @brief Comparison operator for sorting (descending by score).
     *
     * @param other The other NextHopResult to compare with.
     * @return true if this result has a higher score than other.
     */
    bool operator>(const NextHopResult& other) const
    {
        return score > other.score;
    }
};

/**
 * @struct DomainResult
 * @brief Represents a ranked domain result from the new gated scoring formula.
 *
 * Per paper.tex Eq. 2:
 * S_D(q) = max_i [α·(q·C_i)·σ(λ(r_i-d))·log(1+w_i) - β·Cost_D/Cost_max]
 */
struct DomainResult {
    ndn::Name domainId;        ///< Target domain prefix (for FIB lookup)
    double score;              ///< Combined gated score (S_D)
    double confidence;         ///< Semantic confidence: max_i [(q·C_i)·gate]
    uint32_t bestCentroidId;   ///< ID of the best-matching centroid
    double cost;               ///< Network cost (hop count)

    DomainResult() : score(0.0), confidence(0.0), bestCentroidId(0), cost(0.0) {}

    DomainResult(const ndn::Name& domain, double s, double conf, uint32_t cid, double c)
        : domainId(domain), score(s), confidence(conf), bestCentroidId(cid), cost(c) {}

    bool operator>(const DomainResult& other) const {
        return score > other.score;
    }
};

/**
 * @class RouteManager
 * @brief Class managing the semantic Routing Information Base (RIB) per node.
 *
 * RouteManager is the core component that bridges the control plane
 * (RoutingProtocol application) with the data plane (SemanticStrategy).
 * It maintains a collection of RibEntry objects and provides methods
 * to update routes and find the best next-hops based on semantic similarity.
 *
 * @par Design Pattern: Per-Node Instance
 * Each NS-3 node has its own RouteManager, managed by RouteManagerRegistry.
 * Use RouteManagerRegistry::getOrCreate(nodeId, vectorDim) to access.
 *
 * @par Thread Safety
 * All public methods are thread-safe. Internal synchronization is provided
 * via a mutex.
 *
 * @par Example Usage:
 * @code
 * // Get RouteManager for current node
 * uint32_t nodeId = GetNode()->GetId();
 * auto rm = RouteManagerRegistry::getOrCreate(nodeId, 384);
 *
 * // Update a route
 * rm->updateRoute(Name("/router/1"), semanticVector, 1.0, "router_1");
 *
 * // Find best matches
 * auto nextHops = rm->findBestMatches(queryVector, 3, 0.7, 0.3);
 * @endcode
 *
 * @see SemanticStrategy for how this is used in forwarding
 * @see RoutingProtocol for how routes are populated
 */
class RouteManager {
public:
    /**
     * @brief Constructs a RouteManager with specified vector dimension.
     *
     * @param vectorDim The expected dimension for semantic vectors (default: 384).
     */
    explicit RouteManager(size_t vectorDim = 384);

    /**
     * @brief Destructor.
     */
    ~RouteManager() = default;

    /**
     * @brief Copy constructor (deleted for thread safety).
     */
    RouteManager(const RouteManager&) = delete;

    /**
     * @brief Copy assignment operator (deleted for thread safety).
     */
    RouteManager& operator=(const RouteManager&) = delete;

    /**
     * @brief Move constructor (deleted for thread safety).
     */
    RouteManager(RouteManager&&) = delete;

    /**
     * @brief Move assignment operator (deleted for thread safety).
     */
    RouteManager& operator=(RouteManager&&) = delete;

    // ========================================================================
    // Dimension Configuration
    // ========================================================================

    /**
     * @brief Sets the expected vector dimension.
     *
     * @param dim The vector dimension.
     */
    void setVectorDim(size_t dim);

    /**
     * @brief Gets the expected vector dimension.
     *
     * @return The current vector dimension.
     */
    size_t getVectorDim() const;

    // ========================================================================
    // Route Management
    // ========================================================================

    /**
     * @brief Adds or updates a route in the RIB.
     *
     * If a route with the same prefix already exists, it is updated with
     * the new centroid and cost. Otherwise, a new entry is created.
     *
     * @param prefix The router's name prefix.
     * @param centroid The semantic centroid vector.
     * @param cost The routing cost (metric).
     * @param originRouter The router ID that originated this route (for failover).
     *
     * @note Thread-safe.
     * @note In two-stage routing, physical next-hop is determined via FIB lookup.
     *
     * @par Complexity: O(N) where N is the number of existing routes
     *      (linear search for existing prefix).
     */
    void updateRoute(const ndn::Name& prefix,
                     const SemanticVector& centroid,
                     double cost,
                     const std::string& originRouter = "");

    /**
     * @brief Removes a route from the RIB by prefix.
     *
     * @param prefix The router's name prefix to remove.
     * @return true if a route was removed, false if not found.
     *
     * @note Thread-safe.
     *
     * @par Complexity: O(N) where N is the number of routes.
     */
    bool removeRoute(const ndn::Name& prefix);

    /**
     * @brief Removes all routes originated by a specific router.
     *
     * Used for failover scenarios when a router goes down.
     *
     * @param originRouter The router ID whose routes should be removed.
     * @return Number of routes removed.
     *
     * @note Thread-safe.
     *
     * @par Complexity: O(N) where N is the number of routes.
     */
    size_t removeRoutesByOrigin(const std::string& originRouter);

    /**
     * @brief Clears all routes from the RIB.
     *
     * @note Thread-safe.
     *
     * @par Complexity: O(N) where N is the number of routes.
     */
    void clearAllRoutes();

    // ========================================================================
    // Route Lookup
    // ========================================================================

    /**
     * @brief Finds the best matching next-hops for a semantic query.
     *
     * This is the core routing algorithm. For each RIB entry, it computes
     * a combined score based on semantic similarity and physical cost:
     *
     * @f[
     *   \text{Score} = \alpha \cdot \text{Similarity}(query, centroid)
     *                - \beta \cdot \text{Normalize}(cost)
     * @f]
     *
     * The top-K entries with the highest scores are returned.
     *
     * @param query The semantic query vector from the Interest packet.
     * @param topK Maximum number of next-hops to return.
     * @param alpha Weight for semantic similarity (typically 0.7).
     * @param beta Weight for physical cost penalty (typically 0.3).
     *
     * @return Vector of NextHopResult, sorted by score (descending).
     *         May contain fewer than topK results if RIB has fewer entries.
     *         Returns empty vector if RIB is empty or query is invalid.
     *
     * @throws std::invalid_argument If query vector is empty.
     *
     * @note Thread-safe.
     * @note Uses std::partial_sort for efficiency when topK << N.
     *
     * @par Complexity: O(N + K log K) where N is the number of routes
     *      and K is topK.
     *
     * @see computeCosineSimilarity() in SemanticVector
     */
    std::vector<NextHopResult> findBestMatches(const SemanticVector& query,
                                                size_t topK,
                                                double alpha,
                                                double beta) const;

    /**
     * @brief Finds the best matching domains using the new gated scoring formula.
     *
     * Per paper.tex Eq. 2:
     * S_D(q) = max_i [α·(q·C_i)·σ(λ(r_i-d))·log(1+w_i) - β·Cost_D/Cost_max]
     *
     * Uses the index corresponding to semVerId (curr or prev).
     *
     * @param query L2-normalized semantic query vector.
     * @param topK Maximum domains to return (capped at k_max=5).
     * @param semVerId Which version's index to query (must be supported).
     * @return Vector of DomainResult sorted by score descending.
     */
    std::vector<DomainResult> findBestDomainsV2(const SemanticVector& query,
                                                 size_t topK,
                                                 uint32_t semVerId) const;

    /**
     * @brief Updates or inserts a domain entry with multi-centroid data.
     *
     * @param entry The domain entry from a Semantic-LSA Data packet.
     */
    void updateDomain(const DomainEntry& entry);

    /**
     * @brief Gets domain entry by ID.
     * @return Optional domain entry if found.
     */
    std::optional<DomainEntry> getDomain(const ndn::Name& domainId) const;

    /**
     * @brief Updates EWMA success rate for a domain.
     * @param domainId The domain that completed (or failed) Stage-2 fetch.
     * @param success true if Data was received, false otherwise.
     * @param semVerId Which version's index to update.
     */
    void reportFetchOutcome(const ndn::Name& domainId, bool success, uint32_t semVerId);

    /**
     * @brief Gets the number of domains in the semantic index (current).
     */
    size_t domainCount() const;

    /**
     * @brief Gets the number of domains in the specified version's index.
     * @param semVerId Which version's index to query.
     */
    size_t domainCount(uint32_t semVerId) const;

    /**
     * @brief Updates the cost for a domain from FIB lookup.
     *
     * Call this with real hop count from FIB before scoring.
     * @param domainId The domain prefix.
     * @param cost The network cost (hop count or OSPF metric).
     * @param semVerId Which version's index to update.
     */
    void updateDomainCost(const ndn::Name& domainId, double cost, uint32_t semVerId);

    // ========================================================================
    // Dual-Version Indexing (Paper Section 4.5)
    // ========================================================================

    /**
     * @brief Check if a SemVerID is supported (curr or prev).
     */
    bool isSupportedSemVer(uint32_t semVerId) const;

    /**
     * @brief Switch to a new active version.
     *
     * Moves current index to prev, clears current for new LSAs.
     * @param newActiveId The new active SemVerID.
     */
    void switchToNewVersion(uint32_t newActiveId);

    /**
     * @brief Set the active SemVerID.
     */
    void setActiveSemVerId(uint32_t id);

    /**
     * @brief Set the previous (fallback) SemVerID.
     */
    void setPrevSemVerId(uint32_t id);

    /**
     * @brief Get the active SemVerID.
     */
    uint32_t getActiveSemVerId() const { return m_activeSemVerId; }

    /**
     * @brief Get the previous (fallback) SemVerID.
     */
    uint32_t getPrevSemVerId() const { return m_prevSemVerId; }

    // ========================================================================
    // Accessors
    // ========================================================================

    /**
     * @brief Returns the number of routes in the RIB.
     *
     * @return The current number of RIB entries.
     *
     * @note Thread-safe.
     *
     * @par Complexity: O(1).
     */
    size_t size() const;

    /**
     * @brief Checks if the RIB is empty.
     *
     * @return true if there are no routes, false otherwise.
     *
     * @note Thread-safe.
     *
     * @par Complexity: O(1).
     */
    bool empty() const;

    /**
     * @brief Gets the RIB convergence time (seconds).
     *
     * Returns the simulator time when the last NEW prefix was added to the RIB.
     * This represents the time at which the semantic RIB reached its final state.
     *
     * @return Convergence time in seconds (0 if no prefixes added).
     */
    double getConvergenceTime() const;

    /**
     * @brief Resets all metrics (convergence time).
     *
     * Call this at the start of a new experiment run.
     */
    void resetMetrics();


    
    /**
     * @brief Enable or disable EWMA penalty application in scoring.
     * When disabled, EWMA statistics are still updated but not applied to scores.
     * Default is true.
     */
    void
    SetEnableEwmaPenalty(bool enable);

    // ========================================================================
    // Eq.(2) Parameter Configuration
    // ========================================================================

    /**
     * @brief Set the alpha parameter (semantic similarity weight).
     */
    void setAlpha(double alpha) { m_alpha = alpha; }

    /**
     * @brief Set the beta parameter (cost penalty weight).
     */
    void setBeta(double beta) { m_beta = beta; }

    /**
     * @brief Set the lambda parameter (sigmoid steepness for gate).
     */
    void setLambda(double lambda) { m_lambda = lambda; }

    /**
     * @brief Set the weight clamp value (w_max).
     */
    void setWMax(double wMax) { m_wMax = wMax; }

    /**
     * @brief Set the minimum samples before EWMA penalty (n_min).
     */
    void setNMin(size_t nMin) { m_nMin = nMin; }

    /**
     * @brief Set the EWMA decay factor.
     */
    void setEwmaAlpha(double ewmaAlpha) { m_ewmaAlpha = ewmaAlpha; }

    /**
     * @brief Configure all Eq.(2) parameters at once.
     */
    void configureParams(double alpha, double beta, double lambda,
                         double wMax, size_t nMin, double ewmaAlpha) {
        m_alpha = alpha;
        m_beta = beta;
        m_lambda = lambda;
        m_wMax = wMax;
        m_nMin = nMin;
        m_ewmaAlpha = ewmaAlpha;
    }

    // Getters for current parameter values
    double getAlpha() const { return m_alpha; }
    double getBeta() const { return m_beta; }
    double getLambda() const { return m_lambda; }
    double getWMax() const { return m_wMax; }
    size_t getNMin() const { return m_nMin; }
    double getEwmaAlpha() const { return m_ewmaAlpha; }

    /**
     * @brief Gets a copy of all RIB entries (for debugging/logging).
     *
     * @return Vector containing copies of all RibEntry objects.
     *
     * @note Thread-safe. Returns a copy to ensure thread safety.
     *
     * @par Complexity: O(N).
     */
    std::vector<RibEntry> getAllEntries() const;

private:
    /**
     * @brief The expected vector dimension.
     */
    size_t m_vectorDim;

    /**
     * @brief The Routing Information Base storage.
     */
    std::vector<RibEntry> m_rib;

    /**
     * @brief Mutex for thread-safe access to m_rib.
     */
    mutable std::mutex m_mutex;

    /**
     * @brief Maximum cost seen so far (for normalization).
     *
     * Used to normalize costs to [0, 1] range in score calculation.
     */
    double m_maxCost = 0.0;

    /**
     * @brief Timestamp (seconds) of the last NEW prefix addition.
     *
     * Used to compute RIB convergence time for experiments.
     */
    double m_lastNewPrefixTime = 0.0;

    // ========================================================================
    // Domain Index (Paper-Compliant Multi-Centroid)
    // ========================================================================

    /**
     * @brief Current version domain index (V_curr).
     */
    std::map<std::string, DomainEntry> m_domainIndex;

    /**
     * @brief Previous version domain index (V_prev) for rollout compatibility.
     */
    std::map<std::string, DomainEntry> m_domainIndexPrev;

    /**
     * @brief Active SemVerID being used.
     */
    uint32_t m_activeSemVerId = 1;

    /**
     * @brief Previous (fallback) SemVerID for version transition.
     */
    uint32_t m_prevSemVerId = 0;

    /**
     * @brief Per-(OriginID, SemVerID) sequence number tracking.
     *
     * Key: "originId:semVerId", Value: last seen seqNo.
     * Ensures monotonic SeqNo check is per version.
     */
    std::map<std::string, uint64_t> m_lastSeqNo;

    /**
     * @brief Whether to apply EWMA penalty to scores.
     * Default: true.
     */
    bool m_enableEwmaPenalty = true;

    // ========================================================================
    // Configurable Scoring Parameters (Eq.2 from paper)
    // ========================================================================

    double m_alpha = params::kAlpha;       ///< Weight for semantic similarity
    double m_beta = params::kBeta;         ///< Weight for cost penalty
    double m_lambda = params::kLambda;     ///< Sigmoid steepness for gate
    double m_wMax = params::kWMax;         ///< Weight clamp value
    size_t m_nMin = params::kNMin;         ///< Minimum samples before EWMA penalty
    double m_ewmaAlpha = params::kEwmaAlpha; ///< EWMA decay factor

    /**
     * @brief Helper: Get reference to index for a semVerId.
     * @throws std::runtime_error if semVerId not supported.
     */
    std::map<std::string, DomainEntry>& getIndex(uint32_t semVerId);
    const std::map<std::string, DomainEntry>& getIndex(uint32_t semVerId) const;

    /**
     * @brief Helper: Compute gated score for one centroid.
     */
    double computeGatedScore(const SemanticVector& q, const CentroidEntry& c,
                             double costD, double costMax) const;
};

} // namespace iroute

#endif // IROUTE_MANAGER_HPP
