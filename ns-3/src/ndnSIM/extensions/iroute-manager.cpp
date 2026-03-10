/**
 * @file iroute-manager.cpp
 * @brief Implementation of the RouteManager class.
 *
 * This file contains the implementation of all RouteManager methods including
 * route updates and semantic-based next-hop lookup.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see iroute-manager.hpp for class declaration
 * @see Design_Guide.md Section 2.2 for design specifications
 */

#include "iroute-manager.hpp"
#include "iroute-route-manager-registry.hpp"

#include "ns3/simulator.h"

#include "common/logger.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace iroute {

NFD_LOG_INIT(iRoute.Manager);

// =============================================================================
// Constructor
// =============================================================================

RouteManager::RouteManager(size_t vectorDim)
    : m_vectorDim(vectorDim)
    , m_maxCost(0.0)
    , m_lastNewPrefixTime(0.0)
{
    NFD_LOG_DEBUG("RouteManager created with vectorDim=" << m_vectorDim);
}

// =============================================================================
// Dimension Configuration
// =============================================================================

void
RouteManager::setVectorDim(size_t dim)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_vectorDim = dim;
    NFD_LOG_DEBUG("setVectorDim: vectorDim=" << m_vectorDim);
}

size_t
RouteManager::getVectorDim() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_vectorDim;
}

// =============================================================================
// Route Management
// =============================================================================

void
RouteManager::updateRoute(const ndn::Name& prefix,
                           const SemanticVector& centroid,
                           double cost,
                           const std::string& originRouter)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    NFD_LOG_DEBUG("updateRoute: prefix=" << prefix 
                  << ", cost=" << cost
                  << ", originRouter=" << originRouter
                  << ", vectorDim=" << centroid.getDimension());

    // Update max cost for normalization
    if (cost > m_maxCost) {
        m_maxCost = cost;
        NFD_LOG_DEBUG("updateRoute: Updated maxCost to " << m_maxCost);
    }

    // Search for existing entry with the same prefix
    auto it = std::find_if(m_rib.begin(), m_rib.end(),
                           [&prefix](const RibEntry& entry) {
                               return entry.prefix == prefix;
                           });

    if (it != m_rib.end()) {
        // Update existing entry
        it->centroid = centroid;
        it->cost = cost;
        it->originRouter = originRouter;
        it->originRouterName = originRouter.empty() ? ndn::Name() : ndn::Name("/" + originRouter);
        NFD_LOG_INFO("updateRoute: Updated existing route for " << prefix);
    }
    else {
        // Insert new entry - record the time for convergence tracking
        RibEntry entry(prefix, centroid, cost, originRouter);
        m_rib.push_back(entry);
        m_lastNewPrefixTime = ns3::Simulator::Now().GetSeconds();
        NFD_LOG_INFO("updateRoute: Added new route for " << prefix 
                     << ", RIB size now: " << m_rib.size()
                     << ", time: " << m_lastNewPrefixTime << "s");
    }
}

bool
RouteManager::removeRoute(const ndn::Name& prefix)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_rib.begin(), m_rib.end(),
                           [&prefix](const RibEntry& entry) {
                               return entry.prefix == prefix;
                           });

    if (it != m_rib.end()) {
        m_rib.erase(it);
        NFD_LOG_INFO("removeRoute: Removed route for " << prefix 
                     << ", RIB size now: " << m_rib.size());
        return true;
    }

    NFD_LOG_DEBUG("removeRoute: Route not found for " << prefix);
    return false;
}

size_t
RouteManager::removeRoutesByOrigin(const std::string& originRouter)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t originalSize = m_rib.size();
    
    m_rib.erase(
        std::remove_if(m_rib.begin(), m_rib.end(),
                       [&originRouter](const RibEntry& entry) {
                           return entry.originRouter == originRouter;
                       }),
        m_rib.end());

    size_t removed = originalSize - m_rib.size();
    
    if (removed > 0) {
        NFD_LOG_INFO("removeRoutesByOrigin: Removed " << removed 
                     << " routes from " << originRouter
                     << ", RIB size now: " << m_rib.size());
    }

    return removed;
}

void
RouteManager::clearAllRoutes()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = m_rib.size();
    m_rib.clear();
    m_maxCost = 0.0;

    NFD_LOG_INFO("clearAllRoutes: Cleared " << count << " routes");
}

// =============================================================================
// Route Lookup
// =============================================================================

