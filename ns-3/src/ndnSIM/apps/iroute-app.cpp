/**
 * @file iroute-app.cpp
 * @brief Implementation of the IRouteApp routing protocol application.
 *
 * This file implements the LSA broadcasting and processing logic for
 * the iRoute semantic routing protocol.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see iroute-app.hpp for class declaration
 * @see Design_Guide.md Section 2.3 for design specifications
 */

#include "iroute-app.hpp"
#include "iroute-tlv.hpp"

#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"

#include <ndn-cxx/encoding/block-helpers.hpp>
#include <cmath>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.IRouteApp");

NS_OBJECT_ENSURE_REGISTERED(IRouteApp);

// Static member initialization
// New paper-compliant LSA prefix: /iroute/lsa
const Name IRouteApp::kLsaPrefixV2("/iroute/lsa");

// Legacy LSA prefix (for Interest-based broadcast)
const Name IRouteApp::kLsaPrefix("/ndn/broadcast/iroute/lsa");

// Global LSA broadcast counter (across all IRouteApp instances)
uint32_t IRouteApp::s_lsaBroadcastCount = 0;

// Query prefix for ingress Stage-1 resolution
const Name IRouteApp::kQueryPrefix("/iroute/query");

// =============================================================================
// NS-3 TypeId Registration
// =============================================================================

