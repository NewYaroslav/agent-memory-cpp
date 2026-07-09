#include "MemoryObject.hpp"

namespace agent_memory {

    MemoryObjectId::MemoryObjectId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& MemoryObjectId::value() const noexcept {
        return m_value;
    }

    bool MemoryObjectId::empty() const noexcept {
        return m_value.empty();
    }

    bool operator==(const MemoryObjectId& lhs, const MemoryObjectId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const MemoryObjectId& lhs, const MemoryObjectId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const MemoryObjectId& lhs, const MemoryObjectId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    std::string_view to_string(ObjectType type) noexcept {
        switch (type) {
            case ObjectType::Document:    return "document";
            case ObjectType::Section:     return "section";
            case ObjectType::Episode:     return "episode";
            case ObjectType::MemoryCard:  return "memory_card";
            case ObjectType::Chunk:       return "chunk";
        }
        return "chunk";
    }

    bool is_valid(const MemoryObject& object) noexcept {
        if(object.id.empty()) {
            return false;
        }
        switch(object.type) {
            case ObjectType::Document:
                return true;
            case ObjectType::Section:
                return !object.resource_id.empty() && object.section_id != 0;
            case ObjectType::Episode:
            case ObjectType::MemoryCard:
            case ObjectType::Chunk:
                return !object.resource_id.empty();
        }
        return false;
    }

} // namespace agent_memory