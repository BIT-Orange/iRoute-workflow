/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file experiment-data-loader.cpp
 * @brief Implementation of ExperimentDataLoader for reading CSV experiment data.
 *
 * Vector dimension is configurable. CSV parsing now throws on insufficient
 * columns (no silent zero-padding).
 *
 * @author iRoute Team
 * @date 2024
 */

#include "experiment-data-loader.hpp"

#include "ns3/log.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <set>
#include <algorithm>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ExperimentDataLoader");

// =============================================================================
// Constructor
// =============================================================================

ExperimentDataLoader::ExperimentDataLoader(size_t vectorDim)
    : m_vectorDim(vectorDim)
{
    NS_LOG_INFO("ExperimentDataLoader created with vectorDim=" << m_vectorDim);
}

void
ExperimentDataLoader::setVectorDim(size_t dim)
{
    m_vectorDim = dim;
    NS_LOG_DEBUG("ExperimentDataLoader vectorDim set to " << m_vectorDim);
}

size_t
ExperimentDataLoader::getVectorDim() const
{
    return m_vectorDim;
}

// =============================================================================
// CSV Parsing Helpers
// =============================================================================

std::vector<std::string>
ExperimentDataLoader::ParseCsvLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;

    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            tokens.push_back(token.substr(start, end - start + 1));
        } else {
            tokens.push_back("");
        }
    }

    return tokens;
}

iroute::SemanticVector
ExperimentDataLoader::parseVectorFromTokens(
    const std::vector<std::string>& tokens,
    size_t startIndex,
    size_t numDimensions,
    size_t lineNum)
{
    // STRICT VALIDATION: Check if we have enough columns
    if (startIndex + numDimensions > tokens.size()) {
        std::ostringstream oss;
        oss << "ExperimentDataLoader: Line " << lineNum 
            << " has insufficient columns for " << numDimensions << "-dim vector. "
            << "Expected at least " << (startIndex + numDimensions) << " columns, "
            << "but only found " << tokens.size() << " columns. "
            << "Check that your CSV file matches VectorDim=" << numDimensions << ".";
        throw std::runtime_error(oss.str());
    }

    std::vector<float> data(numDimensions);

    for (size_t i = 0; i < numDimensions; ++i) {
        try {
            data[i] = std::stof(tokens[startIndex + i]);
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "ExperimentDataLoader: Line " << lineNum 
                << ", column " << (startIndex + i) 
                << ": failed to parse dimension " << i << " as float. "
                << "Value: '" << tokens[startIndex + i] << "'. "
                << "Error: " << e.what();
            throw std::runtime_error(oss.str());
        }
    }

    return iroute::SemanticVector(data);
}

// =============================================================================
// Producer Content Loading
// =============================================================================

std::vector<ProducerContentEntry>
ExperimentDataLoader::loadProducerContent(
    const std::string& filename,
    size_t maxEntries)
{
    std::vector<ProducerContentEntry> entries;

    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open producer content file: " + filename);
    }

    std::string line;
    
    // Skip header line
    if (!std::getline(file, line)) {
        throw std::runtime_error("Empty producer content file: " + filename);
    }

    size_t lineNum = 1;
    while (std::getline(file, line)) {
        ++lineNum;

        if (line.empty()) continue;

        auto tokens = ParseCsvLine(line);

        // Minimum columns: producer_id, name, dim_0...dim_{N-1}
        size_t requiredColumns = 2 + m_vectorDim;
        if (tokens.size() < requiredColumns) {
            std::ostringstream oss;
            oss << "ExperimentDataLoader: Line " << lineNum 
                << " has insufficient columns. "
                << "Expected at least " << requiredColumns << " (2 + VectorDim=" << m_vectorDim << "), "
                << "but found " << tokens.size() << ". "
                << "Dimension mismatch: CSV may be for different VectorDim.";
            throw std::runtime_error(oss.str());
        }

        ProducerContentEntry entry;

        try {
            entry.producerId = std::stoul(tokens[0]);
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "ExperimentDataLoader: Line " << lineNum 
                << ": invalid producer_id: '" << tokens[0] << "'";
            throw std::runtime_error(oss.str());
        }

        entry.contentName = tokens[1];
        entry.semanticVector = parseVectorFromTokens(tokens, 2, m_vectorDim, lineNum);

        entries.push_back(entry);

        if (maxEntries > 0 && entries.size() >= maxEntries) {
            break;
        }

        // Progress logging for large files
        if (entries.size() % 10000 == 0) {
            NS_LOG_INFO("Loaded " << entries.size() << " producer entries...");
        }
    }

    NS_LOG_INFO("Loaded " << entries.size() << " producer content entries from " << filename 
                << " (vectorDim=" << m_vectorDim << ")");

    return entries;
}

