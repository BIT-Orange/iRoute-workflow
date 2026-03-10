/**
 * @file iroute-vector.hpp
 * @brief Semantic vector wrapper class for iRoute protocol.
 *
 * This file defines the SemanticVector class which encapsulates a 
 * floating-point vector used for semantic routing in NDN networks. The class
 * provides TLV encoding/decoding capabilities following NDN packet format
 * conventions. Vector dimension is configurable (default: 384).
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see Design_Guide.md Section 2.1 for design specifications
 */

#pragma once
#ifndef NDN_SIM_IROUTE_VECTOR_HPP
#define NDN_SIM_IROUTE_VECTOR_HPP

#include <ndn-cxx/encoding/block.hpp>
#include <ndn-cxx/encoding/encoding-buffer.hpp>
#include <ndn-cxx/encoding/tlv.hpp>

#include <cstdint>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <optional>

namespace iroute {

/**
 * @brief Custom TLV type identifiers for iRoute protocol.
 *
 * These TLV types are used to encode/decode semantic vectors in NDN packets.
 * Type values 128-252 are reserved for application-defined use in NDN TLV.
 */
namespace tlv {

/**
 * @brief TLV type for the SemanticVector container (outer wrapper).
 */
constexpr uint32_t SemanticVector = 128;

/**
 * @brief TLV type for the raw vector data (IEEE 754 float bytes).
 */
constexpr uint32_t VectorData = 129;

/**
 * @brief TLV type for the vector dimension (NonNegativeInteger).
 */
constexpr uint32_t VectorDim = 130;

} // namespace tlv

/**
 * @brief Default dimension for semantic vectors in iRoute protocol.
 *
 * Default is 384-dimensional, matching Wikipedia2Vec embeddings.
 * Can be overridden to 64 or other dimensions for experiments.
 */
inline size_t getDefaultVectorDimension() { return 384; }

/**
 * @brief Backward compatibility constant for kDefaultVectorDimension.
 * @deprecated Use getDefaultVectorDimension() instead.
 */
constexpr size_t kDefaultVectorDimension = 384;

/**
 * @brief Epsilon value for floating-point comparisons.
 *
 * Used to handle floating-point precision issues when comparing vectors.
 */
constexpr float kFloatEpsilon = 1e-6f;

/**
 * @class SemanticVector
 * @brief Wrapper class for a variable-dimension floating-point vector.
 *
 * SemanticVector encapsulates semantic embeddings used for content-based
 * routing in the iRoute protocol. It provides:
 * - TLV wire encoding/decoding for NDN packet transmission
 * - Cosine similarity computation for semantic matching
 * - Equality comparison operators
 * - Configurable vector dimensions
 *
 * @note Memory Model: Uses std::vector<float> internally with value semantics.
 *       Copying a SemanticVector performs a deep copy of all float values.
 *
 * @note Thread Safety: This class is NOT thread-safe. External synchronization
 *       is required for concurrent access.
 *
 * @par Example Usage:
 * @code
 * // Create a 384-dim semantic vector
 * std::vector<float> data(384, 0.5f);
 * iroute::SemanticVector vec(data);
 *
 * // Encode to wire format
 * ndn::Block wire = vec.wireEncode();
 *
 * // Decode from wire format with expected dimension validation
 * iroute::SemanticVector decoded;
 * decoded.wireDecode(wire, 384);  // Validate dimension
 *
 * // Compute similarity
 * double sim = vec.computeCosineSimilarity(decoded);
 * @endcode
 */
class SemanticVector {
public:
    /**
     * @brief Default constructor.
     *
     * Creates an empty semantic vector. The vector will have dimension 0
     * until data is set via setData() or wireDecode().
     */
    SemanticVector() = default;

    /**
     * @brief Constructs a SemanticVector from raw float data.
     *
     * @param data The floating-point vector data. Any dimension is accepted.
     *
     * @note Performs a deep copy of the input vector.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    explicit SemanticVector(const std::vector<float>& data);

    /**
     * @brief Constructs a SemanticVector with move semantics.
     *
     * @param data The floating-point vector data to move from.
     *
     * @par Complexity: O(1) for move.
     */
    explicit SemanticVector(std::vector<float>&& data);

