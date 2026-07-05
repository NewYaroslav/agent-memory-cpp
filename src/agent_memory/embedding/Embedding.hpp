#pragma once
#ifndef AGENT_MEMORY_HEADER_EMBEDDING_EMBEDDING_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EMBEDDING_EMBEDDING_HPP_INCLUDED

/// \file Embedding.hpp
/// \brief Dependency-free embedding value types and model metadata.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

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

    /// \brief Dense embedding vector.
    struct Embedding final {
        std::vector<float> values;

        /// \brief Returns true when the vector contains no values.
        [[nodiscard]] bool empty() const noexcept;

        /// \brief Returns the number of scalar values in the vector.
        [[nodiscard]] std::size_t dimension() const noexcept;
    };

    /// \brief Text and purpose passed to an embedding backend.
    struct EmbeddingRequest final {
        std::string text;
        EmbeddingPurpose purpose = EmbeddingPurpose::Symmetric;
    };

    /// \brief Stable model metadata needed by indexes and retrieval code.
    struct EmbeddingModelInfo final {
        std::string model_id;
        std::size_t dimension = 0;
        std::size_t max_tokens = 0;
        SimilarityMetric similarity_metric = SimilarityMetric::Cosine;
        PoolingMode pooling_mode = PoolingMode::ModelDefault;
        bool normalized = false;
    };

    /// \brief Returns stable lowercase purpose name.
    [[nodiscard]] std::string_view to_string(EmbeddingPurpose purpose) noexcept;

    /// \brief Returns stable lowercase similarity metric name.
    [[nodiscard]] std::string_view to_string(SimilarityMetric metric) noexcept;

    /// \brief Returns stable lowercase pooling mode name.
    [[nodiscard]] std::string_view to_string(PoolingMode mode) noexcept;

    /// \brief Parses an embedding purpose from a lowercase or mixed-case name.
    /// \return True when parsing succeeds.
    bool parse_embedding_purpose(std::string_view text, EmbeddingPurpose& purpose);

    /// \brief Parses a similarity metric from a lowercase or mixed-case name.
    /// \return True when parsing succeeds.
    bool parse_similarity_metric(std::string_view text, SimilarityMetric& metric);

    /// \brief Parses a pooling mode from a lowercase or mixed-case name.
    /// \return True when parsing succeeds.
    bool parse_pooling_mode(std::string_view text, PoolingMode& mode);

} // namespace agent_memory

#endif
