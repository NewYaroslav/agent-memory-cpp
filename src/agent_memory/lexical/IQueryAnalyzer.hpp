#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_QUERY_ANALYZER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_QUERY_ANALYZER_HPP_INCLUDED

/// \file IQueryAnalyzer.hpp
/// \brief Dependency-free contract for query intent classification and rewrite.
///
/// Implementations may wrap an LLM, a heuristic pipeline, or a deterministic
/// rule set. The default passthrough implementation returns the raw query
/// unchanged so downstream retrieval stages remain functional without an
/// analyzer being configured.

#include "QueryAnalysis.hpp"

#include <string>

namespace agent_memory {

    /// \brief Contract for query analysis.
    ///
    /// Thread-safety:
    ///   Implementations are not required to be thread-safe.
    ///
    /// Exception contract:
    ///   analyze() may throw std::bad_alloc or transport exceptions from a
    ///   backend (e.g. LLM HTTP client). It must not throw std::invalid_argument
    ///   for empty queries -- empty queries produce a defaulted analysis.
    class IQueryAnalyzer {
    public:
        virtual ~IQueryAnalyzer();

        /// \brief Produces an analysis for an incoming query.
        /// \param query Raw user query.
        /// \return Analysis populated with at least `original == query`.
        [[nodiscard]] virtual QueryAnalysis analyze(const std::string& query) const = 0;
    };

    /// \brief Identity analyzer -- returns a defaulted analysis preserving
    ///        only `original` from the input.
    ///
    /// This is the safe default. Install a smarter implementation to enable
    /// rewrite, keyword extraction, or intent classification.
    class PassthroughQueryAnalyzer final : public IQueryAnalyzer {
    public:
        [[nodiscard]] QueryAnalysis analyze(const std::string& query) const override;
    };

} // namespace agent_memory

#endif