    /**
     * @brief Constructs an empty SemanticVector with specified dimension.
     *
     * @param dimension The dimension of the vector (all zeros).
     */
    explicit SemanticVector(size_t dimension);

    /**
     * @brief Copy constructor.
     *
     * @param other The SemanticVector to copy from.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    SemanticVector(const SemanticVector& other) = default;

    /**
     * @brief Move constructor.
     *
     * @param other The SemanticVector to move from. After the move,
     *              other will be in a valid but unspecified state.
     *
     * @par Complexity: O(1).
     */
    SemanticVector(SemanticVector&& other) noexcept = default;

    /**
     * @brief Copy assignment operator.
     *
     * @param other The SemanticVector to copy from.
     * @return Reference to this object.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    SemanticVector& operator=(const SemanticVector& other) = default;

    /**
     * @brief Move assignment operator.
     *
     * @param other The SemanticVector to move from.
     * @return Reference to this object.
     *
     * @par Complexity: O(1).
     */
    SemanticVector& operator=(SemanticVector&& other) noexcept = default;

    /**
     * @brief Destructor.
     */
    ~SemanticVector() = default;

    // ========================================================================
    // Wire Encoding/Decoding
    // ========================================================================

    /**
     * @brief Encodes this SemanticVector to NDN TLV wire format.
     *
     * The wire format follows the iRoute TLV specification:
     * @verbatim
     * SemanticVector (Type=128)
     *   VectorDim (Type=130): <dimension as NonNegativeInteger>
     *   VectorData (Type=129): <raw IEEE 754 float bytes>
     * @endverbatim
     *
     * @return An ndn::Block containing the TLV-encoded vector.
     *
     * @throws std::runtime_error If the vector is empty or invalid.
     *
     * @note The returned Block owns its underlying buffer. It is safe to
     *       use after this SemanticVector is destroyed.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     *
     * @see wireDecode() for the inverse operation.
     */
    ndn::Block wireEncode() const;

    /**
     * @brief Decodes a SemanticVector from NDN TLV wire format.
     *
     * This method replaces the current vector data with the decoded data.
     *
     * @param wire The TLV Block to decode. Must have type tlv::SemanticVector.
     * @param expectedDim Optional expected dimension for validation.
     *                    If provided and doesn't match, throws error.
     *
     * @throws ndn::tlv::Error If:
     *         - wire.type() != tlv::SemanticVector
     *         - Required sub-elements (VectorDim, VectorData) are missing
     *         - VectorData size does not match VectorDim * sizeof(float)
     *         - expectedDim is provided and doesn't match VectorDim
     *
     * @note This method invalidates any cached wire encoding.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     *
     * @see wireEncode() for the inverse operation.
     */
    void wireDecode(const ndn::Block& wire, 
                    std::optional<size_t> expectedDim = std::nullopt);

    // ========================================================================
    // Similarity Computation
    // ========================================================================

