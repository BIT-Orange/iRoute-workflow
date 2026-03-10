/**
 * @file iroute-discovery-consumer.cpp
 * @brief Implementation of two-stage discovery consumer.
 *
 * @author iRoute Team
 * @date 2024
 */

#include "iroute-discovery-consumer.hpp"
#include "iroute-route-manager-registry.hpp"
#include "extensions/iroute-tlv.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"

#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/lp/tags.hpp>
#include <random>
#include <algorithm>  // PATCH: For std::max_element

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.IRouteDiscoveryConsumer");

NS_OBJECT_ENSURE_REGISTERED(IRouteDiscoveryConsumer);

// Discovery Interest component
const Name IRouteDiscoveryConsumer::kDiscoveryPrefix("/iroute/disc");

// =============================================================================
// TypeId Registration
// =============================================================================

TypeId
IRouteDiscoveryConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::IRouteDiscoveryConsumer")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<IRouteDiscoveryConsumer>()
        .AddAttribute("VectorDim",
                      "Dimension of semantic vectors",
                      UintegerValue(384),
                      MakeUintegerAccessor(&IRouteDiscoveryConsumer::m_vectorDim),
                      MakeUintegerChecker<uint32_t>(1))
        .AddAttribute("Frequency",
                      "Frequency of discovery requests (per second)",
                      DoubleValue(1.0),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_frequency),
                      MakeDoubleChecker<double>(0.0))
        .AddAttribute("LifeTime",
                      "Interest lifetime",
                      TimeValue(Seconds(4.0)),
                      MakeTimeAccessor(&IRouteDiscoveryConsumer::m_interestLifetime),
                      MakeTimeChecker())
        .AddAttribute("SemVerId",
                      "Semantic version ID",
                      UintegerValue(1),
                      MakeUintegerAccessor(&IRouteDiscoveryConsumer::m_semVerId),
                      MakeUintegerChecker<uint32_t>())
        // Configurable protocol parameters for experiments
        .AddAttribute("KMax",
                      "Maximum probing candidates (used for both TopK retrieval and probing cap)",
                      UintegerValue(iroute::params::kKMax),
                      MakeUintegerAccessor(&IRouteDiscoveryConsumer::m_kMax),
                      MakeUintegerChecker<uint32_t>(1, 20))
        .AddAttribute("ScoreThresholdTau",
                      "Ingress-side confidence threshold for domain probing. "
                      "Applied to semantic confidence (q·C·gate), not the full score. "
                      "Domains with confidence < tau are skipped (unless all are below, then top-1 is used).",
                      DoubleValue(iroute::params::kTau),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_tau),
                      MakeDoubleChecker<double>(-1.0, 1.0))  // Confidence is in [-1, 1]
        .AddAttribute("FetchTimeoutMs",
                      "Stage-2 fetch timeout in milliseconds (also sets Stage-2 InterestLifetime)",
                      UintegerValue(4000),
                      MakeUintegerAccessor(&IRouteDiscoveryConsumer::m_fetchTimeoutMs),
                      MakeUintegerChecker<uint32_t>(100))
        .AddAttribute("ExploreRate",
                      "Probability of adding one exploratory probe outside top-K",
                      DoubleValue(0.0),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_exploreRate),
                      MakeDoubleChecker<double>(0.0, 1.0))
        .AddAttribute("ExploreExtraK",
                      "Extra candidates fetched when exploration is enabled",
                      UintegerValue(1),
                      MakeUintegerAccessor(&IRouteDiscoveryConsumer::m_exploreExtraK),
                      MakeUintegerChecker<uint32_t>(1, 10))
        // Eq.(2) gated scoring formula parameters
        .AddAttribute("Alpha",
                      "Weight for semantic similarity in Eq.(2) scoring formula",
                      DoubleValue(iroute::params::kAlpha),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_alpha),
                      MakeDoubleChecker<double>(0.0, 10.0))
        .AddAttribute("Beta",
                      "Weight for cost penalty in Eq.(2) scoring formula",
                      DoubleValue(iroute::params::kBeta),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_beta),
                      MakeDoubleChecker<double>(0.0, 10.0))
        .AddAttribute("Lambda",
                      "Sigmoid steepness for soft gate in Eq.(2)",
                      DoubleValue(iroute::params::kLambda),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_lambda),
                      MakeDoubleChecker<double>(0.1, 100.0))
        .AddAttribute("NMin",
                      "Minimum samples before EWMA penalty activates",
                      UintegerValue(iroute::params::kNMin),
                      MakeUintegerAccessor(&IRouteDiscoveryConsumer::m_nMin),
                      MakeUintegerChecker<uint32_t>(1, 1000))
        .AddAttribute("EwmaAlpha",
                      "EWMA decay factor for reliability tracking",
                      DoubleValue(iroute::params::kEwmaAlpha),
                      MakeDoubleAccessor(&IRouteDiscoveryConsumer::m_ewmaAlpha),
                      MakeDoubleChecker<double>(0.0, 1.0));

    return tid;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

