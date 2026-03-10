/**
 * @file iroute-discovery-consumer.hpp
 * @brief Two-stage discovery consumer for iRoute protocol.
 *
 * Implements the paper's two-stage workflow:
 * - Stage-1: Send Discovery Interests to candidate domains
 * - Stage-2: Fetch content using CanonicalName from Discovery Reply
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include "iroute-vector.hpp"
#include "iroute-manager.hpp"

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

#include <vector>
#include <map>
#include <unordered_map>
#include <fstream>

namespace ns3 {
namespace ndn {

// =============================================================================
// Transaction Record for Real Protocol Metrics
// =============================================================================

/**
 * @struct TxRecord
 * @brief Per-query transaction record for real protocol measurements.
 *
 * Captures actual timestamps from NDN Interest/Data exchanges, not simulated values.
 * PATCH 4B: Uses ns3::Time for precise timing instead of integer milliseconds.
 */
struct TxRecord {
    uint64_t queryId = 0;
    double startTime = 0.0;        // When StartDiscovery() called (seconds)
    
    // PATCH 4B: Use ns3::Time for precise timestamps
    ns3::Time tStage1Send = ns3::Seconds(0);   // When discovery Interest sent
    ns3::Time tStage1Recv = ns3::Seconds(0);   // When discovery Data received
    ns3::Time tStage2Send = ns3::Seconds(0);   // When fetch Interest sent
    ns3::Time tStage2Recv = ns3::Seconds(0);   // When fetch Data received
    
    // Legacy double fields kept for backward compatibility (computed from Time)
    double stage1SendTime = 0.0;   // When discovery Interest sent
    double stage1RecvTime = 0.0;   // When discovery Data received
    double stage2SendTime = 0.0;   // When fetch Interest sent
    double stage2RecvTime = 0.0;   // When fetch Data received
    
    // Legacy ms fields - these are now computed from Time differences
    double stage1SendTimeMs = 0.0;
    double stage1RecvTimeMs = 0.0;
    double stage2SendTimeMs = 0.0;
    double stage2RecvTimeMs = 0.0;

    std::string expectedDomain;    // Ground truth
    std::string selectedDomain;    // Actual domain from discovery reply (legacy field, kept)
    std::string requestedName;     // PR-2: The actual full name requested (Stage 2)
    std::string targetName;        // PATCH 4C: Ground-truth canonical name from trace (deprecated, use targetDocIds)
    std::vector<std::string> targetDocIds;  // Ground-truth relevant doc IDs from qrels (e.g., {"D12345", "D67890"})
    std::vector<std::string> targetDomains; // Ground-truth domains containing relevant docs (e.g., {"/domain0", "/domain2"})
    
    // New PR-B Fields
    std::string firstChoiceDomain;   // The top ranked domain (before probing)
    std::string finalSuccessDomain;  // The domain that actually served the content (Stage-2 success)
    uint32_t discoveryAttempts = 0;
    std::string attemptedDomains;    // Separated by '|'
    
    std::string topKList;            // Top-K domains considered (e.g. "/d1=0.9;/d2=0.8")
    
    uint32_t semVerId = 0;
    uint32_t probesUsed = 0;
    
    double stage1RttMs = -1.0;     // -1.0 = not measured
    double stage2RttMs = -1.0;     // -1.0 = not measured
    double totalMs = -1.0;         // -1.0 = not measured
    
    bool stage1Success = false;
    bool stage2Success = false;
    double confidence = 0.0;
    std::string failureReason;     // Empty = success, otherwise describes failure
    
    bool stage2FromCache = false;
    int32_t stage2HopCount = -1;   // -1 = unknown
    
    uint32_t kMaxEffective = 0;
    double tauEffective = 0.0;
    uint32_t fetchTimeoutMsEffective = 0;
    
    bool fallbackUsed = false;
    uint32_t numStage1Attempts = 0;
    bool usedPrevIndex = false;
    
