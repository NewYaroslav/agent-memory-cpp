#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_LEXICAL_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_LEXICAL_INDEX_HPP_INCLUDED

/// \file ILexicalIndex.hpp
/// \brief Dependency-free lexical index contract.

#include "Lexical.hpp"

#include <optional>

namespace agent_memory {

    /// \brief Contract implemented by lexical search indexes.
    class ILexicalIndex {
    public:
        virtual ~ILexicalIndex();

        /// \brief Number of indexed chunks.
        [[nodiscard]] virtual std::size_t size() const noexcept = 0;

        /// \brief Inserts or replaces one tokenized chunk.
        /// \pre `is_valid(record)` must be true.
        virtual void upsert(LexicalDocumentRecord record) = 0;

        /// \brief Finds stored lexical stats by chunk id.
        [[nodiscard]] virtual std::optional<LexicalDocumentStats> find_stats(
            const ChunkId& chunk_id
        ) const = 0;

        /// \brief Searches indexed chunks with normalized query terms.
        /// \pre `is_valid(query)` must be true unless `query.limit == 0`.
        /// \return Scored hits ordered with higher scores first.
        [[nodiscard]] virtual std::vector<LexicalSearchResult> search(
            const LexicalSearchQuery& query
        ) const = 0;

        /// \brief Removes one indexed chunk.
        [[nodiscard]] virtual bool erase(const ChunkId& chunk_id) = 0;

        /// \brief Removes all chunks owned by one resource.
        /// \return Number of removed chunks.
        [[nodiscard]] virtual std::size_t erase_resource(const ResourceId& resource_id) = 0;

        /// \brief Removes all indexed chunks and token statistics.
        virtual void clear() = 0;
    };

} // namespace agent_memory

#endif
