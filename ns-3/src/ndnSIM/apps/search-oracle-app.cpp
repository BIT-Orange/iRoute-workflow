/**
 * @file search-oracle-app.cpp
 * @brief Implementation of centralized search oracle.
 */

#include "search-oracle-app.hpp"
#include "extensions/iroute-tlv.hpp"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"

#include "helper/ndn-fib-helper.hpp"

#include <ndn-cxx/encoding/block-helpers.hpp>
#include <ndn-cxx/encoding/tlv.hpp>
#include <ndn-cxx/util/span.hpp>

namespace ns3 {
namespace ndn {

NS_LOG_COMPONENT_DEFINE("ndn.SearchOracleApp");
NS_OBJECT_ENSURE_REGISTERED(SearchOracleApp);

// =============================================================================
// TypeId Registration
// =============================================================================

TypeId
SearchOracleApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::SearchOracleApp")
        .SetGroupName("Ndn")
        .SetParent<App>()
        .AddConstructor<SearchOracleApp>()
        .AddAttribute("Prefix",
                      "Service prefix",
                      StringValue("/search/oracle"),
                      MakeNameAccessor(&SearchOracleApp::m_prefix),
                      MakeNameChecker())
        .AddAttribute("PayloadSize",
                      "Response payload size",
                      UintegerValue(150),
                      MakeUintegerAccessor(&SearchOracleApp::m_payloadSize),
                      MakeUintegerChecker<uint32_t>());

    return tid;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

SearchOracleApp::SearchOracleApp()
    : m_prefix("/search/oracle")
    , m_payloadSize(150)
    , m_dotProductCount(0)
    , m_queriesProcessed(0)
{
    NS_LOG_FUNCTION(this);
}

SearchOracleApp::~SearchOracleApp()
{
    NS_LOG_FUNCTION(this);
}

// =============================================================================
// Configuration
// =============================================================================

void
SearchOracleApp::AddContent(const GlobalContentEntry& entry)
{
    m_globalIndex.push_back(entry);
}

void
SearchOracleApp::LoadContent(const std::vector<GlobalContentEntry>& content)
{
    m_globalIndex = content;
    NS_LOG_INFO("SearchOracle loaded " << m_globalIndex.size() << " documents");
}

// =============================================================================
// Application Lifecycle
// =============================================================================

void
SearchOracleApp::StartApplication()
{
    NS_LOG_FUNCTION(this);
    App::StartApplication();

    // Register prefix
    FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);

    NS_LOG_INFO("SearchOracleApp started on node " << GetNode()->GetId()
                << " prefix=" << m_prefix
                << " indexSize=" << m_globalIndex.size());
}

void
SearchOracleApp::StopApplication()
{
    NS_LOG_FUNCTION(this);
    
    NS_LOG_UNCOND("=== SearchOracleApp Statistics ===");
    NS_LOG_UNCOND("Queries processed: " << m_queriesProcessed);
    NS_LOG_UNCOND("Total dot-products: " << m_dotProductCount);
    NS_LOG_UNCOND("Index size: " << m_globalIndex.size());
    
    App::StopApplication();
}

// =============================================================================
// Interest Handling
// =============================================================================

void
SearchOracleApp::OnInterest(shared_ptr<const Interest> interest)
{
    NS_LOG_FUNCTION(this << interest->getName());
    App::OnInterest(interest);

    // Check if this is for us
    if (!m_prefix.isPrefixOf(interest->getName())) {
        NS_LOG_DEBUG("Interest not for us: " << interest->getName());
        return;
    }

    ++m_queriesProcessed;

    // Parse query vector from ApplicationParameters
    iroute::SemanticVector queryVec;
    try {
        if (interest->hasApplicationParameters()) {
            const auto& params = interest->getApplicationParameters();
            // params is an ApplicationParameters Block (type 36)
            // The inner content is the wire-encoded SemanticVector (type 128)
            // Extract value bytes and construct a new Block with proper TLV parsing
            
            // Method: Create a Block from the raw value bytes, which include the full TLV
            auto valueSpan = nonstd::span<const uint8_t>(params.value(), params.value_size());
            ::ndn::Block vectorBlock(valueSpan);
            queryVec.wireDecode(vectorBlock);
            
            if (!queryVec.isNormalized()) {
                queryVec.normalize();
            }
            NS_LOG_DEBUG("Parsed query vector, dim=" << queryVec.getDimension());
        } else {
            NS_LOG_WARN("No ApplicationParameters in search Interest");
            return;
        }
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Failed to parse query vector: " << e.what());
        return;
    }

    // Perform global search
    NS_LOG_DEBUG("Starting global search...");
    auto [found, canonicalName, domainId, confidence] = GlobalSearch(queryVec);
    NS_LOG_DEBUG("Search result: found=" << found << " canonical=" << canonicalName 
                 << " domainId=" << domainId << " confidence=" << confidence);

    NS_LOG_DEBUG("Building response Data...");
    // Build response Data
    auto data = std::make_shared<Data>(interest->getName());
    data->setFreshnessPeriod(ndn::time::seconds(10));

    NS_LOG_DEBUG("Encoding response using iroute::tlv::encodeDiscoveryReply...");

    // Use the same encoding format as IRouteApp for consistency
    Name cName(canonicalName);
    auto contentBlock = iroute::tlv::encodeDiscoveryReply(found, cName, confidence, 1);
    data->setContent(contentBlock);
    NS_LOG_DEBUG("Content set");

    // Sign and send (following ndn-producer.cpp pattern)
    NS_LOG_DEBUG("Signing data...");
    SignatureInfo signatureInfo(static_cast<::ndn::tlv::SignatureTypeValue>(255));
    data->setSignatureInfo(signatureInfo);
    
    ::ndn::EncodingEstimator estimator;
    ::ndn::EncodingBuffer sigEncoder(estimator.appendVarNumber(0), 0);
    sigEncoder.appendVarNumber(0);
    data->setSignatureValue(sigEncoder.getBuffer());
    NS_LOG_DEBUG("Signed");
    
    NS_LOG_DEBUG("Wire encoding...");
    data->wireEncode();
    NS_LOG_DEBUG("Wire encoded");

    m_transmittedDatas(data, this, m_face);
    m_appLink->onReceiveData(*data);

    NS_LOG_DEBUG("SearchOracle replied: found=" << found 
                 << " canonicalName=" << canonicalName 
                 << " domainId=" << domainId
                 << " confidence=" << confidence);
}

// =============================================================================
// Global Search
// =============================================================================

std::tuple<bool, std::string, uint32_t, double>
SearchOracleApp::GlobalSearch(const iroute::SemanticVector& query)
{
    if (m_globalIndex.empty()) {
        return {false, "", 0, 0.0};
    }

    double bestScore = -1.0;
    const GlobalContentEntry* bestEntry = nullptr;

    for (const auto& entry : m_globalIndex) {
        double score = query.dot(entry.vector);
        ++m_dotProductCount;

        if (score > bestScore) {
            bestScore = score;
            bestEntry = &entry;
        }
    }

    if (bestEntry == nullptr) {
        return {false, "", 0, 0.0};
    }

    return {true, bestEntry->canonicalName, bestEntry->domainId, bestScore};
}

} // namespace ndn
} // namespace ns3