    // =========================================================================
    // Fine-grained overhead statistics (bytes)
    // =========================================================================
    size_t stage1InterestBytes = 0;   ///< Discovery Interest wire size
    size_t stage1DataBytes = 0;       ///< Discovery Reply Data wire size
    size_t stage2InterestBytes = 0;   ///< Fetch Interest wire size
    size_t stage2DataBytes = 0;       ///< Fetch Data wire size (content)
    size_t totalControlBytes = 0;     ///< stage1InterestBytes + stage1DataBytes
    size_t totalDataBytes = 0;        ///< stage2InterestBytes + stage2DataBytes
    
    std::string toCsvLine() const {
        std::ostringstream oss;
        oss << queryId << "," << startTime << ","
            << expectedDomain << "," << selectedDomain << "," << requestedName << ","
            << firstChoiceDomain << "," << finalSuccessDomain << ","
            << (expectedDomain == finalSuccessDomain ? 1 : 0) << ","  // correct (using finalSuccess)
            << probesUsed << "," << discoveryAttempts << ","
            << stage1Success << "," << stage2Success << ","
            << stage1RttMs << "," << stage2RttMs << "," << totalMs << ","
            << confidence << "," << failureReason << ","
            << stage2FromCache << "," << stage2HopCount << ","
            << kMaxEffective << "," << tauEffective << "," << fetchTimeoutMsEffective << ","
            << fallbackUsed << "," << attemptedDomains << ","
            << topKList << ","
            << stage1InterestBytes << "," << stage1DataBytes << ","
            << stage2InterestBytes << "," << stage2DataBytes << ","
            << totalControlBytes << "," << totalDataBytes;
        return oss.str();
    }
    
    static std::string csvHeader() {
        return "queryId,startTime,expectedDomain,selectedDomain,requestedName,"
               "firstChoiceDomain,finalSuccessDomain,correct,"
               "probesUsed,discoveryAttempts,stage1Success,stage2Success,"
               "stage1RttMs,stage2RttMs,totalMs,confidence,failureReason,"
               "stage2FromCache,stage2HopCount,"
               "kMaxEffective,tauEffective,fetchTimeoutMsEffective,"
               "fallbackUsed,attemptedDomains,topKList,"
               "stage1InterestBytes,stage1DataBytes,"
               "stage2InterestBytes,stage2DataBytes,"
               "totalControlBytes,totalDataBytes";
    }
};

/**
 * @class IRouteDiscoveryConsumer
 * @brief Consumer app implementing two-stage discovery + fetch workflow.
 *
 * Per paper Section 4.2-4.3:
 * - Stage-1: Query local RouteManager, send Discovery Interests to top-K domains
 * - Stage-2: Parse Discovery Reply, fetch content via CanonicalName
 *
 * Discovery Interest name: /<DomainID>/iroute/disc/<SemVerID>/<nonce>
 * ApplicationParameters: SemanticVector TLV
 */
class IRouteDiscoveryConsumer : public App
{
public:
    static TypeId GetTypeId();

    IRouteDiscoveryConsumer();
    virtual ~IRouteDiscoveryConsumer();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Sets the semantic query vector.
     */
    void SetQueryVector(const iroute::SemanticVector& vector);

    /**
     * @brief Gets the current query vector.
     */
    const iroute::SemanticVector& GetQueryVector() const { return m_queryVector; }

    /**
     * @brief Sets the SemVerID for discovery requests.
     */
    void SetSemVerId(uint32_t semVerId) { m_semVerId = semVerId; }

    // ========================================================================
    // Statistics
    // ========================================================================

    uint32_t GetDiscoveryAttempts() const { return m_discoveryAttempts; }
    uint32_t GetDiscoveryFound() const { return m_discoveryFound; }
    uint32_t GetDiscoveryNotFound() const { return m_discoveryNotFound; }
    uint32_t GetStage2Success() const { return m_stage2Success; }
    uint32_t GetStage2Failure() const { return m_stage2Failure; }

    /**
     * @brief Print statistics at end of simulation.
     */
    void PrintStats() const;

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnData(shared_ptr<const Data> data) override;
    virtual void OnNack(shared_ptr<const lp::Nack> nack) override;

private:
    // ========================================================================
    // Stage-1: Discovery
    // ========================================================================

