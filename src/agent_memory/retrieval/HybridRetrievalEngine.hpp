#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_HYBRID_RETRIEVAL_ENGINE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_HYBRID_RETRIEVAL_ENGINE_HPP_INCLUDED

/// \file HybridRetrievalEngine.hpp
/// \brief Default IRetrievalEngine implementation.
///
/// Composes:
///   - ILexicalIndex (mandatory)
///   - IReranker     (defaults to IdentityReranker)
///   - IQueryAnalyzer (defaults to PassthroughQueryAnalyzer)
///
/// Future retrieval improvements (semantic blending, LLM-based rerank,
/// structured-fact overlay) plug in by swapping the dependencies. The
/// initial behaviour is a pure lexical passthrough: analyse-then-rerank is
/// identity, so the engine is observably equivalent to calling the
/// underlying ILexicalIndex::search().

#include "../lexical/ILexicalIndex.hpp"
#include "../lexical/IQueryAnalyzer.hpp"
#include "../lexical/IReranker.hpp"
#include "IRetrievalEngine.hpp"

#include <memory>

namespace agent_memory {

    /// \brief Hybrid retrieval engine with swappable hooks.
    ///
    /// Lifetime contract:
    ///   The engine does not own the underlying index/analyzer/reranker
    ///   unless the matching `set_*` method is used. Default constructors
    ///   install identity implementations.
    class HybridRetrievalEngine final : public IRetrievalEngine {
    public:
        HybridRetrievalEngine();

        /// \brief Constructs an engine over an externally-owned lexical index.
        /// \param index Non-owning pointer to the index. Must outlive the engine.
        explicit HybridRetrievalEngine(const ILexicalIndex& index);

        /// \brief Replaces the lexical index.
        void set_lexical_index(const ILexicalIndex& index) noexcept;

        /// \brief Replaces the reranker.
        void set_reranker(std::unique_ptr<IReranker> reranker);

        /// \brief Replaces the query analyzer.
        void set_query_analyzer(std::unique_ptr<IQueryAnalyzer> analyzer);

        [[nodiscard]] RetrievalResponse retrieve(
            const RetrievalRequest& request
        ) const override;

    private:
        const ILexicalIndex* m_index = nullptr;
        std::unique_ptr<IReranker> m_reranker;
        std::unique_ptr<IQueryAnalyzer> m_analyzer;
    };

} // namespace agent_memory

#endif