#pragma once
#ifndef AGENT_MEMORY_HEADER_MEMORY_MEMORY_OBJECT_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_MEMORY_MEMORY_OBJECT_HPP_INCLUDED

/// \file MemoryObject.hpp
/// \brief Forward-looking memory hierarchy value types.
///
/// The retrieval layer may surface anything from a raw document section up
/// to an aggregated MemoryCard. This header reserves the value types so
/// future retrieval stages can populate them without churn.

#include "../domain/Metadata.hpp"
#include "../domain/Resource.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Stable identifier for a memory object in the hierarchy.
    class MemoryObjectId final {
    public:
        MemoryObjectId() = default;
        explicit MemoryObjectId(std::string value);

        /// \brief Returns identifier text.
        [[nodiscard]] const std::string& value() const noexcept;

        /// \brief Checks whether the identifier has no value.
        [[nodiscard]] bool empty() const noexcept;

    private:
        std::string m_value;
    };

    /// \brief Discriminator for the kind of memory object referenced.
    enum class ObjectType {
        /// \brief A whole ingested document.
        Document,
        /// \brief A logical section inside a document (chapter, table, etc.).
        Section,
        /// \brief A time-bound episode (e.g. one conversation turn).
        Episode,
        /// \brief A pre-aggregated memory card summarising many chunks.
        MemoryCard,
        /// \brief An individual indexed chunk.
        Chunk
    };

    /// \brief Memory object reference surfaced to retrieval callers.
    ///
    /// A `MemoryObject` is a thin discriminated handle: the value types it
    /// can carry (chunk, section summary, episode card) will be added by
    /// later stages. For now this is a structured placeholder that lets the
    /// retrieval facade talk about "things you can hand back to a caller"
    /// without committing to a payload shape.
    struct MemoryObject final {
        MemoryObjectId id;
        ObjectType type = ObjectType::Chunk;
        ResourceId resource_id;
        /// \brief Section identifier (0 when the object is not section-scoped).
        std::uint64_t section_id = 0;
        /// \brief Optional short summary/title.
        std::string title;
        /// \brief Optional longer text body (may be empty).
        std::string body;
        Metadata metadata;
        /// \brief Optional enrichment level reported by the enricher.
        std::uint32_t enrichment_level = 0;
        /// \brief Retrieval tier (0 = direct chunk, 1 = section, 2 = summary).
        std::uint8_t result_tier = 0;
    };

    [[nodiscard]] bool operator==(const MemoryObjectId& lhs, const MemoryObjectId& rhs) noexcept;
    [[nodiscard]] bool operator!=(const MemoryObjectId& lhs, const MemoryObjectId& rhs) noexcept;
    [[nodiscard]] bool operator<(const MemoryObjectId& lhs, const MemoryObjectId& rhs) noexcept;

    /// \brief Stable lowercase name for the object type.
    [[nodiscard]] std::string_view to_string(ObjectType type) noexcept;

    /// \brief Returns true when a memory object carries enough data to be surfaced.
    [[nodiscard]] bool is_valid(const MemoryObject& object) noexcept;

} // namespace agent_memory

#endif