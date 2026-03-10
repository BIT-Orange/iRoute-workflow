/**
 * @file iroute-v2-exp2.2-debug.cc
 * @brief Debug script to investigate DOMAIN_WRONG failures
 *
 * Outputs detailed per-query analysis of domain selection vs ground truth
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/iroute-app.hpp"
#include "apps/iroute-discovery-consumer.hpp"
#include "apps/semantic-producer.hpp"
#include "extensions/iroute-vector.hpp"
#include "extensions/iroute-route-manager-registry.hpp"

#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <iomanip>
#include <filesystem>

using namespace ns3;
using namespace ns3::ndn;

NS_LOG_COMPONENT_DEFINE("iRouteDebug");

// =============================================================================
// Globals
// =============================================================================
static uint32_t g_vectorDim = 128;
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";
static std::string g_resultDir = "results/exp2.2_debug";

// =============================================================================
// Utility Functions
// =============================================================================

iroute::SemanticVector MakeSemanticVector(const std::vector<float>& v) {
    iroute::SemanticVector sv(v);
    sv.normalize();
    return sv;
}

std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> result;
    std::string current;
    bool inQuotes = false;
    int bracketDepth = 0;
    
    for (char c : line) {
        if (c == '"') inQuotes = !inQuotes;
        else if (c == '[') { bracketDepth++; current += c; }
        else if (c == ']') { bracketDepth--; current += c; }
        else if (c == ',' && !inQuotes && bracketDepth == 0) {
            result.push_back(current); current.clear();
        }
        else current += c;
    }
    if (!current.empty() || !result.empty()) result.push_back(current);
    return result;
}

std::vector<float> ParseVectorString(const std::string& str) {
    std::vector<float> result;
    std::string cleaned = str;
    if (!cleaned.empty() && cleaned.front() == '"') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == '"') cleaned.pop_back();
    if (!cleaned.empty() && cleaned.front() == '[') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == ']') cleaned.pop_back();
    
    std::stringstream ss(cleaned);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try { result.push_back(std::stof(token)); } catch (...) {}
    }
    return result;
}

// =============================================================================
// Data Structures
// =============================================================================

struct QueryEntry {
    std::string queryId;
    std::string queryText;
    std::vector<float> vector;
    std::vector<std::string> targetDocIds;
    std::vector<std::string> targetDomains;
};

struct ContentEntry {
    uint32_t domainId;
    std::string docId;
    std::vector<float> vector;
};

// =============================================================================
// Main Analysis
// =============================================================================

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("centroids", "Centroids CSV file", g_centroidsFile);
    cmd.AddValue("content", "Content CSV file", g_contentFile);
    cmd.AddValue("trace", "Query trace CSV file", g_traceFile);
    cmd.AddValue("resultDir", "Output directory", g_resultDir);
    cmd.Parse(argc, argv);
    
    std::filesystem::create_directories(g_resultDir);
    
    // Load queries
    std::vector<QueryEntry> queries;
    {
        std::ifstream file(g_traceFile);
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            auto fields = ParseCsvLine(line);
            if (fields.size() < 5) continue;
            
            QueryEntry q;
            q.queryId = fields[0];
            q.queryText = fields[1];
            q.vector = ParseVectorString(fields[2]);
            
            std::stringstream ss3(fields[3]);
            std::string docId;
            while (std::getline(ss3, docId, ';')) {
                if (!docId.empty()) q.targetDocIds.push_back(docId);
            }
            
            std::stringstream ss4(fields[4]);
            std::string domain;
            while (std::getline(ss4, domain, ';')) {
                if (!domain.empty()) q.targetDomains.push_back(domain);
            }
            
            queries.push_back(q);
        }
    }
    NS_LOG_UNCOND("Loaded " << queries.size() << " queries");
    
    // Load centroids
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> centroids;
    {
        std::ifstream file(g_centroidsFile);
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            auto fields = ParseCsvLine(line);
            if (fields.size() < 6) continue;
            
            try {
                uint32_t domainId = std::stoul(fields[0]);
                uint32_t centroidId = std::stoul(fields[1]);
                auto vec = ParseVectorString(fields[3]);
                double radius = std::stod(fields[4]);
                double weight = std::stod(fields[5]);
                
                iroute::CentroidEntry c;
                c.centroidId = centroidId;
                c.C = MakeSemanticVector(vec);
                c.radius = radius;
                c.weight = weight;
                
                centroids[domainId].push_back(c);
            } catch (...) { continue; }
        }
    }
    NS_LOG_UNCOND("Loaded centroids for " << centroids.size() << " domains");
    
    // Load content for per-domain document vectors
    std::map<uint32_t, std::map<std::string, std::vector<float>>> contentByDomain;
    {
        std::ifstream file(g_contentFile);
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            auto fields = ParseCsvLine(line);
            if (fields.size() < 4) continue;
            
            try {
                uint32_t domainId = std::stoul(fields[0]);
                std::string docId = fields[1];
                auto vec = ParseVectorString(fields[3]);
                contentByDomain[domainId][docId] = vec;
            } catch (...) { continue; }
        }
    }
    NS_LOG_UNCOND("Loaded content for " << contentByDomain.size() << " domains");
    
    // Analyze each query
    std::ofstream debugFile(g_resultDir + "/domain_wrong_analysis.csv");
    debugFile << "query_id,query_text,target_domains,selected_domain,is_correct,top1_domain,top1_score,target_max_score,score_gap\n";
    
    uint32_t domainCorrect = 0, domainWrong = 0;
    double totalScoreGap = 0;
    
    for (const auto& q : queries) {
        if (q.vector.size() != g_vectorDim) continue;
        
        auto qVec = MakeSemanticVector(q.vector);
        
        // Compute score for each domain using centroid matching
        std::vector<std::pair<uint32_t, double>> domainScores;
        for (const auto& [domId, cents] : centroids) {
            double maxSim = -1.0;
            for (const auto& c : cents) {
                if (c.C.getDimension() == qVec.getDimension()) {
                    double sim = qVec.dot(c.C);
                    if (sim > maxSim) maxSim = sim;
                }
            }
            if (maxSim >= 0) {
                domainScores.push_back({domId, maxSim});
            }
        }
        
        // Sort by score
        std::sort(domainScores.begin(), domainScores.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        if (domainScores.empty()) continue;
        
        std::string top1Domain = "/domain" + std::to_string(domainScores[0].first);
        double top1Score = domainScores[0].second;
        
        // Check if top1 is in target domains
        bool isCorrect = false;
        for (const auto& td : q.targetDomains) {
            if (td == top1Domain) {
                isCorrect = true;
                break;
            }
        }
        
        // Find max score among target domains
        double targetMaxScore = -1.0;
        for (const auto& td : q.targetDomains) {
            // Extract domain id from /domainX
            size_t pos = td.find("/domain");
            if (pos != std::string::npos) {
                try {
                    uint32_t targetDomId = std::stoul(td.substr(pos + 7));
                    for (const auto& ds : domainScores) {
                        if (ds.first == targetDomId) {
                            if (ds.second > targetMaxScore) targetMaxScore = ds.second;
                            break;
                        }
                    }
                } catch (...) {}
            }
        }
        
        double scoreGap = top1Score - targetMaxScore;
        
        if (isCorrect) domainCorrect++;
        else {
            domainWrong++;
            totalScoreGap += scoreGap;
        }
        
        // Build target domains string
        std::string targetDomainsStr;
        for (size_t i = 0; i < q.targetDomains.size(); ++i) {
            if (i > 0) targetDomainsStr += ";";
            targetDomainsStr += q.targetDomains[i];
        }
        
        debugFile << q.queryId << ","
                  << "\"" << q.queryText << "\","
                  << "\"" << targetDomainsStr << "\","
                  << top1Domain << ","
                  << (isCorrect ? "yes" : "no") << ","
                  << top1Domain << ","
                  << std::fixed << std::setprecision(4) << top1Score << ","
                  << std::fixed << std::setprecision(4) << targetMaxScore << ","
                  << std::fixed << std::setprecision(4) << scoreGap << "\n";
    }
    
    debugFile.close();
    
    NS_LOG_UNCOND("\n=== Domain Selection Analysis ===");
    NS_LOG_UNCOND("Domain Correct: " << domainCorrect << " (" << (100.0 * domainCorrect / (domainCorrect + domainWrong)) << "%)");
    NS_LOG_UNCOND("Domain Wrong: " << domainWrong << " (" << (100.0 * domainWrong / (domainCorrect + domainWrong)) << "%)");
    NS_LOG_UNCOND("Avg Score Gap (wrong cases): " << (domainWrong > 0 ? totalScoreGap / domainWrong : 0));
    NS_LOG_UNCOND("\nDetailed analysis saved to: " << g_resultDir << "/domain_wrong_analysis.csv");
    
    // Additional analysis: queries where target domain has multiple relevant docs
    NS_LOG_UNCOND("\n=== Queries with Multi-Domain Targets ===");
    uint32_t multiDomainQueries = 0;
    for (const auto& q : queries) {
        if (q.targetDomains.size() > 1) {
            multiDomainQueries++;
        }
    }
    NS_LOG_UNCOND("Queries with multiple target domains: " << multiDomainQueries << "/" << queries.size());
    
    return 0;
}
