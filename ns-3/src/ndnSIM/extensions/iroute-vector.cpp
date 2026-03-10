/**
 * @file iroute-vector.cpp
 * @brief Implementation of the SemanticVector class.
 *
 * This file contains the implementation of all SemanticVector methods including
 * TLV wire encoding/decoding and cosine similarity computation.
 * Vector dimensions are now configurable.
 *
 * @author iRoute Team
 * @date 2024
 *
 * @see iroute-vector.hpp for class declaration
 * @see Design_Guide.md Section 2.1 for design specifications
 */

#include "iroute-vector.hpp"

#include <ndn-cxx/encoding/block-helpers.hpp>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace iroute {

// =============================================================================
// Constructors
// =============================================================================

SemanticVector::SemanticVector(const std::vector<float>& data)
    : m_data(data)
{
}

SemanticVector::SemanticVector(std::vector<float>&& data)
    : m_data(std::move(data))
{
}

SemanticVector::SemanticVector(size_t dimension)
    : m_data(dimension, 0.0f)
{
}

// =============================================================================
// Wire Encoding
// =============================================================================

ndn::Block
SemanticVector::wireEncode() const
{
    // Return cached encoding if available
    if (m_wire.hasWire()) {
        return m_wire;
    }

    // Validate state
    if (m_data.empty()) {
        throw std::runtime_error("SemanticVector::wireEncode: Cannot encode empty vector");
    }

    // Build the TLV structure using ndn::EncodingBuffer
    // Wire format:
    //   SemanticVector (Type=128)
    //     VectorDim (Type=130): <dimension as NonNegativeInteger>
    //     VectorData (Type=129): <raw float bytes>

    ndn::EncodingBuffer encoder;

    // Calculate the size of float data
    // TODO: This implementation assumes homogeneous endianness (little-endian x86/x64).
    //       For cross-platform compatibility, consider using ntohl/htonl for each float
    //       or a portable serialization format. In NS-3 simulation, this is acceptable
    //       since all nodes run on the same machine architecture.
    size_t floatDataSize = m_data.size() * sizeof(float);

    // Encode in reverse order (NDN TLV encoding convention)
    // 1. First encode VectorData (raw bytes)
    size_t vectorDataLength = encoder.prependRange(
        reinterpret_cast<const uint8_t*>(m_data.data()),
        reinterpret_cast<const uint8_t*>(m_data.data()) + floatDataSize);
    vectorDataLength += encoder.prependVarNumber(vectorDataLength);
    vectorDataLength += encoder.prependVarNumber(tlv::VectorData);

    // 2. Then encode VectorDim (NonNegativeInteger)
    size_t vectorDimLength = ndn::encoding::prependNonNegativeIntegerBlock(
        encoder, tlv::VectorDim, static_cast<uint64_t>(m_data.size()));

    // 3. Finally, wrap everything in SemanticVector container
    size_t totalContentLength = vectorDataLength + vectorDimLength;
    encoder.prependVarNumber(totalContentLength);
    encoder.prependVarNumber(tlv::SemanticVector);

    // Create and cache the Block
    m_wire = encoder.block();
    return m_wire;
}

// =============================================================================
// Wire Decoding
// =============================================================================

void
SemanticVector::wireDecode(const ndn::Block& wire, std::optional<size_t> expectedDim)
{
    // Validate TLV type
    if (wire.type() != tlv::SemanticVector) {
        std::ostringstream oss;
        oss << "SemanticVector::wireDecode: Unexpected TLV type. Expected "
            << tlv::SemanticVector << ", got " << wire.type();
        throw ndn::tlv::Error(oss.str());
    }

    // Parse sub-elements
    wire.parse();

    // Find and validate VectorDim
    auto dimElement = wire.find(tlv::VectorDim);
    if (dimElement == wire.elements_end()) {
        throw ndn::tlv::Error("SemanticVector::wireDecode: Missing VectorDim element");
    }

    uint64_t dimension = ndn::encoding::readNonNegativeInteger(*dimElement);
    
    // Validate expected dimension if provided
    if (expectedDim.has_value() && dimension != expectedDim.value()) {
        std::ostringstream oss;
        oss << "SemanticVector::wireDecode: Dimension mismatch. Expected "
            << expectedDim.value() << ", got " << dimension;
        throw ndn::tlv::Error(oss.str());
    }

    // Find and validate VectorData
    auto dataElement = wire.find(tlv::VectorData);
    if (dataElement == wire.elements_end()) {
        throw ndn::tlv::Error("SemanticVector::wireDecode: Missing VectorData element");
    }

    // Validate data size
    size_t expectedSize = dimension * sizeof(float);
    if (dataElement->value_size() != expectedSize) {
        std::ostringstream oss;
        oss << "SemanticVector::wireDecode: Invalid VectorData size. Expected "
            << expectedSize << " bytes for " << dimension << " dimensions, got " 
            << dataElement->value_size();
        throw ndn::tlv::Error(oss.str());
    }

    // Copy raw bytes to float vector
    // TODO: Endianness assumption - see wireEncode() comment.
    //       This assumes the same byte order as the encoder.
    m_data.resize(dimension);
    std::memcpy(m_data.data(), dataElement->value(), expectedSize);

    // Cache the wire encoding
    m_wire = wire;
}

// =============================================================================
// Similarity Computation
// =============================================================================

