#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_EXACT_LEXICAL_RETRIEVER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_EXACT_LEXICAL_RETRIEVER_HPP_INCLUDED

/// \file ExactLexicalRetriever.hpp
/// \brief Deterministic exact BM25 lexical retriever.
///
/// Composition:
///   - `ITokenizer` (defaulted to a process-wide `StandardTokenizer`) for
///     both corpus ingestion and query normalization.
///   - `agent_memory::lexical::ExactLexicalIndex` holding one BM25-scored
///     chunk per corpus item.
///   - `k_neighbours_max` as an upper safety cap applied to `query.limit`
///     per call.
///
/// The retriever plugs into `IRetriever` so PR #26's
/// `RetrievalEvalRunner` can drive it end-to-end against the same JSON
/// fixture used by `BowVectorRetriever`. Higher BM25 score ranks first;
/// ties are broken deterministically by chunk id ascending (delegated to
/// `ExactLexicalIndex`).

#include <agent_memory/lexical/ExactLexicalIndex.hpp>
#include <agent_memory/lexical/ITokenizer.hpp>
#include <agent_memory/retrieval/IRetriever.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Deterministic exact BM25 retriever.
    ///
    /// Constructs the lexical index eagerly so `retrieve` is a pure read
    /// of the in-memory index. No global RNG state is touched; all
    /// determinism comes from input order and `ExactLexicalIndex`'s
    /// stable BM25 ranking.
    class ExactLexicalRetriever final : public IRetriever {
    public:
        /// \brief Builds a retriever over the supplied corpus.
        ///
        /// \param corpus_ids Document ids, parallel to `corpus_texts`.
        /// \param corpus_texts Source text per corpus item.
        /// \param tokenizer Tokenizer used for both corpus ingestion and
        ///        query normalization. Defaults to a process-wide
        ///        `StandardTokenizer` instance. Must outlive the retriever.
        /// \param k_neighbours_max Upper cap applied to `query.limit` per
        ///        call. Defaults to 1024. Must be greater than zero; pass
        ///        `std::numeric_limits<std::size_t>::max()` to disable.
        /// \throws std::invalid_argument if corpus inputs are inconsistent,
        ///         any text tokenizes to empty, or `k_neighbours_max == 0`.
        /// \throws std::runtime_error if the lexical index rejects a record.
        /// \note Standard C++ exception safety: a failed constructor leaves
        ///       no ExactLexicalRetriever object behind.
        ExactLexicalRetriever(
            std::vector<std::string> corpus_ids,
            std::vector<std::string> corpus_texts,
            ITokenizer& tokenizer = default_tokenizer(),
            std::size_t k_neighbours_max = 1024
        );

        [[nodiscard]] RetrievalResult retrieve(const RetrievalQuery& query)
            const override;

    private:
        /// \brief Returns a process-wide default `StandardTokenizer`.
        [[nodiscard]] static ITokenizer& default_tokenizer();

        ExactLexicalIndex m_index;
        std::vector<std::string> m_corpus_ids;
        ITokenizer* m_tokenizer;
        /// \brief Upper safety cap on per-query result size. Must be > 0.
        /// \note Use `std::numeric_limits<std::size_t>::max()` to disable
        ///       the cap entirely.
        std::size_t m_k_neighbours_max;
    };

} // namespace agent_memory

#endif