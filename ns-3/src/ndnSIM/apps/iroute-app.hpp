/**
 * @file iroute-app.hpp
 * @brief iRoute Routing Protocol Application for LSA broadcasting.
 *
 * This application periodically broadcasts Link State Advertisements (LSAs)
 * containing the local semantic vector. When receiving LSAs from neighbors,
 * it updates the RouteManager with the remote semantic centroids.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see Design_Guide.md Section 2.3 for design specifications
 */

#pragma once

#include "iroute-vector.hpp"
#include "iroute-manager.hpp"
#include "iroute-route-manager-registry.hpp"

#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/ndnSIM/apps/ndn-app.hpp"

#include "ns3/random-variable-stream.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

#include <set>

namespace ns3 {
namespace ndn {

/**
 * @class IRouteApp
 * @brief NDN Application for iRoute semantic routing protocol.
 *
 * IRouteApp implements the control plane of the iRoute protocol. Its main
 * responsibilities are:
 * - Publish Semantic-LSA as signed Data packets (paper-compliant)
 * - Respond to LSA fetch Interests from other routers
 * - Support multi-centroid advertisement per domain
 * - Fetch LSAs from known domains (ingress polling)
 *
 * @par Semantic-LSA Data Format (paper.tex Section 4.1)
 * @verbatim
 * Name: /<OriginID>/iroute/lsa/<SemVerID>/<SeqNo>  (prefix-first for routability)
 * Content: TLV-encoded fields:
 *   - OriginID: Topologically routable prefix
 *   - SemVerID: Embedding version identifier
 *   - SeqNo: Monotonic sequence number
 *   - Lifetime: Soft-state validity
 *   - Scope: Propagation scope
 *   - Centroids[]: List of (CentroidID, C_i, r_i, w_i)
 * Signature: Domain signature
 * @endverbatim
 *
 * @see RouteManager for RIB management
 */
class IRouteApp : public App {
public:
    /**
     * @brief Get the NS-3 TypeId for this class.
     *
     * @return The TypeId object for IRouteApp.
     */
    static TypeId GetTypeId();

    /**
     * @brief Default constructor.
     */
    IRouteApp();

    /**
     * @brief Destructor.
     */
    virtual ~IRouteApp();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Sets the local semantic centroids for this domain.
     *
     * @param centroids Vector of CentroidEntry objects.
     */
    void SetLocalCentroids(const std::vector<iroute::CentroidEntry>& centroids);

    // ========================================================================
    // Content Store (Document-Level Matching for Stage-2)
    // ========================================================================

    /**
     * @brief Entry representing a document in the content store.
     *
     * Loaded from producer_content.csv for document-level Stage-2 matching.
     */
    struct ContentEntry {
        std::string docId;              ///< Document identifier (e.g., "D12345")
        std::string canonicalName;      ///< Full canonical NDN name (e.g., "/domain0/data/doc/D12345")
        iroute::SemanticVector vector;  ///< Normalized semantic embedding
        bool isDistractor = false;      ///< True if this is a distractor document (not relevant to any query)
    };

    /**
     * @brief Sets the local content store for document-level matching.
     *
     * Used by HandleDiscoveryInterest to find the best matching document
     * and return a real canonical name (e.g., /domain0/data/doc/D12345).
     *
     * @param content Vector of ContentEntry objects for this domain.
     */
    void SetLocalContent(const std::vector<ContentEntry>& content);

    /**
     * @brief Gets the local content store.
     *
     * @return Const reference to the content store vector.
     */
    const std::vector<ContentEntry>& GetLocalContent() const;

    /**
     * @brief Trigger an immediate LSA publish (experiment helper).
     *
     * Exposed for experiments that need to align LSA updates with events.
     */
    void TriggerLsaPublish();

    /**
     * @brief Schedules a churn event to update local centroids.
     * 
     * @param eventTime Time delay until churn.
     * @param newCentroids New list of centroids.
     */
    void ScheduleChurn(Time eventTime, const std::vector<iroute::CentroidEntry>& newCentroids);

    /**
     * @brief Adds a single centroid to the local domain.
     *
     * @param centroid The centroid to add.
     */
    void AddCentroid(const iroute::CentroidEntry& centroid);

    /**
     * @brief Gets the local centroids.
     *
     * @return Const reference to the local centroids vector.
     */
    const std::vector<iroute::CentroidEntry>& GetLocalCentroids() const;

    /**
     * @brief Legacy: Sets the local semantic vector (single centroid).
     * @deprecated Use SetLocalCentroids() for multi-centroid support.
     */
    void SetLocalSemantics(const iroute::SemanticVector& vector);

