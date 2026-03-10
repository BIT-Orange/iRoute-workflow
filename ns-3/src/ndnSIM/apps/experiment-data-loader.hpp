/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file experiment-data-loader.hpp
 * @brief Helper class to load experiment data from CSV files for ndnSIM simulations.
 *
 * This class reads producer content and consumer query data from CSV files
 * and loads them into SemanticProducer and SemanticConsumer applications.
 * Vector dimension is configurable (default: 384).
 *
 * @par CSV File Formats:
 * 
 * @b producer_content.csv:
 * @verbatim
 * producer_id,name,dim_0,dim_1,...,dim_N-1
 * @endverbatim
 *
 * @b consumer_trace.csv:
 * @verbatim
 * query_id,expected_producer_id,target_name,completeness,similarity,dim_0,...,dim_N-1
 * @endverbatim
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include "iroute-vector.hpp"

#include "ns3/node-container.h"
#include "ns3/ptr.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace ns3 {
namespace ndn {

// Forward declarations
class SemanticProducer;
class SemanticConsumer;

/**
 * @struct ProducerContentEntry
 * @brief Represents a single producer content entry loaded from CSV.
 */
struct ProducerContentEntry {
    uint32_t producerId;                    ///< Producer node ID
    std::string contentName;                ///< Content name (e.g., "/wiki/...")
    iroute::SemanticVector semanticVector;  ///< Semantic embedding (configurable dimension)
};

/**
 * @struct ConsumerQueryEntry
 * @brief Represents a single consumer query entry loaded from CSV.
 */
struct ConsumerQueryEntry {
    uint32_t queryId;                       ///< Query ID (sequence number)
    uint32_t expectedProducerId;            ///< Ground truth: expected producer to respond
    std::string targetName;                 ///< Target content name
    double completeness;                    ///< Query completeness level (0.2-1.0)
    double similarity;                      ///< Expected similarity score
    iroute::SemanticVector queryVector;     ///< Query vector (configurable dimension)
};

/**
 * @class ExperimentDataLoader
 * @brief Loads experiment data from CSV files for ndnSIM simulations.
 *
 * This class provides methods to:
 * 1. Load producer content entries and register them with SemanticProducer apps
 * 2. Load consumer query entries for scheduling Interests
 *
 * Vector dimension is configurable. If CSV has insufficient columns for the
 * configured dimension, an exception is thrown (no silent zero-padding).
 *
 * @par Example Usage:
 * @code
 * ExperimentDataLoader loader(384);  // 384-dim vectors
 * 
 * auto producers = loader.loadProducerContent(
 *     "datasets/ndnsim_10k/producer_content.csv");
 * 
 * auto queries = loader.loadConsumerTrace(
 *     "datasets/ndnsim_10k/consumer_trace.csv", 1000);  // Load 1000 queries
 * @endcode
 */
class ExperimentDataLoader {
public:
    /**
     * @brief Constructs an ExperimentDataLoader with specified vector dimension.
     *
     * @param vectorDim The expected dimension for semantic vectors (default: 384).
     */
    explicit ExperimentDataLoader(size_t vectorDim = 384);

    /**
     * @brief Sets the vector dimension.
     *
     * @param dim The expected dimension.
     */
    void setVectorDim(size_t dim);

    /**
     * @brief Gets the current vector dimension.
     *
     * @return The vector dimension.
     */
    size_t getVectorDim() const;

    /**
     * @brief Loads producer content entries from a CSV file.
     *
     * Reads the CSV file and parses each row into a ProducerContentEntry.
     * The semantic vector dimensions are comma-separated floats.
     *
     * @param filename Path to the producer_content.csv file.
     * @param maxEntries Maximum number of entries to load (0 = unlimited).
     * @return Vector of ProducerContentEntry objects.
     *
     * @throws std::runtime_error if file cannot be opened or if CSV has
     *         insufficient columns for the configured dimension.
     */
    std::vector<ProducerContentEntry> loadProducerContent(
        const std::string& filename,
        size_t maxEntries = 0);

    /**
     * @brief Loads consumer query entries from a CSV file.
     *
     * Reads the CSV file and parses each row into a ConsumerQueryEntry.
     * The query vector dimensions are comma-separated floats.
     *
     * @param filename Path to the consumer_trace.csv file.
     * @param maxEntries Maximum number of entries to load (0 = unlimited).
     * @return Vector of ConsumerQueryEntry objects.
     *
     * @throws std::runtime_error if file cannot be opened or if CSV has
     *         insufficient columns for the configured dimension.
     */
    std::vector<ConsumerQueryEntry> loadConsumerTrace(
        const std::string& filename,
        size_t maxEntries = 0);

    // ========================================================================
    // Static Utility Functions (for backward compatibility)
    // ========================================================================

    /**
     * @brief Maps producer IDs to node indices.
     *
     * @param entries Vector of producer entries.
     * @return Map from producer_id to assigned node index.
     */
    static std::unordered_map<uint32_t, uint32_t> BuildProducerIdToNodeMap(
        const std::vector<ProducerContentEntry>& entries);

    /**
     * @brief Gets unique producer IDs from the loaded entries.
     *
     * @param entries Vector of producer entries.
     * @return Set of unique producer IDs.
     */
    static std::vector<uint32_t> GetUniqueProducerIds(
        const std::vector<ProducerContentEntry>& entries);

    /**
     * @brief Gets entries for a specific producer ID.
     *
     * @param entries Vector of all producer entries.
     * @param producerId The producer ID to filter by.
     * @return Vector of entries belonging to that producer.
     */
    static std::vector<ProducerContentEntry> GetEntriesForProducer(
        const std::vector<ProducerContentEntry>& entries,
        uint32_t producerId);

    // ========================================================================
    // Backward Compatibility Static Methods
    // ========================================================================

    /**
     * @brief Static wrapper for loadProducerContent (backward compatibility).
     *
     * @deprecated Use instance method loadProducerContent() instead.
     * @param filename Path to CSV file.
     * @param maxEntries Maximum entries (0 = unlimited).
     * @return Vector of ProducerContentEntry.
     */
    static std::vector<ProducerContentEntry> LoadProducerContent(
        const std::string& filename,
        size_t maxEntries = 0);

    /**
     * @brief Static wrapper for loadConsumerTrace (backward compatibility).
     *
     * @deprecated Use instance method loadConsumerTrace() instead.
     * @param filename Path to CSV file.
     * @param maxEntries Maximum entries (0 = unlimited).
     * @return Vector of ConsumerQueryEntry.
     */
    static std::vector<ConsumerQueryEntry> LoadConsumerTrace(
        const std::string& filename,
        size_t maxEntries = 0);

private:
    /**
     * @brief Parses a comma-separated line into tokens.
     *
     * @param line The CSV line to parse.
     * @return Vector of string tokens.
     */
    static std::vector<std::string> ParseCsvLine(const std::string& line);

    /**
     * @brief Parses dimension values from CSV tokens into a SemanticVector.
     *
     * @param tokens CSV tokens containing dimension values.
     * @param startIndex Index of the first dimension value in tokens.
     * @param numDimensions Number of dimensions to parse.
     * @param lineNum Line number for error reporting.
     * @return The parsed SemanticVector.
     *
     * @throws std::runtime_error if insufficient columns for numDimensions.
     */
    iroute::SemanticVector parseVectorFromTokens(
        const std::vector<std::string>& tokens,
        size_t startIndex,
        size_t numDimensions,
        size_t lineNum);

private:
    /**
     * @brief Expected vector dimension.
     */
    size_t m_vectorDim;
};

} // namespace ndn
} // namespace ns3
