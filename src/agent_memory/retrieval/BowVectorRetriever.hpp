#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_BOW_VECTOR_RETRIEVER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_BOW_VECTOR_RETRIEVER_HPP_INCLUDED

/// \file BowVectorRetriever.hpp
/// \brief Exact-vector BoW baseline retriever.
///
/// Composition:
///   - `BowEmbedder` fitted on the corpus (so the dictionary is closed
///     before queries are encoded).
///   - `BruteForceTopKIndex` holding one L2-normalized dense vector per
///     corpus item.
///
/// The retriever plugs into `IRetriever` so PR #26's
/// `RetrievalEvalRunner` can drive it end-to-end against PR #27's JSON
/// fixture. Higher cosine similarity ranks first; ties are broken by
/// doc id ascending for byte-deterministic output.

#include <agent_memory/retrieval/BowEmbedder.hpp>
#include <agent_memory/retrieval/BruteForceTopKIndex.hpp>
#include <agent_memory/retrieval/IRetriever.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    /// \brief Deterministic exact-vector BoW retriever.
    ///
    /// Constructs the embedder and the index eagerly so `retrieve` is a
    /// pure read of the in-memory index. No global RNG state is touched;
    /// all determinism comes from input order and the embedder's stable
    /// term dictionary.
    class BowVectorRetriever final : public IRetriever {
    public:
        /// \brief Builds a retriever over the supplied corpus.
        ///
        /// \param corpus_ids Document ids, parallel to `corpus_texts`.
        /// \param corpus_texts Source text per corpus item.
        /// \param seed Reserved for future embedder seeds; ignored today
        ///        because `BowEmbedder` is purely deterministic. Passing
        ///        zero is fine.
        ///
        /// The constructor is not transactional: if a per-corpus
        /// `m_index.add(...)` call throws after `m_embedder.build()` (e.g.
        /// a future stricter validator rejects a dimension), the object
        /// is left in a partially-built state. The caller MUST discard the
        /// object on construction failure — do not catch and retry, do
        /// not call `retrieve(...)` on a partially-built retriever. RAII
        /// guarantees no resource leak.
        BowVectorRetriever(
            std::vector<std::string> corpus_ids,
            std::vector<std::string> corpus_texts,
            std::uint32_t seed
        );

        [[nodiscard]] RetrievalResult retrieve(const RetrievalQuery& query)
            const override;

    private:
        BowEmbedder m_embedder;
        BruteForceTopKIndex m_index;
        std::vector<std::string> m_corpus_ids;
    };

} // namespace agent_memory

#endif