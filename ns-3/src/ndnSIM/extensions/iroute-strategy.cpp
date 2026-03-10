/**
 * @file iroute-strategy.cpp
 * @brief Implementation of the SemanticStrategy forwarding strategy.
 *
 * This file implements the semantic-based forwarding logic for the iRoute
 * protocol. It decodes semantic vectors from Interest packets and uses
 * the RouteManager to find optimal forwarding paths.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see iroute-strategy.hpp for class declaration
 * @see Design_Guide.md Section 2.4 for design specifications
 */

#include "iroute-strategy.hpp"
#include "iroute-manager.hpp"
#include "iroute-vector.hpp"
#include "iroute-route-manager-registry.hpp"

#include "ns3/simulator.h"

#include "table/fib.hpp"
#include "common/logger.hpp"

#include <ndn-cxx/lp/tags.hpp>

namespace nfd {
namespace fw {

NFD_LOG_INIT(SemanticStrategy);

NFD_REGISTER_STRATEGY(SemanticStrategy);

// =============================================================================
// Strategy Registration
// =============================================================================

const Name&
SemanticStrategy::getStrategyName()
{
    static Name strategyName("/localhost/nfd/strategy/semantic/%FD%01");
    return strategyName;
}

// =============================================================================
// Static Member Definitions
// =============================================================================

uint64_t SemanticStrategy::s_totalInterestsSent = 0;
uint64_t SemanticStrategy::s_redundantInterests = 0;
uint64_t SemanticStrategy::s_pitPeakSize = 0;
uint64_t SemanticStrategy::s_totalHopCount = 0;
uint64_t SemanticStrategy::s_forwardedInterestCount = 0;
uint64_t SemanticStrategy::s_totalBytesSent = 0;
uint64_t SemanticStrategy::s_totalDataBytesReceived = 0;

// =============================================================================
// Ingress Detection Prefixes (Paper-Compliant)
// =============================================================================

// Ingress query trigger: Consumer sends Interest with this prefix to trigger
// semantic ranking at the ingress router. This is the "entry point" for iRoute.
static const ndn::Name kIngressQueryPrefix("/iroute/query");

// Discovery Interest prefix component: inserted after DomainID
// Full format: /<DomainID>/iroute/disc/<SemVerID>/<nonce>
static const ndn::name::Component kDiscoveryComponent("iroute");
static const ndn::name::Component kDiscComponent("disc");

// =============================================================================
// Constructor
// =============================================================================

SemanticStrategy::SemanticStrategy(Forwarder& forwarder, const Name& name)
    : Strategy(forwarder)
    , m_topK(1)       // Forward to single best match for clean PIT path
    , m_alpha(1.0)    // Weight for semantic similarity (default: pure semantic)
    , m_beta(0.0)     // Weight for cost (default: disabled)
    , m_vectorDim(384) // Default vector dimension
    , m_enablePenalty(false)  // Penalty disabled by default
    , m_penaltyGamma(0.9)     // Decay factor
    , m_penaltyStep(0.1)      // Penalty step size
    , m_forwarderRef(forwarder)
{
    ParsedInstanceName parsed = parseInstanceName(name);
    
    // Parse alpha/beta parameters from strategy name
    // Format: /localhost/nfd/strategy/semantic/%FD%01/alpha~0.7/beta~0.3
    for (const auto& param : parsed.parameters) {
        std::string paramStr = param.toUri();
        if (paramStr.find("alpha~") == 0) {
            try {
                m_alpha = std::stod(paramStr.substr(6));
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid alpha value: " + paramStr));
            }
        } else if (paramStr.find("beta~") == 0) {
            try {
                m_beta = std::stod(paramStr.substr(5));
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid beta value: " + paramStr));
            }
        } else if (paramStr.find("topk~") == 0) {
            try {
                m_topK = std::stoul(paramStr.substr(5));
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid topK value: " + paramStr));
            }
        } else if (paramStr.find("vectordim~") == 0) {
            try {
                m_vectorDim = std::stoul(paramStr.substr(10));
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid vectorDim value: " + paramStr));
            }
        } else if (paramStr.find("penalty~") == 0) {
            // Parse penalty mode: penalty~1 or penalty~0
            try {
                m_enablePenalty = (std::stoul(paramStr.substr(8)) != 0);
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid penalty value: " + paramStr));
            }
        } else if (paramStr.find("gamma~") == 0) {
            try {
                m_penaltyGamma = std::stod(paramStr.substr(6));
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid gamma value: " + paramStr));
            }
        } else if (paramStr.find("step~") == 0) {
            try {
                m_penaltyStep = std::stod(paramStr.substr(5));
            } catch (...) {
                NDN_THROW(std::invalid_argument("Invalid step value: " + paramStr));
            }
        } else {
            NDN_THROW(std::invalid_argument("Unknown parameter: " + paramStr));
        }
    }
    
    // Ensure alpha + beta <= 1.0 for normalized scoring
    if (m_alpha + m_beta > 1.0 + 1e-6) {
        NFD_LOG_WARN("alpha + beta > 1.0, normalizing weights");
        double sum = m_alpha + m_beta;
        m_alpha /= sum;
        m_beta /= sum;
    }
    
    if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion()) {
        NDN_THROW(std::invalid_argument(
            "SemanticStrategy does not support version " + to_string(*parsed.version)));
    }
    this->setInstanceName(makeInstanceName(name, getStrategyName()));