IRouteDiscoveryConsumer::IRouteDiscoveryConsumer()
    : m_nodeId(0)
    , m_vectorDim(384)
    , m_semVerId(1)
    , m_frequency(1.0)
    , m_interestLifetime(Seconds(4.0))
    , m_kMax(iroute::params::kKMax)
    , m_tau(iroute::params::kTau)
    , m_fetchTimeoutMs(4000)
    , m_exploreRate(0.0)
    , m_exploreExtraK(1)
    , m_alpha(iroute::params::kAlpha)
    , m_beta(iroute::params::kBeta)
    , m_lambda(iroute::params::kLambda)
    , m_nMin(iroute::params::kNMin)
    , m_ewmaAlpha(iroute::params::kEwmaAlpha)
    , m_discoveryAttempts(0)
    , m_discoveryFound(0)
    , m_discoveryNotFound(0)
    , m_stage2Success(0)
    , m_stage2Failure(0)
    , m_queryIndex(0)
{
    NS_LOG_FUNCTION(this);
    m_exploreRng = CreateObject<UniformRandomVariable>();
}

IRouteDiscoveryConsumer::~IRouteDiscoveryConsumer()
{
    NS_LOG_FUNCTION(this);
}

// =============================================================================
// Configuration
// =============================================================================

void
IRouteDiscoveryConsumer::SetQueryVector(const iroute::SemanticVector& vector)
{
    NS_LOG_FUNCTION(this << vector.getDimension());
    m_queryVector = vector;
    // Ensure L2-normalized
    if (!m_queryVector.isNormalized()) {
        m_queryVector.normalize();
    }
}

// =============================================================================
// Application Lifecycle
// =============================================================================

void
IRouteDiscoveryConsumer::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();

    m_nodeId = GetNode()->GetId();
    std::cout << "IRouteDiscoveryConsumer started on node " << m_nodeId << " freq=" << m_frequency << std::endl;
    NS_LOG_INFO("IRouteDiscoveryConsumer started on node " << m_nodeId);

    // Configure RouteManager with Eq.(2) parameters
    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
    if (rm) {
        rm->configureParams(m_alpha, m_beta, m_lambda, iroute::params::kWMax, m_nMin, m_ewmaAlpha);
        NS_LOG_DEBUG("Configured RouteManager: alpha=" << m_alpha << ", beta=" << m_beta 
                     << ", lambda=" << m_lambda << ", nMin=" << m_nMin << ", ewmaAlpha=" << m_ewmaAlpha);
    }

    // Schedule first discovery
    if (m_frequency > 0) {
        double delay = 1.0 / m_frequency;
        m_sendEvent = Simulator::Schedule(Seconds(delay), 
                                          &IRouteDiscoveryConsumer::StartDiscovery, 
                                          this);
    }
}

void
IRouteDiscoveryConsumer::StopApplication()
{
    NS_LOG_FUNCTION(this);
    
    Simulator::Cancel(m_sendEvent);
    
    // Print final statistics
    PrintStats();
    
    App::StopApplication();
}

// =============================================================================
// Stage-1: Discovery
// =============================================================================

void
IRouteDiscoveryConsumer::StartDiscovery()
{
    NS_LOG_FUNCTION(this);
    if(m_queryIndex % 50 == 0 && Simulator::Now().GetSeconds() > 1.0) 
        std::cout << "StartDiscovery idx=" << m_queryIndex << " traceSize=" << m_queryTrace.size() << std::endl;

    // Schedule next discovery cycle (independent of current one)
    if (m_frequency > 0) {
        m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_frequency),
                                          &IRouteDiscoveryConsumer::StartDiscovery,
                                          this);
    }

    // if (!m_active) return; // Removed implicit dependency

    // =========================================================================
    // Query Trace Mode: Get next query from trace if available
    // =========================================================================
    uint64_t qId = 0;
    if (!m_queryTrace.empty()) {
        if (m_queryIndex >= m_queryTrace.size()) {
            NS_LOG_INFO("Query trace exhausted, stopping discovery");
            Simulator::Cancel(m_sendEvent); // Cancel future events if done
            return;  // All queries processed
        }
        
        const auto& query = m_queryTrace[m_queryIndex];
        qId = query.id;
        if (m_activeTxs.count(qId) > 0) {
            qId = static_cast<uint64_t>(m_queryIndex);
        }
        m_queryVector = query.vector;
        if (!m_queryVector.isNormalized()) {
            m_queryVector.normalize();
        }
        
        // Initialize TxRecord for this query with REAL timestamp
        TxRecord tx;
        tx.queryId = qId;
        tx.startTime = Simulator::Now().GetSeconds();
        tx.expectedDomain = query.expectedDomain;
        // Copy ground truth for doc-level and domain-level accuracy
        tx.targetName = query.targetName;
        tx.targetDocIds = query.targetDocIds;  // Multi-doc relevance set from qrels
        tx.targetDomains = query.targetDomains;  // Domains containing relevant docs
        // Record effective parameter values for reproducibility
        tx.kMaxEffective = m_kMax;
        tx.tauEffective = m_tau;
        tx.fetchTimeoutMsEffective = m_fetchTimeoutMs;
        // Init fallback metrics
        tx.fallbackUsed = false;
        tx.numStage1Attempts = 0;
        tx.usedPrevIndex = false;
        tx.discoveryAttempts = 0;
        tx.firstChoiceDomain = "";
        tx.finalSuccessDomain = "";
        tx.attemptedDomains = "";
        
        m_activeTxs[tx.queryId] = tx;
        
        // Increment index after using it
        NS_LOG_DEBUG("Processing query " << tx.queryId
                     << " tracePos=" << m_queryIndex
                     << " expected=" << tx.expectedDomain 
                     << " objectId=" << query.objectId);
        ++m_queryIndex;
    }

    if (m_queryVector.empty()) {
        NS_LOG_WARN("No query vector set, skipping discovery");
        return;
    }
    
    // Start Discovery
    DoDiscoveryAttempt(m_semVerId, qId);
}

