#ifndef IROUTE_EXP_RESULTS_HPP
#define IROUTE_EXP_RESULTS_HPP

#include "ns3/core-module.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace irouteexp {

inline std::string
TrimCopy(const std::string& s)
{
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
        --last;
    }
    return s.substr(first, last - first);
}

inline std::string
CsvEscape(const std::string& s)
{
    if (s.find_first_of(",\"\n") == std::string::npos) {
        return s;
    }
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        }
        else {
            out.push_back(c);
        }
    }
    out += '"';
    return out;
}

inline std::string
JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

inline std::string
CurrentUtcIso8601()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm utc = *std::gmtime(&tt);
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return "";
    }
    return buffer;
}

inline std::vector<std::string>
SplitBySemicolon(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ';')) {
        token = TrimCopy(token);
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

inline std::string
JoinStr(const std::vector<std::string>& values, char sep = ';')
{
    std::string result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            result.push_back(sep);
        }
        result += values[i];
    }
    return result;
}

inline bool
CheckDomainHit(const std::string& pred, const std::vector<std::string>& gtDomains)
{
    std::string predNorm = TrimCopy(pred);
    if (predNorm.empty()) {
        return false;
    }
    for (const auto& td : gtDomains) {
        std::string tdNorm = TrimCopy(td);
        if (tdNorm.empty()) {
            continue;
        }
        if (predNorm == tdNorm) {
            return true;
        }
        if (predNorm == ("/domain" + tdNorm)) {
            return true;
        }
        if (tdNorm == ("/domain" + predNorm)) {
            return true;
        }
    }
    return false;
}

struct FailureSanityRecord {
    std::string scenario = "none";
    std::string target = "";
    double eventTime = -1.0;
    int scheduled = 0;
    int applied = 0;
    int beforeConnected = -1;
    int afterConnected = -1;
    int affectedApps = 0;
    double preHit = -1.0;
    double postHit = -1.0;
    double preSuccess = -1.0;
    double postSuccess = -1.0;
    double preDomainHit = -1.0;
    double postDomainHit = -1.0;
    double preExactHit = -1.0;
    double postExactHit = -1.0;
    double preTimeoutRate = -1.0;
    double postTimeoutRate = -1.0;
    double preRetransAvg = -1.0;
    double postRetransAvg = -1.0;
    uint32_t preCount = 0;
    uint32_t postCount = 0;
    std::string effectiveReasons = "";
    std::string notes = "";
};

struct QueryLog {
    uint32_t qid = 0;
    std::string scheme;
    std::string query_text;
    std::string gt_domain;
    std::string gt_doc;
    std::string pred_domain;
    std::string pred_doc;
    std::string topk_domains;
    int hit_exact = 0;
    int domain_hit = 0;
    double ssr_score = 0.0;
    double t_send_disc = 0.0;
    double t_recv_disc_reply = 0.0;
    double t_send_fetch = 0.0;
    double t_recv_data = 0.0;
    double rtt_total_ms = 0.0;
    int hops_disc = -1;
    int hops_fetch = -1;
    int hops_total = -1;
    uint32_t n_interest_sent = 0;
    uint32_t n_data_recv = 0;
    uint64_t bytes_ctrl_tx = 0;
    uint64_t bytes_ctrl_rx = 0;
    uint64_t bytes_data_tx = 0;
    uint64_t bytes_data_rx = 0;
    uint32_t timeouts = 0;
    uint32_t retransmissions = 0;
    int is_measurable = 0;
    int is_success = 0;
    int is_timeout = 0;
    std::string timeout_reason;
    std::string failure_scenario;
    std::string failure_target;
    std::string topology;
    double disc_ms = 0.0;
    double fetch_ms = 0.0;
    uint64_t t_send_disc_ns = 0;
    uint64_t t_recv_disc_reply_ns = 0;
    uint64_t t_send_fetch_ns = 0;
    uint64_t t_recv_data_ns = 0;
    int cache_hit_exact = 0;
    int cache_hit_semantic = 0;
    double sim_max = 0.0;
    std::string matched_cache_name;
    std::string sanr_cluster_id;

