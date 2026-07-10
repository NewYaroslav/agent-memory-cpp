#pragma once
#ifndef AGENT_MEMORY_HEADER_EMBEDDING_EMBEDDING_TYPES_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EMBEDDING_EMBEDDING_TYPES_HPP_INCLUDED

/// \file embedding_types.hpp
/// \brief Dependency-free embedding vectors, requests, and model metadata.

#include <agent_memory/embedding/enums.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace agent_memory {

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

} // namespace agent_memory

#endif