    /**
     * @brief Legacy: Gets the local semantic vector.
     * @deprecated Use GetLocalCentroids() for multi-centroid support.
     */
    const iroute::SemanticVector& GetLocalSemantics() const;

    /**
     * @brief Sets the SemVerID for this domain.
     *
     * @param semVerId The embedding version identifier.
     */
    void SetSemVerId(uint32_t semVerId);

    /**
     * @brief Gets the SemVerID.
     */
    uint32_t GetSemVerId() const;

    /**
     * @brief Sets the router ID for this node.
     *
     * @param routerId A unique identifier for this router (e.g., "router1").
     */
    void SetRouterId(const std::string& routerId);

    /**
     * @brief Gets the router ID.
     *
     * @return The router ID string.
     */
    const std::string& GetRouterId() const;

    // ========================================================================
    // Global Metrics (static)
    // ========================================================================

    /**
     * @brief Gets the total number of LSA broadcasts across all IRouteApp instances.
     *
     * @return Total LSA broadcast count.
     */
    static uint32_t GetTotalLsaBroadcasts() { return s_lsaBroadcastCount; }

    /**
     * @brief Resets the LSA broadcast counter.
     *
     * Call at the start of a new experiment run.
     */
    static void ResetLsaCounter() { s_lsaBroadcastCount = 0; }

    // ========================================================================
    // LSA Polling (Ingress Control Plane)
    // ========================================================================

    /**
     * @brief Configures the list of known domains for LSA polling.
     *
     * Only used when m_isIngress=true. The ingress will periodically
     * fetch LSAs from these domains.
     *
     * @param domains Vector of domain name prefixes.
     */
    void SetKnownDomains(const std::vector<Name>& domains);

    /**
     * @brief Gets the count of LSA Data packets received.
     *
     * Used for verification in experiments.
     */
    uint32_t GetLsaRxCount() const { return m_lsaRxCount; }

    /**
     * @brief Gets the count of LSA fetch Interests sent.
     *
     * Used for verification in experiments.
     */
    uint32_t GetLsaFetchCount() const { return m_lsaFetchCount; }

    // ========================================================================
    // LSA Overhead Statistics
    // ========================================================================

    /**
     * @brief Gets the count of LSA Data packets published (Tx).
     */
    uint32_t GetLsaTxCount() const { return m_lsaTxCount; }

    /**
     * @brief Gets total bytes of LSA Data published (wire encoded, after signing).
     */
    uint64_t GetLsaTxBytesTotal() const { return m_lsaTxBytesTotal; }

    /**
     * @brief Gets maximum LSA Data wire size published.
     */
    uint32_t GetLsaTxBytesMax() const { return m_lsaTxBytesMax; }

    /**
     * @brief Gets average LSA Data wire size published.
     */
    double GetLsaTxBytesAvg() const {
        return m_lsaTxCount > 0 ? static_cast<double>(m_lsaTxBytesTotal) / m_lsaTxCount : 0.0;
    }

    /**
     * @brief Gets total bytes of LSA Data received (wire encoded).
     */
    uint64_t GetLsaRxBytesTotal() const { return m_lsaRxBytesTotal; }

protected:
    // ========================================================================
    // Application Lifecycle (from ns3::Application)
    // ========================================================================

    /**
     * @brief Called when the application starts.
     *
     * Initializes the face, registers the LSA prefix, and schedules
     * the first LSA broadcast.
     */
    virtual void StartApplication() override;

    /**
     * @brief Called when the application stops.
     *
     * Cancels all pending events and cleans up resources.
     */
    virtual void StopApplication() override;

    // ========================================================================
    // NDN Callbacks (from ndn::App)
    // ========================================================================

    /**
     * @brief Callback when an Interest is received.
     *
     * Handles incoming LSA Interests:
     * - Extracts the semantic vector from ApplicationParameters
     * - Updates the RouteManager with the new route
     * - Implements flood suppression
     *
     * @param interest The received Interest packet.
     */
    virtual void OnInterest(shared_ptr<const Interest> interest) override;

    /**
     * @brief Callback when Data is received.
     *
     * Currently not used in the LSA flooding protocol.
     *
     * @param data The received Data packet.
     */
    virtual void OnData(shared_ptr<const Data> data) override;

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Publishes the local semantic LSA as a signed Data packet.
     *
     * Creates a Data with:
     * - Name: /iroute/lsa/<OriginID>/<SemVerID>/<SeqNo>
     * - Content: TLV-encoded multi-centroid data
     *
     * Schedules the next publish after m_lsaInterval.
     */
    void PublishLsaData();

