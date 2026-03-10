/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file iroute-synth-corpus.hpp
 * @brief Pr-A: Synthetic semantic corpus generator for controlled difficulty experiments.
 *
 * Provides a reproducible workload with:
 * - Configurable topic overlap between domains.
 * - Quantization error control via budget M (merging topics).
 * - Noise injection for objects and queries.
 */

#ifndef IROUTE_SYNTH_CORPUS_HPP
#define IROUTE_SYNTH_CORPUS_HPP

#include "iroute-vector.hpp"
#include "apps/iroute-discovery-consumer.hpp" 
#include "ns3/random-variable-stream.h"
#include <vector>
#include <map>
#include <string>

namespace iroute {

class SynthCorpus {
public:
    struct ObjectItem {
        uint32_t domain;
        uint32_t topic;
        uint32_t objectId;
        SemanticVector vec;
    };

    struct CentroidEntry {
        uint32_t centroidId;
        SemanticVector C;
        double radius;
        double weight;
    };

    struct Config {
        uint32_t seed = 1;
        uint32_t vectorDim = 64;
        uint32_t domains = 10;
        uint32_t topicsPerDomain = 8;       // T
        uint32_t globalTopics = 16;         // G
        double overlapRatio = 0.5;          // Share pool ratio
        uint32_t objectsPerTopic = 50;
        double objSigma = 0.15;             // Object noise
        double querySigma = 0.08;           // Query noise
        double radiusPercentile = 0.95;
    };

    explicit SynthCorpus(const Config& cfg);

    /**
     * @brief Generates base objects for all domains based on config.
     * Must be called before ExportCentroids or MakeQueryTrace.
     */
    void BuildBaseObjects();

    /**
     * @brief Exports centroids for a specific domain given budget M.
     * 
     * If M < T, topics are merged (Quantization Error).
     * If M >= T, 1:1 mapping (Optimal).
     */
    std::vector<CentroidEntry> ExportCentroids(uint32_t domainId, uint32_t M) const;

    /**
     * @brief Generates a query trace for the consumer.
     * 
     * @param queries Total number of queries.
     * @param zipfS Zipf skew parameter (0.0 = uniform).
     */
    std::vector<ns3::ndn::IRouteDiscoveryConsumer::QueryItem>
    MakeQueryTrace(uint32_t queries, bool roundRobinDomains, bool zipfHotness, double zipfS);

    /**
     * @brief Generates a trace of canonical names for exact-match consumers (baselines).
     */
    std::vector<std::string> 
    MakeNameTrace(uint32_t queries, bool zipfHotness, double zipfS);

    /**
     * @brief Generates a canonical name for an object to avoid cache collision.
     * Format: /<Domain>/data/t<topic>/o<objectId>
     */
    static std::string CanonicalName(uint32_t domain, uint32_t topic, uint32_t objectId);

private:
    Config m_cfg;
    std::vector<SemanticVector> m_globalTopics; // Centers of G topics
    std::map<uint32_t, std::vector<uint32_t>> m_domainTopics; // domain -> list of topic IDs
    std::vector<ObjectItem> m_objects; // All generated objects
    
    ns3::Ptr<ns3::UniformRandomVariable> m_rng;
    ns3::Ptr<ns3::NormalRandomVariable> m_normalRng;

    // Helper to get objects belonging to a domain
    std::vector<const ObjectItem*> GetDomainObjects(uint32_t domainId) const;
};

} // namespace iroute

#endif // IROUTE_SYNTH_CORPUS_HPP