std::vector<NextHopResult>
RouteManager::findBestMatches(const SemanticVector& query,
                               size_t topK,
                               double alpha,
                               double beta) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    NFD_LOG_DEBUG("findBestMatches: query dim=" << query.getDimension()
                  << ", topK=" << topK
                  << ", alpha=" << alpha
                  << ", beta=" << beta
                  << ", RIB size=" << m_rib.size());

    // Validate query
    if (query.empty()) {
        NFD_LOG_WARN("findBestMatches: Empty query vector, returning empty result");
        throw std::invalid_argument("RouteManager::findBestMatches: Query vector is empty");
    }

    // Handle empty RIB
    if (m_rib.empty()) {
        NFD_LOG_DEBUG("findBestMatches: RIB is empty, returning empty result");
        return {};
    }

    // Calculate scores for all entries
    std::vector<NextHopResult> results;
    results.reserve(m_rib.size());

    for (const auto& entry : m_rib) {
        // Skip entries with invalid vectors
        if (entry.centroid.empty()) {
            NFD_LOG_DEBUG("findBestMatches: Skipping entry " << entry.prefix 
                          << " with empty centroid");
            continue;
        }

        // Compute cosine similarity
        double similarity = 0.0;
        try {
            similarity = query.computeCosineSimilarity(entry.centroid);
        }
        catch (const std::exception& e) {
            NFD_LOG_WARN("findBestMatches: Failed to compute similarity for " 
                         << entry.prefix << ": " << e.what());
            continue;
        }

        // Normalize cost to [0, 1] range
        double normalizedCost = 0.0;
        if (m_maxCost > 0.0) {
            normalizedCost = entry.cost / m_maxCost;
        }

        // Calculate combined score
        // Score = alpha * similarity - beta * normalizedCost
        // Higher similarity is better (+), lower cost is better (-)
        double score = (alpha * similarity) - (beta * normalizedCost);

        NFD_LOG_TRACE("findBestMatches: " << entry.prefix
                      << " -> sim=" << similarity
                      << ", normCost=" << normalizedCost
                      << ", score=" << score);

        results.emplace_back(entry.prefix, score, entry.originRouterName, entry.cost);
    }

    // Handle case where no valid results
    if (results.empty()) {
        NFD_LOG_DEBUG("findBestMatches: No valid entries found");
        return {};
    }

    // Determine how many results to return
    size_t k = std::min(topK, results.size());

    // Use partial_sort for efficiency when k << results.size()
    // Sort by score descending (using greater-than comparator)
    std::partial_sort(results.begin(),
                      results.begin() + k,
                      results.end(),
                      [](const NextHopResult& a, const NextHopResult& b) {
                          return a.score > b.score;  // Descending order
                      });

    // Truncate to top-K
    results.resize(k);

    NFD_LOG_DEBUG("findBestMatches: Returning " << results.size() << " results, "
                  << "best score=" << (results.empty() ? 0.0 : results.front().score));

    return results;
}

// =============================================================================
// Accessors
// =============================================================================

size_t
RouteManager::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rib.size();
}

bool
RouteManager::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rib.empty();
}

std::vector<RibEntry>
RouteManager::getAllEntries() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rib;  // Returns a copy
}

double
RouteManager::getConvergenceTime() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastNewPrefixTime;
}

void
RouteManager::resetMetrics()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastNewPrefixTime = 0.0;
    NFD_LOG_DEBUG("resetMetrics: Convergence time reset");
}

// =============================================================================
// Gated Scoring (Paper-Compliant)
// =============================================================================

double
RouteManager::computeGatedScore(const SemanticVector& q, const CentroidEntry& c,
                                 double costD, double costMax) const
{
    // Per paper.tex Eq. 2:
    // S = α·(q·C)·σ(λ(r-d))·log(1+w) - β·Cost_D/Cost_max
    
    // Similarity: for L2-normalized vectors, dot product = cosine similarity
    double sim = q.dot(c.C);
    
    // Distance: d(q,C) = 1 - similarity
    double d = 1.0 - sim;
    
    // Gate: σ(λ(r - d)) where σ(x) = 1/(1+exp(-x))
    double gateArg = m_lambda * (c.radius - d);
    double gate = 1.0 / (1.0 + std::exp(-gateArg));
    
    // Weight term: log(1 + min(w, w_max))
    double clampedWeight = std::min(c.weight, m_wMax);
    double wTerm = std::log(1.0 + clampedWeight);
    
    // Cost penalty (normalized)
    double costPenalty = 0.0;
    if (costMax > 0.0) {
        costPenalty = costD / costMax;
    }
    
    // Combined score using configurable params
    double score = m_alpha * sim * gate * wTerm - m_beta * costPenalty;
    
    return score;
}