    static std::string
    csvHeader()
    {
        return "qid,scheme,query_text,gt_domain,gt_doc,"
               "pred_domain,pred_doc,topk_domains,hit_exact,domain_hit,"
               "ssr_score,t_send_disc,t_recv_disc_reply,t_send_fetch,t_recv_data,"
               "rtt_total_ms,hops_disc,hops_fetch,hops_total,"
               "n_interest_sent,n_data_recv,"
               "bytes_ctrl_tx,bytes_ctrl_rx,bytes_data_tx,bytes_data_rx,"
               "timeouts,retransmissions,"
               "is_measurable,is_success,is_timeout,timeout_reason,failure_scenario,failure_target,"
               "topology,disc_ms,fetch_ms,"
               "t_send_disc_ns,t_recv_disc_reply_ns,t_send_fetch_ns,t_recv_data_ns,"
               "cache_hit_exact,cache_hit_semantic,sim_max,matched_cache_name,sanr_cluster_id";
    }

    std::string
    toCsvLine() const
    {
        std::ostringstream oss;
        oss << qid << "," << scheme << "," << CsvEscape(query_text) << "," << CsvEscape(gt_domain) << ","
            << CsvEscape(gt_doc) << "," << CsvEscape(pred_domain) << "," << CsvEscape(pred_doc) << ","
            << CsvEscape(topk_domains) << "," << hit_exact << "," << domain_hit << "," << std::fixed
            << std::setprecision(6) << ssr_score << "," << std::setprecision(6) << t_send_disc << ","
            << t_recv_disc_reply << "," << t_send_fetch << "," << t_recv_data << "," << std::setprecision(6)
            << rtt_total_ms << "," << hops_disc << "," << hops_fetch << "," << hops_total << ","
            << n_interest_sent << "," << n_data_recv << "," << bytes_ctrl_tx << "," << bytes_ctrl_rx << ","
            << bytes_data_tx << "," << bytes_data_rx << "," << timeouts << "," << retransmissions << ","
            << is_measurable << "," << is_success << "," << is_timeout << "," << CsvEscape(timeout_reason) << ","
            << CsvEscape(failure_scenario) << "," << CsvEscape(failure_target) << "," << CsvEscape(topology)
            << "," << std::setprecision(6) << disc_ms << "," << fetch_ms << "," << t_send_disc_ns << ","
            << t_recv_disc_reply_ns << "," << t_send_fetch_ns << "," << t_recv_data_ns << ","
            << cache_hit_exact << "," << cache_hit_semantic << "," << std::setprecision(6) << sim_max << ","
            << CsvEscape(matched_cache_name) << "," << CsvEscape(sanr_cluster_id);
        return oss.str();
    }
};

struct SummaryStats {
    std::string scheme;
    uint32_t totalQueries = 0;
    uint32_t measurableQueries = 0;
    uint32_t domainCorrect = 0;
    uint32_t docCorrect = 0;
    double meanSSR = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double meanHops = 0.0;
    double ctrlBytesPerSec = 0.0;
    double ctrlPktsPerSec = 0.0;
    uint32_t avgFibEntries = 0;
    uint32_t avgLsdbEntries = 0;
    uint32_t uniqueHopsValues = 0;
    int partitionDetected = 0;
    int failureEffective = 0;
    double domainAcc = 0.0;
    double recall1 = 0.0;
    double recallK = 0.0;
    double singletonRecall1 = 0.0;
    double meanRelSetSize = 0.0;
    uint32_t singletonQueries = 0;
    double ssrMean = 0.0;
    double ssrP50 = 0.0;
    double ssrP95 = 0.0;
    double cacheHitExactRatio = 0.0;
    double cacheHitSemanticRatio = 0.0;
    double semanticHitRatio = 0.0;

