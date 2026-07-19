#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_I_BINARY_SIGNATURE_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_I_BINARY_SIGNATURE_ENCODER_HPP_INCLUDED

/// \file IBinarySignatureEncoder.hpp
/// \brief Dependency-free contract for converting dense vectors to binary signatures.

#include "BinarySignature.hpp"

#include <agent_memory/embedding/embedding_types.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace agent_memory {

    /// \brief Stable metadata describing one binary signature encoder instance.
    struct BinarySignatureEncoderInfo final {
        /// \brief Stable family id, e.g. "random_hyperplane_rademacher".
        std::string encoder_id;
        /// \brief Contract version for this encoder family.
        std::string encoder_version;
        /// \brief Expected number of input vector dimensions.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 0;
        /// \brief Deterministic seed used by seedable encoders; zero otherwise.
        std::uint64_t seed = 0;
        /// \brief Stable text fingerprint of all behaviour-affecting options.
        std::string config_fingerprint;
    };

    /// \brief Encodes dense numeric vectors into packed binary signatures.
    ///
    /// Implementations must be deterministic for a fixed `info()` result and
    /// input vector. The core contract is dependency-free; optimized SIMD,
    /// Eigen, or model-backed adapters may be added behind the same interface.
    class IBinarySignatureEncoder {
    public:
        virtual ~IBinarySignatureEncoder();

        /// \brief Returns immutable encoder metadata.
        [[nodiscard]] virtual const BinarySignatureEncoderInfo& info() const noexcept = 0;

        /// \brief Encodes one vector into a binary signature.
        [[nodiscard]] virtual BinarySignature encode(const Embedding& vector) const = 0;
    };

} // namespace agent_memory

#endif
