/**
 * @file exact-ndn-producer.hpp
 * @brief Exact-NDN syntax baseline producer (header-only).
 *
 * Loads a dictionary from index_exact.csv and responds to Interest names
 * /exact/<type>/<tokenized_query>/<qid> with matching doc info.
 */

#pragma once

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

namespace ns3 {
namespace ndn {

/**
 * @class ExactNdnProducer
 * @brief Producer that does exact dictionary lookup for tokenized queries.
 */
class ExactNdnProducer : public App
{
public:
    static TypeId GetTypeId();

    ExactNdnProducer() = default;
    virtual ~ExactNdnProducer() = default;

    /**
     * @brief Load dictionary from CSV file.
     * Format: tokenized_query,type,doc_id,canonical_name,domain_id
     */
    void LoadDictionary(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[ExactNdnProducer] Cannot open: " << filename << std::endl;
            return;
        }

        std::string line;
        std::getline(file, line);  // Skip header

        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok, type, docId, canonical, domId;
            if (std::getline(ss, tok, ',') &&
                std::getline(ss, type, ',') &&
                std::getline(ss, docId, ',') &&
                std::getline(ss, canonical, ',') &&
                std::getline(ss, domId)) {
                DictEntry e;
                e.docId = docId;
                e.canonicalName = canonical;
                e.domainId = domId;
                e.type = type;
                m_dictionary[tok] = e;
            }
        }
        std::cout << "[ExactNdnProducer] Loaded " << m_dictionary.size()
                  << " entries from " << filename << std::endl;
    }

    uint32_t GetHits() const { return m_hits; }
    uint32_t GetMisses() const { return m_misses; }

protected:
    virtual void StartApplication() override {
        App::StartApplication();
        // Register prefix /exact
        FibHelper::AddRoute(GetNode(), "/exact", m_face, 0);
    }

    virtual void StopApplication() override {
        App::StopApplication();
    }

    virtual void OnInterest(shared_ptr<const Interest> interest) override {
        App::OnInterest(interest);

        const auto& name = interest->getName();
        // Expected: /exact/<type>/<tokenized_query>/<qid>
        if (name.size() < 4) {
            m_misses++;
            return;  // Malformed — don't reply (timeout)
        }

        // Extract components
        // name[0] = "exact"
        // name[1] = type
        // name[2] = tokenized_query
        // name[3] = qid (number)
        std::string tokenized;
        try {
            tokenized = name.at(2).toUri();
            // URI-decode: %XX sequences
            // ndn::Name::Component stores it decoded, use toUri() then decode
            // Actually, the component value IS the string
            // Use a simpler approach: get the raw value
            const auto& comp = name.at(2);
            tokenized = std::string(reinterpret_cast<const char*>(comp.value()),
                                     comp.value_size());
        } catch (...) {
            m_misses++;
            return;
        }

        // Lookup in dictionary
        auto it = m_dictionary.find(tokenized);
        if (it == m_dictionary.end()) {
            m_misses++;
            // Don't reply — Interest will timeout at consumer
            return;
        }

        m_hits++;
        const auto& entry = it->second;

        // Build payload: "doc_id\tcanonical_name\tdomain_id"
        std::string payload = entry.docId + "\t" + entry.canonicalName + "\t" + entry.domainId;

        // Create Data
        auto data = std::make_shared<::ndn::Data>(interest->getName());
        data->setFreshnessPeriod(::ndn::time::seconds(10));
        data->setContent(reinterpret_cast<const uint8_t*>(payload.data()),
                         payload.size());

        // Sign
        ::ndn::security::SigningInfo signingInfo(
            ::ndn::security::SigningInfo::SIGNER_TYPE_SHA256);
        m_keyChain.sign(*data, signingInfo);

        // Send
        m_appLink->onReceiveData(*data);
    }

private:
    struct DictEntry {
        std::string docId;
        std::string canonicalName;
        std::string domainId;
        std::string type;
    };

    std::map<std::string, DictEntry> m_dictionary;
    ::ndn::KeyChain m_keyChain;

    uint32_t m_hits = 0;
    uint32_t m_misses = 0;
};

// =============================================================================
// Implementation
// =============================================================================

NS_OBJECT_ENSURE_REGISTERED(ExactNdnProducer);

TypeId
ExactNdnProducer::GetTypeId() {
    static TypeId tid = TypeId("ns3::ndn::ExactNdnProducer")
        .SetParent<App>()
        .SetGroupName("Ndn")
        .AddConstructor<ExactNdnProducer>();
    return tid;
}

} // namespace ndn
} // namespace ns3