void
IRouteDiscoveryConsumer::DoDiscoveryAttempt(uint32_t semVerId, uint64_t queryId)
{
    NS_LOG_FUNCTION(this << semVerId << queryId);
    
    // Retrieve Query Vector
    auto posIt = m_queryPosById.find(queryId);
    if (posIt == m_queryPosById.end()) {
        NS_LOG_WARN("Unknown queryId=" << queryId << " in DoDiscoveryAttempt");
        return;
    }
    const auto& query = m_queryTrace[posIt->second];
    iroute::SemanticVector qVec = query.vector;
    if (!qVec.isNormalized()) qVec.normalize();
    
    // Retrieve TxRecord
    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) return; // Already finalized?
    TxRecord& tx = txIt->second;

    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
    uint32_t activeVer = rm->getActiveSemVerId();
    uint32_t prevVer = rm->getPrevSemVerId();
    
    m_attemptSemVer = semVerId;
    m_pendingProbes = 0;
    m_foundCandidate = false;
    m_discoveryCandidates.clear();  // PATCH: Clear candidates for new round
    m_currentQueryId = queryId;     // PATCH: Track current query
    
    tx.numStage1Attempts++;
    if (semVerId == prevVer && semVerId != activeVer) {
        tx.fallbackUsed = true;
        tx.usedPrevIndex = true;
    }

    // Query RouteManager for this version (optionally with exploration tail)
    bool doExplore = (m_exploreRate > 0.0 && m_exploreRng &&
                      m_exploreRng->GetValue(0.0, 1.0) < m_exploreRate);
    uint32_t extraK = doExplore ? m_exploreExtraK : 0;
    auto domains = rm->findBestDomainsV2(qVec, m_kMax + extraK, semVerId);

    // PATCH: Record Top-K list for Failure Taxonomy
    if (!domains.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < domains.size(); ++i) {
             if (i > 0) oss << ";";
             // format: /domain=score (confidence)
             oss << domains[i].domainId.toUri() << "=" << domains[i].score 
                 << "(" << domains[i].confidence << ")";
        }
        tx.topKList = oss.str();
    }

    // Filter by τ (applied to confidence, not score)
    // confidence = pure semantic similarity (q·C·gate), without cost penalty
    // score = confidence * wTerm - β * costPenalty (may be negative in star topology)
    std::vector<iroute::DomainResult> candidates;
    size_t baseK = std::min(domains.size(), static_cast<size_t>(m_kMax));
    for (size_t i = 0; i < baseK; ++i) {
        const auto& d = domains[i];
        if (d.confidence >= m_tau) {
            candidates.push_back(d);
        }
    }
    
    // Soft fallback: even if all below τ, still probe the best candidate
    // (prevents "all fail" when embeddings are low-calibrated)
    if (candidates.empty() && baseK > 0) {
        NS_LOG_DEBUG("All " << domains.size() << " domains below τ=" << m_tau 
                     << ", using soft fallback to probe top candidate");
        candidates.push_back(domains.front());
    }

    // Exploration: optionally add one extra candidate from the tail
    if (doExplore && domains.size() > baseK) {
        size_t tailCount = domains.size() - baseK;
        size_t offset = 0;
        if (tailCount > 1 && m_exploreRng) {
            offset = static_cast<size_t>(m_exploreRng->GetInteger(0, static_cast<uint32_t>(tailCount - 1)));
        }
        const auto& extra = domains[baseK + offset];
        bool exists = false;
        for (const auto& c : candidates) {
            if (c.domainId == extra.domainId) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            candidates.push_back(extra);
        }
    }
    
    // Record first choice (Rank 1)
    if (tx.firstChoiceDomain.empty() && !domains.empty()) {
        tx.firstChoiceDomain = domains[0].domainId.toUri();
    }
    
    tx.discoveryAttempts++;
    
    // Record attempted domains (candidates)
    for (const auto& c : candidates) {
        if (!tx.attemptedDomains.empty()) tx.attemptedDomains += "|";
        tx.attemptedDomains += c.domainId.toUri();
    }
    
    // Fallback Logic
    if (candidates.empty()) {
        if (semVerId == activeVer && prevVer != activeVer && prevVer != 0) {
            NS_LOG_WARN("Ver " << semVerId << " yielded no candidates. Falling back to v" << prevVer);
            Simulator::Schedule(MilliSeconds(5), 
                                &IRouteDiscoveryConsumer::DoDiscoveryAttempt, 
                                this, prevVer, queryId);
            return;
        } else {
            NS_LOG_WARN("No candidates found for v" << semVerId);
            // Finalize as failure
            tx.stage2Success = false;
            tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
            m_transactions.push_back(tx);
            m_activeTxs.erase(queryId);
            return;
        }
    }

    // Bounded probing
    size_t k = candidates.size();
    
    // Send Discovery Interests
    m_pendingProbes = k;
    for (size_t i = 0; i < k; ++i) {
        SendDiscoveryInterest(candidates[i].domainId, semVerId, queryId, qVec);
    }
}


