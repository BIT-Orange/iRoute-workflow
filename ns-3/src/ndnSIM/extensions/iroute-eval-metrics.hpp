/**
 * @file iroute-eval-metrics.hpp
 * @brief Unified metrics and CSV logging for iRoute v2 evaluation experiments.
 */

#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sys/stat.h>

namespace iroute {
namespace eval {

struct ExperimentParams {
    uint32_t seed = 42;
    double simTime = 60.0;
    uint32_t vectorDim = 384;
    uint32_t domains = 10;
    uint32_t objectsPerDomain = 1000;
    uint32_t M = 4;
    uint32_t kMax = 5;
    double tau = 0.5;
    double alpha = 0.7;
    double beta = 0.3;
    double lambda = 10.0;
    double wMax = 1000.0;
    uint32_t semVerActive = 1;
    uint32_t semVerPrev = 0;
    bool enableEwma = true;
    bool enableProbing = true;
    std::string resultDir = "results";
    std::string expName = "exp";
    
    std::string toHeaderLine() const {
        std::ostringstream oss;
        oss << "# seed=" << seed << ",simTime=" << simTime 
            << ",vectorDim=" << vectorDim << ",domains=" << domains
            << ",M=" << M << ",kMax=" << kMax << ",tau=" << tau
            << ",alpha=" << alpha << ",beta=" << beta
            << ",enableEwma=" << enableEwma;
        return oss.str();
    }
};

struct QueryMetrics {
    double time = 0.0;
    uint64_t queryId = 0;
    uint32_t semVerId = 0;
    std::string selectedDomain;
    uint32_t kUsed = 0;
    double discRttMs = 0.0;
    double fetchRttMs = 0.0;
    double e2eMs = 0.0;
    bool success = false;
    double confidence = 0.0;
    double score = 0.0;
    uint64_t lsaBytesRx = 0;
    uint64_t lsaBytesTx = 0;
    uint64_t indexEntries = 0;
    uint64_t indexBytes = 0;
    double ewmaValue = 1.0;
    bool isMaliciousChosen = false;
    
    std::string toCsvLine() const {
        std::ostringstream oss;
        oss << time << "," << queryId << "," << semVerId << ","
            << selectedDomain << "," << kUsed << ","
            << discRttMs << "," << fetchRttMs << "," << e2eMs << ","
            << (success ? 1 : 0) << "," << confidence << "," << score << ","
            << lsaBytesRx << "," << lsaBytesTx << ","
            << indexEntries << "," << indexBytes << ","
            << ewmaValue << "," << (isMaliciousChosen ? 1 : 0);
        return oss.str();
    }
    
    static std::string csvHeader() {
        return "time,queryId,semVerId,selectedDomain,kUsed,"
               "discRttMs,fetchRttMs,e2eMs,success,confidence,score,"
               "lsaBytesRx,lsaBytesTx,indexEntries,indexBytes,"
               "ewmaValue,isMaliciousChosen";
    }
};

struct SummaryStats {
    uint64_t totalQueries = 0;
    uint64_t successCount = 0;
    uint64_t top1Hit = 0;
    uint64_t topKHit = 0;
    uint64_t notFound = 0;
    
    double avgE2eMs = 0.0;
    double p50E2eMs = 0.0;
    double p95E2eMs = 0.0;
    double avgDiscRttMs = 0.0;
    double avgFetchRttMs = 0.0;
    double avgConfidence = 0.0;
    double avgKUsed = 0.0;
    
    uint64_t totalLsaBytesRx = 0;
    uint64_t totalLsaBytesTx = 0;
    uint64_t maxIndexEntries = 0;
    uint64_t maxIndexBytes = 0;
    
    double successRate() const { return totalQueries > 0 ? (double)successCount / totalQueries : 0.0; }
    double top1HitRate() const { return totalQueries > 0 ? (double)top1Hit / totalQueries : 0.0; }
    double topKHitRate() const { return totalQueries > 0 ? (double)topKHit / totalQueries : 0.0; }
    double notFoundRate() const { return totalQueries > 0 ? (double)notFound / totalQueries : 0.0; }
    
    std::string toCsvLine(const ExperimentParams& params) const {
        std::ostringstream oss;
        oss << params.M << "," << params.kMax << "," << params.tau << ","
            << params.domains << "," << params.objectsPerDomain << ","
            << totalQueries << "," << successCount << ","
            << top1Hit << "," << topKHit << "," << notFound << ","
            << successRate() << "," << top1HitRate() << "," << topKHitRate() << "," << notFoundRate() << ","
            << avgE2eMs << "," << p50E2eMs << "," << p95E2eMs << ","
            << avgDiscRttMs << "," << avgFetchRttMs << ","
            << avgConfidence << "," << avgKUsed << ","
            << totalLsaBytesRx << "," << totalLsaBytesTx << ","
            << maxIndexEntries << "," << maxIndexBytes;
        return oss.str();
    }
    
