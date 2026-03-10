/**
 * @file iroute-route-manager-registry.hpp
 * @brief Per-node RouteManager registry for iRoute protocol.
 *
 * This file provides the RouteManagerRegistry class, which manages
 * RouteManager instances on a per-node basis. This ensures each NS-3
 * node has its own semantic RIB, enabling realistic distributed routing.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see iroute-manager.hpp for RouteManager class
 */

#pragma once
#ifndef NDN_SIM_IROUTE_ROUTE_MANAGER_REGISTRY_HPP
#define NDN_SIM_IROUTE_ROUTE_MANAGER_REGISTRY_HPP

#include "iroute-manager.hpp"

#include <memory>
#include <unordered_map>
#include <cstdint>

namespace iroute {

/**
 * @class RouteManagerRegistry
 * @brief Manages per-node RouteManager instances.
 *
 * In NS-3 simulations, each node should have its own RouteManager to
 * accurately model distributed semantic routing. This registry provides
 * a centralized way to access the RouteManager for any node.
 *
 * @par Usage:
 * @code
 * // In IRouteApp or SemanticStrategy:
 * uint32_t nodeId = GetNode()->GetId();  // or Simulator::GetContext()
 * auto rm = RouteManagerRegistry::getOrCreate(nodeId, 384);
 * rm->updateRoute(...);
 * @endcode
 *
 * @note All methods are static. The registry is a global singleton,
 *       but each RouteManager instance is per-node.
 */
class RouteManagerRegistry {
public:
    /**
     * @brief Gets or creates the RouteManager for a specific node.
     *
     * If a RouteManager already exists for the given nodeId, it is returned.
     * Otherwise, a new RouteManager is created with the specified vectorDim.
     *
     * @param nodeId The NS-3 node ID.
     * @param vectorDim The expected vector dimension for this node's RIB.
     *
     * @return Shared pointer to the RouteManager for this node.
     *
     * @throws std::runtime_error If a RouteManager already exists for this
     *         node but with a different vectorDim.
     *
     * @note Thread-safe.
     */
    static std::shared_ptr<RouteManager> getOrCreate(uint32_t nodeId, size_t vectorDim);

    /**
     * @brief Gets the RouteManager for a node, if it exists.
     *
     * @param nodeId The NS-3 node ID.
     *
     * @return Shared pointer to the RouteManager, or nullptr if not found.
     *
     * @note Thread-safe.
     */
    static std::shared_ptr<RouteManager> get(uint32_t nodeId);

    /**
     * @brief Sets the vector dimension for all existing RouteManagers.
     *
     * @param vectorDim The new vector dimension.
     *
     * @note Thread-safe.
     */
    static void setVectorDimForAll(size_t vectorDim);

    /**
     * @brief Clears all RouteManager instances.
     *
     * Use this at the start of each simulation run to ensure a clean state.
     *
     * @note Thread-safe.
     */
    static void clear();

    /**
     * @brief Gets the number of registered RouteManagers.
     *
     * @return The number of nodes with RouteManagers.
     */
    static size_t size();

private:
    /**
     * @brief Map from nodeId to RouteManager instance.
     */
    static std::unordered_map<uint32_t, std::shared_ptr<RouteManager>> s_registry;

    /**
     * @brief Mutex for thread-safe access.
     */
    static std::mutex s_mutex;
};

} // namespace iroute

#endif // NDN_SIM_IROUTE_ROUTE_MANAGER_REGISTRY_HPP