std::vector<DomainResult>
RouteManager::findBestDomainsV2(const SemanticVector& query, size_t topK, uint32_t semVerId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Validate SemVerId
    if (!isSupportedSemVer(semVerId)) {
        NFD_LOG_WARN("findBestDomainsV2: unsupported SemVerID=" << semVerId);
        return {};
    }
    
    // Get the correct index for this version
    const auto& index = getIndex(semVerId);
    
    NFD_LOG_DEBUG("findBestDomainsV2: semVerId=" << semVerId
                  << ", query dim=" << query.getDimension()
                  << ", topK=" << topK << ", domain count=" << index.size());
    
    if (query.empty() || index.empty()) {
        return {};
    }
    
    // Cap topK to k_max (note: kKMax is a protocol constant, not configurable per-instance)
    topK = std::min(topK, params::kKMax);
    
    // Find max cost for normalization
    double maxCost = 1.0;  // Avoid division by zero
    for (const auto& [key, domain] : index) {
        if (domain.cost > maxCost) {
            maxCost = domain.cost;
        }
    }
    
    std::vector<DomainResult> results;
    results.reserve(index.size());
    
    for (const auto& [key, domain] : index) {
        if (domain.centroids.empty()) {
            continue;
        }
        
        // Find the best centroid within this domain (max_i)
        double bestScore = -std::numeric_limits<double>::infinity();
        double bestConf = 0.0;
        uint32_t bestCentroidId = 0;
        
        for (const auto& centroid : domain.centroids) {
            if (centroid.C.empty()) continue;
            
            double score = computeGatedScore(query, centroid, domain.cost, maxCost);
            
            // Compute confidence (semantic part only for τ comparison)
            double sim = query.dot(centroid.C);
            double d = 1.0 - sim;
            double gate = 1.0 / (1.0 + std::exp(-m_lambda * (centroid.radius - d)));
            double conf = sim * gate;
            
            if (score > bestScore) {
                bestScore = score;
                bestConf = conf;
                bestCentroidId = centroid.centroidId;
            }
        }
        
        if (bestScore > -std::numeric_limits<double>::infinity()) {
            // Apply EWMA penalty: S'_D = S_D × EWMA_success(D)
            // Apply EWMA penalty only if enabled
            double penalty = 1.0;
            if (m_enableEwmaPenalty) {
                penalty = domain.ewma.getPenalty();
            }
            double penalizedScore = bestScore * penalty;
            
            results.emplace_back(domain.domainId, penalizedScore, bestConf, 
                                 bestCentroidId, domain.cost);
        }
    }
    
    if (results.empty()) {
        return {};
    }
    
    // Sort by score descending and take top-K
    size_t k = std::min(topK, results.size());
    std::partial_sort(results.begin(), results.begin() + k, results.end(),
                      [](const DomainResult& a, const DomainResult& b) {
                          return a.score > b.score;
                      });
    results.resize(k);
    
    NFD_LOG_DEBUG("findBestDomainsV2: returning " << results.size() << " results"
                  << ", best score=" << (results.empty() ? 0.0 : results.front().score)
                  << ", best conf=" << (results.empty() ? 0.0 : results.front().confidence));
    
    return results;
}

void
RouteManager::updateDomain(const DomainEntry& entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Route to correct index based on entry's semVerId
    if (!isSupportedSemVer(entry.semVerId)) {
        NFD_LOG_DEBUG("updateDomain: unsupported SemVerID=" << entry.semVerId);
        return;
    }
    
    auto& index = getIndex(entry.semVerId);
    std::string key = entry.domainId.toUri();
    
    // Per-(OriginID, SemVerID) SeqNo tracking
    std::string seqKey = key + ":" + std::to_string(entry.semVerId);
    
    auto it = index.find(key);
    if (it != index.end()) {
        // Check sequence number for freshness
        if (entry.seqNo <= m_lastSeqNo[seqKey]) {
            NFD_LOG_DEBUG("updateDomain: stale LSA for " << key 
                          << " semVer=" << entry.semVerId
                          << " (new seq=" << entry.seqNo 
                          << ", current=" << m_lastSeqNo[seqKey] << ")");
            return;
        }
        
        // Update existing entry (preserve EWMA state)
        EwmaState preservedEwma = it->second.ewma;
        it->second = entry;
        it->second.ewma = preservedEwma;
        
        NFD_LOG_INFO("updateDomain: updated " << key 
                     << " semVer=" << entry.semVerId
                     << ", seq=" << entry.seqNo
                     << ", centroids=" << entry.centroids.size());
    } else {
        // Insert new entry
        index[key] = entry;
        m_lastNewPrefixTime = ns3::Simulator::Now().GetSeconds();
        
        NFD_LOG_INFO("updateDomain: added new domain " << key
                     << " semVer=" << entry.semVerId
                     << ", seq=" << entry.seqNo
                     << ", centroids=" << entry.centroids.size());
    }
    
    // Update SeqNo tracking
    m_lastSeqNo[seqKey] = entry.seqNo;
    
    // Update max cost
    if (entry.cost > m_maxCost) {
        m_maxCost = entry.cost;
    }
}

