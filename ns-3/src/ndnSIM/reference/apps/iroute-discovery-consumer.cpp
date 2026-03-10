/**
 * @file iroute-discovery-consumer.cpp
 * @brief Implementation of two-stage discovery consumer.
 *
 * @author iRoute Team
 * @date 2024
 */

#include "iroute-discovery-consumer.hpp"
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
                      MakeUintegerChecker<uint32_t>(100));

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
    , m_nonce(0)
    , m_discoveryAttempts(0)
    , m_discoveryFound(0)
    , m_discoveryNotFound(0)
    , m_stage2Success(0)
    , m_stage2Failure(0)
    , m_queryIndex(0)
{
    NS_LOG_FUNCTION(this);
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
    if (!m_queryTrace.empty()) {
        if (m_queryIndex >= m_queryTrace.size()) {
            NS_LOG_INFO("Query trace exhausted, stopping discovery");
            Simulator::Cancel(m_sendEvent); // Cancel future events if done
            return;  // All queries processed
        }
        
        const auto& query = m_queryTrace[m_queryIndex];
        m_queryVector = query.vector;
        if (!m_queryVector.isNormalized()) {
            m_queryVector.normalize();
        }
        
        // Initialize TxRecord for this query with REAL timestamp
        TxRecord tx;
        tx.queryId = m_queryIndex;
        tx.startTime = Simulator::Now().GetSeconds();
        tx.expectedDomain = query.expectedDomain;
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
                     << " expected=" << tx.expectedDomain 
                     << " objectId=" << query.objectId);
        ++m_queryIndex;
    }

    if (m_queryVector.empty()) {
        NS_LOG_WARN("No query vector set, skipping discovery");
        return;
    }
    
    // Start Discovery
    // Use (m_queryIndex - 1) as queryId if trace is used, else 0?
    // If trace used, m_queryIndex was incremented.
    uint64_t qId = (m_queryIndex > 0) ? (m_queryIndex - 1) : 0;
    DoDiscoveryAttempt(m_semVerId, qId);
}

void
IRouteDiscoveryConsumer::DoDiscoveryAttempt(uint32_t semVerId, uint64_t queryId)
{
    NS_LOG_FUNCTION(this << semVerId << queryId);
    
    // Retrieve Query Vector
    if (queryId >= m_queryTrace.size()) return;
    const auto& query = m_queryTrace[queryId];
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
    
    tx.numStage1Attempts++;
    if (semVerId == prevVer && semVerId != activeVer) {
        tx.fallbackUsed = true;
        tx.usedPrevIndex = true;
    }

    // Query RouteManager for this version
    auto domains = rm->findBestDomainsV2(qVec, m_kMax, semVerId);

    // Filter by τ (applied to confidence, not score)
    // confidence = pure semantic similarity (q·C·gate), without cost penalty
    // score = confidence * wTerm - β * costPenalty (may be negative in star topology)
    std::vector<iroute::DomainResult> candidates;
    for (const auto& d : domains) {
        if (d.confidence >= m_tau) {
            candidates.push_back(d);
        }
    }
    
    // Soft fallback: even if all below τ, still probe the best candidate
    // (prevents "all fail" when embeddings are low-calibrated)
    if (candidates.empty() && !domains.empty()) {
        NS_LOG_DEBUG("All " << domains.size() << " domains below τ=" << m_tau 
                     << ", using soft fallback to probe top candidate");
        candidates.push_back(domains.front());
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
    size_t k = std::min(candidates.size(), static_cast<size_t>(m_kMax));
    
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

    // Build Discovery Interest name: /<DomainID>/iroute/disc/<SemVerID>/<nonce>
    Name discName(domainId);
    discName.append("iroute");
    discName.append("disc");
    discName.appendNumber(semVerId);
    discName.appendNumber(m_nonce);

    auto interest = std::make_shared<Interest>(discName);
    interest->setCanBePrefix(true);
    interest->setMustBeFresh(true);
    interest->setInterestLifetime(ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));

    try {
        ::ndn::Block vectorBlock = vec.wireEncode();
        interest->setApplicationParameters(vectorBlock);
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to encode query vector: " << e.what());
        return;
    }

    // Map Nonce -> QueryId
    m_nonceToQueryId[m_nonce] = queryId;
    ++m_nonce;
    ++m_discoveryAttempts;
    
    // Log
    auto it = m_activeTxs.find(queryId);
    if (it != m_activeTxs.end()) {
        TxRecord& tx = it->second;
        tx.stage1SendTime = Simulator::Now().GetSeconds();
        tx.stage1SendTimeMs = Simulator::Now().GetMilliSeconds();
        tx.semVerId = semVerId;
        ++tx.probesUsed;
    }

    // Send Interest
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);

    Time timeout = m_interestLifetime + MilliSeconds(10);
    m_discoveryTimeouts[m_nonce-1] = Simulator::Schedule(timeout, 
        &IRouteDiscoveryConsumer::HandleDiscoveryTimeout, this, m_nonce-1); // Note: nonce was incremented

    NS_LOG_INFO("Sent Discovery Interest: " << discName << " nonce=" << (m_nonce-1));
}