    static std::string csvHeader() {
        return "M,kMax,tau,domains,objectsPerDomain,"
               "totalQueries,successCount,top1Hit,topKHit,notFound,"
               "successRate,top1HitRate,topKHitRate,notFoundRate,"
               "avgE2eMs,p50E2eMs,p95E2eMs,avgDiscRttMs,avgFetchRttMs,"
               "avgConfidence,avgKUsed,"
               "totalLsaBytesRx,totalLsaBytesTx,maxIndexEntries,maxIndexBytes";
    }
};

class MetricsCollector {
public:
    MetricsCollector(const ExperimentParams& params)
        : m_params(params), m_rng(params.seed) {
        ensureDirectory(params.resultDir);
        ensureDirectory(params.resultDir + "/" + params.expName);
        
        std::string rawPath = params.resultDir + "/" + params.expName + "/raw_queries.csv";
        m_rawFile.open(rawPath);
        if (m_rawFile.is_open()) {
            m_rawFile << params.toHeaderLine() << "\n";
            m_rawFile << QueryMetrics::csvHeader() << "\n";
        }
    }
    
    ~MetricsCollector() {
        if (m_rawFile.is_open()) m_rawFile.close();
        writeSummary();
    }
    
    void recordQuery(const QueryMetrics& m) {
        m_queries.push_back(m);
        if (m_rawFile.is_open()) m_rawFile << m.toCsvLine() << "\n";
    }
    
    std::mt19937& rng() { return m_rng; }
    
    SummaryStats computeSummary() const {
        SummaryStats s;
        s.totalQueries = m_queries.size();
        if (m_queries.empty()) return s;
        
        std::vector<double> e2eTimes;
        for (const auto& q : m_queries) {
            if (q.success) { s.successCount++; e2eTimes.push_back(q.e2eMs); }
            if (q.kUsed == 1 && q.success) s.top1Hit++;
            if (q.success) s.topKHit++;
            if (!q.success) s.notFound++;
            
            s.avgE2eMs += q.e2eMs;
            s.avgDiscRttMs += q.discRttMs;
            s.avgFetchRttMs += q.fetchRttMs;
            s.avgConfidence += q.confidence;
            s.avgKUsed += q.kUsed;
            s.totalLsaBytesRx += q.lsaBytesRx;
            s.totalLsaBytesTx += q.lsaBytesTx;
            s.maxIndexEntries = std::max(s.maxIndexEntries, q.indexEntries);
            s.maxIndexBytes = std::max(s.maxIndexBytes, q.indexBytes);
        }
        
        size_t n = m_queries.size();
        s.avgE2eMs /= n; s.avgDiscRttMs /= n; s.avgFetchRttMs /= n;
        s.avgConfidence /= n; s.avgKUsed /= n;
        
        if (!e2eTimes.empty()) {
            std::sort(e2eTimes.begin(), e2eTimes.end());
            s.p50E2eMs = e2eTimes[e2eTimes.size() / 2];
            s.p95E2eMs = e2eTimes[std::min(e2eTimes.size() - 1, (size_t)(e2eTimes.size() * 0.95))];
        }
        return s;
    }
    
    void writeSummary() {
        std::string summaryPath = m_params.resultDir + "/" + m_params.expName + "/summary.csv";
        std::ofstream f(summaryPath);
        if (f.is_open()) {
            f << m_params.toHeaderLine() << "\n";
            f << SummaryStats::csvHeader() << "\n";
            f << computeSummary().toCsvLine(m_params) << "\n";
        }
    }
    
private:
    static void ensureDirectory(const std::string& path) { mkdir(path.c_str(), 0755); }
    
    ExperimentParams m_params;
    std::mt19937 m_rng;
    std::vector<QueryMetrics> m_queries;
    std::ofstream m_rawFile;
};

inline std::vector<float> generateRandomVector(std::mt19937& rng, uint32_t dim) {
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> v(dim);
    float norm = 0.0f;
    for (uint32_t i = 0; i < dim; ++i) { v[i] = dist(rng); norm += v[i] * v[i]; }
    norm = std::sqrt(norm);
    for (uint32_t i = 0; i < dim; ++i) v[i] /= norm;
    return v;
}

} // namespace eval
} // namespace iroute