// =============================================================================
// Consumer Trace Loading
// =============================================================================

std::vector<ConsumerQueryEntry>
ExperimentDataLoader::loadConsumerTrace(
    const std::string& filename,
    size_t maxEntries)
{
    std::vector<ConsumerQueryEntry> entries;

    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open consumer trace file: " + filename);
    }

    std::string line;
    
    // Skip header line
    if (!std::getline(file, line)) {
        throw std::runtime_error("Empty consumer trace file: " + filename);
    }

    size_t lineNum = 1;
    while (std::getline(file, line)) {
        ++lineNum;

        if (line.empty()) continue;

        auto tokens = ParseCsvLine(line);

        // Minimum columns: query_id, expected_producer_id, target_name, 
        //                  completeness, similarity, dim_0...dim_{N-1}
        size_t requiredColumns = 5 + m_vectorDim;
        if (tokens.size() < requiredColumns) {
            std::ostringstream oss;
            oss << "ExperimentDataLoader: Line " << lineNum 
                << " has insufficient columns. "
                << "Expected at least " << requiredColumns << " (5 + VectorDim=" << m_vectorDim << "), "
                << "but found " << tokens.size() << ". "
                << "Dimension mismatch: CSV may be for different VectorDim.";
            throw std::runtime_error(oss.str());
        }

        ConsumerQueryEntry entry;

        try {
            entry.queryId = std::stoul(tokens[0]);
            entry.expectedProducerId = std::stoul(tokens[1]);
            entry.targetName = tokens[2];
            entry.completeness = std::stod(tokens[3]);
            entry.similarity = std::stod(tokens[4]);
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "ExperimentDataLoader: Line " << lineNum 
                << ": failed to parse field. Error: " << e.what();
            throw std::runtime_error(oss.str());
        }

        entry.queryVector = parseVectorFromTokens(tokens, 5, m_vectorDim, lineNum);

        entries.push_back(entry);

        if (maxEntries > 0 && entries.size() >= maxEntries) {
            break;
        }

        // Progress logging for large files
        if (entries.size() % 10000 == 0) {
            NS_LOG_INFO("Loaded " << entries.size() << " consumer query entries...");
        }
    }

    NS_LOG_INFO("Loaded " << entries.size() << " consumer query entries from " << filename
                << " (vectorDim=" << m_vectorDim << ")");

    return entries;
}

// =============================================================================
// Static Utility Functions
// =============================================================================

std::unordered_map<uint32_t, uint32_t>
ExperimentDataLoader::BuildProducerIdToNodeMap(
    const std::vector<ProducerContentEntry>& entries)
{
    std::unordered_map<uint32_t, uint32_t> idToNode;
    std::set<uint32_t> uniqueIds;

    for (const auto& entry : entries) {
        uniqueIds.insert(entry.producerId);
    }

    uint32_t nodeIndex = 0;
    for (uint32_t id : uniqueIds) {
        idToNode[id] = nodeIndex++;
    }

    NS_LOG_INFO("Built producer ID to node map: " << idToNode.size() << " unique producers");

    return idToNode;
}

std::vector<uint32_t>
ExperimentDataLoader::GetUniqueProducerIds(
    const std::vector<ProducerContentEntry>& entries)
{
    std::set<uint32_t> uniqueIds;

    for (const auto& entry : entries) {
        uniqueIds.insert(entry.producerId);
    }

    return std::vector<uint32_t>(uniqueIds.begin(), uniqueIds.end());
}

std::vector<ProducerContentEntry>
ExperimentDataLoader::GetEntriesForProducer(
    const std::vector<ProducerContentEntry>& entries,
    uint32_t producerId)
{
    std::vector<ProducerContentEntry> result;

    std::copy_if(entries.begin(), entries.end(), std::back_inserter(result),
                 [producerId](const ProducerContentEntry& entry) {
                     return entry.producerId == producerId;
                 });

    return result;
}

// =============================================================================
// Backward Compatibility Static Methods
// =============================================================================

std::vector<ProducerContentEntry>
ExperimentDataLoader::LoadProducerContent(
    const std::string& filename,
    size_t maxEntries)
{
    // Use default 384 dimension for backward compatibility
    ExperimentDataLoader loader(384);
    return loader.loadProducerContent(filename, maxEntries);
}

std::vector<ConsumerQueryEntry>
ExperimentDataLoader::LoadConsumerTrace(
    const std::string& filename,
    size_t maxEntries)
{
    // Use default 384 dimension for backward compatibility
    ExperimentDataLoader loader(384);
    return loader.loadConsumerTrace(filename, maxEntries);
}

} // namespace ndn
} // namespace ns3
