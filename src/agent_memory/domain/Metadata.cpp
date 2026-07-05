#include "Metadata.hpp"

#include <utility>

namespace agent_memory {

    void Metadata::set(std::string key, std::string value) {
        m_values[std::move(key)] = std::move(value);
    }

    bool Metadata::erase(const std::string& key) {
        return m_values.erase(key) > 0;
    }

    bool Metadata::contains(const std::string& key) const {
        return m_values.find(key) != m_values.end();
    }

    std::optional<std::string> Metadata::get(const std::string& key) const {
        const auto it = m_values.find(key);
        if(it == m_values.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool Metadata::empty() const noexcept {
        return m_values.empty();
    }

    std::size_t Metadata::size() const noexcept {
        return m_values.size();
    }

    const Metadata::Values& Metadata::values() const noexcept {
        return m_values;
    }

} // namespace agent_memory
