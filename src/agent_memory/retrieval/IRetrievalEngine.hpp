#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_I_RETRIEVAL_ENGINE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_I_RETRIEVAL_ENGINE_HPP_INCLUDED

/// \file IRetrievalEngine.hpp
/// \brief Facade contract over the retrieval pipeline.
///
/// This contract sits one layer above `IRetriever` and reserves hooks for
/// the future enrichment/analyze/rerank/structured-facts pipeline. The
/// default implementation (`HybridRetrievalEngine`) composes `ILexicalIndex`
/// with `IdentityReranker`, `PassthroughQueryAnalyzer`, and
/// `PassthroughChunkEnricher`. Replace any of those to light up richer
/// behaviour without changing call sites.

#include "../domain/MetadataFilter.hpp"
#include "../lexical/IQueryAnalyzer.hpp"
#include "../lexical/IReranker.hpp"
#include "../lexical/Lexical.hpp"
#include "../memory/MemoryObject.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Input request for an IRetrievalEngine.
    struct RetrievalRequest final {
        /// \brief Raw query text.
        std::string query;
        /// \brief Maximum number of items to return. Zero requests no items.
        std::size_t limit = 10;
        /// \brief Mandatory metadata filters (combined with AND).
        std::vector<MetadataFilter> metadata_filters;
    };

    /// \brief Single entry in an engine response.
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