TypeId
IRouteApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::IRouteApp")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<IRouteApp>()
        .AddAttribute("RouterId",
                      "Unique identifier for this router",
                      StringValue("router0"),
                      MakeStringAccessor(&IRouteApp::SetRouterId, &IRouteApp::GetRouterId),
                      MakeStringChecker())
        .AddAttribute("LsaInterval",
                      "Interval between LSA broadcasts",
                      TimeValue(Seconds(5.0)),
                      MakeTimeAccessor(&IRouteApp::m_lsaInterval),
                      MakeTimeChecker())
        .AddAttribute("RouteCost",
                      "Routing cost advertised by this node",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&IRouteApp::m_routeCost),
                      MakeDoubleChecker<double>(0.0))
        .AddAttribute("HysteresisThreshold",
                      "Threshold for centroid drift to trigger LSA broadcast (0=disabled)",
                      DoubleValue(0.0),
                      MakeDoubleAccessor(&IRouteApp::m_hysteresisThreshold),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("VectorDim",
                      "Dimension of semantic vectors (default: 384)",
                      UintegerValue(384),
                      MakeUintegerAccessor(&IRouteApp::m_vectorDim),
                      MakeUintegerChecker<uint32_t>(1))
        .AddAttribute("IsIngress",
                      "Whether this node acts as ingress (semantic query resolver)",
                      BooleanValue(false),
                      MakeBooleanAccessor(&IRouteApp::m_isIngress),
                      MakeBooleanChecker())
        .AddAttribute("EnableLsaPolling",
                      "Allow non-ingress nodes to poll LSAs when known domains are set",
                      BooleanValue(false),
                      MakeBooleanAccessor(&IRouteApp::m_enableLsaPolling),
                      MakeBooleanChecker())
        .AddAttribute("SemVerId",
                      "Semantic embedding version identifier",
                      UintegerValue(1),
                      MakeUintegerAccessor(&IRouteApp::SetSemVerId, &IRouteApp::GetSemVerId),
                      MakeUintegerChecker<uint32_t>(1))
        .AddAttribute("EnableV2LsaData",
                      "Use v2 Data-based LSA publishing instead of legacy Interest broadcast",
                      BooleanValue(true),
                      MakeBooleanAccessor(&IRouteApp::m_enableV2LsaData),
                      MakeBooleanChecker())
        .AddAttribute("LsaFetchInterval",
                      "Interval for polling domain LSAs (ingress only)",
                      TimeValue(Seconds(2.0)),
                      MakeTimeAccessor(&IRouteApp::m_lsaFetchInterval),
                      MakeTimeChecker())
        .AddAttribute("ScoreThresholdTau",
                      "Score threshold for discovery match (τ)",
                      DoubleValue(0.35),
                      MakeDoubleAccessor(&IRouteApp::m_tau),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("DiscReplyDelayUs",
                      "Base processing delay before discovery reply Data (microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&IRouteApp::m_discReplyDelayUs),
                      MakeUintegerChecker<uint32_t>())
        .AddAttribute("DiscReplyJitterUs",
                      "Uniform discovery reply jitter (+/- microseconds)",
                      UintegerValue(0),
                      MakeUintegerAccessor(&IRouteApp::m_discReplyJitterUs),
                      MakeUintegerChecker<uint32_t>());

    return tid;
}

// =============================================================================
// Constructors / Destructor
// =============================================================================

IRouteApp::IRouteApp()
    : m_nodeId(0)
    , m_vectorDim(384)
    , m_routerId("router0")
    , m_semVerId(1)
    , m_isIngress(false)
    , m_enableLsaPolling(false)
    , m_enableV2LsaData(true)
    , m_lsaInterval(Seconds(5.0))
    , m_routeCost(1.0)
    , m_hysteresisThreshold(iroute::params::kDelta)  // Paper default
    , m_tau(iroute::params::kTau)  // Paper default 0.35
    , m_lsaSeqNum(0)
{
    NS_LOG_FUNCTION(this);

    // Initialize random jitter generator (0 to 500ms)
    m_jitter = CreateObject<UniformRandomVariable>();
    m_jitter->SetAttribute("Min", DoubleValue(0.0));
    m_jitter->SetAttribute("Max", DoubleValue(0.5));
    m_discReplyDelayRv = CreateObject<UniformRandomVariable>();
}

IRouteApp::~IRouteApp()
{
    NS_LOG_FUNCTION(this);
}

// =============================================================================
// Configuration Methods
// =============================================================================

void
IRouteApp::SetLocalSemantics(const iroute::SemanticVector& vector)
{
    NS_LOG_FUNCTION(this << vector.getDimension());
    m_localSemantics = vector;
}

const iroute::SemanticVector&
IRouteApp::GetLocalSemantics() const
{
    return m_localSemantics;
}

void
IRouteApp::SetRouterId(const std::string& routerId)
{
    NS_LOG_FUNCTION(this << routerId);
    m_routerId = routerId;
}

const std::string&
IRouteApp::GetRouterId() const
{
    return m_routerId;
}

void
IRouteApp::SetLocalCentroids(const std::vector<iroute::CentroidEntry>& centroids)
{
    NS_LOG_FUNCTION(this << centroids.size());
    m_localCentroids = centroids;
}

void
IRouteApp::AddCentroid(const iroute::CentroidEntry& centroid)
{
    NS_LOG_FUNCTION(this << centroid.centroidId);
    m_localCentroids.push_back(centroid);
}

const std::vector<iroute::CentroidEntry>&
IRouteApp::GetLocalCentroids() const
{
    return m_localCentroids;
}

void
IRouteApp::SetLocalContent(const std::vector<ContentEntry>& content)
{
    NS_LOG_FUNCTION(this << content.size());
    m_localContent = content;
    NS_LOG_INFO("SetLocalContent: loaded " << content.size() << " documents for domain " << m_routerId);
}

const std::vector<IRouteApp::ContentEntry>&
IRouteApp::GetLocalContent() const
{
    return m_localContent;
}

void
IRouteApp::TriggerLsaPublish()
{
    PublishLsaData();
}

void
IRouteApp::ScheduleChurn(Time eventTime, const std::vector<iroute::CentroidEntry>& newCentroids)
{
    // Capture by value to ensure data lifetime
    Simulator::Schedule(eventTime, [this, newCentroids]() {
        NS_LOG_INFO("Churn event! Updating centroids for " << m_routerId 
                    << " from " << m_localCentroids.size() << " to " << newCentroids.size());
        
        m_localCentroids = newCentroids;
        
        // Force immediate LSA broadcast
        TriggerLsaPublish();
    });
}

void
IRouteApp::SetSemVerId(uint32_t semVerId)
{
    NS_LOG_FUNCTION(this << semVerId);
    m_semVerId = semVerId;
}

uint32_t
IRouteApp::GetSemVerId() const
{
    return m_semVerId;
}

Name
IRouteApp::createLsaName() const
{
    // Prefix-first naming for natural routability:
    // /<OriginID>/iroute/lsa/<SemVerID>/<SeqNo>
    // This allows LSA Interests to be routed using the domain's existing FIB entries.
    return Name(m_originIdPrefix)
        .append("iroute").append("lsa")
        .appendNumber(m_semVerId)
        .appendNumber(m_lsaSeqNum);
}

// =============================================================================
// Application Lifecycle
// =============================================================================

void
IRouteApp::StartApplication()
{
    NS_LOG_FUNCTION(this);

    // Call parent class StartApplication to initialize m_face
    App::StartApplication();

    // Get the node ID and ensure we have a RouteManager for this node
    m_nodeId = GetNode()->GetId();
    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
    
    // Configure RouteManager with this node's SemVerID
    rm->setActiveSemVerId(m_semVerId);

    // R2: Initialize origin ID prefix for domain matching
    // If not explicitly configured, derive from routerId
    if (m_originIdPrefix.empty()) {
        m_originIdPrefix = Name("/");
        m_originIdPrefix.append(m_routerId);
    }
    NS_LOG_DEBUG("OriginIdPrefix set to: " << m_originIdPrefix);

    // Register the LSA prefix to receive LSA Interests (legacy)
    FibHelper::AddRoute(GetNode(), kLsaPrefix, m_face, 0);
    
    // Register v2 LSA prefix for pull-based dissemination
    FibHelper::AddRoute(GetNode(), kLsaPrefixV2, m_face, 0);

    // P1-3: If ingress node, register query prefix for Stage-1 resolution
    if (m_isIngress) {
        FibHelper::AddRoute(GetNode(), kQueryPrefix, m_face, 0);
        NS_LOG_INFO("Ingress node: registered " << kQueryPrefix);
    }
    
    // Register domain's own prefix to receive discovery interests
    // Discovery Interest format: /<DomainID>/iroute/disc/<SemVerID>
    if (!m_isIngress && !m_originIdPrefix.empty()) {
        FibHelper::AddRoute(GetNode(), m_originIdPrefix, m_face, 0);
        NS_LOG_INFO("Domain node: registered prefix " << m_originIdPrefix << " for discovery");
    }

    NS_LOG_INFO("IRouteApp started on node " << GetNode()->GetId()
                << ", routerId=" << m_routerId
                << ", vectorDim=" << m_vectorDim
                << ", isIngress=" << m_isIngress
                << ", lsaInterval=" << m_lsaInterval.GetSeconds() << "s");

    // Schedule first LSA broadcast with initial jitter
    ScheduleLsaBroadcast();

    // Ensure LSA polling starts when the app becomes active.
    if ((m_isIngress || m_enableLsaPolling) && !m_knownDomains.empty()
        && !m_lsaFetchEvent.IsRunning()) {
        Time delay = MilliSeconds(100 + m_jitter->GetValue() * 100);
        m_lsaFetchEvent = Simulator::Schedule(delay, &IRouteApp::FetchDomainLsas, this);
    }
}

void
IRouteApp::StopApplication()
{
    NS_LOG_FUNCTION(this);

    // Cancel pending LSA broadcast
    Simulator::Cancel(m_lsaBroadcastEvent);
    
    // Cancel pending LSA fetch (ingress)
    Simulator::Cancel(m_lsaFetchEvent);

    // Clear seen nonces
    m_seenNonces.clear();

    // Call parent class StopApplication
    App::StopApplication();

    NS_LOG_INFO("IRouteApp stopped on node " << GetNode()->GetId()
                << ", lsaRxCount=" << m_lsaRxCount);
}

// =============================================================================
// NDN Callbacks
// =============================================================================

// Helper for v2 notification name
// /ndn/broadcast/iroute/v2/notify/<RouterID>/<SemVerID>/<SeqNo>
static const Name kLsaNotifyPrefix("/ndn/broadcast/iroute/v2/notify");

void
IRouteApp::OnInterest(shared_ptr<const Interest> interest)
{
    NS_LOG_FUNCTION(this << interest->getName());

    // Call parent handler for tracing
    App::OnInterest(interest);

    const Name& name = interest->getName();

    // P0-2: Ingress query service - handle /iroute/query Interests
    if (m_isIngress && kQueryPrefix.isPrefixOf(name)) {
        NS_LOG_INFO("Ingress: Received query Interest: " << name);
        HandleQueryInterest(*interest);
        return;
    }

    // Task 2: Handle Discovery Interests from other domains
    // Format: /<DomainID>/iroute/disc/<SemVerID>
    // Check if this is a Discovery Interest for OUR domain
    // Look for pattern: .../iroute/disc/<number>
    bool isDiscoveryForUs = false;
    uint32_t discoverySemVerId = 0;
    
    for (size_t i = 0; i + 2 < name.size(); ++i) {
        if (name.get(i).toUri() == "iroute" && 
            name.get(i+1).toUri() == "disc") {
            // R2 FIX: Extract DomainID prefix and check against m_originIdPrefix
            Name domainPrefix = name.getPrefix(i);
            
            // Use strict prefix matching instead of fragile string find
            if (m_originIdPrefix.isPrefixOf(domainPrefix) || 
                domainPrefix == m_originIdPrefix) {
                isDiscoveryForUs = true;
                
                // R3: Robust SemVerID parsing with fallback
                try {
                    discoverySemVerId = static_cast<uint32_t>(name.get(i+2).toNumber());
                } catch (...) {
                    // Fallback: try parsing as hex or use hash
                    std::string semVerStr = name.get(i+2).toUri();
                    discoverySemVerId = static_cast<uint32_t>(std::hash<std::string>{}(semVerStr));
                    NS_LOG_DEBUG("SemVerID parsed via hash: " << semVerStr << " -> " << discoverySemVerId);
                }
                break;
            }
        }
    }
    
    if (isDiscoveryForUs) {
        NS_LOG_INFO("Discovery Interest for our domain: " << name);
        HandleDiscoveryInterest(*interest, discoverySemVerId);
        return;
    }

    // Check if this is an LSA Interest (prefix-first format):
    // /<OriginID>/iroute/lsa/<SemVerID>/<SeqNo>
    // Look for "iroute" followed by "lsa" pattern in name
    int lsaIroutePos = -1;
    for (size_t i = 0; i + 1 < name.size(); ++i) {
        if (name.get(i).toUri() == "iroute" && name.get(i + 1).toUri() == "lsa") {
            lsaIroutePos = static_cast<int>(i);
            break;
        }
    }
    
    if (lsaIroutePos >= 0) {
        // Extract OriginID (components 0 to lsaIroutePos-1)
        Name requestedOrigin;
        for (int i = 0; i < lsaIroutePos; ++i) {
            requestedOrigin.append(name.get(i));
        }
        
        // Check if this LSA Interest is for our domain
        if (requestedOrigin == m_originIdPrefix ||
            m_originIdPrefix.isPrefixOf(requestedOrigin)) {
            
            NS_LOG_INFO("LSA Interest for our domain: " << name);
            RespondWithLsaData(*interest);
            return;
        }
        
        // Not for us - forward or ignore
        NS_LOG_DEBUG("LSA Interest for other domain, ignoring: " << name);
        return;
    }

    // Check for v2 LSA Notification
    // /ndn/broadcast/iroute/v2/notify/<RouterID>/<SemVerID>/<SeqNo>
    if (kLsaNotifyPrefix.isPrefixOf(name)) {
        // Parse info and fetch LSA if not seen
        // Check size: prefix(4) + routerId + semVer + seq = 7 components
        if (name.size() >= 7) {
            std::string routerId = name.get(4).toUri();
            uint32_t semVerId = static_cast<uint32_t>(name.get(5).toNumber());
            uint64_t seqNo = name.get(6).toNumber();
            
            if (routerId == m_routerId) return; // Ignore own

            // Construct Data name to check nonce/duplication
            // /iroute/lsa/<RouterID>/<SemVerID>/<SeqNo>
            Name lsaName(kLsaPrefixV2);
            lsaName.append(routerId).appendNumber(semVerId).appendNumber(seqNo);
            
            // Use SeqNo as pseudo-nonce for suppression?
            // Or just check if we have it. 
            // For now, simple fetch.
            
            NS_LOG_INFO("Received LSA Notification from " << routerId << ", seq=" << seqNo);
            
            // Send Fetch Interest
            auto interest = std::make_shared<Interest>(lsaName);
            interest->setCanBePrefix(false);
            interest->setMustBeFresh(true);
            m_transmittedInterests(interest, this, m_face);
            m_appLink->onReceiveInterest(*interest);
        }
        return;
    }

    // Legacy LSA prefix check
    if (!kLsaPrefix.isPrefixOf(name)) {
        NS_LOG_DEBUG("Ignoring non-LSA Interest: " << name);
        return;
    }

    // Process legacy LSA (backward compatibility)
    ProcessLsa(*interest);
}

void
IRouteApp::OnData(shared_ptr<const Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());
    App::OnData(data); // Tracing

    const Name& name = data->getName();

    // Check if this is an LSA Data (prefix-first format):
    // /<OriginID>/iroute/lsa/<SemVerID>/<SeqNo>
    // Look for "iroute" followed by "lsa" pattern in name
    int iroutePos = -1;
    for (size_t i = 0; i + 1 < name.size(); ++i) {
        if (name.get(i).toUri() == "iroute" && name.get(i + 1).toUri() == "lsa") {
            iroutePos = static_cast<int>(i);
            break;
        }
    }
    
    if (iroutePos < 0) {
        NS_LOG_DEBUG("Ignoring non-LSA Data: " << name);
        return;
    }

    // Prefix-first parsing:
    // /<OriginID>/iroute/lsa/<SemVerID>/<SeqNo>
    // - OriginID: components from 0 to (iroutePos - 1)
    // - SemVerID: component at (iroutePos + 2)
    // - SeqNo: component at (iroutePos + 3)
    //
    // Examples:
    //   /domain0/iroute/lsa/1/5         -> OriginID=/domain0, semVer=1, seq=5
    //   /att/edge/a/iroute/lsa/1/5      -> OriginID=/att/edge/a, semVer=1, seq=5

    const size_t minSize = iroutePos + 4;  // OriginID + iroute + lsa + SemVer + Seq
    
    if (name.size() < minSize) {
        NS_LOG_WARN("Invalid LSA Data name format (too short): " << name);
        return;
    }

    // Extract from known positions
    uint64_t seqNo = 0;
    uint32_t semVerId = 0;
    
    try {
        semVerId = static_cast<uint32_t>(name.get(iroutePos + 2).toNumber());
        seqNo = name.get(iroutePos + 3).toNumber();
    } catch (const std::exception& e) {
        NS_LOG_WARN("Failed to parse LSA name components: " << e.what());
        return;
    }

    // OriginID = slice from 0 to iroutePos
    Name originIdName;
    for (int i = 0; i < iroutePos; ++i) {
        originIdName.append(name.get(i));
    }
    std::string originId = originIdName.toUri();

    // Validate SemVerID
    if (semVerId != m_semVerId && semVerId != 0) {
        NS_LOG_DEBUG("LSA SemVerID mismatch: got " << semVerId << ", expected " << m_semVerId);
    }

    NS_LOG_INFO("Processing LSA Data: origin=" << originId 
                << ", semVerId=" << semVerId << ", seqNo=" << seqNo);

    // Parse content TLV for centroids
    // Expected format: CentroidList [ CentroidEntry [ CentroidId, Vector, Radius, Weight ] ... ]
    const auto& content = data->getContent();
    if (content.value_size() == 0) {
        NS_LOG_WARN("LSA Data has empty content");
        return;
    }

    std::vector<iroute::CentroidEntry> centroids;
    double cost = m_routeCost;  // Use local cost for now

    try {
        ::ndn::Block contentBlock(nonstd::span<const uint8_t>(content.value(), content.value_size()));
        contentBlock.parse();

        // Check for CentroidList wrapper
        if (contentBlock.type() == iroute::lsa_tlv::CentroidList) {
            for (const auto& centroidBlock : contentBlock.elements()) {
                if (centroidBlock.type() == iroute::lsa_tlv::CentroidEntry) {
                    centroidBlock.parse();
                    
                    iroute::CentroidEntry entry;
                    
                    for (const auto& field : centroidBlock.elements()) {
                        switch (field.type()) {
                            case iroute::lsa_tlv::CentroidId:
                                entry.centroidId = static_cast<uint32_t>(::ndn::encoding::readNonNegativeInteger(field));
                                break;
                            case iroute::tlv::SemanticVector:
                                entry.C.wireDecode(field);
                                break;
                            case iroute::lsa_tlv::Radius: {
                                uint64_t bits = ::ndn::encoding::readNonNegativeInteger(field);
                                std::memcpy(&entry.radius, &bits, sizeof(double));
                                break;
                            }
                            case iroute::lsa_tlv::Weight: {
                                uint64_t bits = ::ndn::encoding::readNonNegativeInteger(field);
                                std::memcpy(&entry.weight, &bits, sizeof(double));
                                break;
                            }
                        }
                    }
                    
                    // Ensure centroid vector is normalized
                    if (!entry.C.empty() && !entry.C.isNormalized()) {
                        entry.C.normalize();
                    }
                    
                    centroids.push_back(entry);
                }
            }
        }
    } catch (const std::exception& e) {
        NS_LOG_WARN("Failed to decode LSA content TLV: " << e.what());
        // Fall back to legacy single-vector parsing if available
        return;
    }

    if (centroids.empty()) {
        NS_LOG_WARN("No centroids decoded from LSA Data");
        return;
    }

    NS_LOG_DEBUG("Decoded " << centroids.size() << " centroids from LSA");

    // Update RouteManager with the new domain entry
    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
    
    // Create DomainEntry for the origin
    iroute::DomainEntry domainEntry;
    domainEntry.domainId = Name(originId);
    domainEntry.semVerId = semVerId;
    domainEntry.seqNo = seqNo;
    domainEntry.lifetime = 300.0;  // Default 5 minutes
    domainEntry.scope = 0;
    domainEntry.centroids = centroids;
    domainEntry.cost = cost;

    // Update domain (handles seqNo monotonicity internally)
    rm->updateDomain(domainEntry);

    // Update per-domain sequence tracking for incremental fetch (ingress)
    m_lastSeqByDomain[originId] = seqNo;
    ++m_lsaRxCount;

    // Record Rx wire size for control plane overhead statistics
    m_lsaRxBytesTotal += data->wireEncode().size();

    NS_LOG_INFO("Updated RouteManager with LSA from " << originId 
                << " (" << centroids.size() << " centroids, seq=" << seqNo << ")");
}

