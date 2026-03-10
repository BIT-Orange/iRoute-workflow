/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file search-server-app.hpp
 * @brief Centralized semantic search server for NLSR baseline comparison.
 *
 * This application acts as a semantic search resolution node. It receives
 * fuzzy query Interests, resolves them to the best-matching content name
 * using a local semantic index, and returns a Data packet containing the
 * resolved name.
 *
 * @par Baseline Architecture:
 * Consumer → SearchServer → (resolution) → Consumer → Producer
 *
 * @par Protocol:
 * 1. Consumer sends Interest: /search/<query-params-sha256>
 *    ApplicationParameters: SemanticVector (query)
 * 2. SearchServer finds best match in its content index
 * 3. SearchServer returns Data with payload: resolved content name
 * 4. Consumer parses resolution and sends new Interest to that name
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include "iroute-vector.hpp"

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/ndnSIM/model/ndn-common.hpp"

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <unordered_map>
#include <string>
#include <vector>

namespace ns3 {
namespace ndn {

/**
 * @struct ContentRegistration
 * @brief Entry in the SearchServer's content index.
 */
struct ContentRegistration {
    std::string contentName;           ///< Full content name
    uint32_t producerId;               ///< Producer ID for verification
    iroute::SemanticVector vector;     ///< Semantic embedding
};

/**
 * @class SearchServerApp
 * @brief Centralized semantic search server for NLSR baseline comparison.
 *
 * Key responsibilities:
 * 1. Maintain a semantic index of all registered content
 * 2. Resolve fuzzy queries to exact content names
 * 3. Return resolution Data packets to consumers
 *
 * @par Usage in Experiment:
 * @code
 * ndn::AppHelper searchHelper("ns3::ndn::SearchServerApp");
 * searchHelper.SetPrefix("/search");
 * auto apps = searchHelper.Install(searchNode);
 *
 * auto server = DynamicCast<ndn::SearchServerApp>(apps.Get(0));
 * server->RegisterContent("/wiki/apple", producerId, appleVector);
 * @endcode
 */
class SearchServerApp : public App
{
public:
    /**
     * @brief Get the TypeId for this class.
     */
    static TypeId GetTypeId();
    
    /**
     * @brief Default constructor.
     */
    SearchServerApp();
    
    /**
     * @brief Destructor.
     */
    virtual ~SearchServerApp();
    
    /**
     * @brief Register content for semantic lookup.
     *
     * @param name Content name (e.g., "/wiki/...").
     * @param producerId Producer ID for routing.
     * @param vector Semantic embedding vector.
     */
    void RegisterContent(const std::string& name, 
                         uint32_t producerId,
                         const iroute::SemanticVector& vector);
    
    /**
     * @brief Resolve a query vector to the best matching content.
     *
     * @param query Query vector.
     * @return Pair of (content name, similarity score).
     */
    std::pair<std::string, double> ResolveQuery(
        const iroute::SemanticVector& query) const;
    
    /**
     * @brief Get the number of registered content entries.
     */
    size_t GetContentCount() const { return m_contentIndex.size(); }
    
    /**
     * @brief Get the number of queries received.
     */
    uint32_t GetQueriesReceived() const { return m_queriesReceived; }
    
    /**
     * @brief Get the number of resolutions returned.
     */
    uint32_t GetResolutionsReturned() const { return m_resolutionsReturned; }
    
protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    
    /**
     * @brief Handle incoming Interest packets.
     *
     * Extracts the query vector from ApplicationParameters,
     * resolves to best match, and returns Data with the resolved name.
     */
    virtual void OnInterest(shared_ptr<const Interest> interest) override;
    
private:
    /// Prefix for search queries (e.g., "/search")
    Name m_prefix;
    
    /// Content index for semantic search
    std::vector<ContentRegistration> m_contentIndex;
    
    /// Statistics
    uint32_t m_queriesReceived = 0;
    uint32_t m_resolutionsReturned = 0;
    
    /// Key chain for signing Data packets
    ::ndn::KeyChain m_keyChain;
    
    /// Payload size for response
    uint32_t m_payloadSize = 256;
    
    /// Freshness period
    ns3::Time m_freshness = Seconds(10);
};

} // namespace ndn
} // namespace ns3