    /**
     * @brief Legacy: Broadcasts the local semantic vector as an LSA Interest.
     * @deprecated Use PublishLsaData() for paper-compliant implementation.
     */
    void BroadcastLsa();

    /**
     * @brief Schedules the next LSA broadcast.
     *
     * Adds jitter to prevent synchronization among nodes.
     */
    void ScheduleLsaBroadcast();

    /**
     * @brief Processes a received LSA Interest.
     *
     * Updates the RouteManager with the remote router's semantic centroid.
     * Uses two-stage routing: semantic stage only, no face ID needed.
     *
     * @param interest The LSA Interest to process.
     */
    void ProcessLsa(const Interest& interest);

    /**
     * @brief Responds to an LSA fetch Interest with signed LSA Data.
     *
     * This implements the pull-based LSA dissemination per NDN semantics.
     * Creates and sends a signed Data packet containing our LSA.
     *
     * @param interest The LSA fetch Interest to respond to.
     */
    void RespondWithLsaData(const Interest& interest);

    /**
     * @brief Handles /iroute/query Interest for ingress Stage-1 resolution.
     *
     * Only active when m_isIngress is true. Performs semantic ranking using
     * RouteManager::findBestDomainsV2(), returns DiscoveryReply Data with
     * the same name as the Interest (for proper PIT satisfaction).
     *
     * @param interest The query Interest with semantic vector in AppParams.
     */
    void HandleQueryInterest(const Interest& interest);

    /**
     * @brief Handles Discovery Interest from ingress nodes.
     *
     * Format: /<DomainID>/iroute/disc/<SemVerID>
     * Returns DiscoveryReply Data with the same name as the Interest.
     *
     * @param interest The discovery Interest with semantic vector in AppParams.
     * @param semVerId The requested SemVerID from the Interest name.
     */
    void HandleDiscoveryInterest(const Interest& interest, uint32_t semVerId);

    /**
     * @brief Checks if an LSA nonce has been seen before.
     *
     * @param nonce The Interest nonce to check.
     * @return true if the nonce was already seen, false otherwise.
     */
    bool HasSeenNonce(::ndn::Interest::Nonce nonce) const;

    /**
     * @brief Records an LSA nonce as seen.
     *
     * @param nonce The Interest nonce to record.
     */
    void RecordNonce(::ndn::Interest::Nonce nonce);

private:
    // ========================================================================
    // Configuration Parameters
    // ========================================================================

    /**
     * @brief The NS-3 node ID for this application.
     */
    uint32_t m_nodeId;

    /**
     * @brief The vector dimension for semantic vectors.
     *
     * Default: 384. Configurable via NS-3 attribute.
     */
    uint32_t m_vectorDim;

    /**
     * @brief The local semantic centroids for this domain.
     */
    std::vector<iroute::CentroidEntry> m_localCentroids;

    /**
     * @brief Local content store for document-level matching.
     *
     * Loaded from producer_content.csv. Used by HandleDiscoveryInterest
     * to find the best matching document via brute-force cosine similarity.
     */
    std::vector<ContentEntry> m_localContent;

    /**
     * @brief Legacy: The local semantic vector (single centroid).
     * @deprecated Use m_localCentroids for multi-centroid support.
     */
    iroute::SemanticVector m_localSemantics;

    /**
     * @brief Unique identifier for this router/domain.
     */
    std::string m_routerId;

    /**
     * @brief SemVerID for embedding version compatibility.
     */
    uint32_t m_semVerId;

    /**
     * @brief R2: Origin ID prefix for robust domain matching.
     *
     * Discovery Interests matching this prefix are handled by this domain.
     * Default: /<m_routerId>
     */
    Name m_originIdPrefix;

    /**
     * @brief Whether this node acts as ingress (semantic query resolver).
     *
     * When true, this node handles /iroute/query Interests and returns
     * DiscoveryReply Data after performing semantic ranking and probing.
     */
    bool m_isIngress;

    /**
     * @brief Allow LSA polling on non-ingress nodes when known domains are set.
     *
     * Default: false. When true, FetchDomainLsas() runs even if m_isIngress=false.
     */
    bool m_enableLsaPolling;

    /**
     * @brief P0-1: Enable v2 Data-based LSA publishing.
     * Default: true
     */
    bool m_enableV2LsaData;

    /**
     * @brief Previous centroids for hysteresis comparison.
     */
    std::vector<iroute::CentroidEntry> m_previousCentroids;

    /**
     * @brief Legacy: Previous centroid for hysteresis in BroadcastLsa().
     */
    iroute::SemanticVector m_previousCentroid;

