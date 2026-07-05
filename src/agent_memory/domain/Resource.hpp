#pragma once
#ifndef AGENT_MEMORY_HEADER_DOMAIN_RESOURCE_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_DOMAIN_RESOURCE_HPP_INCLUDED

/// \file Resource.hpp
/// \brief Resource revision and derived-record manifest value types.

#include "Identifiers.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    /// \brief Current revision identity for an original source resource.
    struct ResourceRevision final {
        ResourceId resource_id;
        std::uint64_t generation = 0;
        std::uint64_t content_hash = 0;
        std::uint64_t pipeline_config_hash = 0;
    };

    /// \brief Kind of derived record created from a resource revision.
    enum class DerivedRecordKind {
        Document,
        Chunk,
        Embedding,
        VectorRecord,
        BinaryBucketPosting,
        LexicalPosting,
        GraphRecord,
        Custom
    };

    /// \brief Reference to one derived record owned by a resource revision.
    /// \note Chunk, embedding, and vector records use `chunk_id`.
    /// \note Document, posting, graph, and custom records use `key`.
    struct DerivedRecordRef final {
        DerivedRecordKind kind = DerivedRecordKind::Chunk;
        ChunkId chunk_id;
        std::string key;
        std::uint32_t ordinal = 0;
    };

    /// \brief Manifest of records derived from one resource revision.
    struct ResourceManifest final {
        ResourceRevision revision;
        std::vector<DerivedRecordRef> records;
    };

    /// \brief Returns stable lowercase derived-record kind name.
    [[nodiscard]] std::string_view to_string(DerivedRecordKind kind) noexcept;

    /// \brief Parses a derived-record kind from a lowercase or mixed-case name.
    /// \return True when parsing succeeds.
    bool parse_derived_record_kind(std::string_view text, DerivedRecordKind& kind);

    /// \brief Returns true when a derived-record kind uses `chunk_id`.
    [[nodiscard]] bool derived_record_kind_uses_chunk_id(
        DerivedRecordKind kind
    ) noexcept;

    /// \brief Returns true when a derived-record kind uses `key`.
    [[nodiscard]] bool derived_record_kind_uses_key(
        DerivedRecordKind kind
    ) noexcept;

    /// \brief Returns true when the required reference field is present.
    [[nodiscard]] bool has_required_reference(const DerivedRecordRef& ref) noexcept;

    /// \brief Checks whether revision hashes match source and pipeline inputs.
    [[nodiscard]] bool matches_revision_hashes(
        const ResourceRevision& revision,
        std::uint64_t content_hash,
        std::uint64_t pipeline_config_hash
    ) noexcept;

} // namespace agent_memory

#endif
