/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "iroute-synth-corpus.hpp"
#include "ns3/log.h"
#include <algorithm>
#include <cmath>
#include <numeric>

NS_LOG_COMPONENT_DEFINE("iRouteSynthCorpus");

namespace iroute {

SynthCorpus::SynthCorpus(const Config& cfg)
    : m_cfg(cfg)
{
    m_rng = ns3::CreateObject<ns3::UniformRandomVariable>();
    m_normalRng = ns3::CreateObject<ns3::NormalRandomVariable>();
    m_rng->SetStream(cfg.seed);
    m_normalRng->SetStream(cfg.seed + 1);
}

void SynthCorpus::BuildBaseObjects()
{
    NS_LOG_FUNCTION(this);
    m_globalTopics.clear();
    m_domainTopics.clear();
    m_objects.clear();

    // 1. Generate Global Topic Centers
    for (uint32_t i = 0; i < m_cfg.globalTopics; ++i) {
        std::vector<float> data(m_cfg.vectorDim);
        for (auto& val : data) {
            val = m_rng->GetValue(-1.0, 1.0);
        }
        SemanticVector v(data);
        v.normalize();
        m_globalTopics.push_back(v);
    }

    // 2. Assign Topics to Domains
    // Strategy: First portion of T topics comes from [0..OverlapSize] (Shared)
    // Remainder comes from unique slice or random?
    // "overlapRatio * T from shared pool, rest private"
    uint32_t sharedPoolSize = std::max(1u, (uint32_t)(m_cfg.globalTopics * 0.5)); // Arbitrary shared pool
    // Actually, prompt says "globalTopics = 16".
    // Simple logic: Topic ID < sharedPoolSize are shared.
    
    uint32_t numShared = (uint32_t)(m_cfg.topicsPerDomain * m_cfg.overlapRatio);
    uint32_t numPrivate = m_cfg.topicsPerDomain - numShared;
    
    // Distribute private topics cyclically
    uint32_t privateTopicCounter = sharedPoolSize; 

    for (uint32_t d = 0; d < m_cfg.domains; ++d) {
        std::vector<uint32_t>& topics = m_domainTopics[d];
        
        // Add Shared Topics (Round Robin from shared pool)
        for (uint32_t k = 0; k < numShared; ++k) {
            topics.push_back(k % sharedPoolSize); // reuse 0..S
        }
        
        // Add Private Topics
        for (uint32_t k = 0; k < numPrivate; ++k) {
            // If we run out of global topics, wrap around (reusing "private" ones makes them shared effectively)
            // But with G=16, T=8, D=10, we need lots of topics?
            // Prompt says G=16. If D=10 * T=8 = 80 topics needed total?
            // "globalTopics" implies total universe size.
            // So topics must be reused heavily.
            // Overlap simply means: how many topics does D_i share with D_j?
            // Let's just pick T topics randomly from G for each domain, enforcing specific overlap?
            // Implementation: Just pick T unique IDs from [0..G-1].
            // To ensure overlapRatio:
            // Force first 'numShared' topics to be from 0..S.
            
            // Re-eval: Simple random selection from G is realistic.
            // But let's follow the "private" counter logic to ensure coverage.
            topics.push_back(privateTopicCounter % m_cfg.globalTopics);
            privateTopicCounter++;
        }
        std::sort(topics.begin(), topics.end());
        topics.erase(std::unique(topics.begin(), topics.end()), topics.end()); // Dedupe
    }

    // 3. Generate Objects
    for (uint32_t d = 0; d < m_cfg.domains; ++d) {
        for (uint32_t tId : m_domainTopics[d]) {
            const auto& center = m_globalTopics[tId];
            
            for (uint32_t k = 0; k < m_cfg.objectsPerTopic; ++k) {
                // Add noise
                std::vector<float> data = center.getData();
                for (float& val : data) {
                    val += m_normalRng->GetValue(0, m_cfg.objSigma);
                }
                SemanticVector objVec(data);
                objVec.normalize();
                
                ObjectItem item;
                item.domain = d;
                item.topic = tId; // Global Topic ID
                item.objectId = k; // Local ID within (Domain,Topic)
                item.vec = objVec;
                m_objects.push_back(item);
            }
        }
    }
    NS_LOG_INFO("Built " << m_objects.size() << " objects across " << m_cfg.domains << " domains.");
}

std::vector<SynthCorpus::CentroidEntry> 
SynthCorpus::ExportCentroids(uint32_t domainId, uint32_t M) const
{
    std::vector<CentroidEntry> result;
    if (m_domainTopics.find(domainId) == m_domainTopics.end()) return result;

    const std::vector<uint32_t>& topics = m_domainTopics.at(domainId);
    uint32_t T = topics.size();
    if (T == 0) return result;

    // Bucket topics into M centroids
    // If M >= T, each topic gets a centroid.
    // If M < T, merge topics [0, 1] -> C0, [2, 3] -> C1 ...
    
    uint32_t effectiveM = std::min(M, T);
    
    for (uint32_t m = 0; m < effectiveM; ++m) {
        // Identify topics belonging to this bucket
        std::vector<uint32_t> bucketTopics;
        for (uint32_t i = 0; i < T; ++i) {
            if (i % effectiveM == m) { // Round robin distribution
                bucketTopics.push_back(topics[i]);
            }
        }
        
        // Collect all objects for these topics
        std::vector<const ObjectItem*> bucketObjects;
        std::vector<SemanticVector> objVecs;
        
        for (const auto& obj : m_objects) {
            if (obj.domain == domainId) {
                for (uint32_t t : bucketTopics) {
                    if (obj.topic == t) {
                        bucketObjects.push_back(&obj);
                        objVecs.push_back(obj.vec);
                        break;
                    }
                }
            }
        }
        
        if (bucketObjects.empty()) continue;

        // Compute Mean (New Centroid)
        std::vector<float> meanData(m_cfg.vectorDim, 0.0f);
        for (const auto& v : objVecs) {
            for (size_t i = 0; i < m_cfg.vectorDim; ++i) {
                meanData[i] += v[i];
            }
        }
        SemanticVector centroid(meanData);
        centroid.normalize();
        
        // Compute Radius (Percentile)
        std::vector<double> dists;
        dists.reserve(objVecs.size());
        for (const auto& v : objVecs) {
            dists.push_back(centroid.computeCosineDistance(v));
        }
        std::sort(dists.begin(), dists.end());
        size_t idx = (size_t)(dists.size() * m_cfg.radiusPercentile);
        if (idx >= dists.size()) idx = dists.size() - 1;
        
        CentroidEntry entry;
        entry.centroidId = m;
        entry.C = centroid;
        entry.radius = dists[idx];
        entry.weight = static_cast<double>(bucketObjects.size());
        
        result.push_back(entry);
    }
    
    return result;
}

std::vector<ns3::ndn::IRouteDiscoveryConsumer::QueryItem>
SynthCorpus::MakeQueryTrace(uint32_t queries, bool roundRobinDomains, bool zipfHotness, double zipfS)
{
    std::vector<ns3::ndn::IRouteDiscoveryConsumer::QueryItem> trace;
    trace.reserve(queries);
    
    // Zipf generator setup if needed
    // Simple implementation: pre-calculate CDF
    std::vector<double> cdf;
    if (zipfHotness && !m_objects.empty()) {
        double sum = 0;
        for (size_t i = 1; i <= m_objects.size(); ++i) {
            sum += 1.0 / std::pow((double)i, zipfS);
            cdf.push_back(sum);
        }
        // Normalize
        for (auto& v : cdf) v /= sum;
    }

    for (uint32_t i = 0; i < queries; ++i) {
        const ObjectItem* targetObj = nullptr;
        
        if (zipfHotness && !m_objects.empty()) {
            double u = m_rng->GetValue(); // 0..1
            auto it = std::lower_bound(cdf.begin(), cdf.end(), u);
            size_t idx = std::distance(cdf.begin(), it);
            if (idx >= m_objects.size()) idx = m_objects.size() - 1;
            targetObj = &m_objects[idx];
        } else {
            // Uniform
            size_t idx = m_rng->GetInteger(0, m_objects.size() - 1);
            targetObj = &m_objects[idx];
        }
        
        if (!targetObj) continue;

        // Generate Query Vector = Object Vector + Noise
        std::vector<float> data = targetObj->vec.getData();
        for (float& val : data) {
            val += m_normalRng->GetValue(0, m_cfg.querySigma);
        }
        SemanticVector qVec(data);
        qVec.normalize();
        
        ns3::ndn::IRouteDiscoveryConsumer::QueryItem item;
        item.vector = qVec;
        // Format: /domainX (root prefix of domain)
        item.expectedDomain = "/domain" + std::to_string(targetObj->domain);
        
        // PR-2: Unique objectId per query to prevent cache masking
        item.objectId = i;
        
        trace.push_back(item);
    }
    
    return trace;
}

std::vector<std::string> 
SynthCorpus::MakeNameTrace(uint32_t queries, bool zipfHotness, double zipfS)
{
    std::vector<std::string> trace;
    trace.reserve(queries);
    
    // Simple Zipf CDF
    std::vector<double> cdf;
    if (zipfHotness && !m_objects.empty()) {
        double sum = 0;
        for (size_t i = 1; i <= m_objects.size(); ++i) {
            sum += 1.0 / std::pow((double)i, zipfS);
            cdf.push_back(sum);
        }
        for (auto& v : cdf) v /= sum;
    }

    for (uint32_t i = 0; i < queries; ++i) {
        const ObjectItem* targetObj = nullptr;
        if (zipfHotness && !m_objects.empty()) {
            double u = m_rng->GetValue();
            auto it = std::lower_bound(cdf.begin(), cdf.end(), u);
            size_t idx = std::distance(cdf.begin(), it);
            if (idx >= m_objects.size()) idx = m_objects.size() - 1;
            targetObj = &m_objects[idx];
        } else {
            size_t idx = m_rng->GetInteger(0, m_objects.size() - 1);
            targetObj = &m_objects[idx];
        }
        
        if (targetObj) {
            // PR-2: Unique name per query (use loop index i as objectId)
            trace.push_back(CanonicalName(targetObj->domain, targetObj->topic, i));
        }
    }
    return trace;
}

std::string 
SynthCorpus::CanonicalName(uint32_t domain, uint32_t topic, uint32_t objectId)
{
    return "/domain" + std::to_string(domain) + "/data/t" + std::to_string(topic) + "/o" + std::to_string(objectId);
}

} // namespace iroute