    static std::string
    csvHeader()
    {
        return "scheme,totalQueries,measurableQueries,"
               "DomainAcc,Recall_at_1,Recall_at_k,DocHit_at_1_singleton,mean_relset_size,singleton_queries,meanSSR,"
               "P50_ms,P95_ms,mean_hops,"
               "ctrl_bytes_per_sec,ctrl_pkts_per_sec,"
               "avg_FIB_entries,avg_LSDB_entries,"
               "unique_hops_values,partition_detected,failure_effective,"
               "ssr_mean,ssr_p50,ssr_p95,cache_hit_exact_ratio,cache_hit_semantic_ratio,semantic_hit_ratio";
    }

    std::string
    toCsvLine() const
    {
        std::ostringstream oss;
        oss << scheme << "," << totalQueries << "," << measurableQueries << "," << std::fixed
            << std::setprecision(4) << domainAcc << "," << recall1 << "," << recallK << ","
            << singletonRecall1 << "," << meanRelSetSize << "," << singletonQueries << "," << std::setprecision(6)
            << meanSSR << "," << std::setprecision(4) << p50_ms << "," << p95_ms << ","
            << std::setprecision(2) << meanHops << "," << ctrlBytesPerSec << "," << ctrlPktsPerSec << ","
            << avgFibEntries << "," << avgLsdbEntries << "," << uniqueHopsValues << "," << partitionDetected << ","
            << failureEffective << "," << std::setprecision(6) << ssrMean << "," << ssrP50 << "," << ssrP95 << ","
            << cacheHitExactRatio << "," << cacheHitSemanticRatio << "," << semanticHitRatio;
        return oss.str();
    }
};

struct SummaryConfig {
    uint32_t topK = 5;
    double measureStartSec = 0.0;
    bool cdfSuccessOnly = true;
};

struct ManifestConfig {
    std::string scheme;
    uint32_t seed = 0;
    std::string topology;
    uint32_t domains = 0;
    double simTime = 0.0;
    double frequency = 0.0;
    double warmupSec = 0.0;
    double measureStartSec = 0.0;
    uint32_t csSize = 0;
    uint32_t dataFreshnessMs = 0;
    double sanrSimThresh = 0.0;
    double sanrMsrrTopPct = 0.0;
    double sanrCmltSec = 0.0;
    double sanrSlotSec = 0.0;
    uint32_t sanrCcnK = 0;
    uint32_t sanrTopL = 0;
    std::string cacheMode = "disabled";
    std::string runMode = "direct_driver";
    std::string seedProvenance = "native";
};

inline bool
IsInMeasurementWindow(const QueryLog& q, double measureStartSec)
{
    return q.t_send_disc >= (measureStartSec * 1000.0);
}

inline bool
TopKDomainHit(const QueryLog& q, uint32_t k)
{
    auto gt = SplitBySemicolon(q.gt_domain);
    if (gt.empty()) {
        return false;
    }
    auto candidates = SplitBySemicolon(q.topk_domains);
    if (candidates.empty()) {
        return q.domain_hit == 1;
    }
    uint32_t checked = 0;
    for (const auto& cand : candidates) {
        if (checked >= k) {
            break;
        }
        std::string domain = cand;
        auto eqPos = domain.find('=');
        if (eqPos != std::string::npos) {
            domain = domain.substr(0, eqPos);
        }
        if (CheckDomainHit(domain, gt)) {
            return true;
        }
        ++checked;
    }
    return false;
}

