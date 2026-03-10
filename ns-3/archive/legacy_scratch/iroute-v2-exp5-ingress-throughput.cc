/**
 * @file iroute-v2-exp5-ingress-throughput.cc
 * @brief Exp5: Ingress compute throughput microbenchmark (compute-only)
 *
 * Measures search performance with:
 * 1. Linear scan (baseline) - represents Centralized search server cost
 * 2. Sample-based approximate search (simple ANN)
 * 3. HNSW index (advanced ANN using hnswlib)
 *
 * NEW: Supports loading real vectors from dataset files for realistic
 * cache/memory locality and similarity distribution.
 *
 * Outputs key compute metrics:
 * - dot_products: total number of dot product operations
 * - bytes_touched: total bytes read from centroid vectors
 * - These quantify the "compute for bandwidth" trade-off
 *
 * Usage:
 *   # Synthetic mode (legacy):
 *   ./waf --run "iroute-v2-exp5 --index=linear --domains=1000"
 *   
 *   # Real data mode (recommended for paper):
 *   ./waf --run "iroute-v2-exp5 --index=linear \
 *       --centroidsFile=dataset/trec_dl_combined_dim128/domain_centroids.csv \
 *       --traceFile=dataset/trec_dl_combined_dim128/consumer_trace.csv"
 */

#include "ns3/core-module.h"
#include "ns3/ndnSIM-module.h"

// Shared utilities (includes all iRoute headers)
#include "iroute-common-utils.hpp"

// HNSW header-only library (relative to this file in scratch/)
#include "../third_party/hnswlib/hnswlib.h"

#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <vector>
#include <numeric>
#include <cmath>
#include <sys/stat.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("iRouteExp5Throughput");

// Global parameters
static uint32_t g_seed = 42;
static uint32_t g_domains = 1000;
static uint32_t g_M = 8;
static uint32_t g_vectorDim = 128;  // Updated default to match real data
static uint32_t g_queries = 10000;
static uint32_t g_topK = 5;
static std::string g_resultDir = "results/exp5";
static std::string g_indexType = "linear";  // "linear", "sample", "hnsw"
static double g_sampleRatio = 0.1;
static uint32_t g_efSearch = 64;
static uint32_t g_Mgraph = 16;

// NEW: Real data file paths
static std::string g_centroidsFile = "dataset/trec_dl_combined_dim128/domain_centroids.csv";
static std::string g_traceFile = "dataset/trec_dl_combined_dim128/consumer_trace.csv";
static bool g_useRealData = false;        // Auto-detected

std::vector<float> GenerateRandomVector(std::mt19937& rng, uint32_t dim) {
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> v(dim);
    float norm = 0.0f;
    for (uint32_t i = 0; i < dim; ++i) {
        v[i] = dist(rng);
        norm += v[i] * v[i];
    }
    norm = std::sqrt(norm);
    for (uint32_t i = 0; i < dim; ++i) v[i] /= norm;
    return v;
}

