#pragma once
#ifndef AGENT_MEMORY_HEADER_EMBEDDING_ENUMS_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EMBEDDING_ENUMS_HPP_INCLUDED

/// \file enums.hpp
/// \brief Embedding-related enum types and conversion helpers.

#include <string_view>

namespace agent_memory {

    /// \brief Result object for fallible enum parsing.
    template <typename Value>
    struct ParseResult final {
        bool success = false;
        Value value{};

        /// \brief Returns true when parsing succeeded.
        [[nodiscard]] explicit operator bool() const noexcept {
            return success;
        }
    };

    /// \brief Intended use of the embedded text.
    enum class EmbeddingPurpose {
        Query,
        Document,
        Symmetric,
        Classification,
        Custom
    };

    /// \brief Similarity metric expected by embeddings produced by a model.
    enum class SimilarityMetric {
        Cosine,
        DotProduct,
        Euclidean
    };

    /// \brief Token pooling strategy used by an embedding model.
    enum class PoolingMode {
        ModelDefault,
        Mean,
        Cls,
        LastToken,
        Custom
    };

    /// \brief Returns stable lowercase purpose name.
    [[nodiscard]] std::string_view to_string(EmbeddingPurpose purpose) noexcept;

    /// \brief Returns stable lowercase similarity metric name.
    [[nodiscard]] std::string_view to_string(SimilarityMetric metric) noexcept;

    /// \brief Returns stable lowercase pooling mode name.
    [[nodiscard]] std::string_view to_string(PoolingMode mode) noexcept;

    /// \brief Parses a supported enum value from a lowercase or mixed-case name.
    template <typename Enum>
    [[nodiscard]] ParseResult<Enum> to_enum(std::string_view text) noexcept;

    template <>
    [[nodiscard]] ParseResult<EmbeddingPurpose> to_enum<EmbeddingPurpose>(
        std::string_view text
    ) noexcept;

    template <>
    [[nodiscard]] ParseResult<SimilarityMetric> to_enum<SimilarityMetric>(
        std::string_view text
    ) noexcept;

    template <>
    [[nodiscard]] ParseResult<PoolingMode> to_enum<PoolingMode>(
        std::string_view text
    ) noexcept;

} // namespace agent_memory

#endif