void
IRouteDiscoveryConsumer::SendDiscoveryInterest(const Name& domainId, uint32_t semVerId, uint64_t queryId, const iroute::SemanticVector& vec)
{
    NS_LOG_FUNCTION(this << domainId << semVerId);

    // =========================================================================
    // REFACTORED: Per paper, Discovery Interest name is:
    // /<DomainID>/iroute/disc/<SemVerID>/<ParametersSha256DigestComponent>
    // 
    // We do NOT append a nonce to the name. The ParamDigest is auto-added by
    // setApplicationParameters() and serves as the unique identifier.
    // =========================================================================
    
    // Build Discovery Interest name: /<DomainID>/iroute/disc/<SemVerID>
    Name discName(domainId);
    discName.append("iroute");
    discName.append("disc");
    discName.appendNumber(semVerId);
    // NOTE: NO nonce appended here - ParamDigest will be added automatically

    auto interest = std::make_shared<Interest>(discName);
    interest->setCanBePrefix(true);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));

    // Encode query vector into ApplicationParameters
    // This will automatically append ParametersSha256DigestComponent to the name
    try {
        ::ndn::Block vectorBlock = vec.wireEncode();
        interest->setApplicationParameters(vectorBlock);
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to encode query vector: " << e.what());
        return;
    }

    // Explicitly wireEncode to ensure ParamDigest is computed
    interest->wireEncode();
    
    // Get the FULL Interest name (including ParamDigest) as the transaction key
    std::string discNameUri = interest->getName().toUri();
    
    // Map Interest name -> QueryId for transaction correlation
    m_discNameToQueryId[discNameUri] = queryId;
    m_discNameToDomain[discNameUri] = domainId;
    
    ++m_discoveryAttempts;
    
    // Log and record bytes
    auto it = m_activeTxs.find(queryId);
    if (it != m_activeTxs.end()) {
        TxRecord& tx = it->second;
        // PATCH 4B: Use ns3::Time for precise timing
        tx.tStage1Send = Simulator::Now();
        tx.stage1SendTime = tx.tStage1Send.GetSeconds();
        tx.stage1SendTimeMs = tx.tStage1Send.GetSeconds() * 1000.0;
        tx.semVerId = semVerId;
        ++tx.probesUsed;
        // Record Interest wire size for overhead statistics
        tx.stage1InterestBytes += interest->wireEncode().size();
    }

    // Send Interest
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    // Schedule timeout using full name as key
    Time timeout = m_interestLifetime + MilliSeconds(10);
    m_discoveryTimeoutsByName[discNameUri] = Simulator::Schedule(timeout, 
        &IRouteDiscoveryConsumer::HandleDiscoveryTimeout, this, discNameUri);

    NS_LOG_INFO("Sent Discovery Interest: " << interest->getName() 
                << " (full name with digest, queryId=" << queryId << ")");
}

// =============================================================================
// NDN Callbacks
// =============================================================================



void
IRouteDiscoveryConsumer::OnNack(shared_ptr<const lp::Nack> nack)
{
    NS_LOG_FUNCTION(this);
    App::OnNack(nack);

    const Name& name = nack->getInterest().getName();
    std::string nameUri = name.toUri();
    
    // =========================================================================
    // REFACTORED: Check if this NACK is for a discovery Interest using name map
    // =========================================================================
    auto discIt = m_discNameToQueryId.find(nameUri);
    
    if (discIt != m_discNameToQueryId.end()) {
        // This is a discovery NACK
        uint64_t queryId = discIt->second;
        
        // Cancel timeout
        auto timeoutIt = m_discoveryTimeoutsByName.find(nameUri);
        if (timeoutIt != m_discoveryTimeoutsByName.end()) {
            Simulator::Cancel(timeoutIt->second);
            m_discoveryTimeoutsByName.erase(timeoutIt);
        }
        
        // Clean up maps
        m_discNameToQueryId.erase(discIt);
        m_discNameToDomain.erase(nameUri);
        
        NS_LOG_DEBUG("Discovery NACK received for queryId=" << queryId);
        
        // Decrement pending probes
        if (m_pendingProbes > 0) {
            --m_pendingProbes;
        }
        
        // PATCH: When all probes done, use FinalizeDiscoveryRound
        if (m_pendingProbes == 0) {
            FinalizeDiscoveryRound(queryId);
        }
    } else {
        // This is a Stage-2 NACK
        ++m_stage2Failure;
        
        auto it = m_pendingFetch.find(name);
        if (it != m_pendingFetch.end()) {
            ReportFetchOutcome(it->second.domainId, false, it->second.semVerId);
            
            // Cancel fetch timeout
            auto timeoutIt = m_fetchTimeouts.find(name);
            if (timeoutIt != m_fetchTimeouts.end()) {
                Simulator::Cancel(timeoutIt->second);
                m_fetchTimeouts.erase(timeoutIt);
            }
            
            // Finalize TxRecord for stage-2 failure
            uint64_t qId = it->second.queryId;
            auto txIt = m_activeTxs.find(qId);
            
            if (txIt != m_activeTxs.end()) {
                TxRecord& tx = txIt->second;
                // PATCH 4B: Use ns3::Time for precise timing
                tx.tStage2Recv = Simulator::Now();
                tx.stage2RecvTime = tx.tStage2Recv.GetSeconds();
                tx.stage2RecvTimeMs = tx.tStage2Recv.GetSeconds() * 1000.0;
                // PATCH 4B: Compute precise RTT from Time difference
                tx.stage2RttMs = (tx.tStage2Recv - tx.tStage2Send).GetSeconds() * 1000.0;
                // PATCH 4B: Compute total latency from tStage1Send
                if(tx.tStage1Send > Seconds(0))
                    tx.totalMs = (tx.tStage2Recv - tx.tStage1Send).GetSeconds() * 1000.0;
                else
                    tx.totalMs = (tx.stage2RecvTime - tx.startTime) * 1000.0;
                tx.stage2Success = false;
                tx.finalSuccessDomain = "NACK";
                m_transactions.push_back(tx);
                m_activeTxs.erase(qId);
                
                NS_LOG_DEBUG("TxRecord finalized (NACK): queryId=" << qId
                             << " selectedDomain=" << tx.selectedDomain);
            }
            
            m_pendingFetch.erase(it);
        }
    }
}