// =============================================================================
// Internal Methods
// =============================================================================

void
IRouteApp::BroadcastLsa()
{
    NS_LOG_FUNCTION(this);

    if (!m_active) {
        NS_LOG_DEBUG("App not active, skipping LSA broadcast");
        return;
    }

    // Check if local semantics is set
    if (m_localSemantics.empty()) {
        NS_LOG_WARN("Local semantics not set, skipping LSA broadcast");
        ScheduleLsaBroadcast();
        return;
    }

    // Hysteresis check: skip LSA if centroid drift is below threshold
    if (m_hysteresisThreshold > 0.0 && !m_previousCentroid.empty()) {
        double similarity = m_localSemantics.computeCosineSimilarity(m_previousCentroid);
        double drift = 1.0 - similarity;
        if (drift <= m_hysteresisThreshold) {
            NS_LOG_DEBUG("Centroid drift (" << drift << ") <= threshold (" 
                         << m_hysteresisThreshold << "), skipping LSA");
            ScheduleLsaBroadcast();
            return;
        }
        NS_LOG_DEBUG("Centroid drift (" << drift << ") > threshold, broadcasting LSA");
    }
    // Update previous centroid for next comparison
    m_previousCentroid = m_localSemantics;

    // Increment sequence number
    ++m_lsaSeqNum;

    // Build LSA Interest name: /localhop/iroute/lsa/<router-id>/<seq>
    Name lsaName(kLsaPrefix);
    lsaName.append(m_routerId);
    lsaName.appendNumber(m_lsaSeqNum);

    // Create Interest
    auto interest = std::make_shared<Interest>(lsaName);
    interest->setCanBePrefix(false);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(time::seconds(4));  // Slightly less than interval

    // Encode the semantic vector into ApplicationParameters
    try {
        ::ndn::Block vectorBlock = m_localSemantics.wireEncode();
        interest->setApplicationParameters(vectorBlock);
        
        // Debug: Log dimension being broadcast
        std::cout << "[LSA DEBUG] " << m_routerId 
                  << " broadcasting vector dim=" << m_localSemantics.getDimension()
                  << ", block size=" << vectorBlock.size() << std::endl;
    }
    catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to encode semantic vector: " << e.what());
        std::cerr << "[LSA ERROR] " << m_routerId 
                  << " failed to encode vector: " << e.what() << std::endl;
        ScheduleLsaBroadcast();
        return;
    }

    // Record our own nonce to avoid processing it when it comes back
    RecordNonce(interest->getNonce());

    // Increment global broadcast counter
    ++s_lsaBroadcastCount;

    // Send the Interest
    NS_LOG_INFO("Broadcasting LSA: " << lsaName << " (seq=" << m_lsaSeqNum << ")");

    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule next broadcast
    ScheduleLsaBroadcast();
}

