#include "Tokenizer.hpp"

#include <algorithm>

namespace agent_memory {
namespace {

    [[nodiscard]] std::string ascii_lowercase(std::string text) {
        std::transform(
            text.begin(),
            text.end(),
            text.begin(),
            [](const unsigned char value) -> char {
                if(value >= 'A' && value <= 'Z') {
                    return static_cast<char>(value - 'A' + 'a');
                }
                return static_cast<char>(value);
            }
        );
        return text;
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

    std::string to_string(const TokenKind kind) {
        switch(kind) {
        case TokenKind::Word:
            return "word";
        case TokenKind::Number:
            return "number";
        case TokenKind::Identifier:
            return "identifier";
        case TokenKind::Path:
            return "path";
        case TokenKind::Symbol:
            return "symbol";
        case TokenKind::Custom:
            return "custom";
        }

        return "custom";
    }

    bool parse_token_kind(const std::string& text, TokenKind& out_kind) {
        const auto normalized = ascii_lowercase(text);

        if(normalized == "word") {
            out_kind = TokenKind::Word;
            return true;
        }

        if(normalized == "number") {
            out_kind = TokenKind::Number;
            return true;
        }

        if(normalized == "identifier") {
            out_kind = TokenKind::Identifier;
            return true;
        }

        if(normalized == "path") {
            out_kind = TokenKind::Path;
            return true;
        }

        if(normalized == "symbol") {
            out_kind = TokenKind::Symbol;
            return true;
        }

        if(normalized == "custom") {
            out_kind = TokenKind::Custom;
            return true;
        }

        return false;
    }

} // namespace agent_memory
