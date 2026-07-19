#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_RANDOM_HYPERPLANE_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_RANDOM_HYPERPLANE_BINARY_ENCODER_HPP_INCLUDED

/// \file RandomHyperplaneBinaryEncoder.hpp
/// \brief Deterministic random-hyperplane binary signature encoder.

#include "IBinarySignatureEncoder.hpp"

#include <cstddef>
#include <cstdint>

namespace agent_memory {

    /// \brief Options for the deterministic random-hyperplane baseline encoder.
    struct RandomHyperplaneBinaryEncoderOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 128;
        /// \brief Seed used to derive deterministic hyperplane signs.
        std::uint64_t seed = 0;
    };

    /// \brief Zero-training SimHash-style encoder with deterministic +/-1 weights.
    ///
    /// This baseline is intended for plumbing and candidate-filter experiments.
    /// For every output bit, a deterministic Rademacher hyperplane is derived
    /// from `(seed, bit_index, dimension_index)` using integer mixing. The bit
    /// is set when the dot product is strictly positive. Zero vectors therefore
    /// encode to the all-zero signature.
    class RandomHyperplaneBinaryEncoder final : public IBinarySignatureEncoder {
    public:
        explicit RandomHyperplaneBinaryEncoder(RandomHyperplaneBinaryEncoderOptions options);

        [[nodiscard]] const BinarySignatureEncoderInfo& info() const noexcept override;

        [[nodiscard]] BinarySignature encode(const Embedding& vector) const override;

    private:
        [[nodiscard]] static bool hyperplane_sign(
            std::uint64_t seed,
            std::size_t bit_index,
            std::size_t dimension_index
        ) noexcept;

        RandomHyperplaneBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
    };

} // namespace agent_memory

#endif