void
IRouteApp::PublishLsaData()
{
    NS_LOG_FUNCTION(this);

    if (!m_active) {
        return;
    }

    // Check if we have centroids to advertise
    // If using legacy single-centroid mode, convert to multi-centroid
    if (m_localCentroids.empty() && !m_localSemantics.empty()) {
        // Convert legacy single centroid to multi-centroid format
        iroute::CentroidEntry legacy(0, m_localSemantics, 0.5, 1.0);
        m_localCentroids.push_back(legacy);
        NS_LOG_DEBUG("Converted legacy single centroid to multi-centroid format");
    }

    if (m_localCentroids.empty()) {
        NS_LOG_WARN("No centroids to advertise, skipping LSA publication");
        ScheduleLsaBroadcast();
        return;
    }

    // Hysteresis check (paper Eq. 3): skip if drift < δ and radius change < γ
    // For simplicity, we check centroid drift against previous values
    bool shouldPublish = m_previousCentroids.empty();  // Always publish if first time
    
    if (!shouldPublish && m_previousCentroids.size() == m_localCentroids.size()) {
        double maxDrift = 0.0;
        for (size_t i = 0; i < m_localCentroids.size(); ++i) {
            if (i < m_previousCentroids.size()) {
                // Euclidean distance on L2-normalized vectors
                double drift = m_localCentroids[i].C.computeCosineDistance(m_previousCentroids[i].C);
                maxDrift = std::max(maxDrift, drift);
            }
        }
        shouldPublish = (maxDrift > m_hysteresisThreshold);
        
        if (!shouldPublish) {
            NS_LOG_DEBUG("Centroid drift (" << maxDrift << ") <= threshold (" 
                         << m_hysteresisThreshold << "), skipping LSA");
            ScheduleLsaBroadcast();
            return;
        }
    }

    // Update previous centroids for next comparison
    m_previousCentroids = m_localCentroids;

    // Increment sequence number
    ++m_lsaSeqNum;

    // Create LSA Data name: /iroute/lsa/<OriginID>/<SemVerID>/<SeqNo>
    Name lsaName = createLsaName();

    // Create Data packet
    auto data = std::make_shared<Data>(lsaName);
    data->setFreshnessPeriod(time::seconds(static_cast<int>(m_lsaInterval.GetSeconds() * 2)));

    // Encode multi-centroid content as TLV
    // Format: CentroidList [ CentroidEntry [ CentroidId, VectorData, Radius, Weight ] ... ]
    try {
        ::ndn::EncodingBuffer encoder;
        size_t totalLength = 0;

        // Encode centroids in reverse order (TLV convention)
        for (auto it = m_localCentroids.rbegin(); it != m_localCentroids.rend(); ++it) {
            size_t centroidLength = 0;

            // Weight (as double bytes) - use memcpy to avoid strict-aliasing
            uint64_t weightBits;
            std::memcpy(&weightBits, &it->weight, sizeof(double));
            centroidLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, iroute::lsa_tlv::Weight, weightBits);

            // Radius (as double bytes) - use memcpy to avoid strict-aliasing
            uint64_t radiusBits;
            std::memcpy(&radiusBits, &it->radius, sizeof(double));
            centroidLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, iroute::lsa_tlv::Radius, radiusBits);

            // Centroid vector
            ::ndn::Block vectorBlock = it->C.wireEncode();
            centroidLength += encoder.prependRange(vectorBlock.begin(), vectorBlock.end());

            // CentroidId
            centroidLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, iroute::lsa_tlv::CentroidId, it->centroidId);

            // CentroidEntry wrapper
            centroidLength += encoder.prependVarNumber(centroidLength);
            centroidLength += encoder.prependVarNumber(iroute::lsa_tlv::CentroidEntry);
            totalLength += centroidLength;
        }

        // CentroidList wrapper
        totalLength += encoder.prependVarNumber(totalLength);
        totalLength += encoder.prependVarNumber(iroute::lsa_tlv::CentroidList);

        data->setContent(encoder.block());

        NS_LOG_DEBUG("Encoded " << m_localCentroids.size() << " centroids, content size=" 
                     << data->getContent().value_size());
    }
    catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to encode LSA content: " << e.what());
        ScheduleLsaBroadcast();
        return;
    }

    // Sign the Data with KeyChain (per paper Section 4.1)
    m_keyChain.sign(*data);

    // Record wire size for control plane overhead statistics
    size_t wireSize = data->wireEncode().size();
    m_lsaTxBytesTotal += wireSize;
    m_lsaTxBytesMax = std::max(m_lsaTxBytesMax, static_cast<uint32_t>(wireSize));
    ++m_lsaTxCount;

    // Put Data into content store (so it can be served to fetching routers)
    m_transmittedDatas(data, this, m_face);
    m_appLink->onReceiveData(*data);

    // Increment global counter
    ++s_lsaBroadcastCount;

    NS_LOG_INFO("Published LSA Data: " << lsaName << " (seq=" << m_lsaSeqNum 
                << ", centroids=" << m_localCentroids.size() << ", wireSize=" << wireSize << ")");

    // Send Notification Interest to trigger fetch by neighbors
    // Name: /ndn/broadcast/iroute/v2/notify/<RouterID>/<SemVerID>/<SeqNo>
    Name notifyName(kLsaNotifyPrefix);
    notifyName.append(m_routerId)
              .appendNumber(m_semVerId)
              .appendNumber(m_lsaSeqNum);
              
    auto notifyInterest = std::make_shared<Interest>(notifyName);
    notifyInterest->setCanBePrefix(false);
    notifyInterest->setMustBeFresh(true);
    notifyInterest->setInterestLifetime(time::seconds(1));
    
    m_transmittedInterests(notifyInterest, this, m_face);
    m_appLink->onReceiveInterest(*notifyInterest); // Inject to stack
    
    // Schedule next publication
    ScheduleLsaBroadcast();
}