// =============================================================================
// Discovery Reply Handling
// =============================================================================


void
IRouteDiscoveryConsumer::OnData(shared_ptr<const Data> data)
{
    NS_LOG_FUNCTION(this << data->getName());

    // Call parent for tracing
    App::OnData(data);

    const Name& name = data->getName();
    
    // =========================================================================
    // REFACTORED: Detect discovery reply by matching against our pending map
    // The Data name should exactly match our Discovery Interest name (with digest)
    // =========================================================================
    std::string dataNameUri = name.toUri();
    
    // Check if this is a Discovery Reply by looking up in our pending discovery map
    auto discIt = m_discNameToQueryId.find(dataNameUri);
    
    if (discIt == m_discNameToQueryId.end()) {
        // Not a discovery reply - might be Stage-2 Data
        HandleFetchData(*data);
        return;
    }
    
    // Found! This is a Discovery Reply
    uint64_t queryId = discIt->second;
    Name domainId;
    
    auto domainIt = m_discNameToDomain.find(dataNameUri);
    if (domainIt != m_discNameToDomain.end()) {
        domainId = domainIt->second;
    }
    
    // Cancel timeout immediately
    auto timeoutIt = m_discoveryTimeoutsByName.find(dataNameUri);
    if (timeoutIt != m_discoveryTimeoutsByName.end()) {
        Simulator::Cancel(timeoutIt->second);
        m_discoveryTimeoutsByName.erase(timeoutIt);
    }
    
    // Clean up pending discovery maps
    m_discNameToQueryId.erase(discIt);
    if (domainIt != m_discNameToDomain.end()) {
        m_discNameToDomain.erase(domainIt);
    }
    
    // Decrement pending probes
    if (m_pendingProbes > 0) --m_pendingProbes;
    
    NS_LOG_DEBUG("Discovery Reply received for queryId=" << queryId 
                 << " from domain=" << domainId);
    
    // Parse SemVerId from name: /<DomainID>/iroute/disc/<SemVerID>/...
    uint32_t semVerId = m_semVerId;
    for (size_t i = 0; i + 2 < name.size(); ++i) {
        if (name.get(i).toUri() == "iroute" && name.get(i+1).toUri() == "disc") {
            if (i + 2 < name.size()) {
                try {
                    semVerId = static_cast<uint32_t>(name.get(i+2).toNumber());
                } catch (...) {}
            }
            break;
        }
    }
    
    // Decode TLV Content
    std::optional<iroute::tlv::DiscoveryReplyData> replyOpt;
    try {
        const auto& content = data->getContent();
        ::ndn::Block block(nonstd::span<const uint8_t>(content.value(), content.value_size()));
        replyOpt = iroute::tlv::decodeDiscoveryReply(block);
    } catch (const std::exception& e) {
        NS_LOG_WARN("Failed to decode Discovery Reply TLV: " << e.what());
    }

    bool success = false;
    if (replyOpt) {
        const auto& reply = *replyOpt;
        NS_LOG_DEBUG("Decoded Reply: found=" << reply.found 
                     << " conf=" << reply.confidence 
                     << " canonical=" << reply.canonicalName);
        
        // =====================================================================
        // PATCH: "Best Candidate" Strategy
        // Instead of immediately using first "found=true", collect all and pick best.
        // =====================================================================
        if (reply.found) {
            // Store this candidate for later selection
            DiscoveryCandidate candidate;
            candidate.domainId = domainId;
            candidate.canonicalName = reply.canonicalName;
            candidate.confidence = reply.confidence;
            candidate.semVerId = semVerId;
            candidate.recvTime = Simulator::Now();
            candidate.dataBytes = data->wireEncode().size();
            m_discoveryCandidates.push_back(candidate);
            
            m_foundCandidate = true;  // At least one success
            success = true;
            ++m_discoveryFound;
            
            NS_LOG_INFO("Discovery Candidate from: " << domainId 
                        << " conf=" << reply.confidence 
                        << " (total candidates: " << m_discoveryCandidates.size() << ")");
        } else {
            NS_LOG_DEBUG("Discovery NotFound from " << domainId);
        }
    }

    // =========================================================================
    // PATCH: "Best Candidate" Strategy - Wait for all probes to complete
    // =========================================================================
    if (m_pendingProbes == 0) {
        // All probes done, now finalize by picking the best candidate
        FinalizeDiscoveryRound(queryId);
    }
}