double
SemanticVector::computeCosineSimilarity(const SemanticVector& other) const
{
    // Validate dimensions
    if (m_data.empty() || other.m_data.empty()) {
        throw std::invalid_argument(
            "SemanticVector::computeCosineSimilarity: Cannot compute similarity with empty vector");
    }

    if (m_data.size() != other.m_data.size()) {
        std::ostringstream oss;
        oss << "SemanticVector::computeCosineSimilarity: Dimension mismatch. "
            << "This vector has dimension " << m_data.size()
            << ", other has dimension " << other.m_data.size();
        throw std::invalid_argument(oss.str());
    }

    // Compute dot product: A · B
    // Using std::inner_product for efficiency and clarity
    double dotProduct = std::inner_product(
        m_data.begin(), m_data.end(),
        other.m_data.begin(),
        0.0);  // Use double for accumulator to prevent overflow

    // Compute magnitudes: ||A|| and ||B||
    // ||A|| = sqrt(sum(a_i^2))
    double magnitudeA = std::inner_product(
        m_data.begin(), m_data.end(),
        m_data.begin(),
        0.0);
    magnitudeA = std::sqrt(magnitudeA);

    double magnitudeB = std::inner_product(
        other.m_data.begin(), other.m_data.end(),
        other.m_data.begin(),
        0.0);
    magnitudeB = std::sqrt(magnitudeB);

    // Handle division by zero
    // If either vector has zero magnitude, return 0.0 (undefined similarity)
    double magnitudeProduct = magnitudeA * magnitudeB;
    if (magnitudeProduct < static_cast<double>(kFloatEpsilon)) {
        return 0.0;
    }

    // Compute cosine similarity: (A · B) / (||A|| × ||B||)
    double similarity = dotProduct / magnitudeProduct;

    // Clamp to valid range [-1.0, 1.0] to handle floating-point errors
    return std::clamp(similarity, -1.0, 1.0);
}

double
SemanticVector::computeCosineDistance(const SemanticVector& other) const
{
    // Cosine distance: d(A, B) = 1 - similarity(A, B)
    return 1.0 - computeCosineSimilarity(other);
}

double
SemanticVector::dot(const SemanticVector& other) const
{
    // Validate dimensions
    if (m_data.empty() || other.m_data.empty()) {
        throw std::invalid_argument(
            "SemanticVector::dot: Cannot compute dot product with empty vector");
    }

    if (m_data.size() != other.m_data.size()) {
        std::ostringstream oss;
        oss << "SemanticVector::dot: Dimension mismatch. "
            << "This vector has dimension " << m_data.size()
            << ", other has dimension " << other.m_data.size();
        throw std::invalid_argument(oss.str());
    }

    // Compute dot product: A · B
    return std::inner_product(
        m_data.begin(), m_data.end(),
        other.m_data.begin(),
        0.0);  // Use double for accumulator
}

double
SemanticVector::magnitude() const
{
    if (m_data.empty()) {
        return 0.0;
    }

    double sumSquares = std::inner_product(
        m_data.begin(), m_data.end(),
        m_data.begin(),
        0.0);
    return std::sqrt(sumSquares);
}

bool
SemanticVector::isNormalized() const
{
    if (m_data.empty()) {
        return false;
    }

    double mag = magnitude();
    return std::abs(mag - 1.0) < static_cast<double>(kFloatEpsilon);
}

SemanticVector&
SemanticVector::normalize()
{
    if (m_data.empty()) {
        throw std::runtime_error("SemanticVector::normalize: Cannot normalize empty vector");
    }

    double mag = magnitude();
    if (mag < static_cast<double>(kFloatEpsilon)) {
        throw std::runtime_error("SemanticVector::normalize: Cannot normalize zero-magnitude vector");
    }

    // Divide each element by magnitude
    float invMag = static_cast<float>(1.0 / mag);
    for (auto& val : m_data) {
        val *= invMag;
    }

    m_wire.reset();  // Invalidate cached encoding
    return *this;
}

SemanticVector
SemanticVector::normalized() const
{
    SemanticVector result(*this);
    result.normalize();
    return result;
}

// =============================================================================
// Comparison Operators
// =============================================================================

bool
SemanticVector::operator==(const SemanticVector& other) const
{
    // Check dimension first (fast path)
    if (m_data.size() != other.m_data.size()) {
        return false;
    }

    // Compare elements with floating-point tolerance
    for (size_t i = 0; i < m_data.size(); ++i) {
        if (std::abs(m_data[i] - other.m_data[i]) > kFloatEpsilon) {
            return false;
        }
    }

    return true;
}

bool
SemanticVector::operator!=(const SemanticVector& other) const
{
    return !(*this == other);
}

// =============================================================================
// Accessors
// =============================================================================

size_t
SemanticVector::getDimension() const noexcept
{
    return m_data.size();
}

const std::vector<float>&
SemanticVector::getData() const noexcept
{
    return m_data;
}

void
SemanticVector::setData(const std::vector<float>& data)
{
    m_data = data;
    m_wire.reset();  // Invalidate cached encoding
}

void
SemanticVector::setData(std::vector<float>&& data)
{
    m_data = std::move(data);
    m_wire.reset();  // Invalidate cached encoding
}

bool
SemanticVector::empty() const noexcept
{
    return m_data.empty();
}

float
SemanticVector::operator[](size_t index) const
{
    if (index >= m_data.size()) {
        std::ostringstream oss;
        oss << "SemanticVector::operator[]: Index " << index
            << " out of range. Vector size is " << m_data.size();
        throw std::out_of_range(oss.str());
    }
    return m_data[index];
}

} // namespace iroute
