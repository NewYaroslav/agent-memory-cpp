#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_VECTOR_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_VECTOR_INDEX_HPP_INCLUDED

/// \file VectorIndex.hpp
/// \brief Dependency-free vector index value types.

#include "../domain/Identifiers.hpp"
#include "../domain/Metadata.hpp"
#include "../embedding/Embedding.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Exact metadata equality filter used by vector search queries.
    struct MetadataFilter final {
        std::string key;
        std::string value;
    };

    /// \brief Vector payload stored for one chunk.
    struct VectorRecord final {
        ChunkId chunk_id;
        Embedding embedding;
        Metadata metadata;
    };

    /// \brief Nearest-neighbour vector search query.
    struct VectorSearchQuery final {
        Embedding embedding;
        std::size_t limit = 10;
        std::vector<MetadataFilter> metadata_filters;
    };

    /// \brief Scored vector search hit.
    struct VectorSearchResult final {
        ChunkId chunk_id;
        /// \brief Comparable score where higher is always better.
        /// \note Euclidean backends should convert distance to a higher-is-better
        ///       score, for example by using a negative distance.
        float score = 0.0F;
        Metadata metadata;
    };

    /// \brief Returns true when metadata satisfies all exact filters.
    [[nodiscard]] bool matches_metadata_filters(
        const Metadata& metadata,
        const std::vector<MetadataFilter>& filters
    );

} // namespace agent_memory

#endif
