/**
 * @file search-oracle-app.hpp
 * @brief Centralized search oracle for baseline comparison.
 *
 * This app simulates a centralized search server that:
 * - Has global knowledge of all documents across all domains
 * - Performs brute-force vector search to find best match
 * - Returns (canonicalName, domainId) to the consumer
 *
 * Used as a baseline to compare iRoute's distributed approach.
 */

#pragma once

#include "iroute-vector.hpp"
#include "iroute-manager.hpp"
#include "ns3/ndnSIM/apps/ndn-app.hpp"

#include <vector>
#include <map>
#include <unordered_map>

namespace ns3 {
namespace ndn {

/**
 * @struct GlobalContentEntry
 * @brief Content entry with domain association for global index.
 */
struct GlobalContentEntry {
    std::string docId;
    std::string canonicalName;
    iroute::SemanticVector vector;
    uint32_t domainId;
    bool isDistractor = false;
};

/**
 * @class SearchOracleApp
 * @brief Centralized search server with global document index.
 *
 * Naming convention:
 * - Interest: /search/oracle/<QueryHash>
 * - Data: Contains (status, canonicalName, domainId, confidence)
 */
class SearchOracleApp : public App
{
public:
    static TypeId GetTypeId();

    SearchOracleApp();
    virtual ~SearchOracleApp();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Adds a document to the global index.
     */
    void AddContent(const GlobalContentEntry& entry);

    /**
     * @brief Loads all content from a vector of entries.
     */
    void LoadContent(const std::vector<GlobalContentEntry>& content);

    /**
     * @brief Gets the number of indexed documents.
     */
    size_t GetIndexSize() const { return m_globalIndex.size(); }

    /**
     * @brief Gets total dot-product operations performed.
     */
    uint64_t GetDotProductCount() const { return m_dotProductCount; }

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnInterest(shared_ptr<const Interest> interest) override;

private:
    /**
     * @brief Performs global search and returns best match.
     */
    std::tuple<bool, std::string, uint32_t, double> 
    GlobalSearch(const iroute::SemanticVector& query);

private:
    Name m_prefix;                                    ///< Service prefix (/search/oracle)
    uint32_t m_payloadSize;                           ///< Response payload size
    std::vector<GlobalContentEntry> m_globalIndex;    ///< Global document index
    uint64_t m_dotProductCount = 0;                   ///< Compute proxy
    uint64_t m_queriesProcessed = 0;                  ///< Statistics
};

} // namespace ndn
} // namespace ns3