// =============================================================================
// NDN Callbacks
// =============================================================================



void
IRouteDiscoveryConsumer::OnNack(shared_ptr<const lp::Nack> nack)
{
    NS_LOG_FUNCTION(this);
    App::OnNack(nack);

    // Extract nonce from Interest name to find domain
    const Name& name = nack->getInterest().getName();
    
    // Name: /<DomainID>/iroute/disc/<SemVerID>/<nonce>
    // Component order: ... iroute(i) disc(i+1) SemVer(i+2) nonce(i+3)
    // We scan for "iroute"/"disc", then extract nonce at i+3
    // But we need to find "iroute" first.
    
    uint64_t nonce = 0;
    bool isDiscovery = false;
    for (size_t i = 0; i + 3 < name.size(); ++i) {
        if (name.get(i).toUri() == "iroute" && name.get(i+1).toUri() == "disc") {
            isDiscovery = true;
            nonce = name.get(i+3).toNumber();
            break;
        }
    }

    if (isDiscovery) {
        // Cancel timeout
        auto it = m_discoveryTimeouts.find(nonce);
        if (it != m_discoveryTimeouts.end()) {
            Simulator::Cancel(it->second);
            m_discoveryTimeouts.erase(it);
        }
        
        NS_LOG_DEBUG("Discovery NACK received for nonce=" << nonce);
        
        // Find QueryId
        uint64_t queryId = 0;
        auto qIt = m_nonceToQueryId.find(nonce);
        if (qIt != m_nonceToQueryId.end()) queryId = qIt->second;
        else return; // Should not happen
        
        auto txIt = m_activeTxs.find(queryId);
        
        // Decrement pending probes
        if (m_pendingProbes > 0) {
            --m_pendingProbes;
        }
        
        // If all probes returned and none found
        if (m_pendingProbes == 0 && !m_foundCandidate) {
            auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
            uint32_t activeVer = rm->getActiveSemVerId();
            uint32_t prevVer = rm->getPrevSemVerId();
            
            if (m_attemptSemVer == activeVer && prevVer != activeVer && prevVer != 0) {
                 NS_LOG_INFO("All probes for v" << activeVer << " failed. Falling back to v" << prevVer);
                 // Simulate small delay for decision
                 Simulator::Schedule(MilliSeconds(2), 
                                     &IRouteDiscoveryConsumer::DoDiscoveryAttempt, 
                                     this, prevVer, queryId);
            } else {
                // Finalize TxRecord for discovery failure
                if (txIt != m_activeTxs.end()) {
                    TxRecord& tx = txIt->second;
                    tx.stage1Success = false;
                    tx.stage2Success = false;
                    tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
                    m_transactions.push_back(tx);
                    m_activeTxs.erase(queryId);
                }
                ++m_discoveryNotFound;
            }
        }
    } else {
        ++m_stage2Failure;
        // ... (rest of stage2 failure logic) ...
        auto it = m_pendingFetch.find(name);
        if (it != m_pendingFetch.end()) {
            ReportFetchOutcome(it->second.domainId, false, it->second.semVerId);
            
            // Finalize TxRecord for stage-2 failure
            uint64_t qId = it->second.queryId;
            auto txIt = m_activeTxs.find(qId);
            
            if (txIt != m_activeTxs.end()) {
                TxRecord& tx = txIt->second;
                tx.stage2RecvTime = Simulator::Now().GetSeconds();
                tx.stage2RttMs = (tx.stage2RecvTime - tx.stage2SendTime) * 1000.0;
                tx.totalMs = (tx.stage2RecvTime - tx.startTime) * 1000.0;
                tx.stage2Success = false;
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
    
    // Check if this is a Discovery Reply
    // Format: /<DomainID>/iroute/disc/<SemVerID>/[Digest]
    bool isDiscovery = false;
    Name domainId;
    uint32_t semVerId = 0;

    // Linear scan for "iroute" component to handle variable length DomainID
    // We expect "iroute", "disc" OR "reply"
    for (size_t i = 0; i + 2 < name.size(); ++i) {
        if (name.get(i) == IRouteDiscoveryConsumer::kDiscoveryPrefix.get(0)) { // iroute
            if (name.get(i+1) == ::ndn::name::Component::fromEscapedString("reply")) {
                // /<Ingress>/iroute/reply/<nonce>/<SemVerID>
                isDiscovery = true;
                // Parse nonce at i+2?
                // Name format from User Rules: /<Ingress>/iroute/reply/<nonce>/<SemVerID>
                // We should use the nonce to find original target domain if needed
                // But getting SemVerId is useful
                if (i+3 < name.size()) {
                    semVerId = static_cast<uint32_t>(name.get(i+3).toNumber());
                }
                
                // Get nonce to lookup pending discovery
                // If nonce is component i+2
                uint64_t nonce = 0;
                if (i+2 < name.size()) {
                    nonce = name.get(i+2).toNumber();
                    // Cancel timeout
                    auto it = m_discoveryTimeouts.find(nonce);
                    if (it != m_discoveryTimeouts.end()) {
                        Simulator::Cancel(it->second);
                        m_discoveryTimeouts.erase(it);
                    }
                }
                
                // Note: domainId from Name prefix is INGRESS domain, not target.
                // We shouldn't use it for "SelectedDomain" stats directly.
                // We will rely on TLV content or map lookup.
                break;
            }
            else if (name.get(i+1) == IRouteDiscoveryConsumer::kDiscoveryPrefix.get(1)) { // disc
                // Should not happen for Data? Unless echoed?
                isDiscovery = true;
                semVerId = static_cast<uint32_t>(name.get(i+2).toNumber());
                domainId = name.getPrefix(i);
                break;
            }
        }
    }

    if (!isDiscovery) {
        // Might be a Stage-2 Data?
        HandleFetchData(*data);
        return;
    }

    // It's a Discovery Reply
    if (m_pendingProbes > 0) --m_pendingProbes;
    
    // Check if this reply is relevant to current attempt
    // (Though nonces prevent stale replies usually)
    
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
        NS_LOG_DEBUG("Decoded Reply: found=" << reply.found << " conf=" << reply.confidence);
        
        // Find QueryId via Nonce (we need nonce from name)
        uint64_t nonce = 0;
        uint64_t queryId = 0;
        // Re-extract nonce from name (we did it before, but need it here)
        // Name format: /<Ingress>/iroute/reply/<nonce>/<SemVerID>
        bool foundNonce = false;
        for (size_t i = 0; i + 2 < name.size(); ++i) {
             if (name.get(i) == IRouteDiscoveryConsumer::kDiscoveryPrefix.get(0) && // iroute
                 name.get(i+1) == ::ndn::name::Component::fromEscapedString("reply")) {
                 if (i+2 < name.size()) {
                     nonce = name.get(i+2).toNumber();
                     foundNonce = true;
                     break;
                 }
             }
        }
        
        if (foundNonce) {
            auto qIt = m_nonceToQueryId.find(nonce);
            if (qIt != m_nonceToQueryId.end()) queryId = qIt->second;
        }
        
        if (reply.found && !m_foundCandidate) {
            // First success in this attempt wins
            m_foundCandidate = true;
            success = true;
            
            ++m_discoveryFound;
            NS_LOG_INFO("Discovery Success from: " << domainId << " semVer=" << semVerId);

             // Record Stats
            auto txIt = m_activeTxs.find(queryId);
            if (txIt != m_activeTxs.end()) {
                 TxRecord& tx = txIt->second;
                 tx.stage1RecvTime = Simulator::Now().GetSeconds();
                 tx.stage1RecvTimeMs = Simulator::Now().GetMilliSeconds();
                 
                 if (reply.canonicalName.size() > 0) {
                     tx.selectedDomain = reply.canonicalName.getPrefix(1).toUri(); 
                 } else {
                     tx.selectedDomain = domainId.toUri(); 
                 }
                 
                 tx.stage1RttMs = tx.stage1RecvTimeMs - tx.stage1SendTimeMs;
                 tx.stage1Success = true;
                 tx.confidence = reply.confidence;
            }
            
            // Proceed to Stage 2 with Object-Level Granularity (PR-2)
            Name fullFetchName = reply.canonicalName;
            // Get objectId from trace if available using queryId
            if (queryId < m_queryTrace.size()) {
                 uint32_t objId = m_queryTrace[queryId].objectId;
                 fullFetchName.append("o");
                 fullFetchName.appendNumber(objId);
            } else {
                 fullFetchName.append("o");
                 fullFetchName.appendNumber(0);
            }
            
            SendFetchInterest(fullFetchName, domainId, semVerId, queryId);
        }
    }

    // Check Fallback Trigger
    // If not successful AND all probes done
    if (!m_foundCandidate && m_pendingProbes == 0) {
        
        // Find QueryId for fallback (same as above)
        uint64_t nonce = 0;
        uint64_t queryId = 0;
        // ... (reuse extraction - simplified for brevity, assume we have it or re-extract)
        // I should have structured this better. Let's re-find nonce if I put logic inside `if`.
        // Better: extract nonce/queryId at top of function.
        // But let's just do it here for safety.
        bool foundNonce = false;
        for (size_t i = 0; i + 2 < name.size(); ++i) {
             if (name.get(i) == IRouteDiscoveryConsumer::kDiscoveryPrefix.get(0) &&
                 name.get(i+1) == ::ndn::name::Component::fromEscapedString("reply") &&
                 i+2 < name.size()) {
                     nonce = name.get(i+2).toNumber();
                     foundNonce = true;
                     break;
             }
        }
        if (foundNonce) {
            auto qIt = m_nonceToQueryId.find(nonce);
            if (qIt != m_nonceToQueryId.end()) queryId = qIt->second;
        }

        auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
        uint32_t activeVer = rm->getActiveSemVerId();
        uint32_t prevVer = rm->getPrevSemVerId();
        
        if (m_attemptSemVer == activeVer && prevVer != activeVer && prevVer != 0) {
             NS_LOG_INFO("All probes for v" << activeVer << " returned negative. Falling back to v" << prevVer);
             Simulator::Schedule(MilliSeconds(2), 
                                 &IRouteDiscoveryConsumer::DoDiscoveryAttempt, 
                                 this, prevVer, queryId);
        } else {
             NS_LOG_INFO("Discovery attempt failed (no fallback available)");
             // Finalize TxRecord
             auto txIt = m_activeTxs.find(queryId);
             if (txIt != m_activeTxs.end()) {
                 TxRecord& tx = txIt->second;
                 tx.stage1Success = false;
                 tx.stage2Success = false;
                 tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
                 m_transactions.push_back(tx);
                 m_activeTxs.erase(queryId);
             }
             ++m_discoveryNotFound;
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
    
    // Log
    auto it = m_activeTxs.find(queryId);
    if (it != m_activeTxs.end()) {
        TxRecord& tx = it->second;
        tx.stage2SendTime = Simulator::Now().GetSeconds();
        tx.stage2SendTimeMs = Simulator::Now().GetMilliSeconds();
        tx.requestedName = canonicalName.toUri();
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

    ++m_stage2Success;

    // Report success for EWMA
    auto it = m_pendingFetch.find(data.getName());
    if (it != m_pendingFetch.end()) {
        ReportFetchOutcome(it->second.domainId, true, it->second.semVerId);
        
        // =====================================================================
        // Transaction Logging: Record REAL stage2 receive time and finalize
        // =====================================================================
        uint64_t qId = it->second.queryId;
        auto txIt = m_activeTxs.find(qId);
        
        if (txIt != m_activeTxs.end()) {
            TxRecord& tx = txIt->second;
            tx.stage2RecvTime = Simulator::Now().GetSeconds();
            tx.stage2RecvTimeMs = Simulator::Now().GetMilliSeconds();
            
            tx.stage2RttMs = tx.stage2RecvTimeMs - tx.stage2SendTimeMs;
            
            if(tx.stage1SendTimeMs > 0)
                tx.totalMs = tx.stage2RecvTimeMs - tx.stage1SendTimeMs;
            else
                tx.totalMs = (tx.stage2RecvTime - tx.startTime) * 1000.0;

            tx.stage2Success = true;
            tx.finalSuccessDomain = it->second.domainId.toUri();
            
            auto hopCountTag = data.getTag<::ndn::lp::HopCountTag>();
            if (hopCountTag) {
                tx.stage2HopCount = static_cast<int32_t>(*hopCountTag);
            } else {
                tx.stage2HopCount = -1;
            }
            
            tx.stage2FromCache = (tx.stage2RttMs < 1.0);
            
            // Commit transaction to log
            m_transactions.push_back(tx);
            m_activeTxs.erase(qId);
            
            NS_LOG_DEBUG("TxRecord finalized: queryId=" << tx.queryId
                         << " stage1Rtt=" << tx.stage1RttMs << "ms"
                         << " stage2Rtt=" << tx.stage2RttMs << "ms"
                         << " total=" << tx.totalMs << "ms"
                         << " fromCache=" << tx.stage2FromCache
                         << " correct=" << (tx.expectedDomain == tx.finalSuccessDomain));
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
        
        tx.stage2RecvTime = Simulator::Now().GetSeconds();
        tx.stage2RecvTimeMs = Simulator::Now().GetMilliSeconds();
        
        // Timeout duration is RTT
        tx.stage2RttMs = tx.stage2RecvTimeMs - tx.stage2SendTimeMs;
        
        if(tx.stage1SendTimeMs > 0)
            tx.totalMs = tx.stage2RecvTimeMs - tx.stage1SendTimeMs;
        else
            tx.totalMs = (tx.stage2RecvTime - tx.startTime) * 1000.0;
            
        tx.stage2Success = false;
        tx.finalSuccessDomain = "TIMEOUT";
        
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
IRouteDiscoveryConsumer::HandleDiscoveryTimeout(uint64_t nonce)
{
    NS_LOG_FUNCTION(this << nonce);
    
    // Check if valid
    auto it = m_discoveryTimeouts.find(nonce);
    if (it == m_discoveryTimeouts.end()) {
        return; // Already handled
    }
    m_discoveryTimeouts.erase(it);
    
    // Find QueryId
    auto qIt = m_nonceToQueryId.find(nonce);
    if (qIt == m_nonceToQueryId.end()) {
        NS_LOG_WARN("Discovery Timeout for unknown nonce=" << nonce);
        return;
    }
    uint64_t queryId = qIt->second;
    // Cleanup nonce mapping? Maybe keep for robustness or clean later.
    // m_nonceToQueryId.erase(qIt); // Keep until tx finalized?
    
    // Find Tx
    auto txIt = m_activeTxs.find(queryId);
    if (txIt == m_activeTxs.end()) return;
    TxRecord& tx = txIt->second;

    NS_LOG_DEBUG("Discovery Timeout for queryId=" << queryId << " nonce=" << nonce);
    
    // Treat as probe failure
    if (m_pendingProbes > 0) {
        --m_pendingProbes;
    }
    
    // If all probes processed and no success
    if (m_pendingProbes == 0 && !m_foundCandidate) {
        auto rm = iroute::RouteManagerRegistry::getOrCreate(m_nodeId, m_vectorDim);
        uint32_t activeVer = rm->getActiveSemVerId();
        uint32_t prevVer = rm->getPrevSemVerId();
        
        if (m_attemptSemVer == activeVer && prevVer != activeVer && prevVer != 0) {
             NS_LOG_INFO("All probes for v" << activeVer << " failed/timedout. Falling back to v" << prevVer);
             Simulator::Schedule(MilliSeconds(2), 
                                 &IRouteDiscoveryConsumer::DoDiscoveryAttempt, 
                                 this, prevVer, queryId);
        } else {
            // Finalize TxRecord
            tx.stage1Success = false;
            tx.stage2Success = false;
            tx.totalMs = (Simulator::Now().GetSeconds() - tx.startTime) * 1000.0;
            tx.finalSuccessDomain = "DISC_TIMEOUT";
            m_transactions.push_back(tx);
            m_activeTxs.erase(queryId);
            
            ++m_discoveryNotFound;
        }
    }
}

} // namespace ndn
} // namespace ns3
