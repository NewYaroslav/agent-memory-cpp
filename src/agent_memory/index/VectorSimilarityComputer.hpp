#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_VECTOR_SIMILARITY_COMPUTER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_VECTOR_SIMILARITY_COMPUTER_HPP_INCLUDED

/// \file VectorSimilarityComputer.hpp
/// \brief Runtime-selected vector similarity primitives.

#include <string_view>

namespace agent_memory {

    struct Embedding;

    /// \brief Implementation selected for vector arithmetic.
    enum class VectorSimilarityBackend {
        Scalar,
        Sse2,
        Avx2
    };

    /// \brief Returns a stable diagnostic name for a vector similarity backend.
    [[nodiscard]] std::string_view vector_similarity_backend_name(
        VectorSimilarityBackend backend
    ) noexcept;

    /// \brief Computes vector similarity primitives through a runtime-selected backend.
    /// \note SIMD can be disabled to provide a portable reference implementation for tests.
    class VectorSimilarityComputer final {
    public:
        explicit VectorSimilarityComputer(bool enable_simd = true) noexcept;

        /// \brief Returns the backend selected for this computer.
        [[nodiscard]] VectorSimilarityBackend backend() const noexcept;

        /// \brief Computes the dot product of equal-width embeddings.
        /// \throws std::invalid_argument when dimensions differ.
        [[nodiscard]] float dot_product(
            const Embedding& lhs,
            const Embedding& rhs
        ) const;

        /// \brief Dot product over raw equally-sized contiguous float arrays.
        /// \pre Both pointers address at least `size` values.
        [[nodiscard]] float dot_product_values(
            const float* lhs,
            const float* rhs,
            std::size_t size
        ) const noexcept;

        /// \brief Computes the squared L2 norm of an embedding.
        [[nodiscard]] float squared_norm(const Embedding& embedding) const noexcept;

        /// \brief Computes negative squared Euclidean distance for higher-is-better ranking.
        /// \throws std::invalid_argument when dimensions differ.
        [[nodiscard]] float negative_squared_distance(
            const Embedding& lhs,
            const Embedding& rhs
        ) const;

    private:
        VectorSimilarityBackend m_backend = VectorSimilarityBackend::Scalar;
    };

} // namespace agent_memory

#endif
