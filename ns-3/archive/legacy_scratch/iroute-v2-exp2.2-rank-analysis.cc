// Analyze: Where does the correct domain rank in centroid-based ranking?
#include "ns3/core-module.h"
#include "ns3/ndnSIM-module.h"
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace ns3;

std::string g_centroidsFile;
std::string g_traceFile;
std::string g_resultDir;
uint32_t g_vectorDim = 128;

struct QueryInfo {
    uint64_t queryId;
    std::string queryText;
    std::vector<float> vector;
    std::vector<std::string> targetDomains;
};

std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;
    
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(current);
    return fields;
}

std::vector<float> ParseVectorString(const std::string& s) {
    std::vector<float> vec;
    std::string cleaned = s;
    // Remove brackets
    for (char& c : cleaned) {
        if (c == '[' || c == ']') c = ' ';
    }
    std::istringstream iss(cleaned);
    float val;
    while (iss >> val) {
        vec.push_back(val);
        if (iss.peek() == ',' || iss.peek() == ' ') iss.ignore();
    }
    return vec;
}

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("centroids", "Centroids CSV file", g_centroidsFile);
    cmd.AddValue("trace", "Consumer trace CSV file", g_traceFile);
    cmd.AddValue("resultDir", "Output directory", g_resultDir);
    cmd.AddValue("vectorDim", "Vector dimension", g_vectorDim);
    cmd.Parse(argc, argv);
    
    system(("mkdir -p " + g_resultDir).c_str());
    
    // Load queries
    std::vector<QueryInfo> queries;
    {
        std::ifstream file(g_traceFile);
        std::string line;
        std::getline(file, line); // skip header
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            auto fields = ParseCsvLine(line);
            if (fields.size() < 5) continue;
            
            QueryInfo q;
            try {
                q.queryId = std::stoull(fields[0]);
            } catch (...) { continue; }
            q.queryText = fields[1];
            q.vector = ParseVectorString(fields[2]);
            
            // Parse target domains (;-separated)
            std::string domains = fields[4];
            std::istringstream dss(domains);
            std::string dom;
            while (std::getline(dss, dom, ';')) {
                if (!dom.empty()) q.targetDomains.push_back(dom);
            }
            
            if (q.vector.size() == g_vectorDim && !q.targetDomains.empty()) {
                queries.push_back(q);
            }
        }
    }
    std::cout << "Loaded " << queries.size() << " queries\n";
    
    // Load centroids per domain
    std::map<uint32_t, std::vector<std::vector<float>>> centroids;
    {
        std::ifstream file(g_centroidsFile);
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            auto fields = ParseCsvLine(line);
            if (fields.size() < 4) continue;
            
            try {
                uint32_t domainId = std::stoul(fields[0]);
                auto vec = ParseVectorString(fields[3]);
                if (vec.size() == g_vectorDim) {
                    centroids[domainId].push_back(vec);
                }
            } catch (...) { continue; }
        }
    }
    std::cout << "Loaded centroids for " << centroids.size() << " domains\n";
    
    // Analyze rank of correct domain
    std::ofstream out(g_resultDir + "/rank_analysis.csv");
    out << "query_id,num_target_domains,best_target_rank,best_target_score,top1_score,rank_gap\n";
    
    std::map<int, int> rankHistogram; // rank -> count
    int totalQueries = 0;
    
    for (const auto& q : queries) {
        // Compute score for each domain
        std::vector<std::pair<uint32_t, double>> domainScores;
        
        for (const auto& [domId, cents] : centroids) {
            double maxSim = -1.0;
            for (const auto& cent : cents) {
                // Cosine similarity (assume normalized)
                double sim = 0.0;
                for (size_t i = 0; i < g_vectorDim; ++i) {
                    sim += q.vector[i] * cent[i];
                }
                if (sim > maxSim) maxSim = sim;
            }
            domainScores.push_back({domId, maxSim});
        }
        
        // Sort by score descending
        std::sort(domainScores.begin(), domainScores.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // Find rank of best target domain
        int bestTargetRank = domainScores.size() + 1;
        double bestTargetScore = -1.0;
        
        for (const auto& td : q.targetDomains) {
            // Extract domain id from /domainX
            size_t pos = td.find("/domain");
            if (pos == std::string::npos) continue;
            uint32_t targetDomId = std::stoul(td.substr(pos + 7));
            
            for (size_t rank = 0; rank < domainScores.size(); ++rank) {
                if (domainScores[rank].first == targetDomId) {
                    if ((int)(rank + 1) < bestTargetRank) {
                        bestTargetRank = rank + 1;
                        bestTargetScore = domainScores[rank].second;
                    }
                    break;
                }
            }
        }
        
        double top1Score = domainScores.empty() ? 0.0 : domainScores[0].second;
        
        out << q.queryId << "," << q.targetDomains.size() << ","
            << bestTargetRank << "," << bestTargetScore << ","
            << top1Score << "," << (top1Score - bestTargetScore) << "\n";
        
        rankHistogram[bestTargetRank]++;
        totalQueries++;
    }
    
    out.close();
    
    // Print histogram
    std::cout << "\n=== Target Domain Rank Distribution ===\n";
    for (int rank = 1; rank <= 8; ++rank) {
        int count = rankHistogram[rank];
        double pct = 100.0 * count / totalQueries;
        std::cout << "Rank " << rank << ": " << count << " (" << pct << "%)\n";
    }
    
    // Cumulative
    std::cout << "\n=== Cumulative Hit@k ===\n";
    int cumulative = 0;
    for (int k = 1; k <= 8; ++k) {
        cumulative += rankHistogram[k];
        double pct = 100.0 * cumulative / totalQueries;
        std::cout << "Hit@" << k << ": " << cumulative << "/" << totalQueries 
                  << " (" << pct << "%)\n";
    }
    
    return 0;
}
