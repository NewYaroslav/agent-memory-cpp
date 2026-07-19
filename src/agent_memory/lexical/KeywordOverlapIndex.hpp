#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_KEYWORD_OVERLAP_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_KEYWORD_OVERLAP_INDEX_HPP_INCLUDED

/// \file KeywordOverlapIndex.hpp
/// \brief In-memory deterministic keyword-overlap lexical index.

#include "ILexicalIndex.hpp"

#include <map>
#include <set>

namespace agent_memory {

    /// \brief Deterministic in-memory term-set overlap lexical index.
    ///
    /// The score is the number of unique query terms that occur at least once
    /// in a chunk:
    ///
    ///   score(q, d) = |unique_terms(q) intersect unique_terms(d)|
    ///
    /// This is intentionally not BM25: it has no IDF, term-frequency
    /// saturation, or document-length normalization. It is useful as a tiny-KB
    /// baseline and as a dependency-free deterministic oracle for unit tests.
    /// LexicalSearchQuery::bm25 is ignored by this implementation.
    class KeywordOverlapIndex final : public ILexicalIndex {
    public:
        [[nodiscard]] std::size_t size() const noexcept override;

        void upsert(LexicalDocumentRecord record) override;

        [[nodiscard]] std::optional<LexicalDocumentStats> find_stats(
            const ChunkId& chunk_id
        ) const override;

        /// \brief Searches all indexed chunks by unique term overlap.
        [[nodiscard]] std::vector<LexicalSearchResult> search(
            const LexicalSearchQuery& query
        ) const override;

        [[nodiscard]] bool erase(const ChunkId& chunk_id) override;

        [[nodiscard]] std::size_t erase_resource(const ResourceId& resource_id) override;

        void clear() override;

    private:
        struct StoredRecord final {
            LexicalDocumentRecord record;
            std::set<std::string> unique_terms;
        };

        [[nodiscard]] static float score_record(
            const StoredRecord& record,
            const std::set<std::string>& unique_query_terms
        );

        std::map<ChunkId, StoredRecord> m_records;
        std::map<ResourceId, std::set<ChunkId>> m_chunks_by_resource;
    };

} // namespace agent_memory

#endif
