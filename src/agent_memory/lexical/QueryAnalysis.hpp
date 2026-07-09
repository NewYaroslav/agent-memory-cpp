#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_QUERY_ANALYSIS_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_QUERY_ANALYSIS_HPP_INCLUDED

/// \file QueryAnalysis.hpp
/// \brief Forward-looking query intent classification and rewrite scaffold.
///
/// This header introduces the vocabulary that future retrieval improvements
/// (query rewrite, structured facts, diagnostic introspection) will plug
/// into. It does not implement any real LLM/embedding logic -- callers
/// receive passthrough values from identity defaults and may later swap in
/// smarter implementations.

#include "../domain/MetadataFilter.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief High-level intent classifier for an incoming query.
    ///
    /// The classifier is forward-looking: future retrieval stages may inspect
    /// this enum to choose between BM25 retrieval, structured lookups,
    /// aggregation passes, or diagnostic introspection. The default intent
    /// is the most general one (SemanticLookup).
    enum class QueryType {
        /// \brief Open-ended semantic lookup over indexed chunks.
        SemanticLookup,
        /// \brief Verbatim fact lookup expecting a precise named value.
        ExactFactLookup,
        /// \brief Procedural / how-to lookup expecting step-by-step guidance.
        HowToLookup,
        /// \brief Map-reduce style aggregation over many facts.
        Aggregation,
        /// \brief Side-by-side comparison between two or more entities.
        Comparison,
        /// \brief Fielded / structured query (filters, joins, range probes).
        StructuredQuery,
        /// \brief Query with negative constraint filtering ("not X").
        NegativeFilterQuery,
        /// \brief Self-diagnostic query about the memory subsystem itself.
        DiagnosticQuery
    };

    /// \brief Strategy a retrieval engine may select for a query.
    ///
    /// This is the engine's view of "how to answer" and may diverge from the
    /// raw QueryType (e.g. an ExactFactLookup can be served by HybridSearch
    /// when no structured backend is available).
    enum class RetrievalMode {
        /// \brief Combined lexical + semantic hybrid scoring.
        HybridSearch,
        /// \brief Chunk summarization + chunk retrieval (coarse-to-fine).
        HierarchicalSearch,
        /// \brief Fielded / structured lookup over facts or records.
        StructuredSearch,
        /// \brief LLM-based relevance scan over a candidate set.
        LlmScan,
        /// \brief Map-reduce aggregation over many results.
        Aggregation,
        /// \brief Thread/message reconstruction in chat memory.
        ChatThreadSearch
    };

    /// \brief Planned sub-query plan selected by an analyzer.
    enum class AnalysisPlan {
        /// \brief Nothing to plan -- use the query as-is.
        None,
        /// \brief Execute a single retrieval pass.
        SinglePass,
        /// \brief Execute retrieval then a rerank pass.
        RetrieveAndRerank,
        /// \brief Execute retrieval, rerank, then structured aggregation.
        RetrieveRerankAndAggregate
    };

    /// \brief Analysis of a query, produced by IQueryAnalyzer.
    ///
    /// Every field has a safe default so identity implementations can
    /// populate it cheaply and so downstream stages can rely on stable
    /// invariants (original == rewritten, empty keyword/entity lists).
    struct QueryAnalysis final {
        /// \brief Raw user query exactly as supplied.
        std::string original;
        /// \brief Suggested rewritten query (empty when unchanged).
        std::string rewritten;
        /// \brief Salient terms extracted from the query.
        std::vector<std::string> keywords;
        /// \brief Named entities recognized in the query.
        std::vector<std::string> entities;
        /// \brief Alternative phrasings/aliases for terms.
        std::vector<std::string> aliases;
        /// \brief Terms that must be present (positive reinforcement).
        std::vector<std::string> include_terms;
        /// \brief Terms that must NOT be present (negative filter).
        std::vector<std::string> exclude_terms;
        /// \brief Mandatory metadata filters derived from the query.
        std::vector<MetadataFilter> required_filters;
        /// \brief Inferred query intent.
        QueryType type = QueryType::SemanticLookup;
        /// \brief Suggested retrieval strategy.
        RetrievalMode mode = RetrievalMode::HybridSearch;
        /// \brief Selected sub-query plan.
        AnalysisPlan plan = AnalysisPlan::SinglePass;
    };

    /// \brief Returns true when the analysis carries no rewrite or signals.
    inline bool is_identity(const QueryAnalysis& analysis) noexcept {
        return analysis.rewritten.empty()
            && analysis.keywords.empty()
            && analysis.entities.empty()
            && analysis.aliases.empty()
            && analysis.include_terms.empty()
            && analysis.exclude_terms.empty()
            && analysis.required_filters.empty();
    }

} // namespace agent_memory

#endif
