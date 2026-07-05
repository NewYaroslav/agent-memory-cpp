#pragma once
#ifndef AGENT_MEMORY_HEADER_DOMAIN_DOCUMENT_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_DOMAIN_DOCUMENT_HPP_INCLUDED

/// \file Document.hpp
/// \brief Source document and chunk domain records.

#include "Identifiers.hpp"
#include "Metadata.hpp"
#include "SourceKind.hpp"

#include <cstddef>
#include <string>

namespace agent_memory {

    /// \brief Byte range in the original source text.
    struct TextRange final {
        std::size_t offset = 0;
        std::size_t length = 0;
    };

    /// \brief Source document before chunking and indexing.
    struct Document final {
        DocumentId id;
        SourceKind kind = SourceKind::Unknown;
        std::string source_uri;
        std::string text;
        Metadata metadata;
    };

    /// \brief Chunk derived from a source document.
    struct DocumentChunk final {
        ChunkId id;
        DocumentId document_id;
        TextRange source_range;
        std::string text;
        Metadata metadata;
    };

    /// \brief Checks whether a source range has zero length.
    [[nodiscard]] bool is_empty(const TextRange& range) noexcept;

    /// \brief Checks whether a document has non-empty text.
    [[nodiscard]] bool has_content(const Document& document) noexcept;

    /// \brief Checks whether a chunk has non-empty text.
    [[nodiscard]] bool has_content(const DocumentChunk& chunk) noexcept;

} // namespace agent_memory

#endif