inline SummaryStats
ComputeSummary(const std::string& scheme, const std::vector<QueryLog>& logs, double simTimeSec, const SummaryConfig& config)
{
    SummaryStats s;
    s.scheme = scheme;
    s.totalQueries = static_cast<uint32_t>(logs.size());

    std::vector<double> rtts;
    std::vector<double> hops;
    std::set<int> uniqueHops;
    uint64_t totalCtrlBytes = 0;
    uint64_t totalInterests = 0;
    double sumSSR = 0.0;
    std::vector<double> ssrVals;
    uint32_t domainRecallKCorrect = 0;
    uint32_t singletonDocCorrect = 0;
    double totalRelSetSize = 0.0;
    uint32_t relSetQueries = 0;
    uint32_t measurableForCache = 0;
    uint32_t cacheExactCount = 0;
    uint32_t cacheSemanticCount = 0;

    for (const auto& q : logs) {
        bool measurable = (!q.gt_domain.empty()) && IsInMeasurementWindow(q, config.measureStartSec);
        if (measurable) {
            s.measurableQueries++;
            if (q.domain_hit) {
                s.domainCorrect++;
            }
            if (q.hit_exact) {
                s.docCorrect++;
            }
            if (TopKDomainHit(q, config.topK)) {
                domainRecallKCorrect++;
            }
            ssrVals.push_back(q.ssr_score);
            measurableForCache++;
            if (q.cache_hit_exact > 0) {
                cacheExactCount++;
            }
            if (q.cache_hit_semantic > 0) {
                cacheSemanticCount++;
            }

            auto relDocs = SplitBySemicolon(q.gt_doc);
            if (!relDocs.empty()) {
                relSetQueries++;
                totalRelSetSize += relDocs.size();
                if (relDocs.size() == 1) {
                    s.singletonQueries++;
                    if (q.hit_exact) {
                        singletonDocCorrect++;
                    }
                }
            }
        }
        sumSSR += q.ssr_score;
        if (q.rtt_total_ms > 0 && IsInMeasurementWindow(q, config.measureStartSec)) {
            if (!config.cdfSuccessOnly || q.is_success) {
                rtts.push_back(q.rtt_total_ms);
            }
        }

        int hopTotal = 0;
        bool hasHop = false;
        if (q.hops_disc >= 0) {
            hopTotal += q.hops_disc;
            hasHop = true;
        }
        if (q.hops_fetch >= 0) {
            hopTotal += q.hops_fetch;
            hasHop = true;
        }
        if (hasHop && IsInMeasurementWindow(q, config.measureStartSec)) {
            hops.push_back(static_cast<double>(hopTotal));
            uniqueHops.insert(hopTotal);
        }

        if (IsInMeasurementWindow(q, config.measureStartSec)) {
            totalCtrlBytes += q.bytes_ctrl_tx + q.bytes_ctrl_rx;
            totalInterests += q.n_interest_sent;
        }
    }

    s.domainAcc = s.measurableQueries > 0 ? static_cast<double>(s.domainCorrect) / s.measurableQueries : 0.0;
    s.recall1 = s.measurableQueries > 0 ? static_cast<double>(s.docCorrect) / s.measurableQueries : 0.0;
    s.recallK = s.measurableQueries > 0 ? static_cast<double>(domainRecallKCorrect) / s.measurableQueries : 0.0;
    s.singletonRecall1 =
        s.singletonQueries > 0 ? static_cast<double>(singletonDocCorrect) / s.singletonQueries : 0.0;
    s.meanRelSetSize = relSetQueries > 0 ? totalRelSetSize / relSetQueries : 0.0;
    s.meanSSR = !logs.empty() ? sumSSR / static_cast<double>(logs.size()) : 0.0;
    if (!ssrVals.empty()) {
        std::sort(ssrVals.begin(), ssrVals.end());
        s.ssrMean = std::accumulate(ssrVals.begin(), ssrVals.end(), 0.0) / static_cast<double>(ssrVals.size());
        s.ssrP50 = ssrVals[ssrVals.size() / 2];
        s.ssrP95 = ssrVals[static_cast<size_t>(
            std::min<double>(ssrVals.size() - 1, std::floor(ssrVals.size() * 0.95)))];
    }
    if (measurableForCache > 0) {
        s.cacheHitExactRatio = static_cast<double>(cacheExactCount) / measurableForCache;
        s.cacheHitSemanticRatio = static_cast<double>(cacheSemanticCount) / measurableForCache;
        s.semanticHitRatio = s.cacheHitSemanticRatio;
    }

    if (!rtts.empty()) {
        std::sort(rtts.begin(), rtts.end());
        s.p50_ms = rtts[rtts.size() / 2];
        s.p95_ms = rtts[static_cast<size_t>(rtts.size() * 0.95)];
    }

    if (!hops.empty()) {
        s.meanHops = std::accumulate(hops.begin(), hops.end(), 0.0) / hops.size();
    }
    s.uniqueHopsValues = static_cast<uint32_t>(uniqueHops.size());

    double measureDuration = std::max(1.0, simTimeSec - config.measureStartSec);
    s.ctrlBytesPerSec = totalCtrlBytes / measureDuration;
    s.ctrlPktsPerSec = totalInterests / measureDuration;

    return s;
}

