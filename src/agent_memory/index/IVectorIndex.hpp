#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_I_VECTOR_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_I_VECTOR_INDEX_HPP_INCLUDED

/// \file IVectorIndex.hpp
/// \brief Storage and nearest-neighbour contract for chunk embeddings.

#include "VectorIndex.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace agent_memory {

    /// \brief Dependency-free contract implemented by vector index backends.
    class IVectorIndex {
    public:
        virtual ~IVectorIndex();

        /// \brief Similarity metric used by this index.
        [[nodiscard]] virtual SimilarityMetric similarity_metric() const noexcept = 0;

        /// \brief Expected embedding dimension.
        [[nodiscard]] virtual std::size_t dimension() const noexcept = 0;

        /// \brief Number of indexed vector records.
        [[nodiscard]] virtual std::size_t size() const noexcept = 0;

        /// \brief Inserts or replaces a vector record.
        /// \param record Vector payload to index by chunk id.
        /// \pre `record.embedding.dimension() == dimension()` when `dimension() != 0`.
        ///      Backends with dynamic dimensions may adopt the first valid record
        ///      dimension and then enforce it for later records.
        virtual void upsert(VectorRecord record) = 0;

        /// \brief Finds a stored vector record by chunk id.
        /// \param chunk_id Chunk id to look up.
        /// \return Stored record copy when found.
        [[nodiscard]] virtual std::optional<VectorRecord> find(
            const ChunkId& chunk_id
        ) const = 0;

        /// \brief Searches for nearest records.
        /// \param query Query embedding, result limit, and optional metadata filters.
        /// \pre `query.embedding.dimension() == dimension()` when `dimension() != 0`.
        /// \return Scored hits in index-defined deterministic order. Higher scores
        ///         must rank before lower scores.
        [[nodiscard]] virtual std::vector<VectorSearchResult> search(
            const VectorSearchQuery& query
        ) const = 0;

        /// \brief Removes a vector record by chunk id.
        /// \param chunk_id Chunk id to remove.
        /// \return True when a record was removed.
        [[nodiscard]] virtual bool erase(const ChunkId& chunk_id) = 0;

        /// \brief Removes all vector records.
        virtual void clear() = 0;
    };

} // namespace agent_memory

#endif