void
IRouteApp::RespondWithLsaData(const Interest& interest)
{
    NS_LOG_FUNCTION(this << interest.getName());

    if (!m_active) {
        return;
    }

    // Ensure we have centroids to advertise
    if (m_localCentroids.empty() && !m_localSemantics.empty()) {
        iroute::CentroidEntry legacy(0, m_localSemantics, 0.5, 1.0);
        m_localCentroids.push_back(legacy);
    }

    if (m_localCentroids.empty()) {
        NS_LOG_WARN("No centroids to respond with, dropping Interest");
        return;
    }

    // Create Data packet with name matching the Interest
    // This ensures proper PIT satisfaction
    auto data = std::make_shared<Data>(interest.getName());

    // Set FreshnessPeriod (P2 enhancement)
    data->setFreshnessPeriod(ndn::time::seconds(static_cast<int64_t>(m_lsaInterval.GetSeconds())));

    // TLV-encode centroids (same as PublishLsaData)
    try {
        ::ndn::EncodingBuffer encoder;
        size_t totalLength = 0;

        // Encode in reverse order for efficient prepending
        for (auto it = m_localCentroids.rbegin(); it != m_localCentroids.rend(); ++it) {
            size_t centroidLength = 0;

            // Weight (as uint64 bit pattern) 
            uint64_t weightBits;
            std::memcpy(&weightBits, &it->weight, sizeof(double));
            centroidLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, iroute::lsa_tlv::Weight, weightBits);

            // Radius (as uint64 bit pattern)
            uint64_t radiusBits;
            std::memcpy(&radiusBits, &it->radius, sizeof(double));
            centroidLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, iroute::lsa_tlv::Radius, radiusBits);

            // Semantic Vector
            ::ndn::Block vectorBlock = it->C.wireEncode();
            centroidLength += encoder.prependRange(vectorBlock.begin(), vectorBlock.end());

            // CentroidId
            centroidLength += ::ndn::encoding::prependNonNegativeIntegerBlock(
                encoder, iroute::lsa_tlv::CentroidId, it->centroidId);

            // CentroidEntry wrapper
            centroidLength += encoder.prependVarNumber(centroidLength);
            centroidLength += encoder.prependVarNumber(iroute::lsa_tlv::CentroidEntry);
            totalLength += centroidLength;
        }

        // CentroidList wrapper
        totalLength += encoder.prependVarNumber(totalLength);
        totalLength += encoder.prependVarNumber(iroute::lsa_tlv::CentroidList);

        data->setContent(encoder.block());
    }
    catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to encode LSA content: " << e.what());
        return;
    }

    // Sign the Data with KeyChain
    m_keyChain.sign(*data);

    // Send the Data to satisfy the pending Interest
    m_transmittedDatas(data, this, m_face);
    m_appLink->onReceiveData(*data);

    NS_LOG_INFO("Responded with LSA Data: " << data->getName() 
                << " (centroids=" << m_localCentroids.size() << ")");
}

