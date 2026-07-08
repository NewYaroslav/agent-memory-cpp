#include "Tokenizer.hpp"

#include <array>
#include <string_view>

namespace agent_memory {
namespace {

    struct TokenKindName final {
        TokenKind kind = TokenKind::Word;
        std::string_view name;
    };

    constexpr std::array<TokenKindName, 6> TOKEN_KIND_NAMES{{
        {TokenKind::Word, "word"},
        {TokenKind::Number, "number"},
        {TokenKind::Identifier, "identifier"},
        {TokenKind::Path, "path"},
        {TokenKind::Symbol, "symbol"},
        {TokenKind::Custom, "custom"}
    }};

    [[nodiscard]] constexpr char ascii_to_lower(char c) noexcept {
        const auto uc = static_cast<unsigned char>(c);
        return (uc >= 'A' && uc <= 'Z')
            ? static_cast<char>(uc + static_cast<unsigned char>('a' - 'A'))
            : c;
    }

} // namespace

    bool Token::empty() const noexcept {
        return text.empty();
    }

    bool TokenizationResult::empty() const noexcept {
        return tokens.empty();
    }

    std::size_t TokenizationResult::size() const noexcept {
        return tokens.size();
    }

    std::string_view to_string(const TokenKind kind) noexcept {
        for(const auto& item : TOKEN_KIND_NAMES) {
            if(item.kind == kind) {
                return item.name;
            }
        }
        return "custom";
    }

    bool parse_token_kind(std::string_view text, TokenKind& out_kind) noexcept {
        for(const auto& item : TOKEN_KIND_NAMES) {
            if(text.size() != item.name.size()) {
                continue;
            }
            bool match = true;
            for(std::size_t i = 0; i < text.size(); ++i) {
                if(ascii_to_lower(text[i]) != item.name[i]) {
                    match = false;
                    break;
                }
            }
            if(match) {
                out_kind = item.kind;
                return true;
            }
        }
        return false;
    }

} // namespace agent_memory
