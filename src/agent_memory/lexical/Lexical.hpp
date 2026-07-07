#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_LEXICAL_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_LEXICAL_HPP_INCLUDED

/// \file Lexical.hpp
/// \brief Dependency-free lexical search value types.

#include "../domain/Metadata.hpp"
#include "../domain/MetadataFilter.hpp"
#include "../domain/Resource.hpp"
#include "Tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Stable numeric identifier for a normalized lexical token.
    class TokenId final {
    public:
        TokenId() = default;
        explicit TokenId(std::uint64_t value);

        /// \brief Returns the numeric token id.
        [[nodiscard]] std::uint64_t value() const noexcept;

        /// \brief Checks whether this id is empty.
        [[nodiscard]] bool empty() const noexcept;

    private:
        std::uint64_t m_value = 0;
    };

    /// \brief BM25 scorer options.
    struct Bm25Options final {
        float k1 = 1.5F;
        float b = 0.75F;
    };

    /// \brief Token occurrence list for one chunk.
    struct LexicalPosting final {
        TokenId token_id;
        ChunkId chunk_id;
        ResourceRevision revision;
        std::uint32_t term_frequency = 0;
        std::vector<std::uint32_t> positions;
    };

    /// \brief Tokenized document chunk ready for lexical indexing.
    struct LexicalDocumentRecord final {
        ResourceRevision revision;
        ChunkId chunk_id;
        std::vector<Token> tokens;
        Metadata metadata;

        /// \brief Returns true when no tokens are present.
        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Per-chunk lexical statistics.
    struct LexicalDocumentStats final {
        ChunkId chunk_id;
        ResourceRevision revision;
        std::size_t token_count = 0;
        std::size_t unique_token_count = 0;
        Metadata metadata;
    };

    /// \brief Lexical search query over normalized terms.
    struct LexicalSearchQuery final {
        /// \brief Normalized query terms. Text parsing/tokenization happens outside indexes.
        std::vector<std::string> terms;
        /// \brief Maximum number of chunks to return. Zero requests no chunks.
        std::size_t limit = 10;
        std::vector<MetadataFilter> metadata_filters;
        Bm25Options bm25;

        /// \brief Returns true when there are no normalized query terms.
        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Scored lexical search hit.
    struct LexicalSearchResult final {
        ChunkId chunk_id;
        /// \brief Comparable lexical score where higher is always better.
        float score = 0.0F;
        Metadata metadata;
    };

    [[nodiscard]] bool operator==(TokenId lhs, TokenId rhs) noexcept;
    [[nodiscard]] bool operator!=(TokenId lhs, TokenId rhs) noexcept;
    [[nodiscard]] bool operator<(TokenId lhs, TokenId rhs) noexcept;

    /// \brief Returns true when BM25 options are within the supported range.
    [[nodiscard]] bool is_valid(const Bm25Options& options) noexcept;

} // namespace agent_memory

#endif
