#include "StandardTokenizer.hpp"

#include <algorithm>

namespace agent_memory {
namespace {

    [[nodiscard]] bool is_ascii_alpha(const unsigned char value) noexcept {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    }

    [[nodiscard]] bool is_ascii_upper(const unsigned char value) noexcept {
        return value >= 'A' && value <= 'Z';
    }

    [[nodiscard]] bool is_ascii_lower(const unsigned char value) noexcept {
        return value >= 'a' && value <= 'z';
    }

    [[nodiscard]] bool is_ascii_digit(const unsigned char value) noexcept {
        return value >= '0' && value <= '9';
    }

    [[nodiscard]] bool is_ascii_space(const unsigned char value) noexcept {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
            value == '\f' || value == '\v';
    }

    [[nodiscard]] bool is_non_ascii(const unsigned char value) noexcept {
        return value >= 0x80U;
    }

    [[nodiscard]] bool is_token_start(const unsigned char value) noexcept {
        return is_ascii_alpha(value) || is_ascii_digit(value) || is_non_ascii(value);
    }

    [[nodiscard]] bool is_token_body(const unsigned char value) noexcept {
        return is_ascii_alpha(value) || is_ascii_digit(value) || is_non_ascii(value) ||
            value == '_' || value == '-' || value == '.' || value == '/' ||
            value == '\\' || value == ':';
    }

    [[nodiscard]] bool is_identifier_delimiter(const unsigned char value) noexcept {
        return value == '_' || value == '-' || value == '.' || value == ':';
    }

    [[nodiscard]] char ascii_lower(const unsigned char value) noexcept {
        if(value >= 'A' && value <= 'Z') {
            return static_cast<char>(value - 'A' + 'a');
        }
        return static_cast<char>(value);
    }

    [[nodiscard]] std::string normalize_text(
        const std::string_view text,
        const TokenizeOptions& options
    ) {
        std::string normalized{text};
        if(!options.lowercase_ascii) {
            return normalized;
        }

        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](const unsigned char value) -> char {
                return ascii_lower(value);
            }
        );
        return normalized;
    }

    [[nodiscard]] bool is_number_text(const std::string_view text) noexcept {
        bool has_digit = false;
        bool has_dot = false;

        for(const char raw : text) {
            const auto value = static_cast<unsigned char>(raw);
            if(is_ascii_digit(value)) {
                has_digit = true;
                continue;
            }

            if(value == '.' && !has_dot) {
                has_dot = true;
                continue;
            }

            return false;
        }

        return has_digit;
    }

    [[nodiscard]] bool has_path_separator(const std::string_view text) noexcept {
        return text.find('/') != std::string_view::npos ||
            text.find('\\') != std::string_view::npos;
    }

    [[nodiscard]] bool has_identifier_delimiter(const std::string_view text) noexcept {
        for(const char raw : text) {
            if(is_identifier_delimiter(static_cast<unsigned char>(raw))) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool has_camel_boundary(const std::string_view text) noexcept {
        for(std::size_t index = 1; index < text.size(); ++index) {
            const auto previous = static_cast<unsigned char>(text[index - 1]);
            const auto current = static_cast<unsigned char>(text[index]);
            if((is_ascii_lower(previous) || is_ascii_digit(previous)) && is_ascii_upper(current)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] TokenKind classify_token(const std::string_view text) noexcept {
        if(has_path_separator(text)) {
            return TokenKind::Path;
        }

        if(is_number_text(text)) {
            return TokenKind::Number;
        }

        if(has_identifier_delimiter(text) || has_camel_boundary(text)) {
            return TokenKind::Identifier;
        }

        return TokenKind::Word;
    }

    void append_token(
        TokenizationResult& result,
        const std::string_view text,
        const std::size_t offset,
        const TokenKind kind,
        const TokenizeOptions& options
    ) {
        const auto normalized = normalize_text(text, options);
        if(normalized.empty()) {
            return;
        }

        result.tokens.push_back(Token{
            normalized,
            TextRange{offset, text.size()},
            result.tokens.size(),
            kind
        });
    }

    void append_identifier_parts(
        TokenizationResult& result,
        const std::string_view text,
        const std::size_t offset,
        const TokenizeOptions& options
    ) {
        std::size_t part_begin = 0;
        std::size_t emitted_parts = 0;

        const auto flush_part = [&](const std::size_t part_end) {
            if(part_end <= part_begin) {
                return;
            }

            const auto part = text.substr(part_begin, part_end - part_begin);
            append_token(
                result,
                part,
                offset + part_begin,
                is_number_text(part) ? TokenKind::Number : TokenKind::Word,
                options
            );
            ++emitted_parts;
        };

        for(std::size_t index = 0; index < text.size(); ++index) {
            const auto value = static_cast<unsigned char>(text[index]);
            if(is_identifier_delimiter(value)) {
                flush_part(index);
                part_begin = index + 1;
                continue;
            }

            if(index > part_begin) {
                const auto previous = static_cast<unsigned char>(text[index - 1]);
                if(
                    (is_ascii_lower(previous) || is_ascii_digit(previous)) &&
                    is_ascii_upper(value)
                ) {
                    flush_part(index);
                    part_begin = index;
                }
            }
        }

        flush_part(text.size());

        if(emitted_parts == 1 && !result.tokens.empty()) {
            const auto whole = normalize_text(text, options);
            if(result.tokens.back().text == whole) {
                result.tokens.pop_back();
            }
        }
    }

} // namespace

    TokenizationResult StandardTokenizer::tokenize(
        const std::string_view text,
        const TokenizeOptions& options
    ) const {
        TokenizationResult result;

        for(std::size_t index = 0; index < text.size();) {
            const auto value = static_cast<unsigned char>(text[index]);

            if(is_token_start(value)) {
                const auto begin = index;
                ++index;
                while(index < text.size() && is_token_body(static_cast<unsigned char>(text[index]))) {
                    ++index;
                }

                const auto token_text = text.substr(begin, index - begin);
                const auto kind = classify_token(token_text);
                append_token(result, token_text, begin, kind, options);

                if(kind == TokenKind::Identifier && options.emit_identifier_parts) {
                    append_identifier_parts(result, token_text, begin, options);
                }
                continue;
            }

            if(options.emit_symbol_tokens && !is_ascii_space(value) && !is_token_body(value)) {
                append_token(
                    result,
                    text.substr(index, 1),
                    index,
                    TokenKind::Symbol,
                    options
                );
            }

            ++index;
        }

        for(std::size_t position = 0; position < result.tokens.size(); ++position) {
            result.tokens[position].position = position;
        }

        return result;
    }

} // namespace agent_memory
