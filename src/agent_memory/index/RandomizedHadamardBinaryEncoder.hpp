#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_RANDOMIZED_HADAMARD_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_RANDOMIZED_HADAMARD_BINARY_ENCODER_HPP_INCLUDED

/// \file RandomizedHadamardBinaryEncoder.hpp
/// \brief Zero-training randomized Hadamard binary signature encoder.

#include "IBinarySignatureEncoder.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace agent_memory {

    /// \brief Options for the randomized Walsh-Hadamard projection encoder.
    struct RandomizedHadamardBinaryEncoderOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 128;
        /// \brief Seed used for deterministic signs and row permutations.
        std::uint64_t seed = 0;
    };

    /// \brief Zero-training structured random projection using Hadamard blocks.
    ///
    /// Each block applies a deterministic random sign diagonal, zero-pads the
    /// input to the next power of two, runs a Walsh-Hadamard transform, and reads
    /// coefficients through a deterministic permutation.
    ///
    /// For power-of-two input dimensions, rows within a complete block are
    /// orthogonal Hadamard rows after the sign diagonal. For non-power-of-two
    /// dimensions, zero-padding yields a structured randomized Hadamard
    /// projection. A complete padded block forms a tight frame over the original
    /// coordinates (`R^T R = padded_dimension * I`), but partial row sets are not
    /// generally pairwise orthogonal.
    class RandomizedHadamardBinaryEncoder final : public IBinarySignatureEncoder {
    public:
        explicit RandomizedHadamardBinaryEncoder(
            RandomizedHadamardBinaryEncoderOptions options
        );

        [[nodiscard]] const BinarySignatureEncoderInfo& info() const noexcept override;

        [[nodiscard]] BinarySignature encode(const Embedding& vector) const override;

        [[nodiscard]] std::vector<BinarySignature> encode_batch(
            const std::vector<Embedding>& vectors
        ) const override;

        /// \brief Name of the scalar transform backend used by this encoder.
        [[nodiscard]] static const char* compute_backend_name() noexcept;

        /// \brief Transform width after zero-padding to a power of two.
        [[nodiscard]] std::size_t padded_dimension() const noexcept;

    private:
        void validate_input(const Embedding& vector) const;
        [[nodiscard]] BinarySignature encode_validated(
            const Embedding& vector,
            std::vector<float>& work
        ) const;

        RandomizedHadamardBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
        std::size_t m_padded_dimension = 0;
    };

} // namespace agent_memory

#endif
