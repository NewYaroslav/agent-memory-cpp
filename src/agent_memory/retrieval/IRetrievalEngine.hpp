#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_I_RETRIEVAL_ENGINE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_I_RETRIEVAL_ENGINE_HPP_INCLUDED

/// \file IRetrievalEngine.hpp
/// \brief Facade contract over the retrieval pipeline.
///
/// This contract sits one layer above `IRetriever` and reserves hooks for
/// the future enrichment/analyze/rerank/structured-facts pipeline. The
/// default implementation (`HybridRetrievalEngine`) composes `ILexicalIndex`
/// with `IdentityReranker` and `PassthroughQueryAnalyzer`. Replace any of
/// those to light up richer behaviour without changing call sites.

#include <agent_memory/domain.hpp>
#include <agent_memory/lexical/IQueryAnalyzer.hpp>
#include <agent_memory/lexical/IReranker.hpp>
#include <agent_memory/lexical/Lexical.hpp>
#include <agent_memory/memory.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Input request for an IRetrievalEngine.
    struct RetrievalRequest final {
        /// \brief Raw user query text.
        ///
        /// The default `HybridRetrievalEngine` runs the query through the
        /// installed `IQueryAnalyzer` before hitting the lexical index.
        /// Without a custom analyzer the engine installs
        /// `PassthroughQueryAnalyzer`, which performs a minimal
        /// whitespace split: single-term queries are kept as one keyword
        /// and multi-term queries are split into one keyword per
        /// whitespace-separated token. If the analyzer produces no
        /// keywords, the engine falls back to the raw `query` string as a
        /// single term.
        ///
        /// For real tokenization, stemming, query expansion, or
        /// synonym/structured-fact overlay, install a custom
        /// `IQueryAnalyzer` (e.g. an `ITokenizer`-driven pipeline) via
        /// `HybridRetrievalEngine::set_query_analyzer`. Without such a
        /// custom analyzer the engine does no language-aware processing.
        std::string query;
        /// \brief Maximum number of items to return. Zero requests no items.
        std::size_t limit = 10;
        /// \brief Mandatory metadata filters (combined with AND).
        std::vector<MetadataFilter> metadata_filters;
    };

    /// \brief Single entry in an engine response.
    ///
    /// Section, resource, and result-tier identifiers live on `lexical`
    /// only; `MemoryObject` carries the object id (when the result tier
    /// is chunk), metadata, and enrichment level. Callers should read
    /// structural fields from `item.lexical`.
    struct RetrievalResponseItem final {
        LexicalSearchResult lexical;
        MemoryObject object;
    };

    /// \brief Ordered engine response.
    struct RetrievalResponse final {
        std::vector<RetrievalResponseItem> items;

        /// \brief Returns true when no items were produced.
        [[nodiscard]] bool empty() const noexcept;

        /// \brief Returns the number of items.
        [[nodiscard]] std::size_t size() const noexcept;
    };

    /// \brief Facade contract for advanced retrieval pipelines.
    ///
    /// Implementations are not required to be thread-safe. Calls may throw
    /// std::bad_alloc or backend-specific transport exceptions.
    ///
    /// Thread-safety:
    ///   set_* hook installations (e.g. set_lexical_index, set_reranker,
    ///   set_query_analyzer) MUST NOT be invoked concurrently with
    ///   retrieve(); doing so is a data race. Outside of those
    ///   configuration points concurrent reads from retrieve() are
    ///   safe only when documented by the concrete engine.
    class IRetrievalEngine {
    public:
        virtual ~IRetrievalEngine();

        /// \brief Executes the retrieval pipeline for a request.
        /// \note `request.limit == 0` requests an empty response.
        [[nodiscard]] virtual RetrievalResponse retrieve(
            const RetrievalRequest& request
        ) const = 0;
    };

} // namespace agent_memory

#endif