// =============================================================================
// P0-2: Ingress Query Service
// =============================================================================

void
IRouteApp::HandleQueryInterest(const Interest& interest)
{
    NS_LOG_FUNCTION(this << interest.getName());

    // Parse semantic query vector from ApplicationParameters
    iroute::SemanticVector queryVector;
    bool hasVector = false;

    if (interest.hasApplicationParameters()) {
        try {
            const auto& params = interest.getApplicationParameters();
            ::ndn::Block block(nonstd::span<const uint8_t>(params.value(), params.value_size()));
            queryVector.wireDecode(block);
            queryVector.normalize();
            hasVector = true;
            NS_LOG_DEBUG("Parsed query vector, dim=" << queryVector.getDimension());
        }
        catch (const std::exception& e) {
            NS_LOG_WARN("Failed to parse query vector: " << e.what());
        }
    }

    // Get RouteManager for semantic ranking
    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
    
    // Perform semantic ranking via findBestDomainsV2
    std::vector<iroute::DomainResult> candidates;
    ::ndn::Name canonicalName;
    double confidence = 0.0;
    bool found = false;

    if (hasVector && rm->domainCount() > 0) {
        // Use active SemVerID for consistent ranking
        uint32_t activeSemVer = rm->getActiveSemVerId();
        
        // Get top-K candidates with bounded probing
        auto domains = rm->findBestDomainsV2(queryVector, iroute::params::kKMax, activeSemVer);
        
        // Filter by τ threshold
        for (const auto& d : domains) {
            if (d.confidence >= m_tau) {
                candidates.push_back(d);
            }
        }
        
        // If no candidates above τ, use best available (bounded probing fallback)
        if (candidates.empty() && !domains.empty()) {
            candidates.push_back(domains[0]);
            NS_LOG_DEBUG("No candidates above τ, using best available: " << domains[0].domainId);
        }
        
        // Select best candidate and construct CanonicalName
        if (!candidates.empty()) {
            const auto& best = candidates[0];
            found = true;
            confidence = best.confidence;
            
            // CanonicalName = DomainID + /data (simplified for demo)
            // In production, this would be derived from domain's advertised manifest
            canonicalName = best.domainId;
            canonicalName.append("data");
            
            NS_LOG_INFO("Ingress resolved: best domain=" << best.domainId 
                        << " confidence=" << confidence);
        }
    }
    
    // Create DiscoveryReply Data with SAME name as Interest (P0-2 requirement)
    auto data = std::make_shared<Data>(interest.getName());
    data->setFreshnessPeriod(ndn::time::seconds(10));
    
    // Encode DiscoveryReply TLV content
    ::ndn::Block replyContent = iroute::tlv::encodeDiscoveryReply(
        found, canonicalName, confidence, rm->getActiveSemVerId());
    data->setContent(replyContent);
    
    // Sign the Data
    m_keyChain.sign(*data);
    
    // Send the Data to satisfy the pending Interest
    m_transmittedDatas(data, this, m_face);
    m_appLink->onReceiveData(*data);
    
    NS_LOG_INFO("Ingress: Sent DiscoveryReply Data: " << data->getName()
                << " (found=" << found << ", canonical=" << canonicalName << ")");
}

// =============================================================================
// Task 2: Discovery Service - Respond to /<DomainID>/iroute/disc/<SemVerID>
// =============================================================================