// =============================================================================
// PATCH: FinalizeDiscoveryRound - Select best candidate and proceed to Stage-2
// =============================================================================
void
IRouteDiscoveryConsumer::FinalizeDiscoveryRound(uint64_t queryId)
{
    NS_LOG_FUNCTION(this << queryId);
    
    if (m_foundCandidate && !m_discoveryCandidates.empty()) {
        // =====================================================================
        // Select the candidate with HIGHEST confidence
        // =====================================================================
        auto bestIt = std::max_element(m_discoveryCandidates.begin(), m_discoveryCandidates.end(),
                                        [](const DiscoveryCandidate& a, const DiscoveryCandidate& b) {
                                            return a.confidence < b.confidence;
                                        });
        
        NS_LOG_INFO("Best Candidate selected: " << bestIt->domainId 
                    << " conf=" << bestIt->confidence 
                    << " out of " << m_discoveryCandidates.size() << " candidates");
        
        // Record Stats in TxRecord
        auto txIt = m_activeTxs.find(queryId);
        if (txIt != m_activeTxs.end()) {
            TxRecord& tx = txIt->second;
            tx.tStage1Recv = bestIt->recvTime;
            tx.stage1RecvTime = bestIt->recvTime.GetSeconds();
            tx.stage1RecvTimeMs = bestIt->recvTime.GetSeconds() * 1000.0;
            
            if (bestIt->canonicalName.size() > 0) {
                tx.selectedDomain = bestIt->canonicalName.getPrefix(1).toUri();
            } else {
                tx.selectedDomain = bestIt->domainId.toUri();
            }
            
            tx.stage1RttMs = (bestIt->recvTime - tx.tStage1Send).GetSeconds() * 1000.0;
            tx.stage1Success = true;
            tx.confidence = bestIt->confidence;
            tx.stage1DataBytes += bestIt->dataBytes;
            tx.probesUsed = m_discoveryCandidates.size();  // Track how many candidates found
            
            NS_LOG_DEBUG("TxRecord updated: queryId=" << queryId 
                         << " selectedDomain=" << tx.selectedDomain
                         << " stage1RttMs=" << tx.stage1RttMs
                         << " candidates=" << m_discoveryCandidates.size());
        }
        
        // Proceed to Stage 2 with the best candidate
        SendFetchInterest(bestIt->canonicalName, bestIt->domainId, bestIt->semVerId, queryId);
        
        // Clear candidates for next query
        m_discoveryCandidates.clear();
    } else {
        // No successful candidates - try fallback
        auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
        uint32_t activeVer = rm->getActiveSemVerId();
        uint32_t prevVer = rm->getPrevSemVerId();
        
        if (m_attemptSemVer == activeVer && prevVer != activeVer && prevVer != 0) {
            NS_LOG_INFO("All probes for v" << activeVer << " returned negative. Falling back to v" << prevVer);
            m_discoveryCandidates.clear();
            Simulator::Schedule(MilliSeconds(2), 
                                &IRouteDiscoveryConsumer::DoDiscoveryAttempt, 
                                this, prevVer, queryId);
        } else {
            NS_LOG_INFO("Discovery attempt failed (no fallback available)");
            auto txIt = m_activeTxs.find(queryId);
            if (txIt != m_activeTxs.end()) {
                TxRecord& tx = txIt->second;
                tx.stage1Success = false;
                tx.stage2Success = false;
                tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
                tx.finalSuccessDomain = "NOT_FOUND";
                tx.failureReason = "DISCOVERY_NOT_FOUND";
                m_transactions.push_back(tx);
                m_activeTxs.erase(queryId);
            }
            ++m_discoveryNotFound;
            m_discoveryCandidates.clear();
        }
    }
}

// =============================================================================
// Stage-2: Fetch
// =============================================================================

void
IRouteDiscoveryConsumer::SendFetchInterest(const Name& canonicalName, const Name& domainId, uint32_t semVerId, uint64_t queryId)
{
    NS_LOG_FUNCTION(this << canonicalName << domainId << semVerId);

    // Create fetch Interest for canonical content
    auto interest = std::make_shared<Interest>(canonicalName);
    interest->setCanBePrefix(true);
    interest->setMustBeFresh(false);  // Can accept cached Data
    // Use m_fetchTimeoutMs for Stage-2 InterestLifetime (consistent with timeout scheduling)
    interest->setInterestLifetime(ndn::time::milliseconds(m_fetchTimeoutMs));

    // =========================================================================
    // Transaction Logging: Record REAL stage2 send time
    // =========================================================================
    // Track for EWMA with semVerId and queryId for finalization
    m_pendingFetch[canonicalName] = PendingFetchInfo{domainId, semVerId, queryId};
    
    // Explicitly wireEncode to get accurate byte count
    interest->wireEncode();
    
    // Log and record bytes
    auto it = m_activeTxs.find(queryId);
    if (it != m_activeTxs.end()) {
        TxRecord& tx = it->second;
        // PATCH 4B: Use ns3::Time for precise timing
        tx.tStage2Send = Simulator::Now();
        tx.stage2SendTime = tx.tStage2Send.GetSeconds();
        tx.stage2SendTimeMs = tx.tStage2Send.GetSeconds() * 1000.0;
        tx.requestedName = canonicalName.toUri();
        // Record Stage-2 Interest bytes for overhead statistics
        tx.stage2InterestBytes += interest->wireEncode().size();
    }
    
    // Schedule timeout for stage-2 fetch
    // Use m_fetchTimeoutMs for timeout scheduling (same as InterestLifetime for consistency)
    // Note: EWMA failure is reported in HandleFetchTimeout, no duplicate counting
    m_fetchTimeouts[canonicalName] = Simulator::Schedule(
        MilliSeconds(m_fetchTimeoutMs),
        &IRouteDiscoveryConsumer::HandleFetchTimeout, this, canonicalName);

    // Send
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    NS_LOG_INFO("Stage-2 Fetch Interest: " << canonicalName << " semVer=" << semVerId
                << " timeout=" << m_fetchTimeoutMs << "ms");
}

