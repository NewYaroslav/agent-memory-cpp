#include "Lexical.hpp"

#include <cmath>

namespace agent_memory {

    TokenId::TokenId(const std::uint64_t value)
        : m_value(value) {}

    std::uint64_t TokenId::value() const noexcept {
        return m_value;
    }

    bool TokenId::empty() const noexcept {
        return m_value == 0;
    }

    bool LexicalDocumentRecord::empty() const noexcept {
        return tokens.empty();
    }

    bool LexicalSearchQuery::empty() const noexcept {
        return terms.empty();
    }

    bool operator==(const TokenId lhs, const TokenId rhs) noexcept {
        return lhs.value() == rhs.value();
    }

    bool operator!=(const TokenId lhs, const TokenId rhs) noexcept {
        return !(lhs == rhs);
    }

    bool operator<(const TokenId lhs, const TokenId rhs) noexcept {
        return lhs.value() < rhs.value();
    }

    bool is_valid(const Bm25Options& options) noexcept {
        return std::isfinite(options.k1)
            && options.k1 > 0.0F
            && std::isfinite(options.b)
            && options.b >= 0.0F
            && options.b <= 1.0F;
    }

} // namespace agent_memory
