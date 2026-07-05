#pragma once
#ifndef AGENT_MEMORY_HEADER_DOMAIN_SOURCE_KIND_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_DOMAIN_SOURCE_KIND_HPP_INCLUDED

/// \file SourceKind.hpp
/// \brief Source document kind classification.

#include <string_view>

namespace agent_memory {

    /// \brief Broad source category used before storage-specific indexing.
    enum class SourceKind {
        Unknown,
        Text,
        Markdown,
        Chat,
        Code,
        Structured,
        Event,
        Custom
    };

    /// \brief Returns stable lowercase source kind name.
    [[nodiscard]] std::string_view to_string(SourceKind kind) noexcept;

    /// \brief Parses a source kind from a lowercase or mixed-case name.
    /// \return True when parsing succeeds.
    bool parse_source_kind(std::string_view text, SourceKind& kind);

} // namespace agent_memory

#endif