    /**
     * @brief Interval between LSA publications.
     *
     * Default: 5 seconds. Configured via NS-3 attributes.
     */
    Time m_lsaInterval;

    /**
     * @brief Random jitter added to LSA intervals to prevent synchronization.
     *
     * Default: UniformRandom(0, 500ms).
     */
    Ptr<UniformRandomVariable> m_jitter;

    /**
     * @brief Base processing delay before discovery reply (microseconds).
     */
    uint32_t m_discReplyDelayUs = 0;

    /**
     * @brief Uniform per-Interest jitter (+/- microseconds) for discovery reply.
     */
    uint32_t m_discReplyJitterUs = 0;

    /**
     * @brief RNG for discovery reply jitter.
     */
    Ptr<UniformRandomVariable> m_discReplyDelayRv;

    /**
     * @brief The routing cost advertised by this node (hop count).
     */
    double m_routeCost;

    /**
     * @brief Hysteresis threshold for LSA broadcast suppression (δ=0.01).
     */
    double m_hysteresisThreshold;

    /**
     * @brief Score threshold for discovery match (τ, default 0.35).
     */
    double m_tau;

    // ========================================================================
    // Runtime State
    // ========================================================================

    /**
     * @brief Sequence number for LSA broadcasts.
     *
     * Incremented with each broadcast to ensure freshness.
     */
    uint64_t m_lsaSeqNum;

    /**
     * @brief Event ID for the next scheduled LSA broadcast.
     */
    EventId m_lsaBroadcastEvent;

    /**
     * @brief Set of seen LSA nonces for flood suppression.
     *
     * Prevents processing the same LSA multiple times.
     */
    std::set<::ndn::Interest::Nonce> m_seenNonces;

    /**
     * @brief Maximum number of nonces to remember.
     *
     * Older nonces are purged when this limit is exceeded.
     */
    static constexpr size_t kMaxSeenNonces = 10000;

    /**
     * @brief The LSA name prefix (new format: /iroute/lsa).
     */
    static const Name kLsaPrefixV2;

    /**
     * @brief Legacy LSA name prefix (/ndn/broadcast/iroute/lsa).
     * @deprecated
     */
    static const Name kLsaPrefix;

    /**
     * @brief Query prefix for ingress Stage-1 resolution (/iroute/query).
     */
    static const Name kQueryPrefix;

    /**
     * @brief Creates the LSA Data name for this domain.
     */
    Name createLsaName() const;

    /**
     * @brief Global counter for total LSA broadcasts (across all instances).
     */
    static uint32_t s_lsaBroadcastCount;

    /**
     * @brief KeyChain for signing LSA Data packets.
     *
     * Uses the default identity for cryptographic signatures.
     * Per paper Section 4.1: LSA Data packets must be signed.
     */
    ::ndn::KeyChain m_keyChain;

    // ========================================================================
    // LSA Polling (Ingress Only)
    // ========================================================================

    /**
     * @brief Known domain prefixes for LSA polling.
     */
    std::vector<Name> m_knownDomains;

    /**
     * @brief Per-domain sequence number tracking for incremental fetch.
     *
     * Key: domain name, Value: last received SeqNo.
     */
    std::map<std::string, uint64_t> m_lastSeqByDomain;

    /**
     * @brief Interval between LSA fetch rounds (ingress only).
     */
    Time m_lsaFetchInterval;

    /**
     * @brief Event ID for the next scheduled LSA fetch.
     */
    EventId m_lsaFetchEvent;

    /**
     * @brief Counter for received LSA Data packets (verification).
     */
    uint32_t m_lsaRxCount = 0;

    /**
     * @brief Counter for sent LSA fetch Interests (verification).
     */
    uint32_t m_lsaFetchCount = 0;

    // ========================================================================
    // LSA Overhead Statistics
    // ========================================================================

    /**
     * @brief Counter for published LSA Data packets.
     */
    uint32_t m_lsaTxCount = 0;

    /**
     * @brief Total bytes of published LSA Data (wire encoded).
     */
    uint64_t m_lsaTxBytesTotal = 0;

    /**
     * @brief Maximum LSA Data wire size published.
     */
    uint32_t m_lsaTxBytesMax = 0;

    /**
     * @brief Total bytes of received LSA Data (wire encoded).
     */
    uint64_t m_lsaRxBytesTotal = 0;

    /**
     * @brief Fetches LSAs from all known domains.
     *
     * Sends Interest for /<domain>/iroute/lsa/<semVerId>/<lastSeq+1>
     * for each known domain. Schedules next fetch after m_lsaFetchInterval.
     */
    void FetchDomainLsas();
};

} // namespace ndn
} // namespace ns3