std::optional<DomainEntry>
RouteManager::getDomain(const ndn::Name& domainId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string key = domainId.toUri();
    auto it = m_domainIndex.find(key);
    if (it != m_domainIndex.end()) {
        return it->second;
    }
    return std::nullopt;
}

void
RouteManager::reportFetchOutcome(const ndn::Name& domainId, bool success, uint32_t semVerId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!isSupportedSemVer(semVerId)) {
        return;
    }
    
    auto& index = getIndex(semVerId);
    std::string key = domainId.toUri();
    auto it = index.find(key);
    if (it != index.end()) {
        it->second.ewma.update(success);
        NFD_LOG_DEBUG("reportFetchOutcome: " << key 
                      << " semVer=" << semVerId
                      << " success=" << success
                      << ", ewma=" << it->second.ewma.successRate
                      << ", samples=" << it->second.ewma.sampleCount);
    }
}

size_t
RouteManager::domainCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_domainIndex.size();
}

size_t
RouteManager::domainCount(uint32_t semVerId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (semVerId == m_activeSemVerId) {
        return m_domainIndex.size();
    } else if (semVerId == m_prevSemVerId) {
        return m_domainIndexPrev.size();
    }
    return 0;
}

void
RouteManager::updateDomainCost(const ndn::Name& domainId, double cost, uint32_t semVerId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!isSupportedSemVer(semVerId)) {
        return;
    }
    
    auto& index = getIndex(semVerId);
    std::string key = domainId.toUri();
    auto it = index.find(key);
    if (it != index.end()) {
        it->second.cost = cost;
        if (cost > m_maxCost) {
            m_maxCost = cost;
        }
        NFD_LOG_DEBUG("updateDomainCost: " << key 
                      << " cost=" << cost << " maxCost=" << m_maxCost);
    }
}

// =============================================================================
// Dual-Version Indexing
// =============================================================================

bool
RouteManager::isSupportedSemVer(uint32_t semVerId) const
{
    return semVerId == m_activeSemVerId || semVerId == m_prevSemVerId;
}

std::map<std::string, DomainEntry>&
RouteManager::getIndex(uint32_t semVerId)
{
    if (semVerId == m_activeSemVerId) {
        return m_domainIndex;
    }
    if (semVerId == m_prevSemVerId) {
        return m_domainIndexPrev;
    }
    throw std::runtime_error("Unsupported SemVerID: " + std::to_string(semVerId));
}

const std::map<std::string, DomainEntry>&
RouteManager::getIndex(uint32_t semVerId) const
{
    if (semVerId == m_activeSemVerId) {
        return m_domainIndex;
    }
    if (semVerId == m_prevSemVerId) {
        return m_domainIndexPrev;
    }
    throw std::runtime_error("Unsupported SemVerID: " + std::to_string(semVerId));
}

void
RouteManager::switchToNewVersion(uint32_t newActiveId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (newActiveId == m_activeSemVerId) {
        NFD_LOG_INFO("switchToNewVersion: already active="  << newActiveId);
        return;
    }
    
    // Move curr -> prev
    m_domainIndexPrev = std::move(m_domainIndex);
    m_prevSemVerId = m_activeSemVerId;
    
    // New active
    m_activeSemVerId = newActiveId;
    
    // New curr starts empty
    m_domainIndex.clear();
    
    NFD_LOG_INFO("SemVer switch: prev=" << m_prevSemVerId 
                 << " active=" << m_activeSemVerId);
}

void
RouteManager::setActiveSemVerId(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeSemVerId = id;
    NFD_LOG_DEBUG("setActiveSemVerId: " << id);
}

void
RouteManager::setPrevSemVerId(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_prevSemVerId = id;
    NFD_LOG_DEBUG("setPrevSemVerId: " << id);
}

void
RouteManager::SetEnableEwmaPenalty(bool enable)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enableEwmaPenalty = enable;
    NFD_LOG_INFO("SetEnableEwmaPenalty: " << enable);
}

} // namespace iroute
