#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_RANDOM_HYPERPLANE_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_RANDOM_HYPERPLANE_BINARY_ENCODER_HPP_INCLUDED

/// \file RandomHyperplaneBinaryEncoder.hpp
/// \brief Deterministic random-hyperplane binary signature encoder.

#include "IBinarySignatureEncoder.hpp"
#include "VectorSimilarityComputer.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

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

    /// \brief One non-zero component of a sparse encoder input.
    struct SparseEmbeddingValue final {
        std::size_t index = 0;
        float value = 0.0F;
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

        [[nodiscard]] std::vector<BinarySignature> encode_batch(
            const std::vector<Embedding>& vectors
        ) const override;

        /// \brief Encodes a sparse vector without materializing zero components.
        /// \param dimension Logical dense dimension; must match `info().input_dimension`.
        /// \param values Finite non-zero entries with unique strictly increasing indices.
        [[nodiscard]] BinarySignature encode_sparse(
            std::size_t dimension,
            const std::vector<SparseEmbeddingValue>& values
        ) const;

        /// \brief SIMD backend used by the materialized dense projection.
        [[nodiscard]] VectorSimilarityBackend similarity_backend() const noexcept;

    private:
        [[nodiscard]] static bool hyperplane_sign(
            std::uint64_t seed,
            std::size_t bit_index,
            std::size_t dimension_index
        ) noexcept;

        void ensure_hyperplanes() const;
        void validate_dense_input(const Embedding& vector) const;
        [[nodiscard]] BinarySignature encode_dense_validated(const Embedding& vector) const;

        RandomHyperplaneBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
        VectorSimilarityComputer m_similarity;
        mutable std::once_flag m_hyperplanes_once;
        mutable std::vector<float> m_hyperplanes;
    };

} // namespace agent_memory

#endif
