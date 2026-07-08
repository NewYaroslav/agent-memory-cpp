#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_RERANKER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_RERANKER_HPP_INCLUDED

/// \file IReranker.hpp
/// \brief Dependency-free contract for result reranking.
///
/// Rerankers consume an ordered candidate set and emit a reordered (or
/// score-adjusted) set. The identity reranker preserves the input order so
/// pipeline stages that don't enable reranking see no behavioral change.

#include "Lexical.hpp"

#include <vector>

namespace agent_memory {

    /// \brief Contract for reranking backends.
    ///
    /// Thread-safety:
    ///   Implementations are not required to be thread-safe.
    ///
    /// Exception contract:
    ///   rerank() may throw std::bad_alloc or transport exceptions from a
    ///   backend (e.g. cross-encoder model, LLM HTTP client).
    class IReranker {
    public:
        virtual ~IReranker();

        /// \brief Reranks a candidate set for a query.
        /// \param query Original query text.
        /// \param candidates Input candidates ordered by upstream score.
        /// \return Reordered (or score-adjusted) candidates.
        /// \note Must return at most `candidates.size()` items. May return
        ///       fewer (filtering) or none (rejection) at the caller's risk.
        [[nodiscard]] virtual std::vector<LexicalSearchResult> rerank(
            const std::string& query,
            std::vector<LexicalSearchResult> candidates
        ) const = 0;
    };

    /// \brief Identity reranker -- preserves input order verbatim.
    class IdentityReranker final : public IReranker {
    public:
        [[nodiscard]] std::vector<LexicalSearchResult> rerank(
            const std::string& query,
            std::vector<LexicalSearchResult> candidates
        ) const override;
    };

} // namespace agent_memory

#endif