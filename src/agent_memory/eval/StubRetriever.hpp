#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_STUB_RETRIEVER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_STUB_RETRIEVER_HPP_INCLUDED

/// \file StubRetriever.hpp
/// \brief Deterministic stub `IRetriever` used by the eval-runner plumbing tests.

#include <agent_memory/retrieval/IRetriever.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    /// \brief Public baseline-name constant for the stub exact-id retriever.
    inline constexpr std::string_view kBaselineNameStub = "stub_exact_id";

    /// \brief Deterministic stub retriever used by the eval plumbing.
    ///
    /// When the query text starts with the `id:` prefix, the corpus item with
    /// the matching id is placed at the top of the result with a high score,
    /// followed by a deterministic random ordering of the remaining corpus.
    /// Otherwise the entire corpus is returned in a deterministic
    /// random-by-seed order.
    /// \note This class is intentionally dependency-free; it does not index
    ///       the corpus or perform any real scoring. It exists only so the
    ///       eval pipeline can be exercised end-to-end without BM25 or vector
    ///       backends.
    class StubRetriever final : public IRetriever {
    public:
        /// \brief Builds a stub retriever over the supplied corpus item ids.
        /// \param corpus_ids All corpus item ids available to answer queries.
        /// \param seed Deterministic PRNG seed for the random-rank path.
        StubRetriever(
            std::vector<std::string> corpus_ids,
            std::uint32_t seed
        );

        /// \brief Retrieves chunks for the supplied query.
        [[nodiscard]] RetrievalResult retrieve(const RetrievalQuery& query)
            const override;

    private:
        std::vector<std::string> m_corpus_ids;
        std::uint32_t m_seed;
    };

} // namespace agent_memory

#endif