void
IRouteDiscoveryConsumer::HandleFetchData(const Data& data)
{
    NS_LOG_FUNCTION(this << data.getName());

    // Report success for EWMA
    auto it = m_pendingFetch.find(data.getName());
    if (it != m_pendingFetch.end()) {
        ++m_stage2Success;
        ReportFetchOutcome(it->second.domainId, true, it->second.semVerId);
        
        // =====================================================================
        // Transaction Logging: Record REAL stage2 receive time and finalize
        // =====================================================================
        uint64_t qId = it->second.queryId;
        auto txIt = m_activeTxs.find(qId);
        
        if (txIt != m_activeTxs.end()) {
            TxRecord& tx = txIt->second;
            // PATCH 4B: Use ns3::Time for precise timing
            tx.tStage2Recv = Simulator::Now();
            tx.stage2RecvTime = tx.tStage2Recv.GetSeconds();
            tx.stage2RecvTimeMs = tx.tStage2Recv.GetSeconds() * 1000.0;
            
            // PATCH 4B: Compute precise RTT from Time difference
            tx.stage2RttMs = (tx.tStage2Recv - tx.tStage2Send).GetSeconds() * 1000.0;
            
            // PATCH 4B: Compute total latency from tStage1Send to tStage2Recv
            if(tx.tStage1Send > Seconds(0))
                tx.totalMs = (tx.tStage2Recv - tx.tStage1Send).GetSeconds() * 1000.0;
            else
                tx.totalMs = (tx.stage2RecvTime - tx.startTime) * 1000.0;

            tx.stage2Success = true;
            tx.finalSuccessDomain = it->second.domainId.toUri();
            
            // Record Stage-2 Data bytes for overhead statistics
            tx.stage2DataBytes += data.wireEncode().size();
            
            // Calculate total control and data plane bytes
            tx.totalControlBytes = tx.stage1InterestBytes + tx.stage1DataBytes;
            tx.totalDataBytes = tx.stage2InterestBytes + tx.stage2DataBytes;
            
            auto hopCountTag = data.getTag<::ndn::lp::HopCountTag>();
            if (hopCountTag) {
                tx.stage2HopCount = static_cast<int32_t>(*hopCountTag);
            } else {
                tx.stage2HopCount = -1;
            }
            
            // Cache hit detection: Only use HopCountTag if available
            // RTT-based inference (< 1ms) is unreliable and removed
            // If hopCount == 0, data was served from local CS (cache hit)
            tx.stage2FromCache = (tx.stage2HopCount == 0);
            
            // Commit transaction to log
            m_transactions.push_back(tx);
            m_activeTxs.erase(qId);
            
            NS_LOG_DEBUG("TxRecord finalized: queryId=" << tx.queryId
                         << " stage1Rtt=" << tx.stage1RttMs << "ms"
                         << " stage2Rtt=" << tx.stage2RttMs << "ms"
                         << " total=" << tx.totalMs << "ms"
                         << " fromCache=" << tx.stage2FromCache
                         << " correct=" << (tx.expectedDomain == tx.finalSuccessDomain)
                         << " ctrlBytes=" << tx.totalControlBytes
                         << " dataBytes=" << tx.totalDataBytes);
        }
        
        // Cancel pending timeout since we got Data
        auto timeoutIt = m_fetchTimeouts.find(data.getName());
        if (timeoutIt != m_fetchTimeouts.end()) {
            Simulator::Cancel(timeoutIt->second);
            m_fetchTimeouts.erase(timeoutIt);
        }
        
        m_pendingFetch.erase(it);
    }

    NS_LOG_INFO("Stage-2 Data received: " << data.getName() 
                << ", size=" << data.getContent().value_size());
}

// =============================================================================
// EWMA Tracking
// =============================================================================

void
IRouteDiscoveryConsumer::ReportFetchOutcome(const Name& domainId, bool success, uint32_t semVerId)
{
    NS_LOG_FUNCTION(this << domainId << success << semVerId);

    // Update RouteManager's EWMA for this domain (version-specific)
    auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
    rm->reportFetchOutcome(domainId, success, semVerId);

    NS_LOG_DEBUG("EWMA updated for " << domainId << " semVer=" << semVerId 
                 << ": " << (success ? "success" : "failure"));
}

// =============================================================================
// Stage-2 Timeout Handling
// =============================================================================