    /**
     * @brief Initiates Stage-1 discovery process.
     */
    void StartDiscovery();
    
    /**
     * @brief Helper to perform discovery for a specific SemVer.
     */
    void DoDiscoveryAttempt(uint32_t semVerId, uint64_t queryId);

    /**
     * @brief Sends Discovery Interest to a candidate domain.
     *
     * @param domainId Target domain prefix.
     * @param semVerId SemVerID for this request.
     */
    void SendDiscoveryInterest(const Name& domainId, uint32_t semVerId, uint64_t queryId, const iroute::SemanticVector& vec);

    /**
     * @brief Handles Discovery Reply Data.
     *
     * Parses TLV: {status, canonicalName, confidence, [manifestDigest]}
     * If Found: starts Stage-2 fetch.
     */
    void HandleDiscoveryReply(const Data& data);

    /**
     * @brief Handles Discovery Interest timeout (Stage 1).
     * @param discNameUri The full discovery Interest name (with ParamDigest) as URI string.
     */
    void HandleDiscoveryTimeout(const std::string& discNameUri);

    // ========================================================================
    // Stage-2: Fetch
    // ========================================================================

    /**
     * @brief Sends fetch Interest for canonical content.
     *
     * @param canonicalName The resolved content name from Discovery Reply.
     * @param domainId Origin domain for EWMA tracking.
     * @param semVerId Which version for EWMA tracking.
     */
    void SendFetchInterest(const Name& canonicalName, const Name& domainId, uint32_t semVerId, uint64_t queryId);

    /**
     * @brief Handles Stage-2 Data.
     */
    void HandleFetchData(const Data& data);

    /**
     * @brief Handles Stage-2 fetch timeout.
     * @param canonicalName The fetch Interest name that timed out.
     */
    void HandleFetchTimeout(const Name& canonicalName);

    // ========================================================================
    // EWMA Success Tracking
    // ========================================================================

    /**
     * @brief Reports fetch outcome for EWMA update.
     * @param domainId Origin domain.
     * @param success Whether fetch succeeded.
     * @param semVerId Which version was used.
     */
    void ReportFetchOutcome(const Name& domainId, bool success, uint32_t semVerId);

    // ========================================================================
    // Query Trace & Transaction Logging (Real Metrics)
    // ========================================================================
public:
    /**
     * @brief Query item with vector and expected domain (ground truth).
     * PATCH 4C: Added targetDocIds for doc-level ground truth (supports multiple relevant docs).
     * Added targetDomains for domain-level ground truth (domains containing relevant docs).
     */
    struct QueryItem {
        uint32_t id = 0;
        iroute::SemanticVector vector;
        std::string queryText;  // Original query text (for logging)
        std::string expectedDomain;
        std::string targetName;  // Single target (deprecated, use targetDocIds)
        std::vector<std::string> targetDocIds;  // Ground-truth relevant doc IDs from qrels (semicolon-separated in CSV)
        std::vector<std::string> targetDomains; // Ground-truth domains containing relevant docs (semicolon-separated in CSV)
        uint32_t objectId = 0; // PR-2: Unique object ID for this query (prevents cache masking)
    };

    /**
     * @brief Sets the query trace for batch experiments.
     * @param queries Vector of (query vector, expected domain) pairs.
     */
    void SetQueryTrace(const std::vector<QueryItem>& queries);

    /**
     * @brief Gets all transaction records after simulation.
     */
    const std::vector<TxRecord>& GetTransactions() const { return m_transactions; }

    /**
     * @brief Exports transaction records to CSV file.
     * @param filename Output CSV filename.
     */
    void ExportToCsv(const std::string& filename) const;

private:
    // Configuration
    uint32_t m_nodeId;
    uint32_t m_vectorDim;
    uint32_t m_semVerId;
    iroute::SemanticVector m_queryVector;
    double m_frequency;
    Time m_interestLifetime;
    
