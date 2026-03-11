/**
 * @file iroute-exp-baselines.cc
 * @brief Unified multi-scheme experiment driver with per-query and run-level CSV logging.
 *
 * Supports schemes via --scheme CLI parameter:
 *   iroute  — Semantic routing with Top-K probing
 *   exact   — Exact-NDN syntax baseline (tokenized query name, dictionary lookup)
 *   central — Centralized search oracle
 *   flood   — Broadcast discovery to ALL domains
 *   tag     — Tag-based keyword matching with Random-K probing
 *   sanr-tag    — SANR-CMF abstraction on top of Tag forwarding
 *   sanr-oracle — SANR-CMF abstraction with oracle domain selection
 *
 * Output:
 *   results/baselines/query_log.csv   — per-query detailed metrics
 *   results/baselines/summary.csv     — run-level aggregated metrics
 *
 * Usage:
 *   ./waf --run "iroute-exp-baselines --scheme=iroute \
 *     --centroids=dataset/sdm_smartcity_dataset/domain_centroids.csv \
 *     --content=dataset/sdm_smartcity_dataset/producer_content.csv \
 *     --trace=dataset/sdm_smartcity_dataset/consumer_trace.csv \
 *     --domains=8 --simTime=300"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/error-model.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/NFD/daemon/table/fib.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"

#include "ns3/ndnSIM/apps/iroute-app.hpp"
#include "ns3/ndnSIM/apps/iroute-discovery-consumer.hpp"
#include "ns3/ndnSIM/apps/flooding-discovery-consumer.hpp"
#include "ns3/ndnSIM/apps/search-oracle-app.hpp"
#include "ns3/ndnSIM/apps/centralized-search-consumer.hpp"
#include "ns3/ndnSIM/apps/random-k-discovery-consumer.hpp"
#include "ns3/ndnSIM/apps/semantic-producer.hpp"
#include "ns3/ndnSIM/apps/exact-ndn-consumer.hpp"
#include "ns3/ndnSIM/apps/exact-ndn-producer.hpp"
#include "ns3/ndnSIM/apps/directory-server-app.hpp"
#include "ns3/ndnSIM/apps/central-dir-consumer.hpp"
#include "ns3/ndnSIM/apps/flood-responder-app.hpp"
#include "ns3/ndnSIM/apps/tag-router-app.hpp"
#include "ns3/ndnSIM/apps/tag-discovery-consumer.hpp"
#include "ns3/ndnSIM/apps/tag-domain-app.hpp"

#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <iomanip>
#include <numeric>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <limits>

#include "iroute-exp-results.hpp"

using namespace ns3;
using namespace ns3::ndn;
using irouteexp::CheckDomainHit;
using irouteexp::FailureSanityRecord;
using irouteexp::JoinStr;
using irouteexp::ManifestConfig;
using irouteexp::QueryLog;
using irouteexp::SplitBySemicolon;
using irouteexp::SummaryConfig;
using irouteexp::SummaryStats;

NS_LOG_COMPONENT_DEFINE("iRouteExpBaselines");

// =============================================================================
// Global Parameters
// =============================================================================
static std::string g_scheme = "iroute";
static uint32_t g_seed = 42;
static uint32_t g_vectorDim = 128;
static uint32_t g_domains = 8;
static uint32_t g_M = 4;         // centroids per domain
static uint32_t g_K = 5;         // top-K probes (iRoute)
static double   g_tau = 0.3;     // score threshold
static double   g_simTime = 300.0;
static uint32_t g_fetchTimeoutMs = 4000;
static uint32_t g_ttl = 3;       // flood TTL (reserved)
static uint32_t g_tagK = 3;      // tag shortlist size
static uint32_t g_floodProbeBudget = 0; // 0 means all domains
static std::string g_resultDir = "results/baselines";
static double   g_frequency = 2.0;
static std::string g_topo = "star"; // star | rocketfuel | redundant
static std::string g_topoFile = "src/ndnSIM/examples/topologies/as1239-r0.txt";
static uint32_t g_ingressNodeIndex = 0;
static double   g_linkDelayMs = 2.0;
static uint32_t g_linkDelayJitterUs = 0;
static bool     g_rocketfuelAutoJitter = true;
static uint32_t g_serviceJitterUs = 0;
static uint32_t g_dataFreshnessMs = 60000;
static double   g_warmupSec = 20.0;
static double   g_measureStartSec = 20.0;
static uint32_t g_csSize = 1000;
static bool     g_shuffleTrace = true;
static bool     g_cdfSuccessOnly = true;

// Data files
static std::string g_centroidsFile = "";
static std::string g_traceFile = "";
static std::string g_contentFile = "";
static std::string g_indexFile = "";  // index_exact.csv for exact scheme
static bool g_exactUseIndex = true;   // true=oracle exact mapping, false=intent-only strict exact baseline

// Central directory params
static std::string g_dirMode = "oracle";
static uint32_t g_dirProcMs = 2;
static uint32_t g_dirProcJitterUs = 0;
static uint32_t g_dirTopK = 5;
static double   g_dirFailTime = -1.0;  // negative = no failure
static std::string g_qrelsFile = "";

// Flood params
static std::string g_floodResponder = "producer"; // domain | producer
static double      g_floodThreshold = 0.0;         // similarity threshold
// Tag params
static uint32_t g_lsaPeriodMs = 500;
static double   g_tagChurnTime = -1.0; // Deprecated by g_churn? Keep for compatibility or remove?
// SANR-CMF params
static double   g_sanrSimThresh = 0.8;   // semantic hit gate
static double   g_sanrMsrrTopPct = 0.2;  // select top X% semantic clusters
static double   g_sanrCmltSec = 4.0;     // content max lifetime
static double   g_sanrSlotSec = 5.0;     // MSRR/placement slot
static uint32_t g_sanrCcnK = 1;          // CCN count per domain (heuristic)
static uint32_t g_sanrTopL = 32;         // top-L candidates in semantic index lookup
static bool     g_sanrEnableSemantic = true;
// Failure / Churn params (Exp 3)
static std::string g_failLink = "";    // "u-v@time"
static std::string g_failDomain = "";  // "d@time"
static std::string g_churn = "";       // "type@time@ratio"
static std::string g_failureTargetPolicy = "auto-critical"; // auto-critical | auto-noncut | manual
static uint32_t g_failHotDomainRank = 0;
static double   g_failRecoverySec = -1.0; // >0 enables transient link failure recovery
static double   g_churnRecoverySec = 20.0; // >0 enables churn rollback
static uint32_t g_churnRounds = 1; // number of churn events
static double   g_churnIntervalSec = 10.0; // interval between churn events

#include "ns3/names.h"
#include "ns3/node-list.h"
static std::string g_tagIndexFile = "";
static std::string g_queryToTagFile = "";
static std::string g_runFailureScenario = "none";
static std::string g_runFailureTarget = "";
static std::set<std::pair<uint32_t, uint32_t>> g_failedEdges;
static int g_partitionDetected = 0;
static int g_failureEffective = 0;
static std::map<std::string, iroute::DomainEntry> g_irouteChurnBackup;

static FailureSanityRecord g_failureSanity;

// =============================================================================
// Utility Functions
// =============================================================================

void CreateDirectories(const std::string& path) {
    std::filesystem::create_directories(path);
}

iroute::SemanticVector MakeSemanticVector(const std::vector<float>& v) {
    iroute::SemanticVector sv(v);
    sv.normalize();
    return sv;
}

double ToMs(const Time& t) {
    return t.ToDouble(Time::MS);
}

uint64_t ToNs(const Time& t) {
    return static_cast<uint64_t>(t.GetNanoSeconds() > 0 ? t.GetNanoSeconds() : 0);
}

double DeltaMs(const Time& end, const Time& begin) {
    if (end < begin) {
        return 0.0;
    }
    return (end - begin).ToDouble(Time::MS);
}

Time SampleSimpleLinkDelay() {
    double baseUs = std::max(100.0, g_linkDelayMs * 1000.0);
    if (g_linkDelayJitterUs == 0) {
        return MicroSeconds(static_cast<int64_t>(std::llround(baseUs)));
    }

    static std::mt19937 rng;
    static bool initialized = false;
    if (!initialized) {
        rng.seed(g_seed + 1337u);
        initialized = true;
    }
    std::uniform_real_distribution<double> dist(
        -static_cast<double>(g_linkDelayJitterUs),
        static_cast<double>(g_linkDelayJitterUs));
    double us = std::max(100.0, baseUs + dist(rng));
    return MicroSeconds(static_cast<int64_t>(std::llround(us)));
}

std::pair<uint32_t, uint32_t> NormalizeEdge(uint32_t a, uint32_t b) {
    if (a <= b) {
        return {a, b};
    }
    return {b, a};
}

int ParseDomainId(const std::string& text) {
    std::string s = text;
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    s = s.substr(b, e - b);
    if (s.empty()) {
        return -1;
    }
    auto pos = s.find("domain");
    if (pos != std::string::npos) {
        std::string tail = s.substr(pos + std::string("domain").size());
        try {
            return std::stoi(tail);
        } catch (...) {
        }
    }
    try {
        return std::stoi(s);
    } catch (...) {
        return -1;
    }
}

std::string EnsureNodeName(Ptr<Node> node, const std::string& hint = "") {
    if (!node) {
        return "";
    }
    std::string existing = Names::FindName(node);
    if (!existing.empty()) {
        return existing;
    }
    std::string base = hint.empty() ? ("node" + std::to_string(node->GetId())) : hint;
    for (uint32_t i = 0; i < 64; ++i) {
        std::string candidate = (i == 0) ? base : (base + "_" + std::to_string(i));
        try {
            Names::Add(candidate, node);
            return candidate;
        } catch (...) {
        }
    }
    return "node" + std::to_string(node->GetId());
}

std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> result;
    std::string current;
    bool inQuotes = false;
    int bracketDepth = 0;

    for (char c : line) {
        if (c == '\r') {
            continue; // tolerate CRLF input files
        }
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == '[') {
            bracketDepth++;
            current += c;
        } else if (c == ']') {
            bracketDepth--;
            current += c;
        } else if (c == ',' && !inQuotes && bracketDepth == 0) {
            result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty() || !result.empty()) {
        result.push_back(current);
    }
    return result;
}

std::string TrimCopy(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
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

// Escape CSV field (wrap in quotes if contains comma)
std::string CsvEscape(const std::string& s) {
    if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
        std::string escaped = s;
        // Double any existing quotes
        size_t pos = 0;
        while ((pos = escaped.find('"', pos)) != std::string::npos) {
            escaped.insert(pos, "\"");
            pos += 2;
        }
        return "\"" + escaped + "\"";
    }
    return s;
}

// =============================================================================
// Data Loading
// =============================================================================

struct ContentEntry {
    uint32_t domainId;
    std::string docId;
    std::string canonicalName;
    std::vector<float> vector;
    bool isDistractor = false;
};

std::map<uint32_t, std::vector<ContentEntry>> LoadContentFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<ContentEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open content file: " << filename);
        return result;
    }

    std::string line;
    std::getline(file, line);  // Header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 4) continue;

        ContentEntry e;
        try {
            e.domainId = std::stoul(fields[0]);
            e.docId = fields[1];
            e.canonicalName = fields[2];
            e.vector = ParseVectorString(fields[3]);
            if (fields.size() > 4) {
                e.isDistractor = (fields[4] == "1" || fields[4] == "true");
            }
        } catch (...) { continue; }

        result[e.domainId].push_back(e);
    }
    NS_LOG_INFO("Loaded content: " << result.size() << " domains");
    return result;
}

std::vector<IRouteDiscoveryConsumer::QueryItem> LoadTraceFromCsv(const std::string& filename) {
    std::vector<IRouteDiscoveryConsumer::QueryItem> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open trace file: " << filename);
        return result;
    }

    std::string line;
    std::getline(file, line);  // Header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 5) continue;

        IRouteDiscoveryConsumer::QueryItem item;
        try {
            // fields: query_id(0), query_text(1), vector(2), target_docids(3), target_domains(4)
            std::string qidStr = fields[0];
            if (!qidStr.empty() && qidStr[0] == 'q') qidStr = qidStr.substr(1);
            item.id = std::stoul(qidStr);
            item.queryText = fields[1];
            auto vec = ParseVectorString(fields[2]);
            if (vec.empty()) continue;
            item.vector = MakeSemanticVector(vec);

            // Parse target doc IDs
            if (!fields[3].empty()) {
                std::stringstream ss(fields[3]);
                std::string docId;
                while (std::getline(ss, docId, ';')) {
                    docId = TrimCopy(docId);
                    if (!docId.empty()) item.targetDocIds.push_back(docId);
                }
            }

            // Parse target domains
            if (!fields[4].empty()) {
                std::stringstream ss(fields[4]);
                std::string domain;
                while (std::getline(ss, domain, ';')) {
                    domain = TrimCopy(domain);
                    if (!domain.empty()) item.targetDomains.push_back(domain);
                }
            }

            if (!item.targetDomains.empty()) {
                item.expectedDomain = item.targetDomains[0];
            }
        } catch (...) { continue; }

        result.push_back(item);
    }

    NS_LOG_INFO("Loaded trace: " << result.size() << " queries");
    return result;
}

std::map<uint32_t, std::vector<iroute::CentroidEntry>> LoadCentroidsFromCsv(const std::string& filename) {
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open centroids file: " << filename);
        return result;
    }

    std::string line;
    std::getline(file, line);  // Header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = ParseCsvLine(line);
        if (fields.size() < 6) continue;

        try {
            uint32_t domainId = std::stoul(fields[0]);
            uint32_t centroidId = std::stoul(fields[1]);
            auto vec = ParseVectorString(fields[3]);
            if (vec.empty()) continue;
            double radius = std::stod(fields[4]);
            double weight = std::stod(fields[5]);

            iroute::CentroidEntry c;
            c.centroidId = centroidId;
            c.C = MakeSemanticVector(vec);
            c.radius = radius;
            c.weight = weight;

            result[domainId].push_back(c);
        } catch (...) { continue; }
    }

    NS_LOG_INFO("Loaded centroids: " << result.size() << " domains");
    return result;
}

// =============================================================================
// Unified QueryLog Structure
// =============================================================================

// Result structs and artifact writers now live in iroute-exp-results.hpp.

// =============================================================================
// Helper: Convert TxRecord → QueryLog
// =============================================================================

SummaryConfig BuildSummaryConfig() {
    SummaryConfig config;
    config.topK = g_K;
    config.measureStartSec = g_measureStartSec;
    config.cdfSuccessOnly = g_cdfSuccessOnly;
    return config;
}

ManifestConfig BuildManifestConfig() {
    ManifestConfig config;
    config.scheme = g_scheme;
    config.seed = g_seed;
    config.topology = g_topo;
    config.domains = g_domains;
    config.simTime = g_simTime;
    config.frequency = g_frequency;
    config.warmupSec = g_warmupSec;
    config.measureStartSec = g_measureStartSec;
    config.csSize = g_csSize;
    config.dataFreshnessMs = g_dataFreshnessMs;
    config.sanrSimThresh = g_sanrSimThresh;
    config.sanrMsrrTopPct = g_sanrMsrrTopPct;
    config.sanrCmltSec = g_sanrCmltSec;
    config.sanrSlotSec = g_sanrSlotSec;
    config.sanrCcnK = g_sanrCcnK;
    config.sanrTopL = g_sanrTopL;
    config.cacheMode = g_csSize > 0 ? "enabled" : "disabled";
    config.runMode = "direct_driver";
    config.seedProvenance = "native";
    return config;
}

bool IsInMeasurementWindow(const QueryLog& q) {
    return irouteexp::IsInMeasurementWindow(q, g_measureStartSec);
}

SummaryStats BuildRunSummary(const std::string& scheme,
                             const std::vector<QueryLog>& logs,
                             double simTimeSec) {
    return irouteexp::ComputeSummary(scheme, logs, simTimeSec, BuildSummaryConfig());
}

void WriteRunQueryLog(const std::vector<QueryLog>& logs, const std::string& dir) {
    irouteexp::WriteQueryLog(logs, dir);
}

void WriteRunSummary(const SummaryStats& s, const std::string& dir) {
    irouteexp::WriteSummary(s, dir);
}

void WriteRunLatencySanity(const std::vector<QueryLog>& logs, const std::string& dir) {
    irouteexp::WriteLatencySanity(logs, dir, g_scheme, BuildSummaryConfig());
}

void WriteRunManifest(const SummaryStats& s, const std::string& dir) {
    irouteexp::WriteManifest(s, dir, BuildManifestConfig());
}

void WriteRunFailureSanity(const std::vector<QueryLog>& logs, const std::string& dir) {
    irouteexp::WriteFailureSanity(logs, dir, &g_failureSanity, &g_failureEffective, g_failRecoverySec);
}

// Check if predicted doc matches any ground-truth doc ID
bool CheckDocHit(const std::string& predName, const std::vector<std::string>& gtDocs) {
    for (const auto& doc : gtDocs) {
        std::string docNorm = TrimCopy(doc);
        if (!docNorm.empty() && predName.find(docNorm) != std::string::npos) return true;
    }
    return false;
}

bool LooksLikeTimeout(const std::string& reason) {
    std::string u = reason;
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return u.find("TIMEOUT") != std::string::npos;
}

void FinalizeQueryLog(QueryLog& q, bool success, const std::string& failureReason) {
    q.disc_ms = 0.0;
    q.fetch_ms = 0.0;

    if (q.t_recv_disc_reply > 0.0 && q.t_send_disc > 0.0 && q.t_recv_disc_reply >= q.t_send_disc) {
        q.disc_ms = q.t_recv_disc_reply - q.t_send_disc;
    }
    if (q.t_recv_data > 0.0 && q.t_send_fetch > 0.0 && q.t_recv_data >= q.t_send_fetch) {
        q.fetch_ms = q.t_recv_data - q.t_send_fetch;
    }

    // Prefer ns-level RTT reconstruction to avoid artifacts from coarse formatting.
    if (q.t_send_disc_ns > 0 && q.t_recv_data_ns >= q.t_send_disc_ns) {
        q.rtt_total_ms = static_cast<double>(q.t_recv_data_ns - q.t_send_disc_ns) / 1e6;
    }

    q.is_success = success ? 1 : 0;
    q.timeout_reason = failureReason;
    q.is_timeout = (!success && LooksLikeTimeout(failureReason)) ? 1 : 0;
    if (!success && q.is_timeout == 0 && q.rtt_total_ms >= static_cast<double>(g_fetchTimeoutMs) - 1.0) {
        q.is_timeout = 1;
        if (q.timeout_reason.empty()) {
            q.timeout_reason = "TIMEOUT";
        }
    }
    q.timeouts = q.is_timeout ? std::max<uint32_t>(1, q.timeouts) : q.timeouts;

    if (q.rtt_total_ms <= 0.0) {
        if (q.fetch_ms > 0.0) {
            q.rtt_total_ms = q.disc_ms + q.fetch_ms;
        } else if (q.disc_ms > 0.0) {
            q.rtt_total_ms = q.disc_ms;
        }
    }

    q.failure_scenario = g_runFailureScenario;
    q.failure_target = g_runFailureTarget;
    q.topology = g_topo;
    q.hops_total = (q.hops_disc >= 0 && q.hops_fetch >= 0) ? (q.hops_disc + q.hops_fetch) : -1;
    q.is_measurable = (!q.gt_domain.empty() && IsInMeasurementWindow(q)) ? 1 : 0;
}

QueryLog TxRecordToQueryLog(uint32_t qid, const std::string& scheme,
                            const TxRecord& tx,
                            const IRouteDiscoveryConsumer::QueryItem& qi) {
    QueryLog q;
    q.qid = qid;
    q.scheme = scheme;
    q.query_text = qi.queryText;
    q.gt_domain = JoinStr(qi.targetDomains);
    q.gt_doc = JoinStr(qi.targetDocIds);
    q.pred_domain = tx.finalSuccessDomain.empty() ? tx.selectedDomain : tx.finalSuccessDomain;
    q.pred_doc = tx.requestedName;
    q.topk_domains = tx.topKList;
    // Only count document hit if Stage-2 fetch succeeded
    if (!tx.stage2Success) {
        q.pred_doc.clear();
    }
    q.domain_hit = CheckDomainHit(q.pred_domain, qi.targetDomains) ? 1 : 0;
    q.hit_exact = (tx.stage2Success && CheckDocHit(q.pred_doc, qi.targetDocIds)) ? 1 : 0;
    q.ssr_score = tx.confidence;

    q.t_send_disc = ToMs(tx.tStage1Send);
    q.t_recv_disc_reply = ToMs(tx.tStage1Recv);
    q.t_send_fetch = ToMs(tx.tStage2Send);
    q.t_recv_data = ToMs(tx.tStage2Recv);
    q.t_send_disc_ns = ToNs(tx.tStage1Send);
    q.t_recv_disc_reply_ns = ToNs(tx.tStage1Recv);
    q.t_send_fetch_ns = ToNs(tx.tStage2Send);
    q.t_recv_data_ns = ToNs(tx.tStage2Recv);
    q.rtt_total_ms = tx.totalMs > 0.0 ? tx.totalMs : DeltaMs(tx.tStage2Recv, tx.tStage1Send);
    q.hops_disc = -1;  // Not tracked at Interest level currently
    q.hops_fetch = tx.stage2HopCount;

    q.n_interest_sent = tx.probesUsed > 0 ? tx.probesUsed : 1;
    q.n_data_recv = tx.stage2Success ? 1 : 0;

    q.bytes_ctrl_tx = tx.stage1InterestBytes;
    q.bytes_ctrl_rx = tx.stage1DataBytes;
    q.bytes_data_tx = tx.stage2InterestBytes;
    q.bytes_data_rx = tx.stage2DataBytes;

    q.timeouts = tx.failureReason.find("TIMEOUT") != std::string::npos ? 1 : 0;
    q.retransmissions = 0;
    FinalizeQueryLog(q, tx.stage2Success, tx.failureReason);
    return q;
}

QueryLog FloodingTxToQueryLog(uint32_t qid, const std::string& scheme,
                              const FloodingTxRecord& tx,
                              const IRouteDiscoveryConsumer::QueryItem& qi) {
    QueryLog q = TxRecordToQueryLog(qid, scheme, tx, qi);
    q.n_interest_sent = tx.totalInterestsSent;
    q.timeouts = tx.timeoutsOccurred;
    return q;
}

QueryLog ExactTxToQueryLog(uint32_t qid,
                           const ExactTxRecord& tx,
                           const IRouteDiscoveryConsumer::QueryItem& qi) {
    QueryLog q;
    q.qid = qid;
    q.scheme = "exact";
    q.query_text = qi.queryText;
    q.gt_domain = JoinStr(qi.targetDomains);
    q.gt_doc = JoinStr(qi.targetDocIds);

    if (tx.found) {
        q.pred_domain = tx.selectedDomain;
        q.pred_doc = tx.parsedCanonical;
        q.domain_hit = CheckDomainHit(q.pred_domain, qi.targetDomains) ? 1 : 0;
        q.hit_exact = CheckDocHit(q.pred_doc, qi.targetDocIds) ? 1 : 0;
    }
    q.ssr_score = 0.0;  // no semantic score for exact match

    q.t_send_disc = ToMs(tx.tStage1Send);
    q.t_recv_disc_reply = ToMs(tx.tStage1Recv);
    q.t_send_fetch = 0;  // single-stage
    q.t_recv_data = ToMs(tx.tStage1Recv);
    q.t_send_disc_ns = ToNs(tx.tStage1Send);
    q.t_recv_disc_reply_ns = ToNs(tx.tStage1Recv);
    q.t_send_fetch_ns = 0;
    q.t_recv_data_ns = ToNs(tx.tStage1Recv);
    q.rtt_total_ms = tx.totalMs > 0.0 ? tx.totalMs : DeltaMs(tx.tStage1Recv, tx.tStage1Send);
    q.hops_disc = -1;
    q.hops_fetch = tx.stage2HopCount;

    q.n_interest_sent = 1;
    q.n_data_recv = tx.found ? 1 : 0;

    q.bytes_ctrl_tx = tx.stage1InterestBytes;
    q.bytes_ctrl_rx = tx.stage1DataBytes;
    q.bytes_data_tx = 0;
    q.bytes_data_rx = 0;

    q.timeouts = tx.found ? 0 : 1;
    q.retransmissions = 0;
    FinalizeQueryLog(q, tx.found, tx.failureReason);
    return q;
}

// =============================================================================
// Central Directory TxRecord → QueryLog
// =============================================================================

QueryLog CentralDirTxToQueryLog(uint32_t qid,
                                const CentralDirTxRecord& tx,
                                const IRouteDiscoveryConsumer::QueryItem& qi) {
    QueryLog q;
    q.qid = qid;
    q.scheme = "central";
    q.query_text = qi.queryText;
    q.gt_domain = JoinStr(qi.targetDomains);
    q.gt_doc = JoinStr(qi.targetDocIds);

    if (tx.directoryHit) {
        q.pred_domain = "/domain" + std::to_string(tx.top1DomainId);
        q.pred_doc = tx.top1Canonical;
        q.domain_hit = CheckDomainHit(q.pred_domain, qi.targetDomains) ? 1 : 0;
        q.hit_exact = CheckDocHit(q.pred_doc, qi.targetDocIds) ? 1 : 0;
    }
    q.ssr_score = tx.confidence;

    q.t_send_disc = ToMs(tx.tStage1Send);
    q.t_recv_disc_reply = ToMs(tx.tStage1Recv);
    q.t_send_fetch = ToMs(tx.tStage2Send);
    q.t_recv_data = ToMs(tx.tStage2Recv);
    q.t_send_disc_ns = ToNs(tx.tStage1Send);
    q.t_recv_disc_reply_ns = ToNs(tx.tStage1Recv);
    q.t_send_fetch_ns = ToNs(tx.tStage2Send);
    q.t_recv_data_ns = ToNs(tx.tStage2Recv);
    q.rtt_total_ms = tx.totalMs > 0.0 ? tx.totalMs : DeltaMs(tx.tStage2Recv, tx.tStage1Send);
    q.hops_disc = -1;
    q.hops_fetch = -1;

    q.n_interest_sent = tx.directoryHit ? 2 : 1;  // disc + optional fetch
    q.n_data_recv = (tx.directoryHit ? 1 : 0) + (tx.fetchSuccess ? 1 : 0);

    q.bytes_ctrl_tx = tx.stage1InterestBytes;
    q.bytes_ctrl_rx = tx.stage1DataBytes;
    q.bytes_data_tx = tx.stage2InterestBytes;
    q.bytes_data_rx = tx.stage2DataBytes;

    q.timeouts = tx.failureReason_str.find("TIMEOUT") != std::string::npos ? 1 : 0;
    q.retransmissions = 0;
    FinalizeQueryLog(q, tx.fetchSuccess, tx.failureReason_str);
    return q;
}

// =============================================================================
// Simulation Class
// =============================================================================

// =============================================================================
// Failure & Churn Helpers
// =============================================================================

struct TopologyLayout {
    NodeContainer allNodes;
    Ptr<Node> ingress;
    std::vector<Ptr<Node>> domainNodes;
    std::vector<uint32_t> hopFromIngress; // one per domain node
};

std::map<uint32_t, uint32_t> BfsAllDistances(Ptr<Node> src) {
    std::map<uint32_t, uint32_t> dist;
    if (!src) {
        return dist;
    }
    std::queue<Ptr<Node>> q;
    dist[src->GetId()] = 0;
    q.push(src);

    while (!q.empty()) {
        Ptr<Node> cur = q.front();
        q.pop();
        uint32_t curDist = dist[cur->GetId()];

        for (uint32_t i = 0; i < cur->GetNDevices(); ++i) {
            Ptr<NetDevice> dev = cur->GetDevice(i);
            Ptr<Channel> ch = dev->GetChannel();
            if (!ch) {
                continue;
            }
            for (uint32_t j = 0; j < ch->GetNDevices(); ++j) {
                Ptr<Node> nxt = ch->GetDevice(j)->GetNode();
                if (nxt == cur) {
                    continue;
                }
                if (dist.find(nxt->GetId()) != dist.end()) {
                    continue;
                }
                dist[nxt->GetId()] = curDist + 1;
                q.push(nxt);
            }
        }
    }
    return dist;
}

std::vector<Ptr<Node>> CollectIngressPathNodes(const TopologyLayout& topo) {
    std::vector<Ptr<Node>> out;
    if (!topo.ingress) {
        return out;
    }

    std::queue<Ptr<Node>> q;
    std::set<uint32_t> visited;
    std::map<uint32_t, uint32_t> parent;
    visited.insert(topo.ingress->GetId());
    parent[topo.ingress->GetId()] = topo.ingress->GetId();
    q.push(topo.ingress);

    while (!q.empty()) {
        Ptr<Node> cur = q.front();
        q.pop();
        for (uint32_t i = 0; i < cur->GetNDevices(); ++i) {
            Ptr<NetDevice> dev = cur->GetDevice(i);
            Ptr<Channel> ch = dev->GetChannel();
            if (!ch) {
                continue;
            }
            for (uint32_t j = 0; j < ch->GetNDevices(); ++j) {
                Ptr<Node> nxt = ch->GetDevice(j)->GetNode();
                if (!nxt || nxt == cur) {
                    continue;
                }
                if (visited.count(nxt->GetId()) > 0) {
                    continue;
                }
                visited.insert(nxt->GetId());
                parent[nxt->GetId()] = cur->GetId();
                q.push(nxt);
            }
        }
    }

    std::set<uint32_t> keepIds;
    keepIds.insert(topo.ingress->GetId());
    for (const auto& dn : topo.domainNodes) {
        if (!dn) {
            continue;
        }
        uint32_t curId = dn->GetId();
        keepIds.insert(curId);
        auto it = parent.find(curId);
        if (it == parent.end()) {
            continue;
        }
        while (true) {
            keepIds.insert(curId);
            auto pit = parent.find(curId);
            if (pit == parent.end() || pit->second == curId) {
                break;
            }
            curId = pit->second;
        }
    }

    for (uint32_t i = 0; i < topo.allNodes.GetN(); ++i) {
        Ptr<Node> n = topo.allNodes.Get(i);
        if (n && keepIds.count(n->GetId()) > 0) {
            out.push_back(n);
        }
    }
    return out;
}

std::vector<Ptr<Node>> SelectGatewayNodes(const NodeContainer& allNodes,
                                          Ptr<Node> ingress,
                                          uint32_t domains) {
    std::vector<Ptr<Node>> candidates;
    for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
        Ptr<Node> n = allNodes.Get(i);
        if (n != ingress) {
            candidates.push_back(n);
        }
    }
    if (candidates.empty()) {
        return {};
    }
    if (domains >= candidates.size()) {
        return candidates;
    }

    std::vector<Ptr<Node>> selected;
    std::unordered_set<uint32_t> selectedIds;

    auto ingressDist = BfsAllDistances(ingress);
    Ptr<Node> first = candidates.front();
    uint32_t bestDist = 0;
    for (auto& c : candidates) {
        uint32_t d = std::numeric_limits<uint32_t>::max();
        auto it = ingressDist.find(c->GetId());
        if (it != ingressDist.end()) {
            d = it->second;
        }
        if (d > bestDist && d != std::numeric_limits<uint32_t>::max()) {
            bestDist = d;
            first = c;
        }
    }
    selected.push_back(first);
    selectedIds.insert(first->GetId());

    while (selected.size() < domains) {
        Ptr<Node> best = nullptr;
        uint32_t maxMinDist = 0;
        for (auto& c : candidates) {
            if (selectedIds.count(c->GetId()) > 0) {
                continue;
            }
            uint32_t minDist = std::numeric_limits<uint32_t>::max();
            for (auto& s : selected) {
                auto distMap = BfsAllDistances(s);
                auto it = distMap.find(c->GetId());
                if (it != distMap.end() && it->second < minDist) {
                    minDist = it->second;
                }
            }
            if (minDist != std::numeric_limits<uint32_t>::max() && minDist >= maxMinDist) {
                maxMinDist = minDist;
                best = c;
            }
        }
        if (!best) {
            break;
        }
        selected.push_back(best);
        selectedIds.insert(best->GetId());
    }

    return selected;
}

TopologyLayout BuildStarTopology(uint32_t domains) {
    TopologyLayout t;
    t.allNodes.Create(1 + domains);
    t.ingress = t.allNodes.Get(0);
    t.hopFromIngress.assign(domains, 1);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    for (uint32_t d = 0; d < domains; ++d) {
        Ptr<Node> domainNode = t.allNodes.Get(1 + d);
        p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
        p2p.Install(t.ingress, domainNode);
        t.domainNodes.push_back(domainNode);
    }
    return t;
}

TopologyLayout BuildRocketfuelTopology(uint32_t domains) {
    TopologyLayout t;
    AnnotatedTopologyReader reader("", 25);
    reader.SetFileName(g_topoFile);
    reader.Read();
    t.allNodes = reader.GetNodes();

    // Rocketfuel maps often have homogeneous per-link delay (e.g., 2ms on all links).
    // This creates quantized RTT plateaus in CDFs; add small per-link jitter by default.
    {
        uint32_t jitterUs = g_linkDelayJitterUs;
        if (jitterUs == 0 && g_rocketfuelAutoJitter) {
            jitterUs = 1500;
        }
        if (jitterUs > 0 || g_linkDelayMs > 0.0) {
            std::unordered_set<uintptr_t> seenChannels;
            std::mt19937 rng(g_seed + 2026u);
            std::uniform_real_distribution<double> jitterDist(
                -static_cast<double>(jitterUs), static_cast<double>(jitterUs));
            uint32_t touched = 0;
            double minDelayMs = std::numeric_limits<double>::infinity();
            double maxDelayMs = 0.0;

            for (uint32_t ni = 0; ni < t.allNodes.GetN(); ++ni) {
                Ptr<Node> node = t.allNodes.Get(ni);
                for (uint32_t di = 0; di < node->GetNDevices(); ++di) {
                    auto p2p = DynamicCast<PointToPointNetDevice>(node->GetDevice(di));
                    if (!p2p) {
                        continue;
                    }
                    auto ch = DynamicCast<PointToPointChannel>(p2p->GetChannel());
                    if (!ch) {
                        continue;
                    }
                    uintptr_t key = reinterpret_cast<uintptr_t>(PeekPointer(ch));
                    if (!seenChannels.insert(key).second) {
                        continue;
                    }

                    Time baseDelay = MilliSeconds(g_linkDelayMs > 0.0 ? g_linkDelayMs : 2.0);
                    if (g_linkDelayMs <= 0.0) {
                        TimeValue tv;
                        ch->GetAttribute("Delay", tv);
                        baseDelay = tv.Get();
                    }
                    double delayUs = std::max(100.0, baseDelay.ToDouble(Time::US) + jitterDist(rng));
                    Time newDelay = MicroSeconds(static_cast<int64_t>(std::llround(delayUs)));
                    ch->SetAttribute("Delay", TimeValue(newDelay));
                    touched++;

                    double delayMs = newDelay.ToDouble(Time::MS);
                    minDelayMs = std::min(minDelayMs, delayMs);
                    maxDelayMs = std::max(maxDelayMs, delayMs);
                }
            }
            if (touched > 0) {
                NS_LOG_UNCOND("Rocketfuel delay jitter applied: links=" << touched
                              << " jitterUs=" << jitterUs
                              << " delayRangeMs=[" << std::fixed << std::setprecision(3)
                              << minDelayMs << "," << maxDelayMs << "]");
            }
        }
    }

    if (t.allNodes.GetN() < domains + 1) {
        NS_LOG_WARN("Topology has too few nodes for requested domains. "
                    << "nodes=" << t.allNodes.GetN() << " domains=" << domains
                    << ". Falling back to star topology.");
        return BuildStarTopology(domains);
    }

    if (g_ingressNodeIndex >= t.allNodes.GetN()) {
        NS_LOG_WARN("ingressNodeIndex out of range, fallback to 0");
        g_ingressNodeIndex = 0;
    }
    t.ingress = t.allNodes.Get(g_ingressNodeIndex);
    t.domainNodes = SelectGatewayNodes(t.allNodes, t.ingress, domains);
    if (t.domainNodes.size() < domains) {
        NS_LOG_WARN("Gateway selection returned fewer nodes than requested, "
                    "falling back to star topology.");
        return BuildStarTopology(domains);
    }

    auto dist = BfsAllDistances(t.ingress);
    for (auto& dn : t.domainNodes) {
        auto it = dist.find(dn->GetId());
        t.hopFromIngress.push_back(it == dist.end() ? std::numeric_limits<uint32_t>::max() : it->second);
    }
    return t;
}

TopologyLayout BuildRedundantTopology(uint32_t domains) {
    TopologyLayout t;
    // nodes: ingress + coreA + coreB + domains
    t.allNodes.Create(3 + domains);
    t.ingress = t.allNodes.Get(0);
    Ptr<Node> coreA = t.allNodes.Get(1);
    Ptr<Node> coreB = t.allNodes.Get(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));

    // Redundant backbone + domain dual-homing
    p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
    p2p.Install(t.ingress, coreA);
    p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
    p2p.Install(t.ingress, coreB);
    p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
    p2p.Install(coreA, coreB);

    for (uint32_t d = 0; d < domains; ++d) {
        Ptr<Node> domainNode = t.allNodes.Get(3 + d);
        p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
        p2p.Install(coreA, domainNode);
        p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
        p2p.Install(coreB, domainNode);
        t.domainNodes.push_back(domainNode);
    }

    auto dist = BfsAllDistances(t.ingress);
    for (auto& dn : t.domainNodes) {
        auto it = dist.find(dn->GetId());
        t.hopFromIngress.push_back(it == dist.end() ? std::numeric_limits<uint32_t>::max() : it->second);
    }
    return t;
}

TopologyLayout BuildTopology(uint32_t domains) {
    if (g_topo == "rocketfuel") {
        return BuildRocketfuelTopology(domains);
    }
    if (g_topo == "redundant") {
        return BuildRedundantTopology(domains);
    }
    return BuildStarTopology(domains);
}

void AssignNodeNames(const TopologyLayout& topo) {
    if (!topo.ingress) {
        return;
    }
    Names::Clear();
    EnsureNodeName(topo.ingress, "ingress");
    if (g_topo == "redundant" && topo.allNodes.GetN() >= 3) {
        EnsureNodeName(topo.allNodes.Get(1), "coreA");
        EnsureNodeName(topo.allNodes.Get(2), "coreB");
    }
    for (uint32_t d = 0; d < topo.domainNodes.size(); ++d) {
        EnsureNodeName(topo.domainNodes[d], "domain" + std::to_string(d));
    }
    for (uint32_t i = 0; i < topo.allNodes.GetN(); ++i) {
        EnsureNodeName(topo.allNodes.Get(i), "node" + std::to_string(topo.allNodes.Get(i)->GetId()));
    }
}

std::vector<Ptr<Node>> BuildShortestPath(Ptr<Node> src,
                                         Ptr<Node> dst,
                                         const std::set<std::pair<uint32_t, uint32_t>>& blocked) {
    std::vector<Ptr<Node>> path;
    if (!src || !dst) {
        return path;
    }

    std::queue<Ptr<Node>> q;
    std::map<uint32_t, uint32_t> parent;
    std::set<uint32_t> visited;
    visited.insert(src->GetId());
    q.push(src);

    while (!q.empty()) {
        Ptr<Node> cur = q.front();
        q.pop();
        if (cur == dst) {
            break;
        }
        for (uint32_t i = 0; i < cur->GetNDevices(); ++i) {
            Ptr<NetDevice> dev = cur->GetDevice(i);
            Ptr<Channel> ch = dev->GetChannel();
            if (!ch) {
                continue;
            }
            for (uint32_t j = 0; j < ch->GetNDevices(); ++j) {
                Ptr<Node> nxt = ch->GetDevice(j)->GetNode();
                if (!nxt || nxt == cur) {
                    continue;
                }
                auto edge = NormalizeEdge(cur->GetId(), nxt->GetId());
                if (blocked.count(edge) > 0) {
                    continue;
                }
                if (visited.count(nxt->GetId()) > 0) {
                    continue;
                }
                visited.insert(nxt->GetId());
                parent[nxt->GetId()] = cur->GetId();
                q.push(nxt);
            }
        }
    }

    if (visited.count(dst->GetId()) == 0) {
        return {};
    }

    std::vector<uint32_t> rev;
    uint32_t cur = dst->GetId();
    rev.push_back(cur);
    while (cur != src->GetId()) {
        auto it = parent.find(cur);
        if (it == parent.end()) {
            return {};
        }
        cur = it->second;
        rev.push_back(cur);
    }
    std::reverse(rev.begin(), rev.end());
    for (uint32_t id : rev) {
        path.push_back(NodeList::GetNode(id));
    }
    return path;
}

bool AreNodesConnected(Ptr<Node> src,
                       Ptr<Node> dst,
                       const std::set<std::pair<uint32_t, uint32_t>>& blocked) {
    auto path = BuildShortestPath(src, dst, blocked);
    return !path.empty();
}

std::map<uint32_t, uint32_t> BuildTagDomainFrequency(size_t maxQueries) {
    std::map<uint32_t, uint32_t> domainFreq;
    if (g_queryToTagFile.empty() || g_tagIndexFile.empty()) {
        return domainFreq;
    }

    std::map<uint64_t, std::vector<uint32_t>> tagToDomains;
    {
        std::ifstream f(g_tagIndexFile);
        std::string line;
        if (f.is_open()) {
            std::getline(f, line); // header
            while (std::getline(f, line)) {
                auto fields = ParseCsvLine(line);
                if (fields.size() < 2) {
                    continue;
                }
                try {
                    uint64_t tid = std::stoull(TrimCopy(fields[0]));
                    uint32_t did = static_cast<uint32_t>(std::stoul(TrimCopy(fields[1])));
                    tagToDomains[tid].push_back(did);
                } catch (...) {
                }
            }
        }
    }
    if (tagToDomains.empty()) {
        return domainFreq;
    }

    auto ParseTagList = [](const std::string& raw) {
        std::vector<uint64_t> tags;
        std::stringstream ss(raw);
        std::string tok;
        while (std::getline(ss, tok, ';')) {
            tok = TrimCopy(tok);
            if (tok.empty()) {
                continue;
            }
            try {
                tags.push_back(std::stoull(tok));
            } catch (...) {
            }
        }
        return tags;
    };

    {
        std::ifstream f(g_queryToTagFile);
        std::string line;
        if (f.is_open()) {
            std::getline(f, line); // header
            size_t count = 0;
            while (std::getline(f, line) && count < maxQueries) {
                auto fields = ParseCsvLine(line);
                if (fields.size() < 2) {
                    continue;
                }
                std::string qidStr = TrimCopy(fields[0]);
                if (!qidStr.empty() && qidStr[0] == 'q') {
                    qidStr = qidStr.substr(1);
                }
                try {
                    (void)std::stoul(qidStr);
                    auto tags = ParseTagList(fields[1]);
                    if (tags.empty()) {
                        continue;
                    }
                    for (auto tid : tags) {
                        auto it = tagToDomains.find(tid);
                        if (it != tagToDomains.end()) {
                            for (auto did : it->second) {
                                domainFreq[did]++;
                            }
                        }
                    }
                    count++;
                } catch (...) {
                }
            }
        }
    }
    return domainFreq;
}

std::pair<std::string, std::string> SelectAutoCriticalLink(
    const TopologyLayout& topo,
    const std::vector<IRouteDiscoveryConsumer::QueryItem>& queryTrace,
    uint32_t hotDomainRank,
    const std::string& policy) {
    std::map<uint32_t, uint32_t> domainFreq;
    if (g_scheme == "tag" || g_scheme == "sanr-tag") {
        domainFreq = BuildTagDomainFrequency(queryTrace.size());
    }
    for (const auto& q : queryTrace) {
        if (!domainFreq.empty()) {
            break;
        }
        bool counted = false;
        for (const auto& td : q.targetDomains) {
            int did = ParseDomainId(td);
            if (did >= 0) {
                domainFreq[static_cast<uint32_t>(did)]++;
                counted = true;
            }
        }
        if (!counted) {
            int did = ParseDomainId(q.expectedDomain);
            if (did >= 0) {
                domainFreq[static_cast<uint32_t>(did)]++;
            }
        }
    }

    if (topo.domainNodes.empty()) {
        return {"", ""};
    }

    uint32_t domainId = 0;
    if (!domainFreq.empty()) {
        std::vector<std::pair<uint32_t, uint32_t>> ranked;
        for (const auto& kv : domainFreq) {
            ranked.push_back({kv.second, kv.first});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) {
                return a.first > b.first;
            }
            return a.second < b.second;
        });
        uint32_t idx = std::min<uint32_t>(hotDomainRank, static_cast<uint32_t>(ranked.size() - 1));
        domainId = ranked[idx].second;
    }

    domainId %= static_cast<uint32_t>(topo.domainNodes.size());
    Ptr<Node> domainNode = topo.domainNodes[domainId];
    auto path = BuildShortestPath(topo.ingress, domainNode, g_failedEdges);
    if (path.size() < 2) {
        return {"", ""};
    }

    bool preferNonCut = (policy == "auto-noncut");
    size_t fallbackEdgeIdx = path.size() - 2;
    int fallbackStretch = std::numeric_limits<int>::min();
    bool fallbackDisconnects = false;

    size_t nonCutEdgeIdx = path.size() - 2;
    int nonCutBestStretch = std::numeric_limits<int>::min();
    bool hasNonCutCandidate = false;

    size_t nonCutNonIngressIdx = path.size() - 2;
    int nonCutNonIngressBestStretch = std::numeric_limits<int>::min();
    bool hasNonCutNonIngress = false;

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        std::set<std::pair<uint32_t, uint32_t>> blocked = g_failedEdges;
        blocked.insert(NormalizeEdge(path[i]->GetId(), path[i + 1]->GetId()));
        auto alt = BuildShortestPath(topo.ingress, domainNode, blocked);
        bool disconnects = alt.empty();
        int stretch = disconnects
            ? std::numeric_limits<int>::max()
            : static_cast<int>(alt.size()) - static_cast<int>(path.size());

        if (stretch > fallbackStretch) {
            fallbackStretch = stretch;
            fallbackEdgeIdx = i;
            fallbackDisconnects = disconnects;
        }

        bool ingressAdjacent = (path[i] == topo.ingress || path[i + 1] == topo.ingress);
        if (!disconnects && stretch > nonCutBestStretch) {
            nonCutBestStretch = stretch;
            nonCutEdgeIdx = i;
            hasNonCutCandidate = true;
        }
        if (!disconnects && !ingressAdjacent && stretch > nonCutNonIngressBestStretch) {
            nonCutNonIngressBestStretch = stretch;
            nonCutNonIngressIdx = i;
            hasNonCutNonIngress = true;
        }
    }

    size_t edgeIdx = fallbackEdgeIdx;
    if (preferNonCut) {
        if (hasNonCutNonIngress) {
            edgeIdx = nonCutNonIngressIdx;
        }
        else if (hasNonCutCandidate) {
            edgeIdx = nonCutEdgeIdx;
        }
        else {
            g_partitionDetected = 1;
        }
    }
    else if (fallbackDisconnects) {
        edgeIdx = fallbackEdgeIdx;
    }

    Ptr<Node> u = path[edgeIdx];
    Ptr<Node> v = path[edgeIdx + 1];
    return {EnsureNodeName(u), EnsureNodeName(v)};
}

void ApplyLinkFailure(std::string uName, std::string vName) {
    Ptr<Node> u = Names::Find<Node>(uName);
    Ptr<Node> v = Names::Find<Node>(vName);
    if (!u || !v) {
        NS_LOG_WARN("Link failure: nodes " << uName << " or " << vName << " not found");
        g_failureSanity.notes = "node_not_found";
        return;
    }

    // Scan u's devices to find connection to v
    for (uint32_t i = 0; i < u->GetNDevices(); ++i) {
        Ptr<NetDevice> dev = u->GetDevice(i);
        Ptr<Channel> ch = dev->GetChannel();
        if (!ch) continue;
        
        // Check if v is on this channel
        for (uint32_t j = 0; j < ch->GetNDevices(); ++j) {
            if (ch->GetDevice(j)->GetNode() == v) {
                auto devA = DynamicCast<PointToPointNetDevice>(dev);
                auto devB = DynamicCast<PointToPointNetDevice>(ch->GetDevice(j));
                if (!devA || !devB) {
                    NS_LOG_WARN("Link failure: non-PointToPoint device on " << uName << " <-> " << vName);
                    g_failureSanity.notes = "unsupported_device_type";
                    return;
                }

                auto emA = CreateObject<RateErrorModel>();
                emA->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
                emA->SetRate(1.0);
                devA->SetReceiveErrorModel(emA);

                auto emB = CreateObject<RateErrorModel>();
                emB->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
                emB->SetRate(1.0);
                devB->SetReceiveErrorModel(emB);

                g_failedEdges.insert(NormalizeEdge(u->GetId(), v->GetId()));
                g_failureSanity.applied = 1;
                g_failureSanity.afterConnected = AreNodesConnected(u, v, g_failedEdges) ? 1 : 0;
                if (g_failureSanity.afterConnected == 0) {
                    g_partitionDetected = 1;
                }
                NS_LOG_UNCOND("Link Failed: " << uName << " <-> " << vName << " at " << Simulator::Now().GetSeconds() << "s");

                if (g_failRecoverySec > 0.0) {
                    auto recover = [devA, devB, uName, vName]() {
                        auto clearA = CreateObject<RateErrorModel>();
                        clearA->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
                        clearA->SetRate(0.0);
                        devA->SetReceiveErrorModel(clearA);

                        auto clearB = CreateObject<RateErrorModel>();
                        clearB->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
                        clearB->SetRate(0.0);
                        devB->SetReceiveErrorModel(clearB);

                        g_failedEdges.erase(NormalizeEdge(devA->GetNode()->GetId(), devB->GetNode()->GetId()));
                        if (g_failureSanity.notes.empty()) {
                            g_failureSanity.notes = "link_recovered";
                        } else {
                            g_failureSanity.notes += ";link_recovered";
                        }
                        NS_LOG_UNCOND("Link Recovered: " << uName << " <-> " << vName
                                      << " at " << Simulator::Now().GetSeconds() << "s");
                    };
                    Simulator::Schedule(Seconds(g_failRecoverySec), recover);
                    NS_LOG_UNCOND("Scheduled Link Recovery: " << uName << "-" << vName
                                  << " at +" << g_failRecoverySec << "s");
                }
                return;
            }
        }
    }
    NS_LOG_WARN("Link failure: no direct link found between " << uName << " and " << vName);
    g_failureSanity.notes = "no_direct_link";
}

void ApplyDomainFailure(std::string domainName) {
    Ptr<Node> node = Names::Find<Node>(domainName);
    if (!node) {
        NS_LOG_WARN("Domain failure: node " << domainName << " not found");
        g_failureSanity.notes = "domain_not_found";
        return;
    }

    // Stop all applications on the node
    int stopped = 0;
    for (uint32_t i = 0; i < node->GetNApplications(); ++i) {
        node->GetApplication(i)->SetStopTime(Simulator::Now());
        stopped++;
    }

    // Isolate the failed domain node from data plane to ensure failure is effective.
    uint32_t isolatedLinks = 0;
    for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
        auto localDev = DynamicCast<PointToPointNetDevice>(node->GetDevice(i));
        if (!localDev) {
            continue;
        }
        auto localErr = CreateObject<RateErrorModel>();
        localErr->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
        localErr->SetRate(1.0);
        localDev->SetReceiveErrorModel(localErr);

        auto ch = localDev->GetChannel();
        if (!ch) {
            continue;
        }
        for (uint32_t j = 0; j < ch->GetNDevices(); ++j) {
            auto peerNode = ch->GetDevice(j)->GetNode();
            if (!peerNode || peerNode == node) {
                continue;
            }
            auto peerDev = DynamicCast<PointToPointNetDevice>(ch->GetDevice(j));
            if (!peerDev) {
                continue;
            }
            auto peerErr = CreateObject<RateErrorModel>();
            peerErr->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
            peerErr->SetRate(1.0);
            peerDev->SetReceiveErrorModel(peerErr);
            isolatedLinks++;
        }
    }
    g_failureSanity.applied = 1;
    g_failureSanity.affectedApps = stopped + static_cast<int>(isolatedLinks);
    g_failureSanity.notes = "apps_stopped=" + std::to_string(stopped)
        + ";isolated_links=" + std::to_string(isolatedLinks);
    NS_LOG_UNCOND("Domain Failed: " << domainName
                  << " (apps stopped=" << stopped
                  << ", links isolated=" << isolatedLinks << ") at "
                  << Simulator::Now().GetSeconds() << "s");
}

void ApplyTagChurn(double ratio) {
    NS_LOG_UNCOND("Applying Tag Churn (ratio=" << ratio << ") at " << Simulator::Now().GetSeconds() << "s");
    std::vector<Ptr<TagRouterApp>> routers;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        Ptr<Node> node = NodeList::GetNode(i);
        for (uint32_t j = 0; j < node->GetNApplications(); ++j) {
            auto app = DynamicCast<TagRouterApp>(node->GetApplication(j));
            if (app) {
                routers.push_back(app);
            }
        }
    }

    if (routers.empty()) {
        g_failureSanity.applied = 0;
        return;
    }

    double bounded = std::max(0.0, std::min(1.0, ratio));
    uint32_t toChurn = static_cast<uint32_t>(std::round(bounded * routers.size()));
    if (bounded > 0.0 && toChurn == 0) {
        toChurn = 1;
    }
    toChurn = std::min<uint32_t>(toChurn, static_cast<uint32_t>(routers.size()));
    uint32_t eventTick = static_cast<uint32_t>(std::llround(Simulator::Now().GetMilliSeconds()));
    std::mt19937 rng(g_seed + 4001u + eventTick);
    std::shuffle(routers.begin(), routers.end(), rng);
    for (uint32_t i = 0; i < toChurn; ++i) {
        auto originalTags = routers[i]->GetLocalTags();
        std::vector<uint64_t> newTags; // empty list -> simulates local tag loss
        routers[i]->ScheduleChurn(Seconds(0), newTags);
        if (g_churnRecoverySec > 0.0) {
            routers[i]->ScheduleChurn(Seconds(g_churnRecoverySec), originalTags);
        }
        g_failureSanity.affectedApps++;
    }
    g_failureSanity.applied = g_failureSanity.affectedApps > 0 ? 1 : 0;
}

void ApplyIRouteChurn(double ratio) {
    NS_LOG_UNCOND("Applying iRoute Churn (ratio=" << ratio << ") at " << Simulator::Now().GetSeconds() << "s");
    std::vector<std::pair<Ptr<IRouteApp>, std::string>> routers;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        Ptr<Node> node = NodeList::GetNode(i);
        std::string nodeName = EnsureNodeName(node);
        if (ParseDomainId(nodeName) < 0) {
            continue; // churn only real domain nodes, not ingress/core
        }
        for (uint32_t j = 0; j < node->GetNApplications(); ++j) {
            auto app = DynamicCast<IRouteApp>(node->GetApplication(j));
            if (app) {
                std::string domainPrefix = nodeName;
                if (!domainPrefix.empty() && domainPrefix.front() != '/') {
                    domainPrefix = "/" + domainPrefix;
                }
                routers.push_back({app, domainPrefix});
            }
        }
    }

    if (routers.empty()) {
        g_failureSanity.applied = 0;
        return;
    }

    double bounded = std::max(0.0, std::min(1.0, ratio));
    uint32_t toChurn = static_cast<uint32_t>(std::round(bounded * routers.size()));
    if (bounded > 0.0 && toChurn == 0) {
        toChurn = 1;
    }
    toChurn = std::min<uint32_t>(toChurn, static_cast<uint32_t>(routers.size()));
    uint32_t eventTick = static_cast<uint32_t>(std::llround(Simulator::Now().GetMilliSeconds()));
    std::mt19937 rng(g_seed + 5001u + eventTick);
    std::shuffle(routers.begin(), routers.end(), rng);
    Ptr<Node> ingressNode = Names::Find<Node>("ingress");
    auto ingressRm = ingressNode ? iroute::RouteManagerRegistry::getOrCreate(ingressNode->GetId(), g_vectorDim) : nullptr;
    std::vector<std::string> affectedDomains;
    for (uint32_t i = 0; i < toChurn; ++i) {
        auto app = routers[i].first;
        const std::string& domainPrefix = routers[i].second;
        auto originalCentroids = app->GetLocalCentroids();
        std::vector<iroute::CentroidEntry> newCentroids; // empty -> local centroid loss
        app->ScheduleChurn(Seconds(0), newCentroids);
        if (ingressRm && !domainPrefix.empty()) {
            auto existing = ingressRm->getDomain(Name(domainPrefix));
            if (existing) {
                if (g_irouteChurnBackup.find(domainPrefix) == g_irouteChurnBackup.end()) {
                    g_irouteChurnBackup[domainPrefix] = *existing;
                }
                iroute::DomainEntry dropped = *existing;
                dropped.seqNo += 1;
                dropped.centroids.clear();
                ingressRm->updateDomain(dropped);
            }
        }
        if (g_churnRecoverySec > 0.0) {
            app->ScheduleChurn(Seconds(g_churnRecoverySec), originalCentroids);
            if (ingressRm && !domainPrefix.empty()) {
                Simulator::Schedule(Seconds(g_churnRecoverySec), [domainPrefix, ingressRm]() {
                    auto it = g_irouteChurnBackup.find(domainPrefix);
                    if (it == g_irouteChurnBackup.end()) {
                        return;
                    }
                    iroute::DomainEntry restored = it->second;
                    restored.seqNo += 2;
                    ingressRm->updateDomain(restored);
                });
            }
        }
        g_failureSanity.affectedApps++;
        if (!domainPrefix.empty()) {
            affectedDomains.push_back(domainPrefix);
        }
    }
    g_failureSanity.applied = g_failureSanity.affectedApps > 0 ? 1 : 0;
    if (!affectedDomains.empty()) {
        std::sort(affectedDomains.begin(), affectedDomains.end());
        affectedDomains.erase(std::unique(affectedDomains.begin(), affectedDomains.end()), affectedDomains.end());
        if (!g_failureSanity.notes.empty()) {
            g_failureSanity.notes += ";";
        }
        g_failureSanity.notes += "affected_domains=" + irouteexp::JoinStr(affectedDomains, '|');
    }
}

void ApplyFloodChurn(double ratio) {
    NS_LOG_UNCOND("Applying Flood Churn (ratio=" << ratio << ") at " << Simulator::Now().GetSeconds() << "s");
    std::vector<Ptr<Application>> responders;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        Ptr<Node> node = NodeList::GetNode(i);
        for (uint32_t j = 0; j < node->GetNApplications(); ++j) {
            auto app = DynamicCast<FloodResponderApp>(node->GetApplication(j));
            if (app) {
                responders.push_back(node->GetApplication(j));
            }
        }
    }
    if (responders.empty()) {
        g_failureSanity.applied = 0;
        return;
    }

    uint32_t toStop = static_cast<uint32_t>(std::round(std::max(0.0, std::min(1.0, ratio)) * responders.size()));
    if (ratio > 0.0 && toStop == 0) {
        toStop = 1;
    }
    toStop = std::min<uint32_t>(toStop, static_cast<uint32_t>(responders.size()));
    uint32_t eventTick = static_cast<uint32_t>(std::llround(Simulator::Now().GetMilliSeconds()));
    std::mt19937 rng(g_seed + 6001u + eventTick);
    std::shuffle(responders.begin(), responders.end(), rng);

    std::vector<Ptr<PointToPointNetDevice>> impairedDevices;
    std::set<uint32_t> impairedNodeIds;
    std::vector<std::string> affectedDomains;
    for (uint32_t i = 0; i < toStop; ++i) {
        Ptr<Node> node = responders[i]->GetNode();
        if (!node || impairedNodeIds.count(node->GetId()) > 0) {
            continue;
        }
        impairedNodeIds.insert(node->GetId());
        for (uint32_t d = 0; d < node->GetNDevices(); ++d) {
            auto p2p = DynamicCast<PointToPointNetDevice>(node->GetDevice(d));
            if (!p2p) {
                continue;
            }
            auto em = CreateObject<RateErrorModel>();
            em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
            em->SetRate(1.0);
            p2p->SetReceiveErrorModel(em);
            impairedDevices.push_back(p2p);
        }
        g_failureSanity.affectedApps++;
        std::string nodeName = EnsureNodeName(node);
        int domainId = ParseDomainId(nodeName);
        if (domainId >= 0) {
            affectedDomains.push_back("/domain" + std::to_string(domainId));
        }
    }
    if (g_churnRecoverySec > 0.0 && !impairedDevices.empty()) {
        Simulator::Schedule(Seconds(g_churnRecoverySec), [impairedDevices]() {
            for (const auto& dev : impairedDevices) {
                if (!dev) {
                    continue;
                }
                auto clear = CreateObject<RateErrorModel>();
                clear->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
                clear->SetRate(0.0);
                dev->SetReceiveErrorModel(clear);
            }
        });
    }
    g_failureSanity.applied = g_failureSanity.affectedApps > 0 ? 1 : 0;
    if (!affectedDomains.empty()) {
        std::sort(affectedDomains.begin(), affectedDomains.end());
        affectedDomains.erase(std::unique(affectedDomains.begin(), affectedDomains.end()), affectedDomains.end());
        if (!g_failureSanity.notes.empty()) {
            g_failureSanity.notes += ";";
        }
        g_failureSanity.notes += "affected_domains=" + irouteexp::JoinStr(affectedDomains, '|');
    }
}

void ApplyCentralChurn(double ratio) {
    NS_LOG_UNCOND("Applying Central Churn (ratio=" << ratio << ") at " << Simulator::Now().GetSeconds() << "s");
    std::vector<Ptr<SemanticProducer>> producers;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        Ptr<Node> node = NodeList::GetNode(i);
        for (uint32_t j = 0; j < node->GetNApplications(); ++j) {
            auto app = DynamicCast<SemanticProducer>(node->GetApplication(j));
            if (app) {
                producers.push_back(app);
            }
        }
    }
    if (producers.empty()) {
        g_failureSanity.applied = 0;
        return;
    }

    uint32_t toStop = static_cast<uint32_t>(std::round(std::max(0.0, std::min(1.0, ratio)) * producers.size()));
    if (ratio > 0.0 && toStop == 0) {
        toStop = 1;
    }
    toStop = std::min<uint32_t>(toStop, static_cast<uint32_t>(producers.size()));
    uint32_t eventTick = static_cast<uint32_t>(std::llround(Simulator::Now().GetMilliSeconds()));
    std::mt19937 rng(g_seed + 7001u + eventTick);
    std::shuffle(producers.begin(), producers.end(), rng);
    std::vector<std::string> affectedDomains;

    for (uint32_t i = 0; i < toStop; ++i) {
        auto app = producers[i];
        if (!app) {
            continue;
        }
        Ptr<Node> node = app->GetNode();
        std::string nodeName = node ? EnsureNodeName(node) : "";
        int domainId = ParseDomainId(nodeName);
        if (domainId >= 0) {
            affectedDomains.push_back("/domain" + std::to_string(domainId));
        }
        app->SetActive(false);
        g_failureSanity.affectedApps++;
        if (g_churnRecoverySec > 0.0) {
            Simulator::Schedule(Seconds(g_churnRecoverySec), [app]() {
                if (app) {
                    app->SetActive(true);
                }
            });
        }
    }
    g_failureSanity.applied = g_failureSanity.affectedApps > 0 ? 1 : 0;
    if (!affectedDomains.empty()) {
        std::sort(affectedDomains.begin(), affectedDomains.end());
        affectedDomains.erase(std::unique(affectedDomains.begin(), affectedDomains.end()), affectedDomains.end());
        if (!g_failureSanity.notes.empty()) {
            g_failureSanity.notes += ";";
        }
        g_failureSanity.notes += "affected_domains=" + irouteexp::JoinStr(affectedDomains, '|');
    }
}

void ProcessFailureArgs(const TopologyLayout* topo = nullptr,
                        const std::vector<IRouteDiscoveryConsumer::QueryItem>* queryTrace = nullptr) {
    g_failureSanity = FailureSanityRecord();
    g_runFailureScenario = "none";
    g_runFailureTarget.clear();
    g_failedEdges.clear();
    g_partitionDetected = 0;
    g_failureEffective = 0;
    g_irouteChurnBackup.clear();

    // 1) Link Failure: u-v@time OR auto@time (with --failureTargetPolicy=auto-critical)
    if (!g_failLink.empty()) {
        std::string uName;
        std::string vName;
        double timeVal = 0.0;

        size_t atPos = g_failLink.find('@');
        if (atPos == std::string::npos) {
            atPos = g_failLink.size();
        }
        std::string targetPart = g_failLink.substr(0, atPos);
        if (atPos < g_failLink.size()) {
            try {
                timeVal = std::stod(g_failLink.substr(atPos + 1));
            } catch (...) {
            }
        }

        bool useAuto = (g_failureTargetPolicy == "auto-critical" || g_failureTargetPolicy == "auto-noncut");
        if (useAuto && topo && queryTrace) {
            auto autoPair = SelectAutoCriticalLink(*topo, *queryTrace, g_failHotDomainRank, g_failureTargetPolicy);
            uName = autoPair.first;
            vName = autoPair.second;
        }
        if (uName.empty() || vName.empty()) {
            size_t dashPos = targetPart.find('-');
            if (dashPos != std::string::npos) {
                uName = TrimCopy(targetPart.substr(0, dashPos));
                vName = TrimCopy(targetPart.substr(dashPos + 1));
            }
        }

        if (!uName.empty() && !vName.empty()) {
            Ptr<Node> u = Names::Find<Node>(uName);
            Ptr<Node> v = Names::Find<Node>(vName);
            g_failureSanity.beforeConnected = (u && v && AreNodesConnected(u, v, g_failedEdges)) ? 1 : 0;
            g_failureSanity.scenario = "link-fail";
            g_failureSanity.target = uName + "-" + vName;
            g_failureSanity.eventTime = timeVal;
            g_failureSanity.scheduled = 1;
            g_runFailureScenario = "link-fail";
            g_runFailureTarget = g_failureSanity.target;
            Simulator::Schedule(Seconds(timeVal), &ApplyLinkFailure, uName, vName);
            NS_LOG_UNCOND("Scheduled Link Failure: " << uName << "-" << vName << " at " << timeVal << "s");
        }
    }

    // 2) Domain Failure: d@time
    if (!g_failDomain.empty()) {
        std::string domainName;
        double timeVal = 0.0;
        size_t atPos = g_failDomain.find('@');
        if (atPos == std::string::npos) {
            atPos = g_failDomain.size();
        }
        domainName = TrimCopy(g_failDomain.substr(0, atPos));
        if (atPos < g_failDomain.size()) {
            try {
                timeVal = std::stod(g_failDomain.substr(atPos + 1));
            } catch (...) {
            }
        }

        if ((domainName.empty() || domainName == "auto") &&
            (g_failureTargetPolicy == "auto-critical" || g_failureTargetPolicy == "auto-noncut") &&
            topo && queryTrace && !topo->domainNodes.empty()) {
            std::map<uint32_t, double> freq;
            if (g_scheme == "tag" || g_scheme == "sanr-tag") {
                auto tagFreq = BuildTagDomainFrequency(queryTrace->size());
                for (const auto& kv : tagFreq) {
                    freq[kv.first] += static_cast<double>(kv.second);
                }
            }
            if (freq.empty()) {
                for (const auto& q : *queryTrace) {
                    std::set<uint32_t> dids;
                    for (const auto& td : q.targetDomains) {
                        int did = ParseDomainId(td);
                        if (did >= 0) {
                            dids.insert(static_cast<uint32_t>(did));
                        }
                    }
                    if (dids.empty()) {
                        int did = ParseDomainId(q.expectedDomain);
                        if (did >= 0) {
                            dids.insert(static_cast<uint32_t>(did));
                        }
                    }
                    if (dids.empty()) {
                        continue;
                    }
                    double w = 1.0 / static_cast<double>(dids.size());
                    for (uint32_t did : dids) {
                        freq[did] += w;
                    }
                }
            }
            if (!freq.empty()) {
                std::vector<std::pair<double, uint32_t>> ranked;
                for (const auto& kv : freq) {
                    ranked.push_back({kv.second, static_cast<uint32_t>(kv.first)});
                }
                std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                    if (a.first != b.first) return a.first > b.first;
                    return a.second < b.second;
                });
                uint32_t idx = std::min<uint32_t>(g_failHotDomainRank, static_cast<uint32_t>(ranked.size() - 1));
                uint32_t did = ranked[idx].second % topo->domainNodes.size();
                domainName = "domain" + std::to_string(did);
            } else {
                domainName = "domain0";
            }
        }

        if (!domainName.empty()) {
            g_failureSanity.scenario = "domain-fail";
            g_failureSanity.target = domainName;
            g_failureSanity.eventTime = timeVal;
            g_failureSanity.scheduled = 1;
            g_runFailureScenario = "domain-fail";
            g_runFailureTarget = domainName;
            Simulator::Schedule(Seconds(timeVal), &ApplyDomainFailure, domainName);
            NS_LOG_UNCOND("Scheduled Domain Failure: " << domainName << " at " << timeVal << "s");
        }
    }

    // 3) Churn: type@time@ratio
    if (!g_churn.empty()) {
        std::string type;
        double timeVal = 0.0;
        double ratio = 0.0;
        size_t firstAt = g_churn.find('@');
        size_t secondAt = g_churn.find('@', firstAt + 1);
        if (firstAt != std::string::npos && secondAt != std::string::npos) {
            type = g_churn.substr(0, firstAt);
            try {
                timeVal = std::stod(g_churn.substr(firstAt + 1, secondAt - firstAt - 1));
                ratio = std::stod(g_churn.substr(secondAt + 1));
            } catch (...) {
            }
            uint32_t rounds = std::max<uint32_t>(1, g_churnRounds);
            double step = std::max(0.0, g_churnIntervalSec);
            for (uint32_t r = 0; r < rounds; ++r) {
                double t = timeVal + static_cast<double>(r) * step;
                if (type == "tag") {
                    Simulator::Schedule(Seconds(t), &ApplyTagChurn, ratio);
                }
                if (type == "iroute") {
                    Simulator::Schedule(Seconds(t), &ApplyIRouteChurn, ratio);
                }
                if (type == "flood") {
                    Simulator::Schedule(Seconds(t), &ApplyFloodChurn, ratio);
                }
                if (type == "central") {
                    Simulator::Schedule(Seconds(t), &ApplyCentralChurn, ratio);
                }
            }
            g_failureSanity.scenario = "churn";
            g_failureSanity.target = type;
            g_failureSanity.eventTime = timeVal;
            g_failureSanity.scheduled = 1;
            g_runFailureScenario = "churn";
            g_runFailureTarget = type;
            g_failureSanity.notes = "churn_rounds=" + std::to_string(rounds)
                + ";churn_interval_sec=" + std::to_string(step);
            NS_LOG_UNCOND("Scheduled Churn (" << type << ", " << ratio
                          << ", rounds=" << rounds
                          << ", interval=" << step << "s) at " << timeVal << "s");
        }
    }
}

class BaselineSimulation {
public:
    BaselineSimulation(uint32_t seed, uint32_t domains)
        : m_seed(seed), m_domains(domains), m_rng(seed) {}

    void LoadData(const std::string& centroidsFile,
                  const std::string& contentFile,
                  const std::string& traceFile) {
        m_centroids = LoadCentroidsFromCsv(centroidsFile);
        m_content = LoadContentFromCsv(contentFile);
        m_queryTrace = LoadTraceFromCsv(traceFile);
        if (g_shuffleTrace && !m_queryTrace.empty()) {
            std::shuffle(m_queryTrace.begin(), m_queryTrace.end(), m_rng);
        }
        m_queryById.clear();
        for (size_t i = 0; i < m_queryTrace.size(); ++i) {
            m_queryById[m_queryTrace[i].id] = m_queryTrace[i];
        }

        NS_LOG_UNCOND("Loaded " << m_centroids.size() << " domains centroids, "
                      << m_content.size() << " domains content, "
                      << m_queryTrace.size() << " queries");
    }

    // =========================================================================
    // Scheme: iRoute
    // =========================================================================
    std::vector<QueryLog> RunIRoute() {
        NS_LOG_UNCOND("\n=== Running scheme: iRoute ===");
        std::vector<QueryLog> logs;

        TopologyLayout topo = BuildTopology(m_domains);
        AssignNodeNames(topo);

        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(g_csSize);
        ndnHelper.Install(topo.allNodes);

        Ptr<Node> ingressNode = topo.ingress;

        // Install IRouteApp on ingress
        AppHelper irouteHelper("ns3::ndn::IRouteApp");
        irouteHelper.SetAttribute("RouterId", StringValue("ingress"));
        irouteHelper.SetAttribute("IsIngress", BooleanValue(true));
        irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
        irouteHelper.SetAttribute("DiscReplyJitterUs", UintegerValue(g_serviceJitterUs));
        irouteHelper.Install(ingressNode);

        // Install IRouteApp + SemanticProducer on domains
        InstallDomainApps(topo.domainNodes, irouteHelper);

        // Global routing
        SetupGlobalRouting(topo.allNodes, topo.domainNodes);

        // Populate domain index on ingress
        auto ingressRM = iroute::RouteManagerRegistry::getOrCreate(ingressNode->GetId(), g_vectorDim);
        ingressRM->setActiveSemVerId(1);
        uint32_t lsdbEntries = 0;
        for (uint32_t d = 0; d < m_domains; ++d) {
            iroute::DomainEntry entry;
            entry.domainId = Name("/domain" + std::to_string(d));
            entry.semVerId = 1;
            entry.seqNo = 1;
            entry.cost = 1.0;
            if (d < topo.hopFromIngress.size() &&
                topo.hopFromIngress[d] != std::numeric_limits<uint32_t>::max()) {
                entry.cost = static_cast<double>(topo.hopFromIngress[d]);
            }
            entry.centroids = GetDomainCentroids(d);
            lsdbEntries += entry.centroids.size();
            ingressRM->updateDomain(entry);
        }

        // Consumer
        AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
        consumerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
        consumerHelper.SetAttribute("KMax", UintegerValue(g_K));
        consumerHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(ingressNode);
        consumerApps.Start(Seconds(g_warmupSec));

        Ptr<IRouteDiscoveryConsumer> consumer =
            DynamicCast<IRouteDiscoveryConsumer>(consumerApps.Get(0));
        if (consumer) {
            double setTraceAt = std::max(0.0, g_warmupSec - 0.1);
            Simulator::Schedule(Seconds(setTraceAt), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }

        Simulator::Stop(Seconds(g_simTime));
        ProcessFailureArgs(&topo, &m_queryTrace);
        Simulator::Run();

        // Collect logs
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            for (size_t i = 0; i < txs.size(); ++i) {
                const auto& qi = ResolveQueryItem(txs[i].queryId, i);
                logs.push_back(TxRecordToQueryLog(static_cast<uint32_t>(txs[i].queryId), "iroute", txs[i], qi));
            }
        }

        uint32_t fibSize = GetFibSize(ingressNode);
        Simulator::Destroy();

        m_lastFibSize = fibSize;
        m_lastLsdbSize = lsdbEntries;
        return logs;
    }

    // =========================================================================
    // Scheme: exact (Exact-NDN syntax baseline: tokenized query Interest name)
    // =========================================================================
    std::vector<QueryLog> RunExactMatch(const std::string& indexFile) {
        NS_LOG_UNCOND("\n=== Running scheme: exact (NDN syntax) ===");
        std::vector<QueryLog> logs;

        // Simple 2-node topology: consumer <-> producer
        NodeContainer nodes;
        nodes.Create(2);
        Names::Add("ingress", nodes.Get(0));
        Names::Add("domain0", nodes.Get(1)); // Producer as domain0

        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        p2p.Install(nodes.Get(0), nodes.Get(1));

        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(g_csSize);
        ndnHelper.InstallAll();

        Ptr<Node> consumerNode = nodes.Get(0);
        Ptr<Node> producerNode = nodes.Get(1);

        // ExactNdnProducer on producer node
        AppHelper producerHelper("ns3::ndn::ExactNdnProducer");
        auto producerApps = producerHelper.Install(producerNode);

        Ptr<ExactNdnProducer> producer =
            DynamicCast<ExactNdnProducer>(producerApps.Get(0));
        if (producer) {
            if (g_exactUseIndex && !indexFile.empty()) {
                producer->LoadDictionary(indexFile);
            }
        }

        // Global routing
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        grHelper.AddOrigins("/exact", producerNode);
        GlobalRoutingHelper::CalculateRoutes();

        // ExactNdnConsumer on consumer node
        AppHelper consumerHelper("ns3::ndn::ExactNdnConsumer");
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(consumerNode);
        consumerApps.Start(Seconds(g_warmupSec));

        Ptr<ExactNdnConsumer> consumer =
            DynamicCast<ExactNdnConsumer>(consumerApps.Get(0));
        if (consumer) {
            double setTraceAt = std::max(0.0, g_warmupSec - 0.1);
            Simulator::Schedule(Seconds(setTraceAt), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }

        Simulator::Stop(Seconds(g_simTime));
        ProcessFailureArgs();
        Simulator::Run();

        // Collect logs
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            for (size_t i = 0; i < txs.size(); ++i) {
                const auto& qi = ResolveQueryItem(txs[i].queryId, i);
                logs.push_back(ExactTxToQueryLog(static_cast<uint32_t>(txs[i].queryId), txs[i], qi));
            }
        }

        uint32_t fibSize = GetFibSize(consumerNode);
        NS_LOG_UNCOND("  ExactProducer hits=" << (producer ? producer->GetHits() : 0)
                      << " misses=" << (producer ? producer->GetMisses() : 0));

        Simulator::Destroy();
        m_lastFibSize = fibSize;
        m_lastLsdbSize = 0;  // no LSDB in exact scheme
        return logs;
    }

    // =========================================================================
    // Scheme: central (Central Directory Service)
    // =========================================================================
    std::vector<QueryLog> RunCentralized(const std::string& dirMode,
                                         uint32_t dirProcMs,
                                         uint32_t dirProcJitterUs,
                                         uint32_t dirTopK,
                                         double dirFailTime,
                                         const std::string& qrelsFile,
                                         const std::string& contentFile,
                                         const std::string& traceFile) {
        NS_LOG_UNCOND("\n=== Running scheme: central (Directory Service) ===");
        NS_LOG_UNCOND("  dirMode=" << dirMode << " procDelay=" << dirProcMs
                      << "ms procJitterUs=" << dirProcJitterUs
                      << " topK=" << dirTopK
                      << " failTime=" << (dirFailTime > 0 ? std::to_string(dirFailTime) : "none"));
        std::vector<QueryLog> logs;

        // Topology: 0=consumer, 1=directory, 2..N=domain producers
        NodeContainer nodes;
        nodes.Create(2 + m_domains);
        Names::Add("ingress", nodes.Get(0));
        Names::Add("directory", nodes.Get(1));
        for (uint32_t d = 0; d < m_domains; ++d) {
             Names::Add("domain" + std::to_string(d), nodes.Get(2 + d));
        }

        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
        p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
        p2p.Install(nodes.Get(0), nodes.Get(1));  // consumer <-> directory
        for (uint32_t d = 0; d < m_domains; ++d) {
            p2p.SetChannelAttribute("Delay", TimeValue(SampleSimpleLinkDelay()));
            p2p.Install(nodes.Get(0), nodes.Get(2 + d));  // consumer <-> domain
        }

        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(g_csSize);
        ndnHelper.InstallAll();

        Ptr<Node> consumerNode = nodes.Get(0);
        Ptr<Node> dirNode = nodes.Get(1);

        // ---- DirectoryServerApp on directory node ----
        AppHelper dirHelper("ns3::ndn::DirectoryServerApp");
        dirHelper.SetAttribute("Mode", StringValue(dirMode));
        dirHelper.SetAttribute("TopK", UintegerValue(dirTopK));
        dirHelper.SetAttribute("ProcDelayMs", UintegerValue(dirProcMs));
        uint32_t dirJitter = dirProcJitterUs > 0 ? dirProcJitterUs : g_serviceJitterUs;
        dirHelper.SetAttribute("ProcJitterUs", UintegerValue(dirJitter));
        auto dirApps = dirHelper.Install(dirNode);

        Ptr<DirectoryServerApp> dirServer =
            DynamicCast<DirectoryServerApp>(dirApps.Get(0));
        if (dirServer) {
            dirServer->LoadOracleIndex(qrelsFile, contentFile);
            dirServer->LoadQueryTextMap(traceFile);
            if (dirFailTime > 0) {
                dirServer->SetFailureTime(dirFailTime);
            }
        }

        // ---- SemanticProducer on each domain node ----
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.SetAttribute("Freshness", TimeValue(MilliSeconds(g_dataFreshnessMs)));
            producerHelper.SetAttribute("ReplyJitterUs", UintegerValue(g_serviceJitterUs));
            producerHelper.Install(nodes.Get(2 + d));
        }

        // ---- Global routing ----
        GlobalRoutingHelper grHelper;
        grHelper.InstallAll();
        grHelper.AddOrigins("/dir", dirNode);
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, nodes.Get(2 + d));
            grHelper.AddOrigins(domainName + "/data", nodes.Get(2 + d));
        }
        GlobalRoutingHelper::CalculateRoutes();

        // ---- CentralDirConsumer on consumer node ----
        AppHelper consumerHelper("ns3::ndn::CentralDirConsumer");
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("DiscTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        auto consumerApps = consumerHelper.Install(consumerNode);
        consumerApps.Start(Seconds(g_warmupSec));

        Ptr<CentralDirConsumer> consumer =
            DynamicCast<CentralDirConsumer>(consumerApps.Get(0));
        if (consumer) {
            double setTraceAt = std::max(0.0, g_warmupSec - 0.1);
            Simulator::Schedule(Seconds(setTraceAt), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }

        Simulator::Stop(Seconds(g_simTime));
        ProcessFailureArgs();
        Simulator::Run();

        // Collect logs
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            for (size_t i = 0; i < txs.size(); ++i) {
                const auto& qi = ResolveQueryItem(txs[i].queryId, i);
                logs.push_back(CentralDirTxToQueryLog(static_cast<uint32_t>(txs[i].queryId), txs[i], qi));
            }
        }

        uint32_t fibSize = GetFibSize(consumerNode);
        NS_LOG_UNCOND("  DirectoryServer processed="
                      << (dirServer ? dirServer->GetQueriesProcessed() : 0)
                      << " dropped=" << (dirServer ? dirServer->GetQueriesDropped() : 0)
                      << " peakQueue=" << (dirServer ? dirServer->GetPeakQueueLength() : 0)
                      << " bytesServed=" << (dirServer ? dirServer->GetTotalBytesServed() : 0));

        Simulator::Destroy();
        m_lastFibSize = fibSize;
        m_lastLsdbSize = 1; // single directory
        return logs;
    }

    // =========================================================================
    // Scheme: flood (Broadcast Discovery)
    // =========================================================================
    std::vector<QueryLog> RunFlood(const std::string& responderMode,
                                   double floodThreshold) {
        NS_LOG_UNCOND("\n=== Running scheme: flood ===");
        NS_LOG_UNCOND("  responderMode=" << responderMode
                      << " threshold=" << floodThreshold);
        std::vector<QueryLog> logs;

        TopologyLayout topo = BuildTopology(m_domains);
        AssignNodeNames(topo);

        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(g_csSize);
        ndnHelper.Install(topo.allNodes);

        Ptr<Node> consumerNode = topo.ingress;

        // ---- FloodResponderApp on each domain node ----
        AppHelper responderHelper("ns3::ndn::FloodResponderApp");
        responderHelper.SetAttribute("ResponderMode", StringValue(responderMode));
        responderHelper.SetAttribute("Threshold", DoubleValue(floodThreshold));
        responderHelper.SetAttribute("SemVerId", UintegerValue(1));
        responderHelper.SetAttribute("ReplyJitterUs", UintegerValue(g_serviceJitterUs));

        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = topo.domainNodes[d];
            std::string domainName = "/domain" + std::to_string(d);

            responderHelper.SetAttribute("Prefix", StringValue(domainName));
            auto apps = responderHelper.Install(domainNode);

            if (auto responder = DynamicCast<FloodResponderApp>(apps.Get(0))) {
                // Load centroids (for domain mode)
                responder->SetCentroids(GetDomainCentroids(d));
                // Load doc embeddings (for producer mode)
                if (m_content.count(d)) {
                    std::vector<FloodContentEntry> entries;
                    for (const auto& c : m_content[d]) {
                        FloodContentEntry e;
                        e.docId = c.docId;
                        e.vector = MakeSemanticVector(c.vector);
                        entries.push_back(e);
                    }
                    responder->SetLocalContent(entries);
                }
            }

            // SemanticProducer for stage-2 fetch
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.SetAttribute("Freshness", TimeValue(MilliSeconds(g_dataFreshnessMs)));
            producerHelper.SetAttribute("ReplyJitterUs", UintegerValue(g_serviceJitterUs));
            producerHelper.Install(domainNode);
        }

        // ---- Global routing ----
        GlobalRoutingHelper grHelper;
        grHelper.Install(topo.allNodes);
        for (uint32_t d = 0; d < m_domains; ++d) {
            std::string domainName = "/domain" + std::to_string(d);
            grHelper.AddOrigins(domainName, topo.domainNodes[d]);
            grHelper.AddOrigins(domainName + "/data", topo.domainNodes[d]);
        }
        GlobalRoutingHelper::CalculateRoutes();

        // ---- Domain list ----
        std::vector<Name> allDomains;
        for (uint32_t d = 0; d < m_domains; ++d) {
            allDomains.push_back(Name("/domain" + std::to_string(d)));
        }

        // ---- FloodingDiscoveryConsumer ----
        AppHelper consumerHelper("ns3::ndn::FloodingDiscoveryConsumer");
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency));
        consumerHelper.SetAttribute("ParallelMode", BooleanValue(true));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("ProbeBudget", UintegerValue(g_floodProbeBudget));
        auto consumerApps = consumerHelper.Install(consumerNode);
        consumerApps.Start(Seconds(g_warmupSec));

        Ptr<FloodingDiscoveryConsumer> consumer =
            DynamicCast<FloodingDiscoveryConsumer>(consumerApps.Get(0));
        if (consumer) {
            consumer->SetAllDomains(allDomains);
            double setTraceAt = std::max(0.0, g_warmupSec - 0.1);
            Simulator::Schedule(Seconds(setTraceAt), [consumer, this]() {
                consumer->SetQueryTrace(m_queryTrace);
            });
        }

        Simulator::Stop(Seconds(g_simTime));
        ProcessFailureArgs(&topo, &m_queryTrace);
        Simulator::Run();

        // ---- Collect logs ----
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            for (size_t i = 0; i < txs.size(); ++i) {
                const auto& qi = ResolveQueryItem(txs[i].queryId, i);
                logs.push_back(FloodingTxToQueryLog(static_cast<uint32_t>(txs[i].queryId), "flood", txs[i], qi));
            }
        }

        // Print responder stats
        uint64_t totalFloodBytes = 0;
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = topo.domainNodes[d];
            for (uint32_t j = 0; j < domainNode->GetNApplications(); ++j) {
                auto resp = DynamicCast<FloodResponderApp>(domainNode->GetApplication(j));
                if (resp) {
                    totalFloodBytes += resp->GetTotalBytesServed();
                }
            }
        }
        NS_LOG_UNCOND("  FloodResponder totalBytesServed=" << totalFloodBytes
                      << " mode=" << responderMode);

        uint32_t fibSize = GetFibSize(consumerNode);
        Simulator::Destroy();
        m_lastFibSize = fibSize;
        m_lastLsdbSize = 0;
        return logs;
    }

    // =========================================================================
    // Helper: Load Tag Index
    // =========================================================================
    std::map<uint32_t, std::vector<uint64_t>> LoadTagIndex(const std::string& path) {
        std::map<uint32_t, std::vector<uint64_t>> domainTags;
        std::ifstream file(path);
        if (!file.is_open()) {
            NS_LOG_ERROR("Failed to open tag index: " << path);
            return domainTags;
        }
        std::string line;
        std::getline(file, line); // header
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string seg;
            std::vector<std::string> parts;
            while(std::getline(ss, seg, ',')) parts.push_back(seg);
            try {
                if (parts.size() >= 2) {
                    uint64_t tid = std::stoull(parts[0]);
                    uint32_t dom = std::stoul(parts[1]);
                    domainTags[dom].push_back(tid);
                }
            } catch (...) {}
        }
        return domainTags;
    }

    // =========================================================================
    // Scheme: tag (INF-NDN style)
    // =========================================================================
    // =========================================================================
    // Scheme: tag (INF-NDN style)
    // =========================================================================
    std::vector<QueryLog> RunTag() {
        NS_LOG_UNCOND("\n=== Running scheme: tag ===");
        std::vector<QueryLog> logs;
        m_lastTagMissingRate = 0.0;
        m_lastTagFailureBreakdown.clear();

        TopologyLayout topo = BuildTopology(m_domains);
        AssignNodeNames(topo);
        Ptr<Node> consumerNode = topo.ingress;

        // Install Stack
        InternetStackHelper internet;
        internet.Install(topo.allNodes);

        // Install NFD
        StackHelper ndnHelper;
        ndnHelper.setPolicy("nfd::cs::lru");
        ndnHelper.setCsSize(g_csSize);
        ndnHelper.Install(topo.allNodes);

        // Global Routing for base connectivity
        GlobalRoutingHelper grHelper;
        grHelper.Install(topo.allNodes);

        // 2. Load Data
        // Load tag index
        auto domainTags = LoadTagIndex(g_tagIndexFile);
        uint32_t tagLsdbEntries = 0;
        for (const auto& p : domainTags) {
            tagLsdbEntries += p.second.size();
        }

        std::map<uint32_t, uint32_t> nodeIdToDomain;
        for (uint32_t d = 0; d < topo.domainNodes.size(); ++d) {
            nodeIdToDomain[topo.domainNodes[d]->GetId()] = d;
        }
        
        // 3. Install Apps
        
        bool useTagLsa = (g_lsaPeriodMs > 0);
        std::vector<Ptr<Node>> tagRouterNodes;
        if (useTagLsa) {
            // Keep TagRouterApp on ingress + domain gateways to avoid excessive
            // control-plane overhead on large Rocketfuel topologies while still
            // covering transit nodes on ingress->domain shortest paths.
            NS_LOG_UNCOND("Installing TagRouterApp...");
            tagRouterNodes = CollectIngressPathNodes(topo);
            NS_LOG_UNCOND("TagRouter nodes on path-set: " << tagRouterNodes.size());
            for (auto& node : tagRouterNodes) {
                AppHelper routerHelper("ns3::ndn::TagRouterApp");
                std::string routerId = EnsureNodeName(node);
                routerHelper.SetAttribute("RouterId", StringValue(routerId));
                routerHelper.SetAttribute("LsaPeriod", TimeValue(MilliSeconds(g_lsaPeriodMs)));
                
                auto apps = routerHelper.Install(node);
                apps.Start(Seconds(1.0));
                
                auto app = DynamicCast<TagRouterApp>(apps.Get(0));
                if (app) {
                    auto it = nodeIdToDomain.find(node->GetId());
                    if (it != nodeIdToDomain.end() && domainTags.count(it->second)) {
                        app->SetLocalTags(domainTags[it->second]);
                    }
                }
            }
        }

        // TagDomainApp on Domains
        NS_LOG_UNCOND("Installing TagDomainApp...");
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> node = topo.domainNodes[d];
            std::string domainName = "/domain" + std::to_string(d);
            
            AppHelper domainHelper("ns3::ndn::TagDomainApp");
            domainHelper.SetAttribute("DomainPrefix", StringValue(domainName));
            domainHelper.SetAttribute("Threshold", DoubleValue(0.0)); 
            domainHelper.SetAttribute("ReplyJitterUs", UintegerValue(g_serviceJitterUs));
            auto apps = domainHelper.Install(node);
            apps.Start(Seconds(1.0));
            
            auto app = DynamicCast<TagDomainApp>(apps.Get(0));
            if (app) {
                if (domainTags.count(d)) app->SetTags(domainTags[d]);
                std::vector<TagContentEntry> entries;
                if (m_content.count(d)) {
                     for (const auto& c : m_content[d]) {
                         TagContentEntry e;
                         e.docId = c.docId;
                         e.vector = MakeSemanticVector(c.vector); 
                         entries.push_back(e);
                     }
                }
                app->SetLocalContent(entries);
            }
            
            // SemanticProducer for stage-2 fetch
            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainName + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.SetAttribute("Freshness", TimeValue(MilliSeconds(g_dataFreshnessMs)));
            producerHelper.SetAttribute("ReplyJitterUs", UintegerValue(g_serviceJitterUs));
            producerHelper.Install(node);
            
            // Global Routing
            grHelper.AddOrigins(domainName, node);
            grHelper.AddOrigins(domainName + "/data", node);
        }

        // TagDiscoveryConsumer on Ingress
        NS_LOG_UNCOND("Installing TagDiscoveryConsumer...");
        AppHelper consumerHelper("ns3::ndn::TagDiscoveryConsumer");
        consumerHelper.SetAttribute("QueryToTagFile", StringValue(g_queryToTagFile));
        consumerHelper.SetAttribute("FetchTimeoutMs", UintegerValue(g_fetchTimeoutMs));
        consumerHelper.SetAttribute("TagK", UintegerValue(g_tagK));
        consumerHelper.SetAttribute("Frequency", DoubleValue(g_frequency)); 
        
        auto consumerApps = consumerHelper.Install(consumerNode);
        consumerApps.Start(Seconds(g_warmupSec)); 
        
        Ptr<TagDiscoveryConsumer> consumer = 
            DynamicCast<TagDiscoveryConsumer>(consumerApps.Get(0));
        if (consumer) {
            double setTraceAt = std::max(0.0, g_warmupSec - 0.1);
            Simulator::Schedule(Seconds(setTraceAt), [consumer, this]() {
                 consumer->SetQueryTrace(m_queryTrace);
            });
        }
        
        grHelper.CalculateRoutes();
        
        if (useTagLsa) {
            // Build tree-like bidirectional routes for tag LSAs.
            NS_LOG_UNCOND("Configuring FIB...");
            Name broadcastPrefix("/ndn/broadcast/tag-lsa");
            ConfigureBroadcastFib(tagRouterNodes, consumerNode, broadcastPrefix);
            StrategyChoiceHelper::InstallAll(broadcastPrefix, "/localhost/nfd/strategy/multicast");
        }

        Simulator::Stop(Seconds(g_simTime));
        ProcessFailureArgs(&topo, &m_queryTrace);
        Simulator::Run();
        
        // Collect logs
        if (consumer) {
            const auto& txs = consumer->GetTransactions();
            for (size_t i = 0; i < txs.size(); ++i) {
                const auto& qi = ResolveQueryItem(txs[i].queryId, i);
                logs.push_back(FloodingTxToQueryLog(static_cast<uint32_t>(txs[i].queryId), "tag", txs[i], qi));
            }

            uint64_t totalLookups = consumer->GetTotalTagLookups();
            uint64_t missingLookups = consumer->GetMissingTagMappings();
            m_lastTagMissingRate = totalLookups > 0 ? static_cast<double>(missingLookups) / totalLookups : 0.0;
            NS_LOG_UNCOND("  Tag query_to_tag missing rate=" << std::fixed << std::setprecision(4)
                          << m_lastTagMissingRate
                          << " (" << missingLookups << "/" << totalLookups << ")");
        }

        uint32_t noTag = static_cast<uint32_t>(std::round(m_lastTagMissingRate * logs.size()));
        uint32_t noRoute = 0;
        uint32_t timeout = 0;
        uint32_t wrongDomain = 0;
        for (const auto& q : logs) {
            if (q.is_success) {
                continue;
            }
            if (q.is_timeout) {
                timeout++;
                if (q.pred_domain.empty()) {
                    noRoute++;
                }
            } else if (q.domain_hit == 0) {
                wrongDomain++;
            }
        }
        std::ostringstream tagFail;
        tagFail << "no_tag=" << noTag
                << ";no_route=" << noRoute
                << ";timeout=" << timeout
                << ";wrong_domain=" << wrongDomain;
        m_lastTagFailureBreakdown = tagFail.str();
        NS_LOG_UNCOND("  Tag failure breakdown: " << m_lastTagFailureBreakdown);
        
        // Stats
        uint64_t totalLsaBytes = 0;
        for (uint32_t i = 0; i < topo.allNodes.GetN(); ++i) {
            Ptr<Node> n = topo.allNodes.Get(i);
            for (uint32_t j = 0; j < n->GetNApplications(); ++j) {
                auto app = DynamicCast<TagRouterApp>(n->GetApplication(j));
                if (app) {
                    totalLsaBytes += app->GetLsaBytesSent();
                    totalLsaBytes += app->GetLsaBytesRecv();
                }
            }
        }
        NS_LOG_UNCOND("  TagRouter totalLsaBytes=" << totalLsaBytes);
        
        uint32_t fibSize = GetFibSize(consumerNode);
        Simulator::Destroy();
        m_lastFibSize = fibSize;
        m_lastLsdbSize = tagLsdbEntries;
        return logs;
    }

    // =========================================================================
    // Scheme: SANR-CMF abstraction on top of Tag forwarding
    // =========================================================================
    std::vector<QueryLog> RunSanrTag() {
        NS_LOG_UNCOND("\n=== Running scheme: sanr-tag (SANR-CMF over INF-NDN forwarding) ===");
        auto logs = RunTag();
        ApplySanrCacheModel(logs, "sanr-tag", false);
        return logs;
    }

    // =========================================================================
    // Scheme: SANR-CMF abstraction with oracle domain selection (upper-bound)
    // =========================================================================
    std::vector<QueryLog> RunSanrOracle(const std::string& qrelsFile,
                                        const std::string& contentFile,
                                        const std::string& traceFile) {
        NS_LOG_UNCOND("\n=== Running scheme: sanr-oracle (SANR-CMF with oracle domain) ===");
        auto logs = RunCentralized("oracle", g_dirProcMs, g_dirProcJitterUs, g_dirTopK, g_dirFailTime,
                                   qrelsFile, contentFile, traceFile);
        ApplySanrCacheModel(logs, "sanr-oracle", true);
        return logs;
    }

    uint32_t GetLastSanrIndexEntries() const { return m_lastSanrIndexEntries; }
    double GetLastSanrSemanticHitRatio() const { return m_lastSanrSemanticHitRatio; }
    double GetLastSanrExactHitRatio() const { return m_lastSanrExactHitRatio; }

    uint32_t GetLastFibSize() const { return m_lastFibSize; }
    uint32_t GetLastLsdbSize() const { return m_lastLsdbSize; }
    double GetLastTagMissingRate() const { return m_lastTagMissingRate; }
    const std::string& GetLastTagFailureBreakdown() const { return m_lastTagFailureBreakdown; }

private:
    struct SanrObjectInfo {
        std::string docId;
        std::string canonicalName;
        uint32_t domainId = 0;
        iroute::SemanticVector vector;
    };

    struct SanrCacheEntry {
        std::string docId;
        std::string canonicalName;
        uint32_t domainId = 0;
        iroute::SemanticVector vector;
        double insertedSec = 0.0;
        double expireSec = 0.0;
        bool prefetched = false;
        uint64_t hits = 0;
    };

    int ParsePrimaryDomainId(const std::string& text) const {
        auto toks = SplitBySemicolon(text);
        for (const auto& tok : toks) {
            int did = ParseDomainId(tok);
            if (did >= 0) {
                return did;
            }
        }
        return -1;
    }

    static double SafeCosine(const iroute::SemanticVector& a, const iroute::SemanticVector& b) {
        try {
            return a.computeCosineSimilarity(b);
        } catch (...) {
            return -1.0;
        }
    }

    void InsertSanrCache(std::vector<SanrCacheEntry>& cache,
                         const SanrObjectInfo& obj,
                         double nowSec,
                         bool prefetched,
                         size_t maxEntries) {
        for (auto& e : cache) {
            if (e.canonicalName == obj.canonicalName) {
                e.insertedSec = nowSec;
                e.expireSec = nowSec + g_sanrCmltSec;
                e.prefetched = prefetched;
                return;
            }
        }
        SanrCacheEntry e;
        e.docId = obj.docId;
        e.canonicalName = obj.canonicalName;
        e.domainId = obj.domainId;
        e.vector = obj.vector;
        e.insertedSec = nowSec;
        e.expireSec = nowSec + g_sanrCmltSec;
        e.prefetched = prefetched;
        cache.push_back(e);
        if (cache.size() > maxEntries && maxEntries > 0) {
            auto victim = std::min_element(cache.begin(), cache.end(),
                                           [](const auto& x, const auto& y) {
                                               return x.insertedSec < y.insertedSec;
                                           });
            if (victim != cache.end()) {
                cache.erase(victim);
            }
        }
    }

    void ApplySanrCacheModel(std::vector<QueryLog>& logs,
                             const std::string& schemeName,
                             bool oracleDomain) {
        if (logs.empty()) {
            m_lastSanrIndexEntries = 0;
            m_lastSanrExactHitRatio = 0.0;
            m_lastSanrSemanticHitRatio = 0.0;
            return;
        }

        std::unordered_map<std::string, SanrObjectInfo> byCanonical;
        std::unordered_map<std::string, SanrObjectInfo> byDoc;
        std::unordered_map<uint32_t, std::vector<SanrObjectInfo>> byDomain;
        for (const auto& kv : m_content) {
            uint32_t did = kv.first;
            for (const auto& c : kv.second) {
                if (c.vector.empty()) {
                    continue;
                }
                SanrObjectInfo info;
                info.docId = c.docId;
                info.canonicalName = c.canonicalName;
                info.domainId = did;
                info.vector = MakeSemanticVector(c.vector);
                byCanonical[info.canonicalName] = info;
                byDoc[info.docId] = info;
                byDomain[did].push_back(info);
            }
        }

        size_t maxCacheEntries = static_cast<size_t>(std::max<uint32_t>(1, g_csSize > 0 ? g_csSize : 512));
        std::vector<SanrCacheEntry> cache;
        cache.reserve(maxCacheEntries + 8);

        struct MsrrRow { int slot; uint32_t nodeId; std::string clusterId; uint32_t count; int selected; };
        struct CcnRow { int slot; uint32_t domainId; uint32_t nodeId; uint32_t msrrCount; int selected; };
        struct PlacementRow { int slot; uint32_t nodeId; std::string objectName; int success; double latencyMs; uint64_t bytes; };
        std::vector<MsrrRow> msrrRows;
        std::vector<CcnRow> ccnRows;
        std::vector<PlacementRow> placementRows;

        std::vector<size_t> order(logs.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return logs[a].t_send_disc < logs[b].t_send_disc;
        });

        int currentSlot = -1;
        std::map<int, uint32_t> slotClusterCount;
        uint32_t ingressNodeId = g_ingressNodeIndex;
        uint32_t semanticHitCount = 0;
        uint32_t exactHitCount = 0;
        uint32_t measurableCount = 0;
        int prefetchN = std::max(1, std::min<int>(20, static_cast<int>(std::max<uint32_t>(10, g_csSize) / 40)));

        auto finalizeSlot = [&](int slotId, double nowSec) {
            if (slotId < 0) {
                return;
            }
            std::vector<std::pair<int, uint32_t>> ranked(slotClusterCount.begin(), slotClusterCount.end());
            std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;
            });
            size_t pickN = 0;
            if (!ranked.empty()) {
                pickN = static_cast<size_t>(std::ceil(g_sanrMsrrTopPct * static_cast<double>(ranked.size())));
                pickN = std::max<size_t>(1, std::min<size_t>(pickN, ranked.size()));
            }
            std::set<int> selectedClusters;
            for (size_t i = 0; i < pickN; ++i) {
                selectedClusters.insert(ranked[i].first);
            }
            for (const auto& kv : ranked) {
                std::string cid = "domain" + std::to_string(kv.first);
                int selected = selectedClusters.count(kv.first) > 0 ? 1 : 0;
                msrrRows.push_back({slotId, ingressNodeId, cid, kv.second, selected});
                ccnRows.push_back({slotId, static_cast<uint32_t>(kv.first), ingressNodeId, kv.second, selected});
            }

            for (int cid : selectedClusters) {
                auto dit = byDomain.find(static_cast<uint32_t>(cid));
                if (dit == byDomain.end() || dit->second.empty()) {
                    continue;
                }
                int placed = 0;
                for (const auto& obj : dit->second) {
                    if (placed >= prefetchN) {
                        break;
                    }
                    InsertSanrCache(cache, obj, nowSec, true, maxCacheEntries);
                    placementRows.push_back({slotId, ingressNodeId, obj.canonicalName, 1, 0.0, 1200});
                    placed++;
                }
            }
            slotClusterCount.clear();
        };

        for (size_t pos = 0; pos < order.size(); ++pos) {
            auto& q = logs[order[pos]];
            q.scheme = schemeName;

            double nowSec = q.t_send_disc / 1000.0;
            int slot = static_cast<int>(std::floor(nowSec / std::max(0.1, g_sanrSlotSec)));
            if (currentSlot < 0) {
                currentSlot = slot;
            } else if (slot != currentSlot) {
                finalizeSlot(currentSlot, nowSec);
                currentSlot = slot;
            }

            const auto& qi = ResolveQueryItem(q.qid, static_cast<size_t>(q.qid));
            int clusterId = -1;
            for (const auto& td : qi.targetDomains) {
                int did = ParseDomainId(td);
                if (did >= 0) {
                    clusterId = did;
                    break;
                }
            }
            if (clusterId < 0) {
                clusterId = ParsePrimaryDomainId(q.gt_domain);
            }
            if (clusterId < 0) {
                clusterId = ParsePrimaryDomainId(q.pred_domain);
            }
            if (clusterId >= 0) {
                slotClusterCount[clusterId]++;
                q.sanr_cluster_id = "domain" + std::to_string(clusterId);
            }

            cache.erase(std::remove_if(cache.begin(), cache.end(),
                                       [&](const auto& e) { return e.expireSec <= nowSec; }),
                        cache.end());

            bool hasFetchStage = (q.t_send_fetch_ns > 0) || (q.t_send_fetch > 0.0);
            size_t exactIdx = static_cast<size_t>(-1);
            if (!q.pred_doc.empty()) {
                for (size_t i = 0; i < cache.size(); ++i) {
                    if (cache[i].canonicalName == q.pred_doc) {
                        exactIdx = i;
                        break;
                    }
                }
            }

            size_t bestIdx = static_cast<size_t>(-1);
            double bestSim = -1.0;
            if (g_sanrEnableSemantic) {
                size_t scanned = 0;
                // Approximate "top-L semantic index candidates": scan most-recent cache entries first.
                for (size_t rev = cache.size(); rev > 0; --rev) {
                    if (scanned >= std::max<uint32_t>(1, g_sanrTopL)) {
                        break;
                    }
                    size_t i = rev - 1;
                    double sim = SafeCosine(qi.vector, cache[i].vector);
                    if (sim > bestSim) {
                        bestSim = sim;
                        bestIdx = i;
                    }
                    scanned++;
                }
            }
            q.sim_max = std::max(0.0, bestSim);

            bool exactHit = hasFetchStage && exactIdx != static_cast<size_t>(-1);
            bool semanticHit = hasFetchStage && !exactHit &&
                               bestIdx != static_cast<size_t>(-1) &&
                               (bestSim >= g_sanrSimThresh);

            if (q.is_measurable > 0) {
                measurableCount++;
            }

            if (exactHit || semanticHit) {
                size_t hitIdx = exactHit ? exactIdx : bestIdx;
                auto& ce = cache[hitIdx];
                ce.hits++;
                q.cache_hit_exact = exactHit ? 1 : 0;
                q.cache_hit_semantic = semanticHit ? 1 : 0;
                q.matched_cache_name = ce.canonicalName;
                q.pred_doc = ce.canonicalName;
                q.pred_domain = "/domain" + std::to_string(ce.domainId);
                q.domain_hit = CheckDomainHit(q.pred_domain, qi.targetDomains) ? 1 : 0;
                q.hit_exact = CheckDocHit(q.pred_doc, qi.targetDocIds) ? 1 : 0;
                q.ssr_score = std::max(0.0, SafeCosine(qi.vector, ce.vector));
                q.is_success = 1;
                q.is_timeout = 0;
                q.timeouts = 0;
                q.timeout_reason.clear();
                q.hops_fetch = 0;
                q.hops_total = q.hops_disc >= 0 ? q.hops_disc : 0;

                if (q.t_send_fetch_ns == 0 && q.t_recv_disc_reply_ns > 0) {
                    q.t_send_fetch_ns = q.t_recv_disc_reply_ns;
                }
                if (q.t_send_fetch_ns > 0) {
                    q.t_recv_data_ns = q.t_send_fetch_ns;
                }
                if (q.t_send_fetch <= 0.0 && q.t_recv_disc_reply > 0.0) {
                    q.t_send_fetch = q.t_recv_disc_reply;
                }
                if (q.t_send_fetch > 0.0) {
                    q.t_recv_data = q.t_send_fetch;
                }
                q.fetch_ms = 0.0;
                if (q.t_send_disc_ns > 0 && q.t_recv_data_ns >= q.t_send_disc_ns) {
                    q.rtt_total_ms = static_cast<double>(q.t_recv_data_ns - q.t_send_disc_ns) / 1e6;
                } else if (q.t_send_disc > 0.0 && q.t_recv_data >= q.t_send_disc) {
                    q.rtt_total_ms = q.t_recv_data - q.t_send_disc;
                }
                if (q.cache_hit_exact > 0) {
                    exactHitCount++;
                }
                if (q.cache_hit_semantic > 0) {
                    semanticHitCount++;
                }
            } else {
                q.cache_hit_exact = 0;
                q.cache_hit_semantic = 0;
                q.matched_cache_name.clear();
                if (q.is_success > 0 && !q.pred_doc.empty()) {
                    auto itObj = byCanonical.find(q.pred_doc);
                    if (itObj != byCanonical.end()) {
                        q.ssr_score = std::max(0.0, SafeCosine(qi.vector, itObj->second.vector));
                    }
                }
            }

            if (oracleDomain && !qi.targetDomains.empty()) {
                q.pred_domain = qi.targetDomains.front();
                q.domain_hit = 1;
            }

            if (q.is_success > 0 && !q.pred_doc.empty()) {
                auto itObj = byCanonical.find(q.pred_doc);
                if (itObj != byCanonical.end()) {
                    InsertSanrCache(cache, itObj->second, nowSec, false, maxCacheEntries);
                } else {
                    for (const auto& td : qi.targetDocIds) {
                        auto itDoc = byDoc.find(td);
                        if (itDoc != byDoc.end()) {
                            InsertSanrCache(cache, itDoc->second, nowSec, false, maxCacheEntries);
                            break;
                        }
                    }
                }
            }
        }

        if (currentSlot >= 0) {
            double endSec = logs[order.back()].t_send_disc / 1000.0;
            finalizeSlot(currentSlot, endSec + std::max(0.1, g_sanrSlotSec));
        }

        {
            std::ofstream f(g_resultDir + "/msrr_debug.csv");
            f << "time_slot,nodeId,clusterId,count,selected\n";
            for (const auto& r : msrrRows) {
                f << r.slot << "," << r.nodeId << "," << CsvEscape(r.clusterId) << "," << r.count << "," << r.selected << "\n";
            }
        }
        {
            std::ofstream f(g_resultDir + "/ccn_debug.csv");
            f << "time_slot,domainId,nodeId,msrr_count,selected\n";
            for (const auto& r : ccnRows) {
                f << r.slot << "," << r.domainId << "," << r.nodeId << "," << r.msrrCount << "," << r.selected << "\n";
            }
        }
        {
            std::ofstream f(g_resultDir + "/placement_log.csv");
            f << "time_slot,nodeId,object_name,success,latency_ms,bytes\n";
            for (const auto& r : placementRows) {
                f << r.slot << "," << r.nodeId << "," << CsvEscape(r.objectName) << ","
                  << r.success << "," << std::fixed << std::setprecision(4) << r.latencyMs << "," << r.bytes << "\n";
            }
        }

        m_lastSanrIndexEntries = static_cast<uint32_t>(cache.size());
        m_lastSanrExactHitRatio = measurableCount > 0
            ? static_cast<double>(exactHitCount) / static_cast<double>(measurableCount)
            : 0.0;
        m_lastSanrSemanticHitRatio = measurableCount > 0
            ? static_cast<double>(semanticHitCount) / static_cast<double>(measurableCount)
            : 0.0;

        // account SANR semantic index/state overhead on top of baseline LSDB
        m_lastLsdbSize += m_lastSanrIndexEntries;

        NS_LOG_UNCOND("  SANR cache model applied: entries=" << m_lastSanrIndexEntries
                      << " exactHitRatio=" << std::fixed << std::setprecision(4) << m_lastSanrExactHitRatio
                      << " semanticHitRatio=" << m_lastSanrSemanticHitRatio
                      << " slot=" << g_sanrSlotSec << "s cmlt=" << g_sanrCmltSec << "s");
    }

    const IRouteDiscoveryConsumer::QueryItem&
    ResolveQueryItem(uint64_t queryId, size_t fallbackIndex) const {
        auto it = m_queryById.find(queryId);
        if (it != m_queryById.end()) {
            return it->second;
        }
        if (!m_queryTrace.empty()) {
            size_t idx = std::min(fallbackIndex, m_queryTrace.size() - 1);
            return m_queryTrace[idx];
        }
        static const IRouteDiscoveryConsumer::QueryItem kEmptyQuery;
        return kEmptyQuery;
    }

    std::vector<iroute::CentroidEntry> GetDomainCentroids(uint32_t domainId) const {
        std::vector<iroute::CentroidEntry> result;
        auto it = m_centroids.find(domainId);
        if (it == m_centroids.end()) {
            if (m_centroids.empty()) {
                return result;
            }
            // For scaling sweeps with domains > dataset domains, reuse a template domain.
            uint32_t src = domainId % static_cast<uint32_t>(m_centroids.size());
            auto itSrc = m_centroids.find(src);
            if (itSrc == m_centroids.end()) {
                return result;
            }
            result = itSrc->second;
            for (auto& c : result) {
                c.centroidId += domainId * 1000;
            }
        } else {
            result = it->second;
        }
        if (g_M > 0 && result.size() > g_M) {
            result.resize(g_M);
        }
        return result;
    }

    void ConfigureBroadcastFib(const std::vector<Ptr<Node>>& fibNodes,
                               Ptr<Node> root,
                               const Name& prefix) {
        if (!root) {
            return;
        }
        if (fibNodes.empty()) {
            return;
        }

        std::unordered_set<uint32_t> allowed;
        std::unordered_map<uint32_t, Ptr<Node>> idToNode;
        allowed.reserve(fibNodes.size() * 2);
        idToNode.reserve(fibNodes.size() * 2);
        for (const auto& n : fibNodes) {
            if (!n) {
                continue;
            }
            allowed.insert(n->GetId());
            idToNode[n->GetId()] = n;
        }
        if (allowed.count(root->GetId()) == 0) {
            allowed.insert(root->GetId());
            idToNode[root->GetId()] = root;
        }

        std::queue<Ptr<Node>> q;
        std::set<uint32_t> visited;
        std::map<uint32_t, Ptr<Node>> parent;
        q.push(root);
        visited.insert(root->GetId());

        while (!q.empty()) {
            Ptr<Node> cur = q.front();
            q.pop();
            for (uint32_t i = 0; i < cur->GetNDevices(); ++i) {
                Ptr<NetDevice> dev = cur->GetDevice(i);
                Ptr<Channel> ch = dev->GetChannel();
                if (!ch) {
                    continue;
                }
                for (uint32_t j = 0; j < ch->GetNDevices(); ++j) {
                    Ptr<Node> nxt = ch->GetDevice(j)->GetNode();
                    if (!nxt || allowed.count(nxt->GetId()) == 0) {
                        continue;
                    }
                    if (nxt == cur || visited.count(nxt->GetId()) > 0) {
                        continue;
                    }
                    visited.insert(nxt->GetId());
                    parent[nxt->GetId()] = cur;
                    q.push(nxt);
                }
            }
        }

        for (auto& p : parent) {
            auto itChild = idToNode.find(p.first);
            Ptr<Node> child = (itChild == idToNode.end()) ? nullptr : itChild->second;
            Ptr<Node> par = p.second;
            if (!child || !par) {
                continue;
            }
            FibHelper::AddRoute(child, prefix, par, 0);
            FibHelper::AddRoute(par, prefix, child, 0);
        }
    }

    // Install domain-level IRouteApp + SemanticProducer.
    void InstallDomainApps(const std::vector<Ptr<Node>>& domainNodes, AppHelper& irouteHelper) {
        for (uint32_t d = 0; d < m_domains; ++d) {
            Ptr<Node> domainNode = domainNodes[d];
            std::string domainPrefix = "/domain" + std::to_string(d);
            std::string routerId = "domain" + std::to_string(d); // no leading '/'

            irouteHelper.SetAttribute("RouterId", StringValue(routerId));
            irouteHelper.SetAttribute("IsIngress", BooleanValue(false));
            irouteHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            irouteHelper.SetAttribute("ScoreThresholdTau", DoubleValue(g_tau));
            auto apps = irouteHelper.Install(domainNode);

            if (auto irouteApp = DynamicCast<IRouteApp>(apps.Get(0))) {
                irouteApp->SetLocalCentroids(GetDomainCentroids(d));
                if (m_content.count(d)) {
                    std::vector<IRouteApp::ContentEntry> entries;
                    for (const auto& c : m_content[d]) {
                        IRouteApp::ContentEntry e;
                        e.docId = c.docId;
                        e.vector = MakeSemanticVector(c.vector);
                        entries.push_back(e);
                    }
                    irouteApp->SetLocalContent(entries);
                }
            }

            AppHelper producerHelper("ns3::ndn::SemanticProducer");
            producerHelper.SetAttribute("Prefix", StringValue(domainPrefix + "/data"));
            producerHelper.SetAttribute("VectorDim", UintegerValue(g_vectorDim));
            producerHelper.SetAttribute("Freshness", TimeValue(MilliSeconds(g_dataFreshnessMs)));
            producerHelper.Install(domainNode);
        }
    }

    void SetupGlobalRouting(const NodeContainer& allNodes,
                            const std::vector<Ptr<Node>>& domainNodes) {
        GlobalRoutingHelper grHelper;
        grHelper.Install(allNodes);
        for (uint32_t d = 0; d < m_domains; ++d) {
            grHelper.AddOrigins("/domain" + std::to_string(d), domainNodes[d]);
            grHelper.AddOrigins("/domain" + std::to_string(d) + "/data", domainNodes[d]);
        }
        GlobalRoutingHelper::CalculateRoutes();
    }

    uint32_t GetFibSize(Ptr<Node> node) {
        return node->GetObject<L3Protocol>()->getForwarder()->getFib().size();
    }

    // Members
    std::string m_traceFile;
    std::string m_contentFile;
    std::string m_centroidsFile;
    uint32_t m_seed;
    uint32_t m_domains;
    std::map<uint32_t, std::vector<iroute::CentroidEntry>> m_centroids;
    std::map<uint32_t, std::vector<ContentEntry>> m_content;
    std::vector<IRouteDiscoveryConsumer::QueryItem> m_queryTrace;
    std::unordered_map<uint64_t, IRouteDiscoveryConsumer::QueryItem> m_queryById;
    
    std::mt19937 m_rng;
    uint32_t m_lastFibSize = 0;
    uint32_t m_lastLsdbSize = 0;
    double m_lastTagMissingRate = 0.0;
    std::string m_lastTagFailureBreakdown = "";
    uint32_t m_lastSanrIndexEntries = 0;
    double m_lastSanrSemanticHitRatio = 0.0;
    double m_lastSanrExactHitRatio = 0.0;
};

int main(int argc, char* argv[]) {
    CommandLine cmd;
    cmd.AddValue("failLink", "Link failure: u-v@time", g_failLink);
    cmd.AddValue("failDomain", "Domain failure: d@time", g_failDomain);
    cmd.AddValue("churn", "Churn event: type@time@ratio", g_churn);
    cmd.AddValue("scheme", "Routing scheme: iroute|exact|central|flood|tag|sanr-tag|sanr-oracle", g_scheme);
    cmd.AddValue("centroids", "Centroids CSV", g_centroidsFile);
    cmd.AddValue("content", "Content CSV", g_contentFile);
    cmd.AddValue("trace", "Trace CSV", g_traceFile);
    cmd.AddValue("shuffleTrace", "Shuffle query trace order by seed before simulation (0/1)", g_shuffleTrace);
    cmd.AddValue("domains", "Number of domains", g_domains);
    cmd.AddValue("M", "Max centroids per domain to use", g_M);
    cmd.AddValue("K", "Top-K for query", g_K);
    cmd.AddValue("tau", "Similarity threshold", g_tau);
    cmd.AddValue("simTime", "Simulation time (sec)", g_simTime);
    cmd.AddValue("seed", "Random seed", g_seed);
    cmd.AddValue("topo", "Topology type: star|rocketfuel|redundant", g_topo);
    cmd.AddValue("topoFile", "Topology file for rocketfuel mode", g_topoFile);
    cmd.AddValue("ingressNode", "Ingress node index in topology", g_ingressNodeIndex);
    cmd.AddValue("linkDelayMs", "Base link delay (ms) for star/redundant topologies", g_linkDelayMs);
    cmd.AddValue("linkDelayJitterUs", "Per-link delay jitter (uniform, microseconds) for star/redundant", g_linkDelayJitterUs);
    cmd.AddValue("rocketfuelAutoJitter", "Auto-apply per-link delay jitter on Rocketfuel when linkDelayJitterUs=0", g_rocketfuelAutoJitter);
    cmd.AddValue("serviceJitterUs", "Per-reply processing jitter (+/- microseconds) for discovery/data responders", g_serviceJitterUs);
    cmd.AddValue("dataFreshnessMs", "Freshness period for SemanticProducer data (ms)", g_dataFreshnessMs);
    cmd.AddValue("resultDir", "Result directory", g_resultDir);
    cmd.AddValue("index", "Index file for exact/central", g_indexFile);
    cmd.AddValue("exactUseIndex", "Exact baseline mode (true=use index/oracle, false=intent-only strict exact)", g_exactUseIndex);
    cmd.AddValue("qrels", "Qrels TSV (for scheme=central)", g_qrelsFile);
    cmd.AddValue("dirMode", "Directory mode for central: oracle|learned", g_dirMode);
    cmd.AddValue("dirProcMs", "Directory processing delay (ms)", g_dirProcMs);
    cmd.AddValue("dirProcJitterUs", "Directory processing jitter (+/- us)", g_dirProcJitterUs);
    cmd.AddValue("dirTopK", "Directory shortlist size", g_dirTopK);
    cmd.AddValue("dirFailTime", "Directory fail time (s, negative=disabled)", g_dirFailTime);
    cmd.AddValue("frequency", "Query frequency (Hz)", g_frequency);
    cmd.AddValue("floodResponder", "Flood responder mode: domain|producer", g_floodResponder);
    cmd.AddValue("floodThreshold", "Flood similarity threshold", g_floodThreshold);
    cmd.AddValue("floodProbeBudget", "Max domains probed by flood/tag (0=all)", g_floodProbeBudget);
    cmd.AddValue("tagIndex", "Tag index CSV (tid,domain,weight)", g_tagIndexFile);
    cmd.AddValue("queryToTag", "Query->Tag CSV (qid,tid)", g_queryToTagFile);
    cmd.AddValue("tagK", "Tag shortlist size for tag discovery", g_tagK);
    cmd.AddValue("lsaPeriod", "LSA broadcast period (ms)", g_lsaPeriodMs);
    cmd.AddValue("sanrSimThresh", "SANR semantic cache hit gate threshold", g_sanrSimThresh);
    cmd.AddValue("msrrTopPct", "SANR MSRR top percentile per slot", g_sanrMsrrTopPct);
    cmd.AddValue("cmltSec", "SANR content max lifetime (seconds)", g_sanrCmltSec);
    cmd.AddValue("slotSec", "SANR slot duration (seconds)", g_sanrSlotSec);
    cmd.AddValue("ccnK", "SANR CCN count per domain", g_sanrCcnK);
    cmd.AddValue("sanrTopL", "SANR semantic lookup top-L candidates", g_sanrTopL);
    cmd.AddValue("sanrEnableSemantic", "Enable SANR semantic cache hit gate", g_sanrEnableSemantic);
    cmd.AddValue("churnTime", "Time to trigger churn (sec, -1=none)", g_tagChurnTime);
    cmd.AddValue("warmupSec", "Warmup duration with no query traffic", g_warmupSec);
    cmd.AddValue("measureStartSec", "Measurement start time", g_measureStartSec);
    cmd.AddValue("csSize", "Content Store size (set 0 to disable cache)", g_csSize);
    cmd.AddValue("cdfSuccessOnly", "Latency metrics use success-only samples", g_cdfSuccessOnly);
    cmd.AddValue("failureTargetPolicy", "Failure target policy: auto-critical|auto-noncut|manual", g_failureTargetPolicy);
    cmd.AddValue("failHotDomainRank", "Hot-domain rank used by auto-critical", g_failHotDomainRank);
    cmd.AddValue("failRecoverySec", "Recover failed link after N seconds (<=0 disables recovery)", g_failRecoverySec);
    cmd.AddValue("churnRecoverySec", "Recover churned state after N seconds (<=0 disables recovery)", g_churnRecoverySec);
    cmd.AddValue("churnRounds", "Number of churn events to trigger", g_churnRounds);
    cmd.AddValue("churnIntervalSec", "Interval between churn events (seconds)", g_churnIntervalSec);

    cmd.Parse(argc, argv);

    // Auto-detect index file if not specified
    if (g_indexFile.empty() && g_scheme == "exact" && g_exactUseIndex) {
        // Try same directory as trace file
        auto pos = g_traceFile.rfind('/');
        if (pos != std::string::npos) {
            g_indexFile = g_traceFile.substr(0, pos + 1) + "index_exact.csv";
        } else {
            g_indexFile = "index_exact.csv";
        }
        NS_LOG_UNCOND("  Auto-detected indexFile=" << g_indexFile);
    }

    CreateDirectories(g_resultDir);

    // Validate
    if (g_traceFile.empty()) {
        NS_LOG_ERROR("Missing trace file. Use --trace");
        return 1;
    }
    // centroids and content only needed for non-exact/non-central schemes
    if (g_scheme != "exact" && g_scheme != "central" &&
        (g_centroidsFile.empty() || g_contentFile.empty())) {
        NS_LOG_ERROR("Missing data files. Use --centroids, --content");
        return 1;
    }
    // central scheme needs qrels + content
    if (g_scheme == "central") {
        if (g_qrelsFile.empty()) {
            // Auto-detect from trace dir
            auto pos = g_traceFile.rfind('/');
            if (pos != std::string::npos) {
                g_qrelsFile = g_traceFile.substr(0, pos + 1) + "qrels.tsv";
            } else {
                g_qrelsFile = "qrels.tsv";
            }
            NS_LOG_UNCOND("  Auto-detected qrels=" << g_qrelsFile);
        }
        if (g_contentFile.empty()) {
            auto pos = g_traceFile.rfind('/');
            if (pos != std::string::npos) {
                g_contentFile = g_traceFile.substr(0, pos + 1) + "producer_content.csv";
            } else {
                g_contentFile = "producer_content.csv";
            }
            NS_LOG_UNCOND("  Auto-detected content=" << g_contentFile);
        }
    }
    if (g_scheme == "exact" && g_exactUseIndex && !std::filesystem::exists(g_indexFile)) {
        NS_LOG_ERROR("Index file not found: " << g_indexFile
                     << ". Generate with: python3 dataset/gen_exact_index.py");
        return 1;
    }
    if (g_topo == "rocketfuel" && !std::filesystem::exists(g_topoFile)) {
        NS_LOG_ERROR("Topology file not found: " << g_topoFile);
        return 1;
    }

    std::set<std::string> validSchemes = {"iroute", "exact", "central", "flood", "tag", "sanr-tag", "sanr-oracle"};
    if (validSchemes.find(g_scheme) == validSchemes.end()) {
        NS_LOG_ERROR("Invalid scheme: " << g_scheme
                     << ". Valid: iroute, exact, central, flood, tag, sanr-tag, sanr-oracle");
        return 1;
    }
    if (g_topo != "star" && g_topo != "rocketfuel" && g_topo != "redundant") {
        NS_LOG_ERROR("Invalid topo=" << g_topo << ". Valid: star|rocketfuel|redundant");
        return 1;
    }
    if (g_failureTargetPolicy != "auto-critical" &&
        g_failureTargetPolicy != "auto-noncut" &&
        g_failureTargetPolicy != "manual") {
        NS_LOG_ERROR("Invalid failureTargetPolicy=" << g_failureTargetPolicy
                     << ". Valid: auto-critical|auto-noncut|manual");
        return 1;
    }
    if (g_failRecoverySec > 0.0 && g_failLink.empty()) {
        NS_LOG_WARN("failRecoverySec set but failLink is empty; recovery timer will be ignored");
    }
    if ((g_scheme == "sanr-tag" || g_scheme == "sanr-oracle") && g_csSize == 0) {
        NS_LOG_WARN("SANR scheme requested with csSize=0; forcing csSize=512 for semantic cache baseline.");
        g_csSize = 512;
    }

    NS_LOG_UNCOND("=== iroute-exp-baselines ===");
    NS_LOG_UNCOND("  scheme=" << g_scheme << " domains=" << g_domains
                  << " K=" << g_K << " tau=" << g_tau
                  << " M=" << g_M
                  << " topo=" << g_topo
                  << " simTime=" << g_simTime << "s");
    NS_LOG_UNCOND("  warmupSec=" << g_warmupSec
                  << " measureStartSec=" << g_measureStartSec
                  << " failurePolicy=" << g_failureTargetPolicy);
    if (g_topo == "rocketfuel") {
        NS_LOG_UNCOND("  topoFile=" << g_topoFile << " ingressNode=" << g_ingressNodeIndex);
    }
    if (g_scheme == "exact") {
        NS_LOG_UNCOND("  exactUseIndex=" << (g_exactUseIndex ? "true" : "false"));
    }

    // Load data
    BaselineSimulation sim(g_seed, g_domains);
    // Exact scheme only needs trace; others need centroids+content too
    sim.LoadData(g_centroidsFile, g_contentFile, g_traceFile);

    // Run selected scheme
    std::vector<QueryLog> logs;
    if (g_scheme == "iroute")  logs = sim.RunIRoute();
    else if (g_scheme == "exact")   logs = sim.RunExactMatch(g_indexFile);
    else if (g_scheme == "central") logs = sim.RunCentralized(
        g_dirMode, g_dirProcMs, g_dirProcJitterUs, g_dirTopK, g_dirFailTime,
        g_qrelsFile, g_contentFile, g_traceFile);
    else if (g_scheme == "flood")   logs = sim.RunFlood(g_floodResponder, g_floodThreshold);
    else if (g_scheme == "tag")     logs = sim.RunTag();
    else if (g_scheme == "sanr-tag") logs = sim.RunSanrTag();
    else if (g_scheme == "sanr-oracle") logs = sim.RunSanrOracle(g_qrelsFile, g_contentFile, g_traceFile);

    if ((g_scheme == "tag" || g_scheme == "sanr-tag") && sim.GetLastTagMissingRate() > 0.01) {
        NS_LOG_ERROR("query_to_tag missing rate too high: " << sim.GetLastTagMissingRate()
                     << " (>0.01). Breakdown: " << sim.GetLastTagFailureBreakdown());
        return 2;
    }
    if ((g_scheme == "sanr-tag" || g_scheme == "sanr-oracle") && g_sanrEnableSemantic) {
        if (sim.GetLastSanrSemanticHitRatio() <= 0.0) {
            NS_LOG_ERROR("SANR semantic_hit_ratio is zero; semantic cache gate/index may be disconnected.");
            return 3;
        }
        std::vector<double> missRtts;
        std::map<double, uint32_t> missFreq;
        for (const auto& q : logs) {
            if (q.is_measurable <= 0 || q.is_success <= 0) {
                continue;
            }
            if (q.cache_hit_semantic > 0 && q.sim_max + 1e-9 < g_sanrSimThresh) {
                NS_LOG_ERROR("SANR semantic hit has sim_max below threshold: " << q.sim_max
                             << " < " << g_sanrSimThresh << " qid=" << q.qid);
                return 3;
            }
            if (q.cache_hit_exact > 0 || q.cache_hit_semantic > 0) {
                continue;
            }
            if (q.rtt_total_ms > 0.0) {
                missRtts.push_back(q.rtt_total_ms);
                double key = std::round(q.rtt_total_ms * 1000.0) / 1000.0;
                missFreq[key]++;
            }
        }
        std::set<double> uniq;
        for (double v : missRtts) {
            uniq.insert(std::round(v * 1000.0) / 1000.0);
        }
        if (logs.size() >= 500 && uniq.size() < 50) {
            std::vector<std::pair<double, uint32_t>> fv(missFreq.begin(), missFreq.end());
            std::sort(fv.begin(), fv.end(), [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;
            });
            std::ostringstream top;
            for (size_t i = 0; i < std::min<size_t>(10, fv.size()); ++i) {
                if (i > 0) top << ";";
                top << std::fixed << std::setprecision(3) << fv[i].first << ":" << fv[i].second;
            }
            NS_LOG_ERROR("SANR cache-miss RTT unique values < 50 (" << uniq.size()
                         << "). top values: " << top.str());
            return 3;
        }
    }

    // Failure sanity updates g_failureEffective and must happen before summary.
    WriteRunFailureSanity(logs, g_resultDir);

    // Compute summary
    SummaryStats summary = BuildRunSummary(g_scheme, logs, g_simTime);
    summary.avgFibEntries = sim.GetLastFibSize();
    summary.avgLsdbEntries = sim.GetLastLsdbSize();
    summary.partitionDetected = g_partitionDetected;
    summary.failureEffective = g_failureEffective;

    // Write outputs
    WriteRunQueryLog(logs, g_resultDir);
    WriteRunSummary(summary, g_resultDir);
    WriteRunLatencySanity(logs, g_resultDir);
    WriteRunManifest(summary, g_resultDir);

    // Print summary
    NS_LOG_UNCOND("\n=== Summary ===");
    NS_LOG_UNCOND("  Scheme: " << g_scheme);
    NS_LOG_UNCOND("  Queries: " << summary.totalQueries
                  << " (measurable: " << summary.measurableQueries << ")");
    NS_LOG_UNCOND("  DomainAcc: " << std::fixed << std::setprecision(4) << summary.domainAcc);
    NS_LOG_UNCOND("  Recall@1:  " << summary.recall1);
    NS_LOG_UNCOND("  Recall@K(domain): " << summary.recallK);
    NS_LOG_UNCOND("  DocHit@1(singleton): " << summary.singletonRecall1
                  << " on " << summary.singletonQueries << " queries");
    NS_LOG_UNCOND("  Mean relevant set size: " << summary.meanRelSetSize);
    NS_LOG_UNCOND("  meanSSR:   " << summary.meanSSR);
    NS_LOG_UNCOND("  P50 RTT:   " << summary.p50_ms << " ms");
    NS_LOG_UNCOND("  P95 RTT:   " << summary.p95_ms << " ms");
    NS_LOG_UNCOND("  MeanHops:  " << summary.meanHops);
    NS_LOG_UNCOND("  UniqueHops:" << summary.uniqueHopsValues);
    NS_LOG_UNCOND("  Ctrl B/s:  " << summary.ctrlBytesPerSec);
    NS_LOG_UNCOND("  FIB size:  " << summary.avgFibEntries);
    NS_LOG_UNCOND("  LSDB size: " << summary.avgLsdbEntries);
    NS_LOG_UNCOND("  partition_detected: " << summary.partitionDetected
                  << " failure_effective: " << summary.failureEffective);
    if (g_scheme == "sanr-tag" || g_scheme == "sanr-oracle") {
        NS_LOG_UNCOND("  SANR exactHitRatio: " << sim.GetLastSanrExactHitRatio()
                      << " semanticHitRatio: " << sim.GetLastSanrSemanticHitRatio()
                      << " indexEntries: " << sim.GetLastSanrIndexEntries());
    }

    return 0;
}
