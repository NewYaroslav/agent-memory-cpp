#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_CHUNK_ENRICHER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_CHUNK_ENRICHER_HPP_INCLUDED

/// \file IChunkEnricher.hpp
/// \brief Dependency-free contract for chunk-level contextual enrichment.
///
/// Contextual Retrieval (Anthropic, 2024) demonstrated that prefixing each
/// chunk with a short LLM-generated context string improves recall. This
/// contract reserves the slot in the ingestion pipeline. The passthrough
/// default preserves the original text and reports level 0, so existing
/// indexes continue to work unchanged.

#include <cstdint>
#include <string>

namespace agent_memory {

    /// \brief Result of enriching a single chunk.
    struct ChunkEnrichment final {
        /// \brief Original chunk text exactly as supplied.
        std::string original_text;
        /// \brief Enriched text ready for indexing. For passthrough this
        ///        equals `original_text`.
        std::string enriched_text;
        /// \brief Enrichment level: 0 = passthrough, 1+ = LLM-assisted.
        std::uint32_t level = 0;
    };

    /// \brief Contract for chunk enrichment backends.
    ///
    /// Thread-safety:
    ///   Implementations are not required to be thread-safe.
    ///
    /// Exception contract:
    ///   enrich() may throw std::bad_alloc or transport exceptions from a
    ///   backend (e.g. LLM HTTP client). It must not throw on empty input.
    class IChunkEnricher {
    public:
        virtual ~IChunkEnricher();

        /// \brief Produces an enrichment for a single chunk.
        /// \param chunk_text Original chunk text.
        /// \return Enrichment with at least `original_text == chunk_text`.
        [[nodiscard]] virtual ChunkEnrichment enrich(
            const std::string& chunk_text
        ) const = 0;
    };

    /// \brief Identity enricher -- returns the original text unchanged
    ///        with `level == 0`.
    class PassthroughChunkEnricher final : public IChunkEnricher {
    public:
        [[nodiscard]] ChunkEnrichment enrich(
            const std::string& chunk_text
        ) const override;
    };

} // namespace agent_memory

#endif