#include "Identifiers.hpp"

#include <utility>

namespace agent_memory {

    ResourceId::ResourceId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& ResourceId::value() const noexcept {
        return m_value;
    }

    bool ResourceId::empty() const noexcept {
        return m_value.empty();
    }

    DocumentId::DocumentId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& DocumentId::value() const noexcept {
        return m_value;
    }

    bool DocumentId::empty() const noexcept {
        return m_value.empty();
    }

    ChunkId::ChunkId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& ChunkId::value() const noexcept {
        return m_value;
    }

    bool ChunkId::empty() const noexcept {
        return m_value.empty();
    }

    bool operator==(const ResourceId& lhs, const ResourceId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const ResourceId& lhs, const ResourceId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const ResourceId& lhs, const ResourceId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    bool operator==(const DocumentId& lhs, const DocumentId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const DocumentId& lhs, const DocumentId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const DocumentId& lhs, const DocumentId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    bool operator==(const ChunkId& lhs, const ChunkId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const ChunkId& lhs, const ChunkId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const ChunkId& lhs, const ChunkId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

} // namespace agent_memory
