/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file semantic-producer.hpp
 * @brief SemanticProducer application that correctly handles parameterized Interests.
 *
 * This producer is designed to work with Interests that contain ApplicationParameters
 * (e.g., semantic vectors). Unlike the standard ndnSIM Producer, this application
 * uses the EXACT Interest Name (including the params-sha256 component) when 
 * generating Data packets, ensuring correct PIT matching.
 *
 * @note Standard ndnSIM Producer may not correctly handle the params-sha256
 *       component in Interest Names, causing PIT lookup failures for Data.
 *
 * @author iRoute Team
 * @date 2024
 */

#pragma once

#include "iroute-vector.hpp"

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include "ns3/random-variable-stream.h"

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <unordered_map>
#include <string>

namespace ns3 {
namespace ndn {

/**
 * @class SemanticProducer
 * @brief NDN Producer that preserves the full Interest Name for parameterized requests.
 *
 * This producer handles Interests with ApplicationParameters correctly by using
 * the complete Interest Name (including params-sha256 digest) as the Data Name.
 * This ensures that PIT entries created by parameterized Interests can be
 * properly matched when Data packets return.
 *
 * @par Key Features:
 * - Preserves params-sha256 component in Data Name
 * - Supports configurable payload size
 * - Optional semantic vector processing from ApplicationParameters
 *
 * @par Usage in Simulation:
 * @code
 * ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
 * producerHelper.SetPrefix("/semantic/query");
 * producerHelper.SetAttribute("PayloadSize", StringValue("1024"));
 * producerHelper.Install(producerNode);
 * @endcode
 */
class SemanticProducer : public App
{
public:
    /**
     * @brief Get the TypeId for this class.
     * @return The ns-3 TypeId.
     */
    static TypeId
    GetTypeId();

    /**
     * @brief Default constructor.
     */
    SemanticProducer();

    /**
     * @brief Called when an Interest is received.
     *
     * This method generates a Data packet using the EXACT Interest Name
     * (including any params-sha256 component) to ensure PIT matching.
     *
     * @param interest The received Interest packet.
     */
    virtual void
    OnInterest(shared_ptr<const Interest> interest) override;

    /**
     * @brief Sets the semantic vector representing this producer's content.
     *
     * This vector will be used for semantic routing to find this producer.
     *
     * @param vector The semantic embedding for this producer.
     */
    void SetNodeVector(const iroute::SemanticVector& vector);

    /**
     * @brief Gets the producer's semantic vector.
     *
     * @return Const reference to the semantic vector.
     */
    const iroute::SemanticVector& GetNodeVector() const { return m_nodeVector; }

    /**
     * @brief Adds a content entry to this producer's registry.
     *
     * Each content has a name and associated semantic vector.
     * When an Interest matches a registered content name, the producer
     * can look up the associated semantic metadata.
     *
     * @param contentName The content name (e.g., "/wiki/...").
     * @param semanticVector The 384-dim semantic embedding for this content.
     */
    void AddContent(const std::string& contentName, 
                    const iroute::SemanticVector& semanticVector);

    /**
     * @brief Gets the number of registered content entries.
     *
     * @return Number of content entries.
     */
    size_t GetContentCount() const { return m_contentRegistry.size(); }

    /**
     * @brief Sets the producer ID for identification in Data packets.
     *
     * This ID is embedded in Data packets so consumers can verify
     * which producer responded to their query.
     *
     * @param producerId The producer ID (from CSV dataset).
     */
    void SetProducerId(uint32_t producerId) { m_producerId = producerId; }

    /**
     * @brief Gets the producer ID.
     *
     * @return The producer ID.
     */
    uint32_t GetProducerId() const { return m_producerId; }
    
    /**
     * @brief Set the activity state of the producer.
     * 
     * @param active If true, producer responds to Interests. If false, it drops them.
     */
    void SetActive(bool active) { m_active = active; }
    
    /**
     * @brief Get the activity state.
     */
    bool IsActive() const { return m_active; }

protected:
    /**
     * @brief Called when the application starts.
     */
    virtual void
    StartApplication() override;

    /**
     * @brief Called when the application stops.
     */
    virtual void
    StopApplication() override;

private:
    /// The name prefix this producer serves
    Name m_prefix;
    
    /// Size of the payload in generated Data packets
    uint32_t m_payloadSize;
    
    /// Freshness period for generated Data packets (ns3::Time for attribute compatibility)
    ns3::Time m_freshness;
    
    /// Key chain for signing Data packets
    ::ndn::KeyChain m_keyChain;

    /// Semantic vector representing this producer's overall content theme
    iroute::SemanticVector m_nodeVector;

    /// Registry mapping content names to their semantic vectors
    std::unordered_map<std::string, iroute::SemanticVector> m_contentRegistry;

    /// Producer ID for identification in Data packets (from CSV dataset)
    uint32_t m_producerId = 0;

    /// Expected vector dimension (default: 384)
    uint32_t m_vectorDim = 384;
    
    /// Activity flag (false = simulate failure/outage)
    bool m_active = true;

    /// Base processing delay before replying Data (microseconds)
    uint32_t m_replyDelayUs = 0;

    /// Uniform per-Interest jitter (+/- microseconds) applied to reply delay
    uint32_t m_replyJitterUs = 0;

    /// RNG for reply delay jitter
    Ptr<UniformRandomVariable> m_replyDelayRv;
};

} // namespace ndn
} // namespace ns3