    NFD_LOG_INFO("SemanticStrategy initialized: topK=" << m_topK
                 << ", alpha=" << m_alpha << ", beta=" << m_beta
                 << ", vectorDim=" << m_vectorDim
                 << ", penalty=" << (m_enablePenalty ? "ON" : "OFF")
                 << ", gamma=" << m_penaltyGamma
                 << ", step=" << m_penaltyStep);
}

SemanticStrategy::~SemanticStrategy() = default;

// =============================================================================
// Interest Forwarding
// =============================================================================

void
SemanticStrategy::afterReceiveInterest(const Interest& interest,
                                        const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry)
{
    NFD_LOG_DEBUG("afterReceiveInterest: " << interest.getName()
                  << " from face=" << ingress.face.getId());

    // =========================================================================
    // PAPER-COMPLIANT INGRESS-ONLY SEMANTIC ROUTING
    // =========================================================================
    // Per paper Section 4.2:
    // - Ingress router: Performs semantic ranking + bounded probing via IRouteApp
    // - Core router: Standard FIB forwarding (semantic-agnostic)
    //
    // Strategy only detects /iroute/query to offload it to IRouteApp.
    // =========================================================================

    bool isIngressQuery = kIngressQueryPrefix.isPrefixOf(interest.getName());
    
    // P0-2 FIX: Do NOT intercept /iroute/query in strategy!
    // Let normal FIB forwarding deliver it to IRouteApp::HandleQueryInterest.
    // IRouteApp returns DiscoveryReply Data with proper PIT matching.
    
    // For /iroute/query: forward ONLY to LOCAL face (IRouteApp)
    // CRITICAL: /iroute/query is an ingress-local API and MUST NOT leave the node
    if (isIngressQuery) {
        NFD_LOG_DEBUG("Forwarding /iroute/query to local IRouteApp ONLY");
        const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
        
        // Only forward to LOCAL faces - never to network
        for (const auto& nexthop : fibEntry.getNextHops()) {
            Face& face = nexthop.getFace();
            if (face.getScope() == ndn::nfd::FACE_SCOPE_LOCAL) {
                this->sendInterest(interest, face, pitEntry);
                NFD_LOG_INFO("/iroute/query delivered to local app face=" << face.getId());
                return;
            }
        }
        
        // No local face found - reject, do NOT forward to network
        NFD_LOG_WARN("/iroute/query: no local face registered, rejecting");
        this->rejectPendingInterest(pitEntry);
        return;
    }
    
    // =========================================================================
    // STANDARD FORWARDING (Semantic-Agnostic)
    // =========================================================================
    // For all other Interests, perform standard Best-Route forwarding.
    // This enforces "Ingress-Only Semantics" - core routers do not decode vectors.
    
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    
    // 1. Check for local Producer first
    for (const auto& nexthop : fibEntry.getNextHops()) {
        Face& face = nexthop.getFace();
        if (face.getScope() == ndn::nfd::FACE_SCOPE_LOCAL) {
            NFD_LOG_INFO("Destination reached: " << interest.getName());
            this->sendInterest(interest, face, pitEntry);
            return;
        }
    }
    
    // 2. Forward to best next-hop (lowest cost)
    for (const auto& nexthop : fibEntry.getNextHops()) {
        Face& face = nexthop.getFace();
        
        // Loop prevention: don't send back to ingress
        if (face.getId() == ingress.face.getId()) {
            continue;
        }
        
        // Don't send to local faces (unless it was a local producer, handled above)
        if (face.getScope() == ndn::nfd::FACE_SCOPE_LOCAL) {
            continue;
        }

        NFD_LOG_DEBUG("Forwarding Interest: " << interest.getName() << " via face " << face.getId());
        this->sendInterest(interest, face, pitEntry);
        ++s_totalInterestsSent;
        return;
    }
    
    // No valid next hop found
    NFD_LOG_DEBUG("No valid outgoing face for " << interest.getName());
    this->rejectPendingInterest(pitEntry);
}


void
SemanticStrategy::afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                                   const shared_ptr<pit::Entry>& pitEntry)
{
    // Instrumentation: Track Data overhead
    // Data packet size is typically wire size.
    s_totalDataBytesReceived += data.wireEncode().size();
    
    // Penalty: Decrease penalty for successful face
    if (m_enablePenalty) {
        decreasePenalty(ingress.face.getId());
    }
    
    // Standard processing
    this->beforeSatisfyInterest(data, ingress, pitEntry);
}

void
SemanticStrategy::afterReceiveNack(const lp::Nack& nack, const FaceEndpoint& ingress,
                                   const shared_ptr<pit::Entry>& pitEntry)
{
    // Penalty: Increase penalty for face that returned Nack
    if (m_enablePenalty) {
        increasePenalty(ingress.face.getId());
        NFD_LOG_DEBUG("Nack from face " << ingress.face.getId() 
                      << ", penalty now " << getFacePenalty(ingress.face.getId()));
    }
    
    // Let NFD's default Nack handling proceed
    // The base Strategy class will handle forwarding to other faces if available
}

void
SemanticStrategy::increasePenalty(FaceId faceId)
{
    double& penalty = m_facePenalty[faceId];
    penalty = std::min(1.0, penalty + m_penaltyStep);
}

void
SemanticStrategy::decreasePenalty(FaceId faceId)
{
    auto it = m_facePenalty.find(faceId);
    if (it != m_facePenalty.end()) {
        it->second *= m_penaltyGamma;
        if (it->second < 0.01) {
            m_facePenalty.erase(it);  // Remove negligible penalty
        }
    }
}

} // namespace fw
} // namespace nfd
