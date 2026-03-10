/**
 * @file iroute-route-manager-registry.cpp
 * @brief Implementation of RouteManagerRegistry.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see iroute-route-manager-registry.hpp for class declaration
 */

#include "iroute-route-manager-registry.hpp"

#include "common/logger.hpp"

#include <stdexcept>
#include <sstream>

namespace iroute {

NFD_LOG_INIT(iRoute.Registry);

// =============================================================================
// Static Member Definitions
// =============================================================================

std::unordered_map<uint32_t, std::shared_ptr<RouteManager>> RouteManagerRegistry::s_registry;
std::mutex RouteManagerRegistry::s_mutex;

// =============================================================================
// Public Methods
// =============================================================================

std::shared_ptr<RouteManager>
RouteManagerRegistry::getOrCreate(uint32_t nodeId, size_t vectorDim)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    auto it = s_registry.find(nodeId);
    if (it != s_registry.end()) {
        // Check for dimension mismatch
        if (it->second->getVectorDim() != vectorDim) {
            std::ostringstream oss;
            oss << "RouteManagerRegistry: VectorDim mismatch for nodeId=" << nodeId
                << " (existing=" << it->second->getVectorDim()
                << ", requested=" << vectorDim << ")";
            NFD_LOG_ERROR(oss.str());
            throw std::runtime_error(oss.str());
        }
        return it->second;
    }

    // Create new RouteManager for this node
    auto rm = std::make_shared<RouteManager>(vectorDim);
    s_registry.emplace(nodeId, rm);

    NFD_LOG_INFO("Created RouteManager for nodeId=" << nodeId 
                 << ", vectorDim=" << vectorDim
                 << ", total nodes=" << s_registry.size());

    return rm;
}

std::shared_ptr<RouteManager>
RouteManagerRegistry::get(uint32_t nodeId)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    auto it = s_registry.find(nodeId);
    if (it != s_registry.end()) {
        return it->second;
    }
    return nullptr;
}

void
RouteManagerRegistry::setVectorDimForAll(size_t vectorDim)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    for (auto& [nodeId, rm] : s_registry) {
        rm->setVectorDim(vectorDim);
    }

    NFD_LOG_INFO("Set vectorDim=" << vectorDim << " for all " 
                 << s_registry.size() << " RouteManagers");
}

void
RouteManagerRegistry::clear()
{
    std::lock_guard<std::mutex> lock(s_mutex);

    size_t count = s_registry.size();
    s_registry.clear();

    NFD_LOG_INFO("Cleared " << count << " RouteManagers from registry");
}

size_t
RouteManagerRegistry::size()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_registry.size();
}

} // namespace iroute