void
IRouteDiscoveryConsumer::HandleFetchTimeout(const Name& canonicalName)
{
    NS_LOG_FUNCTION(this << canonicalName);
    
    NS_LOG_DEBUG("Stage-2 fetch timeout: " << canonicalName);
    
    auto it = m_pendingFetch.find(canonicalName);
    if (it == m_pendingFetch.end()) {
        return;  // Already handled by NACK or Data
    }
    
    ++m_stage2Failure;
    
    // Report failure for EWMA
    ReportFetchOutcome(it->second.domainId, false, it->second.semVerId);
    
    // Finalize TxRecord for stage-2 timeout
    uint64_t qId = it->second.queryId;
    auto txIt = m_activeTxs.find(qId);
    
    if (txIt != m_activeTxs.end()) {
        TxRecord& tx = txIt->second;
        
        // PATCH 4B: Use ns3::Time for precise timing
        tx.tStage2Recv = Simulator::Now();
        tx.stage2RecvTime = tx.tStage2Recv.GetSeconds();
        tx.stage2RecvTimeMs = tx.tStage2Recv.GetSeconds() * 1000.0;
        
        // PATCH 4B: Timeout duration is RTT (computed from Time difference)
        tx.stage2RttMs = (tx.tStage2Recv - tx.tStage2Send).GetSeconds() * 1000.0;
        
        // PATCH 4B: Compute total latency from tStage1Send
        if(tx.tStage1Send > Seconds(0))
            tx.totalMs = (tx.tStage2Recv - tx.tStage1Send).GetSeconds() * 1000.0;
        else
            tx.totalMs = (tx.stage2RecvTime - tx.startTime) * 1000.0;
            
        tx.stage2Success = false;
        tx.finalSuccessDomain = "TIMEOUT";
        tx.failureReason = "STAGE2_TIMEOUT";
        
        m_transactions.push_back(tx);
        m_activeTxs.erase(qId);
        
        NS_LOG_DEBUG("TxRecord finalized (timeout): queryId=" << qId
                     << " selectedDomain=" << tx.selectedDomain);
    }
    
    // Cleanup
    m_pendingFetch.erase(it);
    m_fetchTimeouts.erase(canonicalName);
}

// =============================================================================
// Statistics
// =============================================================================

void
IRouteDiscoveryConsumer::PrintStats() const
{
    std::cout << "=== IRouteDiscoveryConsumer Stats (Node " << m_nodeId << ") ===" << std::endl;
    std::cout << "Discovery Attempts: " << m_discoveryAttempts << std::endl;
    std::cout << "Discovery Found:    " << m_discoveryFound << std::endl;
    std::cout << "Discovery NotFound: " << m_discoveryNotFound << std::endl;
    std::cout << "Stage-2 Success:    " << m_stage2Success << std::endl;
    std::cout << "Stage-2 Failure:    " << m_stage2Failure << std::endl;
    
    if (m_discoveryAttempts > 0) {
        double successRate = static_cast<double>(m_stage2Success) / m_discoveryAttempts;
        std::cout << "Overall Success Rate: " << (successRate * 100.0) << "%" << std::endl;
    }
    std::cout << "Transactions logged: " << m_transactions.size() << std::endl;
    std::cout << "=================================================" << std::endl;
}

// =============================================================================
// Query Trace & Transaction Logging
// =============================================================================

void
IRouteDiscoveryConsumer::SetQueryTrace(const std::vector<QueryItem>& queries)
{
    NS_LOG_FUNCTION(this << queries.size());
    std::cout << "SetQueryTrace size=" << queries.size() << " at " << Simulator::Now().GetSeconds() << std::endl;
    m_queryTrace = queries;
    m_queryPosById.clear();
    for (size_t i = 0; i < m_queryTrace.size(); ++i) {
        m_queryPosById[m_queryTrace[i].id] = i;
    }
    m_queryIndex = 0;
    m_transactions.clear();
    m_transactions.reserve(queries.size());
    NS_LOG_INFO("Query trace set with " << queries.size() << " items");
}

void
IRouteDiscoveryConsumer::ExportToCsv(const std::string& filename) const
{
    NS_LOG_FUNCTION(this << filename);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Failed to open CSV file: " << filename);
        return;
    }
    
    // Write header
    file << TxRecord::csvHeader() << "\n";
    
    // Write transactions
    for (const auto& tx : m_transactions) {
        file << tx.toCsvLine() << "\n";
    }
    
    file.close();
    NS_LOG_INFO("Exported " << m_transactions.size() << " transactions to " << filename);
}

// =============================================================================
// Stage-1 Timeout
// =============================================================================
// Stage-1 Timeout
// =============================================================================

void
IRouteDiscoveryConsumer::HandleDiscoveryTimeout(const std::string& discNameUri)
{
    NS_LOG_FUNCTION(this << discNameUri);
    
    // Check if valid (might have been handled by Data arrival)
    auto it = m_discoveryTimeoutsByName.find(discNameUri);
    if (it == m_discoveryTimeoutsByName.end()) {
        return; // Already handled
    }
    m_discoveryTimeoutsByName.erase(it);
    
    // Find QueryId
    auto qIt = m_discNameToQueryId.find(discNameUri);
    if (qIt == m_discNameToQueryId.end()) {
        NS_LOG_WARN("Discovery Timeout for unknown name: " << discNameUri);
        return;
    }
    uint64_t queryId = qIt->second;
    
    // Clean up maps
    m_discNameToQueryId.erase(qIt);
    m_discNameToDomain.erase(discNameUri);
    
    NS_LOG_DEBUG("Discovery Timeout for queryId=" << queryId);
    
    // Treat as probe failure
    if (m_pendingProbes > 0) {
        --m_pendingProbes;
    }
    
    // PATCH: When all probes done, use FinalizeDiscoveryRound
    if (m_pendingProbes == 0) {
        FinalizeDiscoveryRound(queryId);
    }
}

} // namespace ndn
} // namespace ns3