inline void
WriteQueryLog(const std::vector<QueryLog>& logs, const std::string& dir)
{
    std::string path = dir + "/query_log.csv";
    std::ofstream f(path);
    f << QueryLog::csvHeader() << "\n";
    for (const auto& q : logs) {
        f << q.toCsvLine() << "\n";
    }
    NS_LOG_UNCOND("Wrote " << path << " (" << logs.size() << " rows)");
}

inline void
WriteSummary(const SummaryStats& s, const std::string& dir)
{
    std::string path = dir + "/summary.csv";
    bool isNew = !std::filesystem::exists(path);
    std::ofstream f(path, std::ios::app);
    if (isNew) {
        f << SummaryStats::csvHeader() << "\n";
    }
    f << s.toCsvLine() << "\n";
    NS_LOG_UNCOND("Wrote " << path);
}

inline void
WriteLatencySanity(const std::vector<QueryLog>& logs, const std::string& dir, const std::string& scheme, const SummaryConfig& config)
{
    std::string path = dir + "/latency_sanity.csv";
    std::ofstream f(path);
    f << "scheme,total_queries,measurement_queries,success_queries,timeout_queries,timeout_rate,"
         "unique_rtt_values,p50_ms,p95_ms,top_rtt_values,"
         "cache_miss_queries,cache_miss_unique_rtt_values,cache_miss_p50_ms,cache_miss_p95_ms\n";

    uint32_t measurement = 0;
    uint32_t success = 0;
    uint32_t timeout = 0;
    uint32_t nonSuccess = 0;
    std::vector<double> successRtt;
    std::vector<double> cacheMissRtt;
    std::map<double, uint32_t> roundedFreq;

    for (const auto& q : logs) {
        if (!IsInMeasurementWindow(q, config.measureStartSec)) {
            continue;
        }
        if (q.is_measurable <= 0) {
            continue;
        }
        ++measurement;
        if (q.is_success) {
            ++success;
            if (q.rtt_total_ms > 0.0) {
                successRtt.push_back(q.rtt_total_ms);
                double bucket = std::round(q.rtt_total_ms * 1000.0) / 1000.0;
                roundedFreq[bucket]++;
                if (q.cache_hit_exact == 0 && q.cache_hit_semantic == 0) {
                    cacheMissRtt.push_back(q.rtt_total_ms);
                }
            }
        }
        if (q.is_timeout) {
            ++timeout;
        }
        if (!q.is_success) {
            ++nonSuccess;
        }
    }

    std::sort(successRtt.begin(), successRtt.end());
    double p50 = 0.0;
    double p95 = 0.0;
    if (!successRtt.empty()) {
        p50 = successRtt[successRtt.size() / 2];
        p95 = successRtt[static_cast<size_t>(
            std::min<double>(successRtt.size() - 1, std::floor(successRtt.size() * 0.95)))];
    }
    std::sort(cacheMissRtt.begin(), cacheMissRtt.end());
    double cmP50 = 0.0;
    double cmP95 = 0.0;
    if (!cacheMissRtt.empty()) {
        cmP50 = cacheMissRtt[cacheMissRtt.size() / 2];
        cmP95 = cacheMissRtt[static_cast<size_t>(
            std::min<double>(cacheMissRtt.size() - 1, std::floor(cacheMissRtt.size() * 0.95)))];
    }
    std::set<double> cmUnique;
    for (double v : cacheMissRtt) {
        cmUnique.insert(std::round(v * 1000.0) / 1000.0);
    }

    std::vector<std::pair<double, uint32_t>> freqVec(roundedFreq.begin(), roundedFreq.end());
    std::sort(freqVec.begin(), freqVec.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });
    std::ostringstream topVals;
    for (size_t i = 0; i < std::min<size_t>(10, freqVec.size()); ++i) {
        if (i > 0) {
            topVals << ";";
        }
        topVals << std::fixed << std::setprecision(3) << freqVec[i].first << ":" << freqVec[i].second;
    }

    double timeoutRate = measurement > 0 ? static_cast<double>(nonSuccess) / measurement : 0.0;
    f << scheme << "," << logs.size() << "," << measurement << "," << success << "," << timeout << ","
      << std::fixed << std::setprecision(6) << timeoutRate << "," << roundedFreq.size() << ","
      << std::setprecision(4) << p50 << "," << p95 << "," << CsvEscape(topVals.str()) << ","
      << cacheMissRtt.size() << "," << cmUnique.size() << "," << cmP50 << "," << cmP95 << "\n";

    NS_LOG_UNCOND("Wrote " << path);
}

