#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_I_BINARY_SIGNATURE_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_I_BINARY_SIGNATURE_INDEX_HPP_INCLUDED

/// \file IBinarySignatureIndex.hpp
/// \brief Storage and nearest-neighbour contract for binary signatures.

#include "BinarySignatureIndex.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace agent_memory {

    /// \brief Dependency-free contract implemented by binary-signature indexes.
    class IBinarySignatureIndex {
    public:
        virtual ~IBinarySignatureIndex();

        /// \brief Expected binary signature width. Zero means not adopted/configured yet.
        [[nodiscard]] virtual std::size_t bit_count() const noexcept = 0;

        /// \brief Number of indexed binary signature records.
        [[nodiscard]] virtual std::size_t size() const noexcept = 0;

        /// \brief Inserts or replaces a binary signature record.
        /// \param record Binary payload to index by chunk id.
        /// \pre `record.signature` and `record.signature_info` must describe
        ///      the same non-empty binary space accepted by the index.
        ///      Backends with dynamic identity may adopt the first valid record
        ///      identity and then enforce it for later records and queries.
        virtual void upsert(BinarySignatureRecord record) = 0;

        /// \brief Finds a stored binary signature record by chunk id.
        /// \param chunk_id Chunk id to look up.
        /// \return Stored record copy when found.
        [[nodiscard]] virtual std::optional<BinarySignatureRecord> find(
            const ChunkId& chunk_id
        ) const = 0;

        /// \brief Searches for nearest binary signatures.
        /// \param query Query signature, result limit, and optional metadata filters.
        /// \pre `query.signature` and `query.signature_info` must describe
        ///      the same binary space accepted by the index, unless
        ///      `query.limit == 0`.
        /// \return Hits ordered by ascending Hamming distance, then chunk id ascending.
        /// \note `query.limit == 0` returns an empty result without validating
        ///       the query signature or identity.
        [[nodiscard]] virtual std::vector<BinarySignatureSearchResult> search(
            const BinarySignatureSearchQuery& query
        ) const = 0;

        /// \brief Removes a binary signature record by chunk id.
        /// \param chunk_id Chunk id to remove.
        /// \return True when a record was removed.
        [[nodiscard]] virtual bool erase(const ChunkId& chunk_id) = 0;

        /// \brief Removes all binary signature records.
        virtual void clear() = 0;
    };

} // namespace agent_memory

#endif