    // Configurable protocol parameters (set via ns-3 Attributes)
    // Note: m_kMax is used as both TopK retrieval size and probing cap
    uint32_t m_kMax;              ///< Maximum probing candidates (bounded probing)
    double m_tau;                 ///< Ingress-side score threshold for triggering probing
    uint32_t m_fetchTimeoutMs;    ///< Stage-2 fetch timeout in milliseconds
    double m_exploreRate = 0.0;   ///< Probability of adding one exploratory probe
    uint32_t m_exploreExtraK = 1; ///< Extra candidates to fetch when exploring
    Ptr<UniformRandomVariable> m_exploreRng;
    
    // Eq.(2) gated scoring parameters (passed to RouteManager on start)
    double m_alpha;               ///< Weight for semantic similarity
    double m_beta;                ///< Weight for cost penalty
    double m_lambda;              ///< Sigmoid steepness for gate
    uint32_t m_nMin;              ///< Minimum samples before EWMA penalty
    double m_ewmaAlpha;           ///< EWMA decay factor
    
    // Fallback / Retry State
    uint32_t m_attemptSemVer = 0;   // Current semver being probed
    uint32_t m_pendingProbes = 0;   // Number of outstanding probes for current attempt
    bool m_foundCandidate = false;  // Whether any candidate in current attempt was successful
    
    // =========================================================================
    // "Best Candidate" Strategy (PATCH: kMax effectiveness fix)
    // Instead of immediately using the first "found=true" reply,
    // collect ALL replies and pick the one with highest confidence.
    // =========================================================================
    struct DiscoveryCandidate {
        Name domainId;
        Name canonicalName;
        double confidence;
        uint32_t semVerId;
        Time recvTime;
        size_t dataBytes;
    };
    std::vector<DiscoveryCandidate> m_discoveryCandidates;  // Collected successful replies
    uint64_t m_currentQueryId = 0;  // Query ID for current discovery round
    
    /**
     * @brief Called when all pending probes complete, selects best candidate.
     */
    void FinalizeDiscoveryRound(uint64_t queryId);
    
    // Pending fetch tracking: fetchName -> (domainId, semVerId)
    struct PendingFetchInfo {
        Name domainId;
        uint32_t semVerId;
        uint64_t queryId;  // Link back to TxRecord
    };
    std::map<Name, PendingFetchInfo> m_pendingFetch;
    
    // Timeout tracking for stage-2 fetch
    std::map<Name, EventId> m_fetchTimeouts;
    
    // =========================================================================
    // REFACTORED: Use full Interest name (with ParamDigest) as key
    // Per paper: Discovery Interest = /<DomainID>/iroute/disc/<SemVerID>/<ParamDigest>
    // =========================================================================
    // Timeout tracking for stage-1 discovery (fullInterestName -> event)
    std::map<std::string, EventId> m_discoveryTimeoutsByName;
    // Mapping fullInterestName -> queryId for Stage 1
    std::map<std::string, uint64_t> m_discNameToQueryId;
    // Mapping fullInterestName -> domainId for discovery
    std::map<std::string, Name> m_discNameToDomain;

    // Scheduling
    EventId m_sendEvent;

    // Statistics
    uint32_t m_discoveryAttempts;
    uint32_t m_discoveryFound;
    uint32_t m_discoveryNotFound;
    uint32_t m_stage2Success;
    uint32_t m_stage2Failure;

    // ==========================================================================
    // Transaction Logging (Real Protocol Metrics)
    // ==========================================================================
    std::vector<QueryItem> m_queryTrace;           // Input query trace
    std::unordered_map<uint64_t, size_t> m_queryPosById; // queryId -> position in trace
    size_t m_queryIndex = 0;                       // Current position in trace
    std::vector<TxRecord> m_transactions;          // Output transaction records
    std::map<uint64_t, TxRecord> m_activeTxs;      // In-flight transactions (queryId -> TxRecord)

    // Constants for Discovery Interest naming
    static const Name kDiscoveryPrefix;
};

} // namespace ndn
} // namespace ns3