void
IRouteApp::HandleDiscoveryInterest(const Interest& interest, uint32_t semVerId)
{
    NS_LOG_FUNCTION(this << interest.getName() << semVerId);

    // SemVer gating: only respond to matching version (or wildcard 0)
    if (semVerId != 0 && semVerId != m_semVerId) {
        NS_LOG_INFO("Discovery: SemVer mismatch (req=" << semVerId
                    << ", local=" << m_semVerId << "), returning NOT_FOUND");

        auto data = std::make_shared<Data>(interest.getName());
        data->setFreshnessPeriod(ndn::time::milliseconds(100));
        ::ndn::Block replyContent = iroute::tlv::encodeDiscoveryReply(
            false, ::ndn::Name(), 0.0, semVerId);
        data->setContent(replyContent);
        m_keyChain.sign(*data);
        auto sendData = [this, data]() {
            m_transmittedDatas(data, this, m_face);
            m_appLink->onReceiveData(*data);
        };
        int64_t delayUs = static_cast<int64_t>(m_discReplyDelayUs);
        if (m_discReplyJitterUs > 0 && m_discReplyDelayRv) {
            delayUs += static_cast<int64_t>(std::llround(m_discReplyDelayRv->GetValue(
                -static_cast<double>(m_discReplyJitterUs),
                static_cast<double>(m_discReplyJitterUs))));
        }
        delayUs = std::max<int64_t>(0, delayUs);
        if (delayUs > 0) {
            Simulator::Schedule(MicroSeconds(delayUs), sendData);
        } else {
            sendData();
        }
        return;
    }

    // Parse semantic query vector from ApplicationParameters
    iroute::SemanticVector queryVector;
    bool hasVector = false;

    if (interest.hasApplicationParameters()) {
        try {
            const auto& params = interest.getApplicationParameters();
            ::ndn::Block block(nonstd::span<const uint8_t>(params.value(), params.value_size()));
            queryVector.wireDecode(block);
            queryVector.normalize();
            hasVector = true;
            NS_LOG_DEBUG("Discovery: Parsed query vector, dim=" << queryVector.getDimension());
        }
        catch (const std::exception& e) {
            NS_LOG_WARN("Discovery: Failed to parse query vector: " << e.what());
        }
    }

    // Compute best match
    ::ndn::Name canonicalName;
    double confidence = 0.0;
    bool found = false;
    std::string bestDocId;

    if (hasVector) {
        // ====================================================================
        // Document-Level Matching: Search content store for best document
        // ====================================================================
        if (!m_localContent.empty()) {
            // Brute-force cosine similarity over all documents in this domain
            double bestDocSimilarity = -1.0;
            
            for (const auto& doc : m_localContent) {
                if (doc.vector.getDimension() == queryVector.getDimension()) {
                    double similarity = queryVector.dot(doc.vector);  // Both normalized
                    if (similarity > bestDocSimilarity) {
                        bestDocSimilarity = similarity;
                        bestDocId = doc.docId;
                    }
                }
            }
            
            confidence = bestDocSimilarity;
            
            // ================================================================
            // PATCH P0-2: Producer ALWAYS returns best candidate with confidence.
            // The decision of whether to accept is made by Consumer using τ.
            // This ensures τ semantics are unified at the Consumer side.
            // ================================================================
            if (!bestDocId.empty() && bestDocSimilarity > 0) {
                found = true;
                // Construct canonical name with document ID: /domainX/data/doc/<docId>
                canonicalName = Name("/");
                canonicalName.append(m_routerId);
                canonicalName.append("data");
                canonicalName.append("doc");
                canonicalName.append(bestDocId);
                
                NS_LOG_INFO("Discovery: Document match found, docId=" << bestDocId
                            << ", confidence=" << confidence 
                            << ", canonical=" << canonicalName);
            } else {
                NS_LOG_DEBUG("Discovery: No valid document found in content store");
            }
        }
        // ====================================================================
        // NO FALLBACK: Without content store, return NOT_FOUND
        // ====================================================================
        // PATCH 4A: Removed centroid-only fallback that returned "/domainX/data"
        // placeholder. For real protocol accuracy experiments, Stage-2 must be
        // doc-level. If no contentFile is provided, discovery should fail.
        else if (!m_localCentroids.empty()) {
            // Centroid match only determines domain-level routing score,
            // but without content store, we cannot return a real document name.
            // Return NOT_FOUND to enforce doc-level matching requirement.
            double bestSimilarity = -1.0;
            for (const auto& centroid : m_localCentroids) {
                if (centroid.C.getDimension() == queryVector.getDimension()) {
                    double similarity = queryVector.dot(centroid.C);
                    if (similarity > bestSimilarity) {
                        bestSimilarity = similarity;
                    }
                }
            }
            confidence = bestSimilarity;
            // Do NOT set found=true - we have no real doc to return
            NS_LOG_WARN("Discovery: Centroid match (conf=" << confidence 
                        << ") but no content store loaded - returning NOT_FOUND. "
                        << "Use --contentFile for real dataset mode.");
        } else {
            // No local centroids or content configured
            NS_LOG_WARN("Discovery: No local centroids or content configured");
        }
    } else {
        // No query vector - cannot compute similarity
        NS_LOG_WARN("Discovery: No query vector provided");
    }

    // CRITICAL: Reply Data name MUST equal Interest name for PIT satisfaction
    auto data = std::make_shared<Data>(interest.getName());
    data->setFreshnessPeriod(ndn::time::milliseconds(100));
    
    // Encode DiscoveryReply TLV content
    ::ndn::Block replyContent = iroute::tlv::encodeDiscoveryReply(
        found, canonicalName, confidence, semVerId);
    data->setContent(replyContent);
    
    // Sign the Data
    m_keyChain.sign(*data);
    
    // Send the Data
    auto sendData = [this, data]() {
        m_transmittedDatas(data, this, m_face);
        m_appLink->onReceiveData(*data);
    };
    int64_t delayUs = static_cast<int64_t>(m_discReplyDelayUs);
    if (m_discReplyJitterUs > 0 && m_discReplyDelayRv) {
        delayUs += static_cast<int64_t>(std::llround(m_discReplyDelayRv->GetValue(
            -static_cast<double>(m_discReplyJitterUs),
            static_cast<double>(m_discReplyJitterUs))));
    }
    delayUs = std::max<int64_t>(0, delayUs);
    if (delayUs > 0) {
        Simulator::Schedule(MicroSeconds(delayUs), sendData);
    } else {
        sendData();
    }
    
    NS_LOG_INFO("Discovery: Sent reply Data: " << data->getName()
                << " (found=" << found << ", canonical=" << canonicalName << ")");
}

void
IRouteApp::ScheduleLsaBroadcast()
{
    NS_LOG_FUNCTION(this);

    if (!m_active) {
        return;
    }

    // Add random jitter to prevent synchronization
    double jitterSeconds = m_jitter->GetValue();
    Time nextBroadcast = m_lsaInterval + Seconds(jitterSeconds);

    // Check for v2 mode
    if (m_enableV2LsaData) {
        m_lsaBroadcastEvent = Simulator::Schedule(nextBroadcast,
                                                   &IRouteApp::PublishLsaData,
                                                   this);
    } else {
        // Legacy mode
        m_lsaBroadcastEvent = Simulator::Schedule(nextBroadcast,
                                                   &IRouteApp::BroadcastLsa,
                                                   this);
    }

    NS_LOG_DEBUG("Next LSA broadcast in " << nextBroadcast.GetSeconds() << "s");
}