    /**
     * @brief Computes the cosine similarity between this vector and another.
     *
     * Cosine similarity is defined as:
     * @f[
     *   \text{similarity}(A, B) = \frac{A \cdot B}{\|A\| \times \|B\|}
     * @f]
     *
     * where @f$ A \cdot B @f$ is the dot product and @f$ \|A\| @f$ is the
     * Euclidean norm (L2 norm) of vector A.
     *
     * @param other The vector to compare with. Must have the same dimension.
     *
     * @return The cosine similarity in the range [-1.0, 1.0]:
     *         - 1.0: vectors are identical in direction
     *         - 0.0: vectors are orthogonal
     *         - -1.0: vectors are opposite in direction
     *
     * @throws std::invalid_argument If dimensions do not match or either
     *         vector is empty.
     *
     * @note Returns 0.0 if either vector has zero magnitude (to avoid
     *       division by zero).
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    double computeCosineSimilarity(const SemanticVector& other) const;

    /**
     * @brief Computes the cosine distance between this vector and another.
     *
     * Cosine distance is defined as: d(A, B) = 1 - similarity(A, B)
     * For L2-normalized vectors, this equals 1 - (A · B).
     *
     * @param other The vector to compare with. Must have the same dimension.
     * @return The cosine distance in the range [0.0, 2.0].
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    double computeCosineDistance(const SemanticVector& other) const;

    /**
     * @brief Computes the dot product between this vector and another.
     *
     * For L2-normalized vectors, the dot product equals cosine similarity.
     *
     * @param other The vector to compute dot product with. Must have same dimension.
     * @return The dot product value.
     *
     * @throws std::invalid_argument If dimensions do not match or either
     *         vector is empty.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    double dot(const SemanticVector& other) const;

    /**
     * @brief L2-normalizes this vector in-place.
     *
     * After normalization, the vector will have unit length (||v|| = 1).
     * This is required for the iRoute scoring formula where all vectors
     * must be L2-normalized so that dot product equals cosine similarity.
     *
     * @return Reference to this vector (for method chaining).
     *
     * @throws std::runtime_error If vector is empty or has zero magnitude.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    SemanticVector& normalize();

    /**
     * @brief Returns a new L2-normalized copy of this vector.
     *
     * @return A new SemanticVector with unit length.
     *
     * @throws std::runtime_error If vector is empty or has zero magnitude.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    SemanticVector normalized() const;

    /**
     * @brief Checks if this vector is L2-normalized (has unit length).
     *
     * @return true if ||v|| is within kFloatEpsilon of 1.0.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    bool isNormalized() const;

    /**
     * @brief Computes the L2 norm (magnitude) of this vector.
     *
     * @return The Euclidean norm: sqrt(sum(v_i^2)).
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    double magnitude() const;

    // ========================================================================
    // Comparison Operators
    // ========================================================================

    /**
     * @brief Checks equality with another SemanticVector.
     *
     * Two vectors are considered equal if they have the same dimension and
     * all corresponding elements are equal within floating-point tolerance
     * (kFloatEpsilon).
     *
     * @param other The vector to compare with.
     * @return true if vectors are equal, false otherwise.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    bool operator==(const SemanticVector& other) const;

    /**
     * @brief Checks inequality with another SemanticVector.
     *
     * @param other The vector to compare with.
     * @return true if vectors are not equal, false otherwise.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    bool operator!=(const SemanticVector& other) const;

    // ========================================================================
    // Accessors
    // ========================================================================

    /**
     * @brief Returns the dimension of this vector.
     *
     * @return The number of elements in the vector. Returns 0 for empty vectors.
     *
     * @par Complexity: O(1).
     */
    size_t getDimension() const noexcept;

    /**
     * @brief Returns a const reference to the underlying float data.
     *
     * @return Const reference to the internal std::vector<float>.
     *
     * @note The returned reference becomes invalid if this SemanticVector
     *       is modified or destroyed.
     *
     * @par Complexity: O(1).
     */
    const std::vector<float>& getData() const noexcept;

    /**
     * @brief Sets the vector data.
     *
     * @param data The new floating-point vector data. Any dimension is accepted.
     *
     * @note This invalidates any cached wire encoding.
     *
     * @par Complexity: O(N) where N is the vector dimension.
     */
    void setData(const std::vector<float>& data);

    /**
     * @brief Sets the vector data with move semantics.
     *
     * @param data The new floating-point vector data to move from.
     *
     * @par Complexity: O(1) for move.
     */
    void setData(std::vector<float>&& data);

    /**
     * @brief Checks if the vector is empty (has no data).
     *
     * @return true if the vector has dimension 0, false otherwise.
     *
     * @par Complexity: O(1).
     */
    bool empty() const noexcept;

    /**
     * @brief Accesses an element by index (const version).
     *
     * @param index The zero-based index of the element.
     * @return The float value at the given index.
     *
     * @throws std::out_of_range If index >= getDimension().
     *
     * @par Complexity: O(1).
     */
    float operator[](size_t index) const;

private:
    /**
     * @brief Internal storage for the semantic vector data.
     */
    std::vector<float> m_data;

    /**
     * @brief Cached wire encoding for efficiency.
     *
     * Lazily populated by wireEncode(). Invalidated when m_data changes.
     * Mutable to allow caching in const methods.
     */
    mutable ndn::Block m_wire;
};

} // namespace iroute

#endif // NDN_SIM_IROUTE_VECTOR_HPP