inline void
WriteManifest(const SummaryStats& s, const std::string& dir, const ManifestConfig& config)
{
    std::string path = dir + "/manifest.json";
    std::ofstream f(path);
    std::string runId = std::filesystem::path(dir).filename().string();
    f << "{\n";
    f << "  \"generated_at_utc\": \"" << JsonEscape(CurrentUtcIso8601()) << "\",\n";
    f << "  \"run_id\": \"" << JsonEscape(runId) << "\",\n";
    f << "  \"run_mode\": \"" << JsonEscape(config.runMode) << "\",\n";
    f << "  \"cache_mode\": \"" << JsonEscape(config.cacheMode) << "\",\n";
    f << "  \"seed_provenance\": \"" << JsonEscape(config.seedProvenance) << "\",\n";
    f << "  \"scheme\": \"" << JsonEscape(config.scheme) << "\",\n";
    f << "  \"seed\": " << config.seed << ",\n";
    f << "  \"topology\": \"" << JsonEscape(config.topology) << "\",\n";
    f << "  \"domains\": " << config.domains << ",\n";
    f << "  \"sim_time\": " << config.simTime << ",\n";
    f << "  \"frequency\": " << config.frequency << ",\n";
    f << "  \"warmup_sec\": " << config.warmupSec << ",\n";
    f << "  \"measure_start_sec\": " << config.measureStartSec << ",\n";
    f << "  \"cs_size\": " << config.csSize << ",\n";
    f << "  \"data_freshness_ms\": " << config.dataFreshnessMs << ",\n";
    f << "  \"sanr\": {\n";
    f << "    \"sim_thresh\": " << config.sanrSimThresh << ",\n";
    f << "    \"msrr_top_pct\": " << config.sanrMsrrTopPct << ",\n";
    f << "    \"cmlt_s\": " << config.sanrCmltSec << ",\n";
    f << "    \"slot_s\": " << config.sanrSlotSec << ",\n";
    f << "    \"ccn_k\": " << config.sanrCcnK << ",\n";
    f << "    \"top_l\": " << config.sanrTopL << "\n";
    f << "  },\n";
    f << "  \"summary\": {\n";
    f << "    \"total_queries\": " << s.totalQueries << ",\n";
    f << "    \"measurable_queries\": " << s.measurableQueries << ",\n";
    f << "    \"domain_acc\": " << s.domainAcc << ",\n";
    f << "    \"recall_at_1\": " << s.recall1 << ",\n";
    f << "    \"p50_ms\": " << s.p50_ms << ",\n";
    f << "    \"p95_ms\": " << s.p95_ms << ",\n";
    f << "    \"cache_hit_exact_ratio\": " << s.cacheHitExactRatio << ",\n";
    f << "    \"cache_hit_semantic_ratio\": " << s.cacheHitSemanticRatio << "\n";
    f << "  }\n";
    f << "}\n";
    NS_LOG_UNCOND("Wrote " << path);
}

