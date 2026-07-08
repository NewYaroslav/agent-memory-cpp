#include "Fact.hpp"

namespace agent_memory {

    FactId::FactId(std::string value)
        : m_value(std::move(value)) {}

    const std::string& FactId::value() const noexcept {
        return m_value;
    }

    bool FactId::empty() const noexcept {
        return m_value.empty();
    }

    bool operator==(const FactId& lhs, const FactId& rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const FactId& lhs, const FactId& rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const FactId& lhs, const FactId& rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    bool is_valid(const Fact& fact) noexcept {
        return !fact.id.empty()
            && !fact.subject.empty()
            && !fact.value.empty();
    }

} // namespace agent_memory