void
IRouteApp::ProcessLsa(const Interest& interest)
{
    NS_LOG_FUNCTION(this << interest.getName());

    // Flood suppression: check if we've seen this nonce
    auto nonce = interest.getNonce();
    if (HasSeenNonce(nonce)) {
        NS_LOG_DEBUG("Dropping duplicate LSA (nonce=" << nonce << ")");
        return;
    }

    // Record the nonce
    RecordNonce(nonce);

    // Parse the LSA name: /ndn/broadcast/iroute/lsa/<router-id>/<seq>
    const Name& name = interest.getName();
    if (name.size() < 6) {
        NS_LOG_WARN("Invalid LSA name format (size=" << name.size() << "): " << name);
        return;
    }

    // Extract router ID (component 4) and sequence number (component 5)
    // Name format: /ndn/broadcast/iroute/lsa/<router-id>/<seq>
    std::string routerId = name.get(4).toUri();  // URI-decoded string
    uint64_t seqNum = name.get(5).toNumber();

    NS_LOG_DEBUG("Processing LSA from " << routerId << ", seq=" << seqNum
                 << ", myId=" << m_routerId);

    // Skip if this is our own LSA
    // Note: toUri() may add percent-encoding, so we compare with the raw component
    if (routerId == m_routerId || name.get(4).toUri(::ndn::name::UriFormat::DEFAULT) == m_routerId) {
        NS_LOG_DEBUG("Ignoring own LSA (routerId=" << routerId << ")");
        return;
    }

    // Extract the semantic vector from ApplicationParameters
    if (!interest.hasApplicationParameters()) {
        NS_LOG_WARN("LSA missing ApplicationParameters");
        return;
    }

    iroute::SemanticVector remoteCentroid;
    try {
        ::ndn::Block params = interest.getApplicationParameters();
        // ApplicationParameters is a TLV (type 36) containing our SemanticVector
        // We need to parse the content inside, not the outer block
        params.parse();

        // The first element inside ApplicationParameters should be our SemanticVector
        if (params.elements_size() > 0) {
            // Use the first element
            remoteCentroid.wireDecode(params.elements().front());
        }
        else {
            // If no sub-elements, try parsing the value as a complete TLV Block
            // Use nonstd::span_lite::span (ndn-cxx polyfill for std::span)
            ::ndn::Block vectorBlock(nonstd::span<const uint8_t>(params.value(), params.value_size()));
            remoteCentroid.wireDecode(vectorBlock);
        }
        
        // Debug: Log successful decode
        std::cout << "[LSA RECV] " << m_routerId 
                  << " decoded vector from " << routerId 
                  << ", dim=" << remoteCentroid.getDimension() << std::endl;
    }
    catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to decode semantic vector: " << e.what());
        std::cerr << "[LSA ERROR] " << m_routerId 
                  << " failed to decode vector from " << routerId 
                  << ": " << e.what() << std::endl;
        return;
    }

    // Build the prefix name for this router
    Name routerPrefix("/");
    routerPrefix.append(routerId);

    // Get this node's RouteManager via Registry
    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);

    // Update the RouteManager with semantic information only
    // Physical next-hop is determined via FIB lookup at forwarding time
    rm->updateRoute(
        routerPrefix,
        remoteCentroid,
        m_routeCost,
        routerId);  // Store the origin router ID

    // P0-1 FIX: Also populate DomainIndex for v2 gated scoring
    // Create DomainEntry with legacy fallback values
    {
        iroute::DomainEntry domainEntry;
        domainEntry.domainId = routerPrefix;
        // Task 3 FIX: Use active SemVerID so entries land in CURRENT index
        domainEntry.semVerId = rm->getActiveSemVerId();
        domainEntry.seqNo = seqNum;
        domainEntry.cost = m_routeCost;
        
        // Create single centroid from legacy LSA vector
        // Constraint B-1: Use sensible fallback values for radius/weight
        iroute::CentroidEntry centroid;
        centroid.centroidId = 0;
        centroid.C = remoteCentroid;
        centroid.C.normalize();  // Ensure L2-normalized
        centroid.radius = 1.0;   // Wide coverage fallback (logs warning below)
        centroid.weight = 1.0;   // Single object fallback
        
        domainEntry.centroids.push_back(centroid);
        
        // Call updateDomain to populate v2 index
        rm->updateDomain(domainEntry);
        
        NS_LOG_INFO("P0-1: Legacy LSA -> DomainIndex, domain=" << routerPrefix
                    << " semVer=" << m_semVerId
                    << " (legacy fallback radius=1.0, weight=1.0)");
    }

    size_t ribSize = rm->size();
    size_t domainCountVal = rm->domainCount();
    std::cout << "[RIB UPDATE] " << m_routerId 
              << " added route for " << routerPrefix 
              << ", RIB size=" << ribSize 
              << ", DomainIndex size=" << domainCountVal << std::endl;

    NS_LOG_INFO("Updated route: " << routerPrefix
                << " (cost=" << m_routeCost << ", domainCount=" << domainCountVal << ")");

    // Flood the LSA to other neighbors (not implemented in this basic version)
    // In a full implementation, we would re-broadcast to all faces
}

bool
IRouteApp::HasSeenNonce(::ndn::Interest::Nonce nonce) const
{
    return m_seenNonces.find(nonce) != m_seenNonces.end();
}

void
IRouteApp::RecordNonce(::ndn::Interest::Nonce nonce)
{
    // Purge old nonces if we've exceeded the limit
    if (m_seenNonces.size() >= kMaxSeenNonces) {
        // Simple strategy: clear half of the nonces
        // A better approach would use LRU or time-based expiration
        auto it = m_seenNonces.begin();
        std::advance(it, kMaxSeenNonces / 2);
        m_seenNonces.erase(m_seenNonces.begin(), it);
        NS_LOG_DEBUG("Purged " << (kMaxSeenNonces / 2) << " old nonces");
    }

    m_seenNonces.insert(nonce);
}

// =============================================================================
// LSA Polling (Ingress Only)
// =============================================================================

void
IRouteApp::SetKnownDomains(const std::vector<Name>& domains)
{
    NS_LOG_FUNCTION(this << domains.size());
    m_knownDomains = domains;
    
    // Initialize lastSeq for each domain to 0
    for (const auto& domain : domains) {
        m_lastSeqByDomain[domain.toUri()] = 0;
    }
    
    NS_LOG_INFO("Configured " << domains.size() << " known domains for LSA polling");
    
    // If app is already active and polling is enabled, start polling immediately
    if (m_active && (m_isIngress || m_enableLsaPolling) && !m_knownDomains.empty()) {
        // Schedule with small jitter
        Time delay = MilliSeconds(100 + m_jitter->GetValue() * 100);
        m_lsaFetchEvent = Simulator::Schedule(delay, &IRouteApp::FetchDomainLsas, this);
    }
}

void
IRouteApp::FetchDomainLsas()
{
    NS_LOG_FUNCTION(this);

    if (!m_active || (!m_isIngress && !m_enableLsaPolling)) {
        return;
    }

    if (m_knownDomains.empty()) {
        NS_LOG_DEBUG("No known domains configured for LSA polling");
        return;
    }

    NS_LOG_INFO("Fetching LSAs from " << m_knownDomains.size() << " known domains");

    for (size_t idx = 0; idx < m_knownDomains.size(); ++idx) {
        const auto& domain = m_knownDomains[idx];
        std::string domainUri = domain.toUri();
        
        // Get next seq to fetch (lastSeq + 1)
        uint64_t nextSeq = m_lastSeqByDomain[domainUri] + 1;
        
        // Build LSA Interest: /<domain>/iroute/lsa/<semVerId>/<nextSeq>
        Name lsaName(domain);
        lsaName.append("iroute").append("lsa")
               .appendNumber(m_semVerId)
               .appendNumber(nextSeq);

        // Schedule with per-domain jitter to avoid burst (0-50ms stagger)
        Time delay = MilliSeconds(idx * 10 + m_jitter->GetValue() * 50);
        
        Simulator::Schedule(delay, [this, lsaName]() {
            if (!m_active) return;
            
            auto interest = std::make_shared<Interest>(lsaName);
            interest->setCanBePrefix(false);
            interest->setMustBeFresh(true);
            interest->setInterestLifetime(::ndn::time::milliseconds(2000));
            
            NS_LOG_DEBUG("Sending LSA fetch Interest: " << lsaName);
            
            m_transmittedInterests(interest, this, m_face);
            m_appLink->onReceiveInterest(*interest);
            
            ++m_lsaFetchCount;
        });
    }

    // Schedule next polling round
    m_lsaFetchEvent = Simulator::Schedule(m_lsaFetchInterval, 
        &IRouteApp::FetchDomainLsas, this);
}

} // namespace ndn
} // namespace ns3