inline void
WriteFailureSanity(const std::vector<QueryLog>& logs,
                   const std::string& dir,
                   FailureSanityRecord* failureSanity,
                   int* failureEffective,
                   double failRecoverySec)
{
    std::string path = dir + "/failure_sanity.csv";
    std::ofstream f(path);
    f << "scenario,target,event_time,scheduled,applied,before_connected,after_connected,"
         "affected_apps,pre_hit,post_hit,pre_success,post_success,pre_domain_hit,post_domain_hit,"
         "pre_exact_hit,post_exact_hit,pre_timeout_rate,post_timeout_rate,pre_retrans_avg,"
         "post_retrans_avg,pre_rtt_ms,post_rtt_ms,pre_count,post_count,effective_reasons,notes\n";

    double windowSec = 20.0;
    if (failRecoverySec > 0.0) {
        windowSec = std::max(windowSec, failRecoverySec + 10.0);
    }
    double preSuccessSum = 0.0;
    double postSuccessSum = 0.0;
    double preDomainSum = 0.0;
    double postDomainSum = 0.0;
    double preExactSum = 0.0;
    double postExactSum = 0.0;
    double preTimeoutSum = 0.0;
    double postTimeoutSum = 0.0;
    double preRetransSum = 0.0;
    double postRetransSum = 0.0;
    double preRttSum = 0.0;
    double postRttSum = 0.0;
    uint32_t preRttCnt = 0;
    uint32_t postRttCnt = 0;
    uint32_t preCnt = 0;
    uint32_t postCnt = 0;
    if (failureSanity->eventTime > 0.0) {
        for (const auto& q : logs) {
            if (q.is_measurable <= 0) {
                continue;
            }
            double tSec = q.t_send_disc / 1000.0;
            if (tSec >= failureSanity->eventTime - windowSec && tSec < failureSanity->eventTime) {
                preSuccessSum += static_cast<double>(q.is_success);
                preDomainSum += static_cast<double>(q.domain_hit);
                preExactSum += static_cast<double>(q.hit_exact);
                preTimeoutSum += static_cast<double>(q.is_timeout);
                preRetransSum += static_cast<double>(q.retransmissions);
                if (q.rtt_total_ms > 0.0) {
                    preRttSum += q.rtt_total_ms;
                    ++preRttCnt;
                }
                preCnt++;
            }
            else if (tSec >= failureSanity->eventTime && tSec < failureSanity->eventTime + windowSec) {
                postSuccessSum += static_cast<double>(q.is_success);
                postDomainSum += static_cast<double>(q.domain_hit);
                postExactSum += static_cast<double>(q.hit_exact);
                postTimeoutSum += static_cast<double>(q.is_timeout);
                postRetransSum += static_cast<double>(q.retransmissions);
                if (q.rtt_total_ms > 0.0) {
                    postRttSum += q.rtt_total_ms;
                    ++postRttCnt;
                }
                postCnt++;
            }
        }
    }
    failureSanity->preCount = preCnt;
    failureSanity->postCount = postCnt;
    failureSanity->preSuccess = preCnt > 0 ? preSuccessSum / preCnt : -1.0;
    failureSanity->postSuccess = postCnt > 0 ? postSuccessSum / postCnt : -1.0;
    failureSanity->preDomainHit = preCnt > 0 ? preDomainSum / preCnt : -1.0;
    failureSanity->postDomainHit = postCnt > 0 ? postDomainSum / postCnt : -1.0;
    failureSanity->preExactHit = preCnt > 0 ? preExactSum / preCnt : -1.0;
    failureSanity->postExactHit = postCnt > 0 ? postExactSum / postCnt : -1.0;
    failureSanity->preTimeoutRate = preCnt > 0 ? preTimeoutSum / preCnt : -1.0;
    failureSanity->postTimeoutRate = postCnt > 0 ? postTimeoutSum / postCnt : -1.0;
    failureSanity->preRetransAvg = preCnt > 0 ? preRetransSum / preCnt : -1.0;
    failureSanity->postRetransAvg = postCnt > 0 ? postRetransSum / postCnt : -1.0;
    // Fig. 5 recovery curves use domain-hit, so pre_hit/post_hit track the same primary metric.
    failureSanity->preHit = failureSanity->preDomainHit;
    failureSanity->postHit = failureSanity->postDomainHit;
    double preRtt = preRttCnt > 0 ? preRttSum / preRttCnt : -1.0;
    double postRtt = postRttCnt > 0 ? postRttSum / postRttCnt : -1.0;
    failureSanity->effectiveReasons.clear();
    if (preCnt > 0 && postCnt > 0) {
        std::vector<std::string> reasons;
        double successDrop = failureSanity->preSuccess - failureSanity->postSuccess;
        double domainDrop = failureSanity->preDomainHit - failureSanity->postDomainHit;
        double exactDrop = failureSanity->preExactHit - failureSanity->postExactHit;
        double timeoutIncrease = failureSanity->postTimeoutRate - failureSanity->preTimeoutRate;
        double retransIncrease = failureSanity->postRetransAvg - failureSanity->preRetransAvg;
        bool latencyChanged = false;
        if (preRtt > 0.0 && postRtt > 0.0) {
            latencyChanged = std::fabs(postRtt - preRtt) / preRtt > 0.05;
        }
        if (domainDrop > 0.05) {
            reasons.push_back("domain_hit_drop");
        }
        if (successDrop > 0.05) {
            reasons.push_back("success_drop");
        }
        if (exactDrop > 0.05) {
            reasons.push_back("exact_hit_drop");
        }
        if (timeoutIncrease > 0.02) {
            reasons.push_back("timeout_increase");
        }
        if (retransIncrease > 0.25) {
            reasons.push_back("retrans_increase");
        }
        if (latencyChanged) {
            reasons.push_back("rtt_shift");
        }
        *failureEffective = reasons.empty() ? 0 : 1;
        failureSanity->effectiveReasons = JoinStr(reasons);
    }
    else {
        *failureEffective = 0;
    }

    f << CsvEscape(failureSanity->scenario) << "," << CsvEscape(failureSanity->target) << "," << std::fixed
      << std::setprecision(3) << failureSanity->eventTime << "," << failureSanity->scheduled << ","
      << failureSanity->applied << "," << failureSanity->beforeConnected << "," << failureSanity->afterConnected
      << "," << failureSanity->affectedApps << "," << std::setprecision(6) << failureSanity->preHit << ","
      << failureSanity->postHit << "," << failureSanity->preSuccess << "," << failureSanity->postSuccess << ","
      << failureSanity->preDomainHit << "," << failureSanity->postDomainHit << ","
      << failureSanity->preExactHit << "," << failureSanity->postExactHit << ","
      << failureSanity->preTimeoutRate << "," << failureSanity->postTimeoutRate << ","
      << failureSanity->preRetransAvg << "," << failureSanity->postRetransAvg << "," << preRtt << ","
      << postRtt << "," << failureSanity->preCount << "," << failureSanity->postCount << ","
      << CsvEscape(failureSanity->effectiveReasons) << ","
      << CsvEscape(failureSanity->notes.empty() ? "metric=domain_hit"
                                               : failureSanity->notes + ";metric=domain_hit")
      << "\n";
    NS_LOG_UNCOND("Wrote " << path);
}

} // namespace irouteexp

#endif // IROUTE_EXP_RESULTS_HPP