double Percentile(std::vector<double>& data, double p) {
    if (data.empty()) return 0.0;
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    return data[idx];
}

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("domains", "Number of domains (synthetic mode only)", g_domains);
    cmd.AddValue("M", "Centroids per domain (synthetic mode only)", g_M);
    cmd.AddValue("vectorDim", "Vector dimension (64/128/384)", g_vectorDim);
    cmd.AddValue("queries", "Number of benchmark queries", g_queries);
    cmd.AddValue("topK", "Top-K candidates to return", g_topK);
    cmd.AddValue("resultDir", "Results directory", g_resultDir);
    cmd.AddValue("index", "Index type: linear, sample, hnsw", g_indexType);
    cmd.AddValue("sampleRatio", "Sample ratio (0-1) for sample mode", g_sampleRatio);
    cmd.AddValue("efSearch", "HNSW efSearch parameter", g_efSearch);
    cmd.AddValue("Mgraph", "HNSW M (graph connectivity)", g_Mgraph);
    // NEW: Real data options
    cmd.AddValue("centroidsFile", "Path to domain_centroids.csv (real data mode)", g_centroidsFile);
    cmd.AddValue("traceFile", "Path to consumer_trace.csv (real data mode)", g_traceFile);
    cmd.Parse(argc, argv);
    
    std::mt19937 rng(g_seed);
    
    // Detect mode
    g_useRealData = !g_centroidsFile.empty();
    
    // =========================================================================
    // Load or generate centroid vectors
    // =========================================================================
    
    std::vector<std::vector<float>> centroidVectors;
    std::vector<uint32_t> centroidToDomain;  // Maps centroid index to domain ID
    uint32_t totalCentroids = 0;
    uint32_t actualDomains = 0;
    
    if (g_useRealData) {
        NS_LOG_UNCOND("=== Loading REAL centroids from: " << g_centroidsFile << " ===");
        auto loadedCentroids = iroute::utils::LoadCentroidsFromCsv(g_centroidsFile);
        
        if (loadedCentroids.empty()) {
            NS_FATAL_ERROR("Failed to load centroids from " << g_centroidsFile);
        }
        
        actualDomains = loadedCentroids.size();
        for (const auto& [domainId, centroids] : loadedCentroids) {
            for (const auto& c : centroids) {
                centroidVectors.push_back(c.C.getData());
                centroidToDomain.push_back(domainId);
            }
        }
        totalCentroids = centroidVectors.size();
        
        // Infer vector dimension from data
        if (!centroidVectors.empty()) {
            g_vectorDim = centroidVectors[0].size();
        }
        
        NS_LOG_UNCOND("Loaded " << totalCentroids << " centroids from " << actualDomains 
                      << " domains, dim=" << g_vectorDim);
    } else {
        NS_LOG_UNCOND("=== SYNTHETIC mode: generating random centroids ===");
        totalCentroids = g_domains * g_M;
        actualDomains = g_domains;
        centroidVectors.reserve(totalCentroids);
        centroidToDomain.reserve(totalCentroids);
        
        for (uint32_t d = 0; d < g_domains; ++d) {
            for (uint32_t m = 0; m < g_M; ++m) {
                centroidVectors.push_back(GenerateRandomVector(rng, g_vectorDim));
                centroidToDomain.push_back(d);
            }
        }
    }
    
    // =========================================================================
    // Load or generate query vectors
    // =========================================================================
    
    std::vector<std::vector<float>> queryVectors;
    
    if (g_useRealData && !g_traceFile.empty()) {
        NS_LOG_UNCOND("Loading query vectors from: " << g_traceFile);
        auto trace = iroute::utils::LoadTraceFromCsv(g_traceFile);
        
        if (trace.empty()) {
            NS_LOG_WARN("Failed to load trace, falling back to random queries");
        } else {
            for (const auto& item : trace) {
                queryVectors.push_back(item.vector.getData());
            }
            g_queries = queryVectors.size();
            NS_LOG_UNCOND("Loaded " << g_queries << " query vectors from trace");
        }
    }
    
    // Generate random queries if not loaded from file
    if (queryVectors.empty()) {
        queryVectors.reserve(g_queries);
        for (uint32_t q = 0; q < g_queries; ++q) {
            queryVectors.push_back(GenerateRandomVector(rng, g_vectorDim));
        }
    }
    
    uint32_t sampleSize = 0;
    if (g_indexType == "sample") {
        sampleSize = static_cast<uint32_t>(totalCentroids * g_sampleRatio);
        if (sampleSize < g_topK) sampleSize = g_topK;
    }
    
    NS_LOG_UNCOND("=== iRoute v2 Exp5: Ingress Throughput Benchmark ===");
    NS_LOG_UNCOND("mode=" << (g_useRealData ? "REAL" : "SYNTHETIC") 
                  << ", index=" << g_indexType << ", domains=" << actualDomains 
                  << ", totalCentroids=" << totalCentroids
                  << ", vectorDim=" << g_vectorDim << ", queries=" << g_queries 
                  << ", topK=" << g_topK);
    
    if (g_indexType == "hnsw") NS_LOG_UNCOND("HNSW: M=" << g_Mgraph << ", efSearch=" << g_efSearch);
    if (g_indexType == "sample") NS_LOG_UNCOND("Sample: ratio=" << g_sampleRatio << ", size=" << sampleSize);
    
    // =========================================================================
    // Build index (HNSW only)
    // =========================================================================
    
    hnswlib::InnerProductSpace* hnswSpace = nullptr;
    hnswlib::HierarchicalNSW<float>* hnswIndex = nullptr;
    double buildMs = 0.0;
    
    // Pre-generate sample indices for sample mode
    std::vector<uint32_t> allIndices(totalCentroids);
    if (g_indexType == "sample") {
        std::iota(allIndices.begin(), allIndices.end(), 0);
    }
    
    if (g_indexType == "hnsw") {
        NS_LOG_UNCOND("Building HNSW index...");
        auto buildStart = std::chrono::steady_clock::now();
        
        hnswSpace = new hnswlib::InnerProductSpace(g_vectorDim);
        hnswIndex = new hnswlib::HierarchicalNSW<float>(hnswSpace, totalCentroids, g_Mgraph, 200);
        
        // Add items
        for (size_t i = 0; i < totalCentroids; ++i) {
            hnswIndex->addPoint(centroidVectors[i].data(), i);
        }
        
        hnswIndex->setEf(g_efSearch);
        
        auto buildEnd = std::chrono::steady_clock::now();
        buildMs = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
        NS_LOG_UNCOND("HNSW build time: " << buildMs << "ms");
    }
    
    NS_LOG_UNCOND("Running benchmark (" << g_indexType << ")...");
    
    // =========================================================================
    // Benchmark with compute statistics
    // =========================================================================
    
    std::vector<double> queryTimesUs;
    queryTimesUs.reserve(g_queries);
    
    // NEW: Compute statistics for paper
    uint64_t totalDotProducts = 0;
    uint64_t totalAnnOps = 0;        // Approx ANN ops (HNSW only)
    uint64_t totalBytesTouched = 0;  // Bytes read from centroid vectors
    std::vector<uint64_t> perQueryDotProducts;
    std::vector<uint64_t> perQueryAnnOps;
    perQueryDotProducts.reserve(g_queries);
    perQueryAnnOps.reserve(g_queries);
    
    auto benchStart = std::chrono::steady_clock::now();
    
    // --- HNSW LOOP ---
    if (g_indexType == "hnsw") {
        // HNSW: Approximate count based on efSearch (not exact, but reasonable estimate)
        // Each search touches approximately efSearch * log(N) nodes
        uint64_t estimatedDotsPerQuery = g_efSearch * static_cast<uint64_t>(std::log2(totalCentroids + 1));
        
        for (uint32_t q = 0; q < g_queries; ++q) {
            auto queryStart = std::chrono::steady_clock::now();
            auto result = hnswIndex->searchKnn(queryVectors[q].data(), g_topK);
            auto queryEnd = std::chrono::steady_clock::now();
            queryTimesUs.push_back(std::chrono::duration<double, std::micro>(queryEnd - queryStart).count());
            
            totalDotProducts += 0;  // Not measured in HNSW, use ANN ops below
            totalAnnOps += estimatedDotsPerQuery;
            totalBytesTouched += estimatedDotsPerQuery * g_vectorDim * sizeof(float);
            perQueryDotProducts.push_back(0);
            perQueryAnnOps.push_back(estimatedDotsPerQuery);
        }
    }
    // --- SAMPLE LOOP ---
    else if (g_indexType == "sample") {
        for (uint32_t q = 0; q < g_queries; ++q) {
            auto queryStart = std::chrono::steady_clock::now();
            
            std::vector<std::pair<float, uint32_t>> scores;
            scores.reserve(sampleSize);
            const auto& qvec = queryVectors[q];
            
            for (uint32_t k = 0; k < sampleSize; ++k) {
                uint32_t idx = rng() % totalCentroids;
                float dot = 0.0f;
                for (uint32_t d = 0; d < g_vectorDim; ++d) {
                    dot += qvec[d] * centroidVectors[idx][d];
                }
                scores.emplace_back(dot, idx);
            }
            
            std::partial_sort(scores.begin(), scores.begin() + std::min((size_t)g_topK, scores.size()), 
                              scores.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            
            auto queryEnd = std::chrono::steady_clock::now();
            queryTimesUs.push_back(std::chrono::duration<double, std::micro>(queryEnd - queryStart).count());
            
            // Exact counts for sample mode
            totalDotProducts += sampleSize;
            totalBytesTouched += sampleSize * g_vectorDim * sizeof(float);
            perQueryDotProducts.push_back(sampleSize);
            perQueryAnnOps.push_back(0);
        }
    }
    // --- LINEAR LOOP (Centralized baseline equivalent) ---
    else {
        for (uint32_t q = 0; q < g_queries; ++q) {
            auto queryStart = std::chrono::steady_clock::now();
            
            std::vector<std::pair<float, uint32_t>> scores;
            scores.reserve(totalCentroids);
            const auto& qvec = queryVectors[q];
            
            for (uint32_t i = 0; i < totalCentroids; ++i) {
                float dot = 0.0f;
                for (uint32_t d = 0; d < g_vectorDim; ++d) {
                    dot += qvec[d] * centroidVectors[i][d];
                }
                scores.emplace_back(dot, i);
            }
            
            std::partial_sort(scores.begin(), scores.begin() + std::min((size_t)g_topK, scores.size()), 
                              scores.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            
            auto queryEnd = std::chrono::steady_clock::now();
            queryTimesUs.push_back(std::chrono::duration<double, std::micro>(queryEnd - queryStart).count());
            
            // Exact counts for linear scan
            totalDotProducts += totalCentroids;
            totalBytesTouched += totalCentroids * g_vectorDim * sizeof(float);
            perQueryDotProducts.push_back(totalCentroids);
            perQueryAnnOps.push_back(0);
        }
    }
    
    auto benchEnd = std::chrono::steady_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(benchEnd - benchStart).count();
    
    // =========================================================================
    // Statistics & Export
    // =========================================================================
    
    double avgUs = 0;
    for (double t : queryTimesUs) avgUs += t;
    avgUs /= g_queries;
    
    double p50Us = Percentile(queryTimesUs, 0.5);
    double p95Us = Percentile(queryTimesUs, 0.95);
    double throughputQps = (g_queries / totalMs) * 1000.0;
    
    // NEW: Compute statistics
    double avgDotProducts = static_cast<double>(totalDotProducts) / g_queries;
    double avgAnnOps = static_cast<double>(totalAnnOps) / g_queries;
    double avgBytesTouched = static_cast<double>(totalBytesTouched) / g_queries;
    double avgBytesTouchedKB = avgBytesTouched / 1024.0;
    
    // Calculate p95 for dot products
    std::vector<double> dotProductsDouble(perQueryDotProducts.begin(), perQueryDotProducts.end());
    std::vector<double> annOpsDouble(perQueryAnnOps.begin(), perQueryAnnOps.end());
    double p95DotProducts = Percentile(dotProductsDouble, 0.95);
    double p95AnnOps = Percentile(annOpsDouble, 0.95);
    
    NS_LOG_UNCOND("=== Result (" << g_indexType << ") ===");
    NS_LOG_UNCOND("Throughput: " << throughputQps << " QPS, Latency (avg): " << avgUs << " us");
    NS_LOG_UNCOND("Compute: avgDotProducts=" << avgDotProducts 
                  << ", avgAnnOps=" << avgAnnOps
                  << ", avgBytesTouched=" << avgBytesTouchedKB << " KB");
    NS_LOG_UNCOND("Total: dotProducts=" << totalDotProducts 
                  << ", bytesTouched=" << (totalBytesTouched / 1024 / 1024) << " MB");
    
    mkdir(g_resultDir.c_str(), 0755);
    std::string summaryFile = g_resultDir + "/exp5_throughput.csv";
    bool writeHeader = false;
    { 
        std::ifstream check(summaryFile); 
        writeHeader = (!check.good() || check.peek() == std::ifstream::traits_type::eof()); 
    }
    
    std::ofstream sf(summaryFile, std::ios::app);
    if (writeHeader) {
        sf << "indexType,mode,domains,M,vectorDim,totalCentroids,sampleRatio,sampleSize,queries,topK,"
           << "efSearch,Mgraph,seed,buildMs,totalMs,avgUs,p50Us,p95Us,throughputQps,"
           << "totalDotProducts,avgDotProducts,p95DotProducts,totalAnnOps,avgAnnOps,p95AnnOps,"
           << "totalBytesTouched,avgBytesTouchedKB\n";
    }
    sf << g_indexType << "," << (g_useRealData ? "real" : "synthetic") << "," 
       << actualDomains << "," << g_M << "," << g_vectorDim << "," << totalCentroids
       << "," << g_sampleRatio << "," << sampleSize << "," << g_queries << "," << g_topK << ","
       << g_efSearch << "," << g_Mgraph << "," << g_seed << ","
       << buildMs << "," << totalMs << "," << avgUs << "," << p50Us << "," << p95Us << "," << throughputQps << ","
       << totalDotProducts << "," << avgDotProducts << "," << p95DotProducts << ","
       << totalAnnOps << "," << avgAnnOps << "," << p95AnnOps << ","
       << totalBytesTouched << "," << avgBytesTouchedKB << "\n";
    sf.close();
    
    // Also export per-query details for analysis
    std::string detailFile = g_resultDir + "/exp5_per_query.csv";
    std::ofstream df(detailFile);
    df << "queryId,latencyUs,dotProducts,annOps,bytesTouched\n";
    for (uint32_t q = 0; q < g_queries; ++q) {
        uint64_t dots = perQueryDotProducts[q];
        uint64_t annOps = perQueryAnnOps[q];
        uint64_t bytesTouched = (dots > 0 ? dots : annOps) * g_vectorDim * sizeof(float);
        df << q << "," << queryTimesUs[q] << "," << dots << "," << annOps << ","
           << bytesTouched << "\n";
    }
    df.close();
    NS_LOG_UNCOND("Exported per-query details to: " << detailFile);
    
    if (g_indexType == "hnsw") {
        delete hnswIndex;
        delete hnswSpace;
    }
    
    return 0;
}
