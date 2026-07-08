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

    bool is_valid(const LexicalPosting& posting) noexcept {
        return !posting.token_id.empty()
            && posting.term_frequency == posting.positions.size();
    }

    bool is_valid(const LexicalDocumentRecord& record) noexcept {
        if(record.revision.resource_id.empty() || record.chunk_id.empty() || record.tokens.empty()) {
            return false;
        }

        for(const auto& token : record.tokens) {
            if(token.empty()) {
                return false;
            }
        }

        return true;
    }

    bool is_valid(const LexicalSearchQuery& query) noexcept {
        if(query.terms.empty()) {
            return false;
        }

        if(query.bm25.has_value() && !is_valid(*query.bm25)) {
            return false;
        }

        for(const auto& term : query.terms) {
            if(term.empty()) {
                return false;
            }
        }

        return true;
    }

} // namespace agent_memory
